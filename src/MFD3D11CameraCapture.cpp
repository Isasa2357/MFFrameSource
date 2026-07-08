#include <MFFrameSource/MFD3D11CameraCapture.hpp>
#include <MFFrameSource/MFPlatformContext.hpp>

#include "internal/MFComUtil.hpp"
#include "internal/MFCpuSampleReader.hpp"
#include "internal/MFD3D11FrameLeaseControl.hpp"
#include "internal/MFD3D11UploadFramePool.hpp"

#include <mutex>
#include <stdexcept>

namespace MFFrameSource {

MFD3D11CameraFrame::MFD3D11CameraFrame(std::shared_ptr<Storage> storage) noexcept
    : storage_(std::move(storage)) {}

MFD3D11CameraFrame::~MFD3D11CameraFrame() {
    releaseStorage();
}

MFD3D11CameraFrame::MFD3D11CameraFrame(MFD3D11CameraFrame&& other) noexcept
    : storage_(std::move(other.storage_)) {}

MFD3D11CameraFrame& MFD3D11CameraFrame::operator=(MFD3D11CameraFrame&& other) noexcept {
    if (this != &other) {
        releaseStorage();
        storage_ = std::move(other.storage_);
    }
    return *this;
}

void MFD3D11CameraFrame::releaseStorage() noexcept {
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

D3D11CoreLib::D3D11Resource& MFD3D11CameraFrame::resource() {
    if (!storage_) throw std::runtime_error("MFD3D11CameraFrame::resource: empty frame");
    return storage_->outputTexture;
}

const D3D11CoreLib::D3D11Resource& MFD3D11CameraFrame::resource() const {
    if (!storage_) throw std::runtime_error("MFD3D11CameraFrame::resource: empty frame");
    return storage_->outputTexture;
}

ID3D11ShaderResourceView* MFD3D11CameraFrame::srv() const noexcept {
    return storage_ ? storage_->outputSrv.Get() : nullptr;
}

UINT MFD3D11CameraFrame::width() const noexcept { return storage_ ? storage_->width : 0; }
UINT MFD3D11CameraFrame::height() const noexcept { return storage_ ? storage_->height : 0; }
DXGI_FORMAT MFD3D11CameraFrame::format() const noexcept { return storage_ ? storage_->format : DXGI_FORMAT_UNKNOWN; }
std::uint64_t MFD3D11CameraFrame::frameNumber() const noexcept { return storage_ ? storage_->frameNumber : 0; }
std::int64_t MFD3D11CameraFrame::sampleTime100ns() const noexcept { return storage_ ? storage_->sampleTime100ns : -1; }
std::int64_t MFD3D11CameraFrame::sampleDuration100ns() const noexcept { return storage_ ? storage_->sampleDuration100ns : -1; }
std::chrono::steady_clock::time_point MFD3D11CameraFrame::acquiredTime() const noexcept {
    return storage_ ? storage_->acquiredTime : std::chrono::steady_clock::time_point{};
}

bool MFD3D11CameraFrame::isReady() const noexcept {
    return static_cast<bool>(storage_);
}

void MFD3D11CameraFrame::waitReady() const {
    if (!storage_) throw std::runtime_error("MFD3D11CameraFrame::waitReady: empty frame");
    if (!storage_->core) throw std::runtime_error("MFD3D11CameraFrame::waitReady: D3D11Core is no longer available");
    storage_->core->Flush();
}

struct MFD3D11CameraCapture::Impl {
    std::unique_ptr<MFPlatformContext> platform;
    std::shared_ptr<D3D11CoreLib::D3D11Core> core;
    internal::MFCpuSampleReader reader;
    MFD3D11UploadFramePool framePool;
    bool opened = false;
};

MFD3D11CameraCapture::MFD3D11CameraCapture()
    : impl_(std::make_unique<Impl>()) {}

MFD3D11CameraCapture::~MFD3D11CameraCapture() {
    close();
}

bool MFD3D11CameraCapture::open(const MFCameraSelector& selector,
                                const MFCameraCaptureConfig& config,
                                std::shared_ptr<D3D11CoreLib::D3D11Core> d3d11) {
    close();
    lastError_.clear();
    selectedFormat_ = {};

    try {
        if (!d3d11) {
            throw std::runtime_error("MFD3D11CameraCapture::open: null D3D11Core");
        }
        if (!config.input.isComplete()) {
            throw std::runtime_error("MFD3D11CameraCapture::open: config.input must be exact format request");
        }
        const DXGI_FORMAT requestedDxgi = MfSubtypeToDxgiFormat(config.input.subtype);
        if (!IsSupportedCpuUploadInputFormat(requestedDxgi)) {
            throw std::runtime_error("MFD3D11CameraCapture::open: requested subtype is not supported by CPU native upload path");
        }

        impl_->platform = std::make_unique<MFPlatformContext>();
        impl_->core = std::move(d3d11);
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
        lastError_ = internal::MakeError(L"MFD3D11CameraCapture::open", internal::Utf8ToWide(e.what()));
        close();
        return false;
    }
}

MFD3D11CameraReadResult MFD3D11CameraCapture::read() {
    MFD3D11CameraReadResult result;
    if (!impl_ || !impl_->opened) {
        result.status = MFFrameStatus::NotOpened;
        result.error = internal::MakeError(L"MFD3D11CameraCapture::read", L"capture is not opened");
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
    } catch (const D3D11CoreLib::Processing::UnsupportedFormatError& e) {
        result.status = MFFrameStatus::UnsupportedFormat;
        result.error = internal::MakeError(L"MFD3D11CameraCapture::read/D3D11Processing", internal::Utf8ToWide(e.what()));
        lastError_ = result.error;
        return result;
    } catch (const std::exception& e) {
        result.status = MFFrameStatus::Error;
        result.error = internal::MakeError(L"MFD3D11CameraCapture::read", internal::Utf8ToWide(e.what()));
        lastError_ = result.error;
        return result;
    }
}

void MFD3D11CameraCapture::close() noexcept {
    if (!impl_) return;
    impl_->opened = false;
    impl_->framePool.close();
    impl_->reader.close();
    impl_->core.reset();
    impl_->platform.reset();
    selectedFormat_ = {};
}

bool MFD3D11CameraCapture::isOpened() const noexcept {
    return impl_ && impl_->opened;
}

} // namespace MFFrameSource
