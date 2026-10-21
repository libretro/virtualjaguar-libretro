#include "tv/tv_app_host.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cctype>
#include <cstdio>

#include "app.h"
#include "aspect_choices.h"
#include "shaders_ui.h"
#include "theme.h"
#include "tv/tv_settings_model.h"

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

// ---- settings ----------------------------------------------------------------------------------------------------------

namespace {

// The three core options on the TV list: the row id, the option key, the label, the group.
struct CoreRow {
    const char* id;
    const char* key;
    const char* label;
    int group;
};
const CoreRow kResolution = {"resolution", "virtualjaguar_internal_resolution", "Internal resolution", 0};
const CoreRow kRiscClock = {"risc_clock", "virtualjaguar_risc_clock_scale", "RISC overclock", 1};
const CoreRow kIdleSkip = {"idle_skip", "virtualjaguar_risc_idle_skip", "Idle-loop skip", 1};

const OptionDef* defFor(const CoreHost& host, const char* key) {
    for (const OptionDef& d : host.optionDefs())
        if (d.key == key) return &d;
    return nullptr;
}

}  // namespace

// The custom themes the folder holds; read again once a second (Settings > Appearance does the same: a theme can be added by
// hand or by another copy of the app), not on every frame the screen draws.
const std::vector<std::string>& AppHost::themeNames() {
    if (app_.now() - themeListedAt_ > 1.0) {
        themeNames_.clear();
        for (const theme::Custom& c : app_.themeStore().list()) themeNames_.push_back(c.name);
        themeListedAt_ = app_.now();
    }
    return themeNames_;
}

// Only the rows that exist: no shader runtime, or no core option definitions yet (no game has run this session), or an
// option the app locks, means no row -- never a greyed one.
std::vector<SettingRow> AppHost::settingRows() {
    CoreHost& host = app_.host;
    ShaderChain& sc = app_.video.shader;
    std::vector<SettingRow> rows;
    auto core = [&](const CoreRow& cr) {
        const OptionDef* def = defFor(host, cr.key);
        bool hidden = false;
        SettingRow r = coreOptionRow(cr.id, cr.label, cr.group, def, def ? host.optionValue(*def) : std::string(), host.optionVisible(cr.key),
                                     host.optionLocked(cr.key), host.optionOverridden(cr.key), &hidden);
        if (!hidden) rows.push_back(std::move(r));
    };
    sc.probeLibrary();   // cached after the first call (the desktop Shader section probes the same way)
    if (ShaderChain::builtWithHeaders() && sc.libraryLoaded()) rows.push_back(shaderRow(sc.preset, sc.enabled, shaderPresetPaths(app_)));
    rows.push_back(onOffRow("integer", "Integer scaling", 0, app_.present.integerScale));
    rows.push_back(aspectRow(app_.present.aspectOverride, kAspectChoices, kAspectChoiceCount));
    core(kResolution);
    core(kRiscClock);
    core(kIdleSkip);
    rows.push_back(volumeRow(app_.volume));
    rows.push_back(onOffRow("fps", "Show FPS", 3, app_.showFps));
    rows.push_back(onOffRow("vsync", "VSync", 3, app_.vsync));
    rows.push_back(themeRow(theme::choiceKey(), themeNames()));
    rows.push_back(tvModeRow(app_.tvSetting));
    rows.push_back(moreRow());
    return rows;
}

