#include "remap.h"

#include "libretro.h"
#include "paths.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace vj {

// ---------------------------------------------------------------------------------
// controls
// ---------------------------------------------------------------------------------
static const char* const kCtlId[C_COUNT] = {
    "up", "down", "left", "right", "a", "b", "c", "option", "pause",
    "k0", "k1", "k2", "k3", "k4", "k5", "k6", "k7", "k8", "k9", "star", "hash", "overlay"};
static const char* const kCtlLabel[C_COUNT] = {
    "D-pad Up", "D-pad Down", "D-pad Left", "D-pad Right", "A", "B", "C", "Option", "Pause",
    "Keypad 0", "Keypad 1", "Keypad 2", "Keypad 3", "Keypad 4", "Keypad 5", "Keypad 6", "Keypad 7",
    "Keypad 8", "Keypad 9", "Keypad *", "Keypad #", "Keypad overlay"};

const char* ctlId(Ctl c) { return c < C_COUNT ? kCtlId[c] : ""; }
const char* ctlLabel(Ctl c) { return c < C_COUNT ? kCtlLabel[c] : ""; }

bool ctlFromId(const std::string& id, Ctl& out) {
    for (int i = 0; i < C_COUNT; i++) {
        if (id == kCtlId[i]) {
            out = (Ctl)i;
            return true;
        }
    }
    return false;
}

// ---------------------------------------------------------------------------------
// bindings
// ---------------------------------------------------------------------------------
std::string Binding::serialize() const {
    switch (kind) {
    case Key: return "k" + std::to_string(code);
    case PadButton: {
        const char* s = SDL_GetGamepadStringForButton((SDL_GamepadButton)code);
        return s ? std::string("b") + s : std::string();
    }
    case PadAxis: {
        const char* s = SDL_GetGamepadStringForAxis((SDL_GamepadAxis)code);
        return s ? std::string("a") + (dir < 0 ? "-" : "+") + s : std::string();
    }
    default: return std::string();
    }
}

bool Binding::parse(const std::string& s, Binding& out) {
    if (s.size() < 2) return false;
    if (s[0] == 'k') {
        char* end = nullptr;
        long v = std::strtol(s.c_str() + 1, &end, 10);
        if (!end || *end != '\0' || v <= 0 || v >= SDL_SCANCODE_COUNT) return false;
        out = Binding::key((int)v);
        return true;
    }
    if (s[0] == 'b') {
        SDL_GamepadButton b = SDL_GetGamepadButtonFromString(s.c_str() + 1);
        if (b == SDL_GAMEPAD_BUTTON_INVALID) return false;
        out = Binding::button((int)b);
        return true;
    }
    if (s[0] == 'a' && s.size() > 2 && (s[1] == '+' || s[1] == '-')) {
        SDL_GamepadAxis a = SDL_GetGamepadAxisFromString(s.c_str() + 2);
        if (a == SDL_GAMEPAD_AXIS_INVALID) return false;
        out = Binding::axis((int)a, s[1] == '-' ? -1 : 1);
        return true;
    }
    return false;
}

static const char* buttonLabel(int b) {
    switch ((SDL_GamepadButton)b) {
    case SDL_GAMEPAD_BUTTON_SOUTH: return "South (A / Cross)";
    case SDL_GAMEPAD_BUTTON_EAST: return "East (B / Circle)";
    case SDL_GAMEPAD_BUTTON_WEST: return "West (X / Square)";
    case SDL_GAMEPAD_BUTTON_NORTH: return "North (Y / Triangle)";
    case SDL_GAMEPAD_BUTTON_BACK: return "Back / Select";
    case SDL_GAMEPAD_BUTTON_GUIDE: return "Guide / Home";
    case SDL_GAMEPAD_BUTTON_START: return "Start";
    case SDL_GAMEPAD_BUTTON_LEFT_STICK: return "Left Stick Click";
    case SDL_GAMEPAD_BUTTON_RIGHT_STICK: return "Right Stick Click";
    case SDL_GAMEPAD_BUTTON_LEFT_SHOULDER: return "Left Shoulder";
    case SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER: return "Right Shoulder";
    case SDL_GAMEPAD_BUTTON_DPAD_UP: return "D-pad Up";
    case SDL_GAMEPAD_BUTTON_DPAD_DOWN: return "D-pad Down";
    case SDL_GAMEPAD_BUTTON_DPAD_LEFT: return "D-pad Left";
    case SDL_GAMEPAD_BUTTON_DPAD_RIGHT: return "D-pad Right";
    case SDL_GAMEPAD_BUTTON_MISC1: return "Share / Capture";
    case SDL_GAMEPAD_BUTTON_TOUCHPAD: return "Touchpad";
    default: return nullptr;
    }
}

