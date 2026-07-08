#include "TestHarness.hpp"

#include <MFFrameSource/MFD3D11VideoCaptureThread.hpp>

#include <D3D11Helper/D3D11Core/D3D11Core.hpp>

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
           !GetEnvString(L"MFFRAMESOURCE_D3D11_PROCESSING_SHADER_DIR").empty();
}

unsigned ParseUIntEnv(const wchar_t* name) {
    const std::wstring value = GetEnvString(name);
    if (value.empty()) throw std::runtime_error("missing required video thread smoke-test environment variable");
    return static_cast<unsigned>(std::stoul(value));
}

GUID ParseSubtype(const std::wstring& value) {
    if (value == L"NV12") return MFVideoFormat_NV12;
    if (value == L"P010") return MFVideoFormat_P010;
    if (value == L"RGB32") return MFVideoFormat_RGB32;
    if (value == L"ARGB32") return MFVideoFormat_ARGB32;
    throw std::runtime_error("MFFRAMESOURCE_TEST_VIDEO_SUBTYPE must be NV12, P010, RGB32, or ARGB32");
}

void PrintLastError(const MFD3D11VideoCaptureThread& thread) {
    const auto& e = thread.lastError();
    if (e) {
        std::wcerr << L"lastError: " << e.where << L": " << e.message << L"\n";
    }
}

} // namespace

MFTEST_MAIN({
    if (!HasRequiredSmokeEnv()) {
        std::cout << "SKIP: set MFFRAMESOURCE_TEST_VIDEO_* and MFFRAMESOURCE_D3D11_PROCESSING_SHADER_DIR env vars to run D3D11 video thread smoke test.\n";
        return 0;
    }

    MFD3D11VideoCaptureThreadConfig cfg;
    cfg.filePath = GetEnvString(L"MFFRAMESOURCE_TEST_VIDEO_PATH");
    cfg.capture.input.width = ParseUIntEnv(L"MFFRAMESOURCE_TEST_VIDEO_WIDTH");
    cfg.capture.input.height = ParseUIntEnv(L"MFFRAMESOURCE_TEST_VIDEO_HEIGHT");
    cfg.capture.input.fps.numerator = ParseUIntEnv(L"MFFRAMESOURCE_TEST_VIDEO_FPS_NUM");
    cfg.capture.input.fps.denominator = ParseUIntEnv(L"MFFRAMESOURCE_TEST_VIDEO_FPS_DEN");
    cfg.capture.input.subtype = ParseSubtype(GetEnvString(L"MFFRAMESOURCE_TEST_VIDEO_SUBTYPE"));
    cfg.capture.outputWidth = cfg.capture.input.width;
    cfg.capture.outputHeight = cfg.capture.input.height;
    cfg.capture.processingShaderDirectory = GetEnvString(L"MFFRAMESOURCE_D3D11_PROCESSING_SHADER_DIR");
    cfg.capture.framePoolSize = 3;
    cfg.defaultQueueCapacity = 2;
    cfg.clonePoolSize = 4;
    cfg.playbackMode = MFVideoPlaybackMode::DecodeAsFastAsPossible;

    D3D11CoreLib::D3D11CoreConfig d3dCfg;
    d3dCfg.enableDebugLayer = true;
    d3dCfg.enableInfoQueue = true;
    d3dCfg.allowWarpAdapter = true;
    d3dCfg.enableMultithreadProtection = true;
    auto core = D3D11CoreLib::D3D11Core::CreateShared(d3dCfg);

    MFD3D11VideoCaptureThread thread;
    if (!thread.open(cfg, core)) {
        PrintLastError(thread);
        MFTEST_CHECK(false);
    }
    MFTEST_CHECK(thread.isOpened());

    auto q = thread.createQueue(2);
    thread.start();

    auto frame = q->waitPopFor(std::chrono::seconds(10));
    thread.stop();
    thread.rethrowWorkerExceptionIfAny();

    if (!frame.has_value()) {
        const auto stats = thread.stats();
        std::wcerr << L"timeout waiting for D3D11 video thread frame. framesRead=" << stats.framesRead
                   << L" framesDelivered=" << stats.framesDelivered
                   << L" eos=" << stats.endOfStreamCount
                   << L" readFailures=" << stats.readFailures
                   << L" cloneFailures=" << stats.cloneFailures << L"\n";
        PrintLastError(thread);
        MFTEST_CHECK(false);
    }

    MFTEST_CHECK(*frame);
    frame->waitReady();
    MFTEST_CHECK(frame->isReady());
    MFTEST_CHECK(frame->resource().Get() != nullptr);
    MFTEST_CHECK(frame->srv() != nullptr);
    MFTEST_CHECK_EQ(frame->width(), cfg.capture.outputWidth);
    MFTEST_CHECK_EQ(frame->height(), cfg.capture.outputHeight);
    MFTEST_CHECK_EQ(frame->format(), DXGI_FORMAT_R8G8B8A8_UNORM);

    auto stats = thread.stats();
    MFTEST_CHECK(stats.framesRead >= 1ull);
    MFTEST_CHECK(stats.framesDelivered >= 1ull);
    thread.close();
    MFTEST_CHECK(!thread.isOpened());
})
