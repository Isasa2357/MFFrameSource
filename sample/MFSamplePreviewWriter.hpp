#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <MFFrameSource/MFD3D12CameraCapture.hpp>

#include <D3D12Helper/D3D12Core/D3D12Core.hpp>
#include <D3D12Helper/D3D12Framework/D3D12Resource.hpp>

#include <Windows.h>
#include <d3d12.h>
#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <wrl/client.h>

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <stdexcept>
#include <string>
#include <vector>

#if defined(_MSC_VER)
#pragma comment(lib, "user32.lib")
#pragma comment(lib, "gdi32.lib")
#pragma comment(lib, "mf.lib")
#pragma comment(lib, "mfplat.lib")
#pragma comment(lib, "mfreadwrite.lib")
#pragma comment(lib, "mfuuid.lib")
#endif

namespace MFSample {

using Microsoft::WRL::ComPtr;

inline void ThrowIfFailed(HRESULT hr, const char* where) {
    if (FAILED(hr)) {
        throw std::runtime_error(where);
    }
}

inline UINT AlignUp(UINT value, UINT alignment) {
    return (value + alignment - 1) & ~(alignment - 1);
}

struct BgraImage {
    UINT width = 0;
    UINT height = 0;
    UINT stride = 0;
    std::vector<std::uint8_t> pixels;

