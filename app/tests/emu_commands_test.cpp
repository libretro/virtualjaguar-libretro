// EmuCommands (emu_commands.h): the emulation thread's command queue (#967).  No SDL, no core.
//
//   vj_emu_commands_test
#include "emu_commands.h"

#include <atomic>
#include <chrono>
#include <cstdio>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>
#include <vector>

using vj::EmuCommands;
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

void testFifo() {
    EmuCommands q;
    std::vector<int> v;
    for (int i = 0; i < 5; i++) CHECK(q.post([&v, i] { v.push_back(i); }), "post %d accepted", i);
    CHECK(q.drain(nullptr) == 5, "five ran");
    CHECK((v == std::vector<int>{0, 1, 2, 3, 4}), "in the order posted");
    CHECK(q.drain(nullptr) == 0, "nothing left");
}

void testWaits() {
    EmuCommands q;
    Clock::time_point t0 = Clock::now();
    q.waitUntil(t0 + std::chrono::milliseconds(50));
    CHECK(since(t0) >= 0.045, "nothing to do: waits until the deadline (%.3f s)", since(t0));

    t0 = Clock::now();
    std::thread poster([&q] {
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
        q.post([] {});
    });
    q.waitUntil(t0 + std::chrono::seconds(5));
    CHECK(since(t0) < 1.0, "a post wakes the wait early (%.3f s)", since(t0));
    poster.join();
    q.drain(nullptr);

    t0 = Clock::now();
    std::thread notifier([&q] {
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
        q.notify();
    });
    q.waitUntil(t0 + std::chrono::seconds(5));
    CHECK(since(t0) < 1.0, "notify() wakes it too (pause, stop, fast-forward) (%.3f s)", since(t0));
    notifier.join();
}

void testCloseAndDrain() {
    EmuCommands q;
    int ran = 0;
    for (int i = 0; i < 3; i++) q.post([&ran] { ran++; });
    CHECK(q.closeAndDrain(nullptr) == 3 && ran == 3, "what was queued still runs (%d)", ran);
    CHECK(q.closed(), "then the queue is closed");
    CHECK(!q.post([&ran] { ran++; }) && ran == 3, "and refuses new commands");
    q.reopen();
    CHECK(q.post([&ran] { ran++; }) && q.drain(nullptr) == 1 && ran == 4, "reopen() takes commands again");
}

void testErrors() {
    EmuCommands q;
    std::vector<std::string> errors;
    bool after = false;
    q.post([] { throw std::runtime_error("boom"); });
    q.post([&after] { after = true; });
    q.drain([&errors](const std::string& e) { errors.push_back(e); });
    CHECK(errors.size() == 1 && errors[0] == "boom", "a throwing command is reported");
    CHECK(after, "and the next command still runs");
}

void testProducers() {
    EmuCommands q;
    static constexpr int kProducers = 4, kEach = 2000;   // static: usable in the lambdas without a capture (MSVC)
    std::vector<std::pair<int, int>> got;   // (producer, sequence); appended by the drains on this thread
    std::atomic<int> finished{0};
    std::vector<std::thread> ps;
    for (int p = 0; p < kProducers; p++)
        ps.emplace_back([&q, &got, &finished, p] {
            for (int k = 0; k < kEach; k++) q.post([&got, p, k] { got.emplace_back(p, k); });
            finished++;
        });
    while (finished.load() < kProducers) q.drain(nullptr);
    for (std::thread& t : ps) t.join();
    q.drain(nullptr);
    std::vector<int> next(kProducers, 0);
    bool ordered = (int)got.size() == kProducers * kEach;
    for (const auto& g : got) {
        if (g.second != next[(size_t)g.first]) ordered = false;
        next[(size_t)g.first] = g.second + 1;
    }
    CHECK(ordered, "every producer's commands run once each, in its own order (%zu ran)", got.size());
}

using Lane = EmuCommands::Lane;

