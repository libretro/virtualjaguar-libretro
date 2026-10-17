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
    if (cfg_) settings_ = DeviceSettings::load(*cfg_, content_);
    kbProfile_ = store_->resolve(DeviceClass::Keyboard, "", content_);
    syncHoldInputs(kKeyboardDev, kbProfile_);
    for (Pad& p : pads_) {
        p.profile = store_->resolve(DeviceClass::Gamepad, p.guid, content_, p.caps);
        syncHoldInputs((uint32_t)p.id, p.profile);
    }
}

void InputManager::syncHoldInputs(uint32_t dev, const Profile& p) {
    std::vector<Binding> v;
    for (int c = 0; c < C_COUNT; c++)
        for (const Binding& b : p.binds[(size_t)c])
            if (b.hold) v.push_back(b);
    holds_.setHoldInputs(dev, v);
}

bool InputManager::padAllowed(SDL_JoystickID id) const {
    if (padFilter_ < 0) return true;
    if (padFilter_ == 0) return false;
    int n = 0;
    SDL_JoystickID* ids = SDL_GetGamepads(&n);
    bool ok = false;
    for (int i = 0; ids && i < n; i++)
        if (ids[i] == id) ok = (i + 1 == padFilter_);
    SDL_free(ids);
    return ok;
}

void InputManager::openPad(SDL_JoystickID id) {
    for (const Pad& q : pads_)
        if (q.id == id) return;
    SDL_Gamepad* g = SDL_OpenGamepad(id);
    if (!g) return;
    Pad p;
    p.handle = g;
    p.id = id;
    char buf[64] = {0};
    SDL_GUIDToString(SDL_GetJoystickGUIDForID(p.id), buf, sizeof buf);
    p.guid = buf;
    p.caps.hasMisc1 = SDL_GamepadHasButton(g, SDL_GAMEPAD_BUTTON_MISC1);
    if (store_) {
        p.profile = store_->resolve(DeviceClass::Gamepad, p.guid, content_, p.caps);
        syncHoldInputs((uint32_t)p.id, p.profile);
    }
    pads_.push_back(p);
}

void InputManager::setPadFilter(int f) {
    padFilter_ = f < 0 ? -1 : f;
    // SDL drops controller input while the window is in the background.  A window that is pinned to
    // one controller (two windows side by side, one player each) must keep playing while the other
    // window has focus; with "all" nothing changes from the SDL default.
    if (padFilter_ > 0) SDL_SetHint(SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS, "1");
    else SDL_ResetHint(SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS);
    // Re-open from scratch in enumeration order, so "controller 2" means the same pad every time.
    for (Pad& p : pads_) {
        if (p.handle) SDL_CloseGamepad(p.handle);
        holds_.forget((uint32_t)p.id);
        guard_.forget((uint32_t)p.id);
    }
    pads_.clear();
    int n = 0;
    SDL_JoystickID* ids = SDL_GetGamepads(&n);
    for (int i = 0; ids && i < n; i++)
        if (padFilter_ < 0 || padFilter_ == i + 1) openPad(ids[i]);
    SDL_free(ids);
}

