// EmuThread (emu_thread.h): lifecycle, ordering, pause / advance, shutdown races, against a fake client -- no core,
// no SDL (#967).
//
//   vj_emu_thread_test
#include "emu_thread.h"
#include "precise_sleep.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <mmsystem.h>   // timeBeginPeriod: see main()
#endif

namespace vj {
struct EmuThreadTestAccess {
    static bool advancePublished(EmuThread& e) {
        // The owner is gated after drain: old code publishes the budget directly, fixed code queues it (on the debug
        // lane) behind the one pending mutation. Neither can be consumed until the test releases the gate.
        if (e.advanceLeft_.load() != 0) return true;
        std::lock_guard<std::mutex> lock(e.cmds_.mu_);
        return !e.cmds_.dbg_.empty();
    }
    static size_t debugQueued(EmuThread& e) {
        std::lock_guard<std::mutex> lock(e.cmds_.mu_);
        return e.cmds_.dbg_.size();
    }
};
}  // namespace vj

using vj::EmuThread;
using Clock = std::chrono::steady_clock;

namespace {

int g_fail = 0;
#define CHECK(c, ...)                                        \
    do {                                                     \
        if (!(c)) {                                          \
            std::printf("FAIL %s:%d: ", __FILE__, __LINE__); \
            std::printf(__VA_ARGS__);                        \
            std::printf("\n");                               \
            g_fail++;                                        \
        }                                                    \
    } while (0)

double since(Clock::time_point t0) { return std::chrono::duration<double>(Clock::now() - t0).count(); }
void sleepMs(int ms) { std::this_thread::sleep_for(std::chrono::milliseconds(ms)); }

struct FakeClient : EmuThread::Client {
    std::atomic<int> frames{0};
    std::atomic<bool> loaded{true};
    std::atomic<bool> block{false};     // runFrame waits while set: a core stuck inside retro_run
    std::atomic<bool> blocked{false};
    std::atomic<int> holdAt{-1};        // runFrame holds, after counting its frame, while frames == holdAt
    std::atomic<int> started{0}, stopping{0}, afterCmds{0};
    std::atomic<int> serveNow{0};       // each count runs onBlocked once, on the emulation thread, inside the blocked frame
    std::function<void()> onBlocked;    // set BEFORE start(), never reassigned while the thread runs
    std::thread::id startedOn;
    std::atomic<double> period{0.010};
    std::mutex errMu;
    std::vector<std::string> errors;

