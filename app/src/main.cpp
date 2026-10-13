// Virtual Jaguar 4.0 -- standalone app entry point.
#include <SDL3/SDL_main.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "app.h"

static void usage(const char* argv0) {
    std::printf(
        "Virtual Jaguar %s -- Atari Jaguar emulator\n"
        "\n"
        "usage: %s [options] [game.j64 | disc.cue | disc.chd ...]\n"
        "\n"
        "  --fullscreen          start in fullscreen\n"
        "  --no-content          boot the console with an empty cartridge slot\n"
        "  --option KEY=VALUE    set a core option for this run (not saved)\n"
        "  --data-dir DIR        keep config, saves and states in DIR\n"
        "                        (a portable.txt next to the binary does this too)\n"
        "  --verbose             log the core's INFO messages\n"
        "  --version, --help\n"
        "\n"
        "dev: --screenshot PNG [--screenshot-frame N] [--show-window settings|settings-video|states|about]\n"
        "\n"
        "headless (no window, no GL, SDL dummy audio/video drivers):\n"
        "  --headless            load each game as a drop event, run it, print a frame hash\n"
        "  --frames N            frames per game (default 300)\n"
        "  --expect-hash HEX     fail unless the first game's final frame hashes to HEX\n"
        "  --state-roundtrip     save a state, run 60 frames, load it, run 60 again: hashes must match\n"
        "  --allow-blank         a blank final frame is not a failure (synthetic test discs)\n"
        "  --dump-frame PATH     save each game's final frame as PATH.N.png\n"
        "\n"
        "Drop a ROM or disc image on the window, or use File > Open.\n",
        VJ_APP_VERSION, argv0);
}

int main(int argc, char** argv) {
    vj::CliArgs a;
    for (int i = 1; i < argc; i++) {
        const char* s = argv[i];
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
        else if (!std::strcmp(s, "--no-content")) a.noContent = true;
        else if (!std::strcmp(s, "--verbose")) a.verbose = true;
        else if (!std::strcmp(s, "--frames")) a.frames = (unsigned)std::strtoul(need(s), nullptr, 10);
        else if (!std::strcmp(s, "--expect-hash")) a.expectHash = need(s);
        else if (!std::strcmp(s, "--screenshot")) a.screenshot = need(s);
        else if (!std::strcmp(s, "--screenshot-frame")) a.screenshotFrame = (unsigned)std::strtoul(need(s), nullptr, 10);
        else if (!std::strcmp(s, "--show-window")) a.showWindow = need(s);
        else if (!std::strcmp(s, "--state-roundtrip")) a.stateRoundtrip = true;
        else if (!std::strcmp(s, "--allow-blank")) a.allowBlank = true;
        else if (!std::strcmp(s, "--dump-frame")) a.dumpFrame = need(s);
        else if (!std::strcmp(s, "--data-dir")) a.dataDir = need(s);
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
    vj::App app;
    return app.run(a);
}
