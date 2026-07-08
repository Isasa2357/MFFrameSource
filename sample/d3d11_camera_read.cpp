#include <MFFrameSource/MFCameraEnumerator.hpp>
#include <MFFrameSource/MFD3D11CameraCapture.hpp>
#include <MFFrameSource/MFPlatformContext.hpp>

#include <D3D11Helper/D3D11Core/D3D11Core.hpp>

#include <iostream>
#include <stdexcept>
#include <string>

using namespace MFFrameSource;

namespace {

void PrintUsage() {
    std::wcout << L"usage:\n"
               << L"  d3d11_camera_read list\n"
               << L"  d3d11_camera_read read <deviceIndex> <width> <height> <fpsNum> <fpsDen> <subtype> <shaderDir>\n"
               << L"subtype: NV12 | P010 | RGB32 | ARGB32\n";
}

GUID ParseSubtype(const std::wstring& s) {
    if (s == L"NV12") return MFVideoFormat_NV12;
    if (s == L"P010") return MFVideoFormat_P010;
    if (s == L"RGB32") return MFVideoFormat_RGB32;
    if (s == L"ARGB32") return MFVideoFormat_ARGB32;
    throw std::runtime_error("unknown subtype");
}

void ListCameras() {
    MFPlatformContext platform;
    auto devices = MFCameraEnumerator::enumerateDevices();
    for (const auto& d : devices) {
        std::wcout << L"[" << d.index << L"] " << d.friendlyName << L"\n";
        MFCameraSelector sel;
        sel.deviceIndex = d.index;
        auto formats = MFCameraEnumerator::enumerateFormats(sel);
        for (const auto& f : formats) {
            std::wcout << L"    " << f.width << L"x" << f.height << L" "
                       << f.fps.numerator << L"/" << f.fps.denominator
                       << L" dxgi=" << DxgiFormatName(f.dxgiFormat) << L"\n";
        }
    }
}

int ReadOne(int argc, wchar_t** argv) {
    if (argc < 9) {
        PrintUsage();
        return 2;
    }

    MFCameraSelector selector;
    selector.deviceIndex = std::stoi(argv[2]);

    MFCameraCaptureConfig cfg;
    cfg.input.width = static_cast<UINT>(std::stoul(argv[3]));
    cfg.input.height = static_cast<UINT>(std::stoul(argv[4]));
    cfg.input.fps.numerator = static_cast<std::uint32_t>(std::stoul(argv[5]));
    cfg.input.fps.denominator = static_cast<std::uint32_t>(std::stoul(argv[6]));
    cfg.input.subtype = ParseSubtype(argv[7]);
    cfg.processingShaderDirectory = argv[8];
    cfg.outputWidth = cfg.input.width;
    cfg.outputHeight = cfg.input.height;

    D3D11CoreLib::D3D11CoreConfig d3dCfg;
    d3dCfg.enableDebugLayer = true;
    d3dCfg.enableInfoQueue = true;
    d3dCfg.allowWarpAdapter = false;
    auto core = D3D11CoreLib::D3D11Core::CreateShared(d3dCfg);

    MFPlatformContext platform;
    MFD3D11CameraCapture cap;
    if (!cap.open(selector, cfg, core)) {
        const auto& e = cap.lastError();
        std::wcerr << L"open failed: " << e.where << L": " << e.message << L"\n";
        return 1;
    }

    auto r = cap.read();
    if (!r.ok()) {
        std::wcerr << L"read failed status=" << static_cast<int>(r.status)
                   << L" " << r.error.where << L": " << r.error.message << L"\n";
        return 1;
    }

    r.frame.waitReady();
    std::wcout << L"frame ok: " << r.frame.width() << L"x" << r.frame.height()
               << L" format=" << DxgiFormatName(r.frame.format())
               << L" frameNumber=" << r.frame.frameNumber()
               << L" sampleTime100ns=" << r.frame.sampleTime100ns()
               << L" resource=" << (r.frame.resource().Get() ? L"yes" : L"no")
               << L" srv=" << (r.frame.srv() ? L"yes" : L"no") << L"\n";
    return 0;
}

} // namespace

int wmain(int argc, wchar_t** argv) {
    try {
        if (argc < 2) {
            PrintUsage();
            return 2;
        }
        const std::wstring cmd = argv[1];
        if (cmd == L"list") {
            ListCameras();
            return 0;
        }
        if (cmd == L"read") {
            return ReadOne(argc, argv);
        }
        PrintUsage();
        return 2;
    } catch (const std::exception& e) {
        std::cerr << "fatal: " << e.what() << "\n";
        return 1;
    }
}
