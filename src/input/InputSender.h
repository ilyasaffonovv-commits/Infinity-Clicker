#pragma once
// The only place in Infinity Clicker that calls SendInput for output actions.
//  * counts calls / requested / accepted events (telemetry stage 1 and 2)
//  * tracks every key/button left DOWN and mirrors it into shared memory so the
//    guardian process (and crash handler) can release it
//  * releaseAll() for stop / emergency / exit paths
#include "input/HeldState.h"
#include "input/InputAction.h"

#include <atomic>
#include <cstdint>

namespace infclick {

class InputSender {
public:
    InputSender() = default;
    InputSender(const InputSender&) = delete;
    InputSender& operator=(const InputSender&) = delete;

    void attachShared(HeldShared* shared) { shared_ = shared; }

    // Hot path: one SendInput call. Returns events accepted by Windows.
    UINT send(const INPUT* in, UINT n);

    // Sends UP for everything we hold. Engine thread (or after it stopped).
    UINT releaseAll();
    bool anyHeld() const { return heldMouse_ != 0 || heldKeyCount_ != 0; }

    // Counters (written by engine thread, read anywhere with relaxed loads).
    std::atomic<uint64_t> calls{0};
    std::atomic<uint64_t> requested{0};
    std::atomic<uint64_t> accepted{0};
    std::atomic<uint64_t> failedCalls{0};
    std::atomic<uint32_t> lastError{0};

private:
    void track(const INPUT& in);
    void publish();

    HeldShared* shared_ = nullptr;
    uint32_t heldMouse_ = 0;
    uint32_t heldKeys_[8]{};
    uint16_t keyMeta_[256]{};
    int heldKeyCount_ = 0;
    bool dirty_ = false;
};

} // namespace infclick