void testLanes() {
    EmuCommands q;
    std::string ran;
    q.post([&ran] { ran += 'F'; });
    q.post([&ran] { ran += 'D'; }, Lane::Debug);
    CHECK(q.pending(Lane::Frame) && q.pending(Lane::Debug), "each lane holds its command");
    CHECK(q.drain(nullptr, Lane::Debug) == 1 && ran == "D" && q.pending(Lane::Frame),
          "drain(Lane::Debug) leaves the frame lane untouched and vice versa (debug first)");
    q.post([&ran] { ran += 'd'; }, Lane::Debug);
    CHECK(q.drain(nullptr, Lane::Frame) == 1 && ran == "DF" && q.pending(Lane::Debug),
          "drain(Lane::Frame) leaves the debug lane untouched (frame first)");
    CHECK(q.drain(nullptr) == 0, "the default lane is the frame lane: nothing left there");
    CHECK(q.drain(nullptr, Lane::Debug) == 1 && ran == "DFd" && !q.pending(Lane::Debug), "the debug lane is FIFO");
}

void testDebugLaneWakesAndCloses() {
    EmuCommands q;
    Clock::time_point t0 = Clock::now();
    std::thread poster([&q] {
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
        q.post([] {}, Lane::Debug);
    });
    q.waitUntil(t0 + std::chrono::seconds(5));
    CHECK(since(t0) < 1.0, "a debug-lane post wakes the wait early (%.3f s)", since(t0));
    poster.join();
    q.drain(nullptr, Lane::Debug);

    std::string ran;
    q.post([&ran] { ran += 'F'; });
    q.post([&q, &ran] {   // a debug command that queues another one while the queue closes: it still runs
        ran += 'D';
        q.post([&ran] { ran += 'E'; }, Lane::Debug);
    }, Lane::Debug);
    CHECK(q.closeAndDrain(nullptr) == 3 && ran == "FDE", "closeAndDrain runs both lanes, frame then debug each round (%s)", ran.c_str());
    CHECK(q.closed() && !q.post([] {}, Lane::Debug) && !q.post([] {}), "then both lanes refuse commands");
    q.reopen();
    CHECK(q.post([] {}, Lane::Debug) && q.pending(Lane::Debug), "reopen() takes debug commands again");
}

// B1: the halt pump's wait listens to the debug lane only.  A frame-lane post must not end it (a halted core cannot
// run frame commands, and the pump would otherwise spin); a debug post or notify() must.
void testDebugLaneWait() {
    EmuCommands q;
    const auto t0 = Clock::now();
    q.post([] {});                                             // frame lane
    q.waitUntil(t0 + std::chrono::milliseconds(30), EmuCommands::Lane::Debug);
    CHECK(since(t0) >= 0.025, "a frame-lane post does not wake the debug-lane wait (%.1f ms)", since(t0) * 1e3);
    std::thread poster([&] { sleepMs(5); q.post([] {}, EmuCommands::Lane::Debug); });
    const auto t1 = Clock::now();
    q.waitUntil(t1 + std::chrono::seconds(5), EmuCommands::Lane::Debug);
    CHECK(since(t1) < 1.0, "a debug-lane post wakes it (%.3f s)", since(t1));
    poster.join();
    std::thread notifier([&] { sleepMs(5); q.notify(); });
    const auto t2 = Clock::now();
    q.waitUntil(t2 + std::chrono::seconds(5), EmuCommands::Lane::Debug);
    CHECK(since(t2) < 1.0, "notify() wakes it (%.3f s)", since(t2));
    notifier.join();
    // The pending debug post is still there for drain(); the frame post too.
    CHECK(q.pending(EmuCommands::Lane::Debug) && q.pending(EmuCommands::Lane::Frame), "nothing was consumed by the wait");
}

}  // namespace

int main() {
    testFifo();
    testWaits();
    testCloseAndDrain();
    testErrors();
    testProducers();
    testLanes();
    testDebugLaneWakesAndCloses();
    testDebugLaneWait();
    std::printf(g_fail ? "emu_commands_test: %d FAILED\n" : "emu_commands_test: OK\n", g_fail);
    return g_fail ? 1 : 0;
}