    bool empty() const noexcept { return pixels.empty() || width == 0 || height == 0 || stride == 0; }
};

inline BgraImage ReadD3D12FrameToBgra(
    D3D12CoreLib::D3D12Core& core,
    MFFrameSource::MFD3D12CameraFrame& frame) {

    frame.waitReady();

    auto& resource = frame.resource();
    ID3D12Resource* texture = resource.Get();
    if (!texture) {
        throw std::runtime_error("ReadD3D12FrameToBgra: null texture");
    }

    const auto desc = texture->GetDesc();
    if (desc.Dimension != D3D12_RESOURCE_DIMENSION_TEXTURE2D) {
        throw std::runtime_error("ReadD3D12FrameToBgra: expected Texture2D");
    }
    if (desc.Format != DXGI_FORMAT_R8G8B8A8_UNORM &&
        desc.Format != DXGI_FORMAT_B8G8R8A8_UNORM &&
        desc.Format != DXGI_FORMAT_R8G8B8A8_UNORM_SRGB &&
        desc.Format != DXGI_FORMAT_B8G8R8A8_UNORM_SRGB) {
        throw std::runtime_error("ReadD3D12FrameToBgra: expected RGBA8/BGRA8 output texture");
    }

    auto* device = core.GetDevice();

    D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint{};
    UINT numRows = 0;
    UINT64 rowSizeBytes = 0;
    UINT64 totalBytes = 0;
    device->GetCopyableFootprints(&desc, 0, 1, 0, &footprint, &numRows, &rowSizeBytes, &totalBytes);

    D3D12_HEAP_PROPERTIES heapProps{};
    heapProps.Type = D3D12_HEAP_TYPE_READBACK;

    D3D12_RESOURCE_DESC bufferDesc{};
    bufferDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    bufferDesc.Alignment = 0;
    bufferDesc.Width = totalBytes;
    bufferDesc.Height = 1;
    bufferDesc.DepthOrArraySize = 1;
    bufferDesc.MipLevels = 1;
    bufferDesc.Format = DXGI_FORMAT_UNKNOWN;
    bufferDesc.SampleDesc.Count = 1;
    bufferDesc.SampleDesc.Quality = 0;
    bufferDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    bufferDesc.Flags = D3D12_RESOURCE_FLAG_NONE;

    ComPtr<ID3D12Resource> readback;
    ThrowIfFailed(device->CreateCommittedResource(
        &heapProps,
        D3D12_HEAP_FLAG_NONE,
        &bufferDesc,
        D3D12_RESOURCE_STATE_COPY_DEST,
        nullptr,
        IID_PPV_ARGS(readback.GetAddressOf())),
        "CreateCommittedResource(readback)");

    auto ctx = core.CreateDirectContext();
    ctx.Reset();

    const auto beforeState = resource.GetState();
    if (beforeState != D3D12_RESOURCE_STATE_COPY_SOURCE) {
        D3D12_RESOURCE_BARRIER barrier{};
        barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        barrier.Transition.pResource = texture;
        barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        barrier.Transition.StateBefore = beforeState;
        barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_SOURCE;
        ctx.ResourceBarrier(barrier);
        resource.SetState(D3D12_RESOURCE_STATE_COPY_SOURCE);
    }

    D3D12_TEXTURE_COPY_LOCATION dst{};
    dst.pResource = readback.Get();
    dst.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
    dst.PlacedFootprint = footprint;

    D3D12_TEXTURE_COPY_LOCATION src{};
    src.pResource = texture;
    src.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    src.SubresourceIndex = 0;

    ctx.GetCommandList()->CopyTextureRegion(&dst, 0, 0, 0, &src, nullptr);

    if (beforeState != D3D12_RESOURCE_STATE_COPY_SOURCE) {
        D3D12_RESOURCE_BARRIER barrier{};
        barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        barrier.Transition.pResource = texture;
        barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_SOURCE;
        barrier.Transition.StateAfter = beforeState;
        ctx.ResourceBarrier(barrier);
        resource.SetState(beforeState);
    }

    ctx.Close();
    ID3D12CommandList* lists[] = { ctx.GetCommandList() };
    core.DirectQueue().ExecuteCommandLists(1, lists);
    const auto fenceValue = core.DirectQueue().Signal();
    core.DirectQueue().WaitForFenceValue(fenceValue);

    void* mapped = nullptr;
    D3D12_RANGE readRange{ footprint.Offset, footprint.Offset + totalBytes };
    ThrowIfFailed(readback->Map(0, &readRange, &mapped), "Map(readback)");

    const auto width = static_cast<UINT>(desc.Width);
    const auto height = desc.Height;
    const auto srcStride = footprint.Footprint.RowPitch;
    const auto srcBase = static_cast<const std::uint8_t*>(mapped) + footprint.Offset;

    BgraImage out;
    out.width = width;
    out.height = height;
    out.stride = width * 4;
    out.pixels.resize(static_cast<std::size_t>(out.stride) * out.height);

    const bool srcIsBgra = desc.Format == DXGI_FORMAT_B8G8R8A8_UNORM ||
                           desc.Format == DXGI_FORMAT_B8G8R8A8_UNORM_SRGB;
    for (UINT y = 0; y < height; ++y) {
        const auto* srcRow = srcBase + static_cast<std::size_t>(srcStride) * y;
        auto* dstRow = out.pixels.data() + static_cast<std::size_t>(out.stride) * y;
        if (srcIsBgra) {
            std::memcpy(dstRow, srcRow, static_cast<std::size_t>(out.stride));
        } else {
            for (UINT x = 0; x < width; ++x) {
                const auto* s = srcRow + static_cast<std::size_t>(x) * 4;
                auto* d = dstRow + static_cast<std::size_t>(x) * 4;
                d[0] = s[2];
                d[1] = s[1];
                d[2] = s[0];
                d[3] = s[3];
            }
        }
    }

    D3D12_RANGE writtenRange{ 0, 0 };
    readback->Unmap(0, &writtenRange);
    return out;
}

inline BgraImage DownscaleBgraBox(const BgraImage& src, UINT dstW, UINT dstH) {
    if (src.empty()) return {};
    if (dstW == 0 || dstH == 0) return {};
    if (dstW == src.width && dstH == src.height) return src;

    BgraImage dst;
    dst.width = dstW;
    dst.height = dstH;
    dst.stride = dstW * 4;
    dst.pixels.resize(static_cast<std::size_t>(dst.stride) * dstH);

    for (UINT y = 0; y < dstH; ++y) {
        const UINT y0 = static_cast<UINT>((static_cast<std::uint64_t>(y) * src.height) / dstH);
        UINT y1 = static_cast<UINT>((static_cast<std::uint64_t>(y + 1) * src.height) / dstH);
        y1 = std::max(y1, y0 + 1);
        y1 = std::min(y1, src.height);

        for (UINT x = 0; x < dstW; ++x) {
            const UINT x0 = static_cast<UINT>((static_cast<std::uint64_t>(x) * src.width) / dstW);
            UINT x1 = static_cast<UINT>((static_cast<std::uint64_t>(x + 1) * src.width) / dstW);
            x1 = std::max(x1, x0 + 1);
            x1 = std::min(x1, src.width);

            std::uint64_t b = 0, g = 0, r = 0, a = 0, n = 0;
            for (UINT sy = y0; sy < y1; ++sy) {
                const auto* row = src.pixels.data() + static_cast<std::size_t>(src.stride) * sy;
                for (UINT sx = x0; sx < x1; ++sx) {
                    const auto* p = row + static_cast<std::size_t>(sx) * 4;
                    b += p[0]; g += p[1]; r += p[2]; a += p[3]; ++n;
                }
            }
            auto* d = dst.pixels.data() + static_cast<std::size_t>(dst.stride) * y + static_cast<std::size_t>(x) * 4;
            d[0] = static_cast<std::uint8_t>(b / n);
            d[1] = static_cast<std::uint8_t>(g / n);
            d[2] = static_cast<std::uint8_t>(r / n);
            d[3] = static_cast<std::uint8_t>(a / n);
        }
    }
    return dst;
}

class PreviewWindow {
public:
    PreviewWindow() = default;
    ~PreviewWindow() { close(); }

