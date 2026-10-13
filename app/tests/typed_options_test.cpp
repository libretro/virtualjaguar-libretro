// Typed (free-form) option values: the validators, CoreHost's acceptance of them, and -- on the REAL
// core -- that a custom port, host and clock-scale percentage reach the emulation.
//
//   vj_typed_options_test ROM
//
// Part 1 (no core): typed_options.h -- ranges, canonical forms, hosts.
// Part 2 (the core, through CoreHost like Settings and --option use it):
//   * a saved / session value that passes the key's validator is handed to the core; one that does not
//     falls back to the default and is reported by checkOverrides() with the reason; list-only options
//     stay strict;
//   * end to end: a custom link port (50123), a typed host name and custom clock-scale percentages are
//     read by the core and shown in its own log lines ("[NETLINK] mode=... port=50123",
//     "[CLOCK] ... M68K 175%, RISC 250%").  The clock-scale list values still map as before.
#include "core_host.h"
#include "typed_options.h"

#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <filesystem>
#include <string>
#include <vector>

using namespace vj;

static int g_fail = 0, g_checks = 0;
#define CHECK(cond)                                                                    \
    do {                                                                               \
        g_checks++;                                                                    \
        if (!(cond)) {                                                                 \
            g_fail++;                                                                  \
            std::fprintf(stderr, "FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond);       \
        }                                                                              \
    } while (0)
#define CHECK_EQ(a, b)                                                                 \
    do {                                                                               \
        g_checks++;                                                                    \
        std::string va_ = (a), vb_ = (b);                                              \
        if (va_ != vb_) {                                                              \
            g_fail++;                                                                  \
            std::fprintf(stderr, "FAIL %s:%d  %s\n   got      '%s'\n   expected '%s'\n", __FILE__, __LINE__, #a, \
                         va_.c_str(), vb_.c_str());                                    \
        }                                                                              \
    } while (0)

static bool ok(const char* key, const std::string& v) {
    const TypedOption* t = typedOptionFor(key);
    return t && validTypedValue(*t, v);
}

static void testValidators() {
    CHECK(typedOptionFor("virtualjaguar_netlink_port") != nullptr);
    CHECK(typedOptionFor("virtualjaguar_frameskip_max") == nullptr);   // stays a list
    CHECK(typedOptionFor("virtualjaguar_usefastblitter") == nullptr);

    // ports
    const char* port = "virtualjaguar_netlink_port";
    CHECK(ok(port, "42171") && ok(port, "50123") && ok(port, "1") && ok(port, "65535"));
    CHECK(!ok(port, "0") && !ok(port, "65536") && !ok(port, "-1") && !ok(port, "") && !ok(port, "42x") &&
          !ok(port, " 42") && !ok(port, "4.2") && !ok(port, "+42") && !ok(port, "99999999999"));
    CHECK(ok("virtualjaguar_gdb_port", "3333") && !ok("virtualjaguar_gdb_port", "70000"));

    // timeouts, sensitivity
    CHECK(ok("virtualjaguar_gdb_halt_timeout", "90") && ok("virtualjaguar_gdb_halt_timeout", "3600"));
    CHECK(!ok("virtualjaguar_gdb_halt_timeout", "3601") && !ok("virtualjaguar_gdb_halt_timeout", "0") &&
          !ok("virtualjaguar_gdb_halt_timeout", "off"));   // "off" is a preset, not a typed number
    CHECK(ok("virtualjaguar_mouse_sensitivity", "137") && ok("virtualjaguar_rotary_sensitivity", "1600"));
    CHECK(!ok("virtualjaguar_mouse_sensitivity", "1601") && !ok("virtualjaguar_mouse_sensitivity", "0"));

    // per-axis tuning, signed
    CHECK(ok("virtualjaguar_mouse_offset_x", "-5") && ok("virtualjaguar_mouse_offset_x", "64") &&
          !ok("virtualjaguar_mouse_offset_x", "65") && !ok("virtualjaguar_mouse_offset_x", "-65"));
    CHECK(ok("virtualjaguar_analog_deadzone_y", "10") && !ok("virtualjaguar_analog_deadzone_y", "-1"));
    CHECK(ok("virtualjaguar_rotary_exponent", "225") && !ok("virtualjaguar_rotary_exponent", "801"));

    // clock scale: a percentage, with or without '%'; the core's "2x" style stays a list value
    const char* m68k = "virtualjaguar_m68k_clock_scale";
    CHECK(ok(m68k, "175") && ok(m68k, "175%") && ok(m68k, "25") && ok(m68k, "400%"));
    CHECK(!ok(m68k, "24") && !ok(m68k, "401") && !ok(m68k, "1.5x") && !ok(m68k, "%") && !ok(m68k, "17 5") &&
          !ok(m68k, "12345") && !ok(m68k, "175%%"));
    CHECK_EQ(canonicalTypedValue(*typedOptionFor(m68k), "175"), "175%");
    CHECK_EQ(canonicalTypedValue(*typedOptionFor(m68k), "175%"), "175%");
    CHECK_EQ(canonicalTypedValue(*typedOptionFor(port), "50123"), "50123");

    // hosts: names and IPv4 only
    const char* hk = "virtualjaguar_netlink_host";
    CHECK(ok(hk, "192.168.1.42") && ok(hk, "localhost") && ok(hk, "jaghub.local") && ok(hk, "my-pc_2.lan") &&
          ok(hk, "vj_netlink.txt") && ok(hk, "a"));
    CHECK(!ok(hk, "") && !ok(hk, "256.1.1.1") && !ok(hk, "1.2.3") && !ok(hk, "1.2.3.4.5") &&
          !ok(hk, "host:42171") && !ok(hk, "two words") && !ok(hk, "::1") && !ok(hk, "[::1]") &&
          !ok(hk, ".local") && !ok(hk, "a..b") && !ok(hk, "host.") && !ok(hk, "-bad") && !ok(hk, "bad-.x") &&
          !ok(hk, "a/b") && !ok(hk, "x\n"));
    CHECK(ok(hk, std::string(127, 'a')) == false);   // one 127-character label: over the 63 limit...
    {
        // ...but 127 characters in labels of <= 63 is fine, and 128 is not
        const std::string longName = std::string(60, 'a') + "." + std::string(60, 'a') + ".bbbbb";
        CHECK(longName.size() == 127);
        CHECK(ok(hk, longName));
        CHECK(!ok(hk, longName + "b"));
    }
    std::string why;
    CHECK(!validTypedHost("host:1", &why) && !why.empty());
}

static std::string optionOf(CoreHost& h, const char* key) {
    for (const OptionDef& d : h.optionDefs())
        if (d.key == key) return h.optionValue(d);
    return "(no such option)";
}

int main(int argc, char** argv) {
    testValidators();
    if (argc < 2) {
        std::printf("typed_options (validators only): %d checks, %d failed\n", g_checks, g_fail);
        return g_fail ? 1 : 0;
    }

    namespace fs = std::filesystem;
    std::error_code ec;
    fs::path tmp = fs::temp_directory_path(ec) / ("vj-typedopts-" + std::to_string((long long)std::time(nullptr)));
    fs::create_directories(tmp / "saves", ec);
    fs::create_directories(tmp / "system", ec);
    CoreHost& host = CoreHost::instance();
    host.setDirectories((tmp / "system").string() + "/", (tmp / "saves").string() + "/");
    host.inputPoll = []() {};
    host.inputState = [](unsigned, unsigned, unsigned, unsigned) -> int16_t { return 0; };
    std::vector<std::string> log;
    host.onLog = [&log](int, const char* text) { if (text) log.push_back(text); };
    // Saved values present BEFORE the load, as config.ini would provide them.
    host.loadUserOptions({{"virtualjaguar_netlink_port", "50123"},
                          {"virtualjaguar_gdb_halt_timeout", "9999"},          // out of range: ignored
                          {"virtualjaguar_frameskip_max", "7"}});               // list-only: still strict
    // The core's LAN discovery socket: keep it off the default port so this cannot collide with a game.
#ifdef _WIN32
    _putenv_s("VJ_DISC_PORT", std::to_string(20000 + (std::time(nullptr) % 9000)).c_str());
#else
    setenv("VJ_DISC_PORT", std::to_string(20000 + (std::time(nullptr) % 9000)).c_str(), 1);
#endif
    std::string err;
    if (!host.loadContent(argv[1], err)) {
        std::fprintf(stderr, "load failed: %s\n", err.c_str());
        return 2;
    }
    host.runFrame();

    auto sawLog = [&](const std::string& needle) {
        for (const std::string& l : log)
            if (l.find(needle) != std::string::npos) return true;
        return false;
    };
    auto frames = [&](int n) { for (int i = 0; i < n; i++) host.runFrame(); };

    // ---- CoreHost's acceptance ---------------------------------------------------------------------
    CHECK_EQ(optionOf(host, "virtualjaguar_netlink_port"), "50123");                 // a custom saved value is honoured
    CHECK_EQ(optionOf(host, "virtualjaguar_gdb_halt_timeout"), "off");               // an invalid one falls back to default
    CHECK_EQ(optionOf(host, "virtualjaguar_frameskip_max"), "3");                    // lists stay strict
    {
        std::vector<std::string> problems = host.checkOverrides(false);
        bool haltReported = false, skipReported = false, portReported = false;
        for (const std::string& p : problems) {
            if (p.find("virtualjaguar_gdb_halt_timeout") != std::string::npos) {
                haltReported = true;
                CHECK(p.find("1-3600") != std::string::npos);   // says what IS valid
            }
            if (p.find("virtualjaguar_frameskip_max") != std::string::npos) skipReported = true;
            if (p.find("virtualjaguar_netlink_port") != std::string::npos) portReported = true;
        }
        CHECK(haltReported && skipReported && !portReported);
    }
    host.overrideOption("virtualjaguar_netlink_host", "build-box.example");   // --option / Netplay style
    host.overrideOption("virtualjaguar_netlink_wait", "enabled");              // list value: fine
    host.overrideOption("virtualjaguar_usefastblitter", "maybe");              // not in the list, not typed
    {
        bool blitReported = false, hostReported = false;
        for (const std::string& p : host.checkOverrides(true)) {
            if (p.find("virtualjaguar_usefastblitter") != std::string::npos) blitReported = true;
            if (p.find("virtualjaguar_netlink_host") != std::string::npos) hostReported = true;
        }
        CHECK(blitReported && !hostReported);
    }
    host.clearOverride("virtualjaguar_usefastblitter");
    host.clearOverride("virtualjaguar_netlink_wait");
    host.overrideOption("virtualjaguar_netlink_host", "not a host:1");
    CHECK_EQ(optionOf(host, "virtualjaguar_netlink_host"), "127.0.0.1");   // invalid typed session value: default
    host.clearOverride("virtualjaguar_netlink_host");

    // ---- the core reads the typed values ----------------------------------------------------------
    // Link port + host name: the core logs the endpoint it will use when the link mode is applied.
    log.clear();
    host.overrideOption("virtualjaguar_netlink", "tcp_server");
    frames(3);
    CHECK(sawLog("mode=tcp_server") && sawLog("port=50123"));
    log.clear();
    host.overrideOption("virtualjaguar_netlink", "tcp_client");
    host.overrideOption("virtualjaguar_netlink_host", "localhost");   // a typed NAME, not a list row
    host.setOptionValue("virtualjaguar_netlink_port", "50124");
    frames(3);
    CHECK(sawLog("peer=localhost:50124"));
    host.overrideOption("virtualjaguar_netlink", "disabled");
    frames(3);

    // Clock scale: typed percentages are read by the core (its log announces any non-stock scale).
    log.clear();
    host.setOptionValue("virtualjaguar_m68k_clock_scale", "175%");
    host.setOptionValue("virtualjaguar_risc_clock_scale", "250");
    frames(3);
    CHECK_EQ(optionOf(host, "virtualjaguar_m68k_clock_scale"), "175%");
    CHECK(sawLog("M68K 175%, RISC 250%"));
    // The list values map exactly as before.
    log.clear();
    host.setOptionValue("virtualjaguar_m68k_clock_scale", "2x");
    host.setOptionValue("virtualjaguar_risc_clock_scale", "1.5x");
    frames(3);
    CHECK(sawLog("M68K 200%, RISC 150%"));
    log.clear();
    host.setOptionValue("virtualjaguar_m68k_clock_scale", "1x");
    host.setOptionValue("virtualjaguar_risc_clock_scale", "1x");
    frames(3);
    CHECK(!sawLog("Non-stock clock scales"));   // stock stays silent

    host.onLog = nullptr;
    host.unloadContent();
    fs::remove_all(tmp, ec);   // our own temp dir only
    std::printf("typed_options: %d checks, %d failed\n", g_checks, g_fail);
    return g_fail ? 1 : 0;
}
