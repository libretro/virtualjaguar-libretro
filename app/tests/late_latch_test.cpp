// LateLatch (late_latch.h) against a fake clock: no thread, no sleeping, no display.  The first cases drive the vblank
// tracker directly; the last one replays the whole pipeline (display, UI loop, mode-A emulation grid, input events) with
// the real LateLatch deciding the waits, and compares event -> present with the single-thread loop (#967, plan section
// "Decided: input latency in mode A").
#include "late_latch.h"

#include <cstdint>
#include <cstdio>
#include <vector>

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

using vj::LateLatch;

constexpr uint64_t kMs = 1000000;
constexpr uint64_t kP60 = 16666667;   // a 60 Hz display
constexpr uint64_t kV0 = 1000000000;  // the first vblank

uint32_t g_rng = 12345;
double unit() {   // deterministic uniform [0, 1)
    g_rng = g_rng * 1664525u + 1013904223u;
    return (double)(g_rng >> 8) / 16777216.0;
}

// Swaps that block until a vblank of an exact grid, 2 ms builds.  Returns the latch after `n` of them.
void runGrid(LateLatch& l, uint64_t period, unsigned n, uint64_t& lastEnd) {
    for (unsigned k = 1; k <= n; k++) {
        const uint64_t v = kV0 + k * period;
        l.noteBuild(2 * kMs);
        l.onSwap(v - 3 * kMs, v + 50000);
        lastEnd = v + 50000;
    }
}

void testLocks() {
    LateLatch l;
    l.setPeriodNs(kP60);
    uint64_t end = 0;
    runGrid(l, kP60, 10, end);
    CHECK(!l.locked(), "not locked after 10 swaps");
    CHECK(l.wakeAt(end + 200000) == end + 200000, "no wait before the lock");
    runGrid(l, kP60, 40, end);
    CHECK(l.locked(), "locked after 40 blocking swaps on an exact grid");
    const uint64_t now = end + 200000;
    const uint64_t nextV = end - 50000 + kP60;
    const uint64_t w = l.wakeAt(now);
    CHECK(l.budgetNs() == 3 * kMs, "budget = the slowest build (2 ms) + the 1 ms pad: %llu", (unsigned long long)l.budgetNs());
    const int64_t err = (int64_t)w - (int64_t)(nextV - 3 * kMs);
    CHECK(err > -200000 && err < 200000, "wakes 3 ms before the next vblank (off by %lld ns)", (long long)err);
}

void testNonBlocking() {
    LateLatch l;
    l.setPeriodNs(kP60);
    for (unsigned k = 1; k <= 200; k++) {
        const uint64_t t = kV0 + k * kP60 + 2 * kMs;
        l.noteBuild(2 * kMs);
        l.onSwap(t, t + 20000);   // returns at once: vsync off, or a queued swap
    }
    CHECK(!l.locked(), "swaps that never block never lock");
    CHECK(l.wakeAt(kV0 + 300 * kP60) == kV0 + 300 * kP60, "and never wait");
}

void testJitter() {
    LateLatch l;
    l.setPeriodNs(kP60);
    for (unsigned k = 1; k <= 600; k++) {
        const uint64_t v = kV0 + k * kP60 + (uint64_t)(unit() * 6 * kMs);   // returns anywhere in a 6 ms window
        l.noteBuild(2 * kMs);
        l.onSwap(v - 3 * kMs, v);
    }
    CHECK(!l.locked(), "swap returns spread over 6 ms do not lock");
}

void testUnknownPeriod() {
    LateLatch l;
    uint64_t end = 0;
    runGrid(l, kP60, 100, end);
    CHECK(!l.locked() && l.wakeAt(end) == end, "no display period: off");
}

void testNoRoom() {
    LateLatch l;
    l.setPeriodNs(kP60);
    uint64_t end = 0;
    runGrid(l, kP60, 60, end);
    l.noteBuild(16 * kMs);   // one build nearly a whole period long: a one-off (a window opening)
    CHECK(l.wakeAt(end + 200000) > end + 200000 && l.budgetNs() == 3 * kMs,
          "one 16 ms build does not set the budget (%llu ns): the wait goes on", (unsigned long long)l.budgetNs());
    l.noteBuild(16 * kMs);   // a second one within the window: the UI really is that slow now
    CHECK(l.wakeAt(end + 200000) == end + 200000, "two 16 ms builds leave no room to wait");
}

