#include <algorithm>

#include "app.h"
#include "imgui.h"
#include "ui_util.h"
#include "window_chrome.h"

namespace vj {

namespace {

void drawServerTab(App& app, ContentServerBrowser& manager, ContentServerTab& browser) {
    ImGui::TextUnformatted(browser.origin.c_str());
    ImGui::BeginDisabled(browser.busy);
    ImGui::SetNextItemWidth(ui::fitWidth(0.7f, 14, 42));
    ImGui::InputText("Access token", browser.token, sizeof browser.token, ImGuiInputTextFlags_Password);
    ImGui::Checkbox("Remember token in credential store", &browser.rememberToken);
    if (!browser.credentialNotice.empty()) ImGui::TextWrapped("%s", browser.credentialNotice.c_str());
    if (browser.protocol == ecs::Protocol::RomM)
        ui::hint("Use a RomM client API token with platforms.read and roms.read.");
    else
        ui::hint("Use an Emulation Server device or guest token. Admitted LAN clients can leave it empty.");
    if (ImGui::Button("Connect / refresh server")) {
        manager.saveToken(browser);
        browser.connect();
    }
    ImGui::EndDisabled();
    ImGui::Separator();

    ImGui::BeginDisabled(browser.busy || browser.libraries.empty());
    const char* selected = browser.selectedLibrary >= 0 && size_t(browser.selectedLibrary) < browser.libraries.size()
        ? browser.libraries[size_t(browser.selectedLibrary)].name.c_str() : "Choose a library";
    if (browser.protocol == ecs::Protocol::Ecs && ImGui::BeginCombo("Library", selected)) {
        for (size_t i = 0; i < browser.libraries.size(); ++i) {
            ImGui::PushID(int(i));
            if (ImGui::Selectable(browser.libraries[i].name.c_str(), int(i) == browser.selectedLibrary)) {
                browser.browse(int(i));
                manager.save();
            }
            ImGui::PopID();
        }
        ImGui::EndCombo();
    }
    const char* systems[] = {"Jaguar cartridges", "Jaguar CD"};
    if (ImGui::Combo("System", &browser.system, systems, 2)) {
        browser.browse(browser.selectedLibrary);
        manager.save();
    }
    ImGui::SetNextItemWidth(ui::fitWidth(0.7f, 12, 40));
    const bool enter = ImGui::InputText("Search games", browser.search, sizeof browser.search, ImGuiInputTextFlags_EnterReturnsTrue);
    if (ImGui::Button("Search / refresh") || enter) {
        browser.browse(browser.selectedLibrary);
        manager.save();
    }
    ImGui::EndDisabled();
    if (!browser.message.empty()) ImGui::TextWrapped("%s", browser.message.c_str());
    if (browser.busy) {
        if (browser.downloading) {
            const uint64_t total = browser.total(), received = browser.received();
            const float fraction = total ? float(double(received) / double(total)) : 0.0f;
            ImGui::ProgressBar(fraction, ImVec2(-1, 0));
            ImGui::Text("Downloading %.1f / %.1f MiB", double(received) / 1048576, double(total) / 1048576);
        } else ImGui::TextUnformatted("Loading server library...");
        if (ImGui::Button("Cancel")) browser.cancel();
    }
    ImGui::BeginDisabled(browser.busy);
    ImGui::BeginChild("##server-games", ImVec2(0, -ui::em(browser.selectedGame >= 0 ? 12 : 3)), ImGuiChildFlags_Borders);
    const float rowHeight = ui::em(3);
    ImGuiListClipper clipper;
    clipper.Begin(int(browser.games.size()), rowHeight + ImGui::GetStyle().ItemSpacing.y);
    while (clipper.Step()) {
        for (int i = clipper.DisplayStart; i < clipper.DisplayEnd; ++i) {
            const auto& game = browser.games[size_t(i)];
            ImGui::PushID(i);
            const ImVec2 pos = ImGui::GetCursorScreenPos();
            if (ImGui::Selectable("##game", browser.selectedGame == i, 0, ImVec2(0, rowHeight)) &&
                    browser.selectedGame != i && !browser.busy) {
                browser.selectGame(i);
                browser.loadAddons();
            }
            auto* draw = ImGui::GetWindowDrawList();
            const ArtView cover = browser.cover(game, app.library);
            if (cover.tex) {
                const float scale = rowHeight / float(std::max(cover.w, cover.h));
                const float w = scale * cover.w, h = scale * cover.h;
                const ImVec2 topLeft(pos.x + (rowHeight - w) / 2, pos.y + (rowHeight - h) / 2);
                draw->AddImage(ImTextureRef((ImTextureID)(intptr_t)cover.tex), topLeft,
                               ImVec2(topLeft.x + w, topLeft.y + h));
            }
            draw->AddText(ImVec2(pos.x + rowHeight + ImGui::GetStyle().ItemSpacing.x,
                                pos.y + (rowHeight - ImGui::GetTextLineHeight()) / 2),
                          ImGui::GetColorU32(ImGuiCol_Text), game.name.c_str());
            ImGui::PopID();
        }
    }
    ImGui::EndChild();
    ui::Flow actions;
    const bool launchPending = std::any_of(manager.tabs.begin(), manager.tabs.end(),
        [](const auto& tab) { return tab->downloading; });
    ImGui::BeginDisabled(browser.selectedGame < 0 || launchPending);
    if (ImGui::Button("Download and play")) browser.download(browser.selectedGame);
    ImGui::EndDisabled();
    if (!browser.nextCursor.empty()) {
        actions.next(ui::Flow::buttonW("Load more"));
        if (ImGui::Button("Load more")) browser.browse(browser.selectedLibrary, true);
    }
    if (!browser.readyPath.empty()) {
        actions.next(ui::Flow::buttonW("Play downloaded game"));
        ImGui::BeginDisabled(launchPending);
        if (ImGui::Button("Play downloaded game")) {
            if (app.openContent(browser.readyPath)) manager.show = false;
        }
        ImGui::EndDisabled();
    }
    if (browser.selectedGame >= 0 && size_t(browser.selectedGame) < browser.games.size()) {
        ImGui::Separator();
        ImGui::TextUnformatted("Add-ons");
        if (!browser.addonsLoaded && !browser.busy) {
            ImGui::SameLine();
            if (ImGui::Button("Load add-ons")) browser.loadAddons();
        }
        if (browser.addonsLoaded && browser.addons.empty())
            ImGui::TextUnformatted("No add-ons available.");
        if (!browser.addons.empty()) {
            const auto& game = browser.games[size_t(browser.selectedGame)];
            ImGui::BeginChild("##server-addons", ImVec2(0, 0), ImGuiChildFlags_Borders);
            for (const auto& addon : browser.addons) {
                ImGui::PushID(addon.id.c_str());
                const char* kind = addon.kind == "patch" ? "Patch" : addon.kind == "mod" ? "Mod" :
                                   addon.kind == "texture_pack" ? "Texture pack" : "Add-on";
                const bool boot = addon.canBoot(game);
                ImGui::TextWrapped("%s · %s · %llu bytes%s", addon.filename.c_str(), kind,
                                   static_cast<unsigned long long>(addon.size),
                                   boot ? "" : " · Download only");
                if (ImGui::IsItemHovered()) {
                    if (!addon.hash.empty()) ImGui::SetTooltip("SHA-256: %s", addon.hash.c_str());
                    else if (!addon.rommSha1.empty()) ImGui::SetTooltip("RomM SHA-1: %s", addon.rommSha1.c_str());
                }
                ImGui::BeginDisabled(launchPending || browser.busy);
                if (ImGui::Button("Download")) browser.downloadAddon(addon, false);
                if (boot) {
                    ImGui::SameLine();
                    if (ImGui::Button("Download and boot patched")) browser.downloadAddon(addon, true);
                }
                ImGui::EndDisabled();
                ImGui::PopID();
            }
            ImGui::EndChild();
        }
    }
    ImGui::EndDisabled();
}

} // namespace

void drawContentServers(App& app) {
    ContentServerBrowser& manager = app.contentServers;
    for (auto& tab : manager.tabs) {
        tab->pump();
        if (tab->patchReady) {
            std::string error;
            if (app.bootServerPatch(*tab->patchReady, error)) manager.show = false;
            else tab->message = std::move(error);
            tab->patchReady.reset();
        }
        if (tab->launchReady) {
            tab->launchReady = false;
            if (app.openContent(tab->readyPath)) manager.show = false;
        }
    }
    if (!manager.show) return;
    ui::nextWindow(68.0f, 38.0f, 26.0f, 20.0f);
    const bool expanded = ui::beginWindow("Server library", &manager.show);
    ui::keepOnScreen();
    int remove = -1;
    bool add = false;
    if (expanded && ImGui::BeginTabBar("##server-tabs", ImGuiTabBarFlags_FittingPolicyScroll | ImGuiTabBarFlags_AutoSelectNewTabs)) {
        for (size_t i = 0; i < manager.tabs.size(); ++i) {
            auto& tab = *manager.tabs[i];
            std::string label = tab.name;
            std::replace(label.begin(), label.end(), '#', ' ');
            label += "###" + tab.credentialKey();
            if (!ImGui::BeginTabItem(label.c_str())) continue;
            if (!tab.opened) tab.connect();
            ImGui::BeginDisabled(tab.busy);
            const bool forget = ImGui::Button("Forget server");
            ImGui::EndDisabled();
            if (forget) remove = int(i);
            else drawServerTab(app, manager, tab);
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Add server")) {
            const char* protocols[] = {"Emulation Server", "RomM"};
            ImGui::Combo("Server type", &manager.protocol, protocols, 2);
            ImGui::InputText("Name", manager.name, sizeof manager.name);
            ImGui::InputText("Server address", manager.address, sizeof manager.address);
            if (!manager.message.empty()) ImGui::TextWrapped("%s", manager.message.c_str());
            add = ImGui::Button("Add server");
            ImGui::EndTabItem();
        }
        ImGui::EndTabBar();
    }
    ImGui::End();
    if (remove >= 0) manager.forget(size_t(remove));
    if (add) manager.add();
}

} // namespace vj
