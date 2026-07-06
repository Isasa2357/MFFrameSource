#include <MFFrameSource/MFCameraEnumerator.hpp>

#include "internal/MFComUtil.hpp"

#include <algorithm>
#include <mferror.h>
#include <stdexcept>

namespace MFFrameSource {
namespace {

constexpr DWORD kFirstVideoStream = static_cast<DWORD>(MF_SOURCE_READER_FIRST_VIDEO_STREAM);

std::vector<ComPtr<IMFActivate>> EnumerateActivates() {
    ComPtr<IMFAttributes> attrs;
    internal::ThrowIfFailed(MFCreateAttributes(&attrs, 1), L"MFCreateAttributes(camera enum)");
    internal::ThrowIfFailed(attrs->SetGUID(MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE,
                                           MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_VIDCAP_GUID),
                            L"Set camera source type");

    IMFActivate** raw = nullptr;
    UINT32 count = 0;
    internal::ThrowIfFailed(MFEnumDeviceSources(attrs.Get(), &raw, &count), L"MFEnumDeviceSources");

    std::vector<ComPtr<IMFActivate>> result;
    result.reserve(count);
    for (UINT32 i = 0; i < count; ++i) {
        result.emplace_back(raw[i]);
        raw[i]->Release();
    }
    CoTaskMemFree(raw);
    return result;
}

MFCameraDeviceInfo InfoFromActivate(IMFActivate* act, int index) {
    MFCameraDeviceInfo info;
    info.index = index;
    info.friendlyName = internal::ReadAllocatedString(act, MF_DEVSOURCE_ATTRIBUTE_FRIENDLY_NAME);
    info.symbolicLink = internal::ReadAllocatedString(act, MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_VIDCAP_SYMBOLIC_LINK);
    return info;
}

} // namespace

std::vector<MFCameraDeviceInfo> MFCameraEnumerator::enumerateDevices() {
    auto activates = EnumerateActivates();
    std::vector<MFCameraDeviceInfo> infos;
    infos.reserve(activates.size());
    for (size_t i = 0; i < activates.size(); ++i) {
        infos.push_back(InfoFromActivate(activates[i].Get(), static_cast<int>(i)));
    }
    return infos;
}

ComPtr<IMFActivate> MFCameraEnumerator::resolveActivate(const MFCameraSelector& selector) {
    auto activates = EnumerateActivates();
    if (selector.deviceIndex >= 0) {
        const auto idx = static_cast<size_t>(selector.deviceIndex);
        if (idx >= activates.size()) {
            throw std::runtime_error("MFCameraEnumerator: deviceIndex out of range");
        }
        return activates[idx];
    }

    if (!selector.symbolicLink.empty()) {
        for (auto& act : activates) {
            const auto sym = internal::ReadAllocatedString(act.Get(), MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_VIDCAP_SYMBOLIC_LINK);
            if (sym == selector.symbolicLink) return act;
        }
        throw std::runtime_error("MFCameraEnumerator: symbolicLink not found");
    }

    if (!selector.friendlyName.empty()) {
        for (auto& act : activates) {
            const auto name = internal::ReadAllocatedString(act.Get(), MF_DEVSOURCE_ATTRIBUTE_FRIENDLY_NAME);
            if (name == selector.friendlyName) return act;
        }
        throw std::runtime_error("MFCameraEnumerator: friendlyName not found");
    }

    if (activates.empty()) {
        throw std::runtime_error("MFCameraEnumerator: no camera found");
    }
    return activates[0];
}

std::vector<MFCameraFormatInfo> MFCameraEnumerator::enumerateFormats(const MFCameraSelector& selector) {
    auto activate = resolveActivate(selector);
    ComPtr<IMFMediaSource> source;
    internal::ThrowIfFailed(activate->ActivateObject(IID_PPV_ARGS(&source)), L"ActivateObject(IMFMediaSource)");

    ComPtr<IMFAttributes> readerAttrs;
    internal::ThrowIfFailed(MFCreateAttributes(&readerAttrs, 2), L"MFCreateAttributes(source reader)");
    readerAttrs->SetUINT32(MF_READWRITE_DISABLE_CONVERTERS, TRUE);

    ComPtr<IMFSourceReader> reader;
    internal::ThrowIfFailed(MFCreateSourceReaderFromMediaSource(source.Get(), readerAttrs.Get(), &reader),
                            L"MFCreateSourceReaderFromMediaSource");

    std::vector<MFCameraFormatInfo> formats;
    for (DWORD i = 0;; ++i) {
        ComPtr<IMFMediaType> type;
        HRESULT hr = reader->GetNativeMediaType(kFirstVideoStream, i, &type);
        if (hr == MF_E_NO_MORE_TYPES) break;
        internal::ThrowIfFailed(hr, L"GetNativeMediaType");

        MFCameraFormatInfo f;
        type->GetGUID(MF_MT_SUBTYPE, &f.subtype);
        f.dxgiFormat = MfSubtypeToDxgiFormat(f.subtype);
        internal::ReadFrameSize(type.Get(), f.width, f.height);
        f.fps = internal::ReadFrameRate(type.Get());
        formats.push_back(f);
    }

    source->Shutdown();
    activate->ShutdownObject();
    return formats;
}

} // namespace MFFrameSource
