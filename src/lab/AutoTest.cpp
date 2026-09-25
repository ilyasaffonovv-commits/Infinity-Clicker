// Functional acceptance tests:  InfinityClicker.exe --autotest
//
// Runs the REAL application (App + ImGui UI on the main thread) against an
// isolated temporary data folder. A test thread simulates physical input by
// injecting events tagged kTagBench (the trigger engine is told to treat that
// tag as physical) and checks what a full-screen test pad actually received.
#include "lab/AutoTest.h"

#include "app/App.h"
#include "core/Clock.h"
#include "core/I18n.h"
#include "core/Paths.h"
#include "core/Str.h"
#include "lab/Bench.h"
#include "telemetry/CpuMeter.h"
#include "ui/Ui.h"

#include <immintrin.h>
#include <shellapi.h>

#include <algorithm>
#include <atomic>
#include <thread>
#include <vector>

namespace infclick::autotest {

namespace {

struct Ctx {
    App* app = nullptr;
    HWND ui = nullptr;
    std::string md;
    int pass = 0, fail = 0;
};

void out(Ctx& c, const std::string& s)
{
    c.md += s;
    bench::consolePrint("%s", s.c_str());
}

void check(Ctx& c, const char* name, bool ok, const std::string& detail)
{
    (ok ? c.pass : c.fail)++;
    out(c, strFormat("| %s | %s | %s |\n", ok ? "PASS" : "**FAIL**", name, detail.c_str()));
}

void key(uint16_t vk, bool up)
{
    bool ext = false;
    uint16_t sc = vkToScan(vk, &ext);
    INPUT in = makeKeyInput(vk, sc, ext, up, KeyInjectMode::ScanCode, kTagBench);
    SendInput(1, &in, sizeof in);
}

void tap(uint16_t vk)
{
    key(vk, false);
    Sleep(15);
    key(vk, true);
}

void mouse(MouseButton b, bool down)
{
    INPUT in = makeMouseInput(b, down, 120, kTagBench);
    SendInput(1, &in, sizeof in);
}

void configure(Ctx& c, const std::function<void(Profile&)>& fn)
{
    c.app->invoke([&] {
        fn(c.app->profile());
        c.app->profileChanged();
    });
    Sleep(120); // configs travel to the engine / trigger threads
}

Profile baseProfile()
{
    Profile p;
    p.name = "AutoTest";
    p.trigger = KeyChord::key(VK_F13);
    p.mode.mode = TriggerMode::Toggle;
    p.cps = 100;
    p.action.chord = KeyChord::mouse(MouseButton::Left);
    p.action.downTimeUs = 0;
    p.precision = Precision::Standard;
    return p;
}

bool waitIdle(App& app, int ms)
{
    const int64_t end = clk::now() + clk::msToTicks(ms);
    while (clk::now() < end) {
        if (!app.engine().running()) return true;
        Sleep(2);
    }
    return !app.engine().running();
}

void drain(App& app)
{
    uint64_t last = app.pad().received();
    for (int i = 0; i < 100; ++i) {
        Sleep(20);
        uint64_t cur = app.pad().received();
        if (cur == last) return;
        last = cur;
    }
}

void resetPad(Ctx& c)
{
    drain(*c.app);
    c.app->pad().reset();
    c.app->pad().activate();
}

bool keysUp(std::initializer_list<int> vks)
{
    for (int vk : vks)
        if (GetAsyncKeyState(vk) & 0x8000) return false;
    return true;
}

void runTests(Ctx& c)
{
    App& app = *c.app;
    TestPad& pad = app.pad();
    out(c, "# Infinity Clicker functional acceptance tests\n\n");
    out(c, "Produced by `InfinityClicker.exe --autotest` (real app + UI, isolated data folder, simulated physical input).\n\n");
    out(c, "| Result | Test | Details |\n|---|---|---|\n");

    const ProcessResources r0 = queryProcessResources();
    app.invoke([&] {
        pad.open(TestPad::Mode::Fullscreen);
        pad.setStatusText(toWide(tr("INFINITY CLICKER AUTOTEST RUNNING - please don't touch mouse/keyboard.  Ctrl+Shift+F12 = abort")));
        app.store().profiles().push_back(baseProfile());
        app.selectProfile("AutoTest");
        app.setAcceptBenchTag(true);
        app.setTestTarget(pad.hwnd());
        app.arm(true);
    });
    Sleep(300);
    pad.activate();

    // 1. HOLD with a keyboard trigger
    {
        configure(c, [](Profile& p) {
            p = baseProfile();
            p.mode.mode = TriggerMode::Hold;
            p.cps = 100;
        });
        resetPad(c);
        key(VK_F13, false);
        Sleep(500);
        const int64_t rel = clk::now();
        key(VK_F13, true);
        waitIdle(app, 500);
        const double stopMs = clk::ticksToMs(clk::now() - rel);
        drain(app);
        const uint64_t d = pad.downs(MouseButton::Left), u = pad.ups(MouseButton::Left);
        check(c, "HOLD: keyboard trigger (F13) held 500 ms at 100 CPS", d >= 45 && d <= 56 && d == u,
              strFormat("%llu downs / %llu ups received, stopped %.1f ms after release", (unsigned long long)d,
                        (unsigned long long)u, stopMs));
        check(c, "HOLD: release stops immediately", stopMs < 30, strFormat("%.2f ms", stopMs));
    }
    // 2. TOGGLE with a mouse trigger (Mouse Button 4)
    {
        configure(c, [](Profile& p) {
            p = baseProfile();
            p.trigger = KeyChord::mouse(MouseButton::X1);
            p.mode.mode = TriggerMode::Toggle;
            p.cps = 200;
        });
        resetPad(c);
        mouse(MouseButton::X1, true);
        mouse(MouseButton::X1, false);
        Sleep(300);
        const bool wasRunning = app.engine().running();
        mouse(MouseButton::X1, true);
        mouse(MouseButton::X1, false);
        waitIdle(app, 500);
        drain(app);
        const uint64_t d = pad.downs(MouseButton::Left);
        check(c, "TOGGLE: mouse trigger (Mouse Button 4) on/off", wasRunning && !app.engine().running() && d >= 45 && d <= 75,
              strFormat("running after 1st press: %s, %llu clicks in ~300 ms at 200 CPS", wasRunning ? "yes" : "no",
                        (unsigned long long)d));
    }
    // 3. BURST / 4. COUNT / 5. DURATION
    {
        configure(c, [](Profile& p) {
            p = baseProfile();
            p.mode.mode = TriggerMode::Burst;
            p.mode.burstCount = 10;
            p.cps = 500;
        });
        resetPad(c);
        tap(VK_F13);
        Sleep(200);
        waitIdle(app, 1000);
        drain(app);
        check(c, "BURST 10", pad.downs(MouseButton::Left) == 10,
              strFormat("%llu clicks", (unsigned long long)pad.downs(MouseButton::Left)));

        configure(c, [](Profile& p) {
            p.mode.mode = TriggerMode::Count;
            p.mode.fixedCount = 25;
        });
        resetPad(c);
        tap(VK_F13);
        Sleep(200);
        waitIdle(app, 1000);
        drain(app);
        check(c, "FIXED COUNT 25", pad.downs(MouseButton::Left) == 25,
              strFormat("%llu clicks", (unsigned long long)pad.downs(MouseButton::Left)));

        configure(c, [](Profile& p) {
            p.mode.mode = TriggerMode::Duration;
            p.mode.durationMs = 500;
            p.cps = 200;
        });
        resetPad(c);
        tap(VK_F13);
        Sleep(650);
        waitIdle(app, 1000);
        drain(app);
        const uint64_t d = pad.downs(MouseButton::Left);
        check(c, "DURATION 500 ms at 200 CPS", d >= 99 && d <= 101, strFormat("%llu clicks (ideal 100)", (unsigned long long)d));
    }
    // 6. Configurable CPS and interval + actual measured
    for (double cps : {10.0, 50.0, 1000.0}) {
        configure(c, [cps](Profile& p) {
            p = baseProfile();
            p.mode.mode = TriggerMode::Duration;
            p.mode.durationMs = 1000;
            p.cps = cps;
            p.rateUnit = cps == 50 ? RateUnit::IntervalMs : RateUnit::Cps;
        });
        resetPad(c);
        tap(VK_F13);
        Sleep(1100);
        waitIdle(app, 1000);
        drain(app);
        EngineSnapshot s;
        app.engine().snapshot(s);
        const uint64_t d = pad.downs(MouseButton::Left);
        check(c, strFormat("RATE %.0f CPS for 1 s (target vs actual)", cps).c_str(),
              std::fabs(double(d) - cps) <= std::max(1.0, cps * 0.01),
              strFormat("received %llu, engine actual %.2f CPS, interval sd %.1f us", (unsigned long long)d, s.runAvgCps,
                        s.runInterval.stddevUs));
    }
    // 7. Keyboard output actions (scan codes) + combos
    {
        configure(c, [](Profile& p) {
            p = baseProfile();
            p.mode.mode = TriggerMode::Count;
            p.mode.fixedCount = 20;
            p.action.chord = KeyChord::key('A');
        });
        resetPad(c);
        tap(VK_F13);
        Sleep(300);
        waitIdle(app, 1000);
        drain(app);
        auto recs = pad.keyRecords();
        const size_t aOk = size_t(std::count_if(recs.begin(), recs.end(), [](const PadKeyRecord& r) { return r.vk == 'A' && r.scan == 0x1E; }));
        check(c, "KEY action 'A' x20 (scan-code injection)", aOk == 20 && pad.counters().keyUp.load() == 20,
              strFormat("%zu key-downs with vk=A scan=0x1E, %llu key-ups", aOk, (unsigned long long)pad.counters().keyUp.load()));

        configure(c, [](Profile& p) {
            p.action.chord = KeyChord::key('K', ModCtrl | ModShift);
            p.mode.fixedCount = 5;
        });
        resetPad(c);
        tap(VK_F13);
        Sleep(200);
        waitIdle(app, 1000);
        drain(app);
        recs = pad.keyRecords();
        const size_t kOk = size_t(std::count_if(recs.begin(), recs.end(), [](const PadKeyRecord& r) {
            return r.vk == 'K' && (r.mods & (ModCtrl | ModShift)) == (ModCtrl | ModShift);
        }));
        check(c, "COMBO Ctrl+Shift+K x5, modifiers released afterwards", kOk == 5 && keysUp({VK_CONTROL, VK_SHIFT, 'K'}),
              strFormat("%zu K presses seen with Ctrl+Shift held", kOk));

        for (uint16_t vk : {uint16_t(VK_F5), uint16_t(VK_SPACE), uint16_t(VK_UP), uint16_t(VK_NUMPAD7), uint16_t(VK_OEM_PERIOD)}) {
            configure(c, [vk](Profile& p) {
                p.action.chord = KeyChord::key(vk);
                p.mode.fixedCount = 3;
            });
            resetPad(c);
            tap(VK_F13);
            Sleep(120);
            waitIdle(app, 1000);
            drain(app);
            recs = pad.keyRecords();
            const size_t n = size_t(std::count_if(recs.begin(), recs.end(), [vk](const PadKeyRecord& r) { return r.vk == vk; }));
            check(c, strFormat("KEY action %s x3", KeyChord::key(vk).name().c_str()).c_str(), n == 3, strFormat("%zu received", n));
        }
    }
    // 8. Mouse output actions
    for (MouseButton b : {MouseButton::Right, MouseButton::Middle, MouseButton::X1, MouseButton::X2, MouseButton::WheelUp,
                          MouseButton::WheelDown}) {
        configure(c, [b](Profile& p) {
            p = baseProfile();
            p.mode.mode = TriggerMode::Count;
            p.mode.fixedCount = 5;
            p.action.chord = KeyChord::mouse(b);
        });
        resetPad(c);
        tap(VK_F13);
        Sleep(120);
        waitIdle(app, 1000);
        drain(app);
        uint64_t got = 0;
        if (b == MouseButton::WheelUp) got = pad.counters().wheel[0].load();
        else if (b == MouseButton::WheelDown) got = pad.counters().wheel[1].load();
        else got = std::min(pad.downs(b), pad.ups(b));
        check(c, strFormat("MOUSE action %s x5", mouseButtonName(b)).c_str(), got == 5, strFormat("%llu received", (unsigned long long)got));
    }
    // 9. Feedback loop: trigger == action (Left Mouse HOLD -> Left clicks)
    {
        configure(c, [](Profile& p) {
            p = baseProfile();
            p.trigger = KeyChord::mouse(MouseButton::Left);
            p.mode.mode = TriggerMode::Hold;
            p.cps = 100;
        });
        resetPad(c);
        const uint64_t runs0 = app.engine().runsCompleted();
        mouse(MouseButton::Left, true);
        Sleep(400);
        const bool running = app.engine().running();
        mouse(MouseButton::Left, false);
        waitIdle(app, 500);
        drain(app);
        const uint64_t runs = app.engine().runsCompleted() - runs0;
        check(c, "NO FEEDBACK LOOP: Left Mouse trigger + Left click action", running && runs == 1 && !app.engine().running(),
              strFormat("kept running while held: %s, runs started: %llu (must be 1), clicks %llu", running ? "yes" : "no",
                        (unsigned long long)runs, (unsigned long long)pad.downs(MouseButton::Left)));
    }
    // 10. Emergency stop while a button is held
    {
        configure(c, [](Profile& p) {
            p = baseProfile();
            p.mode.mode = TriggerMode::Toggle;
            p.cps = 20;
            p.action.downTimeUs = 30000; // button held 60% of the time
        });
        resetPad(c);
        tap(VK_F13);
        Sleep(215);
        const int64_t t0 = clk::now();
        key(VK_LCONTROL, false);
        key(VK_LSHIFT, false);
        key(VK_F12, false);
        key(VK_F12, true);
        key(VK_LSHIFT, true);
        key(VK_LCONTROL, true);
        waitIdle(app, 500);
        const double ms = clk::ticksToMs(clk::now() - t0);
        Sleep(50);
        const bool lUp = (GetAsyncKeyState(VK_LBUTTON) & 0x8000) == 0;
        check(c, "EMERGENCY STOP (Ctrl+Shift+F12) while button held", !app.engine().running() && !app.armed() && lUp &&
                                                                   !app.engine().sender().anyHeld(),
              strFormat("stopped in %.1f ms, disarmed: %s, VK_LBUTTON %s", ms, app.armed() ? "no" : "yes", lUp ? "up" : "DOWN"));
        app.invoke([&] { app.arm(true); });
    }
    // 11. No stuck keyboard keys: stop in the middle of Ctrl+A hold
    {
        configure(c, [](Profile& p) {
            p = baseProfile();
            p.mode.mode = TriggerMode::Toggle;
            p.cps = 2;
            p.action.chord = KeyChord::key('A', ModCtrl);
            p.action.downTimeUs = 300000;
        });
        resetPad(c);
        tap(VK_F13);
        Sleep(120);
        const bool heldMid = (GetAsyncKeyState(VK_CONTROL) & 0x8000) != 0;
        tap(VK_F13); // stop while Ctrl+A are down
        waitIdle(app, 500);
        Sleep(50);
        check(c, "NO STUCK KEYS: stop during Ctrl+A hold", heldMid && keysUp({VK_CONTROL, 'A'}),
              strFormat("Ctrl was down mid-action: %s; after stop Ctrl/A: %s", heldMid ? "yes" : "no",
                        keysUp({VK_CONTROL, 'A'}) ? "released" : "STUCK"));
    }
    // 12. Active window filter
    {
        app.invoke([&] {
            app.setTestTarget(nullptr);
            app.setPadExempt(false);
            app.settings().pauseOnOwnWindow = false;
        });
        configure(c, [](Profile& p) {
            p = baseProfile();
            p.mode.mode = TriggerMode::Duration;
            p.mode.durationMs = 400;
            p.filterEnabled = true;
            p.targetProcess = "some_other_game.exe";
        });
        resetPad(c);
        tap(VK_F13);
        Sleep(250);
        const bool paused = app.engine().state() == EngineState::Paused;
        waitIdle(app, 1000);
        drain(app);
        const uint64_t blocked = pad.downs(MouseButton::Left);
        std::wstring self = paths::exePath();
        self = self.substr(self.find_last_of(L"\\/") + 1);
        configure(c, [&](Profile& p) { p.targetProcess = toUtf8(self); });
        resetPad(c);
        tap(VK_F13);
        Sleep(500);
        waitIdle(app, 1000);
        drain(app);
        const uint64_t allowed = pad.downs(MouseButton::Left);
        check(c, "ACTIVE WINDOW FILTER", paused && blocked == 0 && allowed >= 38,
              strFormat("wrong app in front: paused=%s, %llu clicks; target in front: %llu clicks", paused ? "yes" : "no",
                        (unsigned long long)blocked, (unsigned long long)allowed));
        app.invoke([&] {
            app.setPadExempt(true);
            app.settings().pauseOnOwnWindow = true;
            app.setTestTarget(pad.hwnd());
        });
    }
    // 13. MAX mode: UI stays responsive
    {
        configure(c, [](Profile& p) {
            p = baseProfile();
            p.mode.mode = TriggerMode::Duration;
            p.mode.durationMs = 3000;
            p.maxMode = true;
            p.maxBatch = 8;
            p.maxBackpressure = true;
        });
        resetPad(c);
        std::atomic<bool> stop{false};
        std::vector<double> resp;
        std::thread pinger([&] {
            while (!stop) {
                const int64_t t0 = clk::now();
                DWORD_PTR r;
                SendMessageTimeoutW(c.ui, WM_NULL, 0, 0, SMTO_NORMAL, 2000, &r);
                resp.push_back(clk::ticksToMs(clk::now() - t0));
                Sleep(20);
            }
        });
        tap(VK_F13);
        Sleep(3200);
        waitIdle(app, 2000);
        stop = true;
        pinger.join();
        drain(app);
        EngineSnapshot s;
        app.engine().snapshot(s);
        std::sort(resp.begin(), resp.end());
        const double mx = resp.empty() ? 0 : resp.back();
        const double p99 = resp.empty() ? 0 : resp[size_t(double(resp.size() - 1) * 0.99)];
        check(c, "MAX mode 3 s: GUI stays responsive", mx < 250 && pad.downs(MouseButton::Left) > 1000,
              strFormat("%.0f CPS: %llu clicks generated, %llu received by the pad; UI message round-trip p99 %.1f ms, "
                        "max %.1f ms (%zu pings)",
                        s.runAvgCps, (unsigned long long)s.runActions, (unsigned long long)pad.downs(MouseButton::Left),
                        p99, mx, resp.size()));
    }
    // 14. Idle CPU (armed, window visible, nothing running)
    {
        app.invoke([&] { pad.close(); });
        Sleep(1500);
        const uint64_t c0 = processCycles();
        const int64_t t0 = clk::now();
        Sleep(4000);
        const double cores = cyclesToSec(processCycles() - c0) / clk::ticksToSec(clk::now() - t0);
        check(c, "IDLE CPU (armed, UI visible)", cores < 0.02,
              strFormat("%.4f cores = %.3f%% of one core / %.4f%% of the machine", cores, cores * 100,
                        cores * 100 / logicalCpuCount()));
    }
    // 15. Profiles & settings persist
    {
        app.invoke([&] {
            app.profile().cps = 77.5;
            app.profile().action.chord = KeyChord::key(VK_F24, ModAlt);
            app.settings().emergency = KeyChord::key(VK_PAUSE, ModCtrl);
            app.settings().language = "ru";
            app.settings().alwaysOnTop = true;
            app.saveNow();
        });
        ProfileStore s2;
        s2.load(paths::dataDir());
        const bool ok = s2.active().name == "AutoTest" && s2.active().cps == 77.5 &&
                        s2.active().action.chord == KeyChord::key(VK_F24, ModAlt) && s2.settings().emergency.code == VK_PAUSE &&
                        s2.settings().language == "ru" && s2.settings().alwaysOnTop && s2.settings().uiLayout == 2;
        check(c, "PROFILES & SETTINGS saved and reloaded", ok,
              strFormat("reloaded '%s': %.1f CPS, action %s, emergency %s", s2.active().name.c_str(), s2.active().cps,
                        s2.active().action.chord.name().c_str(), s2.settings().emergency.name().c_str()));
        app.invoke([&] {
            app.settings().emergency = KeyChord::key(VK_F12, ModCtrl | ModShift);
            app.settings().language = "en";
            app.settings().alwaysOnTop = false;
            app.settingsChanged();
        });
    }
    // 15b. Binding capture: click a field, "Listening...", press the key / button
    {
        auto capture = [&](CaptureTarget t, const std::function<void()>& inject) {
            app.invoke([&] { app.beginCapture(t); });
            Sleep(150); // hooks are (re)installed on the trigger thread
            inject();
            const int64_t end = clk::now() + clk::msToTicks(1500);
            while (clk::now() < end && app.capturing() != CaptureTarget::None) Sleep(5);
            Sleep(60);
            return app.capturing() == CaptureTarget::None;
        };
        const KeyChord savedTrig = app.profile().trigger, savedAct = app.profile().action.chord;
        bool ok = capture(CaptureTarget::Trigger, [] { tap(VK_F14); });
        const KeyChord t1 = app.profile().trigger;
        check(c, "BIND: keyboard key (F14) as trigger", ok && t1 == KeyChord::key(VK_F14), "captured: " + t1.name());

        ok = capture(CaptureTarget::Action, [] {
            mouse(MouseButton::X1, true);
            mouse(MouseButton::X1, false);
        });
        const KeyChord a1 = app.profile().action.chord;
        check(c, "BIND: mouse button (Mouse Button 4) as action", ok && a1 == KeyChord::mouse(MouseButton::X1), "captured: " + a1.name());

        ok = capture(CaptureTarget::Trigger, [] {
            key(VK_LCONTROL, false);
            key(VK_LSHIFT, false);
            tap('K');
            key(VK_LSHIFT, true);
            key(VK_LCONTROL, true);
        });
        const KeyChord t2 = app.profile().trigger;
        check(c, "BIND: combination Ctrl+Shift+K", ok && t2.code == 'K' && t2.mods == (ModCtrl | ModShift), "captured: " + t2.name());

        ok = capture(CaptureTarget::Trigger, [] { tap(VK_RCONTROL); });
        const KeyChord t3 = app.profile().trigger;
        check(c, "BIND: modifier alone (Right Ctrl)", ok && t3.code == VK_RCONTROL, "captured: " + t3.name());

        ok = capture(CaptureTarget::Emergency, [] {
            key(VK_LCONTROL, false);
            key(VK_LMENU, false);
            tap(VK_F11);
            key(VK_LMENU, true);
            key(VK_LCONTROL, true);
        });
        const KeyChord e1 = app.settings().emergency;
        check(c, "BIND: emergency stop (Ctrl+Alt+F11)", ok && e1.code == VK_F11 && e1.mods == (ModCtrl | ModAlt), "captured: " + e1.name());

        app.invoke([&] { app.beginCapture(CaptureTarget::Trigger); });
        Sleep(100);
        app.invoke([&] { app.cancelCapture(); });
        Sleep(100);
        check(c, "BIND: cancel leaves the binding unchanged", app.capturing() == CaptureTarget::None && app.profile().trigger == t3,
              "still " + app.profile().trigger.name());
        app.invoke([&] {
            app.profile().trigger = savedTrig;
            app.profile().action.chord = savedAct;
            app.settings().emergency = KeyChord::key(VK_F12, ModCtrl | ModShift);
            app.profileChanged();
        });
        Sleep(150);
    }
    // 15c. Every page of the UI renders (Main, Advanced tabs, Settings tabs, modals) and the UI stays responsive
    {
        struct Nav {
            ui::Page page;
            int sub;
        };
        const Nav pages[] = {{ui::Page::Main, 0},     {ui::Page::Advanced, 0}, {ui::Page::Advanced, 1}, {ui::Page::Advanced, 2},
                             {ui::Page::Settings, 0}, {ui::Page::Settings, 1}, {ui::Page::Settings, 2}, {ui::Page::Settings, 3},
                             {ui::Page::Settings, 4}};
        int64_t beat = app.uiHeartbeat.load();
        int rendered = 0;
        for (const Nav& n : pages) {
            ui::setPage(n.page, n.sub);
            PostMessageW(c.ui, WM_APP_GATE, 0, 0); // wakes the render loop
            Sleep(180);
            const int64_t nb = app.uiHeartbeat.load();
            if (nb != beat) ++rendered;
            beat = nb;
        }
        app.invoke([] {
            ui::setPage(ui::Page::Main, 0);
            ui::openProfileManager();
        });
        Sleep(250);
        PostMessageW(c.ui, WM_KEYDOWN, VK_ESCAPE, 0);
        PostMessageW(c.ui, WM_KEYUP, VK_ESCAPE, 0);
        Sleep(200);
        app.invoke([] { ui::openLog(); });
        Sleep(250);
        PostMessageW(c.ui, WM_KEYDOWN, VK_ESCAPE, 0);
        PostMessageW(c.ui, WM_KEYUP, VK_ESCAPE, 0);
        Sleep(200);
        DWORD_PTR r;
        const bool alive = SendMessageTimeoutW(c.ui, WM_NULL, 0, 0, SMTO_NORMAL, 2000, &r) != 0;
        check(c, "UI: all pages, tabs and modals render without a hang", rendered >= 8 && alive,
              strFormat("%d of %zu pages produced a frame, UI thread alive: %s", rendered, std::size(pages), alive ? "yes" : "NO"));
    }
    // 15d. Tray: minimize hides the window, the icon exists, showing restores it
    {
        NOTIFYICONIDENTIFIER id{sizeof id};
        id.hWnd = c.ui;
        id.uID = 1;
        RECT rc{};
        const bool iconOnStart = SUCCEEDED(Shell_NotifyIconGetRect(&id, &rc));
        app.invoke([&] { app.settings().minimizeToTray = true; });
        PostMessageW(c.ui, WM_SYSCOMMAND, SC_MINIMIZE, 0);
        Sleep(500);
        const bool hidden = !IsWindowVisible(c.ui);
        const bool iconWhileHidden = SUCCEEDED(Shell_NotifyIconGetRect(&id, &rc));
        PostMessageW(c.ui, WM_APP_SHOW, 0, 0);
        Sleep(500);
        const bool shown = IsWindowVisible(c.ui) && !IsIconic(c.ui);
        check(c, "TRAY: icon present, minimize hides to tray, restore shows the window",
              iconOnStart && hidden && iconWhileHidden && shown,
              strFormat("icon at start: %s, hidden after minimize: %s, icon while hidden: %s, visible after restore: %s",
                        iconOnStart ? "yes" : "no", hidden ? "yes" : "no", iconWhileHidden ? "yes" : "no", shown ? "yes" : "no"));
    }
    // 16. Resources
    {
        Sleep(300);
        const ProcessResources r1 = queryProcessResources();
        check(c, "RESOURCES after all tests (leak check)",
              // All Infinity Clicker threads are joined; the Windows thread pool (D3D, WinEvent) may keep a few
              // idle workers around for a while, so allow +3. Steady-state growth is checked by the
              // 60 s stress runs in the benchmark.
              int(r1.handles) - int(r0.handles) < 40 && int(r1.threads) - int(r0.threads) <= 3,
              strFormat("private %.1f -> %.1f MB, handles %u -> %u, threads %u -> %u, GDI %u -> %u", r0.privateBytes / 1048576.0,
                        r1.privateBytes / 1048576.0, r0.handles, r1.handles, r0.threads, r1.threads, r0.gdiObjects,
                        r1.gdiObjects));
    }
    out(c, strFormat("\n**%d passed, %d failed**\n", c.pass, c.fail));
}

} // namespace

int run(const std::wstring& outDir)
{
    bench::consoleInit();
    wchar_t tmp[MAX_PATH];
    GetTempPathW(MAX_PATH, tmp);
    const std::wstring dataDir = std::wstring(tmp) + L"InfinityClickerAutoTest_" + std::to_wstring(GetCurrentProcessId());
    paths::overrideDataDir(dataDir);

    App app;
    app.setLanguageOverride("en"); // reports are English regardless of the Windows language
    app.init();
    Ctx c;
    c.app = &app;
    std::thread tester;
    const int rc = ui::run(app, false, [&](HWND h) {
        c.ui = h;
        tester = std::thread([&c, h] {
            SetThreadDescription(GetCurrentThread(), L"Infinity Clicker AutoTest");
            Sleep(700);
            runTests(c);
            PostMessageW(h, WM_CLOSE, 1, 0);
        });
    });
    if (tester.joinable()) tester.join();
    app.shutdown();
    std::wstring dir = outDir.empty() ? paths::exeDir() + L"\\benchmarks" : outDir;
    paths::ensureDir(dir);
    paths::writeFileAtomic(dir + L"\\autotest_results.md", c.md);
    bench::consolePrint("\nResults written to %s\\autotest_results.md\n", toUtf8(dir).c_str());
    return rc ? rc : (c.fail ? 1 : 0);
}

} // namespace infclick::autotest
