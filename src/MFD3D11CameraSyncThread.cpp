#include <MFFrameSource/MFD3D11CameraSyncThread.hpp>

#include <ThreadKit/Threads/WorkerThread.hpp>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <limits>
#include <stdexcept>
#include <thread>
#include <vector>

namespace MFFrameSource {

namespace {

ThreadKit::Queues::QueueOptions MakeQueueOptions(std::size_t capacity,
                                                 ThreadKit::Queues::QueueOverflowPolicy policy) {
    ThreadKit::Queues::QueueOptions options;
    options.maxSize = capacity;
    options.overflowPolicy = policy;
    return options;
}

std::int64_t Median(std::vector<std::int64_t> v) {
    if (v.empty()) return 0;
    std::sort(v.begin(), v.end());
    return v[v.size() / 2];
}

} // namespace

struct MFD3D11CameraSyncThread::Impl {
    MFD3D11CameraFrameQueuePtr leftQueue;
    MFD3D11CameraFrameQueuePtr rightQueue;
    MFD3D11StereoFrameQueuePtr output;
    MFD3D11CameraSyncThreadConfig config;
    ThreadKit::Threads::WorkerThread worker;
    bool opened = false;

    mutable std::mutex baselineMutex;
    std::int64_t baseline = 0;
    std::vector<std::int64_t> baselineSamples;

    std::atomic<std::uint64_t> leftFramesIn{0};
    std::atomic<std::uint64_t> rightFramesIn{0};
    std::atomic<std::uint64_t> pairsOut{0};
    std::atomic<std::uint64_t> droppedLeft{0};
    std::atomic<std::uint64_t> droppedRight{0};
    std::atomic<std::uint64_t> rejectedPairs{0};
    std::atomic<std::uint64_t> outputPushFailures{0};
    std::atomic<std::uint64_t> outputDroppedOldest{0};

    std::int64_t currentBaseline() const noexcept {
        std::lock_guard<std::mutex> lock(baselineMutex);
        return baseline;
    }

    void addBaselineSample(std::int64_t diff) {
        if (!config.estimateBaseline || config.baselineSampleCount == 0) return;
        std::lock_guard<std::mutex> lock(baselineMutex);
        if (baselineSamples.size() < config.baselineSampleCount) {
            baselineSamples.push_back(diff);
            baseline = Median(baselineSamples);
        }
    }

    struct CandidatePair {
        std::size_t leftIndex = 0;
        std::size_t rightIndex = 0;
        std::int64_t diff = 0;
        std::int64_t adjusted = 0;
        bool valid = false;
    };

    CandidatePair findBestPair(const std::deque<MFD3D11CameraFrame>& left,
                               const std::deque<MFD3D11CameraFrame>& right) const {
        CandidatePair best;
        if (left.empty() || right.empty()) return best;
        const std::int64_t b = currentBaseline();
        std::uint64_t bestAbs = std::numeric_limits<std::uint64_t>::max();
        for (std::size_t li = 0; li < left.size(); ++li) {
            if (left[li].sampleTime100ns() < 0) continue;
            for (std::size_t ri = 0; ri < right.size(); ++ri) {
                if (right[ri].sampleTime100ns() < 0) continue;
                const std::int64_t diff = left[li].sampleTime100ns() - right[ri].sampleTime100ns();
                const std::int64_t adjusted = diff - b;
                const auto absValue = static_cast<std::uint64_t>(adjusted < 0 ? -adjusted : adjusted);
                if (!best.valid || absValue < bestAbs) {
                    best.leftIndex = li;
                    best.rightIndex = ri;
                    best.diff = diff;
                    best.adjusted = adjusted;
                    best.valid = true;
                    bestAbs = absValue;
                }
            }
        }
        return best;
    }

    void trimCapacity(std::deque<MFD3D11CameraFrame>& left,
                      std::deque<MFD3D11CameraFrame>& right) {
        while (left.size() > config.candidateCapacity) {
            left.pop_front();
            droppedLeft.fetch_add(1, std::memory_order_relaxed);
        }
        while (right.size() > config.candidateCapacity) {
            right.pop_front();
            droppedRight.fetch_add(1, std::memory_order_relaxed);
        }
    }

