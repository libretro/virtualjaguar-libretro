#include "tv/tv_nav.h"

#include <SDL3/SDL.h>

#include <algorithm>

namespace vj::tv {

namespace {
// Source ids: one per physical input, so a release finds its press and a key's OS auto-repeat is ignored.
constexpr int kSrcButton = 0x1000, kSrcKey = 0x2000, kSrcAxis = 0x3000;

// A source also carries the pad it came from (pad 0 for the keyboard and the legacy entry points, so those ids
// are what they always were).
int padSrc(uint32_t pad, int base) { return (int)((pad & 0x7FFF) << 16) | base; }
uint32_t srcPad(int src) { return ((uint32_t)src >> 16) & 0x7FFF; }
bool srcIsPadButton(int src) { return (src & 0xFFFF) >= kSrcButton && (src & 0xFFFF) < kSrcKey; }

bool isDirection(Intent in) { return in == Intent::Up || in == Intent::Down || in == Intent::Left || in == Intent::Right; }
}  // namespace

Intent intentForButton(int b) {
    switch (b) {
    case SDL_GAMEPAD_BUTTON_DPAD_UP: return Intent::Up;
    case SDL_GAMEPAD_BUTTON_DPAD_DOWN: return Intent::Down;
    case SDL_GAMEPAD_BUTTON_DPAD_LEFT: return Intent::Left;
    case SDL_GAMEPAD_BUTTON_DPAD_RIGHT: return Intent::Right;
    case SDL_GAMEPAD_BUTTON_SOUTH: return Intent::Accept;
    case SDL_GAMEPAD_BUTTON_EAST: return Intent::Back;
    case SDL_GAMEPAD_BUTTON_WEST: return Intent::Alt;
    case SDL_GAMEPAD_BUTTON_NORTH: return Intent::Aux;
    case SDL_GAMEPAD_BUTTON_START: return Intent::Menu;
    case SDL_GAMEPAD_BUTTON_LEFT_SHOULDER: return Intent::SectionPrev;
    case SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER: return Intent::SectionNext;
    default: return Intent::None;
    }
}

Intent intentForKey(int sc, bool shift) {
    switch (sc) {
    case SDL_SCANCODE_UP: return Intent::Up;
    case SDL_SCANCODE_DOWN: return Intent::Down;
    case SDL_SCANCODE_LEFT: return Intent::Left;
    case SDL_SCANCODE_RIGHT: return Intent::Right;
    case SDL_SCANCODE_RETURN:
    case SDL_SCANCODE_KP_ENTER: return Intent::Accept;
    case SDL_SCANCODE_ESCAPE:
    case SDL_SCANCODE_BACKSPACE: return Intent::Back;
    case SDL_SCANCODE_TAB: return shift ? Intent::SectionPrev : Intent::SectionNext;
    case SDL_SCANCODE_PAGEUP: return Intent::PagePrev;
    case SDL_SCANCODE_PAGEDOWN: return Intent::PageNext;
    default: return Intent::None;
    }
}

const char* triggerLabel(Intent in, bool sony) {
    if (in == Intent::PagePrev) return sony ? "L2" : "LT";
    if (in == Intent::PageNext) return sony ? "R2" : "RT";
    return "";
}

void NavInput::press(int src, Intent in, uint64_t ms) {
    if (in == Intent::None) return;
    if (isDirection(in)) {
        for (const Held& h : held_)
            if (h.src == src) return;                    // already down (OS key auto-repeat)
        held_.push_back({src, in});
        rearm_ = false;   // this press sets the delay
        pending_.push_back(in);
        nextRepeat_ = ms + kRepeatDelayMs;
    } else {
        if (std::find(down_.begin(), down_.end(), src) != down_.end()) return;
        down_.push_back(src);
        if (in == Intent::Accept && longAccept_ && srcIsPadButton(src)) acceptHeld_.push_back({src, ms, false});
        else pending_.push_back(in);
    }
}

void NavInput::release(int src, uint64_t ms) {
    down_.erase(std::remove(down_.begin(), down_.end(), src), down_.end());
    for (size_t i = 0; i < acceptHeld_.size(); i++) {
        if (acceptHeld_[i].src != src) continue;
        // Still undecided: a tap, unless take() was late and the hold already crossed the line.
        if (!acceptHeld_[i].swallowed)
            pending_.push_back(ms >= acceptHeld_[i].downAt && ms - acceptHeld_[i].downAt >= kLongMs ? Intent::AcceptLong : Intent::Accept);
        acceptHeld_.erase(acceptHeld_.begin() + (long)i);
        return;
    }
    for (size_t i = 0; i < held_.size(); i++) {
        if (held_[i].src != src) continue;
        bool wasCurrent = (i + 1 == held_.size());
        held_.erase(held_.begin() + (long)i);
        if (wasCurrent) {
            rearm_ = false;
            if (!held_.empty()) nextRepeat_ = ms + kRepeatDelayMs;   // an older hold takes over
        }
        return;
    }
}

void NavInput::padIntent(uint32_t pad, int b, Intent in, bool down, uint64_t ms) {
    int src = padSrc(pad, kSrcButton + b);
    if (down) press(src, in, ms);
    else release(src, ms);
}

void NavInput::padButton(int b, bool down, uint64_t ms) { padIntent(0, b, intentForButton(b), down, ms); }

void NavInput::padTrigger(uint32_t pad, int axis, Intent in, int16_t v, uint64_t) {
    if (in == Intent::None) return;
    int src = padSrc(pad, kSrcAxis + axis);
    auto it = std::find(trigOn_.begin(), trigOn_.end(), src);
    if (it == trigOn_.end() && v >= kTriggerOn) {
        trigOn_.push_back(src);
        pending_.push_back(in);
    } else if (it != trigOn_.end() && v < kTriggerOff) {
        trigOn_.erase(it);
    }
}

void NavInput::forgetPad(uint32_t pad) {
    pad &= 0x7FFF;
    auto mine = [&](int src) { return srcPad(src) == pad; };
    down_.erase(std::remove_if(down_.begin(), down_.end(), mine), down_.end());
    trigOn_.erase(std::remove_if(trigOn_.begin(), trigOn_.end(), mine), trigOn_.end());
    acceptHeld_.erase(std::remove_if(acceptHeld_.begin(), acceptHeld_.end(), [&](const HeldAccept& a) { return mine(a.src); }), acceptHeld_.end());
    bool wasCurrent = !held_.empty() && mine(held_.back().src);
    held_.erase(std::remove_if(held_.begin(), held_.end(), [&](const Held& h) { return mine(h.src); }), held_.end());
    if (wasCurrent && !held_.empty()) rearm_ = true;   // an older hold takes over, after a fresh delay
}

void NavInput::key(int sc, bool down, bool shift, uint64_t ms) {
    int src = kSrcKey + sc;
    if (down) press(src, intentForKey(sc, shift), ms);
    else release(src, ms);
}

// A stick axis is two directions that cannot both be on: -1 and +1, each with its own on / off thresholds.
void NavInput::stick(int axis, int16_t v, uint64_t ms) {
    int idx = axis - SDL_GAMEPAD_AXIS_LEFTX;   // LEFTX, LEFTY, RIGHTX, RIGHTY are consecutive
    if (idx < 0 || idx > 3) return;
    int cur = stickState_[idx];
    int next = cur;
    if (cur == 0) next = v >= kStickOn ? 1 : v <= -kStickOn ? -1 : 0;
    else if (cur > 0) next = v >= kStickOff ? 1 : (v <= -kStickOn ? -1 : 0);
    else next = v <= -kStickOff ? -1 : (v >= kStickOn ? 1 : 0);
    if (next == cur) return;
    // Y grows downward in SDL.
    bool horiz = (axis == SDL_GAMEPAD_AXIS_LEFTX || axis == SDL_GAMEPAD_AXIS_RIGHTX);
    auto srcFor = [&](int sign) { return kSrcAxis + idx * 2 + (sign > 0 ? 1 : 0); };
    auto dirFor = [&](int sign) { return horiz ? (sign > 0 ? Intent::Right : Intent::Left) : (sign > 0 ? Intent::Down : Intent::Up); };
    if (cur != 0) release(srcFor(cur), ms);
    stickState_[idx] = next;
    if (next != 0) press(srcFor(next), dirFor(next), ms);
}

void NavInput::padAxis(int axis, int16_t v, uint64_t ms) {
    if (axis == SDL_GAMEPAD_AXIS_LEFT_TRIGGER || axis == SDL_GAMEPAD_AXIS_RIGHT_TRIGGER) {
        int t = (axis == SDL_GAMEPAD_AXIS_RIGHT_TRIGGER) ? 1 : 0;
        if (!trig_[t] && v >= kTriggerOn) {
            trig_[t] = true;
            pending_.push_back(t ? Intent::PageNext : Intent::PagePrev);
        } else if (trig_[t] && v < kTriggerOff) {
            trig_[t] = false;
        }
        return;
    }
    stick(axis, v, ms);
}

std::vector<Intent> NavInput::take(uint64_t ms) {
    std::vector<Intent> out;
    out.swap(pending_);
    repeated_ = false;
    for (HeldAccept& a : acceptHeld_) {
        if (a.swallowed || ms < a.downAt || ms - a.downAt < kLongMs) continue;
        a.swallowed = true;   // fires once; its release is swallowed
        out.push_back(Intent::AcceptLong);
    }
    if (rearm_) {
        rearm_ = false;
        nextRepeat_ = ms + kRepeatDelayMs;
    }
    if (!held_.empty() && ms >= nextRepeat_) {
        out.push_back(held_.back().dir);
        repeated_ = true;   // always the last of the call
        // One repeat per call however late the call is; the clock catches up so a stall does not burst.
        nextRepeat_ += kRepeatMs * ((ms - nextRepeat_) / kRepeatMs + 1);
    }
    return out;
}

void NavInput::reset() {
    pending_.clear();
    held_.clear();
    down_.clear();
    acceptHeld_.clear();
    trigOn_.clear();
    rearm_ = false;
    repeated_ = false;
    nextRepeat_ = 0;
    for (int& s : stickState_) s = 0;
    trig_[0] = trig_[1] = false;
}

}  // namespace vj::tv
