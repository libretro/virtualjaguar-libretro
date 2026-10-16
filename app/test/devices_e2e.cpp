// End-to-end device test: synthetic HOST input -> the app's Router -> the REAL core (statically
// linked, through the retro_* API via CoreHost) -> what the emulated console sees.  For every device
// libretro.c supports: the mouse and rotary quadrature state, the analog / driving / 6D values a
// game would decode off $F14000, the paddle ADC at $F17C00, the light gun's aim point, the Team Tap
// sockets' matrix slots, the Pro Controller keyboard layout, and the locked core options.
//
// Every device group has a NEGATIVE CONTROL: the same device, the same frames, but no host input (the
// mapping has nothing to translate), which must read the rest value.  A group that passes both
// ways would pass for the wrong reason; the PR also quotes a run with the Router stubbed out.
//
// usage: vj_devices_e2e <rom.j64>
#include "core_host.h"
#include "devices.h"
#include "hold_arbiter.h"
#include "router.h"

#include "libretro.h"

extern "C" {
#include "inputdev.h"
#include "joystick.h"
#include "paddle.h"
}

#include <cstdio>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <string>

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

static CoreInput g_in;
static Router g_router;
static RouterConfig g_cfg;
static Profile g_kbp, g_padp;
static CoreHost* g_host;
static std::string g_rom;

static DeviceSnapshot keys(std::initializer_list<int> sc) {
    DeviceSnapshot s;
    for (int k : sc) s.key[(size_t)k] = true;
    return s;
}

// One emulated frame: route the host input, then let the core read it.
static void frame(const DeviceSnapshot& kb, const PointerInput& ptr = PointerInput(),
                  const std::vector<DeviceSnapshot>& pads = {}) {
    HostSource k;
    k.isKeyboard = true;
    k.profile = &g_kbp;
    k.snap = kb;
    std::vector<HostSource> ps;
    for (const DeviceSnapshot& s : pads) {
        HostSource h;
        h.profile = &g_padp;
        h.snap = s;
        ps.push_back(h);
    }
    g_in.beginFrame();
    g_router.route(g_cfg, ps, &k, ptr, nullptr, g_in);
    g_host->runFrame();
}

static void frames(int n, const DeviceSnapshot& kb, const PointerInput& ptr = PointerInput(),
                   const std::vector<DeviceSnapshot>& pads = {}) {
    for (int i = 0; i < n; i++) frame(kb, ptr, pads);
}

// Fresh content, the devices claimed with the core exactly as applyPortDevices() does.
static bool load(DevKind p1, DevKind p2) {
    std::string err;
    if (!g_host->loadContent(g_rom, err)) {
        std::fprintf(stderr, "load failed: %s\n", err.c_str());
        return false;
    }
    g_host->setControllerDevice(0, devLibretroId(p1));
    g_host->setControllerDevice(1, devLibretroId(p2));
    g_router = Router();
    g_cfg = RouterConfig();
    g_cfg.kind = {{p1, p2}};
    g_cfg.routes.kb[0] = true;
    g_cfg.routes.kb[1] = false;
    g_cfg.mousePort = resolveMousePort(g_cfg.kind, -1);
    g_cfg.pic = PicRect{0, 0, 640, 480};
    frames(3, DeviceSnapshot());   // geometry published, devices settled
    return true;
}

// ---- bus-side decoding, as a game does it (test/tools/*_decode_test.c) -------------------------------------------
static const uint16_t row_word_p1[4] = {0x81FE, 0x81FD, 0x81FB, 0x81F7};
static const uint16_t row_word_p2[4] = {0x817F, 0x81BF, 0x81DF, 0x81EF};
static const int j_base[2] = {8, 12};
static const int c_bit[2] = {0, 2};
static const int b_bit[2] = {1, 3};

static void scanRow(int port, int row, uint16_t* joy, uint16_t* but) {
    JoystickWriteWord(0, port ? row_word_p2[row] : row_word_p1[row]);
    *joy = JoystickReadWord(0);
    *but = JoystickReadWord(2);
}
static int nib(uint16_t joy, int port) { return (joy >> j_base[port]) & 0x0F; }
static int bitv(uint16_t v, int b) { return (v >> b) & 1; }

