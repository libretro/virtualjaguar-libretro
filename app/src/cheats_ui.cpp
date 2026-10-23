// The Cheats window: pick a cheat set for the running game (matched by name
// from the libretro cheat database), toggle its cheats, add manual codes.
#include "app.h"
#include "cheat_store.h"
#include "cheats.h"
#include "ui_util.h"
#include "window_chrome.h"

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
    // The community store is told when the window becomes visible (or the game changes under it): the frame of the
    // last draw and the game it was told about.
    int lastDrawFrame = -10;
    std::string openedKey;
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
    ImGui::SetNextItemWidth(ui::fitWidth(0.62f, 12.0f, 32.0f));
    if (ImGui::BeginCombo("Cheat set", current.c_str())) {
        ImGui::InputTextWithHint("##setfilter", "Filter", g_ui.filter, sizeof g_ui.filter);
        std::string f = lowerCopy(g_ui.filter);
        if (ImGui::Selectable("(no cheat set)", cm.selectedStem().empty())) cm.selectSet("");
        auto emit = [&](const char* header, const std::vector<size_t>& idx) {
            if (idx.empty()) return;
            ImGui::SeparatorText(header);
            ImGui::PushID(header);   // a set can be both a match and a similar name: one ID scope per list (#932)
            for (size_t i : idx) {
                if (!f.empty() && lowerCopy(lib[i].stem).find(f) == std::string::npos) continue;
                if (ImGui::Selectable(lib[i].stem.c_str(), lib[i].stem == cm.selectedStem())) cm.selectSet(lib[i].stem);
            }
            ImGui::PopID();
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
    if (!st.empty()) {
        // "No set matches by name" is not a problem when the game's community pack already supplies cheats.
        const ChtSet* pack = cm.communitySet();
        const bool packCovers = pack && pack->restriction.empty() && !pack->cheats.empty();
        if (packCovers && cm.selectedStem().empty() && cm.matches().empty() && !lib.empty()) {
            ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
            ImGui::TextWrapped("%s", st.c_str());
            ImGui::PopStyleColor();
        } else
            ui::coloredWrapped(ui::vec4(ui::pal().warning), st.c_str());
    }
}

// The cheat rows of one set: a checkbox, the wrapped description (clicking it toggles too) and a tooltip with the code
// or the reason it is unsupported.  `origin` says which of the game's two sets the toggles belong to.
void drawCheatRows(CheatManager& cm, const ChtSet& set, CheatOrigin origin) {
    for (size_t i = 0; i < set.cheats.size(); i++) {
        const ChtCheat& c = set.cheats[i];
        ImGui::PushID((int)i);
        bool on = cm.isEnabled(c, origin);
        ImGui::BeginDisabled(!c.supported);
        std::string label = c.desc + (c.parts > 1 ? "  (" + std::to_string(c.parts) + " parts)" : std::string());
        // A checkbox label cannot wrap, so the description is a wrapped text beside an empty-label box;
        // clicking the text toggles too.
        if (ImGui::Checkbox("##on", &on)) cm.setEnabled(c, on, origin);
        ImGui::SameLine();
        ImGui::AlignTextToFramePadding();
        ImGui::TextWrapped("%s", label.c_str());
        if (ImGui::IsItemClicked()) cm.setEnabled(c, !on, origin);
        ImGui::EndDisabled();
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
            if (c.supported) ImGui::SetTooltip("%s", c.code.c_str());
            else ImGui::SetTooltip("Not supported: %s", c.note.c_str());
        }
        ImGui::PopID();
    }
}

void drawSetCheats(App& app, bool hardcore) {
    CheatManager& cm = app.cheats;
    const ChtSet* set = cm.selectedSet();
    if (!set) return;
    ImGui::SeparatorText("Database cheats");
    if (set->cheats.empty()) ImGui::TextDisabled("This file has no cheats.");
    ImGui::BeginDisabled(hardcore);
    ImGui::PushID("db");
    drawCheatRows(cm, *set, CheatOrigin::Database);
    ImGui::PopID();
    ImGui::EndDisabled();
}

