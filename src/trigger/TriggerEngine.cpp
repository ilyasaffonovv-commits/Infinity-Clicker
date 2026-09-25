#include "trigger/TriggerEngine.h"

#include "core/Clock.h"
#include "core/Log.h"
#include "core/Str.h"
#include "input/InputAction.h"
#include "platform/win/ProcessUtil.h"

#include <algorithm>

namespace infclick {

namespace {

TriggerEngine* g_self = nullptr;

constexpr UINT WM_APP_RECONFIG = WM_APP + 1;
constexpr UINT WM_APP_REINSTALL = WM_APP + 2;
constexpr UINT WM_APP_GATE = WM_APP + 3;
constexpr UINT WM_APP_HOOKS = WM_APP + 4;
constexpr int kHotkeyId = 0x7ACE;

UINT toHotkeyMods(uint8_t m)
{
    UINT r = MOD_NOREPEAT;
    if (m & ModCtrl) r |= MOD_CONTROL;
    if (m & ModShift) r |= MOD_SHIFT;
    if (m & ModAlt) r |= MOD_ALT;
    if (m & ModWin) r |= MOD_WIN;
    return r;
}

} // namespace

TriggerEngine::TriggerEngine()
{
    readyEvent_ = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    gateEvent_ = CreateEventW(nullptr, FALSE, FALSE, nullptr);
}

TriggerEngine::~TriggerEngine()
{
    shutdown();
    if (readyEvent_) CloseHandle(readyEvent_);
    if (gateEvent_) CloseHandle(gateEvent_);
}

bool TriggerEngine::start(TriggerSink* sink)
{
    if (thread_.joinable()) return true;
    sink_ = sink;
    g_self = this;
    ResetEvent(readyEvent_);
    thread_ = std::thread([this] { threadMain(); });
    return WaitForSingleObject(readyEvent_, 3000) == WAIT_OBJECT_0 && kbHookOn_.load();
}

void TriggerEngine::shutdown()
{
    if (!thread_.joinable()) return;
    if (msgWnd_) PostMessageW(msgWnd_, WM_CLOSE, 0, 0);
    else PostThreadMessageW(threadId_, WM_QUIT, 0, 0);
    thread_.join();
    g_self = nullptr;
}

void TriggerEngine::setConfig(const TriggerConfig& cfg)
{
    {
        std::lock_guard lk(cfgMu_);
        pending_ = cfg;
        pendingValid_ = true;
    }
    if (msgWnd_) PostMessageW(msgWnd_, WM_APP_RECONFIG, 0, 0);
}

void TriggerEngine::beginCapture(bool allowMouse)
{
    captureState_.store(allowMouse ? 2 : 1);
    if (msgWnd_) PostMessageW(msgWnd_, WM_APP_HOOKS, 0, 0);
}

void TriggerEngine::cancelCapture()
{
    captureState_.store(0);
    if (msgWnd_) PostMessageW(msgWnd_, WM_APP_HOOKS, 0, 0);
}

void TriggerEngine::reinstallHooks()
{
    if (msgWnd_) PostMessageW(msgWnd_, WM_APP_REINSTALL, 0, 0);
}

void TriggerEngine::recomputeGate()
{
    if (msgWnd_) PostMessageW(msgWnd_, WM_APP_GATE, 0, 0);
}

std::wstring TriggerEngine::foregroundProcess() const
{
    std::lock_guard lk(fgMu_);
    return fgName_;
}

// ------------------------------------------------------------------ thread

void TriggerEngine::threadMain()
{
    SetThreadDescription(GetCurrentThread(), L"Infinity Clicker Trigger");
    // The LL hook callback runs on this thread and the whole system's input
    // waits for it - it must get CPU instantly and return in microseconds.
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_TIME_CRITICAL);
    threadId_ = GetCurrentThreadId();

