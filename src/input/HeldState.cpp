#include "input/HeldState.h"

#include "input/InputAction.h"

#include <cwchar>
#include <iterator>

namespace infclick {

HeldShared* openHeldShared(DWORD ownerPid, bool create, HANDLE* outMapping)
{
    wchar_t name[96];
    swprintf_s(name, L"Local\\InfinityClicker.Held.%lu", ownerPid);
    HANDLE m = create ? CreateFileMappingW(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE, 0, sizeof(HeldShared), name)
                      : OpenFileMappingW(FILE_MAP_READ | FILE_MAP_WRITE, FALSE, name);
    if (!m) return nullptr;
    auto* h = static_cast<HeldShared*>(MapViewOfFile(m, FILE_MAP_READ | FILE_MAP_WRITE, 0, 0, sizeof(HeldShared)));
    if (!h) {
        CloseHandle(m);
        return nullptr;
    }
    if (create) {
        ZeroMemory(h, sizeof(HeldShared));
        h->magic = HeldShared::kMagic;
        h->version = 1;
        h->ownerPid = LONG(ownerPid);
    }
    if (outMapping) *outMapping = m;
    return h;
}

UINT releaseHeldFrom(HeldShared& h, ULONG_PTR tag)
{
    if (h.magic != HeldShared::kMagic) return 0;
    INPUT buf[64];
    UINT n = 0;
    // Keys first (so a held Ctrl does not turn the mouse-up into Ctrl+click semantics
    // for apps that sample modifier state on button-up), then mouse buttons.
    for (int w = 0; w < 8; ++w) {
        LONG bits = InterlockedExchange(&h.keyBits[w], 0);
        for (int b = 0; b < 32 && bits; ++b) {
            if (!(bits & (1L << b))) continue;
            bits &= ~(1L << b);
            uint16_t vk = uint16_t(w * 32 + b);
            uint16_t meta = h.keyScan[vk];
            uint16_t scan = meta & 0xFF;
            bool ext = (meta & 0x100) != 0;
            KeyInjectMode mode = (meta & 0x200) ? KeyInjectMode::VirtualKey : KeyInjectMode::ScanCode;
            if (n < std::size(buf)) buf[n++] = makeKeyInput(vk, scan, ext, true, mode, tag);
        }
    }
    LONG mm = InterlockedExchange(&h.mouseMask, 0);
    for (int b = 0; b <= int(MouseButton::X2); ++b)
        if ((mm & (1L << b)) && n < std::size(buf)) buf[n++] = makeMouseInput(MouseButton(b), false, 0, tag);
    if (n == 0) return 0;
    return SendInput(n, buf, sizeof(INPUT));
}

} // namespace infclick