static const char* axisLabel(int a, int dir) {
    switch ((SDL_GamepadAxis)a) {
    case SDL_GAMEPAD_AXIS_LEFTX: return dir < 0 ? "Left Stick Left" : "Left Stick Right";
    case SDL_GAMEPAD_AXIS_LEFTY: return dir < 0 ? "Left Stick Up" : "Left Stick Down";
    case SDL_GAMEPAD_AXIS_RIGHTX: return dir < 0 ? "Right Stick Left" : "Right Stick Right";
    case SDL_GAMEPAD_AXIS_RIGHTY: return dir < 0 ? "Right Stick Up" : "Right Stick Down";
    case SDL_GAMEPAD_AXIS_LEFT_TRIGGER: return "Left Trigger";
    case SDL_GAMEPAD_AXIS_RIGHT_TRIGGER: return "Right Trigger";
    default: return nullptr;
    }
}

std::string Binding::display() const {
    switch (kind) {
    case Key: {
        const char* n = SDL_GetScancodeName((SDL_Scancode)code);
        return (n && *n) ? n : "Key " + std::to_string(code);
    }
    case PadButton: {
        const char* n = buttonLabel(code);
        return n ? n : "Button " + std::to_string(code);
    }
    case PadAxis: {
        const char* n = axisLabel(code, dir);
        return n ? n : "Axis " + std::to_string(code) + (dir < 0 ? "-" : "+");
    }
    default: return "";
    }
}

std::string serializeBindings(const std::vector<Binding>& v) {
    std::string out;
    for (const Binding& b : v) {
        std::string s = b.serialize();
        if (s.empty()) continue;
        if (!out.empty()) out += ',';
        out += s;
    }
    return out.empty() ? "none" : out;
}

std::vector<Binding> parseBindings(const std::string& s, bool* ok) {
    std::vector<Binding> out;
    bool good = true;
    if (s != "none") {
        size_t pos = 0;
        while (pos <= s.size()) {
            size_t comma = s.find(',', pos);
            std::string tok = s.substr(pos, comma == std::string::npos ? std::string::npos : comma - pos);
            if (!tok.empty()) {
                Binding b;
                if (Binding::parse(tok, b)) {
                    if (std::find(out.begin(), out.end(), b) == out.end()) out.push_back(b);
                } else {
                    good = false;
                }
            }
            if (comma == std::string::npos) break;
            pos = comma + 1;
        }
    }
    if (ok) *ok = good;
    return out;
}

// ---------------------------------------------------------------------------------
// profiles
// ---------------------------------------------------------------------------------
const char* sourceLabel(Source s) {
    switch (s) {
    case SRC_DEFAULT: return "default";
    case SRC_GLOBAL: return "global";
    case SRC_GLOBAL_PAD: return "global, this controller";
    case SRC_GAME: return "this game";
    case SRC_GAME_PAD: return "this game, this controller";
    }
    return "";
}

static void add(Profile& p, Ctl c, Binding b) { p.binds[c].push_back(b); }

