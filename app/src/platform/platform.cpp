// Shared glue for the native-OS integration (platform.h).
#include "platform/platform.h"

#include <cstdint>
#include <cstdio>
#include <cstdlib>

#include "platform/platform_os.h"

namespace vj {
namespace platform {

namespace {
bool g_menuActive = false;
bool g_haveState = false;
MenuState g_state;
bool g_awake = false, g_awakeKnown = false;
bool g_running = false, g_runningKnown = false;

void renderAll();
}  // namespace

// tray.cpp
namespace tray {
bool enable(SDL_Window* w);
void disable();
bool active();
void render(const std::vector<MenuItem>& items);
}  // namespace tray

void beforeWindow() { os::beforeWindow(); }

void windowCreated(SDL_Window* w) {
    // A screensaver that can start while nobody is playing is the native behaviour; SDL's default is
    // "never".  setGameRunning() turns it back off while a game runs.
    SDL_EnableScreenSaver();
    os::windowCreated(w);
    g_menuActive = os::installMenus();
    if (g_haveState) renderAll();
}

void shutdown() {
    tray::disable();
    os::shutdown();
    g_menuActive = false;
    g_haveState = false;
}

bool nativeMenuActive() { return g_menuActive; }

namespace {
void renderAll() {
    MenuState s = g_state;
    s.fileManager = fileManagerName();
    if (g_menuActive) os::renderMenus(buildMenuBar(s), buildDockMenu(s));
    if (tray::active()) tray::render(buildTrayMenu(s));
}
}  // namespace

void updateState(const MenuState& s) {
    if (g_haveState && s == g_state) return;
    g_state = s;
    g_haveState = true;
    renderAll();
}

bool setTray(bool on, SDL_Window* w) {
    if (!on) {
        tray::disable();
        return true;
    }
    if (tray::active()) return true;
    if (!tray::enable(w)) return false;
    if (g_haveState) renderAll();
    return true;
}

bool trayActive() { return tray::active(); }

void setGameRunning(bool running, bool keepAwake) {
    const bool awake = running && keepAwake;
    if (!g_awakeKnown || awake != g_awake) {
        g_awakeKnown = true;
        g_awake = awake;
        if (awake) SDL_DisableScreenSaver();
        else SDL_EnableScreenSaver();
    }
    if (!g_runningKnown || running != g_running) {
        g_runningKnown = true;
        g_running = running;
        os::setActivity(running);
    }
}

void noteRecent(const std::string& path) { os::noteRecent(path); }
void clearRecents() { os::clearRecents(); }

void openFolder(const std::string& dir) {
    if (dir.empty()) return;
    std::string url = dir;
#ifdef _WIN32
    for (char& c : url)
        if (c == '\\') c = '/';
    url = "file:///" + url;
#else
    url = "file://" + url;
#endif
    if (!SDL_OpenURL(url.c_str())) std::fprintf(stderr, "open folder failed: %s\n", SDL_GetError());
}

void revealFile(const std::string& path) {
    if (path.empty()) return;
    if (os::revealFile(path)) return;
    size_t cut = path.find_last_of("/\\");
    openFolder(cut == std::string::npos ? path : path.substr(0, cut + 1));
}

const char* fileManagerName() { return os::fileManagerName(); }

int dumpNativeMenu() { return os::dumpMenus(); }

}  // namespace platform
}  // namespace vj
