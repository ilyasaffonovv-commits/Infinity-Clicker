#include "scheduler/PreciseWaiter.h"

#include "core/Clock.h"

#include <immintrin.h>

#include <algorithm>

namespace infclick {

PreciseWaiter::PreciseWaiter()
{
    perUs_ = double(clk::freq()) / 1e6;
    timer_ = CreateWaitableTimerExW(nullptr, nullptr, CREATE_WAITABLE_TIMER_HIGH_RESOLUTION, TIMER_ALL_ACCESS);
    highRes_ = timer_ != nullptr;
    if (!timer_) timer_ = CreateWaitableTimerExW(nullptr, nullptr, 0, TIMER_ALL_ACCESS); // pre-1803 fallback
    // Conservative seed until real samples arrive.
    p50_ = int64_t(250 * perUs_);
    p75_ = int64_t(400 * perUs_);
    p99_ = int64_t(600 * perUs_);
    max_ = int64_t(1000 * perUs_);
}

PreciseWaiter::~PreciseWaiter()
{
    if (timer_) CloseHandle(timer_);
}

bool PreciseWaiter::timerSleep(int64_t ticks, HANDLE interruptEvent)
{
    LARGE_INTEGER due;
    int64_t hns = clk::ticksTo100ns(ticks);
    due.QuadPart = -(hns > 0 ? hns : 1); // negative = relative
    // TolerableDelay = 0: we explicitly opt out of timer coalescing.
    if (!SetWaitableTimerEx(timer_, &due, 0, nullptr, nullptr, nullptr, 0)) return true;
    HANDLE hs[2] = {timer_, interruptEvent};
    DWORD r = WaitForMultipleObjects(interruptEvent ? 2 : 1, hs, FALSE, INFINITE);
    if (r == WAIT_OBJECT_0 + 1) {
        CancelWaitableTimer(timer_);
        return false;
    }
    return true;
}

void PreciseWaiter::recordOversleep(int64_t ticks)
{
    samples_[head_] = ticks;
    head_ = (head_ + 1) % kSamples;
    if (count_ < kSamples) ++count_;
    if (++sinceModel_ >= 32 || count_ < 32) recomputeModel();
}

void PreciseWaiter::recomputeModel()
{
    sinceModel_ = 0;
    if (count_ < 8) return;
    int64_t tmp[kSamples];
    std::copy(samples_, samples_ + count_, tmp);
    std::sort(tmp, tmp + count_);
    auto at = [&](double q) { return std::max<int64_t>(0, tmp[std::min(count_ - 1, int(q * (count_ - 1) + 0.5))]); };
    p50_ = at(0.50);
    p75_ = at(0.75);
    p99_ = at(0.99);
    max_ = std::max<int64_t>(0, tmp[count_ - 1]);
}

int64_t PreciseWaiter::marginTicks(Precision p) const
{
    if (fixedMarginTicks_ >= 0) return p == Precision::Eco ? 0 : fixedMarginTicks_;
    switch (p) {
    case Precision::Eco: return 0;
    case Precision::Standard: {
        // Enough early wake-up to cover ~99% of timer oversleep, but never spin
        // more than 10% of the period (CPU budget ~0.1 core). Rates up to ~100/s
        // are therefore exact; above that precision degrades gracefully.
        int64_t want = std::clamp<int64_t>(p99_ + int64_t(100 * perUs_), int64_t(100 * perUs_), int64_t(2500 * perUs_));
        if (periodHint_ > 0) want = std::min<int64_t>(want, periodHint_ / 10);
        return want;
    }
    case Precision::Ultra:
        // No CPU budget: cover the whole observed oversleep distribution.
        return std::clamp<int64_t>(std::max(p99_ + int64_t(200 * perUs_), max_), int64_t(300 * perUs_),
                                   int64_t(3000 * perUs_));
    }
    return 0;
}

double PreciseWaiter::marginUs(Precision p) const { return double(marginTicks(p)) / perUs_; }
double PreciseWaiter::oversleepP50Us() const { return double(p50_) / perUs_; }
double PreciseWaiter::oversleepP99Us() const { return double(p99_) / perUs_; }
double PreciseWaiter::oversleepMaxUs() const { return double(max_) / perUs_; }

WaitResult PreciseWaiter::waitUntil(int64_t deadline, Precision p, HANDLE interruptEvent,
                                    const std::atomic<bool>* interruptFlag, int64_t maxSleepChunkTicks)
{
    const int64_t minSleep = int64_t((p == Precision::Eco ? 30.0 : 120.0) * perUs_);
    for (;;) {
        const int64_t now = clk::now();
        const int64_t rem = deadline - now;
        if (rem <= 0) return WaitResult::Reached;
        if (interruptFlag && interruptFlag->load(std::memory_order_relaxed)) return WaitResult::Interrupted;

        int64_t sleepFor = rem - marginTicks(p);
        if (sleepFor >= minSleep) {
            if (maxSleepChunkTicks > 0 && sleepFor > maxSleepChunkTicks) sleepFor = maxSleepChunkTicks;
            const bool ok = timerSleep(sleepFor, interruptEvent);
            const int64_t woke = clk::now();
            sleepTicks += woke - now;
            ++timerWaits;
            if (!ok) return WaitResult::Interrupted;
            recordOversleep(woke - (now + sleepFor));
            continue;
        }

        // Final approach: busy-wait on QPC. _mm_pause keeps the core's sibling
        // hyper-thread fed and reduces power while spinning.
        int64_t t = now;
        while (t < deadline) {
            if (interruptFlag && interruptFlag->load(std::memory_order_relaxed)) {
                spinTicks += t - now;
                return WaitResult::Interrupted;
            }
            _mm_pause();
            _mm_pause();
            t = clk::now();
        }
        spinTicks += t - now;
        return WaitResult::Reached;
    }
}

void PreciseWaiter::calibrate()
{
    for (int i = 0; i < 32; ++i) {
        const int64_t req = int64_t((i & 1 ? 300.0 : 1000.0) * perUs_);
        const int64_t t0 = clk::now();
        timerSleep(req, nullptr);
        recordOversleep(clk::now() - (t0 + req));
    }
    recomputeModel();
}

} // namespace infclick
