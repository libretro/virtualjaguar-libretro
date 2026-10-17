// The newest input, from the UI thread to the core's input poll on the emulation thread (#967).
//
// The UI thread runs InputManager::poll() (SDL_GetMouseState is main-thread only, and the routes, the keypad overlay
// and bind capture all live there) and publish()es the resulting CoreInput after every batch of events.  The core's
// input-poll callback take()s the newest one right before each frame, so input is never older than the last event
// batch.  Buttons and axes are states (the newest wins); pointer / rotary motion is a delta and is summed until a frame
// takes it, then delivered exactly once, however many publishes or frames fall in between.
#pragma once

#include "remap.h"

#include <cstdint>
#include <mutex>

namespace vj {

class InputSnapshot {
public:
    // UI thread.  `eventNs`: SDL timestamp (SDL_GetTicksNS base) of the oldest input event this state includes that
    // was not published before, 0 = none.
    void publish(const CoreInput& in, uint64_t eventNs) {
        std::lock_guard<std::mutex> l(mu_);
        CoreInput next = in;
        next.carryMotion(latest_);   // motion the emulation has not taken yet is kept, not replaced
        latest_ = next;
        if (eventNs && !eventNs_) eventNs_ = eventNs;
    }

    // Emulation thread (the core's input-poll callback).  Returns the oldest untaken event's timestamp (0 = none),
    // for the input-latency measurement.
    uint64_t take(CoreInput& out) {
        std::lock_guard<std::mutex> l(mu_);
        out = latest_;
        latest_.clearMotion();
        taken_++;
        const uint64_t ns = eventNs_;
        eventNs_ = 0;
        return ns;
    }

    // UI thread: frames that took input since the last call (the keypad overlay ticks once per emulated frame).
    unsigned takenSince() {
        std::lock_guard<std::mutex> l(mu_);
        const unsigned n = taken_;
        taken_ = 0;
        return n;
    }

private:
    std::mutex mu_;
    CoreInput latest_;
    uint64_t eventNs_ = 0;
    unsigned taken_ = 0;
};

}  // namespace vj
