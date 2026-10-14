#include "theme.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cmath>
#include <vector>

namespace vj {
namespace theme {

const char* modeKey(Mode m) {
    switch (m) {
    case Mode::Dark: return "dark";
    case Mode::Light: return "light";
    case Mode::System: break;
    }
    return "system";
}

const char* modeLabel(Mode m) {
    switch (m) {
    case Mode::Dark: return "Dark";
    case Mode::Light: return "Light";
    case Mode::System: break;
    }
    return "System (follow the OS)";
}

Mode parseMode(const std::string& s) {
    if (s == "dark") return Mode::Dark;
    if (s == "light") return Mode::Light;
    return Mode::System;
}

Scheme resolve(Mode m, SystemScheme sys) {
    if (m == Mode::Dark) return Scheme::Dark;
    if (m == Mode::Light) return Scheme::Light;
    return sys == SystemScheme::Light ? Scheme::Light : Scheme::Dark;
}

bool State::setMode(Mode m) {
    Scheme before = effective();
    mode_ = m;
    return effective() != before;
}

bool State::setSystem(SystemScheme s) {
    Scheme before = effective();
    system_ = s;
    return effective() != before;
}

// ---- palettes ----------------------------------------------------------------------------------
// Every text colour here is checked against the background it is drawn on, in both schemes
// (app/tests/theme_test.cpp): body text >= 4.5:1.

static const Palette kDark = {
    /* homeBg */ {11, 11, 14},
    /* text */ {230, 230, 230},
    /* textSecondary */ {217, 217, 217},
    /* textMuted */ {172, 172, 172},
    /* warning */ {255, 204, 102},
    /* error */ {255, 128, 128},
    /* success */ {115, 217, 128},
    /* gold */ {242, 209, 89},
    /* goldFill */ {242, 209, 89},
    /* goldFillText */ {24, 24, 28},
    /* hoverEdge */ {120, 170, 255},
    /* cardBg */ {38, 40, 48},
    /* cardBgHover */ {60, 64, 76},
    /* stateBg */ {26, 28, 34},
    /* stateBgHover */ {60, 64, 76},
    /* emptyEdge */ {120, 120, 120},
    /* pictureBg */ {60, 60, 70},
    /* cursorButton */ {51, 102, 191},
    /* placeholderSat */ 0.35f, /* placeholderVal */ 0.42f, /* biosArtVal */ 0.40f,
    /* placeholderText */ {255, 255, 255, 235},
    /* placeholderDots */ {255, 255, 255, 150},
    /* badgeBg */ {20, 20, 20, 200},
    /* badgeText */ {255, 255, 255},
    /* uncertainBg */ {130, 76, 12, 225},
    /* starDisc */ {15, 15, 20, 190},
    /* starFill */ {245, 190, 40},
    /* starFillHot */ {255, 220, 90},
    /* starEdge */ {60, 40, 0, 230},
    /* starOutline */ {255, 255, 255, 170},
};

static const Palette kLight = {
    /* homeBg */ {240, 242, 246},
    /* text */ {28, 30, 36},
    /* textSecondary */ {58, 62, 72},
    /* textMuted */ {86, 90, 100},
    /* warning */ {138, 90, 0},
    /* error */ {179, 38, 30},
    /* success */ {26, 110, 45},
    /* gold */ {122, 86, 0},
    /* goldFill */ {232, 190, 60},
    /* goldFillText */ {24, 24, 28},
    /* hoverEdge */ {37, 99, 235},
    /* cardBg */ {222, 226, 234},
    /* cardBgHover */ {206, 213, 226},
    /* stateBg */ {222, 226, 234},
    /* stateBgHover */ {214, 220, 232},
    /* emptyEdge */ {128, 134, 148},
    /* pictureBg */ {120, 128, 146},
    /* cursorButton */ {170, 200, 250},
    /* placeholderSat */ 0.35f, /* placeholderVal */ 0.42f, /* biosArtVal */ 0.40f,
    /* placeholderText */ {255, 255, 255, 235},
    /* placeholderDots */ {255, 255, 255, 150},
    /* badgeBg */ {20, 20, 20, 200},
    /* badgeText */ {255, 255, 255},
    /* uncertainBg */ {130, 76, 12, 225},
    /* starDisc */ {15, 15, 20, 190},
    /* starFill */ {245, 190, 40},
    /* starFillHot */ {255, 220, 90},
    /* starEdge */ {60, 40, 0, 230},
    /* starOutline */ {255, 255, 255, 170},
};

static const Overlay kOverlay = {
    /* osdBg */ {0, 0, 0, 225},
    /* osdText */ {240, 240, 240},
    /* osdWarn */ {255, 210, 110},
    /* osdError */ {255, 120, 120},
    /* toastBg */ {18, 18, 24, 225},
    /* toastBody */ {235, 235, 235},
    /* toastIconBg */ {60, 60, 70, 120},
    /* toastGold */ {240, 200, 60},
    /* toastBlue */ {120, 170, 240},
    /* toastRed */ {240, 110, 110},
    /* hudBg */ {0, 0, 0, 180},
    /* hudText */ {255, 230, 120},
    /* challengeBg */ {60, 20, 20, 230},
    /* challengeText */ {255, 150, 150},
    /* progressBg */ {0, 0, 0, 190},
    /* progressText */ {220, 240, 255},
    /* fpsBg */ {0, 0, 0, 170},
    /* fpsText */ {255, 255, 0},
    /* windowBg */ {16, 16, 20},
    /* text */ {240, 240, 240},
    /* textMuted */ {200, 200, 200},
    /* heading */ {153, 217, 255},
    /* title */ {255, 230, 115},
    /* separator */ {110, 110, 128},
    /* windowAlpha */ 0.80f,
};

const Palette& palette(Scheme s) { return s == Scheme::Light ? kLight : kDark; }
const Overlay& overlay() { return kOverlay; }

// ---- process-wide state --------------------------------------------------------------------------
static State g_state;
static std::vector<Listener> g_listeners;
static SystemScheme (*g_query)() = nullptr;

State& state() { return g_state; }

void addListener(Listener l) { g_listeners.push_back(std::move(l)); }

static bool notify(bool changed) {
    if (changed) {
        Scheme s = g_state.effective();
        for (const Listener& l : g_listeners) l(s);
    }
    return changed;
}

bool setMode(Mode m) { return notify(g_state.setMode(m)); }
bool setSystem(SystemScheme s) { return notify(g_state.setSystem(s)); }

static SystemScheme sdlSystem() {
    switch (SDL_GetSystemTheme()) {
    case SDL_SYSTEM_THEME_DARK: return SystemScheme::Dark;
    case SDL_SYSTEM_THEME_LIGHT: return SystemScheme::Light;
    default: break;
    }
    return SystemScheme::Unknown;
}

void setSystemQuery(SystemScheme (*q)()) { g_query = q; }

bool refreshSystem() { return setSystem(g_query ? g_query() : sdlSystem()); }

bool handleEvent(const SDL_Event& e) {
    if (e.type != SDL_EVENT_SYSTEM_THEME_CHANGED) return false;
    refreshSystem();
    return true;
}

// ---- contrast --------------------------------------------------------------------------------------
static double lin(uint8_t v) {
    double c = v / 255.0;
    return c <= 0.04045 ? c / 12.92 : std::pow((c + 0.055) / 1.055, 2.4);
}

double relativeLuminance(Color c) { return 0.2126 * lin(c.r) + 0.7152 * lin(c.g) + 0.0722 * lin(c.b); }

double contrastRatio(Color a, Color b) {
    double la = relativeLuminance(a), lb = relativeLuminance(b);
    if (la < lb) std::swap(la, lb);
    return (la + 0.05) / (lb + 0.05);
}

Color over(Color top, Color bottom) {
    double a = top.a / 255.0;
    auto mix = [&](int t, int b) { return (int)std::lround(t * a + b * (1.0 - a)); };
    return Color(mix(top.r, bottom.r), mix(top.g, bottom.g), mix(top.b, bottom.b), 255);
}

}  // namespace theme
}  // namespace vj
