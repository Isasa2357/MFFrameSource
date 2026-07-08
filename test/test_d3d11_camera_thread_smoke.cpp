#include "TestHarness.hpp"

#include <MFFrameSource/MFD3D11CameraCaptureThread.hpp>

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

std::wstring GetEnvStringAny(const wchar_t* primary, const wchar_t* fallback) {
    auto value = GetEnvString(primary);
    if (!value.empty()) return value;
    return GetEnvString(fallback);
}

bool HasRequiredSmokeEnv() {
    return !GetEnvString(L"MFFRAMESOURCE_TEST_CAMERA_INDEX").empty() &&
           !GetEnvStringAny(L"MFFRAMESOURCE_TEST_CAMERA_WIDTH", L"MFFRAMESOURCE_TEST_WIDTH").empty() &&
           !GetEnvStringAny(L"MFFRAMESOURCE_TEST_CAMERA_HEIGHT", L"MFFRAMESOURCE_TEST_HEIGHT").empty() &&
           !GetEnvStringAny(L"MFFRAMESOURCE_TEST_CAMERA_FPS_NUM", L"MFFRAMESOURCE_TEST_FPS_NUM").empty() &&
           !GetEnvStringAny(L"MFFRAMESOURCE_TEST_CAMERA_FPS_DEN", L"MFFRAMESOURCE_TEST_FPS_DEN").empty() &&
           !GetEnvStringAny(L"MFFRAMESOURCE_TEST_CAMERA_SUBTYPE", L"MFFRAMESOURCE_TEST_SUBTYPE").empty() &&
           !GetEnvString(L"MFFRAMESOURCE_D3D11_PROCESSING_SHADER_DIR").empty();
}

unsigned ParseUIntEnvAny(const wchar_t* primary, const wchar_t* fallback) {
    const std::wstring value = GetEnvStringAny(primary, fallback);
    if (value.empty()) throw std::runtime_error("missing required camera thread smoke-test environment variable");
    return static_cast<unsigned>(std::stoul(value));
}

GUID ParseSubtype(const std::wstring& value) {
    if (value == L"NV12") return MFVideoFormat_NV12;
    if (value == L"P010") return MFVideoFormat_P010;
    if (value == L"RGB32") return MFVideoFormat_RGB32;
    if (value == L"ARGB32") return MFVideoFormat_ARGB32;
    throw std::runtime_error("camera subtype must be NV12, P010, RGB32, or ARGB32");
}

void PrintLastError(const MFD3D11CameraCaptureThread& thread) {
    const auto& e = thread.lastError();
    if (e) {
        std::wcerr << L"lastError: " << e.where << L": " << e.message << L"\n";
    }
}

} // namespace

MFTEST_MAIN({
    if (!HasRequiredSmokeEnv()) {
        std::cout << "SKIP: set MFFRAMESOURCE_TEST_CAMERA_INDEX, MFFRAMESOURCE_TEST_CAMERA_WIDTH, MFFRAMESOURCE_TEST_CAMERA_HEIGHT, MFFRAMESOURCE_TEST_CAMERA_FPS_NUM, MFFRAMESOURCE_TEST_CAMERA_FPS_DEN, MFFRAMESOURCE_TEST_CAMERA_SUBTYPE, and MFFRAMESOURCE_D3D11_PROCESSING_SHADER_DIR to run D3D11 camera thread smoke test.\n";
        return 0;
    }

    MFD3D11CameraCaptureThreadConfig cfg;
    cfg.selector.deviceIndex = static_cast<int>(ParseUIntEnvAny(L"MFFRAMESOURCE_TEST_CAMERA_INDEX", L"MFFRAMESOURCE_TEST_CAMERA_INDEX"));
    cfg.capture.input.width = ParseUIntEnvAny(L"MFFRAMESOURCE_TEST_CAMERA_WIDTH", L"MFFRAMESOURCE_TEST_WIDTH");
    cfg.capture.input.height = ParseUIntEnvAny(L"MFFRAMESOURCE_TEST_CAMERA_HEIGHT", L"MFFRAMESOURCE_TEST_HEIGHT");
    cfg.capture.input.fps.numerator = ParseUIntEnvAny(L"MFFRAMESOURCE_TEST_CAMERA_FPS_NUM", L"MFFRAMESOURCE_TEST_FPS_NUM");
    cfg.capture.input.fps.denominator = ParseUIntEnvAny(L"MFFRAMESOURCE_TEST_CAMERA_FPS_DEN", L"MFFRAMESOURCE_TEST_FPS_DEN");
    cfg.capture.input.subtype = ParseSubtype(GetEnvStringAny(L"MFFRAMESOURCE_TEST_CAMERA_SUBTYPE", L"MFFRAMESOURCE_TEST_SUBTYPE"));
    cfg.capture.outputWidth = cfg.capture.input.width;
    cfg.capture.outputHeight = cfg.capture.input.height;
    cfg.capture.processingShaderDirectory = GetEnvString(L"MFFRAMESOURCE_D3D11_PROCESSING_SHADER_DIR");
    cfg.capture.framePoolSize = 3;
    cfg.defaultQueueCapacity = 2;
    cfg.clonePoolSize = 4;

    D3D11CoreLib::D3D11CoreConfig d3dCfg;
    d3dCfg.enableDebugLayer = true;
    d3dCfg.enableInfoQueue = true;
    d3dCfg.allowWarpAdapter = true;
    d3dCfg.enableMultithreadProtection = true;
    auto core = D3D11CoreLib::D3D11Core::CreateShared(d3dCfg);

    MFD3D11CameraCaptureThread thread;
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
        std::wcerr << L"timeout waiting for D3D11 camera thread frame. framesRead=" << stats.framesRead
                   << L" framesDelivered=" << stats.framesDelivered
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
