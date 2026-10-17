// The Neuro API client in the app (neuro_agent.h): reads the App's state for it once a UI frame, hands its
// input to Player 1 every emulated frame, and draws Settings > Neuro.  docs/neuro.md is the user guide.
//
// Config keys: neuro.enabled, neuro.url, neuro.auto_force, neuro.force_interval (seconds), neuro.game_name.
// The URL defaults to $NEURO_SDK_WS_URL (the variable the Neuro SDKs read), else ws://localhost:8000 (Randy's).
// --neuro URL turns it on for one run without touching the config.
#include <imgui.h>

#include <cctype>
#include <cstdio>
#include <cstdlib>

#include "app.h"
#include "discord_presence.h"
#include "theme.h"
#include "ui_util.h"

extern "C" {
void* retro_get_memory_data(unsigned);
size_t retro_get_memory_size(unsigned);
}

namespace vj {

namespace {

std::string defaultUrl() {
    const char* e = std::getenv("NEURO_SDK_WS_URL");
    return e && *e ? e : "ws://localhost:8000";
}

std::string stemOf(const std::string& path) {
    const size_t sep = path.find_last_of("/\\");
    std::string f = sep == std::string::npos ? path : path.substr(sep + 1);
    const size_t dot = f.find_last_of('.');
    return dot == std::string::npos ? f : f.substr(0, dot);
}

neuro::AgentSettings settingsFrom(const App& app) {
    neuro::AgentSettings s;
    s.enabled = app.neuroCliUrl.empty() ? app.cfg.getBool("neuro.enabled", false) : true;
    s.url = !app.neuroCliUrl.empty() ? app.neuroCliUrl : app.cfg.get("neuro.url", defaultUrl());
    s.autoForce = app.cfg.getBool("neuro.auto_force", true);
    s.forceInterval = app.cfg.getFloat("neuro.force_interval", 1.0f);
    s.gameNameOverride = app.cfg.get("neuro.game_name", "");
    return s;
}

const lib::Entry* libraryEntry(App& app, const std::string& path) {
    if (path.empty()) return nullptr;
    for (const lib::Entry& e : app.library.entries())
        if (e.file.path == path) return &e;
    return nullptr;
}

// The display title, as Discord shows it: the library's dat name when it knows the file, else the file name.
std::string titleOf(App& app, const std::string& path, const lib::Entry* entry) {
    if (path.empty()) return "Jaguar BIOS";   // a no-content boot
    if (entry && !entry->datName.empty()) return discord::displayTitle(entry->datName);
    const std::string stem = app.host.contentName().empty() ? stemOf(path) : app.host.contentName();
    const size_t dot = path.find_last_of('.');
    std::string ext = dot == std::string::npos ? "" : path.substr(dot + 1);
    for (char& c : ext) c = (char)std::tolower((unsigned char)c);
    const bool disc = entry ? entry->section == lib::Section::Disc : lib::isDiscExt(ext);
    return discord::displayTitle(disc ? lib::titleFromDiscStem(stem) : lib::titleFromFileStem(stem));
}

neuro::GameState gameState(App& app) {
    neuro::GameState g;
    g.loaded = app.host.isLoaded();
    if (!g.loaded) return g;
    g.paused = app.paused || app.debug.halted();
    const std::string path = app.host.contentPath();
    const lib::Entry* entry = libraryEntry(app, path);
    g.title = titleOf(app, path, entry);
    if (entry) g.crc = entry->file.crcStripped ? entry->file.crcStripped : entry->file.crcWhole;
    g.ram = (const uint8_t*)retro_get_memory_data(RETRO_MEMORY_SYSTEM_RAM);
    g.ramSize = g.ram ? retro_get_memory_size(RETRO_MEMORY_SYSTEM_RAM) : 0;
    return g;
}

}  // namespace

void neuroInit(App& app, const CliArgs& a) {
    app.neuroCliUrl = a.neuroUrl;
    app.neuro.log = [](const std::string& l) { std::fprintf(stderr, "[neuro] %s\n", l.c_str()); };
    for (const std::string& p : app.neuro.loadProfiles(app.paths.root + "neuro"))
        std::fprintf(stderr, "[neuro] profile skipped: %s\n", p.c_str());
    app.neuro.configure(settingsFrom(app));
}

void neuroTick(App& app) {
    app.neuro.configure(settingsFrom(app));
    app.neuro.tick(app.now(), gameState(app));
}

void neuroShutdown(App& app) { app.neuro.configure(neuro::AgentSettings()); }

// ---------------------------------------------------------------------------------------------------------------
// Settings > Neuro
// ---------------------------------------------------------------------------------------------------------------
namespace {

void drawNeuroStatus(App& app) {
    ImGui::TextUnformatted("Status:");
    ImGui::SameLine();
    const std::string txt = app.neuro.statusText();
    if (app.neuro.link().connected()) ui::coloredWrapped(ui::vec4(ui::pal().success), txt.c_str());
    else if (app.neuro.settings().enabled) ui::coloredWrapped(ui::vec4(ui::pal().warning), txt.c_str());
    else ImGui::TextWrapped("%s", txt.c_str());
    if (!app.neuro.link().connected()) return;
    const neuro::Profile* p = app.neuro.profile();
    ImGui::Text("Game profile: %s", p ? p->gameName.c_str() : "none (controller actions only)");
    ImGui::Text("Actions received: %u, moves asked for: %u%s", app.neuro.link().actionsReceived(),
                app.neuro.link().forcesSent(), app.neuro.link().forcePending() ? " (waiting for her)" : "");
    if (!app.neuro.link().lastAction().empty()) ImGui::TextWrapped("Last: %s", app.neuro.link().lastAction().c_str());
    ImGui::Text("Pressing: %s", neuro::describe(app.neuro.pressedNow()).c_str());
}

}  // namespace

void drawNeuroTab(App& app) {
    ImGui::TextWrapped("Let Neuro-sama, or any AI that speaks the Neuro game API, play: the app connects to a Neuro "
                       "API server, offers her the Jaguar controller as actions, and presses what she chooses as "
                       "Player 1 (your own controls keep working alongside).");
    ImGui::Spacing();
    if (!app.neuroCliUrl.empty()) ui::hint("On for this run (--neuro); these settings take over next time.");
    bool on = app.cfg.getBool("neuro.enabled", false);
    if (ImGui::Checkbox("Connect to Neuro", &on)) app.cfg.setBool("neuro.enabled", on);
    static char urlBuf[256];
    static bool urlInit = false;
    if (!urlInit) {
        std::snprintf(urlBuf, sizeof urlBuf, "%s", app.cfg.get("neuro.url", defaultUrl()).c_str());
        urlInit = true;
    }
    ImGui::SetNextItemWidth(ui::em(20.0f));
    if (ImGui::InputText("Server", urlBuf, sizeof urlBuf, ImGuiInputTextFlags_EnterReturnsTrue)) app.cfg.set("neuro.url", urlBuf);
    ui::hint("A ws:// address; press Enter to use it. Randy, the SDK's test bot, listens on ws://localhost:8000.");
    bool force = app.cfg.getBool("neuro.auto_force", true);
    if (ImGui::Checkbox("Ask for the next move when the last one is done", &force)) app.cfg.setBool("neuro.auto_force", force);
    ui::hint("Sends the game state with an action force whenever her input has played out (one at a time, never while "
             "paused). Off: she only acts when she decides to.");
    float iv = app.cfg.getFloat("neuro.force_interval", 1.0f);
    ImGui::SetNextItemWidth(ui::em(10.0f));
    if (ImGui::SliderFloat("Pause between moves (s)", &iv, 0.0f, 10.0f, "%.1f")) app.cfg.setFloat("neuro.force_interval", iv);
    ImGui::Separator();
    drawNeuroStatus(app);
    app.cfg.save();
}

}  // namespace vj
