#include "MFVideoFileSampleReader.hpp"

#include "MFComUtil.hpp"

#include <cstring>
#include <mferror.h>
#include <propvarutil.h>
#include <sstream>
#include <stdexcept>
#include <string>

namespace MFFrameSource::internal {
namespace {

constexpr DWORD kFirstVideoStream = static_cast<DWORD>(MF_SOURCE_READER_FIRST_VIDEO_STREAM);
constexpr DWORD kAllStreams = static_cast<DWORD>(MF_SOURCE_READER_ALL_STREAMS);
constexpr DWORD kMediaSource = static_cast<DWORD>(MF_SOURCE_READER_MEDIASOURCE);

UINT64 RowBytes(DXGI_FORMAT format, UINT width) {
    switch (format) {
    case DXGI_FORMAT_NV12: return width;
    case DXGI_FORMAT_P010: return static_cast<UINT64>(width) * 2;
    case DXGI_FORMAT_R8G8B8A8_UNORM:
    case DXGI_FORMAT_B8G8R8A8_UNORM: return static_cast<UINT64>(width) * 4;
    default: return 0;
    }
}

UINT PlaneRows(DXGI_FORMAT format, UINT plane, UINT height) {
    if (plane == 0) return height;
    if (format == DXGI_FORMAT_NV12 || format == DXGI_FORMAT_P010) return height / 2;
    return 0;
}

CpuPlaneView MakePlaneView(const void* data, UINT64 rowPitch, UINT rows) {
    CpuPlaneView p;
    p.data = data;
    p.rowPitch = rowPitch;
    p.slicePitch = rowPitch * rows;
    return p;
}

void CopyPitchedRows(std::vector<std::uint8_t>& dst,
                     const std::uint8_t* src,
                     LONG srcPitch,
                     UINT64 rowBytes,
                     UINT rows) {
    const size_t base = dst.size();
    dst.resize(base + static_cast<size_t>(rowBytes) * rows);
    std::uint8_t* out = dst.data() + base;
    for (UINT y = 0; y < rows; ++y) {
        std::memcpy(out + static_cast<size_t>(y) * rowBytes,
                    src + static_cast<std::ptrdiff_t>(y) * srcPitch,
                    static_cast<size_t>(rowBytes));
    }
}

std::wstring FormatRequestText(const MFCameraFormatRequest& request) {
    std::wstringstream ss;
    ss << L"width=" << request.width
       << L" height=" << request.height
       << L" fps=" << request.fps.numerator << L"/" << request.fps.denominator
       << L" subtypeDxgi=" << DxgiFormatName(MfSubtypeToDxgiFormat(request.subtype));
    return ss.str();
}

std::wstring FormatInfoText(const MFCameraFormatInfo& info) {
    std::wstringstream ss;
    ss << L"width=" << info.width
       << L" height=" << info.height
       << L" fps=" << info.fps.numerator << L"/" << info.fps.denominator
       << L" subtypeDxgi=" << DxgiFormatName(info.dxgiFormat);
    return ss.str();
}

void SetExactVideoType(IMFMediaType* type, const MFCameraFormatRequest& request) {
    ThrowIfFailed(type->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video), L"SetGUID(MF_MT_MAJOR_TYPE)");
    ThrowIfFailed(type->SetGUID(MF_MT_SUBTYPE, request.subtype), L"SetGUID(MF_MT_SUBTYPE)");
    ThrowIfFailed(MFSetAttributeSize(type, MF_MT_FRAME_SIZE, request.width, request.height), L"MFSetAttributeSize(MF_MT_FRAME_SIZE)");
    ThrowIfFailed(MFSetAttributeRatio(type, MF_MT_FRAME_RATE, request.fps.numerator, request.fps.denominator), L"MFSetAttributeRatio(MF_MT_FRAME_RATE)");
    ThrowIfFailed(type->SetUINT32(MF_MT_INTERLACE_MODE, static_cast<UINT32>(MFVideoInterlace_Progressive)), L"SetUINT32(MF_MT_INTERLACE_MODE)");
}

MFCameraFormatInfo ReadCurrentVideoFormat(IMFSourceReader* reader, const wchar_t* where) {
    ComPtr<IMFMediaType> actual;
    ThrowIfFailed(reader->GetCurrentMediaType(kFirstVideoStream, &actual), where);

    MFCameraFormatInfo info;
    actual->GetGUID(MF_MT_SUBTYPE, &info.subtype);
    ReadFrameSize(actual.Get(), info.width, info.height);
    info.fps = ReadFrameRate(actual.Get());
    info.dxgiFormat = MfSubtypeToDxgiFormat(info.subtype);
    return info;
}

bool IsCompatibleDecodedFormatAfterChange(const MFCameraFormatInfo& before,
                                          const MFCameraFormatInfo& after) noexcept {
    return GuidEquals(before.subtype, after.subtype) &&
           before.width == after.width &&
           before.height == after.height &&
           IsSupportedCpuUploadInputFormat(after.dxgiFormat);
}

} // namespace

