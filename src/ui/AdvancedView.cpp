#include "ui/Ui.h"

#include "core/Clock.h"
#include "core/I18n.h"
#include "core/Paths.h"
#include "core/Str.h"
#include "platform/win/ProcessUtil.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace infclick::ui {

namespace {

void groupBegin(const char* title, const char* id)
{
    ImGui::Dummy(ImVec2(0, 2 * S()));
    sectionLabel(title);
    beginCard(id, ImVec2(0, 0));
}

void groupEnd()
{
    endCard();
    ImGui::Dummy(ImVec2(0, 2 * S()));
}

// ------------------------------------------------------------------ Engine tab

void drawEngineTab(App& app)
{
    Profile& p = app.profile();
    AppSettings& st = app.settings();
    bool changed = false, settingsChanged = false;
    const float ctlH = 34 * S();

    // ---- timing and click shape
    groupBegin(tr("TIMING"), "##timing");
    {
        const char* pr[] = {tr("ECO"), tr("STANDARD"), tr("ULTRA")};
        const char* hints[] = {tr("Timer only. ~0.3-1 ms jitter, ~0% CPU."),
                               tr("Timer + short QPC spin (<= 10% of the interval). Exact up to ~100 CPS, low CPU."),
                               tr("Timer + spin to the deadline. Microsecond timing at any rate - HIGH CPU above ~250 CPS.")};
        int v = int(p.precision);
        rowBegin(tr("Precision"), hints[v], 270 * S(), ctlH);
        if (segmented("prec", pr, 3, &v, 270 * S())) {
            p.precision = Precision(v);
            changed = true;
        }
        rowEnd();

        const char* units[] = {"CPS", tr("ms"), tr("us")};
        int u = int(p.rateUnit);
        rowBegin(tr("Speed unit"), tr("Unit of the speed value on the main screen."), 200 * S(), ctlH);
        if (segmented("unit", units, 3, &u, 200 * S())) {
            p.rateUnit = RateUnit(u);
            changed = true;
        }
        rowEnd();

        const bool clamp = !p.maxMode && p.action.downTimeUs > 0 && p.action.downTimeUs >= 0.9 * 1e6 / p.cps;
        rowBegin(tr("Down time"),
                 clamp ? tr("Down time >= interval: it will be clamped to 50% of the interval.")
                       : tr("How long the button stays down (us). Games that read the button once per frame need 10 ms or more."),
                 190 * S());
        uint32_t d = p.action.downTimeUs;
        const uint32_t step = 100, fast = 1000;
        ImGui::SetNextItemWidth(190 * S());
        if (ImGui::InputScalar("##down", ImGuiDataType_U32, &d, &step, &fast)) {
            p.action.downTimeUs = std::min<uint32_t>(d, 10'000'000);
            changed = true;
        }
        rowEnd(false);
        bool first = true;
        for (uint32_t v2 : {0u, 1000u, 5000u, 10000u, 20000u}) {
            if (!first) ImGui::SameLine(0, 6 * S());
            first = false;
            if (chip(v2 >= 1000 ? strFormat(tr("%u ms"), v2 / 1000).c_str() : strFormat(tr("%u us"), v2).c_str(), p.action.downTimeUs == v2)) {
                p.action.downTimeUs = v2;
                changed = true;
            }
        }
        ImGui::Dummy(ImVec2(0, 6 * S()));
        ImGui::Separator();
        ImGui::Dummy(ImVec2(0, 6 * S()));

        const char* reps[] = {"x1", "x2", "x3"};
        int r = int(p.action.repeat) - 1;
        rowBegin(tr("Presses per action"), tr("x2 = double click, x3 = triple click."), 150 * S(), ctlH);
        if (segmented("rep", reps, 3, &r, 150 * S())) {
            p.action.repeat = uint32_t(r + 1);
            changed = true;
        }
        rowEnd(p.action.repeat > 1);
        if (p.action.repeat > 1) {
            rowBegin(tr("Gap between presses (us)"), nullptr, 190 * S());
            uint32_t g = p.action.repeatGapUs;
            ImGui::SetNextItemWidth(190 * S());
            if (ImGui::InputScalar("##gap", ImGuiDataType_U32, &g)) {
                p.action.repeatGapUs = std::min<uint32_t>(g, 1'000'000);
                changed = true;
            }
            rowEnd(false);
        }
    }
    groupEnd();

    // ---- scheduler / input backend
    groupBegin(tr("SCHEDULER AND INPUT"), "##sched");
    {
        const char* prios[] = {tr("Normal"), tr("Above normal"), tr("Highest"), tr("Time critical"), tr("MMCSS (Games)")};
        int pr = int(p.priority);
        stackedBegin(tr("Scheduler thread priority"),
                 tr("Priority inside the NORMAL process class (Infinity Clicker never uses REALTIME_PRIORITY_CLASS). Matters mostly when the "
                    "CPU is fully loaded - see BENCHMARK.md."));
        ImGui::SetNextItemWidth(190 * S());
        if (ImGui::Combo("##prio", &pr, prios, 5)) {
            p.priority = power::ThreadPrio(pr);
            changed = true;
        }
        stackedEnd();

        const char* late[] = {tr("Catch up (batch due actions)"), tr("Strict (skip missed slots)")};
        int lp = p.latePolicy == LatePolicy::CatchUp ? 0 : 1;
        stackedBegin(tr("Late policy"),
                 tr("The timeline is absolute (slot k = start + k * interval), so lateness never accumulates into drift. Catch up: "
                    "slots that are already due go out in one SendInput call (max 8). Strict: overdue slots are skipped and counted "
                    "as missed."));
        ImGui::SetNextItemWidth(250 * S());
        if (ImGui::Combo("##late", &lp, late, 2)) {
            p.latePolicy = lp == 0 ? LatePolicy::CatchUp : LatePolicy::Skip;
            changed = true;
        }
        stackedEnd();

        const char* km[] = {tr("Scan code (games, DirectInput/raw input)"), tr("Virtual key")};
        int k = int(p.action.keyMode);
        rowBegin(tr("Keyboard injection"), nullptr, 300 * S());
        ImGui::SetNextItemWidth(300 * S());
        if (ImGui::Combo("##km", &k, km, 2)) {
            p.action.keyMode = KeyInjectMode(k);
            changed = true;
        }
        rowEnd(false);
    }
    groupEnd();

    // ---- trigger handling
    groupBegin(tr("TRIGGER"), "##trigger");
    {
        const char* be[] = {tr("Low-level hook (WH_MOUSE_LL)"), tr("Raw Input (no hook)")};
        int b = int(p.mouseBackend);
        stackedBegin(tr("Mouse trigger backend"),
                 tr("Hook: can block the trigger and tells physical from injected input exactly, but every mouse event in the system "
                    "passes through it and SendInput gets slower (measured). Raw Input: cheaper, cannot block the trigger and cannot "
                    "flag input injected by other programs. Infinity Clicker's own events are always recognised by their tag."));
        ImGui::SetNextItemWidth(250 * S());
        if (ImGui::Combo("##be", &b, be, 2)) {
            p.mouseBackend = TriggerBackend(b);
            changed = true;
        }
        stackedEnd();
        if (toggleRow(tr("Block the trigger from other applications"),
                      tr("The trigger key/button is swallowed, e.g. Mouse Button 4 stops navigating 'Back' in a browser while armed. "
                         "Uses a low-level hook."),
                      &p.suppressTrigger))
            changed = true;
        if (toggleRow(tr("Accept input injected by other programs"),
                      tr("Treat keys sent by macro tools or remote desktop as real presses. Infinity Clicker's own clicks never count."),
                      &st.acceptInjectedTriggers))
            settingsChanged = true;
    }
    groupEnd();

    // ---- values of the extra modes
    groupBegin(tr("MODE VALUES"), "##modes");
    {
        uint64_t one = 1, ten = 10;
        rowBegin(tr("Burst size"), tr("Actions per press in Burst mode."), 160 * S());
        ImGui::SetNextItemWidth(160 * S());
        if (ImGui::InputScalar("##burst", ImGuiDataType_U64, &p.mode.burstCount, &one, &ten)) {
            p.mode.burstCount = std::clamp<uint64_t>(p.mode.burstCount, 1, 1'000'000'000ull);
            changed = true;
        }
        rowEnd();
        rowBegin(tr("Fixed count"), tr("Actions per run in Fixed count mode."), 160 * S());
        ImGui::SetNextItemWidth(160 * S());
        if (ImGui::InputScalar("##count", ImGuiDataType_U64, &p.mode.fixedCount, &one, &ten)) {
            p.mode.fixedCount = std::clamp<uint64_t>(p.mode.fixedCount, 1, 1'000'000'000'000ull);
            changed = true;
        }
        rowEnd();
        rowBegin(tr("Duration"), tr("Length of a run in Duration mode (ms)."), 160 * S());
        int ms = int(p.mode.durationMs);
        ImGui::SetNextItemWidth(160 * S());
        if (ImGui::InputInt("##dur", &ms, 100, 1000)) {
            p.mode.durationMs = uint32_t(std::clamp(ms, 1, 86'400'000));
            changed = true;
        }
        rowEnd(false);
    }
    groupEnd();

    // ---- foreground application filter
    groupBegin(tr("TARGET APPLICATION"), "##target");
    {
        if (toggleRow(tr("Only click while an application is in front"),
                      tr("Output pauses automatically whenever another window is in front, and resumes when the target returns. "
                         "Several names can be separated with ';' (e.g. javaw.exe; java.exe)."),
                      &p.filterEnabled))
            changed = true;
        ImGui::BeginDisabled(!p.filterEnabled);
        char buf[512];
        strncpy_s(buf, p.targetProcess.c_str(), _TRUNCATE);
        ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - 90 * S() - 8 * S());
        if (ImGui::InputTextWithHint("##proc", tr("process name, e.g. javaw.exe"), buf, sizeof buf)) {
            p.targetProcess = buf;
            changed = true;
        }
        ImGui::SameLine(0, 8 * S());
        static std::vector<procutil::WindowProcess> list;
        if (ImGui::Button(tr("Pick"), ImVec2(90 * S(), 0))) {
            list = procutil::listWindowProcesses();
            ImGui::OpenPopup("procpick");
        }
        if (ImGui::BeginPopup("procpick")) {
            for (const auto& wp : list) {
                const std::string label = toUtf8(wp.process) + "   -   " + toUtf8(wp.title).substr(0, 48) +
                                          (wp.elevated ? std::string("  ") + tr("[admin]") : std::string());
                if (ImGui::Selectable(label.c_str())) {
                    p.targetProcess = toUtf8(wp.process);
                    changed = true;
                }
            }
            if (list.empty()) ImGui::TextDisabled("%s", tr("No windows found"));
            ImGui::EndPopup();
        }
        ImGui::EndDisabled();
        ImGui::TextDisabled(tr("Foreground now: %s"), toUtf8(app.trigger().foregroundProcess()).c_str());
    }
    groupEnd();

    // ---- safety
    groupBegin(tr("SAFETY"), "##safety");
    {
        stackedBegin(tr("Emergency stop"),
                     tr("Stops output and disarms regardless of mode, profile or window; releases every held key/button."));
        const bool cap = app.capturing() == CaptureTarget::Emergency;
        if (bindField("emg", st.emergency, cap, app.captureSecondsLeft(), std::min(300.0f * S(), ImGui::GetContentRegionAvail().x - 44 * S()))) {
            if (cap) app.cancelCapture();
            else app.beginCapture(CaptureTarget::Emergency);
        }
        ImGui::SameLine(0, 8 * S());
        KeyChord e = st.emergency;
        if (keyPicker("emgpick", e, true, false) && e.valid()) {
            st.emergency = e;
            settingsChanged = true;
        }
        stackedEnd();
        if (toggleRow(tr("Crash guardian process"),
                      tr("A tiny helper process that releases every held key and button if Infinity Clicker is killed or crashes. Takes effect "
                         "on the next start."),
                      &st.guardian))
            settingsChanged = true;
    }
    groupEnd();

    // ---- experimental
    groupBegin(tr("EXPERIMENTAL"), "##exp");
    {
        if (toggleRow(tr("MAX speed"),
                      tr("No target rate: actions are sent as fast as SendInput accepts them. High CPU usage (one core). The real "
                         "throughput is shown as Actual on the main screen."),
                      &p.maxMode))
            changed = true;
        ImGui::BeginDisabled(!p.maxMode);
        rowBegin(tr("Actions per SendInput call"), nullptr, 200 * S());
        int mb = int(p.maxBatch);
        ImGui::SetNextItemWidth(200 * S());
        if (ImGui::SliderInt("##mb", &mb, 1, 64)) {
            p.maxBatch = uint32_t(mb);
            changed = true;
        }
        rowEnd();
        if (toggleRow(tr("Backpressure"),
                      tr("Limits in-flight events (accepted by SendInput but not yet seen by Infinity Clicker's hook), so physical input, "
                         "including the emergency key, never waits behind a pile of synthetic events."),
                      &p.maxBackpressure))
            changed = true;
        ImGui::EndDisabled();
    }
    groupEnd();

    if (changed) app.profileChanged();
    if (settingsChanged) app.settingsChanged();
}

// ------------------------------------------------------------------ Diagnostics tab

void drawDiagnosticsTab(App& app)
{
    const Profile& p = app.profile();
    const EngineSnapshot& s = app.snap();
    ImGui::Dummy(ImVec2(0, 2 * S()));
    beginCard("##spark", ImVec2(0, 0));
    sparkline(s.cpsHistory, s.cpsHistoryCount, p.maxMode ? 0.0f : float(p.cps), ImVec2(ImGui::GetContentRegionAvail().x, 84 * S()));
    endCard();
    ImGui::Dummy(ImVec2(0, 2 * S()));
    beginCard("##telemetry", ImVec2(0, 0));
    if (ImGui::BeginTable("tel", 2, ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_SizingStretchProp)) {
        ImGui::TableSetupColumn(tr("Metric"), ImGuiTableColumnFlags_WidthStretch, 0.55f);
        ImGui::TableSetupColumn(tr("Value"), ImGuiTableColumnFlags_WidthStretch, 0.45f);
        metricRow(tr("Target CPS"), "%s", p.maxMode ? "MAX" : fmtRate(p.cps).c_str());
        metricRow(tr("Actual CPS - run average"), "%s", fmtRate(s.runAvgCps).c_str());
        metricRow(tr("Actual CPS - last 1 s / 5 s"), "%s / %s", fmtRate(s.cps1s).c_str(), fmtRate(s.cps5s).c_str());
        metricRow(tr("Generated actions (run / total)"), "%llu / %llu", (unsigned long long)s.runActions, (unsigned long long)s.totalActions);
        metricRow(tr("SendInput calls / s"), "%.1f", s.callsPerSec1s);
        metricRow(tr("Events accepted / s"), "%.1f", s.eventsPerSec1s);
        metricRow(tr("Events requested (run)"), "%llu", (unsigned long long)s.runRequested);
        metricRow(tr("Events accepted by SendInput (run)"), "%llu", (unsigned long long)s.runAccepted);
        metricRow(tr("Failed SendInput calls (run)"), "%llu%s", (unsigned long long)s.runFailedCalls,
                  s.lastError ? strFormat(tr("  (last error %u)"), s.lastError).c_str() : "");
        metricRow(tr("Missed deadlines (run)"), "%llu", (unsigned long long)s.runMissed);
        metricRow(tr("SendInput call time p50 / p99 / max"), "%s / %s / %s", fmtUs(s.sendP50Us).c_str(), fmtUs(s.sendP99Us).c_str(),
                  fmtUs(s.sendMaxUs).c_str());
        metricRow(tr("Share of time blocked in SendInput"), "%.1f %%", s.sendShare * 100);
        metricRow(tr("Interval min / avg / max"), "%s / %s / %s", fmtUs(s.runInterval.minUs).c_str(), fmtUs(s.runInterval.avgUs).c_str(),
                  fmtUs(s.runInterval.maxUs).c_str());
        metricRow(tr("Jitter (interval std-dev)"), tr("%s  (last s: %s)"), fmtUs(s.runInterval.stddevUs).c_str(),
                  fmtUs(s.secInterval.stddevUs).c_str());
        metricRow(tr("Mean interval error vs target"), "%.3f %%", s.avgErrorPct);
        metricRow(tr("Lateness p50 / p99 / p99.9"), "%s / %s / %s", fmtUs(s.runLate.p50Us).c_str(), fmtUs(s.runLate.p99Us).c_str(),
                  fmtUs(s.runLate.p999Us).c_str());
        metricRow(tr("Lateness max, >100 us late"), "%s, %llu", fmtUs(s.runLate.maxUs).c_str(), (unsigned long long)s.runLate.over100us);
        metricRow(tr("Spin / sleep share of time"), "%.1f %% / %.1f %%", s.spinShare * 100, s.sleepShare * 100);
        metricRow(tr("Wake-up margin (current)"), "%s", fmtUs(s.marginUs).c_str());
        metricRow(tr("Timer oversleep p50 / p99"), "%s / %s%s", fmtUs(s.oversleepP50Us).c_str(), fmtUs(s.oversleepP99Us).c_str(),
                  s.highResTimer ? "" : tr("  (no HR timer!)"));
        metricRow(tr("Trigger -> first action (last/avg/max)"), "%s / %s / %s", fmtUs(s.lastTriggerLatUs).c_str(),
                  fmtUs(s.avgTriggerLatUs).c_str(), fmtUs(s.maxTriggerLatUs).c_str());
        metricRow(tr("CPU: process (% of machine / cores)"), "%.2f %% / %.3f", app.cpu().processPercentOfMachine(), app.cpu().processCores());
        metricRow(tr("CPU: scheduler thread (cores)"), "%.3f", app.cpu().schedulerCores());
        metricRow(tr("Hooks: keyboard / mouse / raw input"), "%s / %s / %s", app.trigger().keyboardHookInstalled() ? tr("on") : tr("OFF"),
                  app.trigger().mouseHookInstalled() ? tr("on") : tr("off"), app.trigger().rawInputActive() ? tr("on") : tr("off"));
        metricRow(tr("Emergency hotkey backup (RegisterHotKey)"), "%s",
                  app.trigger().emergencyHotkeyRegistered() ? tr("registered") : tr("not registered"));
        ImGui::EndTable();
    }
    ImGui::Dummy(ImVec2(0, 4 * S()));
    if (ImGui::Button(tr("Reset totals"))) app.engine().resetTotals();
    ImGui::SameLine();
    if (ImGui::Button(tr("View log"))) openLog();
    endCard();
}

} // namespace

