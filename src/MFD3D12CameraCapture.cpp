#include <MFFrameSource/MFD3D12CameraCapture.hpp>
#include <MFFrameSource/MFPlatformContext.hpp>

#include "internal/MFComUtil.hpp"
#include "internal/MFCpuSampleReader.hpp"
#include "internal/MFD3D12UploadFramePool.hpp"
#include "internal/MFD3D12FrameLeaseControl.hpp"

#include <mutex>
#include <sstream>
#include <stdexcept>

namespace MFFrameSource {

MFD3D12CameraFrame::MFD3D12CameraFrame(std::shared_ptr<Storage> storage) noexcept
    : storage_(std::move(storage)) {}

MFD3D12CameraFrame::~MFD3D12CameraFrame() {
    releaseStorage();
}

MFD3D12CameraFrame::MFD3D12CameraFrame(MFD3D12CameraFrame&& other) noexcept
    : storage_(std::move(other.storage_)) {}

MFD3D12CameraFrame& MFD3D12CameraFrame::operator=(MFD3D12CameraFrame&& other) noexcept {
    if (this != &other) {
        releaseStorage();
        storage_ = std::move(other.storage_);
    }
    return *this;
}

void MFD3D12CameraFrame::releaseStorage() noexcept {
    if (!storage_) return;
    if (auto control = storage_->leaseControl.lock()) {
        {
            std::lock_guard<std::mutex> lock(control->mutex);
            storage_->leased = false;
        }
        control->cv.notify_all();
    } else {
        storage_->leased = false;
    }
    storage_.reset();
}

D3D12CoreLib::D3D12Resource& MFD3D12CameraFrame::resource() {
    if (!storage_) throw std::runtime_error("MFD3D12CameraFrame::resource: empty frame");
    return storage_->outputTexture;
}

const D3D12CoreLib::D3D12Resource& MFD3D12CameraFrame::resource() const {
    if (!storage_) throw std::runtime_error("MFD3D12CameraFrame::resource: empty frame");
    return storage_->outputTexture;
}

D3D12CoreLib::D3D12DescriptorHandle MFD3D12CameraFrame::srv() const noexcept {
    return storage_ ? storage_->outputSrv : D3D12CoreLib::D3D12DescriptorHandle{};
}

ID3D12DescriptorHeap* MFD3D12CameraFrame::srvDescriptorHeap() const noexcept {
    return storage_ ? storage_->outputSrvHeap : nullptr;
}

UINT MFD3D12CameraFrame::width() const noexcept { return storage_ ? storage_->width : 0; }
UINT MFD3D12CameraFrame::height() const noexcept { return storage_ ? storage_->height : 0; }
DXGI_FORMAT MFD3D12CameraFrame::format() const noexcept { return storage_ ? storage_->format : DXGI_FORMAT_UNKNOWN; }
std::uint64_t MFD3D12CameraFrame::frameNumber() const noexcept { return storage_ ? storage_->frameNumber : 0; }
std::int64_t MFD3D12CameraFrame::sampleTime100ns() const noexcept { return storage_ ? storage_->sampleTime100ns : -1; }
std::int64_t MFD3D12CameraFrame::sampleDuration100ns() const noexcept { return storage_ ? storage_->sampleDuration100ns : -1; }
std::chrono::steady_clock::time_point MFD3D12CameraFrame::acquiredTime() const noexcept {
    return storage_ ? storage_->acquiredTime : std::chrono::steady_clock::time_point{};
}

bool MFD3D12CameraFrame::isReady() const {
    if (!storage_) return false;
    if (storage_->readyFenceValue == 0) return true;
    if (!storage_->core) return false;
    return storage_->core->DirectQueue().Fence().IsCompleted(storage_->readyFenceValue);
}

void MFD3D12CameraFrame::waitReady() const {
    if (!storage_) throw std::runtime_error("MFD3D12CameraFrame::waitReady: empty frame");
    if (storage_->readyFenceValue == 0) return;
    if (!storage_->core) throw std::runtime_error("MFD3D12CameraFrame::waitReady: D3D12Core is no longer available");
    storage_->core->DirectQueue().WaitForFenceValue(storage_->readyFenceValue);
}

UINT64 MFD3D12CameraFrame::readyFenceValue() const noexcept {
    return storage_ ? storage_->readyFenceValue : 0;
}

D3D12_RESOURCE_STATES MFD3D12CameraFrame::resourceState() const noexcept {
    return storage_ ? storage_->resourceState : D3D12_RESOURCE_STATE_COMMON;
}

struct MFD3D12CameraCapture::Impl {
    std::unique_ptr<MFPlatformContext> platform;
    std::shared_ptr<D3D12CoreLib::D3D12Core> core;
    internal::MFCpuSampleReader reader;
    MFD3D12UploadFramePool framePool;
    bool opened = false;
};

MFD3D12CameraCapture::MFD3D12CameraCapture()
    : impl_(std::make_unique<Impl>()) {}

MFD3D12CameraCapture::~MFD3D12CameraCapture() {
    close();
}

bool MFD3D12CameraCapture::open(const MFCameraSelector& selector,
                                const MFCameraCaptureConfig& config,
                                std::shared_ptr<D3D12CoreLib::D3D12Core> d3d12) {
    close();
    lastError_.clear();
    selectedFormat_ = {};

    try {
        if (!d3d12) {
            throw std::runtime_error("MFD3D12CameraCapture::open: null D3D12Core");
        }
        if (!config.input.isComplete()) {
            throw std::runtime_error("MFD3D12CameraCapture::open: config.input must be exact format request");
        }
        const DXGI_FORMAT requestedDxgi = MfSubtypeToDxgiFormat(config.input.subtype);
        if (!IsSupportedCpuUploadInputFormat(requestedDxgi)) {
            throw std::runtime_error("MFD3D12CameraCapture::open: requested subtype is not supported by CPU native upload path");
        }

        impl_->platform = std::make_unique<MFPlatformContext>();
        impl_->core = std::move(d3d12);
        impl_->reader.open(selector, config);
        selectedFormat_ = impl_->reader.selectedFormat();
        impl_->framePool.initialize(impl_->core, config, selectedFormat_);
        impl_->opened = true;
        return true;
    } catch (const internal::HResultException& e) {
        lastError_ = internal::MakeError(e.hr(), e.where());
        close();
        return false;
    } catch (const std::exception& e) {
        lastError_ = internal::MakeError(L"MFD3D12CameraCapture::open", internal::Utf8ToWide(e.what()));
        close();
        return false;
    }
}

MFD3D12CameraReadResult MFD3D12CameraCapture::read() {
    MFD3D12CameraReadResult result;
    if (!impl_ || !impl_->opened) {
        result.status = MFFrameStatus::NotOpened;
        result.error = internal::MakeError(L"MFD3D12CameraCapture::read", L"capture is not opened");
        lastError_ = result.error;
        return result;
    }

    auto cpu = impl_->reader.read();
    if (!cpu.ok()) {
        result.status = cpu.status;
        result.error = cpu.error;
        if (result.error) lastError_ = result.error;
        return result;
    }

    try {
        result.frame = impl_->framePool.process(cpu.sample);
        cpu.sample.unlock();
        result.status = MFFrameStatus::Ok;
        result.error.clear();
        lastError_.clear();
        return result;
    } catch (const internal::HResultException& e) {
        result.status = MFFrameStatus::Error;
        result.error = internal::MakeError(e.hr(), e.where());
        lastError_ = result.error;
        return result;
    } catch (const D3D12CoreLib::Processing::UnsupportedFormatError& e) {
        result.status = MFFrameStatus::UnsupportedFormat;
        result.error = internal::MakeError(L"MFD3D12CameraCapture::read/D3D12Processing", internal::Utf8ToWide(e.what()));
        lastError_ = result.error;
        return result;
    } catch (const std::exception& e) {
        result.status = MFFrameStatus::Error;
        result.error = internal::MakeError(L"MFD3D12CameraCapture::read", internal::Utf8ToWide(e.what()));
        lastError_ = result.error;
        return result;
    }
}

void MFD3D12CameraCapture::close() noexcept {
    if (!impl_) return;
    impl_->opened = false;
    impl_->framePool.close();
    impl_->reader.close();
    impl_->core.reset();
    impl_->platform.reset();
    selectedFormat_ = {};
}

bool MFD3D12CameraCapture::isOpened() const noexcept {
    return impl_ && impl_->opened;
}

} // namespace MFFrameSource
