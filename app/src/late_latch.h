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
    static constexpr unsigned kLockAfter = 30;          // consecutive tracked observations before the first wait
    static constexpr unsigned kUnlockAfter = 4;         // consecutive outliers (or non-blocking swaps x 8) to let go
    static constexpr uint64_t kPadMinNs = 1000000, kPadMaxNs = 4000000;
    static constexpr uint64_t kPadUpNs = 500000, kPadDownNs = 100000;
    static constexpr unsigned kCalmRun = 120;           // on-time swaps before the pad shrinks one step
    static constexpr uint64_t kMinRoomNs = 1000000;     // a budget within this of a whole period: no wait
    static constexpr unsigned kBuilds = 60;             // build times remembered (one second at 60 Hz)
    static constexpr unsigned kOutcomes = 60;           // waits whose outcome (made the vblank / missed) is remembered
    static constexpr unsigned kMinOutcomes = 8;         // waits observed before the miss rate can release the lock
    static constexpr unsigned kCooldownSwaps = 300;     // swaps after a release before the lock may be taken again

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
    void noteWaitEnd(uint64_t ns) { waitEndNs_ = ns; }

    // Right after SDL_GL_SwapWindow: when the call began and when it returned.  A swap that blocked returned at a
    // vblank; one that did not says nothing about where the vblanks are.
    void onSwap(uint64_t beginNs, uint64_t endNs) {
        if (!periodNs_) return;
        const int64_t P = (int64_t)periodNs_;
        if (cooldown_) cooldown_--;
        const bool lateStart = waitEndNs_ && targetNs_ && waitEndNs_ > wakeNs_ + kOverrunNs;
        waitEndNs_ = 0;
        if (targetNs_ && lateStart) {   // the UI was late before the build began: not a budget miss, not an outcome
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
    uint64_t padNs() const { return padNs_; }
    unsigned misses() const { return misses_; }
    unsigned released() const { return releases_; }   // times the miss rate let the lock go
    uint64_t budgetNs() const {   // the slowest recent build, plus the pad
        uint64_t mx = 0;
        for (unsigned i = 0; i < std::min(buildAt_, kBuilds); i++) mx = std::max(mx, builds_[i]);
        return mx + padNs_;
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
    uint64_t wakeNs_ = 0, waitEndNs_ = 0;   // the wake time wakeAt() returned; when the wait really ended (0 = not told)
    uint64_t targetNs_ = 0;   // the vblank the current wait aims at (0 = no wait this iteration)
    uint64_t builds_[kBuilds] = {};
    unsigned buildAt_ = 0;
};

}  // namespace vj