    WNDCLASSEXW wc{sizeof wc};
    wc.lpfnWndProc = wndProc;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.lpszClassName = L"InfinityClickerTriggerSink";
    RegisterClassExW(&wc);
    msgWnd_ = CreateWindowExW(0, wc.lpszClassName, L"", 0, 0, 0, 0, 0, HWND_MESSAGE, nullptr, wc.hInstance, nullptr);

    applyPendingConfig();
    kbHook_ = SetWindowsHookExW(WH_KEYBOARD_LL, kbProc, GetModuleHandleW(nullptr), 0);
    kbHookOn_.store(kbHook_ != nullptr);
    if (!kbHook_) TLOG_E("trigger: SetWindowsHookEx(WH_KEYBOARD_LL) failed, error %lu", GetLastError());
    updateHooks();
    fgHook_ = SetWinEventHook(EVENT_SYSTEM_FOREGROUND, EVENT_SYSTEM_FOREGROUND, nullptr, winEventProc, 0, 0,
                              WINEVENT_OUTOFCONTEXT);
    evaluateGate(GetForegroundWindow());
    SetEvent(readyEvent_);

    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        DispatchMessageW(&msg);
    }

    if (fgHook_) UnhookWinEvent(fgHook_);
    if (mouseHook_) UnhookWindowsHookEx(mouseHook_);
    if (kbHook_) UnhookWindowsHookEx(kbHook_);
    mouseHook_ = kbHook_ = nullptr;
    fgHook_ = nullptr;
    mouseHookOn_ = kbHookOn_ = false;
    if (rawOn_.load()) {
        RAWINPUTDEVICE rid{0x01, 0x02, RIDEV_REMOVE, nullptr};
        RegisterRawInputDevices(&rid, 1, sizeof rid);
        rawOn_ = false;
    }
    if (hotkeyOn_.load()) UnregisterHotKey(msgWnd_, kHotkeyId);
    hotkeyOn_ = false;
    if (msgWnd_) DestroyWindow(msgWnd_);
    msgWnd_ = nullptr;
}

LRESULT CALLBACK TriggerEngine::wndProc(HWND h, UINT msg, WPARAM wp, LPARAM lp)
{
    TriggerEngine* self = g_self;
    switch (msg) {
    case WM_INPUT:
        if (self) self->handleRawInput(reinterpret_cast<HRAWINPUT>(lp));
        return DefWindowProcW(h, msg, wp, lp);
    case WM_HOTKEY:
        if (self && wp == kHotkeyId && self->sink_) {
            TLOG_W("trigger: emergency stop received via RegisterHotKey backup path");
            self->sink_->onEmergency();
        }
        return 0;
    case WM_APP_RECONFIG:
        if (self) {
            self->applyPendingConfig();
            self->updateHooks();
            self->evaluateGate(GetForegroundWindow());
        }
        return 0;
    case WM_APP_HOOKS:
        if (self) self->updateHooks();
        return 0;
    case WM_APP_GATE:
        if (self) {
            self->lastFgPid_ = 0;
            self->evaluateGate(GetForegroundWindow());
        }
        return 0;
    case WM_APP_REINSTALL:
        if (self) {
            TLOG_W("trigger: reinstalling low-level hooks (health check)");
            if (self->kbHook_) UnhookWindowsHookEx(self->kbHook_);
            self->kbHook_ = SetWindowsHookExW(WH_KEYBOARD_LL, kbProc, GetModuleHandleW(nullptr), 0);
            self->kbHookOn_ = self->kbHook_ != nullptr;
            if (self->mouseHook_) {
                UnhookWindowsHookEx(self->mouseHook_);
                self->mouseHook_ = nullptr;
                self->mouseHookOn_ = false;
            }
            self->updateHooks();
        }
        return 0;
    case WM_CLOSE: PostQuitMessage(0); return 0;
    }
    return DefWindowProcW(h, msg, wp, lp);
}

