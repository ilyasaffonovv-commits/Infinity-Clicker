#pragma once
#include <windows.h>

#include <string>
#include <vector>

namespace infclick::procutil {

std::wstring processNameFromPid(DWORD pid); // "javaw.exe" (empty if inaccessible)

struct WindowProcess {
    std::wstring process; // image name
    std::wstring title;   // a representative top-level window title
    DWORD pid = 0;
    bool elevated = false;
};
// Visible top-level windows grouped by process (for the target-app picker).
std::vector<WindowProcess> listWindowProcesses();

bool selfElevated();
bool processElevated(DWORD pid, bool* known = nullptr);
bool relaunchElevated(const std::wstring& args);

} // namespace infclick::procutil
