#pragma once

#include "MFCpuSampleReader.hpp"
#include "MFD3D11FrameLeaseControl.hpp"

#include <MFFrameSource/MFD3D11CameraCapture.hpp>

#include <D3D11Helper/D3D11Core/D3D11Core.hpp>
#include <D3D11Helper/D3D11Gpu/D3D11Gpu.hpp>
#include <D3D11Helper/D3D11Processing/D3D11Processing.hpp>

#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <memory>
#include <mutex>
#include <vector>

namespace MFFrameSource {

class MFD3D11CameraFrame::Storage {
public:
    std::shared_ptr<D3D11CoreLib::D3D11Core> core;
    std::weak_ptr<internal::MFD3D11FrameLeaseControl> leaseControl;

    D3D11CoreLib::D3D11Resource inputTexture;
    D3D11CoreLib::D3D11Resource outputTexture;
    ComPtr<ID3D11ShaderResourceView> outputSrv;

    D3D11CoreLib::Processing::D3D11ProcessingContext processingContext;
    D3D11CoreLib::Processing::D3D11FusedProcessor fusedProcessor;

    UINT width = 0;
    UINT height = 0;
    DXGI_FORMAT format = DXGI_FORMAT_R8G8B8A8_UNORM;
    std::uint64_t frameNumber = 0;
    std::int64_t sampleTime100ns = -1;
    std::int64_t sampleDuration100ns = -1;
    std::chrono::steady_clock::time_point acquiredTime;

    bool leased = false;
};

class MFD3D11UploadFramePool {
public:
    void initialize(std::shared_ptr<D3D11CoreLib::D3D11Core> core,
                    const MFCameraCaptureConfig& config,
                    const MFCameraFormatInfo& inputFormat);

    MFD3D11CameraFrame process(internal::MFCpuVideoSample& sample);
    void close() noexcept;

private:
    struct PoolState {
        std::shared_ptr<internal::MFD3D11FrameLeaseControl> leaseControl =
            std::make_shared<internal::MFD3D11FrameLeaseControl>();
        std::vector<std::shared_ptr<MFD3D11CameraFrame::Storage>> slots;
    };

    MFD3D11CameraFrame acquireSlot();
    std::shared_ptr<MFD3D11CameraFrame::Storage> createSlot();
    void createOutputSrv(MFD3D11CameraFrame::Storage& slot);
    void uploadSampleToInputTexture(const internal::MFCpuVideoSample& sample,
                                    D3D11CoreLib::D3D11Resource& texture);

    std::shared_ptr<D3D11CoreLib::D3D11Core> core_;
    MFCameraCaptureConfig config_ = {};
    MFCameraFormatInfo inputFormat_ = {};
    UINT outputWidth_ = 0;
    UINT outputHeight_ = 0;

    std::shared_ptr<PoolState> state_;
    std::vector<std::uint8_t> planarUploadScratch_;

    std::uint64_t nextFrameNumber_ = 0;
};

} // namespace MFFrameSource
