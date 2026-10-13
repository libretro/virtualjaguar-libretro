// The Cheats window: pick a cheat set for the running game (matched by name
// from the libretro cheat database), toggle its cheats, add manual codes.
#include "app.h"
#include "cheats.h"

#include "imgui.h"

#include <algorithm>
#include <cstdio>
#include <cstring>

namespace vj {

namespace {

struct UiState {
    char desc[96] = "";
    char code[160] = "";
    char dir[512] = "";
    bool dirInit = false;
    std::string error;
    char filter[64] = "";
};
UiState g_ui;

std::string lowerCopy(std::string s) {
    for (char& c : s) c = (char)std::tolower((unsigned char)c);
    return s;
}

void drawSetPicker(App& app) {
    CheatManager& cm = app.cheats;
    const std::vector<ChtRef>& lib = cm.library();
    std::string current = cm.selectedStem().empty() ? "(no cheat set)" : cm.selectedStem();
    ImGui::SetNextItemWidth(ImGui::GetFontSize() * 24.0f);
    if (ImGui::BeginCombo("Cheat set", current.c_str())) {
        ImGui::InputTextWithHint("##setfilter", "Filter", g_ui.filter, sizeof g_ui.filter);
        std::string f = lowerCopy(g_ui.filter);
        if (ImGui::Selectable("(no cheat set)", cm.selectedStem().empty())) cm.selectSet("");
        auto emit = [&](const char* header, const std::vector<size_t>& idx) {
            if (idx.empty()) return;
            ImGui::SeparatorText(header);
            for (size_t i : idx) {
                if (!f.empty() && lowerCopy(lib[i].stem).find(f) == std::string::npos) continue;
                if (ImGui::Selectable(lib[i].stem.c_str(), lib[i].stem == cm.selectedStem())) cm.selectSet(lib[i].stem);
            }
        };
        emit("Matches this game", cm.matches());
        emit("Similar names", cm.suggestions());
        ImGui::SeparatorText("All sets");
        for (const ChtRef& r : lib) {
            if (!f.empty() && lowerCopy(r.stem).find(f) == std::string::npos) continue;
            if (ImGui::Selectable((r.stem + "##all").c_str(), r.stem == cm.selectedStem())) cm.selectSet(r.stem);
        }
        ImGui::EndCombo();
    }
    std::string st = cm.setStatus();
    if (!st.empty()) ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.4f, 1.0f), "%s", st.c_str());
}

void drawSetCheats(App& app) {
    CheatManager& cm = app.cheats;
    const ChtSet* set = cm.selectedSet();
    if (!set) return;
    ImGui::SeparatorText("Database cheats");
    if (set->cheats.empty()) ImGui::TextDisabled("This file has no cheats.");
    for (size_t i = 0; i < set->cheats.size(); i++) {
        const ChtCheat& c = set->cheats[i];
        ImGui::PushID((int)i);
        bool on = cm.isEnabled(c);
        ImGui::BeginDisabled(!c.supported);
        std::string label = c.desc + (c.parts > 1 ? "  (" + std::to_string(c.parts) + " parts)" : std::string());
        if (ImGui::Checkbox(label.c_str(), &on)) cm.setEnabled(c, on);
        ImGui::EndDisabled();
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
            if (c.supported) ImGui::SetTooltip("%s", c.code.c_str());
            else ImGui::SetTooltip("Not supported: %s", c.note.c_str());
        }
        ImGui::PopID();
    }
}

void drawUserCheats(App& app) {
    CheatManager& cm = app.cheats;
    ImGui::SeparatorText("Manual codes");
    const std::vector<UserCheat>& list = cm.userCheats();
    int removeIdx = -1;
    for (size_t i = 0; i < list.size(); i++) {
        ImGui::PushID((int)(10000 + i));
        bool on = list[i].enabled;
        if (ImGui::Checkbox(list[i].desc.c_str(), &on)) cm.setUserEnabled(i, on);
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", list[i].code.c_str());
        ImGui::SameLine();
        ImGui::TextDisabled("%s", list[i].code.c_str());
        ImGui::SameLine();
        if (ImGui::SmallButton("remove")) removeIdx = (int)i;
        ImGui::PopID();
    }
    if (removeIdx >= 0) cm.removeUser((size_t)removeIdx);

    ImGui::SetNextItemWidth(ImGui::GetFontSize() * 12.0f);
    ImGui::InputTextWithHint("##udesc", "Name", g_ui.desc, sizeof g_ui.desc);
    ImGui::SameLine();
    ImGui::SetNextItemWidth(ImGui::GetFontSize() * 16.0f);
    bool enter = ImGui::InputTextWithHint("##ucode", "Code, e.g. 00007D3D:05", g_ui.code, sizeof g_ui.code,
                                          ImGuiInputTextFlags_EnterReturnsTrue);
    ImGui::SameLine();
    if (ImGui::Button("Add") || enter) {
        std::string err;
        if (cm.addUser(g_ui.desc, g_ui.code, err)) {
            g_ui.desc[0] = g_ui.code[0] = 0;
            g_ui.error.clear();
        } else {
            g_ui.error = err;
        }
    }
    if (!g_ui.error.empty()) ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.5f, 1.0f), "%s", g_ui.error.c_str());
    ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
    ImGui::TextWrapped("Hex ADDRESS:VALUE; 2, 4 or 8 value digits write a byte, word or long. "
                       "Join several with '+'. Addresses are Jaguar bus addresses (RAM is 000000-1FFFFF).");
    ImGui::PopStyleColor();
}

