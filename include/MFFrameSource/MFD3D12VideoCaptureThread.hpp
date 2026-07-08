#pragma once

#include "MFCommon.hpp"
#include "MFD3D12VideoCapture.hpp"

#include <D3D12Helper/D3D12Core/D3D12Core.hpp>

#include <ThreadKit/Queues/BlockingQueue.hpp>
#include <ThreadKit/Queues/QueueCommon.hpp>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <exception>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace MFFrameSource {

using MFD3D12VideoFrameQueue = ThreadKit::Queues::BlockingQueue<MFD3D12VideoFrame>;
using MFD3D12VideoFrameQueuePtr = std::shared_ptr<MFD3D12VideoFrameQueue>;

struct MFD3D12VideoCaptureThreadConfig {
    std::wstring filePath;
    MFVideoCaptureConfig capture;

    MFVideoPlaybackMode playbackMode = MFVideoPlaybackMode::RealTimeByTimestamp;

    std::size_t defaultQueueCapacity = 2;
    ThreadKit::Queues::QueueOverflowPolicy defaultOverflowPolicy =
        ThreadKit::Queues::QueueOverflowPolicy::DropOldest;

    std::size_t clonePoolSize = 0;
    UINT clonePersistentSrvDescriptorCount = 0;

    bool closeQueuesOnClose = true;
};

struct MFD3D12VideoCaptureThreadStats {
    std::uint64_t framesRead = 0;
    std::uint64_t framesDelivered = 0;
    std::uint64_t endOfStreamCount = 0;
    std::uint64_t readFailures = 0;
    std::uint64_t cloneFailures = 0;
    std::uint64_t queuePushFailures = 0;
    std::uint64_t queuesDroppedOldest = 0;
    std::uint64_t closedQueuesRemoved = 0;
};

class MFD3D12VideoCaptureThread {
public:
    MFD3D12VideoCaptureThread();
    ~MFD3D12VideoCaptureThread();

    MFD3D12VideoCaptureThread(const MFD3D12VideoCaptureThread&) = delete;
    MFD3D12VideoCaptureThread& operator=(const MFD3D12VideoCaptureThread&) = delete;

    bool open(const MFD3D12VideoCaptureThreadConfig& config,
              std::shared_ptr<D3D12CoreLib::D3D12Core> d3d12);

    void start();
    void requestStop() noexcept;
    void stop();
    void close() noexcept;

    bool isOpened() const noexcept;
    bool isRunning() const noexcept;

    MFD3D12VideoFrameQueuePtr createQueue();
    MFD3D12VideoFrameQueuePtr createQueue(
        std::size_t capacity,
        ThreadKit::Queues::QueueOverflowPolicy overflowPolicy =
            ThreadKit::Queues::QueueOverflowPolicy::DropOldest);
    void registerQueue(const MFD3D12VideoFrameQueuePtr& queue);
    void unregisterExpiredAndClosedQueues();

    MFD3D12VideoCaptureThreadStats stats() const noexcept;
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
