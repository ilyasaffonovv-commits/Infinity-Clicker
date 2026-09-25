#pragma once
// Application controller: owns the engine, trigger engine, test pad, profiles
// and settings, and keeps them consistent. UI-agnostic - the ImGui views and the
// autotest drive it through the same methods (always from the UI thread; other
// threads use invoke()).
#include "lab/TestPad.h"
#include "profiles/Profile.h"
#include "scheduler/Engine.h"
#include "telemetry/CpuMeter.h"
#include "trigger/TriggerEngine.h"

#include <windows.h>

#include <atomic>
#include <functional>
#include <mutex>
#include <string>

namespace infclick {

constexpr UINT WM_APP_ENGINE = WM_APP + 10;   // engine state changed
constexpr UINT WM_APP_CAPTURED = WM_APP + 11; // binding captured
constexpr UINT WM_APP_INVOKE = WM_APP + 12;   // run std::function* on the UI thread
constexpr UINT WM_APP_TRAY = WM_APP + 13;
constexpr UINT WM_APP_SHOW = WM_APP + 14;     // second instance asks us to show up
constexpr UINT WM_APP_GATE = WM_APP + 15;

enum class CaptureTarget : uint8_t { None, Trigger, Action, Emergency };

class App : public TriggerSink {
public:
    App();
    ~App() override;

    bool init();
    void setLanguageOverride(const std::string& code) { langOverride_ = code; } // "en" | "ru" | "auto"; call before init()
    void shutdown();

    // ---- UI-thread API
    ProfileStore& store() { return store_; }
    Profile& profile() { return store_.active(); }
    AppSettings& settings() { return store_.settings(); }
    void profileChanged();   // apply to engine + schedule save
    void settingsChanged();  // apply + schedule save
    void selectProfile(const std::string& name);
    void tick();             // periodic housekeeping (UI loop)
    void saveNow();

    void arm(bool on);
    bool armed() const { return engine_.armed(); }
    void startTest(RunLimits limits); // manual run into the test pad (lab)
    bool labRunActive() const { return labRunActive_; }
    void stop();
    void emergency();

    void beginCapture(CaptureTarget t);
    void cancelCapture();
    CaptureTarget capturing() const { return captureTarget_; }
    double captureSecondsLeft() const;
    bool applyCaptured(); // called on WM_APP_CAPTURED

    void setLabObserve(bool on);
    bool labObserve() const { return labObserve_; }
    bool openPad(bool open);

    void setUiWindow(HWND h);
    HWND uiWindow() const { return uiHwnd_; }
    void invoke(const std::function<void()>& fn); // run on the UI thread (blocking)

    Engine& engine() { return engine_; }
    TriggerEngine& trigger() { return trigger_; }
    TestPad& pad() { return pad_; }
    const CpuMeter& cpu() const { return cpu_; }
    const EngineSnapshot& snap() const { return snap_; }
    const std::string& statusMessage() const { return status_; }
    void setStatus(const std::string& s);
    bool elevated() const { return elevated_; }
    std::atomic<int64_t> uiHeartbeat{0};

    // TriggerSink (trigger thread)
    void onTriggerDown(int64_t qpc) override;
    void onTriggerUp(int64_t qpc) override;
    void onEmergency() override;
    void onCaptured(const KeyChord& chord) override;
    bool isArmed() const override { return engine_.armed(); }
    void onGateChanged() override;

    // Autotest hooks
    void setAcceptBenchTag(bool on);
    void setTestTarget(HWND h) { testTarget_ = h; applyConfigs(); }
    void setPadExempt(bool on) { padExempt_ = on; applyConfigs(); }

private:
    void applyConfigs();
    void healthCheck();

    ProfileStore store_;
    Engine engine_;
    TriggerEngine trigger_;
    TestPad pad_;
    CpuMeter cpu_;
    EngineSnapshot snap_{};
    HWND uiHwnd_ = nullptr;
    HeldShared* held_ = nullptr;

    bool dirty_ = false;
    int64_t saveAt_ = 0;
    bool labObserve_ = false;
    bool acceptBenchTag_ = false;
    HWND testTarget_ = nullptr;
    bool elevated_ = false;
    std::string langOverride_;
    bool labRunActive_ = false;
    bool padExempt_ = true;
    uint64_t labRunBase_ = 0;

    CaptureTarget captureTarget_ = CaptureTarget::None;
    int64_t captureDeadline_ = 0;
    std::mutex capMu_;
    KeyChord captured_;
    bool capturedValid_ = false;

    std::string status_;
    int64_t statusUntil_ = 0;
    int64_t lastHealth_ = 0;
    uint64_t healthAccepted_ = 0, healthObserved_ = 0;
};

} // namespace infclick