Profile Profile::builtin(DeviceClass cls) {
    Profile p;
    p.source.fill(SRC_DEFAULT);
    if (cls == DeviceClass::Keyboard) {
        // The layout the app shipped with (arrows, Z/X/C, Enter, Right Shift or
        // Backspace, number row = keypad), plus the numpad and F2 for the overlay.
        add(p, C_UP, Binding::key(SDL_SCANCODE_UP));
        add(p, C_DOWN, Binding::key(SDL_SCANCODE_DOWN));
        add(p, C_LEFT, Binding::key(SDL_SCANCODE_LEFT));
        add(p, C_RIGHT, Binding::key(SDL_SCANCODE_RIGHT));
        add(p, C_A, Binding::key(SDL_SCANCODE_X));
        add(p, C_B, Binding::key(SDL_SCANCODE_Z));
        add(p, C_C, Binding::key(SDL_SCANCODE_C));
        add(p, C_OPTION, Binding::key(SDL_SCANCODE_RETURN));
        add(p, C_PAUSE, Binding::key(SDL_SCANCODE_RSHIFT));
        add(p, C_PAUSE, Binding::key(SDL_SCANCODE_BACKSPACE));
        for (int d = 1; d <= 9; d++) {
            add(p, (Ctl)(C_K0 + d), Binding::key(SDL_SCANCODE_1 + (d - 1)));
            add(p, (Ctl)(C_K0 + d), Binding::key(SDL_SCANCODE_KP_1 + (d - 1)));
        }
        add(p, C_K0, Binding::key(SDL_SCANCODE_0));
        add(p, C_K0, Binding::key(SDL_SCANCODE_KP_0));
        add(p, C_STAR, Binding::key(SDL_SCANCODE_MINUS));
        add(p, C_STAR, Binding::key(SDL_SCANCODE_KP_DIVIDE));
        add(p, C_HASH, Binding::key(SDL_SCANCODE_EQUALS));
        add(p, C_HASH, Binding::key(SDL_SCANCODE_KP_MULTIPLY));
        add(p, C_OVERLAY, Binding::key(SDL_SCANCODE_F2));
    } else {
        // SDL's standard gamepad layout.  Chosen so that every keypad key is on the pad
        // without opening the overlay: 0-6 sit where the core's own RetroPad defaults
        // put them (X, L1, R1, L2, R2, L3, R3); the right stick (dominant axis only)
        // carries 7 / 8 / 9 / *, and Guide carries #.  Guide is sometimes claimed by the
        // OS, so the overlay (Share/Capture, or F2 / View menu) is the way to reach # then.
        add(p, C_UP, Binding::button(SDL_GAMEPAD_BUTTON_DPAD_UP));
        add(p, C_DOWN, Binding::button(SDL_GAMEPAD_BUTTON_DPAD_DOWN));
        add(p, C_LEFT, Binding::button(SDL_GAMEPAD_BUTTON_DPAD_LEFT));
        add(p, C_RIGHT, Binding::button(SDL_GAMEPAD_BUTTON_DPAD_RIGHT));
        add(p, C_A, Binding::button(SDL_GAMEPAD_BUTTON_EAST));
        add(p, C_B, Binding::button(SDL_GAMEPAD_BUTTON_SOUTH));
        add(p, C_C, Binding::button(SDL_GAMEPAD_BUTTON_WEST));
        add(p, C_OPTION, Binding::button(SDL_GAMEPAD_BUTTON_START));
        add(p, C_PAUSE, Binding::button(SDL_GAMEPAD_BUTTON_BACK));
        add(p, C_K0, Binding::button(SDL_GAMEPAD_BUTTON_NORTH));
        add(p, C_K1, Binding::button(SDL_GAMEPAD_BUTTON_LEFT_SHOULDER));
        add(p, C_K2, Binding::button(SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER));
        add(p, C_K3, Binding::axis(SDL_GAMEPAD_AXIS_LEFT_TRIGGER, 1));
        add(p, C_K4, Binding::axis(SDL_GAMEPAD_AXIS_RIGHT_TRIGGER, 1));
        add(p, C_K5, Binding::button(SDL_GAMEPAD_BUTTON_LEFT_STICK));
        add(p, C_K6, Binding::button(SDL_GAMEPAD_BUTTON_RIGHT_STICK));
        add(p, C_K7, Binding::axis(SDL_GAMEPAD_AXIS_RIGHTX, -1));
        add(p, C_K8, Binding::axis(SDL_GAMEPAD_AXIS_RIGHTY, -1));
        add(p, C_K9, Binding::axis(SDL_GAMEPAD_AXIS_RIGHTX, 1));
        add(p, C_STAR, Binding::axis(SDL_GAMEPAD_AXIS_RIGHTY, 1));
        add(p, C_HASH, Binding::button(SDL_GAMEPAD_BUTTON_GUIDE));
        add(p, C_OVERLAY, Binding::button(SDL_GAMEPAD_BUTTON_MISC1));
    }
    return p;
}

