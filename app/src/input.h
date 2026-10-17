// Keyboard + gamepad -> libretro input.
//
// The devices are snapshotted once per emulated frame and run through the
// remapping layer (remap.h): per-device profiles, per-game overrides, the
// 12-key Jaguar keypad.  The built-in defaults are
//   keyboard  arrows = d-pad, Z = B, X = A, C = C, Enter = Option,
//             Right Shift / Backspace = Pause, number row + numpad = keypad,
//             A S D / Q E = the Pro Controller's X Y Z / shoulders (keypad 9 8 7 / 4 6),
//             F2 = keypad overlay, F3 = controls overlay, F10 = release the mouse
//   gamepad   SDL's standard layout: South=B, East=A, West=C, Back=Pause,
//             Start=Option, d-pad / left stick = d-pad; keypad 0-6 on North,
//             shoulders, triggers and stick clicks, 7/8/9/* on the right stick,
//             # on Guide, the overlay on Share/Capture, the controls overlay on Touchpad.
// How the keypad reaches the core (it has no RetroPad buttons for 7, 8, 9, *, #)
// is documented in remap.h.  What each port's DEVICE reads (mouse, rotary, analog, light
// gun, Team Tap ...) and which host controller plays which player is devices.h / router.h;
// this class only snapshots SDL and hands the snapshot to the Router.
#pragma once
#include <SDL3/SDL.h>

#include <array>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "config.h"
#include "cheatsheet.h"
#include "devices.h"
#include "hold_arbiter.h"
#include "remap.h"
#include "router.h"

namespace vj {

class InputManager {
public:
    void shutdown();
    // Gamepad hot-plug; returns true if the event was consumed.
    bool handleEvent(const SDL_Event& e);
    // Snapshot the devices once per emulated frame.  When `captureKeyboard`
    // is false (a text box has focus, or a modifier chord is held) the
    // keyboard contributes nothing.
    void poll(bool captureKeyboard);
    // Added to Player 1 by every poll(): what the Neuro API client presses this frame (neuro_agent.h).
    ControlSet agentControls;
    // retro_input_state_t for every device (joypad, keyboard, mouse, analog, light gun).
    int16_t state(unsigned port, unsigned device, unsigned index, unsigned id) const;

    // Map an SDL scancode to a RETROK_* code (0 if none).
    static unsigned retroKey(SDL_Scancode sc);

    bool stickAsDpad = true;
    std::vector<std::string> gamepadNames() const;
    size_t gamepadCount() const { return pads_.size(); }

    // ---- which controllers this copy of the app uses (instance.h) ------------------------------
    // SDL hands every connected controller to every process, so two copies side by side would both
    // answer the same pad.  -1 = all (the default), 0 = none (keyboard only), k = only the k-th
    // connected controller (SDL's enumeration order, which is connection order).  Applies at once to
    // the controllers already open; hot-plug honours it too.
    void setPadFilter(int f);
    int padFilter() const { return padFilter_; }
    // Names of every controller SDL currently lists (open or not), in the order "controller k" uses.
    static std::vector<std::string> connectedPadNames();

    // ---- remapping (remap.h) ------------------------------------------------------
    // Binds the profiles to the app config and loads <dataRoot>gamecontrollerdb.txt (extra
    // SDL gamepad mappings for controllers SDL does not know) if it exists.
    void init(Config& cfg, const std::string& dataRoot);
    BindingStore* store() { return store_.get(); }
    // The identity of the running content (contentIdentity()); "" = none.  Selects
    // the per-game overrides.
    void setContent(const std::string& identity);
    const std::string& content() const { return content_; }
    void reresolve();   // after any store edit, hot-plug or content change

    // ---- devices (devices.h) --------------------------------------------------------------------
    // What each Jaguar port holds, from the app's choice and the core's option (devices_ui.cpp).
    void setPortKinds(DevKind port1, DevKind port2);
    DevKind portKind(unsigned port) const { return port < 2 ? kind_[port] : DevKind::Pad; }
    const DeviceSettings& deviceSettings() const { return settings_; }
    // Which host controller / keyboard drives each player slot right now.
    const Routes& routes() const { return routes_; }
    std::array<SlotSource, kSlots> slotConfig() const;
    // The picture rectangle on screen (video.draw()), for the light gun.
    void setPicture(float x, float y, float w, float h) { pic_ = PicRect{x, y, w, h}; }
    const PicRect& picture() const { return pic_; }
    bool uiWantsKeyboard = false;   // a text box has focus (set once per UI frame)
    // The controls-overlay key / button was pressed since the last call.
    bool takeHelpToggle() { bool h = helpToggle_; helpToggle_ = false; return h; }
    // The TV-menu button was pressed (plain binding) or held (a hold binding reached kHoldMs) since the last call.
    bool takeTvMenuToggle() { bool t = tvMenu_; tvMenu_ = false; return t; }

