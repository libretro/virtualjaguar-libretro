#include "settings_reset_ui.h"

#include <algorithm>
#include <cstdio>

#include "app.h"
#include "devices_ui.h"
#include "imgui.h"
#include "ui_util.h"

namespace vj {

namespace {

OptionResetPlan planFor(CoreHost& host, const std::string* cat) {
    return planOptionReset(
        host.optionDefs(), host.userOptions(), cat, [&](const std::string& k) { return host.optionLocked(k); },
        [&](const std::string& k) { return host.optionOverridden(k); });
}

bool has(const std::vector<KeyClass>& v, KeyClass c) { return std::find(v.begin(), v.end(), c) != v.end(); }

// Members -> config before an erase (the Settings widgets edit the members and the config only hears
// about it at save time, so re-reading an unsynced config would undo another tab's change), then
// config -> members after, and the side effects each member needs.
void syncToConfig(App& app) { app.saveConfig(); }

void applyAfterErase(App& app, const std::vector<KeyClass>& touched) {
    app.applySettingsFromConfig();
    if (has(touched, KeyClass::Storage)) app.library.resetDisplaySettings();
    if (has(touched, KeyClass::Shader)) {
        app.video.shader.clearPreset();   // the shader switch and preset came back from the config; drop the loaded chain
        app.video.shader.retry();
    }
    if (has(touched, KeyClass::Video)) app.applyVsync();
    if (has(touched, KeyClass::Audio)) app.applyAudioSettings();
    if (has(touched, KeyClass::Input) || has(touched, KeyClass::Mapping) || has(touched, KeyClass::PerGame)) {
        app.input.reresolve();
        applyPortDevices(app);
    }
    app.saveConfig();
}

std::string optionsSummary(const OptionResetPlan& p, bool loaded) {
    std::string s = "Reset " + std::to_string(p.reset.size()) + (p.reset.size() == 1 ? " option" : " options");
    if (!p.skippedOverridden.empty())
        s += ", kept " + std::to_string(p.skippedOverridden.size()) + " set for this session";
    s += ".";
    if (loaded && p.needsRestart) s += " 'Restart required' options apply on the next load.";
    return s;
}

}  // namespace

void resetAppGroup(App& app, AppGroup g) {
    syncToConfig(app);
    const std::vector<KeyClass> cls = groupClasses(g);
    resetGroupKeys(app.cfg, g);
    applyAfterErase(app, cls);
    app.toast(std::string(groupName(g)) + " settings reset to their defaults.");
}

OptionResetPlan resetCoreOptionGroup(App& app, const std::string* categoryKey) {
    OptionResetPlan p = resetCoreOptions(app.host, categoryKey);
    for (const std::string& k : p.reset) forgetTypedOptionState(&k);
    app.saveConfig();
    return p;
}

void resetEverything(App& app, const ResetAllRequest& r) {
    if (!r.confirmed) return;
    syncToConfig(app);
    const OptionResetPlan p = resetCoreOptions(app.host, nullptr);
    forgetTypedOptionState(nullptr);
    resetAllAppKeys(app.cfg, r);
    applyAfterErase(app, classesFor(r));
    app.toast("All settings reset to their defaults." +
              (app.host.isLoaded() && p.needsRestart ? std::string(" 'Restart required' options apply on the next load.") : std::string()));
}

// ---- ImGui -----------------------------------------------------------------------------------------------------

namespace {

// A SmallButton pushed to the right edge of the row.
bool rightButton(const char* label) {
    const float w = ImGui::CalcTextSize(label).x + ImGui::GetStyle().FramePadding.x * 2.0f;
    const float x = ImGui::GetCursorPosX() + std::max(0.0f, ImGui::GetContentRegionAvail().x - w);
    ImGui::SetCursorPosX(x);
    return ImGui::SmallButton(label);
}

const char* groupNote(AppGroup g) {
    switch (g) {
    case AppGroup::Video: return "Picture options and the shader switch and preset (the shader folder is kept).";
    case AppGroup::Audio: return "Volume, mute and buffer target.";
    case AppGroup::Input:
        return "Controller ports, players, tuning and the controls overlay, for everywhere. Key and button bindings "
               "and per-game choices are not changed; Reset all settings... can include them.";
    case AppGroup::Storage:
        return "Flush saves, home-screen library, the library's tile size, art download, hide-on-launch and Favorites / Recently played display, and Resume where I left off. "
               "Library folders are not changed.";
    }
    return "";
}

}  // namespace

void drawAppGroupReset(App& app, AppGroup g) {
    ImGui::PushID((int)g + 7000);
    if (rightButton("Reset group")) resetAppGroup(app, g);
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort)) {
        ui::beginWrappedTooltip();
        ImGui::Text("Put every %s setting back to its default.", groupName(g));
        ImGui::TextUnformatted(groupNote(g));
        if (app.paths.instance > 1) ImGui::TextUnformatted("This window is a secondary copy: the change lasts for this session only.");
        ui::endWrappedTooltip();
    }
    ImGui::PopID();
}

