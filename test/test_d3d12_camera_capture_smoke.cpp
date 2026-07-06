#include "TestHarness.hpp"

#include <MFFrameSource/MFD3D12CameraCapture.hpp>

#include <D3D12Helper/D3D12Core/D3D12Core.hpp>

#include <cstdlib>
#include <iostream>
#include <memory>
#include <string>

using namespace MFFrameSource;

namespace {

std::wstring GetEnvString(const wchar_t* name) {
#if defined(_WIN32)
    wchar_t* raw = nullptr;
    size_t len = 0;
    if (_wdupenv_s(&raw, &len, name) != 0 || raw == nullptr) {
        return {};
    }
    std::wstring value(raw);
    std::free(raw);
    return value;
#else
    (void)name;
    return {};
#endif
}

bool HasRequiredSmokeEnv() {
    return !GetEnvString(L"MFFRAMESOURCE_TEST_CAMERA_INDEX").empty() &&
           !GetEnvString(L"MFFRAMESOURCE_TEST_WIDTH").empty() &&
           !GetEnvString(L"MFFRAMESOURCE_TEST_HEIGHT").empty() &&
           !GetEnvString(L"MFFRAMESOURCE_TEST_FPS_NUM").empty() &&
           !GetEnvString(L"MFFRAMESOURCE_TEST_FPS_DEN").empty() &&
           !GetEnvString(L"MFFRAMESOURCE_TEST_SUBTYPE").empty() &&
           !GetEnvString(L"MFFRAMESOURCE_D3D12_PROCESSING_SHADER_DIR").empty();
}

unsigned ParseUIntEnv(const wchar_t* name) {
    const std::wstring value = GetEnvString(name);
    if (value.empty()) {
        throw std::runtime_error("missing required smoke-test environment variable");
    }
    return static_cast<unsigned>(std::stoul(value));
}

GUID ParseSubtype(const std::wstring& value) {
    if (value == L"NV12") return MFVideoFormat_NV12;
    if (value == L"P010") return MFVideoFormat_P010;
    if (value == L"RGB32") return MFVideoFormat_RGB32;
    if (value == L"ARGB32") return MFVideoFormat_ARGB32;
    throw std::runtime_error("MFFRAMESOURCE_TEST_SUBTYPE must be NV12, P010, RGB32, or ARGB32");
}

} // namespace

MFTEST_MAIN({
    if (!HasRequiredSmokeEnv()) {
        std::cout << "SKIP: set MFFRAMESOURCE_TEST_CAMERA_INDEX, WIDTH, HEIGHT, FPS_NUM, FPS_DEN, SUBTYPE, and MFFRAMESOURCE_D3D12_PROCESSING_SHADER_DIR to run camera smoke test.\n";
        return 0;
    }

    MFCameraSelector selector;
    selector.deviceIndex = static_cast<int>(ParseUIntEnv(L"MFFRAMESOURCE_TEST_CAMERA_INDEX"));

    MFCameraCaptureConfig cfg;
    cfg.input.width = ParseUIntEnv(L"MFFRAMESOURCE_TEST_WIDTH");
    cfg.input.height = ParseUIntEnv(L"MFFRAMESOURCE_TEST_HEIGHT");
    cfg.input.fps.numerator = ParseUIntEnv(L"MFFRAMESOURCE_TEST_FPS_NUM");
    cfg.input.fps.denominator = ParseUIntEnv(L"MFFRAMESOURCE_TEST_FPS_DEN");
    cfg.input.subtype = ParseSubtype(GetEnvString(L"MFFRAMESOURCE_TEST_SUBTYPE"));
    cfg.outputWidth = cfg.input.width;
    cfg.outputHeight = cfg.input.height;
    cfg.processingShaderDirectory = GetEnvString(L"MFFRAMESOURCE_D3D12_PROCESSING_SHADER_DIR");
    cfg.framePoolSize = 3;

    D3D12CoreLib::D3D12CoreConfig d3dCfg;
    d3dCfg.enableDebugLayer = true;
    d3dCfg.allowWarpAdapter = true;
    auto core = D3D12CoreLib::D3D12Core::CreateShared(d3dCfg);

    MFD3D12CameraCapture capture;
    if (!capture.open(selector, cfg, core)) {
        const auto& e = capture.lastError();
        std::wcerr << L"open failed: " << e.where << L": " << e.message << L"\n";
        MFTEST_CHECK(false);
    }
    MFTEST_CHECK(capture.isOpened());

    auto result = capture.read();
    if (!result.ok()) {
        std::wcerr << L"read failed status=" << static_cast<int>(result.status)
                   << L" " << result.error.where << L": " << result.error.message << L"\n";
        MFTEST_CHECK(false);
    }

    MFTEST_CHECK(result.frame);
    result.frame.waitReady();
    MFTEST_CHECK(result.frame.isReady());
    MFTEST_CHECK(result.frame.resource().Get() != nullptr);
    MFTEST_CHECK_EQ(result.frame.width(), cfg.outputWidth);
    MFTEST_CHECK_EQ(result.frame.height(), cfg.outputHeight);
    MFTEST_CHECK_EQ(result.frame.format(), DXGI_FORMAT_R8G8B8A8_UNORM);
    MFTEST_CHECK(result.frame.srv().IsValid());
    MFTEST_CHECK(result.frame.srvDescriptorHeap() != nullptr);

    capture.close();
    MFTEST_CHECK(!capture.isOpened());
})
