// Mode A pacing (pacing_clock.h) against a fake clock and a fake SDL audio device: no thread, no sleeping, no core
// (#967).  The device pulls 512-frame chunks like SDL's (SDL_HINT_AUDIO_DEVICE_SAMPLE_FRAMES, audio.cpp).
//
//   vj_pacing_clock_test
#include "pacing_clock.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <functional>
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
    void advanceTo(double t) {
        while (nextPull <= t) {
            if (draining) {
                if (queued < kChunk && nextPull >= countFrom) underruns++;
                queued = std::max(0.0, queued - kChunk);
            }
            nextPull += kChunk / rate;
        }
    }
    double ms() const { return 1000.0 * queued / kStreamHz; }
    void prime() { queued = kTargetMs / 1000.0 * kStreamHz; }
};

struct Run {
    std::vector<double> starts, ends;   // each emulated frame's start and end (= when it can be shown)
    uint64_t dropped = 0;
    bool lockedAtEnd = false;
};

// The emulation thread's loop on a fake clock: ask when the next frame is due, "sleep" until then, run a frame of
// cost(i) seconds, and queue its audio at its end (fast-forward drops it, as App does).
Run simulate(double secs, const std::function<double(size_t)>& cost, bool audio, Device& dev, bool ff = false) {
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
        const double due = clock.nextDue(in);
        if (due > t + 1e-12) {
            t = due;
            continue;
        }
        clock.frameStarted(in);
        r.starts.push_back(t);
        t += cost(i++);
        dev.advanceTo(t);
        if (!ff) dev.queued += kBatch;
        r.ends.push_back(t);
    }
    r.dropped = clock.droppedFrames();
    r.lockedAtEnd = clock.audioLocked();
    return r;
}

struct Shown {
    int repeats = 0, skips = 0, vblanks = 0;
};

// A display refreshing at `hz` shows, at each vblank, the newest frame finished by then.
Shown present(const Run& r, double hz, double from, double to) {
    Shown s;
    size_t k = 0;
    long last = -1;
    for (long n = (long)std::ceil(from * hz); (double)n / hz < to; n++) {
        const double v = (double)n / hz;
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
    dev.countFrom = 0.5;
    Run r = simulate(3.0, fiveMs, true, dev);
    double closest = 1.0;
    for (size_t i = 1; i < r.starts.size() && r.starts[i] < 0.5; i++) closest = std::min(closest, r.starts[i] - r.starts[i - 1]);
    CHECK(closest >= kPeriod / PacingClock::kRefillRate - 1e-9, "refilling runs at most %.0fx (closest starts %.2f ms)",
          PacingClock::kRefillRate, 1000.0 * closest);
    CHECK(dev.underruns == 0, "refilled within half a second: no underrun after it (%llu)", (unsigned long long)dev.underruns);
    CHECK(r.lockedAtEnd, "then it follows the audio clock");
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

}  // namespace

int main() {
    testTrueRateOn60Hz();
    testFollowsTheCrystal();
    testNoAudioClock();
    testDeviceStopsDraining();
    testRefillFromEmpty();
    testSpikeCatchesUp();
    testStallDropsOwedTime();
    testFastForward();
    std::printf(g_fail ? "pacing_clock_test: %d FAILED\n" : "pacing_clock_test: OK\n", g_fail);
    return g_fail ? 1 : 0;
}
