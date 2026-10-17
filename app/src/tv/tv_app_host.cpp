#include "tv/tv_app_host.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cctype>
#include <cstdio>

#include "app.h"

namespace vj::tv {

double AppHost::now() const { return app_.now(); }
bool AppHost::gameLoaded() const { return app_.host.isLoaded(); }
std::string AppHost::gameTitle() const { return app_.host.contentName(); }
bool AppHost::menuPause(std::string& why) { return app_.menuPause(why); }
void AppHost::menuResume() { app_.menuResume(); }
bool AppHost::netplayActive() const { return app_.netplay.configured(); }
// With a game running: what the achievements policy says.  With none, hardcoreActive() is always false (no game is being
// looked up), so ask whether hardcore would apply to the next one: a state load started from Home is refused then.
bool AppHost::canLoadState() const { return app_.host.isLoaded() ? app_.ach.allowStateLoad() : !app_.ach.hardcoreNextGame(); }

std::vector<SlotInfo> AppHost::slots() {
    std::vector<SlotInfo> v;
    for (int i = 0; i < 10; i++) {
        SlotInfo s{i, app_.slotExists(i), 0, 0, 0, 0};
        if (s.exists) {
            s.time = (int64_t)app_.slotTime(i);
            if (const SlotThumb* t = app_.slotThumb(i)) {   // null headless (no GL) or without a screenshot
                s.tex = t->tex;
                s.w = t->w;
                s.h = t->h;
            }
        }
        v.push_back(s);
    }
    return v;
}

bool AppHost::saveState(int slot) {
    if (!app_.saveState(slot)) return false;
    app_.stateSlot = slot;   // F5 / F7 then act on the slot just used
    return true;
}

bool AppHost::loadState(int slot) {
    if (!app_.loadState(slot)) return false;
    app_.stateSlot = slot;
    return true;
}

void AppHost::reset() { app_.reset(); }
void AppHost::closeGame() { app_.closeContent(); }
void AppHost::showControlsOverlay() {
    ControlsOverlayState& c = app_.input.controls;
    if (!c.show) overlayEvent(c, OverlayEvent::Toggle);   // Toggle flips: only when hidden
    c.autoShown = false;   // asked for by the player: it stays until they hide it
    c.hideAt = 0;
}
void AppHost::armReleaseGuard() { app_.input.armReleaseGuard(); }
void AppHost::clearHolds() { app_.input.clearHolds(); }
void AppHost::toast(const std::string& text) { app_.toast(text, 2.5, 2); }

bool AppHost::tvActive() const { return app_.tvMode.active(); }
bool AppHost::tvAvailable() const { return effectiveSetting(app_.tvSetting) != TvSetting::Off; }

void AppHost::setTvActive(bool on) {
    if (on != app_.tvMode.active()) app_.toggleTvMode();   // a no-op when the setting is Off
}

// The wall clock for the top bar, in the form the OS prefers (12 h "8:42 PM", 24 h "20:42").
std::string AppHost::clockText() const {
    SDL_Time t = 0;
    SDL_DateTime dt;
    if (!SDL_GetCurrentTime(&t) || !SDL_TimeToDateTime(t, &dt, true)) return "";
    SDL_DateFormat df;
    SDL_TimeFormat tf = SDL_TIME_FORMAT_24HR;
    SDL_GetDateTimeLocalePreferences(&df, &tf);
    char b[24];
    if (tf == SDL_TIME_FORMAT_12HR) std::snprintf(b, sizeof b, "%d:%02d %s", dt.hour % 12 == 0 ? 12 : dt.hour % 12, dt.minute, dt.hour < 12 ? "AM" : "PM");
    else std::snprintf(b, sizeof b, "%02d:%02d", dt.hour, dt.minute);
    return b;
}

std::string AppHost::raStatus() const {
    if (!app_.ach.signedIn()) return "";
    return "RetroAchievements: " + app_.ach.userName() + (app_.ach.hardcoreWanted() ? " \xC2\xB7 Hardcore" : "");
}

bool AppHost::desktopWindowOpen() const { return app_.desktopWindowOpen(); }
bool AppHost::libraryScanning() const { return app_.library.scanning(); }

// ---- the library -----------------------------------------------------------------------------------------------------

namespace {

GameRef gameRef(const lib::Entry& e, int index) {
    GameRef g;
    g.key = e.file.path;
    g.title = e.title;
    g.region = e.region;
    g.disc = e.section == lib::Section::Disc;
    g.index = index;
    return g;
}

Art artOf(const ArtView& v) {
    Art a;
    if (v.state == ArtView::Ready) {
        a.tex = v.tex;
        a.w = v.w;
        a.h = v.h;
    }
    a.loading = v.state == ArtView::Loading;
    return a;
}

}  // namespace

// The entry a GameRef stands for now: the position it carried when it is still right, else a scan by path (a rescan
// moves entries).  Null when the game is gone.
const lib::Entry* AppHost::entryFor(const GameRef& g) const {
    const std::vector<lib::Entry>& all = app_.library.entries();
    if (g.index >= 0 && g.index < (int)all.size() && all[(size_t)g.index].file.path == g.key) return &all[(size_t)g.index];
    for (const lib::Entry& e : all)
        if (e.file.path == g.key) return &e;
    return nullptr;
}

std::vector<GameRef> AppHost::games(List l) {
    std::vector<GameRef> out;
    const std::vector<lib::Entry>& all = app_.library.entries();
    if (l == List::Favorites || l == List::Recent) {   // already in their order: favorites by title, recents newest first
        for (const lib::Resolved& r : l == List::Favorites ? app_.library.favoriteEntries() : app_.library.recentEntries()) {
            if (!r.entry || r.entry->section == lib::Section::Bios) continue;
            out.push_back(gameRef(*r.entry, (int)(r.entry - all.data())));
        }
        return out;
    }
    // entries() is already sorted by case-insensitive title (LibraryService::reidentify), so a filter keeps the order.
    for (size_t i = 0; i < all.size(); i++) {
        const lib::Entry& e = all[i];
        if (e.section == lib::Section::Bios || (l == List::Disc && e.section != lib::Section::Disc)) continue;
        out.push_back(gameRef(e, (int)i));
    }
    return out;
}

Art AppHost::art(const GameRef& g) {
    const lib::Entry* e = entryFor(g);
    return e ? artOf(app_.library.art(*e)) : Art();
}

std::vector<StateRef> AppHost::states(const GameRef& g) {
    std::vector<StateRef> out;
    const lib::Entry* e = entryFor(g);
    const lib::StateGame* sg = e ? app_.library.statesFor(*e) : nullptr;
    if (sg)
        for (const lib::StateInfo& s : sg->states) out.push_back({s.slot, s.mtime});
    return out;
}

Art AppHost::stateThumb(const GameRef& g, int slot) {
    const lib::Entry* e = entryFor(g);
    const lib::StateGame* sg = e ? app_.library.statesFor(*e) : nullptr;
    if (sg)
        for (const lib::StateInfo& s : sg->states)
            if (s.slot == slot) return artOf(app_.library.stateThumb(s));
    return Art();
}

void AppHost::launch(const GameRef& g) {
    if (const lib::Entry* e = entryFor(g)) app_.library.launch(*e, /*fresh=*/true);   // the resume prompt needs a mouse: X / Details resume
}

void AppHost::launchState(const GameRef& g, int slot) {
    if (const lib::Entry* e = entryFor(g)) app_.library.launchState(*e, slot);
}

bool AppHost::isFavorite(const GameRef& g) {
    const lib::Entry* e = entryFor(g);
    return e && app_.library.isFavorite(*e);
}

void AppHost::setFavorite(const GameRef& g, bool on) {
    if (const lib::Entry* e = entryFor(g)) app_.library.setFavorite(*e, on);
}

void AppHost::noteInput(const SDL_Event& e) {
    switch (e.type) {
    case SDL_EVENT_GAMEPAD_BUTTON_DOWN:
        lastWasPad_ = true;
        lastPad_ = e.gbutton.which;
        break;
    case SDL_EVENT_GAMEPAD_AXIS_MOTION:   // a resting stick drifts: only a real push counts
        if (e.gaxis.value > 16000 || e.gaxis.value < -16000) {
            lastWasPad_ = true;
            lastPad_ = e.gaxis.which;
        }
        break;
    case SDL_EVENT_KEY_DOWN:
        lastWasPad_ = false;
        break;
    default: break;
    }
}

// The button caption for a hint: the face buttons as the pad used last labels them (A / Cross / ...), the keyboard's
// keys while the keyboard was used last (or before any input).
std::string AppHost::glyph(Intent i) const {
    SDL_GamepadButton b = SDL_GAMEPAD_BUTTON_INVALID;
    const char* key = "";
    switch (i) {
    case Intent::Up: b = SDL_GAMEPAD_BUTTON_DPAD_UP; key = "Up"; break;
    case Intent::Down: b = SDL_GAMEPAD_BUTTON_DPAD_DOWN; key = "Down"; break;
    case Intent::Left: b = SDL_GAMEPAD_BUTTON_DPAD_LEFT; key = "Left"; break;
    case Intent::Right: b = SDL_GAMEPAD_BUTTON_DPAD_RIGHT; key = "Right"; break;
    case Intent::Accept: b = SDL_GAMEPAD_BUTTON_SOUTH; key = "Enter"; break;
    case Intent::Back: b = SDL_GAMEPAD_BUTTON_EAST; key = "Esc"; break;
    case Intent::Alt: b = SDL_GAMEPAD_BUTTON_WEST; break;   // Alt and Aux likewise have no key
    case Intent::Aux: b = SDL_GAMEPAD_BUTTON_NORTH; break;
    case Intent::Menu: b = SDL_GAMEPAD_BUTTON_START; break;   // no keyboard key (intentForKey): "" so the hint is dropped
    case Intent::SectionPrev: b = SDL_GAMEPAD_BUTTON_LEFT_SHOULDER; key = "Shift+Tab"; break;
    case Intent::SectionNext: b = SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER; key = "Tab"; break;
    case Intent::PagePrev: key = "Page Up"; break;
    case Intent::PageNext: key = "Page Down"; break;
    case Intent::None: break;
    }
    SDL_Gamepad* g = lastWasPad_ ? SDL_GetGamepadFromID(lastPad_) : nullptr;   // null once that pad is gone
    if (g && (i == Intent::PagePrev || i == Intent::PageNext))   // the triggers (axes, not buttons)
        return triggerLabel(i, SDL_GetGamepadButtonLabel(g, SDL_GAMEPAD_BUTTON_SOUTH) == SDL_GAMEPAD_BUTTON_LABEL_CROSS);
    if (g && b != SDL_GAMEPAD_BUTTON_INVALID) {
        const bool sony = SDL_GetGamepadButtonLabel(g, SDL_GAMEPAD_BUTTON_SOUTH) == SDL_GAMEPAD_BUTTON_LABEL_CROSS;
        switch (b) {
        case SDL_GAMEPAD_BUTTON_LEFT_SHOULDER: return sony ? "L1" : "LB";
        case SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER: return sony ? "R1" : "RB";
        case SDL_GAMEPAD_BUTTON_START: return "Start";
        case SDL_GAMEPAD_BUTTON_DPAD_UP:
        case SDL_GAMEPAD_BUTTON_DPAD_DOWN:
        case SDL_GAMEPAD_BUTTON_DPAD_LEFT:
        case SDL_GAMEPAD_BUTTON_DPAD_RIGHT: return "D-pad";
        default: break;
        }
        switch (SDL_GetGamepadButtonLabel(g, b)) {
        case SDL_GAMEPAD_BUTTON_LABEL_A: return "A";
        case SDL_GAMEPAD_BUTTON_LABEL_B: return "B";
        case SDL_GAMEPAD_BUTTON_LABEL_X: return "X";
        case SDL_GAMEPAD_BUTTON_LABEL_Y: return "Y";
        case SDL_GAMEPAD_BUTTON_LABEL_CROSS: return "Cross";
        case SDL_GAMEPAD_BUTTON_LABEL_CIRCLE: return "Circle";
        case SDL_GAMEPAD_BUTTON_LABEL_SQUARE: return "Square";
        case SDL_GAMEPAD_BUTTON_LABEL_TRIANGLE: return "Triangle";
        default: break;
        }
    }
    return key;
}

}  // namespace vj::tv