// TR10 recipe for the 2-bank analog device: scan two banks, bank 0 is the one whose row-0 C column is low.
static bool readAnalog(int port, int* x, int* y, uint16_t* butRow0 = nullptr) {
    uint16_t joy[8], but[8];
    for (int b = 0; b < 2; b++)
        for (int r = 0; r < 4; r++) scanRow(port, r, &joy[4 * b + r], &but[4 * b + r]);
    int base = -1;
    for (int b = 0; b < 2; b++)
        if (bitv(but[4 * b], c_bit[port]) == 0) {
            base = 4 * b;
            break;
        }
    if (base < 0) return false;
    *x = nib(joy[base + 0], port) | (nib(joy[base + 1], port) << 4);
    *y = nib(joy[base + 2], port) | (nib(joy[base + 3], port) << 4);
    if (butRow0) *butRow0 = but[base];
    return true;
}

enum { AX_X, AX_Y, AX_Z, AX_TX, AX_TY, AX_TZ };
// TR10 p.22: which DOF and nibble each (bank, row) carries.
static const uint8_t kMap6D[3][4] = {{AX_X, AX_Y, AX_Z, AX_X | 8}, {AX_TX, AX_TY, AX_TZ, AX_Y | 8}, {AX_TX | 8, AX_TY | 8, AX_TZ | 8, AX_Z | 8}};

static bool read6D(int port, int out[6]) {
    uint16_t joy[12], but[12];
    for (int b = 0; b < 3; b++)
        for (int r = 0; r < 4; r++) scanRow(port, r, &joy[4 * b + r], &but[4 * b + r]);
    int bank0 = -1;
    for (int b = 0; b < 3; b++)
        if (bitv(but[4 * b], c_bit[port]) == 0) {
            bank0 = 4 * b;
            break;
        }
    if (bank0 < 0) return false;
    for (int i = 0; i < 6; i++) out[i] = 0;
    for (int b = 0; b < 3; b++)
        for (int r = 0; r < 4; r++) {
            uint8_t cell = kMap6D[b][r];
            int v = nib(joy[(bank0 + 4 * b) % 12 + r], port);
            out[cell & 7] |= (cell & 8) ? v << 4 : v;
        }
    return true;
}

// InputDevStateSave layout (inputdev.c): per port 20 bytes -- x{backlog i32, frac i32, phase}, y{...}, btn_left, btn_right.
struct QuadState {
    int32_t xb, yb;
    int left, right;
};
static QuadState quad(int port) {
    uint8_t buf[256] = {0};
    InputDevStateSave(buf);
    const uint8_t* p = buf + 20 * port;
    QuadState q;
    std::memcpy(&q.xb, p, 4);
    std::memcpy(&q.yb, p + 9, 4);
    q.left = p[18];
    q.right = p[19];
    return q;
}

// ---------------------------------------------------------------------------------
static void testMouse() {
    std::printf("-- ST mouse on port 2\n");
    if (!load(DevKind::Pad, DevKind::MouseST)) return;
    CHECK(InputDevGetType(1) == INPUTDEV_MOUSE_ST);
    g_cfg.routes.kb[0] = false;
    g_cfg.routes.kb[1] = true;

    // Negative control: nothing moved, nothing queued, no button.
    frames(2, DeviceSnapshot());
    QuadState q = quad(1);
    CHECK(q.xb == 0 && q.yb == 0 && !q.left && !q.right);
    uint16_t joy, but;
    scanRow(1, 0, &joy, &but);
    CHECK(bitv(but, 3) == 1);   // $F14002 bit 3 high: LMB not held

    // Host motion +40 / -25 and the left button -> positive X backlog, negative Y backlog, LMB on the bus.
    PointerInput p;
    p.dx = 40;
    p.dy = -25;
    p.buttons = 1;
    frame(DeviceSnapshot(), p);
    q = quad(1);
    CHECKF(q.xb >= 20 && q.xb <= 40, "x backlog %d", q.xb);
    CHECKF(q.yb <= -12 && q.yb >= -25, "y backlog %d", q.yb);
    CHECK(q.left == 1 && q.right == 0);
    scanRow(1, 0, &joy, &but);
    CHECK(bitv(but, 3) == 0);   // LMB reads as an asserted (low) bit at $F14002 (inputdev.c mouse overlay)

    // Right button from the keyboard (X is the default MB_R binding) and a key-driven nudge.
    frame(keys({SDL_SCANCODE_X}));
    q = quad(1);
    CHECK(q.right == 1);
}

