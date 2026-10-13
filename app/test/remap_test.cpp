// Headless unit tests for the controller remapping layer (src/remap.*): binding
// serialisation, device snapshot -> Jaguar controls, profile / per-game override
// precedence, persistence round trip, Jaguar controls -> what the core reads
// (including the 12-key keypad), the keypad overlay, bind capture and content
// identity.  No SDL_Init, no window, no core.
#include "remap.h"

#include "libretro.h"

#include <chrono>
#include <cstdio>
#include <cstdlib>
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

static ControlSet only(Ctl c) {
    ControlSet s;
    s.set(c);
    return s;
}

static DeviceSnapshot keyDown(int sc) {
    DeviceSnapshot s;
    s.key[(size_t)sc] = true;
    return s;
}

// ---------------------------------------------------------------------------------
static void testBindingSerialisation() {
    Binding all[] = {Binding::key(SDL_SCANCODE_Z), Binding::key(SDL_SCANCODE_KP_7), Binding::key(SDL_SCANCODE_F2),
                     Binding::button(SDL_GAMEPAD_BUTTON_LEFT_SHOULDER), Binding::button(SDL_GAMEPAD_BUTTON_GUIDE),
                     Binding::axis(SDL_GAMEPAD_AXIS_RIGHTY, -1), Binding::axis(SDL_GAMEPAD_AXIS_LEFT_TRIGGER, 1)};
    for (const Binding& b : all) {
        Binding back;
        std::string s = b.serialize();
        CHECK(!s.empty());
        CHECK(Binding::parse(s, back));
        CHECK(back == b);
        CHECK(!b.display().empty());
    }
    std::vector<Binding> v(all, all + 7);
    bool ok = false;
    std::vector<Binding> back = parseBindings(serializeBindings(v), &ok);
    CHECK(ok);
    CHECK(back == v);

    CHECK(serializeBindings({}) == "none");
    CHECK(parseBindings("none", &ok).empty() && ok);

    // Garbage tokens are dropped, the rest survives, and the caller is told.
    back = parseBindings("k29,bnotabutton,a*x,k,k99999,k44", &ok);
    CHECK(!ok);
    CHECK(back.size() == 2);
    CHECK(back[0] == Binding::key(29) && back[1] == Binding::key(44));
    // Duplicates collapse.
    CHECK(parseBindings("k29,k29", &ok).size() == 1);

    Ctl c;
    CHECK(ctlFromId("k7", c) && c == C_K7);
    CHECK(ctlFromId("star", c) && c == C_STAR);
    CHECK(!ctlFromId("k10", c));
    for (int i = 0; i < C_COUNT; i++) {
        CHECK(ctlFromId(ctlId((Ctl)i), c) && c == (Ctl)i);
    }
}

// ---------------------------------------------------------------------------------
static void testKeyboardDefaults() {
    Profile kb = Profile::builtin(DeviceClass::Keyboard);
    kb.stickAsDpad = false;
    CHECK(evaluate(kb, keyDown(SDL_SCANCODE_Z)) == only(C_B));
    CHECK(evaluate(kb, keyDown(SDL_SCANCODE_X)) == only(C_A));
    CHECK(evaluate(kb, keyDown(SDL_SCANCODE_C)) == only(C_C));
    CHECK(evaluate(kb, keyDown(SDL_SCANCODE_RETURN)) == only(C_OPTION));
    CHECK(evaluate(kb, keyDown(SDL_SCANCODE_RSHIFT)) == only(C_PAUSE));
    CHECK(evaluate(kb, keyDown(SDL_SCANCODE_BACKSPACE)) == only(C_PAUSE));
    CHECK(evaluate(kb, keyDown(SDL_SCANCODE_UP)) == only(C_UP));
    CHECK(evaluate(kb, keyDown(SDL_SCANCODE_F2)) == only(C_OVERLAY));
    // Number row and numpad both reach the whole keypad.
    for (int d = 0; d <= 9; d++) {
        int row = d == 0 ? SDL_SCANCODE_0 : SDL_SCANCODE_1 + (d - 1);
        int pad = d == 0 ? SDL_SCANCODE_KP_0 : SDL_SCANCODE_KP_1 + (d - 1);
        CHECK(evaluate(kb, keyDown(row)) == only((Ctl)(C_K0 + d)));
        CHECK(evaluate(kb, keyDown(pad)) == only((Ctl)(C_K0 + d)));
    }
    CHECK(evaluate(kb, keyDown(SDL_SCANCODE_MINUS)) == only(C_STAR));
    CHECK(evaluate(kb, keyDown(SDL_SCANCODE_EQUALS)) == only(C_HASH));
    CHECK(evaluate(kb, keyDown(SDL_SCANCODE_KP_DIVIDE)) == only(C_STAR));
    CHECK(evaluate(kb, keyDown(SDL_SCANCODE_KP_MULTIPLY)) == only(C_HASH));
    // Chords are independent.
    DeviceSnapshot s = keyDown(SDL_SCANCODE_Z);
    s.key[SDL_SCANCODE_UP] = true;
    s.key[SDL_SCANCODE_5] = true;
    ControlSet cs = evaluate(kb, s);
    CHECK(cs.test(C_B) && cs.test(C_UP) && cs.test(C_K5) && !cs.test(C_A));
}

