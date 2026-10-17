// Controller remapping: the pure logic behind input profiles, per-game
// overrides, the 12-key Jaguar keypad and "press a button to bind".
//
// Nothing in here touches a device, ImGui or the core.  InputManager (input.cpp)
// snapshots the keyboard / gamepads, this file turns a snapshot into Jaguar
// controls and the controls into what the core reads through retro_input_state_t,
// and remap_ui.cpp draws it.  Everything is therefore unit-testable headless
// (app/test/remap_test.cpp) and the control -> core-input half is proven against
// the real core in app/test/input_e2e.cpp.
//
// HOW THE KEYPAD REACHES THE CORE (libretro.c, update_input(), default path --
// i.e. virtualjaguar_alt_inputs = disabled):
//   * Pad: UP/DOWN/LEFT/RIGHT, A, B, Y(=Jaguar C), START(=Option), SELECT(=Pause)
//     are read from the RetroPad bitmask.
//   * Keypad: the RetroPad only reaches 0-6 (X, L1, R1, L2, R2, L3, R3), so
//     7, 8, 9, * and # have NO RetroPad button.  The core reads all twelve from
//     RETRO_DEVICE_KEYBOARD on frontend port 0:
//         Jaguar port 1:  0-9, '-' (*), '=' (#)
//         Jaguar port 2:  p q w e r t y u i o  '[' (*)  ']' (#)   (0..9 in that order)
//     but only once a keyboard "has proven itself" (kb_event_seen, set by the
//     SET_KEYBOARD_CALLBACK callback).  So the app (a) answers
//     RETRO_DEVICE_KEYBOARD with SYNTHESISED keys for every keypad control,
//     whichever device pressed it, (b) never sets RetroPad X/L1/R1/L2/R2/L3/R3
//     (that would press the same keypad key a second time, or an alias under
//     the Pro Controller device) and (c) sends one keyboard event after every
//     load so a gamepad-only user is not locked out of the keypad.
//   The one exception to (b) is a Pro Controller port (devices.h): there the Pro's X Y Z L R
//   controls ride RetroPad X / L2 / R2 / L1 / R1, which the core's Pro path turns into keypad
//   9 / 8 / 7 / 4 / 6 itself (router.cpp).
#pragma once

#include <SDL3/SDL.h>

#include <array>
#include <cstdint>
#include <deque>
#include <string>
#include <vector>

#include "config.h"

