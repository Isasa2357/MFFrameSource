#pragma once

#include <MFFrameSource/MFCommon.hpp>

#include <comdef.h>
#include <sstream>
#include <stdexcept>

namespace MFFrameSource::internal {

class HResultException : public std::runtime_error {
public:
    HResultException(HRESULT hr, std::wstring where)
        : std::runtime_error("HRESULT failure"), hr_(hr), where_(std::move(where)) {}

    HRESULT hr() const noexcept { return hr_; }
    const std::wstring& where() const noexcept { return where_; }

private:
    HRESULT hr_;
    std::wstring where_;
};

inline void ThrowIfFailed(HRESULT hr, const wchar_t* where) {
    if (FAILED(hr)) {
        throw HResultException(hr, where ? where : L"");
    }
}

inline std::wstring HrToString(HRESULT hr) {
    _com_error err(hr);
    std::wstringstream ss;
    ss << L"HRESULT 0x" << std::hex << static_cast<unsigned long>(hr);
    if (err.ErrorMessage()) {
        ss << L" (" << err.ErrorMessage() << L")";
    }
    return ss.str();
}

inline MFErrorInfo MakeError(HRESULT hr, std::wstring where, std::wstring message = {}) {
    MFErrorInfo e;
    e.hr = hr;
    e.where = std::move(where);
    e.message = message.empty() ? HrToString(hr) : std::move(message);
    return e;
}

inline MFErrorInfo MakeError(std::wstring where, std::wstring message) {
    MFErrorInfo e;
    e.hr = E_FAIL;
    e.where = std::move(where);
    e.message = std::move(message);
    return e;
}

inline std::wstring Utf8ToWide(const std::string& s) {
    if (s.empty()) return {};
    const int len = MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), nullptr, 0);
    if (len <= 0) return L"<utf8 conversion failed>";
    std::wstring w(static_cast<size_t>(len), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), w.data(), len);
    return w;
}

inline bool GuidEquals(const GUID& a, const GUID& b) noexcept {
    return IsEqualGUID(a, b) != FALSE;
}

inline std::wstring ReadAllocatedString(IMFAttributes* attrs, REFGUID key) {
    if (!attrs) return {};
    wchar_t* raw = nullptr;
    UINT32 len = 0;
    if (FAILED(attrs->GetAllocatedString(key, &raw, &len)) || !raw) return {};
    std::wstring s(raw, raw + len);
    CoTaskMemFree(raw);
    return s;
}

inline MFFrameRate ReadFrameRate(IMFMediaType* type) {
    MFFrameRate rate;
    UINT32 num = 0, den = 1;
    if (SUCCEEDED(MFGetAttributeRatio(type, MF_MT_FRAME_RATE, &num, &den))) {
        rate.numerator = num;
        rate.denominator = den == 0 ? 1 : den;
    }
    return rate;
}

inline void ReadFrameSize(IMFMediaType* type, UINT& width, UINT& height) {
    UINT32 w = 0, h = 0;
    if (SUCCEEDED(MFGetAttributeSize(type, MF_MT_FRAME_SIZE, &w, &h))) {
        width = w;
        height = h;
    }
}

} // namespace MFFrameSource::internal