static void testGamepadDefaults() {
    Profile pad = Profile::builtin(DeviceClass::Gamepad);
    pad.stickAsDpad = true;

    // Every keypad key is reachable on a standard gamepad, each by its own input and
    // without the overlay: press each default binding alone and expect exactly that key.
    for (int c = C_K0; c <= C_HASH; c++) {
        CHECK(!pad.binds[(size_t)c].empty());
        for (const Binding& b : pad.binds[(size_t)c]) {
            DeviceSnapshot s;
            if (b.kind == Binding::PadButton) s.btn[(size_t)b.code] = true;
            else if (b.kind == Binding::PadAxis) s.axis[(size_t)b.code] = (int16_t)(b.dir * 30000);
            else CHECK(false);
            CHECK(evaluate(pad, s) == only((Ctl)c));
        }
    }

    // Face buttons / meta buttons.
    DeviceSnapshot s;
    s.btn[SDL_GAMEPAD_BUTTON_SOUTH] = true;
    CHECK(evaluate(pad, s) == only(C_B));
    s = DeviceSnapshot();
    s.btn[SDL_GAMEPAD_BUTTON_EAST] = true;
    CHECK(evaluate(pad, s) == only(C_A));
    s = DeviceSnapshot();
    s.btn[SDL_GAMEPAD_BUTTON_WEST] = true;
    CHECK(evaluate(pad, s) == only(C_C));
    s = DeviceSnapshot();
    s.btn[SDL_GAMEPAD_BUTTON_START] = true;
    CHECK(evaluate(pad, s) == only(C_OPTION));
    s = DeviceSnapshot();
    s.btn[SDL_GAMEPAD_BUTTON_BACK] = true;
    CHECK(evaluate(pad, s) == only(C_PAUSE));
    s = DeviceSnapshot();
    s.btn[SDL_GAMEPAD_BUTTON_MISC1] = true;
    CHECK(evaluate(pad, s) == only(C_OVERLAY));

    // The right stick resolves to ONE keypad key: the dominant axis.
    s = DeviceSnapshot();
    s.axis[SDL_GAMEPAD_AXIS_RIGHTX] = -20000;
    s.axis[SDL_GAMEPAD_AXIS_RIGHTY] = -30000;
    CHECK(evaluate(pad, s) == only(C_K8));
    s.axis[SDL_GAMEPAD_AXIS_RIGHTX] = 30000;
    s.axis[SDL_GAMEPAD_AXIS_RIGHTY] = -20000;
    CHECK(evaluate(pad, s) == only(C_K9));
    // Below the threshold nothing happens.
    s = DeviceSnapshot();
    s.axis[SDL_GAMEPAD_AXIS_RIGHTX] = 15000;
    CHECK(!evaluate(pad, s).any());
    // Triggers have their own, lower threshold.
    s = DeviceSnapshot();
    s.axis[SDL_GAMEPAD_AXIS_LEFT_TRIGGER] = 9000;
    CHECK(evaluate(pad, s) == only(C_K3));
    s.axis[SDL_GAMEPAD_AXIS_LEFT_TRIGGER] = 7000;
    CHECK(!evaluate(pad, s).any());

    // The left stick drives the d-pad (diagonals allowed) unless switched off.
    s = DeviceSnapshot();
    s.axis[SDL_GAMEPAD_AXIS_LEFTX] = -30000;
    s.axis[SDL_GAMEPAD_AXIS_LEFTY] = 30000;
    ControlSet cs = evaluate(pad, s);
    CHECK(cs.test(C_LEFT) && cs.test(C_DOWN) && !cs.test(C_UP) && !cs.test(C_RIGHT));
    pad.stickAsDpad = false;
    CHECK(!evaluate(pad, s).any());
}

