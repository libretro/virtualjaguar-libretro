// --tv-test-script FILE (headless dev flag): replays a script of key / pad / focus events against the real App --
// no window, no ImGui -- and checks what the TV shell and the input layer did.  It is the test of the input
// ownership rules (spec 3.1): who gets F4, a Share hold, Esc, a pad button, and when.  Format and verbs: the header
// of tests/tv_menu_headless.txt.in.
//
// Time.  Hold bindings read the real clock (the HoldArbiter edges carry SDL_GetTicks()), so the script runs in real
// time.  A line's <ms> only fixes the GAP to the previous line, which is a minimum: a loaded machine can make a gap
// longer, never shorter, and a line that is late still runs before the next input housekeeping tick -- so a held
// button is never held for less than the script says and a focus loss never loses a race to the hold timer.
//
// The pad.  The runner attaches a virtual gamepad (SDL_AttachVirtualJoystick, type GAMEPAD, every button and axis of
// the standard layout including MISC1/Share) and presses its buttons with SDL_SetJoystickVirtualButton.  It reaches
// InputManager through the ordinary SDL_EVENT_GAMEPAD_ADDED hot-plug path, exactly like a real pad.  If it does not
// open there, the run FAILS: nothing here fakes GAMEPAD_* events for an unopened id.
#include <SDL3/SDL.h>

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include "app.h"
#include "devices_ui.h"
#include "tv/tv_shell.h"

