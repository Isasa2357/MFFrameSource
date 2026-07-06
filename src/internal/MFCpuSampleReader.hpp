#pragma once

#include <MFFrameSource/MFCommon.hpp>

#include <array>
#include <chrono>

namespace MFFrameSource::internal {

struct CpuPlaneView {
    const void* data = nullptr;
    UINT64 rowPitch = 0;
    UINT64 slicePitch = 0;
};

class MFCpuVideoSample {
public:
    MFCpuVideoSample() = default;
    ~MFCpuVideoSample();

    MFCpuVideoSample(const MFCpuVideoSample&) = delete;
    MFCpuVideoSample& operator=(const MFCpuVideoSample&) = delete;
    MFCpuVideoSample(MFCpuVideoSample&& other) noexcept;
    MFCpuVideoSample& operator=(MFCpuVideoSample&& other) noexcept;

    void unlock() noexcept;

    bool valid() const noexcept { return valid_; }
    DXGI_FORMAT dxgiFormat = DXGI_FORMAT_UNKNOWN;
    GUID mfSubtype = GUID_NULL;
    UINT width = 0;
    UINT height = 0;
    std::int64_t sampleTime100ns = -1;
    std::int64_t sampleDuration100ns = -1;
    std::chrono::steady_clock::time_point acquiredTime;

    std::array<CpuPlaneView, 2> planes = {};
    UINT planeCount = 0;

private:
    friend class MFCpuSampleReader;

    bool valid_ = false;
    ComPtr<IMFSample> sample_;
    ComPtr<IMFMediaBuffer> buffer_;
    ComPtr<IMF2DBuffer> buffer2d_;
    BYTE* lockPtr_ = nullptr;
    LONG lockPitch_ = 0;
    DWORD lockMaxLen_ = 0;
    DWORD lockCurLen_ = 0;
    bool locked2d_ = false;
    bool lockedMediaBuffer_ = false;

    std::vector<std::uint8_t> owned_; // normalized copy when required
};

struct MFCpuSampleReadResult {
    MFFrameStatus status = MFFrameStatus::Error;
    MFCpuVideoSample sample;
    MFErrorInfo error;
    bool ok() const noexcept { return status == MFFrameStatus::Ok && sample.valid(); }
};

class MFCpuSampleReader {
public:
    MFCpuSampleReader() = default;
    ~MFCpuSampleReader();

    MFCpuSampleReader(const MFCpuSampleReader&) = delete;
    MFCpuSampleReader& operator=(const MFCpuSampleReader&) = delete;

    void open(const MFCameraSelector& selector, const MFCameraCaptureConfig& config);
    MFCpuSampleReadResult read();
    void close() noexcept;
    bool isOpened() const noexcept { return reader_ != nullptr; }

    const MFCameraFormatInfo& selectedFormat() const noexcept { return selectedFormat_; }

private:
    void selectExactNativeFormat(const MFCameraFormatRequest& request);
    MFCpuVideoSample lockSample(IMFSample* sample, LONGLONG sampleTime, LONGLONG sampleDuration);

    ComPtr<IMFMediaSource> source_;
    ComPtr<IMFSourceReader> reader_;
    MFCameraFormatInfo selectedFormat_ = {};
};

} // namespace MFFrameSource::internal
