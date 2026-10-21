#include "tv_more_settings.h"

#include <cstdlib>
#include <vector>

#include "app.h"
#include "desktop_nav_popup.h"
#include "imgui.h"
#include "tv/tv_nav.h"
#include "tv/tv_shell.h"
#include "tv/tv_style.h"

namespace vj {

using tv::Intent;
using tv::NavInput;

namespace {

// The pads' intents for the window.  One window, one App per process, so one NavInput; it starts over whenever the window opens or
// goes (tvMoreSettingsOpen / the frame that sees it gone) and whenever a binding capture takes the input away.
NavInput& nav() {
    static NavInput n;
    return n;
}

// An intent the TV screens act on; Search and AcceptLong are the desktop library's, Menu (Start) stays the app's here.
bool isOurs(Intent in) { return in != Intent::None && in != Intent::Menu && in != Intent::Search && in != Intent::AcceptLong; }

// An ImGui popup (a combo, a modal) is open, or an item is active (a text box, a slider being tweaked): a Back belongs to it.
bool popupOrItemActive() {
    if (!ImGui::GetCurrentContext()) return false;   // headless: nothing to leave
    return ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopupId | ImGuiPopupFlags_AnyPopupLevel) || ImGui::IsAnyItemActive();
}

}  // namespace

bool tvMoreSettingsLive(App& app) {
    // A running game with the menu closed is never live: the game reads the pads by polling (not from the events this takes), so a
    // window driven by the pad would be played through.  The Base layer is TV mode with no game.
    return app.tvMoreSettings_ && app.showSettings && (app.tvShell().menuOpen() || (app.tvMode.active() && !app.host.isLoaded()));
}

void tvMoreSettingsOpen(App& app) {
    app.showSettings = true;
    app.tvMoreSettings_ = true;
    nav().reset();   // whatever the A that opened it left behind is not the window's
}

bool tvMoreSettingsHandleEvent(App& app, const SDL_Event& e, uint64_t ms) {
    if (!tvMoreSettingsLive(app) || app.input.capturing()) return false;   // a capture (Input tab) takes every event for the binding
    NavInput& n = nav();
    switch (e.type) {
    case SDL_EVENT_GAMEPAD_BUTTON_DOWN:
    case SDL_EVENT_GAMEPAD_BUTTON_UP: {
        // The pad's own profile, as the shell takes it.  Start and what is not TV's stay the app's.
        const Intent in = app.input.navProfile(e.gbutton.which).intentForButton(e.gbutton.button);
        if (!isOurs(in)) return false;
        const bool down = e.type == SDL_EVENT_GAMEPAD_BUTTON_DOWN;
        n.padIntent(e.gbutton.which, e.gbutton.button, in, down, ms);
        return down;   // a release is fed (it stops a repeat) but still reaches the app: the shell may have seen its press
    }
    case SDL_EVENT_GAMEPAD_AXIS_MOTION:
        if (e.gaxis.axis == SDL_GAMEPAD_AXIS_LEFT_TRIGGER || e.gaxis.axis == SDL_GAMEPAD_AXIS_RIGHT_TRIGGER) {
            const Intent in = app.input.navProfile(e.gaxis.which).intentForTrigger(e.gaxis.axis);
            if (!isOurs(in)) return false;   // an unbound trigger stays the app's
            n.padTrigger(e.gaxis.which, e.gaxis.axis, in, e.gaxis.value, ms);
            return std::abs((int)e.gaxis.value) >= NavInput::kTriggerOff;   // the way back to rest is fed but not consumed: the shell may be holding it
        }
        if (e.gaxis.axis != SDL_GAMEPAD_AXIS_LEFTX && e.gaxis.axis != SDL_GAMEPAD_AXIS_LEFTY) return false;   // the right stick is the pointer's
        n.padAxis(e.gaxis.axis, e.gaxis.value, ms);
        return std::abs((int)e.gaxis.value) >= NavInput::kStickOff;   // likewise: a stick still pushed when the window opened must reach the shell's nav on centring
    case SDL_EVENT_KEY_DOWN: {
        // ImGui has had every key already (App::handleEvent feeds it first).  What it must not meet is the in-game menu's shell, which
        // would take the same arrows, Enter and Esc as its own and step the Settings screen underneath the window; and with no
        // keyboard capture (the window is not focused) Esc must still close it.  Ctrl/Alt/Cmd chords are hotkeys, as in the shell.
        if (e.key.mod & (SDL_KMOD_CTRL | SDL_KMOD_ALT | SDL_KMOD_GUI)) return false;
        if (e.key.scancode == SDL_SCANCODE_ESCAPE) {
            if (!e.key.repeat) tvMoreSettingsClose(app);   // refused: the combo / text box has it (ImGui saw the key)
            return true;
        }
        if (tv::intentForKey(e.key.scancode, (e.key.mod & SDL_KMOD_SHIFT) != 0) == Intent::None) return false;   // F4 and friends are the app's
        return true;   // (a key-up is not consumed: it reaches the shell, which may have seen the press, and ends a hold)
    }
    case SDL_EVENT_GAMEPAD_REMOVED:
        n.forgetPad(e.gdevice.which);   // the matching release will never come; the left stick is shared, so centre it
        n.padAxis(SDL_GAMEPAD_AXIS_LEFTX, 0, ms);
        n.padAxis(SDL_GAMEPAD_AXIS_LEFTY, 0, ms);
        return false;
    case SDL_EVENT_WINDOW_FOCUS_LOST:
        n.reset();
        return false;
    default: return false;
    }
}

void tvMoreSettingsFrame(App& app, uint64_t ms) {
    NavInput& n = nav();
    if (app.tvMoreSettings_ && !tvMoreSettingsLive(app)) {   // the window closed (its close box, Ctrl/Cmd+Comma, Settings > Reset), or TV mode and the menu are gone
        app.tvMoreSettings_ = false;
        n.reset();
    }
    if (!tvMoreSettingsLive(app)) return;
    if (app.input.capturing()) {   // its events bypass us: a direction that was down would never see its release
        n.reset();
        return;
    }
    std::vector<Intent> feed;
    for (Intent in : n.take(ms)) {
        if (in == Intent::Back && tvMoreSettingsClose(app)) return;   // closed: the rest of this frame's presses went with it
        feed.push_back(in);   // a Back it refused goes on as ImGui's Cancel: it closes the combo / leaves the text box
    }
    if (!feed.empty() && ImGui::GetCurrentContext()) deskFeedPopupIntents(feed);
}

bool tvMoreSettingsClose(App& app) {
    if (!tvMoreSettingsLive(app) || popupOrItemActive()) return false;
    app.showSettings = false;
    app.tvMoreSettings_ = false;
    nav().reset();
    return true;
}

float tvMoreSettingsFontScale(App&) {
    if (!ImGui::GetCurrentContext()) return 1.0f;
    const ImGuiStyle& st = ImGui::GetStyle();
    return tv::bodyFontScale(st.FontSizeBase * st.FontScaleMain * st.FontScaleDpi, ImGui::GetMainViewport()->Size.y);
}

}  // namespace vj
