// Mode A and mode B pacing (pacing_clock.h) against a fake clock, fake vblank ticks and a fake SDL audio device: no
// thread, no sleeping, no core (#967, #1038).  The device pulls 512-frame chunks like SDL's (SDL_HINT_AUDIO_DEVICE_SAMPLE_FRAMES, audio.cpp).
//
//   vj_pacing_clock_test
#include "pacing_clock.h"

#include "audio.h"   // AudioOut::nextRatio, mode B's stream ratio

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <functional>
#include <memory>
#include <string>
#include <vector>

using vj::PacingClock;
using vj::PacingInput;

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

const double kCoreHz = 60.05445;     // NTSC field rate (libretro.c retro_get_system_av_info; JTRM)
const double kPeriod = 1.0 / kCoreHz;
const double kStreamHz = 48044.0;    // AudioOut::open: the stream runs at the rounded core rate
const double kBatch = 800.0;         // stereo frames per emulated frame (src/jerry/dac.c, NTSC)
const double kChunk = 512.0;         // frames per device pull
const double kTargetMs = 64.0;       // audio.target_ms default

struct Device {
    double rate = kStreamHz;   // stream frames this device consumes per second (its crystal)
    double queued = 0;         // stream frames queued
    double nextPull = 0;
    bool draining = true;
    double countFrom = 0;      // underruns before this time are not counted (start-up)
    uint64_t underruns = 0;
    double pullJitter = 0;     // each pull lands up to this many seconds off its slot (a late or early audio thread)
    uint32_t seed = 1;
    double slot = 0;           // the pull's nominal time (pulls stay on the crystal's grid; only their timing jitters)
    void advanceTo(double t) {
        while (nextPull <= t) {
            if (draining) {
                if (queued < kChunk && nextPull >= countFrom) underruns++;
                queued = std::max(0.0, queued - kChunk);
            }
            slot += kChunk / rate;
            nextPull = slot + (pullJitter > 0 ? pullJitter * (2.0 * rnd(seed) - 1.0) : 0.0);
        }
    }
    static double rnd(uint32_t& s) {   // a fixed LCG: the same jitter on every run and platform
        s = s * 1664525u + 1013904223u;
        return (double)(s >> 8) / 16777216.0;
    }
    double ms() const { return 1000.0 * queued / kStreamHz; }
    void prime() { queued = kTargetMs / 1000.0 * kStreamHz; }
};

struct Run {
    std::vector<double> starts, ends;   // each emulated frame's start and end (= when it can be shown)
    std::vector<double> trims;          // the trim each frame's period was stretched by
    uint64_t dropped = 0, steers = 0;
    bool lockedAtEnd = false;
};

// The emulation thread's loop on a fake clock: ask when the next frame is due, "sleep" until then, run a frame of
// cost(i) seconds, and queue its audio at its end (fast-forward drops it, as App does).
// takeHz > 0: the UI takes frames `lead` before each vblank of a takeHz display and tells the clock (steering).
Run simulate(double secs, const std::function<double(size_t)>& cost, bool audio, Device& dev, bool ff = false,
             double takeHz = 0, double lead = 0, const std::function<double(size_t)>& wakeLate = nullptr) {
    PacingClock clock;
    Run r;
    double t = 0;
    clock.reset(t);
    size_t i = 0;
    while (t < secs) {
        dev.advanceTo(t);
        PacingInput in;
        in.now = t;
        in.period = kPeriod;
        in.audioQueuedMs = audio ? dev.ms() : -1.0;
        in.targetMs = kTargetMs;
        in.fastForward = ff;
        if (takeHz > 0) {
            in.takePeriod = 1.0 / takeHz;
            in.takeAt = std::floor((t + lead) * takeHz) / takeHz - lead;   // the UI's latest take
        }
        const double due = clock.nextDue(in);
        if (due > t + 1e-12) {
            t = due + (wakeLate ? wakeLate(i) : 0.0);   // the thread wakes up late (timer slack, scheduling)
            continue;
        }
        clock.frameStarted(in);
        r.trims.push_back(clock.trim());
        r.starts.push_back(t);
        t += cost(i++);
        dev.advanceTo(t);
        if (!ff) dev.queued += kBatch;
        r.ends.push_back(t);
        clock.frameEnded(t);
    }
    r.dropped = clock.droppedFrames();
    r.steers = clock.steers();
    r.lockedAtEnd = clock.audioLocked();
    return r;
}

struct Shown {
    int repeats = 0, skips = 0, vblanks = 0;
};

// A display refreshing at `hz` shows, at each vblank, the newest frame finished by then -- or, with `lead`, by that
// long before it (the late latch's take point: the UI takes the frame `lead` before the vblank it is shown at).
Shown present(const Run& r, double hz, double from, double to, double lead = 0) {
    Shown s;
    size_t k = 0;
    long last = -1;
    for (long n = (long)std::ceil(from * hz); (double)n / hz < to; n++) {
        const double v = (double)n / hz - lead;
        while (k < r.ends.size() && r.ends[k] <= v) k++;
        const long newest = (long)k - 1;
        if (last >= 0) {
            if (newest == last) s.repeats++;
            else if (newest > last + 1) s.skips += (int)(newest - last - 1);
        }
        last = newest;
        s.vblanks++;
    }
    return s;
}

