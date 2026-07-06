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

struct MFVideoCaptureConfig {
    // Exact decoded-output request.  The Source Reader may decode compressed
    // streams, but the resulting CPU sample must exactly match this request.
    // No best-match fallback is performed.
    MFCameraFormatRequest input;

    // 0 means input.width / input.height.
    UINT outputWidth = 0;
    UINT outputHeight = 0;
    DXGI_FORMAT outputFormat = DXGI_FORMAT_R8G8B8A8_UNORM;

    std::wstring processingShaderDirectory;

    std::size_t framePoolSize = 4;
    bool waitForGpuCompletionOnRead = false;
    std::uint64_t uploadRingSizeBytes = 0;

    UINT transientCbvSrvUavDescriptorCount = 256;
    UINT transientSamplerDescriptorCount = 16;
    UINT persistentSrvDescriptorCount = 64;

    // File playback controls.
    bool loop = false;
    std::int64_t startPosition100ns = 0;

    // Keep CPU native output path.  Converters/decoders are enabled by default
    // so compressed files can be decoded into the exact requested subtype.
    bool disableConverters = false;
    bool enableHardwareTransforms = false;
};

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