namespace vj {

namespace {

struct Line {
    unsigned no = 0;
    uint64_t ms = 0;
    std::vector<std::string> w;   // verb and arguments
};

bool readScript(const std::string& path, std::vector<Line>& out, std::string& err) {
    std::ifstream f(path);
    if (!f) {
        err = "cannot read " + path;
        return false;
    }
    std::string text;
    unsigned no = 0;
    uint64_t prev = 0;
    while (std::getline(f, text)) {
        no++;
        const size_t hash = text.find('#');
        if (hash != std::string::npos) text.erase(hash);
        std::istringstream in(text);
        Line l;
        l.no = no;
        std::string ms;
        if (!(in >> ms)) continue;   // blank or comment
        char* end = nullptr;
        l.ms = std::strtoull(ms.c_str(), &end, 10);
        if (!end || *end) {
            err = "line " + std::to_string(no) + ": '" + ms + "' is not a time in ms";
            return false;
        }
        if (l.ms < prev) {
            err = "line " + std::to_string(no) + ": time goes backwards";
            return false;
        }
        prev = l.ms;
        std::string word;
        while (in >> word) l.w.push_back(word);
        if (l.w.empty()) {
            err = "line " + std::to_string(no) + ": no verb";
            return false;
        }
        out.push_back(l);
    }
    return true;
}

bool ctlByName(const std::string& n, Ctl& c) {
    if (n == "help") c = C_HELP;
    else if (n == "tvmenu") c = C_TVMENU;
    else return false;
    return true;
}

}  // namespace

int App::runTvScript(const CliArgs& a) {
    std::vector<Line> lines;
    std::string err;
    if (!readScript(a.tvTestScript, lines, err)) {
        std::fprintf(stderr, "headless: FAIL tv script: %s\n", err.c_str());
        return 2;
    }
    audio.open(48043.6);
    applyAudioSettings();

    // ---- the virtual pad ----------------------------------------------------------------------------------------
    SDL_VirtualJoystickDesc desc;
    SDL_INIT_INTERFACE(&desc);
    desc.type = SDL_JOYSTICK_TYPE_GAMEPAD;
    desc.naxes = SDL_GAMEPAD_AXIS_COUNT;
    desc.nbuttons = SDL_GAMEPAD_BUTTON_COUNT;   // button i of the virtual stick is SDL_GamepadButton i, MISC1 included
    desc.name = "Virtual Jaguar test pad";
    const SDL_JoystickID padId = SDL_AttachVirtualJoystick(&desc);
    if (!padId) {
        std::fprintf(stderr, "headless: FAIL tv script: no virtual gamepad here: %s\n", SDL_GetError());
        return 1;
    }
    SDL_Joystick* joy = SDL_OpenJoystick(padId);

    auto drain = [this]() {
        SDL_Event ev;
        while (SDL_PollEvent(&ev)) handleEvent(ev, true);
    };
    drain();   // GAMEPAD_ADDED -> InputManager opens it
    if (!joy || input.gamepadCount() == 0 || input.padHandles().empty() ||
        !SDL_GamepadHasButton(input.padHandles().back(), SDL_GAMEPAD_BUTTON_MISC1)) {
        std::fprintf(stderr, "headless: FAIL tv script: the virtual gamepad did not open as a pad with a Share button\n");
        return 1;
    }

    // ---- loop pieces --------------------------------------------------------------------------------------------
    uint64_t lastFrame = SDL_GetTicks();
    unsigned stepped = 0;
    auto tick = [this]() { tvInputFrame(*this, SDL_GetTicks()); };
    auto stepGame = [&]() {   // a loaded, running game takes one frame per 16 ms
        const uint64_t t = SDL_GetTicks();
        if (t - lastFrame < 16) return;
        lastFrame = t;
        if (!host.isLoaded() || paused || debug.halted()) return;
        audio.update();
        host.runFrame();
        ach.afterFrame();
        ach.pump();
        if ((++stepped & 15) == 15) audio.clear();   // the dummy device drains in real time
        for (const OsdMessage& m : host.takeMessages()) std::fprintf(stderr, "[osd] %s\n", m.text.c_str());
    };
    auto service = [&]() {
        drain();
        tick();
        stepGame();
    };
    auto fail = [&](const Line& l, const std::string& what) {
        std::fprintf(stderr, "headless: FAIL tv script line %u: %s\n", l.no, what.c_str());
        return 1;
    };

    unsigned expects = 0;
    uint64_t prevExec = SDL_GetTicks(), prevMs = 0;
    for (const Line& l : lines) {
        const uint64_t due = prevExec + (l.ms - prevMs);
        while (SDL_GetTicks() < due) {
            service();
            SDL_Delay(1);
        }
        prevExec = SDL_GetTicks();
        prevMs = l.ms;

        const std::string& verb = l.w[0];
        const std::string arg1 = l.w.size() > 1 ? l.w[1] : std::string();
        const std::string arg2 = l.w.size() > 2 ? l.w[2] : std::string();
        if (verb == "load") {
            if (arg1.empty()) return fail(l, "load wants a path");
            paths.saves = paths.root + "saves" + std::to_string(headlessLoads_++) + "/";   // a private, empty save folder
            SDL_CreateDirectory(paths.saves.c_str());
            SDL_Event ev;
            SDL_zero(ev);
            ev.type = SDL_EVENT_DROP_FILE;
            ev.drop.data = arg1.c_str();
            SDL_PushEvent(&ev);
            drain();
            tick();
            if (!host.isLoaded() || host.contentPath() != absolutePath(arg1)) return fail(l, "could not load " + arg1);
        } else if (verb == "key") {
            const SDL_Scancode sc = SDL_GetScancodeFromName(arg2.c_str());
            if (sc == SDL_SCANCODE_UNKNOWN) return fail(l, "unknown key '" + arg2 + "'");
            if (arg1 != "down" && arg1 != "up" && arg1 != "tap") return fail(l, "key wants down|up|tap");
            for (int pass = 0; pass < 2; pass++) {
                const bool down = pass == 0;
                if ((down && arg1 == "up") || (!down && arg1 == "down")) continue;
                SDL_Event ev;
                SDL_zero(ev);
                ev.type = down ? SDL_EVENT_KEY_DOWN : SDL_EVENT_KEY_UP;
                ev.key.scancode = sc;
                ev.key.key = SDL_GetKeyFromScancode(sc, SDL_KMOD_NONE, false);
                ev.key.down = down;
                SDL_PushEvent(&ev);
                drain();
                tick();
            }
        } else if (verb == "pad") {
            const SDL_GamepadButton b = SDL_GetGamepadButtonFromString(arg2.c_str());
            if (b == SDL_GAMEPAD_BUTTON_INVALID) return fail(l, "unknown pad button '" + arg2 + "'");
            if (arg1 != "down" && arg1 != "up" && arg1 != "tap") return fail(l, "pad wants down|up|tap");
            for (int pass = 0; pass < 2; pass++) {
                const bool down = pass == 0;
                if ((down && arg1 == "up") || (!down && arg1 == "down")) continue;
                SDL_SetJoystickVirtualButton(joy, (int)b, down);
                SDL_UpdateJoysticks();
                drain();
                tick();
            }
        } else if (verb == "focus") {
            SDL_Event ev;
            SDL_zero(ev);
            ev.type = arg1 == "lost" ? SDL_EVENT_WINDOW_FOCUS_LOST : SDL_EVENT_WINDOW_FOCUS_GAINED;
            if (arg1 != "lost" && arg1 != "gained") return fail(l, "focus wants lost|gained");
            SDL_PushEvent(&ev);
            drain();
            tick();
        } else if (verb == "typing") {
            input.uiWantsKeyboard = arg1 == "on";   // headless has no ImGui to say a text box has focus
        } else if (verb == "capture") {
            Ctl c = C_HELP;
            if (!ctlByName(arg1, c)) return fail(l, "capture wants help|tvmenu");
            LayerId layer;
            layer.cls = DeviceClass::Keyboard;
            input.beginCapture(layer, c, false);
        } else if (verb == "expect") {
            tick();   // a hold that fired meanwhile toggles the menu now ...
            tick();   // ... and the housekeeping after it has run
            drain();
            const size_t eq = arg1.find('=');
            if (eq == std::string::npos) return fail(l, "expect wants name=value");
            const std::string name = arg1.substr(0, eq), want = arg1.substr(eq + 1);
            expects++;
            auto flag = [&](bool v) { return std::string(v ? "1" : "0"); };
            std::string got;
            if (name == "menu-open") got = flag(tvShell().menuOpen());
            else if (name == "paused") got = flag(paused);
            else if (name == "loaded") got = flag(host.isLoaded());
            else if (name == "capturing") got = flag(input.capturing());
            else if (name == "kb-bind") {   // CTL:KEY -- the keyboard profile binds KEY (plain press) to CTL
                const size_t colon = want.find(':');
                Ctl c = C_HELP;
                const SDL_Scancode sc = colon == std::string::npos ? SDL_SCANCODE_UNKNOWN : SDL_GetScancodeFromName(want.c_str() + colon + 1);
                if (colon == std::string::npos || !ctlByName(want.substr(0, colon), c) || sc == SDL_SCANCODE_UNKNOWN)
                    return fail(l, "kb-bind wants help|tvmenu:KEY");
                bool has = false;
                for (const Binding& b : input.keyboardProfile().binds[(size_t)c])
                    if (b.kind == Binding::Key && b.code == (int)sc && !b.hold) has = true;
                if (!has) return fail(l, "the keyboard profile does not bind " + want);
                continue;
            } else {
                return fail(l, "unknown expect '" + name + "'");
            }
            if (got != want) return fail(l, "expected " + name + "=" + want + ", got " + got);
        } else {
            return fail(l, "unknown verb '" + verb + "'");
        }
    }

    // Let the last input settle, and a loaded game run a little (--frames N = N frame times).
    const uint64_t end = SDL_GetTicks() + (uint64_t)a.frames * 16;
    while (SDL_GetTicks() < end) {
        service();
        SDL_Delay(1);
    }
    SDL_CloseJoystick(joy);
    SDL_DetachVirtualJoystick(padId);
    drain();
    std::printf("headless: OK (tv script: %zu line(s), %u expect(s))\n", lines.size(), expects);
    return 0;
}

}  // namespace vj
