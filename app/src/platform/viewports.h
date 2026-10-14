// "Let windows move outside the main window" (#833): whether Dear ImGui's multi-viewport mode is on.  Pure logic,
// no ImGui and no SDL, so the rules are unit-tested (tests/platform_test.cpp).
//
// With viewports a Settings / Achievements / Cheats / debug-tool window dragged past the edge of the main window
// becomes its own OS window.  It is a per-user setting with a per-OS default, and a few situations force it off:
//   * Wayland: a client cannot position or raise a secondary window there, so a dragged-out window would land
//     wherever the compositor likes and snap back.  X11 (and XWayland) is fine.  The setting is kept but ignored,
//     and the Settings page says why.
//   * Fullscreen: a window outside the fullscreen window would sit behind a fullscreen Space (macOS) or under the
//     game; everything stays inside while fullscreen.
//   * Headless / dummy video: there is no window system to make windows with.
#pragma once

namespace vj {
namespace viewports {

// The setting's default: macOS and Windows on, Linux and everything else off (Wayland is common there).
bool defaultEnabled();

// SDL_GetCurrentVideoDriver() names that cannot place secondary windows.
bool driverIsWayland(const char* sdlVideoDriver);
// Drivers with no real window system (headless runs).
bool driverIsDummy(const char* sdlVideoDriver);

enum class Why { Active, UserOff, Wayland, Fullscreen, NoWindowSystem };

struct Decision {
    bool active = false;     // ImGuiConfigFlags_ViewportsEnable should be set
    Why why = Why::UserOff;  // the reason when not active
};

// userWants: the setting.  videoDriver: SDL_GetCurrentVideoDriver() (may be null).
Decision decide(bool userWants, const char* videoDriver, bool fullscreen);

// One line for the Settings page when the setting is on but forced off ("" when there is nothing to say).
const char* explain(Why why);

}  // namespace viewports
}  // namespace vj
