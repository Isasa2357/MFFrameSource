#include <MFFrameSource/MFD3D11CameraCaptureThread.hpp>

#include "internal/MFComUtil.hpp"
#include "internal/MFD3D11FrameCloner.hpp"

#include <ThreadKit/Threads/WorkerThread.hpp>

#include <Windows.h>
#include <objbase.h>

#include <algorithm>
#include <chrono>
#include <stdexcept>
#include <thread>

namespace MFFrameSource {

namespace {

class ComMtaScope {
public:
    ComMtaScope() {
        HRESULT hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        if (SUCCEEDED(hr)) initialized_ = true;
        else if (hr != RPC_E_CHANGED_MODE) internal::ThrowIfFailed(hr, L"CoInitializeEx(D3D11 capture thread)");
    }
    ~ComMtaScope() { if (initialized_) CoUninitialize(); }
    ComMtaScope(const ComMtaScope&) = delete;
    ComMtaScope& operator=(const ComMtaScope&) = delete;
private:
    bool initialized_ = false;
};

std::size_t DefaultClonePoolSize(const MFD3D11CameraCaptureThreadConfig& config) {
    if (config.clonePoolSize != 0) return config.clonePoolSize;
    return std::max<std::size_t>(config.capture.framePoolSize * 2, 8);
}

ThreadKit::Queues::QueueOptions MakeQueueOptions(std::size_t capacity,
                                                 ThreadKit::Queues::QueueOverflowPolicy policy) {
    ThreadKit::Queues::QueueOptions options;
    options.maxSize = capacity;
    options.overflowPolicy = policy;
    return options;
}

} // namespace

struct MFD3D11CameraCaptureThread::Impl {
    MFD3D11CameraCapture capture;
    internal::MFD3D11FrameCloner cloner;
    ThreadKit::Threads::WorkerThread worker;

    MFD3D11CameraCaptureThreadConfig config;
    std::shared_ptr<D3D11CoreLib::D3D11Core> core;

    mutable std::mutex mutex;
    std::vector<std::weak_ptr<MFD3D11CameraFrameQueue>> queues;
    bool opened = false;

    mutable std::mutex errorMutex;
    MFErrorInfo lastError;

    std::atomic<std::uint64_t> framesRead{0};
    std::atomic<std::uint64_t> framesDelivered{0};
    std::atomic<std::uint64_t> readFailures{0};
    std::atomic<std::uint64_t> cloneFailures{0};
    std::atomic<std::uint64_t> queuePushFailures{0};
    std::atomic<std::uint64_t> queuesDroppedOldest{0};
    std::atomic<std::uint64_t> closedQueuesRemoved{0};

    void setLastError(const MFErrorInfo& e) {
        std::lock_guard<std::mutex> lock(errorMutex);
        lastError = e;
    }

    std::vector<MFD3D11CameraFrameQueuePtr> liveQueuesSnapshot() {
        std::vector<MFD3D11CameraFrameQueuePtr> out;
        std::lock_guard<std::mutex> lock(mutex);
        auto it = queues.begin();
        while (it != queues.end()) {
            auto q = it->lock();
            if (!q || q->isClosed()) {
                it = queues.erase(it);
                closedQueuesRemoved.fetch_add(1, std::memory_order_relaxed);
            } else {
                out.push_back(std::move(q));
                ++it;
            }
        }
        return out;
    }

