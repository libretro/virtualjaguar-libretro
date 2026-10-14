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
    bool enabled = false;        // Settings: "Show the controls overlay when a game starts" (default OFF)
    bool contentLoaded = false;
    bool padOnPlayer1 = false;   // a gamepad drives Player 1
    bool padSeenBefore = false;  // that controller model has been used with the app before
    bool hintShown = false;      // the "Press F3 for the controls" hint was already shown this session
};
enum class AutoShow { No, Keyboard, Gamepad, Hint };
// Off (the default): a keyboard player (no gamepad on Player 1) gets a one-line toast "Press F3 for the
// controls", once per session, and nothing else.  On: a keyboard player gets the sheet at boot; a gamepad
// player gets it once per controller model, the first time it is used.
AutoShow autoShowDecision(const AutoShowInput& in);

struct ControlsOverlayState {
    bool show = false;
    double hideAt = 0;                 // app clock; 0 = stays until toggled off (only used while autoShown)
    bool autoShown = false;            // opened by the app at boot (times out), not by the player (stays)
    bool hintShown = false;            // the "Press F3" toast was shown this session
    SheetSource source = SheetSource::Keyboard;
    bool lastInputWasPad = false;      // the sheet follows what the player is actually using
};
constexpr double kAutoShowSeconds = 8.0;

// ---- show / hide policy (pure, tested in devices_unit) -------------------------------------------------
// The player's way in is the toggle (F3 / pad button / menu).  A click or a tap anywhere, Esc and the
// toggle all hide it; an overlay the app opened by itself also hides after kAutoShowSeconds.
enum class OverlayEvent { Toggle, Click, Tap, Escape };
void overlayAutoShow(ControlsOverlayState& s, double now);              // the app opens it at boot
// Returns true when the event hid a visible overlay (the caller must then swallow a click / tap so the
// game does not also receive it).  Toggle flips it and always returns false.
bool overlayEvent(ControlsOverlayState& s, OverlayEvent e);
// Per frame: hides an auto-shown overlay once its time is up.  True when it just did.
bool overlayTick(ControlsOverlayState& s, double now);

}  // namespace vj
