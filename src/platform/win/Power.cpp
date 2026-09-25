#include "platform/win/Power.h"

#include <avrt.h>

namespace infclick::power {

const char* prioName(ThreadPrio p)
{
    switch (p) {
    case ThreadPrio::Normal: return "Normal";
    case ThreadPrio::AboveNormal: return "Above normal";
    case ThreadPrio::Highest: return "Highest";
    case ThreadPrio::TimeCritical: return "Time critical";
    case ThreadPrio::Mmcss: return "MMCSS (Games)";
    }
    return "?";
}

bool ThreadPriorityScope::apply(ThreadPrio p)
{
    reset();
    BOOL ok = TRUE;
    switch (p) {
    case ThreadPrio::Normal: ok = SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_NORMAL); break;
    case ThreadPrio::AboveNormal: ok = SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_ABOVE_NORMAL); break;
    case ThreadPrio::Highest: ok = SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_HIGHEST); break;
    case ThreadPrio::TimeCritical: ok = SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_TIME_CRITICAL); break;
    case ThreadPrio::Mmcss: {
        DWORD idx = 0;
        mmcss_ = AvSetMmThreadCharacteristicsW(L"Games", &idx);
        if (mmcss_) AvSetMmThreadPriority(mmcss_, AVRT_PRIORITY_HIGH);
        ok = mmcss_ != nullptr;
        if (!ok) SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_HIGHEST); // graceful fallback
        break;
    }
    }
    cur_ = p;
    return ok != FALSE;
}

void ThreadPriorityScope::reset()
{
    if (mmcss_) {
        AvRevertMmThreadCharacteristics(mmcss_);
        mmcss_ = nullptr;
    }
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_NORMAL);
    cur_ = ThreadPrio::Normal;
}

void setThreadHighQos(bool on)
{
    THREAD_POWER_THROTTLING_STATE s{};
    s.Version = THREAD_POWER_THROTTLING_CURRENT_VERSION;
    s.ControlMask = on ? THREAD_POWER_THROTTLING_EXECUTION_SPEED : 0;
    s.StateMask = 0; // mechanism off = never throttle
    SetThreadInformation(GetCurrentThread(), ThreadPowerThrottling, &s, sizeof s);
}

void setProcessHighQos(bool on)
{
    PROCESS_POWER_THROTTLING_STATE s{};
    s.Version = PROCESS_POWER_THROTTLING_CURRENT_VERSION;
    s.ControlMask = on ? (PROCESS_POWER_THROTTLING_EXECUTION_SPEED | PROCESS_POWER_THROTTLING_IGNORE_TIMER_RESOLUTION) : 0;
    s.StateMask = 0;
    SetProcessInformation(GetCurrentProcess(), ProcessPowerThrottling, &s, sizeof s);
}

bool queryTimerResolution(uint32_t& minRes, uint32_t& maxRes, uint32_t& curRes)
{
    using Fn = LONG(NTAPI*)(PULONG, PULONG, PULONG);
    static Fn fn = reinterpret_cast<Fn>(GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "NtQueryTimerResolution"));
    if (!fn) return false;
    ULONG a = 0, b = 0, c = 0;
    if (fn(&a, &b, &c) != 0) return false;
    // NT naming is inverted: "Maximum" resolution is the finest (smallest) period.
    minRes = a; // coarsest, e.g. 156250 (15.625 ms)
    maxRes = b; // finest,   e.g. 5000   (0.5 ms)
    curRes = c;
    return true;
}

bool setTimerResolution(uint32_t desired100ns, bool set, uint32_t* actual)
{
    using Fn = LONG(NTAPI*)(ULONG, BOOLEAN, PULONG);
    static Fn fn = reinterpret_cast<Fn>(GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "NtSetTimerResolution"));
    if (!fn) return false;
    ULONG cur = 0;
    const bool ok = fn(desired100ns, set ? TRUE : FALSE, &cur) == 0;
    if (actual) *actual = cur;
    return ok;
}

} // namespace infclick::power
