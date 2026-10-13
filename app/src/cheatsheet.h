// The controls cheat sheet: what the keys / buttons in the ACTIVE profile (global bindings,
// per-controller and per-game overrides all applied) do for the device in Player 1's port.
//
// Pure: no ImGui, no SDL window, no core.  cheatsheet_ui.cpp draws it; the headless tests
// (test/devices_test.cpp) check that editing a binding changes the text and that the
// auto-show rule shows it for a keyboard player.
#pragma once

#include <string>
#include <vector>

#include "devices.h"
#include "remap.h"

namespace vj {

struct SheetLine {
    std::string label;   // "Fire" / "Keypad 7 (Pro: Z)" / a heading when `heading`
    std::string keys;    // "Z, South (A / Cross)" or "(unbound)"
    bool heading = false;
};

struct Sheet {
    std::string title;   // "Player 1: Rotary (Tempest) - keyboard"
    std::vector<SheetLine> lines;
    std::string text() const;   // the whole sheet as plain text (tests, logs)
};

enum class SheetSource { Keyboard, Gamepad };

std::string bindingsLabel(const std::vector<Binding>& v);   // "Z, Backspace" or "(unbound)"
// `kind` = Player 1's device, `profile` = the resolved profile of the source the sheet is for,
// `deviceName` = the gamepad's name (shown in the title) or "".
Sheet buildSheet(const Profile& profile, DevKind kind, SheetSource src, const std::string& deviceName);

// ---- when it shows by itself ---------------------------------------------------------------------------
struct AutoShowInput {
    bool enabled = true;         // Settings: "Show the controls overlay when a game starts"
    bool contentLoaded = false;
    bool padOnPlayer1 = false;   // a gamepad drives Player 1
    bool padSeenBefore = false;  // that controller model has been used with the app before
};
enum class AutoShow { No, Keyboard, Gamepad };
// A keyboard player (no gamepad on Player 1) always gets the sheet at boot; a gamepad player gets
// it once per controller model, the first time it is used.
AutoShow autoShowDecision(const AutoShowInput& in);

struct ControlsOverlayState {
    bool show = false;
    double hideAt = 0;                 // app clock; 0 = stays until toggled off
    SheetSource source = SheetSource::Keyboard;
    bool lastInputWasPad = false;      // the sheet follows what the player is actually using
};
constexpr double kAutoShowSeconds = 9.0;

}  // namespace vj
