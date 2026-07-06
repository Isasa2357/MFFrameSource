#include "TestHarness.hpp"

#include <MFFrameSource/MFCommon.hpp>

using namespace MFFrameSource;

MFTEST_MAIN({
    MFFrameRate invalid{};
    MFTEST_CHECK(!invalid.isValid());

    MFFrameRate r60{60, 1};
    MFFrameRate r60000{60000, 1000};
    MFTEST_CHECK(r60.isValid());
    MFTEST_CHECK(!(r60 == r60000));
    MFTEST_CHECK(r60 == MFFrameRate{60, 1});

    MFCameraFormatRequest req;
    MFTEST_CHECK(!req.isComplete());
    req.subtype = MFVideoFormat_NV12;
    req.width = 1920;
    req.height = 1080;
    req.fps = r60;
    MFTEST_CHECK(req.isComplete());

    MFTEST_CHECK_EQ(MfSubtypeToDxgiFormat(MFVideoFormat_NV12), DXGI_FORMAT_NV12);
    MFTEST_CHECK_EQ(MfSubtypeToDxgiFormat(MFVideoFormat_P010), DXGI_FORMAT_P010);
    MFTEST_CHECK_EQ(MfSubtypeToDxgiFormat(MFVideoFormat_RGB32), DXGI_FORMAT_B8G8R8A8_UNORM);
    MFTEST_CHECK_EQ(MfSubtypeToDxgiFormat(MFVideoFormat_ARGB32), DXGI_FORMAT_B8G8R8A8_UNORM);
    MFTEST_CHECK_EQ(MfSubtypeToDxgiFormat(MFVideoFormat_MJPG), DXGI_FORMAT_UNKNOWN);

    MFTEST_CHECK(IsSupportedCpuUploadInputFormat(DXGI_FORMAT_NV12));
    MFTEST_CHECK(IsSupportedCpuUploadInputFormat(DXGI_FORMAT_P010));
    MFTEST_CHECK(IsSupportedCpuUploadInputFormat(DXGI_FORMAT_R8G8B8A8_UNORM));
    MFTEST_CHECK(IsSupportedCpuUploadInputFormat(DXGI_FORMAT_B8G8R8A8_UNORM));
    MFTEST_CHECK(!IsSupportedCpuUploadInputFormat(DXGI_FORMAT_UNKNOWN));
    MFTEST_CHECK(!IsSupportedCpuUploadInputFormat(DXGI_FORMAT_R8_UNORM));

    MFTEST_CHECK_EQ(DxgiPlaneCount(DXGI_FORMAT_NV12), 2u);
    MFTEST_CHECK_EQ(DxgiPlaneCount(DXGI_FORMAT_P010), 2u);
    MFTEST_CHECK_EQ(DxgiPlaneCount(DXGI_FORMAT_R8G8B8A8_UNORM), 1u);
    MFTEST_CHECK_EQ(DxgiPlaneCount(DXGI_FORMAT_B8G8R8A8_UNORM), 1u);
    MFTEST_CHECK_EQ(DxgiPlaneCount(DXGI_FORMAT_UNKNOWN), 0u);

    MFTEST_CHECK_EQ(ExpectedTightImageBytes(DXGI_FORMAT_NV12, 1920, 1080), 1920ull * 1080ull + 1920ull * 540ull);
    MFTEST_CHECK_EQ(ExpectedTightImageBytes(DXGI_FORMAT_P010, 1920, 1080), 1920ull * 1080ull * 2ull + 1920ull * 540ull * 2ull);
    MFTEST_CHECK_EQ(ExpectedTightImageBytes(DXGI_FORMAT_R8G8B8A8_UNORM, 1920, 1080), 1920ull * 1080ull * 4ull);
    MFTEST_CHECK_EQ(ExpectedTightImageBytes(DXGI_FORMAT_B8G8R8A8_UNORM, 1920, 1080), 1920ull * 1080ull * 4ull);
    MFTEST_CHECK_EQ(ExpectedTightImageBytes(DXGI_FORMAT_UNKNOWN, 1920, 1080), 0ull);

    MFTEST_CHECK(std::wstring(DxgiFormatName(DXGI_FORMAT_NV12)) == L"NV12");
    MFTEST_CHECK(std::wstring(DxgiFormatName(DXGI_FORMAT_UNKNOWN)) == L"UNKNOWN");

    MFErrorInfo err;
    MFTEST_CHECK(!err);
    err.hr = E_FAIL;
    err.where = L"where";
    err.message = L"message";
    MFTEST_CHECK(static_cast<bool>(err));
    err.clear();
    MFTEST_CHECK(!err);
})
