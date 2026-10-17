// The Netplay window and menu (link-cable play over the network), the command-line twin used by the
// headless tests, and the glue between Netplay and the App.  See netplay.h for what is (not) being done.
#include <algorithm>
#include <chrono>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "app.h"
#include "ui_util.h"
#include "imgui.h"
#include "netplay.h"
#include "typed_ui.h"

namespace vj {

namespace {

const int kPorts[4] = {42171, 42172, 42173, 42174};   // the presets; any port 1-65535 can be typed

struct UiState {
    bool init = false;
    int port = kPorts[0];
    ui::TypedState portBox;
    char address[128] = "";
    std::string error;
    bool restartAsk = false;
    std::vector<std::string> restartKeys;
    std::vector<LanAddress> lan;
    std::string machine;
    double lanAt = -100.0;
    NetDevice device = NetDevice::JagLink;
};
UiState g_ui;

// Theme colours (theme.h): functions, because the scheme can change while the app runs.
ImVec4 kGreen() { return ui::vec4(ui::pal().success); }
ImVec4 kAmber() { return ui::vec4(ui::pal().warning); }
ImVec4 kRed() { return ui::vec4(ui::pal().error); }
ImVec4 kGrey() { return ui::vec4(ui::pal().textMuted); }

// Greyed text that wraps (ImGui::TextDisabled does not).
void hint(const char* fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
    ImGui::TextWrappedV(fmt, ap);
    ImGui::PopStyleColor();
    va_end(ap);
}

void initUi(App& app) {
    if (g_ui.init) return;
    g_ui.init = true;
    int port = app.cfg.getInt("netplay.port", kPorts[0]);
    g_ui.port = (port >= 1 && port <= 65535) ? port : kPorts[0];
    std::snprintf(g_ui.address, sizeof g_ui.address, "%s", app.cfg.get("netplay.join_addr", "").c_str());
    // The device is an ordinary saved core option; show what it currently is.
    for (const OptionDef& d : app.host.optionDefs())
        if (d.key == "virtualjaguar_uart_device") g_ui.device = app.host.optionValue(d) == "voicemodem" ? NetDevice::VoiceModem : NetDevice::JagLink;
    if (app.host.optionDefs().empty()) {
        auto it = app.host.userOptions().find("virtualjaguar_uart_device");
        if (it != app.host.userOptions().end()) g_ui.device = it->second == "voicemodem" ? NetDevice::VoiceModem : NetDevice::JagLink;
    }
}

void refreshLan(App& app) {
    double t = app.now();
    if (t - g_ui.lanAt < 5.0 && !g_ui.lan.empty()) return;
    g_ui.lanAt = t;
    g_ui.lan = Netplay::lanAddresses();
    g_ui.machine = Netplay::machineName();
}

NetSettings current(NetRole role, const std::string& address) {
    NetSettings s;
    s.role = role;
    s.device = g_ui.device;
    s.port = g_ui.port;
    s.address = address;
    return s;
}

void begin(App& app, const NetSettings& s) {
    g_ui.error.clear();
    g_ui.restartKeys.clear();
    std::string err;
    if (!app.netplay.start(s, err, &g_ui.restartKeys)) {
        g_ui.error = err;
        return;
    }
    app.cfg.setInt("netplay.port", s.port);
    if (s.role == NetRole::Join && !s.address.empty()) app.cfg.set("netplay.join_addr", s.address);
    if (!g_ui.restartKeys.empty() && app.host.isLoaded()) g_ui.restartAsk = true;
}

void drawStatus(App& app) {
    Netplay& np = app.netplay;
    const bool running = app.host.isLoaded();
    ImVec4 col = kGrey();
    std::string head;
    if (!np.configured()) {
        head = "Not connected";
    } else if (np.link() == LinkState::Up) {
        head = "Connected";
        col = kGreen();
    } else if (np.link() == LinkState::Lost) {
        head = "Partner disconnected";
        col = kRed();
    } else if (np.settings().role == NetRole::Join && np.settings().address.empty()) {
        head = "Looking for games";
        col = kAmber();
    } else {
        head = "Waiting for partner";
        col = kAmber();
    }
    ImGui::TextColored(col, "%s", head.c_str());
    ImGui::SameLine();
    ImGui::TextDisabled("-");
    ImGui::SameLine();
    ImGui::TextWrapped("%s", np.statusText().c_str());
    if (np.configured() && !running)
        ImGui::TextColored(kAmber(), "No game is running. The link starts as soon as you open one (both players need the same game).");
    else if (np.configured() && app.paused)
        ImGui::TextColored(kAmber(), "The game is paused, so the link is paused too.");
    for (const std::string& n : np.notices()) ImGui::TextColored(kAmber(), "%s", n.c_str());
    if (!g_ui.error.empty()) ImGui::TextColored(kRed(), "%s", g_ui.error.c_str());
}

const char* qualityWord(double hopMs) {
    if (hopMs < 4.0) return "excellent";
    if (hopMs < 10.0) return "good";
    if (hopMs < 20.0) return "fair - expect some jerkiness";
    return "poor - expect lag";
}

void drawLinkActivity(App& app) {
    Netplay& np = app.netplay;
    if (np.link() != LinkState::Up) return;
    ImGui::SeparatorText("Link activity");
    const LinkStats& s = np.stats();
    double age = np.statsAgeSec();
    if (s.valid && age >= 0.0 && age < 15.0) {
        ImGui::Text("%u exchanges per second", s.hopsPerSec);
        ImGui::Text("Round trip to your partner: %.1f ms (%s)", s.hopMs, qualityWord(s.hopMs));
        ImGui::Text("Late replies: %u%%    Timeouts: %u", s.slippedPct, s.timeouts);
        if (ImGui::TreeNode("Details")) {
            ImGui::TextWrapped("%s", s.raw.c_str());
            ImGui::TreePop();
        }
    } else {
        hint("Connected, no game traffic yet. Start the game's link / network mode (see below).");
    }
    if (np.wireSpeedup() > 1)
        ImGui::TextDisabled("Link speed-up x%u agreed with your partner.", np.wireSpeedup());
}

void deviceChoice(App& app) {
    Netplay& np = app.netplay;
    ImGui::TextUnformatted("What is plugged into the Jaguar's link port:");
    bool changed = false;
    if (ImGui::RadioButton("JagLink / CatBox cable (BattleSphere, AirCars, Doom)", g_ui.device == NetDevice::JagLink)) {
        g_ui.device = NetDevice::JagLink;
        changed = true;
    }
    if (ImGui::RadioButton("Voice Modem (Ultra Vortek)", g_ui.device == NetDevice::VoiceModem)) {
        g_ui.device = NetDevice::VoiceModem;
        changed = true;
    }
    // Both players must pick the same one.
    ImGui::TextDisabled("Both players must choose the same.");
    if (changed) {
        if (np.configured()) begin(app, current(np.settings().role, np.settings().address));
        else app.host.setOptionValue("virtualjaguar_uart_device", Netplay::deviceValue(g_ui.device));
    }
}

// The link port: the four presets the core offers, or any port 1-65535 typed.  live > 0: what the
// running link uses (the box is shown locked then).
void portCombo(int live) {
    static const TypedOption* t = typedOptionFor("virtualjaguar_netlink_port");
    static std::vector<OptionValue> presets;
    if (presets.empty())
        for (int p : kPorts) presets.push_back({std::to_string(p), ""});
    const std::string cur = std::to_string(live > 0 ? live : g_ui.port);
    std::string picked;
    if (ui::typedCombo("Port", ImGui::GetFontSize() * 7.0f, *t, presets, cur, g_ui.portBox, picked))
        g_ui.port = std::atoi(picked.c_str());
    if (g_ui.portBox.custom) {
        ImGui::Indent();
        if (ui::typedInput(*t, cur, g_ui.portBox, picked)) g_ui.port = std::atoi(picked.c_str());
        ImGui::Unindent();
    }
}

void drawHost(App& app) {
    Netplay& np = app.netplay;
    const bool hosting = np.settings().role == NetRole::Host;
    ImGui::TextWrapped("Start a game, then have your partner join it from their Netplay window "
                       "(or from File > New window on this computer, using 127.0.0.1). "
                       "Make sure you both picked the same device above.");
    ImGui::BeginDisabled(hosting);
    portCombo(hosting ? np.settings().port : 0);
    ImGui::EndDisabled();
    refreshLan(app);
    ImGui::SeparatorText("Your addresses");
    if (g_ui.lan.empty()) {
        ImGui::TextColored(kAmber(), "This computer does not seem to be on a network.");
    } else {
        hint("Your partner can pick you from their list, or type one of these:");
        for (size_t i = 0; i < g_ui.lan.size(); i++) {
            ImGui::PushID((int)i);
            ImGui::Text("%s", g_ui.lan[i].ip.c_str());
            ImGui::SameLine();
            ImGui::TextDisabled("(%s)", g_ui.lan[i].iface.c_str());
            ImGui::SameLine();
            if (ImGui::SmallButton("Copy")) ImGui::SetClipboardText(g_ui.lan[i].ip.c_str());
            ImGui::PopID();
        }
        if (!g_ui.machine.empty()) ImGui::TextDisabled("This computer is called \"%s\".", g_ui.machine.c_str());
    }
    ImGui::Spacing();
    if (!hosting) {
        if (ImGui::Button("Start hosting")) begin(app, current(NetRole::Host, ""));
    } else if (ImGui::Button("Stop hosting")) {
        app.netplay.stop();
    }
    hint("If your partner cannot connect, allow Virtual Jaguar through the firewall when asked.");
}

void drawJoin(App& app) {
    Netplay& np = app.netplay;
    const bool joined = np.settings().role == NetRole::Join;
    const bool dialing = joined && !np.settings().address.empty();
    ImGui::TextWrapped("Ask your partner to host, then pick them below or type their address.");
    ImGui::BeginDisabled(dialing);
    portCombo(dialing ? np.settings().port : 0);
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::TextDisabled("(the host must use the same)");

    ImGui::SeparatorText("Games on your network");
    if (!joined) {
        if (ImGui::Button("Look for games")) begin(app, current(NetRole::Join, ""));
        ImGui::SameLine();
        ImGui::TextDisabled("Needs a game running.");
    } else {
        std::vector<DiscoveredHost> found = np.discovered();
        if (found.empty()) {
            hint("%s", app.host.isLoaded()
                           ? "None found yet. The host needs a game running and to be hosting. Some Wi-Fi networks "
                             "block this; type the address instead."
                           : "Open a game first: the search runs while a game is running.");
        }
        for (size_t i = 0; i < found.size(); i++) {
            const DiscoveredHost& h = found[i];
            ImGui::PushID((int)i);
            ImGui::Text("%s", h.name.empty() ? h.address.c_str() : h.name.c_str());
            if (!h.name.empty()) {
                ImGui::SameLine();
                ImGui::TextDisabled("%s", h.address.c_str());
            }
            if (!h.device.empty()) {
                ImGui::SameLine();
                ImGui::TextColored(kAmber(), "uses %s - choose that above", h.device.c_str());
            }
            ImGui::SameLine();
            const bool here = dialing && np.settings().address == h.address;
            ImGui::BeginDisabled(here);
            if (ImGui::SmallButton(here ? "Joined" : "Join")) {
                std::snprintf(g_ui.address, sizeof g_ui.address, "%s", h.address.c_str());
                begin(app, current(NetRole::Join, h.address));
            }
            ImGui::EndDisabled();
            ImGui::PopID();
        }
    }

    ImGui::SeparatorText("Type an address");
    ImGui::SetNextItemWidth(ui::fitWidth(0.5f, 10.0f, 18.0f));
    bool enter = ImGui::InputTextWithHint("##addr", "192.168.1.20", g_ui.address, sizeof g_ui.address,
                                          ImGuiInputTextFlags_EnterReturnsTrue);
    ImGui::SameLine();
    if ((ImGui::Button(dialing ? "Reconnect" : "Connect") || enter) && g_ui.address[0])
        begin(app, current(NetRole::Join, g_ui.address));
    if (joined && ImGui::Button("Disconnect")) app.netplay.stop();
}

void drawTitles() {
    ImGui::SeparatorText("Games that use the link");
    ImGui::TextWrapped("Both players need the same game (and the same version of Virtual Jaguar). Connect first, "
                       "then open the game's link or network mode.");
    if (ImGui::BeginTable("titles", 3, ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_SizingStretchProp)) {
        ImGui::TableSetupColumn("Game");
        ImGui::TableSetupColumn("Device");
        ImGui::TableSetupColumn("In the game");
        ImGui::TableHeadersRow();
        struct Row { const char* game; const char* dev; const char* how; };
        static const Row rows[] = {
            {"BattleSphere Gold", "JagLink", "Pick its link / multiplayer mode."},
            {"AirCars", "JagLink", "Two Player Direct Serial, or Multiple Player Network (up to 8)."},
            {"Doom", "JagLink", "Game Mode > Deathmatch (2 players)."},
            {"Ultra Vortek", "Voice Modem",
             "At the title screen type 911 on the keypad (F2 shows it); one player dials any number, the other answers."},
        };
        for (const Row& r : rows) {
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(r.game);
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(r.dev);
            ImGui::TableNextColumn();
            ImGui::TextWrapped("%s", r.how);
        }
        ImGui::EndTable();
    }
}

}  // namespace

// ---------------------------------------------------------------------------------
// window + menu
// ---------------------------------------------------------------------------------
void drawNetplayMenu(App& app) {
    if (!ImGui::BeginMenu("Netplay")) return;
    if (app.emuThreadThisRun()) {
        ImGui::MenuItem("Netplay window...", nullptr, false, false);
        ImGui::TextDisabled("Not available while Settings > Interface > Emulation thread is on");
        ImGui::EndMenu();
        return;
    }
    if (ImGui::MenuItem("Netplay window...")) app.showNetplay = true;
    Netplay& np = app.netplay;
    if (np.configured()) {
        ImGui::Separator();
        ImGui::PushTextWrapPos(ImGui::GetFontSize() * 24.0f);
        ImGui::TextDisabled("%s", np.statusText().c_str());
        ImGui::PopTextWrapPos();
        if (ImGui::MenuItem("Disconnect")) np.stop();
    }
    ImGui::EndMenu();
}

void drawNetplay(App& app) {
    if (app.emuThreadThisRun()) {   // #967 phase 1: netplay is off for this run
        app.showNetplay = false;
        return;
    }
    initUi(app);
    ui::nextWindow(34.0f, 36.0f, 24.0f, 16.0f);
    bool open = ImGui::Begin("Netplay", &app.showNetplay);
    ui::keepOnScreen();
    if (!open) {
        ImGui::End();
        return;
    }
    Netplay& np = app.netplay;
    ImGui::TextWrapped("Play Jaguar link-cable games with a friend over your network. This connects the two "
                       "emulated link ports; it is not general online play.");
    ImGui::Separator();
    drawStatus(app);
    if (np.configured()) {
        if (ImGui::Button("Disconnect")) np.stop();
    }
    ImGui::Spacing();
    deviceChoice(app);
    ImGui::Spacing();
    if (ImGui::BeginTabBar("##netmode")) {
        if (ImGui::BeginTabItem("Host a game")) {
            drawHost(app);
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Join a game")) {
            drawJoin(app);
            ImGui::EndTabItem();
        }
        ImGui::EndTabBar();
    }
    drawLinkActivity(app);

    ImGui::SeparatorText("About link play and delay");
    ImGui::TextWrapped("The link swaps many tiny messages with your partner every frame, so it is limited by "
                       "network delay, not speed. Use wired Ethernet on both computers if you can: Wi-Fi often "
                       "feels jerky even when it seems fast. Play over the Internet or a VPN is not supported.");
    drawTitles();

    if (ImGui::CollapsingHeader("Advanced")) {
        const bool echo = np.settings().role == NetRole::Loopback;
        if (!echo) {
            if (ImGui::Button("Echo test (no partner)")) begin(app, current(NetRole::Loopback, ""));
        } else if (ImGui::Button("Stop echo test")) {
            np.stop();
        }
        hint("The console hears its own link traffic; games will say no players were found.");
        if (app.host.isLoaded() && ImGui::Button("Restart game")) app.reloadContent();
        hint("Not normally needed: link changes apply straight away.");
    }
    ImGui::End();

    if (g_ui.restartAsk) {
        ImGui::OpenPopup("Restart the game?");
        g_ui.restartAsk = false;
    }
    if (ImGui::BeginPopupModal("Restart the game?", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextWrapped("Some of these changes only take effect when the game starts over. Restart it now? "
                           "Unsaved progress is lost.");
        if (ImGui::Button("Restart")) {
            ImGui::CloseCurrentPopup();
            app.reloadContent();
        }
        ImGui::SameLine();
        if (ImGui::Button("Not now")) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
}

// ---------------------------------------------------------------------------------
// App glue
// ---------------------------------------------------------------------------------
bool netplayInit(App& app, const CliArgs& a) {
    if (app.emuThreadThisRun()) {
        // #967 phase 1: the core's link events and [NETLINK] log lines arrive on the emulation thread and Netplay is not
        // thread-safe yet, so netplay is off for this run: no taps, no window, and the command line says so.
        app.netplay.setSystemDir(app.paths.system);
        if (!a.netplay.role.empty()) {
            std::fprintf(stderr, "netplay: not available while the emulation thread is on (this version; use --emu-thread off)\n");
            return false;
        }
        return true;
    }
    app.host.onLog = [&app](int level, const char* text) { app.netplay.onCoreLog(level, text); };
    app.host.onEvent = [&app](const vj_event& e) { app.netplay.onCoreEvent(e); };
    app.netplay.setSystemDir(app.paths.system);
    const NetplayCli& c = a.netplay;
    if (c.role.empty()) return true;

    NetSettings s;
    s.device = c.device == "voicemodem" ? NetDevice::VoiceModem : NetDevice::JagLink;
    if (!c.device.empty() && c.device != "voicemodem" && c.device != "jaglink") {
        std::fprintf(stderr, "--netplay-device wants jaglink or voicemodem, got '%s'\n", c.device.c_str());
        return false;
    }
    if (c.port > 0) s.port = c.port;
    if (c.role == "host") s.role = NetRole::Host;
    else if (c.role == "join") {
        s.role = NetRole::Join;
        s.address = c.address;
        if (s.address.empty()) {
            std::fprintf(stderr, "--netplay join needs --netplay-addr ADDRESS\n");
            return false;
        }
    } else if (c.role == "join-discovered" || c.role == "browse") {
        s.role = NetRole::Join;
        // Looking around dials localhost; on the machine that is also hosting (the tests) that would
        // connect to the very host being discovered.  Look on the next port, dial on the real one.
        if (c.role == "join-discovered") s.port = (c.port > 0 ? c.port : 42171) + 1;
    }
    else if (c.role == "loopback") s.role = NetRole::Loopback;
    else {
        std::fprintf(stderr, "--netplay wants host, join, join-discovered, browse or loopback, got '%s'\n", c.role.c_str());
        return false;
    }
    std::string err;
    if (!app.netplay.start(s, err)) {
        std::fprintf(stderr, "netplay: %s\n", err.c_str());
        return false;
    }
    return true;
}

// Headless: keep the emulation running until the requested link state shows up.  Returns 0 (ok),
// 1 (failed) or 77 (skipped: UDP discovery found nobody -- CI machines often cannot broadcast).
int netplayHeadlessCheck(App& app, const CliArgs& a) {
    const NetplayCli& c = a.netplay;
    if (c.role.empty() || (c.expectLinkSec <= 0 && c.expectDiscoverySec <= 0)) return 0;
    using Clock = std::chrono::steady_clock;
    Netplay& np = app.netplay;
    const bool discover = c.role == "join-discovered" || c.role == "browse";
    const double discLimit = c.expectDiscoverySec > 0 ? c.expectDiscoverySec : c.expectLinkSec;
    const double total = std::max(c.expectLinkSec, discLimit);
    const auto t0 = Clock::now();
    auto elapsed = [&]() { return std::chrono::duration<double>(Clock::now() - t0).count(); };
    unsigned frame = 0;
    auto step = [&]() {
        app.audio.update();
        app.host.runFrame();
        np.afterFrame();
        if ((++frame & 15) == 15) app.audio.clear();
        app.host.takeMessages();
        SDL_Delay(2);   // real time, so the partner process gets to run and TCP gets to connect
    };

    size_t found = 0;
    bool dialed = !discover;
    while (elapsed() < total) {
        step();
        if (discover && !dialed) {
            if (elapsed() > discLimit) break;
            std::vector<DiscoveredHost> hosts = np.discovered();
            found = hosts.size();
            if (!hosts.empty()) {
                if (c.role == "browse") {
                    std::printf("NETPLAY discovered=%zu first=%s\n", found, hosts[0].address.c_str());
                    std::fflush(stdout);
                    return 0;
                }
                NetSettings s = np.settings();
                s.address = hosts[0].address;
                s.port = c.port > 0 ? c.port : 42171;
                std::string err;
                if (!np.start(s, err)) {
                    std::fprintf(stderr, "netplay: FAIL could not join the discovered host: %s\n", err.c_str());
                    return 1;
                }
                dialed = true;
            }
        }
        // A link that came up while only looking (the "browse" socket dials localhost) does not count.
        if (c.expectLinkSec > 0 && dialed && np.link() == LinkState::Up) break;
    }
    if (discover && !dialed) {
        std::printf("netplay: SKIP no host was discovered within %.0fs (UDP broadcast unavailable here?)\n", discLimit);
        std::fflush(stdout);
        return 77;
    }
    if (c.expectLinkSec <= 0) return 0;
    if (np.link() != LinkState::Up) {
        std::fprintf(stderr, "netplay: FAIL no link within %.0fs (state: %s)\n", total, np.statusText().c_str());
        return 1;
    }
    // With the core's event channel answered, the link must have come up through it, not by a log line.
    if (app.host.eventsActive() && np.linkUpEvents() == 0) {
        std::fprintf(stderr, "netplay: FAIL the link is up but the core sent no LINK_UP event\n");
        return 1;
    }
    const std::string want = Netplay::deviceValue(np.settings().device);
    if (np.linkDevice() != want) {
        std::fprintf(stderr, "netplay: FAIL link came up as device '%s', expected '%s'\n", np.linkDevice().c_str(),
                     want.c_str());
        return 1;
    }
    // The partner needs to see the link too before this process (and so its socket) goes away.
    const auto up = Clock::now();
    while (std::chrono::duration<double>(Clock::now() - up).count() < 1.5) step();
    std::printf("NETPLAY link=UP role=%s device=%s port=%d discovered=%zu via=%s\n", c.role.c_str(),
                np.linkDevice().c_str(), np.settings().port, found, app.host.eventsActive() ? "events" : "log");
    std::fflush(stdout);
    return 0;
}

}  // namespace vj
