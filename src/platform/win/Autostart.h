#pragma once
// "Start with Windows": one value under HKCU\Software\Microsoft\Windows\CurrentVersion\Run.
// Opt-in only; unchecking removes the value again. No services, no scheduled tasks.
namespace infclick::autostart {

bool isEnabled(const wchar_t* valueName = L"InfinityClicker");
// Enabling writes:  "<full path to this exe>" --minimized
bool set(bool enable, const wchar_t* valueName = L"InfinityClicker");

} // namespace infclick::autostart
