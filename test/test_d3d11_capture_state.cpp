#include "TestHarness.hpp"

#include <MFFrameSource/MFD3D11CameraCapture.hpp>

#include <D3D11Helper/D3D11Core/D3D11Core.hpp>

#include <memory>

using namespace MFFrameSource;

MFTEST_MAIN({
    MFD3D11CameraFrame emptyFrame;
    MFTEST_CHECK(!emptyFrame);
    MFTEST_CHECK(!emptyFrame.isReady());

    MFD3D11CameraCapture capture;
    MFTEST_CHECK(!capture.isOpened());

    auto notOpened = capture.read();
    MFTEST_CHECK_EQ(notOpened.status, MFFrameStatus::NotOpened);
    MFTEST_CHECK(!notOpened.ok());
    MFTEST_CHECK(static_cast<bool>(notOpened.error));
    MFTEST_CHECK(MFFrameSourceTest::Contains(capture.lastError().message, L"not opened"));

    MFCameraSelector selector;
    MFCameraCaptureConfig config;

    MFTEST_CHECK(!capture.open(selector, config, nullptr));
    MFTEST_CHECK(!capture.isOpened());
    MFTEST_CHECK(static_cast<bool>(capture.lastError()));
    MFTEST_CHECK(MFFrameSourceTest::Contains(capture.lastError().message, L"null D3D11Core"));

    auto dummyCore = std::make_shared<D3D11CoreLib::D3D11Core>();
    MFTEST_CHECK(!capture.open(selector, config, dummyCore));
    MFTEST_CHECK(!capture.isOpened());
    MFTEST_CHECK(static_cast<bool>(capture.lastError()));
    MFTEST_CHECK(MFFrameSourceTest::Contains(capture.lastError().message, L"exact format request"));

    config.input.subtype = MFVideoFormat_MJPG;
    config.input.width = 640;
    config.input.height = 480;
    config.input.fps = MFFrameRate{30, 1};
    MFTEST_CHECK(config.input.isComplete());
    MFTEST_CHECK(!capture.open(selector, config, dummyCore));
    MFTEST_CHECK(!capture.isOpened());
    MFTEST_CHECK(static_cast<bool>(capture.lastError()));
    MFTEST_CHECK(MFFrameSourceTest::Contains(capture.lastError().message, L"not supported"));

    capture.close();
    capture.close();
    MFTEST_CHECK(!capture.isOpened());
})
