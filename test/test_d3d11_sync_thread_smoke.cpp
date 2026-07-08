#include "TestHarness.hpp"

#include <MFFrameSource/MFD3D11CameraSyncThread.hpp>

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
    return !GetEnvString(L"MFFRAMESOURCE_TEST_LEFT_CAMERA_INDEX").empty() &&
           !GetEnvString(L"MFFRAMESOURCE_TEST_RIGHT_CAMERA_INDEX").empty() &&
           !GetEnvString(L"MFFRAMESOURCE_TEST_CAMERA_WIDTH").empty() &&
           !GetEnvString(L"MFFRAMESOURCE_TEST_CAMERA_HEIGHT").empty() &&
           !GetEnvString(L"MFFRAMESOURCE_TEST_CAMERA_FPS_NUM").empty() &&
           !GetEnvString(L"MFFRAMESOURCE_TEST_CAMERA_FPS_DEN").empty() &&
           !GetEnvString(L"MFFRAMESOURCE_TEST_CAMERA_SUBTYPE").empty() &&
           !GetEnvString(L"MFFRAMESOURCE_D3D11_PROCESSING_SHADER_DIR").empty();
}

unsigned ParseUIntEnv(const wchar_t* name) {
    const std::wstring value = GetEnvString(name);
    if (value.empty()) throw std::runtime_error("missing required D3D11 sync smoke-test environment variable");
    return static_cast<unsigned>(std::stoul(value));
}

GUID ParseSubtype(const std::wstring& value) {
    if (value == L"NV12") return MFVideoFormat_NV12;
    if (value == L"P010") return MFVideoFormat_P010;
    if (value == L"RGB32") return MFVideoFormat_RGB32;
    if (value == L"ARGB32") return MFVideoFormat_ARGB32;
    throw std::runtime_error("MFFRAMESOURCE_TEST_CAMERA_SUBTYPE must be NV12, P010, RGB32, or ARGB32");
}

MFD3D11CameraCaptureThreadConfig MakeCameraThreadConfig(int deviceIndex) {
    MFD3D11CameraCaptureThreadConfig cfg;
    cfg.selector.deviceIndex = deviceIndex;
    cfg.capture.input.width = ParseUIntEnv(L"MFFRAMESOURCE_TEST_CAMERA_WIDTH");
    cfg.capture.input.height = ParseUIntEnv(L"MFFRAMESOURCE_TEST_CAMERA_HEIGHT");
    cfg.capture.input.fps.numerator = ParseUIntEnv(L"MFFRAMESOURCE_TEST_CAMERA_FPS_NUM");
    cfg.capture.input.fps.denominator = ParseUIntEnv(L"MFFRAMESOURCE_TEST_CAMERA_FPS_DEN");
    cfg.capture.input.subtype = ParseSubtype(GetEnvString(L"MFFRAMESOURCE_TEST_CAMERA_SUBTYPE"));
    cfg.capture.outputWidth = cfg.capture.input.width;
    cfg.capture.outputHeight = cfg.capture.input.height;
    cfg.capture.processingShaderDirectory = GetEnvString(L"MFFRAMESOURCE_D3D11_PROCESSING_SHADER_DIR");
    cfg.capture.framePoolSize = 3;
    cfg.defaultQueueCapacity = 8;
    cfg.clonePoolSize = 8;
    return cfg;
}

void PrintLastError(const wchar_t* name, const MFD3D11CameraCaptureThread& thread) {
    const auto& e = thread.lastError();
    if (e) {
        std::wcerr << name << L" lastError: " << e.where << L": " << e.message << L"\n";
    }
}

} // namespace

