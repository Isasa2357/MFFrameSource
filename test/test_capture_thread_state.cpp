#include "TestHarness.hpp"

#include <MFFrameSource/MFD3D12CameraCaptureThread.hpp>

#include <D3D12Helper/D3D12Core/D3D12Core.hpp>

#include <stdexcept>

using namespace MFFrameSource;

MFTEST_MAIN({
    MFD3D12CameraCaptureThread thread;
    MFTEST_CHECK(!thread.isOpened());
    MFTEST_CHECK(!thread.isRunning());

    bool threw = false;
    try { thread.start(); } catch (const std::logic_error&) { threw = true; }
    MFTEST_CHECK(threw);

    auto q = thread.createQueue(2);
    MFTEST_CHECK(q != nullptr);
    MFTEST_CHECK(q->empty());
    q->close();
    thread.unregisterExpiredAndClosedQueues();
    auto sClosed = thread.stats();
    MFTEST_CHECK(sClosed.closedQueuesRemoved >= 1ull);

    MFD3D12CameraCaptureThreadConfig cfg;
    cfg.selector.deviceIndex = 0;
    cfg.capture.input.subtype = MFVideoFormat_NV12;
    cfg.capture.input.width = 640;
    cfg.capture.input.height = 480;
    cfg.capture.input.fps = MFFrameRate{30, 1};

    MFTEST_CHECK(!thread.open(cfg, nullptr));
    MFTEST_CHECK(!thread.isOpened());
    MFTEST_CHECK(thread.lastError());

    auto s = thread.stats();
    MFTEST_CHECK_EQ(s.framesRead, 0ull);
    MFTEST_CHECK_EQ(s.framesDelivered, 0ull);

    thread.close();
    MFTEST_CHECK(!thread.isOpened());
})
