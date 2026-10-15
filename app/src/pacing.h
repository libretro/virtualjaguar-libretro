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
    static constexpr double kReleasePressure = 0.25;   // share of recent steps that trimmed the backlog -> release
    static constexpr double kBatchPressure = 0.03;     // above this, batching is what keeps the core at speed
    static constexpr double kRestorePressure = 0.05;   // ...and below this, with a cheap core, vsync may return
    static constexpr double kRestoreCost = 0.6;   // work per frame as a share of the period: room for UI + jitter
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

}  // namespace vj
