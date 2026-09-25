#include "platform/win/Autostart.h"

#include "core/Paths.h"

#include <windows.h>

#include <string>

namespace infclick::autostart {

namespace {
constexpr const wchar_t* kRunKey = L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";
}

bool isEnabled(const wchar_t* valueName)
{
    HKEY k = nullptr;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, kRunKey, 0, KEY_QUERY_VALUE, &k) != ERROR_SUCCESS) return false;
    const LONG r = RegQueryValueExW(k, valueName, nullptr, nullptr, nullptr, nullptr);
    RegCloseKey(k);
    return r == ERROR_SUCCESS;
}

bool set(bool enable, const wchar_t* valueName)
{
    HKEY k = nullptr;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, kRunKey, 0, nullptr, 0, KEY_SET_VALUE, nullptr, &k, nullptr) != ERROR_SUCCESS) return false;
    LONG r;
    if (enable) {
        const std::wstring cmd = L"\"" + paths::exePath() + L"\" --minimized";
        r = RegSetValueExW(k, valueName, 0, REG_SZ, reinterpret_cast<const BYTE*>(cmd.c_str()), DWORD((cmd.size() + 1) * sizeof(wchar_t)));
    } else {
        r = RegDeleteValueW(k, valueName);
        if (r == ERROR_FILE_NOT_FOUND) r = ERROR_SUCCESS;
    }
    RegCloseKey(k);
    return r == ERROR_SUCCESS;
}

} // namespace infclick::autostart
