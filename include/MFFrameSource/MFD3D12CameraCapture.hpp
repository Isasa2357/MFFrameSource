#pragma once

#include "MFCommon.hpp"

#include <D3D12Helper/D3D12Core/D3D12Core.hpp>
#include <D3D12Helper/D3D12Framework/D3D12DescriptorHandle.hpp>
#include <D3D12Helper/D3D12Framework/D3D12Resource.hpp>

#include <chrono>
#include <memory>

namespace MFFrameSource {

namespace internal {
class MFD3D12FrameCloner;
struct MFD3D12FrameLeaseControl;
}

class MFD3D12CameraFrame {
public:
    class Storage;

    MFD3D12CameraFrame() = default;
    ~MFD3D12CameraFrame();

    MFD3D12CameraFrame(const MFD3D12CameraFrame&) = delete;
    MFD3D12CameraFrame& operator=(const MFD3D12CameraFrame&) = delete;
    MFD3D12CameraFrame(MFD3D12CameraFrame&&) noexcept;
    MFD3D12CameraFrame& operator=(MFD3D12CameraFrame&&) noexcept;

    explicit operator bool() const noexcept { return static_cast<bool>(storage_); }

    D3D12CoreLib::D3D12Resource& resource();
    const D3D12CoreLib::D3D12Resource& resource() const;

    D3D12CoreLib::D3D12DescriptorHandle srv() const noexcept;
    ID3D12DescriptorHeap* srvDescriptorHeap() const noexcept;

    UINT width() const noexcept;
    UINT height() const noexcept;
    DXGI_FORMAT format() const noexcept;

    std::uint64_t frameNumber() const noexcept;
    std::int64_t sampleTime100ns() const noexcept;
    std::int64_t sampleDuration100ns() const noexcept;
    std::chrono::steady_clock::time_point acquiredTime() const noexcept;

    // GPU readiness for asynchronous D3D12 command submission.
    // Most producer APIs return after command submission, not after GPU completion.
    // Call waitReady() before using the resource on CPU or on a different queue.
    bool isReady() const;
    void waitReady() const;
    UINT64 readyFenceValue() const noexcept;
    D3D12_RESOURCE_STATES resourceState() const noexcept;


private:
    friend class MFD3D12UploadFramePool;
    friend class internal::MFD3D12FrameCloner;
    explicit MFD3D12CameraFrame(std::shared_ptr<Storage> storage) noexcept;
    void releaseStorage() noexcept;

    std::shared_ptr<Storage> storage_;
};

struct MFD3D12CameraReadResult {
    MFFrameStatus status = MFFrameStatus::Error;
    MFD3D12CameraFrame frame;
    MFErrorInfo error;

    bool ok() const noexcept { return status == MFFrameStatus::Ok && static_cast<bool>(frame); }
};

class MFD3D12CameraCapture {
public:
    MFD3D12CameraCapture();
    ~MFD3D12CameraCapture();

    MFD3D12CameraCapture(const MFD3D12CameraCapture&) = delete;
    MFD3D12CameraCapture& operator=(const MFD3D12CameraCapture&) = delete;

    bool open(const MFCameraSelector& selector,
              const MFCameraCaptureConfig& config,
              std::shared_ptr<D3D12CoreLib::D3D12Core> d3d12);

    MFD3D12CameraReadResult read();

    void close() noexcept;
    bool isOpened() const noexcept;

    const MFErrorInfo& lastError() const noexcept { return lastError_; }
    const MFCameraFormatInfo& selectedFormat() const noexcept { return selectedFormat_; }

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
    MFErrorInfo lastError_;
    MFCameraFormatInfo selectedFormat_;
};

} // namespace MFFrameSource