void drawCoreGroupReset(App& app, const std::string& categoryKey, bool filterActive) {
    CoreHost& host = app.host;
    const OptionResetPlan p = planFor(host, &categoryKey);
    ImGui::PushID("##resetgroup");
    ImGui::BeginDisabled(p.reset.empty());
    if (ImGui::SmallButton("Reset group")) {
        const OptionResetPlan done = resetCoreOptionGroup(app, &categoryKey);
        app.toast(optionsSummary(done, host.isLoaded()));
    }
    ImGui::EndDisabled();
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort | ImGuiHoveredFlags_AllowWhenDisabled)) {
        ui::beginWrappedTooltip();
        if (p.reset.empty()) ImGui::TextUnformatted("Every option in this group is already at its default.");
        else ImGui::Text("Put the %zu changed option%s in this group back to the default.", p.reset.size(), p.reset.size() == 1 ? "" : "s");
        if (filterActive) ImGui::TextUnformatted("The whole group is reset, not only the options the filter shows.");
        if (!p.skippedOverridden.empty())
            ImGui::Text("%zu option%s set by the Netplay window (or --option) for this session %s skipped.",
                        p.skippedOverridden.size(), p.skippedOverridden.size() == 1 ? "" : "s",
                        p.skippedOverridden.size() == 1 ? "is" : "are");
        if (!p.skippedLocked.empty())
            ImGui::Text("%zu option%s managed by the app (Input tabs) %s skipped.", p.skippedLocked.size(),
                        p.skippedLocked.size() == 1 ? "" : "s", p.skippedLocked.size() == 1 ? "is" : "are");
        if (app.paths.instance > 1) ImGui::TextUnformatted("This window is a secondary copy: the change lasts for this session only.");
        ui::endWrappedTooltip();
    }
    ImGui::PopID();
}

void drawResetAllSettings(App& app) {
    static const char* kTitle = "Reset all settings";
    static bool alsoMappings = false, alsoPerGame = false;
    if (rightButton("Reset all settings...")) {
        alsoMappings = alsoPerGame = false;   // the destructive extras are never remembered between openings
        ImGui::OpenPopup(kTitle);
    }

    ImGuiViewport* vp = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(ImVec2(vp->WorkPos.x + vp->WorkSize.x * 0.5f, vp->WorkPos.y + vp->WorkSize.y * 0.5f),
                            ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    const float w = std::min(ui::em(34.0f), vp->WorkSize.x * 0.92f);
    ImGui::SetNextWindowSizeConstraints(ImVec2(w, 0.0f), ImVec2(w, vp->WorkSize.y * 0.92f));
    if (!ImGui::BeginPopupModal(kTitle, nullptr, ImGuiWindowFlags_AlwaysAutoResize)) return;

    CoreHost& host = app.host;
    const OptionResetPlan plan = planFor(host, nullptr);
    ImGui::TextWrapped("Put the settings back to their defaults:");
    ImGui::BulletText("Core options: %zu saved value%s%s", plan.reset.size(), plan.reset.size() == 1 ? "" : "s",
                      plan.skippedOverridden.empty() ? "" : " (those set for this session by Netplay or --option are kept)");
    ImGui::BulletText("Video and shader, Audio");
    ImGui::BulletText("Input devices: controller ports, players, tuning, controls overlay");
    ImGui::BulletText("Storage: flush saves, home-screen library, library tile size, art and Favorites / Recently played options, resume setting");
    ImGui::Spacing();
    ImGui::TextWrapped("Not touched: library folders, favorites and recent games, covers, save states and game saves, "
                       "RetroAchievements login and settings, netplay settings, cheats, window size.");
    ImGui::Spacing();
    ImGui::Checkbox("Also reset controller mappings", &alsoMappings);
    ui::hint("Key and button bindings (everywhere and per game) and 'left stick also drives the D-pad'.");
    ImGui::Checkbox("Also reset per-game overrides", &alsoPerGame);
    ui::hint("Controller ports, players and tuning chosen for single games.");
    if (app.paths.instance > 1)
        ui::hint("This window is a secondary copy: the reset applies to this session only and the shared settings file is not changed.");
    ImGui::Separator();
    bool done = false;
    if (ImGui::Button("Reset")) {
        ResetAllRequest r;
        r.alsoMappings = alsoMappings;
        r.alsoPerGame = alsoPerGame;
        r.confirmed = true;   // the only place a request is confirmed: the button inside the modal
        resetEverything(app, r);
        done = true;
    }
    ImGui::SameLine();
    if (ImGui::Button("Cancel") || ImGui::IsKeyPressed(ImGuiKey_Escape)) done = true;
    if (done) ImGui::CloseCurrentPopup();
    ImGui::EndPopup();
}

}  // namespace vj
