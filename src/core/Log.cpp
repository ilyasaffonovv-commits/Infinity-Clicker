#include "core/Log.h"

#include "core/Str.h"

#include <windows.h>

#include <atomic>
#include <condition_variable>
#include <cstdarg>
#include <deque>
#include <mutex>
#include <thread>

namespace infclick::log {

namespace {

constexpr size_t kRingLines = 400;
constexpr uint64_t kRotateBytes = 2ull * 1024 * 1024;

struct State {
    std::mutex mu;
    std::condition_variable cv;
    std::deque<std::string> ring;     // UI ring
    std::vector<std::string> pending; // lines waiting for the file writer
    std::wstring dir, path, oldPath;
    std::thread writer;
    bool quit = false;
    std::atomic<bool> fileOn{false};
    std::atomic<bool> debugOn{false};
    std::atomic<uint64_t> counter{0};
    bool started = false;
};

State& S()
{
    static State s;
    return s;
}

const char* levelTag(Level l)
{
    switch (l) {
    case Level::Error: return "ERR ";
    case Level::Warn: return "WARN";
    case Level::Info: return "INFO";
    case Level::Debug: return "DBG ";
    }
    return "?";
}

void appendToFile(const std::vector<std::string>& lines)
{
    State& s = S();
    if (s.path.empty() || lines.empty()) return;
    HANDLE h = CreateFileW(s.path.c_str(), FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                           OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return;
    LARGE_INTEGER size{};
    GetFileSizeEx(h, &size);
    if (uint64_t(size.QuadPart) > kRotateBytes) {
        CloseHandle(h);
        MoveFileExW(s.path.c_str(), s.oldPath.c_str(), MOVEFILE_REPLACE_EXISTING);
        h = CreateFileW(s.path.c_str(), FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_ALWAYS,
                        FILE_ATTRIBUTE_NORMAL, nullptr);
        if (h == INVALID_HANDLE_VALUE) return;
    }
    std::string blob;
    for (const auto& l : lines) {
        blob += l;
        blob += "\r\n";
    }
    DWORD written = 0;
    WriteFile(h, blob.data(), DWORD(blob.size()), &written, nullptr);
    CloseHandle(h);
}

void writerLoop()
{
    State& s = S();
    std::unique_lock lk(s.mu);
    while (!s.quit) {
        s.cv.wait_for(lk, std::chrono::milliseconds(500), [&] { return s.quit || s.pending.size() > 200; });
        if (s.pending.empty()) continue;
        std::vector<std::string> batch;
        batch.swap(s.pending);
        lk.unlock();
        if (s.fileOn.load()) appendToFile(batch);
        lk.lock();
    }
    if (!s.pending.empty() && s.fileOn.load()) {
        std::vector<std::string> batch;
        batch.swap(s.pending);
        lk.unlock();
        appendToFile(batch);
    }
}

} // namespace

void init(const std::wstring& logDir)
{
    State& s = S();
    std::lock_guard lk(s.mu);
    if (s.started) return;
    s.dir = logDir;
    if (!logDir.empty()) {
        CreateDirectoryW(logDir.c_str(), nullptr);
        s.path = logDir + L"\\InfinityClicker.log";
        s.oldPath = logDir + L"\\InfinityClicker.old.log";
    }
    s.quit = false;
    s.writer = std::thread(writerLoop);
    s.started = true;
}

void shutdown()
{
    State& s = S();
    {
        std::lock_guard lk(s.mu);
        if (!s.started) return;
        s.quit = true;
    }
    s.cv.notify_all();
    if (s.writer.joinable()) s.writer.join();
    std::lock_guard lk(s.mu);
    s.started = false;
}

void setFileEnabled(bool e) { S().fileOn.store(e); }
bool fileEnabled() { return S().fileOn.load(); }
void setVerbose(bool d) { S().debugOn.store(d); }
bool verbose() { return S().debugOn.load(); }

void write(Level lvl, const char* fmt, ...)
{
    State& s = S();
    if (lvl == Level::Debug && !s.debugOn.load(std::memory_order_relaxed)) return;
    va_list ap;
    va_start(ap, fmt);
    std::string msg = strFormatV(fmt, ap);
    va_end(ap);

    SYSTEMTIME st;
    GetLocalTime(&st);
    std::string line = strFormat("%04u-%02u-%02u %02u:%02u:%02u.%03u [%s] %s", st.wYear, st.wMonth, st.wDay, st.wHour,
                                 st.wMinute, st.wSecond, st.wMilliseconds, levelTag(lvl), msg.c_str());
    {
        std::lock_guard lk(s.mu);
        s.ring.push_back(line);
        while (s.ring.size() > kRingLines) s.ring.pop_front();
        if (s.fileOn.load(std::memory_order_relaxed) && s.started) s.pending.push_back(std::move(line));
        s.counter.fetch_add(1, std::memory_order_relaxed);
    }
    if (lvl == Level::Error) s.cv.notify_one();
}

void flush()
{
    State& s = S();
    std::vector<std::string> batch;
    {
        std::lock_guard lk(s.mu);
        batch.swap(s.pending);
    }
    if (s.fileOn.load()) appendToFile(batch);
}

std::vector<std::string> tail(size_t maxLines)
{
    State& s = S();
    std::lock_guard lk(s.mu);
    size_t n = std::min(maxLines, s.ring.size());
    return std::vector<std::string>(s.ring.end() - ptrdiff_t(n), s.ring.end());
}

uint64_t lineCounter() { return S().counter.load(std::memory_order_relaxed); }

std::wstring filePath() { return S().path; }

} // namespace infclick::log
