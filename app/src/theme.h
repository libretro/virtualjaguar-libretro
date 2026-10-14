// Colour themes: Dark, Light, and "follow the system" (#833).
//
// Three layers, so each can be tested without a window:
//   * the choice (Mode: System / Dark / Light) and the OS's answer (SystemScheme) resolve to the
//     scheme in force (Scheme); State::setMode / setSystem say whether that changed;
//   * the palette: the semantic colours the UI draws with (warning, error, success, muted text, card
//     background, ...) for each scheme, so no window hard-codes an RGB triple;
//   * the Dear ImGui style colours for each scheme (theme_style.cpp), the one place the dark and light
//     looks are defined.
//
// Other code asks `theme::isDark()` (e.g. the Windows title bar) and registers a listener with
// `theme::addListener` to be told when the scheme in force changes.
//
// Over the game picture the UI cannot know what is behind it, so the in-game overlays (controls
// cheat sheet, OSD, achievement toasts, trackers, frame rate) use `theme::overlay()`: one
// dark-translucent set that is legible on a white or a black frame, the same in both schemes.
#pragma once

#include <cstdint>
#include <functional>
#include <string>

union SDL_Event;

namespace vj {
namespace theme {

struct Color {
    uint8_t r, g, b, a;
    constexpr Color(int r_, int g_, int b_, int a_ = 255) : r((uint8_t)r_), g((uint8_t)g_), b((uint8_t)b_), a((uint8_t)a_) {}
    // Dear ImGui's ImU32 packing (IM_COL32), with the alpha scaled by `alphaMul`.
    uint32_t u32(float alphaMul = 1.0f) const {
        float al = (float)a * alphaMul;
        uint32_t aa = (uint32_t)(al < 0.0f ? 0.0f : al > 255.0f ? 255.0f : al + 0.5f);
        return ((uint32_t)r) | ((uint32_t)g << 8) | ((uint32_t)b << 16) | (aa << 24);
    }
};

enum class Mode { System, Dark, Light };               // the user's choice (config ui.theme)
enum class SystemScheme { Unknown, Light, Dark };      // what the OS reports (SDL_GetSystemTheme)
enum class Scheme { Dark, Light };                     // what is in force

const char* modeKey(Mode m);                           // "system" | "dark" | "light" (the config value)
const char* modeLabel(Mode m);                         // for the Settings combo
Mode parseMode(const std::string& s);                  // anything unknown is System

// The scheme a choice resolves to.  An OS that does not say (Unknown: old Linux desktops, no window yet)
// gets Dark, the look the app always had.
Scheme resolve(Mode m, SystemScheme sys);

// The pure state: the choice and the OS's last answer.  Each setter returns true when the scheme in force changed.
class State {
public:
    Mode mode() const { return mode_; }
    SystemScheme system() const { return system_; }
    Scheme effective() const { return resolve(mode_, system_); }
    bool setMode(Mode m);
    bool setSystem(SystemScheme s);
    // An SDL_EVENT_SYSTEM_THEME_CHANGED arrived: the OS now says `s`.  Only a System choice follows it.
    bool onSystemThemeChanged(SystemScheme s) { return setSystem(s); }
private:
    Mode mode_ = Mode::System;
    SystemScheme system_ = SystemScheme::Unknown;
};

// ---- palette ---------------------------------------------------------------------------------
// Colours for text and shapes drawn on the themed backgrounds (windows, the home screen).
struct Palette {
    Color homeBg;            // the library home screen / welcome screen
    Color text;              // body text drawn straight on the draw list
    Color textSecondary;     // quieter body text (a locked achievement's line)
    Color textMuted;         // hints, "empty", status "idle"
    Color warning, error, success;
    Color gold, goldFill, goldFillText;   // hardcore text; the done progress bar and the text drawn over it
    Color hoverEdge;         // outline of a hovered tile
    Color cardBg, cardBgHover;            // a tile's card behind its art
    Color stateBg, stateBgHover;          // a save state's picture box
    Color emptyEdge;         // the outline of an empty slot
    Color pictureBg;         // the box standing in for a picture that has not arrived (RetroAchievements icons)
    Color cursorButton;      // the keypad button the cursor is on
    // Chips drawn over cover art or a card of their own colour: legible as they are, the same in both schemes.
    float placeholderSat, placeholderVal, biosArtVal;
    Color placeholderText, placeholderDots;
    Color badgeBg, badgeText, uncertainBg;
    Color starDisc, starFill, starFillHot, starEdge, starOutline;
};

// Over the game picture: the same in both schemes (see the header comment).
struct Overlay {
    Color osdBg, osdText, osdWarn, osdError;
    Color toastBg, toastBody, toastIconBg, toastGold, toastBlue, toastRed;
    Color hudBg, hudText, challengeBg, challengeText, progressBg, progressText;
    Color fpsBg, fpsText;
    Color windowBg, text, textMuted, heading, title, separator;   // the controls cheat sheet (a window)
    float windowAlpha;
};

const Palette& palette(Scheme s);
const Overlay& overlay();

// ---- process-wide state ----------------------------------------------------------------------
State& state();
inline Scheme effective() { return state().effective(); }
inline bool isDark() { return state().effective() == Scheme::Dark; }
inline const Palette& pal() { return palette(effective()); }

using Listener = std::function<void(Scheme)>;
void addListener(Listener l);          // called after the scheme in force changes (not at registration)

// These update the global state and call the listeners when the scheme in force changed.
bool setMode(Mode m);
bool setSystem(SystemScheme s);
// Asks the OS (SDL_GetSystemTheme) and calls setSystem.  Call once the video subsystem is up.
bool refreshSystem();
// Feeds an SDL event to the state; true when it was a system-theme event (handled, whether or not it changed anything).
bool handleEvent(const SDL_Event& e);
// Tests: replace the OS query.  nullptr restores SDL_GetSystemTheme.
void setSystemQuery(SystemScheme (*q)());

// ---- contrast (WCAG 2.x) ---------------------------------------------------------------------
double relativeLuminance(Color c);
double contrastRatio(Color a, Color b);                // both opaque: (L1 + .05) / (L2 + .05), L1 >= L2
Color over(Color top, Color bottom);                   // `top` composited on opaque `bottom` (alpha respected)

}  // namespace theme
}  // namespace vj
