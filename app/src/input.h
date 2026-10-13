// Keyboard + gamepad -> libretro input.
//
// The devices are snapshotted once per emulated frame and run through the
// remapping layer (remap.h): per-device profiles, per-game overrides, the
// 12-key Jaguar keypad.  The built-in defaults are
//   keyboard  arrows = d-pad, Z = B, X = A, C = C, Enter = Option,
//             Right Shift / Backspace = Pause, number row + numpad = keypad,
//             F2 = keypad overlay
//   gamepad   SDL's standard layout: South=B, East=A, West=C, Back=Pause,
//             Start=Option, d-pad / left stick = d-pad; keypad 0-6 on North,
//             shoulders, triggers and stick clicks, 7/8/9/* on the right stick,
//             # on Guide, the overlay on Share/Capture.
// How the keypad reaches the core (it has no RetroPad buttons for 7, 8, 9, *, #)
// is documented in remap.h.  Ports whose device is not a plain pad (6D, analog,
// driving) and Team Tap sockets get the physical RetroPad instead, as before.
#pragma once
#include <SDL3/SDL.h>

#include <array>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "config.h"
#include "remap.h"

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
    int16_t state(unsigned port, unsigned device, unsigned index, unsigned id) const;

    // Map an SDL scancode to a RETROK_* code (0 if none).
    static unsigned retroKey(SDL_Scancode sc);

    bool stickAsDpad = true;
    std::vector<std::string> gamepadNames() const;
    size_t gamepadCount() const { return pads_.size(); }

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

    struct PadInfo {
        std::string name;
        std::string guid;
        unsigned port = 0;
    };
    std::vector<PadInfo> pads() const;
    const Profile& keyboardProfile() const { return kbProfile_; }
    const Profile& padProfile(size_t i) const { return pads_[i].profile; }

    KeypadOverlay overlay;
    // "Press a button to bind": the next key / pad button / axis push is stored into
    // `layer` for `ctl` (appended when `add`, replacing otherwise) and swallowed.
    void beginCapture(const LayerId& layer, Ctl ctl, bool add);
    void cancelCapture() { capture_.cancel(); }
    bool capturing() const { return capture_.active(); }
    Ctl captureTarget() const { return capCtl_; }
    DeviceClass captureClass() const { return capLayer_.cls; }
    // Whether Jaguar port `p` must see the physical RetroPad (6D / analog / driving device).
    std::function<bool(unsigned port)> portRaw;
    // True while any keypad key is being synthesised (diagnostics / UI).
    bool keypadSynthesised() const { return core_.keypadSynthesised(); }

private:
    struct Pad {
        SDL_Gamepad* handle = nullptr;
        SDL_JoystickID id = 0;
        uint16_t mask = 0;               // the physical RetroPad (legacy / raw ports)
        int16_t axis[4] = {0, 0, 0, 0};  // LX LY RX RY
        std::string guid;
        Profile profile;
    };
    void bindCaptured(const Binding& b);
    void refreshRawPorts();

    std::vector<Pad> pads_;
    CoreInput core_;
    Config* cfg_ = nullptr;
    std::unique_ptr<BindingStore> store_;
    std::string content_;
    Profile kbProfile_;
    BindCapture capture_;
    LayerId capLayer_;
    Ctl capCtl_ = C_COUNT;
    bool capAdd_ = false;
    bool rawPort_[2] = {false, false};
    unsigned rawAge_ = 1000;
};

}  // namespace vj
