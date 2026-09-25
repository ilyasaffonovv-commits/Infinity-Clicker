#pragma once
// UI localization (English / Russian).
// Source strings are English and double as lookup keys: tr("STOP") -> "СТОП".
// Missing translations fall back to English, so nothing ever shows up empty.
// Logs, benchmark reports and profile JSON stay English (technical artifacts).
#include <cstdint>
#include <string>

namespace infclick {

enum class Lang : uint8_t { En = 0, Ru = 1 };

namespace i18n {
void setLang(Lang l);
Lang lang();
Lang systemLang();                        // from the Windows UI language
Lang resolve(const std::string& setting); // "auto" | "en" | "ru"
} // namespace i18n

const char* tr(const char* en);

} // namespace infclick
