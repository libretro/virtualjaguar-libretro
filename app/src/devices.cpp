#include "devices.h"

#include "libretro.h"

#include <algorithm>
#include <cstdlib>

namespace vj {

// ---------------------------------------------------------------------------------
// kinds
// ---------------------------------------------------------------------------------
// libretro.c ~line 543:  the RETRO_DEVICE_JAG_* subclasses (RETRO_DEVICE_SUBCLASS(base, n)).
unsigned devLibretroId(DevKind k) {
    switch (k) {
    case DevKind::Pad: return RETRO_DEVICE_JOYPAD;
    case DevKind::TeamTap: return RETRO_DEVICE_SUBCLASS(RETRO_DEVICE_JOYPAD, 0);
    case DevKind::MouseST: return RETRO_DEVICE_SUBCLASS(RETRO_DEVICE_MOUSE, 0);
    case DevKind::MouseAmiga: return RETRO_DEVICE_SUBCLASS(RETRO_DEVICE_MOUSE, 1);
    case DevKind::MouseAmigaAD: return RETRO_DEVICE_SUBCLASS(RETRO_DEVICE_MOUSE, 2);
    case DevKind::Rotary: return RETRO_DEVICE_SUBCLASS(RETRO_DEVICE_MOUSE, 3);
    case DevKind::Analog: return RETRO_DEVICE_SUBCLASS(RETRO_DEVICE_ANALOG, 0);
    case DevKind::Driving: return RETRO_DEVICE_SUBCLASS(RETRO_DEVICE_ANALOG, 1);
    case DevKind::Paddle: return RETRO_DEVICE_SUBCLASS(RETRO_DEVICE_ANALOG, 2);
    case DevKind::SixD: return RETRO_DEVICE_SUBCLASS(RETRO_DEVICE_ANALOG, 3);
    case DevKind::LightGun: return RETRO_DEVICE_LIGHTGUN;
    }
    return RETRO_DEVICE_JOYPAD;
}

bool devKindFromLibretro(unsigned id, DevKind& out) {
    for (int i = 0; i < kDevKinds; i++) {
        if (devLibretroId((DevKind)i) == id) {
            out = (DevKind)i;
            return true;
        }
    }
    return false;
}

DevKind devKindFromOption(const std::string& v) {
    if (v == "teamtap") return DevKind::TeamTap;
    if (v == "mouse_st") return DevKind::MouseST;
    if (v == "mouse_amiga") return DevKind::MouseAmiga;
    if (v == "mouse_amiga_adapter") return DevKind::MouseAmigaAD;
    if (v == "rotary") return DevKind::Rotary;
    if (v == "analog") return DevKind::Analog;
    if (v == "driving") return DevKind::Driving;
    if (v == "paddle") return DevKind::Paddle;
    if (v == "6d") return DevKind::SixD;
    if (v == "lightgun") return DevKind::LightGun;
    return DevKind::Pad;   // pad, pad_pro, auto, ""
}

DevKind effectiveDevKind(int frontendDevice, const std::string& optionValue) {
    DevKind k;
    if (frontendDevice >= 0 && devKindFromLibretro((unsigned)frontendDevice, k) && k != DevKind::Pad) return k;
    // libretro.c: a plain RETRO_DEVICE_MOUSE means the ST wiring.
    if (frontendDevice == (int)RETRO_DEVICE_MOUSE) return DevKind::MouseST;
    return devKindFromOption(optionValue);
}

const char* devKindLabel(DevKind k) {
    switch (k) {
    case DevKind::Pad: return "Standard Joypad";
    case DevKind::TeamTap: return "Team Tap (4-player adaptor)";
    case DevKind::MouseST: return "Atari ST / PS2 Mouse";
    case DevKind::MouseAmiga: return "Amiga Mouse (ST adapter)";
    case DevKind::MouseAmigaAD: return "Amiga Mouse (Amiga adapter)";
    case DevKind::Rotary: return "Rotary (Tempest)";
    case DevKind::Analog: return "Analog Joystick (bank-switching)";
    case DevKind::Driving: return "Driving Controller (bank-switching)";
    case DevKind::Paddle: return "Analog Stick (paddle ADC)";
    case DevKind::SixD: return "6D Controller (bank-switching)";
    case DevKind::LightGun: return "Light Gun";
    }
    return "";
}

const char* devKindId(DevKind k) {
    static const char* const ids[kDevKinds] = {"pad", "teamtap", "mouse_st", "mouse_amiga", "mouse_amiga_adapter",
                                               "rotary", "analog", "driving", "paddle", "6d", "lightgun"};
    return ids[(int)k];
}

const char* devKindHelp(DevKind k) {
    switch (k) {
    case DevKind::Pad: return "The standard pad and 12-key keypad.";
    case DevKind::TeamTap:
        return "Four pads on this port. Assign a controller to each socket below. Keypad 7-9, * and # do not exist on sockets 2-4.";
    case DevKind::MouseST:
    case DevKind::MouseAmiga:
    case DevKind::MouseAmigaAD:
        return "Your mouse or trackpad (captured while the game has focus; Esc or F10 releases it). A stick or keys can drive it too.";
    case DevKind::Rotary:
        return "Tempest 2000's spinner: mouse left/right, or a stick / two keys. Buttons and keypad stay on the pad.";
    case DevKind::Analog: return "Analog stick (Stick controls) plus buttons A-D; the D-pad is the hat.";
    case DevKind::Driving: return "Steering on the Stick X axis, accelerator = Right trigger, brake = Left trigger; the D-pad shifts gear.";
    case DevKind::Paddle: return "BattleSphere's analog stick (motherboard ADC): the Stick controls. The pad stays live.";
    case DevKind::SixD: return "Six axes: Stick, Stick 2, the triggers and the shoulders; buttons A-F, Option = G, Pause = Rezero.";
    case DevKind::LightGun:
        return "Point at the picture with the mouse and click to shoot; right-click (or the reload control) fires off-screen.";
    }
    return "";
}

// ---------------------------------------------------------------------------------
// per-port selection
// ---------------------------------------------------------------------------------
static std::string portKey(unsigned port, const std::string& gameId) {
    return (gameId.empty() ? std::string("input.") : "input.game." + gameId + ".") + "port." + std::to_string(port);
}

bool portDeviceDefined(const Config& cfg, unsigned port, const std::string& gameId) {
    return cfg.has(portKey(port, gameId)) && cfg.getInt(portKey(port, gameId), -1) >= 0;
}

int resolvePortDevice(const Config& cfg, unsigned port, const std::string& gameId) {
    if (port >= kJaguarPorts) return -1;
    if (!gameId.empty() && portDeviceDefined(cfg, port, gameId)) return cfg.getInt(portKey(port, gameId), -1);
    if (portDeviceDefined(cfg, port, "")) return cfg.getInt(portKey(port, ""), -1);
    return cfg.getInt("port." + std::to_string(port), -1);   // legacy
}

void setPortDevice(Config& cfg, unsigned port, const std::string& gameId, int device) {
    cfg.setInt(portKey(port, gameId), device);
    if (gameId.empty()) cfg.erase("port." + std::to_string(port));   // migrated
}

void clearPortDevice(Config& cfg, unsigned port, const std::string& gameId) {
    cfg.erase(portKey(port, gameId));
    if (gameId.empty()) cfg.erase("port." + std::to_string(port));
}

// libretro.c SET_CONTROLLER_INFO lists.
bool portOffers(unsigned port, unsigned device) {
    static const DevKind p1[] = {DevKind::Pad, DevKind::TeamTap, DevKind::Rotary, DevKind::Analog,
                                 DevKind::Driving, DevKind::Paddle, DevKind::SixD, DevKind::LightGun};
    static const DevKind p2[] = {DevKind::Pad, DevKind::TeamTap, DevKind::MouseST, DevKind::MouseAmiga,
                                 DevKind::MouseAmigaAD, DevKind::Rotary, DevKind::Analog, DevKind::Driving,
                                 DevKind::Paddle, DevKind::SixD};
    if (port >= kJaguarPorts) return false;
    const DevKind* l = port == 0 ? p1 : p2;
    size_t n = port == 0 ? sizeof p1 / sizeof p1[0] : sizeof p2 / sizeof p2[0];
    for (size_t i = 0; i < n; i++)
        if (devLibretroId(l[i]) == device) return true;
    return false;
}

// ---------------------------------------------------------------------------------
// slots
// ---------------------------------------------------------------------------------
std::string slotName(unsigned slot) {
    if (slot == 0) return "Player 1";
    if (slot == 1) return "Player 2";
    if (slot >= kSlots) return "";
    return "Port " + std::to_string(slotJaguarPort(slot) + 1) + ", socket " + std::to_string(slotSocket(slot) + 1);
}

std::string serializeSlot(const SlotSource& s) {
    switch (s.kind) {
    case SlotSource::None: return "none";
    case SlotSource::Keyboard: return "kb";
    case SlotSource::Pad: return "pad:" + s.guid + "#" + std::to_string(s.instance);
    default: return "auto";
    }
}

bool parseSlot(const std::string& t, SlotSource& out) {
    SlotSource s;
    if (t == "auto") s.kind = SlotSource::Auto;
    else if (t == "none") s.kind = SlotSource::None;
    else if (t == "kb") s.kind = SlotSource::Keyboard;
    else if (t.rfind("pad:", 0) == 0) {
        size_t h = t.rfind('#');
        if (h == std::string::npos || h <= 4) return false;
        s.kind = SlotSource::Pad;
        s.guid = t.substr(4, h - 4);
        char* end = nullptr;
        long v = std::strtol(t.c_str() + h + 1, &end, 10);
        if (!end || *end != '\0' || v < 0 || v > 7) return false;
        s.instance = (int)v;
    } else return false;
    out = s;
    return true;
}

static std::string slotKey(unsigned slot, const std::string& gameId) {
    return (gameId.empty() ? std::string("input.") : "input.game." + gameId + ".") + "slot." + std::to_string(slot);
}

bool slotDefined(const Config& cfg, unsigned slot, const std::string& gameId) {
    SlotSource s;
    return slot < kSlots && cfg.has(slotKey(slot, gameId)) && parseSlot(cfg.get(slotKey(slot, gameId)), s);
}

SlotSource resolveSlot(const Config& cfg, unsigned slot, const std::string& gameId) {
    SlotSource s;
    if (slot >= kSlots) return s;
    if (!gameId.empty() && parseSlot(cfg.get(slotKey(slot, gameId)), s)) return s;
    if (parseSlot(cfg.get(slotKey(slot, "")), s)) return s;
    return SlotSource();
}

void setSlot(Config& cfg, unsigned slot, const std::string& gameId, const SlotSource& s) {
    if (slot < kSlots) cfg.set(slotKey(slot, gameId), serializeSlot(s));
}

void clearSlot(Config& cfg, unsigned slot, const std::string& gameId) {
    if (slot < kSlots) cfg.erase(slotKey(slot, gameId));
}

std::array<bool, kSlots> activeSlots(bool tap1, bool tap2) {
    std::array<bool, kSlots> a{};
    a[0] = a[1] = true;
    for (unsigned s = 2; s <= 4; s++) a[s] = tap1;
    for (unsigned s = 5; s <= 7; s++) a[s] = tap2;
    return a;
}

Routes assignSlots(const std::array<SlotSource, kSlots>& cfg, const std::array<bool, kSlots>& active,
                   const std::vector<std::string>& guids, unsigned kbDefaultSlot) {
    Routes r;
    std::vector<bool> used(guids.size(), false);
    bool kbClaimed = false;
    for (unsigned s = 0; s < kSlots; s++) {
        if (!active[s]) continue;
        const SlotSource& c = cfg[s];
        if (c.kind == SlotSource::Keyboard) {
            r.kb[s] = true;
            kbClaimed = true;
        } else if (c.kind == SlotSource::Pad) {
            int seen = 0;
            for (size_t i = 0; i < guids.size(); i++) {
                if (guids[i] != c.guid) continue;
                if (seen++ == c.instance) {
                    r.pad[s] = (int)i;
                    used[i] = true;
                    break;
                }
            }
        }
    }
    if (!kbClaimed && kbDefaultSlot < kSlots && active[kbDefaultSlot]) r.kb[kbDefaultSlot] = true;
    size_t next = 0;
    for (unsigned s = 0; s < kSlots; s++) {
        if (!active[s] || cfg[s].kind != SlotSource::Auto) continue;
        while (next < guids.size() && used[next]) next++;
        if (next >= guids.size()) break;
        r.pad[s] = (int)next;
        used[next] = true;
    }
    return r;
}

// ---------------------------------------------------------------------------------
// settings
// ---------------------------------------------------------------------------------
static std::string devKey(const std::string& gameId, const char* key) {
    return (gameId.empty() ? std::string("input.dev.") : "input.game." + gameId + ".dev.") + key;
}

bool DeviceSettings::defined(const Config& cfg, const std::string& gameId, const char* key) {
    return cfg.has(devKey(gameId, key));
}

DeviceSettings DeviceSettings::load(const Config& cfg, const std::string& gameId) {
    DeviceSettings d;
    auto f = [&](const char* k, float& v, float lo, float hi) {
        for (int pass = 0; pass < 2; pass++) {
            const std::string& g = pass == 0 ? std::string() : gameId;
            if (pass == 1 && gameId.empty()) break;
            if (cfg.has(devKey(g, k))) v = std::max(lo, std::min(hi, cfg.getFloat(devKey(g, k), v)));
        }
    };
    auto i = [&](const char* k, int& v, int lo, int hi) {
        for (int pass = 0; pass < 2; pass++) {
            const std::string& g = pass == 0 ? std::string() : gameId;
            if (pass == 1 && gameId.empty()) break;
            if (cfg.has(devKey(g, k))) v = std::max(lo, std::min(hi, cfg.getInt(devKey(g, k), v)));
        }
    };
    f("mouse_speed", d.mouseSpeed, 0.05f, 16.0f);
    f("rotary_speed", d.rotarySpeed, 0.05f, 16.0f);
    f("stick_speed", d.stickSpeed, 0.05f, 16.0f);
    i("deadzone", d.deadzonePct, 0, 60);
    i("mouse_port", d.mousePort, -1, 1);
    i("gun_source", d.gunSource, 0, 2);
    return d;
}

void DeviceSettings::set(Config& cfg, const std::string& gameId, const char* key, const std::string& value) {
    cfg.set(devKey(gameId, key), value);
}

void DeviceSettings::clear(Config& cfg, const std::string& gameId, const char* key) { cfg.erase(devKey(gameId, key)); }

// ---------------------------------------------------------------------------------
// geometry / capture
// ---------------------------------------------------------------------------------
bool gunCoords(const PicRect& r, float px, float py, int32_t& gx, int32_t& gy) {
    gx = gy = 0;
    if (!r.valid()) return false;
    float fx = (px - r.x) / r.w, fy = (py - r.y) / r.h;
    bool inside = fx >= 0.0f && fx < 1.0f && fy >= 0.0f && fy < 1.0f;
    auto conv = [](float f) {
        // f in [0,1) -> -0x8000 .. 0x7fff; the core divides back with (v + 0x8000) / 0x10000.
        int32_t v = (int32_t)(f * 65536.0f) - 0x8000;
        return std::max(-0x7fff, std::min(0x7fff, v));
    };
    gx = conv(std::max(0.0f, std::min(fx, 0.99999f)));
    gy = conv(std::max(0.0f, std::min(fy, 0.99999f)));
    return inside;
}

void gunToPointer(const PicRect& r, int32_t gx, int32_t gy, float& px, float& py) {
    px = r.x + ((float)(gx + 0x8000) / 65536.0f) * r.w;
    py = r.y + ((float)(gy + 0x8000) / 65536.0f) * r.h;
}

bool lockedCoreOption(const std::string& key, std::string& value) {
    value.clear();
    if (key == "virtualjaguar_alt_inputs") {
        value = "disabled";
        return true;
    }
    for (const char* port : {"virtualjaguar_p1_", "virtualjaguar_p2_"}) {
        if (key.rfind(port, 0) != 0) continue;
        std::string rest = key.substr(std::string(port).size());
        if (rest == "numpad_to_kb") {
            value = "disabled";
            return true;
        }
        if (rest == "device") {
            value = "auto";
            return true;
        }
        if (rest.rfind("retropad_", 0) == 0) return true;   // its default
    }
    return false;
}

int resolveMousePort(const std::array<DevKind, 2>& kinds, int setting) {
    if (setting >= 0 && setting < 2 && devIsPointerClass(kinds[(size_t)setting])) return setting;
    for (int p = 0; p < 2; p++)
        if (devIsPointerClass(kinds[(size_t)p])) return p;
    return -1;
}

bool wantRelativeMouse(const CaptureInputs& in) {
    return in.deviceWantsMouse && in.windowFocused && in.gameRunning && !in.uiOpen && !in.userReleased;
}

}  // namespace vj
