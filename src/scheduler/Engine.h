#pragma once
// The scheduler / output engine. Owns one dedicated thread:
//
//   idle  : blocked in WaitForSingleObject(cmdEvent)      -> 0% CPU
//   run   : absolute timeline + PreciseWaiter + SendInput  -> CPU depends on precision mode
//
// Everything reaches it as a Command (UI, trigger hook thread, benchmark).
// Commands are delivered through a tiny locked queue + an atomic flag that the
// hot loop polls, plus an event that interrupts any kernel wait - so a trigger
// release stops output immediately even at 1 CPS.
#include "input/InputAction.h"
#include "input/InputSender.h"
#include "platform/win/Power.h"
#include "scheduler/Modes.h"
#include "scheduler/Precision.h"
#include "telemetry/Stats.h"

#include <windows.h>

#include <atomic>
#include <functional>
#include <memory>
#include <thread>
#include <vector>

namespace infclick {

enum class LatePolicy : uint8_t { Skip = 0, CatchUp = 1 };

struct EngineConfig {
    InputAction action;
    ModeParams mode;
    bool maxMode = false;
    double cps = 20.0;
    Precision precision = Precision::Standard;
    power::ThreadPrio priority = power::ThreadPrio::Highest;
    LatePolicy latePolicy = LatePolicy::CatchUp;
    uint32_t maxBatchActions = 8; // MAX mode: actions per SendInput call
    uint32_t maxInflight = 0;     // MAX mode backpressure on hook-observed events (0 = off)
    HWND testTargetHwnd = nullptr; // benchmark/autotest safety gate (foreground + under cursor)
};

enum class CmdType : uint8_t { SetConfig, Arm, TriggerDown, TriggerUp, StartRun, StopRun, Emergency, Quit, ResetTotals };

struct Command {
    CmdType type = CmdType::StopRun;
    int64_t qpc = 0;
    bool flag = false;
    RunLimits limits{};
    std::shared_ptr<const EngineConfig> cfg;
};

class Engine {
public:
    Engine();
    ~Engine();
    Engine(const Engine&) = delete;
    Engine& operator=(const Engine&) = delete;

    bool start();
    void shutdown();

    // --- thread-safe API
    void setConfig(const EngineConfig& c);
    void arm(bool on);
    void triggerDown(int64_t qpc);
    void triggerUp(int64_t qpc);
    void startRun(RunLimits limits = {}); // manual/lab start, ignores "armed"
    void stopRun();
    void emergency();
    void resetTotals();

    bool snapshot(EngineSnapshot& out) const { return snap_.read(out); }
    EngineState state() const { return state_.load(std::memory_order_acquire); }
    bool armed() const { return armed_.load(std::memory_order_acquire); }
    bool running() const
    {
        auto s = state();
        return s == EngineState::Running || s == EngineState::Paused;
    }
    uint64_t runsCompleted() const { return runsCompleted_.load(std::memory_order_acquire); }
    DWORD threadId() const { return threadId_; }
    HANDLE threadHandle() const { return threadHandle_; }

    InputSender& sender() { return sender_; }

    // Wiring (set before start()).
    void setGate(const std::atomic<bool>* gateOpen, const std::atomic<uint8_t>* gateReason, HANDLE gateChanged)
    {
        gateOpen_ = gateOpen;
        gateReason_ = gateReason;
        gateEvent_ = gateChanged;
    }
    void setObservedCounter(const std::atomic<uint64_t>* observed) { observed_ = observed; }
    void setStateCallback(std::function<void(EngineState)> cb) { onState_ = std::move(cb); }

private:
    void post(Command c);
    void threadMain();
    void drain(std::vector<Command>& out);

    // Returns true if the run must stop. Applies config/trigger commands.
    bool handleRunCommands();
    void doRun(RunLimits limits, int64_t triggerQpc);
    void runTimed(RunLimits limits, int64_t triggerQpc);
    void runMax(RunLimits limits, int64_t triggerQpc);
    bool checkGate(PauseReason& why);
    bool pauseUntilGate(PauseReason why); // returns false if the run must stop
    void applyConfig(std::shared_ptr<const EngineConfig> c);
    void rebuildProgram();
    void setState(EngineState s);
    void publish(int64_t now, bool force = false);
    void recordTriggerLatency(int64_t triggerQpc, int64_t fireQpc);

    InputSender sender_;
    class PreciseWaiter* waiter_ = nullptr;

    std::thread thread_;
    DWORD threadId_ = 0;
    HANDLE threadHandle_ = nullptr;
    HANDLE cmdEvent_ = nullptr;
    std::atomic<bool> cmdFlag_{false};
    SRWLOCK cmdLock_ = SRWLOCK_INIT;
    std::vector<Command> queue_;

    std::atomic<EngineState> state_{EngineState::Disarmed};
    std::atomic<bool> armed_{false};
    std::atomic<uint64_t> runsCompleted_{0};

    const std::atomic<bool>* gateOpen_ = nullptr;
    const std::atomic<uint8_t>* gateReason_ = nullptr;
    HANDLE gateEvent_ = nullptr;
    const std::atomic<uint64_t>* observed_ = nullptr;
    std::function<void(EngineState)> onState_;

    // ---- engine-thread-only state
    std::shared_ptr<const EngineConfig> cfg_;
    std::shared_ptr<const EngineConfig> pendingCfg_; // applied at the next action boundary
    ActionProgram prog_;
    ActionProgram maxProg_;
    bool downTimeClamped_ = false;
    power::ThreadPriorityScope prio_;

    struct RunCtl {
        bool stop = false;
        bool emergency = false;
        uint64_t remaining = 0; // UINT64_MAX = unlimited
        int64_t endQpc = INT64_MAX;
    } run_;

    RunStats stats_;
    uint64_t runId_ = 0;
    uint64_t totalMissed_ = 0;
    uint64_t totalActions_ = 0;
    uint64_t baseCalls_ = 0, baseRequested_ = 0, baseAccepted_ = 0, baseFailed_ = 0;
    uint64_t runStartCalls_ = 0, runStartReq_ = 0, runStartAcc_ = 0, runStartFail_ = 0;
    int64_t nextPublish_ = 0;
    int64_t lastPublish_ = 0;
    uint64_t loggedFail_ = 0;
    int64_t lastFailLog_ = 0;
    PauseReason pause_ = PauseReason::None;

    struct RateSample {
        int64_t qpc;
        uint64_t calls, accepted;
    };
    RateSample rateRing_[16]{};
    int rateHead_ = 0, rateCount_ = 0;

    uint64_t trigSamples_ = 0;
    double trigLast_ = 0, trigSum_ = 0, trigMax_ = 0;

    mutable SeqLock<EngineSnapshot> snap_;
    EngineSnapshot work_{};
};

} // namespace infclick