static void testRotary() {
    std::printf("-- rotary on port 1 and port 2\n");
    for (int port = 0; port < 2; port++) {
        DevKind k1 = port == 0 ? DevKind::Rotary : DevKind::Pad, k2 = port == 1 ? DevKind::Rotary : DevKind::Pad;
        if (!load(k1, k2)) return;
        CHECK(InputDevGetType(port) == INPUTDEV_ROTARY);
        g_cfg.routes.kb[0] = port == 0;
        g_cfg.routes.kb[1] = port == 1;
        g_cfg.mousePort = port;
        frames(2, DeviceSnapshot());
        QuadState q = quad(port);
        CHECK(q.xb == 0);   // negative control
        // Mouse right = clockwise = the core's NEGATIVE backlog (inputdev.c: "Mapping host +X to clockwise ... Hence the negation").
        PointerInput p;
        p.dx = 30;
        p.dy = 17;   // ignored: one wheel
        frame(DeviceSnapshot(), p);
        q = quad(port);
        CHECKF(q.xb <= -15 && q.xb >= -30, "port %d x backlog %d", port + 1, q.xb);
        CHECK(q.yb == 0);
        p.dx = -30;
        frame(DeviceSnapshot(), p);
        q = quad(port);
        CHECKF(q.xb > -15, "counter-clockwise motion cancels the clockwise backlog: %d", q.xb);
    }
    // Two keys (Left/Right arrows): sustained rotation queues in the right direction.
    if (!load(DevKind::Rotary, DevKind::Pad)) return;
    g_cfg.settings.rotarySpeed = 4.0f;
    frames(10, keys({SDL_SCANCODE_LEFT}));
    QuadState q = quad(0);
    CHECKF(q.xb >= 10, "left key -> counter-clockwise (positive) backlog %d", q.xb);
    // The rotary keeps its buttons on the pad and loses only U/D/L/R (libretro.c, "Withhold only the four direction slots").
    frame(keys({SDL_SCANCODE_Z, SDL_SCANCODE_UP}));
    CHECK(joypad0Buttons[BUTTON_B] != 0);
    CHECK(joypad0Buttons[BUTTON_U] == 0);
}

static void testAnalog() {
    std::printf("-- analog / driving\n");
    if (!load(DevKind::Analog, DevKind::Pad)) return;
    CHECK(InputDevGetType(0) == INPUTDEV_ANALOG);
    int x = -1, y = -1;
    // Negative control: nothing deflected, the device stays inert (reads as a pad, no bank flag).
    frames(2, DeviceSnapshot());
    CHECK(!readAnalog(0, &x, &y));
    // Stick right + forward (W = the stick's up = libretro -Y = TR10 +Y), A button held.
    uint16_t but = 0;
    frames(2, keys({SDL_SCANCODE_D, SDL_SCANCODE_W, SDL_SCANCODE_X}));
    CHECK(readAnalog(0, &x, &y, &but));
    CHECKF(x == 255 && y == 255, "analog X=%d Y=%d", x, y);
    CHECK(bitv(but, b_bit[0]) == 0);   // A -> row 0 B column low
    frames(2, keys({SDL_SCANCODE_A, SDL_SCANCODE_S}));
    CHECK(readAnalog(0, &x, &y));
    CHECKF(x == 1 && y == 1, "analog full left/back X=%d Y=%d", x, y);
    // A gamepad stick is proportional: 20000 of 32767 past a 10% dead zone.
    DeviceSnapshot g;
    g.axis[SDL_GAMEPAD_AXIS_LEFTX] = 20000;
    g_cfg.routes.pad[0] = 0;
    g_cfg.routes.kb[0] = false;
    frames(2, DeviceSnapshot(), PointerInput(), {g});
    CHECK(readAnalog(0, &x, &y));
    CHECKF(x > 190 && x < 210 && y >= 127 && y <= 129, "analog proportional X=%d Y=%d", x, y);
    g_cfg.routes.pad[0] = -1;
    g_cfg.routes.kb[0] = true;

    // Driving, on port 2: steering = stick X, accelerator = R2, brake = L2.
    if (!load(DevKind::Pad, DevKind::Driving)) return;
    CHECK(InputDevGetType(1) == INPUTDEV_DRIVING);
    g_cfg.routes.kb[0] = false;
    g_cfg.routes.kb[1] = true;
    frames(2, DeviceSnapshot());
    CHECK(!readAnalog(1, &x, &y));
    frames(2, keys({SDL_SCANCODE_D, SDL_SCANCODE_E}));
    CHECK(readAnalog(1, &x, &y));
    CHECKF(x == 255 && y == 255, "driving steer right + accelerate X=%d Y=%d", x, y);
    frames(2, keys({SDL_SCANCODE_Q}));
    CHECK(readAnalog(1, &x, &y));
    CHECKF(y == 1, "driving brake Y=%d", y);
}

