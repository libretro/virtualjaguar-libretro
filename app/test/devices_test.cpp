// Device layer, no core: kinds and ids, per-port and per-game persistence, player-slot assignment
// (Team Tap), light-gun geometry, mouse-capture policy, locked core options, the Router's
// translation (host input -> what libretro.c would be asked for), the Pro Controller keyboard
// layout, and the controls cheat sheet (content follows the bindings; auto-show rule).
#include "cheatsheet.h"
#include "devices.h"
#include "libretro.h"
#include "remap.h"
#include "router.h"

#include <cstdio>
#include <ctime>
#include <filesystem>

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

static DeviceSnapshot keys(std::initializer_list<int> sc) {
    DeviceSnapshot s;
    for (int k : sc) s.key[(size_t)k] = true;
    return s;
}

// ---------------------------------------------------------------------------------
static void testKinds() {
    // The ids libretro.c advertises: RETRO_DEVICE_SUBCLASS(base, n) = ((n + 1) << 8) | base.
    CHECK(devLibretroId(DevKind::TeamTap) == ((1u << 8) | RETRO_DEVICE_JOYPAD));
    CHECK(devLibretroId(DevKind::MouseST) == ((1u << 8) | RETRO_DEVICE_MOUSE));
    CHECK(devLibretroId(DevKind::Rotary) == ((4u << 8) | RETRO_DEVICE_MOUSE));
    CHECK(devLibretroId(DevKind::Analog) == ((1u << 8) | RETRO_DEVICE_ANALOG));
    CHECK(devLibretroId(DevKind::SixD) == ((4u << 8) | RETRO_DEVICE_ANALOG));
    CHECK(devLibretroId(DevKind::LightGun) == RETRO_DEVICE_LIGHTGUN);
    for (int i = 0; i < kDevKinds; i++) {
        DevKind k;
        CHECK(devKindFromLibretro(devLibretroId((DevKind)i), k) && k == (DevKind)i);
        CHECK(devKindFromOption(devKindId((DevKind)i)) == (DevKind)i || (DevKind)i == DevKind::Pad);
        CHECK(*devKindLabel((DevKind)i) && *devKindHelp((DevKind)i));
    }
    CHECK(devKindFromOption("auto") == DevKind::Pad && devKindFromOption("pad_pro") == DevKind::Pad);

    // The app's claim outranks the option (libretro.c: "an explicit frontend selection outranks the core option").
    CHECK(effectiveDevKind(-1, "rotary") == DevKind::Rotary);
    CHECK(effectiveDevKind((int)devLibretroId(DevKind::LightGun), "rotary") == DevKind::LightGun);
    CHECK(effectiveDevKind((int)RETRO_DEVICE_JOYPAD, "analog") == DevKind::Analog);   // JOYPAD releases the claim
    CHECK(effectiveDevKind((int)RETRO_DEVICE_MOUSE, "") == DevKind::MouseST);         // "a plain mouse means the ST wiring"

    // SET_CONTROLLER_INFO: a mouse is port 2 only, the gun port 1 only.
    CHECK(!portOffers(0, devLibretroId(DevKind::MouseST)) && portOffers(1, devLibretroId(DevKind::MouseST)));
    CHECK(portOffers(0, devLibretroId(DevKind::LightGun)) && !portOffers(1, devLibretroId(DevKind::LightGun)));
    CHECK(portOffers(0, devLibretroId(DevKind::Rotary)) && portOffers(1, devLibretroId(DevKind::Rotary)));
    CHECK(portOffers(0, devLibretroId(DevKind::TeamTap)) && portOffers(1, devLibretroId(DevKind::TeamTap)));
    CHECK(!portOffers(2, RETRO_DEVICE_JOYPAD));
}

static void testPortStore() {
    Config cfg;
    const std::string G = "crc32-deadbeef";
    CHECK(resolvePortDevice(cfg, 0, "") == -1 && resolvePortDevice(cfg, 0, G) == -1);
    cfg.setInt("port.1", 77);   // the key the first Settings tab wrote
    CHECK(resolvePortDevice(cfg, 1, "") == 77);
    setPortDevice(cfg, 1, "", (int)devLibretroId(DevKind::MouseST));
    CHECK(resolvePortDevice(cfg, 1, "") == (int)devLibretroId(DevKind::MouseST));
    CHECK(!cfg.has("port.1"));                      // migrated
    CHECK(resolvePortDevice(cfg, 1, G) == (int)devLibretroId(DevKind::MouseST));   // a game inherits the global choice
    setPortDevice(cfg, 1, G, (int)devLibretroId(DevKind::Rotary));
    CHECK(resolvePortDevice(cfg, 1, G) == (int)devLibretroId(DevKind::Rotary));
    CHECK(resolvePortDevice(cfg, 1, "crc32-00000000") == (int)devLibretroId(DevKind::MouseST));   // other games keep the global one
    CHECK(resolvePortDevice(cfg, 0, G) == -1);
    clearPortDevice(cfg, 1, G);
    CHECK(resolvePortDevice(cfg, 1, G) == (int)devLibretroId(DevKind::MouseST));

    // Round trip through the config file.
    namespace fs = std::filesystem;
    std::error_code ec;
    fs::path dir = fs::temp_directory_path(ec) / ("vj-devices-test-" + std::to_string((long long)std::time(nullptr)));
    fs::create_directories(dir, ec);
    std::string path = (dir / "c.ini").string();
    setPortDevice(cfg, 0, G, (int)devLibretroId(DevKind::LightGun));
    cfg.setPath(path);
    CHECK(cfg.save());
    Config back;
    back.load(path);
    CHECK(resolvePortDevice(back, 0, G) == (int)devLibretroId(DevKind::LightGun));
    CHECK(resolvePortDevice(back, 1, "") == (int)devLibretroId(DevKind::MouseST));
    fs::remove_all(dir, ec);   // our own temp dir only
}

