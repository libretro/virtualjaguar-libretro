#include "pad_profile.h"

#include <SDL3/SDL.h>

#include <cstddef>

namespace vj {

namespace {

constexpr size_t kN = sizeof(kNavIntents) / sizeof(kNavIntents[0]);

int indexOf(tv::Intent in) {
    for (size_t i = 0; i < kN; i++)
        if (kNavIntents[i] == in) return (int)i;
    return -1;
}

bool isDpad(int b) {
    return b == SDL_GAMEPAD_BUTTON_DPAD_UP || b == SDL_GAMEPAD_BUTTON_DPAD_DOWN || b == SDL_GAMEPAD_BUTTON_DPAD_LEFT ||
           b == SDL_GAMEPAD_BUTTON_DPAD_RIGHT;
}

bool isTrigger(int axis) { return axis == SDL_GAMEPAD_AXIS_LEFT_TRIGGER || axis == SDL_GAMEPAD_AXIS_RIGHT_TRIGGER; }

// A binding an intent may hold: a non-D-pad button, or a trigger pushed in.
bool usable(const Binding& b) {
    if (b.kind == Binding::PadButton) return !isDpad(b.code);
    if (b.kind == Binding::PadAxis) return isTrigger(b.code) && b.dir > 0;
    return false;
}

// Same physical input, whatever the hold flag says.
bool sameInput(const Binding& a, const Binding& b) { return a.kind == b.kind && a.code == b.code && a.dir == b.dir; }

Binding plain(Binding b) {
    b.hold = false;
    return b;
}

}  // namespace

PadFamily familyForType(int t) {
    switch (t) {
    case SDL_GAMEPAD_TYPE_XBOX360:
    case SDL_GAMEPAD_TYPE_XBOXONE: return PadFamily::Xbox;
    case SDL_GAMEPAD_TYPE_PS3:
    case SDL_GAMEPAD_TYPE_PS4:
    case SDL_GAMEPAD_TYPE_PS5: return PadFamily::PlayStation;
    case SDL_GAMEPAD_TYPE_NINTENDO_SWITCH_PRO:
    case SDL_GAMEPAD_TYPE_NINTENDO_SWITCH_JOYCON_LEFT:
    case SDL_GAMEPAD_TYPE_NINTENDO_SWITCH_JOYCON_RIGHT:
    case SDL_GAMEPAD_TYPE_NINTENDO_SWITCH_JOYCON_PAIR:
    case SDL_GAMEPAD_TYPE_GAMECUBE: return PadFamily::Switch;
    default: return PadFamily::Generic;   // STANDARD, UNKNOWN, and any type a newer SDL adds
    }
}

const char* familyKey(PadFamily f) {
    switch (f) {
    case PadFamily::Xbox: return "xbox";
    case PadFamily::PlayStation: return "playstation";
    case PadFamily::Switch: return "switch";
    case PadFamily::Generic: return "generic";
    }
    return "generic";
}

bool familyFromKey(const std::string& key, PadFamily& out) {
    for (PadFamily f : {PadFamily::Xbox, PadFamily::PlayStation, PadFamily::Switch, PadFamily::Generic})
        if (key == familyKey(f)) {
            out = f;
            return true;
        }
    return false;
}

const char* familyName(PadFamily f) {
    switch (f) {
    case PadFamily::Xbox: return "Xbox";
    case PadFamily::PlayStation: return "PlayStation";
    case PadFamily::Switch: return "Nintendo Switch";
    case PadFamily::Generic: return "MFi / generic";
    }
    return "MFi / generic";
}

const char* navIntentKey(tv::Intent in) {
    switch (in) {
    case tv::Intent::Accept: return "confirm";
    case tv::Intent::Back: return "back";
    case tv::Intent::Alt: return "menu";
    case tv::Intent::Aux: return "favorite";
    case tv::Intent::Menu: return "start";
    case tv::Intent::SectionPrev: return "tabprev";
    case tv::Intent::SectionNext: return "tabnext";
    case tv::Intent::PagePrev: return "pageprev";
    case tv::Intent::PageNext: return "pagenext";
    case tv::Intent::Search: return "search";
    default: return "";
    }
}

const char* navIntentLabel(tv::Intent in) {
    switch (in) {
    case tv::Intent::Accept: return "Confirm";
    case tv::Intent::Back: return "Back";
    case tv::Intent::Alt: return "Tile menu";
    case tv::Intent::Aux: return "Favorite";
    case tv::Intent::Menu: return "Start";
    case tv::Intent::SectionPrev: return "Previous tab";
    case tv::Intent::SectionNext: return "Next tab";
    case tv::Intent::PagePrev: return "Previous letter";
    case tv::Intent::PageNext: return "Next letter";
    case tv::Intent::Search: return "Search";
    default: return "";
    }
}

tv::Intent NavProfile::intentForButton(int b) const {
    switch (b) {
    case SDL_GAMEPAD_BUTTON_DPAD_UP: return tv::Intent::Up;
    case SDL_GAMEPAD_BUTTON_DPAD_DOWN: return tv::Intent::Down;
    case SDL_GAMEPAD_BUTTON_DPAD_LEFT: return tv::Intent::Left;
    case SDL_GAMEPAD_BUTTON_DPAD_RIGHT: return tv::Intent::Right;
    default: break;
    }
    for (size_t i = 0; i < kN; i++)
        if (binds[i].kind == Binding::PadButton && binds[i].code == b) return kNavIntents[i];
    return tv::Intent::None;
}

tv::Intent NavProfile::intentForTrigger(int axis) const {
    if (!isTrigger(axis)) return tv::Intent::None;
    for (size_t i = 0; i < kN; i++)
        if (binds[i].kind == Binding::PadAxis && binds[i].code == axis && binds[i].dir > 0) return kNavIntents[i];
    return tv::Intent::None;
}

Binding NavProfile::binding(tv::Intent in) const {
    int i = indexOf(in);
    return i < 0 ? Binding() : binds[(size_t)i];
}

NavProfile familyNavProfile(PadFamily f) {
    NavProfile p;
    p.family = f;
    const bool sw = f == PadFamily::Switch;   // A/B are swapped on Nintendo's labels: A (East) confirms
    auto set = [&](tv::Intent in, Binding b) { p.binds[(size_t)indexOf(in)] = b; };
    set(tv::Intent::Accept, Binding::button(sw ? SDL_GAMEPAD_BUTTON_EAST : SDL_GAMEPAD_BUTTON_SOUTH));
    set(tv::Intent::Back, Binding::button(sw ? SDL_GAMEPAD_BUTTON_SOUTH : SDL_GAMEPAD_BUTTON_EAST));
    set(tv::Intent::Alt, Binding::button(SDL_GAMEPAD_BUTTON_WEST));
    set(tv::Intent::Aux, Binding::button(SDL_GAMEPAD_BUTTON_NORTH));
    set(tv::Intent::Menu, Binding::button(SDL_GAMEPAD_BUTTON_START));
    set(tv::Intent::SectionPrev, Binding::button(SDL_GAMEPAD_BUTTON_LEFT_SHOULDER));
    set(tv::Intent::SectionNext, Binding::button(SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER));
    set(tv::Intent::PagePrev, Binding::axis(SDL_GAMEPAD_AXIS_LEFT_TRIGGER, +1));
    set(tv::Intent::PageNext, Binding::axis(SDL_GAMEPAD_AXIS_RIGHT_TRIGGER, +1));
    set(tv::Intent::Search, Binding::button(SDL_GAMEPAD_BUTTON_BACK));
    return p;
}

std::string navKeyPrefix(const std::string& guid) { return "input.padguid." + guid + ".nav."; }

NavProfile resolveNavProfile(const Config& cfg, const std::string& guid, int sdlGamepadType) {
    PadFamily fam = familyForType(sdlGamepadType);
    bool overridden = false;
    const std::string prefix = navKeyPrefix(guid);
    if (!guid.empty()) {
        PadFamily f;
        if (familyFromKey(cfg.get(prefix + "family"), f)) {
            fam = f;
            overridden = true;
        }
    }
    NavProfile p = familyNavProfile(fam);
    p.familyOverridden = overridden;
    if (guid.empty()) return p;
    for (size_t i = 0; i < kN; i++) {
        const std::string v = cfg.get(prefix + navIntentKey(kNavIntents[i]));
        Binding b;
        if (!v.empty() && Binding::parse(v, b) && usable(b)) p.binds[i] = plain(b);   // a stray key, a D-pad or a hold flag is ignored
    }
    return p;
}

NavSet setNavBinding(Config& cfg, const std::string& guid, int sdlGamepadType, tv::Intent in, const Binding& want,
                     const std::vector<Binding>& reserved) {
    const int idx = indexOf(in);
    if (idx < 0 || guid.empty() || !usable(want)) return NavSet::Reserved;
    for (const Binding& r : reserved)
        if (sameInput(r, want)) return NavSet::Reserved;

    const Binding b = plain(want);
    NavProfile cur = resolveNavProfile(cfg, guid, sdlGamepadType);
    const Binding old = cur.binds[(size_t)idx];
    if (sameInput(old, b)) return NavSet::Ok;

    int other = -1;   // the intent that already uses `b` swaps with this one
    for (size_t i = 0; i < kN; i++)
        if ((int)i != idx && sameInput(cur.binds[i], b)) other = (int)i;

    const NavProfile def = familyNavProfile(cur.family);
    const std::string prefix = navKeyPrefix(guid);
    auto store = [&](int i, const Binding& v) {
        const std::string key = prefix + navIntentKey(kNavIntents[i]);
        if (sameInput(v, def.binds[(size_t)i])) cfg.erase(key);
        else cfg.set(key, v.serialize());
    };
    store(idx, b);
    if (other >= 0) store(other, old);
    return other >= 0 ? NavSet::Swapped : NavSet::Ok;
}

void setNavFamily(Config& cfg, const std::string& guid, const std::string& keyOrAuto) {
    if (guid.empty()) return;
    PadFamily f;
    if (familyFromKey(keyOrAuto, f)) cfg.set(navKeyPrefix(guid) + "family", familyKey(f));
    else cfg.erase(navKeyPrefix(guid) + "family");
}

void clearNavProfile(Config& cfg, const std::string& guid) {
    if (guid.empty()) return;
    const std::string prefix = navKeyPrefix(guid);
    for (const auto& kv : cfg.withPrefix(prefix)) cfg.erase(prefix + kv.first);
}

std::string navButtonName(PadFamily f, const Binding& b) {
    const bool ps = f == PadFamily::PlayStation, sw = f == PadFamily::Switch;
    if (b.kind == Binding::PadButton) {
        switch (b.code) {
        case SDL_GAMEPAD_BUTTON_SOUTH: return ps ? "Cross" : sw ? "B" : "A";
        case SDL_GAMEPAD_BUTTON_EAST: return ps ? "Circle" : sw ? "A" : "B";
        case SDL_GAMEPAD_BUTTON_WEST: return ps ? "Square" : sw ? "Y" : "X";
        case SDL_GAMEPAD_BUTTON_NORTH: return ps ? "Triangle" : sw ? "X" : "Y";
        case SDL_GAMEPAD_BUTTON_LEFT_SHOULDER: return ps ? "L1" : sw ? "L" : "LB";
        case SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER: return ps ? "R1" : sw ? "R" : "RB";
        case SDL_GAMEPAD_BUTTON_START: return ps ? "Options" : sw ? "+" : "Menu";
        case SDL_GAMEPAD_BUTTON_BACK: return ps ? "Create" : sw ? "\xE2\x88\x92" : "View";   // U+2212 MINUS SIGN
        default: break;
        }
    } else if (b.kind == Binding::PadAxis && b.dir > 0) {
        if (b.code == SDL_GAMEPAD_AXIS_LEFT_TRIGGER) return ps ? "L2" : sw ? "ZL" : "LT";
        if (b.code == SDL_GAMEPAD_AXIS_RIGHT_TRIGGER) return ps ? "R2" : sw ? "ZR" : "RT";
    }
    return b.display();
}

}  // namespace vj