// ---------------------------------------------------------------------------------
// store
// ---------------------------------------------------------------------------------
std::string BindingStore::prefix(const LayerId& l) {
    std::string p = "input.";
    if (!l.gameId.empty()) p += "game." + l.gameId + ".";
    if (l.cls == DeviceClass::Keyboard) p += "kb.";
    else if (l.guid.empty()) p += "pad.";
    else p += "padguid." + l.guid + ".";
    return p;
}

bool BindingStore::defined(const LayerId& l, Ctl c) const {
    std::string k = prefix(l) + ctlId(c);
    if (!cfg_->has(k)) return false;
    std::string v = cfg_->get(k);
    if (v == "none") return true;
    bool ok = true;
    return !parseBindings(v, &ok).empty();
}

std::vector<Binding> BindingStore::get(const LayerId& l, Ctl c) const {
    return parseBindings(cfg_->get(prefix(l) + ctlId(c)));
}

void BindingStore::set(const LayerId& l, Ctl c, const std::vector<Binding>& v) {
    cfg_->set(prefix(l) + ctlId(c), serializeBindings(v));
}

void BindingStore::clear(const LayerId& l, Ctl c) { cfg_->erase(prefix(l) + ctlId(c)); }

void BindingStore::clearLayer(const LayerId& l) {
    for (int i = 0; i < C_COUNT; i++) clear(l, (Ctl)i);
}

int BindingStore::keyboardPort() const { return cfg_->getInt("input.kb.port", 0) == 1 ? 1 : 0; }
void BindingStore::setKeyboardPort(int p) { cfg_->setInt("input.kb.port", p == 1 ? 1 : 0); }

Profile BindingStore::resolve(DeviceClass cls, const std::string& guid, const std::string& gameId) const {
    Profile p = Profile::builtin(cls);
    struct Cand { LayerId id; Source src; };
    std::vector<Cand> cands;
    const bool pad = cls == DeviceClass::Gamepad;
    if (!gameId.empty()) {
        if (pad && !guid.empty()) cands.push_back({LayerId{gameId, cls, guid}, SRC_GAME_PAD});
        cands.push_back({LayerId{gameId, cls, ""}, SRC_GAME});
    }
    if (pad && !guid.empty()) cands.push_back({LayerId{"", cls, guid}, SRC_GLOBAL_PAD});
    cands.push_back({LayerId{"", cls, ""}, SRC_GLOBAL});
    for (int i = 0; i < C_COUNT; i++) {
        for (const Cand& c : cands) {
            if (defined(c.id, (Ctl)i)) {
                p.binds[(size_t)i] = get(c.id, (Ctl)i);
                p.source[(size_t)i] = c.src;
                break;
            }
        }
    }
    return p;
}

// ---------------------------------------------------------------------------------
// snapshot -> controls
// ---------------------------------------------------------------------------------
static int axisPartner(int a) {
    switch (a) {
    case SDL_GAMEPAD_AXIS_LEFTX: return SDL_GAMEPAD_AXIS_LEFTY;
    case SDL_GAMEPAD_AXIS_LEFTY: return SDL_GAMEPAD_AXIS_LEFTX;
    case SDL_GAMEPAD_AXIS_RIGHTX: return SDL_GAMEPAD_AXIS_RIGHTY;
    case SDL_GAMEPAD_AXIS_RIGHTY: return SDL_GAMEPAD_AXIS_RIGHTX;
    default: return -1;
    }
}

static bool bindingDown(const Binding& b, const DeviceSnapshot& s, bool dominant) {
    switch (b.kind) {
    case Binding::Key:
        return b.code > 0 && b.code < (int)s.key.size() && s.key[(size_t)b.code];
    case Binding::PadButton:
        return b.code >= 0 && b.code < (int)s.btn.size() && s.btn[(size_t)b.code];
    case Binding::PadAxis: {
        if (b.code < 0 || b.code >= (int)s.axis.size()) return false;
        int v = s.axis[(size_t)b.code];
        bool trigger = b.code == SDL_GAMEPAD_AXIS_LEFT_TRIGGER || b.code == SDL_GAMEPAD_AXIS_RIGHT_TRIGGER;
        int thr = trigger ? kTriggerThreshold : kStickThreshold;
        if (b.dir > 0 ? v <= thr : v >= -thr) return false;
        if (dominant) {
            int partner = axisPartner(b.code);
            if (partner >= 0 && std::abs((int)s.axis[(size_t)partner]) > std::abs(v)) return false;
        }
        return true;
    }
    default: return false;
    }
}

