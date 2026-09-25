#pragma once
// Scheduling-quality knobs that are safe for a desktop app:
//  * thread priority inside the NORMAL priority class (never REALTIME class)
//  * optional MMCSS registration ("Games" task) - the documented way for
//    latency-sensitive user threads to get boosted scheduling
//  * Windows 11 power throttling (EcoQoS) opt-out for the scheduler while it
//    is running, so it is not parked on efficiency cores / low clocks and its
//    timer requests are honoured while the window is minimized.
#include <windows.h>

#include <cstdint>

namespace infclick::power {

enum class ThreadPrio : uint8_t { Normal = 0, AboveNormal, Highest, TimeCritical, Mmcss };

const char* prioName(ThreadPrio p);

class ThreadPriorityScope {
public:
    ThreadPriorityScope() = default;
    ~ThreadPriorityScope() { reset(); }
    // Applies to the calling thread.
    bool apply(ThreadPrio p);
    void reset();
    ThreadPrio current() const { return cur_; }

private:
    ThreadPrio cur_ = ThreadPrio::Normal;
    HANDLE mmcss_ = nullptr;
};

// HighQoS for the calling thread (disable EXECUTION_SPEED throttling) or back to default.
void setThreadHighQos(bool on);
// Process-wide: always honour timer resolution requests / disable EcoQoS.
void setProcessHighQos(bool on);

// Undocumented but read-only: NtQueryTimerResolution (100 ns units). Returns false if unavailable.
bool queryTimerResolution(uint32_t& minRes, uint32_t& maxRes, uint32_t& curRes);
// Undocumented NtSetTimerResolution - used ONLY by the benchmark to study its effect.
bool setTimerResolution(uint32_t desired100ns, bool set, uint32_t* actual);

} // namespace infclick::power
