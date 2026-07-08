#include "MFD3D11UploadFramePool.hpp"

#include <algorithm>
#include <cstring>
#include <filesystem>
#include <limits>
#include <stdexcept>
#include <string>

namespace MFFrameSource {

using namespace D3D11CoreLib;
using namespace D3D11CoreLib::Processing;

namespace {

std::filesystem::path ToPath(const std::wstring& s) {
    return s.empty() ? std::filesystem::path{} : std::filesystem::path{s};
}

UINT TightRowBytes(DXGI_FORMAT format, UINT width) {
    switch (format) {
    case DXGI_FORMAT_NV12: return width;
    case DXGI_FORMAT_P010: return width * 2;
    case DXGI_FORMAT_R8G8B8A8_UNORM:
    case DXGI_FORMAT_B8G8R8A8_UNORM: return width * 4;
    default: return 0;
    }
}

UINT PlaneRows(DXGI_FORMAT format, UINT plane, UINT height) {
    if (plane == 0) return height;
    if (format == DXGI_FORMAT_NV12 || format == DXGI_FORMAT_P010) return height / 2;
    return 0;
}

UINT ToUintPitch(UINT64 pitch, const char* where) {
    if (pitch > std::numeric_limits<UINT>::max()) {
        throw std::runtime_error(std::string(where) + ": row pitch is too large");
    }
    return static_cast<UINT>(pitch);
}

void CopyRowsToTight(std::vector<std::uint8_t>& dst,
                     const void* src,
                     UINT64 srcPitch,
                     UINT rowBytes,
                     UINT rows) {
    if (!src) throw std::runtime_error("CopyRowsToTight: null source pointer");
    const auto base = dst.size();
    dst.resize(base + static_cast<std::size_t>(rowBytes) * rows);
    auto* out = dst.data() + base;
    const auto* in = static_cast<const std::uint8_t*>(src);
    for (UINT y = 0; y < rows; ++y) {
        std::memcpy(out + static_cast<std::size_t>(rowBytes) * y,
                    in + static_cast<std::size_t>(srcPitch) * y,
                    rowBytes);
    }
}

} // namespace

void MFD3D11UploadFramePool::initialize(std::shared_ptr<D3D11Core> core,
                                        const MFCameraCaptureConfig& config,
                                        const MFCameraFormatInfo& inputFormat) {
    if (!core) throw std::runtime_error("MFD3D11UploadFramePool::initialize: null D3D11Core");
    if (!IsSupportedCpuUploadInputFormat(inputFormat.dxgiFormat)) {
        throw std::runtime_error("MFD3D11UploadFramePool::initialize: unsupported input format");
    }
    if (config.framePoolSize == 0) {
        throw std::runtime_error("MFD3D11UploadFramePool::initialize: framePoolSize must be > 0");
    }

    close();
    core_ = std::move(core);
    config_ = config;
    inputFormat_ = inputFormat;
    outputWidth_ = config.outputWidth ? config.outputWidth : inputFormat.width;
    outputHeight_ = config.outputHeight ? config.outputHeight : inputFormat.height;
    state_ = std::make_shared<PoolState>();

    for (std::size_t i = 0; i < config_.framePoolSize; ++i) {
        state_->slots.push_back(createSlot());
    }
}

std::shared_ptr<MFD3D11CameraFrame::Storage> MFD3D11UploadFramePool::createSlot() {
    auto slot = std::make_shared<MFD3D11CameraFrame::Storage>();
    slot->core = core_;
    slot->leaseControl = state_->leaseControl;

    slot->inputTexture = CreateTexture2D(
        *core_,
        inputFormat_.width,
        inputFormat_.height,
        inputFormat_.dxgiFormat,
        D3D11_BIND_SHADER_RESOURCE,
        D3D11_USAGE_DEFAULT);

    slot->processingContext.Initialize(*core_, ToPath(config_.processingShaderDirectory));
    slot->fusedProcessor.Initialize(slot->processingContext);
    slot->outputTexture = slot->fusedProcessor.CreateOutputTexture(
        *core_, outputWidth_, outputHeight_, config_.outputFormat);

    createOutputSrv(*slot);
    slot->width = outputWidth_;
    slot->height = outputHeight_;
    slot->format = config_.outputFormat;
    return slot;
}

void MFD3D11UploadFramePool::createOutputSrv(MFD3D11CameraFrame::Storage& slot) {
    slot.outputSrv = CreateTexture2DSrv(*core_, slot.outputTexture, config_.outputFormat);
}

MFD3D11CameraFrame MFD3D11UploadFramePool::acquireSlot() {
    if (!state_) throw std::runtime_error("MFD3D11UploadFramePool::acquireSlot: not initialized");
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
        if (control->closing) throw std::runtime_error("MFD3D11UploadFramePool: pool is closing");
        for (auto& p : state_->slots) {
            if (p && !p->leased) {
                p->leased = true;
                selected = p;
                break;
            }
        }
    }

    if (!selected) throw std::runtime_error("MFD3D11UploadFramePool: no slot was selected");
    return MFD3D11CameraFrame(selected);
}

