#pragma once
// Asynchronous, aggregating logger.
//  * Callers only append to an in-memory queue under a short lock; a background
//    thread does the file I/O, so the scheduler never touches the disk.
//  * High-rate events (per-click) must NOT be logged individually - the engine
//    aggregates them into once-per-second summaries.
//  * File is rotated at 2 MB (InfinityClicker.log -> InfinityClicker.old.log).
#include <cstdint>
#include <string>
#include <vector>

namespace infclick::log {

enum class Level : uint8_t { Error = 0, Warn = 1, Info = 2, Debug = 3 };

void init(const std::wstring& logDir);
void shutdown();

// File logging on/off (in-memory ring used by the UI always stays active for Info+).
void setFileEnabled(bool enabled);
bool fileEnabled();
void setVerbose(bool debug); // include Debug level
bool verbose();

void write(Level lvl, const char* fmt, ...);
void flush();

// Last N lines for the UI log view (newest last).
std::vector<std::string> tail(size_t maxLines);
uint64_t lineCounter(); // increments on every accepted line (UI change detection)
std::wstring filePath();

} // namespace infclick::log

#define TLOG_E(...) ::infclick::log::write(::infclick::log::Level::Error, __VA_ARGS__)
#define TLOG_W(...) ::infclick::log::write(::infclick::log::Level::Warn, __VA_ARGS__)
#define TLOG_I(...) ::infclick::log::write(::infclick::log::Level::Info, __VA_ARGS__)
#define TLOG_D(...) ::infclick::log::write(::infclick::log::Level::Debug, __VA_ARGS__)