// Frames per second between two times, from the first and last start inside them.
double rateBetween(const Run& r, double from, double to) {
    double first = -1, last = -1;
    size_t n = 0;
    for (double s : r.starts) {
        if (s < from || s >= to) continue;
        if (first < 0) first = s;
        last = s;
        n++;
    }
    return n > 1 ? (double)(n - 1) / (last - first) : 0.0;
}

// The largest distance of a frame-start interval from the mean interval, between two times (seconds).
double unevenness(const Run& r, double from, double to) {
    std::vector<double> d;
    for (size_t i = 1; i < r.starts.size(); i++)
        if (r.starts[i - 1] >= from && r.starts[i] < to) d.push_back(r.starts[i] - r.starts[i - 1]);
    if (d.empty()) return 1.0;
    double mean = 0;
    for (double x : d) mean += x;
    mean /= (double)d.size();
    double worst = 0;
    for (double x : d) worst = std::max(worst, std::fabs(x - mean));
    return worst;
}

double fiveMs(size_t) { return 0.005; }

void testTrueRateOn60Hz() {
    Device dev;
    dev.prime();
    Run r = simulate(70.0, fiveMs, true, dev);
    const double want = kStreamHz / kBatch;   // frames' worth of audio the device consumes per second
    const double got = rateBetween(r, 10.0, 70.0);
    const Shown s = present(r, 60.0, 10.0, 70.0);
    CHECK(std::fabs(got - want) / want < 1e-4, "frame rate %.5f Hz, the audio clock's %.5f Hz", got, want);
    CHECK(s.repeats == 0, "a 60.05 Hz core on a 60 Hz panel never repeats a frame (%d repeats)", s.repeats);
    CHECK(s.skips >= 2 && s.skips <= 5, "one skipped frame about every 18 s: %d in 60 s", s.skips);
    CHECK(unevenness(r, 10.0, 70.0) < 0.00005, "frame starts are even (worst %.4f ms off)",
          1000.0 * unevenness(r, 10.0, 70.0));
    CHECK(dev.underruns == 0, "no underrun (%llu)", (unsigned long long)dev.underruns);
    CHECK(r.dropped == 0 && r.lockedAtEnd, "nothing dropped, still following the audio clock");
}

void testFollowsTheCrystal() {
    for (double ppm : {300.0, -300.0}) {
        Device dev;
        dev.rate = kStreamHz * (1.0 + ppm * 1e-6);
        dev.prime();
        Run r = simulate(100.0, fiveMs, true, dev);
        const double want = dev.rate / kBatch;
        const double got = rateBetween(r, 50.0, 100.0);
        CHECK(std::fabs(got - want) / want < 3e-5, "a %+.0f ppm sound card: %.5f Hz, want %.5f", ppm, got, want);
        CHECK(dev.underruns == 0, "%+.0f ppm: no underrun (%llu)", ppm, (unsigned long long)dev.underruns);
    }
}

void testNoAudioClock() {
    Device dev;
    Run r = simulate(5.0, fiveMs, false, dev);
    const double got = rateBetween(r, 1.0, 5.0);
    CHECK(std::fabs(got - kCoreHz) / kCoreHz < 1e-6, "no audio clock (no device, a paused or muted stream): the wall clock at the core's rate (%.5f Hz)", got);
    CHECK(unevenness(r, 1.0, 5.0) < 1e-9, "exactly one period apart");
    CHECK(!r.lockedAtEnd, "not audio-locked");
}

void testDeviceStopsDraining() {
    Device dev;
    dev.prime();
    dev.draining = false;   // unplugged, or the stream stopped pulling
    Run r = simulate(5.0, fiveMs, true, dev);
    const double got = rateBetween(r, 1.0, 5.0);
    CHECK(std::fabs(got - kCoreHz) / kCoreHz < 1e-6, "the game keeps its own rate on the wall clock (%.5f Hz)", got);
    CHECK(!r.lockedAtEnd, "and stops following the dead audio clock");
}

