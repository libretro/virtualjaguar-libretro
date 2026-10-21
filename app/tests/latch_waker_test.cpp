// LatchWaker (latch_waker.h, #1026): the UI thread's timed event waits end on a wake event pushed by a thread of its
// own, not on SDL's timeout.  SDL events only (no window), real clock.  Without a video device SDL's wait polls, so the
// wake is timed when it was pushed (its event timestamp), not when this thread saw it.  Its lateness is judged against
// the host's own timers: a thread at the waker's QoS sleeping to the same deadlines with SDL_DelayPrecise (a hosted
// macOS VM oversleeps 3-8 ms; the Studio well under 1).  The waker may be at most 2 ms later at p90 and 10 ms at worst.
// The Studio traces (VJ_PERF_TRACE + the stall probe) measure the real Cocoa wait.  Runs on every platform
// (start(true)), so the ThreadSanitizer job covers it; the app starts it on macOS only.
#include "latch_waker.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <thread>
#include <vector>

#ifdef __APPLE__
#include <pthread.h>
#include <sys/qos.h>
#endif

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

using vj::LatchWaker;

constexpr uint64_t kMs = 1000000;
constexpr int kWaits = 20;
constexpr uint64_t kP90SlackNs = 2 * kMs;   // the waker vs a precise sleep on the same host, at p90 ...
constexpr uint64_t kMaxSlackNs = 10 * kMs;  // ... and at worst
uint64_t g_refMaxNs = 0;                    // the reference sleep's worst lateness

uint64_t dueAfter(int i) { return SDL_GetTicksNS() + 8 * kMs + (uint64_t)i * 300000; }
uint64_t p90(std::vector<uint64_t> v) {
    std::sort(v.begin(), v.end());
    return v[v.size() * 9 / 10];
}

void drain() {
    SDL_Event e;
    while (SDL_PollEvent(&e)) {}
}

// How many wake events arrive within `ms`, and when the first one did (0: none).
unsigned wakesWithin(Sint32 ms, uint64_t& firstNs) {
    unsigned n = 0;
    firstNs = 0;
    const uint64_t end = SDL_GetTicksNS() + (uint64_t)ms * kMs;
    for (;;) {
        const uint64_t now = SDL_GetTicksNS();
        if (now >= end) return n;
        SDL_Event e;
        if (!SDL_WaitEventTimeout(&e, (Sint32)((end - now + kMs - 1) / kMs))) continue;
        if (LatchWaker::isWake(e)) {
            if (!n) firstNs = e.common.timestamp;
            n++;
        }
    }
}

// The host's timer lateness right now: SDL_DelayPrecise to a deadline like the waker's, on a thread at its QoS.
uint64_t referenceLateness(int i) {
    uint64_t late = 0;
    std::thread t([&] {
#ifdef __APPLE__
        pthread_set_qos_class_self_np(QOS_CLASS_USER_INTERACTIVE, 0);
#endif
        const uint64_t due = dueAfter(i);
        SDL_DelayPrecise(due - SDL_GetTicksNS());
        const uint64_t at = SDL_GetTicksNS();
        late = at > due ? at - due : 0;
    });
    t.join();
    return late;
}