    void trimImpossible(std::deque<MFD3D11CameraFrame>& left,
                        std::deque<MFD3D11CameraFrame>& right) {
        if (left.empty() || right.empty()) return;
        const std::int64_t b = currentBaseline();
        const std::int64_t th = config.maxAdjustedDiff100ns;

        bool changed = true;
        while (changed && !left.empty() && !right.empty()) {
            changed = false;
            const auto lf = left.front().sampleTime100ns();
            const auto rb = right.back().sampleTime100ns();
            if (lf >= 0 && rb >= 0 && (lf - rb - b) < -th) {
                left.pop_front();
                droppedLeft.fetch_add(1, std::memory_order_relaxed);
                changed = true;
                continue;
            }

            const auto lb = left.back().sampleTime100ns();
            const auto rf = right.front().sampleTime100ns();
            if (lb >= 0 && rf >= 0 && (lb - rf - b) > th) {
                right.pop_front();
                droppedRight.fetch_add(1, std::memory_order_relaxed);
                changed = true;
            }
        }
    }

    void drainInputs(std::deque<MFD3D11CameraFrame>& left,
                     std::deque<MFD3D11CameraFrame>& right) {
        for (auto& f : leftQueue->tryPopAll()) {
            if (f) {
                left.push_back(std::move(f));
                leftFramesIn.fetch_add(1, std::memory_order_relaxed);
            }
        }
        for (auto& f : rightQueue->tryPopAll()) {
            if (f) {
                right.push_back(std::move(f));
                rightFramesIn.fetch_add(1, std::memory_order_relaxed);
            }
        }
        trimCapacity(left, right);
    }

    void emitPairs(std::deque<MFD3D11CameraFrame>& left,
                   std::deque<MFD3D11CameraFrame>& right) {
        for (;;) {
            auto best = findBestPair(left, right);
            if (!best.valid) return;
            if (std::llabs(best.adjusted) > config.maxAdjustedDiff100ns) {
                rejectedPairs.fetch_add(1, std::memory_order_relaxed);
                trimImpossible(left, right);
                return;
            }

            MFD3D11StereoFrame pair;
            pair.left = std::move(left[best.leftIndex]);
            pair.right = std::move(right[best.rightIndex]);
            pair.timestampDiff100ns = best.diff;
            pair.baselineDiff100ns = currentBaseline();
            pair.adjustedDiff100ns = best.adjusted;
            pair.pairNumber = pairsOut.load(std::memory_order_relaxed);
            pair.pairedTime = std::chrono::steady_clock::now();

            left.erase(left.begin(), left.begin() + static_cast<std::ptrdiff_t>(best.leftIndex + 1));
            right.erase(right.begin(), right.begin() + static_cast<std::ptrdiff_t>(best.rightIndex + 1));

            addBaselineSample(best.diff);
            auto result = output->push(std::move(pair));
            if (ThreadKit::Queues::isPushSucceeded(result)) {
                pairsOut.fetch_add(1, std::memory_order_relaxed);
                if (result == ThreadKit::Queues::QueuePushResult::DroppedOldestAndPushed) {
                    outputDroppedOldest.fetch_add(1, std::memory_order_relaxed);
                }
            } else {
                outputPushFailures.fetch_add(1, std::memory_order_relaxed);
            }
        }
    }

