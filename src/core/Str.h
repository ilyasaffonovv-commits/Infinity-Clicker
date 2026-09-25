#pragma once
#include <cstdarg>
#include <string>
#include <string_view>

namespace infclick {

std::wstring toWide(std::string_view utf8);
std::string toUtf8(std::wstring_view wide);
std::string strFormat(const char* fmt, ...);
std::string strFormatV(const char* fmt, va_list ap);
std::string toLowerAscii(std::string s);
std::wstring toLowerW(std::wstring s);
std::string trim(std::string_view s);

} // namespace infclick
