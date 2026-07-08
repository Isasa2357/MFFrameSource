#pragma once

#include "MFCommon.hpp"
#include "MFD3D11VideoCapture.hpp"

#include <D3D11Helper/D3D11Core/D3D11Core.hpp>

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

using MFD3D11VideoFrameQueue = ThreadKit::Queues::BlockingQueue<MFD3D11VideoFrame>;
using MFD3D11VideoFrameQueuePtr = std::shared_ptr<MFD3D11VideoFrameQueue>;

enum class MFVideoPlaybackMode;

struct MFD3D11VideoCaptureThreadConfig {
    std::wstring filePath;
    MFVideoCaptureConfig capture;

    MFVideoPlaybackMode playbackMode = MFVideoPlaybackMode::RealTimeByTimestamp;

    std::size_t defaultQueueCapacity = 2;
    ThreadKit::Queues::QueueOverflowPolicy defaultOverflowPolicy =
        ThreadKit::Queues::QueueOverflowPolicy::DropOldest;

    std::size_t clonePoolSize = 0;
    UINT clonePersistentSrvDescriptorCount = 0; // kept for D3D12 parity; D3D11 ignores descriptor count.

    bool closeQueuesOnClose = true;
};

struct MFD3D11VideoCaptureThreadStats {
    std::uint64_t framesRead = 0;
    std::uint64_t framesDelivered = 0;
    std::uint64_t endOfStreamCount = 0;
    std::uint64_t readFailures = 0;
    std::uint64_t cloneFailures = 0;
    std::uint64_t queuePushFailures = 0;
    std::uint64_t queuesDroppedOldest = 0;
    std::uint64_t closedQueuesRemoved = 0;
};

class MFD3D11VideoCaptureThread {
public:
    MFD3D11VideoCaptureThread();
    ~MFD3D11VideoCaptureThread();

    MFD3D11VideoCaptureThread(const MFD3D11VideoCaptureThread&) = delete;
    MFD3D11VideoCaptureThread& operator=(const MFD3D11VideoCaptureThread&) = delete;

    bool open(const MFD3D11VideoCaptureThreadConfig& config,
              std::shared_ptr<D3D11CoreLib::D3D11Core> d3d11);

    void start();
    void requestStop() noexcept;
    void stop();
    void close() noexcept;

    bool isOpened() const noexcept;
    bool isRunning() const noexcept;

    MFD3D11VideoFrameQueuePtr createQueue();
    MFD3D11VideoFrameQueuePtr createQueue(
        std::size_t capacity,
        ThreadKit::Queues::QueueOverflowPolicy overflowPolicy =
            ThreadKit::Queues::QueueOverflowPolicy::DropOldest);
    void registerQueue(const MFD3D11VideoFrameQueuePtr& queue);
    void unregisterExpiredAndClosedQueues();

    MFD3D11VideoCaptureThreadStats stats() const noexcept;
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
