// Frame pacing (pacing.h) against a fake clock -- no window, no core, no sleeping, so the result does not depend
// on how loaded the machine is (the real-time numbers of issue #952 moved by 3x with whatever else was running).
//
//   vj_pacing_test
//
// A simulated UI loop: each iteration plans frames, "runs" them (cost c per frame), draws (cost u), then either
// waits for the next vblank (vsync) or sleeps as the loop does.  The LEGACY loop below is the pacing App had
// before: backlog zeroed when over 2 periods, sleep computed from a stale accumulator, vblank wait always on.
#include "pacing.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

using vj::Pacer;

namespace {

int g_fail = 0;
#define CHECK(c, ...)                                  \
    do {                                               \
        if (!(c)) {                                    \
            std::printf("FAIL %s:%d: ", __FILE__, __LINE__); \
            std::printf(__VA_ARGS__);                  \
            std::printf("\n");                         \
            g_fail++;                                  \
        }                                              \
    } while (0)

struct Scenario {
    double period;   // core period, s
    double c;        // cost of one emulated frame, s
    double u;        // UI + upload cost per loop iteration, s
    double vblank;   // display refresh interval, s (0 = no display clock)
    bool vsync;      // vsync wanted
};

// Seconds until the next vblank boundary at/after t.
double nextVblank(double t, double v) { return std::ceil(t / v - 1e-9) * v; }

// Returns emulated frames per real second over `secs`.
double simulateNew(const Scenario& s, double secs) {
    Pacer p;
    p.resetSwap(s.vsync);
    double now = 0, last = 0;
    long frames = 0;
    while (now < secs) {
        double dt = now - last;
        last = now;
        int n = p.plan(dt, s.period);
        now += n * s.c;
        frames += n;
        if (n > 0) {
            p.charge(now - last, s.period);
            last = now;
        } else {
            p.behind = false;
        }
        now += s.u;
        p.updateSwap(s.vsync);
        if (p.swapSync && s.vblank > 0) now = nextVblank(now, s.vblank);
        if (!p.swapSync) now += p.sleepFor(now - last, s.period);
    }
    return (double)frames / now;
}

// The pacing before #952, verbatim in structure.
double simulateLegacy(const Scenario& s, double secs) {
    double accum = 0, now = 0, last = 0;
    long frames = 0;
    while (now < secs) {
        double dt = now - last;
        last = now;
        accum += dt;
        if (accum > 0.25) accum = s.period;
        int n = 0;
        while (accum >= s.period && n < 3) {
            n++;
            accum -= s.period;
        }
        if (accum > s.period * 2) accum = 0;
        now += n * s.c;
        frames += n;
        now += s.u;
        if (s.vsync && s.vblank > 0) now = nextVblank(now, s.vblank);
        if (!s.vsync) {
            double wait = s.period - accum - 0.001;
            if (wait > 0.0005) now += std::min(wait, 0.016);
        }
    }
    return (double)frames / now;
}

void report(const char* what, const Scenario& s, double oldFps, double newFps) {
    std::printf("  %-34s core %.1f ms/frame (%.1f fps alone): legacy %.1f fps -> now %.1f fps\n", what, s.c * 1000.0,
                1.0 / (s.c + s.u / 2), oldFps, newFps);
}

}  // namespace