static void testSlots() {
    SlotSource s;
    CHECK(parseSlot("auto", s) && s.kind == SlotSource::Auto);
    CHECK(parseSlot("none", s) && s.kind == SlotSource::None);
    CHECK(parseSlot("kb", s) && s.kind == SlotSource::Keyboard);
    CHECK(parseSlot("pad:0300aabb#1", s) && s.kind == SlotSource::Pad && s.guid == "0300aabb" && s.instance == 1);
    CHECK(!parseSlot("pad:#1", s) && !parseSlot("pad:x#99", s) && !parseSlot("bogus", s));
    SlotSource p;
    p.kind = SlotSource::Pad;
    p.guid = "g1";
    p.instance = 2;
    CHECK(parseSlot(serializeSlot(p), s) && s == p);

    CHECK(slotJaguarPort(0) == 0 && slotJaguarPort(1) == 1 && slotJaguarPort(2) == 0 && slotJaguarPort(4) == 0 &&
          slotJaguarPort(5) == 1 && slotJaguarPort(7) == 1);
    CHECK(slotSocket(0) == 0 && slotSocket(1) == 0 && slotSocket(2) == 1 && slotSocket(4) == 3 && slotSocket(5) == 1 &&
          slotSocket(7) == 3);

    std::array<SlotSource, kSlots> cfg;   // all auto
    auto act0 = activeSlots(false, false);
    CHECK(act0[0] && act0[1] && !act0[2] && !act0[5]);
    // Two controllers, no tap: players 1 and 2, exactly as before.  The keyboard sits on player 1.
    Routes r = assignSlots(cfg, act0, {"A", "B", "C"}, 0);
    CHECK(r.pad[0] == 0 && r.pad[1] == 1 && r.pad[2] == -1);   // the third has no slot to go to
    CHECK(r.kb[0] && !r.kb[1]);
    // A tap on Jaguar port 1: further controllers fill ITS sockets (slots 2-4).
    Routes t = assignSlots(cfg, activeSlots(true, false), {"A", "B", "C", "D", "E"}, 0);
    CHECK(t.pad[0] == 0 && t.pad[1] == 1 && t.pad[2] == 2 && t.pad[3] == 3 && t.pad[4] == 4 && t.pad[5] == -1);
    // A tap on port 2 only.
    Routes t2 = assignSlots(cfg, activeSlots(false, true), {"A", "B", "C", "D"}, 0);
    CHECK(t2.pad[5] == 2 && t2.pad[6] == 3 && t2.pad[2] == -1);
    // Explicit: the second identical controller plays socket 3; "none" leaves a slot empty.
    cfg[4].kind = SlotSource::Pad;
    cfg[4].guid = "X";
    cfg[4].instance = 1;
    cfg[1].kind = SlotSource::None;
    Routes e = assignSlots(cfg, activeSlots(true, false), {"X", "Y", "X", "Z"}, 0);
    CHECK(e.pad[4] == 2);                        // the second "X"
    CHECK(e.pad[1] == -1);                       // nobody
    CHECK(e.pad[0] == 0 && e.pad[2] == 1 && e.pad[3] == 3);   // the rest, in plug order, skipping the claimed one
    // A controller that is not plugged in leaves its slot empty instead of stealing another.
    cfg[4].guid = "missing";
    Routes m = assignSlots(cfg, activeSlots(true, false), {"X"}, 0);
    CHECK(m.pad[4] == -1 && m.pad[0] == 0);
    // The keyboard: explicit slot wins over the default, and an inactive slot is never driven.
    std::array<SlotSource, kSlots> k;
    k[1].kind = SlotSource::Keyboard;
    Routes kr = assignSlots(k, act0, {}, 0);
    CHECK(!kr.kb[0] && kr.kb[1]);
    Routes kd = assignSlots(std::array<SlotSource, kSlots>(), act0, {}, 1);
    CHECK(kd.kb[1] && !kd.kb[0]);
    CHECK(!assignSlots(std::array<SlotSource, kSlots>(), act0, {"A"}, 3).kb[3]);

    // Persistence and per-game precedence.
    Config cfgs;
    const std::string G = "crc32-1";
    CHECK(resolveSlot(cfgs, 2, G).kind == SlotSource::Auto && !slotDefined(cfgs, 2, ""));
    SlotSource pk;
    pk.kind = SlotSource::Keyboard;
    setSlot(cfgs, 2, "", pk);
    CHECK(resolveSlot(cfgs, 2, G).kind == SlotSource::Keyboard);
    SlotSource pn;
    pn.kind = SlotSource::None;
    setSlot(cfgs, 2, G, pn);
    CHECK(resolveSlot(cfgs, 2, G).kind == SlotSource::None && resolveSlot(cfgs, 2, "").kind == SlotSource::Keyboard);
    clearSlot(cfgs, 2, G);
    CHECK(resolveSlot(cfgs, 2, G).kind == SlotSource::Keyboard);
}

