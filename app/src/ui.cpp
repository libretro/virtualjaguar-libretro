// All ImGui for the app: menu bar, the settings window (including the core
// options menu generated from the options v2 definitions), the save-state
// manager, the on-screen messages.
#include "app.h"

#include "imgui.h"
#include "shaders.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>

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

void drawOption(App& app, const OptionDef& d, bool inCategory) {
    CoreHost& host = app.host;
    std::string cur = host.optionValue(d);
    const std::string& title = (inCategory && !d.descCategorized.empty()) ? d.descCategorized : d.desc;
    const std::string& info = (inCategory && !d.infoCategorized.empty()) ? d.infoCategorized : d.info;

    ImGui::PushID(d.key.c_str());
    ImGui::SetNextItemWidth(ImGui::GetFontSize() * 11.0f);
    if (ImGui::BeginCombo("##v", labelFor(d, cur))) {
        for (const OptionValue& v : d.values) {
            bool sel = (v.value == cur);
            if (ImGui::Selectable(v.label.empty() ? v.value.c_str() : v.label.c_str(), sel))
                host.setOptionValue(d.key, v.value);
            if (sel) ImGui::SetItemDefaultFocus();
        }
        ImGui::EndCombo();
    }
    ImGui::SameLine();
    ImGui::TextUnformatted(title.c_str());
    if (!info.empty() && ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort)) {
        ImGui::BeginTooltip();
        ImGui::PushTextWrapPos(ImGui::GetFontSize() * 30.0f);
        ImGui::TextUnformatted(info.c_str());
        ImGui::PopTextWrapPos();
        ImGui::EndTooltip();
    }
    if (host.userOptions().count(d.key)) {
        ImGui::SameLine();
        if (ImGui::SmallButton("reset")) host.resetOptionToDefault(d.key);
    }
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
        ImGui::SameLine();
        ImGui::TextDisabled("'Restart required' options apply on the next load.");
    }
    ImGui::SetNextItemWidth(ImGui::GetFontSize() * 14.0f);
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
    PresentOptions& p = app.present;
    ImGui::Checkbox("Integer scaling", &p.integerScale);
    ImGui::Checkbox("Smooth (bilinear) filter", &p.linearFilter);
    ImGui::SameLine();
    ImGui::TextDisabled("(off when integer scaling)");
    ImGui::Checkbox("Stretch to window (ignore aspect ratio)", &p.stretch);
    static const char* aspects[] = {"Core default", "4:3", "16:9", "8:7 (square pixels)"};
    static const float vals[] = {0.0f, 4.0f / 3.0f, 16.0f / 9.0f, 8.0f / 7.0f};
    int sel = 0;
    for (int i = 0; i < 4; i++)
        if (std::abs(p.aspectOverride - vals[i]) < 1e-3f) sel = i;
    ImGui::SetNextItemWidth(ImGui::GetFontSize() * 11.0f);
    if (ImGui::Combo("Aspect ratio", &sel, aspects, 4)) p.aspectOverride = vals[sel];
    if (ImGui::Checkbox("VSync", &app.vsync)) app.applyVsync();
    bool fs = app.fullscreen;
    if (ImGui::Checkbox("Fullscreen (F11)", &fs)) app.setFullscreen(fs);
    ImGui::Checkbox("Show frame rate", &app.showFps);
    drawShaderSettings(app);
}

void drawAudioTab(App& app) {
    bool changed = false;
    changed |= ImGui::SliderFloat("Volume", &app.volume, 0.0f, 1.0f, "%.2f");
    changed |= ImGui::Checkbox("Mute", &app.mute);
    changed |= ImGui::SliderFloat("Buffer target (ms)", &app.audioTargetMs, 20.0f, 200.0f, "%.0f");
    if (changed) app.applyAudioSettings();
    ImGui::Separator();
    ImGui::Text("Buffered: %.1f ms   playback ratio: %.5f   underruns: %llu", app.audio.queuedMs(),
                app.audio.ratio(), (unsigned long long)app.audio.underruns());
    if (app.host.isLoaded())
        ImGui::Text("Core rate: %.2f Hz at %.5f fields/s", app.host.sampleRate(), app.host.fps());
}

void drawInputTab(App& app) {
    drawControlsTab(app);
    ImGui::Separator();
    const auto& ports = app.host.controllerPorts();
    if (!app.host.isLoaded() || ports.empty()) {
        ImGui::TextDisabled("Controller types are offered once a game is loaded.");
        return;
    }
    for (unsigned p = 0; p < 2 && p < ports.size(); p++) {
        std::string key = "port." + std::to_string(p);
        int cur = app.cfg.getInt(key, -1);
        const char* shown = "Standard Joypad";
        for (const ControllerType& t : ports[p])
            if ((int)t.id == cur) shown = t.desc.c_str();
        char label[32];
        std::snprintf(label, sizeof label, "Jaguar port %u", p + 1);
        ImGui::SetNextItemWidth(ImGui::GetFontSize() * 18.0f);
        if (ImGui::BeginCombo(label, shown)) {
            for (const ControllerType& t : ports[p]) {
                if (ImGui::Selectable(t.desc.c_str(), (int)t.id == cur)) {
                    app.cfg.setInt(key, (int)t.id);
                    app.host.setControllerDevice(p, t.id);
                }
            }
            ImGui::EndCombo();
        }
    }
}