// ---------------------------------------------------------------------------------
static std::vector<Binding> one(const Binding& b) { return std::vector<Binding>(1, b); }

static void testPrecedence() {
    Config cfg;   // in memory only
    BindingStore st(cfg);
    const std::string G = "crc32-deadbeef", OTHER = "crc32-00000001";
    const std::string PAD = "030000005e040000ea02000000007801", PAD2 = "0300000000000000000000000000dead";
    const Binding kA = Binding::key(SDL_SCANCODE_A), kB = Binding::key(SDL_SCANCODE_B),
                  kC = Binding::key(SDL_SCANCODE_C), kD = Binding::key(SDL_SCANCODE_D);

    // Nothing stored: the built-in defaults, tagged as such.
    Profile p = st.resolve(DeviceClass::Keyboard, "", "");
    CHECK(p.source[C_A] == SRC_DEFAULT && p.binds[C_A] == Profile::builtin(DeviceClass::Keyboard).binds[C_A]);

    // global / class
    st.set(LayerId{"", DeviceClass::Gamepad, ""}, C_A, one(kA));
    p = st.resolve(DeviceClass::Gamepad, PAD, "");
    CHECK(p.binds[C_A] == one(kA) && p.source[C_A] == SRC_GLOBAL);
    CHECK(p.source[C_B] == SRC_DEFAULT);                  // untouched controls fall through
    // The keyboard is a separate class.
    CHECK(st.resolve(DeviceClass::Keyboard, "", "").source[C_A] == SRC_DEFAULT);

    // global / this controller beats global / class, for that controller only
    st.set(LayerId{"", DeviceClass::Gamepad, PAD}, C_A, one(kB));
    CHECK(st.resolve(DeviceClass::Gamepad, PAD, "").binds[C_A] == one(kB));
    CHECK(st.resolve(DeviceClass::Gamepad, PAD, "").source[C_A] == SRC_GLOBAL_PAD);
    CHECK(st.resolve(DeviceClass::Gamepad, PAD2, "").binds[C_A] == one(kA));

    // this game / class beats both globals, for that game only
    st.set(LayerId{G, DeviceClass::Gamepad, ""}, C_A, one(kC));
    CHECK(st.resolve(DeviceClass::Gamepad, PAD, G).binds[C_A] == one(kC));
    CHECK(st.resolve(DeviceClass::Gamepad, PAD, G).source[C_A] == SRC_GAME);
    CHECK(st.resolve(DeviceClass::Gamepad, PAD, OTHER).binds[C_A] == one(kB));
    CHECK(st.resolve(DeviceClass::Gamepad, PAD, "").binds[C_A] == one(kB));

    // this game / this controller beats everything
    st.set(LayerId{G, DeviceClass::Gamepad, PAD}, C_A, one(kD));
    CHECK(st.resolve(DeviceClass::Gamepad, PAD, G).binds[C_A] == one(kD));
    CHECK(st.resolve(DeviceClass::Gamepad, PAD, G).source[C_A] == SRC_GAME_PAD);
    CHECK(st.resolve(DeviceClass::Gamepad, PAD2, G).binds[C_A] == one(kC));   // other model: game / class

    // Clearing a layer's entry falls back to the next layer down.
    st.clear(LayerId{G, DeviceClass::Gamepad, PAD}, C_A);
    CHECK(st.resolve(DeviceClass::Gamepad, PAD, G).binds[C_A] == one(kC));
    st.clear(LayerId{G, DeviceClass::Gamepad, ""}, C_A);
    CHECK(st.resolve(DeviceClass::Gamepad, PAD, G).binds[C_A] == one(kB));
    st.clear(LayerId{"", DeviceClass::Gamepad, PAD}, C_A);
    CHECK(st.resolve(DeviceClass::Gamepad, PAD, G).binds[C_A] == one(kA));
    st.clear(LayerId{"", DeviceClass::Gamepad, ""}, C_A);
    CHECK(st.resolve(DeviceClass::Gamepad, PAD, G).source[C_A] == SRC_DEFAULT);

    // An explicit "unbound" in a higher layer shadows a binding below it (it is not "absent").
    st.set(LayerId{"", DeviceClass::Keyboard, ""}, C_UP, one(kA));
    st.set(LayerId{G, DeviceClass::Keyboard, ""}, C_UP, {});
    CHECK(st.defined(LayerId{G, DeviceClass::Keyboard, ""}, C_UP));
    p = st.resolve(DeviceClass::Keyboard, "", G);
    CHECK(p.binds[C_UP].empty() && p.source[C_UP] == SRC_GAME);
    CHECK(st.resolve(DeviceClass::Keyboard, "", OTHER).binds[C_UP] == one(kA));

    // The keyboard ignores controller layers; the keyboard port defaults to 0.
    st.set(LayerId{"", DeviceClass::Gamepad, PAD}, C_B, one(kD));
    CHECK(st.resolve(DeviceClass::Keyboard, PAD, "").source[C_B] == SRC_DEFAULT);
    CHECK(st.keyboardPort() == 0);
    st.setKeyboardPort(1);
    CHECK(st.keyboardPort() == 1);
    st.setKeyboardPort(7);
    CHECK(st.keyboardPort() == 0);

    // clearLayer drops every control of one layer and no other.
    st.set(LayerId{G, DeviceClass::Gamepad, ""}, C_K7, one(kA));
    st.set(LayerId{G, DeviceClass::Gamepad, ""}, C_K8, one(kB));
    st.clearLayer(LayerId{G, DeviceClass::Gamepad, ""});
    CHECK(!st.defined(LayerId{G, DeviceClass::Gamepad, ""}, C_K7));
    CHECK(!st.defined(LayerId{G, DeviceClass::Gamepad, ""}, C_K8));
    CHECK(st.defined(LayerId{"", DeviceClass::Gamepad, PAD}, C_B));
}