// "Community cheats": the downloaded pack for this cartridge's CRC32 (design: docs/cheat-store-design.md, App section 5).
// The buttons are data-only and stay live in hardcore mode; only the toggles are disabled there.
void drawCommunity(App& app, bool hardcore) {
    CheatManager& cm = app.cheats;
    CheatStore& store = app.cheatStore;
    ImGui::SeparatorText("Community cheats");
    const uint32_t crc = cm.game().crc;
    if (crc == 0) {
        ImGui::TextDisabled("Community packs cover cartridges only.");
        return;
    }
    char crcText[16];
    std::snprintf(crcText, sizeof crcText, "%08X", (unsigned)crc);

    const PackState st = store.state(crc);
    const PackMeta* off = store.offered(crc);
    const PackMeta* inst = store.installed(crc);
    const PackMeta* m = inst ? inst : off;   // what was verified at install wins; the manifest's entry before that
    const ChtSet* set = cm.communitySet();
    const bool downloads = store.downloadsAllowed && store.downloadsAllowed();
    const bool busy = store.busy();
    // A pack file is on disk: Remove (and Update when offered) follow from the store state alone, so a file that does
    // not parse is never a dead end.  Only the cheat rows need the parsed `set`.
    const bool packOnDisk = st == PackState::Installed || st == PackState::UpdateAvailable ||
                            st == PackState::NoLongerOffered || st == PackState::EditedLocally;
    const bool havePack = packOnDisk && set;
    const bool updateOffered = st == PackState::UpdateAvailable || (st == PackState::EditedLocally && off);

    // ---- what the store knows about it ----
    if (m && st != PackState::NotOffered) {
        ImGui::TextWrapped("%s", m->title.empty() ? m->name.c_str() : m->title.c_str());
        ui::hint("%u cheat%s, %u write%s, updated %s", m->cheats, m->cheats == 1 ? "" : "s", m->writes,
                 m->writes == 1 ? "" : "s", m->updated.c_str());
        if (!m->credit.empty()) {
            ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
            ImGui::TextWrapped("%s", m->credit.c_str());
            ImGui::PopStyleColor();
        }
        if (m->partial)
            ui::coloredWrapped(ui::vec4(ui::pal().warning),
                               "Partial pack: some of the source script's cheats could not be converted.");
        if (m->exceedsCoreLimit)
            ui::coloredWrapped(ui::vec4(ui::pal().warning),
                               "Enabling every cheat here exceeds the core's 256-write limit; the first 256 writes win.");
        if (!m->notes.empty()) {
            char head[64];
            std::snprintf(head, sizeof head, "%zu setting%s could not be converted", m->notes.size(),
                          m->notes.size() == 1 ? "" : "s");
            if (ImGui::TreeNode("##notes", "%s", head)) {
                for (const auto& n : m->notes) {
                    ImGui::Bullet();
                    ImGui::TextWrapped("%s", n.first.empty() ? n.second.c_str() : (n.first + ": " + n.second).c_str());
                }
                ImGui::TreePop();
            }
        }
    } else if (packOnDisk && !inst) {
        ImGui::TextDisabled(updateOffered ? "Credits unavailable (no record of where this pack came from). Use Update to replace it."
                                          : "Credits unavailable (no record of where this pack came from).");
    }

    // ---- state line ----
    switch (st) {
        case PackState::DownloadsOff:
            ImGui::TextWrapped("Downloads are off (Library: Download art and cheat packs). "
                               "Turn it on to see community packs.");
            break;
        case PackState::Checking:
            ImGui::TextDisabled("Checking the community store...");
            break;
        case PackState::NotOffered:
            if (store.manifestLoaded()) ImGui::TextWrapped("No community pack for this cartridge (CRC %s).", crcText);
            else ImGui::TextWrapped("Community store not reached yet.");   // no list cached and the fetch failed or has not run
            break;
        case PackState::UpdateAvailable:
            ui::coloredWrapped(ui::vec4(ui::pal().warning),
                               ((off && !off->updated.empty() ? "Update available (pack changed " + off->updated + ")."
                                                              : std::string("Update available.")) +
                                (downloads ? "" : " (downloads are off)"))
                                   .c_str());
            break;
        case PackState::NoLongerOffered:
            ImGui::TextDisabled("No longer offered by the store; installed copy kept.");
            break;
        case PackState::EditedLocally:
            ui::coloredWrapped(ui::vec4(ui::pal().warning),
                               "This pack was edited on disk. Update replaces it with the store's copy.");
            break;
        default:
            break;
    }
    if (packOnDisk && (!set || !set->restriction.empty()))
        ui::coloredWrapped(ui::vec4(ui::pal().warning),
                           set ? set->restriction.c_str() : "This pack could not be read; Remove it, or Update if offered.");
    if (havePack && store.turnedOff() > 0) {
        char m[96];
        std::snprintf(m, sizeof m, "%u cheat%s changed in the update and %s turned off.", store.turnedOff(),
                      store.turnedOff() == 1 ? "" : "s", store.turnedOff() == 1 ? "was" : "were");
        ui::coloredWrapped(ui::vec4(ui::pal().warning), m);
    }
    if (havePack && hardcore)
        ui::coloredWrapped(ui::vec4(ui::pal().warning), "Installed; cheats cannot be enabled in hardcore mode.");
    if (!store.status().empty()) ImGui::TextWrapped("%s", store.status().c_str());

    // ---- buttons ----
    ui::Flow flow;
    bool first = true;
    auto button = [&](const char* label, bool enabled, const char* tip) {
        if (!first) flow.next(ui::Flow::buttonW(label));
        first = false;
        ImGui::BeginDisabled(!enabled);
        const bool hit = ImGui::Button(label);
        ImGui::EndDisabled();
        if (tip && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) ImGui::SetTooltip("%s", tip);
        return hit;
    };
    const bool canInstall = off && downloads && !busy;
    if (st == PackState::Offered && button("Install", canInstall, "Downloads this pack from the project's website."))
        store.install(crc);
    if (updateOffered &&
        button("Update", canInstall,
               downloads ? "Replaces the installed pack. Cheats that changed come back off."
                         : "Downloads are off (Library: Download art and cheat packs)."))
        store.install(crc);
    if (packOnDisk && button("Remove", !busy, "Deletes the pack and forgets which of its cheats were on."))
        store.remove(crc, cm);
    if (button("Check for updates", downloads && !busy && st != PackState::Checking,
               "Asks the project's website for its current list of packs."))
        store.checkNow();

    // ---- the cheats ----
    if (havePack) {
        ImGui::BeginDisabled(hardcore);
        ImGui::PushID("community");
        drawCheatRows(cm, *set, CheatOrigin::Community);
        ImGui::PopID();
        ImGui::EndDisabled();
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
        if (ImGui::Checkbox("##on", &on)) cm.setUserEnabled(i, on);
        ImGui::SameLine();
        if (ImGui::SmallButton("remove")) removeIdx = (int)i;
        ImGui::SameLine();
        ImGui::AlignTextToFramePadding();
        ImGui::TextWrapped("%s  %s", list[i].desc.c_str(), list[i].code.c_str());
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", list[i].code.c_str());
        ImGui::PopID();
    }
    if (removeIdx >= 0) cm.removeUser((size_t)removeIdx);

    // Name and code share the row (name a third, code the rest); Add drops to the next row in a narrow window.
    float rowW = ImGui::GetContentRegionAvail().x, sp = ImGui::GetStyle().ItemSpacing.x;
    float addW = ui::Flow::buttonW("Add");
    bool oneRow = rowW > ImGui::GetFontSize() * 30.0f;
    float nameW = oneRow ? (rowW - addW - sp * 2) * 0.34f : rowW * 0.4f;
    float codeW = oneRow ? (rowW - addW - sp * 2) - nameW : rowW - nameW - sp;
    ImGui::SetNextItemWidth(std::max(1.0f, nameW));
    ImGui::InputTextWithHint("##udesc", "Name", g_ui.desc, sizeof g_ui.desc);
    ImGui::SameLine();
    ImGui::SetNextItemWidth(std::max(1.0f, codeW));
    bool enter = ImGui::InputTextWithHint("##ucode", "Code, e.g. 00007D3D:05", g_ui.code, sizeof g_ui.code,
                                          ImGuiInputTextFlags_EnterReturnsTrue);
    if (oneRow) ImGui::SameLine();
    if (ImGui::Button("Add") || enter) {
        std::string err;
        if (cm.addUser(g_ui.desc, g_ui.code, err)) {
            g_ui.desc[0] = g_ui.code[0] = 0;
            g_ui.error.clear();
        } else {
            g_ui.error = err;
        }
    }
    if (!g_ui.error.empty()) ui::coloredWrapped(ui::vec4(ui::pal().error), g_ui.error.c_str());
    ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
    ImGui::TextWrapped("Hex ADDRESS:VALUE; 2, 4 or 8 value digits write a byte, word or long. "
                       "Join several with '+'. Addresses are Jaguar bus addresses (RAM is 000000-1FFFFF).");
    ImGui::PopStyleColor();
}

