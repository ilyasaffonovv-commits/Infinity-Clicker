// Unit tests for the OS-independent logic (no input is injected here).
// Run: build\...\bin\infclick_tests.exe   (or `ctest`)
#include "core/Clock.h"
#include "core/I18n.h"
#include "core/Json.h"
#include "core/Paths.h"
#include "input/InputAction.h"
#include "input/KeyChord.h"
#include "platform/win/Autostart.h"
#include "profiles/Profile.h"
#include "scheduler/Modes.h"
#include "telemetry/Stats.h"

#include <cmath>
#include <cstdio>
#include <string>

using namespace infclick;

static int g_fail = 0, g_pass = 0;
#define CHECK(cond)                                                                 \
    do {                                                                            \
        if (cond) ++g_pass;                                                         \
        else {                                                                      \
            ++g_fail;                                                               \
            std::printf("  FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond);           \
        }                                                                           \
    } while (0)

static void testJson()
{
    std::printf("json\n");
    auto r = json::parse(R"({"a":1,"b":[true,false,null],"s":"x\"yé😀","n":-2.5e3, // c
                           "o":{"k":"v",},})");
    CHECK(r.ok);
    CHECK(r.value.getInt("a", 0) == 1);
    CHECK(r.value["b"].size() == 3);
    CHECK(r.value["b"].items()[0].asBool() == true);
    CHECK(r.value.getNumber("n", 0) == -2500.0);
    CHECK(r.value["o"].getString("k", "") == "v");
    CHECK(r.value.getString("s", "") == "x\"y\xC3\xA9\xF0\x9F\x98\x80");
    std::string out = json::stringify(r.value);
    auto r2 = json::parse(out);
    CHECK(r2.ok && json::stringify(r2.value) == out);
    CHECK(!json::parse("{\"a\":}").ok);
    CHECK(!json::parse("[1,2").ok);
    // copy-on-write
    json::Value a = json::Value::object();
    a.set("x", 1);
    json::Value b = a;
    b.set("x", 2);
    CHECK(a.getInt("x", 0) == 1 && b.getInt("x", 0) == 2);
}

static void testKeys()
{
    std::printf("keys\n");
    CHECK(KeyChord::mouse(MouseButton::X1).name() == "Mouse Button 4");
    CHECK(KeyChord::mouse(MouseButton::X2).name() == "Mouse Button 5");
    CHECK(KeyChord::key(VK_F6).name() == "F6");
    CHECK(KeyChord::key(VK_F24).name() == "F24");
    CHECK(KeyChord::key(VK_F12, ModCtrl | ModShift).name() == "Ctrl + Shift + F12");
    CHECK(KeyChord::key(VK_RCONTROL).name() == "Right Ctrl");
    CHECK(KeyChord::key(VK_RCONTROL, ModCtrl).name() == "Right Ctrl"); // no "Ctrl + Right Ctrl"
    KeyChord ne = KeyChord::key(VK_RETURN);
    ne.extended = true;
    CHECK(ne.name() == "Numpad Enter");
    CHECK(KeyChord::key(VK_RETURN).name() == "Enter");
    CHECK(KeyChord::key(VK_NUMPAD5).name() == "Numpad 5");
    CHECK(KeyChord::key(VK_LEFT).extended);
    CHECK(!KeyChord::key('A').extended);
    CHECK(KeyChord::key('A').scan == 0x1E);
    KeyChord k = KeyChord::key(VK_F12, ModCtrl);
    CHECK(KeyChord::fromJson(k.toJson()) == k);
    CHECK(keyList().size() > 100);
}

static void testCompile()
{
    std::printf("action compiler\n");
    InputAction a;
    a.chord = KeyChord::mouse(MouseButton::Left);
    a.downTimeUs = 0;
    ActionProgram p = compileAction(a);
    CHECK(p.ok() && p.steps.size() == 1 && p.steps[0].count == 2);
    CHECK(p.steps[0].inputs[0].mi.dwFlags == MOUSEEVENTF_LEFTDOWN);
    CHECK(p.steps[0].inputs[1].mi.dwFlags == MOUSEEVENTF_LEFTUP);
    CHECK(p.steps[0].inputs[0].mi.dwExtraInfo == kTagInfClick);

    a.downTimeUs = 1000;
    p = compileAction(a);
    CHECK(p.steps.size() == 2 && p.steps[1].offsetTicks == clk::usToTicks(1000));

    // Ctrl+Shift+K, hold 0: mods down, K down, K up, mods up in ONE batch, correct order.
    a.chord = KeyChord::key('K', ModCtrl | ModShift);
    a.downTimeUs = 0;
    p = compileAction(a);
    CHECK(p.steps.size() == 1 && p.steps[0].count == 6);
    CHECK(p.steps[0].inputs[0].ki.wVk == VK_LCONTROL && !(p.steps[0].inputs[0].ki.dwFlags & KEYEVENTF_KEYUP));
    CHECK(p.steps[0].inputs[1].ki.wVk == VK_LSHIFT);
    CHECK(p.steps[0].inputs[2].ki.wVk == 'K' && (p.steps[0].inputs[2].ki.dwFlags & KEYEVENTF_SCANCODE));
    CHECK(p.steps[0].inputs[3].ki.wVk == 'K' && (p.steps[0].inputs[3].ki.dwFlags & KEYEVENTF_KEYUP));
    CHECK(p.steps[0].inputs[4].ki.wVk == VK_LSHIFT && (p.steps[0].inputs[4].ki.dwFlags & KEYEVENTF_KEYUP));
    CHECK(p.steps[0].inputs[5].ki.wVk == VK_LCONTROL);

    // Double click with gap.
    a.chord = KeyChord::mouse(MouseButton::Right);
    a.repeat = 2;
    a.downTimeUs = 500;
    a.repeatGapUs = 1000;
    p = compileAction(a);
    CHECK(p.inputsPerAction == 4 && p.steps.size() == 4);
    CHECK(p.spanTicks == clk::usToTicks(500) * 2 + clk::usToTicks(1000));

    // Wheel: single event, X buttons carry mouseData.
    a = InputAction{};
    a.chord = KeyChord::mouse(MouseButton::WheelDown);
    p = compileAction(a);
    CHECK(p.inputsPerAction == 1 && int(p.steps[0].inputs[0].mi.mouseData) == -120);
    a.chord = KeyChord::mouse(MouseButton::X2);
    p = compileAction(a);
    CHECK(p.steps[0].inputs[0].mi.mouseData == XBUTTON2 && p.steps[0].inputs[0].mi.dwFlags == MOUSEEVENTF_XDOWN);
    // Extended key flag on arrows.
    a.chord = KeyChord::key(VK_UP);
    p = compileAction(a);
    CHECK(p.steps[0].inputs[0].ki.dwFlags & KEYEVENTF_EXTENDEDKEY);
}

static void testModes()
{
    std::printf("modes\n");
    using K = ModeDecision::Kind;
    ModeParams m;
    m.mode = TriggerMode::Hold;
    CHECK(decideOnTriggerDown(m, false).kind == K::Start);
    CHECK(decideOnTriggerUp(m, true).kind == K::Stop);
    m.mode = TriggerMode::Toggle;
    CHECK(decideOnTriggerDown(m, false).kind == K::Start);
    CHECK(decideOnTriggerDown(m, true).kind == K::Stop);
    CHECK(decideOnTriggerUp(m, true).kind == K::None);
    m.mode = TriggerMode::Burst;
    m.burstCount = 7;
    auto d = decideOnTriggerDown(m, false);
    CHECK(d.kind == K::Start && d.limits.maxActions == 7);
    d = decideOnTriggerDown(m, true);
    CHECK(d.kind == K::Extend && d.extendBy == 7);
    m.mode = TriggerMode::Count;
    m.fixedCount = 50;
    CHECK(decideOnTriggerDown(m, false).limits.maxActions == 50);
    CHECK(decideOnTriggerDown(m, true).kind == K::Stop);
    m.mode = TriggerMode::Duration;
    m.durationMs = 500;
    CHECK(decideOnTriggerDown(m, false).limits.durationMs == 500);
}

static void testTimeline()
{
    std::printf("timeline (zero drift)\n");
    // 3 CPS at 10 MHz = 3333333.33 ticks: rounding must never accumulate.
    Timeline t;
    t.anchor(1000, 10'000'000.0 / 3.0);
    CHECK(t.slot(3) == 1000 + 10'000'000);
    CHECK(t.slot(3'000'000) == 1000 + 10'000'000'000'000LL);
    // Simulated "slow action execution": every action is 30% late - grid unaffected.
    Timeline g;
    g.anchor(0, 1000.0);
    for (int i = 0; i < 1000; ++i) {
        int64_t fire = g.next() + 300;
        (void)fire;
        g.k++;
    }
    CHECK(g.next() == 1'000'000);
    // Skip policy: we wake 3.5 periods late -> skip 3, keep phase.
    Timeline s;
    s.anchor(0, 1000.0);
    uint64_t skipped = s.skipLate(3500);
    CHECK(skipped == 3 && s.k == 3 && s.next() == 3000);
    CHECK(s.skipLate(3100) == 0); // less than a period late: fire now
    // Rate change keeps phase continuity.
    Timeline c;
    c.anchor(0, 1000.0);
    c.k = 5;
    c.changePeriod(500.0);
    CHECK(c.next() == 5000 && c.slot(1) == 5500);
}

static void testHistogram()
{
    std::printf("histogram\n");
    LogHistogram h;
    for (int i = 1; i <= 1000; ++i) h.add(uint64_t(i) * 1000); // 1..1000 us in ns
    const double p50 = double(h.percentile(0.5)) / 1000.0;
    const double p99 = double(h.percentile(0.99)) / 1000.0;
    CHECK(std::fabs(p50 - 500) / 500 < 0.07);
    CHECK(std::fabs(p99 - 990) / 990 < 0.07);
    for (uint64_t v : {0ull, 7ull, 8ull, 15ull, 16ull, 1000ull, 123456789ull, (1ull << 45)}) {
        int idx = LogHistogram::index(v);
        CHECK(idx >= 0 && idx < LogHistogram::kBuckets);
        if (v >= 8 && v < (1ull << 40)) {
            double mid = double(LogHistogram::bucketMid(idx));
            CHECK(std::fabs(mid - double(v)) / double(v) <= 0.0625 + 1e-9);
        }
    }
}

static void testProfiles()
{
    std::printf("profiles round-trip\n");
    wchar_t tmp[MAX_PATH];
    GetTempPathW(MAX_PATH, tmp);
    std::wstring dir = std::wstring(tmp) + L"infclick_unit_" + std::to_wstring(GetCurrentProcessId());
    ProfileStore a;
    a.load(dir); // creates defaults
    CHECK(a.profiles().size() >= 5);
    a.profiles()[1].cps = 123.5;
    a.profiles()[1].action.chord = KeyChord::key('Q', ModAlt);
    a.profiles()[1].mode.mode = TriggerMode::Duration;
    a.settings().activeProfile = a.profiles()[1].name;
    a.settings().emergency = KeyChord::key(VK_PAUSE);
    CHECK(a.save());
    ProfileStore b;
    CHECK(b.load(dir));
    CHECK(b.profiles().size() == a.profiles().size());
    CHECK(b.active().name == a.profiles()[1].name);
    CHECK(b.active().cps == 123.5);
    CHECK(b.active().action.chord == KeyChord::key('Q', ModAlt));
    CHECK(b.active().mode.mode == TriggerMode::Duration);
    CHECK(b.settings().emergency.code == VK_PAUSE);
    CHECK(b.uniqueName("Default") == "Default (2)");
    DeleteFileW((dir + L"\\profiles.json").c_str());
    DeleteFileW((dir + L"\\settings.json").c_str());
    RemoveDirectoryW(dir.c_str());
}


static void testFirstRunDefaults()
{
    std::printf("first-run defaults and layout gating\n");
    i18n::setLang(Lang::En);
    const Profile p = ProfileStore::defaults().front();
    CHECK(p.name == "Default");
    CHECK(p.trigger == KeyChord::mouse(MouseButton::X2)); // Mouse Button 5
    CHECK(p.action.chord == KeyChord::mouse(MouseButton::Left));
    CHECK(p.mode.mode == TriggerMode::Hold);
    CHECK(p.cps == 20.0 && !p.maxMode);
    CHECK(p.precision == Precision::Standard);
    CHECK(p.action.downTimeUs == 10'000);
    CHECK(!p.filterEnabled && !p.suppressTrigger);

    // A window size saved by an older layout is ignored; one saved by the current layout is kept.
    auto old = json::parse(R"({"window":{"x":10,"y":20,"w":640,"h":880}})");
    AppSettings a = AppSettings::fromJson(old.value);
    CHECK(a.windowW == 720 && a.windowH == 552 && a.windowX == 10);
    auto cur = json::parse(R"({"uiLayout":2,"window":{"x":10,"y":20,"w":800,"h":600}})");
    AppSettings b = AppSettings::fromJson(cur.value);
    CHECK(b.windowW == 800 && b.windowH == 600);
    // Language setting survives a round trip and rejects garbage.
    b.language = "ru";
    CHECK(AppSettings::fromJson(b.toJson()).language == "ru");
    auto bad = json::parse(R"({"language":"xx"})");
    CHECK(AppSettings::fromJson(bad.value).language == "auto");
}

static void testI18n()
{
    std::printf("i18n\n");
    i18n::setLang(Lang::En);
    CHECK(std::string(tr("STOP")) == "STOP");
    i18n::setLang(Lang::Ru);
    CHECK(std::string(tr("STOP")) == "СТОП");
    CHECK(std::string(tr("ARM")) == "ВКЛЮЧИТЬ");
    CHECK(std::string(tr("no such string in the table")) == "no such string in the table"); // falls back to English
    CHECK(KeyChord::mouse(MouseButton::X2).name() == "Кнопка мыши 5");
    CHECK(i18n::resolve("ru") == Lang::Ru && i18n::resolve("en") == Lang::En);
    i18n::setLang(Lang::En);
    CHECK(KeyChord::mouse(MouseButton::X2).name() == "Mouse Button 5");
}

static void testAutostart()
{
    std::printf("autostart (HKCU Run, test value name)\n");
    const wchar_t* name = L"InfinityClickerUnitTest";
    autostart::set(false, name); // clean slate
    CHECK(!autostart::isEnabled(name));
    CHECK(autostart::set(true, name));
    CHECK(autostart::isEnabled(name));
    CHECK(autostart::set(false, name));
    CHECK(!autostart::isEnabled(name));
    CHECK(autostart::set(false, name)); // removing twice is fine
}

int main()
{
    testJson();
    testKeys();
    testCompile();
    testModes();
    testTimeline();
    testHistogram();
    testProfiles();
    testFirstRunDefaults();
    testI18n();
    testAutostart();
    std::printf("\n%d passed, %d failed\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