static void testSettingsAndGeometry() {
    Config cfg;
    const std::string G = "g";
    DeviceSettings d = DeviceSettings::load(cfg, G);
    CHECK(d.mouseSpeed == 1.0f && d.mousePort == -1 && d.deadzonePct == 10);
    DeviceSettings::set(cfg, "", "mouse_speed", "2.5");
    DeviceSettings::set(cfg, G, "rotary_speed", "0.5");
    DeviceSettings::set(cfg, "", "mouse_port", "9");   // out of range -> clamped
    d = DeviceSettings::load(cfg, G);
    CHECK(d.mouseSpeed == 2.5f && d.rotarySpeed == 0.5f && d.mousePort == 1);
    d = DeviceSettings::load(cfg, "other");
    CHECK(d.rotarySpeed == 1.0f);

    // The core's own inverse (libretro.c, light gun):  col = ((gx + 0x8000) * w) / 0x10000.
    auto col = [](int32_t gx, int w) { return ((gx + 0x8000) * w) / 0x10000; };
    PicRect r{100, 50, 640, 480};
    int32_t gx, gy;
    CHECK(gunCoords(r, 100, 50, gx, gy) && col(gx, 320) == 0 && col(gy, 240) == 0);                    // top-left pixel
    CHECK(gunCoords(r, 100 + 639.9f, 50 + 479.9f, gx, gy) && col(gx, 320) == 319 && col(gy, 240) == 239);   // bottom-right pixel
    CHECK(gunCoords(r, 420, 290, gx, gy) && col(gx, 320) == 160 && col(gy, 240) == 120);               // centre
    // A 3x integer-scaled picture in a bigger window: the offset and the scale both count.
    PicRect big{20, 10, 960, 720};
    CHECK(gunCoords(big, 20 + 3 * 100 + 1, 10 + 3 * 50 + 1, gx, gy) && col(gx, 320) == 100 && col(gy, 240) == 50);
    // Outside the picture (letterbox bars, menu bar) is off-screen.
    CHECK(!gunCoords(r, 99, 100, gx, gy) && !gunCoords(r, 300, 49, gx, gy));
    CHECK(!gunCoords(r, 100 + 640, 100, gx, gy) && !gunCoords(r, 300, 50 + 480, gx, gy));
    CHECK(!gunCoords(PicRect(), 1, 1, gx, gy));
    float px, py;
    gunToPointer(r, 0, 0, px, py);
    CHECK(px > 100 + 319 && px < 100 + 321 && py > 50 + 239 && py < 50 + 241);

    CaptureInputs c;
    c.deviceWantsMouse = c.windowFocused = c.gameRunning = true;
    CHECK(wantRelativeMouse(c));
    { CaptureInputs x = c; x.deviceWantsMouse = false; CHECK(!wantRelativeMouse(x)); }
    { CaptureInputs x = c; x.windowFocused = false; CHECK(!wantRelativeMouse(x)); }
    { CaptureInputs x = c; x.gameRunning = false; CHECK(!wantRelativeMouse(x)); }
    { CaptureInputs x = c; x.uiOpen = true; CHECK(!wantRelativeMouse(x)); }       // never trap the cursor behind a menu
    { CaptureInputs x = c; x.userReleased = true; CHECK(!wantRelativeMouse(x)); }
    std::array<DevKind, 2> k = {{DevKind::Pad, DevKind::Rotary}};
    CHECK(resolveMousePort(k, -1) == 1 && resolveMousePort(k, 0) == 1 && resolveMousePort(k, 1) == 1);
    k = {{DevKind::LightGun, DevKind::MouseST}};
    CHECK(resolveMousePort(k, -1) == 0 && resolveMousePort(k, 1) == 1);
    k = {{DevKind::Pad, DevKind::Analog}};
    CHECK(resolveMousePort(k, -1) == -1);

    // Core options the app owns.
    std::string v;
    CHECK(lockedCoreOption("virtualjaguar_alt_inputs", v) && v == "disabled");
    CHECK(lockedCoreOption("virtualjaguar_p1_numpad_to_kb", v) && v == "disabled");
    CHECK(lockedCoreOption("virtualjaguar_p2_retropad_analog_rr", v) && v.empty());
    CHECK(lockedCoreOption("virtualjaguar_p2_device", v) && v == "auto");
    CHECK(!lockedCoreOption("virtualjaguar_mouse_sensitivity", v) && !lockedCoreOption("virtualjaguar_rotary_deadzone", v));
    CHECK(!lockedCoreOption("virtualjaguar_pal", v));
}

