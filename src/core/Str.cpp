#include "core/Str.h"

#include <windows.h>

#include <cstdio>
#include <cwctype>
#include <vector>

namespace infclick {

std::wstring toWide(std::string_view s)
{
    if (s.empty()) return {};
    int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), int(s.size()), nullptr, 0);
    std::wstring w(size_t(n), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), int(s.size()), w.data(), n);
    return w;
}

std::string toUtf8(std::wstring_view w)
{
    if (w.empty()) return {};
    int n = WideCharToMultiByte(CP_UTF8, 0, w.data(), int(w.size()), nullptr, 0, nullptr, nullptr);
    std::string s(size_t(n), '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.data(), int(w.size()), s.data(), n, nullptr, nullptr);
    return s;
}

std::string strFormatV(const char* fmt, va_list ap)
{
    char buf[512];
    va_list ap2;
    va_copy(ap2, ap);
    int n = std::vsnprintf(buf, sizeof buf, fmt, ap);
    if (n < 0) {
        va_end(ap2);
        return {};
    }
    if (size_t(n) < sizeof buf) {
        va_end(ap2);
        return std::string(buf, size_t(n));
    }
    std::vector<char> big(size_t(n) + 1);
    std::vsnprintf(big.data(), big.size(), fmt, ap2);
    va_end(ap2);
    return std::string(big.data(), size_t(n));
}

std::string strFormat(const char* fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    std::string s = strFormatV(fmt, ap);
    va_end(ap);
    return s;
}

std::string toLowerAscii(std::string s)
{
    for (char& c : s)
        if (c >= 'A' && c <= 'Z') c = char(c - 'A' + 'a');
    return s;
}

std::wstring toLowerW(std::wstring s)
{
    for (wchar_t& c : s) c = wchar_t(std::towlower(c));
    return s;
}

std::string trim(std::string_view s)
{
    size_t b = 0, e = s.size();
    while (b < e && (s[b] == ' ' || s[b] == '\t' || s[b] == '\r' || s[b] == '\n')) ++b;
    while (e > b && (s[e - 1] == ' ' || s[e - 1] == '\t' || s[e - 1] == '\r' || s[e - 1] == '\n')) --e;
    return std::string(s.substr(b, e - b));
}

} // namespace infclick
