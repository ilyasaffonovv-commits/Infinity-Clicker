#include "lab/TestPad.h"

#include "core/Clock.h"
#include "core/I18n.h"
#include "core/Str.h"
#include "input/InputAction.h"

#include <windowsx.h>

#include <cwchar>
#include <string>

namespace infclick {

namespace {
constexpr UINT_PTR kRepaintTimer = 1;
const wchar_t* kClass = L"InfinityClickerTestPad";
} // namespace

TestPad::TestPad() { ready_ = CreateEventW(nullptr, TRUE, FALSE, nullptr); }

TestPad::~TestPad()
{
    close();
    if (ready_) CloseHandle(ready_);
}

bool TestPad::open(Mode mode)
{
    if (isOpen()) {
        if (mode_ == mode) return true;
        close();
    }
    mode_ = mode;
    ResetEvent(ready_);
    thread_ = std::thread([this] { threadMain(); });
    WaitForSingleObject(ready_, 3000);
    return isOpen();
}

void TestPad::close()
{
    HWND h = hwnd_.load();
    if (h) PostMessageW(h, WM_CLOSE, 0, 0);
    if (thread_.joinable()) thread_.join();
    hwnd_.store(nullptr);
}

void TestPad::reset()
{
    for (auto& a : c_.btnDown) a.store(0);
    for (auto& a : c_.btnUp) a.store(0);
    for (auto& a : c_.wheel) a.store(0);
    c_.keyDown = 0;
    c_.keyUp = 0;
    c_.oursTotal = 0;
    c_.foreignTotal = 0;
    c_.firstQpc = 0;
    c_.lastQpc = 0;
    c_.lastLeftDownQpc = 0;
    c_.holdSumTicks = 0;
    c_.holdCount = 0;
    std::lock_guard lk(keyMu_);
    keys_.clear();
}

void TestPad::startRecording(size_t capacity)
{
    recording_.store(false);
    rec_.assign(capacity, 0);
    recN_.store(0);
    recording_.store(true);
}

std::vector<int64_t> TestPad::stopRecording()
{
    recording_.store(false);
    Sleep(1);
    size_t n = std::min(recN_.load(), rec_.size());
    return std::vector<int64_t>(rec_.begin(), rec_.begin() + ptrdiff_t(n));
}

std::vector<PadKeyRecord> TestPad::keyRecords()
{
    std::lock_guard lk(keyMu_);
    return keys_;
}

void TestPad::setStatusText(const std::wstring& s)
{
    {
        std::lock_guard lk(textMu_);
        status_ = s;
    }
    if (HWND h = hwnd_.load()) InvalidateRect(h, nullptr, FALSE);
}

bool TestPad::activate()
{
    HWND h = hwnd_.load();
    if (!h) return false;
    for (int attempt = 0; attempt < 6; ++attempt) {
        if (GetForegroundWindow() == h) return true;
        SetForegroundWindow(h);
        Sleep(30);
        if (GetForegroundWindow() == h) return true;
        // Foreground lock: a real (injected) click on the pad activates it.
        // In fullscreen mode the cursor is always over the pad.
        RECT r;
        GetWindowRect(h, &r);
        POINT p;
        GetCursorPos(&p);
        if (PtInRect(&r, p) && WindowFromPoint(p) == h) {
            INPUT in[2] = {makeMouseInput(MouseButton::Left, true, 0, kTagBench),
                           makeMouseInput(MouseButton::Left, false, 0, kTagBench)};
            SendInput(2, in, sizeof(INPUT));
        }
        Sleep(80);
    }
    return GetForegroundWindow() == h;
}

void TestPad::threadMain()
{
    SetThreadDescription(GetCurrentThread(), L"Infinity Clicker TestPad");
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_ABOVE_NORMAL);
    WNDCLASSEXW wc{sizeof wc};
    wc.lpfnWndProc = wndProc;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.lpszClassName = kClass;
    wc.hCursor = LoadCursorW(nullptr, IDC_CROSS);
    wc.hIcon = LoadIconW(GetModuleHandleW(nullptr), MAKEINTRESOURCEW(101));
    wc.hbrBackground = nullptr;
    RegisterClassExW(&wc); // no CS_DBLCLKS: every press arrives as a plain WM_xBUTTONDOWN