std::vector<std::string> InputManager::connectedPadNames() {
    std::vector<std::string> out;
    int n = 0;
    SDL_JoystickID* ids = SDL_GetGamepads(&n);
    for (int i = 0; ids && i < n; i++) {
        const char* s = SDL_GetGamepadNameForID(ids[i]);
        out.push_back(s ? s : "Gamepad");
    }
    SDL_free(ids);
    return out;
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
    if (e.type == SDL_EVENT_GAMEPAD_BUTTON_DOWN) {
        controls.lastInputWasPad = true;
        // The controls-overlay key / button: event driven, so it also works while paused.
        for (const Pad& p : pads_)
            if (p.id == e.gbutton.which) {
                // (hold bindings fire only through the HoldArbiter)
                for (const Binding& b : p.profile.binds[C_HELP])
                    if (b.kind == Binding::PadButton && !b.hold && b.code == (int)e.gbutton.button) helpToggle_ = true;
                for (const Binding& b : p.profile.binds[C_TVMENU])
                    if (b.kind == Binding::PadButton && !b.hold && b.code == (int)e.gbutton.button) tvMenu_ = true;
            }
    } else if (e.type == SDL_EVENT_KEY_DOWN && !e.key.repeat) {
        controls.lastInputWasPad = false;
        if (!uiWantsKeyboard && !(e.key.mod & (SDL_KMOD_CTRL | SDL_KMOD_ALT | SDL_KMOD_GUI))) {
            for (const Binding& b : kbProfile_.binds[C_HELP])
                if (b.kind == Binding::Key && !b.hold && b.code == (int)e.key.scancode) helpToggle_ = true;
            for (const Binding& b : kbProfile_.binds[C_TVMENU])
                if (b.kind == Binding::Key && !b.hold && b.code == (int)e.key.scancode) tvMenu_ = true;
        }
    }
    // ---- hiding the controls overlay: a click / tap on it or the picture, or Esc -----------------
    // (a click on ImGui itself - a menu, a window - is the UI's, not ours).  The press that hides it must
    // not also reach the game (light gun / mouse), so the pointer buttons are masked until it is released.
    bool dismissedByPointer = false;
    if (controls.show) {
        if (e.type == SDL_EVENT_KEY_DOWN && !e.key.repeat && e.key.scancode == SDL_SCANCODE_ESCAPE && !uiWantsKeyboard) {
            overlayEvent(controls, OverlayEvent::Escape);
        } else if (e.type == SDL_EVENT_FINGER_DOWN) {
            dismissedByPointer = overlayEvent(controls, OverlayEvent::Tap);
        } else if (e.type == SDL_EVENT_MOUSE_BUTTON_DOWN && !uiWantsMouse_) {
            dismissedByPointer = overlayEvent(controls, OverlayEvent::Click);
        }
        if (dismissedByPointer) swallowPointer_ = true;
    }
    // ---- the pointer (devices.h): never consumed, the UI needs the same events ------------------
    if (dismissedByPointer) {
        // nothing else: the dismissing click does not also take a released mouse back
    } else if (e.type == SDL_EVENT_MOUSE_MOTION) {
        mouseX_ = e.motion.x;
        mouseY_ = e.motion.y;
        mouseInWindow_ = true;
        if (relative_) {
            accDx_ += e.motion.xrel;
            accDy_ += e.motion.yrel;
        }
    } else if (e.type == SDL_EVENT_WINDOW_MOUSE_LEAVE) {
        mouseInWindow_ = false;
    } else if (e.type == SDL_EVENT_MOUSE_BUTTON_DOWN && e.button.button == SDL_BUTTON_LEFT && userReleased_ &&
               !uiWantsMouse_ && pic_.valid() && e.button.x >= pic_.x && e.button.x < pic_.x + pic_.w &&
               e.button.y >= pic_.y && e.button.y < pic_.y + pic_.h) {
        userReleased_ = false;   // a click on the picture takes the mouse back
    } else if (e.type == SDL_EVENT_KEY_DOWN && !e.key.repeat && mousePortKind() != DevKind::Pad) {
        if (e.key.scancode == SDL_SCANCODE_F10 && devWantsRelativeMouse(mousePortKind())) {
            userReleased_ = relative_;   // F10 toggles: release while captured, take back otherwise
            if (notify) notify(userReleased_ ? "Mouse released (click the picture or press F10 to capture it again)"
                                             : "Mouse captured (Esc or F10 releases it)");
            return true;
        }
        if (e.key.scancode == SDL_SCANCODE_ESCAPE && relative_) {
            userReleased_ = true;
            if (notify) notify("Mouse released (click the picture or press F10 to capture it again)");
        }
    }
    if (e.type == SDL_EVENT_GAMEPAD_ADDED) {
        if (padAllowed(e.gdevice.which)) openPad(e.gdevice.which);
        return true;
    }
    if (e.type == SDL_EVENT_GAMEPAD_REMOVED) {
        for (size_t i = 0; i < pads_.size(); i++) {
            if (pads_[i].id == e.gdevice.which) {
                holds_.forget((uint32_t)pads_[i].id);
                guard_.forget((uint32_t)pads_[i].id);
                SDL_CloseGamepad(pads_[i].handle);
                pads_.erase(pads_.begin() + (long)i);
                break;
            }
        }
        return true;
    }
    return false;
}

