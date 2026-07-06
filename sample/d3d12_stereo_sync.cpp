#include <MFFrameSource/MFD3D12CameraCaptureThread.hpp>
#include <MFFrameSource/MFD3D12CameraSyncThread.hpp>

#include <D3D12Helper/D3D12Core/D3D12Core.hpp>

#include <chrono>
#include <iostream>
#include <stdexcept>
#include <string>

using namespace MFFrameSource;

namespace {

void PrintUsage() {
    std::wcout << L"usage:\n"
               << L"  d3d12_stereo_sync <leftIndex> <rightIndex> <width> <height> <fpsNum> <fpsDen> <subtype> <shaderDir> [pairCount]\n"
               << L"subtype: NV12 | P010 | RGB32 | ARGB32\n";
}

GUID ParseSubtype(const std::wstring& s) {
    if (s == L"NV12") return MFVideoFormat_NV12;
    if (s == L"P010") return MFVideoFormat_P010;
    if (s == L"RGB32") return MFVideoFormat_RGB32;
    if (s == L"ARGB32") return MFVideoFormat_ARGB32;
    throw std::runtime_error("unknown subtype");
}

MFD3D12CameraCaptureThreadConfig MakeConfig(int deviceIndex, int argc, wchar_t** argv) {
    MFD3D12CameraCaptureThreadConfig cfg;
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
    cfg.defaultQueueCapacity = 4;
    cfg.clonePoolSize = 8;
    (void)argc;
    return cfg;
}

} // namespace

int wmain(int argc, wchar_t** argv) {
    try {
        if (argc < 9) {
            PrintUsage();
            return 2;
        }
        const std::uint32_t pairCount = argc >= 10 ? static_cast<std::uint32_t>(std::stoul(argv[9])) : 120u;

        D3D12CoreLib::D3D12CoreConfig d3dCfg;
        d3dCfg.enableDebugLayer = true;
        d3dCfg.allowWarpAdapter = false;
        auto core = D3D12CoreLib::D3D12Core::CreateShared(d3dCfg);

        MFD3D12CameraCaptureThread left;
        MFD3D12CameraCaptureThread right;
        auto leftCfg = MakeConfig(std::stoi(argv[1]), argc, argv);
        auto rightCfg = MakeConfig(std::stoi(argv[2]), argc, argv);

        if (!left.open(leftCfg, core)) {
            const auto& e = left.lastError();
            std::wcerr << L"left open failed: " << e.where << L": " << e.message << L"\n";
            return 1;
        }
        if (!right.open(rightCfg, core)) {
            const auto& e = right.lastError();
            std::wcerr << L"right open failed: " << e.where << L": " << e.message << L"\n";
            return 1;
        }

        auto lq = left.createQueue(4);
        auto rq = right.createQueue(4);

        MFD3D12CameraSyncThread sync;
        MFD3D12CameraSyncThreadConfig syncCfg;
        syncCfg.maxAdjustedDiff100ns = 50000;
        syncCfg.candidateCapacity = 16;
        syncCfg.outputQueueCapacity = 4;
        if (!sync.open(lq, rq, syncCfg)) {
            std::wcerr << L"sync open failed\n";
            return 1;
        }

        left.start();
        right.start();
        sync.start();

        auto out = sync.outputQueue();
        std::uint32_t received = 0;
        while (received < pairCount) {
            auto pair = out->waitPopFor(std::chrono::seconds(5));
            if (!pair) {
                std::wcerr << L"timeout waiting for stereo pair\n";
                break;
            }
            pair->left.waitReady();
            pair->right.waitReady();
            std::wcout << L"pair " << pair->pairNumber
                       << L" diff100ns=" << pair->timestampDiff100ns
                       << L" adjusted100ns=" << pair->adjustedDiff100ns
                       << L" baseline100ns=" << pair->baselineDiff100ns << L"\n";
            ++received;
        }

        sync.stop();
        left.stop();
        right.stop();

        auto ss = sync.stats();
        std::wcout << L"sync stats pairs=" << ss.pairsOut
                   << L" leftIn=" << ss.leftFramesIn
                   << L" rightIn=" << ss.rightFramesIn
                   << L" dropL=" << ss.droppedLeft
                   << L" dropR=" << ss.droppedRight << L"\n";
        return received == 0 ? 1 : 0;
    } catch (const std::exception& e) {
        std::cerr << "fatal: " << e.what() << "\n";
        return 1;
    }
}
