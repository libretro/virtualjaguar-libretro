// Keyboard + gamepad -> libretro input.
//
// Joypad (libretro retropad ids, which is what the core's descriptors use):
//   keyboard  arrows = d-pad, Z = B, X = A, C = Y (Jaguar C), Enter = Option
//             (Start), Right Shift / Backspace = Pause (Select)
//   gamepad   SDL's standard layout: South=B, East=A, West=Y, North=X,
//             Back=Select, Start=Start, shoulders=L/R, triggers=L2/R2,
//             stick clicks=L3/R3; left stick also drives the d-pad.
// The keyboard DEVICE (RETRO_DEVICE_KEYBOARD) is answered truthfully from the
// real key state, which is how the core reads the Jaguar keypad from the
// number row / numpad.
#pragma once
#include <SDL3/SDL.h>

#include <array>
#include <string>
#include <vector>

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

private:
    struct Pad {
        SDL_Gamepad* handle = nullptr;
        SDL_JoystickID id = 0;
        uint16_t mask = 0;
        int16_t axis[4] = {0, 0, 0, 0};  // LX LY RX RY
    };
    std::vector<Pad> pads_;
    uint16_t kbMask_ = 0;
    std::array<bool, 512> keys_{};  // indexed by RETROK_*
};

}  // namespace vj
