#pragma once
// Cross-process record of every key/button Infinity Clicker currently holds DOWN.
// Lives in a named shared-memory section so that the guardian process can send
// the matching UP events if Infinity Clicker is killed or crashes (Windows never
// auto-releases keys injected by a dead process -> "stuck Ctrl" otherwise).
#include <windows.h>

#include <cstdint>

namespace infclick {

struct HeldShared {
    static constexpr uint32_t kMagic = 0x44484C54; // 'TLHD'
    uint32_t magic;
    uint32_t version;
    volatile LONG mouseMask;   // bit i = MouseButton(i) held (Left..X2)
    volatile LONG keyBits[8];  // 256-bit VK set
    uint16_t keyScan[256];     // scan | 0x100 (extended) | 0x200 (VK mode) per VK
    volatile LONG cleanExit;   // set to 1 by an orderly shutdown
    volatile LONG ownerPid;
};

// Builds and sends UP events for everything recorded in `h`, then clears it.
// Safe to call from a crash handler: no heap allocation. Returns #inputs sent.
UINT releaseHeldFrom(HeldShared& h, ULONG_PTR tag);

// Creates/opens the named section for the given owner PID.
HeldShared* openHeldShared(DWORD ownerPid, bool create, HANDLE* outMapping);

} // namespace infclick
