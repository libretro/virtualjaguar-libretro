#include "hold_arbiter.h"

#include <algorithm>
#include <cstdlib>

namespace vj {

namespace {

// Keys and pad buttons only: an axis has no press edge to time.
bool holdable(const Binding& b) {
    if (b.kind == Binding::Key) return b.code > 0 && b.code < SDL_SCANCODE_COUNT;
    if (b.kind == Binding::PadButton) return b.code >= 0 && b.code < SDL_GAMEPAD_BUTTON_COUNT;
    return false;
}

Binding identity(const Binding& b) {
    Binding id;
    id.kind = b.kind;
    id.code = b.code;
    return id;
}

bool isTrigger(int axis) { return axis == SDL_GAMEPAD_AXIS_LEFT_TRIGGER || axis == SDL_GAMEPAD_AXIS_RIGHT_TRIGGER; }

// Triggers rest at 0 and only go positive; sticks swing both ways.
bool axisDown(int axis, int v) { return isTrigger(axis) ? v > kTriggerThreshold : std::abs(v) > kStickThreshold; }

}  // namespace

HoldArbiter::HoldInput* HoldArbiter::find(uint32_t dev, const Binding& in) {
    for (HoldInput& h : inputs_)
        if (h.dev == dev && h.in.kind == in.kind && h.in.code == in.code) return &h;
    return nullptr;
}

const HoldArbiter::HoldInput* HoldArbiter::find(uint32_t dev, const Binding& in) const {
    for (const HoldInput& h : inputs_)
        if (h.dev == dev && h.in.kind == in.kind && h.in.code == in.code) return &h;
    return nullptr;
}

void HoldArbiter::setHoldInputs(uint32_t dev, const std::vector<Binding>& inputs) {
    std::vector<HoldInput> next;
    for (const Binding& b : inputs) {
        if (!holdable(b)) continue;
        Binding id = identity(b);
        bool dup = false;
        for (const HoldInput& n : next) dup |= n.dev == dev && n.in.kind == id.kind && n.in.code == id.code;
        if (dup) continue;
        if (const HoldInput* old = find(dev, id)) {
            next.push_back(*old);   // still a hold input: keep its state
        } else {
            HoldInput h;
            h.dev = dev;
            h.in = id;
            next.push_back(h);
        }
    }
    for (const HoldInput& h : inputs_)
        if (h.dev != dev) next.push_back(h);
    inputs_ = next;
}

bool HoldArbiter::isHoldInput(uint32_t dev, const Binding& in) const { return find(dev, in) != nullptr; }

bool HoldArbiter::withholds(uint32_t dev, const Binding& in) const {
    const HoldInput* h = find(dev, in);
    return h && h->down;
}

void HoldArbiter::press(uint32_t dev, const Binding& in, uint64_t ms) {
    HoldInput* h = find(dev, in);
    if (!h || h->down) return;   // not a hold input, or a repeat of a press already timing
    h->down = true;
    h->swallowed = false;
    h->downAt = ms;
    h->pulse = 0;   // the previous tap's pulse (if still running) ends: this press is withheld
}

void HoldArbiter::release(uint32_t dev, const Binding& in, uint64_t ms) {
    HoldInput* h = find(dev, in);
    if (!h || !h->down) return;
    h->down = false;
    if (h->swallowed) return;   // takeFires already reported it
    if (ms - h->downAt >= kHoldMs) {
        pending_++;   // the threshold passed between two takeFires: the release carries the fire
    } else if (live_) {
        h->pulse = kPulseFrames;
    }
    h->swallowed = false;
}

int HoldArbiter::takeFires(uint64_t ms) {
    int n = pending_;
    pending_ = 0;
    for (HoldInput& h : inputs_) {
        if (!h.down || h.swallowed || ms - h.downAt < kHoldMs) continue;
        h.swallowed = true;
        n++;
    }
    return n;
}

void HoldArbiter::frame() {
    for (HoldInput& h : inputs_)
        if (h.pulse > 0) h.pulse--;
}

void HoldArbiter::setLive(bool live) {
    live_ = live;
    if (!live)
        for (HoldInput& h : inputs_) h.pulse = 0;
}

void HoldArbiter::apply(uint32_t dev, DeviceSnapshot& s) const {
    for (const HoldInput& h : inputs_) {
        if (h.dev != dev) continue;
        bool set;
        if (h.down) set = false;          // withheld: under the threshold, or swallowed after a hold
        else if (h.pulse > 0) set = true;
        else continue;
        if (h.in.kind == Binding::Key) s.key[(size_t)h.in.code] = set;
        else s.btn[(size_t)h.in.code] = set;
    }
}

void HoldArbiter::forget(uint32_t dev) {
    inputs_.erase(std::remove_if(inputs_.begin(), inputs_.end(), [dev](const HoldInput& h) { return h.dev == dev; }), inputs_.end());
}

void HoldArbiter::clear() {
    for (HoldInput& h : inputs_) {
        h.down = false;
        h.swallowed = false;
        h.pulse = 0;
    }
    pending_ = 0;
}

void ReleaseGuard::arm(uint32_t dev, const DeviceSnapshot& now) {
    Mask* m = nullptr;
    for (Mask& x : masks_)
        if (x.dev == dev) m = &x;
    Mask fresh;
    fresh.dev = dev;
    if (!m) m = &fresh;
    auto add = [](std::vector<int>& v, int c) {
        if (std::find(v.begin(), v.end(), c) == v.end()) v.push_back(c);
    };
    for (int k = 0; k < SDL_SCANCODE_COUNT; k++)
        if (now.key[(size_t)k]) add(m->keys, k);
    for (int b = 0; b < SDL_GAMEPAD_BUTTON_COUNT; b++)
        if (now.btn[(size_t)b]) add(m->btns, b);
    for (int a = 0; a < SDL_GAMEPAD_AXIS_COUNT; a++)
        if (axisDown(a, now.axis[(size_t)a])) add(m->axes, a);
    if (m == &fresh && (!fresh.keys.empty() || !fresh.btns.empty() || !fresh.axes.empty())) masks_.push_back(fresh);
}

void ReleaseGuard::apply(uint32_t dev, DeviceSnapshot& s) {
    for (size_t i = 0; i < masks_.size(); i++) {
        Mask& m = masks_[i];
        if (m.dev != dev) continue;
        // Still down: hide it.  Released: forget it, so the next press reaches the game.
        m.keys.erase(std::remove_if(m.keys.begin(), m.keys.end(), [&](int k) {
            if (!s.key[(size_t)k]) return true;
            s.key[(size_t)k] = false;
            return false;
        }), m.keys.end());
        m.btns.erase(std::remove_if(m.btns.begin(), m.btns.end(), [&](int b) {
            if (!s.btn[(size_t)b]) return true;
            s.btn[(size_t)b] = false;
            return false;
        }), m.btns.end());
        m.axes.erase(std::remove_if(m.axes.begin(), m.axes.end(), [&](int a) {
            if (!axisDown(a, s.axis[(size_t)a])) return true;
            s.axis[(size_t)a] = 0;
            return false;
        }), m.axes.end());
        if (m.keys.empty() && m.btns.empty() && m.axes.empty()) masks_.erase(masks_.begin() + (long)i);
        return;
    }
}

void ReleaseGuard::forget(uint32_t dev) {
    masks_.erase(std::remove_if(masks_.begin(), masks_.end(), [&](const Mask& m) { return m.dev == dev; }), masks_.end());
}

bool ReleaseGuard::armed() const { return !masks_.empty(); }

void ReleaseGuard::clear() { masks_.clear(); }

}  // namespace vj