// What the desktop control does for each row, then the config is saved.  An index the row cannot have (the "(custom)" entry
// of a typed option, a theme whose file is gone) changes nothing.
void AppHost::setSetting(const std::string& id, int index) {
    CoreHost& host = app_.host;
    if (id == "shader") {
        ShaderChain& sc = app_.video.shader;
        const std::vector<std::string> paths = shaderPresetPaths(app_);
        if (index == 0) {
            sc.enabled = false;   // the "Use shader" checkbox
            sc.retry();
        } else if (index >= 1 && index <= (int)paths.size()) {
            applyShaderPreset(app_, paths[(size_t)index - 1]);
        }
    } else if (id == "integer") {
        app_.present.integerScale = index != 0;
    } else if (id == "aspect") {
        if (index >= 0 && index < kAspectChoiceCount) app_.present.aspectOverride = kAspectChoices[index].value;
    } else if (id == "resolution" || id == "risc_clock" || id == "idle_skip") {
        const CoreRow& cr = id == "resolution" ? kResolution : id == "risc_clock" ? kRiscClock : kIdleSkip;
        const OptionDef* def = defFor(host, cr.key);
        // The call drawOption makes: the app's own hardcore handling (Achievements::checkSettings) sees it as any other change.
        if (def && !host.optionOverridden(cr.key) && index >= 0 && index < (int)def->values.size())
            host.setOptionValue(cr.key, def->values[(size_t)index].value);
    } else if (id == "volume") {
        app_.volume = (float)std::clamp(index, 0, 10) / 10.0f;
        app_.applyAudioSettings();
    } else if (id == "fps") {
        app_.showFps = index != 0;
    } else if (id == "vsync") {
        app_.vsync = index != 0;
        app_.applyVsync();
    } else if (id == "theme") {
        const std::string key = themeKeyAt(index, themeNames());
        if (!key.empty()) app_.setThemeChoice(key);
    } else if (id == "tv_mode") {
        if (index >= 0 && index <= (int)TvSetting::Auto) {
            const bool wasOn = app_.tvMode.active();
            app_.tvSetting = (TvSetting)index;
            app_.cfg.set("ui.tv_mode", settingKey(app_.tvSetting));   // written on change only: saveConfig() leaves the default free to move
            app_.configureTvMode();   // starts from off ...
            if (wasOn) app_.tvMode.setOn(true);   // ... so a Manual <-> Auto change stays in TV mode; Off stays off (setOn is a no-op then)
        }
    }
    app_.saveConfig();
}

void AppHost::openMoreSettings() { app_.showSettings = true; }

void AppHost::noteInput(const SDL_Event& e) {
    switch (e.type) {
    case SDL_EVENT_GAMEPAD_BUTTON_DOWN:
        lastWasPad_ = true;
        lastPad_ = e.gbutton.which;
        app_.input.notePadUsed(lastPad_);
        break;
    case SDL_EVENT_GAMEPAD_AXIS_MOTION:   // a resting stick drifts: only a real push counts
        if (e.gaxis.value > 16000 || e.gaxis.value < -16000) {
            lastWasPad_ = true;
            lastPad_ = e.gaxis.which;
            app_.input.notePadUsed(lastPad_);
        }
        break;
    case SDL_EVENT_KEY_DOWN:
        lastWasPad_ = false;
        break;
    default: break;
    }
}

// What the pad's menu-button profile (the controller family plus the model's custom layer, #1021) says a button means.
Intent AppHost::padIntent(uint32_t pad, int sdlButton) const { return app_.input.navProfile(pad).intentForButton(sdlButton); }
Intent AppHost::padTriggerIntent(uint32_t pad, int sdlAxis) const { return app_.input.navProfile(pad).intentForTrigger(sdlAxis); }

// The button caption for a hint: the button the last-used pad's profile gives the intent, named as that pad labels it
// (A / Cross / ...), the keyboard's keys while the keyboard was used last (or before any input).
std::string AppHost::glyph(Intent i) const {
    const char* key = "";
    SDL_GamepadButton dpad = SDL_GAMEPAD_BUTTON_INVALID;
    switch (i) {
    case Intent::Up: dpad = SDL_GAMEPAD_BUTTON_DPAD_UP; key = "Up"; break;
    case Intent::Down: dpad = SDL_GAMEPAD_BUTTON_DPAD_DOWN; key = "Down"; break;
    case Intent::Left: dpad = SDL_GAMEPAD_BUTTON_DPAD_LEFT; key = "Left"; break;
    case Intent::Right: dpad = SDL_GAMEPAD_BUTTON_DPAD_RIGHT; key = "Right"; break;
    case Intent::Accept: key = "Enter"; break;
    case Intent::Back: key = "Esc"; break;
    case Intent::Alt: break;    // Alt and Aux have no key
    case Intent::Aux: break;
    case Intent::Menu: break;   // no keyboard key (intentForKey): "" so the hint is dropped
    case Intent::SectionPrev: key = "Shift+Tab"; break;
    case Intent::SectionNext: key = "Tab"; break;
    case Intent::PagePrev: key = "Page Up"; break;
    case Intent::PageNext: key = "Page Down"; break;
    case Intent::Search:        // the desktop library's (#1021): no TV hint
    case Intent::AcceptLong:
    case Intent::None: break;
    }
    SDL_Gamepad* g = lastWasPad_ ? SDL_GetGamepadFromID(lastPad_) : nullptr;   // null once that pad is gone
    if (!g) return key;
    if (dpad != SDL_GAMEPAD_BUTTON_INVALID) return "D-pad";
    if (i == Intent::Search || i == Intent::AcceptLong || i == Intent::None) return key;
    return padButtonLabel(g, app_.input.navProfile(lastPad_), i);   // "" when the profile leaves it unbound: no hint
}

}  // namespace vj::tv
