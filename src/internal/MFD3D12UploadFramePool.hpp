#pragma once

#include "MFCpuSampleReader.hpp"
#include "MFD3D12FrameLeaseControl.hpp"

#include <MFFrameSource/MFD3D12CameraCapture.hpp>

#include <D3D12Helper/D3D12Core/D3D12Core.hpp>
#include <D3D12Helper/D3D12Gpu/D3D12Gpu.hpp>
#include <D3D12Helper/D3D12Processing/D3D12Processing.hpp>

#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <memory>
#include <mutex>
#include <vector>

namespace MFFrameSource {

class MFD3D12CameraFrame::Storage {
public:
    std::shared_ptr<D3D12CoreLib::D3D12Core> core;
    std::weak_ptr<internal::MFD3D12FrameLeaseControl> leaseControl;

    D3D12CoreLib::D3D12Resource inputTexture;
    D3D12CoreLib::D3D12Resource outputTexture;
    D3D12CoreLib::D3D12CommandContext commandContext;
    D3D12CoreLib::D3D12DescriptorHandle outputSrv;
    ID3D12DescriptorHeap* outputSrvHeap = nullptr;

    // Per-slot transient descriptors and processing state.  These are reset only
    // after this slot's previous GPU fence has completed, so in-flight dispatches
    // never observe overwritten descriptors.
    D3D12CoreLib::D3D12DescriptorAllocator transientCbvSrvUav;
    D3D12CoreLib::D3D12DescriptorAllocator transientSampler;
    D3D12CoreLib::Processing::D3D12ProcessingContext processingContext;
    D3D12CoreLib::Processing::D3D12FusedProcessor fusedProcessor;

    UINT width = 0;
    UINT height = 0;
    DXGI_FORMAT format = DXGI_FORMAT_R8G8B8A8_UNORM;
    std::uint64_t frameNumber = 0;
    std::int64_t sampleTime100ns = -1;
    std::int64_t sampleDuration100ns = -1;
    std::chrono::steady_clock::time_point acquiredTime;

    UINT64 readyFenceValue = 0;
    D3D12_RESOURCE_STATES resourceState = D3D12_RESOURCE_STATE_COMMON;
    bool leased = false;
};

class MFD3D12UploadFramePool {
public:
    void initialize(std::shared_ptr<D3D12CoreLib::D3D12Core> core,
                    const MFCameraCaptureConfig& config,
                    const MFCameraFormatInfo& inputFormat);

    MFD3D12CameraFrame process(internal::MFCpuVideoSample& sample);
    void close() noexcept;

private:
    struct PoolState {
        std::shared_ptr<internal::MFD3D12FrameLeaseControl> leaseControl =
            std::make_shared<internal::MFD3D12FrameLeaseControl>();
        std::vector<std::shared_ptr<MFD3D12CameraFrame::Storage>> slots;
    };

    MFD3D12CameraFrame acquireSlot();
    std::shared_ptr<MFD3D12CameraFrame::Storage> createSlot();
    void waitForSlotGpu(MFD3D12CameraFrame::Storage& slot);
    UINT inputSubresourceCount() const noexcept;
    void transitionToCopyDest(D3D12CoreLib::D3D12CommandContext& ctx,
                              D3D12CoreLib::D3D12Resource& texture);
    void createOutputSrv(MFD3D12CameraFrame::Storage& slot);

    std::shared_ptr<D3D12CoreLib::D3D12Core> core_;
    MFCameraCaptureConfig config_ = {};
    MFCameraFormatInfo inputFormat_ = {};
    UINT outputWidth_ = 0;
    UINT outputHeight_ = 0;

    std::shared_ptr<PoolState> state_;

    D3D12CoreLib::D3D12UploadRing uploadRing_;
    D3D12CoreLib::D3D12DescriptorAllocator persistentSrv_;

    std::uint64_t nextFrameNumber_ = 0;
};

} // namespace MFFrameSource