void drawStorageTab(App& app) {
    ImGui::Text("Data folder%s:", app.paths.portable ? " (portable mode)" : "");
    ImGui::TextWrapped("%s", app.paths.root.c_str());
    ImGui::Separator();
    ImGui::Text("Saves:   %s", app.paths.saves.c_str());
    ImGui::Text("States:  %s", app.paths.states.c_str());
    ImGui::Text("System:  %s", app.paths.system.c_str());
    ImGui::Separator();
    ImGui::Checkbox("Flush saves (SRAM / EEPROM / Memory Track) every few seconds", &app.autosaveSram);
    ImGui::TextDisabled("HLE boot is the default, so no BIOS file is needed.");
}

void drawSettings(App& app) {
    ImGui::SetNextWindowSize(ImVec2(ImGui::GetFontSize() * 42.0f, ImGui::GetFontSize() * 30.0f), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Settings", &app.showSettings)) {
        ImGui::End();
        return;
    }
    if (ImGui::BeginTabBar("##tabs")) {
        if (ImGui::BeginTabItem("Core options")) { drawCoreOptions(app); ImGui::EndTabItem(); }
        if (ImGui::BeginTabItem("Video", nullptr, app.devSelectVideoTab ? ImGuiTabItemFlags_SetSelected : 0)) {
            app.devSelectVideoTab = false;
            drawVideoTab(app);
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Audio")) { drawAudioTab(app); ImGui::EndTabItem(); }
        if (ImGui::BeginTabItem("Input")) { drawInputTab(app); ImGui::EndTabItem(); }
        if (ImGui::BeginTabItem("Storage")) { drawStorageTab(app); ImGui::EndTabItem(); }
        ImGui::EndTabBar();
    }
    ImGui::End();
}

// ---- save-state manager --------------------------------------------------------------------

void drawStates(App& app) {
    ImGui::SetNextWindowSize(ImVec2(ImGui::GetFontSize() * 44.0f, ImGui::GetFontSize() * 30.0f), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Save states", &app.showStates)) {
        ImGui::End();
        return;
    }
    if (!app.host.isLoaded()) {
        ImGui::TextDisabled("Load a game to use save states.");
        ImGui::End();
        return;
    }
    ImGui::Text("%s", app.host.contentName().c_str());
    float cell = ImGui::GetFontSize() * 9.5f;
    int perRow = std::max(1, (int)(ImGui::GetContentRegionAvail().x / (cell + ImGui::GetStyle().ItemSpacing.x)));
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
            ImGui::GetWindowDrawList()->AddRect(a, b, IM_COL32(120, 120, 120, 255));
            const char* txt = exists ? "no preview" : "empty";
            ImVec2 ts = ImGui::CalcTextSize(txt);
            ImGui::GetWindowDrawList()->AddText(ImVec2(a.x + (box.x - ts.x) * 0.5f, a.y + (box.y - ts.y) * 0.5f),
                                                IM_COL32(150, 150, 150, 255), txt);
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

void drawAbout(App& app) {
    if (!ImGui::Begin("About Virtual Jaguar", &app.showAbout, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::End();
        return;
    }
    ImGui::Text("Virtual Jaguar %s", VJ_APP_VERSION);
    ImGui::TextDisabled("Atari Jaguar emulator. GPLv3.");
    ImGui::Separator();
    ImGui::Text("Core:    %s %s", app.host.coreName()[0] ? app.host.coreName() : "Virtual Jaguar",
                app.host.coreVersion());
    int sdl = SDL_GetVersion();
    ImGui::Text("SDL:     %d.%d.%d", SDL_VERSIONNUM_MAJOR(sdl), SDL_VERSIONNUM_MINOR(sdl), SDL_VERSIONNUM_MICRO(sdl));
    ImGui::Text("Dear ImGui: %s", ImGui::GetVersion());
    ImGui::Separator();
    ImGui::TextUnformatted("F1 menu   F5/F7 save/load state   F6 next slot   F8 pause\n"
                           "F11 or Alt+Enter fullscreen   Tab fast-forward   Ctrl/Cmd+O open   Ctrl/Cmd+R reset");
    ImGui::End();
}

// ---- menu bar -----------------------------------------------------------------------------------

void drawMenuBar(App& app) {
    CoreHost& host = app.host;
    if (!ImGui::BeginMainMenuBar()) return;
    if (ImGui::BeginMenu("File")) {
        if (ImGui::MenuItem("Open...", "Ctrl+O")) app.showOpenDialog();
        if (ImGui::MenuItem("Game library...", "Ctrl+L", app.library.show)) app.library.show = !app.library.show;
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
        if (ImGui::MenuItem("Boot without cartridge")) app.openNoContent();
        ImGui::Separator();
        if (ImGui::MenuItem("Close game", nullptr, false, host.isLoaded())) app.closeContent();
        ImGui::Separator();
        if (ImGui::MenuItem("Quit")) app.quit = true;
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("Emulation")) {
        bool loaded = host.isLoaded();
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
                for (unsigned i = 0; i < disc.count; i++)
                    if (ImGui::MenuItem(disc.labels[i].c_str(), nullptr, disc.index == i)) host.discSetIndex(i);
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
        if (ImGui::MenuItem("VSync", nullptr, &app.vsync)) app.applyVsync();
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("Settings")) {
        if (ImGui::MenuItem("Settings...", "Ctrl+,")) app.showSettings = true;
        if (ImGui::MenuItem("Mute", nullptr, &app.mute)) app.applyAudioSettings();
        ImGui::EndMenu();
    }
    drawAchievementsMenu(app);
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
    for (size_t i = list.size(); i-- > 0;) {
        const Osd& o = list[i];
        float age = (float)(t - o.born), left = (float)o.life - age;
        float a = std::min(1.0f, std::min(left / 0.5f, age / 0.1f + 0.3f));
        ImVec2 ts = ImGui::CalcTextSize(o.text.c_str());
        y -= ts.y + pad * 2;
        ImVec2 p0(areaPos.x + ImGui::GetFontSize() * 0.6f, y);
        ImVec2 p1(p0.x + ts.x + pad * 2, y + ts.y + pad * 2);
        dl->AddRectFilled(p0, p1, IM_COL32(0, 0, 0, (int)(170 * a)), 4.0f);
        ImU32 col = o.level >= 3 ? IM_COL32(255, 120, 120, (int)(255 * a))
                  : o.level == 2 ? IM_COL32(255, 210, 110, (int)(255 * a))
                                 : IM_COL32(240, 240, 240, (int)(255 * a));
        dl->AddText(ImVec2(p0.x + pad, p0.y + pad), col, o.text.c_str());
        y -= pad * 0.5f;
    }
}

}  // namespace

void drawUi(App& app, bool& wantCaptureKeyboard) {
    ImGuiIO& io = ImGui::GetIO();
    ImGuiViewport* vp = ImGui::GetMainViewport();
    ImDrawList* bg = ImGui::GetBackgroundDrawList();
    bg->AddRectFilled(vp->Pos, ImVec2(vp->Pos.x + vp->Size.x, vp->Pos.y + vp->Size.y), IM_COL32(0, 0, 0, 255));

    // The menu bar is always there in a window; in fullscreen it shows while
    // pinned (F1), while the pointer is at the top edge, or while a menu is open.
    static bool menuHot = false;
    bool nearTop = io.MousePos.y >= 0 && io.MousePos.y < (menuHot ? ImGui::GetFrameHeight() + 4 : 4);
    menuHot = nearTop || ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopupId);
    bool showMenu = !app.fullscreen || app.menuPinned || menuHot;
    float menuH = 0;
    if (showMenu) {
        drawMenuBar(app);
        menuH = ImGui::GetFrameHeight();
    }

    ImVec2 areaPos(vp->Pos.x, vp->Pos.y + menuH);
    ImVec2 areaSize(vp->Size.x, vp->Size.y - menuH);
    if (app.host.isLoaded() && app.video.hasFrame()) {
        app.video.draw(bg, areaPos, areaSize, app.host.aspect(), app.present,
                       io.DisplayFramebufferScale.x);
    } else if (!app.host.isLoaded()) {
        const char* l1 = "Virtual Jaguar";
        const char* l2 = "Drop a Jaguar ROM or CD image here, or use File > Open or File > Game library";
        ImVec2 s1 = ImGui::CalcTextSize(l1), s2 = ImGui::CalcTextSize(l2);
        float cx = areaPos.x + areaSize.x * 0.5f, cy = areaPos.y + areaSize.y * 0.5f;
        bg->AddText(ImVec2(cx - s1.x * 0.5f, cy - s1.y * 1.5f), IM_COL32(230, 230, 230, 255), l1);
        bg->AddText(ImVec2(cx - s2.x * 0.5f, cy + s2.y * 0.2f), IM_COL32(150, 150, 150, 255), l2);
    }

    shaderFrameHook(app);   // file-dialog results and shader notices

    if (app.showFps) {
        char buf[64];
        std::snprintf(buf, sizeof buf, "%.1f fps%s", app.fpsMeasured(), app.fastForward ? "  >>" : "");
        ImGui::GetForegroundDrawList()->AddText(ImVec2(areaPos.x + 8, areaPos.y + 6), IM_COL32(255, 255, 0, 255), buf);
    }

    if (app.showSettings) drawSettings(app);
    if (app.showStates) drawStates(app);
    if (app.showCheats) drawCheats(app);
    if (app.showAbout) drawAbout(app);
    drawLibrary(app);
    drawKeypadOverlay(app);
    drawAchievementsUi(app);
    drawOsd(app, areaPos, areaSize);

    wantCaptureKeyboard = io.WantCaptureKeyboard;
}

}  // namespace vj
