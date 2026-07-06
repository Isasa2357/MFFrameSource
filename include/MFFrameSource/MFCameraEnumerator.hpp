#pragma once

#include "MFCommon.hpp"

namespace MFFrameSource {

class MFCameraEnumerator {
public:
    static std::vector<MFCameraDeviceInfo> enumerateDevices();
    static ComPtr<IMFActivate> resolveActivate(const MFCameraSelector& selector);
    static std::vector<MFCameraFormatInfo> enumerateFormats(const MFCameraSelector& selector);
};

} // namespace MFFrameSource
