#pragma once
// Profiles (per-use-case configuration) and global application settings.
// Stored as human-readable JSON in the data directory:
//   settings.json  - global settings + active profile name
//   profiles.json  - array of profiles
#include "core/Json.h"
#include "input/InputAction.h"
#include "input/KeyChord.h"
#include "platform/win/Power.h"
#include "scheduler/Engine.h"
#include "scheduler/Modes.h"
#include "scheduler/Precision.h"
#include "trigger/TriggerEngine.h"

#include <string>
#include <vector>

namespace infclick {

enum class RateUnit : uint8_t { Cps = 0, IntervalMs = 1, IntervalUs = 2 };

struct Profile {
    std::string name = "Default";

    // Trigger
    KeyChord trigger = KeyChord::key(VK_F6);
    bool suppressTrigger = false;
    TriggerBackend mouseBackend = TriggerBackend::Hook;

    // Output
    InputAction action;
    ModeParams mode;

    // Speed
    RateUnit rateUnit = RateUnit::Cps;
    double cps = 20.0;
    bool maxMode = false;
    uint32_t maxBatch = 8;
    bool maxBackpressure = true;
    uint32_t maxInflight = 2000;

    // Timing quality
    Precision precision = Precision::Standard;
    power::ThreadPrio priority = power::ThreadPrio::Highest;
    LatePolicy latePolicy = LatePolicy::CatchUp;

    // Active window filter
    bool filterEnabled = false;
    std::string targetProcess; // "javaw.exe" (';' separated list allowed)

    json::Value toJson() const;
    static Profile fromJson(const json::Value& v);
};

struct AppSettings {
    std::string activeProfile = "Default";
    KeyChord emergency = KeyChord::key(VK_F12, ModCtrl | ModShift);
    bool armOnLaunch = true;
    bool startMinimized = false;
    bool minimizeToTray = true;
    bool closeToTray = false;
    bool pauseOnOwnWindow = true;
    bool acceptInjectedTriggers = false;
    bool fileLogging = true;
    bool debugLogging = false;
    bool guardian = true;
    bool alwaysOnTop = false;
    std::string language = "auto"; // "auto" (Windows UI language) | "en" | "ru"
    int uiLayout = 2;              // bumped when the window layout changes; older saved window sizes are ignored
    int windowX = INT32_MIN, windowY = INT32_MIN, windowW = 720, windowH = 552;

    json::Value toJson() const;
    static AppSettings fromJson(const json::Value& v);
};

class ProfileStore {
public:
    // Loads from `dir`; creates defaults when files are missing or corrupt.
    bool load(const std::wstring& dir);
    bool save() const;

    std::vector<Profile>& profiles() { return profiles_; }
    const std::vector<Profile>& profiles() const { return profiles_; }
    AppSettings& settings() { return settings_; }
    const AppSettings& settings() const { return settings_; }

    int activeIndex() const;
    Profile& active();
    bool setActive(const std::string& name);
    std::string uniqueName(const std::string& base) const;

    static std::vector<Profile> defaults();
    const std::wstring& dir() const { return dir_; }
    std::string lastError() const { return lastError_; }

private:
    std::wstring dir_;
    std::vector<Profile> profiles_;
    AppSettings settings_;
    mutable std::string lastError_;
};

// Engine / trigger configuration derived from a profile + settings.
EngineConfig makeEngineConfig(const Profile& p);

} // namespace infclick
