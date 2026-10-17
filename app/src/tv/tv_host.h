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

// A picture the app owns: `tex` is an OpenGL texture (0 = draw the placeholder), good for the frame it was fetched in.
struct Art {
    unsigned tex = 0;
    int w = 0, h = 0;
    bool loading = false;
};

// A game as the TV screens see it, by value: nothing here points into the library, so a rescan cannot leave a screen
// holding a dead entry.  Every action is keyed by `key` (the file path, stable across rescans).
struct GameRef {
    std::string key;
    std::string title, region;
    bool disc = false;
    int index = -1;   // the app's position in library.entries(): a hint for finding the entry again, good for the frame it was fetched in only
};

// A save state of a game: slot -1 is the auto-save; `time` is seconds since the epoch.  Newest first.
struct StateRef {
    int slot;
    int64_t time;
};

enum class List { All, Favorites, Recent, Disc };

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
    virtual void showControlsOverlay() = 0;   // show (never hide) the controls cheat sheet
    virtual void armReleaseGuard() = 0;
    virtual void clearHolds() = 0;
    virtual void toast(const std::string& text) = 0;
    virtual std::string glyph(Intent i) const = 0;   // "A", "Cross", "Enter": SDL_GetGamepadButtonLabel on the last-used pad

    // ---- TV mode itself (phase 2) ----------------------------------------------------------------------------------
    virtual bool tvActive() const = 0;
    virtual bool tvAvailable() const = 0;          // the setting is not Off
    virtual void setTvActive(bool on) = 0;         // through App::toggleTvMode's path; no-op when Off
    virtual std::string clockText() const = 0;     // "8:42 PM" or "20:42" (SDL_GetDateTimeLocalePreferences)
    virtual std::string raStatus() const = 0;      // "" unless signed in; "RetroAchievements: <user>", + " - Hardcore" when wanted (a middle dot in the text)

    // ---- the library (phase 2) -------------------------------------------------------------------------------------
    // All / Disc / Favorites: by title, case-insensitive; Recent: newest first.  Never BIOS.  Fetched fresh every frame.
    virtual std::vector<GameRef> games(List l) = 0;
    virtual Art art(const GameRef& g) = 0;                       // starts the load on first use; tex 0 until it is ready
    virtual std::vector<StateRef> states(const GameRef& g) = 0;
    virtual Art stateThumb(const GameRef& g, int slot) = 0;
    virtual void launch(const GameRef& g) = 0;                   // opens the game (an event: the A that did it is the caller's to guard)
    virtual void launchState(const GameRef& g, int slot) = 0;    // opens the game and loads that state
    virtual bool isFavorite(const GameRef& g) = 0;               // library.isFavorite(entry); false when the game is gone
    virtual void setFavorite(const GameRef& g, bool on) = 0;     // library.setFavorite(entry, on); a no-op when the game is gone
};

}  // namespace vj::tv
