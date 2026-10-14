// System tray (Windows, Linux) / menu-bar extra (macOS) through SDL_tray.h.  Off unless the user
// turns it on in Settings.  The menu is the model from commands.h (buildTrayMenu); a click posts a
// command event, exactly like the macOS menu bar, so nothing runs inside SDL's tray callback.
#include <SDL3/SDL.h>

#include <cstdint>
#include <cstdio>
#include <vector>

#include "platform/platform.h"

namespace vj {
namespace platform {
namespace tray {

namespace {

const unsigned char kIcon[] = {
#include "platform/tray_icon.inc"
};

SDL_Tray* g_tray = nullptr;
SDL_Surface* g_icon = nullptr;
SDL_Window* g_window = nullptr;

void SDLCALL onClick(void* ud, SDL_TrayEntry* /*entry*/) {
    // A tray callback can run from deep inside SDL's pump (or another thread on some desktops):
    // post the command, never act here.
    const intptr_t v = (intptr_t)ud;
    postCommand((Cmd)(int)(v >> 16), (int)(v & 0xFFFF));
}

void clearMenu(SDL_TrayMenu* menu) {
    int n = 0;
    const SDL_TrayEntry** e = SDL_GetTrayEntries(menu, &n);
    if (!e || n <= 0) return;
    std::vector<SDL_TrayEntry*> all;
    for (int i = 0; i < n; i++) all.push_back(const_cast<SDL_TrayEntry*>(e[i]));
    for (size_t i = all.size(); i-- > 0;) SDL_RemoveTrayEntry(all[i]);
}

void fill(SDL_TrayMenu* menu, const std::vector<MenuItem>& items) {
    for (const MenuItem& m : items) {
        if (m.separator) {
            SDL_InsertTrayEntryAt(menu, -1, nullptr, 0);
            continue;
        }
        SDL_TrayEntryFlags flags = m.submenu ? SDL_TRAYENTRY_SUBMENU : (m.checked ? SDL_TRAYENTRY_CHECKBOX : SDL_TRAYENTRY_BUTTON);
        if (!m.enabled) flags |= SDL_TRAYENTRY_DISABLED;
        if (m.checked && !m.submenu) flags |= SDL_TRAYENTRY_CHECKED;
        SDL_TrayEntry* ent = SDL_InsertTrayEntryAt(menu, -1, m.title.c_str(), flags);
        if (!ent) continue;
        if (m.submenu) {
            if (SDL_TrayMenu* sub = SDL_CreateTraySubmenu(ent)) fill(sub, m.sub);
        } else {
            SDL_SetTrayEntryCallback(ent, onClick, (void*)(((intptr_t)(int)m.cmd << 16) | (intptr_t)(m.arg & 0xFFFF)));
        }
    }
}

}  // namespace

bool active() { return g_tray != nullptr; }

bool enable(SDL_Window* w) {
    if (g_tray) return true;
    g_window = w;
    if (!g_icon) {
        SDL_IOStream* io = SDL_IOFromConstMem(kIcon, sizeof kIcon);
        if (io) g_icon = SDL_LoadPNG_IO(io, true);
    }
    g_tray = SDL_CreateTray(g_icon, "Virtual Jaguar");
    if (!g_tray) {
        // Linux desktops without a StatusNotifier host (stock GNOME) land here.
        std::fprintf(stderr, "tray: not available (%s)\n", SDL_GetError());
        return false;
    }
    if (!SDL_CreateTrayMenu(g_tray)) {
        std::fprintf(stderr, "tray: no menu (%s)\n", SDL_GetError());
        SDL_DestroyTray(g_tray);
        g_tray = nullptr;
        return false;
    }
    return true;
}

void disable() {
    if (g_tray) {
        SDL_DestroyTray(g_tray);
        g_tray = nullptr;
    }
    if (g_icon) {
        SDL_DestroySurface(g_icon);
        g_icon = nullptr;
    }
}

void render(const std::vector<MenuItem>& items) {
    if (!g_tray) return;
    SDL_TrayMenu* menu = SDL_GetTrayMenu(g_tray);
    if (!menu) return;
    clearMenu(menu);
    fill(menu, items);
}

}  // namespace tray
}  // namespace platform
}  // namespace vj
