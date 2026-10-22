// TV mode (#991): everything a TV screen may touch of the app.  The shell and its screens talk to this interface
// only (App implements it in tv_app_host.cpp; tests use a fake), so they never reach into the core or the other
// services.  Pure interface: no SDL handles (SDL_gamepad.h is included for its enums only: button / axis / type
// numbers), no ImGui.
#pragma once

#include <SDL3/SDL_gamepad.h>

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
    std::string disc{}; // playlist states retain their disc even when two discs use the same slot
};

enum class List { All, Favorites, Recent, Disc };

// One row of the TV Settings screen, by value (AppHost fills them from the app's settings and the core's options, so the
// screen never sees either).  `choices` are what Left / Right / Accept step through; "more" has none.
struct SettingRow {
    std::string id, label;             // ids: shader integer aspect resolution risc_clock idle_skip speed_note volume fps vsync theme tv_mode more
    std::vector<std::string> choices;  // empty for "more"
    int index = 0;
    bool readOnly = false;
    std::string note;                  // shown under the value: why it is read-only
    int group = 0;                     // 0 Picture, 1 Speed, 2 Sound, 3 Interface
};

// One achievement of the loaded game, or one active leaderboard tracker (kind Tracker: read-only, no status).  AppHost fills them
// from rc_client's rows, so the screen never sees rcheevos.
struct AchItem {
    enum Kind { Achievement, Tracker } kind = Achievement;
    uint32_t id = 0;
    std::string title, description, status;   // description: what to do, as the desktop's window shows it; status: the desktop's line ("Unlocked in hardcore  2026-10-16 21:04", "Locked  Progress: 5/10 ...")
    unsigned points = 0;
    bool unlocked = false;
    std::string badgeUrl;        // the one to show (earned or locked); "" = none
};

// One game of the RetroAchievements Jaguar list (only games with a published set).
struct AchGame {
    uint32_t id = 0;
    std::string title, imageUrl;
    unsigned achievements = 0, unlocked = 0;
    bool haveProgress = false, inLibrary = false;
};

// What the Achievements screen's header says.  mode: "Hardcore" | "Softcore" | "Softcore (hardcore is paused: <blocker>)".
struct AchHeader {
    bool signedIn = false, available = true, hasGame = false;   // available: this build has rc_client at all
    std::string title, mode, iconUrl;
    unsigned unlocked = 0, total = 0;
};

// One row of the Overlays screen: a window or a drawing the player can switch on and off.  ids: "fps", "keypad", "controls" and
// "tool:<DebugTool::id>" for each debug tool window.  A row that cannot be switched now (`enabled` false) says why in `note`.
struct ToggleRow {
    std::string id, label;
    bool on = false, enabled = true;
    std::string note;
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
    virtual void showControlsOverlay() = 0;   // show (never hide) the controls cheat sheet
    virtual void armReleaseGuard() = 0;
    virtual void clearHolds() = 0;
    virtual void toast(const std::string& text) = 0;
    virtual std::string glyph(Intent i) const = 0;   // "A", "Cross", "Enter": the last-used pad's profile binding, as that pad labels it

    // What a pad's button / trigger means in the menus (#1021): the pad's controller-family profile in the app, the Xbox
    // table here.  The shell asks per event, so a profile edited in the controls screen takes effect at once.
    // Search and AcceptLong come back from a profile but are not TV's: the shell neither feeds nor consumes them.
    virtual Intent padIntent(uint32_t pad, int sdlButton) const { return intentForButton(sdlButton); }
    virtual Intent padTriggerIntent(uint32_t pad, int sdlAxis) const {
        return sdlAxis == SDL_GAMEPAD_AXIS_LEFT_TRIGGER ? Intent::PagePrev : sdlAxis == SDL_GAMEPAD_AXIS_RIGHT_TRIGGER ? Intent::PageNext : Intent::None;
    }

    // ---- TV mode itself (phase 2) ----------------------------------------------------------------------------------
    virtual bool tvActive() const = 0;
    virtual bool tvAvailable() const = 0;          // the setting is not Off
    virtual void setTvActive(bool on) = 0;         // through App::toggleTvMode's path; no-op when Off
    virtual std::string clockText() const = 0;     // "8:42 PM" or "20:42" (SDL_GetDateTimeLocalePreferences)
    virtual std::string raStatus() const = 0;      // "" unless signed in; "RetroAchievements: <user>", + " - Hardcore" when wanted (a middle dot in the text)
    // A desktop window (Settings, the state manager, cheats, About, the desktop Game library, the controls overlay ...) or
    // a popup / menu is open over the TV screens.  The same set inputFrame treats as "a window is open".  The Base layer
    // takes no nav input then: a pad A must not reach a tile hidden behind Settings.
    virtual bool desktopWindowOpen() const = 0;

    // ---- the library (phase 2) -------------------------------------------------------------------------------------
    // All / Disc / Favorites: by title, case-insensitive; Recent: newest first.  Never BIOS.  Fetched fresh every frame.
    virtual bool libraryScanning() const = 0;                    // a scan is running (an empty library may still fill up)
    virtual std::vector<GameRef> games(List l) = 0;
    virtual bool hasGames() const = 0;                           // the library lists at least one game (never BIOS); no copy, cheap enough for a frame
    virtual Art art(const GameRef& g) = 0;                       // starts the load on first use; tex 0 until it is ready
    virtual std::vector<StateRef> states(const GameRef& g) = 0;
    virtual Art stateThumb(const GameRef& g, const StateRef& state) = 0;
    // Opens the game (an event: the A that did it is the caller's to guard).  A fresh start: the app does not offer the
    // "Resume where you left off?" prompt, which needs a mouse (X and Game details resume a state instead).
    virtual void launch(const GameRef& g) = 0;
    virtual void launchState(const GameRef& g, const StateRef& state) = 0;    // opens the game and loads that state
    virtual bool isFavorite(const GameRef& g) = 0;               // library.isFavorite(entry); false when the game is gone
    virtual void setFavorite(const GameRef& g, bool on) = 0;     // library.setFavorite(entry, on); a no-op when the game is gone

    // ---- Settings (phase 3) ----------------------------------------------------------------------------------------
    virtual std::vector<SettingRow> settingRows() = 0;             // only the rows that exist in this build / session; fetched every frame
    virtual void setSetting(const std::string& id, int index) = 0; // applies and saves, as the desktop control would
    virtual void openMoreSettings() = 0;                           // the desktop Settings window

    // ---- Achievements (phase 3) ------------------------------------------------------------------------------------
    virtual AchHeader achHeader() = 0;                      // fetched every frame
    virtual std::vector<AchItem> achItems() = 0;            // trackers first, then the rows in bucket order; empty with no tracked game
    virtual std::vector<AchGame> achGames() = 0;            // the Jaguar list, title order; must be called every frame the list shows (it loads and fetches)
    virtual Art achImage(const std::string& url) = 0;       // a badge or icon: tex only when ready; asked for only while it is on screen
    virtual void achPlay(uint32_t gameId) = 0;              // opens the library file that runs that game (an event: the caller guards)
    virtual uint32_t achGameId(const GameRef& g) = 0;       // the list entry whose library file is this game, else 0

    // ---- Overlays (phase 3) ----------------------------------------------------------------------------------------
    virtual std::vector<ToggleRow> overlayRows() = 0;       // fetched every frame the screen shows
    virtual void toggleOverlay(const std::string& id) = 0;  // flips that row (a no-op for an id that is not listed or not enabled)
};

}  // namespace vj::tv