namespace vj {

// ---- Jaguar controls --------------------------------------------------------------

enum Ctl : uint8_t {
    C_UP, C_DOWN, C_LEFT, C_RIGHT,
    C_A, C_B, C_C, C_OPTION, C_PAUSE,
    C_K0, C_K1, C_K2, C_K3, C_K4, C_K5, C_K6, C_K7, C_K8, C_K9, C_STAR, C_HASH,
    C_OVERLAY,   // app action (toggle the keypad overlay), not a Jaguar control
    // ---- beyond the standard pad (devices.h): the analog / 6D / driving / pointer devices.
    // Appended AFTER C_OVERLAY so kJaguarControls and every existing id stay put.
    C_D, C_E, C_F,   // D (RetroPad X), E (L3), F (R3): the analog / 6D controllers' extra switches
    // Analog controls carry a strength 0..32767 (AnalogSet), not just down/up: a gamepad axis
    // gives the stick position, a key or button gives full scale.  The pair of a stick axis
    // (N, P) is subtracted: value = P - N.
    C_AX_LX_N, C_AX_LX_P, C_AX_LY_N, C_AX_LY_P,   // primary stick   (left / right, up / down)
    C_AX_RX_N, C_AX_RX_P, C_AX_RY_N, C_AX_RY_P,   // secondary stick (6D rotations)
    C_AX_L2, C_AX_R2, C_AX_L1, C_AX_R1,           // analog trigger / shoulder pairs
    C_PT_L, C_PT_R, C_PT_U, C_PT_D,               // pointer motion (mouse, rotary, light gun aim) -- analog
    C_MB_L, C_MB_R,                               // mouse buttons from the keyboard / a pad
    C_GUN_RELOAD,                                 // light gun: fire off-screen
    C_HELP,      // app action: the controls cheat-sheet overlay
    C_TVMENU,    // app action: the TV-style in-game menu
    // ---- the Jaguar Pro Controller (devices.h DevKind::ProPad).  Appended after C_TVMENU so every
    // saved id keeps its meaning.  The Router presses them as the RetroPad buttons libretro.c's Pro
    // path reads (pro_controller[]: X, L2, R2, L, R -> keypad 9, 8, 7, 4, 6) on a Pro port only.
    C_PRO_X, C_PRO_Y, C_PRO_Z, C_PRO_L, C_PRO_R,
    C_COUNT
};
static_assert(C_COUNT <= 64, "ControlSet is a 64-bit mask");
constexpr int kJaguarControls = C_OVERLAY;   // controls the console itself has

inline bool isKeypad(Ctl c) { return c >= C_K0 && c <= C_HASH; }
inline bool isAnalogCtl(Ctl c) { return c >= C_AX_LX_N && c <= C_PT_D; }   // has a 0..32767 strength
inline bool isPointerCtl(Ctl c) { return c >= C_PT_L && c <= C_GUN_RELOAD; }
inline bool isActionCtl(Ctl c) { return c == C_OVERLAY || c == C_HELP || c == C_TVMENU; }   // app actions, never reach the core
inline bool isProCtl(Ctl c) { return c >= C_PRO_X && c <= C_PRO_R; }
const char* ctlId(Ctl c);                     // stable config id: "up", "a", "k7", "star", "overlay"
const char* ctlLabel(Ctl c);                  // "Up", "A", "Keypad 7", "Keypad *"
bool ctlFromId(const std::string& id, Ctl& out);

// ---- bindings -----------------------------------------------------------------------

struct Binding {
    enum Kind : uint8_t { None, Key, PadButton, PadAxis };
    Kind kind = None;
    int code = 0;   // SDL_Scancode / SDL_GamepadButton / SDL_GamepadAxis
    int dir = 0;    // PadAxis: +1 / -1
    bool hold = false;   // Key / PadButton: fires after a long press (HoldArbiter), never as a plain press

    bool operator==(const Binding& o) const { return kind == o.kind && code == o.code && dir == o.dir && hold == o.hold; }
    bool operator!=(const Binding& o) const { return !(*this == o); }

    static Binding key(int scancode) { Binding b; b.kind = Key; b.code = scancode; return b; }
    static Binding button(int b_) { Binding b; b.kind = PadButton; b.code = b_; return b; }
    static Binding axis(int a, int d) { Binding b; b.kind = PadAxis; b.code = a; b.dir = d < 0 ? -1 : 1; return b; }
    Binding withHold() const { Binding b = *this; b.hold = true; return b; }

    // Persistence form.  Scancodes are stored as numbers (SDL's names are display-only:
    // they differ per platform and a few collide); gamepad names come from SDL's
    // mapping-string tables, which are stable.   "k224"  "bleftshoulder"  "a+righty"  "bmisc1~hold"
    std::string serialize() const;
    static bool parse(const std::string& s, Binding& out);
    std::string display() const;   // for humans: "Z", "Left Shoulder", "Right Stick Up"
};

std::string serializeBindings(const std::vector<Binding>& v);   // "none" for empty
std::vector<Binding> parseBindings(const std::string& s, bool* ok = nullptr);

// ---- device classes, layers, profiles -----------------------------------------------

enum class DeviceClass { Keyboard, Gamepad };

// One place a binding can live.  Precedence, highest first (per control, first layer
// that DEFINES the control wins; "none" is a definition meaning "unbound"):
//     this game / this controller      this game / every gamepad (or the keyboard)
//     global    / this controller      global    / every gamepad (or the keyboard)
//     built-in default
struct LayerId {
    std::string gameId;   // "" = global
    DeviceClass cls = DeviceClass::Gamepad;
    std::string guid;     // gamepads only: "" = every gamepad, else one controller model
    bool operator==(const LayerId& o) const { return gameId == o.gameId && cls == o.cls && guid == o.guid; }
};

enum Source : uint8_t { SRC_DEFAULT, SRC_GLOBAL, SRC_GLOBAL_PAD, SRC_GAME, SRC_GAME_PAD };
const char* sourceLabel(Source s);

// What the connected pad can do; Profile::builtin() adapts its defaults to it.
struct PadCaps {
    bool hasMisc1 = true;   // SDL_GamepadHasButton(MISC1): a Share / Capture button
};

struct Profile {
    std::array<std::vector<Binding>, C_COUNT> binds;
    std::array<Source, C_COUNT> source{};
    bool stickAsDpad = true;   // gamepads: the left stick also drives the D-pad
    // `proPort`: the defaults for a pad driving a Pro Controller port, where the shoulders, triggers and
    // North are the Pro's X Y Z L R and keypad 0-4 have no pad button (the overlay reaches them).  The
    // keyboard's defaults are the same either way.
    static Profile builtin(DeviceClass cls, PadCaps caps = PadCaps(), bool proPort = false);
};

class BindingStore {
public:
    explicit BindingStore(Config& cfg) : cfg_(&cfg) {}