void MFD3D11UploadFramePool::uploadSampleToInputTexture(const internal::MFCpuVideoSample& sample,
                                                        D3D11Resource& texture) {
    ID3D11Texture2D* tex = texture.AsTexture2D();
    if (!tex) throw std::runtime_error("MFD3D11UploadFramePool::uploadSampleToInputTexture: null Texture2D");

    const UINT rowBytes = TightRowBytes(sample.dxgiFormat, sample.width);
    if (rowBytes == 0) {
        throw std::runtime_error("MFD3D11UploadFramePool::uploadSampleToInputTexture: unsupported input format");
    }

    D3D11_TEXTURE2D_DESC desc = {};
    tex->GetDesc(&desc);
    if (desc.Width != sample.width || desc.Height != sample.height || desc.Format != sample.dxgiFormat) {
        throw std::runtime_error("MFD3D11UploadFramePool::uploadSampleToInputTexture: texture/sample mismatch");
    }

    if (sample.planeCount == 1) {
        const auto& p = sample.planes[0];
        if (!p.data || p.rowPitch < rowBytes) {
            throw std::runtime_error("MFD3D11UploadFramePool::uploadSampleToInputTexture: invalid plane 0");
        }
        core_->GetImmediateContext()->UpdateSubresource(
            tex,
            0,
            nullptr,
            p.data,
            ToUintPitch(p.rowPitch, "MFD3D11UploadFramePool::uploadSampleToInputTexture"),
            0);
        return;
    }

    if (sample.planeCount != 2) {
        throw std::runtime_error("MFD3D11UploadFramePool::uploadSampleToInputTexture: unexpected plane count");
    }

    const auto& y = sample.planes[0];
    const auto& uv = sample.planes[1];
    if (!y.data || !uv.data || y.rowPitch < rowBytes || uv.rowPitch < rowBytes) {
        throw std::runtime_error("MFD3D11UploadFramePool::uploadSampleToInputTexture: invalid planar input");
    }

    const auto* yBase = static_cast<const std::uint8_t*>(y.data);
    const auto* uvBase = static_cast<const std::uint8_t*>(uv.data);
    const bool canUseSourceAsOneBuffer =
        y.rowPitch == uv.rowPitch &&
        y.rowPitch <= std::numeric_limits<UINT>::max() &&
        uvBase == yBase + static_cast<std::size_t>(y.rowPitch) * sample.height;

    if (canUseSourceAsOneBuffer) {
        core_->GetImmediateContext()->UpdateSubresource(
            tex,
            0,
            nullptr,
            y.data,
            static_cast<UINT>(y.rowPitch),
            0);
        return;
    }

    planarUploadScratch_.clear();
    CopyRowsToTight(planarUploadScratch_, y.data, y.rowPitch,
                    rowBytes, PlaneRows(sample.dxgiFormat, 0, sample.height));
    CopyRowsToTight(planarUploadScratch_, uv.data, uv.rowPitch,
                    rowBytes, PlaneRows(sample.dxgiFormat, 1, sample.height));

    core_->GetImmediateContext()->UpdateSubresource(
        tex,
        0,
        nullptr,
        planarUploadScratch_.data(),
        rowBytes,
        0);
}

MFD3D11CameraFrame MFD3D11UploadFramePool::process(internal::MFCpuVideoSample& sample) {
    if (!sample.valid()) throw std::runtime_error("MFD3D11UploadFramePool::process: invalid CPU sample");
    if (sample.dxgiFormat != inputFormat_.dxgiFormat || sample.width != inputFormat_.width || sample.height != inputFormat_.height) {
        throw std::runtime_error("MFD3D11UploadFramePool::process: sample format changed");
    }
    if (sample.planeCount != DxgiPlaneCount(inputFormat_.dxgiFormat)) {
        throw std::runtime_error("MFD3D11UploadFramePool::process: unexpected plane count");
    }

    auto frame = acquireSlot();
    auto& slot = *frame.storage_;

    uploadSampleToInputTexture(sample, slot.inputTexture);

    FusedConvertResizeDesc desc;
    desc.srcFormat = inputFormat_.dxgiFormat;
    desc.dstFormat = config_.outputFormat;
    desc.filter = ProcessingFilter::Linear;
    desc.color.srcMatrix = ProcessingColorMatrix::BT709;
    desc.color.srcRange = ProcessingColorRange::Full;
    desc.color.dstMatrix = ProcessingColorMatrix::BT709;
    desc.color.dstRange = ProcessingColorRange::Full;
    desc.color.alphaMode = ProcessingAlphaMode::Ignore;

    slot.fusedProcessor.DispatchConvertResize(
        core_->GetImmediateContext(),
        slot.inputTexture,
        slot.outputTexture,
        desc);

    slot.frameNumber = nextFrameNumber_++;
    slot.sampleTime100ns = sample.sampleTime100ns;
    slot.sampleDuration100ns = sample.sampleDuration100ns;
    slot.acquiredTime = sample.acquiredTime;
    slot.width = outputWidth_;
    slot.height = outputHeight_;
    slot.format = config_.outputFormat;

    if (config_.waitForGpuCompletionOnRead) {
        frame.waitReady();
    }
    return frame;
}

void MFD3D11UploadFramePool::close() noexcept {
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
    inputFormat_ = {};
    config_ = {};
    outputWidth_ = 0;
    outputHeight_ = 0;
    planarUploadScratch_.clear();
    nextFrameNumber_ = 0;
}

} // namespace MFFrameSource