    // ---- dual-use buttons (hold_arbiter.h) -----------------------------------------------------
    // Every UI frame, also while paused: the holds that reached the threshold fire the TV-menu toggle.  `live` = a
    // game is running and owns the input (a tap then reaches it as a short pulse; otherwise it is dropped).
    void tickHolds(uint64_t ms, bool live);
    // The press / release half of the hold bindings.  App::handleEvent calls it once per event, before anything
    // can consume the event, so a release is never lost.  handleEvent() itself does not feed the arbiter.
    void feedHoldEdges(const SDL_Event& e);
    // Input ownership changed (the TV menu closed onto a game): whatever is down now stays masked until released, and
    // the arbiter forgets it (its later release is neither a tap nor a hold).  The guard reads the raw devices.
    void armReleaseGuard();
    // A game was opened or closed: no hold, pulse or unconsumed menu toggle survives into the next one, and what is
    // physically down stays masked until released.
    void clearHolds();
    // The keyboard key is a hold input the arbiter is withholding (or has swallowed): the core must not see the raw key either.
    bool withholdsKey(int scancode) const { return holds_.withholds(kKeyboardDev, Binding::key(scancode)); }
    // The TV in-game menu is open (tv/tv_shell.h): it owns the pads, keyboard and pointer.  While set, poll() hands the
    // core neutral input in every mode -- the menu pauses the game, but under netplay it cannot, and the game must not
    // play under the menu.  Cleared BEFORE armReleaseGuard() so the guard sees what is held at the hand-back.
    void setMenuOwnsInput(bool on) { menuOwns_ = on; }
    bool menuOwnsInput() const { return menuOwns_; }
    std::vector<SDL_Gamepad*> padHandles() const;
    // Mouse capture.  syncMouseCapture() runs once per UI frame: it turns SDL's relative mouse
    // mode on or off from wantRelativeMouse().  `uiOpen` = a menu, popup or window is open.
    void syncMouseCapture(SDL_Window* w, bool gameRunning, bool uiOpen, bool uiWantsMouse);
    bool mouseCaptured() const { return relative_; }
    bool mouseWanted() const { return devWantsRelativeMouse(mousePortKind()); }
    std::function<void(const std::string&)> notify;   // OSD toast

    struct PadInfo {
        std::string name;
        std::string guid;
        bool hasMisc1 = true;    // the pad has a Share / Capture button (the TV menu's hold default is Guide without)
        unsigned port = 0;       // connection order
        int instance = 0;        // which of several identical models (0 = first)
        int slot = -1;           // the player slot it drives right now, -1 none
    };
    std::vector<PadInfo> pads() const;
    const Profile& keyboardProfile() const { return kbProfile_; }
    const Profile& padProfile(size_t i) const { return pads_[i].profile; }

    KeypadOverlay overlay;
    ControlsOverlayState controls;   // the controls cheat sheet (cheatsheet.h)
    // Recompute which host controller / keyboard drives which player slot (poll() does this
    // every frame; the UI calls it when it needs the answer between polls).
    void updateRoutes();
    // "Press a button to bind": the next key / pad button / axis push is stored into
    // `layer` for `ctl` (appended when `add`, replacing otherwise) and swallowed.
    // `hold`: the captured key / button is stored as a hold binding (fires after a long press, hold_arbiter.h).
    void beginCapture(const LayerId& layer, Ctl ctl, bool add, bool hold = false);
    void cancelCapture() { capture_.cancel(); }
    bool capturing() const { return capture_.active(); }
    Ctl captureTarget() const { return capCtl_; }
    DeviceClass captureClass() const { return capLayer_.cls; }
    // True while any keypad key is being synthesised (diagnostics / UI).
    bool keypadSynthesised() const { return core_.keypadSynthesised(); }

private:
    struct Pad {
        SDL_Gamepad* handle = nullptr;
        SDL_JoystickID id = 0;
        std::string guid;
        Profile profile;
        PadCaps caps;
    };
    void bindCaptured(const Binding& b);
    DevKind mousePortKind() const;

    bool padAllowed(SDL_JoystickID id) const;
    void openPad(SDL_JoystickID id);
    void syncHoldInputs(uint32_t dev, const Profile& p);   // tell the arbiter which inputs of `p` are hold inputs
    int padFilter_ = -1;
    std::vector<Pad> pads_;
    CoreInput core_;
    Router router_;
    Config* cfg_ = nullptr;
    std::unique_ptr<BindingStore> store_;
    std::string content_;
    Profile kbProfile_;
    BindCapture capture_;
    LayerId capLayer_;
    Ctl capCtl_ = C_COUNT;
    bool capAdd_ = false;
    bool capHold_ = false;

    DevKind kind_[2] = {DevKind::Pad, DevKind::Pad};
    DeviceSettings settings_;
    Routes routes_;
    PicRect pic_;
    bool helpToggle_ = false;
    bool tvMenu_ = false;
    bool menuOwns_ = false;
    HoldArbiter holds_;
    ReleaseGuard guard_;
    bool swallowPointer_ = false;  // the click / tap that hid the controls overlay: not the game's until released

    // pointer
    bool relative_ = false;        // SDL relative mouse mode is on
    bool userReleased_ = false;    // F10 / Esc released the mouse; a click on the picture takes it back
    bool uiWantsMouse_ = false;
    float accDx_ = 0, accDy_ = 0;  // motion since the last poll (relative mode only)
    float mouseX_ = 0, mouseY_ = 0;
    bool mouseInWindow_ = false;
    bool windowFocused_ = false;
};

}  // namespace vj