    void workerLoop(const ThreadKit::StopToken& token) {
        std::deque<MFD3D11CameraFrame> left;
        std::deque<MFD3D11CameraFrame> right;

        while (!token.stopRequested()) {
            drainInputs(left, right);
            emitPairs(left, right);

            if (left.empty() || right.empty()) {
                if (left.empty()) {
                    auto f = leftQueue->waitPopFor(std::chrono::milliseconds(1), token);
                    if (f && *f) {
                        left.push_back(std::move(*f));
                        leftFramesIn.fetch_add(1, std::memory_order_relaxed);
                    }
                }
                if (right.empty() && !token.stopRequested()) {
                    auto f = rightQueue->waitPopFor(std::chrono::milliseconds(1), token);
                    if (f && *f) {
                        right.push_back(std::move(*f));
                        rightFramesIn.fetch_add(1, std::memory_order_relaxed);
                    }
                }
            } else {
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
            trimCapacity(left, right);
            trimImpossible(left, right);
            emitPairs(left, right);
        }
    }
};

MFD3D11CameraSyncThread::MFD3D11CameraSyncThread()
    : impl_(std::make_unique<Impl>()) {}

MFD3D11CameraSyncThread::~MFD3D11CameraSyncThread() {
    close();
}

bool MFD3D11CameraSyncThread::open(MFD3D11CameraFrameQueuePtr leftQueue,
                                   MFD3D11CameraFrameQueuePtr rightQueue,
                                   const MFD3D11CameraSyncThreadConfig& config) {
    close();
    if (!leftQueue || !rightQueue) return false;
    if (config.maxAdjustedDiff100ns < 0 || config.candidateCapacity == 0 || config.outputQueueCapacity == 0) return false;

    impl_->leftQueue = std::move(leftQueue);
    impl_->rightQueue = std::move(rightQueue);
    impl_->config = config;
    impl_->output = std::make_shared<MFD3D11StereoFrameQueue>(
        MakeQueueOptions(config.outputQueueCapacity, config.outputOverflowPolicy));
    impl_->baseline = config.initialBaselineDiff100ns;
    impl_->baselineSamples.clear();
    impl_->opened = true;
    return true;
}

void MFD3D11CameraSyncThread::start() {
    if (!impl_ || !impl_->opened) throw std::logic_error("MFD3D11CameraSyncThread::start: not opened");
    if (impl_->worker.joinable()) throw std::logic_error("MFD3D11CameraSyncThread::start: already started");
    impl_->worker.start([this](const ThreadKit::StopToken& token) { impl_->workerLoop(token); });
}

void MFD3D11CameraSyncThread::requestStop() noexcept { if (impl_) impl_->worker.requestStop(); }
void MFD3D11CameraSyncThread::stop() { if (impl_) impl_->worker.stopAndJoin(); }

void MFD3D11CameraSyncThread::close() noexcept {
    if (!impl_) return;
    try { impl_->worker.stopAndJoin(); } catch (...) {}
    if (impl_->output) impl_->output->close();
    impl_->leftQueue.reset();
    impl_->rightQueue.reset();
    impl_->output.reset();
    impl_->opened = false;
}

bool MFD3D11CameraSyncThread::isOpened() const noexcept { return impl_ && impl_->opened; }
bool MFD3D11CameraSyncThread::isRunning() const noexcept { return impl_ && impl_->worker.isRunning(); }
MFD3D11StereoFrameQueuePtr MFD3D11CameraSyncThread::outputQueue() const noexcept { return impl_ ? impl_->output : nullptr; }

MFD3D11CameraSyncThreadStats MFD3D11CameraSyncThread::stats() const noexcept {
    MFD3D11CameraSyncThreadStats s;
    if (!impl_) return s;
    s.leftFramesIn = impl_->leftFramesIn.load(std::memory_order_relaxed);
    s.rightFramesIn = impl_->rightFramesIn.load(std::memory_order_relaxed);
    s.pairsOut = impl_->pairsOut.load(std::memory_order_relaxed);
    s.droppedLeft = impl_->droppedLeft.load(std::memory_order_relaxed);
    s.droppedRight = impl_->droppedRight.load(std::memory_order_relaxed);
    s.rejectedPairs = impl_->rejectedPairs.load(std::memory_order_relaxed);
    s.outputPushFailures = impl_->outputPushFailures.load(std::memory_order_relaxed);
    s.outputDroppedOldest = impl_->outputDroppedOldest.load(std::memory_order_relaxed);
    return s;
}

void MFD3D11CameraSyncThread::resetStats() noexcept {
    if (!impl_) return;
    impl_->leftFramesIn.store(0, std::memory_order_relaxed);
    impl_->rightFramesIn.store(0, std::memory_order_relaxed);
    impl_->pairsOut.store(0, std::memory_order_relaxed);
    impl_->droppedLeft.store(0, std::memory_order_relaxed);
    impl_->droppedRight.store(0, std::memory_order_relaxed);
    impl_->rejectedPairs.store(0, std::memory_order_relaxed);
    impl_->outputPushFailures.store(0, std::memory_order_relaxed);
    impl_->outputDroppedOldest.store(0, std::memory_order_relaxed);
}

std::int64_t MFD3D11CameraSyncThread::currentBaselineDiff100ns() const noexcept {
    return impl_ ? impl_->currentBaseline() : 0;
}

std::exception_ptr MFD3D11CameraSyncThread::workerException() const {
    return impl_ ? impl_->worker.exception() : nullptr;
}

void MFD3D11CameraSyncThread::rethrowWorkerExceptionIfAny() const {
    if (impl_) impl_->worker.rethrowIfException();
}

} // namespace MFFrameSource
