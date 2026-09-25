#include "ui/Ui.h"

#include "core/I18n.h"
#include "core/Str.h"

#include <shellapi.h>

namespace infclick::ui {

namespace {
constexpr UINT kTrayId = 1;
enum : UINT { ID_START = 100, ID_STOP, ID_OPEN, ID_EXIT, ID_PROFILE0 = 200 };
} // namespace

bool Tray::add(HWND owner, HICON normal, HICON active)
{
    owner_ = owner;
    normal_ = normal;
    active_ = active;
    NOTIFYICONDATAW nid{sizeof nid};
    nid.hWnd = owner_;
    nid.uID = kTrayId;
    nid.uFlags = NIF_ICON | NIF_TIP | NIF_MESSAGE | NIF_SHOWTIP;
    nid.uCallbackMessage = WM_APP_TRAY;
    nid.hIcon = normal_;
    wcscpy_s(nid.szTip, L"Infinity Clicker");
    added_ = Shell_NotifyIconW(NIM_ADD, &nid) != FALSE;
    if (added_) {
        nid.uVersion = NOTIFYICON_VERSION_4;
        Shell_NotifyIconW(NIM_SETVERSION, &nid);
    }
    return added_;
}

void Tray::readd()
{
    added_ = false;
    add(owner_, normal_, active_);
}

void Tray::remove()
{
    if (!added_) return;
    NOTIFYICONDATAW nid{sizeof nid};
    nid.hWnd = owner_;
    nid.uID = kTrayId;
    Shell_NotifyIconW(NIM_DELETE, &nid);
    added_ = false;
}

void Tray::update(EngineState s, const std::string& tip)
{
    if (!added_) return;
    const bool active = s == EngineState::Running;
    std::wstring wtip = toWide(tip);
    if (active == activeShown_ && wtip == tip_) return;
    activeShown_ = active;
    tip_ = wtip;
    NOTIFYICONDATAW nid{sizeof nid};
    nid.hWnd = owner_;
    nid.uID = kTrayId;
    nid.uFlags = NIF_ICON | NIF_TIP | NIF_SHOWTIP;
    nid.hIcon = active ? active_ : normal_;
    wcsncpy_s(nid.szTip, wtip.c_str(), _TRUNCATE);
    Shell_NotifyIconW(NIM_MODIFY, &nid);
}

void Tray::showMenu(App& app, HWND owner)
{
    HMENU m = CreatePopupMenu();
    HMENU pm = CreatePopupMenu();
    const auto& profiles = app.store().profiles();
    for (size_t i = 0; i < profiles.size() && i < 50; ++i) {
        UINT flags = MF_STRING | (profiles[i].name == app.settings().activeProfile ? MF_CHECKED : 0);
        AppendMenuW(pm, flags, ID_PROFILE0 + UINT(i), toWide(profiles[i].name).c_str());
    }
    const bool armed = app.armed();
    AppendMenuW(m, MF_STRING | (armed ? MF_GRAYED : 0), ID_START, toWide(tr("Start (arm)")).c_str());
    AppendMenuW(m, MF_STRING | (!armed && !app.engine().running() ? MF_GRAYED : 0), ID_STOP, toWide(tr("Stop")).c_str());
    AppendMenuW(m, MF_POPUP, reinterpret_cast<UINT_PTR>(pm), toWide(tr("Profile")).c_str());
    AppendMenuW(m, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(m, MF_STRING, ID_OPEN, toWide(tr("Open Infinity Clicker")).c_str());
    AppendMenuW(m, MF_STRING, ID_EXIT, toWide(tr("Exit")).c_str());
    SetMenuDefaultItem(m, ID_OPEN, FALSE);
    POINT pt;
    GetCursorPos(&pt);
    SetForegroundWindow(owner); // required so the menu closes when clicking elsewhere
    const UINT cmd = TrackPopupMenu(m, TPM_RETURNCMD | TPM_RIGHTBUTTON | TPM_NONOTIFY, pt.x, pt.y, 0, owner, nullptr);
    DestroyMenu(m);
    switch (cmd) {
    case ID_START: app.arm(true); break;
    case ID_STOP: app.arm(false); break;
    case ID_OPEN: PostMessageW(owner, WM_APP_SHOW, 0, 0); break;
    case ID_EXIT: PostMessageW(owner, WM_CLOSE, 1, 0); break;
    default:
        if (cmd >= ID_PROFILE0 && cmd < ID_PROFILE0 + profiles.size()) app.selectProfile(profiles[cmd - ID_PROFILE0].name);
        break;
    }
}

} // namespace infclick::ui
