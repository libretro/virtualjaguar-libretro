// --tv-test-script FILE (dev flag): replays a script of key / pad / focus events against the real App and checks what
// the TV shell and the input layer did.  Headless (the CTest tv_menu_headless): no window, no ImGui, and a failed
// `expect` fails the run.  Windowed (runInteractive): the same lines run between frames -- `load` included -- so a
// screenshot (--screenshot) can be taken of the open menu; `expect` only prints a mismatch and the script goes on.  It is the test of the input
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
// open there, the run FAILS: nothing here fakes GAMEPAD_* events for an unopened id.  Windowed runs attach it too.
//
// Windowed-only verbs (for screenshots of the TV screens): `wait library` blocks the script until the Library has
// scanned (the LibraryService threads run only in a windowed run), `shot PATH` saves the window at the end of the
// next UI frame (App::tvScriptFrameEnd, called after the UI is drawn and before the swap), `quit` ends the run through
// the normal teardown.  Headless, each of them fails its line with "windowed only".
#include <SDL3/SDL.h>

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

#include "app.h"
#include "devices_ui.h"
#include "libretro.h"
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

struct App::TvScriptRun {
    std::vector<Line> lines;
    size_t next = 0;
    bool windowed = false;
    SDL_Joystick* joy = nullptr;
    SDL_JoystickID padId = 0;
    uint64_t prevExec = 0, prevMs = 0;
    unsigned expects = 0;
    // windowed-only verbs
    bool waitLibrary = false;    // `wait library` is blocking the script
    uint64_t waitStart = 0;
    std::string shotPath;        // `shot PATH`: saved at the end of the next UI frame
    unsigned shotLine = 0;
};

bool App::tvScriptBegin(const CliArgs& a, bool windowed) {
    auto run = std::make_shared<TvScriptRun>();
    run->windowed = windowed;
    std::string err;
    if (!readScript(a.tvTestScript, run->lines, err)) {
        std::fprintf(stderr, "%s: FAIL tv script: %s\n", windowed ? "tv" : "headless", err.c_str());
        return false;
    }
    // ---- the virtual pad ----------------------------------------------------------------------------------------
    SDL_VirtualJoystickDesc desc;
    SDL_INIT_INTERFACE(&desc);
    desc.type = SDL_JOYSTICK_TYPE_GAMEPAD;
    desc.naxes = SDL_GAMEPAD_AXIS_COUNT;
    desc.nbuttons = SDL_GAMEPAD_BUTTON_COUNT;   // button i of the virtual stick is SDL_GamepadButton i, MISC1 included
    desc.name = "Virtual Jaguar test pad";
    run->padId = SDL_AttachVirtualJoystick(&desc);
    if (!run->padId) {
        std::fprintf(stderr, "%s: FAIL tv script: no virtual gamepad here: %s\n", windowed ? "tv" : "headless", SDL_GetError());
        return false;
    }
    run->joy = SDL_OpenJoystick(run->padId);
    tvScript_ = run;
    tvScriptDrain();   // GAMEPAD_ADDED -> InputManager opens it
    {
        // Only the virtual pad plays: a real controller on the dev machine must not steer the menu.
        int n = 0, which = -1;
        SDL_JoystickID* ids = SDL_GetGamepads(&n);
        for (int i = 0; ids && i < n; i++)
            if (ids[i] == run->padId) which = i;
        SDL_free(ids);
        if (which >= 0) input.setPadFilter(which + 1);
        if (!run->joy || which < 0 || input.gamepadCount() != 1 ||
            !SDL_GamepadHasButton(input.padHandles()[0], SDL_GAMEPAD_BUTTON_MISC1)) {
            std::fprintf(stderr, "%s: FAIL tv script: the virtual gamepad did not open as a pad with a Share button\n",
                         windowed ? "tv" : "headless");
            tvScriptEnd();
            return false;
        }
    }
    run->prevExec = SDL_GetTicks();
    return true;
}

void App::tvScriptDrain() {
    SDL_Event ev;
    while (SDL_PollEvent(&ev)) handleEvent(ev, headless_);
}

void App::tvScriptEnd() {
    if (!tvScript_) return;
    SDL_CloseJoystick(tvScript_->joy);
    SDL_DetachVirtualJoystick(tvScript_->padId);
    tvScriptDrain();
    tvScript_.reset();
}