void testRefillFromEmpty() {
    Device dev;   // a fresh stream: nothing queued
    dev.countFrom = 1.0;
    Run r = simulate(10.0, fiveMs, true, dev);
    double closest = 1.0;
    for (size_t i = 1; i < r.starts.size() && r.starts[i] < 0.5; i++) closest = std::min(closest, r.starts[i] - r.starts[i - 1]);
    CHECK(closest >= kPeriod / PacingClock::kRefillRate - 1e-9, "refilling runs at most %.0fx (closest starts %.2f ms)",
          PacingClock::kRefillRate, 1000.0 * closest);
    CHECK(dev.underruns == 0, "refilled within a second: no underrun after it (%llu)", (unsigned long long)dev.underruns);
    CHECK(r.lockedAtEnd, "then it follows the audio clock");
    // After the refill the grid must be even again: the refill must not leave the grid ahead of the clock, or the next
    // frame sleeps a long stretch, the queue drains, and refill re-arms (a limit cycle of bursts and stalls).
    const double worst = unevenness(r, 1.0, 10.0);
    CHECK(worst < 0.002, "frame starts are even after the refill (worst %.2f ms off)", 1000.0 * worst);
    const double want = kStreamHz / kBatch;
    const double got = rateBetween(r, 1.0, 10.0);
    CHECK(std::fabs(got - want) / want < 1e-3, "and run at the audio clock's rate: %.4f Hz, want %.4f", got, want);
    CHECK(r.dropped == 0, "nothing dropped (%llu)", (unsigned long long)r.dropped);
}

void testSpikeCatchesUp() {
    Device dev;
    Run r = simulate(3.0, [](size_t i) { return i == 100 ? 0.030 : 0.005; }, false, dev);
    CHECK(r.dropped == 0, "one 30 ms frame (under two periods late) is caught up, not dropped");
    CHECK(std::fabs(r.starts[101] - r.ends[100]) < 1e-9, "the late frame runs at once");
    CHECK(std::fabs((r.starts[110] - r.starts[0]) - 110 * kPeriod) < 1e-9, "and the frames are back on their grid");
}

void testStallDropsOwedTime() {
    Device dev;
    Run r = simulate(3.0, [](size_t i) { return i == 100 ? 0.500 : 0.005; }, false, dev);
    CHECK(r.dropped >= 25, "a 500 ms stall drops the owed frames (%llu)", (unsigned long long)r.dropped);
    const double back = r.ends[100];
    int burst = 0;
    for (double s : r.starts)
        if (s >= back && s < back + 2.5 * kPeriod) burst++;
    CHECK(burst <= 3, "no burst after the stall: %d frames in the next 2.5 periods", burst);
}

void testFastForward() {
    Device dev;
    dev.prime();
    Run r = simulate(2.0, [](size_t) { return 0.001; }, true, dev, true);
    const double want = PacingClock::kFastForward * kCoreHz;
    const double got = rateBetween(r, 0.5, 2.0);
    CHECK(std::fabs(got - want) / want < 1e-6, "fast-forward: %.2f Hz, want %.2f", got, want);
    CHECK(!r.lockedAtEnd, "fast-forward ignores the audio clock");
}

// ---- the presentation-evenness model (#967) ----------------------------------------------------------------------
// The threaded loop showed 60-160 repeats and as many skips per minute where the 60.05-vs-60 Hz beat alone predicts
// 3-4 skips.  Two candidate causes, separated here:
//   * the trim: the +/-0.5% audio trim moving the grid's phase much faster than the 0.054 Hz beat, so frame ends keep
//     crossing the take point (device pulls jittering is what would drive it);
//   * the ends' spread: each frame ends at start + cost, so a cost (or wake-up) spread of S ms makes every crossing of
//     the take point by the slowly drifting grid ambiguous for S / 15 us frames (15 us = the beat's drift per frame),
//     each of which can come out as a repeat + skip pair.

// The emulation grid's phase that the trim alone moved (seconds), max - min between two times.
double trimWander(const Run& r, double from, double to) {
    double ph = 0, lo = 0, hi = 0;
    for (size_t i = 0; i < r.starts.size(); i++) {
        if (r.starts[i] < from || r.starts[i] >= to) continue;
        ph += (r.trims[i] - 1.0) * kPeriod;
        lo = std::min(lo, ph);
        hi = std::max(hi, ph);
    }
    return hi - lo;
}

std::function<double(size_t)> jitteredCost(double base, double spread, uint32_t seed) {
    auto st = std::make_shared<uint32_t>(seed);
    return [=](size_t) { return base + spread * (Device::rnd(*st) - 0.5); };
}

void modelTable() {
    std::printf("model: 70 s, measured 10-70 s, 60 Hz panel; events = repeats + skips\n");
    for (double pull : {0.0, 0.002, 0.005})
        for (double spread : {0.0, 0.0005, 0.001, 0.002, 0.004})
            for (double lead : {0.0, 0.003})
                for (bool steer : {false, true}) {
                    Device dev;
                    dev.prime();
                    dev.pullJitter = pull;
                    Run r = simulate(70.0, jitteredCost(0.006, spread, 7), true, dev, false, steer ? 60.0 : 0.0, lead);
                    const Shown sh = present(r, 60.0, 10.0, 70.0, lead);
                    std::printf("  pull jitter %.0f ms  cost spread %.1f ms  take %.0f ms early  steer %d:  repeats %3d"
                                "  skips %3d  steps %2llu  trim phase wander %.2f ms  underruns %llu\n",
                                1000 * pull, 1000 * spread, 1000 * lead, (int)steer, sh.repeats, sh.skips,
                                (unsigned long long)r.steers, 1000 * trimWander(r, 10, 70),
                                (unsigned long long)dev.underruns);
                }
}

