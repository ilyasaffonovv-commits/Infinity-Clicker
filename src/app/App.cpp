#include "app/App.h"

#include "core/Clock.h"
#include "core/I18n.h"
#include "core/Log.h"
#include "core/Paths.h"
#include "core/Str.h"
#include "platform/win/Guardian.h"
#include "platform/win/ProcessUtil.h"

#include <infclick/Version.h>

namespace infclick {

App::App() = default;

App::~App() { shutdown(); }

bool App::init()
{
    const std::wstring dir = paths::dataDir();
    log::init(dir + L"\\logs");
    if (!langOverride_.empty()) i18n::setLang(i18n::resolve(langOverride_)); // default profile names follow it
    store_.load(dir);
    if (!langOverride_.empty()) settings().language = langOverride_;
    i18n::setLang(i18n::resolve(settings().language));
    log::setFileEnabled(settings().fileLogging);
    log::setVerbose(settings().debugLogging);
    elevated_ = procutil::selfElevated();
    TLOG_I("Infinity Clicker %s starting (pid %lu, data dir %s, %s)", INFCLICK_VERSION, GetCurrentProcessId(),
           toUtf8(dir).c_str(), elevated_ ? "elevated" : "not elevated");

    held_ = guardian::createShared();
    engine_.sender().attachShared(held_);
    guardian::installCrashHandler(held_, dir + L"\\crash");
    if (settings().guardian) guardian::spawnWatchdog();

    engine_.setGate(trigger_.gateOpenPtr(), trigger_.gateReasonPtr(), trigger_.gateEvent());
    engine_.setObservedCounter(&trigger_.observedOurs);
    engine_.setStateCallback([this](EngineState) {
        if (uiHwnd_) PostMessageW(uiHwnd_, WM_APP_ENGINE, 0, 0);
    });
    engine_.start();
    applyConfigs();
    if (!trigger_.start(this)) {
        TLOG_E("trigger engine failed to start - hotkeys will not work");
        setStatus(tr("ERROR: keyboard hook could not be installed"));
    }
    if (settings().armOnLaunch) engine_.arm(true);
    TLOG_I("profile '%s' active; trigger %s, action %s, emergency stop %s", profile().name.c_str(),
           profile().trigger.name().c_str(), profile().action.chord.name().c_str(), settings().emergency.name().c_str());
    return true;
}

void App::shutdown()
{
    static bool done = false;
    if (done) return;
    done = true;
    if (dirty_) saveNow();
    engine_.stopRun();
    engine_.shutdown(); // releases every held key/button
    trigger_.shutdown();
    pad_.close();
    guardian::markCleanExit();
    TLOG_I("Infinity Clicker exiting cleanly");
    log::shutdown();
}

void App::setUiWindow(HWND h)
{
    uiHwnd_ = h;
    applyConfigs();
}

void App::applyConfigs()
{
    const Profile& p = store_.active();
    const AppSettings& s = store_.settings();
    const bool mouseAction = p.action.chord.isMouse();

    TriggerConfig t;
    t.trigger = p.trigger;
    t.suppressTrigger = p.suppressTrigger;
    t.mouseBackend = p.mouseBackend;
    t.acceptInjected = s.acceptInjectedTriggers;
    t.acceptBenchTag = acceptBenchTag_;
    t.emergency = s.emergency;
    // MAX mode backpressure needs our hook to see delivered mouse events.
    const bool bp = p.maxMode && p.maxBackpressure && mouseAction;
    t.observeMouse = labObserve_ || bp;
    t.filterEnabled = p.filterEnabled;
    t.targetProcesses = toWide(p.targetProcess);
    t.pauseOnOwnWindow = s.pauseOnOwnWindow;
    t.ownWindow = uiHwnd_;
    t.testPad = padExempt_ ? pad_.hwnd() : nullptr;
    trigger_.setConfig(t);

    EngineConfig e = makeEngineConfig(p);
    if (!bp) e.maxInflight = 0;
    e.testTargetHwnd = testTarget_;
    engine_.setConfig(e);
}

void App::profileChanged()
{
    applyConfigs();
    dirty_ = true;
    saveAt_ = clk::now() + clk::msToTicks(600);
}

void App::settingsChanged()
{
    log::setFileEnabled(settings().fileLogging);
    log::setVerbose(settings().debugLogging);
    profileChanged();
}

void App::selectProfile(const std::string& name)
{
    if (store_.setActive(name)) {
        TLOG_I("profile switched to '%s'", name.c_str());
        profileChanged();
    }
}

void App::saveNow()
{
    dirty_ = false;
    if (!store_.save()) setStatus(std::string(tr("Could not save settings: ")) + store_.lastError());
}

void App::setStatus(const std::string& s)
{
    status_ = s;
    statusUntil_ = clk::now() + clk::secToTicks(6);
}

void App::tick()
{
    engine_.snapshot(snap_);
    cpu_.sample(engine_.threadHandle());
    const int64_t now = clk::now();
    if (dirty_ && now >= saveAt_) saveNow();
    if (labRunActive_ && engine_.runsCompleted() > labRunBase_) {
        labRunActive_ = false;
        testTarget_ = nullptr;
        applyConfigs();
    }
    if (!status_.empty() && now > statusUntil_) status_.clear();
    if (captureTarget_ != CaptureTarget::None && now > captureDeadline_) {
        cancelCapture();
        setStatus(tr("Binding cancelled (timeout)"));
    }
    if (now - lastHealth_ > clk::secToTicks(2)) {
        lastHealth_ = now;
        healthCheck();
    }
}

// Windows silently removes a low-level hook whose callback ever exceeds
// LowLevelHooksTimeout. Our own injected events must be visible to our mouse
// hook - if they stop arriving while we keep injecting, reinstall.
void App::healthCheck()
{
    const uint64_t acc = engine_.sender().accepted.load();
    const uint64_t obs = trigger_.observedOursMouse.load();
    if (trigger_.mouseHookInstalled() && profile().action.chord.isMouse() && acc - healthAccepted_ > 200 &&
        obs == healthObserved_) {
        TLOG_W("health: mouse hook saw none of %llu injected events - reinstalling hooks",
               (unsigned long long)(acc - healthAccepted_));
        trigger_.reinstallHooks();
    }
    healthAccepted_ = acc;
    healthObserved_ = obs;
}

void App::arm(bool on)
{
    engine_.arm(on);
    if (!on) engine_.stopRun();
}

// Lab runs are gated on the test pad (foreground + cursor over it), so a
// click on "Run" can never spill synthetic clicks into Infinity Clicker's own UI.
void App::startTest(RunLimits limits)
{
    if (!pad_.isOpen()) openPad(true);
    labRunBase_ = engine_.runsCompleted();
    labRunActive_ = true;
    testTarget_ = pad_.hwnd();
    applyConfigs();
    pad_.activate();
    engine_.startRun(limits);
}

void App::stop() { engine_.stopRun(); }

void App::emergency()
{
    engine_.emergency();
    setStatus(tr("EMERGENCY STOP - output halted, held input released, disarmed"));
}

void App::beginCapture(CaptureTarget t)
{
    {
        std::lock_guard lk(capMu_);
        capturedValid_ = false;
    }
    captureTarget_ = t;
    captureDeadline_ = clk::now() + clk::secToTicks(8);
    trigger_.beginCapture(true);
}

void App::cancelCapture()
{
    captureTarget_ = CaptureTarget::None;
    trigger_.cancelCapture();
}

double App::captureSecondsLeft() const
{
    if (captureTarget_ == CaptureTarget::None) return 0;
    return std::max(0.0, clk::ticksToSec(captureDeadline_ - clk::now()));
}

bool App::applyCaptured()
{
    KeyChord c;
    {
        std::lock_guard lk(capMu_);
        if (!capturedValid_) return false;
        c = captured_;
        capturedValid_ = false;
    }
    const CaptureTarget t = captureTarget_;
    captureTarget_ = CaptureTarget::None;
    switch (t) {
    case CaptureTarget::Trigger:
        profile().trigger = c;
        setStatus(std::string(tr("Trigger: ")) + c.name());
        break;
    case CaptureTarget::Action:
        profile().action.chord = c;
        setStatus(std::string(tr("Action: ")) + c.name());
        break;
    case CaptureTarget::Emergency:
        if (c.isMouse() && c.button() == MouseButton::Left && !c.mods) {
            setStatus(tr("Left Mouse alone cannot be the emergency stop"));
            return true;
        }
        settings().emergency = c;
        setStatus(std::string(tr("Emergency stop: ")) + c.name());
        break;
    default: return false;
    }
    TLOG_I("binding captured: %s", c.name().c_str());
    profileChanged();
    return true;
}

void App::setLabObserve(bool on)
{
    labObserve_ = on;
    applyConfigs();
}

bool App::openPad(bool open)
{
    if (open) {
        bool ok = pad_.open(TestPad::Mode::Windowed);
        applyConfigs();
        return ok;
    }
    pad_.close();
    applyConfigs();
    return true;
}

void App::setAcceptBenchTag(bool on)
{
    acceptBenchTag_ = on;
    applyConfigs();
}

void App::invoke(const std::function<void()>& fn)
{
    if (!uiHwnd_ || GetWindowThreadProcessId(uiHwnd_, nullptr) == GetCurrentThreadId()) {
        fn();
        return;
    }
    SendMessageW(uiHwnd_, WM_APP_INVOKE, 0, reinterpret_cast<LPARAM>(&fn));
}

// ---------------------------------------------------------------- TriggerSink (trigger thread)

void App::onTriggerDown(int64_t qpc) { engine_.triggerDown(qpc); }
void App::onTriggerUp(int64_t qpc) { engine_.triggerUp(qpc); }

void App::onEmergency()
{
    engine_.emergency();
    if (uiHwnd_) PostMessageW(uiHwnd_, WM_APP_ENGINE, 1, 0);
}

void App::onCaptured(const KeyChord& chord)
{
    {
        std::lock_guard lk(capMu_);
        captured_ = chord;
        capturedValid_ = true;
    }
    if (uiHwnd_) PostMessageW(uiHwnd_, WM_APP_CAPTURED, 0, 0);
}

void App::onGateChanged()
{
    if (uiHwnd_) PostMessageW(uiHwnd_, WM_APP_GATE, 0, 0);
}

} // namespace infclick
