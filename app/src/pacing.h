// Frame pacing, kept free of SDL / GL / the core so tests/pacing_test.cpp can drive it with a fake clock.
//
// The UI loop runs once per presented frame; the emulation is owed one retro_run() per core period of real time.
// Pacer turns "real time since the last step" into "how many frames to run now", and answers the two questions
// the loop asks afterwards:
//
//   * Is a frame due again right away?  (`behind`, after the batch just run.)  Then the loop must not SLEEP (vsync
//     off) between batches: that time is exactly what a core that cannot hold full speed does not have.  This is a
//     fact about the next few milliseconds only.  It is NOT evidence of a deficit: with the loop locked to a display
//     whose refresh does not match the core's period, a frame runs late in its period and ends with about one
//     period banked, forever, while the core has plenty of time to spare (#952: Doom R1x, 3.7 ms of work).
//   * Should the swap interval be 1?  Vsync paces a core with time to spare; against a core that cannot hold full
//     speed a blocking swap only quantises its rate to whole refreshes (20 ms of work = 30 fps at 60 Hz) on the
//     thread that runs the emulation.  The release is decided from a SUSTAINED deficit only: the backlog having to
//     be trimmed (more than two periods owed, or the per-step frame cap hit) in a large share of recent steps, which
//     a vblank-locked core with time to spare never does.  Vsync comes back when the cost of a frame is far below
//     the period again (a Schmitt trigger, not a mirror image of the release, and never within kMinDwell of the
//     release), so a core hovering at the limit does not flap.  The user's vsync-off always wins.
#pragma once

#include <cmath>

namespace vj {

struct Pacer {
    static constexpr int kMaxFrames = 3;          // per UI frame: keeps the UI responsive while catching up
    static constexpr double kEmaAlpha = 1.0 / 16; // smoothing of the two signals below, per step that ran frames
    static constexpr double kReleasePressure = 0.15;   // share of recent steps that trimmed the backlog -> release
    static constexpr double kBatchPressure = 0.03;     // above this, batching is what keeps the core at speed
    static constexpr double kRestorePressure = 0.05;   // ...and below this, with a cheap core, vsync may return
    static constexpr double kRestoreCost = 0.8;   // work per frame as a share of the period: room left for UI + jitter
    static constexpr double kMinDwell = 2.0;      // seconds a released vsync stays released at least

    double accum = 0;        // emulation time owed, seconds
    bool behind = false;     // a frame is already due again after the last charge() (do not sleep)
    bool swapSync = true;    // the GL swap interval is 1 right now

    void reset() {
        accum = 0;
        behind = false;
        ran_ = 0;
        capped_ = false;
        dropped_ = false;
    }

    // dt: real seconds since the previous step.  Returns the frames to run now (0..kMaxFrames).
    int plan(double dt, double period) {
        clock_ += dt;
        accum += dt;
        if (accum > 0.25) accum = period;   // stalled (window drag, debugger): resync instead of a burst
        // A batch (more than one frame in a step) is a catch-up: it must not become a habit while the core has time
        // to spare.  Two frames cost more than one refresh of a vblank-locked loop, so after a catch-up the next
        // iteration lasts two refreshes, owes two periods again, and the loop stays in 2-frames-per-2-refreshes
        // forever -- half the images, with nothing wrong with the core.  (Only a blocking swap does this; with the swap
        // interval at 0 the loop is not snapped to the display and catches up freely.)  So the step after a batch runs one frame
        // and drops the rest, unless the pressure says the core really needs the batches.
        int cap = kMaxFrames;
        if (swapSync && ran_ > 1 && pressure_ < kBatchPressure) cap = 1;
        int frames = 0;
        while (accum >= period && frames < cap) {
            frames++;
            accum -= period;
        }
        dropped_ = false;
        if (cap < kMaxFrames && accum >= period) {
            accum = std::fmod(accum, period);
            dropped_ = true;
        }
        ran_ = frames;
        capped_ = frames == kMaxFrames && accum >= period;   // more frames due than one UI frame may run
        return frames;
    }

