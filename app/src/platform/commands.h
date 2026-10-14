// Native-OS integration, the part with no OS and no SDL in it (#833).
//
// Every native surface -- the macOS menu bar, the Dock menu, the system tray / menu-bar extra --
// is rendered from ONE menu model built here from a snapshot of the app's state, and every click
// on any of them becomes a Cmd that dispatch() hands to the app.  That keeps three things true:
//
//   * the macOS menu bar, the tray and the Dock menu cannot drift apart (one builder);
//   * a click never calls into the app from inside a Cocoa menu-tracking loop or a tray
//     callback: it is posted as an SDL user event (platform.h) and runs in the main loop;
//   * all of it is unit-testable headless on every CI leg (tests/platform_test.cpp).
//
// Also here: the file-association table (the extension list the Info.plist, the Windows
// installer, the .desktop file and the shared-mime-info XML must all agree on -- a CTest
// script checks that), the recent-games list conversion, and the launcher-argument filter.
#pragma once

#include <string>
#include <vector>

namespace vj {
namespace platform {

// ---- commands ----------------------------------------------------------------------------
enum class Cmd : int {
    None = 0,
    // File
    Open, OpenRecent /*arg = index into the recent list*/, ClearRecents, BootNoCart, NewWindow, Library,
    CloseGame, RevealGame, Quit,
    // Emulation
    Pause, Reset, FastForward, SaveState, LoadState, SetSlot /*arg = 0..9*/, StateManager, Cheats,
    DiscTray, DiscSelect /*arg = disc index*/,
    // View
    Fullscreen, IntegerScale, SmoothFilter, Shader, ShowFps, KeypadOverlay, ControlsOverlay,
    // Settings / help
    Settings, Mute, About, ShowDataFolder, Website, ReportIssue,
    // Online
    AchSignIn, AchSignOut, AchEnable, AchHardcore, AchUnofficial, AchList, Netplay, NetplayDisconnect,
    // Tray
    ShowWindow,
    Count_  // keep last
};

// ---- the state a menu is drawn from ------------------------------------------------------
struct DiscState {
    bool available = false, ejected = false;
    unsigned count = 0, index = 0;
    std::vector<std::string> labels;
    bool operator==(const DiscState& o) const {
        return available == o.available && ejected == o.ejected && count == o.count && index == o.index &&
               labels == o.labels;
    }
};

struct MenuState {
    bool gameLoaded = false, paused = false, fastForward = false, fullscreen = false;
    bool canLoadState = false;     // a state exists in the current slot
    int stateSlot = 0;
    unsigned usedSlots = 0;        // bit n: slot n has a state
    bool mute = false, integerScale = false, smoothFilter = false, shader = false, showFps = false;
    bool keypadOverlay = false, controlsOverlay = false;
    bool libraryOpen = false;      // the library window (or the home screen) is what is showing
    DiscState disc;
    std::vector<std::string> recents;   // full paths, newest first
    // RetroAchievements (hidden unless available)
    bool achAvailable = false, achSignedIn = false, achBusy = false, achEnabled = false, achHardcore = false,
         achUnofficial = false;
    bool netplayConfigured = false;
    std::string fileManager = "File Manager";   // "Finder" / "Explorer", for "Show in ..."
    bool operator==(const MenuState& o) const;
    bool operator!=(const MenuState& o) const { return !(*this == o); }
};

// ---- the menu model ------------------------------------------------------------------------
// `key` is "cmd+shift+s"-style: modifiers cmd (the platform's primary: Cmd on macOS), shift, alt,
// ctrl, then one key name (a letter, a digit, "," or "f5").  Only the macOS renderer shows it.
struct MenuItem {
    Cmd cmd = Cmd::None;
    int arg = 0;
    std::string title;
    std::string key;
    bool enabled = true, checked = false, separator = false, submenu = false;
    std::vector<MenuItem> sub;
};

struct MenuBar {
    std::vector<MenuItem> app;     // the application menu (macOS "Virtual Jaguar"): About, Settings, Quit
    std::vector<MenuItem> menus;   // top-level menus: File, Emulation, View, ... Window, Help
};

MenuBar buildMenuBar(const MenuState& s);
std::vector<MenuItem> buildTrayMenu(const MenuState& s);   // tray / menu-bar extra
std::vector<MenuItem> buildDockMenu(const MenuState& s);   // macOS Dock icon menu

// Every Cmd that appears (non-separator, non-submenu) anywhere in `items`, depth first.
void collectCommands(const std::vector<MenuItem>& items, std::vector<Cmd>& out);
// The text "Pause"/"Resume" etc., exposed so the ImGui menu and the native ones agree.
const char* pauseTitle(bool paused);

// ---- dispatch -------------------------------------------------------------------------------
// What the app must be able to do.  The app implements this (desktop.cpp); the test records.
struct Actions {
    virtual ~Actions() {}
    virtual void open() = 0;
    virtual void openRecent(int index) = 0;
    virtual void clearRecents() = 0;
    virtual void bootNoCart() = 0;
    virtual void newWindow() = 0;
    virtual void toggleLibrary() = 0;
    virtual void closeGame() = 0;
    virtual void revealGame() = 0;
    virtual void quit() = 0;
    virtual void togglePause() = 0;
    virtual void reset() = 0;
    virtual void toggleFastForward() = 0;
    virtual void saveState() = 0;
    virtual void loadState() = 0;
    virtual void setSlot(int slot) = 0;
    virtual void showStateManager() = 0;
    virtual void showCheats() = 0;
    virtual void toggleDiscTray() = 0;
    virtual void selectDisc(int index) = 0;
    virtual void toggleFullscreen() = 0;
    virtual void toggleIntegerScale() = 0;
    virtual void toggleSmoothFilter() = 0;
    virtual void toggleShader() = 0;
    virtual void toggleFps() = 0;
    virtual void toggleKeypadOverlay() = 0;
    virtual void toggleControlsOverlay() = 0;
    virtual void showSettings() = 0;
    virtual void toggleMute() = 0;
    virtual void showAbout() = 0;
    virtual void showDataFolder() = 0;
    virtual void openUrl(const char* url) = 0;
    virtual void achSignIn() = 0;
    virtual void achSignOut() = 0;
    virtual void achToggleEnabled() = 0;
    virtual void achToggleHardcore() = 0;
    virtual void achToggleUnofficial() = 0;
    virtual void achShowList() = 0;
    virtual void showNetplay() = 0;
    virtual void netplayDisconnect() = 0;
    virtual void showWindow() = 0;
};

// Out-of-range arguments (a stale menu item after the recent list shrank) are dropped here, once.
// `recentCount` and `discCount` are the app's current sizes.  Returns false for Cmd::None / unknown.
bool dispatch(Cmd cmd, int arg, Actions& a, int recentCount, int discCount);

constexpr const char* kWebsiteUrl = "https://jaguar.provenance-emu.com";
constexpr const char* kIssuesUrl = "https://github.com/libretro/virtualjaguar-libretro/issues";

// ---- recent games -----------------------------------------------------------------------------
struct RecentEntry {
    std::string path;
    std::string title;   // file stem; "stem (folder)" when two entries share a stem
};
// The first `max` paths, empties and exact duplicates dropped, titled for a native menu.
std::vector<RecentEntry> nativeRecentList(const std::vector<std::string>& recents, size_t max = 10);

// ---- file associations ----------------------------------------------------------------------------
// The extensions the app registers itself for.  The core opens more (bin, prg): those are too
// generic to claim, so they only work through File > Open / drag and drop.  KEEP IN SYNC with
// the Info.plist, the Windows installer, the .desktop file and the mime XML: the CTest
// `platform_assoc_sync` fails when they drift.
//
// VJ_ASSOC_LIST: X(ext, group) -- group "game" = cartridge / executable, "disc" = CD image.
#define VJ_ASSOC_LIST(X) \
    X(j64, game) X(jag, game) X(rom, game) X(abs, game) X(cof, game) \
    X(cue, disc) X(chd, disc) X(cdi, disc)

struct Assoc {
    const char* ext;     // lower case, no dot
    const char* group;   // "game" | "disc"
};
const std::vector<Assoc>& associations();
bool isAssociatedExtension(const std::string& ext);   // case-insensitive, leading dot optional

// ---- launcher arguments -------------------------------------------------------------------------------
// Arguments a launcher adds that are not ours: macOS "-psn_0_12345" (older Finder launches) and
// "-NSDocumentRevisionsDebugMode" style Cocoa flags with their value.  main() skips these instead of
// rejecting them as unknown options.  Returns how many argv entries to skip (0 = not ignorable).
int ignorableLauncherArgs(int argc, char** argv, int i);

}  // namespace platform
}  // namespace vj
