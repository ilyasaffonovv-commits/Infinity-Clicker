#include "scheduler/Engine.h"

#include "core/Clock.h"
#include "core/Log.h"
#include "core/Str.h"
#include "scheduler/PreciseWaiter.h"

#include <immintrin.h>

#include <algorithm>

namespace infclick {

namespace {
constexpr uint64_t kUnlimited = UINT64_MAX;
constexpr uint32_t kMaxCatchUpBatch = 8;
constexpr double kPublishMs = 100.0;
constexpr double kGateCheckMs = 2.0;
} // namespace

Engine::Engine()
{
    cmdEvent_ = CreateEventW(nullptr, FALSE, FALSE, nullptr); // auto-reset
    cfg_ = std::make_shared<EngineConfig>();
}

Engine::~Engine()
{
    shutdown();
    if (cmdEvent_) CloseHandle(cmdEvent_);
}

bool Engine::start()
{
    if (thread_.joinable()) return true;
    thread_ = std::thread([this] { threadMain(); });
    threadHandle_ = thread_.native_handle();
    return true;
}

void Engine::shutdown()
{
    if (!thread_.joinable()) return;
    Command c;
    c.type = CmdType::Quit;
    post(c);
    thread_.join();
    threadHandle_ = nullptr;
}

void Engine::post(Command c)
{
    AcquireSRWLockExclusive(&cmdLock_);
    queue_.push_back(std::move(c));
    ReleaseSRWLockExclusive(&cmdLock_);
    cmdFlag_.store(true, std::memory_order_release);
    SetEvent(cmdEvent_);
}

void Engine::drain(std::vector<Command>& out)
{
    out.clear();
    AcquireSRWLockExclusive(&cmdLock_);
    out.swap(queue_);
    cmdFlag_.store(false, std::memory_order_relaxed);
    ReleaseSRWLockExclusive(&cmdLock_);
}

void Engine::setConfig(const EngineConfig& c)
{
    Command cmd;
    cmd.type = CmdType::SetConfig;
    cmd.cfg = std::make_shared<const EngineConfig>(c);
    post(std::move(cmd));
}

void Engine::arm(bool on)
{
    armed_.store(on, std::memory_order_release);
    Command c;
    c.type = CmdType::Arm;
    c.flag = on;
    post(c);
}

void Engine::triggerDown(int64_t qpc)
{
    Command c;
    c.type = CmdType::TriggerDown;
    c.qpc = qpc;
    post(c);
}

void Engine::triggerUp(int64_t qpc)
{
    Command c;
    c.type = CmdType::TriggerUp;
    c.qpc = qpc;
    post(c);
}

void Engine::startRun(RunLimits limits)
{
    Command c;
    c.type = CmdType::StartRun;
    c.limits = limits;
    c.qpc = clk::now();
    post(c);
}

void Engine::stopRun()
{
    Command c;
    c.type = CmdType::StopRun;
    post(c);
}

void Engine::emergency()
{
    armed_.store(false, std::memory_order_release);
    Command c;
    c.type = CmdType::Emergency;
    c.qpc = clk::now();
    post(c);
}

void Engine::resetTotals()
{
    Command c;
    c.type = CmdType::ResetTotals;
    post(c);
}

void Engine::setState(EngineState s)
{
    EngineState old = state_.exchange(s, std::memory_order_acq_rel);
    if (old != s && onState_) onState_(s);
}

void Engine::applyConfig(std::shared_ptr<const EngineConfig> c)
{
    if (!c) return;
    const bool prioChanged = !cfg_ || cfg_->priority != c->priority;
    cfg_ = std::move(c);
    if (prioChanged) prio_.apply(cfg_->priority);
    rebuildProgram();
}

void Engine::rebuildProgram()
{
    prog_ = compileAction(cfg_->action);
    downTimeClamped_ = false;
    if (!cfg_->maxMode && prog_.ok() && cfg_->cps > 0) {
        const double period = double(clk::freq()) / cfg_->cps;
        // An action must finish before the next one starts; otherwise DOWN/UP
        // ordering between actions would break. Scale hold/gap down to 50%.
        if (prog_.spanTicks > 0 && double(prog_.spanTicks) >= period * 0.9) {
            InputAction a = cfg_->action;
            const double f = (period * 0.5) / double(prog_.spanTicks);
            a.downTimeUs = uint32_t(double(a.downTimeUs) * f);
            a.repeatGapUs = uint32_t(double(a.repeatGapUs) * f);
            prog_ = compileAction(a);
            downTimeClamped_ = true;
        }
    }
    InputAction m = cfg_->action;
    m.downTimeUs = 0;
    m.repeatGapUs = 0;
    maxProg_ = compileAction(m);
}

void Engine::threadMain()
{
    SetThreadDescription(GetCurrentThread(), L"Infinity Clicker Scheduler");
    threadId_ = GetCurrentThreadId();
    PreciseWaiter waiter;
    waiter_ = &waiter;
    waiter.calibrate();
    prio_.apply(cfg_->priority);
    rebuildProgram();
    TLOG_I("engine: scheduler thread up, high-res timer=%s, oversleep p50=%.0fus p99=%.0fus",
           waiter.highResolution() ? "yes" : "NO", waiter.oversleepP50Us(), waiter.oversleepP99Us());
    publish(clk::now(), true);

    std::vector<Command> cmds;
    bool quit = false;
    while (!quit) {
        WaitForSingleObject(cmdEvent_, INFINITE);
        drain(cmds);
        bool start = false;
        RunLimits lim{};
        int64_t trigQ = 0;
        for (Command& c : cmds) {
            switch (c.type) {
            case CmdType::SetConfig: applyConfig(c.cfg); break;
            case CmdType::Arm:
                armed_.store(c.flag, std::memory_order_release);
                setState(c.flag ? EngineState::Armed : EngineState::Disarmed);
                TLOG_I("engine: %s", c.flag ? "armed" : "disarmed");
                break;
            case CmdType::TriggerDown:
                if (armed_.load()) {
                    ModeDecision d = decideOnTriggerDown(cfg_->mode, false);
                    if (d.kind == ModeDecision::Kind::Start) {
                        start = true;
                        lim = d.limits;
                        trigQ = c.qpc;
                    }
                }
                break;
            case CmdType::TriggerUp:
                // A Hold tap shorter than our wake-up: down+up in the same batch.
                if (start && cfg_->mode.mode == TriggerMode::Hold) start = false;
                break;
            case CmdType::StartRun:
                start = true;
                lim = c.limits;
                trigQ = c.qpc;
                break;
            case CmdType::StopRun: start = false; break;
            case CmdType::Emergency:
                start = false;
                armed_.store(false);
                sender_.releaseAll();
                setState(EngineState::Disarmed);
                TLOG_W("engine: EMERGENCY STOP (idle) - all held input released");
                break;
            case CmdType::ResetTotals:
                totalActions_ = totalMissed_ = 0;
                baseCalls_ = sender_.calls.load();
                baseRequested_ = sender_.requested.load();
                baseAccepted_ = sender_.accepted.load();
                baseFailed_ = sender_.failedCalls.load();
                break;
            case CmdType::Quit: quit = true; break;
            }
        }
        publish(clk::now(), true);
        if (!quit && start) doRun(lim, trigQ);
        if (run_.emergency) run_.emergency = false;
    }
    sender_.releaseAll();
    prio_.reset();
    waiter_ = nullptr;
}

bool Engine::handleRunCommands()
{
    static thread_local std::vector<Command> cmds;
    drain(cmds);
    for (Command& c : cmds) {
        switch (c.type) {
        case CmdType::SetConfig: pendingCfg_ = c.cfg; break;
        case CmdType::Arm:
            armed_.store(c.flag, std::memory_order_release);
            if (!c.flag) run_.stop = true;
            break;
        case CmdType::TriggerDown: {
            ModeDecision d = decideOnTriggerDown(cfg_->mode, true);
            if (d.kind == ModeDecision::Kind::Stop) run_.stop = true;
            else if (d.kind == ModeDecision::Kind::Extend && run_.remaining != kUnlimited) run_.remaining += d.extendBy;
            break;
        }
        case CmdType::TriggerUp:
            if (decideOnTriggerUp(cfg_->mode, true).kind == ModeDecision::Kind::Stop) run_.stop = true;
            break;
        case CmdType::StartRun: break;
        case CmdType::StopRun: run_.stop = true; break;
        case CmdType::Emergency:
            run_.stop = true;
            run_.emergency = true;
            armed_.store(false);
            break;
        case CmdType::ResetTotals: break;
        case CmdType::Quit:
            run_.stop = true;
            post(c); // re-queue so the idle loop exits after cleanup
            return true;
        }
    }
    return run_.stop;
}

void Engine::doRun(RunLimits limits, int64_t triggerQpc)
{
    if (!prog_.ok()) {
        TLOG_W("engine: cannot start - %s", prog_.error.c_str());
        return;
    }
    power::setThreadHighQos(true);
    power::setProcessHighQos(true);

    run_ = RunCtl{};
    run_.remaining = limits.maxActions ? limits.maxActions : kUnlimited;
    const int64_t now = clk::now();
    run_.endQpc = limits.durationMs > 0 ? now + clk::msToTicks(limits.durationMs) : INT64_MAX;
    ++runId_;
    runStartCalls_ = sender_.calls.load();
    runStartReq_ = sender_.requested.load();
    runStartAcc_ = sender_.accepted.load();
    runStartFail_ = sender_.failedCalls.load();
    loggedFail_ = 0;
    waiter_->resetAccounting();
    stats_.begin(now, cfg_->maxMode ? 0.0 : double(clk::freq()) / cfg_->cps);
    rateCount_ = 0;
    lastPublish_ = now;
    pause_ = PauseReason::None;
    setState(EngineState::Running);
    publish(now, true);
    TLOG_I("engine: run #%llu start - action=%s, %s, precision=%s, limit=%s", (unsigned long long)runId_,
           cfg_->action.chord.name().c_str(),
           cfg_->maxMode ? "MAX speed" : strFormat("target %.2f CPS", cfg_->cps).c_str(),
           precisionName(cfg_->precision),
           limits.maxActions ? strFormat("%llu actions", (unsigned long long)limits.maxActions).c_str()
           : limits.durationMs > 0 ? strFormat("%.0f ms", limits.durationMs).c_str()
                                   : "none");
    if (downTimeClamped_) TLOG_W("engine: down time >= interval, clamped to 50%% of the interval");

    if (cfg_->maxMode) runMax(limits, triggerQpc);
    else runTimed(limits, triggerQpc);

    sender_.releaseAll();
    const int64_t end = clk::now();
    totalActions_ += stats_.actions();
    totalMissed_ += stats_.missed();
    power::setThreadHighQos(false);
    power::setProcessHighQos(false);
    pause_ = PauseReason::None;

    const EngineState finalState = armed_.load() ? EngineState::Armed : EngineState::Disarmed;
    state_.store(finalState, std::memory_order_release); // snapshot shows the final state
    publish(end, true);
    runsCompleted_.fetch_add(1, std::memory_order_acq_rel);
    if (onState_) onState_(finalState);

    EngineSnapshot s;
    snap_.read(s);
    TLOG_I("engine: run #%llu %s - %llu actions in %.3f s = %.2f CPS (target %s), interval avg %.1f us sd %.1f us, "
           "late p99 %.1f us, missed %llu, SendInput calls %llu, failed %llu",
           (unsigned long long)runId_, run_.emergency ? "EMERGENCY-STOPPED" : "stopped",
           (unsigned long long)s.runActions, s.runElapsedSec, s.runAvgCps,
           cfg_->maxMode ? "MAX" : strFormat("%.2f", cfg_->cps).c_str(), s.runInterval.avgUs, s.runInterval.stddevUs,
           s.runLate.p99Us, (unsigned long long)s.runMissed, (unsigned long long)s.runCalls,
           (unsigned long long)s.runFailedCalls);
    if (run_.emergency) TLOG_W("engine: EMERGENCY STOP - output halted, all held input released, disarmed");

    if (pendingCfg_) {
        applyConfig(pendingCfg_);
        pendingCfg_.reset();
    }
}

bool Engine::checkGate(PauseReason& why)
{
    if (cfg_->testTargetHwnd) {
        HWND target = cfg_->testTargetHwnd;
        HWND fg = GetForegroundWindow();
        POINT p{};
        GetCursorPos(&p);
        HWND under = WindowFromPoint(p);
        if (fg != target || (under != target && !IsChild(target, under))) {
            why = PauseReason::TestGate;
            return false;
        }
        return true;
    }
    if (gateOpen_ && !gateOpen_->load(std::memory_order_acquire)) {
        why = gateReason_ ? PauseReason(gateReason_->load()) : PauseReason::TargetNotForeground;
        return false;
    }
    return true;
}

bool Engine::pauseUntilGate(PauseReason why)
{
    sender_.releaseAll();
    pause_ = why;
    setState(EngineState::Paused);
    publish(clk::now(), true);
    TLOG_D("engine: paused (reason %d)", int(why));
    HANDLE hs[2] = {cmdEvent_, gateEvent_};
    const DWORD n = gateEvent_ ? 2 : 1;
    for (;;) {
        WaitForMultipleObjects(n, hs, FALSE, 50);
        if (cmdFlag_.load(std::memory_order_acquire) && handleRunCommands()) {
            pause_ = PauseReason::None;
            return false;
        }
        PauseReason w;
        if (checkGate(w)) break;
        pause_ = w;
        publish(clk::now());
    }
    pause_ = PauseReason::None;
    setState(EngineState::Running);
    TLOG_D("engine: resumed");
    return true;
}

void Engine::recordTriggerLatency(int64_t triggerQpc, int64_t fireQpc)
{
    if (triggerQpc <= 0 || fireQpc < triggerQpc) return;
    const double us = clk::ticksToUs(fireQpc - triggerQpc);
    if (us > 1e6) return;
    ++trigSamples_;
    trigLast_ = us;
    trigSum_ += us;
    trigMax_ = std::max(trigMax_, us);
}

void Engine::runTimed(RunLimits limits, int64_t triggerQpc)
{
    double period = double(clk::freq()) / cfg_->cps;
    Timeline tl;
    int64_t now = clk::now();
    tl.anchor(now, period);
    waiter_->setPeriodHint(int64_t(period));
    size_t step = 0;
    bool first = true;
    int64_t lastGate = 0;
    const int64_t gateTicks = clk::msToTicks(kGateCheckMs);
    const int64_t pubTicks = clk::msToTicks(kPublishMs);
    const int64_t catchUpLimit = clk::msToTicks(250);

    for (;;) {
        if (cmdFlag_.load(std::memory_order_acquire) && handleRunCommands()) break;

        if (step == 0) {
            if (pendingCfg_) {
                auto c = std::move(pendingCfg_);
                pendingCfg_.reset();
                const bool rateChanged = c->cps != cfg_->cps;
                applyConfig(c);
                if (cfg_->maxMode) { // switched to MAX while running
                    RunLimits rest{};
                    rest.maxActions = run_.remaining == kUnlimited ? 0 : run_.remaining;
                    runMax(rest, 0);
                    return;
                }
                if (rateChanged) {
                    period = double(clk::freq()) / cfg_->cps;
                    tl.changePeriod(period);
                    waiter_->setPeriodHint(int64_t(period));
                }
            }
            if (!prog_.ok() || run_.remaining == 0) break;
            now = clk::now();
            if (now - lastGate >= gateTicks) {
                lastGate = now;
                PauseReason why;
                if (!checkGate(why)) {
                    if (!pauseUntilGate(why)) break;
                    now = clk::now();
                    tl.anchor(now, period); // fresh grid after a pause
                    continue;
                }
            }
            if (!first) {
                if (cfg_->latePolicy == LatePolicy::Skip) {
                    if (uint64_t sk = tl.skipLate(now)) stats_.onMissed(sk);
                } else if (now - tl.next() > catchUpLimit) {
                    stats_.onMissed(tl.skipLate(now));
                }
            }
            if (tl.next() >= run_.endQpc) break;
        }

        const ActionStep& st = prog_.steps[step];
        const int64_t deadline = tl.next() + st.offsetTicks;
        now = clk::now();
        if (deadline > now) {
            // Long waits are chunked so telemetry keeps flowing (ECO wait, no spin).
            if (deadline - now > pubTicks / 2 && nextPublish_ < deadline) {
                waiter_->waitUntil(nextPublish_, Precision::Eco, cmdEvent_, &cmdFlag_);
                publish(clk::now());
                continue;
            }
            if (waiter_->waitUntil(deadline, cfg_->precision, cmdEvent_, &cmdFlag_) == WaitResult::Interrupted)
                continue;
        }

        const int64_t fire = clk::now();
        // Adaptive batching (CatchUp policy, single-step actions only): when the
        // grid is ahead of what one SendInput call per action can deliver, the
        // actions whose slots are already due go out in ONE call (<= 8 - larger
        // batches were measured to lose events and stall behind LL hooks).
        uint32_t batch = 1;
        if (step == 0 && cfg_->latePolicy == LatePolicy::CatchUp && prog_.steps.size() == 1) {
            const uint64_t cap = std::min<uint64_t>(kMaxCatchUpBatch, run_.remaining);
            while (batch < cap && tl.slot(tl.k + batch) <= fire && tl.slot(tl.k + batch) < run_.endQpc) ++batch;
        }
        if (batch == 1) {
            sender_.send(st.inputs, st.count);
        } else {
            INPUT buf[kMaxCatchUpBatch * kMaxStepInputs];
            for (uint32_t j = 0; j < batch; ++j) std::copy(st.inputs, st.inputs + st.count, buf + j * st.count);
            sender_.send(buf, batch * st.count);
        }
        stats_.onSendDuration(clk::now() - fire);
        if (step == 0) {
            for (uint32_t j = 0; j < batch; ++j) stats_.onAction(fire, tl.slot(tl.k + j));
            if (first) {
                recordTriggerLatency(triggerQpc, fire);
                first = false;
            }
        }
        if (++step == prog_.steps.size()) {
            step = 0;
            tl.k += batch;
            if (run_.remaining != kUnlimited) run_.remaining -= batch;
        }
        if (fire >= nextPublish_) publish(fire);
    }
    (void)limits;
}

void Engine::runMax(RunLimits limits, int64_t triggerQpc)
{
    (void)limits;
    if (!maxProg_.ok()) return;
    auto buildBatch = [&](std::vector<INPUT>& b, uint32_t& per, uint32_t& batchActions) {
        per = maxProg_.inputsPerAction;
        batchActions = std::clamp<uint32_t>(cfg_->maxBatchActions, 1, 256);
        b.clear();
        for (uint32_t i = 0; i < batchActions; ++i) b.insert(b.end(), maxProg_.flat.begin(), maxProg_.flat.end());
    };
    std::vector<INPUT> batch;
    uint32_t per = 0, B = 0;
    buildBatch(batch, per, B);

    bool first = true;
    int64_t lastGate = 0;
    const int64_t gateTicks = clk::msToTicks(1.0);
    uint32_t bpSpins = 0;
    // In-flight = accepted-by-SendInput minus seen-by-our-hook, relative to the run start
    // (the counters also contain history from before the hook was installed).
    const int64_t bpBase = observed_ ? int64_t(sender_.accepted.load()) - int64_t(observed_->load()) : 0;

    for (;;) {
        if (cmdFlag_.load(std::memory_order_acquire) && handleRunCommands()) break;
        if (pendingCfg_) {
            auto c = std::move(pendingCfg_);
            pendingCfg_.reset();
            applyConfig(c);
            if (!cfg_->maxMode) break; // left MAX mode: end this run
            buildBatch(batch, per, B);
        }
        if (run_.remaining == 0) break;
        int64_t now = clk::now();
        if (now >= run_.endQpc) break;
        if (now - lastGate >= gateTicks) {
            lastGate = now;
            PauseReason why;
            if (!checkGate(why) && !pauseUntilGate(why)) break;
        }
        // Backpressure: never let the system input queue fall far behind us,
        // otherwise physical input (incl. the emergency key) queues up behind
        // a mountain of synthetic events.
        if (observed_ && cfg_->maxInflight) {
            const int64_t inflight = int64_t(sender_.accepted.load(std::memory_order_relaxed)) -
                                     int64_t(observed_->load(std::memory_order_relaxed)) - bpBase;
            if (inflight > int64_t(cfg_->maxInflight)) {
                if (++bpSpins > 64) {
                    SwitchToThread();
                    bpSpins = 0;
                } else {
                    _mm_pause();
                }
                if (now >= nextPublish_) publish(now);
                continue;
            }
        }
        const uint32_t n = run_.remaining == kUnlimited ? B : uint32_t(std::min<uint64_t>(B, run_.remaining));
        const int64_t fire = clk::now();
        sender_.send(batch.data(), n * per);
        stats_.onSendDuration(clk::now() - fire);
        stats_.onActions(fire, n);
        if (first) {
            recordTriggerLatency(triggerQpc, fire);
            first = false;
        }
        if (run_.remaining != kUnlimited) run_.remaining -= n;
        if (fire >= nextPublish_) publish(fire);
    }
}

void Engine::publish(int64_t now, bool force)
{
    if (!force && now < nextPublish_) return;
    nextPublish_ = now + clk::msToTicks(kPublishMs);
    EngineSnapshot& s = work_;
    ++s.publishSeq;
    s.publishQpc = now;
    s.state = state_.load(std::memory_order_acquire);
    s.pause = pause_;
    s.maxMode = cfg_->maxMode;
    s.precision = cfg_->precision;
    s.mode = cfg_->mode.mode;
    s.threadPriority = int(cfg_->priority);
    s.targetCps = cfg_->maxMode ? 0.0 : cfg_->cps;
    s.targetIntervalUs = cfg_->cps > 0 ? 1e6 / cfg_->cps : 0;

    const uint64_t calls = sender_.calls.load(std::memory_order_relaxed);
    const uint64_t req = sender_.requested.load(std::memory_order_relaxed);
    const uint64_t acc = sender_.accepted.load(std::memory_order_relaxed);
    const uint64_t fail = sender_.failedCalls.load(std::memory_order_relaxed);
    const bool active = s.state == EngineState::Running || s.state == EngineState::Paused;

    s.totalCalls = calls - baseCalls_;
    s.totalRequested = req - baseRequested_;
    s.totalAccepted = acc - baseAccepted_;
    s.totalFailedCalls = fail - baseFailed_;
    s.totalActions = totalActions_ + (active ? stats_.actions() : 0);
    s.totalMissed = totalMissed_ + (active ? stats_.missed() : 0);
    s.totalRuns = runId_;
    s.lastError = sender_.lastError.load(std::memory_order_relaxed);

    if (runId_ > 0 && (active || force)) {
        stats_.fill(s, now);
        s.runId = runId_;
        s.runCalls = calls - runStartCalls_;
        s.runRequested = req - runStartReq_;
        s.runAccepted = acc - runStartAcc_;
        const uint64_t runFail = fail - runStartFail_;
        // Aggregated: at most one line per 2 s even if every call fails
        // (e.g. ERROR_ACCESS_DENIED while the workstation is locked).
        if (runFail > loggedFail_ && (force || now - lastFailLog_ > clk::secToTicks(2))) {
            TLOG_W("engine: SendInput rejected events - %llu failed calls in this run, last error %u%s",
                   (unsigned long long)runFail, s.lastError,
                   s.lastError == ERROR_ACCESS_DENIED ? " (input desktop not accessible: locked screen / UAC prompt)" : "");
            loggedFail_ = runFail;
            lastFailLog_ = now;
        }
        s.runFailedCalls = runFail;
    }

    // Calls/s and events/s over the last ~second.
    rateRing_[rateHead_] = {now, calls, acc};
    rateHead_ = (rateHead_ + 1) % 16;
    if (rateCount_ < 16) ++rateCount_;
    if (active && rateCount_ >= 2) {
        const int64_t horizon = now - clk::msToTicks(1000);
        int idx = (rateHead_ - 1 + 16) % 16;
        for (int i = 1; i < rateCount_; ++i) {
            int j = (rateHead_ - 1 - i + 32) % 16;
            idx = j;
            if (rateRing_[j].qpc <= horizon) break;
        }
        const RateSample& o = rateRing_[idx];
        const double dt = clk::ticksToSec(now - o.qpc);
        if (dt > 0) {
            s.callsPerSec1s = double(calls - o.calls) / dt;
            s.eventsPerSec1s = double(acc - o.accepted) / dt;
        }
    } else if (!active) {
        s.callsPerSec1s = s.eventsPerSec1s = 0;
    }

    if (waiter_) {
        const int64_t span = now - lastPublish_;
        const int64_t sendTicks = stats_.takeSendTicks();
        if (span > 0 && active) {
            s.spinShare = double(waiter_->spinTicks) / double(span);
            s.sleepShare = double(waiter_->sleepTicks) / double(span);
            s.sendShare = double(sendTicks) / double(span);
        } else if (!active) {
            s.spinShare = s.sleepShare = s.sendShare = 0;
        }
        waiter_->resetAccounting();
        if (!active && cfg_->cps > 0) waiter_->setPeriodHint(int64_t(double(clk::freq()) / cfg_->cps));
        s.marginUs = waiter_->marginUs(cfg_->precision);
        s.oversleepP50Us = waiter_->oversleepP50Us();
        s.oversleepP99Us = waiter_->oversleepP99Us();
        s.highResTimer = waiter_->highResolution();
    }
    lastPublish_ = now;

    s.triggerSamples = trigSamples_;
    s.lastTriggerLatUs = trigLast_;
    s.avgTriggerLatUs = trigSamples_ ? trigSum_ / double(trigSamples_) : 0;
    s.maxTriggerLatUs = trigMax_;

    snap_.write(s);

    if (active && log::verbose() && (s.publishSeq % 10) == 0) {
        TLOG_D("engine: run #%llu t=%.1fs cps1s=%.1f cps5s=%.1f late p50=%.1fus p99=%.1fus missed=%llu spin=%.0f%%",
               (unsigned long long)runId_, s.runElapsedSec, s.cps1s, s.cps5s, s.secLate.p50Us, s.secLate.p99Us,
               (unsigned long long)s.runMissed, s.spinShare * 100);
    }
}

} // namespace infclick