void testMissGrowsPad() {
    LateLatch l;
    l.setPeriodNs(kP60);
    uint64_t end = 0;
    runGrid(l, kP60, 60, end);
    const uint64_t w = l.wakeAt(end + 200000);
    const uint64_t target = end - 50000 + kP60;
    CHECK(w < target, "waits");
    l.noteBuild(4 * kMs);                                   // the build ran long...
    l.onSwap(w + 4 * kMs, target + kP60 + 50000);          // ...and the swap landed one vblank late
    CHECK(l.misses() == 1, "a late swap counts as a miss (%u)", l.misses());
    CHECK(l.padNs() == LateLatch::kPadMinNs + LateLatch::kPadUpNs, "the pad grows by 0.5 ms");
    CHECK(l.locked(), "a single miss keeps the lock");
    uint64_t v = target + kP60;
    for (unsigned k = 0; k < LateLatch::kCalmRun * 20; k++) {   // calm again: the pad comes back down to the floor
        const uint64_t ww = l.wakeAt(v + 250000);
        l.noteBuild(2 * kMs);
        v += kP60;
        l.onSwap(ww + 2 * kMs, v + 50000);
    }
    CHECK(l.misses() == 1, "no further misses (%u)", l.misses());
    CHECK(l.padNs() == LateLatch::kPadMinNs, "the pad shrinks back to 1 ms (%llu)", (unsigned long long)l.padNs());
}

// A compositor needs the submission 1.45 ms before vblank even when the CPU build takes exactly 2 ms.
// Repeatedly probing below that learned deadline turns otherwise calm playback into periodic repeated frames (#1026).
void testCompositorDeadline() {
    LateLatch l;
    l.setPeriodNs(kP60);
    uint64_t end = 0;
    runGrid(l, kP60, 60, end);
    for (unsigned k = 0; k < 3600; k++) {
        const uint64_t wake = l.wakeAt(end + 200000);
        const uint64_t target = l.targetNs();
        CHECK(target != 0, "compositor model stays latched");
        if (!target) break;
        const uint64_t submit = wake + 2 * kMs;
        end = target + (submit + 1450000 > target ? kP60 : 0) + 50000;
        l.noteWaitEnd(wake);
        l.noteBuild(2 * kMs);
        l.onSwap(submit, end);
    }
    std::printf("compositor deadline: %u missed vblanks in 3600 iterations\n", l.misses());
    CHECK(l.misses() <= 2, "at most two deadline probes per minute after learning the compositor (%u)", l.misses());
    CHECK(l.locked() && l.released() == 0, "a stable compositor keeps its latch");
}

void testEventWaitHeadroom() {
    unsigned eventWaits = 0;
    for (uint64_t left = 0; left <= 20 * kMs; left += 100000) {
        const uint64_t timeout = LateLatch::eventWaitMs(left);
        if (timeout) {
            eventWaits++;
            // A whole-ms event wait may overshoot by 2 ms; it must still leave time for the precise tail.
            CHECK(timeout * kMs + 2 * kMs <= left, "event wait overruns wake with 2 ms overshoot (%llu ns left)",
                  (unsigned long long)left);
        } else {
            CHECK(left < 4 * kMs, "only the last few ms stop waiting for events (%llu ns)", (unsigned long long)left);
        }
    }
    CHECK(eventWaits > 100, "long waits continue handling input events");
}