    // The frames just run took `work` real seconds; the next plan() must not count that time a second time, so the
    // caller advances its clock by the same amount.
    void charge(double work, double period) {
        clock_ += work;
        accum += work;
        behind = accum >= period;
        // A deficit is time that has to be thrown away: the backlog beyond two periods (or frames beyond the cap).
        // One banked period (a late phase in a vblank-locked loop) is not.
        const bool trimmed = accum > period * 2 || capped_ || dropped_;
        pressure_ += kEmaAlpha * ((trimmed ? 1.0 : 0.0) - pressure_);
        if (ran_ > 0) cost_ += kEmaAlpha * (work / ((double)ran_ * period) - cost_);
        if (accum > period * 2) accum = period * 2;   // never bank a burst: the UI frame in between stays responsive
    }

    // Vsync off: how long the loop may sleep before the next frame is due.  `since` = real seconds since charge().
    // Zero when behind (a frame is already due) or when the remainder is too small to bother.
    double sleepFor(double since, double period) const {
        if (behind) return 0;
        double w = period - accum - since - 0.001;
        if (w <= 0.0005) return 0;
        return w < 0.016 ? w : 0.016;
    }

    // ---- swap interval --------------------------------------------------------------------------------
    void resetSwap(bool vsyncOn) {
        swapSync = vsyncOn;
        pressure_ = 0;
        cost_ = 0;
        changedAt_ = clock_;
    }
    // Call once per UI frame with the user's vsync setting.  Returns true when the swap interval must change
    // (read swapSync for the new value).
    bool updateSwap(bool vsyncOn) {
        if (!vsyncOn) {
            bool changed = swapSync;
            swapSync = false;
            return changed;
        }
        if (swapSync && pressure_ > kReleasePressure) {
            swapSync = false;
        } else if (!swapSync && clock_ - changedAt_ >= kMinDwell && pressure_ < kRestorePressure &&
                   cost_ < kRestoreCost) {
            swapSync = true;
        } else {
            return false;
        }
        changedAt_ = clock_;
        return true;
    }

private:
    double clock_ = 0;       // real seconds seen so far (plan dt + charged work)
    double changedAt_ = 0;
    double pressure_ = 0;    // EMA: share of steps that trimmed the backlog
    double cost_ = 0;        // EMA: work per emulated frame, in periods
    int ran_ = 0;
    bool capped_ = false;
    bool dropped_ = false;
};

// Inline step times on the display's vblank grid (#1141).  With the swap interval at 1 the inline loop is locked to the
// display, but plan() is handed the real time between steps, and a step starts when its swap returned plus whatever the
// loop did since (events, input, the cheat pump): 0.3-1 ms of jitter on a quiet host, several ms on a loaded one.  The
// core's period is a hair shorter than a 60 Hz refresh (60.05 Hz), so the backlog's phase creeps across a period
// boundary about every 18 s, and while it sits near one, that jitter decides step by step whether 0, 1 or 2 frames run:
// bursts of repeated and dropped images (16-60 a minute on Cybermorph, more when a cheaper core frame leaves a longer
// and less even swap block).  SwapGrid books each step at the vblank its swap returned at instead.  It tracks the grid
// from swaps that blocked (as LateLatch does for the threaded loop), and correct() turns the step's real time into that
// grid time, so the backlog gains exactly the time between those vblanks and crosses a boundary once: one two-frame step
// per crossing, the only slip a 60.05 Hz core has to make on a 60 Hz display.  The tracked phase follows the real
// vblanks (a 59.94 Hz panel announced as 60 Hz included), so the sum stays real time and the game speed is unchanged.
// Not locked (vsync off or released, swaps that do not block, a grid that does not hold: VRR, a compositor hiccup):
// correct() is 0 and the pacer sees real time, as before.  Seconds on any one monotonic clock.
class SwapGrid {
public:
    static constexpr double kBlocked = 0.00025;      // a swap that took this long waited for a vblank (LateLatch::kBlockedNs)
    static constexpr double kTrack = 0.0015;         // an observation within this of the grid is tracked
    static constexpr double kJitterMax = 0.001;      // locked only while the smoothed |residual| stays under this
    static constexpr double kUnlockJitter = 0.002;   // ... and let go only when it reaches this (hysteresis)
    static constexpr int kLockAfter = 30;            // tracked observations before the grid is trusted
    static constexpr int kUnlockAfter = 4;           // consecutive outliers (or non-blocking swaps x 8) to let go
    static constexpr double kGain = 1.0 / 32;        // locked: the phase moves this share toward each observation

