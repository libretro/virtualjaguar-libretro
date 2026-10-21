// The late latch (#967): how long the UI thread may wait after a swap so the frame it builds next still makes the
// next vblank.  Threaded mode with vsync only.  App::latchWait sleeps until wakeAt(), handing every input event to the
// emulation thread the moment it arrives; then the iteration takes the newest finished frame, builds the UI and swaps.
// That moves the frame take from just after a vblank to just before the next one, which brings event -> present back
// to the single-thread loop's ~25 ms at 60 Hz for a light core frame (plan: "Decided: input latency in mode A").
//
// Pure arithmetic on the times it is handed (SDL_GetTicksNS), so tests/late_latch_test.cpp drives it with a fake clock.
// It can only shorten the wait: whenever it is unsure where the vblanks are, wakeAt() returns `now`, which is the
// unlatched loop of Task 13.
#pragma once

#include <algorithm>
#include <cstdint>

namespace vj {

class LateLatch {
public:
    static constexpr uint64_t kBlockedNs = 250000;      // a swap that took this long waited for a vblank
    static constexpr uint64_t kTrackNs = 1500000;       // a vblank observation within this of the grid is tracked
    static constexpr uint64_t kJitterMaxNs = 1000000;   // locked only while the smoothed |residual| stays under this
    static constexpr uint64_t kUnlockJitterNs = 2000000;   // ... and let go only when it reaches this (hysteresis)
    static constexpr uint64_t kOverrunNs = 500000;      // a wait that ended this much after its wake time started the UI late
    static constexpr unsigned kLockAfter = 30;          // tracked vblank observations (single outliers do not reset the count) before the first wait
    static constexpr unsigned kUnlockAfter = 4;         // consecutive outliers (or non-blocking swaps x 8) to let go
    static constexpr uint64_t kPadMinNs = 1000000, kPadMaxNs = 4000000;
    static constexpr uint64_t kPadUpNs = 500000, kPadDownNs = 100000;

    // Stop the whole-ms event wait 3 ms before the wake time: SDL's Cocoa run-loop wait commonly overshoots
    // by 0.5-2 ms (#1026). Zero hands the last <4 ms (including rounding) to SDL_DelayPrecise.
    static uint64_t eventWaitMs(uint64_t leftNs) {
        return leftNs < 4000000 ? 0 : (leftNs - 3000000) / 1000000;
    }

    // The threaded loop's nap when vsync is off or the swap did not block: until the display's next refresh, at least
    // 1 ms; a flat 4 ms when the refresh rate is unknown (0).
    static uint64_t idleNapNs(uint64_t sinceSwapNs, uint64_t displayPeriodNs) {
        if (!displayPeriodNs) return 4000000ull;
        const uint64_t left = sinceSwapNs < displayPeriodNs ? displayPeriodNs - sinceSwapNs : 0;
        return left < 1000000ull ? 1000000ull : left;
    }
    // The same nap on a deadline grid (vsync off, or a swap that did not block).  Anchored at the swap end, the loop
    // period was refresh + build + swap (~54-57 Hz on a 60 Hz display against a 60.05 Hz core: 3-7 dropped frames a
    // second).  Here the next iteration is due one display period after the previous one was, whatever the build cost.
    //   advanceGridNs: the next due time, right after a swap at `nowNs`.  First call (gridNs 0): one period from now.
    //     More than a period behind (a stall): re-anchored at now, no burst of catch-up frames.  More than a period ahead
    //     (iterations cut short by input): pulled back to one period from now.  0 when the refresh rate is unknown.
    //   gridNapNs: how long to nap from `nowNs` to the grid point; at least 1 ms; the old flat 4 ms when unknown.
    static uint64_t advanceGridNs(uint64_t gridNs, uint64_t nowNs, uint64_t periodNs) {
        if (!periodNs) return 0;
        uint64_t g = gridNs ? gridNs + periodNs : nowNs + periodNs;
        if (nowNs > g + periodNs) g = nowNs;
        if (g > nowNs + periodNs) g = nowNs + periodNs;
        return g;
    }
    static uint64_t gridNapNs(uint64_t nowNs, uint64_t gridNs, uint64_t periodNs) {
        if (!periodNs) return idleNapNs(0, 0);
        const uint64_t left = gridNs > nowNs ? gridNs - nowNs : 0;
        return left < 1000000ull ? 1000000ull : left;
    }
    static constexpr unsigned kCalmRun = 600;           // ten calm seconds at 60 Hz before probing a tighter pad (#1026)
    static constexpr uint64_t kMinRoomNs = 1000000;     // a budget within this of a whole period: no wait
    static constexpr unsigned kBuilds = 60;             // build times remembered (one second at 60 Hz)
    static constexpr unsigned kOutcomes = 60;           // waits whose outcome (made the vblank / missed) is remembered
    static constexpr unsigned kMinOutcomes = 8;         // waits observed before the miss rate can release the lock
    static constexpr unsigned kCooldownSwaps = 300;     // swaps after a release before the lock may be taken again
    static constexpr unsigned kMaxLateExempt = 8;       // consecutive late-start waits excused; after that they count

