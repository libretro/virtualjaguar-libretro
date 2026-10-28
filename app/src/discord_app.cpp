// Discord Rich Presence glue (#833): reads the App's state once or twice a second, describes it as a
// discord::PresenceInput and hands it to discord::Presence, which only talks to Discord when it changed.
// Runs on the UI thread and does no I/O of its own beyond a few stat() calls; every socket operation is on
// the presence worker thread (discord_ipc.cpp), so Discord being slow or absent cannot stall a frame.
#include <SDL3/SDL.h>

#include <algorithm>
#include <cctype>

#include "app.h"
#include "discord_presence.h"
#include "paths.h"

#ifndef VJ_DISCORD_APP_ID
#define VJ_DISCORD_APP_ID ""
#endif
#ifndef VJ_APP_VERSION
#define VJ_APP_VERSION ""
#endif

namespace vj {

namespace {

std::string trim(const std::string& s) {
    size_t a = 0, b = s.size();
    while (a < b && std::isspace((unsigned char)s[a])) a++;
    while (b > a && std::isspace((unsigned char)s[b - 1])) b--;
    return s.substr(a, b - a);
}

long long unixNow() {
    SDL_Time t = 0;
    SDL_GetCurrentTime(&t);
    return (long long)(t / 1000000000LL);
}

std::string lowerExt(const std::string& path) {
    const size_t dot = path.find_last_of('.');
    const size_t sep = path.find_last_of("/\\");
    if (dot == std::string::npos || (sep != std::string::npos && dot < sep)) return "";
    std::string e = path.substr(dot + 1);
    std::transform(e.begin(), e.end(), e.begin(), [](unsigned char c) { return (char)std::tolower(c); });
    return e;
}

std::string stemOf(const std::string& path) {
    const size_t sep = path.find_last_of("/\\");
    std::string f = sep == std::string::npos ? path : path.substr(sep + 1);
    const size_t dot = f.find_last_of('.');
    return dot == std::string::npos ? f : f.substr(0, dot);
}

}  // namespace

std::string discordConfiguredAppId(const Config& cfg) {
#ifdef VJ_MAC_APP_STORE
    (void)cfg;
    return trim(""); // Discord's external Unix socket isn't available inside App Sandbox.
#else
    std::string id = trim(cfg.get("discord.app_id", ""));
    return id.empty() ? trim(VJ_DISCORD_APP_ID) : id;
#endif
}

void discordNoteGameStarted(App& app) { app.discordState.gameStartUnix = unixNow(); }

void discordTick(App& app) {
    App::DiscordState& st = app.discordState;
    const double now = app.now();
    if (now - st.lastTick < 0.5) return;
    st.lastTick = now;
    if (st.appStartUnix == 0) st.appStartUnix = unixNow();

    discord::PresenceSettings ps;
    ps.appId = discordConfiguredAppId(app.cfg);
    ps.enabled = app.cfg.getBool("discord.enabled", true);
    ps.instance = app.paths.instance;
    ps.pid = processId();
    app.discordPresence.configure(ps);
    if (!app.discordPresence.active()) return;

    discord::PresenceInput in;
    in.version = VJ_APP_VERSION;
    in.showArt = app.cfg.getBool("discord.art", true);
    in.showButtons = app.cfg.getBool("discord.buttons", true);
    in.playing = app.host.isLoaded();
    in.startUnix = in.playing && st.gameStartUnix ? st.gameStartUnix : st.appStartUnix;
    if (in.playing) {
        in.paused = app.paused;
        const std::string path = app.host.contentPath();
        const lib::Entry* entry = nullptr;
        for (const lib::Entry& e : app.library.entries())
            if (!path.empty() && e.file.path == path) { entry = &e; break; }
        const std::string ext = lowerExt(path);
        in.disc = entry ? entry->section == lib::Section::Disc : lib::isDiscExt(ext);
        if (path.empty()) {
            in.title = "Jaguar BIOS";   // a no-content boot
        } else if (entry && !entry->datName.empty()) {
            in.title = discord::displayTitle(entry->datName);
        } else {
            const std::string stem = app.host.contentName().empty() ? stemOf(path) : app.host.contentName();
            in.title = discord::displayTitle(in.disc ? lib::titleFromDiscStem(stem) : lib::titleFromFileStem(stem));
        }

        // Box art: only a picture the library already holds in its cache (so it exists), as its public URL.
        const std::string datName = entry ? entry->datName : std::string();
        const std::string key = path + "|" + datName;
        if (key != st.artKey || (st.artUrl.empty() && now - st.artAt > 5.0)) {
            st.artKey = key;
            st.artAt = now;
            st.artUrl = discord::chooseArtUrl(in.disc ? lib::Section::Disc : lib::Section::Cart, datName, app.paths.root,
                                              app.library.artBaseUrl, app.library.artMirrorUrl, fileExists);
        }
        in.artUrl = st.artUrl;

        // Link-cable play: the role only, never an address.
        const NetSettings& ns = app.netplay.settings();
        if (ns.role == NetRole::Host) in.netplay = "hosting";
        else if (ns.role == NetRole::Join) in.netplay = app.netplay.link() == LinkState::Up ? "joined" : "waiting";

        // RetroAchievements: progress and the game's page.
        const auto snapshot = app.ach.clientSnapshot();
        if (snapshot.gameId != 0) {
            in.raGameId = snapshot.gameId;
            in.hardcore = app.ach.hardcoreActive();
            if (snapshot.signedIn) {
                const auto& s = snapshot.summary;
                in.achTotal = s.num_core_achievements;
                in.achUnlocked = s.num_unlocked_achievements;
            }
        }
    }
    app.discordPresence.update(in);
}

void discordShutdown(App& app) { app.discordPresence.shutdown(); }

}  // namespace vj