static void testSixD() {
    std::printf("-- 6D\n");
    if (!load(DevKind::SixD, DevKind::Pad)) return;
    CHECK(InputDevGetType(0) == INPUTDEV_6D);
    int ax[6];
    frames(2, DeviceSnapshot());
    CHECK(!read6D(0, ax));   // negative control: inert until an axis deflects
    struct { int sc; int axis; int want; const char* what; } t[] = {
        {SDL_SCANCODE_A, AX_X, 255, "stick left -> X (TR10 +X is left)"},
        {SDL_SCANCODE_W, AX_Y, 255, "stick up -> Y (TR10 +Y is up)"},
        {SDL_SCANCODE_E, AX_Z, 1, "R2 -> Z fore (thrust), TR10 +Z is toward the user"},
        {SDL_SCANCODE_J, AX_TX, 1, "stick 2 left -> TX (not inverted)"},
        {SDL_SCANCODE_O, AX_TY, 255, "R shoulder -> TY (roll)"},
        {SDL_SCANCODE_I, AX_TZ, 1, "stick 2 up -> TZ (not inverted)"},
    };
    for (const auto& c : t) {
        frames(2, keys({c.sc}));
        bool ok = read6D(0, ax);
        CHECKF(ok, "%s: no bank flag", c.what);
        if (!ok) continue;
        CHECKF(ax[c.axis] == c.want, "%s: got %d want %d", c.what, ax[c.axis], c.want);
        for (int i = 0; i < 6; i++)
            if (i != c.axis) CHECKF(ax[i] == 128, "%s: axis %d left rest (%d)", c.what, i, ax[i]);
    }
}

static void testPaddle() {
    std::printf("-- paddle ADC\n");
    if (!load(DevKind::Paddle, DevKind::Paddle)) return;
    CHECK(InputDevGetType(0) == INPUTDEV_PADDLE && InputDevGetType(1) == INPUTDEV_PADDLE);
    auto sample = [](int ch) {
        PaddleWriteWord(0, (uint16_t)(ch | 4));   // BattleSphere's Timer 1 ISR: select channel, start conversion
        return (int)(PaddleReadWord(0) & 0xFF);
    };
    g_cfg.routes.kb[0] = true;
    g_cfg.routes.kb[1] = true;
    frames(2, DeviceSnapshot());
    CHECKF(sample(0) == 128 && sample(1) == 128 && sample(2) == 128, "centre: %d %d %d", sample(0), sample(1), sample(2));   // negative control
    frames(2, keys({SDL_SCANCODE_D, SDL_SCANCODE_S}));
    int c0 = sample(0), c1 = sample(1), c2 = sample(2), c3 = sample(3);
    CHECKF(c0 == 255 && c1 == 255, "port 1 stick right/down: %d %d", c0, c1);   // +Y is down: the ADC rises (paddle.h, no inversion)
    CHECKF(c2 == 255 && c3 == 255, "port 2 channels follow the same keys: %d %d", c2, c3);
    frames(2, keys({SDL_SCANCODE_A, SDL_SCANCODE_W}));
    CHECKF(sample(0) == 1 && sample(1) == 1, "stick left/up: %d %d", sample(0), sample(1));
    // Port 2 alone (BattleSphere's "2nd Controller: Analog Stick"): the keyboard plays port 2 only.
    g_cfg.routes.kb[0] = false;
    g_cfg.routes.kb[1] = true;
    frames(2, keys({SDL_SCANCODE_D}));
    CHECKF(sample(2) == 255 && sample(0) == 128, "port 2 only: ch2=%d ch0=%d", sample(2), sample(0));
}

