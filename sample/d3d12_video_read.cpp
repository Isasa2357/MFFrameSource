#include <MFFrameSource/MFD3D12VideoCapture.hpp>
#include <MFFrameSource/MFPlatformContext.hpp>

#include <D3D12Helper/D3D12Core/D3D12Core.hpp>

#include <iostream>
#include <stdexcept>
#include <string>

using namespace MFFrameSource;

namespace {

void PrintUsage() {
    std::wcout << L"usage:\n"
               << L"  d3d12_video_read <file> <width> <height> <fpsNum> <fpsDen> <subtype> <shaderDir> [frameCount]\n"
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

        MFVideoCaptureConfig cfg;
        cfg.input.width = static_cast<UINT>(std::stoul(argv[2]));
        cfg.input.height = static_cast<UINT>(std::stoul(argv[3]));
        cfg.input.fps.numerator = static_cast<std::uint32_t>(std::stoul(argv[4]));
        cfg.input.fps.denominator = static_cast<std::uint32_t>(std::stoul(argv[5]));
        cfg.input.subtype = ParseSubtype(argv[6]);
        cfg.processingShaderDirectory = argv[7];
        cfg.outputWidth = cfg.input.width;
        cfg.outputHeight = cfg.input.height;

        const int frameCount = (argc >= 9) ? std::stoi(argv[8]) : 1;

        D3D12CoreLib::D3D12CoreConfig d3dCfg;
        d3dCfg.enableDebugLayer = true;
        d3dCfg.allowWarpAdapter = false;
        auto core = D3D12CoreLib::D3D12Core::CreateShared(d3dCfg);

        MFPlatformContext platform;
        MFD3D12VideoCapture cap;
        if (!cap.open(argv[1], cfg, core)) {
            const auto& e = cap.lastError();
            std::wcerr << L"open failed: " << e.where << L": " << e.message << L"\n";
            return 1;
        }

        std::wcout << L"opened: duration100ns=" << cap.duration100ns()
                   << L" selected=" << cap.selectedFormat().width << L"x" << cap.selectedFormat().height
                   << L" " << cap.selectedFormat().fps.numerator << L"/" << cap.selectedFormat().fps.denominator
                   << L" " << DxgiFormatName(cap.selectedFormat().dxgiFormat) << L"\n";

        for (int i = 0; i < frameCount; ++i) {
            auto r = cap.read();
            if (r.status == MFFrameStatus::EndOfStream) {
                std::wcout << L"end of stream\n";
                break;
            }
            if (!r.ok()) {
                std::wcerr << L"read failed status=" << static_cast<int>(r.status)
                           << L" " << r.error.where << L": " << r.error.message << L"\n";
                return 1;
            }
            r.frame.waitReady();
            std::wcout << L"frame " << i
                       << L" number=" << r.frame.frameNumber()
                       << L" time100ns=" << r.frame.sampleTime100ns()
                       << L" size=" << r.frame.width() << L"x" << r.frame.height()
                       << L" format=" << DxgiFormatName(r.frame.format()) << L"\n";
        }
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "fatal: " << e.what() << "\n";
        return 1;
    }
}
