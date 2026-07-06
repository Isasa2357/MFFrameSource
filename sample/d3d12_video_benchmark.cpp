#include <MFFrameSource/MFD3D12VideoCapture.hpp>
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
               << L"  d3d12_video_benchmark <file> <width> <height> <fpsNum> <fpsDen> <subtype> <shaderDir> <frameCount>\n"
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
        if (argc < 9) {
            PrintUsage();
            return 2;
        }

        MFVideoCaptureConfig cfg;
        cfg.input.width = static_cast<UINT>(std::stoul(argv[2]));
        cfg.input.height = static_cast<UINT>(std::stoul(argv[3]));
        cfg.input.fps.numerator = static_cast<std::uint32_t>(std::stoul(argv[4]));
        cfg.input.fps.denominator = static_cast<std::uint32_t>(std::stoul(argv[5]));
        cfg.input.subtype = ParseSubtype(argv[6]);
        cfg.processingShaderDirectory = argv[7];
        cfg.outputWidth = cfg.input.width;
        cfg.outputHeight = cfg.input.height;
        cfg.waitForGpuCompletionOnRead = true;

        const int frameCount = std::stoi(argv[8]);

        D3D12CoreLib::D3D12CoreConfig d3dCfg;
        d3dCfg.enableDebugLayer = false;
        d3dCfg.allowWarpAdapter = false;
        auto core = D3D12CoreLib::D3D12Core::CreateShared(d3dCfg);

        MFPlatformContext platform;
        MFD3D12VideoCapture cap;
        if (!cap.open(argv[1], cfg, core)) {
            const auto& e = cap.lastError();
            std::wcerr << L"open failed: " << e.where << L": " << e.message << L"\n";
            return 1;
        }

        int ok = 0;
        const auto t0 = std::chrono::steady_clock::now();
        for (int i = 0; i < frameCount; ++i) {
            auto r = cap.read();
            if (r.status == MFFrameStatus::EndOfStream) break;
            if (!r.ok()) {
                std::wcerr << L"read failed status=" << static_cast<int>(r.status)
                           << L" " << r.error.where << L": " << r.error.message << L"\n";
                return 1;
            }
            ++ok;
        }
        const auto t1 = std::chrono::steady_clock::now();
        const double sec = std::chrono::duration<double>(t1 - t0).count();
        std::wcout << L"frames=" << ok
                   << L" seconds=" << sec
                   << L" fps=" << (sec > 0.0 ? ok / sec : 0.0) << L"\n";
        return ok > 0 ? 0 : 1;
    } catch (const std::exception& e) {
        std::cerr << "fatal: " << e.what() << "\n";
        return 1;
    }
}
