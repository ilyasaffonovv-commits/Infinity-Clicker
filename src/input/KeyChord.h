#pragma once
// KeyChord: one physical key or mouse button plus modifier set.
// Used uniformly for triggers, the emergency-stop hotkey and output actions.
#include "core/Json.h"

#include <cstdint>
#include <string>
#include <vector>

namespace infclick {

enum class Device : uint8_t { None = 0, Keyboard = 1, Mouse = 2 };

enum class MouseButton : uint8_t {
    Left = 0,
    Right,
    Middle,
    X1, // "Mouse Button 4" (usually "Back")
    X2, // "Mouse Button 5" (usually "Forward")
    WheelUp,
    WheelDown,
    WheelLeft,
    WheelRight,
    Count
};

enum ModBits : uint8_t { ModNone = 0, ModCtrl = 1, ModShift = 2, ModAlt = 4, ModWin = 8 };

struct KeyChord {
    Device device = Device::None;
    uint16_t code = 0;     // VK code (keyboard) or MouseButton (mouse)
    uint16_t scan = 0;     // keyboard make code without E0 prefix; 0 = derive from layout
    bool extended = false; // keyboard: E0-prefixed key (Right Ctrl, arrows, Numpad Enter...)
    uint8_t mods = 0;      // ModBits: required (trigger) or held-around (action)

    bool valid() const { return device != Device::None; }
    bool isMouse() const { return device == Device::Mouse; }
    bool isKeyboard() const { return device == Device::Keyboard; }
    bool isWheel() const
    {
        return isMouse() && code >= uint16_t(MouseButton::WheelUp) && code <= uint16_t(MouseButton::WheelRight);
    }
    MouseButton button() const { return MouseButton(code); }

    bool sameKey(const KeyChord& o) const; // same physical key/button, ignoring mods
    bool operator==(const KeyChord& o) const
    {
        return device == o.device && code == o.code && mods == o.mods &&
               (device != Device::Keyboard || extended == o.extended);
    }

    std::string name() const;     // "Ctrl + Shift + F12", "Mouse Button 4", "Numpad Enter"
    std::string keyName() const;  // without modifiers

    json::Value toJson() const;
    static KeyChord fromJson(const json::Value& v);

    static KeyChord mouse(MouseButton b, uint8_t mods = 0);
    static KeyChord key(uint16_t vk, uint8_t mods = 0); // scan/extended derived from VK
};

// ---- helpers ---------------------------------------------------------------
const char* mouseButtonName(MouseButton b);
std::string vkName(uint16_t vk, bool extended);
bool isModifierVk(uint16_t vk);
uint8_t modBitForVk(uint16_t vk);           // ModCtrl for VK_LCONTROL etc.
bool vkDefaultExtended(uint16_t vk);        // keys that are E0-prefixed by default
uint16_t vkToScan(uint16_t vk, bool* ext);  // layout-aware make code
std::string modsToString(uint8_t mods);

struct KeyListEntry {
    uint16_t vk;
    bool extended;
    const char* name;
    const char* group;
};
// Every key selectable from the manual picker (covers keys that are awkward to
// capture interactively, e.g. Esc, Print Screen, F13-F24, media keys).
const std::vector<KeyListEntry>& keyList();

} // namespace infclick
