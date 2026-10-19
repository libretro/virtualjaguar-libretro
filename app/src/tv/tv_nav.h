// TV mode (#991): pad and keyboard events -> navigation intents.  NavInput turns raw SDL button / axis / key
// events into Intents for the TV shell, with held-direction repeat (350 ms, then every 80 ms) and a
// hysteresis on the stick so a drifting thumb does not chatter.  Only directions repeat, and only one held
// direction at a time does: the newest.  Letting go of the newest hands the repeat back to an older one that
// is still down (after a fresh delay, with no new press).
//
// Pure: no SDL handles and no clock of its own; callers pass SDL's button / axis / scancode numbers and a
// millisecond timestamp, tests pass made-up ones.  Several pads feed one NavInput (the TV menu is one
// cursor); a button on any of them counts.  A source is (pad, button): two pads holding the same button are two
// sources, and forgetPad drops everything one pad was holding.  padIntent / padTrigger take the Intent from the
// caller (a per-family button profile, #1021); padButton / padAxis keep the Xbox table for the TV shell.
#pragma once

#include <cstdint>
#include <vector>

namespace vj::tv {

enum class Intent { None, Up, Down, Left, Right, Accept, Back, Alt, Aux, Menu, SectionPrev, SectionNext, PagePrev, PageNext, Search, AcceptLong };
// Search: focus the library's search box (desktop pad navigation, #1021).  AcceptLong: Accept held kLongMs.  Both are
// supplied by a pad profile through padIntent / padTrigger, never by intentForButton / intentForKey.

Intent intentForButton(int sdlButton);           // SDL_GamepadButton -> Intent (None when unmapped)
Intent intentForKey(int scancode, bool shift);   // SDL_Scancode -> Intent (None when unmapped)
// The caption of the trigger behind PagePrev / PageNext on a pad ("LT"/"RT", "L2"/"R2" on a PlayStation-family pad); "" for any other intent.
const char* triggerLabel(Intent in, bool sony);

class NavInput {
public:
    static constexpr uint64_t kRepeatDelayMs = 350, kRepeatMs = 80;
    static constexpr int kStickOn = 16000, kStickOff = 8000;   // hysteresis
    static constexpr int kTriggerOn = 8000, kTriggerOff = 4000;

    static constexpr uint64_t kLongMs = 500;   // Accept held this long becomes AcceptLong (setLongAccept)

    void padButton(int sdlButton, bool down, uint64_t ms);   // == padIntent(0, b, intentForButton(b), ...)
    void padAxis(int sdlAxis, int16_t v, uint64_t ms);
    // A pad button whose Intent the caller chose.  With setLongAccept(true), Accept arriving here waits for the
    // release: under kLongMs it is a tap (Accept, on release); at kLongMs while still held take() emits AcceptLong
    // once and the release is swallowed.  A release whose press was not seen (or predates reset()) emits nothing.
    void padIntent(uint32_t pad, int sdlButton, Intent in, bool down, uint64_t ms);
    // A trigger axis as an edge: `in` fires once when v reaches kTriggerOn, re-arms below kTriggerOff.  No repeat.
    // Latched per (pad, axis); Intent::None ignores the axis.
    void padTrigger(uint32_t pad, int sdlAxis, Intent in, int16_t v, uint64_t ms);
    void setLongAccept(bool on) { longAccept_ = on; }   // off (default): Accept fires on press, as before
    void forgetPad(uint32_t pad);            // pad unplugged: drop what it held; emits nothing, edges already queued stay
    void key(int scancode, bool down, bool shift, uint64_t ms);
    std::vector<Intent> take(uint64_t ms);   // edges since the last call, plus repeats (directions only)
    void reset();                            // forget everything held and pending

private:
    struct Held { int src; Intent dir; };
    struct HeldAccept { int src; uint64_t downAt; bool swallowed; };
    void press(int src, Intent in, uint64_t ms);
    void release(int src, uint64_t ms);
    void stick(int axis, int16_t v, uint64_t ms);

    std::vector<Intent> pending_;
    std::vector<Held> held_;           // directions currently down, oldest first; back() repeats
    uint64_t nextRepeat_ = 0;
    int stickState_[4] = {0, 0, 0, 0}; // per stick axis: -1 / 0 / +1
    bool trig_[2] = {false, false};
    std::vector<int> down_;            // non-direction sources down (their repeats are not ours)
    bool longAccept_ = false;
    std::vector<HeldAccept> acceptHeld_;   // pad Accepts down, waiting to become a tap or AcceptLong
    std::vector<int> trigOn_;          // padTrigger latches that are on, by (pad, axis) source
    bool rearm_ = false;               // forgetPad handed the repeat to an older hold: restart its delay at the next take
};

}  // namespace vj::tv
