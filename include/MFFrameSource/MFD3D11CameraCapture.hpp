#pragma once

#include "MFCommon.hpp"

#include <D3D11Helper/D3D11Core/D3D11Core.hpp>
#include <D3D11Helper/D3D11Gpu/D3D11Gpu.hpp>

#include <chrono>
#include <memory>

namespace MFFrameSource {

namespace internal {
class MFD3D11FrameCloner;
struct MFD3D11FrameLeaseControl;
}

class MFD3D11CameraFrame {
public:
    class Storage;

    MFD3D11CameraFrame() = default;
    ~MFD3D11CameraFrame();

    MFD3D11CameraFrame(const MFD3D11CameraFrame&) = delete;
    MFD3D11CameraFrame& operator=(const MFD3D11CameraFrame&) = delete;
    MFD3D11CameraFrame(MFD3D11CameraFrame&&) noexcept;
    MFD3D11CameraFrame& operator=(MFD3D11CameraFrame&&) noexcept;

    explicit operator bool() const noexcept { return static_cast<bool>(storage_); }

    D3D11CoreLib::D3D11Resource& resource();
    const D3D11CoreLib::D3D11Resource& resource() const;

    ID3D11ShaderResourceView* srv() const noexcept;

    UINT width() const noexcept;
    UINT height() const noexcept;
    DXGI_FORMAT format() const noexcept;

    std::uint64_t frameNumber() const noexcept;
    std::int64_t sampleTime100ns() const noexcept;
    std::int64_t sampleDuration100ns() const noexcept;
    std::chrono::steady_clock::time_point acquiredTime() const noexcept;

    // D3D11 backend uses the immediate-context command stream.
    // isReady() returns true for non-empty frames. waitReady() calls D3D11Core::Flush()
    // to provide an explicit CPU-side completion point when needed.
    bool isReady() const noexcept;
    void waitReady() const;

private:
    friend class MFD3D11UploadFramePool;
    friend class internal::MFD3D11FrameCloner;
    explicit MFD3D11CameraFrame(std::shared_ptr<Storage> storage) noexcept;
    void releaseStorage() noexcept;

    std::shared_ptr<Storage> storage_;
};

struct MFD3D11CameraReadResult {
    MFFrameStatus status = MFFrameStatus::Error;
    MFD3D11CameraFrame frame;
    MFErrorInfo error;

    bool ok() const noexcept { return status == MFFrameStatus::Ok && static_cast<bool>(frame); }
};

class MFD3D11CameraCapture {
public:
    MFD3D11CameraCapture();
    ~MFD3D11CameraCapture();

    MFD3D11CameraCapture(const MFD3D11CameraCapture&) = delete;
    MFD3D11CameraCapture& operator=(const MFD3D11CameraCapture&) = delete;

    bool open(const MFCameraSelector& selector,
              const MFCameraCaptureConfig& config,
              std::shared_ptr<D3D11CoreLib::D3D11Core> d3d11);

    MFD3D11CameraReadResult read();

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