static void testPersistence() {
    namespace fs = std::filesystem;
    std::error_code ec;
    fs::path dir = fs::temp_directory_path(ec) / ("vj-remap-test-" + std::to_string((long long)std::chrono::steady_clock::now().time_since_epoch().count()));
    fs::create_directories(dir, ec);
    std::string path = (dir / "config.ini").string();

    const std::string G = "crc32-1234abcd", PAD = "03000000c82d00000160000000000000";
    std::vector<Binding> multi = {Binding::button(SDL_GAMEPAD_BUTTON_LEFT_SHOULDER),
                                  Binding::axis(SDL_GAMEPAD_AXIS_RIGHT_TRIGGER, 1), Binding::key(SDL_SCANCODE_F9)};
    Profile before;
    {
        Config cfg;
        cfg.setPath(path);
        BindingStore st(cfg);
        st.set(LayerId{"", DeviceClass::Gamepad, ""}, C_K1, multi);
        st.set(LayerId{G, DeviceClass::Gamepad, PAD}, C_HASH, one(Binding::button(SDL_GAMEPAD_BUTTON_TOUCHPAD)));
        st.set(LayerId{G, DeviceClass::Keyboard, ""}, C_PAUSE, {});
        st.setKeyboardPort(1);
        CHECK(cfg.save());
        before = st.resolve(DeviceClass::Gamepad, PAD, G);
    }
    {
        Config cfg;
        cfg.load(path);
        BindingStore st(cfg);
        Profile after = st.resolve(DeviceClass::Gamepad, PAD, G);
        for (int i = 0; i < C_COUNT; i++) {
            CHECK(after.binds[(size_t)i] == before.binds[(size_t)i]);
            CHECK(after.source[(size_t)i] == before.source[(size_t)i]);
        }
        CHECK(after.binds[C_K1] == multi);
        CHECK(after.source[C_HASH] == SRC_GAME_PAD);
        CHECK(st.resolve(DeviceClass::Keyboard, "", G).binds[C_PAUSE].empty());
        CHECK(st.resolve(DeviceClass::Keyboard, "", "").binds[C_PAUSE].size() == 2);   // other games keep the default
        CHECK(st.keyboardPort() == 1);
        // A hand-edited / corrupt value falls through to the layer below instead of unbinding.
        cfg.set("input.pad.k2", "bnonsense,k");
        CHECK(!st.defined(LayerId{"", DeviceClass::Gamepad, ""}, C_K2));
        CHECK(st.resolve(DeviceClass::Gamepad, PAD, "").source[C_K2] == SRC_DEFAULT);
    }
    fs::remove_all(dir, ec);   // our own temp dir only
}

