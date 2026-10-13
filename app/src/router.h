// Host input -> what the core reads, for every device the Jaguar ports can hold.
//
// The pure half of InputManager::poll(): it takes already-snapshotted keyboards, gamepads and
// the mouse, the per-port device kinds and the player-slot routes, evaluates each host source
// through its (remappable) Profile and fills a CoreInput.  No SDL handles, no window, no core --
// test/devices_e2e.cpp feeds it synthetic input and reads the core's own state back.
//
// Each mapping below quotes the libretro.c code it satisfies (update_input(), ~line 4560+).
#pragma once

#include <array>
#include <vector>

#include "devices.h"
#include "remap.h"

namespace vj {

struct HostSource {
    const Profile* profile = nullptr;
    DeviceSnapshot snap;
    bool isKeyboard = false;
    unsigned overlaySlot = 0;   // KeypadOverlay::filter() slot
};

struct PointerInput {
    float dx = 0, dy = 0;       // relative motion since the previous emulated frame
    unsigned buttons = 0;       // 1 left, 2 right, 4 middle
    bool havePos = false;       // absolute position is meaningful (over the picture area, not over a menu)
    float x = 0, y = 0;         // in the PicRect's units
};

struct RouterConfig {
    std::array<DevKind, 2> kind{{DevKind::Pad, DevKind::Pad}};
    Routes routes;
    int mousePort = -1;         // Jaguar port the physical mouse drives, -1 none
    DeviceSettings settings;
    PicRect pic;
};

// Everything the host sources driving one player slot press this frame, merged.
struct SlotAcc {
    ControlSet cs;       // standard pad + keypad controls
    ControlSet ex;       // C_D .. C_GUN_RELOAD digital
    AnalogSet an;
};

class Router {
public:
    void reset();
    // `pads` and `kb` are indexed by Routes::pad / Routes::kb.  `overlay` may be null.
    void route(const RouterConfig& cfg, const std::vector<HostSource>& pads, const HostSource* kb,
                 const PointerInput& ptr, KeypadOverlay* overlay, CoreInput& out);

private:
    float frac_[2][2] = {{0, 0}, {0, 0}};      // sub-count remainder per Jaguar port (x, y)
    float cursor_[2][2] = {{0.5f, 0.5f}, {0.5f, 0.5f}};   // stick-driven aim, fraction of the picture
    bool gunOnStick_[2] = {false, false};
    float lastX_[2] = {-1, -1}, lastY_[2] = {-1, -1};
};

// Tuning constants (documented in the PR): counts per emulated frame at full deflection.
constexpr float kRotaryCountsPerFrame = 1.0f;    // the encoder emits at most one state per poll (inputdev.h)
constexpr float kStickPixelsPerFrame = 8.0f;     // a stick / key driven mouse
constexpr float kAimFractionPerFrame = 0.02f;    // a stick / key driven light-gun cursor, of the picture width

}  // namespace vj
