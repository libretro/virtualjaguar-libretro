// Frame pacing (pacing.h) against a fake clock -- no window, no core, no sleeping, so the result does not depend
// on how loaded the machine is (the real-time numbers of issue #952 moved by 3x with whatever else was running).
//
//   vj_pacing_test
//
// A simulated UI loop that mirrors App::run / App::stepEmulation: each iteration plans frames, "runs" them (cost c
// per frame, with jitter), draws (cost u), blocks until the next vblank when the swap interval is 1, then either
// leaves the swap interval alone or sleeps as the loop does.  The LEGACY loop below is the pacing App had before
// #956: backlog zeroed when over 2 periods, sleep computed from a stale accumulator, vblank wait always on.
#include "pacing.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <functional>

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

// Deterministic jitter source (xorshift): uniform in [-1, 1).
struct Rng {
    uint64_t s = 0x9E3779B97F4A7C15ull;
    double next() {
        s ^= s << 13;
        s ^= s >> 7;
        s ^= s << 17;
        return (double)(s >> 11) / (double)(1ull << 52) - 1.0;
    }
};

struct Scenario {
    double period;   // core period, s
    double c;        // cost of one emulated frame, s (mean)
    double u;        // UI + upload cost per loop iteration, s
    double vblank;   // display refresh interval, s (0 = no display clock)
    bool vsync;      // vsync wanted
    double jitter = 0;                       // +- uniform noise on every cost (frame, UI), s
    std::function<double(long)> cost;        // optional: cost of emulated frame #i (overrides c)
};

struct Result {
    double fps = 0;        // emulated frames per real second
    double images = 0;     // loop iterations that produced at least one new emulated frame, per second
    double multi = 0;      // fraction of those iterations that ran more than one frame
    double sleepSteps = 0; // fraction of iterations that slept
    double slept = 0;      // fraction of real time spent in sleep
    int toggles = 0;       // swap-interval changes
};

// Seconds until the next vblank boundary at/after t.
double nextVblank(double t, double v) { return std::ceil(t / v - 1e-9) * v; }

double frameCost(const Scenario& s, long i, Rng& r) {
    double c = s.cost ? s.cost(i) : s.c;
    return std::max(0.0005, c + s.jitter * r.next());
}

Result simulateNew(const Scenario& s, double secs) {
    Pacer p;
    p.resetSwap(s.vsync);
    Rng rng;
    double now = 0, last = 0, sleptT = 0;
    long frames = 0, steps = 0, imgs = 0, multi = 0, sleeps = 0;
    Result res;
    while (now < secs) {
        double dt = now - last;
        last = now;
        int n = p.plan(dt, s.period);
        for (int i = 0; i < n; i++) now += frameCost(s, frames + i, rng);
        frames += n;
        if (n > 0) {
            p.charge(now - last, s.period);
            last = now;
            imgs++;
            if (n > 1) multi++;
        } else {
            p.behind = false;
        }
        now += std::max(0.0, s.u + s.jitter * rng.next());
        if (p.swapSync && s.vblank > 0) now = nextVblank(now, s.vblank);   // SDL_GL_SwapWindow blocks
        if (p.updateSwap(s.vsync)) res.toggles++;
        steps++;
        if (!p.swapSync) {
            double w = p.sleepFor(now - last, s.period);
            if (w > 0) {
                sleeps++;
                sleptT += w;
                now += w + 0.00015 * (1.0 + rng.next());   // oversleep
            }
        }
    }
    res.fps = (double)frames / now;
    res.images = (double)imgs / now;
    res.multi = imgs ? (double)multi / (double)imgs : 0;
    res.sleepSteps = steps ? (double)sleeps / (double)steps : 0;
    res.slept = sleptT / now;
    return res;
}