    void threadStarted() override {
        startedOn = std::this_thread::get_id();
        started++;
    }
    void threadStopping() override { stopping++; }
    bool canRun() override { return loaded.load(); }
    double framePeriod() override { return period.load(); }
    double audioQueuedMs() override { return -1.0; }
    void runFrame() override {
        while (block.load()) {
            blocked = true;
            if (onBlocked && serveNow.load() > 0) {
                serveNow--;
                onBlocked();
            }
            sleepMs(1);
        }
        frames++;
        while (holdAt.load() == frames.load()) sleepMs(1);
    }
    void afterCommands() override { afterCmds++; }
    void commandFailed(const std::string& what) override {
        std::lock_guard<std::mutex> l(errMu);
        errors.push_back(what);
    }
};

void testInline() {
    EmuThread e;
    FakeClient c;
    CHECK(e.start(EmuThread::Mode::Inline, c), "inline start");
    CHECK(!e.threaded() && !e.onEmuThread(), "inline: no thread");
    int x = 0;
    e.post([&x] { x = 1; });
    CHECK(x == 1, "inline post runs at once");
    CHECK(e.call([&x] { return x + 1; }) == 2, "inline call runs at once");
    e.inlineStep();
    CHECK(c.frames == 1 && e.framesRun() == 1, "inlineStep runs one frame");
    CHECK(e.advance(3) && c.frames == 4, "advance(3) runs three more");
    CHECK(e.stop(), "stop is a no-op inline");
}

void testCallSeesPosts() {
    EmuThread e;
    FakeClient c;
    CHECK(e.start(EmuThread::Mode::Threaded, c), "threaded start");
    CHECK(e.threaded() && !e.onEmuThread(), "the caller is not the emulation thread");
    CHECK(c.started == 1 && c.startedOn != std::this_thread::get_id(), "threadStarted ran on the new thread");
    std::vector<int> seen;   // touched only on the emulation thread until the call returns
    for (int i = 0; i < 1000; i++) e.post([&seen, i] { seen.push_back(i); });
    const std::vector<int> got = e.call([&seen] { return seen; });
    bool inOrder = got.size() == 1000;
    for (size_t i = 0; inOrder && i < got.size(); i++) inOrder = got[i] == (int)i;
    CHECK(inOrder, "a call sees every earlier post applied, in order (%zu)", got.size());
    CHECK(e.call([&e] { return e.onEmuThread(); }), "a call runs on the emulation thread");
    CHECK(e.call([&e] { return e.call([] { return 7; }); }) == 7, "a call made on the emulation thread runs inline");
    CHECK(c.afterCmds.load() > 0, "afterCommands ran after a batch");
    CHECK(e.stop(), "stop");
    CHECK(c.stopping == 1, "threadStopping ran once");
}

void testProducers() {
    EmuThread e;
    FakeClient c;
    e.setPaused(true);
    CHECK(e.start(EmuThread::Mode::Threaded, c), "start");
    std::vector<std::pair<int, int>> got;   // emulation thread only, until the final call
    std::vector<std::thread> ps;
    for (int p = 0; p < 4; p++)
        ps.emplace_back([&e, &got, p] {
            for (int k = 0; k < 500; k++) e.post([&got, p, k] { got.emplace_back(p, k); });
        });
    for (std::thread& t : ps) t.join();
    const std::vector<std::pair<int, int>> all = e.call([&got] { return got; });
    std::vector<int> next(4, 0);
    bool ordered = all.size() == 2000;
    for (const auto& g : all) {
        if (g.second != next[(size_t)g.first]) ordered = false;
        next[(size_t)g.first] = g.second + 1;
    }
    CHECK(ordered, "four producers: each one's posts run in its order (%zu)", all.size());
    CHECK(e.stop(), "stop");
}

void testPauseAdvance() {
    EmuThread e;
    FakeClient c;
    c.period = 0.002;
    e.setPaused(true);
    CHECK(e.start(EmuThread::Mode::Threaded, c), "start paused");
    sleepMs(50);
    const int f0 = c.frames;
    sleepMs(50);
    CHECK(c.frames == f0, "paused: no frames (%d -> %d)", f0, c.frames.load());
    CHECK(e.advance(5) && c.frames == f0 + 5, "advance(5) runs exactly five while paused (%d)", c.frames.load() - f0);
    e.setPaused(false);
    for (int i = 0; i < 2000 && c.frames <= f0 + 15; i++) sleepMs(1);   // polled, not a fixed window: the host may be slow
    CHECK(c.frames > f0 + 15, "running again (%d)", c.frames.load() - f0);
    e.setPaused(true);
    c.loaded = false;
    CHECK(!e.advance(2), "advance() with nothing loaded gives up instead of waiting");
    CHECK(e.stop(), "stop");
}

// Rates over measured windows, wide tolerances: a loaded host (or TSan) stretches sleeps and starves the thread.
// What this host's timers can do: the mean of 20 sleep_for(1 ms), and the smallest steady_clock step.  Fast-forward at
// 4x a 10 ms period needs ~2.5 ms waits; on a host whose sleeps or clock are much coarser than that, the ratio check
// below measures the platform, not EmuThread, so it is reported instead of asserted.
struct TimerCaps {
    double sleepMs = 0, clockStepMs = 0, preciseMs = 0, preciseMinMs = 0;
};
TimerCaps measureTimers() {
    TimerCaps t;
    const Clock::time_point s0 = Clock::now();
    for (int i = 0; i < 20; i++) sleepMs(1);
    t.sleepMs = since(s0) * 1000.0 / 20.0;
    // Mean and shortest of 20 waits: a starved host still has some on-time wakes, a platform whose timers are coarse
    // (every wake ~15.6 ms) has none, so the check below asserts on the shortest.
    const Clock::time_point p0 = Clock::now();
    t.preciseMinMs = 1e9;
    for (int i = 0; i < 20; i++) {
        const Clock::time_point w0 = Clock::now();
        vj::preciseSleepNs(1000000);
        t.preciseMinMs = std::min(t.preciseMinMs, since(w0) * 1000.0);
    }
    t.preciseMs = since(p0) * 1000.0 / 20.0;
    double step = 1e9;
    for (int i = 0; i < 50; i++) {
        const Clock::time_point a = Clock::now();
        Clock::time_point b = Clock::now();
        while (b == a) b = Clock::now();
        step = std::min(step, std::chrono::duration<double, std::milli>(b - a).count());
    }
    t.clockStepMs = step;
    return t;
}

void testPacingWallClock() {
    EmuThread e;
    FakeClient c;
    c.period = 0.010;
    CHECK(e.start(EmuThread::Mode::Threaded, c), "start");
    sleepMs(50);   // settle
    int n0 = c.frames;
    Clock::time_point t0 = Clock::now();
    sleepMs(300);
    const int n = c.frames - n0;
    const double dt = since(t0);
    const double expect = dt / 0.010;
    CHECK(n <= expect + 3, "normal speed is not faster than real time (%d frames, at most %.1f in %.3f s)", n, expect + 3,
          dt);
    // A starved runner can stall the thread for most of a fixed window, so "it keeps running" is a deadline poll.
    const int r0 = c.frames;
    for (int i = 0; i < 5000 && c.frames < r0 + 10; i++) sleepMs(1);
    CHECK(c.frames >= r0 + 10, "and keeps running (%d frames within 5 s)", c.frames.load() - r0);
    const double normalRate = n / dt;
    e.setFastForward(true);
    sleepMs(50);   // let the clock rebase
    n0 = c.frames;
    t0 = Clock::now();
    sleepMs(300);
    const int ff = c.frames - n0;
    const double dtf = since(t0);
    const bool ffOk = ff / dtf > 1.5 * normalRate;
    const TimerCaps caps = measureTimers();
    std::printf("note: timers here: sleep_for(1 ms) takes %.2f ms, preciseSleepNs(1 ms) takes %.2f ms (min %.2f), "
                "steady_clock steps %.4f ms; fast-forward %.1f vs normal %.1f frames/s\n", caps.sleepMs, caps.preciseMs,
                caps.preciseMinMs, caps.clockStepMs, ff / dtf, normalRate);
    // Sleep promises not to return early. Upper latency includes scheduler starvation/coalescing on shared
    // runners, so precision stays in the informational timer report rather than being a correctness gate.
    CHECK(caps.preciseMinMs >= 0.9, "preciseSleepNs(1 ms) returned too early (%.3f ms)", caps.preciseMinMs);
    if (caps.preciseMinMs >= 4.0)
        std::printf("note: this host delayed every 1 ms precise sleep by at least %.2f ms\n", caps.preciseMinMs);
    if (!ffOk && (caps.sleepMs > 6.0 || caps.clockStepMs > 1.0)) {
        std::printf("note: fast-forward not checked: this host's timers are too coarse to pace 2.5 ms frames\n");
    } else if (!ffOk && n < expect * 0.6) {   // the host could not even sustain real time: no headroom to show
        std::printf("note: fast-forward not checked: normal speed only reached %.1f of %.1f frames/s\n", normalRate,
                    1.0 / 0.010);
    } else {
        CHECK(ffOk, "fast-forward (%gx) is well above normal (%.1f vs %.1f frames/s)", vj::PacingClock::kFastForward,
              ff / dtf, normalRate);
    }
    CHECK(e.stop(), "stop");
}

void testStopJoin() {
    EmuThread e;
    FakeClient c;
    CHECK(e.start(EmuThread::Mode::Threaded, c), "start");
    const Clock::time_point t0 = Clock::now();
    CHECK(e.stop(), "stop joins");
    CHECK(since(t0) < 3.0, "promptly (%.3f s)", since(t0));
    CHECK(!e.threaded() && c.stopping == 1, "stopped");
    int x = 0;
    e.post([&x] { x = 5; });
    CHECK(x == 5, "after stop, post runs inline");
    CHECK(e.call([] { return 3; }) == 3, "after stop, call runs inline");
}

void testCallDuringShutdown() {
    EmuThread e;
    FakeClient c;
    c.period = 0.001;
    CHECK(e.start(EmuThread::Mode::Threaded, c), "start");
    std::atomic<int> calls{0}, ran{0};
    std::atomic<bool> go{true};
    std::thread caller([&] {
        while (go.load()) {
            e.call([&ran] {
                ran++;
                return 1;
            });
            calls++;
        }
    });
    sleepMs(30);
    const Clock::time_point t0 = Clock::now();
    CHECK(e.stop(), "stop while another thread keeps calling");
    go = false;
    caller.join();
    CHECK(since(t0) < 2.0, "no deadlock (%.3f s)", since(t0));
    CHECK(ran.load() == calls.load() && calls.load() > 0, "every call ran exactly once (%d calls, %d ran)", calls.load(),
          ran.load());
}

void testCommandErrors() {
    EmuThread e;
    FakeClient c;
    CHECK(e.start(EmuThread::Mode::Threaded, c), "start");
    e.post([] { throw std::runtime_error("boom"); });
    CHECK(e.call([] { return 1; }) == 1, "the thread keeps running after a command throws");
    {
        std::lock_guard<std::mutex> l(c.errMu);
        CHECK(c.errors.size() == 1 && c.errors[0] == "boom", "the error reached commandFailed");
    }
    bool threw = false;
    try {
        e.call([]() -> int { throw std::runtime_error("bad call"); });
    } catch (const std::runtime_error& x) {
        threw = std::string(x.what()) == "bad call";
    }
    CHECK(threw, "a call's exception reaches its caller");
    CHECK(e.stop(), "stop");
}

// Leaves an abandoned thread behind.  The objects are never destroyed on purpose -- the detached thread still uses them.
void testJoinTimeout() {
    // Never destroyed: the abandoned thread still uses them.  Held by a static pointer so LeakSanitizer stays quiet (a
    // static object would be destroyed at exit with no ordering against the detached thread, which ThreadSanitizer flags).
    static EmuThread* const e = new EmuThread;
    static FakeClient* const c = new FakeClient;
    c->block = true;
    CHECK(e->start(EmuThread::Mode::Threaded, *c), "start");
    for (int i = 0; i < 1000 && !c->blocked.load(); i++) sleepMs(1);
    CHECK(c->blocked.load(), "the fake core is stuck inside a frame");
    std::shared_ptr<std::atomic<bool>> late = std::make_shared<std::atomic<bool>>(false);
    CHECK(!e->callFor(std::chrono::milliseconds(50), [late] { *late = true; }), "callFor times out on a stuck thread");
    const Clock::time_point t0 = Clock::now();
    CHECK(!e->stop(std::chrono::milliseconds(100)), "stop gives up on a thread stuck in the core");
    CHECK(since(t0) < 3.0, "after the timeout (%.3f s)", since(t0));
    CHECK(e->abandoned() && !e->threaded(), "the thread is abandoned");
    bool ran = false;
    CHECK(e->call([&ran] {
        ran = true;
        return 1;
    }) == 0 && !ran, "after abandoning, call() runs nothing and returns a default value");
    CHECK(e->debugCall([&ran] {
        ran = true;
        return 1;
    }) == 0 && !ran, "after abandoning, debugCall() runs nothing and returns a default value");
    CHECK(!e->callFor(std::chrono::milliseconds(10), [&ran] { ran = true; }) && !ran, "nor callFor()");
    c->block = false;   // let the abandoned thread finish its frame and exit; it runs the queued callFor on the way out
    for (int i = 0; i < 1000 && !late->load(); i++) sleepMs(1);
    CHECK(late->load(), "the command queued before the stop still ran, on the thread, once it got free");
}

// A call() already queued when the thread is abandoned must come back with R(), not hang in the future.
void testPendingCallAbandoned() {
    static EmuThread& e = *new EmuThread;   // never destroyed, see testJoinTimeout
    static FakeClient& c = *new FakeClient;
    c.block = true;
    CHECK(e.start(EmuThread::Mode::Threaded, c), "start");
    for (int i = 0; i < 1000 && !c.blocked.load(); i++) sleepMs(1);
    CHECK(c.blocked.load(), "the fake core is stuck inside a frame");
    std::shared_ptr<std::atomic<bool>> ranBody = std::make_shared<std::atomic<bool>>(false);
    std::atomic<int> result{-1};
    std::atomic<bool> returned{false};
    std::thread caller([ranBody, &result, &returned] {
        result = e.call([ranBody] {
            *ranBody = true;
            return 42;
        });
        returned = true;
    });
    sleepMs(100);   // the call is queued behind the stuck frame
    CHECK(!returned.load(), "the pending call is still waiting");
    CHECK(!e.stop(std::chrono::milliseconds(100)), "stop gives up on the stuck thread");
    const Clock::time_point t0 = Clock::now();
    for (int i = 0; i < 3000 && !returned.load(); i++) sleepMs(1);
    CHECK(returned.load(), "the pending call returned after the abandon (%.3f s)", since(t0));
    CHECK(result.load() == 0 && !ranBody->load(), "with a default value, its body not run (result %d)", result.load());
    c.block = false;
    caller.join();
    for (int i = 0; i < 1000 && !ranBody->load(); i++) sleepMs(1);   // the queued body still runs once the thread is free
}

// A batch can finish draining just before the UI queues an option/input change plus advance().
// The requested frame must not start until that earlier command has been applied (#967).
void testAdvanceSeesPostsAfterDrain() {
    struct GatedClient : FakeClient {
        std::atomic<bool> holdBatch{true}, batchHeld{false}, commandApplied{false};
        std::atomic<bool> frameSawCommand{false};
        void afterCommands() override {
            if (holdBatch.load()) {
                batchHeld = true;
                while (holdBatch.load()) sleepMs(1);
            }
        }
        void runFrame() override {
            frameSawCommand = commandApplied.load();
            FakeClient::runFrame();
        }
    } c;
    EmuThread e;
    e.setPaused(true);
    CHECK(e.start(EmuThread::Mode::Threaded, c), "start");
    e.post([] {});
    for (int i = 0; i < 2000 && !c.batchHeld.load(); ++i) sleepMs(1);
    CHECK(c.batchHeld.load(), "owner held after draining a command batch");
    e.post([&] { c.commandApplied = true; });   // queued after that drain
    std::atomic<bool> ok{false};
    std::thread caller([&] { ok = e.advance(1); });
    for (int i = 0; i < 2000 && !vj::EmuThreadTestAccess::advancePublished(e); ++i) sleepMs(1);
    CHECK(vj::EmuThreadTestAccess::advancePublished(e), "advance request published while the owner is held");
    c.holdBatch = false;
    caller.join();
    CHECK(ok.load() && c.frames == 1, "advance runs exactly one requested frame");
    CHECK(c.frameSawCommand.load(), "advance frame sees every earlier queued command");
    CHECK(e.stop(), "stop");
}

// advance(n) must wait for n frames run for it, not be satisfied by a frame already in flight.
void testAdvanceExact() {
    EmuThread e;
    FakeClient c;
    c.period = 0.002;
    c.block = true;
    CHECK(e.start(EmuThread::Mode::Threaded, c), "start");
    for (int i = 0; i < 1000 && !c.blocked.load(); i++) sleepMs(1);
    CHECK(c.blocked.load(), "a frame is in flight");
    e.setPaused(true);
    const int start = c.frames;
    std::atomic<bool> ok{false}, returned{false};
    std::atomic<int> atReturn{-1};
    std::thread t([&] {
        ok = e.advance(1);
        atReturn = c.frames.load();
        returned = true;
    });
    sleepMs(30);
    CHECK(!returned.load(), "advance(1) is still waiting while the in-flight frame is stuck");
    c.holdAt = start + 2;   // hold the budgeted frame half-way
    c.block = false;        // the in-flight frame ends, then the one budgeted frame runs (and is held)
    for (int i = 0; i < 2000 && c.frames.load() < start + 2; i++) sleepMs(1);
    sleepMs(30);
    CHECK(c.frames == start + 2 && !returned.load(), "advance(1) is not satisfied by the in-flight frame (%d frames, returned %d)",
          c.frames.load() - start, (int)returned.load());
    c.holdAt = -1;
    t.join();
    CHECK(ok.load(), "advance(1) succeeded");
    CHECK(atReturn.load() == start + 2, "the in-flight frame did not count: one budgeted frame ran after it (%d)",
          atReturn.load() - start);
    sleepMs(50);
    CHECK(c.frames == start + 2, "no frame runs after advance() returned (%d)", c.frames.load() - start);
    CHECK(e.stop(), "stop");
}

// #967 phase 2: a call() slower than 250 ms runs the wait hook on the caller (the UI draws a spinner there); a fast
// one never does; the hook is never re-entered and never runs on the emulation thread.
void testWaitHook() {
    EmuThread e;
    FakeClient c;
    CHECK(e.start(EmuThread::Mode::Threaded, c), "start");
    std::atomic<int> calls{0};
    std::atomic<int> firstTicks{0};
    std::atomic<bool> onEmu{false};
    std::atomic<bool> nested{false};
    std::atomic<int> depth{0};
    e.setWaitHook([&](double waited, unsigned tick) {
        if (depth.fetch_add(1) != 0) nested = true;
        if (e.onEmuThread()) onEmu = true;
        if (tick == 0) {
            firstTicks++;
            sleepMs(220);   // a busy UI may spend most of a short call drawing its first busy frame
        }
        if (waited < 0.24) nested = true;   // (abuse the flag: never before 250 ms)
        calls++;
        depth--;
    });
    const auto fastStart = Clock::now();
    e.call([] { return 0; });
    if (since(fastStart) < 0.24)
        CHECK(calls.load() == 0, "a fast call runs no hook (%d)", calls.load());
    // An empty queued call may itself wait over 250 ms on a starved runner. Reset its observations before
    // testing a genuinely blocked command: hold it until repeated callbacks arrive, with a failure deadline.
    calls = 0;
    firstTicks = 0;
    e.call([&] {
        const auto start = Clock::now();
        while (calls.load() < 3 && since(start) < 5.0) sleepMs(1);
        return 0;
    });
    CHECK(calls.load() >= 3, "a blocked call repeats the hook despite a slow first callback (%d)", calls.load());
    CHECK(firstTicks.load() == 1, "tick 0 once per call (%d)", firstTicks.load());
    CHECK(!onEmu.load(), "never on the emulation thread");
    CHECK(!nested.load(), "never re-entered, never before 250 ms");
    const int before = calls.load();
    e.post([] { std::this_thread::sleep_for(std::chrono::milliseconds(400)); });   // a slow post: nobody waits on it
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    CHECK(calls.load() == before, "posts never run the hook");
    CHECK(e.stop(), "stop");
}

// #967 phase 3: the debug lane. Inline it runs at once; threaded it runs on the emulation thread, wakes a parked
// (paused) loop, waits for a frame in progress and runs at the boundary BEFORE the next frame starts, and runs the
// wait hook like call().
void testDebugLane() {
    {
        EmuThread e;
        FakeClient c;
        CHECK(e.start(EmuThread::Mode::Inline, c), "inline start");
        int x = 0;
        e.debugPost([&x] { x = 1; });
        CHECK(x == 1, "inline debugPost runs at once");
        CHECK(e.debugCall([&x] { return x + 1; }) == 2, "inline debugCall runs at once");
    }
    EmuThread e;
    FakeClient c;
    e.setPaused(true);   // no frames: only a command can wake the loop
    CHECK(e.start(EmuThread::Mode::Threaded, c), "threaded start");
    CHECK(e.debugCall([&e] { return e.onEmuThread(); }), "a debugCall runs on the emulation thread");
    CHECK(e.debugCall([&e] { return e.debugCall([] { return 7; }); }) == 7,
          "a debugCall made on the emulation thread runs inline");
    std::atomic<bool> ran{false};
    const Clock::time_point t0 = Clock::now();
    e.debugPost([&ran] { ran = true; });
    for (int i = 0; i < 400 && !ran.load(); i++) sleepMs(1);
    CHECK(ran.load() && since(t0) < 0.2, "a debugPost runs on a paused thread well inside its 250 ms idle wait (%.3f s)", since(t0));
    CHECK(c.frames == 0, "...without running a frame");
    std::atomic<bool> late{false};
    e.debugPost([&late] { late = true; });
    CHECK(e.stop() && late.load(), "stop() drains the debug lane too");
}

void testDebugCallWaitsForTheFrame() {
    EmuThread e;
    FakeClient c;
    c.holdAt = 1;   // the first frame counts itself, then holds
    CHECK(e.start(EmuThread::Mode::Threaded, c), "start");
    for (int i = 0; i < 1000 && c.frames.load() < 1; i++) sleepMs(1);
    std::atomic<int> framesWhenRun{-1};
    std::atomic<bool> returned{false};
    std::thread caller([&] {
        e.debugCall([&] { framesWhenRun = c.frames.load(); });
        returned = true;
    });
    sleepMs(80);
    CHECK(!returned.load() && framesWhenRun.load() == -1, "a debugCall waits while a frame is in progress");
    c.holdAt = -1;
    caller.join();
    CHECK(framesWhenRun.load() == 1, "it runs at the frame boundary, before the next frame starts (frames %d)",
          framesWhenRun.load());
    CHECK(e.stop(), "stop");
}

void testDebugCallWaitHook() {
    EmuThread e;
    FakeClient c;
    std::atomic<int> calls{0};
    e.setWaitHook([&](double, unsigned) { calls++; });
    c.holdAt = 1;
    CHECK(e.start(EmuThread::Mode::Threaded, c), "start");
    for (int i = 0; i < 1000 && c.frames.load() < 1; i++) sleepMs(1);
    // Hold the frame until the hook has run three times (kBusyAfter, then ~every 16 ms), not for a fixed 450 ms: a slow
    // hosted VM can oversleep so much that three hooks do not fit (#967).  The 5 s bound keeps a broken hook from hanging.
    std::thread release([&c, &calls] {
        for (int i = 0; i < 5000 && calls.load() < 3; i++) sleepMs(1);
        c.holdAt = -1;
    });
    e.debugCall([] { return 0; });
    release.join();
    CHECK(calls.load() >= 3, "a debugCall behind a slow frame runs the wait hook like call() (%d)", calls.load());
    CHECK(e.stop(), "stop");
}

}  // namespace