ControlSet evaluate(const Profile& p, const DeviceSnapshot& s) {
    ControlSet out;
    for (int i = 0; i < C_COUNT; i++) {
        bool dominant = isKeypad((Ctl)i);
        for (const Binding& b : p.binds[(size_t)i]) {
            if (bindingDown(b, s, dominant)) {
                out.set((Ctl)i);
                break;
            }
        }
    }
    if (p.stickAsDpad) {
        int x = s.axis[SDL_GAMEPAD_AXIS_LEFTX], y = s.axis[SDL_GAMEPAD_AXIS_LEFTY];
        if (x < -kStickThreshold) out.set(C_LEFT);
        if (x > kStickThreshold) out.set(C_RIGHT);
        if (y < -kStickThreshold) out.set(C_UP);
        if (y > kStickThreshold) out.set(C_DOWN);
    }
    return out;
}

// ---------------------------------------------------------------------------------
// controls -> core input
// ---------------------------------------------------------------------------------
int CoreInput::padId(Ctl c) {
    switch (c) {
    case C_UP: return RETRO_DEVICE_ID_JOYPAD_UP;
    case C_DOWN: return RETRO_DEVICE_ID_JOYPAD_DOWN;
    case C_LEFT: return RETRO_DEVICE_ID_JOYPAD_LEFT;
    case C_RIGHT: return RETRO_DEVICE_ID_JOYPAD_RIGHT;
    case C_A: return RETRO_DEVICE_ID_JOYPAD_A;
    case C_B: return RETRO_DEVICE_ID_JOYPAD_B;
    case C_C: return RETRO_DEVICE_ID_JOYPAD_Y;        // the core's Jaguar C is the RetroPad Y
    case C_OPTION: return RETRO_DEVICE_ID_JOYPAD_START;
    case C_PAUSE: return RETRO_DEVICE_ID_JOYPAD_SELECT;
    default: return -1;
    }
}

// libretro.c: number_keys[] / the port-1 fallbacks ('-' and '=' are * and #), and
// the port-2 fallbacks p q w e r t y u i o [ ].
unsigned CoreInput::keypadKey(unsigned port, Ctl c) {
    if (!isKeypad(c) || port > 1) return 0;
    static const unsigned p1[12] = {RETROK_0, RETROK_1, RETROK_2, RETROK_3, RETROK_4, RETROK_5,
                                    RETROK_6, RETROK_7, RETROK_8, RETROK_9, RETROK_MINUS, RETROK_EQUALS};
    static const unsigned p2[12] = {RETROK_p, RETROK_q, RETROK_w, RETROK_e, RETROK_r, RETROK_t,
                                    RETROK_y, RETROK_u, RETROK_i, RETROK_o, RETROK_LEFTBRACKET, RETROK_RIGHTBRACKET};
    return (port == 0 ? p1 : p2)[c - C_K0];
}

bool CoreInput::isCoreKeypadKey(unsigned rk) {
    for (unsigned port = 0; port < 2; port++)
        for (int c = C_K0; c <= C_HASH; c++)
            if (keypadKey(port, (Ctl)c) == rk) return true;
    return false;
}

void CoreInput::beginFrame() {
    pad_.fill(0);
    keys_.fill(false);
    synth_ = false;
}

void CoreInput::addControls(unsigned port, ControlSet c) {
    for (int i = 0; i < C_COUNT; i++) {
        if (!c.test((Ctl)i)) continue;
        int id = padId((Ctl)i);
        if (id >= 0) {
            if (port < pad_.size()) pad_[port] |= 1u << id;
        } else if (isKeypad((Ctl)i)) {
            unsigned rk = keypadKey(port, (Ctl)i);
            if (rk && rk < keys_.size()) {
                keys_[rk] = true;
                synth_ = true;
            }
        }
    }
}