static void testLightgun() {
    std::printf("-- light gun\n");
    if (!load(DevKind::LightGun, DevKind::Pad)) return;
    CHECK(InputDevGetType(0) == INPUTDEV_LIGHTGUN);
    const Frame& f = g_host->frame();
    const int W = (int)f.width, H = (int)f.height;
    CHECKF(W > 100 && H > 100, "frame %dx%d", W, H);
    int32_t col = -1, row = -1;
    // Negative control: the pointer is not over the picture -> no aim latched.
    PointerInput p;
    frames(2, DeviceSnapshot(), p);
    CHECK(InputDevLightgunAim(&col, &row) == 0);

    // The picture shown 1:1 at an offset, pointer on a known pixel.
    g_cfg.pic = PicRect{50, 30, (float)W, (float)H};
    g_cfg.mousePort = 0;
    p.havePos = true;
    p.x = 50 + 100.5f;
    p.y = 30 + 60.5f;
    frames(2, DeviceSnapshot(), p);
    CHECK(InputDevLightgunAim(&col, &row) == 1);
    CHECKF(col == 100 && row == 60, "aim %d,%d want 100,60", col, row);

    // The same pixel with the picture scaled 3x into a bigger window (integer scaling / HiDPI): same aim.
    g_cfg.pic = PicRect{10, 5, 3.0f * W, 3.0f * H};
    p.x = 10 + 3 * 100.5f;
    p.y = 5 + 3 * 60.5f;
    frames(2, DeviceSnapshot(), p);
    CHECK(InputDevLightgunAim(&col, &row) == 1);
    CHECKF(col == 100 && row == 60, "3x aim %d,%d want 100,60", col, row);
    // ... and a letterboxed picture: the bar is off-screen, the picture's far corner is its last pixel.
    g_cfg.pic = PicRect{0, 40, (float)W, (float)H};
    p.x = 5;
    p.y = 10;
    frames(2, DeviceSnapshot(), p);
    CHECK(InputDevLightgunAim(&col, &row) == 0);
    p.x = W - 0.5f;
    p.y = 40 + H - 0.5f;
    frames(2, DeviceSnapshot(), p);
    CHECK(InputDevLightgunAim(&col, &row) == 1);
    CHECKF(col == W - 1 && row == H - 1, "corner aim %d,%d want %d,%d", col, row, W - 1, H - 1);

    // Trigger = left click -> the Jaguar's B; reload (right click) = off-screen + trigger.
    g_cfg.pic = PicRect{0, 0, (float)W, (float)H};
    p.x = W / 2.0f;
    p.y = H / 2.0f;
    p.buttons = 1;
    frames(2, DeviceSnapshot(), p);
    CHECK(joypad0Buttons[BUTTON_B] != 0 && InputDevLightgunAim(&col, &row) == 1);
    p.buttons = 2;
    frames(2, DeviceSnapshot(), p);
    CHECK(joypad0Buttons[BUTTON_B] != 0 && InputDevLightgunAim(&col, &row) == 0);
    p.buttons = 0;
    frames(2, DeviceSnapshot(), p);
    CHECK(joypad0Buttons[BUTTON_B] == 0);
}