    HWND h;
    if (mode_ == Mode::Fullscreen) {
        const int x = GetSystemMetrics(SM_XVIRTUALSCREEN), y = GetSystemMetrics(SM_YVIRTUALSCREEN);
        const int w = GetSystemMetrics(SM_CXVIRTUALSCREEN), hh = GetSystemMetrics(SM_CYVIRTUALSCREEN);
        h = CreateWindowExW(WS_EX_TOPMOST | WS_EX_TOOLWINDOW, kClass, L"Infinity Clicker Benchmark Pad", WS_POPUP, x, y, w, hh,
                            nullptr, nullptr, wc.hInstance, this);
    } else {
        const UINT dpi = GetDpiForSystem();
        const int w = MulDiv(560, int(dpi), 96), hh = MulDiv(400, int(dpi), 96);
        h = CreateWindowExW(WS_EX_TOPMOST, kClass, toWide(tr("Infinity Clicker Test Pad - click target")).c_str(), WS_OVERLAPPEDWINDOW,
                            CW_USEDEFAULT, CW_USEDEFAULT, w, hh, nullptr, nullptr, wc.hInstance, this);
    }
    if (!h) {
        SetEvent(ready_);
        return;
    }
    hwnd_.store(h);
    ShowWindow(h, SW_SHOW);
    UpdateWindow(h);
    SetTimer(h, kRepaintTimer, mode_ == Mode::Fullscreen ? 500 : 100, nullptr);
    SetEvent(ready_);

    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        // No TranslateMessage: we want raw key messages, not WM_CHAR noise.
        DispatchMessageW(&msg);
    }
    hwnd_.store(nullptr);
    for (HFONT& f : fonts_) {
        if (f) DeleteObject(f);
        f = nullptr;
    }
    fontDpi_ = 0;
    bandRect_ = RECT{};
}

LRESULT CALLBACK TestPad::wndProc(HWND h, UINT msg, WPARAM wp, LPARAM lp)
{
    if (msg == WM_NCCREATE) {
        auto* cs = reinterpret_cast<CREATESTRUCTW*>(lp);
        SetWindowLongPtrW(h, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(cs->lpCreateParams));
    }
    auto* self = reinterpret_cast<TestPad*>(GetWindowLongPtrW(h, GWLP_USERDATA));
    if (self) return self->handle(h, msg, wp, lp);
    return DefWindowProcW(h, msg, wp, lp);
}

void TestPad::onButton(int button, bool down)
{
    const int64_t q = clk::now();
    const bool ours = GetMessageExtraInfo() == LPARAM(kTagInfClick);
    if (!ours) {
        c_.foreignTotal.fetch_add(1, std::memory_order_relaxed);
        return;
    }
    (down ? c_.btnDown : c_.btnUp)[button].fetch_add(1, std::memory_order_relaxed);
    c_.oursTotal.fetch_add(1, std::memory_order_relaxed);
    if (button == 0) {
        if (down) {
            c_.lastLeftDownQpc.store(q, std::memory_order_relaxed);
        } else if (int64_t d = c_.lastLeftDownQpc.exchange(0, std::memory_order_relaxed)) {
            c_.holdSumTicks.fetch_add(q - d, std::memory_order_relaxed);
            c_.holdCount.fetch_add(1, std::memory_order_relaxed);
        }
    }
    int64_t zero = 0;
    c_.firstQpc.compare_exchange_strong(zero, q);
    c_.lastQpc.store(q, std::memory_order_relaxed);
    if (down && recording_.load(std::memory_order_relaxed)) {
        size_t i = recN_.fetch_add(1, std::memory_order_relaxed);
        if (i < rec_.size()) rec_[i] = q;
    }
}