void InputManager::feedHoldEdges(const SDL_Event& e) {
    // Losing focus drops the key-up / button-up with it: nothing may stay held, and nothing fires.
    if (e.type == SDL_EVENT_WINDOW_FOCUS_LOST) {
        holds_.clear();
        tvMenu_ = false;
        return;
    }
    const uint64_t ms = SDL_GetTicks();   // one clock for both edges and tickHolds()
    // Only DOWN edges are gated (by a bind capture: the press being bound is the binding's, not a hold); an UP edge
    // is always fed, or an input pressed before the capture and let go during it would stay down forever.
    if (e.type == SDL_EVENT_GAMEPAD_BUTTON_DOWN) {
        if (!capture_.active()) holds_.press((uint32_t)e.gbutton.which, Binding::button((int)e.gbutton.button), ms);
    } else if (e.type == SDL_EVENT_GAMEPAD_BUTTON_UP) {
        holds_.release((uint32_t)e.gbutton.which, Binding::button((int)e.gbutton.button), ms);
    } else if (e.type == SDL_EVENT_KEY_DOWN && !e.key.repeat) {
        if (!capture_.active() && !uiWantsKeyboard && !(e.key.mod & (SDL_KMOD_CTRL | SDL_KMOD_ALT | SDL_KMOD_GUI)))
            holds_.press(kKeyboardDev, Binding::key((int)e.key.scancode), ms);
    } else if (e.type == SDL_EVENT_KEY_UP) {
        // Always: a key that went down before a text box or a chord took the keyboard must still come up.
        holds_.release(kKeyboardDev, Binding::key((int)e.key.scancode), ms);
    }
}

void InputManager::tickHolds(uint64_t ms, bool live) {
    holds_.setLive(live);
    if (holds_.takeFires(ms) > 0) tvMenu_ = true;
}

static void snapshotPad(SDL_Gamepad* g, DeviceSnapshot& s) {
    for (int b = 0; b < SDL_GAMEPAD_BUTTON_COUNT; b++) s.btn[(size_t)b] = SDL_GetGamepadButton(g, (SDL_GamepadButton)b);
    for (int a = 0; a < SDL_GAMEPAD_AXIS_COUNT; a++) s.axis[(size_t)a] = SDL_GetGamepadAxis(g, (SDL_GamepadAxis)a);
}

void InputManager::armReleaseGuard() {
    DeviceSnapshot kb;
    int n = 0;
    const bool* ks = SDL_GetKeyboardState(&n);
    for (int sc = 0; ks && sc < n && sc < (int)kb.key.size(); sc++) kb.key[(size_t)sc] = ks[sc];
    guard_.arm(kKeyboardDev, kb);
    for (const Pad& p : pads_) {
        DeviceSnapshot s;
        snapshotPad(p.handle, s);
        guard_.arm((uint32_t)p.id, s);
    }
    // After the guard has the raw state: whatever is still down is the guard's now, so its release must not count
    // as a tap or a hold for the arbiter.
    holds_.clear();
}

void InputManager::clearHolds() {
    holds_.clear();
    tvMenu_ = false;
    armReleaseGuard();
}

std::vector<SDL_Gamepad*> InputManager::padHandles() const {
    std::vector<SDL_Gamepad*> v;
    for (const Pad& p : pads_) v.push_back(p.handle);
    return v;
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
        pi.hasMisc1 = pads_[i].caps.hasMisc1;
        pi.port = (unsigned)i;
        for (size_t j = 0; j < i; j++)
            if (pads_[j].guid == pads_[i].guid) pi.instance++;
        for (unsigned sl = 0; sl < kSlots; sl++)
            if (routes_.pad[sl] == (int)i) {
                pi.slot = (int)sl;
                break;
            }
        v.push_back(pi);
    }
    return v;
}

std::array<SlotSource, kSlots> InputManager::slotConfig() const {
    std::array<SlotSource, kSlots> a;
    if (!cfg_) return a;
    for (unsigned sl = 0; sl < kSlots; sl++) a[sl] = resolveSlot(*cfg_, sl, content_);
    return a;
}

