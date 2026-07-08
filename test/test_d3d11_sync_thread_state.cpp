#include "TestHarness.hpp"

#include <MFFrameSource/MFD3D11CameraSyncThread.hpp>

using namespace MFFrameSource;

MFTEST_MAIN({
    MFD3D11CameraSyncThread sync;
    MFTEST_CHECK(!sync.isOpened());
    MFTEST_CHECK(!sync.isRunning());
    MFTEST_CHECK(!sync.open(nullptr, nullptr));

    ThreadKit::Queues::QueueOptions qopt;
    qopt.maxSize = 2;
    qopt.overflowPolicy = ThreadKit::Queues::QueueOverflowPolicy::DropOldest;
    auto left = std::make_shared<MFD3D11CameraFrameQueue>(qopt);
    auto right = std::make_shared<MFD3D11CameraFrameQueue>(qopt);

    MFD3D11CameraSyncThreadConfig cfg;
    cfg.maxAdjustedDiff100ns = 50000;
    cfg.candidateCapacity = 4;
    cfg.outputQueueCapacity = 2;
    MFTEST_CHECK(sync.open(left, right, cfg));
    MFTEST_CHECK(sync.isOpened());
    MFTEST_CHECK(sync.outputQueue() != nullptr);
    MFTEST_CHECK_EQ(sync.currentBaselineDiff100ns(), 0ll);

    sync.start();
    MFTEST_CHECK(sync.isRunning());
    sync.stop();
    MFTEST_CHECK(!sync.isRunning());

    auto stats = sync.stats();
    MFTEST_CHECK_EQ(stats.pairsOut, 0ull);

    sync.close();
    MFTEST_CHECK(!sync.isOpened());
})
