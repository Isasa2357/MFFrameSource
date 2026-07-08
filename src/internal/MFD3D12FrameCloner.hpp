#pragma once

#include "MFD3D12FrameLeaseControl.hpp"

#include <MFFrameSource/MFD3D12CameraCapture.hpp>

#include <D3D12Helper/D3D12Core/D3D12Core.hpp>
#include <D3D12Helper/D3D12Gpu/D3D12Gpu.hpp>

#include <memory>
#include <mutex>
#include <vector>

namespace MFFrameSource::internal {

class MFD3D12FrameCloner {
public:
    void initialize(std::shared_ptr<D3D12CoreLib::D3D12Core> core,
                    UINT width,
                    UINT height,
                    DXGI_FORMAT format,
                    std::size_t poolSize,
                    UINT persistentSrvDescriptorCount,
                    bool waitForGpuCompletionOnClone = false);

    MFD3D12CameraFrame clone(MFD3D12CameraFrame& source);
    void close() noexcept;

private:
    struct PoolState {
        std::shared_ptr<MFD3D12FrameLeaseControl> leaseControl =
            std::make_shared<MFD3D12FrameLeaseControl>();
        std::vector<std::shared_ptr<MFD3D12CameraFrame::Storage>> slots;
    };

    std::shared_ptr<MFD3D12CameraFrame::Storage> createSlot();
    MFD3D12CameraFrame acquireSlot();
    void waitForSlotGpu(MFD3D12CameraFrame::Storage& slot);
    void createSrv(MFD3D12CameraFrame::Storage& slot);

    std::shared_ptr<D3D12CoreLib::D3D12Core> core_;
    UINT width_ = 0;
    UINT height_ = 0;
    DXGI_FORMAT format_ = DXGI_FORMAT_UNKNOWN;
    bool waitForGpuCompletionOnClone_ = false;

    std::shared_ptr<PoolState> state_;
    D3D12CoreLib::D3D12DescriptorAllocator persistentSrv_;
};

} // namespace MFFrameSource::internal
