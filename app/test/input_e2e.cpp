// End-to-end input test: drives the REAL core (statically linked, through the retro_*
// API via CoreHost) with the app's control -> core translation (CoreInput) and
// reads back the Jaguar's own joypad matrix (joypad0Buttons / joypad1Buttons, the
// bytes joystick.c serves to the 68000).  For every Jaguar control on both ports:
// exactly that matrix slot is set and no other.  This is what proves the keypad
// (incl. 7, 8, 9, * and #, which have no RetroPad button) really reaches the core.
//
// usage: vj_input_e2e <rom.j64>
#include "core_host.h"
#include "paths.h"
#include "remap.h"

#include "libretro.h"

#include <cstdio>
#include <ctime>
#include <filesystem>
#include <string>

extern "C" {
// src/jerry/joystick.h: socket 0 = slots [0..20]; 4 sockets x 21 slots.
extern uint8_t joypad0Buttons[];
extern uint8_t joypad1Buttons[];
}

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

static const int kSlots = 4 * 21;

// Expected matrix slot per control, transcribed from the BUTTON_* enum in src/jerry/joystick.h
// (BUTTON_s=4 BUTTON_7=5 BUTTON_4=6 BUTTON_1=7 BUTTON_0=8 BUTTON_8=9 BUTTON_5=10 BUTTON_2=11
//  BUTTON_d=12 BUTTON_9=13 BUTTON_6=14 BUTTON_3=15; A=16 B=17 C=18 OPTION=19 PAUSE=20).
static int slotFor(vj::Ctl c) {
    switch (c) {
    case vj::C_UP: return 0;
    case vj::C_DOWN: return 1;
    case vj::C_LEFT: return 2;
    case vj::C_RIGHT: return 3;
    case vj::C_A: return 16;
    case vj::C_B: return 17;
    case vj::C_C: return 18;
    case vj::C_OPTION: return 19;
    case vj::C_PAUSE: return 20;
    case vj::C_K0: return 8;
    case vj::C_K1: return 7;
    case vj::C_K2: return 11;
    case vj::C_K3: return 15;
    case vj::C_K4: return 6;
    case vj::C_K5: return 10;
    case vj::C_K6: return 14;
    case vj::C_K7: return 5;
    case vj::C_K8: return 9;
    case vj::C_K9: return 13;
    case vj::C_STAR: return 4;
    case vj::C_HASH: return 12;
    default: return -1;
    }
}

static CoreInput g_in;

// How many matrix bytes are set on a port, and the (first) slot.
static int pressed(unsigned port, int* firstSlot) {
    const uint8_t* m = port == 0 ? joypad0Buttons : joypad1Buttons;
    int n = 0;
    *firstSlot = -1;
    for (int i = 0; i < kSlots; i++) {
        if (m[i]) {
            if (*firstSlot < 0) *firstSlot = i;
            n++;
        }
    }
    return n;
}

static void runWith(CoreHost& host, unsigned port, ControlSet c) {
    g_in.beginFrame();
    g_in.addControls(port, c);
    host.runFrame();
    host.runFrame();   // the held state must be stable across frames, not a one-frame artefact
}

int main(int argc, char** argv) {
    if (argc < 2) {
        std::fprintf(stderr, "usage: vj_input_e2e <rom.j64>\n");
        return 2;
    }
    namespace fs = std::filesystem;
    std::error_code ec;
    fs::path tmp = fs::temp_directory_path(ec) / ("vj-input-e2e-" + std::to_string((long long)std::time(nullptr)));
    fs::create_directories(tmp / "saves", ec);
    fs::create_directories(tmp / "system", ec);
    std::string sys = (tmp / "system").string() + "/", sav = (tmp / "saves").string() + "/";

    CoreHost& host = CoreHost::instance();
    host.setDirectories(sys, sav);
    host.inputPoll = []() {};
    host.inputState = [](unsigned p, unsigned d, unsigned i, unsigned id) { return g_in.state(p, d, i, id); };

    std::string err;

    // ---- the keyboard-presence announcement is what unlocks the keypad ------------------------
    host.announceKeyboard = false;
    if (!host.loadContent(argv[1], err)) {
        std::fprintf(stderr, "load failed: %s\n", err.c_str());
        return 2;
    }
    {
        int slot = -1;
        runWith(host, 0, ControlSet());
        CHECK(pressed(0, &slot) == 0);
        ControlSet a;
        a.set(C_A);
        runWith(host, 0, a);
        CHECK(pressed(0, &slot) == 1 && slot == slotFor(C_A));   // the RetroPad needs no announcement
        ControlSet k;
        k.set(C_K7);
        runWith(host, 0, k);
        CHECK(pressed(0, &slot) == 0);                           // keypad 7: no keyboard seen yet -> nothing
        host.sendKeyEvent(false, 0, 0, 0);                       // ...the announcement finishLoad() makes
        runWith(host, 0, k);
        CHECK(pressed(0, &slot) == 1 && slot == slotFor(C_K7));
    }

    // ---- every control, both ports, with the announcement as the app makes it -------------------
    host.announceKeyboard = true;
    if (!host.loadContent(argv[1], err)) {
        std::fprintf(stderr, "reload failed: %s\n", err.c_str());
        return 2;
    }
    for (unsigned port = 0; port < 2; port++) {
        for (int i = 0; i < kJaguarControls; i++) {
            Ctl c = (Ctl)i;
            ControlSet cs;
            cs.set(c);
            runWith(host, port, cs);
            int slot = -1, n = pressed(port, &slot), other = -1;
            CHECK(n == 1);
            CHECK(slot == slotFor(c));
            CHECK(pressed(1 - port, &other) == 0);
            if (n != 1 || slot != slotFor(c))
                std::fprintf(stderr, "  port %u control %s: %d slot(s) set, first=%d, want %d\n", port + 1, ctlId(c), n, slot,
                             slotFor(c));
        }
    }

    // Both players' keypads at once, and a chord of pad + keypad on one port.
    {
        g_in.beginFrame();
        ControlSet a;
        a.set(C_K7);
        a.set(C_B);
        a.set(C_HASH);
        ControlSet b;
        b.set(C_STAR);
        b.set(C_K9);
        g_in.addControls(0, a);
        g_in.addControls(1, b);
        host.runFrame();
        host.runFrame();
        CHECK(joypad0Buttons[slotFor(C_K7)] && joypad0Buttons[slotFor(C_B)] && joypad0Buttons[slotFor(C_HASH)]);
        CHECK(joypad1Buttons[slotFor(C_STAR)] && joypad1Buttons[slotFor(C_K9)]);
        int s;
        CHECK(pressed(0, &s) == 3);
        CHECK(pressed(1, &s) == 2);
    }

    // Releasing everything releases everything.
    {
        runWith(host, 0, ControlSet());
        int s;
        CHECK(pressed(0, &s) == 0 && pressed(1, &s) == 0);
    }

    host.unloadContent();
    fs::remove_all(tmp, ec);   // our own temp dir only
    std::printf("input_e2e: %d checks, %d failed\n", g_checks, g_fail);
    return g_fail ? 1 : 0;
}
