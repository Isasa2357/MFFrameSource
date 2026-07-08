#pragma once

#include "MFCommon.hpp"
#include "MFD3D11CameraCapture.hpp"

#include <D3D11Helper/D3D11Core/D3D11Core.hpp>

#include <ThreadKit/Queues/BlockingQueue.hpp>
#include <ThreadKit/Queues/QueueCommon.hpp>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <exception>
#include <memory>
#include <mutex>
#include <vector>

namespace MFFrameSource {

using MFD3D11CameraFrameQueue = ThreadKit::Queues::BlockingQueue<MFD3D11CameraFrame>;
using MFD3D11CameraFrameQueuePtr = std::shared_ptr<MFD3D11CameraFrameQueue>;

struct MFD3D11CameraCaptureThreadConfig {
    MFCameraSelector selector;
    MFCameraCaptureConfig capture;

    std::size_t defaultQueueCapacity = 2;
    ThreadKit::Queues::QueueOverflowPolicy defaultOverflowPolicy =
        ThreadKit::Queues::QueueOverflowPolicy::DropOldest;

    std::size_t clonePoolSize = 0;
    UINT clonePersistentSrvDescriptorCount = 0; // kept for D3D12 parity; D3D11 ignores descriptor count.

    bool closeQueuesOnClose = true;
};

struct MFD3D11CameraCaptureThreadStats {
    std::uint64_t framesRead = 0;
    std::uint64_t framesDelivered = 0;
    std::uint64_t readFailures = 0;
    std::uint64_t cloneFailures = 0;
    std::uint64_t queuePushFailures = 0;
    std::uint64_t queuesDroppedOldest = 0;
    std::uint64_t closedQueuesRemoved = 0;
};

class MFD3D11CameraCaptureThread {
public:
    MFD3D11CameraCaptureThread();
    ~MFD3D11CameraCaptureThread();

    MFD3D11CameraCaptureThread(const MFD3D11CameraCaptureThread&) = delete;
    MFD3D11CameraCaptureThread& operator=(const MFD3D11CameraCaptureThread&) = delete;

    bool open(const MFD3D11CameraCaptureThreadConfig& config,
              std::shared_ptr<D3D11CoreLib::D3D11Core> d3d11);

    void start();
    void requestStop() noexcept;
    void stop();
    void close() noexcept;

    bool isOpened() const noexcept;
    bool isRunning() const noexcept;

    MFD3D11CameraFrameQueuePtr createQueue();
    MFD3D11CameraFrameQueuePtr createQueue(
        std::size_t capacity,
        ThreadKit::Queues::QueueOverflowPolicy overflowPolicy =
            ThreadKit::Queues::QueueOverflowPolicy::DropOldest);
    void registerQueue(const MFD3D11CameraFrameQueuePtr& queue);
    void unregisterExpiredAndClosedQueues();

    MFD3D11CameraCaptureThreadStats stats() const noexcept;
    void resetStats() noexcept;

    const MFErrorInfo& lastError() const noexcept;
    const MFCameraFormatInfo& selectedFormat() const noexcept;
    std::exception_ptr workerException() const;
    void rethrowWorkerExceptionIfAny() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace MFFrameSource
