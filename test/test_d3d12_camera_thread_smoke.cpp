#include "TestHarness.hpp"

#include <MFFrameSource/MFD3D12CameraCaptureThread.hpp>

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
    if (value.empty()) throw std::runtime_error("missing required smoke-test environment variable");
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
        std::cout << "SKIP: set camera smoke-test environment variables to run capture-thread smoke test.\n";
        return 0;
    }

    MFD3D12CameraCaptureThreadConfig cfg;
    cfg.selector.deviceIndex = static_cast<int>(ParseUIntEnv(L"MFFRAMESOURCE_TEST_CAMERA_INDEX"));
    cfg.capture.input.width = ParseUIntEnv(L"MFFRAMESOURCE_TEST_WIDTH");
    cfg.capture.input.height = ParseUIntEnv(L"MFFRAMESOURCE_TEST_HEIGHT");
    cfg.capture.input.fps.numerator = ParseUIntEnv(L"MFFRAMESOURCE_TEST_FPS_NUM");
    cfg.capture.input.fps.denominator = ParseUIntEnv(L"MFFRAMESOURCE_TEST_FPS_DEN");
    cfg.capture.input.subtype = ParseSubtype(GetEnvString(L"MFFRAMESOURCE_TEST_SUBTYPE"));
    cfg.capture.outputWidth = cfg.capture.input.width;
    cfg.capture.outputHeight = cfg.capture.input.height;
    cfg.capture.processingShaderDirectory = GetEnvString(L"MFFRAMESOURCE_D3D12_PROCESSING_SHADER_DIR");
    cfg.capture.framePoolSize = 3;
    cfg.defaultQueueCapacity = 2;
    cfg.clonePoolSize = 4;

    D3D12CoreLib::D3D12CoreConfig d3dCfg;
    d3dCfg.enableDebugLayer = true;
    d3dCfg.allowWarpAdapter = true;
    auto core = D3D12CoreLib::D3D12Core::CreateShared(d3dCfg);

    MFD3D12CameraCaptureThread thread;
    if (!thread.open(cfg, core)) {
        const auto& e = thread.lastError();
        std::wcerr << L"open failed: " << e.where << L": " << e.message << L"\n";
        MFTEST_CHECK(false);
    }

    auto q = thread.createQueue(2);
    thread.start();
    auto frame = q->waitPopFor(std::chrono::seconds(5));
    thread.stop();

    MFTEST_CHECK(frame.has_value());
    MFTEST_CHECK(*frame);
    frame->waitReady();
    MFTEST_CHECK(frame->isReady());
    MFTEST_CHECK((*frame).resource().Get() != nullptr);
    MFTEST_CHECK_EQ((*frame).width(), cfg.capture.outputWidth);
    MFTEST_CHECK_EQ((*frame).height(), cfg.capture.outputHeight);
    MFTEST_CHECK_EQ((*frame).format(), DXGI_FORMAT_R8G8B8A8_UNORM);

    auto stats = thread.stats();
    MFTEST_CHECK(stats.framesRead >= 1ull);
    MFTEST_CHECK(stats.framesDelivered >= 1ull);
    thread.close();
})
