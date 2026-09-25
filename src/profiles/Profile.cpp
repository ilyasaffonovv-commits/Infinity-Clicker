#include "profiles/Profile.h"

#include "core/I18n.h"
#include "core/Log.h"
#include "core/Paths.h"
#include "core/Str.h"

#include <algorithm>
#include <climits>

namespace infclick {

namespace {

const char* modeKey(TriggerMode m)
{
    switch (m) {
    case TriggerMode::Hold: return "hold";
    case TriggerMode::Toggle: return "toggle";
    case TriggerMode::Burst: return "burst";
    case TriggerMode::Count: return "count";
    case TriggerMode::Duration: return "duration";
    }
    return "toggle";
}

TriggerMode parseMode(const std::string& s)
{
    if (s == "hold") return TriggerMode::Hold;
    if (s == "burst") return TriggerMode::Burst;
    if (s == "count") return TriggerMode::Count;
    if (s == "duration") return TriggerMode::Duration;
    return TriggerMode::Toggle;
}

const char* precisionKey(Precision p)
{
    switch (p) {
    case Precision::Eco: return "eco";
    case Precision::Standard: return "standard";
    case Precision::Ultra: return "ultra";
    }
    return "standard";
}

Precision parsePrecision(const std::string& s)
{
    if (s == "eco") return Precision::Eco;
    if (s == "ultra") return Precision::Ultra;
    return Precision::Standard;
}

const char* prioKey(power::ThreadPrio p)
{
    switch (p) {
    case power::ThreadPrio::Normal: return "normal";
    case power::ThreadPrio::AboveNormal: return "above_normal";
    case power::ThreadPrio::Highest: return "highest";
    case power::ThreadPrio::TimeCritical: return "time_critical";
    case power::ThreadPrio::Mmcss: return "mmcss";
    }
    return "highest";
}

power::ThreadPrio parsePrio(const std::string& s)
{
    if (s == "normal") return power::ThreadPrio::Normal;
    if (s == "above_normal") return power::ThreadPrio::AboveNormal;
    if (s == "time_critical") return power::ThreadPrio::TimeCritical;
    if (s == "mmcss") return power::ThreadPrio::Mmcss;
    return power::ThreadPrio::Highest;
}

const char* rateKey(RateUnit u)
{
    switch (u) {
    case RateUnit::Cps: return "cps";
    case RateUnit::IntervalMs: return "interval_ms";
    case RateUnit::IntervalUs: return "interval_us";
    }
    return "cps";
}

RateUnit parseRate(const std::string& s)
{
    if (s == "interval_ms") return RateUnit::IntervalMs;
    if (s == "interval_us") return RateUnit::IntervalUs;
    return RateUnit::Cps;
}

} // namespace

json::Value Profile::toJson() const
{
    json::Value v = json::Value::object();
    v.set("name", name);
    json::Value t = json::Value::object();
    t.set("chord", trigger.toJson());
    t.set("suppress", suppressTrigger);
    t.set("mouseBackend", mouseBackend == TriggerBackend::RawInput ? "rawinput" : "hook");
    v.set("trigger", t);
    v.set("action", action.toJson());
    json::Value m = json::Value::object();
    m.set("type", modeKey(mode.mode));
    m.set("burstCount", mode.burstCount);
    m.set("fixedCount", mode.fixedCount);
    m.set("durationMs", mode.durationMs);
    v.set("mode", m);
    json::Value s = json::Value::object();
    s.set("unit", rateKey(rateUnit));
    s.set("cps", cps);
    s.set("max", maxMode);
    s.set("maxBatch", maxBatch);
    s.set("maxBackpressure", maxBackpressure);
    s.set("maxInflight", maxInflight);
    v.set("speed", s);
    json::Value q = json::Value::object();
    q.set("precision", precisionKey(precision));
    q.set("threadPriority", prioKey(priority));
    q.set("latePolicy", latePolicy == LatePolicy::CatchUp ? "catchup" : "skip");
    v.set("timing", q);
    json::Value f = json::Value::object();
    f.set("enabled", filterEnabled);
    f.set("process", targetProcess);
    v.set("targetApp", f);
    return v;
}

Profile Profile::fromJson(const json::Value& v)
{
    Profile p;
    p.name = v.getString("name", "Profile");
    const json::Value& t = v["trigger"];
    if (const json::Value* c = t.find("chord")) p.trigger = KeyChord::fromJson(*c);
    p.suppressTrigger = t.getBool("suppress", false);
    p.mouseBackend = t.getString("mouseBackend", "hook") == "rawinput" ? TriggerBackend::RawInput : TriggerBackend::Hook;
    if (const json::Value* a = v.find("action")) p.action = InputAction::fromJson(*a);
    const json::Value& m = v["mode"];
    p.mode.mode = parseMode(m.getString("type", "toggle"));
    p.mode.burstCount = uint64_t(std::clamp<int64_t>(m.getInt("burstCount", 10), 1, 1'000'000'000));
    p.mode.fixedCount = uint64_t(std::clamp<int64_t>(m.getInt("fixedCount", 100), 1, 1'000'000'000'000));
    p.mode.durationMs = uint32_t(std::clamp<int64_t>(m.getInt("durationMs", 1000), 1, 86'400'000));
    const json::Value& s = v["speed"];
    p.rateUnit = parseRate(s.getString("unit", "cps"));
    p.cps = std::clamp(s.getNumber("cps", 20.0), 0.01, 1'000'000.0);
    p.maxMode = s.getBool("max", false);
    p.maxBatch = uint32_t(std::clamp<int64_t>(s.getInt("maxBatch", 8), 1, 256));
    p.maxBackpressure = s.getBool("maxBackpressure", true);
    p.maxInflight = uint32_t(std::clamp<int64_t>(s.getInt("maxInflight", 2000), 16, 1'000'000));
    const json::Value& q = v["timing"];
    p.precision = parsePrecision(q.getString("precision", "standard"));
    p.priority = parsePrio(q.getString("threadPriority", "highest"));
    p.latePolicy = q.getString("latePolicy", "catchup") == "skip" ? LatePolicy::Skip : LatePolicy::CatchUp;
    const json::Value& f = v["targetApp"];
    p.filterEnabled = f.getBool("enabled", false);
    p.targetProcess = f.getString("process", "");
    return p;
}

json::Value AppSettings::toJson() const
{
    json::Value v = json::Value::object();
    v.set("activeProfile", activeProfile);
    v.set("emergencyStop", emergency.toJson());
    v.set("armOnLaunch", armOnLaunch);
    v.set("startMinimized", startMinimized);
    v.set("minimizeToTray", minimizeToTray);
    v.set("closeToTray", closeToTray);
    v.set("pauseOnOwnWindow", pauseOnOwnWindow);
    v.set("acceptInjectedTriggers", acceptInjectedTriggers);
    v.set("fileLogging", fileLogging);
    v.set("debugLogging", debugLogging);
    v.set("guardian", guardian);
    v.set("alwaysOnTop", alwaysOnTop);
    v.set("language", language);
    v.set("uiLayout", uiLayout);
    json::Value w = json::Value::object();
    w.set("x", windowX);
    w.set("y", windowY);
    w.set("w", windowW);
    w.set("h", windowH);
    v.set("window", w);
    return v;
}

AppSettings AppSettings::fromJson(const json::Value& v)
{
    AppSettings s;
    s.activeProfile = v.getString("activeProfile", s.activeProfile);
    if (const json::Value* e = v.find("emergencyStop")) {
        KeyChord c = KeyChord::fromJson(*e);
        if (c.valid()) s.emergency = c;
    }
    s.armOnLaunch = v.getBool("armOnLaunch", s.armOnLaunch);
    s.startMinimized = v.getBool("startMinimized", s.startMinimized);
    s.minimizeToTray = v.getBool("minimizeToTray", s.minimizeToTray);
    s.closeToTray = v.getBool("closeToTray", s.closeToTray);
    s.pauseOnOwnWindow = v.getBool("pauseOnOwnWindow", s.pauseOnOwnWindow);
    s.acceptInjectedTriggers = v.getBool("acceptInjectedTriggers", s.acceptInjectedTriggers);
    s.fileLogging = v.getBool("fileLogging", s.fileLogging);
    s.debugLogging = v.getBool("debugLogging", s.debugLogging);
    s.guardian = v.getBool("guardian", s.guardian);
    s.alwaysOnTop = v.getBool("alwaysOnTop", s.alwaysOnTop);
    const int layout = int(v.getInt("uiLayout", 1));
    s.language = v.getString("language", "auto");
    if (s.language != "en" && s.language != "ru") s.language = "auto";
    const json::Value& w = v["window"];
    s.windowX = int(w.getInt("x", INT32_MIN));
    s.windowY = int(w.getInt("y", INT32_MIN));
    // Window sizes saved by an older layout would open the compact UI far too large.
    if (layout >= s.uiLayout) {
        s.windowW = int(std::clamp<int64_t>(w.getInt("w", 720), 620, 4000));
        s.windowH = int(std::clamp<int64_t>(w.getInt("h", 552), 480, 4000));
    }
    return s;
}

std::vector<Profile> ProfileStore::defaults()
{
    std::vector<Profile> v;
    {
        Profile p;
        p.name = tr("Default");
        p.trigger = KeyChord::mouse(MouseButton::X2); // "Mouse Button 5"
        p.mode.mode = TriggerMode::Hold;
        p.cps = 20;
        p.action.downTimeUs = 10'000; // long enough for games that poll the button once per frame
        v.push_back(p);
    }
    {
        Profile p;
        p.name = "Minecraft";
        p.trigger = KeyChord::mouse(MouseButton::X1);
        p.suppressTrigger = true;
        p.mode.mode = TriggerMode::Hold;
        p.cps = 16;
        p.action.downTimeUs = 12'000;
        p.filterEnabled = true;
        p.targetProcess = "javaw.exe; java.exe";
        v.push_back(p);
    }
    {
        Profile p;
        p.name = tr("Browser");
        p.trigger = KeyChord::key(VK_F8);
        p.mode.mode = TriggerMode::Toggle;
        p.cps = 10;
        p.action.downTimeUs = 5'000;
        p.precision = Precision::Eco;
        v.push_back(p);
    }
    {
        Profile p;
        p.name = tr("Testing");
        p.trigger = KeyChord::key(VK_F7);
        p.mode.mode = TriggerMode::Burst;
        p.mode.burstCount = 100;
        p.cps = 100;
        p.action.downTimeUs = 0;
        v.push_back(p);
    }
    {
        Profile p;
        p.name = tr("Fast");
        p.trigger = KeyChord::key(VK_F9);
        p.mode.mode = TriggerMode::Toggle;
        p.cps = 1000;
        p.action.downTimeUs = 0;
        p.precision = Precision::Ultra;
        v.push_back(p);
    }
    {
        Profile p;
        p.name = tr("Custom");
        p.trigger = KeyChord::key(VK_F10);
        p.mode.mode = TriggerMode::Hold;
        p.action.chord = KeyChord::key('E');
        p.action.downTimeUs = 15'000;
        p.cps = 25;
        v.push_back(p);
    }
    return v;
}

bool ProfileStore::load(const std::wstring& dir)
{
    dir_ = dir;
    paths::ensureDir(dir);
    std::string text;
    bool ok = true;
    if (paths::readFile(dir + L"\\settings.json", text)) {
        auto r = json::parse(text);
        if (r.ok) settings_ = AppSettings::fromJson(r.value);
        else {
            TLOG_W("settings.json is corrupt (%s at %zu) - using defaults", r.error.c_str(), r.errorOffset);
            ok = false;
        }
    }
    profiles_.clear();
    if (paths::readFile(dir + L"\\profiles.json", text)) {
        auto r = json::parse(text);
        if (r.ok) {
            for (const auto& e : r.value["profiles"].items()) profiles_.push_back(Profile::fromJson(e));
        } else {
            TLOG_W("profiles.json is corrupt (%s at %zu) - keeping a backup and using defaults", r.error.c_str(),
                   r.errorOffset);
            CopyFileW((dir + L"\\profiles.json").c_str(), (dir + L"\\profiles.corrupt.json").c_str(), FALSE);
            ok = false;
        }
    }
    if (profiles_.empty()) profiles_ = defaults();
    // Unique, non-empty names.
    for (size_t i = 0; i < profiles_.size(); ++i) {
        if (trim(profiles_[i].name).empty()) profiles_[i].name = "Profile";
        for (size_t j = 0; j < i; ++j)
            if (profiles_[j].name == profiles_[i].name) profiles_[i].name = uniqueName(profiles_[i].name);
    }
    if (std::none_of(profiles_.begin(), profiles_.end(),
                     [&](const Profile& p) { return p.name == settings_.activeProfile; }))
        settings_.activeProfile = profiles_.front().name;
    return ok;
}

bool ProfileStore::save() const
{
    json::Value root = json::Value::object();
    root.set("format", "infclick-profiles");
    root.set("version", 1);
    json::Value arr = json::Value::array();
    for (const auto& p : profiles_) arr.push(p.toJson());
    root.set("profiles", arr);
    bool ok = paths::writeFileAtomic(dir_ + L"\\profiles.json", json::stringify(root));
    ok = paths::writeFileAtomic(dir_ + L"\\settings.json", json::stringify(settings_.toJson())) && ok;
    if (!ok) {
        lastError_ = "could not write settings to the data folder";
        TLOG_E("profiles: save failed (error %lu)", GetLastError());
    }
    return ok;
}

int ProfileStore::activeIndex() const
{
    for (size_t i = 0; i < profiles_.size(); ++i)
        if (profiles_[i].name == settings_.activeProfile) return int(i);
    return profiles_.empty() ? -1 : 0;
}

Profile& ProfileStore::active()
{
    int i = activeIndex();
    if (i < 0) {
        profiles_ = defaults();
        i = 0;
    }
    return profiles_[size_t(i)];
}

bool ProfileStore::setActive(const std::string& name)
{
    for (const auto& p : profiles_)
        if (p.name == name) {
            settings_.activeProfile = name;
            return true;
        }
    return false;
}

std::string ProfileStore::uniqueName(const std::string& base) const
{
    auto exists = [&](const std::string& n) {
        return std::any_of(profiles_.begin(), profiles_.end(), [&](const Profile& p) { return p.name == n; });
    };
    if (!exists(base)) return base;
    for (int i = 2; i < 10000; ++i) {
        std::string n = strFormat("%s (%d)", base.c_str(), i);
        if (!exists(n)) return n;
    }
    return base + " *";
}

EngineConfig makeEngineConfig(const Profile& p)
{
    EngineConfig c;
    c.action = p.action;
    c.mode = p.mode;
    c.maxMode = p.maxMode;
    c.cps = std::clamp(p.cps, 0.01, 1'000'000.0);
    c.precision = p.precision;
    c.priority = p.priority;
    c.latePolicy = p.latePolicy;
    c.maxBatchActions = p.maxBatch;
    c.maxInflight = p.maxBackpressure ? p.maxInflight : 0;
    return c;
}

} // namespace infclick
