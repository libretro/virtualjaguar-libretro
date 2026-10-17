// Virtual Jaguar -- standalone app entry point.
#include <SDL3/SDL_main.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "app.h"
#include "platform/commands.h"
#include "platform/platform.h"

static void usage(const char* argv0) {
    std::printf(
        "Virtual Jaguar %s -- Atari Jaguar emulator\n"
        "\n"
        "usage: %s [options] [game.j64 | disc.cue | disc.chd ...]\n"
        "\n"
        "  --fullscreen          start in fullscreen\n"
        "  --theme system|dark|light|custom:NAME   colour theme for this run (not saved; Settings > Interface > Appearance sets it)\n"
        "  --no-content          boot the console with no cartridge: the CD BIOS (use --boot-bios cart for the boot ROM)\n"
        "  --boot-bios cart|cd   boot a BIOS on its own: the cartridge boot ROM with an empty slot, or the CD BIOS with no disc\n"
        "  --cart-bios FILE      run the console boot ROM from FILE for this run (128 KiB; wins over the saved choice)\n"
        "  --cd-bios FILE        run the Jaguar CD BIOS from FILE for this run (256 KiB)\n"
        "  --option KEY=VALUE    set a core option for this run (not saved)\n"
        "  --data-dir DIR        keep config, saves and states in DIR\n"
        "                        (a portable.txt next to the binary does this too)\n"
        "  --verbose             log the core's INFO messages\n"
        "  --version, --help\n"
        "\n"
        "netplay (the command-line twin of the Netplay window; link-cable play over the network):\n"
        "  --neuro URL           let Neuro-sama (or any Neuro API server) play, this run: ws://HOST:PORT (docs/neuro.md)\n"
        "  --netplay ROLE        host | join | join-discovered | browse | loopback\n"
        "  --netplay-addr ADDR   join: the host's address (no port)\n"
        "  --netplay-port N      link port, 1-65535 (default 42171; the window offers 42171-42174 and a custom box)\n"
        "  --netplay-device D    jaglink (default) | voicemodem\n"
        "  --netplay-expect-link SECS       headless: run until the link is UP, FAIL after SECS\n"
        "  --netplay-expect-discovery SECS  headless: join-discovered / browse; exit 77 (skip) if no host appears\n"
        "\n"
        "dev: --perf-frames N [--perf-gpu] [--perf-pause] [--perf-no-reuse] [--perf-shader-out WxH]\n"
        "     print where a UI frame's time goes over N frames (and the shader chain's GPU cost), then exit\n"
        "dev: --dump-native-menu   print and check the real macOS menu bar and Dock menu, then exit (77 = none on this OS)\n"
        "dev: --screenshot PNG [--screenshot-frame N] [--ui-scale X] [--low-dpi] [--pixel-density N]\n"
        "     [--font retro|system|mono[:SIZE]|custom:FILE] [--font-then SPEC]   (this run only; --font-then switches live after 60 frames)\n"
        "     [--show-window settings[-video|-input|-audio|-bios|-storage|-interface]|states|about|cheats|library|login|achievements|keypad|netplay|controls|theme-editor]\n"
        "     [--show-window memory|palette|scanner|objects|blitter|textures|scope]   (Tools windows; needs a game)\n"
        "\n"
        "headless (no window, no GL, SDL dummy audio/video drivers):\n"
        "  --headless            load each game as a drop event, run it, print a frame hash\n"
        "  --frames N            frames per game (default 300)\n"
        "  --expect-hash HEX     fail unless the first game's final frame hashes to HEX\n"
        "  --state-roundtrip     save a state, run 60 frames, load it, run 60 again: hashes must match\n"
        "  --allow-blank         a blank final frame is not a failure (synthetic test discs)\n"
        "  --expect-bios R:SRC   fail unless the core ran the cart|cd BIOS from builtin, system or file (the --cart-bios/--cd-bios file)\n"
        "  --print-controls      print Player 1's controls cheat sheet (the active bindings) after the load\n"
        "  --expect-controls-overlay shown|hidden   fail unless the controls overlay is (not) showing after the load\n"
        "  --dump-frame PATH     save each game's final frame as PATH.N.png\n"
        "  --tv-test-script FILE replay FILE's key / pad / focus events against the TV menu (format: tests/tv_menu_headless.txt.in).\n"
        "                        --headless: instead of running content, --frames N = settle for N frame times at the end.\n"
        "                        Windowed: the lines run between frames, `expect` only prints a mismatch (for a --screenshot of the menu)\n"
        "\n"
        "cheats (the command-line twins of the Cheats window; also work headless):\n"
        "  --cheats-dir DIR      look for .cht files (libretro cheat database) in DIR too\n"
        "  --cheat-set NAME      use the cheat set NAME.cht for the loaded game (remembered)\n"
        "  --cheat-on CHEAT      turn on a database cheat by description, id or position (repeatable)\n"
        "  --cheat CODE          add a manual code, hex ADDRESS:VALUE e.g. 00007D3D:05 (repeatable)\n"
        "  --expect-ram A=VV     headless: fail unless main-RAM byte A (hex) is VV (hex) at the end\n"
        "\n"
        "RetroAchievements test hooks (headless only; the only server is a directory of canned\n"
        "<api>.json replies, so nothing here can reach or sign in to the real service):\n"
        "  --ra-mock DIR         answer rc_client's requests from DIR/<r>.json and sign in as a mock user\n"
        "  --ra-hardcore         run the mock session in hardcore mode\n"
        "  --ra-poke FRAME:CODE  switch the core cheat CODE on at FRAME (a known RAM write)\n"
        "  --ra-expect-unlock ID / --ra-expect-no-unlock ID / --ra-expect-request NAME / --ra-expect-hash HEX\n"
        "  --ra-step FRAME:ACTION  hardcore:on | hardcore:off | signout | poke:ADDR=VV | expect:ADDR=VV (hex, main RAM)\n"
        "  --ra-expect-notice TEXT  a player notice containing TEXT must have been shown\n"
        "  --ra-login-at FRAME   sign in only at FRAME (a late sign-in); --ra-expect-reset N: clean restarts\n  --ra-expect-load-error / --ra-expect-hardcore / --ra-expect-softcore / --ra-expect-blocked\n"
        "\n"
        "Drop a ROM or disc image on the window, or use File > Open.\n",
        VJ_APP_VERSION, argv0);
}

