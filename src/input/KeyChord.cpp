#include "input/KeyChord.h"

#include "core/I18n.h"
#include "core/Str.h"

#include <windows.h>

#include <iterator>

namespace infclick {

namespace {

struct NamedKey {
    uint16_t vk;
    bool ext;
    const char* name;
    const char* group;
};

// Fixed English names: stable across keyboard layouts and readable in profiles.
const NamedKey kNamed[] = {
    {VK_ESCAPE, false, "Esc", "Main"},
    {VK_TAB, false, "Tab", "Main"},
    {VK_CAPITAL, false, "Caps Lock", "Main"},
    {VK_SPACE, false, "Space", "Main"},
    {VK_RETURN, false, "Enter", "Main"},
    {VK_BACK, false, "Backspace", "Main"},
    {VK_LSHIFT, false, "Left Shift", "Modifiers"},
    {VK_RSHIFT, false, "Right Shift", "Modifiers"},
    {VK_LCONTROL, false, "Left Ctrl", "Modifiers"},
    {VK_RCONTROL, true, "Right Ctrl", "Modifiers"},
    {VK_LMENU, false, "Left Alt", "Modifiers"},
    {VK_RMENU, true, "Right Alt", "Modifiers"},
    {VK_LWIN, true, "Left Win", "Modifiers"},
    {VK_RWIN, true, "Right Win", "Modifiers"},
    {VK_SHIFT, false, "Shift", "Modifiers"},
    {VK_CONTROL, false, "Ctrl", "Modifiers"},
    {VK_MENU, false, "Alt", "Modifiers"},
    {VK_APPS, true, "Menu", "Modifiers"},
    {VK_INSERT, true, "Insert", "Navigation"},
    {VK_DELETE, true, "Delete", "Navigation"},
    {VK_HOME, true, "Home", "Navigation"},
    {VK_END, true, "End", "Navigation"},
    {VK_PRIOR, true, "Page Up", "Navigation"},
    {VK_NEXT, true, "Page Down", "Navigation"},
    {VK_UP, true, "Up", "Navigation"},
    {VK_DOWN, true, "Down", "Navigation"},
    {VK_LEFT, true, "Left", "Navigation"},
    {VK_RIGHT, true, "Right", "Navigation"},
    {VK_SNAPSHOT, true, "Print Screen", "System"},
    {VK_SCROLL, false, "Scroll Lock", "System"},
    {VK_PAUSE, false, "Pause", "System"},
    {VK_NUMLOCK, true, "Num Lock", "Numpad"},
    {VK_NUMPAD0, false, "Numpad 0", "Numpad"},
    {VK_NUMPAD1, false, "Numpad 1", "Numpad"},
    {VK_NUMPAD2, false, "Numpad 2", "Numpad"},
    {VK_NUMPAD3, false, "Numpad 3", "Numpad"},
    {VK_NUMPAD4, false, "Numpad 4", "Numpad"},
    {VK_NUMPAD5, false, "Numpad 5", "Numpad"},
    {VK_NUMPAD6, false, "Numpad 6", "Numpad"},
    {VK_NUMPAD7, false, "Numpad 7", "Numpad"},
    {VK_NUMPAD8, false, "Numpad 8", "Numpad"},
    {VK_NUMPAD9, false, "Numpad 9", "Numpad"},
    {VK_MULTIPLY, false, "Numpad *", "Numpad"},
    {VK_ADD, false, "Numpad +", "Numpad"},
    {VK_SUBTRACT, false, "Numpad -", "Numpad"},
    {VK_DECIMAL, false, "Numpad .", "Numpad"},
    {VK_DIVIDE, true, "Numpad /", "Numpad"},
    {VK_RETURN, true, "Numpad Enter", "Numpad"},
    {VK_OEM_1, false, "; :", "OEM"},
    {VK_OEM_PLUS, false, "= +", "OEM"},
    {VK_OEM_COMMA, false, ", <", "OEM"},
    {VK_OEM_MINUS, false, "- _", "OEM"},
    {VK_OEM_PERIOD, false, ". >", "OEM"},
    {VK_OEM_2, false, "/ ?", "OEM"},
    {VK_OEM_3, false, "` ~", "OEM"},
    {VK_OEM_4, false, "[ {", "OEM"},
    {VK_OEM_5, false, "\\ |", "OEM"},
    {VK_OEM_6, false, "] }", "OEM"},
    {VK_OEM_7, false, "' \"", "OEM"},
    {VK_OEM_102, false, "OEM 102 (<>)", "OEM"},
    {VK_VOLUME_MUTE, true, "Volume Mute", "Media"},
    {VK_VOLUME_DOWN, true, "Volume Down", "Media"},
    {VK_VOLUME_UP, true, "Volume Up", "Media"},
    {VK_MEDIA_NEXT_TRACK, true, "Media Next", "Media"},
    {VK_MEDIA_PREV_TRACK, true, "Media Previous", "Media"},
    {VK_MEDIA_STOP, true, "Media Stop", "Media"},
    {VK_MEDIA_PLAY_PAUSE, true, "Media Play/Pause", "Media"},
    {VK_BROWSER_BACK, true, "Browser Back", "Media"},
    {VK_BROWSER_FORWARD, true, "Browser Forward", "Media"},
};

const NamedKey* findNamed(uint16_t vk, bool ext)
{
    // Exact (vk, ext) match first - distinguishes Enter / Numpad Enter.
    for (const auto& k : kNamed)
        if (k.vk == vk && k.ext == ext) return &k;
    for (const auto& k : kNamed)
        if (k.vk == vk && vk != VK_RETURN) return &k;
    return nullptr;
}

} // namespace

const char* mouseButtonName(MouseButton b)
{
    switch (b) {
    case MouseButton::Left: return tr("Left Mouse");
    case MouseButton::Right: return tr("Right Mouse");
    case MouseButton::Middle: return tr("Middle Mouse");
    case MouseButton::X1: return tr("Mouse Button 4");
    case MouseButton::X2: return tr("Mouse Button 5");
    case MouseButton::WheelUp: return tr("Wheel Up");
    case MouseButton::WheelDown: return tr("Wheel Down");
    case MouseButton::WheelLeft: return tr("Wheel Left");
    case MouseButton::WheelRight: return tr("Wheel Right");
    default: return "Mouse ?";
    }
}

std::string vkName(uint16_t vk, bool extended)
{
    if (vk >= 'A' && vk <= 'Z') return std::string(1, char(vk));
    if (vk >= '0' && vk <= '9') return std::string(1, char(vk));
    if (vk >= VK_F1 && vk <= VK_F24) return strFormat("F%d", vk - VK_F1 + 1);
    if (const NamedKey* k = findNamed(vk, extended)) return tr(k->name);
    // Unknown VK: ask the active layout for a label.
    bool ext = false;
    UINT scan = vkToScan(vk, &ext);
    if (scan) {
        wchar_t buf[64];
        LONG lparam = LONG(scan << 16) | (ext ? (1 << 24) : 0);
        if (GetKeyNameTextW(lparam, buf, int(std::size(buf))) > 0) return toUtf8(buf);
    }
    return strFormat("VK 0x%02X", vk);
}

bool isModifierVk(uint16_t vk) { return modBitForVk(vk) != 0; }

uint8_t modBitForVk(uint16_t vk)
{
    switch (vk) {
    case VK_CONTROL:
    case VK_LCONTROL:
    case VK_RCONTROL: return ModCtrl;
    case VK_SHIFT:
    case VK_LSHIFT:
    case VK_RSHIFT: return ModShift;
    case VK_MENU:
    case VK_LMENU:
    case VK_RMENU: return ModAlt;
    case VK_LWIN:
    case VK_RWIN: return ModWin;
    default: return 0;
    }
}

bool vkDefaultExtended(uint16_t vk)
{
    switch (vk) {
    case VK_RCONTROL:
    case VK_RMENU:
    case VK_LWIN:
    case VK_RWIN:
    case VK_APPS:
    case VK_INSERT:
    case VK_DELETE:
    case VK_HOME:
    case VK_END:
    case VK_PRIOR:
    case VK_NEXT:
    case VK_UP:
    case VK_DOWN:
    case VK_LEFT:
    case VK_RIGHT:
    case VK_DIVIDE:
    case VK_NUMLOCK:
    case VK_SNAPSHOT:
    case VK_VOLUME_MUTE:
    case VK_VOLUME_DOWN:
    case VK_VOLUME_UP:
    case VK_MEDIA_NEXT_TRACK:
    case VK_MEDIA_PREV_TRACK:
    case VK_MEDIA_STOP:
    case VK_MEDIA_PLAY_PAUSE:
    case VK_BROWSER_BACK:
    case VK_BROWSER_FORWARD: return true;
    default: return false;
    }
}

uint16_t vkToScan(uint16_t vk, bool* ext)
{
    // Generic modifiers map to their left-hand key.
    if (vk == VK_CONTROL) vk = VK_LCONTROL;
    if (vk == VK_SHIFT) vk = VK_LSHIFT;
    if (vk == VK_MENU) vk = VK_LMENU;
    UINT sc = MapVirtualKeyW(vk, MAPVK_VK_TO_VSC_EX);
    bool e = (sc & 0xFF00) == 0xE000 || vkDefaultExtended(vk);
    if ((sc & 0xFF00) == 0xE100) sc = 0; // Pause: E1 sequence, cannot be expressed as a single scan code
    if (ext) *ext = e;
    return uint16_t(sc & 0xFF);
}

std::string modsToString(uint8_t mods)
{
    std::string s;
    if (mods & ModCtrl) s += "Ctrl + ";
    if (mods & ModShift) s += "Shift + ";
    if (mods & ModAlt) s += "Alt + ";
    if (mods & ModWin) s += "Win + ";
    return s;
}

bool KeyChord::sameKey(const KeyChord& o) const
{
    if (device != o.device) return false;
    if (device == Device::Mouse) return code == o.code;
    if (device == Device::Keyboard) {
        if (code != o.code) return false;
        return code != VK_RETURN || extended == o.extended;
    }
    return true;
}

std::string KeyChord::keyName() const
{
    switch (device) {
    case Device::Mouse: return mouseButtonName(MouseButton(code));
    case Device::Keyboard: return vkName(code, extended);
    default: return tr("(none)");
    }
}

std::string KeyChord::name() const
{
    if (device == Device::None) return tr("(none)");
    uint8_t m = mods;
    if (device == Device::Keyboard) m &= uint8_t(~modBitForVk(code)); // don't print "Ctrl + Right Ctrl"
    return modsToString(m) + keyName();
}

json::Value KeyChord::toJson() const
{
    json::Value v = json::Value::object();
    v.set("device", device == Device::Mouse ? "mouse" : device == Device::Keyboard ? "keyboard" : "none");
    v.set("code", int(code));
    if (device == Device::Keyboard) {
        v.set("scan", int(scan));
        v.set("extended", extended);
    }
    v.set("mods", int(mods));
    v.set("name", name()); // informational only
    return v;
}

KeyChord KeyChord::fromJson(const json::Value& v)
{
    KeyChord c;
    std::string d = v.getString("device", "none");
    c.device = d == "mouse" ? Device::Mouse : d == "keyboard" ? Device::Keyboard : Device::None;
    c.code = uint16_t(v.getInt("code", 0));
    c.scan = uint16_t(v.getInt("scan", 0));
    c.extended = v.getBool("extended", false);
    c.mods = uint8_t(v.getInt("mods", 0) & 0x0F);
    if (c.device == Device::Mouse && c.code >= uint16_t(MouseButton::Count)) c.device = Device::None;
    if (c.device == Device::Keyboard && (c.code == 0 || c.code > 0xFE)) c.device = Device::None;
    return c;
}

KeyChord KeyChord::mouse(MouseButton b, uint8_t mods)
{
    KeyChord c;
    c.device = Device::Mouse;
    c.code = uint16_t(b);
    c.mods = mods;
    return c;
}

KeyChord KeyChord::key(uint16_t vk, uint8_t mods)
{
    KeyChord c;
    c.device = Device::Keyboard;
    c.code = vk;
    bool ext = false;
    c.scan = vkToScan(vk, &ext);
    c.extended = ext;
    c.mods = mods;
    return c;
}

const std::vector<KeyListEntry>& keyList()
{
    static const std::vector<KeyListEntry> list = [] {
        std::vector<KeyListEntry> v;
        static std::vector<std::string> storage; // stable c_str() for generated names
        storage.reserve(64);
        for (char c = 'A'; c <= 'Z'; ++c) {
            storage.emplace_back(1, c);
            v.push_back({uint16_t(c), false, nullptr, "Letters"});
        }
        for (char c = '0'; c <= '9'; ++c) {
            storage.emplace_back(1, c);
            v.push_back({uint16_t(c), false, nullptr, "Digits"});
        }
        for (int i = 0; i < 24; ++i) {
            storage.push_back(strFormat("F%d", i + 1));
            v.push_back({uint16_t(VK_F1 + i), false, nullptr, "Function"});
        }
        for (size_t i = 0; i < storage.size(); ++i) v[i].name = storage[i].c_str();
        for (const auto& k : kNamed) v.push_back({k.vk, k.ext, k.name, k.group});
        return v;
    }();
    return list;
}

} // namespace infclick