// Runs every line that is due now.  0 = more to come, 1 = the script is done, 2 = a headless check failed.
int App::tvScriptStep() {
    TvScriptRun& run = *tvScript_;
    const char* tag = run.windowed ? "tv" : "headless";
    auto tick = [this]() { tvInputFrame(*this, SDL_GetTicks()); };
    // A failure: headless ends the run; windowed prints it and goes on (a screenshot run has no use for an abort).
    auto fail = [&](const Line& l, const std::string& what) {
        std::fprintf(stderr, "%s: FAIL tv script line %u: %s\n", tag, l.no, what.c_str());
        return run.windowed ? 0 : 2;
    };
    while (run.next < run.lines.size()) {
        if (run.waitLibrary) {   // `wait library`: nothing runs until the scan is done (or 120 s have passed)
            const bool ready = !library.scanning() && !library.entries().empty();
            if (!ready && SDL_GetTicks() - run.waitStart < 120000) return 0;
            run.waitLibrary = false;
            run.prevExec = SDL_GetTicks();   // the gaps of the lines after it count from here
            if (!ready) fail(run.lines[run.next - 1], "wait library: the library did not finish scanning in 120 s");
        }
        const Line& l = run.lines[run.next];
        if (SDL_GetTicks() < run.prevExec + (l.ms - run.prevMs)) return 0;
        run.next++;
        run.prevExec = SDL_GetTicks();
        run.prevMs = l.ms;

        const std::string& verb = l.w[0];
        const std::string arg1 = l.w.size() > 1 ? l.w[1] : std::string();
        const std::string arg2 = l.w.size() > 2 ? l.w[2] : std::string();
        int rc = 0;
        if (verb == "load") {
            if (arg1.empty()) return fail(l, "load wants a path");
            paths.saves = paths.root + "saves" + std::to_string(headlessLoads_++) + "/";   // a private, empty save folder
            SDL_CreateDirectory(paths.saves.c_str());
            SDL_Event ev;
            SDL_zero(ev);
            ev.type = SDL_EVENT_DROP_FILE;
            ev.drop.data = arg1.c_str();
            SDL_PushEvent(&ev);
            tvScriptDrain();
            tick();
            if (!host.isLoaded() || host.contentPath() != absolutePath(arg1)) rc = fail(l, "could not load " + arg1);
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
                tvScriptDrain();
                tick();
            }
        } else if (verb == "pad") {
            const SDL_GamepadButton b = SDL_GetGamepadButtonFromString(arg2.c_str());
            if (b == SDL_GAMEPAD_BUTTON_INVALID) return fail(l, "unknown pad button '" + arg2 + "'");
            if (arg1 != "down" && arg1 != "up" && arg1 != "tap") return fail(l, "pad wants down|up|tap");
            for (int pass = 0; pass < 2; pass++) {
                const bool down = pass == 0;
                if ((down && arg1 == "up") || (!down && arg1 == "down")) continue;
                SDL_SetJoystickVirtualButton(run.joy, (int)b, down);
                SDL_UpdateJoysticks();
                tvScriptDrain();
                tick();
            }
        } else if (verb == "focus") {
            SDL_Event ev;
            SDL_zero(ev);
            ev.type = arg1 == "lost" ? SDL_EVENT_WINDOW_FOCUS_LOST : SDL_EVENT_WINDOW_FOCUS_GAINED;
            if (arg1 != "lost" && arg1 != "gained") return fail(l, "focus wants lost|gained");
            SDL_PushEvent(&ev);
            tvScriptDrain();
            tick();
        } else if (verb == "typing") {
            input.uiWantsKeyboard = arg1 == "on";   // headless has no ImGui to say a text box has focus (windowed: the UI frame resets it)
        } else if (verb == "tvsetting") {   // what Settings > Interface will do: store the setting, reconfigure TV mode
            if (!tv::settingFromKey(arg1, tvSetting)) return fail(l, "tvsetting wants off|manual|auto");
            configureTvMode();
        } else if (verb == "wait" || verb == "shot" || verb == "quit") {   // windowed only
            if (!run.windowed) return fail(l, verb + " is windowed only");
            if (verb == "wait") {
                if (arg1 != "library") return fail(l, "wait wants library");
                run.waitLibrary = true;
                run.waitStart = SDL_GetTicks();
            } else if (verb == "shot") {
                if (arg1.empty()) return fail(l, "shot wants a path");
                run.shotPath = arg1;
                run.shotLine = l.no;
            } else {
                quit = true;   // the loop finishes this frame (a pending shot is still taken), then the normal teardown
            }
        } else if (verb == "capture") {
            Ctl c = C_HELP;
            if (!ctlByName(arg1, c)) return fail(l, "capture wants help|tvmenu");
            LayerId layer;
            layer.cls = DeviceClass::Keyboard;
            input.beginCapture(layer, c, false);
        } else if (verb == "expect") {
            tick();   // a hold that fired meanwhile toggles the menu now ...
            tick();   // ... and the housekeeping after it has run
            tvScriptDrain();
            const size_t eq = arg1.find('=');
            if (eq == std::string::npos) return fail(l, "expect wants name=value");
            const std::string name = arg1.substr(0, eq), want = arg1.substr(eq + 1);
            run.expects++;
            auto flag = [&](bool v) { return std::string(v ? "1" : "0"); };
            std::string got;
            if (name == "menu-open") got = flag(tvShell().menuOpen());
            else if (name == "tv") got = flag(tvMode.active());
            else if (name == "tv-screen") got = tvShell().screenId();
            else if (name == "paused") got = flag(paused);
            else if (name == "loaded") got = flag(host.isLoaded());
            else if (name == "capturing") got = flag(input.capturing());
            else if (name == "core-input") {   // does the core read any Player 1 pad button / keypad key right now?
                input.poll(true);              // the paused menu runs no emulated frame, so take the snapshot by hand
                bool any = false;
                for (unsigned id = 0; id <= RETRO_DEVICE_ID_JOYPAD_R3; id++)
                    if (input.state(0, RETRO_DEVICE_JOYPAD, 0, id)) any = true;
                got = flag(any);
            } else if (name == "fast-forward") got = flag(fastForward);
            else if (name == "kb-bind") {   // CTL:KEY -- the keyboard profile binds KEY (plain press) to CTL
                const size_t colon = want.find(':');
                Ctl c = C_HELP;
                const SDL_Scancode sc = colon == std::string::npos ? SDL_SCANCODE_UNKNOWN : SDL_GetScancodeFromName(want.c_str() + colon + 1);
                if (colon == std::string::npos || !ctlByName(want.substr(0, colon), c) || sc == SDL_SCANCODE_UNKNOWN)
                    return fail(l, "kb-bind wants help|tvmenu:KEY");
                bool has = false;
                for (const Binding& b : input.keyboardProfile().binds[(size_t)c])
                    if (b.kind == Binding::Key && b.code == (int)sc && !b.hold) has = true;
                if (!has) rc = fail(l, "the keyboard profile does not bind " + want);
                if (rc) return rc;
                continue;
            } else {
                return fail(l, "unknown expect '" + name + "'");
            }
            if (got != want) rc = fail(l, "expected " + name + "=" + want + ", got " + got);
        } else {
            return fail(l, "unknown verb '" + verb + "'");
        }
        if (rc) return rc;
    }
    return 1;
}