MFVideoFileSampleReader::~MFVideoFileSampleReader() {
    close();
}

void MFVideoFileSampleReader::open(const std::wstring& filePath, const MFVideoCaptureConfig& config) {
    close();
    if (filePath.empty()) {
        throw std::runtime_error("MFVideoFileSampleReader::open: filePath is empty");
    }
    if (!config.input.isComplete()) {
        throw std::runtime_error("MFVideoFileSampleReader::open: config.input must specify subtype, width, height, fps");
    }
    const DXGI_FORMAT requestedDxgi = MfSubtypeToDxgiFormat(config.input.subtype);
    if (!IsSupportedCpuUploadInputFormat(requestedDxgi)) {
        throw std::runtime_error("MFVideoFileSampleReader::open: requested subtype is not supported by CPU upload path");
    }

    filePath_ = filePath;

    ComPtr<IMFAttributes> attrs;
    ThrowIfFailed(MFCreateAttributes(&attrs, 6), L"MFCreateAttributes(video source reader)");
    attrs->SetUINT32(MF_READWRITE_DISABLE_CONVERTERS, config.disableConverters ? TRUE : FALSE);
    attrs->SetUINT32(MF_READWRITE_ENABLE_HARDWARE_TRANSFORMS, config.enableHardwareTransforms ? TRUE : FALSE);

    ThrowIfFailed(MFCreateSourceReaderFromURL(filePath.c_str(), attrs.Get(), &reader_),
                  L"MFCreateSourceReaderFromURL");
    ThrowIfFailed(reader_->SetStreamSelection(kAllStreams, FALSE), L"SetStreamSelection(all false)");
    ThrowIfFailed(reader_->SetStreamSelection(kFirstVideoStream, TRUE), L"SetStreamSelection(video true)");

    readDurationAttribute();
    configureExactDecodedFormat(config.input);

    if (config.startPosition100ns > 0) {
        if (!seek(config.startPosition100ns)) {
            throw std::runtime_error("MFVideoFileSampleReader::open: failed to seek to startPosition100ns");
        }
    }
}

void MFVideoFileSampleReader::readDurationAttribute() noexcept {
    duration100ns_ = -1;
    if (!reader_) return;
    PROPVARIANT var;
    PropVariantInit(&var);
    if (SUCCEEDED(reader_->GetPresentationAttribute(kMediaSource, MF_PD_DURATION, &var))) {
        if (var.vt == VT_UI8) {
            duration100ns_ = static_cast<std::int64_t>(var.uhVal.QuadPart);
        } else if (var.vt == VT_I8) {
            duration100ns_ = static_cast<std::int64_t>(var.hVal.QuadPart);
        }
    }
    PropVariantClear(&var);
}

void MFVideoFileSampleReader::configureExactDecodedFormat(const MFCameraFormatRequest& request) {
    ComPtr<IMFMediaType> desired;
    ThrowIfFailed(MFCreateMediaType(&desired), L"MFCreateMediaType(decoded output)");
    SetExactVideoType(desired.Get(), request);

    HRESULT hr = reader_->SetCurrentMediaType(kFirstVideoStream, nullptr, desired.Get());
    if (FAILED(hr)) {
        std::wstringstream ss;
        ss << L"SetCurrentMediaType failed for exact decoded output. Requested "
           << FormatRequestText(request) << L". HRESULT=0x" << std::hex
           << static_cast<unsigned long>(hr)
           << L". The file may not be decodable to the requested CPU format without a converter/decoder.";
        const auto msgw = ss.str();
        throw std::runtime_error(WideToUtf8(msgw));
    }

    const MFCameraFormatInfo actual = ReadCurrentVideoFormat(reader_.Get(), L"GetCurrentMediaType(decoded output)");

    if (!GuidEquals(actual.subtype, request.subtype) ||
        actual.width != request.width ||
        actual.height != request.height ||
        !(actual.fps == request.fps)) {
        std::wstringstream ss;
        ss << L"Decoded video output does not exactly match request. Requested "
           << FormatRequestText(request)
           << L". Actual " << FormatInfoText(actual);
        const auto msgw = ss.str();
        throw std::runtime_error(WideToUtf8(msgw));
    }

    if (!IsSupportedCpuUploadInputFormat(actual.dxgiFormat)) {
        throw std::runtime_error("MFVideoFileSampleReader::configureExactDecodedFormat: exact decoded output is unsupported by CPU upload path");
    }

    selectedFormat_ = actual;
}

