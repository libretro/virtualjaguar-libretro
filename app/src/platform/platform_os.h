// The per-OS half of platform.h.  Exactly one of platform_macos.mm, platform_windows.cpp or
// platform_other.cpp defines these.  Internal: only platform.cpp, tray.cpp and the three OS files include it.
#pragma once

#include <SDL3/SDL.h>

#include <string>
#include <vector>

#include "platform/commands.h"

namespace vj {
namespace platform {
namespace os {

void beforeWindow();
void windowCreated(SDL_Window* w);
bool installMenus();                       // true: a native menu bar is now in charge
void renderMenus(const MenuBar& bar, const std::vector<MenuItem>& dock);
void noteRecent(const std::string& path);
void clearRecents();
void setActivity(bool running);            // macOS App Nap
bool revealFile(const std::string& path);  // false: not supported, caller opens the folder
const char* fileManagerName();
int dumpMenus();                           // see platform.h dumpNativeMenu()
void shutdown();

}  // namespace os
}  // namespace platform
}  // namespace vj
