#pragma once

#include "MFD3D11CameraCaptureThread.hpp"

#include <ThreadKit/Queues/BlockingQueue.hpp>
#include <ThreadKit/Queues/QueueCommon.hpp>

#include <chrono>
#include <cstdint>
#include <deque>
#include <memory>
#include <mutex>
#include <optional>

namespace MFFrameSource {

struct MFD3D11StereoFrame {
    MFD3D11CameraFrame left;
    MFD3D11CameraFrame right;

    std::uint64_t pairNumber = 0;
    std::int64_t timestampDiff100ns = 0;      // left - right
    std::int64_t baselineDiff100ns = 0;       // estimated/target left - right
    std::int64_t adjustedDiff100ns = 0;       // timestampDiff100ns - baselineDiff100ns
    std::chrono::steady_clock::time_point pairedTime;

    explicit operator bool() const noexcept { return static_cast<bool>(left) && static_cast<bool>(right); }
};

using MFD3D11StereoFrameQueue = ThreadKit::Queues::BlockingQueue<MFD3D11StereoFrame>;
using MFD3D11StereoFrameQueuePtr = std::shared_ptr<MFD3D11StereoFrameQueue>;

struct MFD3D11CameraSyncThreadConfig {
    std::int64_t maxAdjustedDiff100ns = 50000;
    std::int64_t initialBaselineDiff100ns = 0;

    bool estimateBaseline = true;
    std::size_t baselineSampleCount = 30;

    std::size_t candidateCapacity = 16;

    std::size_t outputQueueCapacity = 2;
    ThreadKit::Queues::QueueOverflowPolicy outputOverflowPolicy =
        ThreadKit::Queues::QueueOverflowPolicy::DropOldest;
};

struct MFD3D11CameraSyncThreadStats {
    std::uint64_t leftFramesIn = 0;
    std::uint64_t rightFramesIn = 0;
    std::uint64_t pairsOut = 0;
    std::uint64_t droppedLeft = 0;
    std::uint64_t droppedRight = 0;
    std::uint64_t rejectedPairs = 0;
    std::uint64_t outputPushFailures = 0;
    std::uint64_t outputDroppedOldest = 0;
};

class MFD3D11CameraSyncThread {
public:
    MFD3D11CameraSyncThread();
    ~MFD3D11CameraSyncThread();

    MFD3D11CameraSyncThread(const MFD3D11CameraSyncThread&) = delete;
    MFD3D11CameraSyncThread& operator=(const MFD3D11CameraSyncThread&) = delete;

    bool open(MFD3D11CameraFrameQueuePtr leftQueue,
              MFD3D11CameraFrameQueuePtr rightQueue,
              const MFD3D11CameraSyncThreadConfig& config = {});

    void start();
    void requestStop() noexcept;
    void stop();
    void close() noexcept;

    bool isOpened() const noexcept;
    bool isRunning() const noexcept;

    MFD3D11StereoFrameQueuePtr outputQueue() const noexcept;
    MFD3D11CameraSyncThreadStats stats() const noexcept;
    void resetStats() noexcept;

    std::int64_t currentBaselineDiff100ns() const noexcept;
    std::exception_ptr workerException() const;
    void rethrowWorkerExceptionIfAny() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace MFFrameSource