    PreviewWindow(const PreviewWindow&) = delete;
    PreviewWindow& operator=(const PreviewWindow&) = delete;

    void open(const wchar_t* title, UINT sourceWidth, UINT sourceHeight, UINT maxWidth = 1280, UINT maxHeight = 720) {
        if (hwnd_) return;
        sourceWidth_ = sourceWidth;
        sourceHeight_ = sourceHeight;

        const double sx = maxWidth > 0 ? static_cast<double>(maxWidth) / static_cast<double>(sourceWidth) : 1.0;
        const double sy = maxHeight > 0 ? static_cast<double>(maxHeight) / static_cast<double>(sourceHeight) : 1.0;
        const double s = std::min(1.0, std::min(sx, sy));
        displayWidth_ = std::max<UINT>(1, static_cast<UINT>(sourceWidth * s));
        displayHeight_ = std::max<UINT>(1, static_cast<UINT>(sourceHeight * s));

        HINSTANCE instance = GetModuleHandleW(nullptr);
        WNDCLASSW wc{};
        wc.lpfnWndProc = &PreviewWindow::WndProc;
        wc.hInstance = instance;
        wc.lpszClassName = L"MFFrameSourceSamplePreviewWindow";
        wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
        RegisterClassW(&wc);

        RECT rc{ 0, 0, static_cast<LONG>(displayWidth_), static_cast<LONG>(displayHeight_) };
        AdjustWindowRect(&rc, WS_OVERLAPPEDWINDOW, FALSE);
        hwnd_ = CreateWindowExW(
            0,
            wc.lpszClassName,
            title,
            WS_OVERLAPPEDWINDOW | WS_VISIBLE,
            CW_USEDEFAULT, CW_USEDEFAULT,
            rc.right - rc.left,
            rc.bottom - rc.top,
            nullptr,
            nullptr,
            instance,
            this);
        if (!hwnd_) {
            throw std::runtime_error("CreateWindowExW(preview)");
        }
    }

    bool update(const BgraImage& fullResolution) {
        if (!hwnd_ || closed_) return false;
        if (fullResolution.empty()) return !closed_;

        if (displayWidth_ != fullResolution.width || displayHeight_ != fullResolution.height) {
            display_ = DownscaleBgraBox(fullResolution, displayWidth_, displayHeight_);
        } else {
            display_ = fullResolution;
        }

        InvalidateRect(hwnd_, nullptr, FALSE);
        UpdateWindow(hwnd_);
        pumpMessages();
        return !closed_;
    }

    void close() noexcept {
        if (hwnd_) {
            DestroyWindow(hwnd_);
            hwnd_ = nullptr;
        }
        closed_ = true;
    }

    bool closed() const noexcept { return closed_; }

private:
    static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
        PreviewWindow* self = nullptr;
        if (msg == WM_NCCREATE) {
            auto* cs = reinterpret_cast<CREATESTRUCTW*>(lp);
            self = static_cast<PreviewWindow*>(cs->lpCreateParams);
            SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
        } else {
            self = reinterpret_cast<PreviewWindow*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
        }

