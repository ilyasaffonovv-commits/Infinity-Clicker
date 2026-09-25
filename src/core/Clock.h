#pragma once
// High-resolution monotonic clock built directly on QueryPerformanceCounter.
// Every timestamp inside Infinity Clicker is a raw QPC tick count (int64). Conversions
// are done only at the edges (UI / reports), never in the hot scheduling path.
#include <cstdint>

namespace infclick::clk {

int64_t freq();     // QPC ticks per second (constant since boot)
int64_t now();      // current QPC tick

inline double ticksToSec(int64_t t) { return double(t) / double(freq()); }
inline double ticksToMs(int64_t t)  { return double(t) * 1e3 / double(freq()); }
inline double ticksToUs(int64_t t)  { return double(t) * 1e6 / double(freq()); }
inline double ticksToNs(int64_t t)  { return double(t) * 1e9 / double(freq()); }
inline int64_t secToTicks(double s) { return int64_t(s * double(freq()) + 0.5); }
inline int64_t msToTicks(double ms) { return int64_t(ms * double(freq()) / 1e3 + 0.5); }
inline int64_t usToTicks(double us) { return int64_t(us * double(freq()) / 1e6 + 0.5); }

// Converts a QPC tick span into 100-ns units used by SetWaitableTimer.
int64_t ticksTo100ns(int64_t t);

} // namespace infclick::clk