    Profile resolve(DeviceClass cls, const std::string& guid, const std::string& gameId, PadCaps caps = PadCaps(),
                    bool proPort = false) const;

    bool defined(const LayerId& l, Ctl c) const;
    std::vector<Binding> get(const LayerId& l, Ctl c) const;   // what the layer defines (empty if not)
    void set(const LayerId& l, Ctl c, const std::vector<Binding>& v);   // empty = explicitly unbound
    void clear(const LayerId& l, Ctl c);                       // back to the next layer down
    void clearLayer(const LayerId& l);

    static std::string prefix(const LayerId& l);   // config key prefix of a layer
    // Which Jaguar port the keyboard drives (0 or 1).
    int keyboardPort() const;
    void setKeyboardPort(int p);

private:
    Config* cfg_;
};

// ---- device snapshot -> controls ------------------------------------------------------

struct ControlSet {
    uint64_t bits = 0;
    bool test(Ctl c) const { return (bits >> c) & 1u; }
    void set(Ctl c) { bits |= 1ull << c; }
    void clear(Ctl c) { bits &= ~(1ull << c); }
    bool any() const { return bits != 0; }
    bool operator==(const ControlSet& o) const { return bits == o.bits; }
    bool operator!=(const ControlSet& o) const { return bits != o.bits; }
};

struct DeviceSnapshot {
    std::array<bool, SDL_SCANCODE_COUNT> key{};
    std::array<bool, SDL_GAMEPAD_BUTTON_COUNT> btn{};
    std::array<int16_t, SDL_GAMEPAD_AXIS_COUNT> axis{};
};

constexpr int kStickThreshold = 16000;
constexpr int kTriggerThreshold = 8000;

// evaluate() covers C_UP..C_OVERLAY (the standard pad, the keypad, the keypad overlay).
// Keypad controls resolve to the DOMINANT axis of a stick, so pushing a stick
// diagonally never presses two keypad keys (digit chords are not wanted).
ControlSet evaluate(const Profile& p, const DeviceSnapshot& s);

// The analog controls' strengths (C_AX_*, C_PT_*), 0..32767 each: a gamepad axis gives its
// deflection past `deadzone` rescaled to full range, a key or button gives 32767.  Several
// bindings on one control take the strongest.  Every other entry is 0.
constexpr int kAnalogDeadzone = 3000;
struct AnalogSet {
    std::array<int16_t, C_COUNT> v{};
    int16_t get(Ctl c) const { return c < C_COUNT ? v[c] : 0; }
    // P - N of a stick axis, -32767..32767 (+ = the P control's direction).
    int32_t axis(Ctl neg, Ctl pos) const { return (int32_t)get(pos) - (int32_t)get(neg); }
    bool any() const {
        for (int16_t x : v)
            if (x) return true;
        return false;
    }
};
AnalogSet evaluateAnalog(const Profile& p, const DeviceSnapshot& s, int deadzone = kAnalogDeadzone);
// The digital controls beyond the standard pad: C_D, C_E, C_F, C_MB_L, C_MB_R, C_GUN_RELOAD, C_HELP, C_TVMENU, C_PRO_*.
// (evaluate() above covers C_UP..C_OVERLAY only, exactly as before the exotic devices existed.)
ControlSet evaluateExtra(const Profile& p, const DeviceSnapshot& s);

// ---- controls -> what the core reads -------------------------------------------------------

// retro_input_state_t answers for the Jaguar ports.  `beginFrame()` then
// `addControls()` (the remapped path) or `addRawPad()` (ports that need the plain
// RetroPad, see below) for every device, `addRawKey()` for the physical keyboard.
class CoreInput {
public:
    void beginFrame();
    // A Jaguar port driven through the profile: pad controls land on the RetroPad
    // bitmask, keypad controls become synthesised keyboard keys.
    void addControls(unsigned port, ControlSet c);
    // A port that must see the physical RetroPad (6D / analog / driving devices read
    // X, L3, R3, L2, R2 ...; Team Tap sockets read the RetroPad buttons 0-6).
    void addRawPad(unsigned port, uint16_t mask);
    // A Team Tap socket (frontend ports 2-7, libretro.c teamtap_map[]): the core reads only the
    // RetroPad there -- no keyboard -- so the keypad controls 0-6 ride X, L, R, L2, R2, L3, R3
    // and 7, 8, 9, * and # cannot be pressed at all.
    void addTapControls(unsigned frontendPort, ControlSet c);
    // The physical keyboard, reported truthfully EXCEPT for the 24 keys the core's default
    // path reads as keypad: those only ever come from addControls().
    void addRawKey(unsigned retroKey);

