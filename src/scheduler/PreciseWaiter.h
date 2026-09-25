#pragma once
// Hybrid "sleep then spin" wait primitive.
//
//   |<------------ remaining until deadline ------------>|
//   |<---- high-resolution waitable timer ---->|<-spin->|
//                                    wake-up margin ^
//
// * The sleep part uses CreateWaitableTimerExW(CREATE_WAITABLE_TIMER_HIGH_RESOLUTION)
//   (Win10 1803+): sub-millisecond expiry without touching the global timer
//   resolution. Wait is interruptible by the engine's command event.
// * The spin part polls QueryPerformanceCounter with _mm_pause until the deadline.
// * The margin is ADAPTIVE: we continuously measure how late the timer wakes us
//   (oversleep) and size the margin from that distribution:
//      ECO      margin = 0             -> no spin at all, lowest CPU
//      STANDARD margin ~ p75 oversleep -> short spins, most events exact
//      ULTRA    margin ~ max oversleep -> practically every event exact
#include "scheduler/Precision.h"

#include <windows.h>

#include <atomic>
#include <cstdint>

namespace infclick {

enum class WaitResult : uint8_t { Reached, Interrupted };

class PreciseWaiter {
public:
    PreciseWaiter();
    ~PreciseWaiter();
    PreciseWaiter(const PreciseWaiter&) = delete;
    PreciseWaiter& operator=(const PreciseWaiter&) = delete;

    bool highResolution() const { return highRes_; }

    // Blocks until QPC >= deadline. `interrupt` (optional) aborts the wait
    // (checked in both phases). maxSleepChunk bounds each kernel wait so that
    // callers can do periodic housekeeping.
    WaitResult waitUntil(int64_t deadline, Precision p, HANDLE interruptEvent, const std::atomic<bool>* interruptFlag,
                         int64_t maxSleepChunkTicks = 0);

    // Kernel sleep for a relative duration (no spin). Used by calibration/bench.
    bool timerSleep(int64_t ticks, HANDLE interruptEvent);

    // Tuning / inspection.
    void setFixedMarginUs(double us) { fixedMarginTicks_ = us < 0 ? -1 : int64_t(us * perUs_); } // <0 = adaptive
    // Action period; STANDARD limits its spin to 10% of it (0 = no budget).
    void setPeriodHint(int64_t ticks) { periodHint_ = ticks; }
    double marginUs(Precision p) const;
    double oversleepP50Us() const;
    double oversleepP99Us() const;
    double oversleepMaxUs() const;

    // Accounting since last reset (engine publishes these).
    int64_t spinTicks = 0;
    int64_t sleepTicks = 0;
    uint64_t timerWaits = 0;
    void resetAccounting() { spinTicks = sleepTicks = 0; timerWaits = 0; }

    void calibrate(); // ~30 short timer waits to seed the oversleep model

private:
    void recordOversleep(int64_t ticks);
    int64_t marginTicks(Precision p) const;
    void recomputeModel();

    HANDLE timer_ = nullptr;
    bool highRes_ = false;
    double perUs_ = 10.0;           // QPC ticks per microsecond
    int64_t fixedMarginTicks_ = -1; // >= 0 overrides adaptive margin
    int64_t periodHint_ = 0;

    static constexpr int kSamples = 256;
    int64_t samples_[kSamples]{};
    int count_ = 0;
    int head_ = 0;
    int sinceModel_ = 0;
    int64_t p50_ = 0, p75_ = 0, p99_ = 0, max_ = 0;
};

} // namespace infclick
