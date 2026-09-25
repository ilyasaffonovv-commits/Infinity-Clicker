#pragma once
// InputAction - the universal "what to press" description, and ActionProgram -
// its compiled, allocation-free form executed by the scheduler.
//
//   InputAction { chord (key/button + modifiers), downTime, repeat, gap, ... }
//        |  compile()
//        v
//   ActionProgram: [ Step{offset=0,   INPUT[] = mods-down, BTN-down (+BTN-up if downTime==0)}
//                    Step{offset=hold, INPUT[] = BTN-up, mods-up} ... ]
//
// Every step is one SendInput() call, so events inside a step are atomic in the
// system input stream (SendInput guarantees no interleaving within one call).
// New action types (double click, sequences, macros) only need a new compile()
// path - the scheduler just walks steps.
#include "input/KeyChord.h"

#include <windows.h>

#include <cstdint>
#include <vector>

namespace infclick {

// dwExtraInfo tags stamped on every injected event.
constexpr ULONG_PTR kTagInfClick = 0x494E4643; // 'INFC' - output actions
constexpr ULONG_PTR kTagBench = 0x4942434E;   // 'IBCN' - autotest-simulated "physical" input

enum class KeyInjectMode : uint8_t { ScanCode = 0, VirtualKey = 1 };

struct InputAction {
    KeyChord chord = KeyChord::mouse(MouseButton::Left);
    uint32_t downTimeUs = 0;   // DOWN -> UP hold time; 0 = both in one SendInput batch
    uint32_t repeat = 1;       // presses per action (2 = double click, 3 = triple)
    uint32_t repeatGapUs = 0;  // UP -> next DOWN inside one action (repeat > 1)
    int32_t wheelDelta = 120;  // wheel notch size (WHEEL_DELTA)
    KeyInjectMode keyMode = KeyInjectMode::ScanCode;

    json::Value toJson() const;
    static InputAction fromJson(const json::Value& v);
};

constexpr int kMaxStepInputs = 16;

struct ActionStep {
    int64_t offsetTicks = 0; // QPC ticks relative to the action start
    uint32_t count = 0;
    INPUT inputs[kMaxStepInputs]{};
};

struct ActionProgram {
    std::vector<ActionStep> steps;
    int64_t spanTicks = 0;        // offset of the last step
    uint32_t inputsPerAction = 0; // total INPUTs for one action
    std::vector<INPUT> flat;      // all steps concatenated (MAX mode batching)
    std::string error;            // non-empty if the action could not be compiled

    bool ok() const { return error.empty() && !steps.empty(); }
};

ActionProgram compileAction(const InputAction& a, ULONG_PTR tag = kTagInfClick);

// Low-level INPUT builders (also used by the bench/autotest harness).
INPUT makeMouseInput(MouseButton b, bool down, int32_t wheelDelta, ULONG_PTR tag);
INPUT makeKeyInput(uint16_t vk, uint16_t scan, bool extended, bool up, KeyInjectMode mode, ULONG_PTR tag);

} // namespace infclick