    // ---- the non-pad devices (libretro.c update_input(), "non-pad devices") ----------------
    // Mouse / rotary: RETRO_DEVICE_MOUSE on the Jaguar port's frontend port.  Deltas accumulate
    // within a frame; buttons are RETRO_DEVICE_ID_MOUSE_LEFT / _RIGHT bits (1 = left, 2 = right).
    void addMouse(unsigned port, int32_t dx, int32_t dy, unsigned buttons);
    // Analog stick axis: RETRO_DEVICE_ANALOG, index LEFT(0)/RIGHT(1), id X(0)/Y(1), -32768..32767.
    void setAnalog(unsigned port, unsigned index, unsigned id, int32_t value);
    // Analog button (RETRO_DEVICE_INDEX_ANALOG_BUTTON, id = a RETRO_DEVICE_ID_JOYPAD_*): 0..32767.
    void setAnalogButton(unsigned port, unsigned id, int32_t value);
    // Light gun: RETRO_DEVICE_LIGHTGUN.  x / y are the libretro -0x7fff..0x7fff screen
    // coordinates, `buttons` a GUN_* mask.
    enum { GUN_TRIGGER = 1, GUN_AUX_A = 2, GUN_AUX_B = 4, GUN_START = 8, GUN_SELECT = 16 };
    void setGun(unsigned port, int32_t x, int32_t y, bool offscreen, unsigned buttons);

    // InputSnapshot (#967): pointer / rotary motion is a delta, not a state.  carryMotion() adds an older snapshot's
    // motion that the core has not taken yet; clearMotion() forgets it once taken.
    void carryMotion(const CoreInput& older);
    void clearMotion();

    int16_t pad(unsigned port) const { return port < 8 ? (int16_t)pad_[port] : 0; }
    bool key(unsigned retroKey) const { return retroKey < keys_.size() && keys_[retroKey]; }
    bool keypadSynthesised() const { return synth_; }
    // retro_input_state_t for the devices the Jaguar ports use: JOYPAD (incl. the bitmask id),
    // KEYBOARD, MOUSE, ANALOG (sticks and analog buttons) and LIGHTGUN.
    int16_t state(unsigned port, unsigned device, unsigned index, unsigned id) const;

