#include "platform/win/Guardian.h"

#include "core/Log.h"
#include "core/Paths.h"
#include "input/InputAction.h"

#include <dbghelp.h>

#include <cstdio>
#include <cstdlib>
#include <exception>

namespace infclick::guardian {

namespace {
HeldShared* g_shared = nullptr;
HANDLE g_mapping = nullptr;
HANDLE g_child = nullptr;
std::wstring g_dumpDir;

using MiniDumpFn = BOOL(WINAPI*)(HANDLE, DWORD, HANDLE, MINIDUMP_TYPE, PMINIDUMP_EXCEPTION_INFORMATION,
                                 PMINIDUMP_USER_STREAM_INFORMATION, PMINIDUMP_CALLBACK_INFORMATION);
MiniDumpFn g_miniDump = nullptr;
volatile LONG g_inCrash = 0;

void writeDump(EXCEPTION_POINTERS* ep)
{
    if (!g_miniDump || g_dumpDir.empty()) return;
    SYSTEMTIME st;
    GetLocalTime(&st);
    wchar_t path[MAX_PATH * 2];
    swprintf_s(path, L"%s\\infclick_%04u%02u%02u_%02u%02u%02u.dmp", g_dumpDir.c_str(), st.wYear, st.wMonth, st.wDay,
               st.wHour, st.wMinute, st.wSecond);
    CreateDirectoryW(g_dumpDir.c_str(), nullptr);
    HANDLE f = CreateFileW(path, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (f == INVALID_HANDLE_VALUE) return;
    MINIDUMP_EXCEPTION_INFORMATION mei{GetCurrentThreadId(), ep, FALSE};
    g_miniDump(GetCurrentProcess(), GetCurrentProcessId(), f, MiniDumpNormal, ep ? &mei : nullptr, nullptr, nullptr);
    CloseHandle(f);
}

void crashRelease()
{
    if (InterlockedExchange(&g_inCrash, 1)) return;
    if (g_shared) releaseHeldFrom(*g_shared, kTagInfClick); // allocation-free
}

LONG WINAPI unhandledFilter(EXCEPTION_POINTERS* ep)
{
    crashRelease();
    writeDump(ep);
    return EXCEPTION_EXECUTE_HANDLER;
}

void onTerminate()
{
    crashRelease();
    writeDump(nullptr);
    std::abort();
}

void onPurecall()
{
    crashRelease();
    std::abort();
}

void onInvalidParam(const wchar_t*, const wchar_t*, const wchar_t*, unsigned, uintptr_t)
{
    crashRelease();
    std::abort();
}

} // namespace

HeldShared* createShared()
{
    if (g_shared) return g_shared;
    g_shared = openHeldShared(GetCurrentProcessId(), true, &g_mapping);
    return g_shared;
}

bool spawnWatchdog()
{
    if (!g_shared) return false;
    std::wstring exe = paths::exePath();
    wchar_t cmd[MAX_PATH * 4 + 64];
    swprintf_s(cmd, L"\"%s\" --guardian %lu --data \"%s\"", exe.c_str(), GetCurrentProcessId(), paths::dataDir().c_str());
    STARTUPINFOW si{sizeof si};
    PROCESS_INFORMATION pi{};
    if (!CreateProcessW(exe.c_str(), cmd, nullptr, nullptr, FALSE, CREATE_NO_WINDOW | BELOW_NORMAL_PRIORITY_CLASS,
                        nullptr, nullptr, &si, &pi)) {
        TLOG_W("guardian: could not start watchdog process (error %lu)", GetLastError());
        return false;
    }
    CloseHandle(pi.hThread);
    g_child = pi.hProcess;
    TLOG_I("guardian: watchdog pid %lu protects against stuck keys if Infinity Clicker is killed", pi.dwProcessId);
    return true;
}

void markCleanExit()
{
    if (g_shared) InterlockedExchange(&g_shared->cleanExit, 1);
    if (g_child) {
        CloseHandle(g_child);
        g_child = nullptr;
    }
}

int runGuardian(DWORD parentPid)
{
    HANDLE parent = OpenProcess(SYNCHRONIZE, FALSE, parentPid);
    if (!parent) return 2;
    HANDLE mapping = nullptr;
    // Open the section while the parent is alive; our handle keeps it valid afterwards.
    HeldShared* shared = nullptr;
    for (int i = 0; i < 50 && !shared; ++i) {
        shared = openHeldShared(parentPid, false, &mapping);
        if (!shared) Sleep(20);
    }
    if (!shared) {
        CloseHandle(parent);
        return 3;
    }
    WaitForSingleObject(parent, INFINITE);
    CloseHandle(parent);
    int rc = 0;
    if (!shared->cleanExit) {
        UINT n = releaseHeldFrom(*shared, kTagInfClick);
        // Leave a trace in the log (plain append, the main logger is gone).
        paths::ensureDir(paths::dataDir() + L"\\logs");
        std::wstring logPath = paths::dataDir() + L"\\logs\\InfinityClicker.log";
        HANDLE f = CreateFileW(logPath.c_str(), FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_ALWAYS,
                               FILE_ATTRIBUTE_NORMAL, nullptr);
        if (f != INVALID_HANDLE_VALUE) {
            char line[200];
            SYSTEMTIME st;
            GetLocalTime(&st);
            int len = std::snprintf(line, sizeof line,
                                    "%04u-%02u-%02u %02u:%02u:%02u.%03u [WARN] guardian: Infinity Clicker (pid %lu) exited "
                                    "unexpectedly - released %u held input(s)\r\n",
                                    st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond, st.wMilliseconds,
                                    parentPid, n);
            DWORD w;
            WriteFile(f, line, DWORD(len), &w, nullptr);
            CloseHandle(f);
        }
        rc = n ? 10 : 0;
    }
    UnmapViewOfFile(shared);
    CloseHandle(mapping);
    return rc;
}

void installCrashHandler(HeldShared* shared, const std::wstring& dumpDir)
{
    g_shared = shared ? shared : g_shared;
    g_dumpDir = dumpDir;
    if (HMODULE dbg = LoadLibraryExW(L"dbghelp.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32))
        g_miniDump = reinterpret_cast<MiniDumpFn>(GetProcAddress(dbg, "MiniDumpWriteDump"));
    SetUnhandledExceptionFilter(unhandledFilter);
    std::set_terminate(onTerminate);
    _set_purecall_handler(onPurecall);
    _set_invalid_parameter_handler(onInvalidParam);
}

} // namespace infclick::guardian
