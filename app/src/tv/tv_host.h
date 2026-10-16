// TV mode (#991): everything a TV screen may touch of the app.  The shell and its screens talk to this interface
// only (App implements it in tv_app_host.cpp; tests use a fake), so they never reach into the core or the other
// services.  Pure interface: no SDL handles, no ImGui.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "tv/tv_nav.h"

namespace vj::tv {

// One save-state slot, as the slot picker shows it.  `tex` is an OpenGL texture owned by the app (0 = none) and
// is only good for the frame it was fetched in: a save or a reload replaces it, so fetch slots() again each draw.
struct SlotInfo {
    int slot;
    bool exists;
    int64_t time;   // SDL_Time (ns since the epoch) of the state file; 0 when it does not exist
    unsigned tex;
    int w, h;
};

class Host {
public:
    virtual ~Host() = default;
    // SECONDS (App::now(), a steady clock): the unit the tweens run in.  Not the clock of the shell's input, which is
    // SDL_GetTicks() MILLISECONDS (Shell::handleEvent / frame, NavInput, the hold arbiter); the two never mix.
    virtual double now() const = 0;
    virtual bool gameLoaded() const = 0;
    virtual std::string gameTitle() const = 0;
    virtual bool menuPause(std::string& why) = 0;    // false = refused (hardcore rationing); netplay: true, not paused
    virtual void menuResume() = 0;                   // restores the pause state from before the menu
    virtual bool netplayActive() const = 0;          // netplay.configured()
    virtual bool canLoadState() const = 0;           // ach.allowStateLoad()
    virtual std::vector<SlotInfo> slots() = 0;       // 0..9, thumbnails from App::slotThumb
    virtual bool saveState(int slot) = 0;
    virtual bool loadState(int slot) = 0;
    virtual void reset() = 0;
    virtual void closeGame() = 0;
    virtual void toggleControlsOverlay() = 0;
    virtual void armReleaseGuard() = 0;
    virtual void clearHolds() = 0;
    virtual void toast(const std::string& text) = 0;
    virtual std::string glyph(Intent i) const = 0;   // "A", "Cross", "Enter": SDL_GetGamepadButtonLabel on the last-used pad
};

}  // namespace vj::tv