MFCpuSampleReadResult MFVideoFileSampleReader::read() {
    MFCpuSampleReadResult result;
    if (!reader_) {
        result.status = MFFrameStatus::NotOpened;
        result.error = MakeError(L"MFVideoFileSampleReader::read", L"reader is not opened");
        return result;
    }

    try {
        for (int attempt = 0; attempt < 8; ++attempt) {
            DWORD streamIndex = 0;
            DWORD flags = 0;
            LONGLONG sampleTime = -1;
            ComPtr<IMFSample> sample;
            HRESULT hr = reader_->ReadSample(kFirstVideoStream, 0,
                                              &streamIndex, &flags, &sampleTime, &sample);
            ThrowIfFailed(hr, L"IMFSourceReader::ReadSample(video)");

            if (flags & MF_SOURCE_READERF_ENDOFSTREAM) {
                result.status = MFFrameStatus::EndOfStream;
                return result;
            }
            if (flags & MF_SOURCE_READERF_ERROR) {
                result.status = MFFrameStatus::Error;
                result.error = MakeError(L"MFVideoFileSampleReader::read", L"MF_SOURCE_READERF_ERROR was returned");
                return result;
            }
            if (flags & MF_SOURCE_READERF_CURRENTMEDIATYPECHANGED) {
                const MFCameraFormatInfo changed = ReadCurrentVideoFormat(reader_.Get(), L"GetCurrentMediaType(after CURRENTMEDIATYPECHANGED)");
                if (!IsCompatibleDecodedFormatAfterChange(selectedFormat_, changed)) {
                    std::wstringstream ss;
                    ss << L"current media type changed to an incompatible format. Previous "
                       << FormatInfoText(selectedFormat_)
                       << L". Current " << FormatInfoText(changed);
                    result.status = MFFrameStatus::FormatChanged;
                    result.error = MakeError(L"MFVideoFileSampleReader::read", ss.str());
                    return result;
                }

                // Some decoders adjust non-layout metadata such as frame rate after the first read.
                // Width / height / subtype remain compatible with the upload path, so continue.
                selectedFormat_ = changed;
                if (!sample) {
                    continue;
                }
            }
            if (!sample) {
                result.status = MFFrameStatus::NoSample;
                return result;
            }

            LONGLONG duration = -1;
            sample->GetSampleDuration(&duration);
            result.sample = lockSample(sample.Get(), sampleTime, duration);
            result.status = MFFrameStatus::Ok;
            return result;
        }

        result.status = MFFrameStatus::NoSample;
        return result;
    } catch (const HResultException& e) {
        result.status = MFFrameStatus::Error;
        result.error = MakeError(e.hr(), e.where());
        return result;
    } catch (const std::exception& e) {
        result.status = MFFrameStatus::Error;
        result.error = MakeError(L"MFVideoFileSampleReader::read", Utf8ToWide(e.what()));
        return result;
    }
}

bool MFVideoFileSampleReader::seek(std::int64_t position100ns) {
    if (!reader_) return false;
    if (position100ns < 0) position100ns = 0;
    PROPVARIANT var;
    PropVariantInit(&var);
    var.vt = VT_I8;
    var.hVal.QuadPart = position100ns;
    const HRESULT hr = reader_->SetCurrentPosition(GUID_NULL, var);
    PropVariantClear(&var);
    return SUCCEEDED(hr);
}

