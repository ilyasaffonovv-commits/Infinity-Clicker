#pragma once
// Trigger engine: turns *physical* user input into trigger edges.
//
// Runs on its own TIME_CRITICAL thread with a message loop that hosts:
//  * WH_KEYBOARD_LL  - always (keyboard triggers, emergency stop, binding capture)
//  * WH_MOUSE_LL     - only while needed (mouse trigger / capture / lab observer),
//                      because every system-wide mouse event pays a context switch
//                      into a low-level mouse hook
//  * Raw Input sink  - optional backend for mouse triggers (no per-event
//                      synchronous callback, but cannot block the trigger)
//  * WinEvent EVENT_SYSTEM_FOREGROUND - active-window filter (event driven, no polling)
//  * RegisterHotKey  - redundant emergency-stop path in case Windows ever
//                      silently removes the LL hook (LowLevelHooksTimeout)
//
// Physical vs synthetic input:
//   our own output  -> dwExtraInfo == kTagInfClick      -> never a trigger (no feedback loop)
//   other injectors -> LLKHF_INJECTED / LLMHF_INJECTED -> ignored unless acceptInjected
//   hardware        -> no injected flag                -> trigger
#include "input/KeyChord.h"

#include <windows.h>

#include <atomic>
#include <cstdint>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace infclick {

enum class TriggerBackend : uint8_t { Hook = 0, RawInput = 1 };

struct TriggerConfig {
    KeyChord trigger;
    bool suppressTrigger = false;  // block the trigger key/button from reaching other apps
    bool acceptInjected = false;   // treat input injected by *other* software as physical
    bool acceptBenchTag = false;   // autotest only: kTagBench events count as physical
    KeyChord emergency;
    TriggerBackend mouseBackend = TriggerBackend::Hook;
    bool observeMouse = false;     // lab: keep the mouse hook installed to count delivered events
    // Active-window filter
    bool filterEnabled = false;
    std::wstring targetProcesses;  // "javaw.exe; notepad.exe"
    bool pauseOnOwnWindow = true;
    HWND ownWindow = nullptr;
    HWND testPad = nullptr;
};

class TriggerSink {
public:
    virtual ~TriggerSink() = default;
    virtual void onTriggerDown(int64_t qpc) = 0;
    virtual void onTriggerUp(int64_t qpc) = 0;
    virtual void onEmergency() = 0;
    virtual void onCaptured(const KeyChord& chord) = 0; // called on the trigger thread
    virtual bool isArmed() const = 0;
    virtual void onGateChanged() {}
};

class TriggerEngine {
public:
    TriggerEngine();
    ~TriggerEngine();

    bool start(TriggerSink* sink);
    void shutdown();

    void setConfig(const TriggerConfig& cfg); // thread-safe, applied on the trigger thread

    void beginCapture(bool allowMouse);
    void cancelCapture();
    bool capturing() const { return captureState_.load() != 0; }

    // Active-window gate (read by the scheduler)
    const std::atomic<bool>* gateOpenPtr() const { return &gateOpen_; }
    const std::atomic<uint8_t>* gateReasonPtr() const { return &gateReason_; }
    HANDLE gateEvent() const { return gateEvent_; }
    std::wstring foregroundProcess() const;

    // Our own injected events as seen by the hook / raw input chain (telemetry stage 3).
    std::atomic<uint64_t> observedOursMouse{0};
    std::atomic<uint64_t> observedOursKeyboard{0};
    std::atomic<uint64_t> observedOurs{0};
    std::atomic<uint64_t> observedForeignInjected{0};
    std::atomic<uint64_t> physicalButtonEvents{0};

    bool mouseHookInstalled() const { return mouseHookOn_.load(); }
    bool keyboardHookInstalled() const { return kbHookOn_.load(); }
    bool rawInputActive() const { return rawOn_.load(); }
    bool emergencyHotkeyRegistered() const { return hotkeyOn_.load(); }
    DWORD threadId() const { return threadId_; }

    void reinstallHooks(); // health-check recovery
    void recomputeGate();  // e.g. after the target window list changed

private:
    static LRESULT CALLBACK kbProc(int code, WPARAM wp, LPARAM lp);
    static LRESULT CALLBACK mouseProc(int code, WPARAM wp, LPARAM lp);
    static LRESULT CALLBACK wndProc(HWND h, UINT msg, WPARAM wp, LPARAM lp);
    static void CALLBACK winEventProc(HWINEVENTHOOK, DWORD, HWND, LONG, LONG, DWORD, DWORD);

    void threadMain();
    void applyPendingConfig();
    void updateHooks();
    void evaluateGate(HWND fg);
    bool handleKey(bool down, uint16_t vk, uint16_t scan, bool ext, int64_t qpc, bool& swallow);
    bool handleMouse(MouseButton b, bool down, int64_t qpc, bool& swallow);
    void handleRawInput(HRAWINPUT h);
    bool classifyPhysical(ULONG_PTR extra, bool injected);
    uint8_t physMods() const;
    static bool keyMatches(const KeyChord& c, uint16_t vk, bool ext);

    TriggerSink* sink_ = nullptr;
    std::thread thread_;
    DWORD threadId_ = 0;
    HWND msgWnd_ = nullptr;
    HHOOK kbHook_ = nullptr;
    HHOOK mouseHook_ = nullptr;
    HWINEVENTHOOK fgHook_ = nullptr;
    HANDLE readyEvent_ = nullptr;
    HANDLE gateEvent_ = nullptr;

    std::mutex cfgMu_;
    TriggerConfig pending_;
    bool pendingValid_ = false;

    // --- trigger-thread-only state
    TriggerConfig cfg_;
    std::vector<std::wstring> targets_;
    bool physKey_[256]{};
    bool swallowUp_[256]{};
    bool swallowMouseUp_[5]{};
    bool trigDown_ = false;
    bool trigSuppressed_ = false;
    bool emergencyDown_ = false;
    uint16_t captureModVk_ = 0;
    uint16_t captureModScan_ = 0;
    bool captureModExt_ = false;
    DWORD lastFgPid_ = 0;
    POINT lastPt_{};
    std::wstring lastFgName_;

    std::atomic<int> captureState_{0}; // 0 off, 1 keyboard only, 2 keyboard + mouse
    std::atomic<bool> gateOpen_{true};
    std::atomic<uint8_t> gateReason_{0};
    std::atomic<bool> mouseHookOn_{false}, kbHookOn_{false}, rawOn_{false}, hotkeyOn_{false};
    mutable std::mutex fgMu_;
    std::wstring fgName_;
};

} // namespace infclick