// ------------------------------------------------------------------ Input Lab

void drawLabPage(App& app)
{
    static uint64_t baseReq = 0, baseAcc = 0, baseObs = 0;
    static int64_t lastT = 0;
    static uint64_t lastRecv = 0, lastAcc = 0;
    static double recvRate = 0, accRate = 0;
    static HANDLE benchProc = nullptr;

    const EngineSnapshot& s = app.snap();
    TestPad& pad = app.pad();
    ImGui::Dummy(ImVec2(0, 2 * S()));
    ImGui::PushStyleColor(ImGuiCol_Text, v4(col::dim));
    ImGui::PushTextWrapPos(0);
    ImGui::TextUnformatted(tr("The Input Lab measures the whole pipeline without any game: Infinity Clicker's own clicks go into a separate Test Pad "
                              "window (own thread) that counts what an application actually receives."));
    ImGui::PopTextWrapPos();
    ImGui::PopStyleColor();
    ImGui::Dummy(ImVec2(0, 2 * S()));
    const bool padOpen = pad.isOpen();
    if (ImGui::Button(padOpen ? tr("Close Test Pad") : tr("Open Test Pad"), ImVec2(170 * S(), 0))) app.openPad(!padOpen);
    ImGui::SameLine();
    bool obs = app.labObserve();
    ImGui::AlignTextToFramePadding();
    if (toggleSwitch("obs", &obs)) app.setLabObserve(obs);
    ImGui::SameLine();
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(tr("Observe via mouse hook"));
    help(tr("Installs WH_MOUSE_LL so stage 3 (events delivered through the system input chain) can be counted. The hook itself costs "
            "throughput - that is part of what you measure."));
    ImGui::SameLine();
    if (ImGui::Button(tr("Reset counters"))) {
        baseReq = s.totalRequested;
        baseAcc = s.totalAccepted;
        baseObs = app.trigger().observedOurs.load();
        pad.reset();
    }

    const uint64_t req = s.totalRequested - std::min(baseReq, s.totalRequested);
    const uint64_t acc = s.totalAccepted - std::min(baseAcc, s.totalAccepted);
    const uint64_t obsNow = app.trigger().observedOurs.load();
    const uint64_t ob = obsNow - std::min(baseObs, obsNow);
    const uint64_t rec = pad.received();
    const int64_t now = clk::now();
    if (clk::ticksToSec(now - lastT) >= 0.5) {
        const double dt = clk::ticksToSec(now - lastT);
        recvRate = lastT ? double(rec - std::min(lastRecv, rec)) / dt : 0;
        accRate = lastT ? double(acc - std::min(lastAcc, acc)) / dt : 0;
        lastT = now;
        lastRecv = rec;
        lastAcc = acc;
    }

    ImGui::Dummy(ImVec2(0, 4 * S()));
    const float w = ImGui::GetContentRegionAvail().x;
    const float gap = 8 * S();
    const float bw = (w - 3 * gap) / 4;
    struct Stage {
        const char* name;
        const char* sub;
        std::string value;
        ImU32 c;
    };
    const Stage st[4] = {
        {tr("1. REQUESTED"), tr("events the scheduler asked for"), u64str(req), col::violet},
        {tr("2. ACCEPTED"), tr("SendInput return value"), u64str(acc) + strFormat(tr("  (%.0f/s)"), accRate), col::cyan},
        {tr("3. OBSERVED"), app.trigger().mouseHookInstalled() ? tr("seen by our LL hook") : tr("enable hook to measure"),
         app.trigger().mouseHookInstalled() ? u64str(ob) : std::string("-"), col::amber},
        {tr("4. RECEIVED"), tr("messages the Test Pad got"), u64str(rec) + strFormat(tr("  (%.0f/s)"), recvRate), col::green},
    };
    for (int i = 0; i < 4; ++i) {
        if (i) ImGui::SameLine(0, gap);
        ImGui::PushID(i);
        ImGui::PushStyleColor(ImGuiCol_Border, v4(alpha(st[i].c, 0.7f)));
        beginCard("##stage", ImVec2(bw, 104 * S()));
        ImGui::PushFont(fonts().semibold, 12.0f);
        ImGui::TextColored(v4(st[i].c), "%s", st[i].name);
        ImGui::PopFont();
        ImGui::PushFont(fonts().display, 20.0f);
        ImGui::TextUnformatted(st[i].value.c_str());
        ImGui::PopFont();
        ImGui::PushTextWrapPos(0);
        ImGui::TextDisabled("%s", st[i].sub);
        ImGui::PopTextWrapPos();
        endCard();
        ImGui::PopStyleColor();
        ImGui::PopID();
    }
    if (acc > rec && pad.isOpen() && !app.engine().running())
        ImGui::TextColored(v4(col::amber), tr("Accepted but not received by the pad: %llu (lost or still queued)"), (unsigned long long)(acc - rec));

    ImGui::Dummy(ImVec2(0, 4 * S()));
    sectionLabel(tr("RUN A TEST WITH THE CURRENT PROFILE INTO THE TEST PAD"));
    ImGui::TextDisabled("%s", tr("Output only flows while the Test Pad is in front AND the cursor is over it."));
    auto run = [&](RunLimits l) {
        baseReq = s.totalRequested;
        baseAcc = s.totalAccepted;
        baseObs = app.trigger().observedOurs.load();
        pad.reset();
        app.startTest(l);
    };
    if (ImGui::Button(tr("Run 1 s"))) run(RunLimits{0, 1000});
    ImGui::SameLine();
    if (ImGui::Button(tr("Run 5 s"))) run(RunLimits{0, 5000});
    ImGui::SameLine();
    if (ImGui::Button(tr("Run 1000 actions"))) run(RunLimits{1000, 0});
    ImGui::SameLine();
    ImGui::BeginDisabled(!app.engine().running());
    if (ImGui::Button(tr("Stop"))) app.stop();
    ImGui::EndDisabled();
    if (app.labRunActive()) {
        ImGui::SameLine();
        ImGui::TextColored(v4(col::amber), "%s", tr("running..."));
    }

    ImGui::Dummy(ImVec2(0, 4 * S()));
    sectionLabel(tr("FULL BENCHMARK SUITE"));
    ImGui::PushStyleColor(ImGuiCol_Text, v4(col::dim));
    ImGui::PushTextWrapPos(0);
    ImGui::TextUnformatted(tr("Runs `InfinityClicker.exe --bench quick` in a separate process: timers, spin threshold, SendInput throughput, CPS "
                              "matrix, priorities, hold-time visibility, trigger latency, stress. Covers the screen with a full-screen pad "
                              "for ~3 minutes. Results: benchmarks\\latest.md"));
    ImGui::PopTextWrapPos();
    ImGui::PopStyleColor();
    const bool benchRunning = benchProc && WaitForSingleObject(benchProc, 0) == WAIT_TIMEOUT;
    if (benchProc && !benchRunning) {
        CloseHandle(benchProc);
        benchProc = nullptr;
    }
    ImGui::BeginDisabled(benchRunning || app.engine().running());
    if (ImGui::Button(benchRunning ? tr("Benchmark running...") : tr("Run benchmark (quick)"))) {
        std::wstring exe = paths::exePath();
        std::wstring cmd = L"\"" + exe + L"\" --bench quick";
        STARTUPINFOW si{sizeof si};
        PROCESS_INFORMATION pi{};
        app.arm(false);
        if (CreateProcessW(exe.c_str(), cmd.data(), nullptr, nullptr, FALSE, 0, nullptr, nullptr, &si, &pi)) {
            CloseHandle(pi.hThread);
            benchProc = pi.hProcess;
        }
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    if (ImGui::Button(tr("Open results"))) openPath(paths::exeDir() + L"\\benchmarks");
}

// ------------------------------------------------------------------ page

void drawAdvanced(App& app)
{
    if (pageHeader(tr("Advanced"))) {
        setPage(Page::Main);
        return;
    }
    ImGui::Dummy(ImVec2(0, 4 * S()));
    const char* tabs[] = {tr("Engine"), tr("Diagnostics"), tr("Input Lab")};
    int tab = std::clamp(currentSubTab(), 0, 2);
    if (segmented("advtabs", tabs, 3, &tab, ImGui::GetContentRegionAvail().x)) setSubTab(tab);
    ImGui::Dummy(ImVec2(0, 2 * S()));
    ImGui::BeginChild("##advscroll", ImVec2(0, 0), ImGuiChildFlags_None, ImGuiWindowFlags_NoBackground);
    switch (tab) {
    case 0: drawEngineTab(app); break;
    case 1: drawDiagnosticsTab(app); break;
    default: drawLabPage(app); break;
    }
    ImGui::EndChild();
}

} // namespace infclick::ui
