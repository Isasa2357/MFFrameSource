#include <MFFrameSource/MFD3D12CameraCapture.hpp>

#include <D3D12Helper/D3D12Core/D3D12Core.hpp>

#include <chrono>
#include <iostream>
#include <stdexcept>
#include <string>

using namespace MFFrameSource;

namespace {

void PrintUsage() {
    std::wcout << L"usage:\n"
               << L"  d3d12_camera_benchmark <deviceIndex> <width> <height> <fpsNum> <fpsDen> <subtype> <shaderDir> [frameCount]\n"
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
        const std::uint32_t frameCount = argc >= 9 ? static_cast<std::uint32_t>(std::stoul(argv[8])) : 300u;

        MFCameraSelector selector;
        selector.deviceIndex = std::stoi(argv[1]);

        MFCameraCaptureConfig cfg;
        cfg.input.width = static_cast<UINT>(std::stoul(argv[2]));
        cfg.input.height = static_cast<UINT>(std::stoul(argv[3]));
        cfg.input.fps.numerator = static_cast<std::uint32_t>(std::stoul(argv[4]));
        cfg.input.fps.denominator = static_cast<std::uint32_t>(std::stoul(argv[5]));
        cfg.input.subtype = ParseSubtype(argv[6]);
        cfg.processingShaderDirectory = argv[7];
        cfg.outputWidth = cfg.input.width;
        cfg.outputHeight = cfg.input.height;
        cfg.framePoolSize = 5;

        D3D12CoreLib::D3D12CoreConfig d3dCfg;
        d3dCfg.enableDebugLayer = false;
        d3dCfg.allowWarpAdapter = false;
        auto core = D3D12CoreLib::D3D12Core::CreateShared(d3dCfg);

        MFD3D12CameraCapture cap;
        if (!cap.open(selector, cfg, core)) {
            const auto& e = cap.lastError();
            std::wcerr << L"open failed: " << e.where << L": " << e.message << L"\n";
            return 1;
        }

        std::uint32_t ok = 0;
        std::uint32_t failed = 0;
        const auto t0 = std::chrono::steady_clock::now();
        for (std::uint32_t i = 0; i < frameCount; ++i) {
            auto r = cap.read();
            if (r.ok()) {
                r.frame.waitReady();
                ++ok;
            } else {
                ++failed;
            }
        }
        const auto t1 = std::chrono::steady_clock::now();
        const double sec = std::chrono::duration<double>(t1 - t0).count();
        const double fps = sec > 0.0 ? static_cast<double>(ok) / sec : 0.0;
        std::wcout << L"frames ok=" << ok << L" failed=" << failed
                   << L" seconds=" << sec << L" fps=" << fps << L"\n";
        return ok == 0 ? 1 : 0;
    } catch (const std::exception& e) {
        std::cerr << "fatal: " << e.what() << "\n";
        return 1;
    }
}
