#pragma once

#include "MFCommon.hpp"
#include "MFD3D12CameraCapture.hpp"

#include <D3D12Helper/D3D12Core/D3D12Core.hpp>

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

using MFD3D12CameraFrameQueue = ThreadKit::Queues::BlockingQueue<MFD3D12CameraFrame>;
using MFD3D12CameraFrameQueuePtr = std::shared_ptr<MFD3D12CameraFrameQueue>;

struct MFD3D12CameraCaptureThreadConfig {
    MFCameraSelector selector;
    MFCameraCaptureConfig capture;

    // Queue defaults for createQueue(). 0 means unlimited, but bounded DropOldest
    // queues are strongly recommended for realtime camera pipelines.
    std::size_t defaultQueueCapacity = 2;
    ThreadKit::Queues::QueueOverflowPolicy defaultOverflowPolicy =
        ThreadKit::Queues::QueueOverflowPolicy::DropOldest;

    // Number of reusable D3D12 output textures used for per-queue GPU copies.
    // If 0, a conservative value based on framePoolSize and expected queue count is used.
    std::size_t clonePoolSize = 0;

    // Descriptor capacity for cloned frames. If 0, clonePoolSize is used.
    UINT clonePersistentSrvDescriptorCount = 0;

    // If true, queues are closed by close()/stopAndCloseQueues().
    bool closeQueuesOnClose = true;
};

struct MFD3D12CameraCaptureThreadStats {
    std::uint64_t framesRead = 0;
    std::uint64_t framesDelivered = 0;
    std::uint64_t readFailures = 0;
    std::uint64_t cloneFailures = 0;
    std::uint64_t queuePushFailures = 0;
    std::uint64_t queuesDroppedOldest = 0;
    std::uint64_t closedQueuesRemoved = 0;
};

class MFD3D12CameraCaptureThread {
public:
    MFD3D12CameraCaptureThread();
    ~MFD3D12CameraCaptureThread();

    MFD3D12CameraCaptureThread(const MFD3D12CameraCaptureThread&) = delete;
    MFD3D12CameraCaptureThread& operator=(const MFD3D12CameraCaptureThread&) = delete;

    bool open(const MFD3D12CameraCaptureThreadConfig& config,
              std::shared_ptr<D3D12CoreLib::D3D12Core> d3d12);

    void start();
    void requestStop() noexcept;
    void stop();
    void close() noexcept;

    bool isOpened() const noexcept;
    bool isRunning() const noexcept;

    MFD3D12CameraFrameQueuePtr createQueue();
    MFD3D12CameraFrameQueuePtr createQueue(
        std::size_t capacity,
        ThreadKit::Queues::QueueOverflowPolicy overflowPolicy =
            ThreadKit::Queues::QueueOverflowPolicy::DropOldest);
    void registerQueue(const MFD3D12CameraFrameQueuePtr& queue);
    void unregisterExpiredAndClosedQueues();

    MFD3D12CameraCaptureThreadStats stats() const noexcept;
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