    // The display's refresh period (SDL_DisplayMode), 0 = unknown: the latch stays off.
    void setPeriodNs(uint64_t ns) {
        if (ns == periodNs_) return;
        periodNs_ = ns;
        havePhase_ = locked_ = false;
        tracked_ = outliers_ = nonBlocking_ = 0;
        jitterNs_ = 0;
        targetNs_ = 0;
        outcomeAt_ = recentMisses_ = 0;
        cooldown_ = 0;
    }
    uint64_t periodNs() const { return periodNs_; }

    // The UI's own work in one iteration: from the end of the wait to the start of the swap.
    void noteBuild(uint64_t ns) {
        builds_[buildAt_ % kBuilds] = ns;
        buildAt_++;
    }

    // When the wait that wakeAt() sized really ended.  A wait that overran its wake time (a call() or event handling
    // inside it, a descheduled thread) started the UI late: its swap missing the vblank says nothing about the budget.
    // Only kMaxLateExempt in a row are excused, though: a wait that overruns every time (event handling that always
    // takes longer than the wake leaves room for) is the latch's own cost, and must reach the miss rate that releases it.
    void noteWaitEnd(uint64_t ns) { waitEndNs_ = ns; }

    // Right after SDL_GL_SwapWindow: when the call began and when it returned.  A swap that blocked returned at a
    // vblank; one that did not says nothing about where the vblanks are.
    void onSwap(uint64_t beginNs, uint64_t endNs) {
        if (!periodNs_) return;
        const int64_t P = (int64_t)periodNs_;
        if (cooldown_) cooldown_--;
        const bool lateStart = waitEndNs_ && targetNs_ && waitEndNs_ > wakeNs_ + kOverrunNs;
        waitEndNs_ = 0;
        if (targetNs_) lateRun_ = lateStart ? lateRun_ + 1 : 0;
        if (targetNs_ && lateStart && lateRun_ <= kMaxLateExempt) {   // the UI was late before the build began: not a
                                                                      // budget miss, not an outcome
            targetNs_ = 0;
        } else if (targetNs_) {   // this iteration waited: did its swap make the vblank it aimed at?
            const bool miss = (int64_t)(endNs - targetNs_) > P / 2;
            if (miss) {
                misses_++;
                padNs_ = std::min(kPadMaxNs, padNs_ + kPadUpNs);
                calm_ = 0;
            } else if (++calm_ >= kCalmRun) {
                padNs_ = std::max(kPadMinNs, padNs_ - kPadDownNs);
                calm_ = 0;
            }
            targetNs_ = 0;
            // Persistent misses (a GPU that finishes past the vblank, which the CPU-side budget and the capped pad
            // cannot see): more than a quarter of the last waits missing means the latch is worse than no latch.
            const unsigned slot = outcomeAt_ % kOutcomes;
            if (outcomeAt_ >= kOutcomes) recentMisses_ -= outcomes_[slot];
            outcomes_[slot] = miss ? 1 : 0;
            recentMisses_ += outcomes_[slot];
            outcomeAt_++;
            const unsigned seen = std::min(outcomeAt_, kOutcomes);
            if (locked_ && seen >= kMinOutcomes && recentMisses_ * 4 > seen) {
                unlock();
                releases_++;
                cooldown_ = kCooldownSwaps;
                outcomeAt_ = recentMisses_ = 0;
            }
        }
        if (endNs - beginNs < kBlockedNs) {   // vsync off, or a driver that queues the swap: nothing learned
            if (++nonBlocking_ >= kUnlockAfter * 8) unlock();
            return;
        }
        nonBlocking_ = 0;
        if (!havePhase_) {
            phaseNs_ = endNs;
            havePhase_ = true;
            return;
        }
        int64_t r = (int64_t)(endNs - phaseNs_) % P;   // signed distance to the nearest grid point
        if (r < 0) r += P;
        if (r > P / 2) r -= P;
        const uint64_t ar = (uint64_t)(r < 0 ? -r : r);
        jitterNs_ = (jitterNs_ * 7 + ar) / 8;
        if (ar < kTrackNs) {
            phaseNs_ = endNs - (uint64_t)(r - r / 4);   // move a quarter of the way toward the observation
            outliers_ = 0;
            if (++tracked_ >= kLockAfter && jitterNs_ < kJitterMaxNs && !cooldown_) locked_ = true;
        } else if (++outliers_ >= kUnlockAfter) {   // the grid moved (a new display mode, a compositor hiccup)
            unlock();
            phaseNs_ = endNs;
        }
        if (locked_ && jitterNs_ >= kUnlockJitterNs) locked_ = false;
    }

