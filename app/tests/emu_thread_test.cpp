// EmuThread (emu_thread.h): lifecycle, ordering, pause / advance, shutdown races, against a fake client -- no core,
// no SDL (#967).
//
//   vj_emu_thread_test
#include "emu_thread.h"

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
    sleepMs(100);
    CHECK(c.frames > f0 + 15, "running again (%d)", c.frames.load() - f0);
    e.setPaused(true);
    c.loaded = false;
    CHECK(!e.advance(2), "advance() with nothing loaded gives up instead of waiting");
    CHECK(e.stop(), "stop");
}

void testPacingWallClock() {
    EmuThread e;
    FakeClient c;
    c.period = 0.010;
    CHECK(e.start(EmuThread::Mode::Threaded, c), "start");
    sleepMs(300);
    const int n = c.frames;
    CHECK(n >= 10 && n <= 45, "about 30 frames at a 10 ms period in 0.3 s (got %d)", n);
    e.setFastForward(true);
    sleepMs(200);
    const int ff = c.frames - n;
    CHECK(ff > 30, "fast-forward runs %gx (got %d frames in 0.2 s)", vj::PacingClock::kFastForward, ff);
    CHECK(e.stop(), "stop");
}

void testStopJoin() {
    EmuThread e;
    FakeClient c;
    CHECK(e.start(EmuThread::Mode::Threaded, c), "start");
    const Clock::time_point t0 = Clock::now();
    CHECK(e.stop(), "stop joins");
    CHECK(since(t0) < 1.0, "promptly (%.3f s)", since(t0));
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
            ran += e.call([] { return 1; });
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

// Last: leaves an abandoned thread behind.  Both objects are leaked on purpose -- the detached thread still uses them.
void testJoinTimeout() {
    EmuThread* e = new EmuThread;
    FakeClient* c = new FakeClient;
    c->block = true;
    CHECK(e->start(EmuThread::Mode::Threaded, *c), "start");
    for (int i = 0; i < 1000 && !c->blocked.load(); i++) sleepMs(1);
    CHECK(c->blocked.load(), "the fake core is stuck inside a frame");
    std::shared_ptr<std::atomic<bool>> late = std::make_shared<std::atomic<bool>>(false);
    CHECK(!e->callFor(std::chrono::milliseconds(50), [late] { *late = true; }), "callFor times out on a stuck thread");
    const Clock::time_point t0 = Clock::now();
    CHECK(!e->stop(std::chrono::milliseconds(100)), "stop gives up on a thread stuck in the core");
    CHECK(since(t0) < 1.0, "after the timeout (%.3f s)", since(t0));
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

}  // namespace

int main() {
    testInline();
    testCallSeesPosts();
    testProducers();
    testPauseAdvance();
    testPacingWallClock();
    testStopJoin();
    testCallDuringShutdown();
    testCommandErrors();
    testJoinTimeout();
    std::printf(g_fail ? "emu_thread_test: %d FAILED\n" : "emu_thread_test: OK\n", g_fail);
    return g_fail ? 1 : 0;
}
