#include <MFFrameSource/MFPlatformContext.hpp>

#include "internal/MFComUtil.hpp"

namespace MFFrameSource {

MFPlatformContext::MFPlatformContext() {
    HRESULT hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    if (SUCCEEDED(hr)) {
        coInitialized_ = true;
    } else if (hr != RPC_E_CHANGED_MODE) {
        internal::ThrowIfFailed(hr, L"CoInitializeEx");
    }

    internal::ThrowIfFailed(MFStartup(MF_VERSION, MFSTARTUP_FULL), L"MFStartup");
    mfStarted_ = true;
    initialized_ = true;
}

MFPlatformContext::~MFPlatformContext() {
    if (mfStarted_) {
        MFShutdown();
    }
    if (coInitialized_) {
        CoUninitialize();
    }
}

} // namespace MFFrameSource