        if (!self) return DefWindowProcW(hwnd, msg, wp, lp);

        switch (msg) {
        case WM_PAINT:
            self->paint();
            return 0;
        case WM_CLOSE:
            self->closed_ = true;
            DestroyWindow(hwnd);
            return 0;
        case WM_DESTROY:
            self->closed_ = true;
            self->hwnd_ = nullptr;
            return 0;
        default:
            return DefWindowProcW(hwnd, msg, wp, lp);
        }
    }

    void paint() {
        PAINTSTRUCT ps{};
        HDC hdc = BeginPaint(hwnd_, &ps);
        if (!display_.empty()) {
            BITMAPV5HEADER bi{};
            bi.bV5Size = sizeof(BITMAPV5HEADER);
            bi.bV5Width = static_cast<LONG>(display_.width);
            bi.bV5Height = -static_cast<LONG>(display_.height);
            bi.bV5Planes = 1;
            bi.bV5BitCount = 32;
            bi.bV5Compression = BI_BITFIELDS;
            bi.bV5RedMask = 0x00FF0000;
            bi.bV5GreenMask = 0x0000FF00;
            bi.bV5BlueMask = 0x000000FF;
            bi.bV5AlphaMask = 0xFF000000;
            SetDIBitsToDevice(
                hdc,
                0,
                0,
                display_.width,
                display_.height,
                0,
                0,
                0,
                display_.height,
                display_.pixels.data(),
                reinterpret_cast<BITMAPINFO*>(&bi),
                DIB_RGB_COLORS);
        }
        EndPaint(hwnd_, &ps);
    }

    void pumpMessages() {
        MSG msg{};
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
    }

    HWND hwnd_ = nullptr;
    bool closed_ = false;
    UINT sourceWidth_ = 0;
    UINT sourceHeight_ = 0;
    UINT displayWidth_ = 0;
    UINT displayHeight_ = 0;
    BgraImage display_;
};

class Mp4BgraWriter {
public:
    Mp4BgraWriter() = default;
    ~Mp4BgraWriter() { close(); }

    Mp4BgraWriter(const Mp4BgraWriter&) = delete;
    Mp4BgraWriter& operator=(const Mp4BgraWriter&) = delete;

