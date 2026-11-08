#include "app.h"
#include "imgui.h"
#include "ui_util.h"
#include "window_chrome.h"

namespace vj {

void drawContentServers(App& app) {
    ContentServerBrowser& browser = app.contentServers;
    browser.pump();
    if (browser.launchReady) {
        browser.launchReady = false;
        if (app.openContent(browser.readyPath)) browser.show = false;
    }
    if (!browser.show) return;
    ui::nextWindow(68.0f, 38.0f, 26.0f, 20.0f);
    const bool expanded = ui::beginWindow("Server library", &browser.show);
    ui::keepOnScreen();
    if (!expanded) { ImGui::End(); return; }
    ImGui::BeginDisabled(browser.busy);
    if (ImGui::BeginCombo("Saved servers", browser.address)) {
        for (size_t i = 0; i < browser.servers.size(); ++i)
            if (ImGui::Selectable(browser.servers[i].c_str())) browser.selectServer(i);
        ImGui::EndCombo();
    }
    ImGui::SetNextItemWidth(ui::fitWidth(0.7f, 14, 42));
    if (ImGui::InputText("Server address", browser.address, sizeof browser.address)) browser.addressEdited();
    ImGui::SetNextItemWidth(ui::fitWidth(0.7f, 14, 42));
    if (ImGui::InputText("Access token", browser.token, sizeof browser.token, ImGuiInputTextFlags_Password)) browser.tokenEdited();
    ImGui::Checkbox("Remember token in credential store", &browser.rememberToken);
    if (!browser.credentialNotice.empty()) ImGui::TextWrapped("%s", browser.credentialNotice.c_str());
    ui::hint("Paste a device or guest token from Emulation Server, or leave it empty for an admitted LAN connection.");
    if (ImGui::Button("Connect")) browser.connect();
    ImGui::SameLine();
    if (ImGui::Button("Forget server")) browser.forget();
    ImGui::EndDisabled();
    ImGui::Separator();

    ImGui::BeginDisabled(browser.busy || browser.libraries.empty());
    const char* selected = browser.selectedLibrary >= 0 && size_t(browser.selectedLibrary) < browser.libraries.size()
        ? browser.libraries[size_t(browser.selectedLibrary)].name.c_str() : "Choose a library";
    if (ImGui::BeginCombo("Library", selected)) {
        for (size_t i = 0; i < browser.libraries.size(); ++i) {
            ImGui::PushID(int(i));
            if (ImGui::Selectable(browser.libraries[i].name.c_str(), int(i) == browser.selectedLibrary)) browser.browse(int(i));
            ImGui::PopID();
        }
        ImGui::EndCombo();
    }
    const char* systems[] = {"Jaguar cartridges", "Jaguar CD"};
    if (ImGui::Combo("System", &browser.system, systems, 2)) browser.browse(browser.selectedLibrary);
    ImGui::SetNextItemWidth(ui::fitWidth(0.7f, 12, 40));
    const bool enter = ImGui::InputText("Search games", browser.search, sizeof browser.search, ImGuiInputTextFlags_EnterReturnsTrue);
    if (ImGui::Button("Search / refresh") || enter) browser.browse(browser.selectedLibrary);
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
    ImGui::BeginChild("##server-games", ImVec2(0, -ui::em(3)), ImGuiChildFlags_Borders);
    for (size_t i = 0; i < browser.games.size(); ++i) {
        ImGui::PushID(int(i));
        if (ImGui::Selectable(browser.games[i].name.c_str(), browser.selectedGame == int(i))) browser.selectedGame = int(i);
        ImGui::PopID();
    }
    ImGui::EndChild();
    ui::Flow actions;
    ImGui::BeginDisabled(browser.selectedGame < 0);
    if (ImGui::Button("Download and play")) browser.download(browser.selectedGame);
    ImGui::EndDisabled();
    if (!browser.nextCursor.empty()) {
        actions.next(ui::Flow::buttonW("Load more"));
        if (ImGui::Button("Load more")) browser.browse(browser.selectedLibrary, true);
    }
    if (!browser.readyPath.empty()) {
        actions.next(ui::Flow::buttonW("Play downloaded game"));
        if (ImGui::Button("Play downloaded game")) {
            if (app.openContent(browser.readyPath)) browser.show = false;
        }
    }
    ImGui::EndDisabled();
    ImGui::End();
}

} // namespace vj
