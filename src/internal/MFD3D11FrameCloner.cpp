#include "MFD3D11FrameCloner.hpp"
#include "MFD3D11UploadFramePool.hpp"

#include <D3D11Helper/D3D11Gpu/D3D11Copy.hpp>

#include <algorithm>
#include <stdexcept>

namespace MFFrameSource::internal {

using namespace D3D11CoreLib;

void MFD3D11FrameCloner::initialize(std::shared_ptr<D3D11Core> core,
                                    UINT width,
                                    UINT height,
                                    DXGI_FORMAT format,
                                    std::size_t poolSize,
                                    UINT persistentSrvDescriptorCount,
                                    bool waitForGpuCompletionOnClone) {
    (void)persistentSrvDescriptorCount;
    if (!core) throw std::runtime_error("MFD3D11FrameCloner::initialize: null D3D11Core");
    if (width == 0 || height == 0) throw std::runtime_error("MFD3D11FrameCloner::initialize: size is zero");
    if (format == DXGI_FORMAT_UNKNOWN) throw std::runtime_error("MFD3D11FrameCloner::initialize: unknown format");
    if (poolSize == 0) throw std::runtime_error("MFD3D11FrameCloner::initialize: poolSize must be > 0");

    close();
    core_ = std::move(core);
    width_ = width;
    height_ = height;
    format_ = format;
    waitForGpuCompletionOnClone_ = waitForGpuCompletionOnClone;
    state_ = std::make_shared<PoolState>();

    for (std::size_t i = 0; i < poolSize; ++i) {
        state_->slots.push_back(createSlot());
    }
}

std::shared_ptr<MFD3D11CameraFrame::Storage> MFD3D11FrameCloner::createSlot() {
    auto slot = std::make_shared<MFD3D11CameraFrame::Storage>();
    slot->core = core_;
    slot->leaseControl = state_->leaseControl;
    slot->outputTexture = CreateTexture2D(
        *core_,
        width_,
        height_,
        format_,
        D3D11_BIND_SHADER_RESOURCE,
        D3D11_USAGE_DEFAULT);
    slot->width = width_;
    slot->height = height_;
    slot->format = format_;
    createSrv(*slot);
    return slot;
}

void MFD3D11FrameCloner::createSrv(MFD3D11CameraFrame::Storage& slot) {
    slot.outputSrv = CreateTexture2DSrv(*core_, slot.outputTexture, format_);
}

MFD3D11CameraFrame MFD3D11FrameCloner::acquireSlot() {
    if (!state_) throw std::runtime_error("MFD3D11FrameCloner::acquireSlot: not initialized");
    auto control = state_->leaseControl;

    std::shared_ptr<MFD3D11CameraFrame::Storage> selected;
    {
        std::unique_lock<std::mutex> lock(control->mutex);
        control->cv.wait(lock, [&] {
            if (control->closing) return true;
            return std::any_of(state_->slots.begin(), state_->slots.end(), [](const auto& p) {
                return p && !p->leased;
            });
        });
        if (control->closing) throw std::runtime_error("MFD3D11FrameCloner: pool is closing");
        for (auto& p : state_->slots) {
            if (p && !p->leased) {
                p->leased = true;
                selected = p;
                break;
            }
        }
    }

    if (!selected) throw std::runtime_error("MFD3D11FrameCloner: no slot was selected");
    return MFD3D11CameraFrame(selected);
}

MFD3D11CameraFrame MFD3D11FrameCloner::clone(MFD3D11CameraFrame& source) {
    if (!source) throw std::runtime_error("MFD3D11FrameCloner::clone: empty source frame");
    if (!core_) throw std::runtime_error("MFD3D11FrameCloner::clone: not initialized");
    if (source.width() != width_ || source.height() != height_ || source.format() != format_) {
        throw std::runtime_error("MFD3D11FrameCloner::clone: source frame format/size mismatch");
    }

    auto dstFrame = acquireSlot();
    auto& dst = *dstFrame.storage_;
    auto& srcResource = source.resource();
    auto& dstResource = dst.outputTexture;

    CopyTexture2D(core_->GetImmediateContext(), dstResource.AsTexture2D(), srcResource.AsTexture2D());

    dst.frameNumber = source.frameNumber();
    dst.sampleTime100ns = source.sampleTime100ns();
    dst.sampleDuration100ns = source.sampleDuration100ns();
    dst.acquiredTime = source.acquiredTime();
    dst.width = source.width();
    dst.height = source.height();
    dst.format = source.format();

    if (waitForGpuCompletionOnClone_) {
        dstFrame.waitReady();
    }
    return dstFrame;
}

void MFD3D11FrameCloner::close() noexcept {
    if (state_) {
        auto control = state_->leaseControl;
        {
            std::lock_guard<std::mutex> lock(control->mutex);
            control->closing = true;
        }
        control->cv.notify_all();
        state_->slots.clear();
        state_.reset();
    }
    core_.reset();
    width_ = 0;
    height_ = 0;
    format_ = DXGI_FORMAT_UNKNOWN;
    waitForGpuCompletionOnClone_ = false;
}

} // namespace MFFrameSource::internal
