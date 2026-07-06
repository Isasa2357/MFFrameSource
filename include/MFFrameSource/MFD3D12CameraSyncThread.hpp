#pragma once

#include "MFD3D12CameraCaptureThread.hpp"

#include <ThreadKit/Queues/BlockingQueue.hpp>
#include <ThreadKit/Queues/QueueCommon.hpp>

#include <chrono>
#include <cstdint>
#include <deque>
#include <memory>
#include <mutex>
#include <optional>

namespace MFFrameSource {

struct MFD3D12StereoFrame {
    MFD3D12CameraFrame left;
    MFD3D12CameraFrame right;

    std::uint64_t pairNumber = 0;
    std::int64_t timestampDiff100ns = 0;      // left - right
    std::int64_t baselineDiff100ns = 0;       // estimated/target left - right
    std::int64_t adjustedDiff100ns = 0;       // timestampDiff100ns - baselineDiff100ns
    std::chrono::steady_clock::time_point pairedTime;

    explicit operator bool() const noexcept { return static_cast<bool>(left) && static_cast<bool>(right); }
};

using MFD3D12StereoFrameQueue = ThreadKit::Queues::BlockingQueue<MFD3D12StereoFrame>;
using MFD3D12StereoFrameQueuePtr = std::shared_ptr<MFD3D12StereoFrameQueue>;

struct MFD3D12CameraSyncThreadConfig {
    // Maximum allowed absolute timestamp difference after subtracting baseline.
    // 50000 = 5 ms in Media Foundation 100 ns units.
    std::int64_t maxAdjustedDiff100ns = 50000;

    // Initial target offset, left.sampleTime - right.sampleTime.
    std::int64_t initialBaselineDiff100ns = 0;

    // If true, accepted bootstrap pairs update the baseline using a running median.
    bool estimateBaseline = true;
    std::size_t baselineSampleCount = 30;

    // Candidate buffering per side. Old impossible candidates are dropped.
    std::size_t candidateCapacity = 16;

    std::size_t outputQueueCapacity = 2;
    ThreadKit::Queues::QueueOverflowPolicy outputOverflowPolicy =
        ThreadKit::Queues::QueueOverflowPolicy::DropOldest;
};

struct MFD3D12CameraSyncThreadStats {
    std::uint64_t leftFramesIn = 0;
    std::uint64_t rightFramesIn = 0;
    std::uint64_t pairsOut = 0;
    std::uint64_t droppedLeft = 0;
    std::uint64_t droppedRight = 0;
    std::uint64_t rejectedPairs = 0;
    std::uint64_t outputPushFailures = 0;
    std::uint64_t outputDroppedOldest = 0;
};

class MFD3D12CameraSyncThread {
public:
    MFD3D12CameraSyncThread();
    ~MFD3D12CameraSyncThread();

    MFD3D12CameraSyncThread(const MFD3D12CameraSyncThread&) = delete;
    MFD3D12CameraSyncThread& operator=(const MFD3D12CameraSyncThread&) = delete;

    bool open(MFD3D12CameraFrameQueuePtr leftQueue,
              MFD3D12CameraFrameQueuePtr rightQueue,
              const MFD3D12CameraSyncThreadConfig& config = {});

    void start();
    void requestStop() noexcept;
    void stop();
    void close() noexcept;

    bool isOpened() const noexcept;
    bool isRunning() const noexcept;

    MFD3D12StereoFrameQueuePtr outputQueue() const noexcept;
    MFD3D12CameraSyncThreadStats stats() const noexcept;
    void resetStats() noexcept;

    std::int64_t currentBaselineDiff100ns() const noexcept;
    std::exception_ptr workerException() const;
    void rethrowWorkerExceptionIfAny() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace MFFrameSource