void CoreInput::addRawPad(unsigned port, uint16_t mask) {
    if (port < pad_.size()) pad_[port] |= mask;
}

void CoreInput::addRawKey(unsigned rk) {
    if (rk < keys_.size() && !isCoreKeypadKey(rk)) keys_[rk] = true;
}

int16_t CoreInput::state(unsigned port, unsigned device, unsigned index, unsigned id) const {
    (void)index;
    device &= RETRO_DEVICE_MASK;
    if (device == RETRO_DEVICE_JOYPAD) {
        if (port >= pad_.size()) return 0;
        uint32_t m = pad_[port];
        if (id == RETRO_DEVICE_ID_JOYPAD_MASK) return (int16_t)m;
        return id < 16 ? (int16_t)((m >> id) & 1u) : 0;
    }
    if (device == RETRO_DEVICE_KEYBOARD) return (port == 0 && key(id)) ? 1 : 0;
    return 0;
}

// ---------------------------------------------------------------------------------
// keypad overlay
// ---------------------------------------------------------------------------------
Ctl KeypadOverlay::keyAt(int row, int col) {
    static const Ctl grid[4][3] = {{C_K1, C_K2, C_K3}, {C_K4, C_K5, C_K6}, {C_K7, C_K8, C_K9}, {C_STAR, C_K0, C_HASH}};
    row = ((row % 4) + 4) % 4;
    col = ((col % 3) + 3) % 3;
    return grid[row][col];
}

void KeypadOverlay::toggle() { open = !open; }

void KeypadOverlay::move(int dRow, int dCol) {
    row = ((row + dRow) % 4 + 4) % 4;
    col = ((col + dCol) % 3 + 3) % 3;
}

void KeypadOverlay::press() { tap(keyAt(row, col)); }

void KeypadOverlay::tap(Ctl key) {
    if (!isKeypad(key) || queue_.size() >= 32) return;
    queue_.push_back(key);
}

void KeypadOverlay::tick() {
    if (hold_ > 0) {
        if (--hold_ == 0) {
            cur_ = C_COUNT;
            gap_ = kGapFrames;
        }
        return;
    }
    if (gap_ > 0) {
        --gap_;
        return;
    }
    if (!queue_.empty()) {
        cur_ = queue_.front();
        queue_.pop_front();
        hold_ = kHoldFrames;
    }
}

ControlSet KeypadOverlay::held() const {
    ControlSet s;
    if (hold_ > 0 && cur_ < C_COUNT) s.set(cur_);
    return s;
}

ControlSet KeypadOverlay::filter(unsigned slot, ControlSet in) {
    if (slot >= prev_.size()) return in;
    ControlSet prev = prev_[slot];
    prev_[slot] = in;
    auto edge = [&](Ctl c) { return in.test(c) && !prev.test(c); };

    if (edge(C_OVERLAY)) {
        toggle();
        // Whatever is held now was meant for the other mode: hold it back until released.
        suppress_[slot] = in;
    } else if (open) {
        if (edge(C_UP)) move(-1, 0);
        if (edge(C_DOWN)) move(1, 0);
        if (edge(C_LEFT)) move(0, -1);
        if (edge(C_RIGHT)) move(0, 1);
        if (edge(C_A) || edge(C_B)) press();
        if (edge(C_C)) {
            open = false;
            suppress_[slot] = in;
        }
    }
    suppress_[slot].bits &= in.bits;   // released controls stop being held back
    if (open) return ControlSet();
    ControlSet out = in;
    out.clear(C_OVERLAY);
    out.bits &= ~suppress_[slot].bits;
    return out;
}

// ---------------------------------------------------------------------------------
// bind capture
// ---------------------------------------------------------------------------------
void BindCapture::begin(DeviceClass cls) {
    active_ = true;
    cls_ = cls;
    base_.clear();
}

void BindCapture::setBaseline(SDL_JoystickID which, int axis, int16_t value) {
    for (Base& b : base_) {
        if (b.which == which && b.axis == axis) {
            b.v = value;
            return;
        }
    }
    base_.push_back({which, axis, value});
}

