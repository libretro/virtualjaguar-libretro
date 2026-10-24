// Whether the swap interval may be 1 (#1081).  Pure logic: no SDL, no OS call; the display state comes in as a bool.
//
// On macOS SDL emulates vsync itself: SDL_GL_SwapWindow waits on a condition variable that a CVDisplayLink
// callback signals once per refresh (SDL_cocoaopengl.m, Cocoa_GL_SwapWindow).  The link is made from the active
// displays when the GL context is created.  With the display asleep at that moment there are none, the link never
// ticks -- not even once the display wakes -- and the first swap blocks forever, the emulation with it when it runs
// inline.  (A display that falls asleep later does not block: the link survives.)  So:
//   * display asleep when the context was made: interval 0 for the rest of the run (the link is dead);
//   * display asleep later:                     interval 0 for as long as it sleeps, back to the setting on wake
//                                               (defensive: develop did not block here);
//   * otherwise                                 the user's setting.
// Interval 0 is the existing vsync-off path (the loop sleeps to its own clock), so the emulation keeps its pace while
// nobody can see it.
#pragma once

namespace vj {

class SwapGuard {
public:
    // The GL context was just made; `asleep` = the display was asleep at that moment.
    void contextCreated(bool asleep) {
        deadLink_ = asleep;
        asleep_ = asleep;
    }

    // Once per poll with the display's state now.  True when it changed (the swap interval needs re-applying).
    bool update(bool asleep) {
        if (asleep == asleep_) return false;
        asleep_ = asleep;
        return true;
    }

    // May the swap interval be 1, given the user's vsync setting?
    bool syncAllowed(bool userVsync) const { return userVsync && !asleep_ && !deadLink_; }

    bool asleep() const { return asleep_; }
    bool deadLink() const { return deadLink_; }

private:
    bool asleep_ = false;
    bool deadLink_ = false;
};

}  // namespace vj
