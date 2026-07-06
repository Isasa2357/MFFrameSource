#pragma once

#include <MFFrameSource/MFD3D12VideoCapture.hpp>
#include "MFCpuSampleReader.hpp"

#include <chrono>
#include <string>

namespace MFFrameSource::internal {

class MFVideoFileSampleReader {
public:
    MFVideoFileSampleReader() = default;
    ~MFVideoFileSampleReader();

    MFVideoFileSampleReader(const MFVideoFileSampleReader&) = delete;
    MFVideoFileSampleReader& operator=(const MFVideoFileSampleReader&) = delete;

    void open(const std::wstring& filePath, const MFVideoCaptureConfig& config);
    MFCpuSampleReadResult read();
    bool seek(std::int64_t position100ns);
    void close() noexcept;

    bool isOpened() const noexcept { return reader_ != nullptr; }
    const MFCameraFormatInfo& selectedFormat() const noexcept { return selectedFormat_; }
    std::int64_t duration100ns() const noexcept { return duration100ns_; }
    const std::wstring& filePath() const noexcept { return filePath_; }

private:
    void configureExactDecodedFormat(const MFCameraFormatRequest& request);
    MFCpuVideoSample lockSample(IMFSample* sample, LONGLONG sampleTime, LONGLONG sampleDuration);
    void readDurationAttribute() noexcept;

    ComPtr<IMFSourceReader> reader_;
    MFCameraFormatInfo selectedFormat_ = {};
    std::wstring filePath_;
    std::int64_t duration100ns_ = -1;
};

} // namespace MFFrameSource::internal
