// Colour themes (#833): the selection logic and the contrast of every palette colour.
//
//   vj_theme_test
//
// Part 1, selection: Mode x SystemScheme -> Scheme, the config spelling, State (a System choice follows the OS,
// Dark and Light ignore it, "changed" is reported only when the scheme in force really moves), and the
// process-wide wiring: SDL_EVENT_SYSTEM_THEME_CHANGED re-queries the OS and tells the listeners.
// Part 2, contrast (WCAG 2.x): body text >= 4.5:1 against the background it is drawn on, in both themes,
// measured on the style the app really applies (theme::applyStyleColors) and on the palette the windows
// draw with; and the in-game overlays against both a white and a black game frame, which is why they stay
// dark-translucent whatever the theme is.
#include "theme_style.h"

#include <SDL3/SDL.h>

#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

using namespace vj;
using theme::Color;
using theme::Mode;
using theme::Scheme;
using theme::SystemScheme;

static int g_checks = 0, g_fail = 0;
#define CHECK(c)                                                              \
    do {                                                                      \
        g_checks++;                                                           \
        if (!(c)) {                                                           \
            g_fail++;                                                         \
            std::fprintf(stderr, "FAIL %s:%d  %s\n", __FILE__, __LINE__, #c); \
        }                                                                     \
    } while (0)

// ---- part 1 -------------------------------------------------------------------------------------

static SystemScheme g_fakeOs = SystemScheme::Unknown;
static SystemScheme fakeOs() { return g_fakeOs; }

static void testSelection() {
    CHECK(theme::parseMode("system") == Mode::System);
    CHECK(theme::parseMode("dark") == Mode::Dark);
    CHECK(theme::parseMode("light") == Mode::Light);
    CHECK(theme::parseMode("") == Mode::System);
    CHECK(theme::parseMode("Light") == Mode::System);   // the config value is lower case; anything else is the default
    CHECK(theme::parseMode("solarized") == Mode::System);
    for (Mode m : {Mode::System, Mode::Dark, Mode::Light}) CHECK(theme::parseMode(theme::modeKey(m)) == m);

    // The table.
    CHECK(theme::resolve(Mode::Dark, SystemScheme::Light) == Scheme::Dark);
    CHECK(theme::resolve(Mode::Dark, SystemScheme::Dark) == Scheme::Dark);
    CHECK(theme::resolve(Mode::Dark, SystemScheme::Unknown) == Scheme::Dark);
    CHECK(theme::resolve(Mode::Light, SystemScheme::Dark) == Scheme::Light);
    CHECK(theme::resolve(Mode::Light, SystemScheme::Light) == Scheme::Light);
    CHECK(theme::resolve(Mode::Light, SystemScheme::Unknown) == Scheme::Light);
    CHECK(theme::resolve(Mode::System, SystemScheme::Light) == Scheme::Light);
    CHECK(theme::resolve(Mode::System, SystemScheme::Dark) == Scheme::Dark);
    CHECK(theme::resolve(Mode::System, SystemScheme::Unknown) == Scheme::Dark);   // an OS that does not say: the look the app always had

    // State: what "changed" means.
    theme::State s;
    CHECK(s.mode() == Mode::System && s.effective() == Scheme::Dark);   // the default
    CHECK(!s.setSystem(SystemScheme::Dark));                             // dark -> dark
    CHECK(s.setSystem(SystemScheme::Light) && s.effective() == Scheme::Light);
    CHECK(!s.onSystemThemeChanged(SystemScheme::Light));                 // the same answer twice
    CHECK(s.setMode(Mode::Dark) && s.effective() == Scheme::Dark);       // pinned: the OS is ignored from here
    CHECK(!s.setSystem(SystemScheme::Dark));                             // (the OS changing under a pinned choice)
    CHECK(!s.setSystem(SystemScheme::Light) && s.effective() == Scheme::Dark);
    CHECK(s.system() == SystemScheme::Light);                            // ... but it is remembered
    CHECK(s.setMode(Mode::System) && s.effective() == Scheme::Light);    // so System picks the latest answer up at once
    CHECK(!s.setMode(Mode::Light));                                      // light -> light: no change
    CHECK(s.setMode(Mode::Dark));
}

static void testEvents() {
    theme::setSystemQuery(&fakeOs);
    int calls = 0;
    Scheme last = Scheme::Dark;
    theme::addListener([&](Scheme s) { calls++; last = s; });

    g_fakeOs = SystemScheme::Dark;
    theme::setMode(Mode::System);
    CHECK(theme::refreshSystem() == false && calls == 0 && theme::isDark());   // dark OS, dark app: nothing to tell

    SDL_Event e;
    SDL_zero(e);
    e.type = SDL_EVENT_SYSTEM_THEME_CHANGED;
    g_fakeOs = SystemScheme::Light;                                  // the OS switches to light
    CHECK(theme::handleEvent(e));
    CHECK(calls == 1 && last == Scheme::Light && !theme::isDark());  // a System choice followed it, live
    CHECK(&theme::pal() == &theme::palette(Scheme::Light));

    g_fakeOs = SystemScheme::Dark;
    CHECK(theme::handleEvent(e));
    CHECK(calls == 2 && last == Scheme::Dark && theme::isDark());    // and back

    CHECK(theme::handleEvent(e));                                    // the same answer again: handled, nobody told
    CHECK(calls == 2);

    theme::setMode(Mode::Light);                                     // the user pins Light (a change: told)
    CHECK(calls == 3 && last == Scheme::Light);
    g_fakeOs = SystemScheme::Dark;
    CHECK(theme::handleEvent(e));
    CHECK(calls == 3 && !theme::isDark());                           // a pinned choice ignores the OS

    theme::setMode(Mode::System);                                    // back to following: the OS says dark, so it flips
    CHECK(calls == 4 && last == Scheme::Dark);

    SDL_Event other;
    SDL_zero(other);
    other.type = SDL_EVENT_WINDOW_FOCUS_LOST;
    CHECK(!theme::handleEvent(other) && calls == 4);                 // not a theme event

    theme::setSystemQuery(nullptr);
}

// ---- part 2 -------------------------------------------------------------------------------------

static Color fromVec(ImVec4 v) {
    auto q = [](float f) { return (int)std::lround(std::fmax(0.0f, std::fmin(1.0f, f)) * 255.0f); };
    return Color(q(v.x), q(v.y), q(v.z), q(v.w));
}

static void ratio(const char* what, Color fg, Color bg, double min) {
    double r = theme::contrastRatio(fg, bg);
    g_checks++;
    if (r + 1e-9 < min) {
        g_fail++;
        std::fprintf(stderr, "FAIL contrast %-46s %.2f < %.1f\n", what, r, min);
    }
}

static void testStyle(Scheme scheme, const char* tag) {
    ImGuiStyle st;
    theme::applyStyleColors(st, scheme);
    auto col = [&](ImGuiCol i) { return fromVec(st.Colors[i]); };
    const Color window = col(ImGuiCol_WindowBg);
    CHECK(window.a == 255);   // opaque windows (#904)
    auto on = [&](ImGuiCol i) { return theme::over(col(i), window); };
    const Color text = col(ImGuiCol_Text), dim = col(ImGuiCol_TextDisabled);

    // Resting surfaces: body text 4.5:1 and the quiet (hint) text 4.5:1 too -- hints are whole paragraphs.
    const struct { const char* n; ImGuiCol c; } rest[] = {
        {"WindowBg", ImGuiCol_WindowBg}, {"PopupBg", ImGuiCol_PopupBg},   {"FrameBg", ImGuiCol_FrameBg},
        {"MenuBarBg", ImGuiCol_MenuBarBg}, {"TitleBg", ImGuiCol_TitleBg}, {"TitleBgActive", ImGuiCol_TitleBgActive},
        {"Button", ImGuiCol_Button},       {"Header", ImGuiCol_Header},   {"TableHeaderBg", ImGuiCol_TableHeaderBg},
        {"Tab", ImGuiCol_Tab},             {"TabSelected", ImGuiCol_TabSelected},
    };
    for (const auto& r : rest) {
        std::string n = std::string(tag) + " Text on " + r.n;
        ratio(n.c_str(), text, on(r.c), 4.5);
    }
    for (ImGuiCol c : {ImGuiCol_WindowBg, ImGuiCol_PopupBg, ImGuiCol_FrameBg, ImGuiCol_MenuBarBg}) {
        std::string n = std::string(tag) + " TextDisabled on surface " + std::to_string((int)c);
        ratio(n.c_str(), dim, on(c), 4.5);
    }
    // Hovered / pressed controls are momentary and brighter.  The light theme keeps 4.5 there too; the dark
    // theme keeps Dear ImGui's own bright blue, which clears the 3:1 bar WCAG sets for UI components.
    const double hoverMin = scheme == Scheme::Light ? 4.5 : 3.0;
    const struct { const char* n; ImGuiCol c; } hot[] = {
        {"ButtonHovered", ImGuiCol_ButtonHovered}, {"ButtonActive", ImGuiCol_ButtonActive},
        {"HeaderHovered", ImGuiCol_HeaderHovered}, {"HeaderActive", ImGuiCol_HeaderActive},
        {"FrameBgHovered", ImGuiCol_FrameBgHovered}, {"FrameBgActive", ImGuiCol_FrameBgActive},
        {"TabHovered", ImGuiCol_TabHovered},
    };
    for (const auto& r : hot) {
        std::string n = std::string(tag) + " Text on " + r.n;
        ratio(n.c_str(), text, on(r.c), hoverMin);
    }
}

static void testPalette(Scheme scheme, const char* tag) {
    const theme::Palette& p = theme::palette(scheme);
    ImGuiStyle st;
    theme::applyStyleColors(st, scheme);
    const Color window = fromVec(st.Colors[ImGuiCol_WindowBg]);
    const Color text = fromVec(st.Colors[ImGuiCol_Text]);

    struct Fg { const char* n; Color c; };
    const Fg fgs[] = {{"text", p.text},       {"textSecondary", p.textSecondary}, {"textMuted", p.textMuted},
                      {"warning", p.warning}, {"error", p.error},                 {"success", p.success},
                      {"gold", p.gold}};
    struct Bg { const char* n; Color c; };
    const Bg bgs[] = {{"window", window}, {"home", p.homeBg}};
    for (const Fg& f : fgs)
        for (const Bg& b : bgs) {
            std::string n = std::string(tag) + " " + f.n + " on " + b.n;
            ratio(n.c_str(), f.c, b.c, 4.5);
        }
    // The ImGui window text itself on the home screen (the toolbar and the search box sit right on it).
    ratio((std::string(tag) + " Text on home").c_str(), text, p.homeBg, 4.5);
    // The "no screenshot" label sits on a save state's box.
    ratio((std::string(tag) + " textMuted on stateBg").c_str(), p.textMuted, p.stateBg, 4.5);
    ratio((std::string(tag) + " textMuted on stateBgHover").c_str(), p.textMuted, p.stateBgHover, 4.5);
    ratio((std::string(tag) + " text on cardBg").c_str(), p.text, p.cardBg, 4.5);
    ratio((std::string(tag) + " text on cardBgHover").c_str(), p.text, p.cardBgHover, 4.5);
    // The finished progress bar's "n / n" on the gold fill; the keypad cursor button's label.
    ratio((std::string(tag) + " goldFillText on goldFill").c_str(), p.goldFillText, p.goldFill, 4.5);
    ratio((std::string(tag) + " Text on cursorButton").c_str(), text, p.cursorButton, 4.5);
    // Non-text edges need 3:1: the empty-slot outline and the hovered-tile outline against what they sit on.
    ratio((std::string(tag) + " emptyEdge on window").c_str(), p.emptyEdge, window, 3.0);
    ratio((std::string(tag) + " hoverEdge on home").c_str(), p.hoverEdge, p.homeBg, 3.0);
    ratio((std::string(tag) + " hoverEdge on cardBg").c_str(), p.hoverEdge, p.cardBg, 3.0);

    // Cover-art placeholders: the title in white over a colour taken from the game's name -- every hue.
    float worst = 1e9f;
    for (int h = 0; h < 360; h++) {
        float r, g, b;
        ImGui::ColorConvertHSVtoRGB((float)h / 360.0f, p.placeholderSat, p.placeholderVal, r, g, b);
        Color card = fromVec(ImVec4(r, g, b, 1));
        Color t = theme::over(p.placeholderText, card);
        worst = std::fmin(worst, (float)theme::contrastRatio(t, card));
        ImGui::ColorConvertHSVtoRGB((float)h / 360.0f, 0.30f, p.biosArtVal, r, g, b);
        Color bios = fromVec(ImVec4(r, g, b, 1));
        // the BIOS tile's label is drawn at 215/255 of the placeholder text alpha
        worst = std::fmin(worst, (float)theme::contrastRatio(theme::over(Color(255, 255, 255, 215), bios), bios));
    }
    CHECK(worst >= 4.5f);
    if (worst < 4.5f) std::fprintf(stderr, "  (%s placeholder worst %.2f)\n", tag, worst);
    // The chips over art, on the worst backdrop each can have (white or black art).
    for (Color art : {Color(255, 255, 255), Color(0, 0, 0)}) {
        ratio((std::string(tag) + " badge").c_str(), p.badgeText, theme::over(p.badgeBg, art), 4.5);
        ratio((std::string(tag) + " uncertain chip").c_str(), p.badgeText, theme::over(p.uncertainBg, art), 4.5);
        // the favourite star: its disc against the art, then the star on the disc
        Color disc = theme::over(p.starDisc, art);
        ratio((std::string(tag) + " star on disc").c_str(), p.starFill, disc, 3.0);
        ratio((std::string(tag) + " star outline on disc").c_str(), theme::over(p.starOutline, disc), disc, 3.0);
    }
}

static void testOverlay() {
    const theme::Overlay& o = theme::overlay();
    for (Color frame : {Color(255, 255, 255), Color(0, 0, 0), Color(255, 255, 0), Color(128, 128, 128)}) {
        const char* f = frame.r == 255 && frame.g == 255 && frame.b == 255 ? "white" : frame.r == 0 ? "black"
                       : frame.b == 0 ? "yellow" : "grey";
        auto chk = [&](const char* what, Color fg, Color bg) {
            std::string n = std::string("overlay ") + what + " over " + f;
            ratio(n.c_str(), fg, theme::over(bg, frame), 4.5);
        };
        chk("osd text", o.osdText, o.osdBg);
        chk("osd warn", o.osdWarn, o.osdBg);
        chk("osd error", o.osdError, o.osdBg);
        chk("toast body", o.toastBody, o.toastBg);
        chk("toast gold", o.toastGold, o.toastBg);
        chk("toast blue", o.toastBlue, o.toastBg);
        chk("toast red", o.toastRed, o.toastBg);
        chk("tracker", o.hudText, o.hudBg);
        chk("challenge", o.challengeText, o.challengeBg);
        chk("progress", o.progressText, o.progressBg);
        chk("fps", o.fpsText, o.fpsBg);
        Color win(o.windowBg.r, o.windowBg.g, o.windowBg.b, (int)std::lround(o.windowAlpha * 255.0f));
        chk("controls text", o.text, win);
        chk("controls muted", o.textMuted, win);
        chk("controls heading", o.heading, win);
        chk("controls title", o.title, win);
    }
}

int main() {
    testSelection();
    testEvents();
    testStyle(Scheme::Dark, "dark");
    testStyle(Scheme::Light, "light");
    testPalette(Scheme::Dark, "dark");
    testPalette(Scheme::Light, "light");
    testOverlay();
    // The two schemes must really differ (a theme that maps both to one palette would pass every check above).
    CHECK(theme::relativeLuminance(theme::palette(Scheme::Dark).homeBg) < 0.05);
    CHECK(theme::relativeLuminance(theme::palette(Scheme::Light).homeBg) > 0.6);
    ImGuiStyle d, l;
    theme::applyStyleColors(d, Scheme::Dark);
    theme::applyStyleColors(l, Scheme::Light);
    CHECK(d.Colors[ImGuiCol_WindowBg].x < 0.2f && l.Colors[ImGuiCol_WindowBg].x > 0.8f);
    std::printf("theme: %d checks, %d failed\n", g_checks, g_fail);
    return g_fail ? 1 : 0;
}