    void open(const std::wstring& path,
              UINT width,
              UINT height,
              UINT fpsNum,
              UINT fpsDen,
              UINT32 bitrate) {
        close();
        if (path.empty() || path == L"-") return;
        if (width == 0 || height == 0 || fpsNum == 0 || fpsDen == 0) {
            throw std::runtime_error("Mp4BgraWriter::open: invalid video format");
        }

        width_ = width;
        height_ = height;
        stride_ = width * 4;
        fpsNum_ = fpsNum;
        fpsDen_ = fpsDen;
        frameDuration100ns_ = static_cast<LONGLONG>((10000000LL * static_cast<LONGLONG>(fpsDen)) / static_cast<LONGLONG>(fpsNum));
        if (frameDuration100ns_ <= 0) frameDuration100ns_ = 1;
        frameIndex_ = 0;

        ThrowIfFailed(MFCreateSinkWriterFromURL(path.c_str(), nullptr, nullptr, writer_.GetAddressOf()),
                      "MFCreateSinkWriterFromURL");

        ComPtr<IMFMediaType> outputType;
        ThrowIfFailed(MFCreateMediaType(outputType.GetAddressOf()), "MFCreateMediaType(output)");
        ThrowIfFailed(outputType->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video), "Set output major type");
        ThrowIfFailed(outputType->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_H264), "Set output subtype");
        ThrowIfFailed(outputType->SetUINT32(MF_MT_AVG_BITRATE, bitrate), "Set output bitrate");
        ThrowIfFailed(outputType->SetUINT32(MF_MT_INTERLACE_MODE, MFVideoInterlace_Progressive), "Set interlace");
        ThrowIfFailed(MFSetAttributeSize(outputType.Get(), MF_MT_FRAME_SIZE, width, height), "Set output size");
        ThrowIfFailed(MFSetAttributeRatio(outputType.Get(), MF_MT_FRAME_RATE, fpsNum, fpsDen), "Set output fps");
        ThrowIfFailed(MFSetAttributeRatio(outputType.Get(), MF_MT_PIXEL_ASPECT_RATIO, 1, 1), "Set pixel aspect");
        ThrowIfFailed(writer_->AddStream(outputType.Get(), &streamIndex_), "AddStream");

        ComPtr<IMFMediaType> inputType;
        ThrowIfFailed(MFCreateMediaType(inputType.GetAddressOf()), "MFCreateMediaType(input)");
        ThrowIfFailed(inputType->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video), "Set input major type");
        ThrowIfFailed(inputType->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_RGB32), "Set input subtype");
        ThrowIfFailed(inputType->SetUINT32(MF_MT_INTERLACE_MODE, MFVideoInterlace_Progressive), "Set input interlace");
        ThrowIfFailed(MFSetAttributeSize(inputType.Get(), MF_MT_FRAME_SIZE, width, height), "Set input size");
        ThrowIfFailed(MFSetAttributeRatio(inputType.Get(), MF_MT_FRAME_RATE, fpsNum, fpsDen), "Set input fps");
        ThrowIfFailed(MFSetAttributeRatio(inputType.Get(), MF_MT_PIXEL_ASPECT_RATIO, 1, 1), "Set input pixel aspect");
        ThrowIfFailed(writer_->SetInputMediaType(streamIndex_, inputType.Get(), nullptr), "SetInputMediaType");
        ThrowIfFailed(writer_->BeginWriting(), "BeginWriting");
        opened_ = true;
    }

    bool isOpened() const noexcept { return opened_; }

    void write(const BgraImage& image) {
        if (!opened_) return;
        if (image.width != width_ || image.height != height_ || image.stride < stride_) {
            throw std::runtime_error("Mp4BgraWriter::write: image size mismatch");
        }

        ComPtr<IMFMediaBuffer> buffer;
        const DWORD bytes = static_cast<DWORD>(stride_ * height_);
        ThrowIfFailed(MFCreateMemoryBuffer(bytes, buffer.GetAddressOf()), "MFCreateMemoryBuffer");

        BYTE* dst = nullptr;
        DWORD maxLen = 0;
        DWORD currentLen = 0;
        ThrowIfFailed(buffer->Lock(&dst, &maxLen, &currentLen), "buffer Lock");
        for (UINT y = 0; y < height_; ++y) {
            std::memcpy(dst + static_cast<std::size_t>(stride_) * y,
                        image.pixels.data() + static_cast<std::size_t>(image.stride) * y,
                        stride_);
        }
        ThrowIfFailed(buffer->Unlock(), "buffer Unlock");
        ThrowIfFailed(buffer->SetCurrentLength(bytes), "SetCurrentLength");

        ComPtr<IMFSample> sample;
        ThrowIfFailed(MFCreateSample(sample.GetAddressOf()), "MFCreateSample");
        ThrowIfFailed(sample->AddBuffer(buffer.Get()), "sample AddBuffer");
        ThrowIfFailed(sample->SetSampleTime(static_cast<LONGLONG>(frameIndex_) * frameDuration100ns_), "SetSampleTime");
        ThrowIfFailed(sample->SetSampleDuration(frameDuration100ns_), "SetSampleDuration");
        ThrowIfFailed(writer_->WriteSample(streamIndex_, sample.Get()), "WriteSample");
        ++frameIndex_;
    }

    void close() noexcept {
        if (writer_) {
            if (opened_) {
                writer_->Finalize();
            }
            writer_.Reset();
        }
        opened_ = false;
    }

private:
    ComPtr<IMFSinkWriter> writer_;
    DWORD streamIndex_ = 0;
    UINT width_ = 0;
    UINT height_ = 0;
    UINT stride_ = 0;
    UINT fpsNum_ = 0;
    UINT fpsDen_ = 1;
    UINT64 frameIndex_ = 0;
    LONGLONG frameDuration100ns_ = 0;
    bool opened_ = false;
};

inline bool IsRecordingDisabledPath(const std::wstring& path) {
    return path.empty() || path == L"-";
}

} // namespace MFSample
