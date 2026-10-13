// The app-side glue of the device layer: claiming devices with the core, the Settings > Input
// panel, the per-frame mouse-capture and controls-overlay housekeeping, the cheat-sheet window.
#pragma once

#include "imgui.h"

namespace vj {

class App;

// Hand the per-port device choice (per-game override -> global -> default) to the core with
// retro_set_controller_port_device and tell the input layer what ended up in each port.
// Call after the content identity is known (input.setContent) and after any change.
void applyPortDevices(App& app);

// Once per UI frame, before the emulation steps: mouse capture, the controls-overlay key and timer.
void inputFrame(App& app);

// A game was just loaded: show the controls overlay by itself when the auto-show rule says so.
void controlsOverlayOnLoad(App& app);

void drawDevicesPanel(App& app);   // Settings > Input > Devices
void drawControlsOverlay(App& app, ImVec2 areaPos, ImVec2 areaSize);

}  // namespace vj
