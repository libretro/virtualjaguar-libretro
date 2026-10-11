#include "input.h"

#include "libretro.h"

#include <cstdlib>

namespace vj {

unsigned InputManager::retroKey(SDL_Scancode sc) {
    if (sc >= SDL_SCANCODE_A && sc <= SDL_SCANCODE_Z) return RETROK_a + (sc - SDL_SCANCODE_A);
    if (sc >= SDL_SCANCODE_1 && sc <= SDL_SCANCODE_9) return RETROK_1 + (sc - SDL_SCANCODE_1);
    if (sc == SDL_SCANCODE_0) return RETROK_0;
    if (sc >= SDL_SCANCODE_F1 && sc <= SDL_SCANCODE_F12) return RETROK_F1 + (sc - SDL_SCANCODE_F1);
    if (sc >= SDL_SCANCODE_KP_1 && sc <= SDL_SCANCODE_KP_9) return RETROK_KP1 + (sc - SDL_SCANCODE_KP_1);
    switch (sc) {
    case SDL_SCANCODE_KP_0: return RETROK_KP0;
    case SDL_SCANCODE_KP_PERIOD: return RETROK_KP_PERIOD;
    case SDL_SCANCODE_KP_DIVIDE: return RETROK_KP_DIVIDE;
    case SDL_SCANCODE_KP_MULTIPLY: return RETROK_KP_MULTIPLY;
    case SDL_SCANCODE_KP_MINUS: return RETROK_KP_MINUS;
    case SDL_SCANCODE_KP_PLUS: return RETROK_KP_PLUS;
    case SDL_SCANCODE_KP_ENTER: return RETROK_KP_ENTER;
    case SDL_SCANCODE_KP_EQUALS: return RETROK_KP_EQUALS;
    case SDL_SCANCODE_MINUS: return RETROK_MINUS;
    case SDL_SCANCODE_EQUALS: return RETROK_EQUALS;
    case SDL_SCANCODE_LEFTBRACKET: return RETROK_LEFTBRACKET;
    case SDL_SCANCODE_RIGHTBRACKET: return RETROK_RIGHTBRACKET;
    case SDL_SCANCODE_BACKSLASH: return RETROK_BACKSLASH;
    case SDL_SCANCODE_SEMICOLON: return RETROK_SEMICOLON;
    case SDL_SCANCODE_APOSTROPHE: return RETROK_QUOTE;
    case SDL_SCANCODE_GRAVE: return RETROK_BACKQUOTE;
    case SDL_SCANCODE_COMMA: return RETROK_COMMA;
    case SDL_SCANCODE_PERIOD: return RETROK_PERIOD;
    case SDL_SCANCODE_SLASH: return RETROK_SLASH;
    case SDL_SCANCODE_SPACE: return RETROK_SPACE;
    case SDL_SCANCODE_TAB: return RETROK_TAB;
    case SDL_SCANCODE_RETURN: return RETROK_RETURN;
    case SDL_SCANCODE_BACKSPACE: return RETROK_BACKSPACE;
    case SDL_SCANCODE_ESCAPE: return RETROK_ESCAPE;
    case SDL_SCANCODE_UP: return RETROK_UP;
    case SDL_SCANCODE_DOWN: return RETROK_DOWN;
    case SDL_SCANCODE_LEFT: return RETROK_LEFT;
    case SDL_SCANCODE_RIGHT: return RETROK_RIGHT;
    case SDL_SCANCODE_LSHIFT: return RETROK_LSHIFT;
    case SDL_SCANCODE_RSHIFT: return RETROK_RSHIFT;
    case SDL_SCANCODE_LCTRL: return RETROK_LCTRL;
    case SDL_SCANCODE_RCTRL: return RETROK_RCTRL;
    case SDL_SCANCODE_LALT: return RETROK_LALT;
    case SDL_SCANCODE_RALT: return RETROK_RALT;
    default: return 0;
    }
}

void InputManager::shutdown() {
    for (Pad& p : pads_)
        if (p.handle) SDL_CloseGamepad(p.handle);
    pads_.clear();
}

bool InputManager::handleEvent(const SDL_Event& e) {
    if (e.type == SDL_EVENT_GAMEPAD_ADDED) {
        SDL_Gamepad* g = SDL_OpenGamepad(e.gdevice.which);
        if (!g) return true;
        Pad p;
        p.handle = g;
        p.id = e.gdevice.which;
        pads_.push_back(p);
        return true;
    }
    if (e.type == SDL_EVENT_GAMEPAD_REMOVED) {
        for (size_t i = 0; i < pads_.size(); i++) {
            if (pads_[i].id == e.gdevice.which) {
                SDL_CloseGamepad(pads_[i].handle);
                pads_.erase(pads_.begin() + (long)i);
                break;
            }
        }
        return true;
    }
    return false;
}

std::vector<std::string> InputManager::gamepadNames() const {
    std::vector<std::string> n;
    for (const Pad& p : pads_) {
        const char* s = SDL_GetGamepadName(p.handle);
        n.push_back(s ? s : "Gamepad");
    }
    return n;
}

static inline uint16_t bit(unsigned id) { return (uint16_t)(1u << id); }

void InputManager::poll(bool captureKeyboard) {
    keys_.fill(false);
    kbMask_ = 0;
    if (captureKeyboard) {
        int n = 0;
        const bool* ks = SDL_GetKeyboardState(&n);
        SDL_Keymod mods = SDL_GetModState();
        // A held Ctrl/Alt/Cmd chord is an app shortcut, not Jaguar input.
        if (ks && !(mods & (SDL_KMOD_CTRL | SDL_KMOD_ALT | SDL_KMOD_GUI))) {
            for (int sc = 0; sc < n; sc++) {
                if (!ks[sc]) continue;
                unsigned rk = retroKey((SDL_Scancode)sc);
                if (rk && rk < keys_.size()) keys_[rk] = true;
            }
            if (keys_[RETROK_UP]) kbMask_ |= bit(RETRO_DEVICE_ID_JOYPAD_UP);
            if (keys_[RETROK_DOWN]) kbMask_ |= bit(RETRO_DEVICE_ID_JOYPAD_DOWN);
            if (keys_[RETROK_LEFT]) kbMask_ |= bit(RETRO_DEVICE_ID_JOYPAD_LEFT);
            if (keys_[RETROK_RIGHT]) kbMask_ |= bit(RETRO_DEVICE_ID_JOYPAD_RIGHT);
            if (keys_[RETROK_z]) kbMask_ |= bit(RETRO_DEVICE_ID_JOYPAD_B);
            if (keys_[RETROK_x]) kbMask_ |= bit(RETRO_DEVICE_ID_JOYPAD_A);
            if (keys_[RETROK_c]) kbMask_ |= bit(RETRO_DEVICE_ID_JOYPAD_Y);
            if (keys_[RETROK_RETURN]) kbMask_ |= bit(RETRO_DEVICE_ID_JOYPAD_START);
            if (keys_[RETROK_RSHIFT] || keys_[RETROK_BACKSPACE]) kbMask_ |= bit(RETRO_DEVICE_ID_JOYPAD_SELECT);
        }
    }

    for (Pad& p : pads_) {
        p.mask = 0;
        SDL_Gamepad* g = p.handle;
        auto btn = [&](SDL_GamepadButton b, unsigned id) {
            if (SDL_GetGamepadButton(g, b)) p.mask |= bit(id);
        };
        btn(SDL_GAMEPAD_BUTTON_SOUTH, RETRO_DEVICE_ID_JOYPAD_B);
        btn(SDL_GAMEPAD_BUTTON_EAST, RETRO_DEVICE_ID_JOYPAD_A);
        btn(SDL_GAMEPAD_BUTTON_WEST, RETRO_DEVICE_ID_JOYPAD_Y);
        btn(SDL_GAMEPAD_BUTTON_NORTH, RETRO_DEVICE_ID_JOYPAD_X);
        btn(SDL_GAMEPAD_BUTTON_BACK, RETRO_DEVICE_ID_JOYPAD_SELECT);
        btn(SDL_GAMEPAD_BUTTON_START, RETRO_DEVICE_ID_JOYPAD_START);
        btn(SDL_GAMEPAD_BUTTON_DPAD_UP, RETRO_DEVICE_ID_JOYPAD_UP);
        btn(SDL_GAMEPAD_BUTTON_DPAD_DOWN, RETRO_DEVICE_ID_JOYPAD_DOWN);
        btn(SDL_GAMEPAD_BUTTON_DPAD_LEFT, RETRO_DEVICE_ID_JOYPAD_LEFT);
        btn(SDL_GAMEPAD_BUTTON_DPAD_RIGHT, RETRO_DEVICE_ID_JOYPAD_RIGHT);
        btn(SDL_GAMEPAD_BUTTON_LEFT_SHOULDER, RETRO_DEVICE_ID_JOYPAD_L);
        btn(SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER, RETRO_DEVICE_ID_JOYPAD_R);
        btn(SDL_GAMEPAD_BUTTON_LEFT_STICK, RETRO_DEVICE_ID_JOYPAD_L3);
        btn(SDL_GAMEPAD_BUTTON_RIGHT_STICK, RETRO_DEVICE_ID_JOYPAD_R3);
        if (SDL_GetGamepadAxis(g, SDL_GAMEPAD_AXIS_LEFT_TRIGGER) > 8000) p.mask |= bit(RETRO_DEVICE_ID_JOYPAD_L2);
        if (SDL_GetGamepadAxis(g, SDL_GAMEPAD_AXIS_RIGHT_TRIGGER) > 8000) p.mask |= bit(RETRO_DEVICE_ID_JOYPAD_R2);
        p.axis[0] = SDL_GetGamepadAxis(g, SDL_GAMEPAD_AXIS_LEFTX);
        p.axis[1] = SDL_GetGamepadAxis(g, SDL_GAMEPAD_AXIS_LEFTY);
        p.axis[2] = SDL_GetGamepadAxis(g, SDL_GAMEPAD_AXIS_RIGHTX);
        p.axis[3] = SDL_GetGamepadAxis(g, SDL_GAMEPAD_AXIS_RIGHTY);
        if (stickAsDpad) {
            const int t = 16000;
            if (p.axis[0] < -t) p.mask |= bit(RETRO_DEVICE_ID_JOYPAD_LEFT);
            if (p.axis[0] > t) p.mask |= bit(RETRO_DEVICE_ID_JOYPAD_RIGHT);
            if (p.axis[1] < -t) p.mask |= bit(RETRO_DEVICE_ID_JOYPAD_UP);
            if (p.axis[1] > t) p.mask |= bit(RETRO_DEVICE_ID_JOYPAD_DOWN);
        }
    }
}

int16_t InputManager::state(unsigned port, unsigned device, unsigned index, unsigned id) const {
    device &= RETRO_DEVICE_MASK;
    switch (device) {
    case RETRO_DEVICE_JOYPAD: {
        uint16_t m = 0;
        if (port == 0) m |= kbMask_;
        if (port < pads_.size()) m |= pads_[port].mask;
        if (id == RETRO_DEVICE_ID_JOYPAD_MASK) return (int16_t)m;
        return id < 16 ? (int16_t)((m >> id) & 1u) : 0;
    }
    case RETRO_DEVICE_ANALOG: {
        if (port >= pads_.size()) return 0;
        const Pad& p = pads_[port];
        if (index == RETRO_DEVICE_INDEX_ANALOG_LEFT)
            return id == RETRO_DEVICE_ID_ANALOG_X ? p.axis[0] : id == RETRO_DEVICE_ID_ANALOG_Y ? p.axis[1] : 0;
        if (index == RETRO_DEVICE_INDEX_ANALOG_RIGHT)
            return id == RETRO_DEVICE_ID_ANALOG_X ? p.axis[2] : id == RETRO_DEVICE_ID_ANALOG_Y ? p.axis[3] : 0;
        return 0;
    }
    case RETRO_DEVICE_KEYBOARD:
        return (port == 0 && id < keys_.size() && keys_[id]) ? 1 : 0;
    default:
        return 0;
    }
}

}  // namespace vj
