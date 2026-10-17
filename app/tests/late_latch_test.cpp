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
    l.noteBuild(16 * kMs);   // one build nearly a whole period long
    CHECK(l.wakeAt(end + 200000) == end + 200000, "a 16 ms build leaves no room to wait");
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
            it.push_back({now, take, kV0 + n * kP60});
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
            // Published at once while the UI waits; otherwise when the next wait starts (latched) or the next
            // iteration starts (unlatched, where take == now + pump).
            const uint64_t pub = (latched && e >= it[k].now && e < it[k].take) ? e : it[k + 1].now;
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
    testOffNominal();
    testModel();
    std::printf(g_fail ? "late_latch_test: %d FAILED\n" : "late_latch_test: OK\n", g_fail);
    return g_fail ? 1 : 0;
}