    // When to stop waiting, given the time now (after the swap and the end-of-iteration work).  <= now: no wait.
    uint64_t wakeAt(uint64_t nowNs) {
        targetNs_ = 0;
        waitEndNs_ = 0;
        if (!locked_ || !periodNs_) return nowNs;
        const uint64_t b = budgetNs();
        if (b + kMinRoomNs >= periodNs_) return nowNs;   // the UI needs nearly a whole period: nothing to gain
        const int64_t d = (int64_t)(nowNs + b - phaseNs_);
        const uint64_t v = d <= 0 ? phaseNs_ : phaseNs_ + ((uint64_t)d + periodNs_ - 1) / periodNs_ * periodNs_;
        targetNs_ = v;
        wakeNs_ = v - b;
        return wakeNs_;
    }

    bool locked() const { return locked_; }
    uint64_t targetNs() const { return targetNs_; }   // the vblank the wait wakeAt() just sized aims at, 0 = none
    uint64_t padNs() const { return padNs_; }
    unsigned misses() const { return misses_; }
    unsigned released() const { return releases_; }   // times the miss rate let the lock go
    // The second-slowest of the last kBuilds builds, plus the pad.  One long build (a window opening, a shader compile)
    // misses its own vblank whatever the budget says; letting it set the budget would also give up the wait (budget near a
    // whole period) or move the take point several ms earlier for the next second.  Two long builds in a second do count.
    uint64_t budgetNs() const {
        uint64_t mx = 0, mx2 = 0;
        for (unsigned i = 0; i < std::min(buildAt_, kBuilds); i++) {
            if (builds_[i] > mx) {
                mx2 = mx;
                mx = builds_[i];
            } else if (builds_[i] > mx2) {
                mx2 = builds_[i];
            }
        }
        return (buildAt_ > 1 ? mx2 : mx) + padNs_;
    }

private:
    void unlock() {
        locked_ = false;
        tracked_ = outliers_ = nonBlocking_ = 0;
        outcomeAt_ = recentMisses_ = 0;
    }

    uint64_t periodNs_ = 0;
    uint64_t phaseNs_ = 0;   // a vblank time
    bool havePhase_ = false;
    bool locked_ = false;
    unsigned tracked_ = 0, outliers_ = 0, nonBlocking_ = 0;
    uint64_t jitterNs_ = 0;
    uint64_t padNs_ = kPadMinNs;
    unsigned calm_ = 0;
    unsigned misses_ = 0;
    unsigned releases_ = 0, cooldown_ = 0;
    uint8_t outcomes_[kOutcomes] = {};   // 1 = that wait's swap missed its vblank
    unsigned outcomeAt_ = 0, recentMisses_ = 0;
    unsigned lateRun_ = 0;   // consecutive waits that ended late (noteWaitEnd)
    uint64_t wakeNs_ = 0, waitEndNs_ = 0;   // the wake time wakeAt() returned; when the wait really ended (0 = not told)
    uint64_t targetNs_ = 0;   // the vblank the current wait aims at (0 = no wait this iteration)
    uint64_t builds_[kBuilds] = {};
    unsigned buildAt_ = 0;
};

}  // namespace vj
