#pragma once
// Process / thread CPU usage and resource counters (memory, handles, threads).
#include <windows.h>

#include <cstdint>

namespace infclick {

struct ProcessResources {
    uint64_t privateBytes = 0;
    uint64_t workingSet = 0;
    uint32_t handles = 0;
    uint32_t threads = 0;
    uint32_t gdiObjects = 0;
    uint32_t userObjects = 0;
};

ProcessResources queryProcessResources();
uint64_t processCpuTime100ns();
uint64_t threadCpuTime100ns(HANDLE thread);
int logicalCpuCount();

// Cycle-accurate CPU accounting (QueryThreadCycleTime / QueryProcessCycleTime).
// GetThreadTimes only advances on clock-tick boundaries (15.6 ms), which makes it
// useless for short measurements; cycle counters are exact.
double cyclesPerSecond(); // calibrated once (reference TSC rate)
uint64_t threadCycles(HANDLE thread);
uint64_t processCycles();
inline double cyclesToSec(uint64_t c) { return double(c) / cyclesPerSecond(); }

class CpuMeter {
public:
    CpuMeter();
    // Call periodically (e.g. every 500 ms). Returns true when a new value is ready.
    bool sample(HANDLE schedulerThread = nullptr);
    double processPercentOfMachine() const { return procPct_; } // 0..100 of all logical CPUs
    double processCores() const { return procCores_; }          // 1.0 = one core fully busy
    double schedulerCores() const { return schedCores_; }

private:
    int64_t lastQpc_ = 0;
    uint64_t lastProc_ = 0;
    uint64_t lastSched_ = 0;
    HANDLE lastThread_ = nullptr;
    double procPct_ = 0, procCores_ = 0, schedCores_ = 0;
};

} // namespace infclick