void testPersistentMissReleases() {
    // A build whose GPU work finishes past the aimed vblank every time (the CPU-side budget cannot see it, and the pad is
    // capped): every wait misses by one period.  The latch must give up rather than present a vblank late forever.
    LateLatch l;
    l.setPeriodNs(kP60);
    uint64_t cur = 0;
    runGrid(l, kP60, 60, cur);
    CHECK(l.locked(), "locked before the misses");
    unsigned waits = 0;
    for (; waits < 200; waits++) {
        const uint64_t now = cur + 200000;
        const uint64_t w = l.wakeAt(now);
        if (w == now) break;   // released
        const uint64_t nextV = cur - 50000 + kP60;
        l.noteBuild(2 * kMs);
        l.onSwap(w + 2 * kMs, nextV + kP60 + 50000);   // lands one vblank late, on the grid
        cur = nextV + kP60 + 50000;
    }
    CHECK(waits >= LateLatch::kMinOutcomes && waits <= 60, "released within 60 waits (%u)", waits);
    CHECK(!l.locked() && l.released() == 1, "released once (locked %d, released %u)", (int)l.locked(), l.released());
    CHECK(l.misses() == waits, "every wait missed (%u of %u)", l.misses(), waits);
    // Cool-down: swaps keep arriving on the grid, the latch tracks them but does not wait.
    for (unsigned k = 1; k <= LateLatch::kCooldownSwaps - 2; k++) {
        const uint64_t v = cur - 50000 + kP60;
        l.noteBuild(2 * kMs);
        l.onSwap(v - 3 * kMs, v + 50000);
        cur = v + 50000;
        CHECK(!l.locked() && l.wakeAt(cur + 200000) == cur + 200000, "no wait in the cool-down (swap %u)", k);
        if (g_fail) break;
    }
    for (unsigned k = 0; k < 4 && !l.locked(); k++) {   // the cool-down ends: tracking locks again
        const uint64_t v = cur - 50000 + kP60;
        l.noteBuild(2 * kMs);
        l.onSwap(v - 3 * kMs, v + 50000);
        cur = v + 50000;
    }
    CHECK(l.locked(), "locks again after the cool-down");
}

void testJitterHysteresis() {
    // Locked on a clean grid, then swap returns scatter +-1.4 ms around it (a compositor, a busy GPU): smoothed jitter
    // sits near 1.4 ms, over the 1 ms needed to lock but under the 2 ms needed to let go.  It must stay locked.
    LateLatch l;
    l.setPeriodNs(kP60);
    uint64_t end = 0;
    runGrid(l, kP60, 60, end);
    CHECK(l.locked(), "locked before the scatter");
    unsigned unlocked = 0;
    for (unsigned k = 1; k <= 400; k++) {
        const uint64_t v = kV0 + (60 + k) * kP60 + ((k & 1) ? 1400000 : 0) - ((k & 1) ? 0 : 1400000);
        l.noteBuild(2 * kMs);
        l.onSwap(v - 3 * kMs, v);
        if (!l.locked()) unlocked++;
    }
    CHECK(unlocked == 0, "+-1.4 ms scatter keeps the lock (unlocked on %u of 400 swaps)", unlocked);
}

void testLateStartIsNotAMiss() {
    // The wait overran its wake time by 3 ms (a call() inside it, a descheduled thread), the build then ran late and the
    // swap landed a vblank after the aimed one: that is not the budget's fault.
    for (int overran = 0; overran < 2; overran++) {
        LateLatch l;
        l.setPeriodNs(kP60);
        uint64_t end = 0;
        runGrid(l, kP60, 60, end);
        const uint64_t w = l.wakeAt(end + 200000);
        const uint64_t target = end - 50000 + kP60;
        l.noteWaitEnd(overran ? w + 3 * kMs : w + 100000);
        l.noteBuild(4 * kMs);
        l.onSwap(w + 3 * kMs + 4 * kMs, target + kP60 + 50000);
        if (overran) {
            CHECK(l.misses() == 0 && l.padNs() == LateLatch::kPadMinNs, "an overrun wait is not a miss (%u, pad %llu)",
                  l.misses(), (unsigned long long)l.padNs());
        } else {
            CHECK(l.misses() == 1 && l.padNs() == LateLatch::kPadMinNs + LateLatch::kPadUpNs,
                  "the same late swap after an on-time wait is a miss (%u)", l.misses());
        }
    }
}