// ---------------------------------------------------------------------------------
// controls -> what the core reads.  The expected tables below are transcribed from
// libretro.c (update_input default path), NOT derived from remap.cpp.
// ---------------------------------------------------------------------------------
static void testCoreInput() {
    // Pad controls ride the RetroPad bitmask (libretro.c: ret[0] & (1 << RETRO_DEVICE_ID_JOYPAD_x)).
    struct { Ctl c; unsigned id; } padMap[] = {
        {C_UP, RETRO_DEVICE_ID_JOYPAD_UP}, {C_DOWN, RETRO_DEVICE_ID_JOYPAD_DOWN},
        {C_LEFT, RETRO_DEVICE_ID_JOYPAD_LEFT}, {C_RIGHT, RETRO_DEVICE_ID_JOYPAD_RIGHT},
        {C_A, RETRO_DEVICE_ID_JOYPAD_A}, {C_B, RETRO_DEVICE_ID_JOYPAD_B}, {C_C, RETRO_DEVICE_ID_JOYPAD_Y},
        {C_OPTION, RETRO_DEVICE_ID_JOYPAD_START}, {C_PAUSE, RETRO_DEVICE_ID_JOYPAD_SELECT}};
    for (unsigned port = 0; port < 2; port++) {
        for (const auto& m : padMap) {
            CoreInput ci;
            ci.beginFrame();
            ci.addControls(port, only(m.c));
            CHECK(ci.state(port, RETRO_DEVICE_JOYPAD, 0, m.id) == 1);
            CHECK((ci.state(port, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_MASK) & 0xFFFF) == (1 << m.id));
            CHECK(ci.state(1 - port, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_MASK) == 0);
            CHECK(!ci.keypadSynthesised());
        }
    }

    // Keypad controls become keyboard keys on frontend port 0: port 1 = 0-9 - =, port 2 = p q w e r t y u i o [ ].
    const unsigned p1[12] = {RETROK_0, RETROK_1, RETROK_2, RETROK_3, RETROK_4, RETROK_5,
                             RETROK_6, RETROK_7, RETROK_8, RETROK_9, RETROK_MINUS, RETROK_EQUALS};
    const unsigned p2[12] = {'p', 'q', 'w', 'e', 'r', 't', 'y', 'u', 'i', 'o', '[', ']'};
    for (unsigned port = 0; port < 2; port++) {
        for (int k = 0; k < 12; k++) {
            Ctl c = (Ctl)(C_K0 + k);
            unsigned want = port == 0 ? p1[k] : p2[k];
            CoreInput ci;
            ci.beginFrame();
            ci.addControls(port, only(c));
            CHECK(ci.keypadSynthesised());
            CHECK(ci.state(0, RETRO_DEVICE_KEYBOARD, 0, want) == 1);
            // Exactly one key down, and the RetroPad is untouched: pressing RetroPad X / L1 / R1 /
            // L2 / R2 / L3 / R3 as well would press the same Jaguar key a second time (or an alias
            // under the Pro Controller).
            int down = 0;
            for (unsigned rk = 0; rk < 512; rk++) down += ci.state(0, RETRO_DEVICE_KEYBOARD, 0, rk) ? 1 : 0;
            CHECK(down == 1);
            CHECK(ci.pad(0) == 0 && ci.pad(1) == 0);
            // The core only ever reads the keyboard on port 0.
            CHECK(ci.state(1, RETRO_DEVICE_KEYBOARD, 0, want) == 0);
            CHECK(CoreInput::keypadKey(port, c) == want);
        }
    }

    // Two players' keypads never share a key, so both can be used at once.
    CoreInput both;
    both.beginFrame();
    both.addControls(0, only(C_K7));
    both.addControls(1, only(C_K7));
    CHECK(both.state(0, RETRO_DEVICE_KEYBOARD, 0, RETROK_7) == 1);
    CHECK(both.state(0, RETRO_DEVICE_KEYBOARD, 0, 'u') == 1);

    // The truthful keyboard: other keys pass through (push-to-talk reads them) but the 24 keys the
    // core's default path reads as keypad are only ever answered from synthesis.
    CoreInput ci;
    ci.beginFrame();
    ci.addRawKey(RETROK_v);
    ci.addRawKey(RETROK_SPACE);
    ci.addRawKey(RETROK_5);          // physical '5' -- masked
    ci.addRawKey('p');               // physical 'p' -- masked
    ci.addRawKey(RETROK_KP5);        // numpad -- the core only reads it in alt-input mode
    CHECK(ci.state(0, RETRO_DEVICE_KEYBOARD, 0, RETROK_v) == 1);
    CHECK(ci.state(0, RETRO_DEVICE_KEYBOARD, 0, RETROK_SPACE) == 1);
    CHECK(ci.state(0, RETRO_DEVICE_KEYBOARD, 0, RETROK_KP5) == 1);
    CHECK(ci.state(0, RETRO_DEVICE_KEYBOARD, 0, RETROK_5) == 0);
    CHECK(ci.state(0, RETRO_DEVICE_KEYBOARD, 0, 'p') == 0);
    for (unsigned port = 0; port < 2; port++)
        for (int k = 0; k < 12; k++) CHECK(CoreInput::isCoreKeypadKey(CoreInput::keypadKey(port, (Ctl)(C_K0 + k))));
    CHECK(!CoreInput::isCoreKeypadKey(RETROK_v));

    // Rebinding the physical '1' key to Jaguar A: the keyboard profile no longer says K1, so
    // pressing '1' must not press keypad 1 -- not even through the truthful key state.
    {
        Config cfg;
        BindingStore st(cfg);
        st.set(LayerId{"", DeviceClass::Keyboard, ""}, C_A, one(Binding::key(SDL_SCANCODE_1)));
        st.set(LayerId{"", DeviceClass::Keyboard, ""}, C_K1, {});
        Profile kb = st.resolve(DeviceClass::Keyboard, "", "");
        kb.stickAsDpad = false;
        ControlSet cs = evaluate(kb, keyDown(SDL_SCANCODE_1));
        CHECK(cs == only(C_A));
        CoreInput r;
        r.beginFrame();
        r.addRawKey(RETROK_1);
        r.addControls(0, cs);
        CHECK(r.state(0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_A) == 1);
        CHECK(r.state(0, RETRO_DEVICE_KEYBOARD, 0, RETROK_1) == 0);
    }

    // Raw RetroPad ports (analog / 6D / driving / Team Tap) see the physical pad unchanged.
    CoreInput raw;
    raw.beginFrame();
    raw.addRawPad(0, (uint16_t)((1u << RETRO_DEVICE_ID_JOYPAD_L3) | (1u << RETRO_DEVICE_ID_JOYPAD_X)));
    CHECK(raw.state(0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_L3) == 1);
    CHECK(raw.state(0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_X) == 1);
    CHECK(!raw.keypadSynthesised());
    // beginFrame clears everything.
    raw.beginFrame();
    CHECK(raw.pad(0) == 0);
}

