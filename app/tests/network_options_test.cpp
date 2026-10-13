// Settings > Core options > Network: picking a value from a combo must show that value.
//
// Drives the REAL core through CoreHost the way drawOption() does (setOptionValue(), then
// optionValue() on the next frame), for every option in the core's "network" category and, as a
// control, for options in other categories.  A pick must stick across the frames that follow,
// because the core re-reads its variables and may re-push its option definitions.
//
// usage: vj_network_options_test <rom.j64>
#include "core_host.h"
#include "netplay.h"

#include "libretro.h"

#include <cstdio>
#include <ctime>
#include <filesystem>
#include <string>

using namespace vj;

static int g_fail = 0, g_checks = 0;
#define CHECKF(cond, ...)                                                              \
    do {                                                                               \
        g_checks++;                                                                    \
        if (!(cond)) {                                                                 \
            g_fail++;                                                                  \
            std::fprintf(stderr, "FAIL %s:%d  %s  --  ", __FILE__, __LINE__, #cond);   \
            std::fprintf(stderr, __VA_ARGS__);                                         \
            std::fprintf(stderr, "\n");                                                \
        }                                                                              \
    } while (0)

static const OptionDef* find(CoreHost& h, const std::string& key) {
    for (const OptionDef& d : h.optionDefs())
        if (d.key == key) return &d;
    return nullptr;
}

// Every value of the option other than its current one, picked one at a time like a user would.
static Netplay* g_np = nullptr;
static void frame(CoreHost& host) {
    host.runFrame();
    if (g_np) g_np->afterFrame();
}

// `expectShown` false: the pick is allowed NOT to show, but then the row must say why
// (optionOverridden), so Settings can tell the user who controls it.
static void exercise(CoreHost& host, const std::string& key, int& tried, bool allowOverridden = false) {
    const OptionDef* d0 = find(host, key);
    if (!d0) { CHECKF(false, "option %s is not registered", key.c_str()); return; }
    const std::vector<OptionValue> values = d0->values;   // a push may rebuild defs_
    for (const OptionValue& v : values) {
        const unsigned pushes = host.optionPushes();
        host.setOptionValue(key, v.value);
        tried++;
        // The UI is mid-iteration over the definitions when it calls this: it must not rebuild them.
        CHECKF(host.optionPushes() == pushes, "%s: setOptionValue re-pushed the option definitions", key.c_str());
        const OptionDef* d = find(host, key);
        if (allowOverridden && host.optionOverridden(key)) continue;
        CHECKF(d && host.optionValue(*d) == v.value, "%s: picked '%s', shows '%s' immediately", key.c_str(),
               v.value.c_str(), d ? host.optionValue(*d).c_str() : "(gone)");
        for (int f = 0; f < 3; f++) {
            frame(host);
            d = find(host, key);
            CHECKF(d && host.optionValue(*d) == v.value, "%s: picked '%s', shows '%s' after frame %d", key.c_str(),
                   v.value.c_str(), d ? host.optionValue(*d).c_str() : "(gone)", f + 1);
        }
    }
}

int main(int argc, char** argv) {
    if (argc < 2) {
        std::fprintf(stderr, "usage: vj_network_options_test <rom.j64>\n");
        return 2;
    }
    namespace fs = std::filesystem;
    std::error_code ec;
    fs::path tmp = fs::temp_directory_path(ec) / ("vj-netopts-" + std::to_string((long long)std::time(nullptr)));
    fs::create_directories(tmp / "saves", ec);
    fs::create_directories(tmp / "system", ec);
    CoreHost& host = CoreHost::instance();
    host.setDirectories((tmp / "system").string() + "/", (tmp / "saves").string() + "/");
    host.inputPoll = []() {};
    host.inputState = [](unsigned, unsigned, unsigned, unsigned) -> int16_t { return 0; };
    std::string err;
    if (!host.loadContent(argv[1], err)) {
        std::fprintf(stderr, "load failed: %s\n", err.c_str());
        return 2;
    }
    host.runFrame();

    // Which categories exist, and which options sit in "network".
    int tried = 0, netOpts = 0;
    for (const OptionDef& d : std::vector<OptionDef>(host.optionDefs()))
        if (d.categoryKey == "network") {
            netOpts++;
            exercise(host, d.key, tried);
        }
    CHECKF(netOpts >= 5, "expected the network category to hold the link options, found %d", netOpts);

    // ---- with a Netplay session active -------------------------------------------------------------
    // The Netplay window pins the link mode (and, joining, host and port) with session overrides.  A pick
    // made in Settings meanwhile may not take effect, but the row must then report that the session owns
    // it (optionOverridden) instead of silently showing the old value; every other network option, and
    // every pick once the session is over, must work as normal.
    {
        Netplay np(host);
        g_np = &np;
        NetSettings ns;
        ns.role = NetRole::Join;   // no address: mode + host + port are all overridden, nothing is dialled
        std::string nerr;
        CHECKF(np.start(ns, nerr), "netplay start: %s", nerr.c_str());
        for (int f = 0; f < 4; f++) frame(host);
        int pinned = 0;
        for (const OptionDef& d : std::vector<OptionDef>(host.optionDefs()))
            if (d.categoryKey == "network") {
                const bool owned = host.optionOverridden(d.key);
                if (owned) pinned++;
                exercise(host, d.key, tried, true);
                // not owned by the session -> the pick must have shown (exercise() checked it)
                CHECKF(owned == host.optionOverridden(d.key), "%s: ownership changed", d.key.c_str());
            }
        CHECKF(pinned == 3, "a Join session should own the mode, host and port options, owns %d", pinned);
        np.stop();
        g_np = nullptr;
        for (int f = 0; f < 4; f++) host.runFrame();
        // Session over: nothing is overridden, and the last pick made above is what shows.
        for (const OptionDef& d : std::vector<OptionDef>(host.optionDefs()))
            if (d.categoryKey == "network") {
                CHECKF(!host.optionOverridden(d.key), "%s still overridden after Disconnect", d.key.c_str());
                auto it = host.userOptions().find(d.key);
                CHECKF(it != host.userOptions().end() && host.optionValue(d) == it->second,
                       "%s: after Disconnect shows '%s', saved pick is '%s'", d.key.c_str(), host.optionValue(d).c_str(),
                       it == host.userOptions().end() ? "(none)" : it->second.c_str());
            }
    }

    // Control: a category that is known to work.
    int ctl = 0;
    exercise(host, "virtualjaguar_usefastblitter", ctl);
    CHECKF(ctl >= 2, "control option exercised %d values", ctl);

    std::printf("network_options: %d network options, %d picks, %d checks, %d failed\n", netOpts, tried, g_checks, g_fail);
    return g_fail ? 1 : 0;
}