void CALLBACK TriggerEngine::winEventProc(HWINEVENTHOOK, DWORD ev, HWND hwnd, LONG idObject, LONG, DWORD, DWORD)
{
    if (ev == EVENT_SYSTEM_FOREGROUND && idObject == OBJID_WINDOW && g_self) g_self->evaluateGate(hwnd);
}

void TriggerEngine::applyPendingConfig()
{
    TriggerConfig c;
    {
        std::lock_guard lk(cfgMu_);
        if (!pendingValid_) return;
        c = pending_;
        pendingValid_ = false;
    }
    const bool triggerChanged = !(c.trigger == cfg_.trigger);
    cfg_ = std::move(c);
    if (triggerChanged) {
        trigDown_ = false;
        trigSuppressed_ = false;
    }
    targets_.clear();
    std::string list = toUtf8(cfg_.targetProcesses);
    size_t start = 0;
    while (start <= list.size()) {
        size_t end = list.find_first_of(";,", start);
        if (end == std::string::npos) end = list.size();
        std::string item = toLowerAscii(trim(list.substr(start, end - start)));
        if (!item.empty()) {
            if (item.find('.') == std::string::npos) item += ".exe";
            targets_.push_back(toLowerW(toWide(item)));
        }
        start = end + 1;
    }
}

void TriggerEngine::updateHooks()
{
    const bool mouseCapture = captureState_.load() == 2;
    const bool mouseTrig = cfg_.trigger.isMouse() || cfg_.emergency.isMouse();
    const bool needHook =
        mouseCapture || cfg_.observeMouse || (mouseTrig && (cfg_.mouseBackend == TriggerBackend::Hook || cfg_.suppressTrigger));
    const bool needRaw = !needHook && mouseTrig; // exactly one mouse source feeds the trigger logic

    if (needHook && !mouseHook_) {
        mouseHook_ = SetWindowsHookExW(WH_MOUSE_LL, mouseProc, GetModuleHandleW(nullptr), 0);
        if (!mouseHook_) TLOG_E("trigger: SetWindowsHookEx(WH_MOUSE_LL) failed, error %lu", GetLastError());
        else TLOG_D("trigger: mouse LL hook installed");
    } else if (!needHook && mouseHook_) {
        UnhookWindowsHookEx(mouseHook_);
        mouseHook_ = nullptr;
        TLOG_D("trigger: mouse LL hook removed");
    }
    mouseHookOn_.store(mouseHook_ != nullptr);

    if (needRaw != rawOn_.load()) {
        RAWINPUTDEVICE rid{0x01, 0x02, needRaw ? DWORD(RIDEV_INPUTSINK) : DWORD(RIDEV_REMOVE), needRaw ? msgWnd_ : nullptr};
        if (RegisterRawInputDevices(&rid, 1, sizeof rid)) rawOn_.store(needRaw);
        else TLOG_E("trigger: RegisterRawInputDevices failed, error %lu", GetLastError());
    }

    if (!kbHook_) {
        kbHook_ = SetWindowsHookExW(WH_KEYBOARD_LL, kbProc, GetModuleHandleW(nullptr), 0);
        kbHookOn_.store(kbHook_ != nullptr);
    }

    // Redundant emergency path. Only for keyboard combos with modifiers (a bare
    // key would steal it from every other application).
    if (hotkeyOn_.load()) {
        UnregisterHotKey(msgWnd_, kHotkeyId);
        hotkeyOn_ = false;
    }
    if (cfg_.emergency.isKeyboard() && cfg_.emergency.mods) {
        if (RegisterHotKey(msgWnd_, kHotkeyId, toHotkeyMods(cfg_.emergency.mods), cfg_.emergency.code)) hotkeyOn_ = true;
        else TLOG_W("trigger: RegisterHotKey(%s) failed (error %lu) - LL hook path still active",
                    cfg_.emergency.name().c_str(), GetLastError());
    }
}