    void workerLoop(const ThreadKit::StopToken& token) {
        ComMtaScope com;
        while (!token.stopRequested()) {
            auto rr = capture.read();
            if (!rr.ok()) {
                readFailures.fetch_add(1, std::memory_order_relaxed);
                if (rr.error) setLastError(rr.error);
                if (rr.status == MFFrameStatus::EndOfStream || rr.status == MFFrameStatus::NotOpened) {
                    break;
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
                continue;
            }

            framesRead.fetch_add(1, std::memory_order_relaxed);
            auto queuesSnapshot = liveQueuesSnapshot();
            if (queuesSnapshot.empty()) {
                continue;
            }

            for (auto& q : queuesSnapshot) {
                if (token.stopRequested()) break;
                try {
                    auto cloned = cloner.clone(rr.frame);
                    auto pushResult = q->push(std::move(cloned));
                    if (ThreadKit::Queues::isPushSucceeded(pushResult)) {
                        framesDelivered.fetch_add(1, std::memory_order_relaxed);
                        if (pushResult == ThreadKit::Queues::QueuePushResult::DroppedOldestAndPushed) {
                            queuesDroppedOldest.fetch_add(1, std::memory_order_relaxed);
                        }
                    } else {
                        queuePushFailures.fetch_add(1, std::memory_order_relaxed);
                    }
                } catch (const std::exception& e) {
                    cloneFailures.fetch_add(1, std::memory_order_relaxed);
                    setLastError(internal::MakeError(L"MFD3D11CameraCaptureThread::clone/push", internal::Utf8ToWide(e.what())));
                }
            }
        }
    }
};

MFD3D11CameraCaptureThread::MFD3D11CameraCaptureThread()
    : impl_(std::make_unique<Impl>()) {}

MFD3D11CameraCaptureThread::~MFD3D11CameraCaptureThread() {
    close();
}

bool MFD3D11CameraCaptureThread::open(const MFD3D11CameraCaptureThreadConfig& config,
                                      std::shared_ptr<D3D11CoreLib::D3D11Core> d3d11) {
    close();
    try {
        if (!d3d11) throw std::runtime_error("MFD3D11CameraCaptureThread::open: null D3D11Core");
        if (!impl_->capture.open(config.selector, config.capture, d3d11)) {
            impl_->setLastError(impl_->capture.lastError());
            return false;
        }

        const UINT outW = config.capture.outputWidth ? config.capture.outputWidth : impl_->capture.selectedFormat().width;
        const UINT outH = config.capture.outputHeight ? config.capture.outputHeight : impl_->capture.selectedFormat().height;
        const std::size_t clonePoolSize = DefaultClonePoolSize(config);

        impl_->cloner.initialize(d3d11, outW, outH, config.capture.outputFormat,
                                 clonePoolSize, config.clonePersistentSrvDescriptorCount,
                                 config.capture.waitForGpuCompletionOnRead);
        impl_->config = config;
        impl_->core = std::move(d3d11);
        impl_->opened = true;
        impl_->lastError.clear();
        return true;
    } catch (const std::exception& e) {
        impl_->setLastError(internal::MakeError(L"MFD3D11CameraCaptureThread::open", internal::Utf8ToWide(e.what())));
        close();
        return false;
    }
}

void MFD3D11CameraCaptureThread::start() {
    if (!impl_ || !impl_->opened) throw std::logic_error("MFD3D11CameraCaptureThread::start: not opened");
    if (impl_->worker.joinable()) throw std::logic_error("MFD3D11CameraCaptureThread::start: already started");
    impl_->worker.start([this](const ThreadKit::StopToken& token) { impl_->workerLoop(token); });
}

void MFD3D11CameraCaptureThread::requestStop() noexcept {
    if (impl_) impl_->worker.requestStop();
}

void MFD3D11CameraCaptureThread::stop() {
    if (impl_) impl_->worker.stopAndJoin();
}

void MFD3D11CameraCaptureThread::close() noexcept {
    if (!impl_) return;
    try { impl_->worker.stopAndJoin(); } catch (...) {}
    if (impl_->config.closeQueuesOnClose) {
        std::lock_guard<std::mutex> lock(impl_->mutex);
        for (auto& w : impl_->queues) if (auto q = w.lock()) q->close();
    }
    impl_->queues.clear();
    impl_->cloner.close();
    impl_->capture.close();
    impl_->core.reset();
    impl_->opened = false;
}

bool MFD3D11CameraCaptureThread::isOpened() const noexcept { return impl_ && impl_->opened; }
bool MFD3D11CameraCaptureThread::isRunning() const noexcept { return impl_ && impl_->worker.isRunning(); }

MFD3D11CameraFrameQueuePtr MFD3D11CameraCaptureThread::createQueue() {
    if (!impl_) throw std::logic_error("MFD3D11CameraCaptureThread::createQueue: invalid object");
    return createQueue(impl_->config.defaultQueueCapacity, impl_->config.defaultOverflowPolicy);
}

MFD3D11CameraFrameQueuePtr MFD3D11CameraCaptureThread::createQueue(
    std::size_t capacity,
    ThreadKit::Queues::QueueOverflowPolicy overflowPolicy) {
    auto q = std::make_shared<MFD3D11CameraFrameQueue>(MakeQueueOptions(capacity, overflowPolicy));
    registerQueue(q);
    return q;
}

void MFD3D11CameraCaptureThread::registerQueue(const MFD3D11CameraFrameQueuePtr& queue) {
    if (!queue) throw std::invalid_argument("MFD3D11CameraCaptureThread::registerQueue: null queue");
    std::lock_guard<std::mutex> lock(impl_->mutex);
    impl_->queues.emplace_back(queue);
}

void MFD3D11CameraCaptureThread::unregisterExpiredAndClosedQueues() {
    if (impl_) (void)impl_->liveQueuesSnapshot();
}

MFD3D11CameraCaptureThreadStats MFD3D11CameraCaptureThread::stats() const noexcept {
    MFD3D11CameraCaptureThreadStats s;
    if (!impl_) return s;
    s.framesRead = impl_->framesRead.load(std::memory_order_relaxed);
    s.framesDelivered = impl_->framesDelivered.load(std::memory_order_relaxed);
    s.readFailures = impl_->readFailures.load(std::memory_order_relaxed);
    s.cloneFailures = impl_->cloneFailures.load(std::memory_order_relaxed);
    s.queuePushFailures = impl_->queuePushFailures.load(std::memory_order_relaxed);
    s.queuesDroppedOldest = impl_->queuesDroppedOldest.load(std::memory_order_relaxed);
    s.closedQueuesRemoved = impl_->closedQueuesRemoved.load(std::memory_order_relaxed);
    return s;
}

void MFD3D11CameraCaptureThread::resetStats() noexcept {
    if (!impl_) return;
    impl_->framesRead.store(0, std::memory_order_relaxed);
    impl_->framesDelivered.store(0, std::memory_order_relaxed);
    impl_->readFailures.store(0, std::memory_order_relaxed);
    impl_->cloneFailures.store(0, std::memory_order_relaxed);
    impl_->queuePushFailures.store(0, std::memory_order_relaxed);
    impl_->queuesDroppedOldest.store(0, std::memory_order_relaxed);
    impl_->closedQueuesRemoved.store(0, std::memory_order_relaxed);
}

const MFErrorInfo& MFD3D11CameraCaptureThread::lastError() const noexcept {
    return impl_->lastError;
}

const MFCameraFormatInfo& MFD3D11CameraCaptureThread::selectedFormat() const noexcept {
    return impl_->capture.selectedFormat();
}

std::exception_ptr MFD3D11CameraCaptureThread::workerException() const {
    return impl_ ? impl_->worker.exception() : nullptr;
}

void MFD3D11CameraCaptureThread::rethrowWorkerExceptionIfAny() const {
    if (impl_) impl_->worker.rethrowIfException();
}

} // namespace MFFrameSource