// B1: the halt pump's lane service.  Threaded, it drains the debug lane, waits at most maxWait for a DEBUG post, drains
// again; a frame-lane post does not shorten the wait; inline it drains and returns at once.
void testServeDebugLane() {
    EmuThread e;
    FakeClient c;
    std::atomic<size_t> served{0};
    std::atomic<double> waited{0};
    c.onBlocked = [&] {                       // runs on the emulation thread, inside the blocked frame (the halt)
        const auto t0 = Clock::now();
        served = e.serveDebugLane(std::chrono::milliseconds(4));
        waited = since(t0);
    };
    c.block = true;
    CHECK(e.start(EmuThread::Mode::Threaded, c), "start");
    for (int i = 0; i < 1000 && !c.blocked.load(); i++) sleepMs(1);
    e.post([] {});                            // a frame-lane command queued behind the halted frame (the SRAM autosave)
    c.serveNow = 1;
    for (int i = 0; i < 1000 && c.serveNow.load() > 0; i++) sleepMs(1);
    sleepMs(10);
    CHECK(served.load() == 0, "nothing in the debug lane: 0 commands (%zu)", served.load());
    CHECK(waited.load() >= 0.003 && waited.load() < 0.5, "and it waited about maxWait despite the frame post (%.1f ms)", waited.load() * 1e3);
    std::atomic<int> ran{0};
    e.debugPost([&] { ran++; });
    c.serveNow = 1;
    for (int i = 0; i < 1000 && (c.serveNow.load() > 0 || ran.load() == 0 || served.load() == 0); i++) sleepMs(1);   // `served` is stored after the lane service returns
    CHECK(ran.load() == 1 && served.load() == 1, "a debug post is run by the lane service (%d, %zu)", ran.load(), served.load());
    CHECK(e.serveDebugLane(std::chrono::milliseconds(50)) == 0, "off the emulation thread: 0, no wait");
    c.block = false;
    CHECK(e.stop(), "stop");
    EmuThread in;
    FakeClient ic;
    CHECK(in.start(EmuThread::Mode::Inline, ic), "inline start");
    int x = 0;
    in.debugPost([&] { x = 1; });            // inline: ran at once
    const auto t0 = Clock::now();
    CHECK(in.serveDebugLane(std::chrono::milliseconds(50)) == 0 && since(t0) < 0.01 && x == 1, "inline: returns at once");
}

