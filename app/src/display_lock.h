// Mode B: display lock (#1038, docs/app-emulation-thread-design.md "Mode B", plan-phase4).  Should the emulation thread
// follow the display's vblanks instead of the audio clock, and at how many vblanks per frame?
//
// Mode B runs one emulated frame every k vblanks, k = round(display rate / core rate), so it only locks where the
// display runs at a whole multiple of the core's rate: within kTolerance (0.3%, inside the audio controller's +/-0.5%
// room).  NTSC (60.05 Hz) locks on 60 / 59.94 / 120 / 119.88 Hz, PAL (50.08 Hz) on 50 / 100 Hz; 144 Hz and 75 Hz fall
// back for both.  The display rate is the latch's measured vblank period, never SDL's nominal refresh rate.
//
// Everything else is a reason to stay in mode A, each reported on its own so the UI can say which:
//   * the setting is Jaguar rate (or the emulation thread is off);
//   * no vsync (vsync off, or SwapGuard forcing interval 0 while the display sleeps, #1081);
//   * the latch has not locked onto the swap grid (VRR, a compositor dropping frames, not tracked yet);
//   * the measured rate is not a whole multiple of the core's;
//   * fast-forward, or link play (both peers must run the same frame clock).
// Hysteresis: the display conditions (vsync, latch, ratio) must hold for kEnterSwaps swaps in a row before mode B
// starts, and mode B ends on the first swap they fail.  The latch has its own hysteresis (one late swap does not
// unlock it), so "first swap" means the first swap the display stops being a clock to follow.  Fast-forward and link
// play switch at once both ways; the display streak keeps counting under them.
// Pure arithmetic, called once per swap on the UI thread; tests/display_lock_test.cpp drives it directly.
#pragma once

#include <cmath>

namespace vj {

enum class PacingMode { A, B };

enum class LockReason {
    Locked,        // mode B
    Setting,       // Jaguar rate chosen, or the emulation thread is off
    NoVsync,
    Unlocked,      // the latch is not locked onto the swap grid
    Ratio,         // the display is not a whole multiple of the core's rate
    Settling,      // the display conditions hold, but not for kEnterSwaps swaps yet
    FastForward,
    Link,
};

struct DisplayLockInput {
    bool matchDisplay = false;   // Video > Frame pacing = Match display, with the emulation thread on
    bool vsync = false;          // vsync wanted and the swap interval really is 1
    bool latchLocked = false;    // LateLatch::locked()
    double displayPeriod = 0;    // measured vblank period, seconds (0 = not measured)
    double corePeriod = 0;       // the core's frame period, seconds
    bool fastForward = false;
    bool linked = false;         // Netplay::link() != Off
};

struct DisplayLockVerdict {
    PacingMode mode = PacingMode::A;
    LockReason reason = LockReason::Setting;
    unsigned k = 0;          // vblanks per emulated frame (mode B); 0 when the ratio test did not pass
    double baseRatio = 1.0;  // audio stream frequency ratio, (display / k) / core (mode B), else 1
};

class DisplayLock {
public:
    static constexpr double kTolerance = 0.003;   // |display/k - core| / core below this locks
    static constexpr unsigned kEnterSwaps = 120;  // display conditions held this many swaps in a row (~2 s)
    static constexpr unsigned kMaxK = 8;          // up to 480 Hz for a 60 Hz core

    // The ratio test alone: k and the base ratio, or false.
    static bool ratioLocks(double displayPeriod, double corePeriod, unsigned& k, double& baseRatio) {
        k = 0;
        baseRatio = 1.0;
        if (!(displayPeriod > 0) || !(corePeriod > 0)) return false;
        const double display = 1.0 / displayPeriod, core = 1.0 / corePeriod;
        const double n = std::round(display / core);
        if (n < 1 || n > kMaxK) return false;
        const double perFrame = display / n;
        if (std::fabs(perFrame - core) / core >= kTolerance) return false;
        k = (unsigned)n;
        baseRatio = perFrame / core;
        return true;
    }

    // Once per swap.
    DisplayLockVerdict evaluate(const DisplayLockInput& in) {
        DisplayLockVerdict v;
        unsigned k = 0;
        double base = 1.0;
        LockReason display = LockReason::Locked;
        if (!in.vsync) display = LockReason::NoVsync;
        else if (!in.latchLocked) display = LockReason::Unlocked;
        else if (!ratioLocks(in.displayPeriod, in.corePeriod, k, base)) display = LockReason::Ratio;
        if (display == LockReason::Locked) {
            if (k != k_) streak_ = 0;   // a different multiple (60 -> 120 Hz) is a different clock: settle again
            k_ = k;
            if (streak_ < kEnterSwaps) streak_++;
        } else {
            streak_ = 0;
            k_ = 0;
        }
        v.k = k;
        if (!in.matchDisplay) v.reason = LockReason::Setting;
        else if (display != LockReason::Locked) v.reason = display;
        else if (in.fastForward) v.reason = LockReason::FastForward;
        else if (in.linked) v.reason = LockReason::Link;
        else if (streak_ < kEnterSwaps) v.reason = LockReason::Settling;
        else {
            v.reason = LockReason::Locked;
            v.mode = PacingMode::B;
            v.baseRatio = base;
        }
        return v;
    }

    // The display changed (moved to another one, its mode changed, fullscreen): start the streak over.
    void reset() {
        streak_ = 0;
        k_ = 0;
    }

    static const char* name(LockReason r) {   // perf-trace column and log token
        switch (r) {
            case LockReason::Locked: return "locked";
            case LockReason::Setting: return "setting";
            case LockReason::NoVsync: return "no_vsync";
            case LockReason::Unlocked: return "unlocked";
            case LockReason::Ratio: return "ratio";
            case LockReason::Settling: return "settling";
            case LockReason::FastForward: return "fast_forward";
            case LockReason::Link: return "link";
        }
        return "?";
    }

private:
    unsigned streak_ = 0;   // swaps in a row the display conditions held, capped at kEnterSwaps
    unsigned k_ = 0;        // the multiple they held at
};

}  // namespace vj
