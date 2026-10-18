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
    double sleepMs = 0, clockStepMs = 0, preciseMs = 0;
};
TimerCaps measureTimers() {
    TimerCaps t;
    const Clock::time_point s0 = Clock::now();
    for (int i = 0; i < 20; i++) sleepMs(1);
    t.sleepMs = since(s0) * 1000.0 / 20.0;
    const Clock::time_point p0 = Clock::now();
    for (int i = 0; i < 20; i++) vj::preciseSleepNs(1000000);
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
    std::printf("note: timers here: sleep_for(1 ms) takes %.2f ms, preciseSleepNs(1 ms) takes %.2f ms, "
                "steady_clock steps %.4f ms; fast-forward %.1f vs normal %.1f frames/s\n", caps.sleepMs, caps.preciseMs,
                caps.clockStepMs, ff / dtf, normalRate);
    CHECK(caps.preciseMs < 4.0, "preciseSleepNs(1 ms) takes %.2f ms on average", caps.preciseMs);
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

}  // namespace

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
    testJoinTimeout();
    testPendingCallAbandoned();
#ifdef _WIN32
    timeEndPeriod(1);
#endif
    std::printf(g_fail ? "emu_thread_test: %d FAILED\n" : "emu_thread_test: OK\n", g_fail);
    return g_fail ? 1 : 0;
}
