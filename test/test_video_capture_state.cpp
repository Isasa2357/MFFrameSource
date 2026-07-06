#include "TestHarness.hpp"

#include <MFFrameSource/MFD3D12VideoCapture.hpp>

#include <D3D12Helper/D3D12Core/D3D12Core.hpp>

#include <memory>

using namespace MFFrameSource;

MFTEST_MAIN({
    MFD3D12VideoCapture capture;
    MFTEST_CHECK(!capture.isOpened());
    MFTEST_CHECK_EQ(capture.duration100ns(), -1ll);

    auto notOpened = capture.read();
    MFTEST_CHECK_EQ(notOpened.status, MFFrameStatus::NotOpened);
    MFTEST_CHECK(!notOpened.ok());
    MFTEST_CHECK(static_cast<bool>(notOpened.error));

    MFTEST_CHECK(!capture.seek(0));
    MFTEST_CHECK(static_cast<bool>(capture.lastError()));

    MFVideoCaptureConfig config;
    MFTEST_CHECK(!capture.open(L"dummy.mp4", config, nullptr));
    MFTEST_CHECK(!capture.isOpened());
    MFTEST_CHECK(static_cast<bool>(capture.lastError()));
    MFTEST_CHECK(MFFrameSourceTest::Contains(capture.lastError().message, L"null D3D12Core"));

    auto dummyCore = std::make_shared<D3D12CoreLib::D3D12Core>();
    MFTEST_CHECK(!capture.open(L"dummy.mp4", config, dummyCore));
    MFTEST_CHECK(!capture.isOpened());
    MFTEST_CHECK(static_cast<bool>(capture.lastError()));
    MFTEST_CHECK(MFFrameSourceTest::Contains(capture.lastError().message, L"exact decoded format request"));

    config.input.subtype = MFVideoFormat_MJPG;
    config.input.width = 640;
    config.input.height = 480;
    config.input.fps = MFFrameRate{30, 1};
    MFTEST_CHECK(config.input.isComplete());
    MFTEST_CHECK(!capture.open(L"dummy.mp4", config, dummyCore));
    MFTEST_CHECK(!capture.isOpened());
    MFTEST_CHECK(static_cast<bool>(capture.lastError()));
    MFTEST_CHECK(MFFrameSourceTest::Contains(capture.lastError().message, L"not supported"));

    capture.close();
    capture.close();
    MFTEST_CHECK(!capture.isOpened());
})
