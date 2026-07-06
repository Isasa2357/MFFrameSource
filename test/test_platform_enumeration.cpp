#include "TestHarness.hpp"

#include <MFFrameSource/MFCameraEnumerator.hpp>
#include <MFFrameSource/MFPlatformContext.hpp>

#include <iostream>

using namespace MFFrameSource;

MFTEST_MAIN({
    MFPlatformContext platform;
    MFTEST_CHECK(platform.initialized());

    const auto devices = MFCameraEnumerator::enumerateDevices();
    std::wcout << L"camera devices: " << devices.size() << L"\n";

    for (size_t i = 0; i < devices.size(); ++i) {
        MFTEST_CHECK_EQ(devices[i].index, static_cast<int>(i));
        std::wcout << L"[" << devices[i].index << L"] " << devices[i].friendlyName << L"\n";
    }

    if (!devices.empty()) {
        MFCameraSelector selector;
        selector.deviceIndex = devices.front().index;
        const auto formats = MFCameraEnumerator::enumerateFormats(selector);
        std::wcout << L"first camera formats: " << formats.size() << L"\n";
        for (const auto& f : formats) {
            MFTEST_CHECK(f.width > 0);
            MFTEST_CHECK(f.height > 0);
            MFTEST_CHECK(f.fps.denominator != 0);
            MFTEST_CHECK(f.subtype != GUID_NULL);
        }
    }
})