// ---------------------------------------------------------------------------------
static void testOverlay() {
    CHECK(KeypadOverlay::keyAt(0, 0) == C_K1 && KeypadOverlay::keyAt(0, 2) == C_K3);
    CHECK(KeypadOverlay::keyAt(2, 0) == C_K7 && KeypadOverlay::keyAt(2, 2) == C_K9);
    CHECK(KeypadOverlay::keyAt(3, 0) == C_STAR && KeypadOverlay::keyAt(3, 1) == C_K0 && KeypadOverlay::keyAt(3, 2) == C_HASH);
    CHECK(KeypadOverlay::keyAt(-1, -1) == C_HASH);   // wraps

    KeypadOverlay ov;
    ControlSet none;
    // Closed: input passes through, minus the overlay control itself.
    ControlSet pass = ov.filter(0, only(C_B));
    CHECK(pass == only(C_B));

    // The overlay control opens it; the game then sees nothing from any device.
    ControlSet in = only(C_OVERLAY);
    in.set(C_B);
    CHECK(ov.filter(0, in) == none);
    CHECK(ov.open);
    CHECK(ov.filter(0, none) == none);
    CHECK(ov.filter(1, only(C_UP)) == none);       // another pad: nav + swallowed
    CHECK(ov.row == 3);                             // wrapped up from row 0

    // Navigate to 5 (row 1, col 1) and press it with A.
    ov.row = 0;
    ov.col = 0;
    ov.filter(0, none);
    ov.filter(0, only(C_DOWN));
    ov.filter(0, none);
    ov.filter(0, only(C_RIGHT));
    ov.filter(0, none);
    CHECK(ov.row == 1 && ov.col == 1);
    ov.filter(0, only(C_A));
    ov.filter(0, none);
    // The press is held for kHoldFrames, released, then the gap passes.
    int heldFrames = 0;
    for (int f = 0; f < 30; f++) {
        ov.tick();
        if (ov.held().test(C_K5)) heldFrames++;
        CHECK(ov.held().bits == 0 || ov.held() == only(C_K5));
    }
    CHECK(heldFrames == KeypadOverlay::kHoldFrames);

    // Two taps queue and don't merge: a gap of released frames separates them.
    ov.tap(C_K1);
    ov.tap(C_K1);
    int transitions = 0;
    bool last = false;
    for (int f = 0; f < 40; f++) {
        ov.tick();
        bool now = ov.held().test(C_K1);
        if (now && !last) transitions++;
        last = now;
    }
    CHECK(transitions == 2);
    ov.tap(C_UP);   // not a keypad key: ignored
    for (int f = 0; f < 20; f++) {
        ov.tick();
        CHECK(!ov.held().any());
    }

    // C closes it, and the C press that closed it is not leaked to the game.
    CHECK(ov.filter(0, only(C_C)) == none);
    CHECK(!ov.open);
    CHECK(ov.filter(0, only(C_C)) == none);          // still held: suppressed
    CHECK(ov.filter(0, none) == none);
    CHECK(ov.filter(0, only(C_C)) == only(C_C));     // released and pressed again: passes

    // The overlay toggles off with its own control, too.
    ov.filter(0, none);
    ov.filter(0, only(C_OVERLAY));
    CHECK(ov.open);
    ov.filter(0, none);
    ov.filter(0, only(C_OVERLAY));
    CHECK(!ov.open);
}