void drawLibrary(App& app) {
    CheatManager& cm = app.cheats;
    if (!ImGui::CollapsingHeader("Cheat library")) return;
    if (!g_ui.dirInit) {
        std::snprintf(g_ui.dir, sizeof g_ui.dir, "%s", app.cfg.get("cheats.dir", "").c_str());
        g_ui.dirInit = true;
    }
    ImGui::Text("%zu cheat file%s found in:", cm.library().size(), cm.library().size() == 1 ? "" : "s");
    for (const std::string& d : cm.libraryDirs()) ImGui::BulletText("%s", d.c_str());
    ImGui::SetNextItemWidth(ImGui::GetFontSize() * 28.0f);
    ImGui::InputTextWithHint("##cdir", "Extra folder with .cht files", g_ui.dir, sizeof g_ui.dir);
    ImGui::SameLine();
    if (ImGui::Button("Use folder")) cheatsSetUserDir(app, g_ui.dir);
    if (ImGui::Button("Rescan")) cm.rescan();
    ImGui::SameLine();
    if (ImGui::Button("Open cheats folder")) {
        std::string d = app.paths.root + "cheats/";
        std::string url = "file://" + (d.size() > 1 && d[1] == ':' ? "/" + d : d);
        std::replace(url.begin(), url.end(), '\\', '/');
        SDL_OpenURL(url.c_str());
    }
    ImGui::TextWrapped(
        "Cheat sets are the .cht files of the libretro cheat database (libretro-database, folder "
        "cht/Atari - Jaguar). Builds bundle them in a 'cheats' folder next to the program; "
        "you can add your own to the cheats folder above or to any folder you point this at.");
    ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
    ImGui::TextWrapped("Database: libretro-database, CC BY-SA 4.0. Jaguar cheats by Gent (Emucheats).");
    ImGui::PopStyleColor();
}

}  // namespace

void drawCheats(App& app) {
    ImGui::SetNextWindowSize(ImVec2(ImGui::GetFontSize() * 40.0f, ImGui::GetFontSize() * 32.0f), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Cheats", &app.showCheats)) {
        ImGui::End();
        return;
    }
    CheatManager& cm = app.cheats;
    if (!cm.active()) {
        ImGui::TextWrapped("Cheats are kept per game. Load a cartridge or disc to use them.");
        drawLibrary(app);
        ImGui::End();
        return;
    }
    const GameIdentity& g = cm.game();
    const bool hardcore = cm.blockedNow();
    ImGui::Text("%s", !g.title.empty() ? g.title.c_str() : g.fileStem.c_str());
    ImGui::SameLine();
    ImGui::TextDisabled("(%s)", g.key.c_str());
    ImGui::SameLine();
    ImGui::TextDisabled("%u on", cm.enabledCount());
    ImGui::SameLine();
    ImGui::BeginDisabled(hardcore);
    if (ImGui::SmallButton("Turn all off")) cm.disableAll();
    ImGui::EndDisabled();
    if (hardcore)
        ImGui::TextColored(ImVec4(1.0f, 0.75f, 0.35f, 1.0f),
                           "Disabled in hardcore mode (saved cheats are kept).");
    if (cm.appliedSegments() > CheatManager::kCoreLimit)
        ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.5f, 1.0f), "%u writes: the core keeps only the first %u.",
                           cm.appliedSegments(), CheatManager::kCoreLimit);
    ImGui::Separator();

    ImGui::BeginDisabled(hardcore);
    drawSetPicker(app);
    ImGui::EndDisabled();
    ImGui::BeginChild("##cheatlist", ImVec2(0, 0), ImGuiChildFlags_Borders);
    ImGui::BeginDisabled(hardcore);   // toggles, set choice and manual codes: nothing changes in hardcore
    drawSetCheats(app);
    drawUserCheats(app);
    ImGui::EndDisabled();
    ImGui::Spacing();
    drawLibrary(app);
    ImGui::EndChild();
    ImGui::End();
}

}  // namespace vj