LRESULT TestPad::handle(HWND h, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg) {
    case WM_LBUTTONDOWN: onButton(0, true); return 0;
    case WM_LBUTTONUP: onButton(0, false); return 0;
    case WM_RBUTTONDOWN: onButton(1, true); return 0;
    case WM_RBUTTONUP: onButton(1, false); return 0;
    case WM_MBUTTONDOWN: onButton(2, true); return 0;
    case WM_MBUTTONUP: onButton(2, false); return 0;
    case WM_XBUTTONDOWN: onButton(GET_XBUTTON_WPARAM(wp) == XBUTTON1 ? 3 : 4, true); return TRUE;
    case WM_XBUTTONUP: onButton(GET_XBUTTON_WPARAM(wp) == XBUTTON1 ? 3 : 4, false); return TRUE;
    case WM_MOUSEWHEEL:
    case WM_MOUSEHWHEEL: {
        const bool ours = GetMessageExtraInfo() == LPARAM(kTagInfClick);
        if (!ours) {
            c_.foreignTotal.fetch_add(1);
            return 0;
        }
        const short d = GET_WHEEL_DELTA_WPARAM(wp);
        const int idx = msg == WM_MOUSEWHEEL ? (d > 0 ? 0 : 1) : (d > 0 ? 3 : 2);
        c_.wheel[idx].fetch_add(1);
        c_.oursTotal.fetch_add(1);
        c_.lastQpc.store(clk::now());
        return 0;
    }
    case WM_KEYDOWN:
    case WM_SYSKEYDOWN:
    case WM_KEYUP:
    case WM_SYSKEYUP: {
        const bool ours = GetMessageExtraInfo() == LPARAM(kTagInfClick);
        const bool down = msg == WM_KEYDOWN || msg == WM_SYSKEYDOWN;
        if (!ours) {
            c_.foreignTotal.fetch_add(1);
            return 0;
        }
        (down ? c_.keyDown : c_.keyUp).fetch_add(1);
        c_.oursTotal.fetch_add(1);
        c_.lastQpc.store(clk::now());
        if (down) {
            PadKeyRecord r;
            r.vk = uint16_t(wp);
            r.scan = uint16_t((lp >> 16) & 0xFF);
            r.extended = (lp >> 24) & 1;
            r.ours = true;
            if (GetKeyState(VK_CONTROL) & 0x8000) r.mods |= ModCtrl;
            if (GetKeyState(VK_SHIFT) & 0x8000) r.mods |= ModShift;
            if (GetKeyState(VK_MENU) & 0x8000) r.mods |= ModAlt;
            if ((GetKeyState(VK_LWIN) | GetKeyState(VK_RWIN)) & 0x8000) r.mods |= ModWin;
            std::lock_guard lk(keyMu_);
            if (keys_.size() < 4096) keys_.push_back(r);
        }
        return 0; // swallow: no system menu / Alt handling inside the pad
    }
    case WM_SYSCHAR:
    case WM_CHAR: return 0;
    case WM_MOUSEACTIVATE: return MA_ACTIVATE;
    case WM_TIMER:
        if (wp == kRepaintTimer) InvalidateRect(h, bandRect_.bottom > bandRect_.top ? &bandRect_ : nullptr, FALSE);
        return 0;
    case WM_ERASEBKGND: return 1;
    case WM_PAINT: paint(h); return 0;
    case WM_CLOSE:
        KillTimer(h, kRepaintTimer);
        DestroyWindow(h);
        return 0;
    case WM_DESTROY: PostQuitMessage(0); return 0;
    }
    return DefWindowProcW(h, msg, wp, lp);
}

