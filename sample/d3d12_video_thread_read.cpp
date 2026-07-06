#include <MFFrameSource/MFD3D12VideoCaptureThread.hpp>
#include <MFFrameSource/MFPlatformContext.hpp>

#include <D3D12Helper/D3D12Core/D3D12Core.hpp>

#include <chrono>
#include <iostream>
#include <stdexcept>
#include <string>

using namespace MFFrameSource;

namespace {

void PrintUsage() {
    std::wcout << L"usage:\n"
               << L"  d3d12_video_thread_read <file> <width> <height> <fpsNum> <fpsDen> <subtype> <shaderDir> [frameCount] [fast]\n"
               << L"subtype: NV12 | P010 | RGB32 | ARGB32\n"
               << L"fast: 0 = realtime, 1 = decode as fast as possible\n";
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

        MFD3D12VideoCaptureThreadConfig cfg;
        cfg.filePath = argv[1];
        cfg.capture.input.width = static_cast<UINT>(std::stoul(argv[2]));
        cfg.capture.input.height = static_cast<UINT>(std::stoul(argv[3]));
        cfg.capture.input.fps.numerator = static_cast<std::uint32_t>(std::stoul(argv[4]));
        cfg.capture.input.fps.denominator = static_cast<std::uint32_t>(std::stoul(argv[5]));
        cfg.capture.input.subtype = ParseSubtype(argv[6]);
        cfg.capture.processingShaderDirectory = argv[7];
        cfg.capture.outputWidth = cfg.capture.input.width;
        cfg.capture.outputHeight = cfg.capture.input.height;
        cfg.defaultQueueCapacity = 4;

        const int frameCount = (argc >= 9) ? std::stoi(argv[8]) : 60;
        if (argc >= 10 && std::stoi(argv[9]) != 0) {
            cfg.playbackMode = MFVideoPlaybackMode::DecodeAsFastAsPossible;
        }

        D3D12CoreLib::D3D12CoreConfig d3dCfg;
        d3dCfg.enableDebugLayer = true;
        d3dCfg.allowWarpAdapter = false;
        auto core = D3D12CoreLib::D3D12Core::CreateShared(d3dCfg);

        MFPlatformContext platform;
        MFD3D12VideoCaptureThread thread;
        if (!thread.open(cfg, core)) {
            const auto& e = thread.lastError();
            std::wcerr << L"open failed: " << e.where << L": " << e.message << L"\n";
            return 1;
        }

        auto q = thread.createQueue(4);
        thread.start();

        int received = 0;
        while (received < frameCount) {
            auto item = q->waitPopFor(std::chrono::seconds(5));
            if (!item) {
                std::wcerr << L"timeout waiting for video frame\n";
                break;
            }
            item->waitReady();
            std::wcout << L"frame " << received
                       << L" number=" << item->frameNumber()
                       << L" time100ns=" << item->sampleTime100ns()
                       << L" size=" << item->width() << L"x" << item->height() << L"\n";
            ++received;
        }

        thread.stop();
        const auto s = thread.stats();
        std::wcout << L"stats read=" << s.framesRead
                   << L" delivered=" << s.framesDelivered
                   << L" eos=" << s.endOfStreamCount
                   << L" readFailures=" << s.readFailures << L"\n";
        return received > 0 ? 0 : 1;
    } catch (const std::exception& e) {
        std::cerr << "fatal: " << e.what() << "\n";
        return 1;
    }
}
