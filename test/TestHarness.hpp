#pragma once

#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>

namespace MFFrameSourceTest {

inline void Fail(const char* expr, const char* file, int line, const std::string& message = {}) {
    std::ostringstream os;
    os << file << ":" << line << ": check failed: " << expr;
    if (!message.empty()) {
        os << " -- " << message;
    }
    throw std::runtime_error(os.str());
}

inline std::string WideToUtf8Lossy(const std::wstring& value) {
    std::string out;
    out.reserve(value.size());
    for (wchar_t ch : value) {
        out.push_back((ch >= 0 && ch <= 0x7f) ? static_cast<char>(ch) : '?');
    }
    return out;
}

inline bool Contains(const std::wstring& text, const std::wstring& needle) {
    return text.find(needle) != std::wstring::npos;
}

} // namespace MFFrameSourceTest

// Use variadic macros because many tests contain braced initializers such as
// MFFrameRate{60, 1}.  A one-argument function-like macro treats the comma in
// that braced initializer as an argument separator on MSVC's preprocessor.
#define MFTEST_CHECK(...) \
    do { \
        if (!(__VA_ARGS__)) { \
            ::MFFrameSourceTest::Fail(#__VA_ARGS__, __FILE__, __LINE__); \
        } \
    } while (false)

#define MFTEST_CHECK_MSG(expr, msg) \
    do { \
        if (!(expr)) { \
            ::MFFrameSourceTest::Fail(#expr, __FILE__, __LINE__, (msg)); \
        } \
    } while (false)

#define MFTEST_CHECK_EQ(a, b) \
    do { \
        const auto mf_a_value = (a); \
        const auto mf_b_value = (b); \
        if (!(mf_a_value == mf_b_value)) { \
            std::ostringstream mf_os; \
            mf_os << "left != right"; \
            ::MFFrameSourceTest::Fail(#a " == " #b, __FILE__, __LINE__, mf_os.str()); \
        } \
    } while (false)

// Variadic for the same reason as MFTEST_CHECK: the test body may contain
// commas inside braced initializers.
#define MFTEST_MAIN(...) \
    int main() { \
        try { \
            __VA_ARGS__; \
            std::cout << "PASS\n"; \
            return 0; \
        } catch (const std::exception& e) { \
            std::cerr << "FAIL: " << e.what() << "\n"; \
            return 1; \
        } catch (...) { \
            std::cerr << "FAIL: unknown exception\n"; \
            return 1; \
        } \
    }
