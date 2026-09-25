#pragma once
// Pure (OS-independent, unit-testable) pieces of the scheduler:
//  * trigger-mode state machine (Hold / Toggle / Burst / Count / Duration)
//  * zero-drift absolute timeline math
#include <cmath>
#include <cstdint>

namespace infclick {

enum class TriggerMode : uint8_t { Hold = 0, Toggle, Burst, Count, Duration };

inline const char* triggerModeName(TriggerMode m)
{
    switch (m) {
    case TriggerMode::Hold: return "Hold";
    case TriggerMode::Toggle: return "Toggle";
    case TriggerMode::Burst: return "Burst";
    case TriggerMode::Count: return "Count";
    case TriggerMode::Duration: return "Duration";
    }
    return "?";
}

struct ModeParams {
    TriggerMode mode = TriggerMode::Toggle;
    uint64_t burstCount = 10;   // Burst: actions per press
    uint64_t fixedCount = 100;  // Count: actions per run
    uint32_t durationMs = 1000; // Duration: run length
};

struct RunLimits {
    uint64_t maxActions = 0;  // 0 = unlimited
    double durationMs = 0;    // 0 = unlimited
};

struct ModeDecision {
    enum class Kind : uint8_t { None, Start, Stop, Extend } kind = Kind::None;
    RunLimits limits{};
    uint64_t extendBy = 0;
};

// What a trigger edge means in each mode.
//   Hold     : down -> start (unlimited), up -> stop
//   Toggle   : down -> start/stop
//   Burst    : down -> start N actions; pressing again while running queues N more
//   Count    : down -> start N actions; pressing again while running stops
//   Duration : down -> run for T ms;    pressing again while running stops
inline ModeDecision decideOnTriggerDown(const ModeParams& p, bool running)
{
    ModeDecision d;
    using K = ModeDecision::Kind;
    switch (p.mode) {
    case TriggerMode::Hold:
        d.kind = running ? K::None : K::Start;
        break;
    case TriggerMode::Toggle:
        d.kind = running ? K::Stop : K::Start;
        break;
    case TriggerMode::Burst:
        if (running) {
            d.kind = K::Extend;
            d.extendBy = p.burstCount;
        } else {
            d.kind = K::Start;
            d.limits.maxActions = p.burstCount ? p.burstCount : 1;
        }
        break;
    case TriggerMode::Count:
        d.kind = running ? K::Stop : K::Start;
        d.limits.maxActions = p.fixedCount ? p.fixedCount : 1;
        break;
    case TriggerMode::Duration:
        d.kind = running ? K::Stop : K::Start;
        d.limits.durationMs = p.durationMs ? p.durationMs : 1;
        break;
    }
    return d;
}

inline ModeDecision decideOnTriggerUp(const ModeParams& p, bool running)
{
    ModeDecision d;
    if (p.mode == TriggerMode::Hold && running) d.kind = ModeDecision::Kind::Stop;
    return d;
}

// Absolute timeline: slot(k) = t0 + round(k * period). Deadlines are always
// computed from the anchor, never from "now", so execution time of an action
// or a late wake-up never accumulates into drift.
struct Timeline {
    int64_t t0 = 0;
    double period = 0; // QPC ticks per action (fractional allowed)
    uint64_t k = 0;    // index of the next action

    void anchor(int64_t start, double periodTicks)
    {
        t0 = start;
        period = periodTicks;
        k = 0;
    }
    int64_t slot(uint64_t idx) const { return t0 + int64_t(std::floor(double(idx) * period + 0.5)); }
    int64_t next() const { return slot(k); }

    // If `now` is at least one full period past slot(k), jump to the latest slot
    // that is <= now (fire it immediately) and report how many were skipped.
    // Grid phase is preserved - we never restart the interval from "now".
    uint64_t skipLate(int64_t now)
    {
        if (period <= 0 || now < slot(k + 1)) return 0;
        uint64_t idx = uint64_t(std::floor(double(now - t0) / period));
        while (idx > k && slot(idx) > now) --idx; // guard rounding
        uint64_t skipped = idx > k ? idx - k : 0;
        k = idx;
        return skipped;
    }

    // Change the rate mid-run without a phase jump: re-anchor at the next slot.
    void changePeriod(double periodTicks)
    {
        int64_t nextSlot = slot(k);
        t0 = nextSlot;
        k = 0;
        period = periodTicks;
    }
};

} // namespace infclick
