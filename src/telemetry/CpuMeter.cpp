#include "telemetry/CpuMeter.h"

#include "core/Clock.h"

#include <psapi.h>
#include <tlhelp32.h>

namespace infclick {

namespace {
uint64_t ft(const FILETIME& f) { return (uint64_t(f.dwHighDateTime) << 32) | f.dwLowDateTime; }
} // namespace

int logicalCpuCount()
{
    static const int n = [] {
        DWORD c = GetActiveProcessorCount(ALL_PROCESSOR_GROUPS);
        return c ? int(c) : 1;
    }();
    return n;
}

double cyclesPerSecond()
{
    static const double cps = [] {
        // Spin ~30 ms on this thread and relate its cycle counter to QPC.
        ULONG64 c0 = 0, c1 = 0;
        QueryThreadCycleTime(GetCurrentThread(), &c0);
        const int64_t t0 = clk::now();
        while (clk::now() - t0 < clk::msToTicks(30)) {
        }
        QueryThreadCycleTime(GetCurrentThread(), &c1);
        const double sec = clk::ticksToSec(clk::now() - t0);
        return sec > 0 ? double(c1 - c0) / sec : 1e9;
    }();
    return cps;
}

uint64_t threadCycles(HANDLE thread)
{
    ULONG64 c = 0;
    if (thread) QueryThreadCycleTime(thread, &c);
    return c;
}

uint64_t processCycles()
{
    ULONG64 c = 0;
    QueryProcessCycleTime(GetCurrentProcess(), &c);
    return c;
}

uint64_t processCpuTime100ns()
{
    FILETIME c, e, k, u;
    if (!GetProcessTimes(GetCurrentProcess(), &c, &e, &k, &u)) return 0;
    return ft(k) + ft(u);
}

uint64_t threadCpuTime100ns(HANDLE thread)
{
    if (!thread) return 0;
    FILETIME c, e, k, u;
    if (!GetThreadTimes(thread, &c, &e, &k, &u)) return 0;
    return ft(k) + ft(u);
}

ProcessResources queryProcessResources()
{
    ProcessResources r;
    PROCESS_MEMORY_COUNTERS_EX pmc{};
    pmc.cb = sizeof pmc;
    if (GetProcessMemoryInfo(GetCurrentProcess(), reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&pmc), sizeof pmc)) {
        r.privateBytes = pmc.PrivateUsage;
        r.workingSet = pmc.WorkingSetSize;
    }
    DWORD h = 0;
    if (GetProcessHandleCount(GetCurrentProcess(), &h)) r.handles = h;
    r.gdiObjects = GetGuiResources(GetCurrentProcess(), GR_GDIOBJECTS);
    r.userObjects = GetGuiResources(GetCurrentProcess(), GR_USEROBJECTS);
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
    if (snap != INVALID_HANDLE_VALUE) {
        THREADENTRY32 te{sizeof te};
        const DWORD pid = GetCurrentProcessId();
        if (Thread32First(snap, &te)) {
            do {
                if (te.th32OwnerProcessID == pid) ++r.threads;
            } while (Thread32Next(snap, &te));
        }
        CloseHandle(snap);
    }
    return r;
}

CpuMeter::CpuMeter()
{
    lastQpc_ = clk::now();
    lastProc_ = processCycles();
}

bool CpuMeter::sample(HANDLE schedulerThread)
{
    const int64_t now = clk::now();
    const double wall = clk::ticksToSec(now - lastQpc_);
    if (wall < 0.2) return false;
    const uint64_t proc = processCycles();
    const uint64_t sched = threadCycles(schedulerThread);
    procCores_ = cyclesToSec(proc - lastProc_) / wall;
    procPct_ = procCores_ / double(logicalCpuCount()) * 100.0;
    if (schedulerThread && schedulerThread == lastThread_) schedCores_ = cyclesToSec(sched - lastSched_) / wall;
    else schedCores_ = 0;
    lastThread_ = schedulerThread;
    lastSched_ = sched;
    lastProc_ = proc;
    lastQpc_ = now;
    return true;
}

} // namespace infclick