// ---------------------------------------------------------------------------------
static SDL_Event keyEvent(SDL_EventType t, SDL_Scancode sc, bool repeat = false) {
    SDL_Event e;
    SDL_zero(e);
    e.type = t;
    e.key.scancode = sc;
    e.key.repeat = repeat;
    return e;
}
static SDL_Event buttonEvent(SDL_GamepadButton b) {
    SDL_Event e;
    SDL_zero(e);
    e.type = SDL_EVENT_GAMEPAD_BUTTON_DOWN;
    e.gbutton.which = 7;
    e.gbutton.button = (Uint8)b;
    return e;
}
static SDL_Event axisEvent(SDL_JoystickID which, SDL_GamepadAxis a, int16_t v) {
    SDL_Event e;
    SDL_zero(e);
    e.type = SDL_EVENT_GAMEPAD_AXIS_MOTION;
    e.gaxis.which = which;
    e.gaxis.axis = (Uint8)a;
    e.gaxis.value = v;
    return e;
}

static void testCapture() {
    Binding out;
    BindCapture cap;
    CHECK(!cap.active());
    CHECK(cap.feed(keyEvent(SDL_EVENT_KEY_DOWN, SDL_SCANCODE_Q), out) == BindCapture::Pending);   // inactive

    cap.begin(DeviceClass::Keyboard);
    CHECK(cap.active());
    CHECK(cap.feed(keyEvent(SDL_EVENT_KEY_UP, SDL_SCANCODE_RETURN), out) == BindCapture::Pending);      // the key that clicked "Bind"
    CHECK(cap.feed(keyEvent(SDL_EVENT_KEY_DOWN, SDL_SCANCODE_Q, true), out) == BindCapture::Pending);   // auto-repeat
    CHECK(cap.feed(buttonEvent(SDL_GAMEPAD_BUTTON_SOUTH), out) == BindCapture::Pending);                // wrong device class
    CHECK(cap.active());
    CHECK(cap.feed(keyEvent(SDL_EVENT_KEY_DOWN, SDL_SCANCODE_Q), out) == BindCapture::Captured);
    CHECK(out == Binding::key(SDL_SCANCODE_Q));
    CHECK(!cap.active());

    cap.begin(DeviceClass::Keyboard);
    CHECK(cap.feed(keyEvent(SDL_EVENT_KEY_DOWN, SDL_SCANCODE_ESCAPE), out) == BindCapture::Cancelled);
    CHECK(!cap.active());

    cap.begin(DeviceClass::Gamepad);
    CHECK(cap.feed(keyEvent(SDL_EVENT_KEY_DOWN, SDL_SCANCODE_Q), out) == BindCapture::Pending);
    CHECK(cap.feed(buttonEvent(SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER), out) == BindCapture::Captured);
    CHECK(out == Binding::button(SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER));

    // Axes bind on movement relative to where they rested, and the push direction is recorded.
    cap.begin(DeviceClass::Gamepad);
    cap.setBaseline(7, SDL_GAMEPAD_AXIS_LEFT_TRIGGER, -32768);        // some pads rest negative
    CHECK(cap.feed(axisEvent(7, SDL_GAMEPAD_AXIS_LEFT_TRIGGER, -32000), out) == BindCapture::Pending);
    CHECK(cap.feed(axisEvent(7, SDL_GAMEPAD_AXIS_LEFTX, 5000), out) == BindCapture::Pending);          // jitter
    CHECK(cap.feed(axisEvent(7, SDL_GAMEPAD_AXIS_LEFT_TRIGGER, -10000), out) == BindCapture::Captured);
    CHECK(out == Binding::axis(SDL_GAMEPAD_AXIS_LEFT_TRIGGER, 1));
    cap.begin(DeviceClass::Gamepad);
    CHECK(cap.feed(axisEvent(7, SDL_GAMEPAD_AXIS_RIGHTY, -30000), out) == BindCapture::Captured);
    CHECK(out == Binding::axis(SDL_GAMEPAD_AXIS_RIGHTY, -1));
    // A stick already held at full deflection when capture began doesn't bind until it moves further.
    cap.begin(DeviceClass::Gamepad);
    cap.setBaseline(7, SDL_GAMEPAD_AXIS_RIGHTX, 32000);
    CHECK(cap.feed(axisEvent(7, SDL_GAMEPAD_AXIS_RIGHTX, 31000), out) == BindCapture::Pending);
    CHECK(cap.feed(axisEvent(7, SDL_GAMEPAD_AXIS_RIGHTX, 0), out) == BindCapture::Captured);
    CHECK(out == Binding::axis(SDL_GAMEPAD_AXIS_RIGHTX, -1));
}

