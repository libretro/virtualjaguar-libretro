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

}  // namespace

int main() {
    testFifo();
    testWaits();
    testCloseAndDrain();
    testErrors();
    testProducers();
    std::printf(g_fail ? "emu_commands_test: %d FAILED\n" : "emu_commands_test: OK\n", g_fail);
    return g_fail ? 1 : 0;
}