void InputManager::updateRoutes() {
    std::vector<std::string> guids;
    for (const Pad& p : pads_) guids.push_back(p.guid);
    unsigned kbSlot = store_ ? (unsigned)store_->keyboardPort() : 0u;
    routes_ = assignSlots(slotConfig(), activeSlots(kind_[0] == DevKind::TeamTap, kind_[1] == DevKind::TeamTap), guids, kbSlot);
}

void InputManager::setPortKinds(DevKind p1, DevKind p2) {
    kind_[0] = p1;
    kind_[1] = p2;
}

DevKind InputManager::mousePortKind() const {
    int p = resolveMousePort({{kind_[0], kind_[1]}}, settings_.mousePort);
    return p < 0 ? DevKind::Pad : kind_[p];
}

void InputManager::syncMouseCapture(SDL_Window* w, bool gameRunning, bool uiOpen, bool uiWantsMouse) {
    uiWantsMouse_ = uiWantsMouse;
    windowFocused_ = w && (SDL_GetWindowFlags(w) & SDL_WINDOW_INPUT_FOCUS) != 0;
    CaptureInputs ci;
    ci.deviceWantsMouse = devWantsRelativeMouse(mousePortKind());
    ci.windowFocused = windowFocused_;
    ci.gameRunning = gameRunning;
    ci.uiOpen = uiOpen;
    ci.userReleased = userReleased_;
    bool want = wantRelativeMouse(ci);
    if (want == relative_ || !w) return;
    if (!SDL_SetWindowRelativeMouseMode(w, want)) return;
    relative_ = want;
    accDx_ = accDy_ = 0;
    if (want && notify) notify("Mouse captured (Esc or F10 releases it)");
}

void InputManager::beginCapture(const LayerId& layer, Ctl ctl, bool add, bool hold) {
    capLayer_ = layer;
    capCtl_ = ctl;
    capAdd_ = add;
    capHold_ = hold;
    capture_.begin(layer.cls);
    for (const Pad& p : pads_)
        for (int a = 0; a < SDL_GAMEPAD_AXIS_COUNT; a++)
            capture_.setBaseline(p.id, a, SDL_GetGamepadAxis(p.handle, (SDL_GamepadAxis)a));
}

void InputManager::bindCaptured(const Binding& captured) {
    if (!store_ || capCtl_ >= C_COUNT) return;
    Binding b = captured;
    if (capHold_ && (b.kind == Binding::Key || b.kind == Binding::PadButton)) b = b.withHold();   // axes cannot be held
    std::vector<Binding> v;
    if (capAdd_) {
        // Start from what is in effect now, so "add" extends rather than shadows.
        if (store_->defined(capLayer_, capCtl_)) {
            v = store_->get(capLayer_, capCtl_);
        } else {
            // A controller's own layer seeds from what that pad would get (the TV menu's Share default
            // is Guide on a pad without Share); the shared layer seeds from the default caps and
            // resolve() maps the Share default per pad.
            PadCaps caps;
            for (const Pad& p : pads_)
                if (!capLayer_.guid.empty() && p.guid == capLayer_.guid) {
                    caps = p.caps;
                    break;
                }
            v = store_->resolve(capLayer_.cls, capLayer_.guid, capLayer_.gameId, caps).binds[capCtl_];
        }
    }
    if (std::find(v.begin(), v.end(), b) == v.end()) v.push_back(b);
    store_->set(capLayer_, capCtl_, v);
    if (cfg_) cfg_->save();
    reresolve();
}

