// Dev-only stall probe (#1026): with VJ_PERF_TRACE=<prefix>, the late latch's blocking calls on the UI thread
// (SDL_WaitEventTimeout, SDL_DelayPrecise) are bracketed by enter()/leave().  A call that returns more than
// kStallNs past the time it was asked to return by is a stall, and <prefix>-stall.txt records for each one:
//
//   - the UI thread's CPU time during the call (busy in code vs blocked or preempted),
//   - the main run loop's activity transitions during the call (CFRunLoopObserver: when the run loop went to sleep,
//     when the kernel woke it, which phases it then ran),
//   - samples taken by a watcher thread once the call is kSampleNs late: the UI thread's scheduler state
//     (thread_basic_info run_state, cpu_usage) and its stack (frame-pointer walk, symbolized when written).
//
// Every call's CPU time also goes into the UI trace row (wait_cpu_ns).  macOS only; elsewhere the probe is empty.
// Nothing runs unless the trace is on, and the watcher samples only calls that are already late.
#pragma once

#include <cstdint>
#include <string>

namespace vj {

class StallProbe {
public:
    static constexpr uint64_t kStallNs = 3000000;    // a call this late is a stall
    static constexpr uint64_t kSampleNs = 1000000;   // the watcher starts sampling a call this late

    // UI thread (the main thread): starts the watcher and the run-loop observer.  False where unsupported.
    bool start(const std::string& prefix);
    void stop();                              // UI thread: joins the watcher, writes <prefix>-stall.txt
    bool on() const { return on_; }

    // UI thread, around one blocking call that should return by dueNs (SDL_GetTicksNS).  kind: 1 = wait-event,
    // 2 = precise delay.  row = the UI trace row this call belongs to.
    void enter(uint64_t dueNs, unsigned char kind, size_t row);
    void leave();

    uint64_t takeCpuNs();   // UI thread: CPU time inside calls since the last take (for the trace row)

    ~StallProbe();

private:
    struct Impl;
    Impl* p_ = nullptr;
    bool on_ = false;
};

}  // namespace vj