    void reset() {
        refresh_ = 0;
        havePhase_ = false;
        unlock();
    }
    // A step that is not paced (paused, halted, fast-forward, a new game): the next correct() starts a new chain.
    void restart() {
        haveAt_ = haveLag_ = false;
    }

    // Right after the swap: when it began and returned, and the display's refresh period (0 = unknown: off).
    void onSwap(double begin, double end, double refresh) {
        if (refresh != refresh_) {
            reset();
            refresh_ = refresh;
        }
        haveAt_ = false;
        if (refresh <= 0) return;
        if (end - begin < kBlocked) {   // vsync off, or a driver that queued the swap: nothing learned
            if (++nonBlocking_ >= kUnlockAfter * 8) unlock();
            return;
        }
        nonBlocking_ = 0;
        if (!havePhase_) {
            phase_ = end;
            havePhase_ = true;
            return;
        }
        const double pred = phase_ + std::floor((end - phase_) / refresh + 0.5) * refresh;   // the nearest grid point
        const double r = end - pred;
        jitter_ += (std::fabs(r) - jitter_) / 8;
        if (std::fabs(r) < kTrack) {
            phase_ = pred + r * (locked_ ? kGain : 0.25);
            outliers_ = 0;
            if (++tracked_ >= kLockAfter && jitter_ < kJitterMax) locked_ = true;
        } else if (++outliers_ >= kUnlockAfter) {   // the grid moved (a new mode, a compositor hiccup)
            unlock();
            phase_ = end;
            return;
        } else {
            phase_ = pred;   // one late swap: booked at its vblank, the grid kept
        }
        if (locked_ && jitter_ >= kUnlockJitter) unlock();
        if (locked_) {
            at_ = phase_;
            haveAt_ = true;
        }
    }

    // At the start of a step (`stepAt`, the clock onSwap() was given): the seconds to add to plan()'s dt, so that from
    // one step to the next the backlog gains the time between their swaps' vblanks.  0 while not locked, and on the first
    // step of a chain.
    double correct(double stepAt) {
        if (!haveAt_) {
            haveLag_ = false;
            return 0;
        }
        haveAt_ = false;
        const double lag = stepAt - at_;
        const double c = haveLag_ ? lag_ - lag : 0;
        lag_ = lag;
        haveLag_ = true;
        return c;
    }

    bool locked() const { return locked_; }

private:
    void unlock() {
        locked_ = false;
        tracked_ = outliers_ = nonBlocking_ = 0;
        jitter_ = 0;
        restart();
    }

    double refresh_ = 0;
    double phase_ = 0;     // a vblank on the tracked grid
    double jitter_ = 0;    // smoothed |residual| of the observations
    double at_ = 0;        // the vblank the last swap returned at (haveAt_)
    double lag_ = 0;       // the last step's start minus its swap's vblank (haveLag_)
    int tracked_ = 0, outliers_ = 0, nonBlocking_ = 0;
    bool havePhase_ = false, locked_ = false, haveAt_ = false, haveLag_ = false;
};

}  // namespace vj