void InputManager::poll(bool captureKeyboard) {
    core_.beginFrame();
    overlay.tick();
    // While a binding is being captured nothing reaches the core: the press being bound
    // must not also act in the game (binding Start would pause it, for one).
    if (capture_.active()) return;
    // The TV menu has the input: the core sees neutral everything, and a relative-mouse delta gathered meanwhile does
    // not arrive as one jump when the menu closes.
    if (menuOwns_) {
        accDx_ = accDy_ = 0;
        return;
    }

    // ---- keyboard ---------------------------------------------------------------------
    HostSource kb;
    kb.isKeyboard = true;
    kb.overlaySlot = 7;
    kb.profile = &kbProfile_;
    if (captureKeyboard) {
        int n = 0;
        const bool* ks = SDL_GetKeyboardState(&n);
        SDL_Keymod mods = SDL_GetModState();
        // A held Ctrl/Alt/Cmd chord is an app shortcut, not Jaguar input.
        if (ks && !(mods & (SDL_KMOD_CTRL | SDL_KMOD_ALT | SDL_KMOD_GUI))) {
            for (int sc = 0; sc < n && sc < (int)kb.snap.key.size(); sc++) {
                if (!ks[sc]) continue;
                kb.snap.key[(size_t)sc] = true;
                unsigned rk = retroKey((SDL_Scancode)sc);
                if (rk && !holds_.withholds(kKeyboardDev, Binding::key(sc))) core_.addRawKey(rk);   // truthful, except the keys only the keypad synthesis may press and a hold input being timed
            }
            holds_.apply(kKeyboardDev, kb.snap);
            guard_.apply(kKeyboardDev, kb.snap);
        }
    }
    kbProfile_.stickAsDpad = false;

    updateRoutes();

    // ---- gamepads ----------------------------------------------------------------------
    std::vector<HostSource> padSrc(pads_.size());
    for (size_t i = 0; i < pads_.size(); i++) {
        Pad& p = pads_[i];
        SDL_Gamepad* g = p.handle;
        HostSource& hs = padSrc[i];
        hs.overlaySlot = (unsigned)i < 8 ? (unsigned)i : 7u;
        // The left stick is the analog axis on a stick-device port: no D-pad from it there (devices.h).
        bool stickDpad = stickAsDpad;
        for (unsigned sl = 0; sl < kSlots; sl++)
            if (routes_.pad[sl] == (int)i && slotSocket(sl) == 0)
                stickDpad = stickDrivesDpad(kind_[slotJaguarPort(sl)], stickDpad);
        p.profile.stickAsDpad = stickDpad;
        hs.profile = &p.profile;
        snapshotPad(g, hs.snap);
        holds_.apply((uint32_t)p.id, hs.snap);
        guard_.apply((uint32_t)p.id, hs.snap);
    }

    // ---- who plays what ------------------------------------------------------------------
    RouterConfig rc;
    rc.kind = {{kind_[0], kind_[1]}};
    rc.settings = settings_;
    rc.pic = pic_;
    rc.mousePort = resolveMousePort(rc.kind, settings_.mousePort);
    rc.routes = routes_;

    // ---- the pointer ---------------------------------------------------------------------
    // Motion is handed to the first emulated frame of a UI frame only: stepEmulation() may run
    // several frames per loop and the same delta must not be played twice.
    PointerInput pi;
    float mx = 0, my = 0;
    SDL_MouseButtonFlags mb = SDL_GetMouseState(&mx, &my);
    const bool ptrUsable = relative_ || (windowFocused_ && mouseInWindow_ && !uiWantsMouse_);
    if (ptrUsable) {
        if (mb & SDL_BUTTON_LMASK) pi.buttons |= 1u;
        if (mb & SDL_BUTTON_RMASK) pi.buttons |= 2u;
        if (mb & SDL_BUTTON_MMASK) pi.buttons |= 4u;
    }
    if (swallowPointer_) {   // the press that hid the controls overlay stays out of the game until it is let go
        if (mb & (SDL_BUTTON_LMASK | SDL_BUTTON_RMASK | SDL_BUTTON_MMASK)) pi.buttons = 0;
        else swallowPointer_ = false;
    }
    if (relative_) {
        pi.dx = accDx_;
        pi.dy = accDy_;
        accDx_ = accDy_ = 0;
    }
    // The light gun aims with the absolute position, which a captured (relative) mouse does not have.
    pi.havePos = !relative_ && windowFocused_ && mouseInWindow_ && !uiWantsMouse_;
    pi.x = mx;
    pi.y = my;

    router_.route(rc, padSrc, &kb, pi, &overlay, core_);   // (the controls-overlay key is handled in handleEvent)
    holds_.frame();   // tap pulses count emulated frames

    // Keys the overlay is holding down (a mouse click or a gamepad press in the overlay).
    core_.addControls(overlay.port > 1 ? 0u : overlay.port, overlay.held());
    if (agentControls.any()) core_.addControls(0, agentControls);
}

int16_t InputManager::state(unsigned port, unsigned device, unsigned index, unsigned id) const {
    return core_.state(port, device, index, id);
}

}  // namespace vj