// Windowed: called once per UI frame after the UI is drawn and before the buffers swap, so the picture has this
// frame's TV drawing.  Saves the window for a pending `shot`.
void App::tvScriptFrameEnd() {
    if (!tvScript_ || tvScript_->shotPath.empty()) return;
    const std::string path = tvScript_->shotPath;
    const unsigned line = tvScript_->shotLine;
    tvScript_->shotPath.clear();
    saveWindowScreenshot(path);
    SDL_PathInfo info;
    if (SDL_GetPathInfo(path.c_str(), &info)) std::fprintf(stderr, "tv script: shot %s\n", path.c_str());
    else std::fprintf(stderr, "tv: FAIL tv script line %u: shot wrote no file at %s\n", line, path.c_str());
}

int App::runTvScript(const CliArgs& a) {
    audio.open(48043.6);
    applyAudioSettings();
    if (!tvScriptBegin(a, false)) return 1;

    uint64_t lastFrame = SDL_GetTicks();
    unsigned stepped = 0;
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
        tvScriptDrain();
        tvInputFrame(*this, SDL_GetTicks());
        stepGame();
    };

    // The time of a line is a minimum gap to the previous one: service the app until it is due, then run it.
    int rc = 0;
    while (!(rc = tvScriptStep())) {
        service();
        SDL_Delay(1);
    }
    const unsigned expects = tvScript_->expects;
    const size_t nLines = tvScript_->lines.size();
    if (rc == 2) {
        tvScriptEnd();
        return 1;
    }

    // Let the last input settle, and a loaded game run a little (--frames N = N frame times).
    const uint64_t end = SDL_GetTicks() + (uint64_t)a.frames * 16;
    while (SDL_GetTicks() < end) {
        service();
        SDL_Delay(1);
    }
    tvScriptEnd();
    std::printf("headless: OK (tv script: %zu line(s), %u expect(s))\n", nLines, expects);
    return 0;
}

}  // namespace vj