int main() {
    const double P = 1.0 / 60.0;
    const double V120 = 1.0 / 120.0, V60 = 1.0 / 60.0;

    // ---- a core with time to spare: pacing must be unchanged -------------------------------------------
    for (double v : {V60, V120}) {
        for (bool sync : {true, false}) {
            Scenario s{P, 0.005, 0.0015, sync ? v : 0, sync};
            double o = simulateLegacy(s, 20), n = simulateNew(s, 20);
            CHECK(std::fabs(n - 60.0) < 0.6, "ahead (vblank %.1f, sync %d): %.2f fps, want 60", 1.0 / v, sync, n);
            CHECK(std::fabs(n - o) < 0.6, "ahead: legacy %.2f vs now %.2f", o, n);
        }
    }

    // ---- a core that cannot keep up: it must run flat out -----------------------------------------------
    // RISC 300% on Doom, idle skip off, M2: 19.4 ms per frame in the window.
    for (bool sync : {false, true}) {
        Scenario s{P, 0.0194, 0.0015, sync ? V120 : 0, sync};
        double o = simulateLegacy(s, 30), n = simulateNew(s, 30);
        report(sync ? "behind, vsync on, 120 Hz display" : "behind, vsync off", s, o, n);
        double ceiling = 1.0 / (s.c + s.u / 3);   // 3 frames share one UI frame
        CHECK(n > 0.95 * ceiling, "behind (sync %d): %.2f fps is under 95%% of the core's %.2f", sync, n, ceiling);
        CHECK(n > o * 1.08, "behind (sync %d): now %.2f is not >8%% better than legacy %.2f", sync, n, o);
    }
    {   // the same on a 60 Hz display
        Scenario s{P, 0.0194, 0.0015, V60, true};
        double o = simulateLegacy(s, 30), n = simulateNew(s, 30);
        report("behind, vsync on, 60 Hz display", s, o, n);
        CHECK(n > o * 1.08, "behind 60 Hz: now %.2f is not >8%% better than legacy %.2f", n, o);
    }

    // ---- hovering at the limit (core ~ one period): never worse than before --------------------------------
    for (double c : {0.0145, 0.0155, 0.0165, 0.0175}) {
        Scenario s{P, c, 0.0015, V120, true};
        double o = simulateLegacy(s, 30), n = simulateNew(s, 30);
        report("edge, vsync on, 120 Hz display", s, o, n);
        CHECK(n >= o - 0.5, "edge c=%.1f ms: now %.2f is worse than legacy %.2f", c * 1000, n, o);
    }

    // ---- the pieces ---------------------------------------------------------------------------------------
    {
        Pacer p;
        CHECK(p.plan(0.0, P) == 0, "no time, no frames");
        CHECK(p.plan(P, P) == 1, "one period, one frame");
        CHECK(p.plan(10 * P, P) == 3, "a long gap is capped at %d frames", Pacer::kMaxFrames);
        p.charge(0.0194 * 3, P);
        CHECK(p.behind, "3 frames at 19.4 ms leave the pacer behind");
        CHECK(p.sleepFor(0, P) == 0, "never sleep while behind");
        CHECK(p.accum <= 2 * P + 1e-12, "the backlog is clamped to two periods (%.4f)", p.accum);
        Pacer q;
        q.plan(P, P);
        q.charge(0.004, P);
        CHECK(!q.behind, "a 4 ms frame leaves it ahead");
        double w = q.sleepFor(0.0005, P);
        CHECK(w > 0.010 && w < P, "ahead: sleeps the rest of the period (%.4f)", w);
        Pacer st;
        st.plan(1.0, P);   // a 1 s stall (window drag): resync, not a burst
        CHECK(st.accum < P + 1e-9 || st.accum < 3 * P, "stall resync (%.4f)", st.accum);
    }
    {   // swap-interval hysteresis
        Pacer p;
        p.resetSwap(true);
        p.behind = true;
        CHECK(!p.updateSwap(true) && p.swapSync, "1 behind step: still synced");
        CHECK(!p.updateSwap(true) && p.swapSync, "2 behind steps: still synced");
        CHECK(p.updateSwap(true) && !p.swapSync, "3 behind steps: released");
        p.behind = false;
        for (int i = 0; i < Pacer::kRestoreAfter - 1; i++) CHECK(!p.updateSwap(true), "restored too early (%d)", i);
        CHECK(p.updateSwap(true) && p.swapSync, "restored after %d keeping-up steps", Pacer::kRestoreAfter);
        // flapping at the limit must not toggle
        int flips = 0;
        for (int i = 0; i < 300; i++) {
            p.behind = (i % 2) == 0;
            if (p.updateSwap(true)) flips++;
        }
        CHECK(flips == 0, "alternating behind/ahead flipped the swap interval %d times", flips);
        // the user's vsync-off wins
        p.resetSwap(true);
        CHECK(p.updateSwap(false) && !p.swapSync, "vsync off in settings drops the swap interval");
        CHECK(!p.updateSwap(false), "and stays off");
    }

    std::printf(g_fail ? "pacing_test: %d FAILED\n" : "pacing_test: OK\n", g_fail);
    return g_fail ? 1 : 0;
}
