// Glue between the cheat manager (cheats.cpp, pure) and the rest of the app:
// the core's retro_cheat_* entry points, the core's notion of which game is
// loaded, the library folders, and the command-line hooks the headless tests use.
#include "app.h"
#include "cheats.h"

#include "libretro.h"
#include "titledb.h"   // TitleDBContentCRC / TitleDBTitleName: the identity the core logs at load

#include <cstdio>
#include <cstdlib>

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

}  // namespace

void cheatsInit(App& app, const CliArgs& a) {
    g_cli = CliCheats();
    g_cli.set = a.cheatSet;
    g_cli.on = a.cheatOn;
    g_cli.codes = a.cheatCodes;
    g_cli.expectRam = a.expectRam;
    g_cli.dir = a.cheatsDir;

    CheatManager& cm = app.cheats;
    cm.resetSink = [] { retro_cheat_reset(); };
    cm.setSink = [](unsigned index, bool enabled, const char* code) { retro_cheat_set(index, enabled, code); };
    cm.warn = [&app](const std::string& m) {
        std::fprintf(stderr, "%s\n", m.c_str());
        app.toast(m, 5.0, 2);
    };
    cm.setStateDir(app.paths.root + "cheats/games/");
    rebuildLibraryDirs(app);
}

void cheatsSetUserDir(App& app, const std::string& dir) {
    app.cfg.set("cheats.dir", dir);
    rebuildLibraryDirs(app);
}

void cheatsAfterLoad(App& app) {
    CheatManager& cm = app.cheats;
    cm.gameLoaded(identityOf(app.host));
    if (!cm.active()) return;

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

void cheatsOnClose(App& app) { app.cheats.gameClosed(); }

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