static void testTeamTap() {
    std::printf("-- Team Tap\n");
    DeviceSnapshot idle, b, a, k0, k7;
    b.btn[SDL_GAMEPAD_BUTTON_SOUTH] = true;      // Jaguar B
    a.btn[SDL_GAMEPAD_BUTTON_EAST] = true;       // Jaguar A
    k0.btn[SDL_GAMEPAD_BUTTON_NORTH] = true;     // keypad 0 -> RetroPad X on a tap socket (teamtap_map[])
    k7.axis[SDL_GAMEPAD_AXIS_RIGHTX] = -30000;   // keypad 7: no RetroPad button there
    for (int tapPort = 0; tapPort < 2; tapPort++) {
        if (!load(tapPort == 0 ? DevKind::TeamTap : DevKind::Pad, tapPort == 1 ? DevKind::TeamTap : DevKind::Pad)) return;
        CHECK(JoystickGetTeamTap(tapPort) && !JoystickGetTeamTap(1 - tapPort));
        std::array<SlotSource, kSlots> cfg;   // all automatic
        g_cfg.routes = assignSlots(cfg, activeSlots(tapPort == 0, tapPort == 1), {"a", "b", "c", "d", "e"}, 0);
        g_cfg.routes.kb[0] = false;
        uint8_t* tp = tapPort == 0 ? joypad0Buttons : joypad1Buttons;
        uint8_t* op = tapPort == 0 ? joypad1Buttons : joypad0Buttons;
        // Negative control: five idle controllers press nothing anywhere.
        frames(2, DeviceSnapshot(), PointerInput(), {idle, idle, idle, idle, idle});
        int any = 0;
        for (int i = 0; i < JOYPAD_BUTTON_SLOTS; i++) any += tp[i] || op[i];
        CHECKF(any == 0, "tap on port %d idle: %d slots set", tapPort + 1, any);
        // Controllers 1 and 2 are players 1 and 2; 3, 4 and 5 take the tap's sockets 1, 2 and 3.
        frames(2, DeviceSnapshot(), PointerInput(), {idle, idle, b, a, k0});
        CHECKF(tp[21 * 1 + BUTTON_B] != 0, "tap port %d socket 1 B", tapPort + 1);
        CHECKF(tp[21 * 2 + BUTTON_A] != 0, "tap port %d socket 2 A", tapPort + 1);
        CHECKF(tp[21 * 3 + BUTTON_0] != 0, "tap port %d socket 3 keypad 0", tapPort + 1);
        int stray = 0;
        for (int i = 0; i < JOYPAD_BUTTON_SLOTS; i++) stray += op[i] != 0;
        CHECKF(stray == 0, "the other port saw %d slots", stray);
        // Controllers 1 and 2 play socket 0 of each port.
        frames(2, DeviceSnapshot(), PointerInput(), {b, a, idle, idle, idle});
        CHECK(joypad0Buttons[BUTTON_B] != 0 && joypad1Buttons[BUTTON_A] != 0);
        // Keypad 7 has no RetroPad button on a tap socket: unreachable (a core limitation).
        frames(2, DeviceSnapshot(), PointerInput(), {idle, idle, k7, k7, k7});
        int sockets = 0;
        for (int i = 21; i < JOYPAD_BUTTON_SLOTS; i++) sockets += tp[i] != 0;
        CHECK(sockets == 0);
    }
    // An explicit assignment: socket 1 of port 2 is played by the SECOND of two identical controllers.
    if (!load(DevKind::Pad, DevKind::TeamTap)) return;
    std::array<SlotSource, kSlots> ex;
    ex[5].kind = SlotSource::Pad;
    ex[5].guid = "same";
    ex[5].instance = 1;
    g_cfg.routes = assignSlots(ex, activeSlots(false, true), {"same", "same"}, 0);
    g_cfg.routes.kb[0] = false;
    CHECK(g_cfg.routes.pad[5] == 1 && g_cfg.routes.pad[0] == 0);
    frames(2, DeviceSnapshot(), PointerInput(), {idle, b});
    CHECK(joypad1Buttons[21 * 1 + BUTTON_B] != 0 && joypad0Buttons[BUTTON_B] == 0);
}

static void testProKeyboard() {
    std::printf("-- Jaguar Pro Controller on the keyboard\n");
    if (!load(DevKind::Pad, DevKind::Pad)) return;
    // The core's own aliases (libretro.c pro_controller[]): X -> keypad 9, Y (L2) -> 8, Z (R2) -> 7, L -> 4, R -> 6.
    struct { int sc; int slot; const char* what; } t[] = {
        {SDL_SCANCODE_A, BUTTON_9, "Pro X"}, {SDL_SCANCODE_S, BUTTON_8, "Pro Y"}, {SDL_SCANCODE_D, BUTTON_7, "Pro Z"},
        {SDL_SCANCODE_Q, BUTTON_4, "Pro left shoulder"}, {SDL_SCANCODE_E, BUTTON_6, "Pro right shoulder"},
        {SDL_SCANCODE_Z, BUTTON_B, "B"}, {SDL_SCANCODE_X, BUTTON_A, "A"}, {SDL_SCANCODE_C, BUTTON_C, "C"},
    };
    frames(2, DeviceSnapshot());
    int none = 0;
    for (int i = 0; i < 21; i++) none += joypad0Buttons[i] != 0;
    CHECK(none == 0);   // negative control
    for (const auto& c : t) {
        frames(2, keys({c.sc}));
        int n = 0, first = -1;
        for (int i = 0; i < 21; i++)
            if (joypad0Buttons[i]) {
                n++;
                if (first < 0) first = i;
            }
        CHECKF(n == 1 && first == c.slot, "%s: %d slot(s), first %d, want %d", c.what, n, first, c.slot);
    }
}

// ---- TV-mode dual-use buttons: HoldArbiter + ReleaseGuard + Router + the real core (#991) --------------------------
// One pad (device 1) on a controller with no Share / Capture button, so Guide is both the keypad's # and the TV
// menu's held button.  Per frame the order is poll()'s: HoldArbiter::apply -> ReleaseGuard::apply -> Router::route
// -> HoldArbiter::frame.  The profile is this test's own; the shared g_padp stays what every other case ran with.
static const uint32_t kPadDev = 1;

