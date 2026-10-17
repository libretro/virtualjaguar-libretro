// All ImGui for the app: menu bar, the settings window (including the core
// options menu generated from the options v2 definitions), the save-state
// manager, the on-screen messages.
#include "app.h"
#include "devices_ui.h"
#include "fonts_ui.h"

#include "imgui.h"
#include "platform/platform.h"
#include "settings_reset_ui.h"
#include "shaders.h"
#include "tv/tv_shell.h"
#include "typed_ui.h"
#include "ui_util.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <map>

namespace vj {

namespace {

std::string lower(std::string s) {
    for (char& c : s) c = (char)std::tolower((unsigned char)c);
    return s;
}

bool contains(const std::string& hay, const std::string& needleLower) {
    return needleLower.empty() || lower(hay).find(needleLower) != std::string::npos;
}

// ---- core options (generated; no per-option code) ----------------------------------

const char* labelFor(const OptionDef& d, const std::string& value) {
    for (const OptionValue& v : d.values)
        if (v.value == value) return v.label.empty() ? v.value.c_str() : v.label.c_str();
    return value.c_str();
}

// The "Custom..." box state of the typed options, by key.  File scope so a reset can forget it.
std::map<std::string, ui::TypedState>& typedStateMap() {
    static std::map<std::string, ui::TypedState> m;
    return m;
}

void drawOption(App& app, const OptionDef& d, bool inCategory) {
    CoreHost& host = app.host;
    std::string cur = host.optionValue(d);
    const std::string& title = (inCategory && !d.descCategorized.empty()) ? d.descCategorized : d.desc;
    const std::string& info = (inCategory && !d.infoCategorized.empty()) ? d.infoCategorized : d.info;

    ImGui::PushID(d.key.c_str());
    // The value box takes a share of the width; the description wraps in what is left.
    // A session override (the Netplay window's link mode / host / port, or --option) decides what the core
    // reads, so a pick here would be saved yet never show: the row is read-only until the override ends.
    const bool pinned = host.optionOverridden(d.key);
    // A key in typed_options.h gets the core's presets plus "Custom..." (a typed value); every other
    // option is a plain list.
    const TypedOption* typed = typedOptionFor(d.key);
    ui::TypedState* ts = typed ? &typedStateMap()[d.key] : nullptr;
    ImGui::BeginDisabled(pinned);
    if (typed) {
        std::string picked;
        if (ui::typedCombo("##v", ui::fitWidth(0.34f, 8.0f, 14.0f), *typed, d.values, cur, *ts, picked))
            host.setOptionValue(d.key, picked);
    } else {
        ImGui::SetNextItemWidth(ui::fitWidth(0.34f, 8.0f, 14.0f));
        if (ImGui::BeginCombo("##v", labelFor(d, cur))) {
            int vi = 0;
            for (const OptionValue& v : d.values) {
                bool sel = (v.value == cur);
                ImGui::PushID(vi++);   // two values may share a label
                if (ImGui::Selectable(v.label.empty() ? v.value.c_str() : v.label.c_str(), sel))
                    host.setOptionValue(d.key, v.value);
                ImGui::PopID();
                if (sel) ImGui::SetItemDefaultFocus();
            }
            ImGui::EndCombo();
        }
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    // "reset" has a fixed slot so the descriptions stay aligned whether or not it shows.
    if (host.userOptions().count(d.key)) {
        if (ImGui::SmallButton("reset")) {
            host.resetOptionToDefault(d.key);
            if (ts) ts->custom = false;
        }
    } else {
        ImGui::Dummy(ImVec2(ui::Flow::buttonW("reset"), 1.0f));
    }
    ImGui::SameLine();
    ImGui::AlignTextToFramePadding();
    ImGui::TextWrapped("%s", title.c_str());
    if (!info.empty() && ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort)) {
        ui::beginWrappedTooltip();
        ImGui::TextUnformatted(info.c_str());
        ui::endWrappedTooltip();
    }
    if (typed && ts->custom) {
        // The typed value, under the row.
        ImGui::BeginDisabled(pinned);
        ImGui::Indent();
        std::string picked;
        if (ui::typedInput(*typed, cur, *ts, picked)) host.setOptionValue(d.key, picked);
        ImGui::Unindent();
        ImGui::EndDisabled();
    }
    if (pinned)
        ui::hint("Set by the Netplay window (or --option) for this session. Disconnect in the Netplay menu to change it here.");
    ImGui::PopID();
}

bool matchesFilter(const OptionDef& d, const std::string& f) {
    return f.empty() || contains(d.key, f) || contains(d.desc, f) || contains(d.descCategorized, f) ||
           contains(d.info, f);
}

void drawCoreOptions(App& app) {
    CoreHost& host = app.host;
    if (!host.isLoaded() && host.optionDefs().empty()) {
        ImGui::TextWrapped(
            "The core's options appear here once a game is loaded (or the console is booted). "
            "The menu is generated from the core's own option definitions.");
        return;
    }
    if (host.isLoaded()) {
        if (ImGui::Button("Restart game to apply")) app.reloadContent();
        ui::hint("'Restart required' options apply on the next load.");
    }
    ImGui::SetNextItemWidth(ui::fitWidth(0.5f, 10.0f, 24.0f));
    ImGui::InputTextWithHint("##filter", "Filter options", app.optionFilter, sizeof app.optionFilter);
    std::string f = lower(app.optionFilter);
    ImGui::Separator();

    ImGui::BeginChild("##opts", ImVec2(0, 0), ImGuiChildFlags_None);
    const std::vector<OptionDef>& defs = host.optionDefs();

    // Options with no category first, then each category in definition order.
    auto drawGroup = [&](const std::string& catKey, const std::string& title) {
        bool any = false;
        for (const OptionDef& d : defs)
            if (d.categoryKey == catKey && host.optionVisible(d.key) && matchesFilter(d, f)) { any = true; break; }
        if (!any) return;
        bool open = true;
        if (!title.empty()) {
            if (!f.empty()) ImGui::SetNextItemOpen(true, ImGuiCond_Always);
            open = ImGui::CollapsingHeader(title.c_str());
        }
        if (!open) return;
        ImGui::PushID(catKey.c_str());
        ImGui::Indent();
        drawCoreGroupReset(app, catKey, !f.empty());
        for (const OptionDef& d : defs)
            if (d.categoryKey == catKey && host.optionVisible(d.key) && matchesFilter(d, f))
                drawOption(app, d, !catKey.empty());
        ImGui::Unindent();
        ImGui::PopID();
    };

    drawGroup("", "General");
    for (const OptionCategory& c : host.optionCategories()) drawGroup(c.key, c.desc);
    // Options naming a category the core never defined still show up.
    for (const OptionDef& d : defs) {
        if (d.categoryKey.empty()) continue;
        bool known = false;
        for (const OptionCategory& c : host.optionCategories())
            if (c.key == d.categoryKey) { known = true; break; }
        if (!known && host.optionVisible(d.key) && matchesFilter(d, f)) drawOption(app, d, false);
    }
    ImGui::EndChild();
}

// ---- settings window ------------------------------------------------------------------

void drawVideoTab(App& app) {
    drawAppGroupReset(app, AppGroup::Video);
    PresentOptions& p = app.present;
    ImGui::Checkbox("Integer scaling", &p.integerScale);
    ImGui::Checkbox("Smooth (bilinear) filter", &p.linearFilter);
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Has no effect while integer scaling is on.");
    ImGui::Checkbox("Stretch to window (ignore aspect ratio)", &p.stretch);
    static const char* aspects[] = {"Core default", "4:3", "16:9", "8:7 (square pixels)"};
    static const float vals[] = {0.0f, 4.0f / 3.0f, 16.0f / 9.0f, 8.0f / 7.0f};
    int sel = 0;
    for (int i = 0; i < 4; i++)
        if (std::abs(p.aspectOverride - vals[i]) < 1e-3f) sel = i;
    ImGui::SetNextItemWidth(ui::fitWidth(0.4f, 9.0f, 16.0f));
    if (ImGui::Combo("Aspect ratio", &sel, aspects, 4)) p.aspectOverride = vals[sel];
    if (ImGui::Checkbox("VSync", &app.vsync)) app.applyVsync();
    bool fs = app.fullscreen;
    if (ImGui::Checkbox("Fullscreen (F11)", &fs)) app.setFullscreen(fs);
    drawShaderSettings(app);
}

void drawAudioTab(App& app) {
    drawAppGroupReset(app, AppGroup::Audio);
    bool changed = false;
    ImGui::PushItemWidth(ui::fitWidth(0.55f, 10.0f, 28.0f));
    changed |= ImGui::SliderFloat("Volume", &app.volume, 0.0f, 1.0f, "%.2f");
    changed |= ImGui::Checkbox("Mute", &app.mute);
    changed |= ImGui::SliderFloat("Buffer target (ms)", &app.audioTargetMs, 20.0f, 200.0f, "%.0f");
    ImGui::PopItemWidth();
    if (changed) app.applyAudioSettings();
    ImGui::Separator();
    ImGui::TextWrapped("Buffered: %.1f ms   playback ratio: %.5f   underruns: %llu", app.audio.queuedMs(),
                       app.audio.ratio(), (unsigned long long)app.audio.underruns());
    if (app.host.isLoaded())
        ImGui::TextWrapped("Core rate: %.2f Hz at %.5f fields/s", app.host.sampleRate(), app.host.fps());
}

void drawInputTab(App& app) {
    drawAppGroupReset(app, AppGroup::Input);
    if (!ImGui::BeginTabBar("##inputtabs")) return;
    if (ImGui::BeginTabItem("Devices & players")) {
        drawDevicesPanel(app);
        ImGui::EndTabItem();
    }
    if (ImGui::BeginTabItem("Key / button bindings")) {
        drawControlsTab(app);
        ImGui::EndTabItem();
    }
    ImGui::EndTabBar();
}

void drawStorageTab(App& app) {
    drawAppGroupReset(app, AppGroup::Storage);
    ImGui::Text("Data folder%s:", app.paths.portable ? " (portable mode)" : "");
    ImGui::TextWrapped("%s", app.paths.root.c_str());
    if (app.paths.instance > 1)
        ui::hint("This is window %d of several on this data folder: its game saves live in their own folder, and "
                 "changes here are not saved (only the window size and the controller choice are). Change settings "
                 "in the first window to keep them.", app.paths.instance);
    ImGui::Separator();
    // Paths are long and have no spaces to break at: ImGui wraps them at the window edge.
    ImGui::TextWrapped("Saves: %s", app.paths.saves.c_str());
    ImGui::TextWrapped("States: %s", app.paths.states.c_str());
    ImGui::TextWrapped("System: %s", app.paths.system.c_str());
    ImGui::Separator();
    ImGui::Checkbox("Flush saves every few seconds", &app.autosaveSram);
    ui::hint("SRAM, EEPROM and the Memory Track. HLE boot is the default, so no BIOS file is needed (Settings > BIOS).");
}

// Settings > Interface: how the app itself looks and behaves (as opposed to Video's picture and Storage's saves).
// The config keys are the ones these controls always had (ui.*, saves.resume, library.*): nothing is renamed.
void drawInterfaceTab(App& app) {
    drawAppGroupReset(app, AppGroup::Interface);
    ImGui::SeparatorText("Overlays");
    ImGui::Checkbox("Show frame rate", &app.showFps);
    drawControlsOverlaySettings(app);
    drawAppearanceSettings(app);
    drawFontSettings(app);
    ImGui::SeparatorText("Save states");
    ImGui::TextUnformatted("Resume where I left off");
    ImGui::SetNextItemWidth(ui::em(10.0f));
    if (ImGui::BeginCombo("##resume", lib::resumeModeLabel(app.resumeMode))) {
        for (int m = 0; m < lib::kResumeModeCount; m++)
            if (ImGui::Selectable(lib::resumeModeLabel(m), app.resumeMode == m)) app.resumeMode = m;
        ImGui::EndCombo();
    }
    ui::hint("When a game is closed, left for another game, or the app quits, its state is kept as an \"Auto-save\" "
             "(one per game, the latest; listed in the library's Save states tab). Next time the game boots: Always "
             "resumes it, Ask offers to, Off does nothing and writes none. Never in RetroAchievements hardcore or during "
             "netplay, not for a game that ran under %d seconds or that the crash watchdog flagged. Another window "
             "(copy 2, 3...) keeps its own.", (int)lib::kAutoSaveMinSeconds);
    ImGui::SeparatorText("Game library");
    ImGui::Checkbox("Show the game library on the home screen", &app.homeLibrary);
    ui::hint("When no game is loaded the window shows your library (add folders there). Off: a plain "
             "welcome message instead; File > Game library (Ctrl/Cmd+L) still opens the library in its own window.");
    drawDesktopSettings(app);
    drawLibraryListOptions(app.library);
}

void drawDiscordTab(App& app) {
    drawAppGroupReset(app, AppGroup::Discord);
    const bool avail = app.discordPresence.available();
    bool on = app.cfg.getBool("discord.enabled", true);
    bool art = app.cfg.getBool("discord.art", true);
    bool buttons = app.cfg.getBool("discord.buttons", true);
    ImGui::TextWrapped("Show on your Discord profile which game you are playing, how long, and (for RetroAchievements "
                       "games) your progress. Discord has to be running on this computer.");
    ImGui::Spacing();
    ImGui::BeginDisabled(!avail);
    if (ImGui::Checkbox("Show what I'm playing on Discord", &on)) app.cfg.setBool("discord.enabled", on);
    ImGui::BeginDisabled(!on);
    if (ImGui::Checkbox("Show game artwork", &art)) app.cfg.setBool("discord.art", art);
    ui::hint("The game's box art (the same picture the library shows) is sent to Discord as a public web address. "
             "Off: a generic Jaguar picture. Only art already downloaded by the library is used.");
    if (ImGui::Checkbox("Show buttons", &buttons)) app.cfg.setBool("discord.buttons", buttons);
    ui::hint("\"Get Virtual Jaguar\" (the download page), and for a RetroAchievements game a link to its page. "
             "Discord shows buttons to other people only, never on your own profile.");
    ImGui::EndDisabled();
    ImGui::EndDisabled();
    ImGui::Separator();
    {
        // Green when connected, amber while Discord is not running, red when it refused the ID, plain otherwise.
        const bool live = app.discordPresence.active();
        const discord::Status st = live ? app.discordPresence.client()->status() : discord::Status::Stopped;
        ImGui::TextUnformatted("Status:");
        ImGui::SameLine();
        const std::string txt = app.discordPresence.statusText();
        if (live && st == discord::Status::Connected) ui::coloredWrapped(ui::vec4(ui::pal().success), txt.c_str());
        else if (live && st == discord::Status::Searching) ui::coloredWrapped(ui::vec4(ui::pal().warning), txt.c_str());
        else if (live && st == discord::Status::InvalidAppId) ui::coloredWrapped(ui::vec4(ui::pal().error), txt.c_str());
        else ImGui::TextWrapped("%s", txt.c_str());
    }
    if (app.paths.instance > 1)
        ui::hint("Several copies of the app can run at once; only the first one publishes, so two windows never fight "
                 "over your status.");
    ui::hint("Shared with Discord: the game title, the platform, whether it is paused, hardcore / netplay mode "
             "(never an address), achievement counts, and when you started. Nothing else.");
    if (ImGui::CollapsingHeader("Application ID (advanced)")) {
        static char idBuf[64];
        static bool idInit = false;
        if (!idInit) {
            std::snprintf(idBuf, sizeof idBuf, "%s", app.cfg.get("discord.app_id", "").c_str());
            idInit = true;
        }
        ImGui::SetNextItemWidth(ui::em(16.0f));
        if (ImGui::InputText("##discordid", idBuf, sizeof idBuf, ImGuiInputTextFlags_CharsDecimal)) {
            if (idBuf[0]) app.cfg.set("discord.app_id", idBuf);
            else app.cfg.erase("discord.app_id");
        }
        ui::hint("The Discord application this status is published under. Builds that ship an ID fill it in; "
                 "leave this empty to use that one. Forks can use their own application from the Discord developer portal.");
    }
    app.cfg.save();
}

void drawSettings(App& app) {
    ui::nextWindow(42.0f, 30.0f, 34.0f, 16.0f);
    bool open = ImGui::Begin("Settings", &app.showSettings);
    ui::keepOnScreen();
    if (!open) {
        ImGui::End();
        return;
    }
    drawResetAllSettings(app);
    if (ImGui::BeginTabBar("##tabs", ImGuiTabBarFlags_FittingPolicyScroll)) {
        // dev: --show-window settings-video etc. selects a tab once
        auto tabFlag = [&](const char* name) {
            if (app.devSelectTab != name) return (ImGuiTabItemFlags)0;
            app.devSelectTab.clear();
            return (ImGuiTabItemFlags)ImGuiTabItemFlags_SetSelected;
        };
        if (ImGui::BeginTabItem("Core options")) { drawCoreOptions(app); ImGui::EndTabItem(); }
        if (ImGui::BeginTabItem("Video", nullptr, tabFlag("Video"))) { drawVideoTab(app); ImGui::EndTabItem(); }
        if (ImGui::BeginTabItem("Audio", nullptr, tabFlag("Audio"))) { drawAudioTab(app); ImGui::EndTabItem(); }
        if (ImGui::BeginTabItem("Input", nullptr, tabFlag("Input"))) { drawInputTab(app); ImGui::EndTabItem(); }
        if (ImGui::BeginTabItem("BIOS", nullptr, tabFlag("BIOS"))) { drawBiosPanel(app); ImGui::EndTabItem(); }
        if (ImGui::BeginTabItem("Storage", nullptr, tabFlag("Storage"))) { drawStorageTab(app); ImGui::EndTabItem(); }
        if (ImGui::BeginTabItem("Interface", nullptr, tabFlag("Interface"))) { drawInterfaceTab(app); ImGui::EndTabItem(); }
        if (ImGui::BeginTabItem("Discord", nullptr, tabFlag("Discord"))) { drawDiscordTab(app); ImGui::EndTabItem(); }
        if (ImGui::BeginTabItem("Neuro", nullptr, tabFlag("Neuro"))) { drawNeuroTab(app); ImGui::EndTabItem(); }
        ImGui::EndTabBar();
    }
    ImGui::End();
}

// ---- save-state manager --------------------------------------------------------------------

void drawStates(App& app) {
    ui::nextWindow(44.0f, 30.0f, 22.0f, 14.0f);
    bool open = ImGui::Begin("Save states", &app.showStates);
    ui::keepOnScreen();
    if (!open) {
        ImGui::End();
        return;
    }
    if (!app.host.isLoaded()) {
        ImGui::TextDisabled("Load a game to use save states.");
        ImGui::End();
        return;
    }
    ImGui::TextWrapped("%s", app.host.contentName().c_str());
    // As many columns as fit at a comfortable thumbnail size, then the thumbnails share the row evenly.
    float gap = ImGui::GetStyle().ItemSpacing.x;
    float avail = ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ScrollbarSize;
    int perRow = std::max(1, (int)((avail + gap) / (ImGui::GetFontSize() * 9.0f + gap)));
    float cell = std::min(ImGui::GetFontSize() * 14.0f, std::max(ImGui::GetFontSize() * 6.0f, (avail - gap * (perRow - 1)) / perRow));
    for (int s = 0; s < 10; s++) {
        if (s % perRow != 0) ImGui::SameLine();
        ImGui::BeginGroup();
        ImGui::PushID(s);
        bool exists = app.slotExists(s);
        const SlotThumb* t = exists ? app.slotThumb(s) : nullptr;
        ImVec2 box(cell, cell * 0.75f);
        if (t && t->tex) {
            ImGui::Image(ImTextureRef((ImTextureID)(intptr_t)t->tex), box);
        } else {
            ImGui::Dummy(box);
            ImVec2 a = ImGui::GetItemRectMin(), b = ImGui::GetItemRectMax();
            ImGui::GetWindowDrawList()->AddRect(a, b, ui::u32(ui::pal().emptyEdge));
            const char* txt = exists ? "no preview" : "empty";
            ImVec2 ts = ImGui::CalcTextSize(txt);
            ImGui::GetWindowDrawList()->AddText(ImVec2(a.x + (box.x - ts.x) * 0.5f, a.y + (box.y - ts.y) * 0.5f),
                                                ui::u32(ui::pal().textMuted), txt);
        }
        std::string head = "Slot " + std::to_string(s) + (s == app.stateSlot ? "  <" : "");
        ImGui::TextUnformatted(head.c_str());
        if (exists) {
            SDL_DateTime dt;
            if (SDL_TimeToDateTime(app.slotTime(s), &dt, true))
                ImGui::TextDisabled("%04d-%02d-%02d %02d:%02d", dt.year, dt.month, dt.day, dt.hour, dt.minute);
        } else {
            ImGui::TextDisabled(" ");
        }
        if (ImGui::SmallButton("Save")) { app.stateSlot = s; app.saveState(s); }
        ImGui::SameLine();
        ImGui::BeginDisabled(!exists);
        if (ImGui::SmallButton("Load")) { app.stateSlot = s; app.loadState(s); }
        ImGui::EndDisabled();
        ImGui::PopID();
        ImGui::EndGroup();
    }
    ImGui::End();
}

// "Resume from <time>?", after a game with an auto-save booted (Settings > Interface > Resume where I left off = Ask).
void drawResumePrompt(App& app) {
    static bool always = false;
    if (!app.resumeOffer.active) return;
    if (!ImGui::IsPopupOpen("Resume where you left off?")) {
        always = false;
        ImGui::OpenPopup("Resume where you left off?");
    }
    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    if (ImGui::BeginPopupModal("Resume where you left off?", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        std::string when;
        SDL_DateTime dt;
        if (SDL_TimeToDateTime(app.resumeOffer.savedAt, &dt, true)) {
            char b[40];
            std::snprintf(b, sizeof b, "%04d-%02d-%02d %02d:%02d", dt.year, dt.month, dt.day, dt.hour, dt.minute);
            when = b;
        }
        ImGui::Text("Resume %s from %s?", app.resumeOffer.game.c_str(), when.c_str());
        ImGui::Checkbox("Always resume without asking", &always);
        if (ImGui::Button("Resume")) {
            app.answerResume(true, always);
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Start fresh")) {
            app.answerResume(false, false);
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
}

void drawAbout(App& app) {
    ui::nextWindow(30.0f, 0.0f, 0.0f, 0.0f, ui::WinFit::FixedWidth);
    bool open = ImGui::Begin("About Virtual Jaguar", &app.showAbout, ImGuiWindowFlags_AlwaysAutoResize);
    ui::keepOnScreen();
    if (!open) {
        ImGui::End();
        return;
    }
    ImGui::Text("Virtual Jaguar %s", VJ_APP_VERSION);
    ImGui::TextDisabled("Atari Jaguar emulator. GPLv3.");
    ImGui::Separator();
    ImGui::TextUnformatted("Authors");
    ImGui::PushTextWrapPos(0.0f);   // to the window's content width
    ImGui::TextWrapped("Virtual Jaguar app and core: Joseph Mattiello (@JoeMatt)");
    ImGui::TextDisabled("Based on the original Virtual Jaguar by James Hammons (Shamus), after David Raingeard's "
                        "original and the SDLEMU port by Niels Wagenaar and Carwin Jones.");
    ImGui::TextDisabled("libretro port: libretro/RetroArch contributors.");
    ImGui::PopTextWrapPos();
    ImGui::TextLinkOpenURL("All contributors", "https://github.com/libretro/virtualjaguar-libretro/graphs/contributors");
    ImGui::Separator();
    ImGui::Text("Core:    %s %s", app.host.coreName()[0] ? app.host.coreName() : "Virtual Jaguar",
                app.host.coreVersion());
    int sdl = SDL_GetVersion();
    ImGui::Text("SDL:     %d.%d.%d", SDL_VERSIONNUM_MAJOR(sdl), SDL_VERSIONNUM_MINOR(sdl), SDL_VERSIONNUM_MICRO(sdl));
    ImGui::Text("Dear ImGui: %s", ImGui::GetVersion());
    ImGui::Separator();
    const char* const keys[][2] = {
        {"F1", platform::nativeMenuActive() ? "show the in-window menu bar as well" : "show or pin the menu bar (fullscreen)"},
        {"F5 / F7", "save / load state"},
        {"F6", "next state slot (Shift: previous)"},
        {"F8", "pause"},
        {"F11, Alt+Enter", "fullscreen"},
        {"Tab (hold)", "fast-forward"},
        {"Ctrl/Cmd+O", "open a game"},
        {"Ctrl/Cmd+L", "game library"},
        {"Ctrl/Cmd+N", "open another window (a second copy of the app)"},
        {"Ctrl/Cmd+wheel, +/-/0", "library tile size (trackpad: pinch)"},
        {"Ctrl/Cmd+R", "reset"},
    };
    if (ImGui::BeginTable("##keys", 2, ImGuiTableFlags_SizingFixedFit)) {
        ImGui::TableSetupColumn("##k", ImGuiTableColumnFlags_WidthFixed, ImGui::GetFontSize() * 9.0f);
        ImGui::TableSetupColumn("##a", ImGuiTableColumnFlags_WidthStretch);
        for (const auto& kv : keys) {
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(kv[0]);
            ImGui::TableNextColumn();
            ImGui::TextWrapped("%s", kv[1]);
        }
        ImGui::EndTable();
    }
    ImGui::End();
}

// ---- menu bar -----------------------------------------------------------------------------------

void drawMenuBar(App& app) {
    CoreHost& host = app.host;
    if (!ImGui::BeginMainMenuBar()) return;
    if (ImGui::BeginMenu("File")) {
        if (ImGui::MenuItem("Open...", "Ctrl+O")) app.showOpenDialog();
        if (ImGui::MenuItem("New window", "Ctrl+N")) app.newWindow();
        if (ImGui::MenuItem("Game library...", "Ctrl+L", app.library.show && !app.homeVisible())) app.toggleLibrary();
        if (ImGui::BeginMenu("Open recent", !app.recents.empty())) {
            std::vector<std::string> list = app.recents;  // opening reorders the list
            for (size_t i = 0; i < list.size(); i++) {
                std::string label = pathStem(list[i]) + "##r" + std::to_string(i);
                if (ImGui::MenuItem(label.c_str())) app.openContent(list[i]);
                if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", list[i].c_str());
            }
            ImGui::Separator();
            if (ImGui::MenuItem("Clear list")) app.recents.clear();
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("Boot BIOS")) {
            if (ImGui::MenuItem("CD BIOS (no disc)")) app.bootBios(bios::Cd);
            if (ImGui::MenuItem("Cartridge boot ROM (empty slot)")) app.bootBios(bios::CartBoot);
            ImGui::Separator();
            if (ImGui::MenuItem("BIOS & boot settings...")) {
                app.showSettings = true;
                app.devSelectTab = "BIOS";
            }
            ImGui::EndMenu();
        }
        ImGui::Separator();
        if (ImGui::MenuItem("Close game", nullptr, false, host.isLoaded() && !host.inFrame())) app.closeContent();
        ImGui::Separator();
        if (ImGui::MenuItem("Quit")) app.quit = true;
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("Emulation")) {
        bool loaded = host.isLoaded() && !host.inFrame();   // a debugger halt parks the core: nothing that re-enters it
        if (ImGui::MenuItem(app.paused ? "Resume" : "Pause", "F8", false, loaded)) app.togglePause();
        if (ImGui::MenuItem("Reset", "Ctrl+R", false, loaded)) app.reset();
        ImGui::MenuItem("Fast-forward (hold Tab)", nullptr, &app.fastForward, loaded);
        ImGui::Separator();
        if (ImGui::MenuItem("Save state", "F5", false, loaded)) app.saveState(app.stateSlot);
        if (ImGui::MenuItem("Load state", "F7", false, loaded && app.slotExists(app.stateSlot))) app.loadState(app.stateSlot);
        if (ImGui::BeginMenu("Slot")) {
            for (int s = 0; s < 10; s++) {
                std::string l = "Slot " + std::to_string(s) + (app.slotExists(s) ? "  (used)" : "");
                if (ImGui::MenuItem(l.c_str(), nullptr, app.stateSlot == s)) app.stateSlot = s;
            }
            ImGui::EndMenu();
        }
        if (ImGui::MenuItem("State manager...", nullptr, false, loaded)) app.showStates = true;
        if (ImGui::MenuItem("Cheats...", nullptr, false, loaded)) app.showCheats = true;
        DiscInfo disc = host.discInfo();
        if (disc.available) {
            ImGui::Separator();
            if (ImGui::MenuItem(disc.ejected ? "Close tray" : "Open tray", nullptr, false)) host.discSetEject(!disc.ejected);
            if (disc.count > 1 && ImGui::BeginMenu("Disc")) {
                for (unsigned i = 0; i < disc.count; i++) {
                    ImGui::PushID((int)i);   // a multi-disc set can repeat a label
                    if (ImGui::MenuItem(disc.labels[i].c_str(), nullptr, disc.index == i)) host.discSetIndex(i);
                    ImGui::PopID();
                }
                ImGui::EndMenu();
            }
        }
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("View")) {
        if (ImGui::MenuItem("Fullscreen", "F11", app.fullscreen)) app.setFullscreen(!app.fullscreen);
        ImGui::MenuItem("Integer scaling", nullptr, &app.present.integerScale);
        ImGui::MenuItem("Smooth filter", nullptr, &app.present.linearFilter);
        if (ImGui::MenuItem("Shader", nullptr, &app.video.shader.enabled)) app.video.shader.retry();
        ImGui::MenuItem("Show frame rate", nullptr, &app.showFps);
        ImGui::MenuItem("Keypad overlay", "F2", &app.input.overlay.open);
        if (ImGui::MenuItem("Controls overlay", "F3", app.input.controls.show)) {
            overlayEvent(app.input.controls, OverlayEvent::Toggle);
        }
        ImGui::MenuItem("Library on home screen", nullptr, &app.homeLibrary);
        if (ImGui::MenuItem("VSync", nullptr, &app.vsync)) app.applyVsync();
        ImGui::EndMenu();
    }
    drawToolsMenu(app);
    if (ImGui::BeginMenu("Settings")) {
        if (ImGui::MenuItem("Settings...", "Ctrl+,")) app.showSettings = true;
        if (ImGui::MenuItem("Mute", nullptr, &app.mute)) app.applyAudioSettings();
        ImGui::EndMenu();
    }
    drawAchievementsMenu(app);
    drawNetplayMenu(app);
    if (ImGui::BeginMenu("Help")) {
        if (ImGui::MenuItem("About")) app.showAbout = true;
        ImGui::EndMenu();
    }
    ImGui::EndMainMenuBar();
}

// ---- OSD ----------------------------------------------------------------------------------------------

void drawOsd(App& app, ImVec2 areaPos, ImVec2 areaSize) {
    ImDrawList* dl = ImGui::GetForegroundDrawList();
    double t = app.now();
    std::vector<Osd>& list = app.osd();
    list.erase(std::remove_if(list.begin(), list.end(), [&](const Osd& o) { return t - o.born > o.life; }), list.end());
    float y = areaPos.y + areaSize.y - ImGui::GetFontSize() * 0.8f;
    float pad = ImGui::GetFontSize() * 0.4f;
    // Long messages (an error with a path in it) wrap instead of running off the window.
    float wrap = std::max(ImGui::GetFontSize() * 8.0f, std::min(ImGui::GetFontSize() * 44.0f, areaSize.x - ImGui::GetFontSize() * 2.0f - pad * 2));
    for (size_t i = list.size(); i-- > 0;) {
        const Osd& o = list[i];
        float age = (float)(t - o.born), left = (float)o.life - age;
        float a = std::min(1.0f, std::min(left / 0.5f, age / 0.1f + 0.3f));
        ImVec2 ts = ImGui::CalcTextSize(o.text.c_str(), nullptr, false, wrap);
        y -= ts.y + pad * 2;
        ImVec2 p0(areaPos.x + ImGui::GetFontSize() * 0.6f, y);
        ImVec2 p1(p0.x + ts.x + pad * 2, y + ts.y + pad * 2);
        // Over the game picture (or the home screen): the dark-translucent overlay palette in both themes.
        const theme::Overlay& ov = ui::overlay();
        dl->AddRectFilled(p0, p1, ui::u32(ov.osdBg, a), 4.0f);
        ImU32 col = o.level >= 3 ? ui::u32(ov.osdError, a) : o.level == 2 ? ui::u32(ov.osdWarn, a) : ui::u32(ov.osdText, a);
        dl->AddText(nullptr, 0.0f, ImVec2(p0.x + pad, p0.y + pad), col, o.text.c_str(), nullptr, wrap);
        y -= pad * 0.5f;
    }
}

}  // namespace

void forgetTypedOptionState(const std::string* key) {
    if (key) typedStateMap().erase(*key);
    else typedStateMap().clear();
}

void drawUi(App& app, bool& wantCaptureKeyboard) {
    ImGuiIO& io = ImGui::GetIO();
    ImGuiViewport* vp = ImGui::GetMainViewport();
    ImDrawList* bg = ImGui::GetBackgroundDrawList();
    // Black behind the game (the letterbox bars); the theme's home colour behind the library / welcome screen.
    bg->AddRectFilled(vp->Pos, ImVec2(vp->Pos.x + vp->Size.x, vp->Pos.y + vp->Size.y),
                      app.host.isLoaded() ? IM_COL32(0, 0, 0, 255) : ui::u32(ui::pal().homeBg));

    // The menu bar is always there in a window; in fullscreen it shows while
    // pinned (F1), while the pointer is at the top edge, or while a menu is open.
    static bool menuHot = false;
    const float mouseY = io.MousePos.y - vp->Pos.y;   // (with extra OS windows on, ImGui's coordinates are the screen's)
    bool nearTop = mouseY >= 0 && mouseY < (menuHot ? ImGui::GetFrameHeight() + 4 : 4);
    menuHot = nearTop || ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopupId);
    // A native menu bar (macOS) replaces this one; every item here has a twin in platform/commands.cpp.
    bool showMenu = platform::nativeMenuActive() ? app.showInWindowMenu : (!app.fullscreen || app.menuPinned || menuHot);
    float menuH = 0;
    if (showMenu) {
        drawMenuBar(app);
        menuH = ImGui::GetFrameHeight();
    }

    ImVec2 areaPos(vp->Pos.x, vp->Pos.y + menuH);
    ImVec2 areaSize(vp->Size.x, vp->Size.y - menuH);
    if (app.host.isLoaded() && app.video.hasFrame()) {
        ImVec4 pic = app.video.draw(bg, areaPos, areaSize, app.host.aspect(), app.present,
                                    io.DisplayFramebufferScale.x);
        app.input.setPicture(pic.x - vp->Pos.x, pic.y - vp->Pos.y, pic.z, pic.w);   // window-relative, like SDL's pointer events   // where the picture is, for the light gun
    } else if (!app.host.isLoaded()) {
        if (app.homeLibrary) {
            drawLibraryHome(app, areaPos.x, areaPos.y, areaSize.x, areaSize.y);   // the game library is the home screen
        } else {
            // The plain welcome screen (Settings > Interface turns the home-screen library off).
            const char* l1 = "Virtual Jaguar";
            const char* l2 = "Drop a Jaguar ROM or CD image here, or use File > Open or File > Game library";
            float wrap = std::max(ImGui::GetFontSize() * 10.0f, areaSize.x - ImGui::GetFontSize() * 4.0f);
            ImVec2 s1 = ImGui::CalcTextSize(l1), s2 = ImGui::CalcTextSize(l2, nullptr, false, wrap);
            float cx = areaPos.x + areaSize.x * 0.5f, cy = areaPos.y + areaSize.y * 0.5f;
            bg->AddText(ImVec2(cx - s1.x * 0.5f, cy - s1.y * 1.5f), ui::u32(ui::pal().text), l1);
            bg->AddText(nullptr, 0.0f, ImVec2(cx - s2.x * 0.5f, cy + s1.y * 0.2f), ui::u32(ui::pal().textMuted), l2,
                        nullptr, wrap);
        }
    }

    shaderFrameHook(app);   // file-dialog results and shader notices

    if (app.showFps) {
        char buf[64];
        std::snprintf(buf, sizeof buf, "%.1f fps%s", app.fpsMeasured(), app.fastForward ? "  >>" : "");
        // A chip behind the number: it can sit over a white game frame or the light home screen.  On the
        // background list, right after the game picture, so every ImGui window, menu and modal covers it
        // (the foreground list would draw over them, #934).
        ImVec2 ts = ImGui::CalcTextSize(buf), at(areaPos.x + 8, areaPos.y + 6);
        bg->AddRectFilled(ImVec2(at.x - 4, at.y - 2), ImVec2(at.x + ts.x + 4, at.y + ts.y + 2), ui::u32(ui::overlay().fpsBg), 3.0f);
        bg->AddText(at, ui::u32(ui::overlay().fpsText), buf);
    }

    if (app.showSettings) drawSettings(app);
    if (app.showStates) drawStates(app);
    if (app.showCheats) drawCheats(app);
    if (app.showNetplay) drawNetplay(app);
    if (app.showAbout) drawAbout(app);
    if (app.showThemeEditor) drawThemeEditor(app);
    themeFrameHook(app);   // theme file-dialog results
    drawResumePrompt(app);
    drawLibrary(app);
    drawKeypadOverlay(app);
    drawControlsOverlay(app, areaPos, areaSize);
    drawAchievementsUi(app);
    drawDebugWindows(app);
    app.tvShell().draw(tv::Layer::Menu, vp->Pos, vp->Size);   // the in-game menu over everything but the OSD (no window while it is closed)
    drawOsd(app, areaPos, areaSize);
    drawDebugOverlay(app, areaPos.x, areaPos.y);

    wantCaptureKeyboard = io.WantCaptureKeyboard;
}

}  // namespace vj
