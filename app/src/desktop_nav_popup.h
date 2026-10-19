// Pad -> ImGui's own gamepad navigation, for the popups the desktop library opens from pad focus (#1021, design section 4).
// Header-only so the headless ImGui smoke test (tv_ui_smoke) drives the same code the app does.
//
// The SDL3 backend is in Manual gamepad mode (it submits no pad state), so ImGui sees a pad only through these events, which
// carry OUR intents: the pad profile has already swapped Confirm / Back on a Switch-family pad, so a popup follows the same
// swap as the grid.  Nothing else (Alt, Aux, the section / page intents) means anything inside a popup.
#pragma once

#include <vector>

#include "imgui.h"
#include "tv/tv_nav.h"

namespace vj {

// The ImGui gamepad key an intent presses inside a popup; ImGuiKey_None for the intents a popup ignores.
inline ImGuiKey deskPopupKey(tv::Intent in) {
    switch (in) {
    case tv::Intent::Up: return ImGuiKey_GamepadDpadUp;
    case tv::Intent::Down: return ImGuiKey_GamepadDpadDown;
    case tv::Intent::Left: return ImGuiKey_GamepadDpadLeft;
    case tv::Intent::Right: return ImGuiKey_GamepadDpadRight;
    case tv::Intent::Accept: return ImGuiKey_GamepadFaceDown;
    case tv::Intent::Back: return ImGuiKey_GamepadFaceRight;
    default: return ImGuiKey_None;
    }
}

// Each intent is a press and a release (ImGui spreads the pair over two frames, ConfigInputTrickleEventQueue): a tap, since
// NavInput already did the repeating.  Call before the frame's ImGui::NewFrame().
inline void deskFeedPopupIntents(const std::vector<tv::Intent>& intents) {
    ImGuiIO& io = ImGui::GetIO();
    for (tv::Intent in : intents) {
        const ImGuiKey k = deskPopupKey(in);
        if (k == ImGuiKey_None) continue;
        io.AddKeyEvent(k, true);
        io.AddKeyEvent(k, false);
    }
}

// Is an ImGui popup open that the pad did not open (a right-click or menu-bar menu, the Lists popup, a combo, a modal)?  The
// library then does not own the pad.  False without an ImGui context (headless).
inline bool deskForeignPopupOpen(bool padPopup) {
    return !padPopup && ImGui::GetCurrentContext() &&
           ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopupId | ImGuiPopupFlags_AnyPopupLevel);
}

// Gamepad navigation on or off.  HasGamepad is a backend flag: the Manual-mode SDL3 backend clears it every NewFrame, so
// this runs between ImGui_ImplSDL3_NewFrame() and ImGui::NewFrame().  Navigation is live only with both flags set.
// While it is on the nav cursor is always visible: a popup the pad opened has its first row focused from the first frame
// (a press of Accept acts on it at once), and ImGui would otherwise draw no highlight until a direction was pressed.  A
// popup the mouse opened never runs with this on, so its look is unchanged.
inline void deskSetPopupNav(bool on) {
    ImGuiIO& io = ImGui::GetIO();
    if (on) {
        io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;
        io.BackendFlags |= ImGuiBackendFlags_HasGamepad;
        io.ConfigNavCursorVisibleAlways = true;
    } else {
        io.ConfigFlags &= ~ImGuiConfigFlags_NavEnableGamepad;
        io.BackendFlags &= ~ImGuiBackendFlags_HasGamepad;
        if (io.ConfigNavCursorVisibleAlways) {   // just turned off: the highlight goes with it
            io.ConfigNavCursorVisibleAlways = false;
            ImGui::SetNavCursorVisible(false);
        }
    }
}

}  // namespace vj
