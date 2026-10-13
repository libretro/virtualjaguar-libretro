// The Jaguar's non-pad peripherals and how the app attaches them: which device is in each
// port (Settings > Input, persisted globally and per game), which host controller plays which
// player slot (Team Tap), the light-gun geometry, and when the mouse is captured.
//
// Pure logic: no SDL window, no ImGui, no core.  router.cpp turns host input into what the
// core reads; devices_ui.cpp draws the pickers; the headless tests (test/devices_test.cpp,
// test/devices_e2e.cpp) drive both.
//
// What the core offers (libretro.c, SET_CONTROLLER_INFO, ~line 7509):
//   Jaguar port 1  Standard Joypad, Team Tap, Rotary, Analog, Driving, Paddle, 6D, Light Gun
//   Jaguar port 2  Standard Joypad, Team Tap, ST / Amiga / Amiga-AD mouse, Rotary, Analog,
//                  Driving, Paddle, 6D
//   Team Tap sockets (frontend ports 3-8)  Standard Joypad only
#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <vector>

#include "config.h"

namespace vj {

// ---- device kinds --------------------------------------------------------------------------

enum class DevKind : uint8_t {
    Pad, TeamTap, MouseST, MouseAmiga, MouseAmigaAD, Rotary, Analog, Driving, Paddle, SixD, LightGun
};
constexpr int kDevKinds = 11;

// RETRO_DEVICE_JAG_* of libretro.c (the same ids SET_CONTROLLER_INFO advertises).
unsigned devLibretroId(DevKind k);
bool devKindFromLibretro(unsigned id, DevKind& out);
// The core's virtualjaguar_pN_device option values: pad, pad_pro, auto -> Pad; teamtap; mouse_st ...
DevKind devKindFromOption(const std::string& v);
// What a port ends up as: the app's choice (a libretro id, -1 = none) outranks the core option,
// exactly as libretro.c lets a frontend claim outrank the option.
DevKind effectiveDevKind(int frontendDevice, const std::string& optionValue);
const char* devKindLabel(DevKind k);   // "Rotary (Tempest)"
const char* devKindId(DevKind k);      // "rotary"
const char* devKindHelp(DevKind k);    // one sentence: what drives it

inline bool devIsMouse(DevKind k) { return k == DevKind::MouseST || k == DevKind::MouseAmiga || k == DevKind::MouseAmigaAD; }
inline bool devWantsRelativeMouse(DevKind k) { return devIsMouse(k) || k == DevKind::Rotary; }
inline bool devIsPointerClass(DevKind k) { return devWantsRelativeMouse(k) || k == DevKind::LightGun; }
inline bool devIsAbsStick(DevKind k) { return k == DevKind::Analog || k == DevKind::Driving || k == DevKind::Paddle || k == DevKind::SixD; }
// Devices that read the RetroPad's X / L3 / R3 / shoulders themselves (libretro.c): the keypad does not exist there.
inline bool devHasNoKeypad(DevKind k) { return k == DevKind::Analog || k == DevKind::Driving || k == DevKind::SixD; }

// ---- per-port device selection (persisted) -------------------------------------------------------
//   input.port.N                 global          (a libretro device id)
//   input.game.<id>.port.N       this game       (overrides the global one)
//   port.N                       legacy global key written by the first Settings tab; still read
// Resolution: game -> global -> legacy -> -1 ("the core option decides", i.e. a Standard Joypad
// unless virtualjaguar_pN_device says otherwise).
constexpr unsigned kJaguarPorts = 2;
int resolvePortDevice(const Config& cfg, unsigned port, const std::string& gameId);
bool portDeviceDefined(const Config& cfg, unsigned port, const std::string& gameId);   // this layer only
void setPortDevice(Config& cfg, unsigned port, const std::string& gameId, int device);
void clearPortDevice(Config& cfg, unsigned port, const std::string& gameId);
// Is `device` one the core offers on `port` (SET_CONTROLLER_INFO)?  Stale config (a mouse on
// port 1, a gun on port 2) must not reach retro_set_controller_port_device.
bool portOffers(unsigned port, unsigned device);

// ---- player slots and host controllers (Team Tap) ----------------------------------------------------
// Eight player slots, numbered as the core numbers its frontend ports (libretro.c
// teamtap_user[]):  0 = Jaguar port 1 / socket 0,  1 = Jaguar port 2 / socket 0,
//                   2,3,4 = Jaguar port 1 / sockets 1-3,   5,6,7 = Jaguar port 2 / sockets 1-3.
constexpr unsigned kSlots = 8;
inline unsigned slotJaguarPort(unsigned slot) { return slot == 0 || (slot >= 2 && slot <= 4) ? 0u : 1u; }
inline unsigned slotSocket(unsigned slot) { return slot < 2 ? 0u : slot <= 4 ? slot - 1u : slot - 4u; }
std::string slotName(unsigned slot);   // "Player 1", "Player 2", "Port 1, socket 2"

struct SlotSource {
    enum Kind : uint8_t { Auto, None, Keyboard, Pad } kind = Auto;
    std::string guid;   // Pad: the controller model ...
    int instance = 0;   // ... and which of several identical ones (0 = first connected)
    bool operator==(const SlotSource& o) const { return kind == o.kind && guid == o.guid && instance == o.instance; }
};
std::string serializeSlot(const SlotSource& s);    // "auto" "none" "kb" "pad:<guid>#<n>"
bool parseSlot(const std::string& text, SlotSource& out);
SlotSource resolveSlot(const Config& cfg, unsigned slot, const std::string& gameId);
bool slotDefined(const Config& cfg, unsigned slot, const std::string& gameId);
void setSlot(Config& cfg, unsigned slot, const std::string& gameId, const SlotSource& s);
void clearSlot(Config& cfg, unsigned slot, const std::string& gameId);

struct Routes {
    std::array<int, kSlots> pad;     // connected-controller index driving the slot, -1 = none
    std::array<bool, kSlots> kb;     // the keyboard drives the slot as well
    Routes() { pad.fill(-1); kb.fill(false); }
};
// Slots 0 and 1 always exist; 2-4 only while Jaguar port 1 holds a Team Tap, 5-7 for port 2.
std::array<bool, kSlots> activeSlots(bool tap1, bool tap2);
// Explicit choices first (a controller already claimed by a slot is not handed out again), the
// keyboard falls back to `kbDefaultSlot` when no slot claims it, then every remaining "auto"
// slot takes the next unclaimed controller in connection order.  The first two controllers
// therefore land on players 1 and 2 as before, and further ones fill the Team Tap sockets.
Routes assignSlots(const std::array<SlotSource, kSlots>& cfg, const std::array<bool, kSlots>& active,
                   const std::vector<std::string>& connectedGuids, unsigned kbDefaultSlot);

// ---- app-level device settings (persisted, global with per-game overrides) -----------------
struct DeviceSettings {
    float mouseSpeed = 1.0f;     // host pixels -> mouse counts
    float rotarySpeed = 1.0f;    // rotary turns per host input: mouse pixels, key / stick pushes
    float stickSpeed = 1.0f;     // stick / key driven pointer (mouse, gun aim) speed
    int deadzonePct = 10;        // gamepad stick dead zone, % of full travel
    int mousePort = -1;          // which Jaguar port the physical mouse drives: -1 auto, 0, 1
    int gunSource = 0;           // 0 auto, 1 mouse only, 2 stick / keys only
    int deadzone() const { return deadzonePct * 32767 / 100; }
    static DeviceSettings load(const Config& cfg, const std::string& gameId);
    static void set(Config& cfg, const std::string& gameId, const char* key, const std::string& value);
    static void clear(Config& cfg, const std::string& gameId, const char* key);
    static bool defined(const Config& cfg, const std::string& gameId, const char* key);
};

// ---- light gun geometry -----------------------------------------------------------------------------------
// The rectangle the picture occupies on screen, in the same units as the pointer position
// (ImGui points == SDL window coordinates).  VideoOut::draw() returns it; it already accounts
// for the aspect override, stretch and integer scaling, so a shot lands on the pixel under the
// pointer in every mode.
struct PicRect {
    float x = 0, y = 0, w = 0, h = 0;
    bool valid() const { return w > 0 && h > 0; }
};
// libretro RETRO_DEVICE_ID_LIGHTGUN_SCREEN_X/Y: -0x8000 is the picture's left / top edge and +0x7fff
// its right / bottom, which is what libretro.c inverts with  col = ((gx + 0x8000) * w) / 0x10000.
// Returns false (and leaves gx, gy at the edge) when the pointer is outside the picture.
bool gunCoords(const PicRect& r, float px, float py, int32_t& gx, int32_t& gy);
// The inverse, for the tests and the crosshair preview.
void gunToPointer(const PicRect& r, int32_t gx, int32_t gy, float& px, float& py);

// Whether a gamepad's left stick may also press the D-pad on a port holding `kind`.  Not on the
// analog / driving / 6D / paddle ports: there the stick IS the analog axis, and a D-pad press
// shifts the driving controller's gear, presses the analog hat, or steers BattleSphere twice.
// Explicit D-pad bindings still work.
inline bool stickDrivesDpad(DevKind kind, bool userSetting) { return userSetting && !devIsAbsStick(kind); }

// Which Jaguar port the physical mouse drives: `setting` (0 / 1) if that port holds a pointer-class
// device, otherwise the first port that does (-1 = none).
int resolveMousePort(const std::array<DevKind, 2>& kinds, int setting);

// ---- core options the app owns -------------------------------------------------------------------------
// The core has its own remapping and device options (for frontends with no Controls menu of their
// own).  The app does all of that itself, and leaving them live breaks input: enabling
// virtualjaguar_alt_inputs swaps the RetroPad paths for the core's per-port remap tables and stops
// the keyboard keypad fallbacks the app's keypad synthesis rides on.  Locked (forced, hidden):
//   virtualjaguar_alt_inputs                    -> disabled   (the app's remap layer expects the default paths)
//   virtualjaguar_p1/p2_numpad_to_kb            -> disabled   (only read under alt_inputs)
//   virtualjaguar_p1/p2_retropad_*  (24 each)   -> default    (only read under alt_inputs)
//   virtualjaguar_p1/p2_device                  -> auto       (Settings > Input picks the device; a stale saved
//                                                              value must not fight retro_set_controller_port_device)
// The tuning options (sensitivity, dead zone, offset, response) stay: the app has no equivalent.
bool lockedCoreOption(const std::string& key, std::string& value);

// ---- mouse capture -----------------------------------------------------------------------------------------
struct CaptureInputs {
    bool deviceWantsMouse = false;   // a mouse or rotary is attached to the port the mouse drives
    bool windowFocused = false;
    bool gameRunning = false;        // content loaded and not paused
    bool uiOpen = false;             // a menu, popup or window that takes the pointer is open
    bool userReleased = false;       // the release key was pressed; stays until a click on the picture
};
// Relative mode is on only when every condition holds -- the cursor is never held while a menu is open.
bool wantRelativeMouse(const CaptureInputs& in);

}  // namespace vj