// Review Focus 1: a call posted an instant before the core halts.  With a give-up predicate it returns R() once the
// predicate turns true, and the cancelled task never runs; a task that already started is waited for; a predicate that
// is already true means nothing is queued at all.
void testCallGivesUp() {
    EmuThread e;
    FakeClient c;
    c.block = true;   // a frame that never ends: the halt
    CHECK(e.start(EmuThread::Mode::Threaded, c), "start");
    for (int i = 0; i < 1000 && !c.blocked.load(); i++) sleepMs(1);
    std::atomic<bool> halted{false};
    std::atomic<int> polls{0};
    auto ran = std::make_shared<std::atomic<bool>>(false);
    std::thread flip([&] { sleepMs(60); halted = true; });
    const Clock::time_point t0 = Clock::now();
    const int r = e.call([ran] { *ran = true; return 42; }, [&] { polls++; return halted.load(); });
    flip.join();
    CHECK(r == 0, "the call returned R() (%d)", r);
    CHECK(since(t0) < 0.3 && since(t0) >= 0.05, "once the predicate turned true, within 300 ms (%.3f s)", since(t0));
    CHECK(polls.load() >= 2, "the predicate was polled before the post and after a slice (%d)", polls.load());
    // Already halted when the call is made: nothing is queued, R() at once.
    polls = 0;
    auto ran2 = std::make_shared<std::atomic<bool>>(false);
    const Clock::time_point t1 = Clock::now();
    CHECK(e.call([ran2] { *ran2 = true; return 7; }, [&] { polls++; return halted.load(); }) == 0 && since(t1) < 0.01 && polls.load() == 1,
          "a true predicate before the post returns at once (%.1f ms, %d polls)", since(t1) * 1e3, polls.load());
    c.block = false;
    sleepMs(100);                                   // the frame ends, the queue drains
    CHECK(!ran->load() && !ran2->load(), "the cancelled task never ran, the refused one was never queued");
    // A task that started before the predicate turned true runs to completion and returns its value.
    halted = false;
    const int r2 = e.call([&] { sleepMs(120); halted = true; sleepMs(30); return 7; }, [&] { return halted.load(); });
    CHECK(r2 == 7, "a started task is waited for (%d)", r2);
    // Never consulted inline or on the emulation thread.
    std::atomic<int> consulted{0};
    CHECK(e.call([&] { return e.call([] { return 1; }, [&] { consulted++; return true; }); }) == 1, "on the thread: runs");
    CHECK(consulted.load() == 0, "predicate ignored on the emulation thread (%d)", consulted.load());
    CHECK(e.stop(), "stop");
    EmuThread in;
    FakeClient ic;
    CHECK(in.start(EmuThread::Mode::Inline, ic), "inline");
    CHECK(in.call([] { return 3; }, [&] { consulted++; return true; }) == 3 && consulted.load() == 0, "inline: runs, never asks");
}