void TriggerEngine::evaluateGate(HWND fg)
{
    DWORD pid = 0;
    if (fg) GetWindowThreadProcessId(fg, &pid);
    std::wstring name;
    if (pid && pid == lastFgPid_) {
        name = lastFgName_;
    } else {
        name = pid ? procutil::processNameFromPid(pid) : L"";
        lastFgPid_ = pid;
        lastFgName_ = name;
    }
    {
        std::lock_guard lk(fgMu_);
        fgName_ = name;
    }

    bool open = true;
    uint8_t reason = 0;
    bool decided = false;
    if (pid && pid == GetCurrentProcessId()) {
        HWND root = GetAncestor(fg, GA_ROOT);
        if (cfg_.testPad && root == cfg_.testPad) {
            decided = true; // the lab test pad is always a valid target
        } else if (cfg_.pauseOnOwnWindow) {
            open = false;
            reason = 2; // PauseReason::OwnWindow
            decided = true;
        }
    }
    if (!decided && cfg_.filterEnabled && !targets_.empty()) {
        const std::wstring lower = toLowerW(name);
        open = std::find(targets_.begin(), targets_.end(), lower) != targets_.end();
        if (!open) reason = 1; // PauseReason::TargetNotForeground
    }
    const bool was = gateOpen_.exchange(open);
    const uint8_t wasReason = gateReason_.exchange(reason);
    if (was != open || wasReason != reason) {
        SetEvent(gateEvent_);
        if (sink_) sink_->onGateChanged();
    }
}

// ------------------------------------------------------------------ input

bool TriggerEngine::classifyPhysical(ULONG_PTR extra, bool injected)
{
    if (extra == kTagBench) return cfg_.acceptBenchTag;
    if (!injected) return true;
    return cfg_.acceptInjected;
}

uint8_t TriggerEngine::physMods() const
{
    uint8_t m = 0;
    if (physKey_[VK_LCONTROL] || physKey_[VK_RCONTROL]) m |= ModCtrl;
    if (physKey_[VK_LSHIFT] || physKey_[VK_RSHIFT]) m |= ModShift;
    if (physKey_[VK_LMENU] || physKey_[VK_RMENU]) m |= ModAlt;
    if (physKey_[VK_LWIN] || physKey_[VK_RWIN]) m |= ModWin;
    return m;
}

bool TriggerEngine::keyMatches(const KeyChord& c, uint16_t vk, bool ext)
{
    if (!c.isKeyboard()) return false;
    switch (c.code) {
    case VK_CONTROL: return vk == VK_LCONTROL || vk == VK_RCONTROL || vk == VK_CONTROL;
    case VK_SHIFT: return vk == VK_LSHIFT || vk == VK_RSHIFT || vk == VK_SHIFT;
    case VK_MENU: return vk == VK_LMENU || vk == VK_RMENU || vk == VK_MENU;
    case VK_RETURN: return vk == VK_RETURN && ext == c.extended;
    default: return c.code == vk;
    }
}

LRESULT CALLBACK TriggerEngine::kbProc(int code, WPARAM wp, LPARAM lp)
{
    TriggerEngine* self = g_self;
    if (code == HC_ACTION && self) {
        const int64_t qpc = clk::now();
        const auto* k = reinterpret_cast<const KBDLLHOOKSTRUCT*>(lp);
        if (k->dwExtraInfo == kTagInfClick) {
            self->observedOursKeyboard.fetch_add(1, std::memory_order_relaxed);
            self->observedOurs.fetch_add(1, std::memory_order_relaxed);
            return CallNextHookEx(nullptr, code, wp, lp);
        }
        const bool injected = (k->flags & LLKHF_INJECTED) != 0;
        if (!self->classifyPhysical(k->dwExtraInfo, injected)) {
            if (injected) self->observedForeignInjected.fetch_add(1, std::memory_order_relaxed);
            return CallNextHookEx(nullptr, code, wp, lp);
        }
        const bool down = wp == WM_KEYDOWN || wp == WM_SYSKEYDOWN;
        bool swallow = false;
        self->handleKey(down, uint16_t(k->vkCode & 0xFF), uint16_t(k->scanCode), (k->flags & LLKHF_EXTENDED) != 0,
                        qpc, swallow);
        if (swallow) return 1;
    }
    return CallNextHookEx(nullptr, code, wp, lp);
}

