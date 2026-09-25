#include "telemetry/Stats.h"

#include "core/Clock.h"

#include <intrin.h>

#include <algorithm>

namespace infclick {

int LogHistogram::index(uint64_t v)
{
    if (v < uint64_t(kSub)) return int(v);
    unsigned long msb;
    _BitScanReverse64(&msb, v);
    if (int(msb) > kMaxMsb) return kBuckets - 1;
    const int shift = int(msb) - kSubBits;
    const int sub = int((v >> shift) & (kSub - 1));
    return (int(msb) - kSubBits + 1) * kSub + sub;
}

uint64_t LogHistogram::bucketMid(int idx)
{
    if (idx < kSub) return uint64_t(idx);
    const int msb = idx / kSub + kSubBits - 1;
    const int sub = idx % kSub;
    const int shift = msb - kSubBits;
    const uint64_t lower = (uint64_t(kSub + sub)) << shift;
    return lower + ((uint64_t(1) << shift) >> 1);
}

uint64_t LogHistogram::percentile(double q) const
{
    if (total == 0) return 0;
    const uint64_t target = uint64_t(std::ceil(q * double(total)));
    uint64_t acc = 0;
    for (int i = 0; i < kBuckets; ++i) {
        acc += counts[i];
        if (acc >= target && counts[i]) return bucketMid(i);
    }
    return bucketMid(kBuckets - 1);
}

void RunStats::begin(int64_t now, double targetPeriodTicks)
{
    start_ = now;
    targetPeriod_ = targetPeriodTicks;
    actions_ = missed_ = 0;
    lastFire_ = 0;
    firstFire_ = 0;
    intervalRun_.reset();
    intervalSec_.reset();
    intervalLastSec_.reset();
    lateRun_.reset();
    lateSec_.reset();
    lateLastSec_.reset();
    lateHistRun_.reset();
    lateHistSec_.reset();
    lateHistLastSec_.reset();
    over100Run_ = over100Sec_ = over100LastSec_ = 0;
    sendHist_.reset();
    sendMax_ = sendMaxSec_ = sendMaxLastSec_ = sendAccum_ = 0;
    slotTicks_ = clk::msToTicks(100);
    slotEnd_ = now + slotTicks_;
    slotCount_ = 0;
    std::fill(std::begin(slots_), std::end(slots_), 0u);
    slotHead_ = slotFilled_ = 0;
    slotsDone_ = 0;
}

void RunStats::pushSlot()
{
    slots_[slotHead_] = slotCount_;
    slotHead_ = (slotHead_ + 1) % kCpsHistory;
    if (slotFilled_ < kCpsHistory) ++slotFilled_;
    slotCount_ = 0;
    ++slotsDone_;
    if (slotsDone_ % 10 == 0) { // one second completed
        intervalLastSec_ = intervalSec_;
        lateLastSec_ = lateSec_;
        lateHistLastSec_ = lateHistSec_;
        over100LastSec_ = over100Sec_;
        sendMaxLastSec_ = sendMaxSec_;
        sendMaxSec_ = 0;
        intervalSec_.reset();
        lateSec_.reset();
        lateHistSec_.reset();
        over100Sec_ = 0;
    }
}

void RunStats::advance(int64_t now)
{
    int guard = 0;
    while (now >= slotEnd_) {
        pushSlot();
        slotEnd_ += slotTicks_;
        if (++guard > kCpsHistory + 10) { // long pause: resync without looping forever
            slotEnd_ = now + slotTicks_;
            break;
        }
    }
}

void RunStats::onAction(int64_t fire, int64_t deadline)
{
    advance(fire);
    ++actions_;
    ++slotCount_;
    if (!firstFire_) firstFire_ = fire;
    if (lastFire_) {
        const double iv = double(fire - lastFire_);
        intervalRun_.add(iv);
        intervalSec_.add(iv);
    }
    lastFire_ = fire;
    const int64_t late = fire > deadline ? fire - deadline : 0;
    const double lateD = double(late);
    lateRun_.add(lateD);
    lateSec_.add(lateD);
    const uint64_t ns = uint64_t(clk::ticksToNs(late));
    lateHistRun_.add(ns);
    lateHistSec_.add(ns);
    if (ns > 100'000) {
        ++over100Run_;
        ++over100Sec_;
    }
}

void RunStats::onSendDuration(int64_t ticks)
{
    if (ticks < 0) ticks = 0;
    sendHist_.add(uint64_t(clk::ticksToNs(ticks)));
    if (ticks > sendMax_) sendMax_ = ticks;
    if (ticks > sendMaxSec_) sendMaxSec_ = ticks;
    sendAccum_ += ticks;
}

void RunStats::onActions(int64_t now, uint64_t n)
{
    advance(now);
    actions_ += n;
    slotCount_ += uint32_t(n);
    if (!firstFire_) firstFire_ = now;
    if (lastFire_) {
        const double iv = double(now - lastFire_) / double(n);
        intervalRun_.add(iv);
        intervalSec_.add(iv);
    }
    lastFire_ = now;
}

namespace {
IntervalSummary summarize(const Running& r)
{
    IntervalSummary s;
    s.count = r.n;
    s.minUs = clk::ticksToUs(int64_t(r.mn));
    s.maxUs = clk::ticksToUs(int64_t(r.mx));
    s.avgUs = r.mean() * 1e6 / double(clk::freq());
    s.stddevUs = r.stddev() * 1e6 / double(clk::freq());
    return s;
}
LatenessSummary summarize(const Running& r, const LogHistogram& h, uint64_t over100)
{
    LatenessSummary s;
    s.count = r.n;
    s.avgUs = r.mean() * 1e6 / double(clk::freq());
    s.maxUs = clk::ticksToUs(int64_t(r.mx));
    s.p50Us = double(h.percentile(0.50)) / 1e3;
    s.p99Us = double(h.percentile(0.99)) / 1e3;
    s.p999Us = double(h.percentile(0.999)) / 1e3;
    s.over100us = over100;
    return s;
}
} // namespace

void RunStats::fill(EngineSnapshot& s, int64_t now) const
{
    const double elapsed = clk::ticksToSec(now - start_);
    s.runElapsedSec = elapsed;
    s.runActions = actions_;
    s.runMissed = missed_;
    // Achieved rate = (N-1) intervals between first and last action. Dividing N by the
    // run length would overstate short runs (10 actions end at 0.9 s -> "11.1 CPS").
    if (actions_ >= 2 && lastFire_ > firstFire_) s.runAvgCps = double(actions_ - 1) / clk::ticksToSec(lastFire_ - firstFire_);
    else s.runAvgCps = elapsed > 0 ? double(actions_) / elapsed : 0;

    // Rolling CPS from completed 100 ms slots (+ the partial current slot).
    auto sumLast = [&](int n) {
        uint64_t acc = 0;
        int have = std::min(n, slotFilled_);
        for (int i = 1; i <= have; ++i) acc += slots_[(slotHead_ - i + kCpsHistory) % kCpsHistory];
        return std::pair<uint64_t, int>(acc, have);
    };
    auto [c1, n1] = sumLast(10);
    auto [c5, n5] = sumLast(50);
    s.cps1s = n1 ? double(c1) / (0.1 * n1) : s.runAvgCps;
    s.cps5s = n5 ? double(c5) / (0.1 * n5) : s.runAvgCps;

    s.runInterval = summarize(intervalRun_);
    s.secInterval = summarize(intervalLastSec_.n ? intervalLastSec_ : intervalSec_);
    s.runLate = summarize(lateRun_, lateHistRun_, over100Run_);
    s.secLate = lateLastSec_.n ? summarize(lateLastSec_, lateHistLastSec_, over100LastSec_)
                               : summarize(lateSec_, lateHistSec_, over100Sec_);
    if (targetPeriod_ > 0 && intervalRun_.n > 0) {
        s.avgErrorPct = std::fabs(intervalRun_.mean() - targetPeriod_) / targetPeriod_ * 100.0;
    } else {
        s.avgErrorPct = 0;
    }

    s.sendP50Us = double(sendHist_.percentile(0.50)) / 1e3;
    s.sendP99Us = double(sendHist_.percentile(0.99)) / 1e3;
    s.sendMaxUs = clk::ticksToUs(sendMax_);
    s.sendMaxLastSecUs = clk::ticksToUs(std::max(sendMaxLastSec_, sendMaxSec_));

    s.cpsHistoryCount = slotFilled_;
    for (int i = 0; i < slotFilled_; ++i) {
        int idx = (slotHead_ - slotFilled_ + i + kCpsHistory) % kCpsHistory;
        s.cpsHistory[i] = float(slots_[idx]) * 10.0f;
    }
}

} // namespace infclick