// Carried from 3a: advance() gives up like call().  The budget is registered on the debug lane even while a frame is
// parked; the wait ends when the predicate turns true; advance(0) later waits for the frames still budgeted.
void testAdvanceGivesUp() {
    EmuThread e;
    FakeClient c;
    e.setPaused(true);
    CHECK(e.start(EmuThread::Mode::Threaded, c), "start");
    c.block = true;                                 // the next frame parks: the halt
    std::atomic<bool> halted{false};
    std::thread flip([&] { sleepMs(80); halted = true; });
    const Clock::time_point t0 = Clock::now();
    const bool r = e.advance(2, [&] { return halted.load(); });
    flip.join();
    CHECK(!r && since(t0) >= 0.07 && since(t0) < 0.5, "gave up once the predicate turned true, not before (%.3f s)", since(t0));
    CHECK(c.blocked.load(), "the budget was registered: the first frame started and parked");
    halted = false;
    c.block = false;
    CHECK(e.advance(0, [&] { return halted.load(); }), "advance(0) waits for the frames still budgeted");
    CHECK(c.frames.load() == 2, "both budgeted frames ran (%d)", c.frames.load());
    const Clock::time_point t1 = Clock::now();
    CHECK(e.advance(0) && since(t1) < 0.05, "nothing pending: true at once");
    CHECK(e.stop(), "stop");
    EmuThread in;
    FakeClient ic;
    CHECK(in.start(EmuThread::Mode::Inline, ic), "inline");
    std::atomic<int> consulted{0};
    CHECK(in.advance(2, [&] { consulted++; return true; }) && ic.frames.load() == 2 && consulted.load() == 0, "inline: runs, never asks");
}

