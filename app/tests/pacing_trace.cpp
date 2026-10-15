// Replays a MEASURED per-frame cost trace through the real Pacer (pacing.h) on a fake clock and reports what a
// viewer would see: how often a display refresh repeats the previous emulated frame, how often an emulated frame
// is run but never shown, and how uneven the gaps between new images are (issue #952, stutter).
//
//   c++ -std=c++17 -O2 -Isrc -o pacing_trace tests/pacing_trace.cpp
//   pacing_trace costs.csv [--col ms] [--scale 1.7] [--ui 1.5] [--hz 60|120] [--vsync 1|0] [--secs 60]
//                          [--period 16.65] [--decoupled 1 --lat 16.7]
//
// costs.csv is frame_cost_probe's --csv output (header row, a "ms" column; or --col N for a 0-based column);
// the trace is replayed cyclically.  --scale multiplies every cost (a host that is k times slower than the one
// that measured it); --ui is the per-iteration UI + upload cost in ms; --hz the display refresh.
//
// With --decoupled 1 the same cost trace is replayed through the alternative that keeps emulation off the UI
// thread (frame i started on the i * period timeline, shown at the first vblank >= i * period + --lat); it
// models a core that holds speed, so use it on traces where the core does.  Not a model of an overloaded core.
//
// Validated against the real app: `virtualjaguar --perf-frames N` with VJ_PERF_CSV=iters.csv writes one row per
// loop iteration (see perf.h); the sim's swap-toggle rate and uneven-gap share match it to within a factor of two
// on a 60 Hz display (Doom R1/R2/R3, Mac Studio).
//
// Model: one loop iteration = plan() -> run the owed frames (their traced costs) -> charge() -> draw (ui) ->
// vsync wait to the next vblank (swap interval 1) or the loop's own sleepFor() (swap interval 0), exactly the
// order App::stepEmulation / the main loop use.  Only the LAST frame of a batch is shown (wantVideo), so a batch
// of 2 drops one image.  A refresh "repeats" when the iteration that ended at it ran no frame.
#include "pacing.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

using vj::Pacer;


static double pct(std::vector<double> v, double p) {
    if (v.empty()) return 0;
    std::sort(v.begin(), v.end());
    size_t i = (size_t)(p * (double)(v.size() - 1) + 0.5);
    return v[i];
}

