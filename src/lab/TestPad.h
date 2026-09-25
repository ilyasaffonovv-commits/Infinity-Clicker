#pragma once
// Input Lab receiver: a plain Win32 window running on its OWN thread (so ImGui
// rendering can never delay it) that counts the input messages an ordinary
// application actually receives (telemetry stage 4 "received by app").
//
// It separates our own events (GetMessageExtraInfo() == kTagInfClick) from
// the user's real clicks, records receive timestamps for latency analysis
// and remembers modifier state seen with each key-down (combo verification).
#include "input/KeyChord.h"

#include <windows.h>

#include <atomic>
#include <cstdint>
#include <mutex>
#include <thread>
#include <vector>

namespace infclick {

struct PadKeyRecord {
    uint16_t vk = 0;
    uint16_t scan = 0;
    bool extended = false;
    uint8_t mods = 0; // GetKeyState() modifiers at the time of the key-down
    bool ours = false;
};

struct PadCounters {
    std::atomic<uint64_t> btnDown[5]{};  // ours, by MouseButton (Left..X2)
    std::atomic<uint64_t> btnUp[5]{};
    std::atomic<uint64_t> wheel[4]{};    // ours: up, down, left, right notches
    std::atomic<uint64_t> keyDown{0}, keyUp{0};
    std::atomic<uint64_t> oursTotal{0};  // all our events
    std::atomic<uint64_t> foreignTotal{0};
    std::atomic<int64_t> firstQpc{0}, lastQpc{0};
    // Left-button hold time as observed by the app (receipt of UP - receipt of DOWN).
    std::atomic<int64_t> lastLeftDownQpc{0};
    std::atomic<int64_t> holdSumTicks{0};
    std::atomic<uint64_t> holdCount{0};
};

class TestPad {
public:
    enum class Mode : uint8_t { Windowed, Fullscreen };

    TestPad();
    ~TestPad();

    bool open(Mode mode);
    void close();
    bool isOpen() const { return hwnd_.load() != nullptr; }
    HWND hwnd() const { return hwnd_.load(); }
    Mode mode() const { return mode_; }

    void reset();
    PadCounters& counters() { return c_; }
    uint64_t received() const { return c_.oursTotal.load(); }
    uint64_t downs(MouseButton b) const { return c_.btnDown[int(b)].load(); }
    uint64_t ups(MouseButton b) const { return c_.btnUp[int(b)].load(); }

    // Receive-time recording of our button-down messages (bench latency/interval analysis).
    void startRecording(size_t capacity);
    std::vector<int64_t> stopRecording();

    std::vector<PadKeyRecord> keyRecords();

    // Bring the pad to the foreground. Uses a tagged click on the pad itself if
    // SetForegroundWindow is refused (foreground lock rules).
    bool activate();

    void setStatusText(const std::wstring& s);

private:
    static LRESULT CALLBACK wndProc(HWND h, UINT msg, WPARAM wp, LPARAM lp);
    LRESULT handle(HWND h, UINT msg, WPARAM wp, LPARAM lp);
    void threadMain();
    void paint(HWND h);
    void onButton(int button, bool down);

    std::thread thread_;
    std::atomic<HWND> hwnd_{nullptr};
    HANDLE ready_ = nullptr;
    Mode mode_ = Mode::Windowed;
    PadCounters c_;

    std::atomic<bool> recording_{false};
    std::vector<int64_t> rec_;
    std::atomic<size_t> recN_{0};

    std::mutex keyMu_;
    std::vector<PadKeyRecord> keys_;

    std::mutex textMu_;
    std::wstring status_;

    HFONT fonts_[3]{};
    UINT fontDpi_ = 0;
    RECT bandRect_{};
};

} // namespace infclick
