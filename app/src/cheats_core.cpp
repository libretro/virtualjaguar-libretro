// Glue between the cheat manager (cheats.cpp, pure) and the rest of the app:
// the core's retro_cheat_* entry points, the core's notion of which game is
// loaded, the library folders, and the command-line hooks the headless tests use.
#include "app.h"
#include "cheats.h"
#include "http.h"

#include "libretro.h"
#include "titledb.h"   // TitleDBContentCRC / TitleDBTitleName: the identity the core logs at load

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <thread>
#include <vector>

namespace vj {

namespace {

struct CliCheats {
    std::string set;
    std::vector<std::string> on;
    std::vector<std::string> codes;
    std::vector<std::pair<uint32_t, uint8_t>> expectRam;
    std::string dir;
    bool failed = false;   // a --cheat-set / --cheat-on / --cheat the manager refused
};
CliCheats g_cli;

GameIdentity identityOf(const CoreHost& host) {
    GameIdentity id;
    if (!host.isLoaded() || host.contentPath().empty()) return id;   // no cartridge: nothing to key on
    id.fileStem = host.contentName();
    id.crc = TitleDBContentCRC();   // cartridges only; header-normalised, so a copier-headered dump keys alike
    const char* t = TitleDBTitleName();
    if (t) id.title = t;
    char buf[32];
    if (id.crc) {
        std::snprintf(buf, sizeof buf, "crc-%08x", (unsigned)id.crc);
        id.key = buf;
    } else {
        // Discs are never hashed (a disc image is hundreds of MB); the core's
        // boot-stub row, or failing that the file name, stands in.
        std::string slug = slugify(!id.title.empty() ? id.title : id.fileStem);
        id.key = (!id.title.empty() ? "title-" : "file-") + (slug.empty() ? std::string("unnamed") : slug);
    }
    return id;
}

void rebuildLibraryDirs(App& app) {
    std::vector<std::string> dirs;
    if (!g_cli.dir.empty()) dirs.push_back(g_cli.dir);
    std::string user = app.cfg.get("cheats.dir", "");
    if (!user.empty()) dirs.push_back(user);
    dirs.push_back(app.paths.root + "cheats/");
    const char* base = SDL_GetBasePath();   // build/cheats/ is where -DVJ_APP_FETCH_CHEATS=ON puts the database
    if (base) dirs.push_back(std::string(base) + "cheats/");
    app.cheats.setLibraryDirs(dirs);
    app.cheats.rescan();
}

uint8_t* systemRam(size_t& size) {
    size = retro_get_memory_size(RETRO_MEMORY_SYSTEM_RAM);
    return (uint8_t*)retro_get_memory_data(RETRO_MEMORY_SYSTEM_RAM);
}

// On the emulation thread (or inline): the cheat list as one command.
void applyCheatBatch(const CheatBatch& b) {
    for (const CheatOp& o : b) {
        if (o.reset) retro_cheat_reset();
        else retro_cheat_set(o.index, o.enabled, o.code.c_str());
    }
}

}  // namespace

void cheatsInit(App& app, const CliArgs& a) {
    g_cli = CliCheats();
    g_cli.set = a.cheatSet;
    g_cli.on = a.cheatOn;
    g_cli.codes = a.cheatCodes;
    g_cli.expectRam = a.expectRam;
    g_cli.dir = a.cheatsDir;

    CheatManager& cm = app.cheats;
    // The cheat list is one command: posted to the emulation thread, run at once when inline or already there.
    cm.commit = [&app](CheatBatch b) {
        app.emu.post([b = std::move(b)] { applyCheatBatch(b); });
    };
    // RetroAchievements hardcore: no cheat may reach the core.  The saved selections
    // stay on disk untouched and go back in when hardcore ends (see the callback).
    cm.blocked = [&app] { return !app.ach.allowCheats(); };
    app.ach.onHardcoreChanged = [&app](bool hardcore) {
        app.cheats.apply();   // empties the core's list; re-sends the selections only when allowed
        const unsigned n = app.cheats.active() ? app.cheats.enabledCount() : 0;
        if (hardcore && n)
            app.ach.notice("Cheats are off", "Hardcore mode: no cheat reaches the game. Your saved cheats come back when it ends.");
        else if (!hardcore && n)
            app.ach.notice("Cheats are back", std::to_string(n) + " cheat" + (n == 1 ? "" : "s") + " applied again (hardcore ended).");
    };
    cm.warn = [&app](const std::string& m) {
        std::fprintf(stderr, "%s\n", m.c_str());
        app.toast(m, 5.0, 2);
    };
    cm.setStateDir(app.paths.root + "cheats/games/");
    rebuildLibraryDirs(app);

    // The community cheat store (#1071).  Packs live under <data>/cheats/community/packs/, which the library scan never
    // looks into (it is not recursive), so a pack is only ever found by its cartridge's CRC.
    CheatStore& store = app.cheatStore;
    store.setDir(app.paths.root + "cheats/community/");
    cm.setCommunityDir(store.packsDir());
    store.setBaseUrl(app.cfg.get("cheats.community.url", ""));   // "" = the project site; a loopback server for tests
    // The one downloads switch (Library: "Download art and cheat packs") and a backend that can do HTTP.
    store.downloadsAllowed = [&app] { return app.library.downloadArt.load() && http::available(); };
    // Runs on the worker thread: touches nothing but the cancel flag (stable for the store's life) and http.
    const std::atomic<bool>* cancel = &store.cancelFlag();
    store.fetch = [cancel](const std::string& url, size_t maxBytes, std::string& body, std::string& err) {
        http::Response r;
        if (!http::get(url, r, maxBytes, cancel)) {
            err = r.error.empty() ? "request failed" : r.error;
            return false;
        }
        if (r.status != 200) {   // a redirect that http::get did not follow, an error page: never a pack or a manifest
            err = "HTTP " + std::to_string(r.status);
            return false;
        }
        body.assign(r.body.begin(), r.body.end());
        return true;
    };
    app.cheatNet.start();
    store.run = [&app](std::function<void()> job) { app.cheatNet.post(std::move(job)); };
    store.message = [&app](const std::string& text) { app.toast(text, 4.0); };   // a pack's on-load notes
}

void cheatsSetUserDir(App& app, const std::string& dir) {
    app.cfg.set("cheats.dir", dir);
    rebuildLibraryDirs(app);
}

void cheatsAfterLoad(App& app) {
    CheatManager& cm = app.cheats;
    cm.gameLoaded(identityOf(app.host));   // apply() inside sends nothing while hardcore blocks cheats
    if (!cm.active()) return;
    // This runs on the emulation thread in threaded mode; the store is main-thread only, so the load is handed over and
    // cheatsPump applies it (after the manager above: it reads the community set that load just made).
    app.cheatStore.postGameLoaded(cm.game().crc);

    if (cm.blockedNow()) {
        // The command-line twins of the window's toggles are as disabled as the window:
        // they would also rewrite the saved selections.
        if (!g_cli.set.empty() || !g_cli.on.empty() || !g_cli.codes.empty())
            std::fprintf(stderr, "cheats: --cheat-set/--cheat-on/--cheat ignored: disabled in hardcore mode\n");
        const unsigned saved = cm.enabledCount();
        // app.toast / app.ach.notice here (and the toast below) run on the emulation thread in threaded mode.  That is
        // safe only because every caller is inside emu.call (the main thread is parked in busyFrame, which touches
        // neither osd_ nor ImGui); posting loads via emu.post instead would break this.
        app.ach.notice("Cheats disabled in hardcore mode",
                       saved ? std::to_string(saved) + " saved cheat" + (saved == 1 ? " is" : "s are") + " not applied."
                             : std::string("RetroAchievements hardcore does not allow them."));
        return;
    }

    // Command-line equivalents of what the window does (the headless tests drive these).
    if (!g_cli.set.empty() && !cm.selectSet(g_cli.set)) {
        std::fprintf(stderr, "cheats: no cheat set named '%s' in the library\n", g_cli.set.c_str());
        g_cli.failed = true;
    }
    for (const std::string& name : g_cli.on)
        if (!cm.setEnabledByName(name, true)) {
            std::fprintf(stderr, "cheats: no usable cheat '%s' in the selected set\n", name.c_str());
            g_cli.failed = true;
        }
    for (const std::string& code : g_cli.codes) {
        std::string err;
        if (!cm.addUser("Command line", code, err)) {
            std::fprintf(stderr, "cheats: --cheat '%s': %s\n", code.c_str(), err.c_str());
            g_cli.failed = true;
        }
    }
    unsigned n = cm.enabledCount();
    if (n) app.toast(std::to_string(n) + " cheat" + (n == 1 ? "" : "s") + " on");
}

void cheatsAfterStateLoad(App& app) {
    // The core keeps its cheat list outside the savestate, so it is untouched by
    // a restore; re-sending it anyway costs nothing and keeps this independent of that.
    app.cheats.apply();
}

void cheatsOnClose(App& app) {
    app.cheats.gameClosed();
    app.cheatStore.postGameClosed();   // may be the emulation thread: applied by cheatsPump
}

void cheatsPump(App& app) {
    // Pending load / close first (inside pump), then finished requests.  Every frame, not only while the Cheats
    // window is open: an install must finish (and its files be written) even if the window was closed meanwhile.
    // afterApply is latched and cheap; it covers a cheat turned on in the window and the end of a hardcore block.
    app.cheatStore.pump(app.cheats);
    app.cheatStore.afterApply(app.cheats);
}

void cheatsShutdown(App& app) {
    // The store first: it raises the cancel flag, so a request inside http::get ends at once instead of after its
    // timeouts; then the worker (drops anything queued, joins).  Both before `cheats` is destroyed.
    app.cheatStore.stop();
    app.cheatNet.stop();
}

// --cheats-community-install: what opening the Cheats window and pressing Install do, without a window (the headless
// smoke test).  Needs a cartridge; the store's URL comes from cheats.community.url in the data dir's config.ini.
bool cheatsHeadlessCommunity(App& app, const CliArgs& a) {
    if (!a.cheatsCommunityInstall) return true;
    CheatManager& cm = app.cheats;
    CheatStore& store = app.cheatStore;
    if (!cm.active() || cm.game().crc == 0) {
        std::fprintf(stderr, "headless: FAIL --cheats-community-install needs a cartridge\n");
        return false;
    }
    app.library.downloadArt = app.cfg.getBool("library.art", true);   // the library service is not started headless
    const uint32_t crc = cm.game().crc;
    auto wait = [&](const char* what) {
        for (int i = 0; i < 600 && store.busy(); i++) {   // 30 s
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
            store.pump(cm);
        }
        store.pump(cm);
        if (store.busy()) {
            std::fprintf(stderr, "headless: FAIL community %s did not finish\n", what);
            return false;
        }
        return true;
    };
    store.windowOpened(crc, app.cfg);
    if (!wait("manifest fetch")) return false;
    PackState st = store.state(crc);
    if (st == PackState::Offered || st == PackState::UpdateAvailable) {
        if (!store.install(crc) || !wait("install")) {
            std::fprintf(stderr, "headless: FAIL community install: %s\n", store.status().c_str());
            return false;
        }
        st = store.state(crc);
    }
    const ChtSet* set = cm.communitySet();
    if (st != PackState::Installed || !set || set->cheats.empty()) {
        std::fprintf(stderr, "headless: FAIL community pack for %08X not installed (%s)\n", (unsigned)crc,
                     store.status().c_str());
        return false;
    }
    std::printf("COMMUNITY pack %08X installed: %zu cheats\n", (unsigned)crc, set->cheats.size());
    return true;
}

bool cheatsHeadlessCheck(App& app, const CliArgs& a) {
    (void)app;
    bool ok = !g_cli.failed;
    size_t size = 0;
    const uint8_t* ram = systemRam(size);
    for (const auto& e : a.expectRam) {
        if (!ram || e.first >= size) {
            std::fprintf(stderr, "headless: FAIL ram 0x%06X is not readable\n", (unsigned)e.first);
            ok = false;
            continue;
        }
        if (ram[e.first] != e.second) {
            std::fprintf(stderr, "headless: FAIL ram 0x%06X is %02X, expected %02X\n", (unsigned)e.first,
                         (unsigned)ram[e.first], (unsigned)e.second);
            ok = false;
        } else {
            std::printf("RAM 0x%06X = %02X ok\n", (unsigned)e.first, (unsigned)e.second);
        }
    }
    if (!ok && g_cli.failed) std::fprintf(stderr, "headless: FAIL a cheat command-line option was rejected\n");
    return ok;
}

}  // namespace vj
