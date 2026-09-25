// Infinity Clicker - autoclicker for Windows 10/11 (x64)
//
//   InfinityClicker.exe                       normal GUI (single instance, tray)
//   InfinityClicker.exe --bench [quick] ...   benchmark suite -> benchmarks\*.md
//   InfinityClicker.exe --autotest            functional acceptance tests -> benchmarks\autotest_results.md
//   InfinityClicker.exe --data <dir>          use a custom data folder
//   InfinityClicker.exe --guardian <pid>      (internal) stuck-key watchdog process
#include "app/App.h"
#include "core/I18n.h"
#include "core/Paths.h"
#include "core/Str.h"
#include "lab/AutoTest.h"
#include "lab/Bench.h"
#include "platform/win/Guardian.h"
#include "ui/Ui.h"

#include <windows.h>
#include <shellapi.h>

#include <string>
#include <vector>

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int)
{
    int argc = 0;
    LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    std::vector<std::wstring> args;
    for (int i = 1; i < argc; ++i) args.emplace_back(argv[i]);
    LocalFree(argv);

    auto has = [&](const wchar_t* a) {
        for (const auto& s : args)
            if (_wcsicmp(s.c_str(), a) == 0) return true;
        return false;
    };
    auto valueOf = [&](const wchar_t* a) -> std::wstring {
        for (size_t i = 0; i + 1 < args.size(); ++i)
            if (_wcsicmp(args[i].c_str(), a) == 0) return args[i + 1];
        return {};
    };

    if (std::wstring d = valueOf(L"--data"); !d.empty()) infclick::paths::overrideDataDir(d);
    // Until settings are loaded (and for --bench), follow the Windows UI language.
    infclick::i18n::setLang(infclick::i18n::systemLang());
    if (has(L"--guardian")) return infclick::guardian::runGuardian(DWORD(_wtoi(valueOf(L"--guardian").c_str())));

    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);

    if (has(L"--bench")) {
        std::vector<std::wstring> rest;
        bool after = false;
        for (const auto& a : args) {
            if (after) rest.push_back(a);
            if (_wcsicmp(a.c_str(), L"--bench") == 0) after = true;
        }
        return infclick::bench::run(infclick::bench::parseArgs(rest));
    }
    if (has(L"--autotest")) return infclick::autotest::run(valueOf(L"--out"));
    if (has(L"--kill-test")) {
        // Stuck-key safety check: hold Left Shift, then die WITHOUT any cleanup.
        // The guardian process must send the Shift UP (verified by the caller).
        infclick::HeldShared* held = infclick::guardian::createShared();
        infclick::guardian::spawnWatchdog();
        Sleep(400);
        infclick::InputSender s;
        s.attachShared(held);
        bool ext = false;
        const uint16_t sc = infclick::vkToScan(VK_LSHIFT, &ext);
        INPUT in = infclick::makeKeyInput(VK_LSHIFT, sc, ext, false, infclick::KeyInjectMode::ScanCode, infclick::kTagInfClick);
        s.send(&in, 1);
        Sleep(300);
        TerminateProcess(GetCurrentProcess(), 99); // simulates "End task" / crash with a key held
    }

    // Single instance: a second launch just brings the running one to front.
    HANDLE mutex = CreateMutexW(nullptr, TRUE, L"Local\\InfinityClicker.SingleInstance");
    if (mutex && GetLastError() == ERROR_ALREADY_EXISTS && !has(L"--restarted")) {
        if (HWND w = FindWindowW(L"InfinityClickerMainWindow", nullptr)) {
            DWORD pid = 0;
            GetWindowThreadProcessId(w, &pid);
            AllowSetForegroundWindow(pid);
            PostMessageW(w, infclick::WM_APP_SHOW, 0, 0);
        }
        CloseHandle(mutex);
        return 0;
    }
    if (has(L"--restarted") && mutex && GetLastError() == ERROR_ALREADY_EXISTS) {
        // The previous (non-elevated) instance is closing - wait for it.
        WaitForSingleObject(mutex, 5000);
    }

    int rc = 0;
    {
        infclick::App app;
        if (std::wstring lg = valueOf(L"--lang"); !lg.empty()) app.setLanguageOverride(infclick::toUtf8(lg));
        app.init();
        // --page main|advanced|diagnostics|lab|settings[:0-4]   --modal profiles|log   (used for screenshots)
        if (std::wstring pg = valueOf(L"--page"); !pg.empty()) {
            using infclick::ui::Page;
            const size_t colon = pg.find(L':');
            const int sub = colon == std::wstring::npos ? 0 : _wtoi(pg.c_str() + colon + 1);
            const std::wstring name = pg.substr(0, colon);
            if (name == L"advanced") infclick::ui::setPage(Page::Advanced, 0);
            else if (name == L"diagnostics") infclick::ui::setPage(Page::Advanced, 1);
            else if (name == L"lab") infclick::ui::setPage(Page::Advanced, 2);
            else if (name == L"settings") infclick::ui::setPage(Page::Settings, sub);
        }
        if (std::wstring md = valueOf(L"--modal"); md == L"profiles") infclick::ui::openProfileManager();
        else if (md == L"log") infclick::ui::openLog();
        rc = infclick::ui::run(app, has(L"--minimized"));
        app.shutdown();
    }
    if (mutex) {
        ReleaseMutex(mutex);
        CloseHandle(mutex);
    }
    return rc;
}
