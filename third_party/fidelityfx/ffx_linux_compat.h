#pragma once
#include <cerrno>
#include <cstddef>
#include <cstdio>
#include <cstdarg>
#include <cmath>
#include <cstring>
#include <cwchar>
#include <locale>
#include <FidelityFX/host/ffx_assert.h>

// The SDK's portable host code also uses the MSVC checked string helpers.
// Keep their bounds checks when building with Linux's 32-bit wchar_t.
template<class T, std::size_t N>
constexpr std::size_t _countof(const T (&)[N]) { return N; }
inline int wcscpy_s(wchar_t* target, std::size_t count, const wchar_t* source) {
    if (!target || !source || !count) return EINVAL;
    const auto length = std::wcslen(source);
    if (length >= count) { target[0]=0; return ERANGE; }
    std::wmemcpy(target, source, length+1);
    return 0;
}
template<std::size_t N>
inline int wcscpy_s(wchar_t (&target)[N], const wchar_t* source) {
    return wcscpy_s(target, N, source);
}
inline int strcpy_s(char* target, std::size_t count, const char* source) {
    if (!target || !source || !count) return EINVAL;
    const auto length = std::strlen(source);
    if (length >= count) { target[0]=0; return ERANGE; }
    std::memcpy(target, source, length+1);
    return 0;
}
inline int sprintf_s(char* target, std::size_t count, const char* format, ...) {
    va_list arguments;
    va_start(arguments, format);
    const int result=std::vsnprintf(target, count, format, arguments);
    va_end(arguments);
    if (result<0 || static_cast<std::size_t>(result)>=count) {
        if (target && count) target[0]=0;
        return -1;
    }
    return result;
}
