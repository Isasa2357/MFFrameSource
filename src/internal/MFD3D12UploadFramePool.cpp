#include "MFD3D12UploadFramePool.hpp"

#include "MFComUtil.hpp"

#include <algorithm>
#include <sstream>
#include <stdexcept>

namespace MFFrameSource {

using namespace D3D12CoreLib;
using namespace D3D12CoreLib::Processing;

namespace {

UINT64 AlignUp(UINT64 value, UINT64 alignment) noexcept {
    return (value + alignment - 1) & ~(alignment - 1);
}

std::filesystem::path ToPath(const std::wstring& s) {
    return s.empty() ? std::filesystem::path{} : std::filesystem::path{s};
}

D3D12TextureSubresourceData MakeSubresource(const internal::CpuPlaneView& p) {
    D3D12TextureSubresourceData d;
    d.data = p.data;
    d.rowPitch = p.rowPitch;
    d.slicePitch = p.slicePitch;
    return d;
}

} // namespace

void MFD3D12UploadFramePool::initialize(std::shared_ptr<D3D12Core> core,
                                        const MFCameraCaptureConfig& config,
                                        const MFCameraFormatInfo& inputFormat) {
    if (!core) throw std::runtime_error("MFD3D12UploadFramePool::initialize: null D3D12Core");
    if (!IsSupportedCpuUploadInputFormat(inputFormat.dxgiFormat)) {
        throw std::runtime_error("MFD3D12UploadFramePool::initialize: unsupported input format");
    }
    if (config.framePoolSize == 0) {
        throw std::runtime_error("MFD3D12UploadFramePool::initialize: framePoolSize must be > 0");
    }
    if (config.transientCbvSrvUavDescriptorCount == 0 || config.transientSamplerDescriptorCount == 0) {
        throw std::runtime_error("MFD3D12UploadFramePool::initialize: transient descriptor counts must be > 0");
    }

    close();
    core_ = std::move(core);
    config_ = config;
    inputFormat_ = inputFormat;
    outputWidth_ = config.outputWidth ? config.outputWidth : inputFormat.width;
    outputHeight_ = config.outputHeight ? config.outputHeight : inputFormat.height;
    state_ = std::make_shared<PoolState>();

    persistentSrv_.Initialize(core_->GetDevice(), D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV,
                              std::max<UINT>(config_.persistentSrvDescriptorCount,
                                             static_cast<UINT>(config_.framePoolSize)),
                              true);

    auto temp = CreateTexture2D(*core_, inputFormat_.width, inputFormat_.height, inputFormat_.dxgiFormat,
                                D3D12_RESOURCE_STATE_COPY_DEST);
    const UINT subCount = inputSubresourceCount();
    const UINT64 required = GetRequiredUploadSize(*core_, temp, 0, subCount);
    const UINT64 autoSize = AlignUp(required * static_cast<UINT64>(config_.framePoolSize) +
                                        (4ull * 1024ull * 1024ull),
                                    D3D12_TEXTURE_DATA_PLACEMENT_ALIGNMENT);
    uploadRing_.Initialize(core_->GetDevice(), config_.uploadRingSizeBytes ? config_.uploadRingSizeBytes : autoSize);

    for (std::size_t i = 0; i < config_.framePoolSize; ++i) {
        state_->slots.push_back(createSlot());
    }
}

UINT MFD3D12UploadFramePool::inputSubresourceCount() const noexcept {
    return DxgiPlaneCount(inputFormat_.dxgiFormat);
}

std::shared_ptr<MFD3D12CameraFrame::Storage> MFD3D12UploadFramePool::createSlot() {
    auto slot = std::make_shared<MFD3D12CameraFrame::Storage>();
    slot->core = core_;
    slot->leaseControl = state_->leaseControl;
    slot->inputTexture = CreateTexture2D(*core_, inputFormat_.width, inputFormat_.height, inputFormat_.dxgiFormat,
                                         D3D12_RESOURCE_STATE_COPY_DEST);

    slot->transientCbvSrvUav.Initialize(core_->GetDevice(), D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV,
                                        config_.transientCbvSrvUavDescriptorCount, true);
    slot->transientSampler.Initialize(core_->GetDevice(), D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER,
                                      config_.transientSamplerDescriptorCount, true);
    slot->processingContext.Initialize(*core_, &slot->transientCbvSrvUav, &slot->transientSampler,
                                       ToPath(config_.processingShaderDirectory));
    slot->fusedProcessor.Initialize(slot->processingContext);

    slot->outputTexture = slot->fusedProcessor.CreateOutputTexture(*core_, outputWidth_, outputHeight_, config_.outputFormat,
                                                                   D3D12_RESOURCE_STATE_COMMON);
    slot->commandContext = core_->CreateDirectContext();
    createOutputSrv(*slot);
    slot->width = outputWidth_;
    slot->height = outputHeight_;
    slot->format = config_.outputFormat;
    slot->resourceState = slot->outputTexture.GetState();
    return slot;
}

void MFD3D12UploadFramePool::createOutputSrv(MFD3D12CameraFrame::Storage& slot) {
    slot.outputSrv = persistentSrv_.Allocate();
    slot.outputSrvHeap = persistentSrv_.GetHeap();
    CreateTexture2DSrv(*core_, slot.outputTexture, slot.outputSrv.cpu, config_.outputFormat);
}

void MFD3D12UploadFramePool::waitForSlotGpu(MFD3D12CameraFrame::Storage& slot) {
    if (slot.readyFenceValue != 0) {
        core_->DirectQueue().WaitForFenceValue(slot.readyFenceValue);
        slot.readyFenceValue = 0;
    }
}

MFD3D12CameraFrame MFD3D12UploadFramePool::acquireSlot() {
    if (!state_) throw std::runtime_error("MFD3D12UploadFramePool::acquireSlot: not initialized");
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
        if (control->closing) throw std::runtime_error("MFD3D12UploadFramePool: pool is closing");
        for (auto& p : state_->slots) {
            if (p && !p->leased) {
                p->leased = true;
                selected = p;
                break;
            }
        }
    }

