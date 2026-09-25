#include "platform/win/ProcessUtil.h"

#include "core/Paths.h"

#include <shellapi.h>

#include <algorithm>
#include <iterator>

namespace infclick::procutil {

std::wstring processNameFromPid(DWORD pid)
{
    HANDLE h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!h) return {};
    wchar_t buf[MAX_PATH * 2];
    DWORD n = DWORD(std::size(buf));
    std::wstring name;
    if (QueryFullProcessImageNameW(h, 0, buf, &n)) {
        std::wstring full(buf, n);
        size_t pos = full.find_last_of(L"\\/");
        name = pos == std::wstring::npos ? full : full.substr(pos + 1);
    }
    CloseHandle(h);
    return name;
}

bool processElevated(DWORD pid, bool* known)
{
    if (known) *known = false;
    HANDLE p = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!p) return false;
    HANDLE tok = nullptr;
    bool elevated = false;
    if (OpenProcessToken(p, TOKEN_QUERY, &tok)) {
        TOKEN_ELEVATION e{};
        DWORD sz = 0;
        if (GetTokenInformation(tok, TokenElevation, &e, sizeof e, &sz)) {
            elevated = e.TokenIsElevated != 0;
            if (known) *known = true;
        }
        CloseHandle(tok);
    }
    CloseHandle(p);
    return elevated;
}

bool selfElevated() { return processElevated(GetCurrentProcessId()); }

std::vector<WindowProcess> listWindowProcesses()
{
    std::vector<WindowProcess> out;
    EnumWindows(
        [](HWND h, LPARAM lp) -> BOOL {
            auto& v = *reinterpret_cast<std::vector<WindowProcess>*>(lp);
            if (!IsWindowVisible(h) || GetWindow(h, GW_OWNER)) return TRUE;
            wchar_t title[256];
            int len = GetWindowTextW(h, title, int(std::size(title)));
            if (len <= 0) return TRUE;
            DWORD pid = 0;
            GetWindowThreadProcessId(h, &pid);
            if (!pid || pid == GetCurrentProcessId()) return TRUE;
            for (auto& e : v)
                if (e.pid == pid) return TRUE;
            WindowProcess wp;
            wp.pid = pid;
            wp.title.assign(title, size_t(len));
            wp.process = processNameFromPid(pid);
            if (wp.process.empty()) wp.process = L"(access denied)";
            wp.elevated = processElevated(pid);
            v.push_back(std::move(wp));
            return TRUE;
        },
        reinterpret_cast<LPARAM>(&out));
    std::sort(out.begin(), out.end(), [](const WindowProcess& a, const WindowProcess& b) {
        return _wcsicmp(a.process.c_str(), b.process.c_str()) < 0;
    });
    return out;
}

bool relaunchElevated(const std::wstring& args)
{
    std::wstring exe = paths::exePath();
    SHELLEXECUTEINFOW sei{sizeof sei};
    sei.lpVerb = L"runas";
    sei.lpFile = exe.c_str();
    sei.lpParameters = args.c_str();
    sei.nShow = SW_SHOWNORMAL;
    return ShellExecuteExW(&sei) != FALSE;
}

} // namespace infclick::procutil