MFCpuVideoSample MFVideoFileSampleReader::lockSample(IMFSample* sample, LONGLONG sampleTime, LONGLONG sampleDuration) {
    // Same layout handling as camera samples.
    MFCpuVideoSample out;
    out.sample_ = sample;
    out.dxgiFormat = selectedFormat_.dxgiFormat;
    out.mfSubtype = selectedFormat_.subtype;
    out.width = selectedFormat_.width;
    out.height = selectedFormat_.height;
    out.sampleTime100ns = sampleTime;
    out.sampleDuration100ns = sampleDuration;
    out.acquiredTime = std::chrono::steady_clock::now();

    DWORD bufferCount = 0;
    ThrowIfFailed(sample->GetBufferCount(&bufferCount), L"IMFSample::GetBufferCount(video)");
    if (bufferCount == 0) throw std::runtime_error("video sample has no media buffer");

    if (bufferCount == 1) {
        ThrowIfFailed(sample->ConvertToContiguousBuffer(&out.buffer_), L"IMFSample::ConvertToContiguousBuffer(video)");
        if (SUCCEEDED(out.buffer_.As(&out.buffer2d_))) {
            BYTE* scan0 = nullptr;
            LONG pitch = 0;
            ThrowIfFailed(out.buffer2d_->Lock2D(&scan0, &pitch), L"IMF2DBuffer::Lock2D(video)");
            out.locked2d_ = true;
            out.lockPtr_ = scan0;
            out.lockPitch_ = pitch;
        } else {
            BYTE* data = nullptr;
            DWORD maxLen = 0, curLen = 0;
            ThrowIfFailed(out.buffer_->Lock(&data, &maxLen, &curLen), L"IMFMediaBuffer::Lock(video)");
            out.lockedMediaBuffer_ = true;
            out.lockPtr_ = data;
            out.lockPitch_ = static_cast<LONG>(RowBytes(out.dxgiFormat, out.width));
            out.lockMaxLen_ = maxLen;
            out.lockCurLen_ = curLen;
        }
        if (out.lockPitch_ < 0) throw std::runtime_error("negative video sample pitch is not supported");
        const UINT64 rowPitch = static_cast<UINT64>(out.lockPitch_);
        out.planes[0] = MakePlaneView(out.lockPtr_, rowPitch, PlaneRows(out.dxgiFormat, 0, out.height));
        out.planeCount = 1;
        if (out.dxgiFormat == DXGI_FORMAT_NV12 || out.dxgiFormat == DXGI_FORMAT_P010) {
            out.planes[1] = MakePlaneView(
                out.lockPtr_ + static_cast<std::ptrdiff_t>(out.lockPitch_) * out.height,
                rowPitch,
                PlaneRows(out.dxgiFormat, 1, out.height));
            out.planeCount = 2;
        }
        out.valid_ = true;
        return out;
    }

    out.owned_.clear();
    out.owned_.reserve(static_cast<size_t>(RowBytes(out.dxgiFormat, out.width)) * out.height * 3 / 2);
    for (DWORD b = 0; b < bufferCount; ++b) {
        ComPtr<IMFMediaBuffer> mb;
        ThrowIfFailed(sample->GetBufferByIndex(b, &mb), L"IMFSample::GetBufferByIndex(video)");
        ComPtr<IMF2DBuffer> b2d;
        if (SUCCEEDED(mb.As(&b2d))) {
            BYTE* scan0 = nullptr;
            LONG pitch = 0;
            ThrowIfFailed(b2d->Lock2D(&scan0, &pitch), L"IMF2DBuffer::Lock2D(video split)");
            if (pitch < 0) {
                b2d->Unlock2D();
                throw std::runtime_error("negative video split sample pitch is not supported");
            }
            const UINT rows = (b == 0) ? PlaneRows(out.dxgiFormat, 0, out.height) : PlaneRows(out.dxgiFormat, 1, out.height);
            CopyPitchedRows(out.owned_, scan0, pitch, RowBytes(out.dxgiFormat, out.width), rows);
            b2d->Unlock2D();
        } else {
            BYTE* data = nullptr;
            DWORD maxLen = 0, curLen = 0;
            ThrowIfFailed(mb->Lock(&data, &maxLen, &curLen), L"IMFMediaBuffer::Lock(video split)");
            out.owned_.insert(out.owned_.end(), data, data + curLen);
            mb->Unlock();
        }
    }

    out.lockPtr_ = out.owned_.data();
    out.lockPitch_ = static_cast<LONG>(RowBytes(out.dxgiFormat, out.width));
    const UINT64 tightPitch = static_cast<UINT64>(out.lockPitch_);
    out.planes[0] = MakePlaneView(out.lockPtr_, tightPitch, PlaneRows(out.dxgiFormat, 0, out.height));
    out.planeCount = 1;
    if (out.dxgiFormat == DXGI_FORMAT_NV12 || out.dxgiFormat == DXGI_FORMAT_P010) {
        out.planes[1] = MakePlaneView(
            out.lockPtr_ + static_cast<size_t>(out.lockPitch_) * out.height,
            tightPitch,
            PlaneRows(out.dxgiFormat, 1, out.height));
        out.planeCount = 2;
    }
    out.valid_ = true;
    return out;
}

void MFVideoFileSampleReader::close() noexcept {
    reader_.Reset();
    filePath_.clear();
    selectedFormat_ = {};
    duration100ns_ = -1;
}

} // namespace MFFrameSource::internal
