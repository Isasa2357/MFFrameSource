#include <MFFrameSource/MFD3D11VideoCapture.hpp>
#include <MFFrameSource/MFPlatformContext.hpp>

#include "internal/MFComUtil.hpp"
#include "internal/MFVideoFileSampleReader.hpp"
#include "internal/MFD3D11UploadFramePool.hpp"

#include <stdexcept>

namespace MFFrameSource {
namespace {

MFCameraCaptureConfig ToUploadConfig(const MFVideoCaptureConfig& cfg) {
    MFCameraCaptureConfig out;
    out.input = cfg.input;
    out.outputWidth = cfg.outputWidth;
    out.outputHeight = cfg.outputHeight;
    out.outputFormat = cfg.outputFormat;
    out.processingShaderDirectory = cfg.processingShaderDirectory;
    out.framePoolSize = cfg.framePoolSize;
    out.waitForGpuCompletionOnRead = cfg.waitForGpuCompletionOnRead;
    out.uploadRingSizeBytes = cfg.uploadRingSizeBytes;
    out.transientCbvSrvUavDescriptorCount = cfg.transientCbvSrvUavDescriptorCount;
    out.transientSamplerDescriptorCount = cfg.transientSamplerDescriptorCount;
    out.persistentSrvDescriptorCount = cfg.persistentSrvDescriptorCount;
    return out;
}

bool SameInputFormat(const MFCameraFormatInfo& a, const MFCameraFormatInfo& b) noexcept {
    return internal::GuidEquals(a.subtype, b.subtype) &&
           a.dxgiFormat == b.dxgiFormat &&
           a.width == b.width &&
           a.height == b.height;
}

bool SampleMatchesFormat(const internal::MFCpuVideoSample& sample,
                         const MFCameraFormatInfo& format) noexcept {
    return internal::GuidEquals(sample.mfSubtype, format.subtype) &&
           sample.dxgiFormat == format.dxgiFormat &&
           sample.width == format.width &&
           sample.height == format.height;
}

} // namespace

struct MFD3D11VideoCapture::Impl {
    std::unique_ptr<MFPlatformContext> platform;
    std::shared_ptr<D3D11CoreLib::D3D11Core> core;
    internal::MFVideoFileSampleReader reader;
    MFD3D11UploadFramePool framePool;
    MFVideoCaptureConfig config;
    bool opened = false;
};

MFD3D11VideoCapture::MFD3D11VideoCapture()
    : impl_(std::make_unique<Impl>()) {}

MFD3D11VideoCapture::~MFD3D11VideoCapture() {
    close();
}

bool MFD3D11VideoCapture::open(const std::wstring& filePath,
                               const MFVideoCaptureConfig& config,
                               std::shared_ptr<D3D11CoreLib::D3D11Core> d3d11) {
    close();
    lastError_.clear();
    selectedFormat_ = {};

    try {
        if (!d3d11) {
            throw std::runtime_error("MFD3D11VideoCapture::open: null D3D11Core");
        }
        if (!config.input.isComplete()) {
            throw std::runtime_error("MFD3D11VideoCapture::open: config.input must be exact decoded format request");
        }
        const DXGI_FORMAT requestedDxgi = MfSubtypeToDxgiFormat(config.input.subtype);
        if (!IsSupportedCpuUploadInputFormat(requestedDxgi)) {
            throw std::runtime_error("MFD3D11VideoCapture::open: requested subtype is not supported by CPU native upload path");
        }

        impl_->platform = std::make_unique<MFPlatformContext>();
        impl_->core = std::move(d3d11);
        impl_->config = config;
        impl_->reader.open(filePath, config);
        selectedFormat_ = impl_->reader.selectedFormat();
        impl_->framePool.initialize(impl_->core, ToUploadConfig(config), selectedFormat_);
        impl_->opened = true;
        return true;
    } catch (const internal::HResultException& e) {
        lastError_ = internal::MakeError(e.hr(), e.where());
        close();
        return false;
    } catch (const std::exception& e) {
        lastError_ = internal::MakeError(L"MFD3D11VideoCapture::open", internal::Utf8ToWide(e.what()));
        close();
        return false;
    }
}

MFD3D11VideoReadResult MFD3D11VideoCapture::read() {
    MFD3D11VideoReadResult result;
    if (!impl_ || !impl_->opened) {
        result.status = MFFrameStatus::NotOpened;
        result.error = internal::MakeError(L"MFD3D11VideoCapture::read", L"capture is not opened");
        lastError_ = result.error;
        return result;
    }

    auto cpu = impl_->reader.read();
    if (cpu.status == MFFrameStatus::EndOfStream && impl_->config.loop) {
        if (!impl_->reader.seek(impl_->config.startPosition100ns)) {
            result.status = MFFrameStatus::Error;
            result.error = internal::MakeError(L"MFD3D11VideoCapture::read", L"loop seek failed");
            lastError_ = result.error;
            return result;
        }
        cpu = impl_->reader.read();
    }

    if (!cpu.ok()) {
        result.status = cpu.status;
        result.error = cpu.error;
        if (result.error) lastError_ = result.error;
        return result;
    }

    try {
        const MFCameraFormatInfo readerFormat = impl_->reader.selectedFormat();
        if (!SameInputFormat(selectedFormat_, readerFormat) ||
            !SampleMatchesFormat(cpu.sample, selectedFormat_)) {
            // Video decoders may update the CPU sample layout after the first read
            // (for example, visible 1404px height -> coded 1408px height). Rebuild
            // the upload pool to match the actual sample layout while preserving the
            // requested output size from config.
            selectedFormat_ = readerFormat;
            impl_->framePool.close();
            impl_->framePool.initialize(impl_->core, ToUploadConfig(impl_->config), selectedFormat_);
        }

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
        result.error = internal::MakeError(L"MFD3D11VideoCapture::read/D3D11Processing", internal::Utf8ToWide(e.what()));
        lastError_ = result.error;
        return result;
    } catch (const std::exception& e) {
        result.status = MFFrameStatus::Error;
        result.error = internal::MakeError(L"MFD3D11VideoCapture::read", internal::Utf8ToWide(e.what()));
        lastError_ = result.error;
        return result;
    }
}

bool MFD3D11VideoCapture::seek(std::int64_t position100ns) {
    if (!impl_ || !impl_->opened) {
        lastError_ = internal::MakeError(L"MFD3D11VideoCapture::seek", L"capture is not opened");
        return false;
    }
    const bool ok = impl_->reader.seek(position100ns);
    if (!ok) {
        lastError_ = internal::MakeError(L"MFD3D11VideoCapture::seek", L"IMFSourceReader::SetCurrentPosition failed");
    }
    return ok;
}

void MFD3D11VideoCapture::close() noexcept {
    if (!impl_) return;
    impl_->opened = false;
    impl_->framePool.close();
    impl_->reader.close();
    impl_->core.reset();
    impl_->platform.reset();
    impl_->config = {};
    selectedFormat_ = {};
}

bool MFD3D11VideoCapture::isOpened() const noexcept {
    return impl_ && impl_->opened;
}

std::int64_t MFD3D11VideoCapture::duration100ns() const noexcept {
    return impl_ ? impl_->reader.duration100ns() : -1;
}

const std::wstring& MFD3D11VideoCapture::filePath() const noexcept {
    static const std::wstring empty;
    return impl_ ? impl_->reader.filePath() : empty;
}

} // namespace MFFrameSource