LRESULT CALLBACK TriggerEngine::mouseProc(int code, WPARAM wp, LPARAM lp)
{
    TriggerEngine* self = g_self;
    if (code == HC_ACTION && self && wp != WM_MOUSEMOVE) { // moves: straight through, zero work
        const int64_t qpc = clk::now();
        const auto* m = reinterpret_cast<const MSLLHOOKSTRUCT*>(lp);
        if (m->dwExtraInfo == kTagInfClick) {
            self->observedOursMouse.fetch_add(1, std::memory_order_relaxed);
            self->observedOurs.fetch_add(1, std::memory_order_relaxed);
            return CallNextHookEx(nullptr, code, wp, lp);
        }
        const bool injected = (m->flags & LLMHF_INJECTED) != 0;
        if (!self->classifyPhysical(m->dwExtraInfo, injected)) {
            if (injected) self->observedForeignInjected.fetch_add(1, std::memory_order_relaxed);
            return CallNextHookEx(nullptr, code, wp, lp);
        }
        MouseButton b;
        bool down = true;
        switch (wp) {
        case WM_LBUTTONDOWN: b = MouseButton::Left; break;
        case WM_LBUTTONUP: b = MouseButton::Left; down = false; break;
        case WM_RBUTTONDOWN: b = MouseButton::Right; break;
        case WM_RBUTTONUP: b = MouseButton::Right; down = false; break;
        case WM_MBUTTONDOWN: b = MouseButton::Middle; break;
        case WM_MBUTTONUP: b = MouseButton::Middle; down = false; break;
        case WM_XBUTTONDOWN:
        case WM_XBUTTONUP:
            b = HIWORD(m->mouseData) == XBUTTON1 ? MouseButton::X1 : MouseButton::X2;
            down = wp == WM_XBUTTONDOWN;
            break;
        case WM_MOUSEWHEEL: b = short(HIWORD(m->mouseData)) > 0 ? MouseButton::WheelUp : MouseButton::WheelDown; break;
        case WM_MOUSEHWHEEL:
            b = short(HIWORD(m->mouseData)) > 0 ? MouseButton::WheelRight : MouseButton::WheelLeft;
            break;
        default: return CallNextHookEx(nullptr, code, wp, lp);
        }
        bool swallow = false;
        self->lastPt_ = m->pt;
        self->handleMouse(b, down, qpc, swallow);
        if (swallow) return 1;
    }
    return CallNextHookEx(nullptr, code, wp, lp);
}

