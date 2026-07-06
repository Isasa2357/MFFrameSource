#include <MFFrameSource/MFCommon.hpp>

namespace MFFrameSource {

DXGI_FORMAT MfSubtypeToDxgiFormat(const GUID& subtype) noexcept {
    if (IsEqualGUID(subtype, MFVideoFormat_NV12)) return DXGI_FORMAT_NV12;
    if (IsEqualGUID(subtype, MFVideoFormat_P010)) return DXGI_FORMAT_P010;
    if (IsEqualGUID(subtype, MFVideoFormat_RGB32)) return DXGI_FORMAT_B8G8R8A8_UNORM;
    if (IsEqualGUID(subtype, MFVideoFormat_ARGB32)) return DXGI_FORMAT_B8G8R8A8_UNORM;
#ifdef MFVideoFormat_ABGR32
    if (IsEqualGUID(subtype, MFVideoFormat_ABGR32)) return DXGI_FORMAT_R8G8B8A8_UNORM;
#endif
    return DXGI_FORMAT_UNKNOWN;
}

const wchar_t* DxgiFormatName(DXGI_FORMAT format) noexcept {
    switch (format) {
    case DXGI_FORMAT_NV12: return L"NV12";
    case DXGI_FORMAT_P010: return L"P010";
    case DXGI_FORMAT_R8G8B8A8_UNORM: return L"R8G8B8A8_UNORM";
    case DXGI_FORMAT_B8G8R8A8_UNORM: return L"B8G8R8A8_UNORM";
    default: return L"UNKNOWN";
    }
}

bool IsSupportedCpuUploadInputFormat(DXGI_FORMAT format) noexcept {
    return format == DXGI_FORMAT_NV12 ||
           format == DXGI_FORMAT_P010 ||
           format == DXGI_FORMAT_R8G8B8A8_UNORM ||
           format == DXGI_FORMAT_B8G8R8A8_UNORM;
}

UINT DxgiPlaneCount(DXGI_FORMAT format) noexcept {
    switch (format) {
    case DXGI_FORMAT_NV12:
    case DXGI_FORMAT_P010:
        return 2;
    case DXGI_FORMAT_R8G8B8A8_UNORM:
    case DXGI_FORMAT_B8G8R8A8_UNORM:
        return 1;
    default:
        return 0;
    }
}

UINT64 ExpectedTightImageBytes(DXGI_FORMAT format, UINT width, UINT height) noexcept {
    switch (format) {
    case DXGI_FORMAT_NV12:
        return static_cast<UINT64>(width) * height + static_cast<UINT64>(width) * (height / 2);
    case DXGI_FORMAT_P010:
        return static_cast<UINT64>(width) * height * 2 + static_cast<UINT64>(width) * (height / 2) * 2;
    case DXGI_FORMAT_R8G8B8A8_UNORM:
    case DXGI_FORMAT_B8G8R8A8_UNORM:
        return static_cast<UINT64>(width) * height * 4;
    default:
        return 0;
    }
}

} // namespace MFFrameSource