void testLateStartsAreCapped() {
    // Every wait overruns its wake time by 3 ms (event handling that always takes longer than the room it is given) and
    // every swap then misses.  The first kMaxLateExempt are excused; after that they are outcomes, and the miss rate
    // must release the latch.
    LateLatch l;
    l.setPeriodNs(kP60);
    uint64_t cur = 0;
    runGrid(l, kP60, 60, cur);
    unsigned waits = 0;
    for (; waits < 200; waits++) {
        const uint64_t now = cur + 200000;
        const uint64_t w = l.wakeAt(now);
        if (w == now) break;   // released
        const uint64_t nextV = cur - 50000 + kP60;
        l.noteWaitEnd(w + 3 * kMs);
        l.noteBuild(2 * kMs);
        l.onSwap(w + 5 * kMs, nextV + kP60 + 50000);
        cur = nextV + kP60 + 50000;
    }
    CHECK(!l.locked() && l.released() == 1, "always-late waits release the latch (after %u waits)", waits);
    CHECK(waits > LateLatch::kMaxLateExempt && waits <= LateLatch::kMaxLateExempt + 60,
          "after the excused ones (%u waits)", waits);
    CHECK(l.misses() == waits - LateLatch::kMaxLateExempt, "the excused waits are not misses (%u of %u)", l.misses(),
          waits);
}

void testOffNominal() {
    // A display that really runs at 59.94 Hz while the mode says 60: the tracker follows the drift.
    LateLatch l;
    l.setPeriodNs(kP60);
    const uint64_t real = 16683350;
    uint64_t v = kV0, worst = 0;
    for (unsigned k = 0; k < 10000; k++) {
        const uint64_t w = l.wakeAt(v + 250000);
        if (l.locked()) {
            const int64_t err = (int64_t)(v + real) - (int64_t)l.budgetNs() - (int64_t)w;
            const uint64_t a = (uint64_t)(err < 0 ? -err : err);
            if (k > 100 && a > worst) worst = a;
        }
        l.noteBuild(2 * kMs);
        v += real;
        l.onSwap(v - 2 * kMs, v + 50000);
    }
    CHECK(l.locked(), "stays locked on a 59.94 Hz display announced as 60 Hz");
    CHECK(worst < 400000, "wake error at most 0.4 ms (%llu ns)", (unsigned long long)worst);
    CHECK(l.misses() == 0, "no misses (%u)", l.misses());
}

// ---- the pipeline model ---------------------------------------------------------------------------------------------

struct Iter {
    uint64_t now;    // the wait starts (after the swap and the end-of-iteration work)
    uint64_t precise; // event wait ends; input in the precise-sleep tail is published at take
    uint64_t take;   // the wait ends: pump the rest, publish, take the newest frame
    uint64_t present;
};

struct Result {
    double inlineMs, unlatchedMs, latchMs, latchPollMs;
    unsigned misses, iters;
};

// Display at 60 Hz, emulation on the mode-A grid at 60.05445 Hz (its phase sweeps the display's every ~18 s), core
// frames of `frameNs`, UI builds of 2 ms +- 0.3 ms, one input event per display period at a uniform time.
Result model(uint64_t frameNs) {
    const double E = 1e9 / 60.05445;
    const double T0 = (double)kV0 + 0.37 * (double)kP60;
    const uint64_t kEnd = 50000, kWork = 200000, kPump = 50000;
    const unsigned K = 22000;   // ~6 minutes: the drift sweeps every phase many times
    Result r = {};
    for (int latched = 0; latched < 2; latched++) {
        LateLatch l;
        l.setPeriodNs(kP60);
        g_rng = 777;
        std::vector<Iter> it;
        it.reserve(K);
        uint64_t m = 0;   // the vblank index the last swap returned at
        for (unsigned k = 0; k < K; k++) {
            const uint64_t now = kV0 + m * kP60 + kEnd + kWork;
            uint64_t w = latched ? l.wakeAt(now) : now;
            if (w < now) w = now;
            const uint64_t take = w + kPump;
            const uint64_t build = (uint64_t)(1.7e6 + unit() * 0.6e6);
            const uint64_t begin = take + build;
            const uint64_t n = (begin - kV0 + kP60 - 1) / kP60;   // the first vblank at or after the swap call
            l.noteBuild(begin - w);
            l.onSwap(begin, kV0 + n * kP60 + kEnd);
            uint64_t precise = now;
            // No event-wait overshoot in this model: the longest precise tail, including ms rounding.
            for (;;) {
                const uint64_t ms = LateLatch::eventWaitMs(w - precise);
                if (!ms) break;
                precise += ms * kMs;
            }
            it.push_back({now, precise, take, kV0 + n * kP60});
            m = n;
        }
        if (latched) {
            r.misses = l.misses();
            r.iters = K;
        }
        // Events from 2 s in (after the lock) to 2 s before the end.
        double sum = 0, pollSum = 0, inl = 0;
        unsigned cnt = 0;
        size_t k = 0;
        g_rng = 4242;
        for (uint64_t e0 = kV0 + 120 * kP60; e0 + 120 * kP60 < it.back().present; e0 += kP60) {
            const uint64_t e = e0 + (uint64_t)(unit() * (double)kP60);
            while (k + 1 < it.size() && it[k + 1].now <= e) k++;
            // The event wait publishes immediately; the precise tail defers events until the next pump/take.
            // Outside the wait, publication is at the next wait/iteration start (as in the unlatched loop).
            const uint64_t pub = (latched && e >= it[k].now && e < it[k].take)
                                     ? (e < it[k].precise ? e : it[k].take) : it[k + 1].now;
            const double jf = (double)pub - T0 > 0 ? ((double)pub - T0) / E : 0;
            uint64_t j = (uint64_t)jf;
            if ((double)j < jf) j++;
            const uint64_t poll = (uint64_t)(T0 + (double)j * E);
            const uint64_t fin = poll + frameNs;
            size_t q = k;
            while (q < it.size() && it[q].take < fin) q++;
            if (q >= it.size()) break;
            sum += (double)(it[q].present - e);
            pollSum += (double)(poll - e);
            // The single-thread loop: polled when the iteration after the event starts (just after a vblank),
            // presented one vblank later (the frame and the UI fit in a period).
            const uint64_t vi = (e - kV0 + kP60 - 1) / kP60;
            inl += (double)(kV0 + (vi + 1) * kP60 - e);
            cnt++;
        }
        if (latched) {
            r.latchMs = sum / cnt / 1e6;
            r.latchPollMs = pollSum / cnt / 1e6;
            r.inlineMs = inl / cnt / 1e6;
        } else {
            r.unlatchedMs = sum / cnt / 1e6;
        }
    }
    return r;
}

