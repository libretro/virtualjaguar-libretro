// Frame pacing, kept free of SDL / GL / the core so tests/pacing_test.cpp can drive it with a fake clock.
//
// The UI loop runs once per presented frame; the emulation is owed one retro_run() per core period of real time.
// Pacer turns "real time since the last step" into "how many frames to run now", and answers the two questions
// the loop asks afterwards:
//
//   * Is the core behind?  (A frame is already due again after the batch just run.)  A core that cannot hold full
//     speed -- RISC overclock, a heavy title, a loaded machine -- must neither SLEEP (vsync off) nor wait for a
//     VBLANK (vsync on) between batches: that time is exactly what it does not have.  Both used to happen, and a
//     core that ran 55 fps headless showed 45 in the window (RISC 300% on an M2: 7 ms of every 66 ms slept).
//   * Should the swap interval be 1?  Vsync is a pacing tool for a core with time to spare; while the core is behind
//     it only adds a stall on the thread that runs the emulation, so it is released and restored with hysteresis.
#pragma once

namespace vj {

struct Pacer {
    static constexpr int kMaxFrames = 3;          // per UI frame: keeps the UI responsive while catching up
    static constexpr int kReleaseAfter = 3;       // consecutive behind steps before the vsync is released
    static constexpr int kRestoreAfter = 30;      // consecutive keeping-up steps before it comes back (~0.5 s)

    double accum = 0;        // emulation time owed, seconds
    bool behind = false;     // a frame is already due again after the last charge()

    void reset() {
        accum = 0;
        behind = false;
    }

    // dt: real seconds since the previous step.  Returns the frames to run now (0..kMaxFrames).
    int plan(double dt, double period) {
        accum += dt;
        if (accum > 0.25) accum = period;   // stalled (window drag, debugger): resync instead of a burst
        int frames = 0;
        while (accum >= period && frames < kMaxFrames) {
            frames++;
            accum -= period;
        }
        return frames;
    }

    // The frames just run took `work` real seconds; the next plan() must not count that time a second time, so the
    // caller advances its clock by the same amount.  Decides `behind` from the true backlog.
    void charge(double work, double period) {
        accum += work;
        behind = accum >= period;
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

    // ---- swap-interval hysteresis ---------------------------------------------------------------------
    bool swapSync = true;    // the GL swap interval is 1 right now
    int behindRun = 0, aheadRun = 0;

    void resetSwap(bool vsyncOn) {
        swapSync = vsyncOn;
        behindRun = aheadRun = 0;
    }
    // Call once per UI frame with the user's vsync setting.  Returns true when the swap interval must change
    // (read swapSync for the new value).
    bool updateSwap(bool vsyncOn) {
        if (!vsyncOn) {
            bool changed = swapSync;
            swapSync = false;
            return changed;
        }
        if (behind) {
            aheadRun = 0;
            if (swapSync && ++behindRun >= kReleaseAfter) {
                swapSync = false;
                return true;
            }
        } else {
            behindRun = 0;
            if (!swapSync && ++aheadRun >= kRestoreAfter) {
                swapSync = true;
                return true;
            }
        }
        return false;
    }
};

}  // namespace vj
