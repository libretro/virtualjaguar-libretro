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
#include <cstdint>

namespace vj {

struct PacingInput {
    double now = 0;               // seconds, any monotonic origin
    double period = 1.0 / 60.0;   // the core's frame period, seconds
    double audioQueuedMs = -1;    // queued audio; < 0 = no usable audio clock
    double targetMs = 64;         // audio.target_ms
    bool fastForward = false;
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

    void reset(double now) {
        next_ = now;
        lastStart_ = now - 1.0;
        haveQ_ = overfull_ = refill_ = audio_ = false;
        trim_ = 1.0;
        init_ = true;
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
        // While refilling, frames start early; rebase the grid on each one.  Advancing the old grid a full period per
        // early frame would leave it ahead of the clock by the extra audio just queued, and the loop would then sleep
        // that long, drain the queue below the low-water mark and re-arm the refill (bursts and stalls forever).
        next_ = (refill_ ? in.now : next_) + base * (audio_ ? trim_ : 1.0);
    }

    bool audioLocked() const { return audio_; }
    double trim() const { return audio_ ? trim_ : 1.0; }
    uint64_t droppedFrames() const { return dropped_; }

private:
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
};

}  // namespace vj
