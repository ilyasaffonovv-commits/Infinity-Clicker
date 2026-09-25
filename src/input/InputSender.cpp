#include "input/InputSender.h"

#include <iterator>

namespace infclick {

void InputSender::track(const INPUT& in)
{
    if (in.type == INPUT_MOUSE) {
        const DWORD f = in.mi.dwFlags;
        auto set = [&](int bit, bool down) {
            uint32_t m = 1u << bit;
            uint32_t before = heldMouse_;
            heldMouse_ = down ? (heldMouse_ | m) : (heldMouse_ & ~m);
            dirty_ |= before != heldMouse_;
        };
        if (f & MOUSEEVENTF_LEFTDOWN) set(0, true);
        if (f & MOUSEEVENTF_LEFTUP) set(0, false);
        if (f & MOUSEEVENTF_RIGHTDOWN) set(1, true);
        if (f & MOUSEEVENTF_RIGHTUP) set(1, false);
        if (f & MOUSEEVENTF_MIDDLEDOWN) set(2, true);
        if (f & MOUSEEVENTF_MIDDLEUP) set(2, false);
        if (f & (MOUSEEVENTF_XDOWN | MOUSEEVENTF_XUP)) {
            bool down = (f & MOUSEEVENTF_XDOWN) != 0;
            if (in.mi.mouseData & XBUTTON1) set(3, down);
            if (in.mi.mouseData & XBUTTON2) set(4, down);
        }
    } else if (in.type == INPUT_KEYBOARD) {
        const uint16_t vk = in.ki.wVk & 0xFF;
        if (vk == 0) return;
        const uint32_t word = vk >> 5, bit = 1u << (vk & 31);
        const bool up = (in.ki.dwFlags & KEYEVENTF_KEYUP) != 0;
        const bool was = (heldKeys_[word] & bit) != 0;
        if (!up && !was) {
            heldKeys_[word] |= bit;
            ++heldKeyCount_;
            keyMeta_[vk] = uint16_t((in.ki.wScan & 0xFF) | ((in.ki.dwFlags & KEYEVENTF_EXTENDEDKEY) ? 0x100 : 0) |
                                    ((in.ki.dwFlags & KEYEVENTF_SCANCODE) ? 0 : 0x200));
            dirty_ = true;
        } else if (up && was) {
            heldKeys_[word] &= ~bit;
            --heldKeyCount_;
            dirty_ = true;
        }
    }
}

void InputSender::publish()
{
    dirty_ = false;
    if (!shared_) return;
    // Metadata first, then the bit (the guardian reads bits, then metadata).
    for (int w = 0; w < 8; ++w) {
        uint32_t bits = heldKeys_[w];
        for (int b = 0; b < 32 && bits; ++b) {
            if (bits & (1u << b)) {
                shared_->keyScan[w * 32 + b] = keyMeta_[w * 32 + b];
                bits &= ~(1u << b);
            }
        }
    }
    MemoryBarrier();
    for (int w = 0; w < 8; ++w) InterlockedExchange(&shared_->keyBits[w], LONG(heldKeys_[w]));
    InterlockedExchange(&shared_->mouseMask, LONG(heldMouse_));
}

UINT InputSender::send(const INPUT* in, UINT n)
{
    if (n == 0) return 0;
    const UINT ok = SendInput(n, const_cast<INPUT*>(in), sizeof(INPUT));
    calls.fetch_add(1, std::memory_order_relaxed);
    requested.fetch_add(n, std::memory_order_relaxed);
    accepted.fetch_add(ok, std::memory_order_relaxed);
    if (ok != n) {
        failedCalls.fetch_add(1, std::memory_order_relaxed);
        lastError.store(GetLastError(), std::memory_order_relaxed);
    }
    // Only events Windows accepted change the real key state.
    for (UINT i = 0; i < ok; ++i) track(in[i]);
    if (dirty_) publish();
    return ok;
}

UINT InputSender::releaseAll()
{
    INPUT buf[64];
    UINT n = 0;
    for (int w = 0; w < 8; ++w) {
        for (int b = 0; b < 32; ++b) {
            if (!(heldKeys_[w] & (1u << b))) continue;
            uint16_t vk = uint16_t(w * 32 + b);
            uint16_t meta = keyMeta_[vk];
            KeyInjectMode mode = (meta & 0x200) ? KeyInjectMode::VirtualKey : KeyInjectMode::ScanCode;
            if (n < std::size(buf)) buf[n++] = makeKeyInput(vk, meta & 0xFF, (meta & 0x100) != 0, true, mode, kTagInfClick);
        }
    }
    for (int b = 0; b <= int(MouseButton::X2); ++b)
        if ((heldMouse_ & (1u << b)) && n < std::size(buf))
            buf[n++] = makeMouseInput(MouseButton(b), false, 0, kTagInfClick);
    if (n == 0) return 0;
    UINT ok = send(buf, n);
    if (ok != n) {
        // Retry once - releasing is safety-critical.
        Sleep(1);
        ok += SendInput(n - ok, buf + ok, sizeof(INPUT));
        for (UINT i = 0; i < n; ++i) track(buf[i]);
        publish();
    }
    return ok;
}

} // namespace infclick
