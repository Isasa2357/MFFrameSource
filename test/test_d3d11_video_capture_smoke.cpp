#include "TestHarness.hpp"

#include <MFFrameSource/MFD3D11VideoCapture.hpp>

#include <D3D11Helper/D3D11Core/D3D11Core.hpp>

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
    return !GetEnvString(L"MFFRAMESOURCE_TEST_VIDEO_PATH").empty() &&
           !GetEnvString(L"MFFRAMESOURCE_TEST_VIDEO_WIDTH").empty() &&
           !GetEnvString(L"MFFRAMESOURCE_TEST_VIDEO_HEIGHT").empty() &&
           !GetEnvString(L"MFFRAMESOURCE_TEST_VIDEO_FPS_NUM").empty() &&
           !GetEnvString(L"MFFRAMESOURCE_TEST_VIDEO_FPS_DEN").empty() &&
           !GetEnvString(L"MFFRAMESOURCE_TEST_VIDEO_SUBTYPE").empty() &&
           !GetEnvString(L"MFFRAMESOURCE_D3D11_PROCESSING_SHADER_DIR").empty();
}

unsigned ParseUIntEnv(const wchar_t* name) {
    const std::wstring value = GetEnvString(name);
    if (value.empty()) {
        throw std::runtime_error("missing required video smoke-test environment variable");
    }
    return static_cast<unsigned>(std::stoul(value));
}

GUID ParseSubtype(const std::wstring& value) {
    if (value == L"NV12") return MFVideoFormat_NV12;
    if (value == L"P010") return MFVideoFormat_P010;
    if (value == L"RGB32") return MFVideoFormat_RGB32;
    if (value == L"ARGB32") return MFVideoFormat_ARGB32;
    throw std::runtime_error("MFFRAMESOURCE_TEST_VIDEO_SUBTYPE must be NV12, P010, RGB32, or ARGB32");
}

} // namespace

MFTEST_MAIN({
    if (!HasRequiredSmokeEnv()) {
        std::cout << "SKIP: set MFFRAMESOURCE_TEST_VIDEO_PATH, MFFRAMESOURCE_TEST_VIDEO_WIDTH, MFFRAMESOURCE_TEST_VIDEO_HEIGHT, MFFRAMESOURCE_TEST_VIDEO_FPS_NUM, MFFRAMESOURCE_TEST_VIDEO_FPS_DEN, MFFRAMESOURCE_TEST_VIDEO_SUBTYPE, and MFFRAMESOURCE_D3D11_PROCESSING_SHADER_DIR to run D3D11 video smoke test.\n";
        return 0;
    }

    MFVideoCaptureConfig cfg;
    cfg.input.width = ParseUIntEnv(L"MFFRAMESOURCE_TEST_VIDEO_WIDTH");
    cfg.input.height = ParseUIntEnv(L"MFFRAMESOURCE_TEST_VIDEO_HEIGHT");
    cfg.input.fps.numerator = ParseUIntEnv(L"MFFRAMESOURCE_TEST_VIDEO_FPS_NUM");
    cfg.input.fps.denominator = ParseUIntEnv(L"MFFRAMESOURCE_TEST_VIDEO_FPS_DEN");
    cfg.input.subtype = ParseSubtype(GetEnvString(L"MFFRAMESOURCE_TEST_VIDEO_SUBTYPE"));
    cfg.outputWidth = cfg.input.width;
    cfg.outputHeight = cfg.input.height;
    cfg.processingShaderDirectory = GetEnvString(L"MFFRAMESOURCE_D3D11_PROCESSING_SHADER_DIR");
    cfg.framePoolSize = 3;

    D3D11CoreLib::D3D11CoreConfig d3dCfg;
    d3dCfg.enableDebugLayer = true;
    d3dCfg.enableInfoQueue = true;
    d3dCfg.allowWarpAdapter = true;
    auto core = D3D11CoreLib::D3D11Core::CreateShared(d3dCfg);

    MFD3D11VideoCapture capture;
    if (!capture.open(GetEnvString(L"MFFRAMESOURCE_TEST_VIDEO_PATH"), cfg, core)) {
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
    MFTEST_CHECK(result.frame.srv() != nullptr);
    MFTEST_CHECK_EQ(result.frame.width(), cfg.outputWidth);
    MFTEST_CHECK_EQ(result.frame.height(), cfg.outputHeight);
    MFTEST_CHECK_EQ(result.frame.format(), DXGI_FORMAT_R8G8B8A8_UNORM);

    capture.close();
    MFTEST_CHECK(!capture.isOpened());
})
