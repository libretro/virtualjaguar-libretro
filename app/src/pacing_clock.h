// Mode A pacing for the emulation thread (#967, design section 4): when is the next emulated frame due?
//
// The sound card is the master clock.  Frames are scheduled on a wall-clock grid at the core's period, and that period
// is trimmed by at most +/-0.5% from the SDL audio queue's level against its target (audio.target_ms), smoothed over
// half a second: more queued than the target -> frames come slightly slower, less -> slightly faster.  Over time the
// game runs at exactly the rate the audio device consumes samples (the true Jaguar rate on the sound card's crystal),
// and audio never drifts.  Not the raw "queue below target -> run now" rule: SDL's device pulls 512-frame chunks
// (10.7 ms), so frames triggered by those steps would start up to a chunk early or late, and a 60 Hz display would then
// repeat and skip frames all the time.
//
//   * no audio clock (no device, a paused or muted stream, fast-forward): the wall clock at the core's period;
//   * a device that is not draining (more than 3x the target queued): the wall clock until it drains again;
//   * nearly empty (under a quarter of the target): frames may start early, at most 2x the core's rate, until the
//     target is reached (a new stream, or after a stall);
//   * more than two frames late (a stall, a slow frame): the owed time is dropped, never run as a burst (#968);
//   * fast-forward: 4x the core's rate on the wall clock.
// Pure arithmetic on the times it is handed, so tests/pacing_clock_test.cpp drives it with a fake clock and device.
#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace vj {

struct PacingInput {
    double now = 0;               // seconds, any monotonic origin
    double period = 1.0 / 60.0;   // the core's frame period, seconds
    double audioQueuedMs = -1;    // queued audio; < 0 = no usable audio clock
    double targetMs = 64;         // audio.target_ms
    bool fastForward = false;
    // Where the UI takes finished frames (threaded + vsync, while the swaps show the vblank grid): one take time on
    // in.now's clock and the take period.  takePeriod 0 = no grid (vsync off, not tracked yet): no steering.
    double takeAt = 0;
    double takePeriod = 0;
};

class PacingClock {
public:
    static constexpr double kMaxTrim = 0.005;       // the period moves at most +/-0.5% to follow the audio clock
    static constexpr double kSmoothSec = 0.5;       // queue-level smoothing (dozens of device pulls)
    static constexpr double kMaxLateFrames = 2.0;   // owed time beyond this many frames is dropped
    static constexpr double kFastForward = 4.0;     // fast-forward runs this many times the core's rate
    static constexpr double kLowWater = 0.25;       // below this fraction of the target: refill...
    static constexpr double kRefillRate = 2.0;      // ...at up to this multiple of the core's rate, up to the target
    static constexpr double kHighWater = 3.0;       // above this multiple of the target the device is not draining
    static constexpr unsigned kEnds = 120;          // frame ends remembered (2 s) for the steering window
    static constexpr unsigned kMinEnds = 30;        // ... before the window is trusted
    static constexpr double kSteerMargin = 0.00075; // room kept on each side of the window (take-time jitter)
    static constexpr double kWindowDecay = 0.0002;  // the held window shrinks back at 0.2 ms per second

    void reset(double now) {
        next_ = now;
        lastStart_ = now - 1.0;
        haveQ_ = overfull_ = refill_ = audio_ = false;
        trim_ = 1.0;
        init_ = true;
        endAt_ = 0;
        curDue_ = -1;
        loHeld_ = 1e9;
        hiHeld_ = -1e9;
        lastSteer_ = now;
    }

    // When the next frame should start (absolute, on in.now's clock).  At or before in.now: start it now.
    // Deliberately non-const: it feeds the queue filter and accumulates dropped frames; safe to call repeatedly.
    double nextDue(const PacingInput& in) {
        if (!init_) reset(in.now);
        observe(in);
        const double base = basePeriod(in);
        if (in.now - next_ > kMaxLateFrames * base) {
            dropped_ += (uint64_t)((in.now - next_) / base);
            next_ = in.now;
        }
        if (refill_) return std::min(next_, std::max(in.now, lastStart_ + base / kRefillRate));
        return next_;
    }

    // The caller starts a frame now (in.now).
    void frameStarted(const PacingInput& in) {
        if (!init_) reset(in.now);
        const double base = basePeriod(in);
        if (in.now - next_ > kMaxLateFrames * base) next_ = in.now;
        lastStart_ = in.now;
        curDue_ = (refill_ || in.fastForward) ? -1 : std::min(next_, in.now);   // (early refill frames: no grid time)
        // While refilling, frames start early; rebase the grid on each one.  Advancing the old grid a full period per
        // early frame would leave it ahead of the clock by the extra audio just queued, and the loop would then sleep
        // that long, drain the queue below the low-water mark and re-arm the refill (bursts and stalls forever).
        next_ = (refill_ ? in.now : next_) + base * (audio_ ? trim_ : 1.0);
        if (!refill_ && !in.fastForward) steer(in, base);
    }

    // The frame started last has finished (its picture is handed over) now.  Feeds the steering window.
    void frameEnded(double now) {
        if (curDue_ < 0) return;
        ends_[endAt_ % kEnds] = now - curDue_;
        endAt_++;
        curDue_ = -1;
    }

