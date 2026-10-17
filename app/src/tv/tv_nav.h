// TV mode (#991): pad and keyboard events -> navigation intents.  NavInput turns raw SDL button / axis / key
// events into Intents for the TV shell, with held-direction repeat (350 ms, then every 80 ms) and a
// hysteresis on the stick so a drifting thumb does not chatter.  Only directions repeat, and only one held
// direction at a time does: the newest.  Letting go of the newest hands the repeat back to an older one that
// is still down (after a fresh delay, with no new press).
//
// Pure: no SDL handles and no clock of its own; callers pass SDL's button / axis / scancode numbers and a
// millisecond timestamp, tests pass made-up ones.  Several pads feed one NavInput (the TV menu is one
// cursor); a button on any of them counts.
#pragma once

#include <cstdint>
#include <vector>

namespace vj::tv {

enum class Intent { None, Up, Down, Left, Right, Accept, Back, Alt, Aux, Menu, SectionPrev, SectionNext, PagePrev, PageNext };

Intent intentForButton(int sdlButton);           // SDL_GamepadButton -> Intent (None when unmapped)
Intent intentForKey(int scancode, bool shift);   // SDL_Scancode -> Intent (None when unmapped)
// The caption of the trigger behind PagePrev / PageNext on a pad ("LT"/"RT", "L2"/"R2" on a PlayStation-family pad); "" for any other intent.
const char* triggerLabel(Intent in, bool sony);

class NavInput {
public:
    static constexpr uint64_t kRepeatDelayMs = 350, kRepeatMs = 80;
    static constexpr int kStickOn = 16000, kStickOff = 8000;   // hysteresis
    static constexpr int kTriggerOn = 8000, kTriggerOff = 4000;

    void padButton(int sdlButton, bool down, uint64_t ms);
    void padAxis(int sdlAxis, int16_t v, uint64_t ms);
    void key(int scancode, bool down, bool shift, uint64_t ms);
    std::vector<Intent> take(uint64_t ms);   // edges since the last call, plus repeats (directions only)
    void reset();                            // forget everything held and pending

private:
    struct Held { int src; Intent dir; };
    void press(int src, Intent in, uint64_t ms);
    void release(int src, uint64_t ms);
    void stick(int axis, int16_t v, uint64_t ms);

    std::vector<Intent> pending_;
    std::vector<Held> held_;           // directions currently down, oldest first; back() repeats
    uint64_t nextRepeat_ = 0;
    int stickState_[4] = {0, 0, 0, 0}; // per stick axis: -1 / 0 / +1
    bool trig_[2] = {false, false};
    std::vector<int> down_;            // non-direction sources down (their repeats are not ours)
};

}  // namespace vj::tv
