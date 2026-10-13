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
    C_COUNT
};
constexpr int kJaguarControls = C_OVERLAY;   // controls the console itself has

inline bool isKeypad(Ctl c) { return c >= C_K0 && c <= C_HASH; }
const char* ctlId(Ctl c);                     // stable config id: "up", "a", "k7", "star", "overlay"
const char* ctlLabel(Ctl c);                  // "Up", "A", "Keypad 7", "Keypad *"
bool ctlFromId(const std::string& id, Ctl& out);

// ---- bindings -----------------------------------------------------------------------

struct Binding {
    enum Kind : uint8_t { None, Key, PadButton, PadAxis };
    Kind kind = None;
    int code = 0;   // SDL_Scancode / SDL_GamepadButton / SDL_GamepadAxis
    int dir = 0;    // PadAxis: +1 / -1

    bool operator==(const Binding& o) const { return kind == o.kind && code == o.code && dir == o.dir; }
    bool operator!=(const Binding& o) const { return !(*this == o); }

    static Binding key(int scancode) { Binding b; b.kind = Key; b.code = scancode; return b; }
    static Binding button(int b_) { Binding b; b.kind = PadButton; b.code = b_; return b; }
    static Binding axis(int a, int d) { Binding b; b.kind = PadAxis; b.code = a; b.dir = d < 0 ? -1 : 1; return b; }

    // Persistence form.  Scancodes are stored as numbers (SDL's names are display-only:
    // they differ per platform and a few collide); gamepad names come from SDL's
    // mapping-string tables, which are stable.   "k224"  "bleftshoulder"  "a+righty"
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

struct Profile {
    std::array<std::vector<Binding>, C_COUNT> binds;
    std::array<Source, C_COUNT> source{};
    bool stickAsDpad = true;   // gamepads: the left stick also drives the D-pad
    static Profile builtin(DeviceClass cls);
};

class BindingStore {
public:
    explicit BindingStore(Config& cfg) : cfg_(&cfg) {}

    Profile resolve(DeviceClass cls, const std::string& guid, const std::string& gameId) const;

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
    uint32_t bits = 0;
    bool test(Ctl c) const { return (bits >> c) & 1u; }
    void set(Ctl c) { bits |= 1u << c; }
    void clear(Ctl c) { bits &= ~(1u << c); }
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

// Keypad controls resolve to the DOMINANT axis of a stick, so pushing a stick
// diagonally never presses two keypad keys (digit chords are not wanted).
ControlSet evaluate(const Profile& p, const DeviceSnapshot& s);

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
    // The physical keyboard, reported truthfully EXCEPT for the 24 keys the core's default
    // path reads as keypad: those only ever come from addControls().
    void addRawKey(unsigned retroKey);

    int16_t pad(unsigned port) const { return port < 8 ? (int16_t)pad_[port] : 0; }
    bool key(unsigned retroKey) const { return retroKey < keys_.size() && keys_[retroKey]; }
    bool keypadSynthesised() const { return synth_; }
    // retro_input_state_t for RETRO_DEVICE_JOYPAD (incl. the bitmask id) and
    // RETRO_DEVICE_KEYBOARD.  Other devices return 0 (the caller handles analog).
    int16_t state(unsigned port, unsigned device, unsigned index, unsigned id) const;

    // The libretro RETROK_* the core reads for a keypad control on a Jaguar port (0 = none).
    static unsigned keypadKey(unsigned port, Ctl c);
    // The RETRO_DEVICE_ID_JOYPAD_* a pad control rides on (-1 = not a pad control).
    static int padId(Ctl c);
    static bool isCoreKeypadKey(unsigned retroKey);

private:
    std::array<uint32_t, 8> pad_{};
    std::array<bool, 512> keys_{};
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

// ---- which ports need the physical RetroPad -------------------------------------------

// The bank-switching analog / driving / 6D devices read X, L3, R3, the shoulders and
// triggers straight off the RetroPad, so remapping those buttons to Jaguar controls would
// break them.  Everything else -- a plain pad, Pro Controller, Team Tap socket 0, the
// rotary, the paddle ADC (its pad stays "fully connected"), a PLAIN RETRO_DEVICE_ANALOG
// (libretro.c maps that to the standard pad) -- is an ordinary pad and stays remapped.
// `frontendDevice` is what Settings assigned via retro_set_controller_port_device (-1 =
// none), `optionValue` the core's virtualjaguar_pN_device value ("" = unknown).
bool deviceNeedsRawPad(int frontendDevice, const std::string& optionValue);

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