    // The libretro RETROK_* the core reads for a keypad control on a Jaguar port (0 = none).
    static unsigned keypadKey(unsigned port, Ctl c);
    // The RETRO_DEVICE_ID_JOYPAD_* a pad control rides on (-1 = not a pad control).
    static int padId(Ctl c);
    // The RetroPad button a Team Tap socket reads for a control (-1 = unreachable there).
    static int tapPadId(Ctl c);
    static bool isCoreKeypadKey(unsigned retroKey);

private:
    struct PortExtra {
        int32_t mouseDx = 0, mouseDy = 0;
        unsigned mouseButtons = 0;
        int16_t stick[2][2] = {{0, 0}, {0, 0}};
        int16_t abtn[16] = {0};
        int32_t gunX = 0, gunY = 0;
        bool gunOff = true;
        unsigned gunButtons = 0;
    };
    std::array<uint32_t, 8> pad_{};
    std::array<bool, 512> keys_{};
    std::array<PortExtra, 2> extra_{};
    bool synth_ = false;
};

// ---- keypad overlay ------------------------------------------------------------------------------

// A modal, gamepad-navigable keypad (1 2 3 / 4 5 6 / 7 8 9 / * 0 #).  While open it
// swallows every device's controls: D-pad/stick move the cursor, A or B presses the
// key under it, C (or the overlay control again) closes it.  Mouse clicks tap keys too.
class KeypadOverlay {
public:
    bool open = false;
    unsigned port = 0;   // Jaguar port the keys go to
    int row = 0, col = 0;

    static Ctl keyAt(int row, int col);

    void toggle();
    void move(int dRow, int dCol);
    void press();                 // press the key under the cursor
    void tap(Ctl key);            // queue a key (mouse / touch)
    void tick();                  // once per emulated frame: advances the press queue
    ControlSet held() const;      // keys currently held down
    // Feed one device's controls (`slot` = device index, < 8).  Returns what the game
    // should see from that device this frame (the input unchanged while closed, nothing
    // while open) and handles the overlay's own toggle / navigation edges.
    ControlSet filter(unsigned slot, ControlSet in);

    static constexpr int kHoldFrames = 6;   // ~100 ms at 60 Hz
    static constexpr int kGapFrames = 3;

private:
    std::array<ControlSet, 8> prev_{};
    std::array<ControlSet, 8> suppress_{};
    std::deque<Ctl> queue_;
    Ctl cur_ = C_COUNT;
    int hold_ = 0, gap_ = 0;
};

// ---- "press a button to bind" -----------------------------------------------------------------------

class BindCapture {
public:
    enum Result { Pending, Captured, Cancelled };
    bool active() const { return active_; }
    DeviceClass cls() const { return cls_; }

    void begin(DeviceClass cls);
    void cancel() { active_ = false; }
    // Resting position of an axis when capture starts (a trigger that rests at -32768
    // must not count as "pressed").  Optional; unseen axes rest at 0.
    void setBaseline(SDL_JoystickID which, int axis, int16_t value);
    // Feed an SDL event while active.  Keyboard class takes KEY_DOWN; gamepad class takes
    // GAMEPAD_BUTTON_DOWN and an axis that moved far enough from its baseline.  Escape
    // cancels either.  Key-ups and repeats never bind (so the key that activated the
    // "Bind" button cannot bind itself).
    Result feed(const SDL_Event& e, Binding& out);

private:
    struct Base { SDL_JoystickID which; int axis; int16_t v; };
    bool active_ = false;
    DeviceClass cls_ = DeviceClass::Keyboard;
    std::vector<Base> base_;
};

// ---- content identity ------------------------------------------------------------------------------------

uint32_t crc32Bytes(const uint8_t* data, size_t size);
// Cartridge images: "crc32-<8 hex>" of the image with a prepended copier header
// stripped -- the same key the core's titledb / filedb use (src/core/file.c
// DetectPrependedHeaderSize + titledb.c TitleDBSetContent).
std::string cartIdentity(const uint8_t* data, size_t size);
// What per-game overrides are keyed by.  Cartridges: cartIdentity().  Discs (.cue/.cdi/.chd)
// are path-loaded and titledb keys them by a boot-stub CRC only the core can extract, so
// they fall back to "disc-<file stem>".  "" if the file cannot be identified at all.
std::string contentIdentity(const std::string& path);

}  // namespace vj
