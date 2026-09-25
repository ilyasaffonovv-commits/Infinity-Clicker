#pragma once
// Engine telemetry: allocation-free accumulators updated on the scheduler
// thread (a few adds per action) and a seqlock-published snapshot read by the
// UI / benchmark without ever blocking the scheduler.
#include "scheduler/Modes.h"
#include "scheduler/Precision.h"

#include <immintrin.h>

#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstring>

namespace infclick {

// Log-linear histogram (8 sub-buckets per power of two) over nanoseconds.
// Percentile error <= 6.25% - plenty for jitter/latency reporting.
struct LogHistogram {
    static constexpr int kSubBits = 3;
    static constexpr int kSub = 1 << kSubBits;
    static constexpr int kMaxMsb = 40; // ~1100 s
    static constexpr int kBuckets = (kMaxMsb - kSubBits + 1) * kSub + kSub;

    uint32_t counts[kBuckets]{};
    uint64_t total = 0;

    static int index(uint64_t v);
    static uint64_t bucketMid(int idx);
    void add(uint64_t ns)
    {
        ++counts[index(ns)];
        ++total;
    }
    void reset()
    {
        std::memset(counts, 0, sizeof counts);
        total = 0;
    }
    uint64_t percentile(double q) const;
};

struct Running {
    uint64_t n = 0;
    double sum = 0, sumSq = 0, mn = 0, mx = 0;
    void add(double v)
    {
        if (n == 0) mn = mx = v;
        else {
            if (v < mn) mn = v;
            if (v > mx) mx = v;
        }
        ++n;
        sum += v;
        sumSq += v * v;
    }
    double mean() const { return n ? sum / double(n) : 0; }
    double stddev() const
    {
        if (n < 2) return 0;
        double m = mean();
        double var = sumSq / double(n) - m * m;
        return var > 0 ? std::sqrt(var) : 0;
    }
    void reset() { *this = Running{}; }
};

struct IntervalSummary {
    uint64_t count = 0;
    double minUs = 0, maxUs = 0, avgUs = 0, stddevUs = 0;
};

struct LatenessSummary {
    uint64_t count = 0;
    double avgUs = 0, p50Us = 0, p99Us = 0, p999Us = 0, maxUs = 0;
    uint64_t over100us = 0; // events fired >100 us after their deadline
};

enum class EngineState : uint8_t { Disarmed = 0, Armed, Running, Paused };

enum class PauseReason : uint8_t { None = 0, TargetNotForeground, OwnWindow, TestGate };

constexpr int kCpsHistory = 100; // 100 ms slots -> 10 s sparkline

struct EngineSnapshot {
    uint64_t publishSeq = 0;
    int64_t publishQpc = 0;

    EngineState state = EngineState::Disarmed;
    PauseReason pause = PauseReason::None;
    bool maxMode = false;
    Precision precision = Precision::Standard;
    TriggerMode mode = TriggerMode::Toggle;
    int threadPriority = 0;

    double targetCps = 0;
    double targetIntervalUs = 0;

    // Totals since application start
    uint64_t totalActions = 0, totalCalls = 0, totalRequested = 0, totalAccepted = 0, totalFailedCalls = 0;
    uint64_t totalMissed = 0, totalRuns = 0;
    uint32_t lastError = 0;

    // Current (or last finished) run
    uint64_t runId = 0;
    double runElapsedSec = 0;
    uint64_t runActions = 0, runMissed = 0;
    uint64_t runCalls = 0, runRequested = 0, runAccepted = 0, runFailedCalls = 0;
    double runAvgCps = 0;
    double cps1s = 0, cps5s = 0;
    double callsPerSec1s = 0, eventsPerSec1s = 0;
    IntervalSummary runInterval, secInterval;
    LatenessSummary runLate, secLate;
    double avgErrorPct = 0; // |mean interval - target| / target * 100

    // Time spent *inside* SendInput (blocked by the system input path / LL hooks)
    double sendP50Us = 0, sendP99Us = 0, sendMaxUs = 0; // per call, whole run
    double sendMaxLastSecUs = 0;                         // worst call in the last completed second
    double sendShare = 0;                                // fraction of wall time inside SendInput (last publish period)

    // Scheduler internals
    double spinShare = 0;   // fraction of wall time spent spinning (last second)
    double sleepShare = 0;
    double marginUs = 0;
    double oversleepP50Us = 0, oversleepP99Us = 0;
    bool highResTimer = false;

    // Trigger -> first action latency (measured with QPC inside the process)
    uint64_t triggerSamples = 0;
    double lastTriggerLatUs = 0, avgTriggerLatUs = 0, maxTriggerLatUs = 0;

    float cpsHistory[kCpsHistory]{};
    int cpsHistoryCount = 0;
};

template <class T>
class SeqLock {
public:
    void write(const T& v)
    {
        const uint32_t s = seq_.load(std::memory_order_relaxed);
        seq_.store(s + 1, std::memory_order_relaxed);
        std::atomic_thread_fence(std::memory_order_release);
        std::memcpy(&data_, &v, sizeof(T));
        seq_.store(s + 2, std::memory_order_release);
    }
    bool read(T& out) const
    {
        for (int i = 0; i < 1000; ++i) {
            const uint32_t s1 = seq_.load(std::memory_order_acquire);
            if (s1 & 1) {
                _mm_pause();
                continue;
            }
            std::memcpy(&out, &data_, sizeof(T));
            std::atomic_thread_fence(std::memory_order_acquire);
            if (seq_.load(std::memory_order_relaxed) == s1) return true;
        }
        return false;
    }

private:
    std::atomic<uint32_t> seq_{0};
    T data_{};
};

// Accumulates per-run statistics on the scheduler thread.
class RunStats {
public:
    void begin(int64_t now, double targetPeriodTicks);
    void onAction(int64_t fireQpc, int64_t deadlineQpc);
    void onActions(int64_t now, uint64_t n); // MAX mode: n actions in one call, no deadline
    void onMissed(uint64_t n) { missed_ += n; }
    void onSendDuration(int64_t ticks); // time one SendInput call blocked us
    int64_t takeSendTicks()             // accumulated SendInput time since last call
    {
        const int64_t t = sendAccum_;
        sendAccum_ = 0;
        return t;
    }
    void advance(int64_t now); // roll 100 ms slots
    void fill(EngineSnapshot& s, int64_t now) const;

    uint64_t actions() const { return actions_; }
    uint64_t missed() const { return missed_; }

private:
    void pushSlot();

    int64_t start_ = 0;
    double targetPeriod_ = 0;
    uint64_t actions_ = 0, missed_ = 0;
    int64_t lastFire_ = 0;
    int64_t firstFire_ = 0;

    Running intervalRun_, intervalSec_, intervalLastSec_;
    Running lateRun_, lateSec_, lateLastSec_;
    LogHistogram lateHistRun_, lateHistSec_, lateHistLastSec_;
    uint64_t over100Run_ = 0, over100Sec_ = 0, over100LastSec_ = 0;
    LogHistogram sendHist_;
    int64_t sendMax_ = 0, sendMaxSec_ = 0, sendMaxLastSec_ = 0, sendAccum_ = 0;

    int64_t slotTicks_ = 0, slotEnd_ = 0;
    uint32_t slotCount_ = 0;
    uint32_t slots_[kCpsHistory]{};
    int slotHead_ = 0, slotFilled_ = 0;
    uint64_t slotsDone_ = 0;
};

} // namespace infclick
