#include "MFD3D12FrameCloner.hpp"
#include "MFD3D12UploadFramePool.hpp"

#include <D3D12Helper/D3D12Gpu/D3D12Copy.hpp>

#include <algorithm>
#include <stdexcept>

namespace MFFrameSource::internal {

using namespace D3D12CoreLib;

void MFD3D12FrameCloner::initialize(std::shared_ptr<D3D12Core> core,
                                    UINT width,
                                    UINT height,
                                    DXGI_FORMAT format,
                                    std::size_t poolSize,
                                    UINT persistentSrvDescriptorCount,
                                    bool waitForGpuCompletionOnClone) {
    if (!core) throw std::runtime_error("MFD3D12FrameCloner::initialize: null D3D12Core");
    if (width == 0 || height == 0) throw std::runtime_error("MFD3D12FrameCloner::initialize: size is zero");
    if (format == DXGI_FORMAT_UNKNOWN) throw std::runtime_error("MFD3D12FrameCloner::initialize: unknown format");
    if (poolSize == 0) throw std::runtime_error("MFD3D12FrameCloner::initialize: poolSize must be > 0");

    close();
    core_ = std::move(core);
    width_ = width;
    height_ = height;
    format_ = format;
    waitForGpuCompletionOnClone_ = waitForGpuCompletionOnClone;
    state_ = std::make_shared<PoolState>();

    const UINT descriptorCount = persistentSrvDescriptorCount
        ? persistentSrvDescriptorCount
        : static_cast<UINT>(poolSize);
    persistentSrv_.Initialize(core_->GetDevice(), D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV,
                              descriptorCount, true);

    for (std::size_t i = 0; i < poolSize; ++i) {
        state_->slots.push_back(createSlot());
    }
}

std::shared_ptr<MFD3D12CameraFrame::Storage> MFD3D12FrameCloner::createSlot() {
    auto slot = std::make_shared<MFD3D12CameraFrame::Storage>();
    slot->core = core_;
    slot->leaseControl = state_->leaseControl;
    slot->outputTexture = CreateTexture2D(*core_, width_, height_, format_,
                                          D3D12_RESOURCE_STATE_COPY_DEST);
    slot->commandContext = core_->CreateDirectContext();
    slot->width = width_;
    slot->height = height_;
    slot->format = format_;
    slot->resourceState = D3D12_RESOURCE_STATE_COPY_DEST;
    createSrv(*slot);
    return slot;
}

void MFD3D12FrameCloner::createSrv(MFD3D12CameraFrame::Storage& slot) {
    slot.outputSrv = persistentSrv_.Allocate();
    slot.outputSrvHeap = persistentSrv_.GetHeap();
    CreateTexture2DSrv(*core_, slot.outputTexture, slot.outputSrv.cpu, format_);
}

void MFD3D12FrameCloner::waitForSlotGpu(MFD3D12CameraFrame::Storage& slot) {
    if (slot.readyFenceValue != 0) {
        core_->DirectQueue().WaitForFenceValue(slot.readyFenceValue);
        slot.readyFenceValue = 0;
    }
}

MFD3D12CameraFrame MFD3D12FrameCloner::acquireSlot() {
    if (!state_) throw std::runtime_error("MFD3D12FrameCloner::acquireSlot: not initialized");
    auto control = state_->leaseControl;

    std::shared_ptr<MFD3D12CameraFrame::Storage> selected;
    {
        std::unique_lock<std::mutex> lock(control->mutex);
        control->cv.wait(lock, [&] {
            if (control->closing) return true;
            return std::any_of(state_->slots.begin(), state_->slots.end(), [](const auto& p) {
                return p && !p->leased;
            });
        });
        if (control->closing) throw std::runtime_error("MFD3D12FrameCloner: pool is closing");
        for (auto& p : state_->slots) {
            if (p && !p->leased) {
                p->leased = true;
                selected = p;
                break;
            }
        }
    }

    if (!selected) throw std::runtime_error("MFD3D12FrameCloner: no slot was selected");
    waitForSlotGpu(*selected);
    return MFD3D12CameraFrame(selected);
}

MFD3D12CameraFrame MFD3D12FrameCloner::clone(MFD3D12CameraFrame& source) {
    if (!source) throw std::runtime_error("MFD3D12FrameCloner::clone: empty source frame");
    if (!core_) throw std::runtime_error("MFD3D12FrameCloner::clone: not initialized");
    if (source.width() != width_ || source.height() != height_ || source.format() != format_) {
        throw std::runtime_error("MFD3D12FrameCloner::clone: source frame format/size mismatch");
    }

    auto dstFrame = acquireSlot();
    auto& dst = *dstFrame.storage_;
    auto& srcResource = source.resource();
    auto& dstResource = dst.outputTexture;

    auto& ctx = dst.commandContext;
    ctx.Reset();

    const auto srcBefore = srcResource.GetState();
    RecordCopyTextureSubresource(
        ctx,
        dstResource,
        0,
        srcResource,
        0,
        D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,
        srcBefore);

    ctx.Close();
    ID3D12CommandList* lists[] = { ctx.GetCommandList() };
    core_->DirectQueue().ExecuteCommandLists(1, lists);
    const UINT64 fenceValue = core_->DirectQueue().Signal();

    dst.readyFenceValue = fenceValue;
    dst.frameNumber = source.frameNumber();
    dst.sampleTime100ns = source.sampleTime100ns();
    dst.sampleDuration100ns = source.sampleDuration100ns();
    dst.acquiredTime = source.acquiredTime();
    dst.width = source.width();
    dst.height = source.height();
    dst.format = source.format();
    dst.resourceState = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;

    if (waitForGpuCompletionOnClone_) {
        dstFrame.waitReady();
    }
    return dstFrame;
}

void MFD3D12FrameCloner::close() noexcept {
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