// Each wait is paired with a reference sleep just before it, so both see the same load.
void wakesOnTime(LatchWaker& w) {
    drain();
    std::vector<uint64_t> late, ref;
    for (int i = 0; i < kWaits; i++) {
        ref.push_back(referenceLateness(i));
        const uint64_t due = dueAfter(i);
        w.arm(due);
        SDL_Event e;
        const bool got = SDL_WaitEventTimeout(&e, 8 + i / 3 + LatchWaker::kBackstopMs + 40);
        w.disarm();
        const uint64_t at = got ? e.common.timestamp : SDL_GetTicksNS();   // when the waker pushed it
        CHECK(got && LatchWaker::isWake(e), "wait %d: no wake event (got %d, type 0x%x)", i, (int)got,
              got ? e.type : 0);
        CHECK(at + 200000 >= due, "wait %d: woke %.3f ms early", i, (double)(due - at) / 1e6);
        late.push_back(at > due ? at - due : 0);
    }
    g_refMaxNs = *std::max_element(ref.begin(), ref.end());
    const uint64_t wp = p90(late), rp = p90(ref), wm = *std::max_element(late.begin(), late.end());
    std::printf("latch_waker: lateness p90 %.3f ms max %.3f ms; precise sleep p90 %.3f ms max %.3f ms\n", wp / 1e6,
                wm / 1e6, rp / 1e6, g_refMaxNs / 1e6);
    CHECK(wp <= rp + kP90SlackNs, "p90 lateness %.3f ms, a precise sleep's %.3f ms", wp / 1e6, rp / 1e6);
    CHECK(wm <= g_refMaxNs + kMaxSlackNs, "worst lateness %.3f ms, a precise sleep's %.3f ms", wm / 1e6,
          g_refMaxNs / 1e6);
}

void disarmedNeverWakes(LatchWaker& w) {
    drain();
    w.arm(SDL_GetTicksNS() + 10 * kMs);
    w.disarm();
    uint64_t first;
    CHECK(wakesWithin(40, first) == 0, "a disarmed waker pushed a wake");
}

void rearmMovesTheWake(LatchWaker& w) {
    drain();
    const uint64_t t = SDL_GetTicksNS();
    w.arm(t + 30 * kMs);
    w.arm(t + 10 * kMs);   // the wait's deadline moved earlier: the old one must not also fire
    uint64_t first;
    const unsigned n = wakesWithin(60, first);
    w.disarm();
    CHECK(n == 1, "%u wakes after a re-arm, want 1", n);
    CHECK(first && first >= t + 10 * kMs - 200000 && first <= t + 10 * kMs + g_refMaxNs + kMaxSlackNs,
          "re-armed wake at +%.3f ms, want +10", first ? (double)(first - t) / 1e6 : -1.0);
}

void anEventEndsTheWaitFirst(LatchWaker& w) {
    drain();
    const uint64_t t = SDL_GetTicksNS();
    w.arm(t + 30 * kMs);
    std::thread other([] {
        SDL_DelayPrecise(5 * kMs);
        SDL_Event u;
        SDL_zero(u);
        u.type = SDL_EVENT_USER;
        u.user.code = 1;
        SDL_PushEvent(&u);
    });
    SDL_Event e;
    const bool got = SDL_WaitEventTimeout(&e, 30 + LatchWaker::kBackstopMs);
    w.disarm();
    other.join();
    CHECK(got && e.type == SDL_EVENT_USER && !LatchWaker::isWake(e), "the other thread's event did not end the wait");
    uint64_t first;
    CHECK(wakesWithin(50, first) == 0, "a wake arrived after the wait ended on an event and was disarmed");
}

void stopWhileArmed() {
    LatchWaker w;
    w.start(true);
    w.arm(SDL_GetTicksNS() + 1000 * kMs);
    const uint64_t t = SDL_GetTicksNS();
    w.stop();
    CHECK(SDL_GetTicksNS() - t < 100 * kMs, "stop() waited for an armed deadline");
    CHECK(!w.running(), "running after stop()");
}

}  // namespace

int main() {
    if (!SDL_Init(SDL_INIT_EVENTS)) {
        std::printf("FAIL SDL_Init: %s\n", SDL_GetError());
        return 1;
    }
    LatchWaker w;
#ifndef __APPLE__
    CHECK(!w.start() && !w.running(), "the app's start() runs the waker off macOS");
#endif
    CHECK(w.start(true) && w.running(), "not running after start()");
    wakesOnTime(w);
    disarmedNeverWakes(w);
    rearmMovesTheWake(w);
    anEventEndsTheWaitFirst(w);
    w.stop();
    stopWhileArmed();
    SDL_Quit();
    std::printf("latch_waker: %s\n", g_fail ? "FAILED" : "ok");
    return g_fail ? 1 : 0;
}
