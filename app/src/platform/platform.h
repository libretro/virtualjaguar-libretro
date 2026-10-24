// Native-OS integration (#833): the one interface the rest of the app talks to.  Everything that
// is specific to macOS, Windows or Linux lives behind this and behind platform_os.h; on a platform
// with nothing to add every function is a harmless no-op, so the other OSes compile unchanged.
//
//   commands.h      the menu model + command dispatch (no OS, unit-tested)
//   platform.cpp    shared glue: command events, state caching, screensaver, tray hand-off
//   tray.cpp        system tray / macOS menu-bar extra (SDL_tray.h; all three OSes)
//   platform_macos.mm / platform_windows.cpp / platform_other.cpp   the per-OS parts (platform_os.h)
//
// All of this runs on the main thread, except postCommand(), which is safe from any thread.
#pragma once

#include <SDL3/SDL.h>

#include <string>

#include "platform/commands.h"

namespace vj {
namespace platform {

// The SDL_EVENT_USER code of a native-menu / tray click.  data1 = Cmd, data2 = arg (both as intptr_t).
constexpr Sint32 kCommandEventCode = 0x564A4E43;   // 'VJNC'

void postCommand(Cmd cmd, int arg = 0);
bool decodeCommandEvent(const SDL_Event& e, Cmd& cmd, int& arg);

// ---- lifecycle ----------------------------------------------------------------------------------
// Winsock is per process and the core never starts it (RetroArch does, as the host): without it every socket() the core
// makes fails on Windows, the GDB stub ("failed to open port") included.  Call once, before the first core load, headless
// runs too.  A no-op elsewhere.  Reference counted by Windows, so another WSAStartup (neuro, netplay) is harmless.
void initNetworking();
void beforeWindow();                    // after SDL_Init, before the window: process identity (Windows AppUserModelID)
void windowCreated(SDL_Window* w);      // dark title bar (Windows), native menu bar + Dock menu (macOS)
// The window frame follows the app theme (Windows dark title bar).  Call after windowCreated() and whenever the
// theme changes (theme::addListener).  No-op where the OS draws the frame from its own appearance (macOS, Linux).
void setDarkChrome(bool dark);

void shutdown();

// True when a native menu bar replaced the in-window one (macOS): the ImGui menu bar is then hidden.
bool nativeMenuActive();

// Re-renders the native menus and the tray when `s` differs from the last call (cheap otherwise).
void updateState(const MenuState& s);

// ---- tray / menu-bar extra (off by default; Settings > Interface) ------------------------------------
// Returns false (and leaves it off) when the desktop has no tray to show it in.
bool setTray(bool on, SDL_Window* w);
bool trayActive();

// ---- while a game runs ---------------------------------------------------------------------------------
// `running` = a game is loaded and not paused.  Edge-triggered: call it every frame.  Holds the
// display awake (SDL_DisableScreenSaver) when `keepAwake`, and on macOS keeps App Nap off.
void setGameRunning(bool running, bool keepAwake);

// ---- recent documents / file managers ---------------------------------------------------------------------
void noteRecent(const std::string& path);   // OS recent-documents list (NSDocumentController, Windows Recent)
void clearRecents();
void revealFile(const std::string& path);   // "Show in Finder / Explorer"; falls back to opening the folder
void openFolder(const std::string& dir);
const char* fileManagerName();              // "Finder" | "Explorer" | "File Manager"

// ---- display power ---------------------------------------------------------------------------------------------
// True while the display the window is on is asleep (macOS only; false everywhere else).  A vsync swap on macOS waits
// for a display-link tick that a sleeping display never sends (swap_guard.h, #1081).  Main thread.
bool displayAsleep(SDL_Window* w);

// Dev / test: "virtualjaguar --dump-native-menu".  Prints the real NSMenu tree and checks it.
// Returns 0 ok, 1 broken, 77 skip (no native menu on this OS, or no window server).
int dumpNativeMenu();

}  // namespace platform
}  // namespace vj
