#pragma once

#include "MFD3D11FrameLeaseControl.hpp"

#include <MFFrameSource/MFD3D11CameraCapture.hpp>

#include <D3D11Helper/D3D11Core/D3D11Core.hpp>
#include <D3D11Helper/D3D11Gpu/D3D11Gpu.hpp>

#include <memory>
#include <mutex>
#include <vector>

namespace MFFrameSource::internal {

class MFD3D11FrameCloner {
public:
    void initialize(std::shared_ptr<D3D11CoreLib::D3D11Core> core,
                    UINT width,
                    UINT height,
                    DXGI_FORMAT format,
                    std::size_t poolSize,
                    UINT persistentSrvDescriptorCount,
                    bool waitForGpuCompletionOnClone = false);

    MFD3D11CameraFrame clone(MFD3D11CameraFrame& source);
    void close() noexcept;

private:
    struct PoolState {
        std::shared_ptr<MFD3D11FrameLeaseControl> leaseControl =
            std::make_shared<MFD3D11FrameLeaseControl>();
        std::vector<std::shared_ptr<MFD3D11CameraFrame::Storage>> slots;
    };

    std::shared_ptr<MFD3D11CameraFrame::Storage> createSlot();
    MFD3D11CameraFrame acquireSlot();
    void createSrv(MFD3D11CameraFrame::Storage& slot);

    std::shared_ptr<D3D11CoreLib::D3D11Core> core_;
    UINT width_ = 0;
    UINT height_ = 0;
    DXGI_FORMAT format_ = DXGI_FORMAT_UNKNOWN;
    bool waitForGpuCompletionOnClone_ = false;

    std::shared_ptr<PoolState> state_;
};

} // namespace MFFrameSource::internal
