#include "desktop_nav_app.h"

#include <cstdlib>

#include "app.h"
#include "desktop_nav_popup.h"
#include "imgui.h"
#include "tv/tv_shell.h"

namespace vj {

using tv::Intent;
using tv::NavInput;

bool deskPadActive(App& app) {
    if (app.tvMode.active() || app.tvShell().menuOpen() || app.input.capturing()) return false;
    if (!(app.homeVisible() || app.library.show)) return false;
    // A running game owns its pad; the library window over a paused one is the exception.
    if (app.host.isLoaded() && !(app.paused && app.library.show)) return false;
    return !(app.showSettings || app.showStates || app.showCheats || app.showAbout || app.input.overlay.open);
}

bool deskPadHandleEvent(App& app, const SDL_Event& e, uint64_t ms) {
    DesktopPad& d = app.deskPad;
    switch (e.type) {
    case SDL_EVENT_MOUSE_MOTION:
    case SDL_EVENT_MOUSE_BUTTON_DOWN:
        d.nav.setVisible(false);   // the pointer is the player's again: the ring goes
        return false;
    case SDL_EVENT_GAMEPAD_REMOVED:
        // Only that pad's holds go; the left stick is shared (padAxis), so centre it or a direction held on the unplugged
        // pad would repeat forever.
        d.in.forgetPad(e.gdevice.which);
        d.in.padAxis(SDL_GAMEPAD_AXIS_LEFTX, 0, ms);
        d.in.padAxis(SDL_GAMEPAD_AXIS_LEFTY, 0, ms);
        d.scrollY = 0;
        return false;
    case SDL_EVENT_WINDOW_FOCUS_LOST:
        d.in.reset();   // the matching "up" will never come: no direction may keep repeating
        d.scrollY = 0;  // ... nor may the grid keep scrolling
        return false;
    default: break;
    }
    if (!deskPadActive(app)) return false;
    switch (e.type) {
    case SDL_EVENT_GAMEPAD_BUTTON_DOWN:
    case SDL_EVENT_GAMEPAD_BUTTON_UP: {
        // The pad's own profile.  Start (Menu) and anything unmapped are not the library's: they stay the app's.  Pads are
        // never held back by a focused text field (they do not type).
        const Intent in = app.input.navProfile(e.gbutton.which).intentForButton(e.gbutton.button);
        if (in == Intent::None || in == Intent::Menu) return false;
        const bool down = e.type == SDL_EVENT_GAMEPAD_BUTTON_DOWN;
        if (down) {
            d.nav.setVisible(true);
            d.seq++;
        }
        d.in.padIntent(e.gbutton.which, e.gbutton.button, in, down, ms);
        return down;   // a release is fed (it ends a repeat, or fires a tap) but still reaches the app
    }
    case SDL_EVENT_GAMEPAD_AXIS_MOTION:
        if (e.gaxis.axis == SDL_GAMEPAD_AXIS_LEFT_TRIGGER || e.gaxis.axis == SDL_GAMEPAD_AXIS_RIGHT_TRIGGER) {
            const Intent in = app.input.navProfile(e.gaxis.which).intentForTrigger(e.gaxis.axis);
            if (in == Intent::None) return false;   // an unbound trigger stays the app's
            if (std::abs((int)e.gaxis.value) >= NavInput::kTriggerOn) {
                d.nav.setVisible(true);
                d.seq++;
            }
            d.in.padTrigger(e.gaxis.which, e.gaxis.axis, in, e.gaxis.value, ms);
            return true;
        }
        if (e.gaxis.axis == SDL_GAMEPAD_AXIS_RIGHTY) {   // scrolls the grid (library_ui reads scrollY); nothing else uses it here
            d.scrollY = (float)e.gaxis.value / 32767.0f;
            return false;
        }
        if (e.gaxis.axis != SDL_GAMEPAD_AXIS_LEFTX && e.gaxis.axis != SDL_GAMEPAD_AXIS_LEFTY) return false;
        if (std::abs((int)e.gaxis.value) >= NavInput::kStickOn) {   // a resting stick drifts: only a push counts
            d.nav.setVisible(true);
            d.seq++;
        }
        d.in.padAxis(e.gaxis.axis, e.gaxis.value, ms);
        return true;
    default: return false;
    }
}

// The pad's popup is live: the pad opened it, the library still owns the pad, and an ImGui popup is still open.  The one
// place `padPopup` is cleared (closed by the pad, a row, the mouse or Escape; or the pad changed hands under it).
static bool popupLive(App& app) {
    return app.deskPad.padPopup && ImGui::GetCurrentContext() && deskPadActive(app) &&
           ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopupId | ImGuiPopupFlags_AnyPopupLevel);
}

void deskPadFrame(App& app, uint64_t ms) {
    DesktopPad& d = app.deskPad;
    if (d.padPopup && !popupLive(app)) d.padPopup = false;   // before the edge check: the way out is seen this frame
    const bool act = deskPadActive(app);
    const bool show = app.library.show;
    const int tab = app.library.activeTab();
    // Ownership or the screen changed (a game started or closed, a window opened, a tab switched, a popup opened or closed):
    // the releases in between never reached the nav, so forget what it thinks is held and what it queued, or a direction
    // would repeat on and a release of a press seen elsewhere would fire.  That includes the Accept still held from the
    // long press that opened a tile menu: its release is then a press-never-seen release and does nothing.
    if (act != d.active || show != d.seenShow || tab != d.seenTab || d.padPopup != d.seenPopup) {
        d.active = act;
        d.seenShow = show;
        d.seenTab = tab;
        d.seenPopup = d.padPopup;
        d.in.reset();
        d.pending.clear();
        d.nav.reset();
        d.scrollY = 0;   // a right stick let go while the library was not listening must not scroll on when it is
        d.in.setLongAccept(!d.padPopup);   // inside a popup Accept acts on press (it is a row, not the grid's tap / hold)
    }
    if (!act) return;
    if (d.padPopup) d.scrollY = 0;   // the grid stays put under the menu it anchors
    const std::vector<Intent> intents = d.in.take(ms);
    if (intents.empty()) return;
    if (d.padPopup) {   // the popup's rows are ImGui's (design section 4): our intents, as its gamepad keys
        deskFeedPopupIntents(intents);
        return;
    }
    const std::vector<DeskCmd> cmds = d.nav.apply(intents);
    d.pending.insert(d.pending.end(), cmds.begin(), cmds.end());
}

void deskPadBeforeNewFrame(App& app) {
    deskSetPopupNav(popupLive(app));
}

}  // namespace vj
