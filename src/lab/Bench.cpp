#include "lab/Bench.h"

#include "core/Clock.h"
#include "core/I18n.h"
#include "core/Log.h"
#include "core/Paths.h"
#include "core/Str.h"
#include "input/InputAction.h"
#include "lab/TestPad.h"
#include "platform/win/Guardian.h"
#include "platform/win/Power.h"
#include "scheduler/Engine.h"
#include "scheduler/PreciseWaiter.h"
#include "telemetry/CpuMeter.h"
#include "trigger/TriggerEngine.h"

#include <immintrin.h>
#include <timeapi.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdarg>
#include <functional>
#include <numeric>
#include <random>
#include <thread>

namespace infclick::bench {

// ============================================================ console

namespace {
HANDLE g_out = nullptr;
std::string g_transcript;
} // namespace

void consoleInit()
{
    g_out = GetStdHandle(STD_OUTPUT_HANDLE);
    DWORD type = g_out && g_out != INVALID_HANDLE_VALUE ? GetFileType(g_out) : FILE_TYPE_UNKNOWN;
    if (type == FILE_TYPE_UNKNOWN) {
        if (AttachConsole(ATTACH_PARENT_PROCESS)) {
            g_out = CreateFileW(L"CONOUT$", GENERIC_WRITE, FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr);
            if (g_out == INVALID_HANDLE_VALUE) g_out = nullptr;
        } else {
            g_out = nullptr;
        }
    }
    if (g_out) SetConsoleOutputCP(CP_UTF8);
}

void consolePrint(const char* fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    std::string s = strFormatV(fmt, ap);
    va_end(ap);
    if (g_out) {
        DWORD w;
        WriteFile(g_out, s.data(), DWORD(s.size()), &w, nullptr);
    }
    OutputDebugStringA(s.c_str());
}

// ============================================================ helpers

namespace {

std::atomic<bool> g_abort{false};

struct Dist {
    size_t n = 0;
    double mean = 0, min = 0, p50 = 0, p90 = 0, p99 = 0, max = 0, sd = 0;
};

Dist dist(std::vector<double> v)
{
    Dist d;
    d.n = v.size();
    if (v.empty()) return d;
    std::sort(v.begin(), v.end());
    auto q = [&](double p) { return v[std::min(v.size() - 1, size_t(p * double(v.size() - 1) + 0.5))]; };
    d.min = v.front();
    d.max = v.back();
    d.p50 = q(0.5);
    d.p90 = q(0.9);
    d.p99 = q(0.99);
    d.mean = std::accumulate(v.begin(), v.end(), 0.0) / double(v.size());
    double acc = 0;
    for (double x : v) acc += (x - d.mean) * (x - d.mean);
    d.sd = v.size() > 1 ? std::sqrt(acc / double(v.size() - 1)) : 0;
    return d;
}

class Report {
public:
    void h1(const std::string& s) { add("\n# " + s + "\n\n"); }
    void h2(const std::string& s) { add("\n## " + s + "\n\n"); }
    void para(const std::string& s) { add(s + "\n\n"); }
    void line(const std::string& s) { add(s + "\n"); }
    void table(const std::vector<std::string>& header)
    {
        std::string a = "|", b = "|";
        for (const auto& h : header) {
            a += " " + h + " |";
            b += "---|";
        }
        add(a + "\n" + b + "\n");
    }
    void row(const std::vector<std::string>& cells)
    {
        std::string a = "|";
        for (const auto& c : cells) a += " " + c + " |";
        add(a + "\n");
    }
    void end() { add("\n"); }
    const std::string& text() const { return md_; }

private:
    void add(const std::string& s)
    {
        md_ += s;
        consolePrint("%s", s.c_str());
    }
    std::string md_;
};

std::string f0(double v) { return strFormat("%.0f", v); }
std::string f1(double v) { return strFormat("%.1f", v); }
std::string f2(double v) { return strFormat("%.2f", v); }
std::string f3(double v) { return strFormat("%.3f", v); }
std::string u64(uint64_t v) { return strFormat("%llu", (unsigned long long)v); }
std::string pct(double v) { return strFormat("%.2f%%", v); }

// ------------------------------------------------------------ environment

struct BenchSink : TriggerSink {
    Engine* engine = nullptr;
    std::atomic<int64_t> lastDown{0}, lastUp{0};
    void onTriggerDown(int64_t q) override
    {
        lastDown.store(q);
        engine->triggerDown(q);
    }
    void onTriggerUp(int64_t q) override
    {
        lastUp.store(q);
        engine->triggerUp(q);
    }
    void onEmergency() override
    {
        g_abort.store(true);
        engine->emergency();
    }
    void onCaptured(const KeyChord&) override {}
    bool isArmed() const override { return engine->armed(); }
};

struct Env {
    Engine engine;
    TriggerEngine trigger;
    TestPad pad;
    BenchSink sink;
    TriggerConfig tcfg;
    Options opt;
    Report rep;
};

bool ensurePadForeground(Env& env)
{
    if (GetForegroundWindow() == env.pad.hwnd()) return true;
    return env.pad.activate();
}

void setObserveMouse(Env& env, bool on, TriggerBackend backend = TriggerBackend::Hook, bool mouseTrigger = false)
{
    env.tcfg.observeMouse = on;
    env.tcfg.mouseBackend = backend;
    env.tcfg.trigger = mouseTrigger ? KeyChord::mouse(MouseButton::X2) : KeyChord::key(VK_F13);
    env.trigger.setConfig(env.tcfg);
    for (int i = 0; i < 100; ++i) {
        bool hookOk = env.trigger.mouseHookInstalled() == on;
        bool rawOk = env.trigger.rawInputActive() == (!on && mouseTrigger && backend == TriggerBackend::RawInput);
        if (hookOk && rawOk) break;
        Sleep(5);
    }
}

// Waits until the pad stops receiving (or reaches `expected`). Returns last receive QPC.
int64_t waitPadDrain(Env& env, uint64_t expected, int maxMs = 4000)
{
    const int64_t deadline = clk::now() + clk::msToTicks(maxMs);
    uint64_t last = env.pad.received();
    int64_t stableSince = clk::now();
    while (clk::now() < deadline) {
        Sleep(2);
        uint64_t cur = env.pad.received();
        if (expected && cur >= expected) break;
        if (cur != last) {
            last = cur;
            stableSince = clk::now();
        } else if (clk::now() - stableSince > clk::msToTicks(expected ? 400 : 150)) {
            break;
        }
    }
    return env.pad.counters().lastQpc.load();
}

EngineConfig baseConfig(Env& env)
{
    EngineConfig c;
    c.action.chord = KeyChord::mouse(MouseButton::Left);
    c.action.downTimeUs = 0;
    c.mode.mode = TriggerMode::Toggle;
    c.cps = 100;
    c.precision = Precision::Standard;
    c.priority = power::ThreadPrio::Highest;
    c.testTargetHwnd = env.pad.hwnd();
    return c;
}

struct RunResult {
    EngineSnapshot s{};
    double wall = 0;
    double procCores = 0;
    double schedCores = 0;
    uint64_t padDowns = 0, padUps = 0, padTotal = 0;
    double padSpanSec = 0;
    bool paused = false;
};

RunResult runEngine(Env& env, const EngineConfig& cfg, RunLimits lim, double expectSec)
{
    RunResult r;
    ensurePadForeground(env);
    env.engine.setConfig(cfg);
    env.pad.reset();
    const uint64_t before = env.engine.runsCompleted();
    const uint64_t cpu0 = processCycles();
    const uint64_t sch0 = threadCycles(env.engine.threadHandle());
    const int64_t t0 = clk::now();
    env.engine.startRun(lim);
    const int64_t timeout = t0 + clk::secToTicks(expectSec + 10);
    bool sawPause = false;
    while (env.engine.runsCompleted() == before) {
        Sleep(5);
        if (env.engine.state() == EngineState::Paused) sawPause = true;
        if (g_abort.load() || clk::now() > timeout) {
            env.engine.stopRun();
            while (env.engine.runsCompleted() == before) Sleep(5);
            break;
        }
    }
    const int64_t t1 = clk::now();
    const uint64_t cpu1 = processCycles();
    const uint64_t sch1 = threadCycles(env.engine.threadHandle());
    waitPadDrain(env, 0, 3000);
    env.engine.snapshot(r.s);
    r.wall = clk::ticksToSec(t1 - t0);
    r.procCores = cyclesToSec(cpu1 - cpu0) / r.wall;
    r.schedCores = cyclesToSec(sch1 - sch0) / r.wall;
    r.padDowns = env.pad.downs(MouseButton::Left);
    r.padUps = env.pad.ups(MouseButton::Left);
    r.padTotal = env.pad.received();
    const auto& pc = env.pad.counters();
    r.padSpanSec = pc.firstQpc.load() ? clk::ticksToSec(pc.lastQpc.load() - pc.firstQpc.load()) : 0;
    r.paused = sawPause;
    return r;
}

std::string osVersion()
{
    using RtlGetVersionFn = LONG(WINAPI*)(PRTL_OSVERSIONINFOW);
    auto fn = reinterpret_cast<RtlGetVersionFn>(GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "RtlGetVersion"));
    RTL_OSVERSIONINFOW v{sizeof v};
    if (fn) fn(&v);
    DWORD ubr = 0, sz = sizeof ubr;
    RegGetValueW(HKEY_LOCAL_MACHINE, L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion", L"UBR", RRF_RT_REG_DWORD,
                 nullptr, &ubr, &sz);
    wchar_t disp[64] = L"";
    DWORD dsz = sizeof disp;
    RegGetValueW(HKEY_LOCAL_MACHINE, L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion", L"DisplayVersion",
                 RRF_RT_REG_SZ, nullptr, disp, &dsz);
    return strFormat("Windows %s %lu.%lu build %lu.%lu (%s)", v.dwBuildNumber >= 22000 ? "11" : "10", v.dwMajorVersion,
                     v.dwMinorVersion, v.dwBuildNumber, ubr, toUtf8(disp).c_str());
}

std::string cpuName()
{
    wchar_t buf[256] = L"";
    DWORD sz = sizeof buf;
    RegGetValueW(HKEY_LOCAL_MACHINE, L"HARDWARE\\DESCRIPTION\\System\\CentralProcessor\\0", L"ProcessorNameString",
                 RRF_RT_REG_SZ, nullptr, buf, &sz);
    return trim(toUtf8(buf));
}

// ============================================================ tests

void testSystem(Env& env)
{
    Report& R = env.rep;
    R.h2("1. Test system");
    uint32_t mn = 0, mx = 0, cur = 0;
    power::queryTimerResolution(mn, mx, cur);
    SYSTEM_POWER_STATUS ps{};
    GetSystemPowerStatus(&ps);
    SYSTEMTIME st;
    GetLocalTime(&st);
    R.table({"Property", "Value"});
    R.row({"Date", strFormat("%04u-%02u-%02u %02u:%02u", st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute)});
    R.row({"OS", osVersion()});
    R.row({"CPU", cpuName()});
    R.row({"Logical processors", strFormat("%d", logicalCpuCount())});
    R.row({"QPC frequency", strFormat("%lld Hz (%.1f ns/tick)", (long long)clk::freq(), 1e9 / double(clk::freq()))});
    R.row({"Timer resolution (NtQueryTimerResolution)",
           strFormat("coarsest %.3f ms, finest %.3f ms, current %.3f ms", mn / 1e4, mx / 1e4, cur / 1e4)});
    R.row({"Power source", ps.ACLineStatus == 1 ? "AC" : ps.ACLineStatus == 0 ? "Battery" : "Unknown"});
    R.row({"Build", strFormat("MSVC %d, %s", _MSC_FULL_VER, sizeof(void*) == 8 ? "x64" : "x86")});
    R.end();
}

// ---------------------------------------------------------------- timers

void testTimers(Env& env)
{
    Report& R = env.rep;
    R.h2("2. Timer / sleep precision (actual wait vs requested)");
    R.para("Each row: N waits of the requested length on a thread at HIGHEST priority. `error` = actual - requested "
           "(microseconds). CPU = thread CPU time / wall time for that row (100% = one core busy).");
    const int n = env.opt.quick ? 60 : 300;
    const std::vector<double> reqUs = {100, 500, 1000, 2000, 5000};
    power::ThreadPriorityScope prio;
    prio.apply(power::ThreadPrio::Highest);
    PreciseWaiter waiter;
    waiter.calibrate();

    struct Method {
        const char* name;
        std::function<void(double)> wait; // wait approximately `us`
        bool msOnly;
    };
    HANDLE legacy = CreateWaitableTimerExW(nullptr, nullptr, 0, TIMER_ALL_ACCESS);
    HANDLE hires = CreateWaitableTimerExW(nullptr, nullptr, CREATE_WAITABLE_TIMER_HIGH_RESOLUTION, TIMER_ALL_ACCESS);
    auto timerWait = [](HANDLE t, double us) {
        LARGE_INTEGER due;
        due.QuadPart = -int64_t(us * 10.0);
        SetWaitableTimerEx(t, &due, 0, nullptr, nullptr, nullptr, 0);
        WaitForSingleObject(t, INFINITE);
    };
    std::vector<Method> methods = {
        {"Sleep() default resolution", [](double us) { Sleep(DWORD(std::max(1.0, std::round(us / 1000.0)))); }, true},
        {"Sleep() + timeBeginPeriod(1)", [](double us) { Sleep(DWORD(std::max(1.0, std::round(us / 1000.0)))); }, true},
        {"std::this_thread::sleep_for",
         [](double us) { std::this_thread::sleep_for(std::chrono::microseconds(int64_t(us))); }, false},
        {"Waitable timer (legacy)", [&](double us) { timerWait(legacy, us); }, false},
        {"Waitable timer HIGH_RESOLUTION", [&](double us) { timerWait(hires, us); }, false},
        {"HIGH_RESOLUTION + timeBeginPeriod(1)", [&](double us) { timerWait(hires, us); }, false},
        {"HIGH_RESOLUTION + NtSetTimerResolution(0.5ms)", [&](double us) { timerWait(hires, us); }, false},
        {"Hybrid STANDARD (timer+spin)",
         [&](double us) { waiter.waitUntil(clk::now() + clk::usToTicks(us), Precision::Standard, nullptr, nullptr); },
         false},
        {"Hybrid ULTRA (timer+spin)",
         [&](double us) { waiter.waitUntil(clk::now() + clk::usToTicks(us), Precision::Ultra, nullptr, nullptr); },
         false},
        {"Pure QPC spin",
         [](double us) {
             const int64_t d = clk::now() + clk::usToTicks(us);
             while (clk::now() < d) _mm_pause();
         },
         false},
    };

    R.para("NtSetTimerResolution is undocumented and is used here only as a research data point - Infinity Clicker itself "
           "only uses documented APIs.");
    R.table({"Method", "Requested", "mean error", "p50 error", "p99 error", "max error", "CPU"});
    for (size_t mi = 0; mi < methods.size() && !g_abort; ++mi) {
        const Method& m = methods[mi];
        if (mi == 1) timeBeginPeriod(1);
        if (mi == 2) timeEndPeriod(1);
        if (mi == 5) timeBeginPeriod(1);
        if (mi == 6) {
            timeEndPeriod(1);
            uint32_t act = 0;
            power::setTimerResolution(5000, true, &act);
        }
        if (mi == 7) power::setTimerResolution(5000, false, nullptr);
        for (double us : reqUs) {
            if (m.msOnly && us < 1000) continue;
            const double effective = m.msOnly ? std::max(1.0, std::round(us / 1000.0)) * 1000.0 : us;
            const int count = (mi <= 1) ? std::max(20, n / 3) : n;
            std::vector<double> errs;
            errs.reserve(size_t(count));
            const uint64_t c0 = threadCycles(GetCurrentThread());
            const int64_t w0 = clk::now();
            for (int i = 0; i < count && !g_abort; ++i) {
                const int64_t t0 = clk::now();
                m.wait(effective);
                errs.push_back(clk::ticksToUs(clk::now() - t0) - effective);
            }
            const double wall = clk::ticksToSec(clk::now() - w0);
            const double cpu = cyclesToSec(threadCycles(GetCurrentThread()) - c0) / wall * 100.0;
            Dist d = dist(errs);
            R.row({m.name, strFormat("%.0f us", effective), f1(d.mean), f1(d.p50), f1(d.p99), f1(d.max),
                   strFormat("%.0f%%", cpu)});
        }
    }
    R.end();
    CloseHandle(legacy);
    CloseHandle(hires);
}

// ---------------------------------------------------------------- spin threshold sweep

void testMargins(Env& env)
{
    Report& R = env.rep;
    R.h2("3. Spin-threshold sweep (choosing the hybrid wake-up margin)");
    R.para("Absolute timeline at a fixed rate, no input sent - pure scheduling. `margin` = how early the high-resolution "
           "timer wakes us before the deadline; the rest is QPC spin. Lateness = fire time - deadline.");
    PreciseWaiter waiter;
    waiter.calibrate();
    power::ThreadPriorityScope prio;
    prio.apply(power::ThreadPrio::Highest);
    power::setThreadHighQos(true);
    const double seconds = env.opt.quick ? 0.6 : 1.5;
    R.table({"Rate", "Margin", "late p50 us", "late p99 us", "late max us", ">100us late", "thread CPU"});
    for (double rate : {1000.0, 250.0}) {
        const double period = double(clk::freq()) / rate;
        const int count = int(rate * seconds);
        waiter.setPeriodHint(int64_t(period));
        struct Cfg {
            std::string name;
            double margin;
            Precision p;
        };
        std::vector<Cfg> cfgs = {{"0 (ECO, timer only)", -1, Precision::Eco}};
        for (double m : {50.0, 100.0, 200.0, 300.0, 500.0, 750.0, 1000.0, 1500.0})
            cfgs.push_back({strFormat("%.0f us", m), m, Precision::Standard});
        cfgs.push_back({"adaptive STANDARD", -1, Precision::Standard});
        cfgs.push_back({"adaptive ULTRA", -1, Precision::Ultra});
        for (const Cfg& c : cfgs) {
            if (g_abort) return;
            waiter.setFixedMarginUs(c.margin);
            std::vector<double> late;
            late.reserve(size_t(count));
            const uint64_t c0 = threadCycles(GetCurrentThread());
            const int64_t start = clk::now() + clk::msToTicks(2);
            for (int k = 0; k < count; ++k) {
                const int64_t dl = start + int64_t(double(k) * period + 0.5);
                waiter.waitUntil(dl, c.p, nullptr, nullptr);
                late.push_back(clk::ticksToUs(clk::now() - dl));
            }
            const double wall = clk::ticksToSec(clk::now() - start);
            const double cpu = cyclesToSec(threadCycles(GetCurrentThread()) - c0) / wall * 100.0;
            Dist d = dist(late);
            size_t over = size_t(std::count_if(late.begin(), late.end(), [](double v) { return v > 100.0; }));
            R.row({strFormat("%.0f/s", rate), c.name, f1(d.p50), f1(d.p99), f1(d.max),
                   strFormat("%.1f%%", 100.0 * double(over) / double(late.size())), strFormat("%.0f%%", cpu)});
        }
        waiter.setFixedMarginUs(-1);
    }
    R.end();
    power::setThreadHighQos(false);
}

// ---------------------------------------------------------------- SendInput microbench

void testSendInput(Env& env)
{
    Report& R = env.rep;
    R.h2("4. SendInput throughput: call pattern x hook state");
    R.para("Left clicks (DOWN+UP) injected straight from the benchmark thread into the full-screen test pad. "
           "`API` = rate at which SendInput returned; `delivered` = rate at which the pad's window procedure "
           "received the messages (first..last receive); `drain` = time from the last SendInput return until the pad "
           "got the last message. All events accepted means SendInput's return value == number requested.");
    const uint64_t clicks = env.opt.quick ? 3000 : 12000;
    struct Pattern {
        const char* name;
        uint32_t clicksPerCall; // 0 = split DOWN and UP into separate calls
    };
    const std::vector<Pattern> patterns = {{"SendInput(1) x2 per click", 0},
                                           {"SendInput(2) per click", 1},
                                           {"SendInput batch 8 clicks", 8},
                                           {"SendInput batch 64 clicks", 64}};
    struct HookState {
        const char* name;
        bool hook;
        bool raw;
    };
    const std::vector<HookState> states = {
        {"no mouse hook", false, false}, {"WH_MOUSE_LL installed", true, false}, {"Raw Input sink", false, true}};

    R.table({"Hook state", "Pattern", "API calls/s", "API events/s", "us/call", "accepted", "received", "delivered clicks/s",
             "drain ms", "hook saw"});
    for (const HookState& hs : states) {
        setObserveMouse(env, hs.hook, hs.raw ? TriggerBackend::RawInput : TriggerBackend::Hook, hs.raw);
        for (const Pattern& p : patterns) {
            if (g_abort) return;
            if (!ensurePadForeground(env)) {
                R.row({hs.name, p.name, "pad lost foreground - skipped"});
                continue;
            }
            env.pad.reset();
            const uint64_t obs0 = env.trigger.observedOursMouse.load();
            std::vector<INPUT> buf;
            const uint32_t per = p.clicksPerCall ? p.clicksPerCall : 1;
            for (uint32_t i = 0; i < per; ++i) {
                buf.push_back(makeMouseInput(MouseButton::Left, true, 0, kTagInfClick));
                buf.push_back(makeMouseInput(MouseButton::Left, false, 0, kTagInfClick));
            }
            uint64_t calls = 0, accepted = 0, requested = 0;
            const int64_t t0 = clk::now();
            for (uint64_t c = 0; c < clicks;) {
                if (p.clicksPerCall == 0) {
                    accepted += SendInput(1, &buf[0], sizeof(INPUT));
                    accepted += SendInput(1, &buf[1], sizeof(INPUT));
                    calls += 2;
                    requested += 2;
                    c += 1;
                } else {
                    const uint32_t k = uint32_t(std::min<uint64_t>(per, clicks - c));
                    accepted += SendInput(k * 2, buf.data(), sizeof(INPUT));
                    calls += 1;
                    requested += k * 2;
                    c += k;
                }
            }
            const int64_t t1 = clk::now();
            const int64_t last = waitPadDrain(env, requested, 6000);
            const double apiSec = clk::ticksToSec(t1 - t0);
            const uint64_t got = env.pad.received();
            const double span = clk::ticksToSec(last - t0);
            const double drainMs = last > t1 ? clk::ticksToMs(last - t1) : 0.0;
            const uint64_t saw = env.trigger.observedOursMouse.load() - obs0;
            R.row({hs.name, p.name, f0(double(calls) / apiSec), f0(double(requested) / apiSec),
                   f2(apiSec * 1e6 / double(calls)), accepted == requested ? "all" : u64(accepted) + "/" + u64(requested),
                   u64(got) + "/" + u64(requested), span > 0 ? f0(double(got) / 2.0 / span) : "-", f1(drainMs),
                   hs.hook || hs.raw ? u64(saw) : "-"});
            Sleep(100);
        }
    }
    setObserveMouse(env, false);
    R.end();
}

// ---------------------------------------------------------------- CPS matrix

void testCpsMatrix(Env& env)
{
    Report& R = env.rep;
    R.h2("5. CPS matrix (full engine path: timeline -> waiter -> SendInput -> test pad)");
    R.para("Left click, down time 0 (DOWN+UP in one SendInput call). `actual` = actions generated / run time; "
           "`received` = WM_LBUTTONDOWN messages the pad processed / same run time; `err` = |mean interval - target| / "
           "target; `jitter` = standard deviation of the interval between consecutive actions; `late` = fire time - "
           "scheduled deadline. CPU in cores (1.00 = one logical CPU fully busy) for the whole process.");
    std::vector<double> rates = {1, 5, 10, 20, 50, 100, 250, 500, 1000, 2000, 5000, 10000, 20000, 50000};
    struct Row {
        double rate;
        Precision p;
        bool max;
        uint32_t batch;
    };
    std::vector<Row> rows;
    for (double r : rates) rows.push_back({r, Precision::Standard, false, 0});
    for (double r : {20.0, 100.0, 1000.0, 5000.0}) {
        rows.push_back({r, Precision::Eco, false, 0});
        rows.push_back({r, Precision::Ultra, false, 0});
    }
    rows.push_back({0, Precision::Standard, true, 1});
    rows.push_back({0, Precision::Standard, true, 8});
    rows.push_back({0, Precision::Standard, true, 64});

    R.table({"Target", "Precision", "Run s", "Actual CPS", "Received CPS", "err", "jitter us", "late p50 us", "late p99 us",
             "late max us", "missed", "failed calls", "SendInput p50/p99 us", "CPU cores", "sched cores"});
    std::string csv = "target,precision,run_s,actual_cps,received_cps,err_pct,jitter_us,late_p50_us,late_p99_us,"
                      "late_max_us,missed,failed_calls,cpu_cores,sched_cores\n";
    for (const Row& row : rows) {
        if (g_abort) break;
        EngineConfig c = baseConfig(env);
        c.cps = row.max ? 1000 : row.rate;
        c.maxMode = row.max;
        c.maxBatchActions = row.batch ? row.batch : 8;
        c.maxInflight = 0;
        c.precision = row.p;
        double sec = row.rate > 0 && row.rate <= 1 ? (env.opt.quick ? 4 : 10)
                     : row.rate > 0 && row.rate <= 5 ? (env.opt.quick ? 3 : 6)
                     : row.rate > 0 && row.rate <= 50 ? (env.opt.quick ? 2 : 4)
                                                      : (env.opt.quick ? 1.5 : 3);
        RunLimits lim;
        lim.durationMs = sec * 1000.0;
        RunResult rr = runEngine(env, c, lim, sec);
        const EngineSnapshot& s = rr.s;
        // Same definition as the engine's actual CPS: (N-1) intervals between first and last receipt.
        const double received = rr.padDowns >= 2 && rr.padSpanSec > 0 ? double(rr.padDowns - 1) / rr.padSpanSec : 0;
        std::string target = row.max ? strFormat("MAX (batch %u)", row.batch) : strFormat("%.0f", row.rate);
        R.row({target, row.max ? "-" : precisionName(row.p), f2(s.runElapsedSec), f2(s.runAvgCps), f2(received),
               row.max ? "-" : pct(s.avgErrorPct), f1(s.runInterval.stddevUs), row.max ? "-" : f1(s.runLate.p50Us),
               row.max ? "-" : f1(s.runLate.p99Us), row.max ? "-" : f1(s.runLate.maxUs), u64(s.runMissed),
               u64(s.runFailedCalls), strFormat("%.0f / %.0f", s.sendP50Us, s.sendP99Us), f2(rr.procCores),
               f2(rr.schedCores)});
        csv += strFormat("%s,%s,%.3f,%.3f,%.3f,%.4f,%.2f,%.2f,%.2f,%.2f,%llu,%llu,%.3f,%.3f\n", target.c_str(),
                         row.max ? "-" : precisionName(row.p), s.runElapsedSec, s.runAvgCps, received, s.avgErrorPct,
                         s.runInterval.stddevUs, s.runLate.p50Us, s.runLate.p99Us, s.runLate.maxUs,
                         (unsigned long long)s.runMissed, (unsigned long long)s.runFailedCalls, rr.procCores,
                         rr.schedCores);
        if (rr.paused) R.line("> note: run was paused at least once (test pad lost foreground)");
        Sleep(150);
    }
    R.end();
    paths::writeFileAtomic(env.opt.outDir + L"\\cps_matrix.csv", csv);

    // Same with the WH_MOUSE_LL hook installed (mouse-button trigger configurations).
    R.para("Same engine path, but with Infinity Clicker's own WH_MOUSE_LL hook installed (what happens when the trigger is a "
           "mouse button) and MAX mode with hook-based backpressure:");
    setObserveMouse(env, true);
    R.table({"Target", "Hook", "Actual CPS", "Received CPS", "jitter us", "late p99 us", "hook saw events", "CPU cores"});
    struct HRow {
        double rate;
        bool max;
        uint32_t inflight;
    };
    for (const HRow& h : {HRow{1000, false, 0}, HRow{5000, false, 0}, HRow{20000, false, 0}, HRow{0, true, 0},
                          HRow{0, true, 2000}}) {
        if (g_abort) break;
        EngineConfig c = baseConfig(env);
        c.cps = h.max ? 1000 : h.rate;
        c.maxMode = h.max;
        c.maxBatchActions = 8;
        c.maxInflight = h.inflight;
        env.engine.setObservedCounter(&env.trigger.observedOurs);
        const uint64_t obs0 = env.trigger.observedOursMouse.load();
        RunLimits lim;
        lim.durationMs = env.opt.quick ? 1500 : 3000;
        RunResult rr = runEngine(env, c, lim, lim.durationMs / 1000.0);
        const double received = rr.s.runElapsedSec > 0 ? double(rr.padDowns) / rr.s.runElapsedSec : 0;
        R.row({h.max ? (h.inflight ? strFormat("MAX, backpressure %u", h.inflight) : std::string("MAX, no backpressure"))
                     : f0(h.rate),
               "WH_MOUSE_LL", f2(rr.s.runAvgCps), f2(received), f1(rr.s.runInterval.stddevUs), f1(rr.s.runLate.p99Us),
               u64(env.trigger.observedOursMouse.load() - obs0), f2(rr.procCores)});
        Sleep(150);
    }
    setObserveMouse(env, false);
    R.end();
}

// ---------------------------------------------------------------- thread priority

void testPriority(Env& env)
{
    Report& R = env.rep;
    R.h2("6. Scheduler thread priority (1000 CPS, STANDARD), idle vs. fully loaded CPU");
    R.para("`loaded` = one busy-looping NORMAL-priority thread per logical CPU for the duration of the run.");
    R.table({"Condition", "Priority", "Actual CPS", "late p50 us", "late p99 us", "late max us", ">100us late", "missed"});
    const std::vector<power::ThreadPrio> prios = {power::ThreadPrio::Normal, power::ThreadPrio::AboveNormal,
                                                  power::ThreadPrio::Highest, power::ThreadPrio::TimeCritical,
                                                  power::ThreadPrio::Mmcss};
    for (int loaded = 0; loaded < 2; ++loaded) {
        std::atomic<bool> stop{false};
        std::vector<std::thread> burners;
        if (loaded) {
            for (int i = 0; i < logicalCpuCount(); ++i)
                burners.emplace_back([&stop] {
                    volatile double x = 1.0;
                    while (!stop.load(std::memory_order_relaxed)) x = x * 1.0000001 + 0.5;
                });
        }
        for (auto p : prios) {
            if (g_abort) break;
            EngineConfig c = baseConfig(env);
            c.cps = 1000;
            c.priority = p;
            RunLimits lim;
            lim.durationMs = env.opt.quick ? 1500 : 3000;
            RunResult rr = runEngine(env, c, lim, lim.durationMs / 1000.0);
            const auto& s = rr.s;
            R.row({loaded ? "loaded" : "idle", power::prioName(p), f2(s.runAvgCps), f1(s.runLate.p50Us), f1(s.runLate.p99Us),
                   f1(s.runLate.maxUs),
                   strFormat("%.2f%%", s.runLate.count ? 100.0 * double(s.runLate.over100us) / double(s.runLate.count) : 0),
                   u64(s.runMissed)});
        }
        stop = true;
        for (auto& t : burners) t.join();
    }
    // restore default
    EngineConfig c = baseConfig(env);
    env.engine.setConfig(c);
    R.end();
}

// ---------------------------------------------------------------- down time / polling visibility

void testDownTime(Env& env)
{
    Report& R = env.rep;
    R.h2("7. DOWN->UP hold time: who still sees the click?");
    R.para("37 clicks/s (not a divisor of the polling rates, so click phases are spread evenly), 120 clicks per row. "
           "`messages` = WM_LBUTTONDOWN received by an event-driven window (how GLFW/LWJGL, browsers and normal desktop "
           "apps read the mouse). The two pollers read GetAsyncKeyState(VK_LBUTTON) at a fixed rate and count rising "
           "edges - this is how a game that samples button *state* once per frame sees clicks; `expected` = "
           "min(1, hold / poll period) for an ideal poller. `app hold` = time between the app receiving DOWN and UP.");
    const std::vector<double> holdsUs = {0, 50, 100, 250, 500, 1000, 2000, 5000, 10000, 16700, 20000};
    R.table({"Hold", "messages", "poll 1000 Hz sees", "expected", "poll 60 Hz sees", "expected", "app-observed hold (mean)"});
    for (double h : holdsUs) {
        if (g_abort) break;
        std::atomic<bool> stop{false};
        std::atomic<uint64_t> edges1000{0}, edges60{0};
        auto poller = [&](double hz, std::atomic<uint64_t>& edges) {
            PreciseWaiter w;
            power::ThreadPriorityScope pr;
            pr.apply(power::ThreadPrio::Highest);
            const double period = double(clk::freq()) / hz;
            const int64_t t0 = clk::now();
            bool prev = false;
            for (uint64_t k = 1; !stop.load(); ++k) {
                w.waitUntil(t0 + int64_t(double(k) * period), Precision::Ultra, nullptr, &stop);
                const bool dn = (GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0;
                if (dn && !prev) edges.fetch_add(1);
                prev = dn;
            }
        };
        std::thread p1(poller, 1000.0, std::ref(edges1000));
        std::thread p2(poller, 60.0, std::ref(edges60));
        Sleep(50);
        EngineConfig c = baseConfig(env);
        c.cps = 37;
        c.action.downTimeUs = uint32_t(h);
        c.precision = Precision::Ultra;
        RunLimits lim;
        lim.maxActions = env.opt.quick ? 60 : 120;
        RunResult rr = runEngine(env, c, lim, double(lim.maxActions) / 37.0);
        Sleep(60);
        stop = true;
        p1.join();
        p2.join();
        const uint64_t n = lim.maxActions;
        const auto& pc = env.pad.counters();
        const double appHold = pc.holdCount.load() ? clk::ticksToUs(pc.holdSumTicks.load()) / double(pc.holdCount.load()) : 0;
        R.row({h >= 1000 ? strFormat("%.1f ms", h / 1000.0) : strFormat("%.0f us", h),
               strFormat("%llu/%llu", (unsigned long long)rr.padDowns, (unsigned long long)n),
               strFormat("%llu/%llu (%.0f%%)", (unsigned long long)edges1000.load(), (unsigned long long)n,
                         100.0 * double(edges1000.load()) / double(n)),
               strFormat("%.0f%%", std::min(100.0, h / 1000.0 * 100.0)),
               strFormat("%llu/%llu (%.0f%%)", (unsigned long long)edges60.load(), (unsigned long long)n,
                         100.0 * double(edges60.load()) / double(n)),
               strFormat("%.0f%%", std::min(100.0, h / 16667.0 * 100.0)), strFormat("%.1f us", appHold)});
    }
    R.end();
}

// ---------------------------------------------------------------- trigger latency

void testLatency(Env& env)
{
    Report& R = env.rep;
    R.h2("8. Trigger latency (trigger press -> first synthetic click)");
    R.para("A physical key press is simulated by injecting F13 with the benchmark tag (the trigger engine is told to "
           "treat that tag as physical). `inject->hook` = SendInput(F13) until our WH_KEYBOARD_LL callback ran; "
           "`hook->SendInput` = callback until the scheduler thread issued the first click; `end-to-end` = SendInput(F13) "
           "until the test pad *received* the click. Real hardware adds USB polling + driver time before the hook.");
    const int trials = env.opt.quick ? 25 : 100;
    env.tcfg.trigger = KeyChord::key(VK_F13);
    env.tcfg.acceptBenchTag = true;
    env.tcfg.observeMouse = false;
    env.trigger.setConfig(env.tcfg);
    Sleep(50);
    bool f13ext = false;
    const uint16_t f13scan = vkToScan(VK_F13, &f13ext);
    auto key = [&](bool up) {
        INPUT in = makeKeyInput(VK_F13, f13scan, f13ext, up, KeyInjectMode::ScanCode, kTagBench);
        SendInput(1, &in, sizeof in);
    };
    std::mt19937 rng(1234);
    R.table({"Mode", "Priority", "Metric", "mean us", "p50 us", "p99 us", "max us", "n"});
    for (TriggerMode mode : {TriggerMode::Hold, TriggerMode::Toggle}) {
        for (auto prio : {power::ThreadPrio::Highest, power::ThreadPrio::Normal}) {
            EngineConfig c = baseConfig(env);
            c.mode.mode = mode;
            c.cps = 100;
            c.priority = prio;
            env.engine.setConfig(c);
            env.engine.arm(true);
            Sleep(30);
            std::vector<double> injHook, hookSend, e2e, stopLat;
            for (int i = 0; i < trials && !g_abort; ++i) {
                if (!ensurePadForeground(env)) break;
                env.pad.reset();
                env.sink.lastDown = 0;
                const uint64_t runs0 = env.engine.runsCompleted();
                Sleep(15 + int(rng() % 15));
                const int64_t t0 = clk::now();
                key(false);
                const int64_t limit = t0 + clk::msToTicks(500);
                while (env.pad.counters().firstQpc.load() == 0 && clk::now() < limit) _mm_pause();
                const int64_t t3 = env.pad.counters().firstQpc.load();
                const int64_t t1 = env.sink.lastDown.load();
                EngineSnapshot s;
                env.engine.snapshot(s);
                if (t3 && t1) {
                    injHook.push_back(clk::ticksToUs(t1 - t0));
                    e2e.push_back(clk::ticksToUs(t3 - t0));
                    if (s.triggerSamples) hookSend.push_back(s.lastTriggerLatUs);
                }
                if (mode == TriggerMode::Hold) {
                    Sleep(20);
                    const int64_t u0 = clk::now();
                    key(true);
                    while (env.engine.runsCompleted() == runs0 && clk::now() < u0 + clk::msToTicks(500)) _mm_pause();
                    stopLat.push_back(clk::ticksToUs(clk::now() - u0));
                } else {
                    key(true);
                    Sleep(20);
                    const int64_t u0 = clk::now();
                    key(false);
                    while (env.engine.runsCompleted() == runs0 && clk::now() < u0 + clk::msToTicks(500)) _mm_pause();
                    stopLat.push_back(clk::ticksToUs(clk::now() - u0));
                    key(true);
                }
                waitPadDrain(env, 0, 300);
            }
            auto emit = [&](const char* metric, const std::vector<double>& v) {
                Dist d = dist(v);
                R.row({triggerModeName(mode), power::prioName(prio), metric, f1(d.mean), f1(d.p50), f1(d.p99), f1(d.max),
                       strFormat("%zu", d.n)});
            };
            emit("inject -> hook", injHook);
            emit("hook -> SendInput", hookSend);
            emit("end-to-end (inject -> pad received)", e2e);
            emit(mode == TriggerMode::Hold ? "release -> engine stopped" : "2nd press -> engine stopped", stopLat);
            env.engine.arm(false);
        }
    }
    env.tcfg.acceptBenchTag = false;
    env.trigger.setConfig(env.tcfg);
    R.end();
}

// ---------------------------------------------------------------- stress

void testStress(Env& env)
{
    Report& R = env.rep;
    const int secs = env.opt.quick ? 15 : env.opt.stressSeconds;
    R.h2(strFormat("9. Stress test (%d s per speed)", secs));
    R.para("Continuous run; every 10 s we sample generated actions vs. the ideal count (rate x elapsed) and the "
           "process resources. `drift` = actions - ideal (an absolute timeline must stay within +-1).");
    struct S {
        double rate;
        bool max;
        Precision p;
    };
    for (const S& st : {S{100, false, Precision::Standard}, S{1000, false, Precision::Standard},
                        S{5000, false, Precision::Ultra}, S{0, true, Precision::Standard}}) {
        if (g_abort) break;
        const int runSecs = st.max ? std::min(secs, 30) : secs;
        R.line(strFormat("\n**%s, %s, %d s**\n", st.max ? "MAX (batch 8, backpressure 2000)" : strFormat("%.0f CPS", st.rate).c_str(),
                         st.max ? "-" : precisionName(st.p), runSecs));
        if (st.max) {
            setObserveMouse(env, true);
            env.engine.setObservedCounter(&env.trigger.observedOurs);
        }
        EngineConfig c = baseConfig(env);
        c.cps = st.max ? 1000 : st.rate;
        c.maxMode = st.max;
        c.maxInflight = st.max ? 2000 : 0;
        c.precision = st.p;
        ensurePadForeground(env);
        env.engine.setConfig(c);
        env.pad.reset();
        const ProcessResources r0 = queryProcessResources();
        const uint64_t before = env.engine.runsCompleted();
        RunLimits lim;
        lim.durationMs = runSecs * 1000.0;
        const uint64_t cpu0 = processCycles();
        env.engine.startRun(lim);
        R.table({"t s", "actions", "ideal", "drift", "cps (last 1 s)", "pad received", "time in SendInput",
                 "worst SendInput (last s)", "private MB", "handles", "threads", "GDI/USER"});
        const int64_t t0 = clk::now();
        int nextSample = 10;
        while (env.engine.runsCompleted() == before && !g_abort) {
            Sleep(50);
            const double el = clk::ticksToSec(clk::now() - t0);
            if (el >= nextSample && nextSample <= runSecs) {
                EngineSnapshot s;
                env.engine.snapshot(s);
                const ProcessResources r = queryProcessResources();
                const double ideal = st.max ? 0 : st.rate * s.runElapsedSec;
                R.row({f0(s.runElapsedSec), u64(s.runActions), st.max ? "-" : f0(ideal),
                       st.max ? "-" : strFormat("%+.1f", double(s.runActions) - ideal), f1(s.cps1s),
                       u64(env.pad.downs(MouseButton::Left)), strFormat("%.0f%%", s.sendShare * 100),
                       strFormat("%.2f ms", s.sendMaxLastSecUs / 1000), f2(double(r.privateBytes) / 1048576.0),
                       strFormat("%u", r.handles),
                       strFormat("%u", r.threads), strFormat("%u/%u", r.gdiObjects, r.userObjects)});
                nextSample += 10;
            }
        }
        if (g_abort) env.engine.stopRun();
        while (env.engine.runsCompleted() == before) Sleep(5);
        const double wall = clk::ticksToSec(clk::now() - t0);
        const double cores = cyclesToSec(processCycles() - cpu0) / wall;
        waitPadDrain(env, 0, 4000);
        EngineSnapshot s;
        env.engine.snapshot(s);
        const ProcessResources r1 = queryProcessResources();
        const bool lDown = (GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0;
        R.end();
        R.line(strFormat("- final: %llu actions in %.3f s = %.2f CPS; pad received %llu downs / %llu ups; missed %llu; "
                         "failed calls %llu; CPU %.2f cores; SendInput call p50 %.0f us / p99 %.0f us / max %.1f ms",
                         (unsigned long long)s.runActions, s.runElapsedSec, s.runAvgCps,
                         (unsigned long long)env.pad.downs(MouseButton::Left),
                         (unsigned long long)env.pad.ups(MouseButton::Left), (unsigned long long)s.runMissed,
                         (unsigned long long)s.runFailedCalls, cores, s.sendP50Us, s.sendP99Us, s.sendMaxUs / 1000));
        if (!st.max)
            R.line(strFormat("- timeline drift at end: %+.1f actions (ideal %.1f)",
                             double(s.runActions) - st.rate * s.runElapsedSec, st.rate * s.runElapsedSec));
        R.line(strFormat("- resources: private %+.2f MB, handles %+d, threads %+d, GDI %+d, USER %+d",
                         (double(r1.privateBytes) - double(r0.privateBytes)) / 1048576.0, int(r1.handles) - int(r0.handles),
                         int(r1.threads) - int(r0.threads), int(r1.gdiObjects) - int(r0.gdiObjects),
                         int(r1.userObjects) - int(r0.userObjects)));
        R.line(strFormat("- stuck-button check after stop: VK_LBUTTON %s, engine holds %s", lDown ? "**DOWN (FAIL)**" : "up (ok)",
                         env.engine.sender().anyHeld() ? "**something (FAIL)**" : "nothing (ok)"));
        R.line("");
        if (st.max) setObserveMouse(env, false);
        Sleep(300);
    }
}

bool want(const Options& o, const char* id)
{
    if (o.only.empty()) return true;
    return std::find(o.only.begin(), o.only.end(), id) != o.only.end();
}

} // namespace

// ============================================================ entry

Options parseArgs(const std::vector<std::wstring>& args)
{
    Options o;
    for (const auto& a : args) {
        std::string s = toLowerAscii(toUtf8(a));
        if (s == "quick") o.quick = true;
        else if (s == "nostress") o.stress = false;
        else if (s.rfind("only=", 0) == 0) {
            std::string list = s.substr(5);
            size_t p = 0;
            while (p <= list.size()) {
                size_t e = list.find(',', p);
                if (e == std::string::npos) e = list.size();
                if (e > p) o.only.push_back(list.substr(p, e - p));
                p = e + 1;
            }
        } else if (s.rfind("stress=", 0) == 0) {
            o.stressSeconds = std::clamp(atoi(s.c_str() + 7), 5, 3600);
        } else if (s.rfind("out=", 0) == 0) {
            o.outDir = a.substr(4);
        }
    }
    return o;
}

int run(const Options& optIn)
{
    Options opt = optIn;
    if (opt.outDir.empty()) opt.outDir = paths::exeDir() + L"\\benchmarks";
    paths::ensureDir(opt.outDir);
    consoleInit();
    log::init(paths::dataDir() + L"\\logs");
    log::setFileEnabled(true);

    auto env = std::make_unique<Env>();
    env->opt = opt;
    HeldShared* held = guardian::createShared();
    env->engine.sender().attachShared(held);
    guardian::installCrashHandler(held, paths::dataDir() + L"\\crash");

    consolePrint("Infinity Clicker benchmark - opening full-screen test pad. Ctrl+Shift+F12 aborts.\n");
    if (!env->pad.open(TestPad::Mode::Fullscreen)) {
        consolePrint("ERROR: could not create the test pad window\n");
        return 2;
    }
    env->pad.setStatusText(toWide(tr("BENCHMARK RUNNING - please don't touch mouse/keyboard.   Ctrl+Shift+F12 = abort")));
    env->sink.engine = &env->engine;
    env->tcfg.emergency = KeyChord::key(VK_F12, ModCtrl | ModShift);
    env->tcfg.trigger = KeyChord::key(VK_F13);
    env->tcfg.testPad = env->pad.hwnd();
    env->tcfg.pauseOnOwnWindow = false;
    env->trigger.setConfig(env->tcfg);
    if (!env->trigger.start(&env->sink)) {
        consolePrint("ERROR: trigger engine failed to start\n");
        return 2;
    }
    env->engine.start();
    env->engine.setConfig(baseConfig(*env));
    Sleep(300); // scheduler calibration
    if (!env->pad.activate()) consolePrint("warning: pad is not foreground; runs will pause until it is\n");

    const int64_t t0 = clk::now();
    Report& R = env->rep;
    R.h1("Infinity Clicker benchmark results");
    R.para(strFormat("Mode: %s. Produced by `InfinityClicker.exe --bench` - raw output, no manual edits.",
                     opt.quick ? "quick" : "full"));
    if (want(opt, "sys")) testSystem(*env);
    if (want(opt, "timer") && !g_abort) testTimers(*env);
    if (want(opt, "margin") && !g_abort) testMargins(*env);
    if (want(opt, "sendinput") && !g_abort) testSendInput(*env);
    if (want(opt, "cps") && !g_abort) testCpsMatrix(*env);
    if (want(opt, "prio") && !g_abort) testPriority(*env);
    if (want(opt, "downtime") && !g_abort) testDownTime(*env);
    if (want(opt, "latency") && !g_abort) testLatency(*env);
    if (opt.stress && want(opt, "stress") && !g_abort) testStress(*env);
    if (g_abort) R.para("**ABORTED by emergency stop.**");
    R.para(strFormat("Total benchmark time: %.1f s", clk::ticksToSec(clk::now() - t0)));

    env->engine.shutdown();
    env->trigger.shutdown();
    env->pad.close();
    guardian::markCleanExit();

    SYSTEMTIME st;
    GetLocalTime(&st);
    std::wstring file = opt.outDir + L"\\" +
                        toWide(strFormat("bench_%04u%02u%02u_%02u%02u%02u%s.md", st.wYear, st.wMonth, st.wDay, st.wHour,
                                         st.wMinute, st.wSecond, opt.quick ? "_quick" : ""));
    paths::writeFileAtomic(file, R.text());
    paths::writeFileAtomic(opt.outDir + L"\\latest.md", R.text());
    consolePrint("\nResults written to %s\n", toUtf8(file).c_str());
    log::shutdown();
    return g_abort ? 3 : 0;
}

} // namespace infclick::bench