// How many vblanks each frame stayed on screen: hold[n] = frames shown for n refreshes (0 = never shown).
std::vector<int> holds(const Run& r, double hz, double from, double to, double lead) {
    std::vector<int> hold(8, 0);
    size_t k = 0;
    long last = -1;
    int run = 0;
    for (long n = (long)std::ceil(from * hz); (double)n / hz < to; n++) {
        const double v = (double)n / hz - lead;
        while (k < r.ends.size() && r.ends[k] <= v) k++;
        const long newest = (long)k - 1;
        if (newest == last) {
            run++;
            continue;
        }
        if (last >= 0) {
            hold[std::min(run, 7)]++;
            hold[0] += (int)(newest - last - 1);
        }
        last = newest;
        run = 1;
    }
    return hold;
}

void testSteeringRestoresTheBeat() {
    // A 6 ms frame whose cost varies by 4 ms, taken 3 ms before each vblank of a 60 Hz panel.
    Device dev;
    dev.prime();
    Run plain = simulate(70.0, jitteredCost(0.006, 0.004, 7), true, dev, false, 0.0, 0.003);
    const Shown a = present(plain, 60.0, 10.0, 70.0, 0.003);
    CHECK(a.repeats + a.skips > 100, "the mechanism: unsteered, a 4 ms spread repeats and skips at every crossing "
          "(%d + %d in 60 s)", a.repeats, a.skips);
    Device dev2;
    dev2.prime();
    Run r = simulate(70.0, jitteredCost(0.006, 0.004, 7), true, dev2, false, 60.0, 0.003);
    const Shown s = present(r, 60.0, 10.0, 70.0, 0.003);
    CHECK(s.repeats == 0, "steered: no repeat (%d)", s.repeats);
    CHECK(s.skips >= 2 && s.skips <= 5, "steered: the beat's one skip per ~18 s (%d in 60 s)", s.skips);
    CHECK(r.steers >= 3 && r.steers <= 8, "one phase step per crossing, not one per frame (%llu)",
          (unsigned long long)r.steers);
    CHECK(dev2.underruns == 0 && r.dropped == 0, "no underrun, nothing dropped");
}

void testSteeringKeepsTheAudioClock() {
    Device dev;
    dev.rate = kStreamHz * (1.0 + 200e-6);   // a sound card 200 ppm fast
    dev.prime();
    Run r = simulate(200.0, jitteredCost(0.006, 0.004, 11), true, dev, false, 60.0, 0.003);
    const double want = dev.rate / kBatch;
    const double got = rateBetween(r, 50.0, 200.0);
    CHECK(std::fabs(got - want) / want < 3e-5, "steered, the long-run rate is still the sound card's: %.5f Hz, want %.5f",
          got, want);
    CHECK(dev.underruns == 0, "no underrun (%llu)", (unsigned long long)dev.underruns);
    CHECK(std::fabs(dev.ms() - kTargetMs) < 25.0, "the queue stays near its target (%.1f ms)", dev.ms());
}

void testSteeringAt120Hz() {
    // Two refreshes per frame: every frame should stay on screen for exactly two, except at the beat's crossings.
    for (bool steer : {false, true}) {
        Device dev;
        dev.prime();
        Run r = simulate(70.0, jitteredCost(0.006, 0.004, 5), true, dev, false, steer ? 120.0 : 0.0, 0.002);
        const std::vector<int> h = holds(r, 120.0, 10.0, 70.0, 0.002);
        const int odd = h[0] + h[1] + h[3] + h[4];
        if (steer) CHECK(odd <= 8, "120 Hz, steered: frames held 1 or 3 refreshes only at crossings (%d)", odd);
        else CHECK(odd > 40, "120 Hz, unsteered: the same mechanism (%d frames held 1 or 3 refreshes)", odd);
    }
}

void testSteeringFollowsACostThatSwitches() {
    // Measured on the Mac Studio (yarc, VJ_PERF_TRACE): the thread wakes 0.5-3 ms late and the frame cost switches with
    // the content between ~7.5 ms and ~2.9 ms for seconds at a time.  When it drops, the ends jump 4.5 ms earlier,
    // possibly onto the take point, while the window still remembers the 7.5 ms frames.
    auto cost = [](size_t i) { return ((i / 300) % 2 ? 0.0029 : 0.0075) + 0.0004 * (double)((i * 7) % 5) / 4.0; };
    auto late = [](size_t i) { return 0.0005 + 0.0025 * (double)((i * 13) % 11) / 10.0; };
    for (double lead : {0.0025, 0.003, 0.004, 0.006, 0.009, 0.012}) {
        Device dev;
        dev.prime();
        Run r = simulate(70.0, cost, true, dev, false, 60.0, lead, late);
        const Shown s = present(r, 60.0, 10.0, 70.0, lead);
        CHECK(s.repeats + s.skips <= 12, "a cost that switches 7.5 <-> 2.9 ms every 5 s, take %.1f ms early: %d repeats "
              "+ %d skips in 60 s (%llu steps)", 1000 * lead, s.repeats, s.skips, (unsigned long long)r.steers);
    }
}

