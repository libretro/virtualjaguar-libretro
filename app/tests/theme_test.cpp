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
#include "paths.h"
#include "theme_custom.h"
#include "theme_style.h"

#include <SDL3/SDL.h>

#include <cmath>
#include <cstdio>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <sstream>
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

static void testStyleOf(ImGuiStyle& st, double hoverMin, const char* tag) {
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

static void testStyle(Scheme scheme, const char* tag) {
    ImGuiStyle st;
    theme::applyStyleColors(st, scheme);
    testStyleOf(st, scheme == Scheme::Light ? 4.5 : 3.0, tag);
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


// ---- part 3: custom themes ------------------------------------------------------------------------------

namespace fs = std::filesystem;
using theme::Custom;

static bool has(const std::string& s, const char* sub) { return s.find(sub) != std::string::npos; }

static void testCustomChoice() {
    CHECK(theme::parseMode("custom:Mine") == Mode::Custom);
    CHECK(theme::parseMode("custom:") == Mode::System);   // no name: not a theme
    CHECK(theme::parseMode("custom") == Mode::System);
    CHECK(theme::customNameOf("custom:Mine") == "Mine" && theme::customNameOf("dark").empty());
    theme::State s;
    CHECK(!s.setMode(Mode::Custom));                       // a custom theme needs a name: setCustom
    CHECK(s.setCustom("A", Scheme::Light) && s.effective() == Scheme::Light && s.mode() == Mode::Custom);
    CHECK(!s.setSystem(SystemScheme::Dark) && s.effective() == Scheme::Light);   // a custom theme ignores the OS
    CHECK(s.setMode(Mode::System) && s.effective() == Scheme::Dark && s.customName().empty());   // and System follows it again

    // The process-wide wiring: listeners hear every custom change (live preview), the palette is the theme's.
    theme::setSystemQuery(&fakeOs);
    g_fakeOs = SystemScheme::Dark;
    theme::setMode(Mode::Dark);
    int calls = 0;
    theme::addListener([&](Scheme) { calls++; });
    Custom c = Custom::fromBase(Scheme::Dark, "Warm");
    c.colors[theme::KeyText] = theme::Color(250, 240, 200);
    theme::setCustom(c);
    CHECK(calls == 1 && theme::activeCustom() && theme::choiceKey() == "custom:Warm");
    CHECK(theme::pal().text.r == 250 && theme::pal().text.g == 240);
    c.colors[theme::KeyText] = theme::Color(250, 250, 200);
    theme::setCustom(c);                                   // same scheme, new colours: still announced
    CHECK(calls == 2 && theme::pal().text.g == 250);
    g_fakeOs = SystemScheme::Light;
    CHECK(!theme::refreshSystem() && calls == 2 && theme::isDark());   // the OS flipping does not move a custom theme
    Custom light = Custom::fromBase(Scheme::Light, "Paper");
    theme::setCustom(light);
    CHECK(!theme::isDark() && theme::choiceKey() == "custom:Paper");
    Custom darkOnLight = Custom::fromBase(Scheme::Light, "Odd");   // a "light" base with a dark window reads as dark
    darkOnLight.colors[theme::KeyWindow] = theme::Color(10, 10, 12);
    theme::setCustom(darkOnLight);
    CHECK(theme::isDark());
    theme::setMode(Mode::Dark);                            // leaving the custom theme: the palette is the scheme's again
    CHECK(theme::activeCustom() == nullptr && &theme::pal() == &theme::palette(Scheme::Dark) && theme::choiceKey() == "dark");
    CHECK(calls == 5);
    theme::setMode(Mode::System);
    theme::setSystemQuery(nullptr);
}

static void testFileFormat() {
    Custom c = Custom::fromBase(Scheme::Light, "Round trip (1)");
    c.colors[theme::KeyAccent] = theme::Color(1, 2, 3);
    c.colors[theme::KeyGold] = theme::Color(255, 254, 253);
    c.rounding = 7.5f;
    c.spacing = 1.25f;
    Custom back;
    std::string err;
    CHECK(theme::parseTheme(theme::serializeTheme(c), back, err) && back == c);
    CHECK(has(theme::serializeTheme(c), "color.accent=#010203"));

    // Unknown keys and comments are ignored; missing colours keep the base's.
    const std::string minimal = "# x\nvjtheme=1\nname=Min\nbase=dark\nfuture.key=whatever\ncolor.nonsense=#zzzzzz\n";
    CHECK(theme::parseTheme(minimal, back, err));
    CHECK(back.name == "Min" && back == Custom::fromBase(Scheme::Dark, "Min"));
    CHECK(theme::parseTheme("vjtheme=1\nname=A/B\\C\nbase=light\n", back, err) && back.name == "A_B_C");   // never a path

    struct Bad { const char* text; const char* msg; };
    const Bad bad[] = {
        {"", "not a Virtual Jaguar theme"},
        {"just some text\nwith lines\n", "not a Virtual Jaguar theme"},
        {"name=X\nbase=dark\n", "not a Virtual Jaguar theme"},                       // no vjtheme
        {"vjtheme=2\nname=X\nbase=dark\n", "newer version"},
        {"vjtheme=1\nbase=dark\n", "no name"},
        {"vjtheme=1\nname=X\n", "dark or light"},
        {"vjtheme=1\nname=X\nbase=blue\n", "dark or light"},
        {"vjtheme=1\nname=   \nbase=dark\n", "name"},
        {"vjtheme=1\nname=X\nbase=dark\ncolor.text=red\n", "color.text must look like #RRGGBB"},
        {"vjtheme=1\nname=X\nbase=dark\ncolor.window=#12345\n", "color.window"},
        {"vjtheme=1\nname=X\nbase=dark\ncolor.card=#12345g\n", "color.card"},
        {"vjtheme=1\nname=X\nbase=dark\ncolor.error=#1234567\n", "color.error"},
        {"vjtheme=1\nname=X\nbase=dark\ncolor.muted=\n", "color.muted"},
        {"vjtheme=1\nname=X\nbase=dark\nstyle.rounding=99\n", "style.rounding"},
        {"vjtheme=1\nname=X\nbase=dark\nstyle.rounding=-1\n", "style.rounding"},
        {"vjtheme=1\nname=X\nbase=dark\nstyle.spacing=abc\n", "style.spacing"},
        {"vjtheme=1\nname=X\nbase=dark\nstyle.spacing=nan\n", "style.spacing"},
        {"vjtheme=1\nname=X\nbase=dark\nstyle.spacing=0.1\n", "style.spacing"},
        {"\x01\x02\xff\xfe binary \x00 garbage\n", "not a Virtual Jaguar theme"},
    };
    for (const Bad& b : bad) {
        Custom o = Custom::fromBase(Scheme::Dark, "untouched");
        std::string e;
        const bool ok = theme::parseTheme(std::string(b.text), o, e);
        CHECK(!ok);
        CHECK(has(e, b.msg));
        CHECK(o.name == "untouched");   // a refused file changes nothing
        if (ok || !has(e, b.msg)) std::fprintf(stderr, "  case: %s -> '%s'\n", b.text, e.c_str());
    }

    CHECK(theme::sanitizeName("  Hello World  ") == "Hello World");
    CHECK(theme::sanitizeName("../etc/passwd") == "_etc_passwd");
    CHECK(theme::sanitizeName("..") .empty());
    CHECK(theme::sanitizeName(".hidden") == "hidden");
    CHECK(theme::sanitizeName("a:b*c?") == "a_b_c_");
    CHECK(theme::sanitizeName(std::string(100, 'x')).size() == 40);
    CHECK(theme::sanitizeName("").empty() && theme::sanitizeName("   ").empty());
}

static std::string readAll(const fs::path& p) {
    std::ifstream f(p, std::ios::binary);
    std::stringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

static void testStore(const fs::path& tmp) {
    const std::string dir = (tmp / "themes").string();
    theme::Store st(dir, false), ro(dir, true);
    CHECK(st.list().empty());   // no folder yet is fine
    std::string err;
    Custom a = Custom::fromBase(Scheme::Dark, "Alpha");
    a.colors[theme::KeyAccent] = theme::Color(200, 30, 30);
    CHECK(st.save(a, err));
    Custom b = Custom::fromBase(Scheme::Light, "bravo");
    CHECK(st.save(b, err));
    CHECK(!ro.save(Custom::fromBase(Scheme::Dark, "Nope"), err) && has(err, "secondary copy"));
    CHECK(st.list().size() == 2 && st.list()[0].name == "Alpha" && st.list()[1].name == "bravo");
    Custom got;
    CHECK(st.load("ALPHA", got) && got == a);   // names are case-insensitive
    CHECK(st.exists("Bravo") && !st.exists("Charlie"));
    CHECK(st.uniqueName("Charlie") == "Charlie" && st.uniqueName("alpha") == "alpha 2");
    CHECK(!st.save(Custom::fromBase(Scheme::Dark, "  "), err) && has(err, "name"));

    // Duplicate, then replace by saving the same name again.
    Custom d;
    CHECK(st.duplicate("Alpha", d, err) && d.name == "Alpha copy" && d.colors[theme::KeyAccent].r == 200);
    CHECK(st.duplicate("Alpha", d, err) && d.name == "Alpha copy 2");
    CHECK(st.list().size() == 4);
    a.rounding = 9;
    CHECK(st.save(a, err) && st.list().size() == 4);
    CHECK(st.load("Alpha", got) && got.rounding == 9.0f);
    CHECK(!st.duplicate("Missing", d, err));

    // A corrupt or foreign file in the folder is skipped, not fatal, and left alone.
    { std::ofstream f(fs::path(dir) / "junk.ini"); f << "this is not a theme\n"; }
    { std::ofstream f(fs::path(dir) / "notes.txt"); f << "hello\n"; }
    CHECK(st.list().size() == 4);

    // Delete: only that theme's file.
    CHECK(!ro.remove("Alpha copy 2", err));
    CHECK(st.remove("Alpha copy 2", err) && st.list().size() == 3 && !st.exists("Alpha copy 2"));
    CHECK(!st.remove("Alpha copy 2", err));
    CHECK(fs::exists(fs::path(dir) / "junk.ini") && fs::exists(fs::path(dir) / "notes.txt"));

    // Export is the same bytes the store writes, and importing it elsewhere gives the theme back, renamed on a clash.
    const fs::path exported = tmp / "exported.ini";
    CHECK(theme::saveThemeFile(a, exported.string(), err));
    CHECK(readAll(exported) == theme::serializeTheme(a));
    Custom imp;
    CHECK(st.importFile(exported.string(), imp, err) && imp.name == "Alpha 2");
    CHECK(imp.colors[theme::KeyAccent].r == 200 && imp.rounding == 9.0f);
    CHECK(!ro.importFile(exported.string(), imp, err) && has(err, "secondary copy"));
    theme::Store other((tmp / "elsewhere").string(), false);
    CHECK(other.importFile(exported.string(), imp, err) && imp.name == "Alpha");   // no clash there

    // Refusals say why and write nothing.
    const size_t before = st.list().size();
    CHECK(!st.importFile((tmp / "missing.ini").string(), imp, err) && has(err, "Cannot read"));
    { std::ofstream f(tmp / "garbage.ini", std::ios::binary); f << "vjtheme=1\nname=G\nbase=dark\ncolor.text=banana\n"; }
    CHECK(!st.importFile((tmp / "garbage.ini").string(), imp, err) && has(err, "color.text"));
    { std::ofstream f(tmp / "huge.ini", std::ios::binary); f << "vjtheme=1\nname=H\nbase=dark\n" << std::string(70 * 1024, '#'); }
    CHECK(!st.importFile((tmp / "huge.ini").string(), imp, err) && has(err, "too big"));
    CHECK(st.list().size() == before);
    // No half-written files are left in the folder (atomic writes): only .ini and the two files we put there.
    for (const auto& e : fs::directory_iterator(dir)) {
        const std::string n = e.path().filename().string();
        CHECK(n.size() > 4 && (n.substr(n.size() - 4) == ".ini" || n == "notes.txt"));
    }
}

static void testWarnings() {
    for (Scheme b : {Scheme::Dark, Scheme::Light}) {
        Custom c = Custom::fromBase(b, "x");
        const auto w = theme::contrastWarnings(c);
        for (const auto& x : w) std::fprintf(stderr, "  default %s warns: %s %.2f\n", b == Scheme::Dark ? "dark" : "light", x.what.c_str(), x.ratio);
        CHECK(w.empty());   // starting from a built-in theme never starts with a warning
        // Text nearly the colour of the window: the headline warning, with the ratio.
        Custom bad = c;
        bad.colors[theme::KeyText] = theme::Color(bad.colors[theme::KeyWindow].r + (b == Scheme::Dark ? 20 : -20),
                                                  bad.colors[theme::KeyWindow].g + (b == Scheme::Dark ? 20 : -20),
                                                  bad.colors[theme::KeyWindow].b + (b == Scheme::Dark ? 20 : -20));
        const auto bw = theme::contrastWarnings(bad);
        bool found = false;
        for (const auto& x : bw)
            if (x.what == "Text on window") { found = true; CHECK(x.ratio < 1.5 && x.needed == 4.5); }
        CHECK(found && bw.size() >= 4);
        // One bad semantic colour names itself and only that.
        Custom warn = c;
        warn.colors[theme::KeyWarning] = warn.colors[theme::KeyWindow];
        const auto ww = theme::contrastWarnings(warn);
        CHECK(!ww.empty());
        for (const auto& x : ww) CHECK(has(x.what, "Warning"));
    }
    // The ratio is the WCAG one: black on white is 21.
    Custom bw = Custom::fromBase(Scheme::Light, "bw");
    bw.colors[theme::KeyText] = theme::Color(0, 0, 0);
    bw.colors[theme::KeyWindow] = theme::Color(255, 255, 255);
    CHECK(std::fabs(theme::contrastRatio(theme::Color(0, 0, 0), theme::Color(255, 255, 255)) - 21.0) < 1e-6);
}

static void testCustomStyle() {
    Custom c = Custom::fromBase(Scheme::Dark, "S");
    c.colors[theme::KeyWindow] = theme::Color(20, 40, 60);
    c.colors[theme::KeyAccent] = theme::Color(250, 100, 50);
    c.colors[theme::KeyText] = theme::Color(255, 250, 240);
    ImGuiStyle st;
    theme::applyCustomColors(st, c);
    auto eq = [](ImVec4 v, int r, int g, int b) {
        return std::lround(v.x * 255) == r && std::lround(v.y * 255) == g && std::lround(v.z * 255) == b;
    };
    CHECK(eq(st.Colors[ImGuiCol_WindowBg], 20, 40, 60) && st.Colors[ImGuiCol_WindowBg].w == 1.0f);
    CHECK(eq(st.Colors[ImGuiCol_Text], 255, 250, 240));
    CHECK(eq(st.Colors[ImGuiCol_ButtonHovered], 250, 100, 50) && eq(st.Colors[ImGuiCol_CheckMark], 250, 100, 50));
    // Derived colours follow the three that were set: a theme with the default accent differs.
    ImGuiStyle plain;
    theme::applyCustomColors(plain, Custom::fromBase(Scheme::Dark, "P"));
    CHECK(plain.Colors[ImGuiCol_Button].x != st.Colors[ImGuiCol_Button].x);
    // The defaults of both bases are themselves readable (the derivation is sane).
    ImGuiStyle d, l;
    theme::applyCustomColors(d, Custom::fromBase(Scheme::Dark, "D"));
    theme::applyCustomColors(l, Custom::fromBase(Scheme::Light, "L"));
    testStyleOf(d, 3.0, "custom-dark");
    testStyleOf(l, 3.0, "custom-light");

    // Metrics: rounding 4 / spacing 1 is exactly what ScaleAllSizes gave the stock style.
    for (float scale : {1.0f, 1.25f, 1.5f, 2.0f, 3.0f}) {
        ImGuiStyle ref;
        ref.WindowRounding = 4.0f;
        ref.FrameRounding = 3.0f;
        ref.ScaleAllSizes(scale);
        ImGuiStyle mine;
        theme::applyStyleMetrics(mine, theme::kDefaultRounding, theme::kDefaultSpacing, scale);
        CHECK(mine.WindowRounding == ref.WindowRounding && mine.FrameRounding == ref.FrameRounding);
        CHECK(mine.WindowPadding.x == ref.WindowPadding.x && mine.WindowPadding.y == ref.WindowPadding.y);
        CHECK(mine.FramePadding.x == ref.FramePadding.x && mine.FramePadding.y == ref.FramePadding.y);
        CHECK(mine.ItemSpacing.x == ref.ItemSpacing.x && mine.ItemSpacing.y == ref.ItemSpacing.y);
        CHECK(mine.ItemInnerSpacing.x == ref.ItemInnerSpacing.x && mine.CellPadding.y == ref.CellPadding.y);
    }
    ImGuiStyle m;
    theme::applyStyleMetrics(m, 8.0f, 1.5f, 2.0f);
    CHECK(m.WindowRounding == 16.0f && m.FrameRounding == 14.0f);
    CHECK(m.ItemSpacing.x == std::floor(ImGuiStyle().ItemSpacing.x * 1.5f * 2.0f));
    theme::applyStyleMetrics(m, 0.0f, 0.75f, 1.0f);
    CHECK(m.WindowRounding == 0.0f && m.FrameRounding == 0.0f);   // never negative
    // Fonts are not the theme's business.
    ImGuiStyle f;
    f.FontScaleDpi = 2.5f;
    const float fsb = f.FontSizeBase;
    theme::applyStyleMetrics(f, 8.0f, 1.5f, 2.0f);
    theme::applyCustomColors(f, c);
    CHECK(f.FontScaleDpi == 2.5f && f.FontSizeBase == fsb);

    // applyStyle follows the theme in force, and going back restores the stock metrics.
    Custom round = Custom::fromBase(Scheme::Light, "R");
    round.rounding = 10.0f;
    theme::setCustom(round);
    ImGuiStyle a1;
    theme::applyStyle(a1, 1.0f);
    CHECK(a1.WindowRounding == 10.0f && a1.Colors[ImGuiCol_WindowBg].x > 0.9f);
    theme::setMode(Mode::Dark);
    theme::applyStyle(a1, 1.0f);
    CHECK(a1.WindowRounding == 4.0f && a1.FrameRounding == 3.0f && a1.Colors[ImGuiCol_WindowBg].x < 0.2f);
    theme::setMode(Mode::System);
}


int main() {
    testSelection();
    testEvents();
    testStyle(Scheme::Dark, "dark");
    testStyle(Scheme::Light, "light");
    testPalette(Scheme::Dark, "dark");
    testPalette(Scheme::Light, "light");
    testOverlay();
    testCustomChoice();
    testFileFormat();
    {
        std::error_code ec;
        fs::path tmp = fs::temp_directory_path(ec) / ("vj-theme-test-" + std::to_string((long long)std::time(nullptr)));
        fs::create_directories(tmp, ec);
        testStore(tmp);
        fs::remove_all(tmp, ec);   // our own temp folder
    }
    testWarnings();
    testCustomStyle();
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
