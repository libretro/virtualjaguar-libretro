// Dual-use buttons for TV mode (#991): HoldArbiter and ReleaseGuard.
//
// HoldArbiter.  A pad button (Guide, Share) is both a Jaguar keypad key for the game and the TV menu's
// way in.  The two are told apart by time: a tap (released under kHoldMs) is the game's, a hold (kHoldMs or
// more) is the menu's.  Nothing can know which it is until the button comes up or the clock runs out, so
// while a hold input is down the game does not see it; a tap is replayed to the game as a short pulse on
// release, a hold fires once and stays swallowed until it is released.  The cost is that the game's keypad
// key arrives late (at release) and as a 4-frame pulse, and that a chord needing the key together with
// another input only forms as the key comes up.
//
// ReleaseGuard.  When input ownership changes (the TV menu closes onto a game, a menu button is let go
// inside the game), whatever is already down must not reach the new owner as a fresh press: it stays
// masked until it is released.
//
// Both are pure: no SDL handles and no clock of their own, so tests drive them with made-up timestamps.
// An input is identified by (device, Binding with kind / code / dir); the Binding's hold flag is ignored.
// Devices are SDL joystick ids, with kKeyboardDev for the keyboard.
#pragma once

#include <cstdint>
#include <vector>

#include "remap.h"

namespace vj {

constexpr uint32_t kKeyboardDev = 0;   // SDL never hands out joystick id 0

class HoldArbiter {
public:
    static constexpr uint64_t kHoldMs = 500;
    static constexpr int kPulseFrames = 4;

    // The inputs that are hold inputs on `dev` (replaces the previous list).  Keys and pad buttons only; the
    // hold flag is ignored.  Inputs no longer listed lose any state they had.
    void setHoldInputs(uint32_t dev, const std::vector<Binding>& inputs);
    bool isHoldInput(uint32_t dev, const Binding& in) const;

    void press(uint32_t dev, const Binding& in, uint64_t ms);     // no-op unless isHoldInput; a repeat keeps the clock
    void release(uint32_t dev, const Binding& in, uint64_t ms);
    // UI frame: the holds that reached kHoldMs since the last call, each firing once.
    int takeFires(uint64_t ms);
    // One emulated frame: pulses count down.
    void frame();
    // A tap queues a pulse only while live (a game is loaded, not paused, and the game owns input).  A tap in the
    // library or the paused menu means nothing to a game and must not reach the next one.  Going not-live drops the
    // queued pulses.  Holds fire either way.
    void setLive(bool live);
    // Rewrite the core-facing snapshot: a hold input that is down (under the threshold, or swallowed after a
    // hold) is cleared, one pulsing is set.  Everything else is left alone.
    void apply(uint32_t dev, DeviceSnapshot& s) const;
    void forget(uint32_t dev);   // pad unplugged: its hold inputs, holds and pulses go; nothing fires
    void clear();                // focus lost / ownership change: every hold and pulse goes, nothing fires

private:
    struct HoldInput {
        uint32_t dev;
        Binding in;   // kind / code only
        bool down = false;
        bool swallowed = false;   // reached kHoldMs: the menu has it until release
        uint64_t downAt = 0;
        int pulse = 0;            // emulated frames of pulse left
    };
    HoldInput* find(uint32_t dev, const Binding& in);
    const HoldInput* find(uint32_t dev, const Binding& in) const;

    std::vector<HoldInput> inputs_;
    int pending_ = 0;     // fires earned by a release at >= kHoldMs that takeFires has not reported yet
    bool live_ = true;
};

class ReleaseGuard {
public:
    // Mask every key / button down in `now` and every axis past its threshold (sticks kStickThreshold by absolute
    // value, triggers kTriggerThreshold).  Adds to what is already masked on `dev`.
    void arm(uint32_t dev, const DeviceSnapshot& now);
    // Clear what is masked and still down; forget what has been released.
    void apply(uint32_t dev, DeviceSnapshot& s);
    void forget(uint32_t dev);   // pad unplugged: its masks go
    bool armed() const;   // anything still masked
    void clear();

private:
    struct Mask {
        uint32_t dev;
        std::vector<int> keys, btns, axes;   // codes
    };
    std::vector<Mask> masks_;
};

}  // namespace vj
