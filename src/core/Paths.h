#pragma once
#include <string>

namespace infclick::paths {

std::wstring exePath();
std::wstring exeDir();

// Portable-first data directory:
//   1. %INFCLICK_DATA_DIR% if set (used by automated tests for isolation)
//   2. <exe dir>\data            if writable  (portable mode, default)
//   3. %LOCALAPPDATA%\InfinityClicker     fallback (e.g. EXE in Program Files)
// Layout: settings.json, profiles.json, logs/, crash/ subfolders.
std::wstring dataDir();
void overrideDataDir(const std::wstring& dir);

bool readFile(const std::wstring& path, std::string& out);
// Atomic write: temp file + MoveFileEx(REPLACE_EXISTING | WRITE_THROUGH).
bool writeFileAtomic(const std::wstring& path, const std::string& data);
bool ensureDir(const std::wstring& dir);

} // namespace infclick::paths
