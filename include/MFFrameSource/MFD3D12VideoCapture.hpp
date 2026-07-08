#pragma once

#include "MFCommon.hpp"
#include "MFD3D12CameraCapture.hpp"

#include <D3D12Helper/D3D12Core/D3D12Core.hpp>

#include <chrono>
#include <cstdint>
#include <memory>
#include <string>

namespace MFFrameSource {

using MFD3D12VideoFrame = MFD3D12CameraFrame;

struct MFD3D12VideoReadResult {
    MFFrameStatus status = MFFrameStatus::Error;
    MFD3D12VideoFrame frame;
    MFErrorInfo error;

    bool ok() const noexcept { return status == MFFrameStatus::Ok && static_cast<bool>(frame); }
};

class MFD3D12VideoCapture {
public:
    MFD3D12VideoCapture();
    ~MFD3D12VideoCapture();

    MFD3D12VideoCapture(const MFD3D12VideoCapture&) = delete;
    MFD3D12VideoCapture& operator=(const MFD3D12VideoCapture&) = delete;

    bool open(const std::wstring& filePath,
              const MFVideoCaptureConfig& config,
              std::shared_ptr<D3D12CoreLib::D3D12Core> d3d12);

    MFD3D12VideoReadResult read();
    bool seek(std::int64_t position100ns);

    void close() noexcept;
    bool isOpened() const noexcept;

    const MFErrorInfo& lastError() const noexcept { return lastError_; }
    const MFCameraFormatInfo& selectedFormat() const noexcept { return selectedFormat_; }
    std::int64_t duration100ns() const noexcept;
    const std::wstring& filePath() const noexcept;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
    MFErrorInfo lastError_;
    MFCameraFormatInfo selectedFormat_;
};

} // namespace MFFrameSource