// ---------------------------------------------------------------------------------
static void testIdentity() {
    const char* check = "123456789";
    CHECK(crc32Bytes((const uint8_t*)check, 9) == 0xCBF43926u);

    // A plain 1 MiB image is hashed whole.
    std::vector<uint8_t> rom(1048576);
    for (size_t i = 0; i < rom.size(); i++) rom[i] = (uint8_t)(i * 31u + 7u);
    std::string plain = cartIdentity(rom.data(), rom.size());
    char buf[32];
    std::snprintf(buf, sizeof buf, "crc32-%08x", (unsigned)crc32Bytes(rom.data(), rom.size()));
    CHECK(plain == buf);

    // A copier-headered image (N MiB + 512, universal marker $04040404 at payload+$400) hashes as the
    // payload, so it gets the same identity as the headerless dump -- the key titledb uses.
    rom[0x400] = rom[0x401] = rom[0x402] = rom[0x403] = 4;
    plain = cartIdentity(rom.data(), rom.size());
    std::vector<uint8_t> headered(512, 0xAA);
    headered.insert(headered.end(), rom.begin(), rom.end());
    CHECK(headered.size() == 1048576 + 512);
    CHECK(cartIdentity(headered.data(), headered.size()) == plain);
    // ...but the same bytes WITHOUT the marker are not stripped.
    headered[512 + 0x400] = 9;
    CHECK(cartIdentity(headered.data(), headered.size()) != plain);

    CHECK(cartIdentity(nullptr, 0).empty());

    // Discs are path-loaded: identified by file stem (no file access happens for these).
    CHECK(contentIdentity("/games/Myst (USA) [Disc 1].cue") == "disc-myst-usa-disc-1");
    CHECK(contentIdentity("C:\\x\\Battle_Sphere.CHD") == "disc-battle-sphere");
    CHECK(contentIdentity("/nonexistent/path/missing.j64").empty());
}

int main() {
    testBindingSerialisation();
    testKeyboardDefaults();
    testGamepadDefaults();
    testPrecedence();
    testPersistence();
    testCoreInput();
    testOverlay();
    testCapture();
    testIdentity();
    std::printf("remap_test: %d checks, %d failed\n", g_checks, g_fail);
    return g_fail ? 1 : 0;
}