// ---------------------------------------------------------------------------------
// Router (no core): what libretro.c would be asked for.
// ---------------------------------------------------------------------------------
struct Rig {
    Profile kbp = Profile::builtin(DeviceClass::Keyboard);
    Profile padp = Profile::builtin(DeviceClass::Gamepad);
    Router router;
    CoreInput in;
    RouterConfig cfg;
    Rig() {
        cfg.routes.kb[0] = true;
        cfg.pic = PicRect{0, 0, 640, 480};
    }
    void frame(const DeviceSnapshot& kb, const PointerInput& ptr = PointerInput(),
               const std::vector<DeviceSnapshot>& pads = {}) {
        HostSource k;
        k.isKeyboard = true;
        k.profile = &kbp;
        k.snap = kb;
        std::vector<HostSource> ps;
        for (const DeviceSnapshot& s : pads) {
            HostSource h;
            h.profile = &padp;
            h.snap = s;
            ps.push_back(h);
        }
        in.beginFrame();
        router.route(cfg, ps, &k, ptr, nullptr, in);
    }
};

static void testRouter() {
    // ---- ST mouse on port 2 -----------------------------------------------------------------
    {
        Rig r;
        r.cfg.kind = {{DevKind::Pad, DevKind::MouseST}};
        r.cfg.routes.kb[0] = false;
        r.cfg.routes.kb[1] = true;
        r.cfg.mousePort = 1;
        PointerInput p;
        p.dx = 7;
        p.dy = -3;
        p.buttons = 1;
        r.frame(DeviceSnapshot(), p);
        CHECK(r.in.state(1, RETRO_DEVICE_MOUSE, 0, RETRO_DEVICE_ID_MOUSE_X) == 7);
        CHECK(r.in.state(1, RETRO_DEVICE_MOUSE, 0, RETRO_DEVICE_ID_MOUSE_Y) == -3);
        CHECK(r.in.state(1, RETRO_DEVICE_MOUSE, 0, RETRO_DEVICE_ID_MOUSE_LEFT) == 1);
        CHECK(r.in.state(1, RETRO_DEVICE_MOUSE, 0, RETRO_DEVICE_ID_MOUSE_RIGHT) == 0);
        CHECK(r.in.state(0, RETRO_DEVICE_MOUSE, 0, RETRO_DEVICE_ID_MOUSE_X) == 0);   // port 1 untouched
        // The speed setting scales it, fractions are carried.
        r.cfg.settings.mouseSpeed = 0.5f;
        p.dx = 3;
        p.dy = 0;
        r.frame(DeviceSnapshot(), p);
        CHECK(r.in.state(1, RETRO_DEVICE_MOUSE, 0, RETRO_DEVICE_ID_MOUSE_X) == 1);   // 1.5 -> 1, carry 0.5
        r.frame(DeviceSnapshot(), p);
        CHECK(r.in.state(1, RETRO_DEVICE_MOUSE, 0, RETRO_DEVICE_ID_MOUSE_X) == 2);   // 1.5 + 0.5 -> 2
        // The keyboard's pointer keys move it too (default: the arrows), and Z / X are the buttons.
        r.cfg.settings.mouseSpeed = 1.0f;
        r.frame(keys({SDL_SCANCODE_RIGHT, SDL_SCANCODE_Z}));
        CHECK(r.in.state(1, RETRO_DEVICE_MOUSE, 0, RETRO_DEVICE_ID_MOUSE_X) > 0);
        CHECK(r.in.state(1, RETRO_DEVICE_MOUSE, 0, RETRO_DEVICE_ID_MOUSE_LEFT) == 1);
        r.frame(keys({SDL_SCANCODE_X}));
        CHECK(r.in.state(1, RETRO_DEVICE_MOUSE, 0, RETRO_DEVICE_ID_MOUSE_RIGHT) == 1);
        // Nothing pressed: nothing moves (the negative control for all of the above).
        r.frame(DeviceSnapshot());
        CHECK(r.in.state(1, RETRO_DEVICE_MOUSE, 0, RETRO_DEVICE_ID_MOUSE_X) == 0);
        CHECK(r.in.state(1, RETRO_DEVICE_MOUSE, 0, RETRO_DEVICE_ID_MOUSE_LEFT) == 0);
    }
    // ---- rotary: mouse X, or two keys, or a stick -------------------------------------------------
    {
        Rig r;
        r.cfg.kind = {{DevKind::Rotary, DevKind::Pad}};
        r.cfg.mousePort = 0;
        PointerInput p;
        p.dx = 5;
        p.dy = 9;   // a rotary is one wheel: Y is ignored
        r.frame(DeviceSnapshot(), p);
        CHECK(r.in.state(0, RETRO_DEVICE_MOUSE, 0, RETRO_DEVICE_ID_MOUSE_X) == 5);
        CHECK(r.in.state(0, RETRO_DEVICE_MOUSE, 0, RETRO_DEVICE_ID_MOUSE_Y) == 0);
        // Two keys: one count per frame at speed 1; the sign follows the key.
        for (int i = 0; i < 4; i++) r.frame(keys({SDL_SCANCODE_RIGHT}));
        CHECK(r.in.state(0, RETRO_DEVICE_MOUSE, 0, RETRO_DEVICE_ID_MOUSE_X) == 1);
        r.frame(keys({SDL_SCANCODE_LEFT}));
        CHECK(r.in.state(0, RETRO_DEVICE_MOUSE, 0, RETRO_DEVICE_ID_MOUSE_X) == -1);
        // The sensitivity setting: speed 2.5 emits 2, 3, 2 ... (fractions carried), never loses a count.
        r.cfg.settings.rotarySpeed = 2.5f;
        int total = 0;
        for (int i = 0; i < 4; i++) {
            r.frame(keys({SDL_SCANCODE_RIGHT}));
            total += r.in.state(0, RETRO_DEVICE_MOUSE, 0, RETRO_DEVICE_ID_MOUSE_X);
        }
        CHECK(total == 10);
        // A gamepad stick: proportional.
        r.cfg.settings.rotarySpeed = 4.0f;
        DeviceSnapshot half;
        half.axis[SDL_GAMEPAD_AXIS_RIGHTX] = 32767;
        r.cfg.routes.pad[0] = 0;
        r.cfg.routes.kb[0] = false;
        r.frame(DeviceSnapshot(), PointerInput(), {half});
        CHECK(r.in.state(0, RETRO_DEVICE_MOUSE, 0, RETRO_DEVICE_ID_MOUSE_X) == 4);
        // The rotary keeps its buttons and keypad on the pad: Z (B) still reaches the RetroPad.
        r.cfg.routes.pad[0] = -1;
        r.cfg.routes.kb[0] = true;
        r.cfg.settings.rotarySpeed = 1.0f;
        r.frame(keys({SDL_SCANCODE_Z}));
        CHECK(r.in.state(0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_B) == 1);
    }
    // ---- analog / driving / 6D / paddle ----------------------------------------------------------------
    {
        Rig r;
        r.cfg.kind = {{DevKind::Analog, DevKind::Pad}};
        r.frame(keys({SDL_SCANCODE_D, SDL_SCANCODE_W}));   // stick right + up
        CHECK(r.in.state(0, RETRO_DEVICE_ANALOG, RETRO_DEVICE_INDEX_ANALOG_LEFT, RETRO_DEVICE_ID_ANALOG_X) == 32767);
        CHECK(r.in.state(0, RETRO_DEVICE_ANALOG, RETRO_DEVICE_INDEX_ANALOG_LEFT, RETRO_DEVICE_ID_ANALOG_Y) == -32767);   // up = -Y
        r.frame(keys({SDL_SCANCODE_A, SDL_SCANCODE_S}));
        CHECK(r.in.state(0, RETRO_DEVICE_ANALOG, RETRO_DEVICE_INDEX_ANALOG_LEFT, RETRO_DEVICE_ID_ANALOG_X) == -32767);
        CHECK(r.in.state(0, RETRO_DEVICE_ANALOG, RETRO_DEVICE_INDEX_ANALOG_LEFT, RETRO_DEVICE_ID_ANALOG_Y) == 32767);
        // D is the RetroPad's X.
        r.frame(keys({SDL_SCANCODE_V}));
        CHECK(r.in.state(0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_X) == 1);
        // Rest.
        r.frame(DeviceSnapshot());
        CHECK(r.in.state(0, RETRO_DEVICE_ANALOG, RETRO_DEVICE_INDEX_ANALOG_LEFT, RETRO_DEVICE_ID_ANALOG_X) == 0);
        CHECK(r.in.state(0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_X) == 0);
        // A gamepad stick is proportional, with the dead zone cut out and the rest rescaled.
        DeviceSnapshot g;
        g.axis[SDL_GAMEPAD_AXIS_LEFTX] = 3000;   // inside the 10% dead zone
        r.cfg.routes.pad[0] = 0;
        r.cfg.routes.kb[0] = false;
        r.frame(DeviceSnapshot(), PointerInput(), {g});
        CHECK(r.in.state(0, RETRO_DEVICE_ANALOG, RETRO_DEVICE_INDEX_ANALOG_LEFT, RETRO_DEVICE_ID_ANALOG_X) == 0);
        g.axis[SDL_GAMEPAD_AXIS_LEFTX] = 20000;
        r.frame(DeviceSnapshot(), PointerInput(), {g});
        int x = r.in.state(0, RETRO_DEVICE_ANALOG, RETRO_DEVICE_INDEX_ANALOG_LEFT, RETRO_DEVICE_ID_ANALOG_X);
        CHECK(x > 15000 && x < 20000);
        g.axis[SDL_GAMEPAD_AXIS_LEFTX] = 32767;
        r.frame(DeviceSnapshot(), PointerInput(), {g});
        CHECK(r.in.state(0, RETRO_DEVICE_ANALOG, RETRO_DEVICE_INDEX_ANALOG_LEFT, RETRO_DEVICE_ID_ANALOG_X) == 32767);
    }
    {
        Rig r;   // driving: steering on stick X, R2 accelerator, L2 brake (libretro.c, the driving skin)
        r.cfg.kind = {{DevKind::Driving, DevKind::Pad}};
        r.frame(keys({SDL_SCANCODE_E}));
        CHECK(r.in.state(0, RETRO_DEVICE_ANALOG, RETRO_DEVICE_INDEX_ANALOG_BUTTON, RETRO_DEVICE_ID_JOYPAD_R2) == 32767);
        CHECK(r.in.state(0, RETRO_DEVICE_ANALOG, RETRO_DEVICE_INDEX_ANALOG_BUTTON, RETRO_DEVICE_ID_JOYPAD_L2) == 0);
        r.frame(keys({SDL_SCANCODE_Q}));
        CHECK(r.in.state(0, RETRO_DEVICE_ANALOG, RETRO_DEVICE_INDEX_ANALOG_BUTTON, RETRO_DEVICE_ID_JOYPAD_L2) == 32767);
        DeviceSnapshot g;
        g.axis[SDL_GAMEPAD_AXIS_RIGHT_TRIGGER] = 32767;
        r.cfg.routes.pad[0] = 0;
        r.cfg.routes.kb[0] = false;
        r.frame(DeviceSnapshot(), PointerInput(), {g});
        CHECK(r.in.state(0, RETRO_DEVICE_ANALOG, RETRO_DEVICE_INDEX_ANALOG_BUTTON, RETRO_DEVICE_ID_JOYPAD_R2) == 32767);
    }
    {
        Rig r;   // 6D: both sticks and four analog buttons; E / F are the stick clicks
        r.cfg.kind = {{DevKind::SixD, DevKind::Pad}};
        r.frame(keys({SDL_SCANCODE_J, SDL_SCANCODE_O, SDL_SCANCODE_U, SDL_SCANCODE_B, SDL_SCANCODE_N}));
        CHECK(r.in.state(0, RETRO_DEVICE_ANALOG, RETRO_DEVICE_INDEX_ANALOG_RIGHT, RETRO_DEVICE_ID_ANALOG_X) == -32767);
        CHECK(r.in.state(0, RETRO_DEVICE_ANALOG, RETRO_DEVICE_INDEX_ANALOG_BUTTON, RETRO_DEVICE_ID_JOYPAD_R) == 32767);
        CHECK(r.in.state(0, RETRO_DEVICE_ANALOG, RETRO_DEVICE_INDEX_ANALOG_BUTTON, RETRO_DEVICE_ID_JOYPAD_L) == 32767);
        CHECK(r.in.state(0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_L3) == 1);
        CHECK(r.in.state(0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_R3) == 1);
    }
    {
        Rig r;   // paddle: left stick only; the pad stays an ordinary pad (V is NOT the RetroPad X here)
        r.cfg.kind = {{DevKind::Pad, DevKind::Paddle}};
        r.cfg.routes.kb[0] = false;
        r.cfg.routes.kb[1] = true;
        r.frame(keys({SDL_SCANCODE_D, SDL_SCANCODE_V, SDL_SCANCODE_Z}));
        CHECK(r.in.state(1, RETRO_DEVICE_ANALOG, RETRO_DEVICE_INDEX_ANALOG_LEFT, RETRO_DEVICE_ID_ANALOG_X) == 32767);
        CHECK(r.in.state(1, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_X) == 0);
        CHECK(r.in.state(1, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_B) == 1);
    }
    // ---- light gun --------------------------------------------------------------------------------------------
    {
        Rig r;
        r.cfg.kind = {{DevKind::LightGun, DevKind::Pad}};
        r.cfg.mousePort = 0;
        PointerInput p;
        p.havePos = true;
        p.x = 320;
        p.y = 240;
        r.frame(DeviceSnapshot(), p);
        int gx = r.in.state(0, RETRO_DEVICE_LIGHTGUN, 0, RETRO_DEVICE_ID_LIGHTGUN_SCREEN_X);
        int gy = r.in.state(0, RETRO_DEVICE_LIGHTGUN, 0, RETRO_DEVICE_ID_LIGHTGUN_SCREEN_Y);
        CHECK(gx == 0 && gy == 0);   // the exact middle of the picture is 0,0
        CHECK(r.in.state(0, RETRO_DEVICE_LIGHTGUN, 0, RETRO_DEVICE_ID_LIGHTGUN_IS_OFFSCREEN) == 0);
        CHECK(r.in.state(0, RETRO_DEVICE_LIGHTGUN, 0, RETRO_DEVICE_ID_LIGHTGUN_TRIGGER) == 0);
        p.buttons = 1;
        r.frame(DeviceSnapshot(), p);
        CHECK(r.in.state(0, RETRO_DEVICE_LIGHTGUN, 0, RETRO_DEVICE_ID_LIGHTGUN_TRIGGER) == 1);
        // Right click reloads: off-screen + trigger.
        p.buttons = 2;
        r.frame(DeviceSnapshot(), p);
        CHECK(r.in.state(0, RETRO_DEVICE_LIGHTGUN, 0, RETRO_DEVICE_ID_LIGHTGUN_IS_OFFSCREEN) == 1);
        CHECK(r.in.state(0, RETRO_DEVICE_LIGHTGUN, 0, RETRO_DEVICE_ID_LIGHTGUN_TRIGGER) == 1);
        // The pointer on the menu bar (no position) is off-screen.
        p.buttons = 0;
        p.havePos = false;
        r.frame(DeviceSnapshot(), p);
        CHECK(r.in.state(0, RETRO_DEVICE_LIGHTGUN, 0, RETRO_DEVICE_ID_LIGHTGUN_IS_OFFSCREEN) == 1);
        // A stick drives a cursor (when the mouse is not moving): it starts in the middle and walks.
        Rig s;
        s.cfg.kind = {{DevKind::LightGun, DevKind::Pad}};
        s.cfg.mousePort = 0;
        s.cfg.routes.kb[0] = true;
        s.frame(keys({SDL_SCANCODE_RIGHT}));
        int sx = s.in.state(0, RETRO_DEVICE_LIGHTGUN, 0, RETRO_DEVICE_ID_LIGHTGUN_SCREEN_X);
        CHECK(sx > 0);
        CHECK(s.in.state(0, RETRO_DEVICE_LIGHTGUN, 0, RETRO_DEVICE_ID_LIGHTGUN_IS_OFFSCREEN) == 0);
        s.frame(keys({SDL_SCANCODE_R}));   // reload key
        CHECK(s.in.state(0, RETRO_DEVICE_LIGHTGUN, 0, RETRO_DEVICE_ID_LIGHTGUN_IS_OFFSCREEN) == 1);
        CHECK(s.in.state(0, RETRO_DEVICE_LIGHTGUN, 0, RETRO_DEVICE_ID_LIGHTGUN_TRIGGER) == 1);
        // gun_source = mouse only ignores the keys.
        Rig m;
        m.cfg.kind = {{DevKind::LightGun, DevKind::Pad}};
        m.cfg.mousePort = 0;
        m.cfg.settings.gunSource = 1;
        PointerInput q;
        q.havePos = true;
        q.x = 640 * 0.75f;
        q.y = 240;
        m.frame(keys({SDL_SCANCODE_LEFT}), q);
        int mx = m.in.state(0, RETRO_DEVICE_LIGHTGUN, 0, RETRO_DEVICE_ID_LIGHTGUN_SCREEN_X);
        CHECK(mx > 16000 && mx < 16500);
    }
    // ---- Team Tap sockets ---------------------------------------------------------------------------------------
    {
        Rig r;
        r.cfg.kind = {{DevKind::TeamTap, DevKind::Pad}};
        r.cfg.routes = Routes();
        r.cfg.routes.pad[0] = 0;
        r.cfg.routes.pad[2] = 1;
        r.cfg.routes.pad[3] = 2;
        DeviceSnapshot b, a, k0, k7;
        b.btn[SDL_GAMEPAD_BUTTON_SOUTH] = true;      // B
        a.btn[SDL_GAMEPAD_BUTTON_EAST] = true;       // A
        k0.btn[SDL_GAMEPAD_BUTTON_NORTH] = true;     // keypad 0
        k7.axis[SDL_GAMEPAD_AXIS_RIGHTX] = -30000;   // keypad 7: no RetroPad button on a tap socket
        r.frame(DeviceSnapshot(), PointerInput(), {b, a, k0});
        CHECK(r.in.state(0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_B) == 1);    // socket 0
        CHECK(r.in.state(2, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_A) == 1);    // socket 1 = frontend port 3
        CHECK(r.in.state(2, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_B) == 0);
        CHECK(r.in.state(3, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_X) == 1);    // keypad 0 -> X (teamtap_map)
        r.frame(DeviceSnapshot(), PointerInput(), {k7, k7, k7});
        for (unsigned port = 2; port < 5; port++) CHECK(r.in.state(port, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_MASK) == 0);
        CHECK(CoreInput::tapPadId(C_K6) == RETRO_DEVICE_ID_JOYPAD_R3 && CoreInput::tapPadId(C_K7) == -1 &&
              CoreInput::tapPadId(C_HASH) == -1);
    }
}