void testSteeringWithAWideWindow() {
    // Cybermorph on the Studio: 5-11 ms frames, 0.5-3 ms late, so a ~9 ms window and ~9 ms steps.  Each early step
    // queues that much audio; the trim then slows the grid, and past ~12 ms of queue error (0.09%, the beat) the grid
    // drifts the other way.  Steering must follow the drift it sees, not the untrimmed one.
    auto cost = [](size_t i) { return 0.005 + 0.006 * (double)((i * 37) % 101) / 100.0; };
    auto late = [](size_t i) { return 0.0005 + 0.0025 * (double)((i * 13) % 11) / 10.0; };
    for (double lead : {0.002, 0.004, 0.006}) {
        Device dev;
        dev.prime();
        Run r = simulate(130.0, cost, true, dev, false, 60.0, lead, late);
        const Shown s = present(r, 60.0, 10.0, 130.0, lead);
        CHECK(s.repeats + s.skips <= 20, "a 9 ms window, take %.0f ms early: %d repeats + %d skips in 120 s (%llu steps)",
              1000 * lead, s.repeats, s.skips, (unsigned long long)r.steers);
        CHECK(dev.underruns == 0, "no underrun (%llu)", (unsigned long long)dev.underruns);
    }
}

void testSteeringFollowsTheTrimmedDrift() {
    // The trim held where the grid runs slower than the panel: here a sound card 0.15% slow (59.96 Hz of frames on a
    // 60 Hz panel), on hardware the queue error the steps themselves leave.  The grid now drifts the other way; the
    // crossings are repeats, about one per 28 s, and steering must step later, not earlier.
    for (double lead : {0.003, 0.009}) {
        Device dev;
        dev.rate = kStreamHz * (1.0 - 0.0015);
        dev.prime();
        Run r = simulate(130.0, jitteredCost(0.006, 0.004, 9), true, dev, false, 60.0, lead);
        const Shown s = present(r, 60.0, 10.0, 130.0, lead);
        CHECK(s.skips == 0 && s.repeats >= 2 && s.repeats <= 8, "a grid slower than the panel, take %.0f ms early: "
              "%d repeats + %d skips in 120 s (%llu steps)", 1000 * lead, s.repeats, s.skips,
              (unsigned long long)r.steers);
    }
}

void testNoSteeringOffAWholeRatio() {
    // A PAL game (50.08 Hz) on a 60 Hz panel, or 60.05 Hz on 75 / 144 Hz: each frame lands ~3 ms further along the take
    // grid, so some frame's window always holds a take point -- that is ordinary 5:6 (4:5, 5:12) judder, not a slow
    // crossing, and stepping for it would cost production every 30 frames forever.  No steps, no underruns, the rate kept.
    struct Case { double coreHz, panelHz; };
    for (const Case c : {Case{50.08013, 60.0}, Case{60.05445, 75.0}, Case{60.05445, 144.0}}) {
        Device dev;
        const double period = 1.0 / c.coreHz;
        dev.rate = kBatch / period;   // a sound card consuming exactly the core's rate (dev.rate is in stream frames/s)
        dev.prime();
        PacingClock clock;
        Run r;
        double t = 0;
        clock.reset(t);
        uint32_t seed = 3;
        while (t < 60.0) {
            dev.advanceTo(t);
            PacingInput in;
            in.now = t;
            in.period = period;
            in.audioQueuedMs = dev.ms();
            in.targetMs = kTargetMs;
            in.takePeriod = 1.0 / c.panelHz;
            in.takeAt = std::floor((t + 0.003) * c.panelHz) / c.panelHz - 0.003;
            const double due = clock.nextDue(in);
            if (due > t + 1e-12) {
                t = due;
                continue;
            }
            clock.frameStarted(in);
            r.starts.push_back(t);
            t += 0.004 + 0.004 * Device::rnd(seed);
            dev.advanceTo(t);
            dev.queued += kBatch;
            clock.frameEnded(t);
        }
        const double got = rateBetween(r, 20.0, 60.0);
        CHECK(clock.steers() == 0, "%.2f Hz on %.0f Hz: no steps (%llu)", c.coreHz, c.panelHz,
              (unsigned long long)clock.steers());
        CHECK(dev.underruns == 0, "%.2f Hz on %.0f Hz: no underrun (%llu)", c.coreHz, c.panelHz,
              (unsigned long long)dev.underruns);
        CHECK(std::fabs(got - c.coreHz) / c.coreHz < 1e-4, "%.2f Hz on %.0f Hz: the rate is kept (%.4f Hz)", c.coreHz,
              c.panelHz, got);
    }
}