void testModel() {
    const Result a = model(5 * kMs);
    std::printf("model, 5 ms frames:  inline %.1f ms, threaded %.1f ms, threaded + late latch %.1f ms (poll %.1f ms), "
                "misses %u of %u\n",
                a.inlineMs, a.unlatchedMs, a.latchMs, a.latchPollMs, a.misses, a.iters);
    CHECK(a.latchMs <= a.inlineMs + 2.0, "5 ms frames: the late latch is within 2 ms of inline (%.2f vs %.2f)", a.latchMs,
          a.inlineMs);
    CHECK(a.latchMs + 15.0 < a.unlatchedMs, "and far below the unlatched loop (%.2f vs %.2f)", a.latchMs, a.unlatchedMs);
    CHECK(a.misses * 100 <= a.iters, "at most 1%% missed vblanks (%u of %u)", a.misses, a.iters);
    const Result b = model(14 * kMs);
    std::printf("model, 14 ms frames: inline %.1f ms, threaded %.1f ms, threaded + late latch %.1f ms (poll %.1f ms), "
                "misses %u of %u\n",
                b.inlineMs, b.unlatchedMs, b.latchMs, b.latchPollMs, b.misses, b.iters);
    // Mode A cannot hide a heavy frame: event -> present is about poll + frame + half a period + the budget, against
    // inline's poll + one period.  Mode B (Phase 4) is the fix.  Pinned so a change to the model shows up here.
    CHECK(b.latchMs + 15.0 < b.unlatchedMs, "14 ms frames: still far below the unlatched loop (%.2f vs %.2f)", b.latchMs,
          b.unlatchedMs);
    CHECK(b.latchMs < 36.0, "14 ms frames: the latched loop stays near the model's 34.2 ms (%.2f)", b.latchMs);
    CHECK(b.misses * 100 <= b.iters, "at most 1%% missed vblanks (%u of %u)", b.misses, b.iters);
}

// #967 final-review minor 3: with vsync off (or a swap that did not block), the threaded loop naps until the display's
// next refresh instead of a flat 4 ms (which presented at ~250 Hz for nothing).
void testIdleNap() {
    const uint64_t ms = 1000000ull;
    const uint64_t p60 = 16666667ull;
    CHECK(vj::LateLatch::idleNapNs(0, p60) == p60, "just swapped: a whole period");
    CHECK(vj::LateLatch::idleNapNs(10 * ms, p60) == p60 - 10 * ms, "the rest of the period");
    CHECK(vj::LateLatch::idleNapNs(16 * ms, p60) == 1 * ms, "never under 1 ms (do not spin)");
    CHECK(vj::LateLatch::idleNapNs(40 * ms, p60) == 1 * ms, "late already: 1 ms");
    CHECK(vj::LateLatch::idleNapNs(0, 0) == 4 * ms, "refresh unknown: the old 4 ms");
}