void TestPad::paint(HWND h)
{
    PAINTSTRUCT ps;
    HDC dc = BeginPaint(h, &ps);
    const UINT dpi = GetDpiForWindow(h);
    RECT full;
    GetClientRect(h, &full);
    HBRUSH bg = CreateSolidBrush(RGB(12, 15, 26));
    // Full-screen pad: only a text band is redrawn (a full virtual-screen bitmap per
    // repaint would cost milliseconds and delay the input messages we are measuring).
    RECT rc = full;
    int destY = 0;
    if (mode_ == Mode::Fullscreen) {
        const int bandH = MulDiv(320, int(dpi), 96);
        destY = full.bottom / 3 - MulDiv(24, int(dpi), 96);
        bandRect_ = RECT{0, destY, full.right, destY + bandH};
        rc = RECT{0, 0, full.right, bandH};
        if (ps.rcPaint.top < bandRect_.top || ps.rcPaint.bottom > bandRect_.bottom) FillRect(dc, &ps.rcPaint, bg);
    }
    HDC mem = CreateCompatibleDC(dc);
    HBITMAP bmp = CreateCompatibleBitmap(dc, rc.right, rc.bottom);
    HGDIOBJ oldBmp = SelectObject(mem, bmp);
    FillRect(mem, &rc, bg);
    DeleteObject(bg);
    SetBkMode(mem, TRANSPARENT);

    if (fontDpi_ != dpi) { // fonts are cached: painting must stay cheap so input messages are never delayed
        for (HFONT& f : fonts_)
            if (f) DeleteObject(f);
        auto mkFont = [&](int pt, int weight) {
            return CreateFontW(-MulDiv(pt, int(dpi), 72), 0, 0, 0, weight, 0, 0, 0, DEFAULT_CHARSET, 0, 0,
                               CLEARTYPE_QUALITY, 0, L"Segoe UI");
        };
        fonts_[0] = mkFont(11, FW_SEMIBOLD);
        fonts_[1] = mkFont(34, FW_BOLD);
        fonts_[2] = mkFont(10, FW_NORMAL);
        fontDpi_ = dpi;
    }
    HFONT fTitle = fonts_[0], fBig = fonts_[1], fBody = fonts_[2];

    int cx = rc.right / 2;
    int y = MulDiv(mode_ == Mode::Fullscreen ? 24 : 18, int(dpi), 96);
    auto line = [&](HFONT f, COLORREF col, const std::wstring& s) {
        SelectObject(mem, f);
        SetTextColor(mem, col);
        RECT r{0, y, rc.right, y + 400};
        DrawTextW(mem, s.c_str(), -1, &r, DT_CENTER | DT_TOP | DT_SINGLELINE | DT_NOPREFIX);
        SIZE sz;
        GetTextExtentPoint32W(mem, L"Ag", 2, &sz);
        y += sz.cy + MulDiv(6, int(dpi), 96);
    };
    (void)cx;
    auto W = [](const std::string& s) { return toWide(s); };
    line(fTitle, RGB(34, 211, 238), W(tr("INFINITY CLICKER  -  INPUT LAB TEST PAD")));
    line(fBig, RGB(236, 240, 255), std::to_wstring(c_.oursTotal.load()));
    line(fBody, RGB(150, 160, 190), W(tr("synthetic events received by this window (GetMessageExtraInfo = Infinity Clicker tag)")));
    line(fBody, RGB(200, 206, 230),
         W(strFormat(tr("L %llu/%llu   R %llu/%llu   M %llu/%llu   X1 %llu/%llu   X2 %llu/%llu   (down/up)"),
                     c_.btnDown[0].load(), c_.btnUp[0].load(), c_.btnDown[1].load(), c_.btnUp[1].load(),
                     c_.btnDown[2].load(), c_.btnUp[2].load(), c_.btnDown[3].load(), c_.btnUp[3].load(),
                     c_.btnDown[4].load(), c_.btnUp[4].load())));
    line(fBody, RGB(200, 206, 230),
         W(strFormat(tr("Wheel up %llu  down %llu  left %llu  right %llu     Keys down %llu  up %llu"), c_.wheel[0].load(),
                     c_.wheel[1].load(), c_.wheel[2].load(), c_.wheel[3].load(), c_.keyDown.load(), c_.keyUp.load())));
    line(fBody, RGB(120, 128, 160), W(strFormat(tr("Your own (non-Infinity Clicker) input here: %llu"), c_.foreignTotal.load())));
    {
        std::lock_guard lk(textMu_);
        if (!status_.empty()) {
            y += MulDiv(10, int(dpi), 96);
            line(fTitle, RGB(250, 204, 21), status_);
        }
    }

    BitBlt(dc, 0, destY, rc.right, rc.bottom, mem, 0, 0, SRCCOPY);
    SelectObject(mem, oldBmp);
    DeleteObject(bmp);
    DeleteDC(mem);
    EndPaint(h, &ps);
}

} // namespace infclick
