#pragma once

#include "MFCommon.hpp"
#include "MFD3D11CameraCapture.hpp"

#include <D3D11Helper/D3D11Core/D3D11Core.hpp>

#include <chrono>
#include <cstdint>
#include <memory>
#include <string>

namespace MFFrameSource {

using MFD3D11VideoFrame = MFD3D11CameraFrame;

struct MFD3D11VideoReadResult {
    MFFrameStatus status = MFFrameStatus::Error;
    MFD3D11VideoFrame frame;
    MFErrorInfo error;

    bool ok() const noexcept { return status == MFFrameStatus::Ok && static_cast<bool>(frame); }
};

class MFD3D11VideoCapture {
public:
    MFD3D11VideoCapture();
    ~MFD3D11VideoCapture();

    MFD3D11VideoCapture(const MFD3D11VideoCapture&) = delete;
    MFD3D11VideoCapture& operator=(const MFD3D11VideoCapture&) = delete;

    bool open(const std::wstring& filePath,
              const MFVideoCaptureConfig& config,
              std::shared_ptr<D3D11CoreLib::D3D11Core> d3d11);

    MFD3D11VideoReadResult read();
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