// The vsync-off loop presents on a deadline grid: the nap ends at the grid point, not a period after the swap, so
// the loop period is the display period whatever the build costs (a 2 ms build used to make it period + 2 ms: 53 Hz).
void testPresentGrid() {
    const uint64_t ms = 1000000ull;
    const uint64_t p60 = 16666667ull;
    const auto run = [&](uint64_t buildNs, bool grid, unsigned iters) {   // returns the time of iteration `iters`
        uint64_t t = 1000 * ms, g = 0;   // t = the iteration's start
        for (unsigned i = 0; i < iters; i++) {
            const uint64_t swapEnd = t + buildNs;
            if (grid) {
                g = vj::LateLatch::advanceGridNs(g, swapEnd, p60);
                t = swapEnd + vj::LateLatch::gridNapNs(swapEnd, g, p60);
            } else {
                t = swapEnd + vj::LateLatch::idleNapNs(0, p60);
            }
        }
        return t - 1000 * ms;
    };
    const uint64_t withGrid = run(2 * ms, true, 600);
    const uint64_t flat = run(2 * ms, false, 600);
    CHECK(withGrid > 600 * p60 - 2 * p60 && withGrid < 600 * p60 + 2 * p60, "grid: 600 iterations take 600 display periods");
    CHECK(flat > 600 * (p60 + 2 * ms) - ms, "(the old nap: period + build per iteration)");

    // First call anchors on the swap; a stall of more than a period re-anchors at now (no burst of catch-up frames).
    uint64_t g = vj::LateLatch::advanceGridNs(0, 5000 * ms, p60);
    CHECK(g == 5000 * ms + p60, "first call: one period after the swap");
    g = vj::LateLatch::advanceGridNs(g, 5000 * ms + p60 + 3 * ms, p60);   // 3 ms behind: keep the grid, catch up
    CHECK(g == 5000 * ms + 2 * p60, "slightly behind: the grid does not move");
    g = vj::LateLatch::advanceGridNs(g, 5000 * ms + 400 * ms, p60);   // a 400 ms stall
    CHECK(g == 5000 * ms + 400 * ms, "a stall: re-anchored at now");
    // Ahead of the clock by more than a period (early wakes on input): pulled back to one period from now.
    g = vj::LateLatch::advanceGridNs(6000 * ms + 10 * p60, 6000 * ms, p60);
    CHECK(g == 6000 * ms + p60, "ahead: at most one period");

    // The nap: until the grid point, 1 ms at least, the old 4 ms when the refresh rate is unknown.
    CHECK(vj::LateLatch::gridNapNs(100 * ms, 110 * ms, p60) == 10 * ms, "the rest of the grid step");
    CHECK(vj::LateLatch::gridNapNs(100 * ms, 100 * ms + 300000ull, p60) == 1 * ms, "never under 1 ms");
    CHECK(vj::LateLatch::gridNapNs(100 * ms, 90 * ms, p60) == 1 * ms, "behind: 1 ms");
    CHECK(vj::LateLatch::gridNapNs(100 * ms, 0, 0) == 4 * ms, "refresh unknown: 4 ms");
    CHECK(vj::LateLatch::advanceGridNs(123, 456, 0) == 0, "refresh unknown: no grid");
}

}  // namespace

int main() {
    testLocks();
    testNonBlocking();
    testJitter();
    testUnknownPeriod();
    testNoRoom();
    testMissGrowsPad();
    testPersistentMissReleases();
    testJitterHysteresis();
    testLateStartIsNotAMiss();
    testLateStartsAreCapped();
    testOffNominal();
    testModel();
    testIdleNap();
    testPresentGrid();
    testCompositorDeadline();
    testEventWaitHeadroom();
    std::printf(g_fail ? "late_latch_test: %d FAILED\n" : "late_latch_test: OK\n", g_fail);
    return g_fail ? 1 : 0;
}
