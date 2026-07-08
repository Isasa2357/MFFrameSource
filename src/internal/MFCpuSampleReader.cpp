#include "MFCpuSampleReader.hpp"

#include <MFFrameSource/MFCameraEnumerator.hpp>

#include "MFComUtil.hpp"

#include <cstring>
#include <mferror.h>
#include <sstream>
#include <stdexcept>
#include <string>

namespace MFFrameSource::internal {
namespace {

constexpr DWORD kFirstVideoStream = static_cast<DWORD>(MF_SOURCE_READER_FIRST_VIDEO_STREAM);
constexpr DWORD kAllStreams = static_cast<DWORD>(MF_SOURCE_READER_ALL_STREAMS);

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

} // namespace

MFCpuVideoSample::~MFCpuVideoSample() {
    unlock();
}

MFCpuVideoSample::MFCpuVideoSample(MFCpuVideoSample&& other) noexcept {
    *this = std::move(other);
}

MFCpuVideoSample& MFCpuVideoSample::operator=(MFCpuVideoSample&& other) noexcept {
    if (this == &other) return *this;
    unlock();

    valid_ = other.valid_;
    dxgiFormat = other.dxgiFormat;
    mfSubtype = other.mfSubtype;
    width = other.width;
    height = other.height;
    sampleTime100ns = other.sampleTime100ns;
    sampleDuration100ns = other.sampleDuration100ns;
    acquiredTime = other.acquiredTime;
    planes = other.planes;
    planeCount = other.planeCount;
    sample_ = std::move(other.sample_);
    buffer_ = std::move(other.buffer_);
    buffer2d_ = std::move(other.buffer2d_);
    lockPtr_ = other.lockPtr_;
    lockPitch_ = other.lockPitch_;
    lockMaxLen_ = other.lockMaxLen_;
    lockCurLen_ = other.lockCurLen_;
    locked2d_ = other.locked2d_;
    lockedMediaBuffer_ = other.lockedMediaBuffer_;
    owned_ = std::move(other.owned_);

    other.valid_ = false;
    other.lockPtr_ = nullptr;
    other.lockPitch_ = 0;
    other.lockMaxLen_ = 0;
    other.lockCurLen_ = 0;
    other.locked2d_ = false;
    other.lockedMediaBuffer_ = false;
    other.planeCount = 0;
    other.planes = {};
    return *this;
}

void MFCpuVideoSample::unlock() noexcept {
    if (locked2d_ && buffer2d_) {
        buffer2d_->Unlock2D();
    }
    if (lockedMediaBuffer_ && buffer_) {
        buffer_->Unlock();
    }
    locked2d_ = false;
    lockedMediaBuffer_ = false;
    lockPtr_ = nullptr;
    planes = {};
    planeCount = 0;
}

MFCpuSampleReader::~MFCpuSampleReader() {
    close();
}

void MFCpuSampleReader::open(const MFCameraSelector& selector, const MFCameraCaptureConfig& config) {
    close();
    if (!config.input.isComplete()) {
        throw std::runtime_error("MFCpuSampleReader::open: config.input must specify subtype, width, height, fps");
    }

    auto activate = MFCameraEnumerator::resolveActivate(selector);
    ThrowIfFailed(activate->ActivateObject(IID_PPV_ARGS(&source_)), L"ActivateObject(IMFMediaSource)");

    ComPtr<IMFAttributes> readerAttrs;
    ThrowIfFailed(MFCreateAttributes(&readerAttrs, 4), L"MFCreateAttributes(source reader)");
    // D3D12-first CPU upload path: do not attach a D3D manager. Keep exact native CPU samples.
    readerAttrs->SetUINT32(MF_READWRITE_DISABLE_CONVERTERS, TRUE);
    readerAttrs->SetUINT32(MF_READWRITE_ENABLE_HARDWARE_TRANSFORMS, FALSE);

    ThrowIfFailed(MFCreateSourceReaderFromMediaSource(source_.Get(), readerAttrs.Get(), &reader_),
                  L"MFCreateSourceReaderFromMediaSource");
    ThrowIfFailed(reader_->SetStreamSelection(kAllStreams, FALSE),
                  L"SetStreamSelection(all false)");
    ThrowIfFailed(reader_->SetStreamSelection(kFirstVideoStream, TRUE),
                  L"SetStreamSelection(video true)");

    selectExactNativeFormat(config.input);
}

void MFCpuSampleReader::selectExactNativeFormat(const MFCameraFormatRequest& request) {
    std::wstringstream available;
    available << L"Exact native camera format was not found. Requested width=" << request.width
              << L" height=" << request.height
              << L" fps=" << request.fps.numerator << L"/" << request.fps.denominator
              << L" subtypeDxgi=" << DxgiFormatName(MfSubtypeToDxgiFormat(request.subtype))
              << L". Available native formats:";
    bool any = false;

    for (DWORD i = 0;; ++i) {
        ComPtr<IMFMediaType> type;
        HRESULT hr = reader_->GetNativeMediaType(kFirstVideoStream, i, &type);
        if (hr == MF_E_NO_MORE_TYPES) break;
        ThrowIfFailed(hr, L"GetNativeMediaType");

        GUID subtype = GUID_NULL;
        type->GetGUID(MF_MT_SUBTYPE, &subtype);
        UINT width = 0, height = 0;
        ReadFrameSize(type.Get(), width, height);
        MFFrameRate fps = ReadFrameRate(type.Get());
        const DXGI_FORMAT dxgi = MfSubtypeToDxgiFormat(subtype);

        any = true;
        available << L" [" << i << L"] " << width << L"x" << height
                  << L" @ " << fps.numerator << L"/" << fps.denominator
                  << L" " << DxgiFormatName(dxgi);

        if (!GuidEquals(subtype, request.subtype)) continue;
        if (width != request.width || height != request.height) continue;
        if (!(fps == request.fps)) continue;

        if (!IsSupportedCpuUploadInputFormat(dxgi)) {
            throw std::runtime_error("MFCpuSampleReader::selectExactNativeFormat: exact format was found but is unsupported by CPU upload path");
        }

        ThrowIfFailed(reader_->SetCurrentMediaType(kFirstVideoStream, nullptr, type.Get()),
                      L"SetCurrentMediaType(exact native)");

        selectedFormat_.subtype = subtype;
        selectedFormat_.dxgiFormat = dxgi;
        selectedFormat_.width = width;
        selectedFormat_.height = height;
        selectedFormat_.fps = fps;
        return;
    }

    if (!any) available << L" <none>";
    const auto msgw = available.str();
    throw std::runtime_error(std::string("MFCpuSampleReader: ") + WideToUtf8(msgw));
}

MFCpuSampleReadResult MFCpuSampleReader::read() {
    MFCpuSampleReadResult result;
    if (!reader_) {
        result.status = MFFrameStatus::NotOpened;
        result.error = MakeError(L"MFCpuSampleReader::read", L"reader is not opened");
        return result;
    }

    try {
        DWORD streamIndex = 0;
        DWORD flags = 0;
        LONGLONG sampleTime = -1;
        ComPtr<IMFSample> sample;
        HRESULT hr = reader_->ReadSample(kFirstVideoStream, 0,
                                          &streamIndex, &flags, &sampleTime, &sample);
        ThrowIfFailed(hr, L"IMFSourceReader::ReadSample");

        if (flags & MF_SOURCE_READERF_ENDOFSTREAM) {
            result.status = MFFrameStatus::EndOfStream;
            return result;
        }
        if (flags & MF_SOURCE_READERF_CURRENTMEDIATYPECHANGED) {
            result.status = MFFrameStatus::FormatChanged;
            result.error = MakeError(L"MFCpuSampleReader::read", L"current media type changed");
            return result;
        }
        if (flags & MF_SOURCE_READERF_ERROR) {
            result.status = MFFrameStatus::Error;
            result.error = MakeError(L"MFCpuSampleReader::read", L"MF_SOURCE_READERF_ERROR was returned");
            return result;
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
    } catch (const HResultException& e) {
        result.status = MFFrameStatus::Error;
        result.error = MakeError(e.hr(), e.where());
        return result;
    } catch (const std::exception& e) {
        result.status = MFFrameStatus::Error;
        result.error = MakeError(L"MFCpuSampleReader::read", Utf8ToWide(e.what()));
        return result;
    }
}

MFCpuVideoSample MFCpuSampleReader::lockSample(IMFSample* sample, LONGLONG sampleTime, LONGLONG sampleDuration) {
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
    ThrowIfFailed(sample->GetBufferCount(&bufferCount), L"IMFSample::GetBufferCount");
    if (bufferCount == 0) {
        throw std::runtime_error("sample has no media buffer");
    }
    if (bufferCount == 1) {
        ThrowIfFailed(sample->ConvertToContiguousBuffer(&out.buffer_), L"IMFSample::ConvertToContiguousBuffer");
        if (SUCCEEDED(out.buffer_.As(&out.buffer2d_))) {
            BYTE* scan0 = nullptr;
            LONG pitch = 0;
            ThrowIfFailed(out.buffer2d_->Lock2D(&scan0, &pitch), L"IMF2DBuffer::Lock2D");
            out.locked2d_ = true;
            out.lockPtr_ = scan0;
            out.lockPitch_ = pitch;
        } else {
            BYTE* data = nullptr;
            DWORD maxLen = 0, curLen = 0;
            ThrowIfFailed(out.buffer_->Lock(&data, &maxLen, &curLen), L"IMFMediaBuffer::Lock");
            out.lockedMediaBuffer_ = true;
            out.lockPtr_ = data;
            out.lockPitch_ = static_cast<LONG>(RowBytes(out.dxgiFormat, out.width));
            out.lockMaxLen_ = maxLen;
            out.lockCurLen_ = curLen;
        }

        const UINT64 rowBytes = RowBytes(out.dxgiFormat, out.width);
        if (rowBytes == 0 || !out.lockPtr_) throw std::runtime_error("unsupported locked sample layout");
        if (out.lockPitch_ < 0) throw std::runtime_error("negative sample pitch is not supported");
        const UINT64 rowPitch = static_cast<UINT64>(out.lockPitch_);

        const UINT yRows = PlaneRows(out.dxgiFormat, 0, out.height);
        out.planes[0] = MakePlaneView(out.lockPtr_, rowPitch, yRows);
        out.planeCount = 1;

        if (out.dxgiFormat == DXGI_FORMAT_NV12 || out.dxgiFormat == DXGI_FORMAT_P010) {
            const UINT yRowsForOffset = out.height;
            std::uint8_t* uv = out.lockPtr_ + static_cast<std::ptrdiff_t>(out.lockPitch_) * yRowsForOffset;
            out.planes[1] = MakePlaneView(uv, rowPitch, PlaneRows(out.dxgiFormat, 1, out.height));
            out.planeCount = 2;
        }
        out.valid_ = true;
        return out;
    }

    // Fallback for samples split into multiple media buffers. Copy into a tight contiguous buffer.
    out.owned_.clear();
    out.owned_.reserve(static_cast<size_t>(RowBytes(out.dxgiFormat, out.width)) * out.height * 3 / 2);
    for (DWORD b = 0; b < bufferCount; ++b) {
        ComPtr<IMFMediaBuffer> mb;
        ThrowIfFailed(sample->GetBufferByIndex(b, &mb), L"IMFSample::GetBufferByIndex");
        ComPtr<IMF2DBuffer> b2d;
        if (SUCCEEDED(mb.As(&b2d))) {
            BYTE* scan0 = nullptr;
            LONG pitch = 0;
            ThrowIfFailed(b2d->Lock2D(&scan0, &pitch), L"IMF2DBuffer::Lock2D(split)");
            if (pitch < 0) {
                b2d->Unlock2D();
                throw std::runtime_error("negative split sample pitch is not supported");
            }
            const UINT rows = (b == 0) ? PlaneRows(out.dxgiFormat, 0, out.height) : PlaneRows(out.dxgiFormat, 1, out.height);
            CopyPitchedRows(out.owned_, scan0, pitch, RowBytes(out.dxgiFormat, out.width), rows);
            b2d->Unlock2D();
        } else {
            BYTE* data = nullptr;
            DWORD maxLen = 0, curLen = 0;
            ThrowIfFailed(mb->Lock(&data, &maxLen, &curLen), L"IMFMediaBuffer::Lock(split)");
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

void MFCpuSampleReader::close() noexcept {
    reader_.Reset();
    if (source_) {
        source_->Shutdown();
        source_.Reset();
    }
    selectedFormat_ = {};
}

} // namespace MFFrameSource::internal