MFTEST_MAIN({
    if (!HasRequiredSmokeEnv()) {
        std::cout << "SKIP: set MFFRAMESOURCE_TEST_LEFT_CAMERA_INDEX, MFFRAMESOURCE_TEST_RIGHT_CAMERA_INDEX, MFFRAMESOURCE_TEST_CAMERA_WIDTH, MFFRAMESOURCE_TEST_CAMERA_HEIGHT, MFFRAMESOURCE_TEST_CAMERA_FPS_NUM, MFFRAMESOURCE_TEST_CAMERA_FPS_DEN, MFFRAMESOURCE_TEST_CAMERA_SUBTYPE, and MFFRAMESOURCE_D3D11_PROCESSING_SHADER_DIR to run D3D11 sync thread smoke test.\n";
        return 0;
    }

    const int leftIndex = static_cast<int>(ParseUIntEnv(L"MFFRAMESOURCE_TEST_LEFT_CAMERA_INDEX"));
    const int rightIndex = static_cast<int>(ParseUIntEnv(L"MFFRAMESOURCE_TEST_RIGHT_CAMERA_INDEX"));
    if (leftIndex == rightIndex) {
        std::cout << "SKIP: D3D11 sync smoke test requires two different camera indexes.\n";
        return 0;
    }

    D3D11CoreLib::D3D11CoreConfig d3dCfg;
    d3dCfg.enableDebugLayer = true;
    d3dCfg.enableInfoQueue = true;
    d3dCfg.allowWarpAdapter = true;
    d3dCfg.enableMultithreadProtection = true;
    auto core = D3D11CoreLib::D3D11Core::CreateShared(d3dCfg);

    auto leftCfg = MakeCameraThreadConfig(leftIndex);
    auto rightCfg = MakeCameraThreadConfig(rightIndex);

    MFD3D11CameraCaptureThread leftThread;
    MFD3D11CameraCaptureThread rightThread;

    if (!leftThread.open(leftCfg, core)) {
        PrintLastError(L"left", leftThread);
        MFTEST_CHECK(false);
    }
    if (!rightThread.open(rightCfg, core)) {
        PrintLastError(L"right", rightThread);
        leftThread.close();
        MFTEST_CHECK(false);
    }

    auto leftQueue = leftThread.createQueue(8);
    auto rightQueue = rightThread.createQueue(8);

    MFD3D11CameraSyncThreadConfig syncCfg;
    syncCfg.maxAdjustedDiff100ns = 10000000; // 1 second. Smoke test checks pairing path, not strict sync quality.
    syncCfg.baselineSampleCount = 5;
    syncCfg.candidateCapacity = 32;
    syncCfg.outputQueueCapacity = 2;

    MFD3D11CameraSyncThread sync;
    if (!sync.open(leftQueue, rightQueue, syncCfg)) {
        leftThread.close();
        rightThread.close();
        MFTEST_CHECK(false);
    }
    MFTEST_CHECK(sync.isOpened());

    sync.start();
    leftThread.start();
    rightThread.start();

    auto pair = sync.outputQueue()->waitPopFor(std::chrono::seconds(15));

    leftThread.stop();
    rightThread.stop();
    sync.stop();
    leftThread.rethrowWorkerExceptionIfAny();
    rightThread.rethrowWorkerExceptionIfAny();
    sync.rethrowWorkerExceptionIfAny();

    if (!pair.has_value()) {
        const auto ls = leftThread.stats();
        const auto rs = rightThread.stats();
        const auto ss = sync.stats();
        std::wcerr << L"timeout waiting for D3D11 stereo pair. "
                   << L"leftRead=" << ls.framesRead << L" leftDelivered=" << ls.framesDelivered
                   << L" rightRead=" << rs.framesRead << L" rightDelivered=" << rs.framesDelivered
                   << L" leftIn=" << ss.leftFramesIn << L" rightIn=" << ss.rightFramesIn
                   << L" pairsOut=" << ss.pairsOut << L" rejected=" << ss.rejectedPairs
                   << L" droppedLeft=" << ss.droppedLeft << L" droppedRight=" << ss.droppedRight << L"\n";
        PrintLastError(L"left", leftThread);
        PrintLastError(L"right", rightThread);
        MFTEST_CHECK(false);
    }

    MFTEST_CHECK(*pair);
    pair->left.waitReady();
    pair->right.waitReady();
    MFTEST_CHECK(pair->left.isReady());
    MFTEST_CHECK(pair->right.isReady());
    MFTEST_CHECK(pair->left.resource().Get() != nullptr);
    MFTEST_CHECK(pair->right.resource().Get() != nullptr);
    MFTEST_CHECK(pair->left.srv() != nullptr);
    MFTEST_CHECK(pair->right.srv() != nullptr);
    MFTEST_CHECK_EQ(pair->left.width(), leftCfg.capture.outputWidth);
    MFTEST_CHECK_EQ(pair->right.width(), rightCfg.capture.outputWidth);
    MFTEST_CHECK_EQ(pair->left.height(), leftCfg.capture.outputHeight);
    MFTEST_CHECK_EQ(pair->right.height(), rightCfg.capture.outputHeight);
    MFTEST_CHECK_EQ(pair->left.format(), DXGI_FORMAT_R8G8B8A8_UNORM);
    MFTEST_CHECK_EQ(pair->right.format(), DXGI_FORMAT_R8G8B8A8_UNORM);

    const auto ss = sync.stats();
    MFTEST_CHECK(ss.leftFramesIn >= 1ull);
    MFTEST_CHECK(ss.rightFramesIn >= 1ull);
    MFTEST_CHECK(ss.pairsOut >= 1ull);

    sync.close();
    leftThread.close();
    rightThread.close();
    MFTEST_CHECK(!sync.isOpened());
    MFTEST_CHECK(!leftThread.isOpened());
    MFTEST_CHECK(!rightThread.isOpened());
})