void testWindowTooWideDoesNotSteer() {
    Device dev;
    dev.prime();
    Run r = simulate(20.0, jitteredCost(0.008, 0.016, 3), true, dev, false, 60.0, 0.003);   // 0-16 ms frames
    CHECK(r.steers == 0, "a spread of a whole period leaves no gap to steer into (%llu steps)",
          (unsigned long long)r.steers);
}

// ---- Mode B (#1038): frames on the display's vblank ticks -------------------------------------------------------

struct ModeRun : Run {
    uint64_t droppedTicks = 0, refillFrames = 0;
    std::vector<unsigned> kAtStart;   // the mode each frame started in (0 = A)
};

// The emulation thread under either mode, one model for both: a display refreshing at `hz` swaps at each vblank and
// publishes a tick 0.5 ms later; kAt(t) is DisplayLock's verdict (0 = mode A, else ticks per frame).  Mode A runs as
// simulate() does (the stream's ratio stays 1: update(false)); mode B waits for ticks, and before each frame the
// stream's ratio takes AudioOut's step around base = (hz / k) / core (update(true)).  The device consumes the stream
// at its rate times that ratio.  extra(t) adds to a frame's cost (a stall).
ModeRun simulateModes(double secs, double hz, const std::function<unsigned(double)>& kAt, Device& dev,
                      const std::function<double(size_t)>& cost, const std::function<double(double)>& extra = nullptr,
                      double targetMs = kTargetMs) {
    PacingClock clock;
    ModeRun r;
    double t = 0, ratio = 1.0;
    const double tickLag = 0.0005, nominal = dev.rate;
    uint64_t seq = 0;
    double nextTick = tickLag;
    clock.reset(t);
    size_t i = 0;
    while (t < secs) {
        while (nextTick <= t + 1e-12) {
            seq++;
            nextTick += 1.0 / hz;
        }
        const unsigned k = kAt(t);
        const double base = k ? (hz / k) * kPeriod : 1.0;
        if (!k) ratio = 1.0;                 // leaving B: setBaseRatio(1)
        else if (!clock.ticking()) ratio = base;   // entering B: setBaseRatio(base)
        dev.rate = nominal * ratio;
        dev.advanceTo(t);
        PacingInput in;
        in.now = t;
        in.period = kPeriod;
        in.audioQueuedMs = dev.ms();
        in.targetMs = targetMs;
        in.tickSeq = seq;
        in.ticksPerFrame = k;
        if (k) {
            if (!clock.tickDue(in)) {
                t = nextTick;
                continue;
            }
            ratio = vj::AudioOut::nextRatio(ratio, base, (float)dev.ms(), (float)targetMs);
            dev.rate = nominal * ratio;
            clock.tickStarted(in);
        } else {
            const double due = clock.nextDue(in);
            if (due > t + 1e-12) {
                t = due;
                continue;
            }
            clock.frameStarted(in);
        }
        r.starts.push_back(t);
        r.kAtStart.push_back(k);
        t += cost(i++) + (extra ? extra(t) : 0.0);
        dev.advanceTo(t);
        dev.queued += kBatch;
        r.ends.push_back(t);
        clock.frameEnded(t);
    }
    r.dropped = clock.droppedFrames();
    r.droppedTicks = clock.droppedTicks();
    r.refillFrames = clock.refillFrames();
    dev.rate = nominal;
    return r;
}

size_t startsBetween(const Run& r, double from, double to) {
    size_t n = 0;
    for (double s : r.starts) n += (s >= from && s < to) ? 1 : 0;
    return n;
}

void testTicksPerFrame() {
    for (unsigned k : {1u, 2u}) {
        const double hz = 60.0 * k;
        Device dev;
        dev.prime();
        ModeRun r = simulateModes(20.0, hz, [k](double) { return k; }, dev, fiveMs);
        const double got = rateBetween(r, 5.0, 20.0);
        CHECK(std::fabs(got - hz / k) < 1e-6, "k=%u on %.0f Hz: one frame every %u ticks (%.5f Hz)", k, hz, k, got);
        // Every frame starts on a tick: 0.5 ms after a vblank.
        double worst = 0;
        for (size_t i = 0; i < r.starts.size(); i++) {
            if (r.starts[i] < 1.0) continue;
            const double ph = r.starts[i] * hz - std::floor(r.starts[i] * hz);
            worst = std::max(worst, std::fabs(ph / hz - 0.0005));
        }
        CHECK(worst < 1e-6, "k=%u: every frame starts on a tick (worst %.3f ms off)", k, 1000.0 * worst);
        CHECK(r.dropped == 0 && r.droppedTicks == 0, "k=%u: nothing dropped", k);
        CHECK(dev.underruns == 0, "k=%u: no underrun (%llu)", k, (unsigned long long)dev.underruns);
        CHECK(std::fabs(dev.ms() - kTargetMs) < 0.25 * kTargetMs, "k=%u: the queue holds near the target (%.1f ms)", k,
              dev.ms());
    }
}