BindCapture::Result BindCapture::feed(const SDL_Event& e, Binding& out) {
    if (!active_) return Pending;
    if (e.type == SDL_EVENT_KEY_DOWN) {
        if (e.key.repeat) return Pending;
        if (e.key.scancode == SDL_SCANCODE_ESCAPE) {
            active_ = false;
            return Cancelled;
        }
        if (cls_ == DeviceClass::Keyboard && e.key.scancode != SDL_SCANCODE_UNKNOWN) {
            out = Binding::key((int)e.key.scancode);
            active_ = false;
            return Captured;
        }
        return Pending;
    }
    if (cls_ != DeviceClass::Gamepad) return Pending;
    if (e.type == SDL_EVENT_GAMEPAD_BUTTON_DOWN) {
        out = Binding::button((int)e.gbutton.button);
        active_ = false;
        return Captured;
    }
    if (e.type == SDL_EVENT_GAMEPAD_AXIS_MOTION) {
        int base = 0;
        for (const Base& b : base_)
            if (b.which == e.gaxis.which && b.axis == e.gaxis.axis) base = b.v;
        int delta = (int)e.gaxis.value - base;
        bool trigger = e.gaxis.axis == SDL_GAMEPAD_AXIS_LEFT_TRIGGER || e.gaxis.axis == SDL_GAMEPAD_AXIS_RIGHT_TRIGGER;
        int need = trigger ? 12000 : 20000;
        if (std::abs(delta) >= need) {
            out = Binding::axis((int)e.gaxis.axis, delta > 0 ? 1 : -1);
            active_ = false;
            return Captured;
        }
    }
    return Pending;
}

// ---------------------------------------------------------------------------------
// content identity
// ---------------------------------------------------------------------------------
uint32_t crc32Bytes(const uint8_t* data, size_t size) {
    static uint32_t table[256];
    static bool init = false;
    if (!init) {
        for (uint32_t i = 0; i < 256; i++) {
            uint32_t c = i;
            for (int k = 0; k < 8; k++) c = (c & 1u) ? 0xEDB88320u ^ (c >> 1) : c >> 1;
            table[i] = c;
        }
        init = true;
    }
    uint32_t crc = 0xFFFFFFFFu;
    for (size_t i = 0; i < size; i++) crc = table[(crc ^ data[i]) & 0xFFu] ^ (crc >> 8);
    return crc ^ 0xFFFFFFFFu;
}

// src/core/file.c DetectPrependedHeaderSize(), reproduced so the key matches titledb's.
static uint32_t prependedHeaderSize(const uint8_t* b, size_t size) {
    const uint32_t skip = 512, markerOff = 0x400;
    if (size < skip + markerOff + 4) return 0;
    if ((size % 1048576) == 0) return 0;
    size_t payload = size - skip;
    auto marker = [&](size_t off) { return b[off] == 4 && b[off + 1] == 4 && b[off + 2] == 4 && b[off + 3] == 4; };
    if (!marker(skip + markerOff)) return 0;
    if ((payload % 1048576) == 0) return skip;
    if (payload < 1048576 && !marker(markerOff)) return skip;
    return 0;
}

std::string cartIdentity(const uint8_t* data, size_t size) {
    if (!data || size == 0) return "";
    uint32_t hdr = prependedHeaderSize(data, size);
    uint32_t crc = crc32Bytes(data + hdr, size - hdr);
    char buf[24];
    std::snprintf(buf, sizeof buf, "crc32-%08x", (unsigned)crc);
    return buf;
}

static std::string slug(const std::string& s) {
    std::string o;
    for (char ch : s) {
        unsigned char c = (unsigned char)ch;
        if (std::isalnum(c)) o += (char)std::tolower(c);
        else if (!o.empty() && o.back() != '-') o += '-';
    }
    while (!o.empty() && o.back() == '-') o.pop_back();
    return o;
}

std::string contentIdentity(const std::string& path) {
    std::string ext = pathExt(path);
    if (ext == "cue" || ext == "cdi" || ext == "chd") {
        std::string s = slug(pathStem(path));
        return s.empty() ? "" : "disc-" + s;
    }
    std::vector<unsigned char> bytes;
    if (!readFile(path, bytes) || bytes.empty() || bytes.size() > (64u << 20)) return "";
    return cartIdentity(bytes.data(), bytes.size());
}

}  // namespace vj
