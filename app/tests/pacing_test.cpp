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
using vj::SwapGrid;

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
    std::function<double(long)> cost = nullptr;   // optional: cost of emulated frame #i (overrides c)
    // A blocking swap returns this long after its vblank: swapMin + swapSpread * u^2 (u uniform in [0, 1)), and one in
    // 50 a further swapLate (a compositor hiccup).  Real inline traces (#1141): 0.3-1 ms, positive-skewed.
    double swapMin = 0, swapSpread = 0, swapLate = 0;
    bool grid = false;        // book steps on the vblank grid (SwapGrid), as App::stepEmulation does
    double gridRefresh = 0;   // the refresh period SwapGrid is told (0 = the display's true one)
};

struct Result {
    double fps = 0;        // emulated frames per real second
    double images = 0;     // loop iterations that produced at least one new emulated frame, per second
    double multi = 0;      // fraction of those iterations that ran more than one frame
    double sleepSteps = 0; // fraction of iterations that slept
    double slept = 0;      // fraction of real time spent in sleep
    int toggles = 0;       // swap-interval changes
    bool syncEnd = false;  // the swap interval is 1 when the run ends
    double uneven = 0;     // per minute, vsync locked: a new image after more (or fewer) refreshes than the core's
                           // period rounds to (a repeat at 60 Hz)
    double dropped = 0;    // per minute: emulated frames never shown (all but the last of a multi-frame step)
    bool gridLocked = false;
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
    SwapGrid g;
    Rng rng;
    double now = 0, last = 0, sleptT = 0, shownAt = -1;
    long frames = 0, steps = 0, imgs = 0, multi = 0, sleeps = 0, uneven = 0, dropped = 0;
    const long want = s.vblank > 0 ? std::max(1L, std::lround(s.period / s.vblank)) : 1;
    Result res;
    while (now < secs) {
        double dt = now - last;
        last = now;
        const double c = s.grid ? g.correct(now) : 0;
        int n = p.plan(dt + c, s.period);
        for (int i = 0; i < n; i++) now += frameCost(s, frames + i, rng);
        frames += n;
        if (n > 1) dropped += n - 1;
        if (n > 0) {
            p.charge(now - last, s.period);
            last = now;
            imgs++;
            if (n > 1) multi++;
        } else {
            p.behind = false;
        }
        now += std::max(0.0, s.u + s.jitter * rng.next());
        if (p.swapSync && s.vblank > 0) {   // SDL_GL_SwapWindow blocks
            const double begin = now, v = nextVblank(now, s.vblank);
            now = v + s.swapMin;
            if (s.swapSpread > 0 || s.swapLate > 0) {   // (no draws otherwise: the other scenarios keep their stream)
                const double u = 0.5 * (1.0 + rng.next());
                now += s.swapSpread * u * u + (rng.next() > 0.96 ? s.swapLate : 0.0);
            }
            if (s.grid) g.onSwap(begin, now, s.gridRefresh > 0 ? s.gridRefresh : s.vblank);
            if (n > 0) {
                if (shownAt >= 0 && std::lround((v - shownAt) / s.vblank) != want) uneven++;
                shownAt = v;
            }
        } else {
            if (s.grid) g.reset();
            shownAt = -1;
        }
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
    res.syncEnd = p.swapSync;
    res.gridLocked = g.locked();
    res.uneven = 60.0 * (double)uneven / now;
    res.dropped = 60.0 * (double)dropped / now;
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

    // ---- a transient must not cost the vsync for the rest of the session ------------------------------------
    // A heavy phase (Cybermorph 5.8 -> 14 ms, a savestate load, a loaded machine) releases the swap interval; when
    // the core is back to a cost a vblank-locked loop holds, it must come back, once.
    for (double v : {V60, V120}) {
        for (double after : {0.011, 0.0125}) {
            Scenario s{P, after, 0.0015, v, true, 0.0003, [=](long i) { return i < 150 ? 0.022 : after; }};
            Result n = simulateNew(s, 20);
            // the tail of the run must be vsync-locked again: images/s ~ frames/s and no batching
            CHECK(n.toggles >= 1 && n.toggles <= 2, "heavy phase then %.1f ms, %.0f Hz: %d toggles, want 1 or 2",
                  after * 1000, 1.0 / v, n.toggles);
            CHECK(n.syncEnd, "heavy phase then %.1f ms, %.0f Hz: vsync never came back", after * 1000, 1.0 / v);
        }
    }
    // a stall (OS hitch, a state load) on a core with time to spare: no change of swap interval
    for (double v : {V60, V120}) {
        Scenario s{P, 0.008, 0.0015, v, true, 0.0003, [](long i) { return i == 300 ? 0.250 : (i >= 600 && i < 603) ? 0.040 : 0.008; }};
        Result n = simulateNew(s, kLong);
        CHECK(n.toggles == 0, "a 250 ms stall and a 3-frame 40 ms hitch, %.0f Hz: %d toggles", 1.0 / v, n.toggles);
    }

    // ---- swap-return jitter, inline + vsync (#1141) ---------------------------------------------------------
    // A blocking swap returns a little after its vblank, by a varying amount (Studio traces: residual sd 0.3-0.5 ms, a
    // few ms late now and then on a loaded host).  On real time the backlog's phase then flips takes near a period
    // boundary: bursts of repeats and dropped frames.  Booked on the vblank grid, the 60.05 Hz core slips once per
    // crossing: (60.05 - 60) x 60 = 3 frames a minute on a 60 Hz display (dropped), 6.6 at 59.94 Hz.  A slip may cost a
    // repeat too where two frames do not fit in one refresh (7 ms of work), never more.  At 120 Hz a step owes half a
    // period, so the pacer has two boundaries per period and an image comes one refresh early twice as often.  The grid
    // must not slow the game down.
    struct Disp {
        const char* name;
        double vblank, told;   // the display's true refresh, and the one SwapGrid is told (0 = the true one)
        double slip;           // slips a minute a 60.05 Hz core has to make on it
        double maxUneven;      // uneven images a minute allowed on the grid
    };
    const Disp disps[] = {{"60 Hz", V60, 0, 3.0, 4.0},
                          {"59.94 Hz", V5994, 0, 6.6, 7.6},
                          {"59.94 Hz announced as 60", V5994, V60, 6.6, 7.6},
                          {"120 Hz", V120, 0, 3.0, 7.0}};
    for (const Disp& d : disps) {
        for (double c : {0.0035, 0.007}) {
            // (7 ms of work + the UI is a whole 120 Hz refresh: whether a frame makes the next vblank is the frame cost's
            // jitter, not the pacer's)
            if (c > 0.005 && d.vblank < 0.01) continue;
            for (int noisy = 0; noisy < 2; noisy++) {
                Scenario s{P, c, 0.0015, d.vblank, true, 0.0003};
                s.swapMin = 0.0002;
                s.swapSpread = noisy ? 0.0016 : 0.0012;
                s.swapLate = noisy ? std::min(0.004, 0.25 * d.vblank) : 0.0;   // (later than half a refresh is the next vblank's)
                s.gridRefresh = d.told;
                Result r = simulateNew(s, kLong);
                s.grid = true;
                Result n = simulateNew(s, kLong);
                char what[96];
                std::snprintf(what, sizeof what, "%s swap jitter, %s, core %.1f ms", noisy ? "noisy" : "quiet", d.name,
                              c * 1000);
                std::printf("  %-52s real time: uneven %5.1f dropped %5.1f /min -> grid: uneven %4.1f dropped %4.1f "
                            "/min, %.3f fps\n",
                            what, r.uneven, r.dropped, n.uneven, n.dropped, n.fps);
                CHECK(n.gridLocked, "%s: the grid never locked", what);
                // (noisy at 120 Hz: a swap returning 2-4 ms late leaves too little of an 8.3 ms refresh for the next
                // step's work, and that frame misses its vblank whatever the pacing)
                const double allowed = noisy ? std::max(d.maxUneven, 0.25 * r.uneven) : d.maxUneven;
                CHECK(n.uneven <= allowed, "%s: %.1f uneven images a minute on the grid, want <= %.1f", what, n.uneven,
                      allowed);
                CHECK(n.dropped <= d.slip + 1.5, "%s: %.1f dropped frames a minute on the grid, want ~%.1f", what, n.dropped,
                      d.slip);
                CHECK(n.fps >= r.fps - 0.04 && n.fps < 1.0 / P + 0.04, "%s: %.3f fps on the grid, %.3f on real time", what,
                      n.fps, r.fps);
                CHECK(n.toggles == 0, "%s: %d swap-interval toggles", what, n.toggles);
                if (!noisy) CHECK(r.uneven >= 5.0, "%s: only %.1f uneven a minute on real time (the point of the test)", what,
                                  r.uneven);
            }
        }
    }
    {   // PAL (50.08 Hz) on a 60 Hz display: one refresh in six repeats whatever the pacing; the grid changes nothing
        Scenario s{1.0 / 50.08, 0.004, 0.0015, V60, true, 0.0003};
        s.swapMin = 0.0002;
        s.swapSpread = 0.0012;
        Result r = simulateNew(s, kLong);
        s.grid = true;
        Result n = simulateNew(s, kLong);
        std::printf("  %-52s real time: %.3f fps %.1f img/s dropped %.1f /min -> grid: %.3f fps %.1f img/s dropped %.1f\n",
                    "PAL on 60 Hz, quiet swap jitter", r.fps, r.images, r.dropped, n.fps, n.images, n.dropped);
        CHECK(std::fabs(n.fps - r.fps) < 0.01 && std::fabs(n.fps - 50.08) < 0.05, "PAL: %.3f fps on the grid", n.fps);
        CHECK(n.images >= r.images - 0.05, "PAL: %.2f img/s on the grid, %.2f on real time", n.images, r.images);
        CHECK(n.dropped <= r.dropped + 0.5, "PAL: %.1f dropped a minute on the grid", n.dropped);
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
    {   // SwapGrid on its own (#1141)
        Rng r;
        // swaps that do not block (vsync off, an occluded window) never lock it
        SwapGrid g;
        double t = 0;
        for (int i = 0; i < 600; i++) {
            t += V60 + 0.002 * r.next();
            g.onSwap(t - 0.0001, t, V60);
            CHECK(g.correct(t + 0.0002) == 0, "non-blocking swap %d: a correction", i);
        }
        CHECK(!g.locked(), "non-blocking swaps locked the grid");
        // blocking swaps at no fixed rate (VRR, a compositor that is not refresh-locked) never lock it either
        SwapGrid h;
        t = 0;
        for (int i = 0; i < 600; i++) {
            t += 0.008 + 0.017 * 0.5 * (1.0 + r.next());
            h.onSwap(t - 0.004, t, V60);
            CHECK(!h.locked(), "random intervals locked the grid at swap %d", i);
        }
        // a vblank-locked loop: locked after kLockAfter swaps; the corrections telescope (no rate change: their sum is the
        // first step's lag minus the last one's), and each step is booked on its swap's vblank
        SwapGrid k;
        double sum = 0, raw0 = 0, raw = 0, grid0 = 0;
        int first = -1;
        for (int i = 0; i < 1200; i++) {
            const double v = (i + 1) * V5994;
            const double u = 0.5 * (1.0 + r.next());
            const double end = v + 0.0002 + 0.0012 * u * u;
            k.onSwap(v - 0.005, end, V60);   // (told 60 Hz, the display is 59.94)
            const double step = end + 0.0003 * (1.0 + r.next());
            const double c = k.correct(step);
            if (k.locked() && first < 0) {
                first = i;
                CHECK(c == 0, "the first locked step starts a chain (correction %.6f)", c);
                raw0 = step;
                grid0 = v;
            }
            if (first >= 0) {
                sum += c;
                raw = step;
            }
        }
        CHECK(first >= SwapGrid::kLockAfter - 1 && first < 60, "locked at swap %d", first);
        CHECK(k.locked(), "a 59.94 Hz grid announced as 60 Hz lost the lock");
        const double booked = (raw - raw0) + sum, grid = 1200 * V5994 - grid0;   // (the last swap's vblank - the first's)
        CHECK(std::fabs(booked - grid) < 0.001, "booked %.6f s, the vblanks span %.6f s", booked, grid);
        CHECK(std::fabs(sum) < 0.003, "the corrections drifted: %.6f s", sum);
        // one swap 4 ms late while locked: booked at its vblank, the lock kept
        const double v = 1201 * V5994;
        k.onSwap(v - 0.005, v + 0.004, V60);
        const double c = k.correct(v + 0.0043);
        CHECK(k.locked(), "one late swap dropped the lock");
        CHECK(c < -0.0025 && c > -0.0045, "a 4 ms late swap was booked %.4f s early, want ~-3.4 ms", c);
        // a step that is not paced starts a new chain; a new refresh rate starts over and locks on the new grid
        k.restart();
        k.onSwap(v + V5994 - 0.005, v + V5994 + 0.0004, V60);
        CHECK(k.correct(v + V5994 + 0.0006) == 0, "after restart(): a correction on the first step");
        int relock = -1;
        for (int i = 0; i < 200 && relock < 0; i++) {
            const double w = v + 0.1 + i * V120;
            k.onSwap(w - 0.003, w + 0.0003, V120);
            if (k.locked()) relock = i;
        }
        CHECK(relock >= SwapGrid::kLockAfter - 1 && relock < 60, "a new refresh rate relocked at swap %d", relock);
        k.reset();
        CHECK(!k.locked() && k.correct(1e3) == 0, "reset() leaves it unlocked");
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