static void testProKeyboard() {
    // The Pro Controller's X Y Z are keypad 9 8 7, its shoulders 4 6 (libretro.c pro_controller[]); the
    // keyboard profile carries them on A S D / Q E.
    struct { int sc; Ctl c; } map[] = {{SDL_SCANCODE_A, C_K9}, {SDL_SCANCODE_S, C_K8}, {SDL_SCANCODE_D, C_K7},
                                       {SDL_SCANCODE_Q, C_K4}, {SDL_SCANCODE_E, C_K6}};
    Profile kb = Profile::builtin(DeviceClass::Keyboard);
    for (const auto& m : map) {
        ControlSet cs = evaluate(kb, keys({m.sc}));
        CHECK(cs.test(m.c));
        Rig r;
        r.frame(keys({m.sc}));
        CHECK(r.in.key(CoreInput::keypadKey(0, m.c)));
    }
    // ... and the three bottom-row buttons stay where they were.
    CHECK(evaluate(kb, keys({SDL_SCANCODE_Z})).test(C_B) && evaluate(kb, keys({SDL_SCANCODE_X})).test(C_A) &&
          evaluate(kb, keys({SDL_SCANCODE_C})).test(C_C));
}

// ---------------------------------------------------------------------------------
static void testCheatSheet() {
    Config cfg;
    BindingStore st(cfg);
    const std::string G = "crc32-cafe";
    auto prof = [&](const std::string& game) { return st.resolve(DeviceClass::Keyboard, "", game); };
    Sheet base = buildSheet(prof(""), DevKind::Pad, SheetSource::Keyboard, "");
    std::string t0 = base.text();
    CHECK(t0.find("Player 1: Standard Joypad - keyboard") != std::string::npos);
    CHECK(t0.find("B: Z") != std::string::npos);                       // the actual binding
    CHECK(t0.find("Keypad 7 (Pro: Z): ") != std::string::npos && t0.find(", D\n") != std::string::npos);   // Pro Z on D
    CHECK(t0.find("Controls overlay: F3") != std::string::npos);
    CHECK(t0.find("Keypad overlay: F2") != std::string::npos);

    // A global rebind changes the text ...
    st.set(LayerId{"", DeviceClass::Keyboard, ""}, C_B, {Binding::key(SDL_SCANCODE_M), Binding::key(SDL_SCANCODE_N)});
    std::string t1 = buildSheet(prof(""), DevKind::Pad, SheetSource::Keyboard, "").text();
    CHECK(t1 != t0 && t1.find("B: M, N") != std::string::npos && t1.find("B: Z") == std::string::npos);
    // ... a per-game override applies to that game only ...
    st.set(LayerId{G, DeviceClass::Keyboard, ""}, C_A, {Binding::key(SDL_SCANCODE_G)});
    std::string tg = buildSheet(prof(G), DevKind::Pad, SheetSource::Keyboard, "").text();
    CHECK(tg.find("A: G") != std::string::npos && t1.find("A: X") != std::string::npos);
    CHECK(buildSheet(prof("crc32-other"), DevKind::Pad, SheetSource::Keyboard, "").text() == t1);
    // ... and unbinding shows as such.
    st.set(LayerId{"", DeviceClass::Keyboard, ""}, C_PAUSE, {});
    CHECK(buildSheet(prof(""), DevKind::Pad, SheetSource::Keyboard, "").text().find("Pause: (unbound)") != std::string::npos);

    // The sheet follows the device in Player 1's port.
    Profile p = prof("");
    for (int i = 0; i < kDevKinds; i++) {
        Sheet s = buildSheet(p, (DevKind)i, SheetSource::Keyboard, "");
        CHECK(s.title.find(devKindLabel((DevKind)i)) != std::string::npos);
        CHECK(s.lines.size() > 6);
    }
    CHECK(buildSheet(p, DevKind::LightGun, SheetSource::Keyboard, "").text().find("Reload") != std::string::npos);
    CHECK(buildSheet(p, DevKind::Driving, SheetSource::Keyboard, "").text().find("Accelerator: E") != std::string::npos);
    CHECK(buildSheet(p, DevKind::Rotary, SheetSource::Keyboard, "").text().find("Spin clockwise: Right") != std::string::npos);
    CHECK(buildSheet(p, DevKind::SixD, SheetSource::Keyboard, "").text().find("Rezero") != std::string::npos);
    // A gamepad sheet shows the pad's bindings and its name.
    Profile gp = Profile::builtin(DeviceClass::Gamepad);
    std::string pg = buildSheet(gp, DevKind::Pad, SheetSource::Gamepad, "Test Pad").text();
    CHECK(pg.find("gamepad") == std::string::npos && pg.find("Test Pad") != std::string::npos);
    CHECK(pg.find("B: South") != std::string::npos);
    CHECK(pg.find("Controls overlay: Touchpad") != std::string::npos);

    // ---- auto-show ---------------------------------------------------------------------------------------------------
    AutoShowInput in;
    in.contentLoaded = true;
    CHECK(autoShowDecision(in) == AutoShow::Keyboard);                  // no gamepad on Player 1 -> shown
    in.padOnPlayer1 = true;
    CHECK(autoShowDecision(in) == AutoShow::Gamepad);                   // first use of this controller
    in.padSeenBefore = true;
    CHECK(autoShowDecision(in) == AutoShow::No);                        // a known controller -> not shown
    in.padOnPlayer1 = false;
    in.enabled = false;
    CHECK(autoShowDecision(in) == AutoShow::No);                        // "don't show again"
    in.enabled = true;
    in.contentLoaded = false;
    CHECK(autoShowDecision(in) == AutoShow::No);
}

int main() {
    // SDL's scancode name table does not need SDL_Init.
    testKinds();
    testPortStore();
    testSlots();
    testSettingsAndGeometry();
    testRouter();
    testProKeyboard();
    testCheatSheet();
    std::printf("devices_unit: %d checks, %d failed\n", g_checks, g_fail);
    return g_fail ? 1 : 0;
}