// Steady state never refills, over the whole audio.target_ms range (20-200 ms, ui.cpp) and a jittery audio thread: at
// 20 ms one frame's audio is most of the target, so a refill keyed on the queue level alone would fire every frame.
void testTickSteadyStateNeverRefills() {
    for (double target : {20.0, 64.0, 200.0})
        for (unsigned k : {1u, 2u})
            for (double jitter : {0.0, 0.002, 0.005}) {
                Device dev;
                dev.queued = target / 1000.0 * kStreamHz;
                dev.pullJitter = jitter;
                dev.countFrom = 2.0;
                const double hz = 60.0 * k;
                ModeRun r = simulateModes(30.0, hz, [k](double) { return k; }, dev, fiveMs, nullptr, target);
                CHECK(r.refillFrames == 0 && r.droppedTicks == 0,
                      "target %.0f ms, k=%u, pull jitter %.0f ms: no refill (%llu extra frames, %llu dropped ticks)",
                      target, k, 1000.0 * jitter, (unsigned long long)r.refillFrames, (unsigned long long)r.droppedTicks);
                CHECK(std::fabs(rateBetween(r, 5.0, 30.0) - hz / k) < 1e-6, "target %.0f ms, k=%u: one frame per k ticks",
                      target, k);
                CHECK(jitter > 0.0 || dev.underruns == 0, "target %.0f ms, k=%u: no underrun (%llu)", target, k,
                      (unsigned long long)dev.underruns);
            }
}

// The reason mode B exists: a 60.05 Hz core on a 60.00 / 59.94 Hz panel skips a frame every 18 s / 9 s in mode A, and
// never in mode B.  Both arms through the same loop, device and display model.
void testNoPeriodicSkipInModeB() {
    struct Panel {
        double hz;
        int aMin, aMax;
    };
    for (const Panel& p : {Panel{60.0, 2, 5}, Panel{59.94, 5, 9}}) {
        Device devA, devB;
        devA.prime();
        devB.prime();
        ModeRun a = simulateModes(70.0, p.hz, [](double) { return 0u; }, devA, fiveMs);
        ModeRun b = simulateModes(70.0, p.hz, [](double) { return 1u; }, devB, fiveMs);
        const Shown sa = present(a, p.hz, 10.0, 70.0), sb = present(b, p.hz, 10.0, 70.0);
        CHECK(sa.skips + sa.repeats >= p.aMin && sa.skips + sa.repeats <= p.aMax,
              "mode A on %.2f Hz: the beat's %d-%d skips in 60 s (%d skips, %d repeats)", p.hz, p.aMin, p.aMax, sa.skips,
              sa.repeats);
        CHECK(sb.skips == 0 && sb.repeats == 0, "mode B on %.2f Hz: no skip or repeat in 60 s (%d skips, %d repeats)",
              p.hz, sb.skips, sb.repeats);
        CHECK(devB.underruns == 0, "mode B on %.2f Hz: no underrun (%llu)", p.hz, (unsigned long long)devB.underruns);
        CHECK(std::fabs(devB.ms() - kTargetMs) < 0.25 * kTargetMs, "mode B on %.2f Hz: the queue holds (%.1f ms)", p.hz,
              devB.ms());
    }
}

// A stall of 3.5 periods: the owed ticks are dropped and counted, and at most one catch-up frame follows.
void testTickStallDropsOwedTicks() {
    Device dev;
    dev.prime();
    bool stalled = false;
    double stallEnd = 0;
    ModeRun r = simulateModes(15.0, 60.0, [](double) { return 1u; }, dev, fiveMs, [&](double t) {
        if (stalled || t < 10.0) return 0.0;
        stalled = true;
        stallEnd = t + 0.005 + 3.5 / 60.0;
        return 3.5 / 60.0;
    });
    CHECK(r.droppedTicks >= 1 && r.droppedTicks <= 3, "a 3.5-period stall drops the owed ticks (%llu)",
          (unsigned long long)r.droppedTicks);
    CHECK(startsBetween(r, stallEnd, stallEnd + 1.0 / 60.0) <= 2, "at most one catch-up frame after it (%zu starts)",
          startsBetween(r, stallEnd, stallEnd + 1.0 / 60.0));
    CHECK(std::fabs(rateBetween(r, 11.0, 15.0) - 60.0) < 1e-6, "then one frame per tick again");
}

