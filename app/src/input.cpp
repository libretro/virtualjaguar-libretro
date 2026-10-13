#include "input.h"

#include "libretro.h"
#include "paths.h"

#include <algorithm>
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

void InputManager::init(Config& cfg, const std::string& dataRoot) {
    cfg_ = &cfg;
    store_.reset(new BindingStore(cfg));
    // Extra SDL gamepad mappings (SDL's gamecontrollerdb.txt format) for controllers
    // SDL does not already know; SDL3 also reads SDL_GAMECONTROLLERCONFIG.
    std::string db = dataRoot + "gamecontrollerdb.txt";
    if (fileExists(db)) SDL_AddGamepadMappingsFromFile(db.c_str());
    reresolve();
}

void InputManager::setContent(const std::string& identity) {
    content_ = identity;
    reresolve();
}

void InputManager::reresolve() {
    if (!store_) return;
    kbProfile_ = store_->resolve(DeviceClass::Keyboard, "", content_);
    for (Pad& p : pads_) p.profile = store_->resolve(DeviceClass::Gamepad, p.guid, content_);
}

void InputManager::shutdown() {
    for (Pad& p : pads_)
        if (p.handle) SDL_CloseGamepad(p.handle);
    pads_.clear();
}

bool InputManager::handleEvent(const SDL_Event& e) {
    if (capture_.active()) {
        Binding b;
        BindCapture::Result r = capture_.feed(e, b);
        if (r == BindCapture::Captured) bindCaptured(b);
        // Swallow whatever could have been the bound press, so it does not also act as a hotkey.
        if (r != BindCapture::Pending || e.type == SDL_EVENT_KEY_DOWN || e.type == SDL_EVENT_GAMEPAD_BUTTON_DOWN ||
            e.type == SDL_EVENT_GAMEPAD_AXIS_MOTION)
            return true;
    }
    if (e.type == SDL_EVENT_GAMEPAD_ADDED) {
        SDL_Gamepad* g = SDL_OpenGamepad(e.gdevice.which);
        if (!g) return true;
        Pad p;
        p.handle = g;
        p.id = e.gdevice.which;
        char buf[64] = {0};
        SDL_GUIDToString(SDL_GetJoystickGUIDForID(p.id), buf, sizeof buf);
        p.guid = buf;
        if (store_) p.profile = store_->resolve(DeviceClass::Gamepad, p.guid, content_);
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

std::vector<InputManager::PadInfo> InputManager::pads() const {
    std::vector<PadInfo> v;
    for (size_t i = 0; i < pads_.size(); i++) {
        PadInfo pi;
        const char* s = SDL_GetGamepadName(pads_[i].handle);
        pi.name = s ? s : "Gamepad";
        pi.guid = pads_[i].guid;
        pi.port = (unsigned)i;
        v.push_back(pi);
    }
    return v;
}

void InputManager::beginCapture(const LayerId& layer, Ctl ctl, bool add) {
    capLayer_ = layer;
    capCtl_ = ctl;
    capAdd_ = add;
    capture_.begin(layer.cls);
    for (const Pad& p : pads_)
        for (int a = 0; a < SDL_GAMEPAD_AXIS_COUNT; a++)
            capture_.setBaseline(p.id, a, SDL_GetGamepadAxis(p.handle, (SDL_GamepadAxis)a));
}

void InputManager::bindCaptured(const Binding& b) {
    if (!store_ || capCtl_ >= C_COUNT) return;
    std::vector<Binding> v;
    if (capAdd_) {
        // Start from what is in effect now, so "add" extends rather than shadows.
        if (store_->defined(capLayer_, capCtl_)) {
            v = store_->get(capLayer_, capCtl_);
        } else {
            std::string guid = capLayer_.guid;
            v = store_->resolve(capLayer_.cls, guid, capLayer_.gameId).binds[capCtl_];
        }
    }
    if (std::find(v.begin(), v.end(), b) == v.end()) v.push_back(b);
    store_->set(capLayer_, capCtl_, v);
    if (cfg_) cfg_->save();
    reresolve();
}

static inline uint16_t bit(unsigned id) { return (uint16_t)(1u << id); }

void InputManager::refreshRawPorts() {
    if (!portRaw) return;
    if (++rawAge_ < 30) return;
    rawAge_ = 0;
    for (unsigned p = 0; p < 2; p++) rawPort_[p] = portRaw(p);
}

void InputManager::poll(bool captureKeyboard) {
    core_.beginFrame();
    overlay.tick();
    refreshRawPorts();
    // While a binding is being captured nothing reaches the core: the press being bound
    // must not also act in the game (binding Start would pause it, for one).
    if (capture_.active()) return;

    // ---- keyboard ---------------------------------------------------------------------
    DeviceSnapshot kb;
    if (captureKeyboard) {
        int n = 0;
        const bool* ks = SDL_GetKeyboardState(&n);
        SDL_Keymod mods = SDL_GetModState();
        // A held Ctrl/Alt/Cmd chord is an app shortcut, not Jaguar input.
        if (ks && !(mods & (SDL_KMOD_CTRL | SDL_KMOD_ALT | SDL_KMOD_GUI))) {
            for (int sc = 0; sc < n && sc < (int)kb.key.size(); sc++) {
                if (!ks[sc]) continue;
                kb.key[(size_t)sc] = true;
                unsigned rk = retroKey((SDL_Scancode)sc);
                if (rk) core_.addRawKey(rk);   // truthful, except the keys only the keypad synthesis may press
            }
        }
    }
    kbProfile_.stickAsDpad = false;
    unsigned kbPort = store_ ? (unsigned)store_->keyboardPort() : 0u;
    core_.addControls(kbPort, overlay.filter(7, evaluate(kbProfile_, kb)));

    // ---- gamepads ----------------------------------------------------------------------
    for (size_t i = 0; i < pads_.size(); i++) {
        Pad& p = pads_[i];
        p.mask = 0;
        SDL_Gamepad* g = p.handle;
        DeviceSnapshot snap;
        for (int b = 0; b < SDL_GAMEPAD_BUTTON_COUNT; b++) snap.btn[(size_t)b] = SDL_GetGamepadButton(g, (SDL_GamepadButton)b);
        for (int a = 0; a < SDL_GAMEPAD_AXIS_COUNT; a++) snap.axis[(size_t)a] = SDL_GetGamepadAxis(g, (SDL_GamepadAxis)a);
        p.axis[0] = snap.axis[SDL_GAMEPAD_AXIS_LEFTX];
        p.axis[1] = snap.axis[SDL_GAMEPAD_AXIS_LEFTY];
        p.axis[2] = snap.axis[SDL_GAMEPAD_AXIS_RIGHTX];
        p.axis[3] = snap.axis[SDL_GAMEPAD_AXIS_RIGHTY];

        if (i < 2 && !rawPort_[i]) {
            p.profile.stickAsDpad = stickAsDpad;
            core_.addControls((unsigned)i, overlay.filter(i, evaluate(p.profile, snap)));
            continue;
        }

        // Raw RetroPad (ports >= 2 are Team Tap sockets; 6D / analog / driving devices read
        // X, L3, R3, the shoulders and triggers directly): the standard layout, unremapped.
        auto btn = [&](SDL_GamepadButton b, unsigned id) {
            if (snap.btn[(size_t)b]) p.mask |= bit(id);
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
        if (snap.axis[SDL_GAMEPAD_AXIS_LEFT_TRIGGER] > kTriggerThreshold) p.mask |= bit(RETRO_DEVICE_ID_JOYPAD_L2);
        if (snap.axis[SDL_GAMEPAD_AXIS_RIGHT_TRIGGER] > kTriggerThreshold) p.mask |= bit(RETRO_DEVICE_ID_JOYPAD_R2);
        if (stickAsDpad) {
            if (p.axis[0] < -kStickThreshold) p.mask |= bit(RETRO_DEVICE_ID_JOYPAD_LEFT);
            if (p.axis[0] > kStickThreshold) p.mask |= bit(RETRO_DEVICE_ID_JOYPAD_RIGHT);
            if (p.axis[1] < -kStickThreshold) p.mask |= bit(RETRO_DEVICE_ID_JOYPAD_UP);
            if (p.axis[1] > kStickThreshold) p.mask |= bit(RETRO_DEVICE_ID_JOYPAD_DOWN);
        }
        core_.addRawPad((unsigned)i, p.mask);
    }

    // Keys the overlay is holding down (a mouse click or a gamepad press in the overlay).
    core_.addControls(overlay.port > 1 ? 0u : overlay.port, overlay.held());
}

int16_t InputManager::state(unsigned port, unsigned device, unsigned index, unsigned id) const {
    device &= RETRO_DEVICE_MASK;
    switch (device) {
    case RETRO_DEVICE_JOYPAD:
    case RETRO_DEVICE_KEYBOARD:
        return core_.state(port, device, index, id);
    case RETRO_DEVICE_ANALOG: {
        if (port >= pads_.size()) return 0;
        const Pad& p = pads_[port];
        if (index == RETRO_DEVICE_INDEX_ANALOG_LEFT)
            return id == RETRO_DEVICE_ID_ANALOG_X ? p.axis[0] : id == RETRO_DEVICE_ID_ANALOG_Y ? p.axis[1] : 0;
        if (index == RETRO_DEVICE_INDEX_ANALOG_RIGHT)
            return id == RETRO_DEVICE_ID_ANALOG_X ? p.axis[2] : id == RETRO_DEVICE_ID_ANALOG_Y ? p.axis[3] : 0;
        return 0;
    }
    default:
        return 0;
    }
}

}  // namespace vj