int main(int argc, char** argv) {
    vj::CliArgs a;
    for (int i = 1; i < argc; i++) {
        const char* s = argv[i];
        // Arguments a launcher adds (macOS "-psn_0_12345") are not ours and not an error.
        if (int skip = vj::platform::ignorableLauncherArgs(argc, argv, i)) {
            i += skip - 1;
            continue;
        }
        auto need = [&](const char* name) -> const char* {
            if (i + 1 >= argc) {
                std::fprintf(stderr, "%s needs an argument\n", name);
                std::exit(2);
            }
            return argv[++i];
        };
        if (!std::strcmp(s, "--help") || !std::strcmp(s, "-h")) a.showHelp = true;
        else if (!std::strcmp(s, "--version")) a.showVersion = true;
        else if (!std::strcmp(s, "--headless")) a.headless = true;
        else if (!std::strcmp(s, "--fullscreen")) a.fullscreen = true;
        else if (!std::strcmp(s, "--theme")) {
            a.theme = need(s);
            if (a.theme != "system" && a.theme != "dark" && a.theme != "light" && a.theme.compare(0, 7, "custom:") != 0) {
                std::fprintf(stderr, "--theme wants system, dark, light or custom:NAME, got '%s'\n", a.theme.c_str());
                return 2;
            }
        }
        else if (!std::strcmp(s, "--no-content")) a.noContent = true;
        else if (!std::strcmp(s, "--boot-bios")) {
            a.bootBios = need(s);
            if (a.bootBios != "cart" && a.bootBios != "cd") {
                std::fprintf(stderr, "--boot-bios wants cart or cd, got '%s'\n", a.bootBios.c_str());
                return 2;
            }
            a.noContent = true;
        }
        else if (!std::strcmp(s, "--cart-bios")) a.cartBios = need(s);
        else if (!std::strcmp(s, "--cd-bios")) a.cdBios = need(s);
        else if (!std::strcmp(s, "--expect-bios")) {
            std::string v = need(s);
            size_t c = v.find(':');
            std::string role = c == std::string::npos ? "" : v.substr(0, c), src = c == std::string::npos ? "" : v.substr(c + 1);
            if ((role != "cart" && role != "cd") || (src != "builtin" && src != "system" && src != "file")) {
                std::fprintf(stderr, "--expect-bios wants cart|cd:builtin|system|file, got '%s'\n", v.c_str());
                return 2;
            }
            a.expectBios.push_back(v);
        }
        else if (!std::strcmp(s, "--verbose")) a.verbose = true;
        else if (!std::strcmp(s, "--dump-native-menu")) a.dumpNativeMenu = true;
        else if (!std::strcmp(s, "--ra-mock")) a.raMock = need(s);
        else if (!std::strcmp(s, "--ra-hardcore")) a.raHardcore = true;
        else if (!std::strcmp(s, "--ra-poke")) {
            std::string v = need(s);
            size_t c = v.find(':');
            if (c == std::string::npos || c == 0) {
                std::fprintf(stderr, "--ra-poke wants FRAME:CODE, got '%s'\n", v.c_str());
                return 2;
            }
            a.raPokes.emplace_back((unsigned)std::strtoul(v.c_str(), nullptr, 10), v.substr(c + 1));
        }
        else if (!std::strcmp(s, "--ra-expect-unlock")) a.raExpectUnlock.push_back((unsigned)std::strtoul(need(s), nullptr, 10));
        else if (!std::strcmp(s, "--ra-expect-no-unlock")) a.raExpectNoUnlock.push_back((unsigned)std::strtoul(need(s), nullptr, 10));
        else if (!std::strcmp(s, "--ra-expect-request")) a.raExpectRequests.emplace_back(need(s));
        else if (!std::strcmp(s, "--ra-expect-hash")) a.raExpectHash = need(s);
        else if (!std::strcmp(s, "--ra-expect-load-error")) a.raExpectLoadError = true;
        else if (!std::strcmp(s, "--ra-expect-hardcore")) a.raExpectHardcore = true;
        else if (!std::strcmp(s, "--ra-expect-softcore")) a.raExpectSoftcore = true;
        else if (!std::strcmp(s, "--ra-expect-blocked")) a.raExpectBlocked = true;
        else if (!std::strcmp(s, "--ra-step")) {
            std::string v = need(s);
            size_t c = v.find(':');
            if (c == std::string::npos || c == 0) {
                std::fprintf(stderr, "--ra-step wants FRAME:ACTION, got '%s'\n", v.c_str());
                return 2;
            }
            a.raSteps.emplace_back((unsigned)std::strtoul(v.c_str(), nullptr, 10), v.substr(c + 1));
        }
        else if (!std::strcmp(s, "--ra-expect-notice")) a.raExpectNotice.emplace_back(need(s));
        else if (!std::strcmp(s, "--ra-login-at")) a.raLoginAt = (unsigned)std::strtoul(need(s), nullptr, 10);
        else if (!std::strcmp(s, "--ra-expect-reset")) a.raExpectReset = (int)std::strtol(need(s), nullptr, 10);
        else if (!std::strcmp(s, "--print-controls")) a.printControls = true;
        else if (!std::strcmp(s, "--expect-controls-overlay")) {
            std::string v = need(s);
            if (v != "shown" && v != "hidden") {
                std::fprintf(stderr, "--expect-controls-overlay wants shown or hidden, got '%s'\n", v.c_str());
                return 2;
            }
            a.expectControls = v == "shown" ? 1 : 0;
        }
        else if (!std::strcmp(s, "--tv-test-script")) a.tvTestScript = need(s);
        else if (!std::strcmp(s, "--neuro")) a.neuroUrl = need(s);
        else if (!std::strcmp(s, "--netplay")) a.netplay.role = need(s);
        else if (!std::strcmp(s, "--netplay-addr")) a.netplay.address = need(s);
        else if (!std::strcmp(s, "--netplay-device")) a.netplay.device = need(s);
        else if (!std::strcmp(s, "--netplay-port")) a.netplay.port = (int)std::strtol(need(s), nullptr, 10);
        else if (!std::strcmp(s, "--netplay-expect-link")) a.netplay.expectLinkSec = std::strtod(need(s), nullptr);
        else if (!std::strcmp(s, "--netplay-expect-discovery")) a.netplay.expectDiscoverySec = std::strtod(need(s), nullptr);
        else if (!std::strcmp(s, "--frames")) a.frames = (unsigned)std::strtoul(need(s), nullptr, 10);
        else if (!std::strcmp(s, "--expect-hash")) a.expectHash = need(s);
        else if (!std::strcmp(s, "--perf-frames")) a.perfFrames = (unsigned)std::strtoul(need(s), nullptr, 10);
        else if (!std::strcmp(s, "--perf-gpu")) a.perfGpu = true;
        else if (!std::strcmp(s, "--perf-pause")) a.perfPause = true;
        else if (!std::strcmp(s, "--perf-no-reuse")) a.perfNoReuse = true;
        else if (!std::strcmp(s, "--perf-shader-out")) {
            unsigned w = 0, h = 0;
            if (std::sscanf(need(s), "%ux%u", &w, &h) != 2 || !w || !h) {
                std::fprintf(stderr, "--perf-shader-out wants WxH\n");
                return 2;
            }
            a.perfShaderW = w;
            a.perfShaderH = h;
        }
        else if (!std::strcmp(s, "--screenshot")) a.screenshot = need(s);
        else if (!std::strcmp(s, "--screenshot-frame")) a.screenshotFrame = (unsigned)std::strtoul(need(s), nullptr, 10);
        else if (!std::strcmp(s, "--show-window")) a.showWindow = need(s);
        else if (!std::strcmp(s, "--ui-scale")) a.uiScale = (float)std::strtod(need(s), nullptr);
        else if (!std::strcmp(s, "--low-dpi")) a.lowDpi = true;
        else if (!std::strcmp(s, "--pixel-density")) a.pixelDensity = (unsigned)std::strtoul(need(s), nullptr, 10);
        else if (!std::strcmp(s, "--font")) a.font = need(s);
        else if (!std::strcmp(s, "--font-then")) a.fontThen = need(s);
        else if (!std::strcmp(s, "--state-roundtrip")) a.stateRoundtrip = true;
        else if (!std::strcmp(s, "--allow-blank")) a.allowBlank = true;
        else if (!std::strcmp(s, "--dump-frame")) a.dumpFrame = need(s);
        else if (!std::strcmp(s, "--data-dir")) a.dataDir = need(s);
        else if (!std::strcmp(s, "--cheats-dir")) a.cheatsDir = need(s);
        else if (!std::strcmp(s, "--cheat-set")) a.cheatSet = need(s);
        else if (!std::strcmp(s, "--cheat-on")) a.cheatOn.emplace_back(need(s));
        else if (!std::strcmp(s, "--cheat")) a.cheatCodes.emplace_back(need(s));
        else if (!std::strcmp(s, "--expect-ram")) {
            std::string kv = need(s);
            size_t eq = kv.find('=');
            char *e1 = nullptr, *e2 = nullptr;
            unsigned long addr = 0, val = 0;
            if (eq != std::string::npos) {
                addr = std::strtoul(kv.c_str(), &e1, 16);
                val = std::strtoul(kv.c_str() + eq + 1, &e2, 16);
            }
            if (eq == std::string::npos || e1 != kv.c_str() + eq || !*(kv.c_str() + eq + 1) || *e2 || val > 0xFF) {
                std::fprintf(stderr, "--expect-ram wants ADDR=VV (hex, one byte), got '%s'\n", kv.c_str());
                return 2;
            }
            a.expectRam.emplace_back((uint32_t)addr, (uint8_t)val);
        }
        else if (!std::strcmp(s, "--option")) {
            std::string kv = need(s);
            size_t eq = kv.find('=');
            if (eq == std::string::npos) {
                std::fprintf(stderr, "--option wants KEY=VALUE, got '%s'\n", kv.c_str());
                return 2;
            }
            a.options.emplace_back(kv.substr(0, eq), kv.substr(eq + 1));
        } else if (s[0] == '-' && s[1]) {
            std::fprintf(stderr, "unknown option %s (try --help)\n", s);
            return 2;
        } else {
            a.content.emplace_back(s);
        }
    }
    if (a.showHelp) {
        usage(argv[0]);
        return 0;
    }
    if (a.showVersion) {
        std::printf("Virtual Jaguar %s\n", VJ_APP_VERSION);
        return 0;
    }
    if (a.dumpNativeMenu) return vj::platform::dumpNativeMenu();
    vj::App app;
    return app.run(a);
}