int main(int argc, char** argv) {
    if (argc < 2) {
        std::fprintf(stderr, "usage: pacing_trace costs.csv [--col NAME|N] [--scale k] [--ui ms] [--hz H] [--vsync 0|1] "
                             "[--secs S] [--period ms] [--decoupled 1 [--lat ms]]\n");
        return 2;
    }
    const char* path = argv[1];
    std::string col = "ms";
    double scale = 1.0, ui = 1.5, hz = 60, secs = 60, periodMs = 1000.0 / 60.0544;
    int vsync = 1, decoupled = 0;
    double latMs = 16.7;
    for (int i = 2; i + 1 < argc; i += 2) {
        if (!std::strcmp(argv[i], "--col")) col = argv[i + 1];
        else if (!std::strcmp(argv[i], "--scale")) scale = std::atof(argv[i + 1]);
        else if (!std::strcmp(argv[i], "--ui")) ui = std::atof(argv[i + 1]);
        else if (!std::strcmp(argv[i], "--hz")) hz = std::atof(argv[i + 1]);
        else if (!std::strcmp(argv[i], "--vsync")) vsync = std::atoi(argv[i + 1]);
        else if (!std::strcmp(argv[i], "--secs")) secs = std::atof(argv[i + 1]);
        else if (!std::strcmp(argv[i], "--decoupled")) decoupled = std::atoi(argv[i + 1]);
        else if (!std::strcmp(argv[i], "--lat")) latMs = std::atof(argv[i + 1]);
        else if (!std::strcmp(argv[i], "--period")) periodMs = std::atof(argv[i + 1]);
    }
    std::ifstream f(path);
    std::string line;
    if (!std::getline(f, line)) { std::fprintf(stderr, "empty %s\n", path); return 2; }
    int ci = -1;
    {
        std::stringstream ss(line);
        std::string tok;
        int k = 0;
        while (std::getline(ss, tok, ',')) {
            if (tok == col) ci = k;
            k++;
        }
        if (ci < 0 && std::isdigit((unsigned char)col[0])) ci = std::atoi(col.c_str());
    }
    if (ci < 0) { std::fprintf(stderr, "no column %s\n", col.c_str()); return 2; }
    std::vector<double> cost;
    while (std::getline(f, line)) {
        std::stringstream ss(line);
        std::string tok;
        for (int k = 0; std::getline(ss, tok, ','); k++)
            if (k == ci) cost.push_back(std::atof(tok.c_str()) * scale / 1000.0);
    }
    if (cost.empty()) { std::fprintf(stderr, "no rows\n"); return 2; }

    const double period = periodMs / 1000.0, u = ui / 1000.0, vb = 1.0 / hz;
    if (decoupled) {
        // Proposal: emulation on its own thread, frame i started on the fixed timeline i * period (or as soon as
        // the previous one is done), each finished frame queued; the presenter shows frame i at the first vblank
        // at or after (i * period + latency), or when it is finished if that is later.  The UI frame on the main
        // thread no longer sits in the emulation's way.
        const double lat = latMs / 1000.0;
        double fin = 0, lastShown = -1;
        long total = (long)(secs * 1000.0 / periodMs), shown = 0, late = 0;
        std::vector<double> gaps;
        for (long i = 0; i < total + 60; i++) {
            double start = std::max((double)i * period, fin);
            fin = start + cost[(size_t)i % cost.size()];
            double want = (double)i * period + lat;
            double at = std::ceil(std::max(want, fin) / vb - 1e-9) * vb;
            if (fin > want + 1e-9) late++;
            if (i >= 60) {
                shown++;
                if (lastShown >= 0) gaps.push_back((at - lastShown) * 1000.0);
                lastShown = at;
            }
        }
        long bad = 0;
        for (double g : gaps) if (std::fabs(g - periodMs) > periodMs * 0.2) bad++;
        std::printf("trace %-34s scale %.2f hz %.0f DECOUPLED lat %.1f ms: frames %ld, finished after their present slot %ld (%.1f%%) | "
                    "image gap ms p50 %.1f p99 %.1f max %.1f, off-by->20%% %ld (%.1f%%)\n",
                    std::strrchr(path, '/') ? std::strrchr(path, '/') + 1 : path, scale, hz, latMs, shown, late,
                    100.0 * (double)late / (double)total, pct(gaps, 0.5), pct(gaps, 0.99), pct(gaps, 1.0), bad,
                    gaps.empty() ? 0.0 : 100.0 * (double)bad / (double)gaps.size());
        return 0;
    }
    Pacer p;
    p.resetSwap(vsync != 0);
    double now = 0, last = 0;
    size_t ci_ = 0;
    long toggles = 0;
    double relTime = 0;
    long emulated = 0, shown = 0, dropped = 0, repeats = 0, refreshes = 0, iters = 0, idle = 0;
    double lastShown = -1;
    std::vector<double> gaps;
    // Warm: do not count the first second.
    const double warm = 1.0;
    long emu0 = 0;
    double t0 = 0;
    bool counting = false;
    auto nextVblank = [&](double t) { return std::ceil(t / vb - 1e-9) * vb; };
    while (now < secs + warm) {
        double lastNow = now;
        double dt = now - last;
        last = now;
        int n = p.plan(dt, period);
        for (int i = 0; i < n; i++) {
            now += cost[ci_++ % cost.size()];
            if (counting) emulated++;
        }
        if (n > 0) {
            p.charge(now - last, period);
            last = now;
        } else {
            p.behind = false;
        }
        now += u;
        bool wasSync = p.swapSync;
        if (p.updateSwap(vsync != 0) && counting) toggles++;
        (void)wasSync;
        // The swap: with swap interval 1 the loop blocks to the next vblank; with 0 it does not, but the display
        // (and the macOS compositor) still only shows the image at the next vblank.
        double swapAt = p.swapSync ? nextVblank(now) : now;
        double shownAt = nextVblank(now);
        if (!counting && now >= warm) { counting = true; t0 = now; emu0 = emulated; }
        if (counting) {
            iters++;
            if (n > 0) {
                shown++;
                if (n > 1) dropped += n - 1;
                if (lastShown >= 0) gaps.push_back((shownAt - lastShown) * 1000.0);
                lastShown = shownAt;
            } else {
                idle++;
            }
        }
        if (counting && !p.swapSync) relTime += swapAt - lastNow;
        now = swapAt;
        if (!p.swapSync) now += p.sleepFor(now - last, period);
    }
    double wall = now - t0;
    refreshes = (long)std::floor(wall * hz);
    repeats = refreshes - shown;   // refreshes that showed an image already shown
    double ideal = period * 1000.0;
    long bad = 0;
    for (double g : gaps)
        if (std::fabs(g - ideal) > ideal * 0.2) bad++;
    std::printf("trace %-34s scale %.2f ui %.1f hz %.0f vsync %d: core %.1f fps | refreshes %ld new-images %ld "
                "repeats %ld (%.1f%%) dropped-frames %ld | image gap ms p50 %.1f p99 %.1f max %.1f, off-by->20%% %ld (%.1f%%) | "
                "swap toggles %ld, unsynced %.0f%% of time\n",
                std::strrchr(path, '/') ? std::strrchr(path, '/') + 1 : path, scale, ui, hz, vsync,
                (double)(emulated - emu0) / wall, refreshes, shown, repeats, refreshes ? 100.0 * (double)repeats / (double)refreshes : 0.0,
                dropped, pct(gaps, 0.5), pct(gaps, 0.99), pct(gaps, 1.0), bad,
                gaps.empty() ? 0.0 : 100.0 * (double)bad / (double)gaps.size(), toggles, 100.0 * relTime / wall);
    (void)iters;
    (void)idle;
    return 0;
}