static void drawCheatLibrary(App& app) {
    CheatManager& cm = app.cheats;
    if (!ImGui::CollapsingHeader("Cheat library")) return;
    if (!g_ui.dirInit) {
        std::snprintf(g_ui.dir, sizeof g_ui.dir, "%s", app.cfg.get("cheats.dir", "").c_str());
        g_ui.dirInit = true;
    }
    ImGui::TextWrapped("%zu cheat file%s found in:", cm.library().size(), cm.library().size() == 1 ? "" : "s");
    for (const std::string& d : cm.libraryDirs()) {
        ImGui::Bullet();
        ImGui::TextWrapped("%s", d.c_str());
    }
    // The folder box takes what the "Use folder" button leaves.
    ImGui::SetNextItemWidth(std::max(ImGui::GetFontSize() * 8.0f,
                                     ImGui::GetContentRegionAvail().x - ui::Flow::buttonW("Use folder") -
                                         ImGui::GetStyle().ItemSpacing.x));
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
    ui::nextWindow(40.0f, 32.0f, 24.0f, 14.0f);
    bool open = ui::beginWindow("Cheats", &app.showCheats);
    ui::keepOnScreen();
    if (!open) {
        ImGui::End();
        return;
    }
    CheatManager& cm = app.cheats;
    if (!cm.active()) {
        ImGui::TextWrapped("Cheats are kept per game. Load a cartridge or disc to use them.");
        drawCheatLibrary(app);
        ImGui::End();
        return;
    }
    const GameIdentity& g = cm.game();
    const bool hardcore = cm.blockedNow();
    // The community store: told once when the window becomes visible (or the game changes under it); it decides
    // whether a manifest check is due.  Responses are applied every frame by cheatsPump, not here.
    {
        const int frame = ImGui::GetFrameCount();
        if (frame - g_ui.lastDrawFrame > 1 || g_ui.openedKey != g.key) {
            app.cheatStore.windowOpened(g.crc, app.cfg);
            g_ui.openedKey = g.key;
        }
        g_ui.lastDrawFrame = frame;
    }
    ImGui::TextWrapped("%s", !g.title.empty() ? g.title.c_str() : g.fileStem.c_str());
    ui::hint("%s    %u on", g.key.c_str(), cm.enabledCount());
    ImGui::SameLine();
    ImGui::BeginDisabled(hardcore);
    if (ImGui::SmallButton("Turn all off")) cm.disableAll();
    ImGui::EndDisabled();
    if (hardcore)
        ui::coloredWrapped(ui::vec4(ui::pal().warning), "Disabled in hardcore mode (saved cheats are kept).");
    if (cm.appliedSegments() > CheatManager::kCoreLimit) {
        char m[96];
        std::snprintf(m, sizeof m, "%u writes: the core keeps only the first %u.", cm.appliedSegments(),
                      CheatManager::kCoreLimit);
        ui::coloredWrapped(ui::vec4(ui::pal().error), m);
    }
    ImGui::Separator();

    ImGui::BeginDisabled(hardcore);
    drawSetPicker(app);
    ImGui::EndDisabled();
    ImGui::BeginChild("##cheatlist", ImVec2(0, 0), ImGuiChildFlags_Borders);
    // Toggles, set choice and manual codes: nothing changes in hardcore.  The community section's buttons are data-only
    // (install / update / remove) and stay live, so each section applies the disabling itself.
    drawSetCheats(app, hardcore);
    drawCommunity(app, hardcore);
    ImGui::BeginDisabled(hardcore);
    drawUserCheats(app);
    ImGui::EndDisabled();
    ImGui::Spacing();
    drawCheatLibrary(app);
    ImGui::EndChild();
    ImGui::End();
}

}  // namespace vj
