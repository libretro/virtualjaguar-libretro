// Linux and any other OS: nothing OS-specific beyond what SDL already does (the Wayland app_id /
// X11 WM_CLASS come from SDL_SetAppMetadata, the screensaver / idle inhibit from SDL_DisableScreenSaver,
// which goes through the freedesktop portal inside Flatpak).  The desktop file, the mime type and the
// icon theme are packaging (app/packaging/linux).
#include <cstdio>

#include "platform/platform_os.h"

namespace vj {
namespace platform {
namespace os {

void initNetworking() {}
void beforeWindow() {}
void windowCreated(SDL_Window*) {}
void setDarkChrome(bool) {}
bool installMenus() { return false; }
void renderMenus(const MenuBar&, const std::vector<MenuItem>&) {}
void noteRecent(const std::string&) {}
void clearRecents() {}
void setActivity(bool) {}
bool revealFile(const std::string&) { return false; }
const char* fileManagerName() { return "File Manager"; }
int dumpMenus() {
    std::printf("native-menu: SKIP no native menu bar on this OS (the in-window menu is used)\n");
    return 77;
}
void shutdown() {}

}  // namespace os
}  // namespace platform
}  // namespace vj