// A stall longer than the target empties the queue.  The frame that ends it and the catch-up frame leave it near half
// the target; the refill after the dropped ticks puts it back at once, where the +/-0.5% trim alone takes about 6 s.
void testTickRefillAfterAStall() {
    Device dev;
    dev.prime();
    bool stalled = false;
    double stallEnd = 1e9, qSum = 0, qMin = 1e9;
    int qN = 0;
    ModeRun r = simulateModes(11.25, 60.0, [](double) { return 1u; }, dev, fiveMs, [&](double t) {
        if (!stalled && t >= 10.0) {
            stalled = true;
            stallEnd = t + 0.205;
            dev.countFrom = stallEnd + 0.1;
            return 0.2;
        }
        if (t >= stallEnd + 0.5) {   // the queue at each frame start, before its audio
            qSum += dev.ms();
            qN++;
        }
        if (t >= stallEnd + 0.1) qMin = std::min(qMin, dev.ms());
        return 0.0;
    });
    const double qMean = qN ? qSum / qN : 0.0;
    CHECK(stallEnd < 10.25, "the stall ran (ends %.3f s)", stallEnd);
    CHECK(qMean > 0.75 * kTargetMs, "half a second after a 200 ms stall the queue is back near the target (mean %.1f ms)",
          qMean);
    CHECK(dev.underruns == 0 && qMin > 0.5 * kTargetMs, "no underrun after it (%llu; lowest %.1f ms)",
          (unsigned long long)dev.underruns, qMin);
    CHECK(std::fabs(rateBetween(r, 10.75, 11.25) - 60.0) < 1e-6, "and frames are back on one per tick (%.4f Hz)",
          rateBetween(r, 10.75, 11.25));
}

// A <-> B: no owed time crosses the switch (no burst either way), mode A starts its grid over, and the ratio is
// mode A's 1 again after B.
void testModeSwitches() {
    Device dev;
    dev.prime();
    auto kAt = [](double t) { return (t >= 5.0 && t < 10.0) ? 1u : 0u; };
    ModeRun r = simulateModes(15.0, 60.0, kAt, dev, fiveMs);
    for (double sw : {5.0, 10.0}) {
        size_t n = startsBetween(r, sw, sw + 0.1);
        CHECK(n >= 5 && n <= 7, "switch at %.0f s: no burst (%zu frames in the next 100 ms)", sw, n);
    }
    double minGap = 1.0;
    for (size_t i = 1; i < r.starts.size(); i++) minGap = std::min(minGap, r.starts[i] - r.starts[i - 1]);
    CHECK(minGap >= 0.005 - 1e-9, "frames never overlap or burst (closest starts %.2f ms apart)", 1000.0 * minGap);
    CHECK(r.dropped == 0 && r.droppedTicks == 0, "nothing dropped across the switches");
    CHECK(std::fabs(rateBetween(r, 6.0, 10.0) - 60.0) < 1e-6, "B in the middle: one frame per tick");
    const double wantA = kStreamHz / kBatch;
    CHECK(std::fabs(rateBetween(r, 12.0, 15.0) - wantA) / wantA <= PacingClock::kMaxTrim,
          "A after: the audio clock's rate within mode A's trim (%.4f Hz)", rateBetween(r, 12.0, 15.0));
    CHECK(dev.underruns == 0, "no underrun across the switches (%llu)", (unsigned long long)dev.underruns);

    PacingClock c;
    PacingInput in;
    in.now = 1.0;
    in.tickSeq = 100;
    in.ticksPerFrame = 1;
    in.audioQueuedMs = kTargetMs;
    CHECK(!c.tickDue(in) && c.ticking(), "entering B: the first frame waits for the next tick");
    in.tickSeq = 101;
    CHECK(c.tickDue(in), "and runs on it");
    c.tickStarted(in);
    CHECK(c.trim() == 1.0 && !c.audioLocked(), "B has no trim of its own (the stream's ratio does it)");
    in.tickSeq = 40;   // a new stream of ticks (counted from 0 again)
    CHECK(!c.tickDue(in), "a tick count that went back starts over: wait for the next tick");
    in.tickSeq = 41;
    CHECK(c.tickDue(in), "...and run on it");
    in.now = 2.0;
    CHECK(c.nextDue(in) <= in.now && !c.ticking(), "back to A: due at once, nothing owed from B");
}

}  // namespace

int main(int argc, char** argv) {
    if (argc > 1 && std::string(argv[1]) == "--model") modelTable();   // the numbers behind the #967 evenness notes
    testTrueRateOn60Hz();
    testFollowsTheCrystal();
    testNoAudioClock();
    testDeviceStopsDraining();
    testRefillFromEmpty();
    testSpikeCatchesUp();
    testStallDropsOwedTime();
    testFastForward();
    testSteeringRestoresTheBeat();
    testSteeringKeepsTheAudioClock();
    testSteeringAt120Hz();
    testSteeringFollowsACostThatSwitches();
    testSteeringWithAWideWindow();
    testSteeringFollowsTheTrimmedDrift();
    testNoSteeringOffAWholeRatio();
    testWindowTooWideDoesNotSteer();
    testTicksPerFrame();
    testTickSteadyStateNeverRefills();
    testNoPeriodicSkipInModeB();
    testTickStallDropsOwedTicks();
    testTickRefillAfterAStall();
    testModeSwitches();
    std::printf(g_fail ? "pacing_clock_test: %d FAILED\n" : "pacing_clock_test: OK\n", g_fail);
    return g_fail ? 1 : 0;
}
