#include "core/Paths.h"

#include <windows.h>
#include <shlobj.h>

#include <iterator>
#include <mutex>

namespace infclick::paths {

namespace {
std::mutex g_mu;
std::wstring g_dataDir;

bool isWritableDir(const std::wstring& dir)
{
    if (!ensureDir(dir)) return false;
    std::wstring probe = dir + L"\\.write_probe";
    HANDLE h = CreateFileW(probe.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
                           FILE_ATTRIBUTE_TEMPORARY | FILE_FLAG_DELETE_ON_CLOSE, nullptr);
    if (h == INVALID_HANDLE_VALUE) return false;
    CloseHandle(h);
    return true;
}
} // namespace

std::wstring exePath()
{
    std::wstring buf(MAX_PATH, L'\0');
    for (;;) {
        DWORD n = GetModuleFileNameW(nullptr, buf.data(), DWORD(buf.size()));
        if (n == 0) return {};
        if (n < buf.size()) {
            buf.resize(n);
            return buf;
        }
        buf.resize(buf.size() * 2);
    }
}

std::wstring exeDir()
{
    std::wstring p = exePath();
    size_t pos = p.find_last_of(L"\\/");
    return pos == std::wstring::npos ? p : p.substr(0, pos);
}

void overrideDataDir(const std::wstring& dir)
{
    std::lock_guard lk(g_mu);
    g_dataDir = dir;
    ensureDir(dir);
}

std::wstring dataDir()
{
    std::lock_guard lk(g_mu);
    if (!g_dataDir.empty()) return g_dataDir;

    wchar_t env[MAX_PATH * 2];
    DWORD n = GetEnvironmentVariableW(L"INFCLICK_DATA_DIR", env, DWORD(std::size(env)));
    if (n > 0 && n < std::size(env)) {
        g_dataDir = env;
        ensureDir(g_dataDir);
        return g_dataDir;
    }

    std::wstring portable = exeDir() + L"\\data";
    if (isWritableDir(portable)) {
        g_dataDir = portable;
        return g_dataDir;
    }

    PWSTR local = nullptr;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, nullptr, &local))) {
        g_dataDir = std::wstring(local) + L"\\InfinityClicker";
        CoTaskMemFree(local);
        ensureDir(g_dataDir);
    }
    return g_dataDir;
}

bool ensureDir(const std::wstring& dir)
{
    if (dir.empty()) return false;
    DWORD a = GetFileAttributesW(dir.c_str());
    if (a != INVALID_FILE_ATTRIBUTES) return (a & FILE_ATTRIBUTE_DIRECTORY) != 0;
    size_t pos = dir.find_last_of(L"\\/");
    if (pos != std::wstring::npos && pos > 2) ensureDir(dir.substr(0, pos));
    return CreateDirectoryW(dir.c_str(), nullptr) || GetLastError() == ERROR_ALREADY_EXISTS;
}

bool readFile(const std::wstring& path, std::string& out)
{
    HANDLE h = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING,
                           FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return false;
    LARGE_INTEGER size{};
    if (!GetFileSizeEx(h, &size) || size.QuadPart > 64ll * 1024 * 1024) {
        CloseHandle(h);
        return false;
    }
    out.resize(size_t(size.QuadPart));
    DWORD read = 0;
    BOOL ok = out.empty() ? TRUE : ReadFile(h, out.data(), DWORD(out.size()), &read, nullptr);
    CloseHandle(h);
    if (!ok || read != out.size()) return false;
    return true;
}

bool writeFileAtomic(const std::wstring& path, const std::string& data)
{
    std::wstring tmp = path + L".tmp";
    HANDLE h = CreateFileW(tmp.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return false;
    DWORD written = 0;
    BOOL ok = WriteFile(h, data.data(), DWORD(data.size()), &written, nullptr);
    FlushFileBuffers(h);
    CloseHandle(h);
    if (!ok || written != data.size()) {
        DeleteFileW(tmp.c_str());
        return false;
    }
    if (!MoveFileExW(tmp.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        DeleteFileW(tmp.c_str());
        return false;
    }
    return true;
}

} // namespace infclick::paths
