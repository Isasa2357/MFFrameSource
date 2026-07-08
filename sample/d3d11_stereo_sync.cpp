#include <MFFrameSource/MFD3D11CameraCaptureThread.hpp>
#include <MFFrameSource/MFD3D11CameraSyncThread.hpp>
#include <MFFrameSource/MFPlatformContext.hpp>

#include <D3D11Helper/D3D11Core/D3D11Core.hpp>

#include <chrono>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <string>

using namespace MFFrameSource;

namespace {

void PrintUsage() {
    std::wcout << L"usage:\n"
               << L"  d3d11_stereo_sync <leftIndex> <rightIndex> <width> <height> <fpsNum> <fpsDen> <subtype> <shaderDir> [pairCount]\n"
               << L"subtype: NV12 | P010 | RGB32 | ARGB32\n";
}

GUID ParseSubtype(const std::wstring& s) {
    if (s == L"NV12") return MFVideoFormat_NV12;
    if (s == L"P010") return MFVideoFormat_P010;
    if (s == L"RGB32") return MFVideoFormat_RGB32;
    if (s == L"ARGB32") return MFVideoFormat_ARGB32;
    throw std::runtime_error("unknown subtype");
}

MFD3D11CameraCaptureThreadConfig MakeConfig(int deviceIndex, wchar_t** argv) {
    MFD3D11CameraCaptureThreadConfig cfg;
    cfg.selector.deviceIndex = deviceIndex;
    cfg.capture.input.width = static_cast<UINT>(std::stoul(argv[3]));
    cfg.capture.input.height = static_cast<UINT>(std::stoul(argv[4]));
    cfg.capture.input.fps.numerator = static_cast<std::uint32_t>(std::stoul(argv[5]));
    cfg.capture.input.fps.denominator = static_cast<std::uint32_t>(std::stoul(argv[6]));
    cfg.capture.input.subtype = ParseSubtype(argv[7]);
    cfg.capture.processingShaderDirectory = argv[8];
    cfg.capture.outputWidth = cfg.capture.input.width;
    cfg.capture.outputHeight = cfg.capture.input.height;
    cfg.capture.framePoolSize = 4;
    cfg.defaultQueueCapacity = 8;
    cfg.clonePoolSize = 8;
    return cfg;
}

void PrintThreadError(const wchar_t* name, const MFD3D11CameraCaptureThread& thread) {
    const auto& e = thread.lastError();
    if (e) {
        std::wcerr << name << L" error: " << e.where << L": " << e.message << L"\n";
    }
}

} // namespace

int wmain(int argc, wchar_t** argv) {
    try {
        if (argc < 9) {
            PrintUsage();
            return 2;
        }

        const int leftIndex = std::stoi(argv[1]);
        const int rightIndex = std::stoi(argv[2]);
        const int pairCount = argc >= 10 ? std::stoi(argv[9]) : 120;
        if (leftIndex == rightIndex) {
            std::wcerr << L"leftIndex and rightIndex must be different\n";
            return 2;
        }

        MFPlatformContext platform;

        D3D11CoreLib::D3D11CoreConfig d3dCfg;
        d3dCfg.enableDebugLayer = true;
        d3dCfg.enableInfoQueue = true;
        d3dCfg.allowWarpAdapter = false;
        d3dCfg.enableMultithreadProtection = true;
        auto core = D3D11CoreLib::D3D11Core::CreateShared(d3dCfg);

        auto leftCfg = MakeConfig(leftIndex, argv);
        auto rightCfg = MakeConfig(rightIndex, argv);

        MFD3D11CameraCaptureThread leftThread;
        MFD3D11CameraCaptureThread rightThread;
        if (!leftThread.open(leftCfg, core)) {
            PrintThreadError(L"left", leftThread);
            return 1;
        }
        if (!rightThread.open(rightCfg, core)) {
            PrintThreadError(L"right", rightThread);
            leftThread.close();
            return 1;
        }

        auto leftQueue = leftThread.createQueue(8);
        auto rightQueue = rightThread.createQueue(8);

        MFD3D11CameraSyncThreadConfig syncCfg;
        syncCfg.maxAdjustedDiff100ns = 50000; // 5 ms default strict pairing window.
        syncCfg.baselineSampleCount = 30;
        syncCfg.candidateCapacity = 32;
        syncCfg.outputQueueCapacity = 3;

        MFD3D11CameraSyncThread sync;
        if (!sync.open(leftQueue, rightQueue, syncCfg)) {
            std::wcerr << L"sync open failed\n";
            leftThread.close();
            rightThread.close();
            return 1;
        }

        sync.start();
        leftThread.start();
        rightThread.start();

        int received = 0;
        while (received < pairCount) {
            auto pair = sync.outputQueue()->waitPopFor(std::chrono::seconds(5));
            if (!pair) {
                std::wcerr << L"timeout waiting for stereo pair\n";
                break;
            }
            pair->left.waitReady();
            pair->right.waitReady();
            std::wcout << L"pair " << pair->pairNumber
                       << L" leftFrame=" << pair->left.frameNumber()
                       << L" rightFrame=" << pair->right.frameNumber()
                       << L" leftTs=" << pair->left.sampleTime100ns()
                       << L" rightTs=" << pair->right.sampleTime100ns()
                       << L" diff100ns=" << pair->timestampDiff100ns
                       << L" adjusted100ns=" << pair->adjustedDiff100ns
                       << L" size=" << pair->left.width() << L"x" << pair->left.height() << L"\n";
            ++received;
        }

        leftThread.stop();
        rightThread.stop();
        sync.stop();
        leftThread.rethrowWorkerExceptionIfAny();
        rightThread.rethrowWorkerExceptionIfAny();
        sync.rethrowWorkerExceptionIfAny();

        const auto ls = leftThread.stats();
        const auto rs = rightThread.stats();
        const auto ss = sync.stats();
        std::wcout << L"left stats: read=" << ls.framesRead << L" delivered=" << ls.framesDelivered << L" failures=" << ls.readFailures << L"\n";
        std::wcout << L"right stats: read=" << rs.framesRead << L" delivered=" << rs.framesDelivered << L" failures=" << rs.readFailures << L"\n";
        std::wcout << L"sync stats: leftIn=" << ss.leftFramesIn
                   << L" rightIn=" << ss.rightFramesIn
                   << L" pairsOut=" << ss.pairsOut
                   << L" rejected=" << ss.rejectedPairs
                   << L" droppedLeft=" << ss.droppedLeft
                   << L" droppedRight=" << ss.droppedRight << L"\n";

        sync.close();
        leftThread.close();
        rightThread.close();
        return received > 0 ? 0 : 1;
    } catch (const std::exception& e) {
        std::cerr << "fatal: " << e.what() << "\n";
        return 1;
    }
}
