#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <Windows.h>
#include <dxgiformat.h>
#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <wrl/client.h>

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace MFFrameSource {

using Microsoft::WRL::ComPtr;

enum class MFFrameStatus {
    Ok,
    EndOfStream,
    NotOpened,
    DeviceLost,
    FormatChanged,
    UnsupportedFormat,
    NoSample,
    Error,
};

struct MFErrorInfo {
    HRESULT hr = S_OK;
    std::wstring where;
    std::wstring message;

    explicit operator bool() const noexcept { return FAILED(hr) || !message.empty(); }

    void clear() noexcept {
        hr = S_OK;
        where.clear();
        message.clear();
    }
};

struct MFFrameRate {
    std::uint32_t numerator = 0;
    std::uint32_t denominator = 1;

    bool isValid() const noexcept { return numerator != 0 && denominator != 0; }
};

inline bool operator==(const MFFrameRate& a, const MFFrameRate& b) noexcept {
    return a.numerator == b.numerator && a.denominator == b.denominator;
}

struct MFCameraSelector {
    // deviceIndex >= 0 を優先。未指定なら symbolicLink、次に friendlyName で解決する。
    int deviceIndex = -1;
    std::wstring symbolicLink;
    std::wstring friendlyName;
};

struct MFCameraDeviceInfo {
    int index = -1;
    std::wstring friendlyName;
    std::wstring symbolicLink;
};

struct MFCameraFormatInfo {
    GUID subtype = GUID_NULL;
    DXGI_FORMAT dxgiFormat = DXGI_FORMAT_UNKNOWN;
    UINT width = 0;
    UINT height = 0;
    MFFrameRate fps;
};

struct MFCameraFormatRequest {
    // 初期実装は exact match。subtype / width / height / fps を全て指定する。
    GUID subtype = GUID_NULL;
    UINT width = 0;
    UINT height = 0;
    MFFrameRate fps;

    bool isComplete() const noexcept {
        return subtype != GUID_NULL && width != 0 && height != 0 && fps.isValid();
    }
};

struct MFCameraCaptureConfig {
    MFCameraFormatRequest input;

    // 0 の場合は input.width / input.height を使う。
    UINT outputWidth = 0;
    UINT outputHeight = 0;
    DXGI_FORMAT outputFormat = DXGI_FORMAT_R8G8B8A8_UNORM;

    // D3D12Processing shader directory。空なら D3D12Helper 側の既定探索に任せる。
    std::wstring processingShaderDirectory;

    // CameraCapture::read 用の最小 frame pool。利用者が frame を保持したまま
    // 複数回 read しても破壊しない。GPU 完了は frame の ready fence で管理する。
    std::size_t framePoolSize = 4;

    // true の場合、read()/clone() は GPU 完了まで CPU wait してから返す。
    // false の場合、frame は ready fence/value を持った非同期 frame として返る。
    bool waitForGpuCompletionOnRead = false;

    // UploadRing size。0 の場合は input texture の必要 upload size と framePoolSize から自動計算する。
    std::uint64_t uploadRingSizeBytes = 0;

    // Descriptor allocator capacity。D3D12Processing は dispatch ごとに transient descriptor を使う。
    UINT transientCbvSrvUavDescriptorCount = 256;
    UINT transientSamplerDescriptorCount = 16;
    UINT persistentSrvDescriptorCount = 64;
};

// Supported initial CPU sample formats for D3D12-first path.
DXGI_FORMAT MfSubtypeToDxgiFormat(const GUID& subtype) noexcept;
const wchar_t* DxgiFormatName(DXGI_FORMAT format) noexcept;
bool IsSupportedCpuUploadInputFormat(DXGI_FORMAT format) noexcept;
UINT DxgiPlaneCount(DXGI_FORMAT format) noexcept;
UINT64 ExpectedTightImageBytes(DXGI_FORMAT format, UINT width, UINT height) noexcept;

} // namespace MFFrameSource