// The pacing before #952, verbatim in structure.
Result simulateLegacy(const Scenario& s, double secs) {
    Rng rng;
    double accum = 0, now = 0, last = 0;
    long frames = 0, imgs = 0, multi = 0;
    Result res;
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
        for (int i = 0; i < n; i++) now += frameCost(s, frames + i, rng);
        frames += n;
        if (n > 0) imgs++;
        if (n > 1) multi++;
        now += std::max(0.0, s.u + s.jitter * rng.next());
        if (s.vsync && s.vblank > 0) now = nextVblank(now, s.vblank);
        if (!s.vsync) {
            double wait = s.period - accum - 0.001;
            if (wait > 0.0005) now += std::min(wait, 0.016);
        }
    }
    res.fps = (double)frames / now;
    res.images = (double)imgs / now;
    res.multi = imgs ? (double)multi / (double)imgs : 0;
    return res;
}

void report(const char* what, const Scenario& s, const Result& o, const Result& n) {
    std::printf("  %-38s core %5.1f ms: legacy %5.1f fps %5.1f img/s -> now %5.1f fps %5.1f img/s, %d toggles\n", what,
                s.c * 1000.0, o.fps, o.images, n.fps, n.images, n.toggles);
}

}  // namespace

int main() {
    const double P = 1.0 / 60.05;   // the core's field rate (JTRM 524 half-lines): a hair shorter than a 60 Hz display's
    const double V60 = 1.0 / 60.0, V5994 = 1.0 / 59.94, V120 = 1.0 / 120.0, V11988 = 1.0 / 119.88;
    const double kLong = 30;        // simulated seconds of a "long run"

    // ---- a core with time to spare, vsync on: the swap interval is never touched ---------------------------
    // 8 ms of work per frame, a few hundred us of jitter.  The old pacer read "behind" whenever the frame ran
    // late in its period (accum includes the work just run), so the vsync was released and restored forever.
    for (double v : {V60, V5994, V120, V11988}) {
        Scenario s{P, 0.008, 0.0015, v, true, 0.0003};
        Result o = simulateLegacy(s, kLong), n = simulateNew(s, kLong);
        char what[64];
        std::snprintf(what, sizeof what, "ahead, vsync on, %.2f Hz display", 1.0 / v);
        report(what, s, o, n);
        CHECK(n.toggles == 0, "%s: %d swap-interval toggles in %.0f s, want 0", what, n.toggles, kLong);
        CHECK(n.multi < 0.01, "%s: %.1f%% of iterations ran more than one frame", what, 100 * n.multi);
        // a vblank-locked loop cannot run faster than the display (59.94 Hz) -- game speed within 0.3% of the core's
        CHECK(n.fps > 59.8 && n.fps < 60.2, "%s: %.2f fps, want ~%.2f", what, n.fps, 1.0 / P);
        CHECK(n.images > 0.98 * n.fps, "%s: only %.1f of %.1f frames/s became an image", what, n.images, n.fps);
        CHECK(n.fps >= o.fps - 0.15, "%s: %.2f fps is worse than legacy %.2f", what, n.fps, o.fps);
    }
    // the same across core costs from idle to nearly a whole period (the loop is vblank locked all the way)
    for (double c : {0.002, 0.005, 0.011, 0.0135}) {
        for (double v : {V60, V120}) {
            Scenario s{P, c, 0.0015, v, true, 0.0003};
            Result n = simulateNew(s, kLong);
            CHECK(n.toggles == 0, "ahead c=%.1f ms, %.0f Hz: %d toggles", c * 1000, 1.0 / v, n.toggles);
            CHECK(n.multi < 0.02, "ahead c=%.1f ms, %.0f Hz: %.1f%% multi-frame iterations", c * 1000, 1.0 / v,
                  100 * n.multi);
        }
    }

    // heavier jitter (1 ms) and other refresh rates: still no toggles, no habit of batching
    // (only where a vblank-locked loop can hold the core's rate: a 50 Hz display, or 13.5 ms of work at 75 Hz, is a
    // real deficit for vsync and may release it)
    for (double hz : {59.94, 60.0, 75.0, 100.0, 144.0}) {
        for (double c : {0.003, 0.008, 0.012}) {
            if (std::ceil((c + 0.0015 + 0.003) * hz) / hz > P) continue;
            Scenario s{P, c, 0.0015, 1.0 / hz, true, 0.001};
            Result n = simulateNew(s, kLong);
            CHECK(n.toggles == 0, "ahead c=%.0f ms, %.2f Hz, 1 ms jitter: %d toggles", c * 1000, hz, n.toggles);
            CHECK(n.fps > 0.97 * std::min(hz, 1.0 / P), "ahead c=%.0f ms, %.2f Hz: %.2f fps", c * 1000, hz, n.fps);
        }
    }

    // ---- vsync off, a core that cannot hold 60: it runs flat out (the win of #956) -------------------------
    // RISC 300% on Doom, idle skip off, M2: 18-19.4 ms per frame in the window.
    for (double c : {0.018, 0.0194}) {
        Scenario s{P, c, 0.0015, 0, false, 0.0003};
        Result o = simulateLegacy(s, kLong), n = simulateNew(s, kLong);
        report("behind, vsync off", s, o, n);
        double ceiling = 1.0 / (s.c + s.u / 3);   // 3 frames share one UI frame
        CHECK(n.fps > 0.95 * ceiling, "behind, vsync off (c=%.1f): %.2f fps is under 95%% of the core's %.2f", c * 1000,
              n.fps, ceiling);
        CHECK(n.fps > o.fps * 1.05, "behind, vsync off (c=%.1f): now %.2f is not >5%% better than legacy %.2f", c * 1000,
              n.fps, o.fps);
        CHECK(n.slept < 0.01, "behind, vsync off (c=%.1f): slept %.1f%% of the time between batches", c * 1000,
              100 * n.slept);
    }

    // ---- vsync off, keeping up: the loop sleeps (no busy spin) ---------------------------------------------
    for (double c : {0.003, 0.008}) {
        Scenario s{P, c, 0.0015, 0, false, 0.0003};
        Result n = simulateNew(s, kLong);
        CHECK(n.sleepSteps > 0.5, "ahead, vsync off (c=%.1f): slept on only %.0f%% of iterations", c * 1000,
              100 * n.sleepSteps);
        CHECK(std::fabs(n.fps - 1.0 / P) < 0.3, "ahead, vsync off (c=%.1f): %.2f fps, want %.2f", c * 1000, n.fps, 1.0 / P);
        CHECK(n.toggles == 0, "ahead, vsync off: %d toggles", n.toggles);
    }

    // ---- a core that truly cannot keep up, vsync ON ---------------------------------------------------------
    // The swap interval is released on a sustained deficit (and only then): a blocking swap on the thread that runs
    // the emulation otherwise quantises the rate to whole refreshes (20 ms of work = 30 fps at 60 Hz, 40 at best).
    for (double v : {V60, V120}) {
        for (double c : {0.0194, 0.022, 0.025}) {
            Scenario s{P, c, 0.0015, v, true, 0.0003};
            Result o = simulateLegacy(s, kLong), n = simulateNew(s, kLong);
            char what[64];
            std::snprintf(what, sizeof what, "behind, vsync on, %.0f Hz display", 1.0 / v);
            report(what, s, o, n);
            double ceiling = 1.0 / (s.c + s.u / 3);
            CHECK(n.fps > 0.95 * ceiling, "%s (c=%.1f): %.2f fps is under 95%% of the core's %.2f", what, c * 1000, n.fps,
                  ceiling);
            CHECK(n.fps > o.fps * 1.05, "%s (c=%.1f): now %.2f is not >5%% better than legacy %.2f", what, c * 1000, n.fps,
                  o.fps);
            CHECK(n.toggles <= 2, "%s (c=%.1f): %d toggles, a real deficit should release once", what, c * 1000, n.toggles);
        }
    }

    // ---- hovering at the limit (core ~ one period): never worse than before --------------------------------
    for (double v : {V60, V120}) {
        for (double c : {0.0145, 0.0155, 0.0165, 0.0175}) {
            Scenario s{P, c, 0.0015, v, true, 0.0003};
            Result o = simulateLegacy(s, kLong), n = simulateNew(s, kLong);
            report("edge, vsync on", s, o, n);
            CHECK(n.fps >= o.fps - 0.6, "edge c=%.1f ms, %.0f Hz: now %.2f is worse than legacy %.2f", c * 1000, 1.0 / v,
                  n.fps, o.fps);
            CHECK(n.images >= o.images - 0.5, "edge c=%.1f ms, %.0f Hz: %.2f img/s is worse than legacy %.2f", c * 1000,
                  1.0 / v, n.images, o.images);
            CHECK(n.toggles <= 4, "edge c=%.1f ms, %.0f Hz: %d toggles in %.0f s", c * 1000, 1.0 / v, n.toggles, kLong);
        }
    }

    // ---- cost ALTERNATING around one refresh (the #952 trap) ----------------------------------------------
    // A naive EMA or a "one frame per vblank" rule collapses into running 2 frames per 2 refreshes when the cost
    // alternates around a refresh interval.  Whatever the pacer does here must be no worse than v3.9.0.
    for (double v : {V60, V120}) {
        for (double hi : {0.020, 0.022}) {
            double lo = 0.034 - hi;   // mean cost 17 ms: just over the period
            Scenario s{P, 0.017, 0.0015, v, true, 0.0003, [=](long i) { return (i & 1) ? hi : lo; }};
            Result o = simulateLegacy(s, kLong), n = simulateNew(s, kLong);
            char what[64];
            std::snprintf(what, sizeof what, "alternating %.0f/%.0f ms, %.0f Hz", lo * 1000, hi * 1000, 1.0 / v);
            report(what, s, o, n);
            CHECK(n.fps >= o.fps - 0.6, "%s: %.2f fps is worse than legacy %.2f", what, n.fps, o.fps);
            CHECK(n.images >= o.images - 0.3, "%s: %.2f img/s is worse than legacy %.2f", what, n.images, o.images);
            CHECK(n.toggles <= 4, "%s: %d toggles", what, n.toggles);
        }
        // and a cheaper alternation that a vblank-locked loop can hold at full speed (mean 14 ms)
        Scenario s{P, 0.014, 0.0015, v, true, 0.0003, [](long i) { return (i & 1) ? 0.018 : 0.010; }};
        Result o = simulateLegacy(s, kLong), n = simulateNew(s, kLong);
        char what[64];
        std::snprintf(what, sizeof what, "alternating 10/18 ms, %.0f Hz", 1.0 / v);
        report(what, s, o, n);
        CHECK(n.fps >= o.fps - 0.6, "%s: %.2f fps is worse than legacy %.2f", what, n.fps, o.fps);
        CHECK(n.images >= o.images - 0.3, "%s: %.2f img/s is worse than legacy %.2f", what, n.images, o.images);
        CHECK(n.toggles <= 4, "%s: %d toggles", what, n.toggles);
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
    {   // a frame run late in its period (vblank locked) is not a deficit: one banked period must not read "behind"
        Pacer p;
        p.resetSwap(true);
        p.plan(P + 0.009, P);   // a late phase: the first frame runs 9 ms into its period
        for (int i = 0; i < 600; i++) {
            p.charge(0.0125, P);   // a 12.5 ms frame ends 21.5 ms into it: one period banked, not a deficit
            CHECK(p.behind, "step %d: the late phase reads as a frame due (the point of the test)", i);
            CHECK(!p.updateSwap(true), "step %d: swap interval changed on a steady loop", i);
            p.plan(P - 0.0125, P);   // the iteration lasts exactly one period (vblank locked)
        }
        CHECK(p.swapSync, "still synced");
    }
    {   // the user's vsync-off wins, always
        Pacer p;
        p.resetSwap(true);
        CHECK(p.updateSwap(false) && !p.swapSync, "vsync off in settings drops the swap interval");
        CHECK(!p.updateSwap(false), "and stays off");
        for (int i = 0; i < 2000; i++) {
            p.plan(P, P);
            p.charge(0.004, P);
            CHECK(!p.updateSwap(false), "vsync off in settings must never be turned back on (%d)", i);
        }
        CHECK(!p.swapSync, "still off");
    }

    std::printf(g_fail ? "pacing_test: %d FAILED\n" : "pacing_test: OK\n", g_fail);
    return g_fail ? 1 : 0;
}