void TriggerEngine::handleRawInput(HRAWINPUT h)
{
    alignas(8) BYTE buf[sizeof(RAWINPUT) + 64];
    UINT size = sizeof buf;
    if (GetRawInputData(h, RID_INPUT, buf, &size, sizeof(RAWINPUTHEADER)) == UINT(-1)) return;
    const auto* ri = reinterpret_cast<const RAWINPUT*>(buf);
    if (ri->header.dwType != RIM_TYPEMOUSE) return;
    const RAWMOUSE& m = ri->data.mouse;
    const USHORT f = m.usButtonFlags;
    if (f == 0) return; // pure movement
    if (m.ulExtraInformation == kTagInfClick) {
        observedOursMouse.fetch_add(1, std::memory_order_relaxed);
        observedOurs.fetch_add(1, std::memory_order_relaxed);
        return;
    }
    // Raw input carries no "injected" flag (hDevice is 0 both for SendInput and
    // for precision touchpads), so foreign injected input counts as physical here.
    if (m.ulExtraInformation == kTagBench && !cfg_.acceptBenchTag) return;
    const int64_t qpc = clk::now();
    bool swallow = false;
    struct Map {
        USHORT flag;
        MouseButton b;
        bool down;
    };
    static constexpr Map kMap[] = {
        {RI_MOUSE_LEFT_BUTTON_DOWN, MouseButton::Left, true},   {RI_MOUSE_LEFT_BUTTON_UP, MouseButton::Left, false},
        {RI_MOUSE_RIGHT_BUTTON_DOWN, MouseButton::Right, true}, {RI_MOUSE_RIGHT_BUTTON_UP, MouseButton::Right, false},
        {RI_MOUSE_MIDDLE_BUTTON_DOWN, MouseButton::Middle, true}, {RI_MOUSE_MIDDLE_BUTTON_UP, MouseButton::Middle, false},
        {RI_MOUSE_BUTTON_4_DOWN, MouseButton::X1, true},        {RI_MOUSE_BUTTON_4_UP, MouseButton::X1, false},
        {RI_MOUSE_BUTTON_5_DOWN, MouseButton::X2, true},        {RI_MOUSE_BUTTON_5_UP, MouseButton::X2, false},
    };
    for (const Map& e : kMap)
        if (f & e.flag) handleMouse(e.b, e.down, qpc, swallow);
    if (f & RI_MOUSE_WHEEL) handleMouse(short(m.usButtonData) > 0 ? MouseButton::WheelUp : MouseButton::WheelDown, true, qpc, swallow);
    if (f & RI_MOUSE_HWHEEL)
        handleMouse(short(m.usButtonData) > 0 ? MouseButton::WheelRight : MouseButton::WheelLeft, true, qpc, swallow);
}

bool TriggerEngine::handleKey(bool down, uint16_t vk, uint16_t scan, bool ext, int64_t qpc, bool& swallow)
{
    const bool wasDown = physKey_[vk];
    physKey_[vk] = down;

    // 1) Binding capture has absolute priority and eats the keys it uses.
    if (const int cap = captureState_.load(std::memory_order_relaxed)) {
        (void)cap;
        if (down) {
            if (isModifierVk(vk)) {
                if (!wasDown) {
                    captureModVk_ = vk;
                    captureModScan_ = scan;
                    captureModExt_ = ext;
                }
                swallow = true;
                swallowUp_[vk] = true;
                return true;
            }
            KeyChord c;
            c.device = Device::Keyboard;
            c.code = vk;
            c.scan = scan;
            c.extended = ext;
            c.mods = physMods();
            captureModVk_ = 0;
            captureState_.store(0);
            swallow = true;
            swallowUp_[vk] = true;
            if (sink_) sink_->onCaptured(c);
            PostMessageW(msgWnd_, WM_APP_HOOKS, 0, 0);
            return true;
        }
        if (captureModVk_ == vk) { // a modifier pressed and released on its own
            KeyChord c;
            c.device = Device::Keyboard;
            c.code = vk;
            c.scan = captureModScan_;
            c.extended = captureModExt_;
            c.mods = physMods();
            captureModVk_ = 0;
            captureState_.store(0);
            if (sink_) sink_->onCaptured(c);
            PostMessageW(msgWnd_, WM_APP_HOOKS, 0, 0);
        }
        if (swallowUp_[vk]) {
            swallowUp_[vk] = false;
            swallow = true;
        }
        return true;
    }

    // 2) Key-ups whose key-down we swallowed.
    if (!down && swallowUp_[vk]) {
        swallowUp_[vk] = false;
        swallow = true;
        if (!(trigDown_ && keyMatches(cfg_.trigger, vk, ext))) return true;
    }

    // 3) Emergency stop - independent of mode, profile, armed state.
    if (down && !wasDown && keyMatches(cfg_.emergency, vk, ext) &&
        (physMods() & cfg_.emergency.mods) == cfg_.emergency.mods) {
        if (sink_) sink_->onEmergency();
        swallow = true;
        swallowUp_[vk] = true;
        return true;
    }

    // 4) Trigger.
    if (keyMatches(cfg_.trigger, vk, ext)) {
        if (down) {
            if (trigDown_) { // typematic auto-repeat
                if (trigSuppressed_) swallow = true;
                return true;
            }
            if ((physMods() & cfg_.trigger.mods) == cfg_.trigger.mods) {
                trigDown_ = true;
                trigSuppressed_ = cfg_.suppressTrigger && sink_ && sink_->isArmed();
                if (sink_) sink_->onTriggerDown(qpc);
                if (trigSuppressed_) swallow = true;
            }
        } else if (trigDown_) {
            trigDown_ = false;
            if (sink_) sink_->onTriggerUp(qpc);
            if (trigSuppressed_) {
                swallow = true;
                trigSuppressed_ = false;
            }
        }
        return true;
    }
    return false;
}