    bool audioLocked() const { return audio_; }
    double trim() const { return audio_ ? trim_ : 1.0; }
    uint64_t droppedFrames() const { return dropped_; }
    double filteredQueueMs() const { return haveQ_ ? filtQ_ : -1.0; }   // (VJ_PERF_TRACE)
    bool refilling() const { return refill_; }
    uint64_t steers() const { return steers_; }   // phase steps taken to keep frame ends off the take point

private:
    // Keep frame ends off the UI's take point (#967 evenness).  A frame ends somewhere in [due + lo, due + hi] (its
    // wake-up lateness plus its cost, over the last kEnds frames).  The grid drifts against the take points at the
    // core-vs-display beat (15 us per frame for 60.054 Hz on 60 Hz), so while a take point sits inside that window,
    // whether a frame makes it is decided by its own cost: for spread / 15 us frames in a row the UI shows a repeat,
    // then a skip, at random -- dozens of pairs per crossing instead of the one skip the beat owes.  So when the next
    // frame's window would contain a take point, the grid steps once across it, in the direction the beat is already
    // moving it: the crossing happens at once, as the single skip (or repeat) it has to be, and the drift then carries
    // the window away from the take point for the next ~18 s.  Only the grid's phase moves, by at most the window's
    // width once per beat; its rate stays the trimmed audio rate, and the step's audio (a few ms early or late) is what
    // the queue absorbs and the trim corrects.  This is not a display lock: the drift is never held back.
    void steer(const PacingInput& in, double base) {
        const double P = in.takePeriod;
        if (P <= 0 || endAt_ < kMinEnds) return;
        double sorted[kEnds];
        const unsigned n = std::min(endAt_, kEnds);
        std::copy(ends_, ends_ + n, sorted);
        std::sort(sorted, sorted + n);
        // ~p2 and ~p98 (one outlier does not move them), held for a while: costs switch with the content (yarc runs
        // ~7.5 ms frames for a few seconds, then ~2.9 ms), and a window that forgot the slow frames after kEnds would
        // let the next switch land them on the take point.  The held edges come back at kWindowDecay.
        const double dt = std::max(0.0, in.now - lastSteer_);
        lastSteer_ = in.now;
        loHeld_ = std::min(sorted[n / 50], loHeld_ + kWindowDecay * dt);
        hiHeld_ = std::max(sorted[n - 1 - n / 50], hiHeld_ - kWindowDecay * dt);
        const double lo = loHeld_ - kSteerMargin;
        const double hi = hiHeld_ + kSteerMargin;
        if (hi - lo >= P - 2 * kSteerMargin) return;               // no gap between take points wide enough
        double x = std::fmod(in.takeAt - next_, P);                 // from the next frame's due to a take point
        if (x < lo) x += P * std::ceil((lo - x) / P);
        if (x > hi) return;                                         // (x is now the first candidate >= lo)
        double r = std::fmod(base, P);                              // how the grid moves against the take points per
        if (r > P / 2) r -= P;                                      // frame: x changes by -r (the untrimmed period:
        double step;                                                // the trim averages out to the audio clock)
        if (r <= 0) step = -(hi - x);                               // x rising: start earlier, so this frame makes it
        else step = x - lo;                                         // x falling: start later, so it waits a period
        if (next_ + step < in.now) return;                          // (never before the frame starting now)
        next_ += step;
        steers_++;
    }

    static double basePeriod(const PacingInput& in) { return in.fastForward ? in.period / kFastForward : in.period; }

    void observe(const PacingInput& in) {
        audio_ = false;
        if (in.fastForward || in.audioQueuedMs < 0 || in.targetMs <= 0) {
            haveQ_ = refill_ = overfull_ = false;
            trim_ = 1.0;
            return;
        }
        const double q = in.audioQueuedMs;
        if (!haveQ_) {
            filtQ_ = q;
            lastObs_ = in.now;
            haveQ_ = true;
        }
        filtQ_ += (q - filtQ_) * std::min(1.0, std::max(0.0, in.now - lastObs_) / kSmoothSec);
        lastObs_ = in.now;
        if (q > kHighWater * in.targetMs) overfull_ = true;
        else if (q < 2.0 * in.targetMs) overfull_ = false;
        if (overfull_) {
            refill_ = false;
            trim_ = 1.0;
            return;
        }
        if (q < kLowWater * in.targetMs) {
            refill_ = true;
        } else if (refill_ && q >= in.targetMs) {
            refill_ = false;
            filtQ_ = q;   // the filter lagged the refill; start the trim from where the queue really is
        }
        audio_ = true;
        const double e = std::max(-1.0, std::min(1.0, (filtQ_ - in.targetMs) / in.targetMs));
        trim_ = 1.0 + kMaxTrim * e;
    }

    double next_ = 0, lastStart_ = 0, filtQ_ = 0, lastObs_ = 0, trim_ = 1.0;
    bool init_ = false, haveQ_ = false, overfull_ = false, refill_ = false, audio_ = false;
    uint64_t dropped_ = 0;
    double ends_[kEnds] = {};   // frame end - due, seconds
    unsigned endAt_ = 0;
    double curDue_ = -1;        // the running frame's grid time (-1 = none: refill, fast-forward)
    uint64_t steers_ = 0;
    double loHeld_ = 1e9, hiHeld_ = -1e9, lastSteer_ = 0;   // the held window edges (end - due), seconds
};

}  // namespace vj