// The CAS-lost branch: the predicate turns true only AFTER the task has started (it spins until it has).  call() must then
// wait for the task and return its value, never R(), and the by-reference local it wrote must be intact.
void testCallGiveUpLosesToStart() {
    EmuThread e;
    FakeClient c;
    c.block = true;                                  // the first frame parks: the task stays queued past the first slice
    CHECK(e.start(EmuThread::Mode::Threaded, c), "start");
    for (int i = 0; i < 1000 && !c.blocked.load(); i++) sleepMs(1);
    std::atomic<bool> started{false};
    std::atomic<int> polls{0};
    int local = 0;
    const int r = e.call(
        [&] {
            started = true;
            sleepMs(30);
            local = 1;
            return 7;
        },
        [&] {
            if (polls++ == 0) return false;          // the poll before the post
            c.block = false;                         // the poll after the first slice: let the frame end ...
            for (int i = 0; i < 2000 && !started.load(); i++) sleepMs(1);   // ... and answer true only once the task runs
            return true;
        });
    CHECK(r == 7 && local == 1, "a predicate that turns true after the task started still gets its value (%d, %d)", r, local);
    CHECK(polls.load() == 2, "polled before the post and once after a slice (%d)", polls.load());
    CHECK(e.stop(), "stop");
}

// The loop's second frame-lane drain: advance()'s budget rides the debug lane, so a frame-lane post made while the debug
// lane is draining (after the frame lane was) must still be applied before the frame the budget starts.  A debug command
// queued ahead of the budget posts it from another thread, in the middle of that drain.
void testAdvanceBudgetDoesNotOvertakeFramePost() {
    struct GatedClient : FakeClient {
        std::atomic<bool> holdBatch{true}, batchHeld{false}, commandApplied{false}, frameSawCommand{false};
        void afterCommands() override {
            if (holdBatch.load()) {
                batchHeld = true;
                while (holdBatch.load()) sleepMs(1);
            }
        }
        void runFrame() override {
            frameSawCommand = commandApplied.load();
            FakeClient::runFrame();
        }
    } c;
    EmuThread e;
    e.setPaused(true);
    CHECK(e.start(EmuThread::Mode::Threaded, c), "start");
    e.post([] {});
    for (int i = 0; i < 2000 && !c.batchHeld.load(); ++i) sleepMs(1);
    CHECK(c.batchHeld.load(), "owner held after a command batch");
    e.debugPost([&] {                                // runs inside the debug drain, after the frame drain
        std::thread t([&] { e.post([&] { c.commandApplied = true; }); });
        t.join();                                    // queued on the frame lane before this command returns
    });
    std::atomic<bool> ok{false};
    std::thread caller([&] { ok = e.advance(1); });  // the budget queues behind the command above
    for (int i = 0; i < 2000 && vj::EmuThreadTestAccess::debugQueued(e) < 2; ++i) sleepMs(1);   // the command and the budget
    CHECK(vj::EmuThreadTestAccess::debugQueued(e) == 2, "both debug commands are queued behind the held batch");
    c.holdBatch = false;
    caller.join();
    CHECK(ok.load() && c.frames == 1, "advance ran its frame");
    CHECK(c.frameSawCommand.load(), "the frame-lane post made during the debug drain was applied before the budgeted frame");
    CHECK(e.stop(), "stop");
}

int main() {
#ifdef _WIN32
    // The app runs EmuThread after SDL_Init, which sets the Windows timer resolution to 1 ms
    // (SDL_HINT_TIMER_RESOLUTION).  Without it, timed waits round up to ~15.6 ms and fast-forward cannot beat normal speed.
    timeBeginPeriod(1);
#endif
    testInline();
    testCallSeesPosts();
    testProducers();
    testPauseAdvance();
    testPacingWallClock();
    testStopJoin();
    testCallDuringShutdown();
    testCommandErrors();
    testAdvanceExact();
    testAdvanceSeesPostsAfterDrain();
    testJoinTimeout();
    testPendingCallAbandoned();
    testWaitHook();
    testDebugLane();
    testDebugCallWaitsForTheFrame();
    testDebugCallWaitHook();
    testServeDebugLane();
    testCallGivesUp();
    testAdvanceGivesUp();
    testCallGiveUpLosesToStart();
    testAdvanceBudgetDoesNotOvertakeFramePost();
#ifdef _WIN32
    timeEndPeriod(1);
#endif
    std::printf(g_fail ? "emu_thread_test: %d FAILED\n" : "emu_thread_test: OK\n", g_fail);
    return g_fail ? 1 : 0;
}
