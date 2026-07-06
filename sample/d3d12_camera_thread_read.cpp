#include <MFFrameSource/MFD3D12CameraCaptureThread.hpp>

#include <D3D12Helper/D3D12Core/D3D12Core.hpp>

#include <chrono>
#include <iostream>
#include <stdexcept>
#include <string>

using namespace MFFrameSource;

namespace {

void PrintUsage() {
    std::wcout << L"usage:\n"
               << L"  d3d12_camera_thread_read <deviceIndex> <width> <height> <fpsNum> <fpsDen> <subtype> <shaderDir> [frameCount]\n"
               << L"subtype: NV12 | P010 | RGB32 | ARGB32\n";
}

GUID ParseSubtype(const std::wstring& s) {
    if (s == L"NV12") return MFVideoFormat_NV12;
    if (s == L"P010") return MFVideoFormat_P010;
    if (s == L"RGB32") return MFVideoFormat_RGB32;
    if (s == L"ARGB32") return MFVideoFormat_ARGB32;
    throw std::runtime_error("unknown subtype");
}

} // namespace

int wmain(int argc, wchar_t** argv) {
    try {
        if (argc < 8) {
            PrintUsage();
            return 2;
        }
        const std::uint32_t frameCount = argc >= 9 ? static_cast<std::uint32_t>(std::stoul(argv[8])) : 120u;

        MFD3D12CameraCaptureThreadConfig cfg;
        cfg.selector.deviceIndex = std::stoi(argv[1]);
        cfg.capture.input.width = static_cast<UINT>(std::stoul(argv[2]));
        cfg.capture.input.height = static_cast<UINT>(std::stoul(argv[3]));
        cfg.capture.input.fps.numerator = static_cast<std::uint32_t>(std::stoul(argv[4]));
        cfg.capture.input.fps.denominator = static_cast<std::uint32_t>(std::stoul(argv[5]));
        cfg.capture.input.subtype = ParseSubtype(argv[6]);
        cfg.capture.processingShaderDirectory = argv[7];
        cfg.capture.outputWidth = cfg.capture.input.width;
        cfg.capture.outputHeight = cfg.capture.input.height;
        cfg.capture.framePoolSize = 4;
        cfg.defaultQueueCapacity = 3;
        cfg.clonePoolSize = 8;

        D3D12CoreLib::D3D12CoreConfig d3dCfg;
        d3dCfg.enableDebugLayer = true;
        d3dCfg.allowWarpAdapter = false;
        auto core = D3D12CoreLib::D3D12Core::CreateShared(d3dCfg);

        MFD3D12CameraCaptureThread captureThread;
        if (!captureThread.open(cfg, core)) {
            const auto& e = captureThread.lastError();
            std::wcerr << L"open failed: " << e.where << L": " << e.message << L"\n";
            return 1;
        }

        auto queue = captureThread.createQueue(3);
        captureThread.start();

        std::uint32_t received = 0;
        while (received < frameCount) {
            auto frame = queue->waitPopFor(std::chrono::seconds(5));
            if (!frame) {
                std::wcerr << L"timeout waiting for frame\n";
                break;
            }
            frame->waitReady();
            std::wcout << L"frame " << frame->frameNumber()
                       << L" " << frame->width() << L"x" << frame->height()
                       << L" ts=" << frame->sampleTime100ns() << L"\n";
            ++received;
        }

        captureThread.stop();
        auto stats = captureThread.stats();
        std::wcout << L"stats: read=" << stats.framesRead
                   << L" delivered=" << stats.framesDelivered
                   << L" readFailures=" << stats.readFailures
                   << L" cloneFailures=" << stats.cloneFailures
                   << L" droppedOldest=" << stats.queuesDroppedOldest << L"\n";
        return received == 0 ? 1 : 0;
    } catch (const std::exception& e) {
        std::cerr << "fatal: " << e.what() << "\n";
        return 1;
    }
}
