#include "input/InputAction.h"

#include "core/Clock.h"

#include <algorithm>

namespace infclick {

INPUT makeMouseInput(MouseButton b, bool down, int32_t wheelDelta, ULONG_PTR tag)
{
    INPUT in{};
    in.type = INPUT_MOUSE;
    in.mi.dwExtraInfo = tag;
    switch (b) {
    case MouseButton::Left: in.mi.dwFlags = down ? MOUSEEVENTF_LEFTDOWN : MOUSEEVENTF_LEFTUP; break;
    case MouseButton::Right: in.mi.dwFlags = down ? MOUSEEVENTF_RIGHTDOWN : MOUSEEVENTF_RIGHTUP; break;
    case MouseButton::Middle: in.mi.dwFlags = down ? MOUSEEVENTF_MIDDLEDOWN : MOUSEEVENTF_MIDDLEUP; break;
    case MouseButton::X1:
        in.mi.dwFlags = down ? MOUSEEVENTF_XDOWN : MOUSEEVENTF_XUP;
        in.mi.mouseData = XBUTTON1;
        break;
    case MouseButton::X2:
        in.mi.dwFlags = down ? MOUSEEVENTF_XDOWN : MOUSEEVENTF_XUP;
        in.mi.mouseData = XBUTTON2;
        break;
    case MouseButton::WheelUp:
        in.mi.dwFlags = MOUSEEVENTF_WHEEL;
        in.mi.mouseData = DWORD(wheelDelta);
        break;
    case MouseButton::WheelDown:
        in.mi.dwFlags = MOUSEEVENTF_WHEEL;
        in.mi.mouseData = DWORD(-wheelDelta);
        break;
    case MouseButton::WheelRight:
        in.mi.dwFlags = MOUSEEVENTF_HWHEEL;
        in.mi.mouseData = DWORD(wheelDelta);
        break;
    case MouseButton::WheelLeft:
        in.mi.dwFlags = MOUSEEVENTF_HWHEEL;
        in.mi.mouseData = DWORD(-wheelDelta);
        break;
    default: break;
    }
    return in;
}

INPUT makeKeyInput(uint16_t vk, uint16_t scan, bool extended, bool up, KeyInjectMode mode, ULONG_PTR tag)
{
    INPUT in{};
    in.type = INPUT_KEYBOARD;
    in.ki.dwExtraInfo = tag;
    // wVk is always filled (ignored by Windows in scan-code mode, but lets our
    // own bookkeeping know which VK is being held).
    in.ki.wVk = vk;
    in.ki.wScan = scan;
    DWORD f = 0;
    if (up) f |= KEYEVENTF_KEYUP;
    if (extended) f |= KEYEVENTF_EXTENDEDKEY;
    // Scan-code injection is what DirectInput/raw-input games read; fall back
    // to VK injection for keys without a single make code (Pause, some media keys).
    if (mode == KeyInjectMode::ScanCode && scan != 0) f |= KEYEVENTF_SCANCODE;
    in.ki.dwFlags = f;
    return in;
}

namespace {

struct ModKey {
    uint8_t bit;
    uint16_t vk;
};
constexpr ModKey kModKeys[] = {{ModCtrl, VK_LCONTROL}, {ModShift, VK_LSHIFT}, {ModAlt, VK_LMENU}, {ModWin, VK_LWIN}};

void appendMods(std::vector<INPUT>& v, uint8_t mods, uint16_t mainVk, bool up, KeyInjectMode mode, ULONG_PTR tag)
{
    const uint8_t own = modBitForVk(mainVk);
    auto one = [&](const ModKey& m) {
        if (!(mods & m.bit) || (own & m.bit)) return;
        bool ext = false;
        uint16_t sc = vkToScan(m.vk, &ext);
        v.push_back(makeKeyInput(m.vk, sc, ext, up, mode, tag));
    };
    if (!up) {
        for (const auto& m : kModKeys) one(m);
    } else {
        for (int i = int(std::size(kModKeys)) - 1; i >= 0; --i) one(kModKeys[i]);
    }
}

} // namespace

ActionProgram compileAction(const InputAction& a, ULONG_PTR tag)
{
    ActionProgram p;
    const KeyChord& c = a.chord;
    if (!c.valid()) {
        p.error = "no action key/button assigned";
        return p;
    }
    const uint32_t repeat = std::clamp<uint32_t>(a.repeat, 1, 3);
    const int64_t hold = clk::usToTicks(a.downTimeUs);
    const int64_t gap = clk::usToTicks(a.repeatGapUs);
    const bool wheel = c.isWheel();

    uint16_t scan = c.scan;
    bool ext = c.extended;
    if (c.isKeyboard() && scan == 0) scan = vkToScan(c.code, &ext);
    if (c.isKeyboard() && c.code == VK_RETURN) ext = c.extended; // Enter vs Numpad Enter

    auto press = [&](bool down) -> INPUT {
        if (c.isMouse()) return makeMouseInput(c.button(), down, a.wheelDelta, tag);
        // Numpad digits/decimal share scan codes with Home/End/arrows...: with NumLock
        // OFF, scan 0x47 *is* Home. Inject them by VK (scan still filled for raw-input readers).
        const bool numpadVk = (c.code >= VK_NUMPAD0 && c.code <= VK_NUMPAD9) || c.code == VK_DECIMAL;
        return makeKeyInput(c.code, scan, ext, !down, numpadVk ? KeyInjectMode::VirtualKey : a.keyMode, tag);
    };

    // Build a timeline of (time, INPUT) and then group equal times into steps.
    struct Ev {
        int64_t t;
        INPUT in;
    };
    std::vector<Ev> evs;
    std::vector<INPUT> tmp;
    const uint16_t mainVk = c.isKeyboard() ? c.code : 0;

    int64_t t = 0;
    tmp.clear();
    appendMods(tmp, c.mods, mainVk, false, a.keyMode, tag);
    for (const INPUT& in : tmp) evs.push_back({0, in});

    for (uint32_t r = 0; r < repeat; ++r) {
        if (wheel) {
            evs.push_back({t, press(true)});
        } else {
            evs.push_back({t, press(true)});
            t += hold;
            evs.push_back({t, press(false)});
        }
        if (r + 1 < repeat) t += gap;
    }
    tmp.clear();
    appendMods(tmp, c.mods, mainVk, true, a.keyMode, tag);
    for (const INPUT& in : tmp) evs.push_back({t, in});

    // Group by timestamp (stable order preserved).
    for (const Ev& e : evs) {
        if (p.steps.empty() || p.steps.back().offsetTicks != e.t || p.steps.back().count >= kMaxStepInputs) {
            ActionStep s;
            s.offsetTicks = e.t;
            p.steps.push_back(s);
        }
        ActionStep& s = p.steps.back();
        s.inputs[s.count++] = e.in;
    }
    p.spanTicks = p.steps.empty() ? 0 : p.steps.back().offsetTicks;
    for (const auto& s : p.steps) {
        p.inputsPerAction += s.count;
        p.flat.insert(p.flat.end(), s.inputs, s.inputs + s.count);
    }
    return p;
}

json::Value InputAction::toJson() const
{
    json::Value v = json::Value::object();
    v.set("chord", chord.toJson());
    v.set("downTimeUs", downTimeUs);
    v.set("repeat", repeat);
    v.set("repeatGapUs", repeatGapUs);
    v.set("wheelDelta", wheelDelta);
    v.set("keyMode", keyMode == KeyInjectMode::ScanCode ? "scancode" : "vk");
    return v;
}

InputAction InputAction::fromJson(const json::Value& v)
{
    InputAction a;
    if (const json::Value* c = v.find("chord")) a.chord = KeyChord::fromJson(*c);
    a.downTimeUs = uint32_t(std::clamp<int64_t>(v.getInt("downTimeUs", 0), 0, 10'000'000));
    a.repeat = uint32_t(std::clamp<int64_t>(v.getInt("repeat", 1), 1, 3));
    a.repeatGapUs = uint32_t(std::clamp<int64_t>(v.getInt("repeatGapUs", 0), 0, 10'000'000));
    a.wheelDelta = int32_t(std::clamp<int64_t>(v.getInt("wheelDelta", 120), 1, 1200));
    a.keyMode = v.getString("keyMode", "scancode") == "vk" ? KeyInjectMode::VirtualKey : KeyInjectMode::ScanCode;
    return a;
}

} // namespace infclick