    if (!selected) throw std::runtime_error("MFD3D12UploadFramePool: no slot was selected");
    waitForSlotGpu(*selected);
    return MFD3D12CameraFrame(selected);
}

void MFD3D12UploadFramePool::transitionToCopyDest(D3D12CommandContext& ctx, D3D12Resource& texture) {
    const auto before = texture.GetState();
    if (before != D3D12_RESOURCE_STATE_COPY_DEST) {
        ctx.ResourceBarrier(MakeTransitionBarrier(texture.Get(), before, D3D12_RESOURCE_STATE_COPY_DEST));
        texture.SetState(D3D12_RESOURCE_STATE_COPY_DEST);
    }
}

MFD3D12CameraFrame MFD3D12UploadFramePool::process(internal::MFCpuVideoSample& sample) {
    if (!sample.valid()) throw std::runtime_error("MFD3D12UploadFramePool::process: invalid CPU sample");
    if (sample.dxgiFormat != inputFormat_.dxgiFormat || sample.width != inputFormat_.width || sample.height != inputFormat_.height) {
        throw std::runtime_error("MFD3D12UploadFramePool::process: sample format changed");
    }
    if (sample.planeCount != inputSubresourceCount()) {
        throw std::runtime_error("MFD3D12UploadFramePool::process: unexpected plane count");
    }

    auto frame = acquireSlot();
    auto& slot = *frame.storage_;

    slot.transientCbvSrvUav.Reset();
    slot.transientSampler.Reset();
    uploadRing_.ReclaimCompleted(core_->DirectQueue().Fence());

    slot.commandContext.Reset();
    transitionToCopyDest(slot.commandContext, slot.inputTexture);

    D3D12TextureSubresourceData subresources[2] = {};
    for (UINT i = 0; i < sample.planeCount; ++i) {
        subresources[i] = MakeSubresource(sample.planes[i]);
    }

    RecordUploadTextureSubresources(*core_, slot.commandContext, slot.inputTexture, uploadRing_,
                                    subresources, 0, sample.planeCount,
                                    D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);

    FusedConvertResizeDesc desc;
    desc.srcFormat = inputFormat_.dxgiFormat;
    desc.dstFormat = config_.outputFormat;
    desc.filter = ProcessingFilter::Linear;
    desc.color.srcMatrix = ProcessingColorMatrix::BT709;
    desc.color.srcRange = ProcessingColorRange::Full;
    desc.color.dstMatrix = ProcessingColorMatrix::BT709;
    desc.color.dstRange = ProcessingColorRange::Full;
    desc.color.alphaMode = ProcessingAlphaMode::Ignore;

    D3D12ProcessingStateDesc state;
    state.useExplicitStates = true;
    state.srcBefore = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
    state.srcAfter = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
    state.dstBefore = slot.outputTexture.GetState();
    state.dstAfter = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
    slot.fusedProcessor.RecordConvertResize(slot.commandContext, slot.inputTexture, slot.outputTexture, desc, state);
    slot.inputTexture.SetState(D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    slot.outputTexture.SetState(D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);

    slot.commandContext.Close();
    ID3D12CommandList* lists[] = { slot.commandContext.GetCommandList() };
    core_->DirectQueue().ExecuteCommandLists(1, lists);
    const UINT64 fenceValue = core_->DirectQueue().Signal();
    uploadRing_.FinishFrame(fenceValue);

    slot.readyFenceValue = fenceValue;
    slot.frameNumber = nextFrameNumber_++;
    slot.sampleTime100ns = sample.sampleTime100ns;
    slot.sampleDuration100ns = sample.sampleDuration100ns;
    slot.acquiredTime = sample.acquiredTime;
    slot.width = outputWidth_;
    slot.height = outputHeight_;
    slot.format = config_.outputFormat;
    slot.resourceState = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;

    if (config_.waitForGpuCompletionOnRead) {
        frame.waitReady();
    }
    return frame;
}

void MFD3D12UploadFramePool::close() noexcept {
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
    nextFrameNumber_ = 0;
}

} // namespace MFFrameSource