static void holdFrame(HoldArbiter& arb, ReleaseGuard& guard, const Profile& prof, DeviceSnapshot snap) {
    arb.apply(kPadDev, snap);
    guard.apply(kPadDev, snap);
    HostSource h;
    h.profile = &prof;
    h.snap = snap;
    HostSource k;
    k.isKeyboard = true;
    k.profile = &g_kbp;
    g_in.beginFrame();
    g_router.route(g_cfg, {h}, &k, PointerInput(), nullptr, g_in);
    g_host->runFrame();
    arb.frame();
}

static std::vector<Binding> holdBindings(const Profile& p) {
    std::vector<Binding> v;
    for (int c = 0; c < C_COUNT; c++)
        for (const Binding& b : p.binds[c])
            if (b.hold) v.push_back(b);
    return v;
}

static void testHoldButtons() {
    std::printf("-- Guide: tap = keypad #, hold = menu, release guard\n");
    if (!load(DevKind::Pad, DevKind::Pad)) return;
    g_host->sendKeyEvent(false, 0, 0, 0);   // the keypad announcement, as finishLoad() makes
    g_cfg.routes.kb[0] = false;
    g_cfg.routes.pad[0] = 0;
    const Profile prof = Profile::builtin(DeviceClass::Gamepad, PadCaps{false});
    const Binding G = Binding::button(SDL_GAMEPAD_BUTTON_GUIDE);
    DeviceSnapshot none, guideDown, eastDown;
    guideDown.btn[SDL_GAMEPAD_BUTTON_GUIDE] = true;
    eastDown.btn[SDL_GAMEPAD_BUTTON_EAST] = true;
    auto hash = [&]() { return joypad0Buttons[BUTTON_d] != 0; };
    auto east = [&]() { return joypad0Buttons[BUTTON_A] != 0; };
    CHECK(holdBindings(prof).size() == 1);   // Guide is the hold input of this pad

    // A tap: nothing while it is down, then # for exactly kPulseFrames emulated frames.
    {
        HoldArbiter arb;
        ReleaseGuard guard;
        arb.setHoldInputs(kPadDev, holdBindings(prof));
        for (int f = 0; f < 3; f++) { holdFrame(arb, guard, prof, none); CHECK(!hash()); }   // negative control
        arb.press(kPadDev, G, 0);
        for (int f = 0; f < 10; f++) { holdFrame(arb, guard, prof, guideDown); CHECK(!hash()); }
        arb.release(kPadDev, G, 200);
        int on = 0;
        for (int f = 0; f < 8; f++) { holdFrame(arb, guard, prof, none); on += hash(); }
        CHECKF(on == HoldArbiter::kPulseFrames, "# pulse lasted %d frames", on);
    }
    // A hold: fires once, and # never reaches the game, not even after the button comes up.
    {
        HoldArbiter arb;
        ReleaseGuard guard;
        arb.setHoldInputs(kPadDev, holdBindings(prof));
        arb.press(kPadDev, G, 0);
        CHECK(arb.takeFires(600) == 1);
        int on = 0;
        for (int f = 0; f < 40; f++) { holdFrame(arb, guard, prof, guideDown); on += hash(); }
        arb.release(kPadDev, G, 2000);
        for (int f = 0; f < 8; f++) { holdFrame(arb, guard, prof, none); on += hash(); }
        CHECKF(on == 0, "# reached the core %d times during a hold", on);
    }
    // The guard: East was already down when the menu closed; it must not be a fresh A press for the game.
    {
        HoldArbiter arb;
        ReleaseGuard guard;
        arb.setHoldInputs(kPadDev, holdBindings(prof));
        holdFrame(arb, guard, prof, eastDown);
        CHECK(east());   // negative control: unguarded, East is A
        holdFrame(arb, guard, prof, none);
        guard.arm(kPadDev, eastDown);
        int a = 0;
        for (int f = 0; f < 10; f++) { holdFrame(arb, guard, prof, eastDown); a += east(); }
        CHECKF(a == 0, "A reached the game %d times while guarded", a);
        holdFrame(arb, guard, prof, none);
        CHECK(!guard.armed());
        holdFrame(arb, guard, prof, eastDown);
        CHECK(east());
    }
    // held_guide_across_handoff_never_reaches_core: Guide went down before the menu closed onto the game.  The hand-off
    // as InputManager::armReleaseGuard() does it: the guard takes the RAW state, then the arbiter forgets the hold.  Its
    // release is then neither a tap (the negative control is the tap case above: the same release WITHOUT the hand-off
    // pulses #) nor a hold.
    {
        HoldArbiter arb;
        ReleaseGuard guard;
        arb.setHoldInputs(kPadDev, holdBindings(prof));
        arb.press(kPadDev, G, 0);
        guard.arm(kPadDev, guideDown);
        arb.clear();
        int on = 0;
        for (int f = 0; f < 10; f++) { holdFrame(arb, guard, prof, guideDown); on += hash(); }
        arb.release(kPadDev, G, 200);   // a no-op for the arbiter
        for (int f = 0; f < 8; f++) { holdFrame(arb, guard, prof, none); on += hash(); }
        CHECKF(on == 0, "# reached the core %d times for a Guide held across the hand-off", on);
        CHECK(arb.takeFires(5000) == 0);
        CHECK(!guard.armed());
    }
}