bool TriggerEngine::handleMouse(MouseButton b, bool down, int64_t qpc, bool& swallow)
{
    const int bi = int(b);
    const bool wheel = b >= MouseButton::WheelUp;
    if (!wheel) physicalButtonEvents.fetch_add(1, std::memory_order_relaxed);

    const int cap = captureState_.load(std::memory_order_relaxed);
    if (cap == 2) {
        // Left clicks on Infinity Clicker's own window pass through, so "Cancel" stays clickable
        // (Left Mouse can still be bound by clicking anywhere else).
        if (b == MouseButton::Left && cfg_.ownWindow) {
            RECT r;
            if (GetWindowRect(cfg_.ownWindow, &r) && PtInRect(&r, lastPt_)) return false;
        }
        if (down) {
            KeyChord c = KeyChord::mouse(b, physMods());
            captureState_.store(0);
            if (!wheel) swallowMouseUp_[bi] = true;
            swallow = true;
            if (sink_) sink_->onCaptured(c);
            PostMessageW(msgWnd_, WM_APP_HOOKS, 0, 0);
        }
        return true;
    }
    if (!wheel && !down && swallowMouseUp_[bi]) {
        swallowMouseUp_[bi] = false;
        swallow = true;
        if (!(trigDown_ && cfg_.trigger.isMouse() && cfg_.trigger.code == bi)) return true;
    }

    if (down && cfg_.emergency.isMouse() && cfg_.emergency.code == bi &&
        (physMods() & cfg_.emergency.mods) == cfg_.emergency.mods) {
        if (sink_) sink_->onEmergency();
        swallow = true;
        if (!wheel) swallowMouseUp_[bi] = true;
        return true;
    }

    if (cfg_.trigger.isMouse() && cfg_.trigger.code == bi) {
        const bool modsOk = (physMods() & cfg_.trigger.mods) == cfg_.trigger.mods;
        if (wheel) { // a wheel notch is a momentary press
            if (down && modsOk && sink_) {
                sink_->onTriggerDown(qpc);
                sink_->onTriggerUp(qpc);
                if (cfg_.suppressTrigger && sink_->isArmed()) swallow = true;
            }
            return true;
        }
        if (down) {
            if (trigDown_) return true;
            if (modsOk) {
                trigDown_ = true;
                trigSuppressed_ = cfg_.suppressTrigger && sink_ && sink_->isArmed();
                if (sink_) sink_->onTriggerDown(qpc);
                if (trigSuppressed_) swallow = true;
            }
        } else if (trigDown_) {
            trigDown_ = false;
            if (sink_) sink_->onTriggerUp(qpc);
            if (trigSuppressed_) {
                swallow = true;
                trigSuppressed_ = false;
            }
        }
        return true;
    }
    return false;
}

} // namespace infclick
