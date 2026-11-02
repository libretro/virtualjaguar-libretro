#include "tv/tv_app_host.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cctype>
#include <cstdio>

#include "app.h"
#include "aspect_choices.h"
#include "devices.h"
#include "devices_ui.h"
#include "platform/platform.h"
#include "typed_options.h"
#include "ra_dash.h"
#include "shaders_ui.h"
#include "theme.h"
#include "tv/tv_settings_model.h"
#include "tv_more_settings.h"

namespace vj::tv {

double AppHost::now() const { return app_.now(); }
bool AppHost::gameLoaded() const { return app_.host.isLoaded(); }
std::string AppHost::gameTitle() const { return app_.host.contentName(); }
bool AppHost::menuPause(std::string& why) { return app_.menuPause(why); }
void AppHost::menuResume() { app_.menuResume(); }
bool AppHost::netplayActive() const { return app_.netplay.configured(); }
bool AppHost::menuRunsOn() const { return app_.menuRunsOn(); }
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
bool AppHost::tvAvailable() const { return app_.tvSetting != TvSetting::Off; }

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

bool AppHost::hasGames() const {
    for (const lib::Entry& e : app_.library.entries())
        if (e.section != lib::Section::Bios) return true;
    return false;
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
        for (const lib::StateInfo& s : sg->states) out.push_back({s.slot, s.mtime, s.disc});
    return out;
}

Art AppHost::stateThumb(const GameRef& g, const StateRef& state) {
    const lib::Entry* e = entryFor(g);
    const lib::StateGame* sg = e ? app_.library.statesFor(*e) : nullptr;
    if (sg)
        for (const lib::StateInfo& s : sg->states)
            if (s.slot == state.slot && s.disc == state.disc) return artOf(app_.library.stateThumb(s));
    return Art();
}

void AppHost::launch(const GameRef& g) {
    if (const lib::Entry* e = entryFor(g)) app_.library.launch(*e, /*fresh=*/true);   // the resume prompt needs a mouse: X / Details resume
}

void AppHost::launchState(const GameRef& g, const StateRef& state) {
    if (const lib::Entry* e = entryFor(g)) app_.library.launchState(*e, state.slot, state.disc);
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

const OptionDef* defFor(const CoreHost& host, const std::string& key) {
    for (const OptionDef& d : host.optionDefs())
        if (d.key == key) return &d;
    return nullptr;
}

constexpr float kDeadZoneLo = 0, kDeadZoneHi = 40, kDeadZoneStep = 5;
constexpr float kBufferLo = 20, kBufferHi = 200, kBufferStep = 10;
constexpr float kPauseLo = 0, kPauseHi = 10, kPauseStep = 0.5f;
constexpr float kTileStep = 0.1f;

// Where a device setting is written: the game's own scope when it already overrides the key (the desktop's "This game only"),
// else everywhere -- so the row's value is the one the change takes effect in.
std::string deviceScope(App& app, const char* key) {
    const std::string game = app.input.content();
    return !game.empty() && DeviceSettings::defined(app.cfg, game, key) ? game : std::string();
}

SettingRow pictureRow(const char* id, const char* label, bool on) { return onOffRow(id, label, "picture", "Picture", on); }
SettingRow interfaceRow(const char* id, const char* label, bool on) { return onOffRow(id, label, "interface", "Interface", on); }
SettingRow libraryRow(const char* id, const char* label, bool on) { return onOffRow(id, label, "library", "Library", on); }

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

// Every setting that is a choice, a toggle or a slider on the desktop (the plan's catalogue), grouped as the rail shows them:
// Picture, Sound, Interface, Library, Achievements, Discord, Neuro, then the core's own categories.  Only the rows that exist:
// no shader runtime, no RetroAchievements client or Discord application means no row -- never a greyed one.  A core option the
// core hides (or the app locks) is no row either.  Settings that need text (folders, BIOS files, sign-in, addresses) are the
// "Desktop settings..." row's.
std::vector<SettingRow> AppHost::settingRows() {
    CoreHost& host = app_.host;
    ShaderChain& sc = app_.video.shader;
    std::vector<SettingRow> rows;
    auto add = [&](SettingRow r) { rows.push_back(std::move(r)); };

    // ---- Picture
    sc.probeLibrary();   // cached after the first call (the desktop Shader section probes the same way)
    if (ShaderChain::builtWithHeaders() && sc.libraryLoaded()) add(shaderRow(sc.preset, sc.enabled, shaderPresetPaths(app_)));
    add(pictureRow("integer", "Integer scaling", app_.present.integerScale));
    add(pictureRow("smooth", "Smooth (bilinear) filter", app_.present.linearFilter));
    add(pictureRow("stretch", "Stretch to window (ignore aspect ratio)", app_.present.stretch));
    add(aspectRow(app_.present.aspectOverride, kAspectChoices, kAspectChoiceCount));
    add(pictureRow("vsync", "VSync", app_.vsync));
    {
        SettingRow r = choiceRow("pacing", "Frame pacing", "picture", "Picture", {"Jaguar rate", "Match display"},
                                 app_.pacingDisplay ? 1 : 0);
        r.note = app_.pacingStatus();
        add(std::move(r));
    }
    add(pictureRow("fullscreen", "Fullscreen", app_.fullscreen));

    // ---- Sound
    add(volumeRow(app_.volume));
    add(onOffRow("mute", "Mute", "sound", "Sound", app_.mute));
    add(sliderRow("audio_buffer", "Audio buffer", "sound", "Sound", app_.audioTargetMs, kBufferLo, kBufferHi, kBufferStep, "%.0f ms"));

    // ---- Interface
    add(interfaceRow("fps", "Show FPS", app_.showFps));
    add(interfaceRow("controls_at_start", "Show the controls overlay when a game starts", app_.cfg.getBool("ui.controls.autoshow", false)));
    add(themeRow(theme::choiceKey(), themeNames()));
    add(tvModeRow(app_.tvSetting));
    for (SettingRow& r : tvAutoRows(app_.tvSetting, app_.cfg.getBool("ui.tv_auto_fullscreen", true),
                                    app_.cfg.getBool("ui.tv_auto_controller", true)))
        add(std::move(r));
    if (platform::nativeMenuActive()) add(interfaceRow("menu_in_window", "Show the menu bar inside the window", app_.showInWindowMenu));
    add(interfaceRow("menu_autohide", "Auto-hide the menu bar in fullscreen", app_.fullscreenAutoHideMenu));
    {
        SettingRow r = interfaceRow("windows_outside", "Let windows move outside the main window", app_.windowsOutside);
        r.note = app_.viewportsNote();   // why it is ignored right now, or ""
        add(std::move(r));
    }
    add(interfaceRow("keep_awake", "Keep the display awake while a game is running", app_.keepAwake));
    add(interfaceRow("tray", "Show a tray icon", app_.trayIcon));
    {
        SettingRow r = interfaceRow("emu_thread", "Emulation thread", app_.emuThreadWanted());
        if (app_.emuThreadWanted() != app_.emuThreadThisRun()) r.note = "Restart the app to apply";
        add(std::move(r));
    }
    {
        const DeviceSettings ds = DeviceSettings::load(app_.cfg, app_.input.content());
        add(sliderRow("dead_zone", "Stick dead zone", "interface", "Interface", (float)ds.deadzonePct, kDeadZoneLo, kDeadZoneHi, kDeadZoneStep, "%.0f %%"));
        add(choiceRow("mouse_port", "Mouse drives", "interface", "Interface", {"Automatic", "Jaguar port 1", "Jaguar port 2"},
                      std::clamp(ds.mousePort + 1, 0, 2)));
        add(choiceRow("gun_source", "Light gun aims with", "interface", "Interface", {"Automatic", "Mouse only", "Stick / keys only"},
                      std::clamp(ds.gunSource, 0, 2)));
    }
    add(interfaceRow("flush_saves", "Flush saves every few seconds", app_.autosaveSram));
    add(moreRow());

    // ---- Library
    {
        std::vector<std::string> modes, shows;
        for (int m = 0; m < lib::kResumeModeCount; m++) modes.push_back(lib::resumeModeLabel(m));
        for (int m = 0; m < lib::kShowAsCount; m++) shows.push_back(lib::showAsLabel(m));
        add(choiceRow("resume", "Resume where I left off", "library", "Library", modes, std::clamp(app_.resumeMode, 0, lib::kResumeModeCount - 1)));
        add(libraryRow("home_library", "Show the game library on the home screen", app_.homeLibrary));
        add(choiceRow("show_as", "Favorites show as", "library", "Library", shows, std::clamp(app_.library.favoritesShow, 0, lib::kShowAsCount - 1)));
        add(choiceRow("show_as_recent", "Recently played shows as", "library", "Library", shows,
                      std::clamp(app_.library.recentsShow, 0, lib::kShowAsCount - 1)));
        add(sliderRow("tile_size", "Tile size", "library", "Library", app_.library.cellScale, lib::kTileScaleMin, lib::kTileScaleMax, kTileStep, "%.1fx"));
        add(libraryRow("download_art", "Download box art", app_.library.downloadArt.load()));
        add(libraryRow("close_on_launch", "Close the library on launch", app_.library.hideOnLaunch));
    }

    // ---- Achievements (a build without the client has nothing to set)
    if (app_.ach.available()) add(onOffRow("ra_images", "Download pictures", "achievements", "Achievements", app_.ach.imagesWanted()));

    // ---- Discord (no application ID, or a second copy of the app, publishes nothing)
    if (app_.discordPresence.available()) {
        add(onOffRow("discord_enabled", "Show what I'm playing on Discord", "discord", "Discord", app_.cfg.getBool("discord.enabled", true)));
        add(onOffRow("discord_art", "Show game artwork", "discord", "Discord", app_.cfg.getBool("discord.art", true)));
        add(onOffRow("discord_buttons", "Show buttons", "discord", "Discord", app_.cfg.getBool("discord.buttons", true)));
    }

#ifdef VJ_HAVE_NEURO
    add(onOffRow("neuro_enabled", "Connect to Neuro", "neuro", "Neuro", app_.cfg.getBool("neuro.enabled", false)));
    add(onOffRow("neuro_auto_force", "Ask for the next move when the last one is done", "neuro", "Neuro", app_.cfg.getBool("neuro.auto_force", true)));
    add(sliderRow("neuro_interval", "Pause between moves", "neuro", "Neuro", app_.cfg.getFloat("neuro.force_interval", 1.0f), kPauseLo, kPauseHi, kPauseStep, "%.1f s"));
#endif

    // ---- the core's own groups (read at startup: CoreHost::probeOptions)
    for (SettingRow& r : coreOptionRows(
             host.optionCategories(), host.optionDefs(), [&](const OptionDef& d) { return host.optionValue(d); },
             [&](const std::string& k) { return host.optionVisible(k); }, [&](const std::string& k) { return host.optionOverridden(k); },
             [](const std::string& k) { return typedOptionFor(k); }))
        rows.push_back(std::move(r));
    return rows;
}

// What the desktop control does for each row, then the config is saved.  An index the row cannot have (the "(custom)" entry
// of a typed option or a slider, a theme whose file is gone) changes nothing.
void AppHost::setSetting(const std::string& id, int index) {
    CoreHost& host = app_.host;
    const bool on = index != 0;
    auto deviceChanged = [&] {   // devices_ui.cpp's deviceChanged (the config is saved below)
        app_.input.reresolve();
        applyPortDevices(app_);
    };
    if (id.rfind("core:", 0) == 0) {
        const std::string key = id.substr(5);
        const OptionDef* def = defFor(host, key);
        // The call drawOption makes: the app's own hardcore handling (Achievements::checkSettings) sees it as any other change.
        if (def && host.optionVisible(key) && !host.optionOverridden(key) && index >= 0) {
            // The row may carry a "(custom)" entry among the values (coreCustomPos): that index changes nothing, the ones after it shift.
            const int custom = coreCustomPos(*def, host.optionValue(*def), typedOptionFor(key));
            const int at = custom >= 0 && index > custom ? index - 1 : index;
            if (index != custom && at < (int)def->values.size()) host.setOptionValue(key, def->values[(size_t)at].value);
        }
    } else if (id == "shader") {
        ShaderChain& sc = app_.video.shader;
        const std::vector<std::string> paths = shaderPresetPaths(app_);
        if (index == 0) {
            sc.enabled = false;   // the "Use shader" checkbox
            sc.retry();
        } else if (index >= 1 && index <= (int)paths.size()) {
            applyShaderPreset(app_, paths[(size_t)index - 1]);
        }
    } else if (id == "integer") {
        app_.present.integerScale = on;
    } else if (id == "smooth") {
        app_.present.linearFilter = on;
    } else if (id == "stretch") {
        app_.present.stretch = on;
    } else if (id == "aspect") {
        if (index >= 0 && index < kAspectChoiceCount) app_.present.aspectOverride = kAspectChoices[index].value;
    } else if (id == "vsync") {
        app_.vsync = on;
        app_.applyVsync();
    } else if (id == "pacing") {
        app_.pacingDisplay = index == 1;
    } else if (id == "fullscreen") {
        app_.setFullscreen(on);
    } else if (id == "volume") {
        app_.volume = stepValue(volumeRow(app_.volume), index, 0.0f, 10.0f, app_.volume * 100.0f) / 100.0f;
        app_.applyAudioSettings();
    } else if (id == "mute") {
        app_.mute = on;
        app_.applyAudioSettings();
    } else if (id == "audio_buffer") {
        const SettingRow r = sliderRow(id, "", "sound", "Sound", app_.audioTargetMs, kBufferLo, kBufferHi, kBufferStep, "%.0f");
        app_.audioTargetMs = stepValue(r, index, kBufferLo, kBufferStep, app_.audioTargetMs);
        app_.applyAudioSettings();
    } else if (id == "fps") {
        app_.showFps = on;
    } else if (id == "controls_at_start") {
        app_.cfg.setBool("ui.controls.autoshow", on);
    } else if (id == "theme") {
        const std::string key = themeKeyAt(index, themeNames());
        if (!key.empty()) app_.setThemeChoice(key);
    } else if (id == "tv_mode") {
        if (index >= 0 && index <= (int)TvSetting::Auto) {
            app_.setTvSetting((TvSetting)index);   // writes ui.tv_mode on change only, stays in TV mode across Manual <-> Auto
        }
    } else if (id == "tv_auto_fs") {
        app_.setTvAuto(true, index == 1);      // ui.tv_auto_fullscreen; a trigger-started TV mode stays on, as a Manual one
    } else if (id == "tv_auto_pad") {
        app_.setTvAuto(false, index == 1);     // ui.tv_auto_controller
    } else if (id == "menu_in_window") {
        app_.showInWindowMenu = on;
    } else if (id == "menu_autohide") {
        app_.fullscreenAutoHideMenu = on;
    } else if (id == "windows_outside") {
        app_.windowsOutside = on;
    } else if (id == "keep_awake") {
        app_.keepAwake = on;
    } else if (id == "tray") {
        app_.trayIcon = on;
        if (!app_.applyTray()) {   // as the desktop checkbox: a desktop with no tray puts it back
            app_.trayIcon = false;
            app_.toast("No system tray is available on this desktop", 4.0, 2);
        }
    } else if (id == "emu_thread") {
        app_.emuThreadPref = on ? 1 : 0;
    } else if (id == "dead_zone") {
        const float cur = (float)DeviceSettings::load(app_.cfg, app_.input.content()).deadzonePct;
        const SettingRow r = sliderRow(id, "", "interface", "Interface", cur, kDeadZoneLo, kDeadZoneHi, kDeadZoneStep, "%.0f");
        const float pct = stepValue(r, index, kDeadZoneLo, kDeadZoneStep, cur);
        if (pct != cur) {
            DeviceSettings::set(app_.cfg, deviceScope(app_, "deadzone"), "deadzone", std::to_string((int)pct));
            deviceChanged();
        }
    } else if (id == "mouse_port") {
        if (index >= 0 && index <= 2) {
            DeviceSettings::set(app_.cfg, deviceScope(app_, "mouse_port"), "mouse_port", std::to_string(index - 1));
            deviceChanged();
        }
    } else if (id == "gun_source") {
        if (index >= 0 && index <= 2) {
            DeviceSettings::set(app_.cfg, deviceScope(app_, "gun_source"), "gun_source", std::to_string(index));
            deviceChanged();
        }
    } else if (id == "flush_saves") {
        app_.autosaveSram = on;
    } else if (id == "resume") {
        if (index >= 0 && index < lib::kResumeModeCount) app_.resumeMode = index;
    } else if (id == "home_library") {
        app_.homeLibrary = on;
    } else if (id == "show_as" || id == "show_as_recent") {
        if (index >= 0 && index < lib::kShowAsCount) {
            (id == "show_as" ? app_.library.favoritesShow : app_.library.recentsShow) = index;
            app_.library.saveSettings();
        }
    } else if (id == "tile_size") {
        const SettingRow r = sliderRow(id, "", "library", "Library", app_.library.cellScale, lib::kTileScaleMin, lib::kTileScaleMax, kTileStep, "%.1f");
        app_.library.cellScale = lib::clampTileScale(stepValue(r, index, lib::kTileScaleMin, kTileStep, app_.library.cellScale));
        app_.library.saveSettings();
    } else if (id == "download_art") {
        app_.library.setDownloadArt(on);
    } else if (id == "close_on_launch") {
        app_.library.hideOnLaunch = on;
        app_.library.saveSettings();
    } else if (id == "ra_images") {
        app_.ach.setImagesWanted(on);
    } else if (id == "discord_enabled") {
        app_.cfg.setBool("discord.enabled", on);
    } else if (id == "discord_art") {
        app_.cfg.setBool("discord.art", on);
    } else if (id == "discord_buttons") {
        app_.cfg.setBool("discord.buttons", on);
#ifdef VJ_HAVE_NEURO
    } else if (id == "neuro_enabled") {
        app_.cfg.setBool("neuro.enabled", on);
    } else if (id == "neuro_auto_force") {
        app_.cfg.setBool("neuro.auto_force", on);
    } else if (id == "neuro_interval") {
        const float cur = app_.cfg.getFloat("neuro.force_interval", 1.0f);
        const SettingRow r = sliderRow(id, "", "neuro", "Neuro", cur, kPauseLo, kPauseHi, kPauseStep, "%.1f");
        app_.cfg.setFloat("neuro.force_interval", stepValue(r, index, kPauseLo, kPauseStep, cur));
#endif
    }
    app_.saveConfig();
}

void AppHost::openMoreSettings() { tvMoreSettingsOpen(app_); }   // the window, with the pad handed to it (tv_more_settings.h)

// ---- achievements ------------------------------------------------------------------------------------------------------
// All UI-thread calls into Achievements (the plan's "Threading" list): they take its client lock themselves.

AchHeader AppHost::achHeader() {
    Achievements& a = app_.ach;
    AchHeader h;
    h.available = a.available();
    h.signedIn = a.signedIn();
    if (!h.available || !h.signedIn) return h;
    const Achievements::ClientSnapshot s = a.clientSnapshot();
    h.hasGame = s.gameId != 0;
    if (!h.hasGame) return h;
    h.title = s.gameTitle;
    h.iconUrl = a.gameIconUrl();
    h.unlocked = s.summary.num_unlocked_achievements;
    h.total = s.summary.num_core_achievements;
    // modeChip's words (achievements_ui.cpp): hardcore, or softcore with what pauses it when hardcore is wanted.
    if (s.hardcore) h.mode = "Hardcore";
    else if (a.hardcoreWanted() && !a.hardcoreBlocker().empty()) h.mode = "Softcore (hardcore is paused: " + a.hardcoreBlocker() + ")";
    else h.mode = "Softcore";
    return h;
}

// The active leaderboard trackers, then the rows in the desktop list's default order (rc_client's buckets).
std::vector<AchItem> AppHost::achItems() {
    Achievements& a = app_.ach;
    std::vector<AchItem> out;
    if (a.clientSnapshot().gameId == 0) return out;
    for (const auto& kv : a.trackers) {
        AchItem t;
        t.kind = AchItem::Tracker;
        t.id = kv.first;
        t.title = kv.second;
        out.push_back(std::move(t));
    }
    std::vector<ra::AchRow> rows;
    a.achievementRows(rows);
    for (size_t i : ra::viewAchievements(rows, ra::AchFilter::All, ra::AchSort::Default, "")) {
        const ra::AchRow& r = rows[i];
        AchItem it;
        it.id = r.id;
        it.title = r.title;
        it.description = r.description;
        it.status = ra::achStatusLine(r);
        it.points = r.points;
        it.unlocked = r.unlocked();
        it.badgeUrl = r.unlocked() ? r.badgeUrl : r.badgeLockedUrl;
        out.push_back(std::move(it));
    }
    return out;
}

// The games with a published set, by title (the desktop's default view); gamesTabShown() first, as the desktop tab does every
// frame it shows: it loads the cached list, refreshes a stale one and fetches the player's progress.
std::vector<AchGame> AppHost::achGames() {
    Achievements& a = app_.ach;
    a.gamesTabShown();
    const std::vector<ra::GameEntry>& games = a.games();
    std::vector<AchGame> out;
    for (size_t i : ra::viewGames(games, ra::GameFilter::All, ra::GameSort::Title, "")) {
        const ra::GameEntry& g = games[i];
        AchGame r;
        r.id = g.id;
        r.title = g.title;
        r.imageUrl = g.imageUrl;
        r.achievements = g.achievements;
        r.unlocked = std::max(g.unlocked, g.unlockedHardcore);
        r.haveProgress = g.haveProgress;
        r.inLibrary = g.libIndex >= 0;
        out.push_back(std::move(r));
    }
    return out;
}

Art AppHost::achImage(const std::string& url) {
    Art a;
    if (url.empty()) return a;
    const ra::ImageView v = app_.ach.images.get(url);
    if (v.state == ra::ImageView::Ready) {
        a.tex = v.tex;
        a.w = v.w;
        a.h = v.h;
    }
    a.loading = v.state == ra::ImageView::Loading;
    return a;
}

// Achievements::playGame minus the "Resume where you left off?" offer, which needs a mouse (launch() with `fresh`, as AppHost::launch).
void AppHost::achPlay(uint32_t gameId) {
    const std::vector<lib::Entry>& es = app_.library.entries();
    for (const ra::GameEntry& g : app_.ach.games())
        if (g.id == gameId && g.libIndex >= 0 && (size_t)g.libIndex < es.size()) {
            app_.library.launch(es[(size_t)g.libIndex], /*fresh=*/true);
            return;
        }
}

// The list entry the library file runs.  Only while signed in (the row leads to a list that needs it), and asking for the list
// like the screen does: nothing else loads it, so Game details would never see an entry before the first visit to Achievements.
uint32_t AppHost::achGameId(const GameRef& g) {
    if (!app_.ach.signedIn()) return 0;
    const lib::Entry* e = entryFor(g);
    if (!e) return 0;
    app_.ach.gamesTabShown();
    const int index = (int)(e - app_.library.entries().data());
    for (const ra::GameEntry& r : app_.ach.games())
        if (r.libIndex == index && r.achievements > 0) return r.id;
    return 0;
}

// ---- overlays ----------------------------------------------------------------------------------------------------------
// The members the View and Tools menus drive (desktop: ui.cpp, debug_ui.cpp), all UI-thread state.

namespace {

// "Memory viewer..." -> "Memory viewer" (the menu's ellipsis says "opens a window"; a row that toggles does not need it).
std::string toolLabel(std::string s) {
    for (const char* dots : {"...", "\xE2\x80\xA6"}) {
        const size_t n = std::char_traits<char>::length(dots);
        if (s.size() >= n && s.compare(s.size() - n, n, dots) == 0) s.erase(s.size() - n);
    }
    while (!s.empty() && s.back() == ' ') s.pop_back();
    return s;
}

}  // namespace

std::vector<ToggleRow> AppHost::overlayRows() {
    std::vector<ToggleRow> rows;
    auto add = [&](const char* id, const char* label, bool on) {
        ToggleRow r;
        r.id = id;
        r.label = label;
        r.on = on;
        rows.push_back(std::move(r));
    };
    add("fps", "Show FPS", app_.showFps);
    add("keypad", "Keypad overlay", app_.input.overlay.open);
    add("controls", "Controls overlay", app_.input.controls.show);
    const DebugSession& d = app_.debug;
    for (const DebugTool& t : d.tools()) {
        if (!t.toggle) continue;   // a greyed placeholder for a window that does not exist yet
        ToggleRow r;
        r.id = "tool:" + t.id;
        r.label = toolLabel(t.label);
        r.on = t.isOpen && t.isOpen();
        r.enabled = d.toolAvailable(t);
        if (!r.enabled)   // toolAvailable(): the tools are enabled, and a processor is halted when the window needs one
            r.note = !d.enabled() ? "Debug windows are off (hardcore, the emulation thread, or no game)" : "Break into the game first (F9)";
        rows.push_back(std::move(r));
    }
    return rows;
}

// What each row's menu item does.  A disabled tool stays a no-op, as the Tools menu leaves it.
void AppHost::toggleOverlay(const std::string& id) {
    if (id == "fps") {
        app_.showFps = !app_.showFps;
        app_.saveConfig();
    } else if (id == "keypad") {
        app_.input.overlay.open = !app_.input.overlay.open;
    } else if (id == "controls") {
        overlayEvent(app_.input.controls, OverlayEvent::Toggle);
    } else if (id.rfind("tool:", 0) == 0) {
        const DebugTool* t = app_.debug.findTool(id.substr(5));
        if (t && app_.debug.toolAvailable(*t)) t->toggle();
    }
}

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
