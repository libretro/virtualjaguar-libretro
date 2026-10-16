// The app-side glue of the device layer: claiming devices with the core, the Settings > Input
// panel, the per-frame mouse-capture and controls-overlay housekeeping, the cheat-sheet window.
#pragma once

#include <cstdint>
#include <string>

#include "cheatsheet.h"
#include "imgui.h"

namespace vj {

class App;

// Hand the per-port device choice (per-game override -> global -> default) to the core with
// retro_set_controller_port_device and tell the input layer what ended up in each port.
// Call after the content identity is known (input.setContent) and after any change.
void applyPortDevices(App& app);

// Once per UI frame, before the emulation steps: mouse capture, the controls-overlay key and timer.
void inputFrame(App& app);

// The TV-menu half of the per-frame input housekeeping, free of ImGui: ticks the hold bindings with `ms`
// (SDL_GetTicks()).  inputFrame() calls it; so does the headless script runner, which never calls inputFrame.
void tvInputFrame(App& app, uint64_t ms);

// A game was just loaded: show the controls overlay by itself when the auto-show rule says so.
void controlsOverlayOnLoad(App& app);

// The sheet for Player 1 as it is drawn: the active profile (global, per-controller and per-game
// layers applied) of whatever the player is using, for the device in port 1.
Sheet currentControlsSheet(App& app);
std::string controlsOverlayText(App& app);   // the same, as plain text (--print-controls)

void drawDevicesPanel(App& app);
   // Settings > Input > Devices
void drawControlsOverlay(App& app, ImVec2 areaPos, ImVec2 areaSize);
void drawControlsOverlaySettings(App& app);   // Settings > Interface > Overlays (auto-show rule, show / hide now)

}  // namespace vj
