#include "TestHarness.hpp"

#include <MFFrameSource/MFD3D12VideoCaptureThread.hpp>

#include <D3D12Helper/D3D12Core/D3D12Core.hpp>

#include <chrono>
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
    if (_wdupenv_s(&raw, &len, name) != 0 || raw == nullptr) return {};
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
           !GetEnvString(L"MFFRAMESOURCE_D3D12_PROCESSING_SHADER_DIR").empty();
}

unsigned ParseUIntEnv(const wchar_t* name) {
    const std::wstring value = GetEnvString(name);
    if (value.empty()) throw std::runtime_error("missing required video smoke-test environment variable");
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
        std::cout << "SKIP: set MFFRAMESOURCE_TEST_VIDEO_* env vars to run video thread smoke test.\n";
        return 0;
    }

    MFD3D12VideoCaptureThreadConfig cfg;
    cfg.filePath = GetEnvString(L"MFFRAMESOURCE_TEST_VIDEO_PATH");
    cfg.capture.input.width = ParseUIntEnv(L"MFFRAMESOURCE_TEST_VIDEO_WIDTH");
    cfg.capture.input.height = ParseUIntEnv(L"MFFRAMESOURCE_TEST_VIDEO_HEIGHT");
    cfg.capture.input.fps.numerator = ParseUIntEnv(L"MFFRAMESOURCE_TEST_VIDEO_FPS_NUM");
    cfg.capture.input.fps.denominator = ParseUIntEnv(L"MFFRAMESOURCE_TEST_VIDEO_FPS_DEN");
    cfg.capture.input.subtype = ParseSubtype(GetEnvString(L"MFFRAMESOURCE_TEST_VIDEO_SUBTYPE"));
    cfg.capture.outputWidth = cfg.capture.input.width;
    cfg.capture.outputHeight = cfg.capture.input.height;
    cfg.capture.processingShaderDirectory = GetEnvString(L"MFFRAMESOURCE_D3D12_PROCESSING_SHADER_DIR");
    cfg.capture.framePoolSize = 3;
    cfg.playbackMode = MFVideoPlaybackMode::DecodeAsFastAsPossible;

    D3D12CoreLib::D3D12CoreConfig d3dCfg;
    d3dCfg.enableDebugLayer = true;
    d3dCfg.allowWarpAdapter = true;
    auto core = D3D12CoreLib::D3D12Core::CreateShared(d3dCfg);

    MFD3D12VideoCaptureThread thread;
    if (!thread.open(cfg, core)) {
        const auto& e = thread.lastError();
        std::wcerr << L"open failed: " << e.where << L": " << e.message << L"\n";
        MFTEST_CHECK(false);
    }
    auto q = thread.createQueue(2);
    thread.start();

    auto item = q->waitPopFor(std::chrono::seconds(5));
    MFTEST_CHECK(static_cast<bool>(item));
    item->waitReady();
    MFTEST_CHECK(item->isReady());
    MFTEST_CHECK_EQ(item->width(), cfg.capture.outputWidth);
    MFTEST_CHECK_EQ(item->height(), cfg.capture.outputHeight);

    thread.stop();
    auto s = thread.stats();
    MFTEST_CHECK(s.framesRead >= 1ull);
    MFTEST_CHECK(s.framesDelivered >= 1ull);
    thread.close();
})