static void testLockedOptions() {
    std::printf("-- locked core options\n");
    if (!load(DevKind::Pad, DevKind::Pad)) return;
    int locked = 0;
    for (const OptionDef& d : g_host->optionDefs()) {
        std::string v;
        bool want = lockedCoreOption(d.key, v);
        CHECKF(g_host->optionLocked(d.key) == want, "%s", d.key.c_str());
        if (want) {
            locked++;
            CHECKF(!g_host->optionVisible(d.key), "%s still visible", d.key.c_str());
        }
        if (d.key == "virtualjaguar_alt_inputs") CHECK(g_host->optionValue(d) == "disabled");
    }
    CHECKF(locked >= 50, "only %d locked", locked);
    // A saved / session 'enabled' cannot reach the core: the keypad fallbacks the app relies on keep working.
    g_host->setOptionValue("virtualjaguar_alt_inputs", "enabled");
    g_host->overrideOption("virtualjaguar_p1_numpad_to_kb", "keypad");
    frames(3, keys({SDL_SCANCODE_7}));
    CHECK(joypad0Buttons[BUTTON_7] != 0);
    for (const OptionDef& d : g_host->optionDefs())
        if (d.key == "virtualjaguar_alt_inputs") CHECK(g_host->optionValue(d) == "disabled");
    // ... and an --option naming a locked key is REPORTED (it would otherwise silently run the forced value);
    // a stale saved value only warns.
    std::vector<std::string> bad = g_host->checkOverrides(true);
    CHECKF(bad.size() == 1 && bad[0].find("managed by the app") != std::string::npos, "%zu problems", bad.size());
    std::vector<std::string> warn = g_host->checkOverrides(false);
    CHECKF(warn.size() == 1 && warn[0].find("saved value is ignored") != std::string::npos, "%zu warnings", warn.size());
    g_host->overrideOption("virtualjaguar_p1_numpad_to_kb", "disabled");   // the forced value itself is fine
    CHECK(g_host->checkOverrides(true).empty());
}

int main(int argc, char** argv) {
    if (argc < 2) {
        std::fprintf(stderr, "usage: vj_devices_e2e <rom.j64>\n");
        return 2;
    }
    g_rom = argv[1];
    namespace fs = std::filesystem;
    std::error_code ec;
    fs::path tmp = fs::temp_directory_path(ec) / ("vj-devices-e2e-" + std::to_string((long long)std::time(nullptr)));
    fs::create_directories(tmp / "saves", ec);
    fs::create_directories(tmp / "system", ec);
    std::string sys = (tmp / "system").string() + "/", sav = (tmp / "saves").string() + "/";

    g_kbp = Profile::builtin(DeviceClass::Keyboard);
    g_kbp.stickAsDpad = false;
    g_padp = Profile::builtin(DeviceClass::Gamepad);

    CoreHost& host = CoreHost::instance();
    g_host = &host;
    host.setDirectories(sys, sav);
    host.optionLock = &lockedCoreOption;   // as App::wireHost() does
    host.announceKeyboard = true;
    host.inputPoll = []() {};
    host.inputState = [](unsigned p, unsigned d, unsigned i, unsigned id) { return g_in.state(p, d, i, id); };

    testMouse();
    testRotary();
    testAnalog();
    testSixD();
    testPaddle();
    testLightgun();
    testTeamTap();
    testProKeyboard();
    testHoldButtons();
    testLockedOptions();

    host.unloadContent();
    fs::remove_all(tmp, ec);   // our own temp dir only
    std::printf("devices_e2e: %d checks, %d failed\n", g_checks, g_fail);
    return g_fail ? 1 : 0;
}
