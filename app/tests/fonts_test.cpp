// UI fonts (fonts.h).  No window, no GL, no ImGui context, no core.
//
//   * names / config round trip / clamping, and that a secondary copy of the app never writes the keys
//   * the per-OS font lookup (macOS, Windows, Linux) against fake machines: order, fallbacks, nothing found
//   * custom font files: a real TTF is accepted and copied, junk / web fonts / truncated files are refused
//   * glyph coverage: every non-ASCII character in the app's own sources is inside the required ranges
//   * the fonts on THIS machine (informational unless FONTS_STRICT is set)
//
//   vj_fonts_test SRC_DIR IMGUI_DIR
#include "config.h"
#include "fonts.h"
#include "paths.h"
#include "ui_scale.h"

#include <SDL3/SDL.h>

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <vector>

namespace fs = std::filesystem;
using namespace vj;
using namespace vj::fonts;

static int g_checks = 0, g_fail = 0;
#define CHECK(c) do { g_checks++; if (!(c)) { g_fail++; std::fprintf(stderr, "FAIL %s:%d  %s\n", __FILE__, __LINE__, #c); } } while (0)

static std::string slurp(const fs::path& p) {
    std::ifstream f(p, std::ios::binary);
    std::stringstream ss;
    ss << f.rdbuf();
    return ss.str();
}
static void spit(const fs::path& p, const std::string& s) {
    std::ofstream f(p, std::ios::binary);
    f.write(s.data(), (std::streamsize)s.size());
}

// A pretend machine: which files exist, which environment variables are set, what fontconfig says.
struct FakeMachine {
    std::set<std::string> files;
    std::map<std::string, std::string> env;
    std::map<std::string, std::string> fc;
    Probe probe() const {
        Probe p;
        const FakeMachine* m = this;
        p.exists = [m](const std::string& f) { return m->files.count(f) != 0; };
        p.env = [m](const std::string& k) {
            auto it = m->env.find(k);
            return it == m->env.end() ? std::string() : it->second;
        };
        p.fcMatch = [m](const std::string& pat) {
            auto it = m->fc.find(pat);
            return it == m->fc.end() ? std::string() : it->second;
        };
        return p;
    }
};

static void testNamesAndConfig(const fs::path& tmp) {
    for (Family f : {Family::Retro, Family::System, Family::Mono, Family::Custom, Family::Installed}) {
        Family back = Family::Retro;
        CHECK(parseFamily(familyKey(f), back) && back == f);
        CHECK(std::strlen(familyLabel(f)) > 3);
    }
    for (Hinting h : {Hinting::None, Hinting::Light, Hinting::Full}) {
        Hinting back = Hinting::None;
        CHECK(parseHinting(hintingKey(h), back) && back == h);
    }
    Family junk = Family::Mono;
    CHECK(!parseFamily("comic", junk) && junk == Family::Mono);

    // defaults == what the app has always drawn
    Config empty;
    const Settings d = load(empty);
    CHECK(d.family == Family::Retro && d.size == kDefaultSize && kDefaultSize == 13.0f);
    CHECK(d.antialias && d.hinting == Hinting::Light && d.custom.empty());

    // the dev spec ("--font system:15")
    Settings sp;
    CHECK(applySpec("system", sp) && sp.family == Family::System && sp.size == kDefaultSize);
    CHECK(applySpec("mono:16", sp) && sp.family == Family::Mono && sp.size == 16.0f);
    CHECK(applySpec("custom:My.ttf", sp) && sp.family == Family::Custom && sp.custom == "My.ttf" && sp.size == 16.0f);
    CHECK(applySpec("retro:99", sp) && sp.size == kMaxSize);
    Settings keep = sp;
    CHECK(!applySpec("comic", sp) && sp == keep);
    CHECK(!applySpec("system:big", sp) && sp == keep);
    Settings fresh;
    CHECK(applySpec("custom", fresh) && fresh.family == Family::Retro);   // no file named: cannot be selected

    // round trip
    Settings s;
    s.family = Family::Custom;
    s.size = 17.0f;
    s.hinting = Hinting::Full;
    s.antialias = false;
    s.custom = "My Font.ttf";
    Config c;
    store(c, s);
    CHECK(load(c) == s);
    // ... through a file
    const std::string path = (tmp / "config.ini").string();
    c.setPath(path);
    CHECK(c.save());
    Config c2;
    c2.load(path);
    CHECK(load(c2) == s);
    // erase -> defaults
    erase(c2);
    CHECK(load(c2) == Settings());

    // garbage in the file
    Config g;
    g.set("ui.font.family", "wingdings");
    g.set("ui.font.size", "9999");
    g.set("ui.font.hint", "extreme");
    const Settings gs = load(g);
    CHECK(gs.family == Family::Retro && gs.size == kMaxSize && gs.hinting == Hinting::Light);
    g.set("ui.font.size", "-3");
    CHECK(load(g).size == kMinSize);
    g.set("ui.font.size", "nan");
    CHECK(std::isfinite(load(g).size));
    // Custom with no file named cannot be selected
    g.set("ui.font.family", "custom");
    g.erase("ui.font.custom");
    CHECK(load(g).family == Family::Retro);
    CHECK(sanitized(Settings()).size == kDefaultSize);

    // A second copy of the app (instance.h) reads the shared settings but writes only its own keys.
    const std::string shared = (tmp / "shared.ini").string(), own = (tmp / "config-2.ini").string();
    {
        Config first;
        first.setPath(shared);
        Settings a;
        a.family = Family::System;
        a.size = 15;
        store(first, a);
        CHECK(first.save());
    }
    const std::string before = slurp(shared);
    {
        Config second;
        second.load(shared);
        second.overlay(own);
        second.setPath(own);
        second.setSavePrefixes({"window.", "input.pads."});
        CHECK(load(second).family == Family::System && load(second).size == 15.0f);   // reads
        Settings b = load(second);
        b.family = Family::Mono;
        b.size = 22;
        store(second, b);                      // the user changes it in window 2 ...
        second.setInt("window.w", 800);
        CHECK(second.save());
    }
    CHECK(slurp(shared) == before);             // ... the shared file is untouched
    CHECK(slurp(own).find("ui.font") == std::string::npos);   // and the fonts are not in the window's own file
    CHECK(slurp(own).find("window.w") != std::string::npos);
}

static bool hasPath(const std::vector<FontFile>& v, const std::string& p) {
    for (const FontFile& f : v)
        if (f.path == p) return true;
    return false;
}

// The font list's state (#935): which OS font, the debugger font, favorites and recents.
static void testChoices(const fs::path& tmp) {
    // choices <-> settings
    Settings s;
    CHECK(choiceId(s) == "@retro");
    CHECK(applyChoice(s, "@system") && s.family == Family::System && choiceId(s) == "@system");
    CHECK(applyChoice(s, "@mono") && s.family == Family::Mono);
    CHECK(applyChoice(s, "@custom:My Font.ttf") && s.family == Family::Custom && s.custom == "My Font.ttf" &&
          choiceId(s) == "@custom:My Font.ttf");
    CHECK(applyChoice(s, "/Library/Fonts/Georgia.ttf|2") && s.family == Family::Installed &&
          s.installed == "/Library/Fonts/Georgia.ttf|2" && choiceId(s) == s.installed);
    Settings before = s;
    CHECK(!applyChoice(s, "nonsense") && !applyChoice(s, "@custom:") && s == before);

    // favorites: newest first, no repeats, removable
    Settings f;
    setFavorite(f, "/a.ttf|0", true);
    setFavorite(f, "/b.ttf|0", true);
    setFavorite(f, "/a.ttf|0", true);
    CHECK(f.favorites.size() == 2 && f.favorites[0] == "/b.ttf|0" && isFavorite(f, "/a.ttf|0"));
    setFavorite(f, "/b.ttf|0", false);
    CHECK(f.favorites.size() == 1 && !isFavorite(f, "/b.ttf|0"));

    // recents: most recent first, deduped, capped, built-ins not recorded
    Settings r;
    for (int i = 0; i < 12; i++) noteRecent(r, "/f" + std::to_string(i) + ".ttf|0");
    CHECK(r.recents.size() == kMaxRecents && r.recents[0] == "/f11.ttf|0" && r.recents.back() == "/f4.ttf|0");
    noteRecent(r, "/f6.ttf|0");
    CHECK(r.recents.size() == kMaxRecents && r.recents[0] == "/f6.ttf|0");
    int sixes = 0;
    for (const std::string& e : r.recents) sixes += e == "/f6.ttf|0";
    CHECK(sixes == 1);
    noteRecent(r, "@retro");
    noteRecent(r, "@system");
    noteRecent(r, "");
    CHECK(r.recents[0] == "/f6.ttf|0");
    forgetChoice(r, "/f6.ttf|0");
    CHECK(r.recents[0] == "/f11.ttf|0");

    // config round trip, including a path with odd characters, an empty list, and old files without the new keys
    const fs::path cfgPath = tmp / "choices.ini";
    {
        Config c;
        c.setPath(cfgPath.string());
        Settings w;
        w.family = Family::Installed;
        w.installed = "/Fonts/Weird = Name;1.ttc|3";
        w.debugFont = "/Fonts/Mono.ttf|0";
        w.favorites = {"/Fonts/A B.ttf|0", "@retro", w.installed};
        w.recents = {"/x.ttf|0", "/y.ttf|1"};
        store(c, w);
        CHECK(c.save());
    }
    {
        Config c;
        c.load(cfgPath.string());
        const Settings l = load(c);
        CHECK(l.family == Family::Installed && l.installed == "/Fonts/Weird = Name;1.ttc|3");
        CHECK(l.debugFont == "/Fonts/Mono.ttf|0");
        CHECK(l.favorites.size() == 3 && l.favorites[0] == "/Fonts/A B.ttf|0" && l.favorites[2] == l.installed);
        CHECK(l.recents.size() == 2 && l.recents[1] == "/y.ttf|1");
        // shrinking the lists leaves no stale tail
        Settings w = l;
        w.favorites.resize(1);
        w.recents.clear();
        store(c, w);
        const Settings l2 = load(c);
        CHECK(l2.favorites.size() == 1 && l2.recents.empty());
        CHECK(c.get("ui.font.fav.1", "gone") == "gone" && c.get("ui.font.recent.0", "gone") == "gone");
        erase(c);
        const Settings l3 = load(c);
        CHECK(l3 == Settings() && !c.has("ui.font.os") && !c.has("ui.font.debug") && !c.has("ui.font.fav.0"));
    }
    {   // a config written by an older version: no new keys, same result as before
        Config c;
        c.set("ui.font.family", "system");
        c.set("ui.font.size", "15");
        const Settings l = load(c);
        CHECK(l.family == Family::System && l.size == 15.0f && l.installed.empty() && l.favorites.empty());
    }

    // sanitising: Installed with no usable id is Retro; junk debug ids and over-long lists are tidied
    Settings bad;
    bad.family = Family::Installed;
    CHECK(sanitized(bad).family == Family::Retro);
    bad.installed = "not an id";
    CHECK(sanitized(bad).family == Family::Retro && sanitized(bad).installed.empty());
    bad.debugFont = "junk";
    CHECK(sanitized(bad).debugFont.empty());
    bad.debugFont = "@retro";
    CHECK(sanitized(bad).debugFont == "@retro");
    for (int i = 0; i < 300; i++) bad.favorites.push_back("/f" + std::to_string(i % 250) + ".ttf|0");
    bad.favorites.push_back("");
    CHECK(sanitized(bad).favorites.size() == kMaxFavorites);

    // the dev spec
    Settings d;
    CHECK(applySpec("installed:/Fonts/X.ttf", d) && d.family == Family::Installed && d.installed == "/Fonts/X.ttf|0");
    CHECK(applySpec("installed:/Fonts/Z.ttc|2", d) && d.installed == "/Fonts/Z.ttc|2");

    // a font that can draw its own name / cannot
    const FontFile ui = resolve(Role::Ui);
    if (!ui.empty()) {
        std::vector<unsigned char> data;
        if (readFile(ui.path, data)) {
            CHECK(dataCoversText(data.data(), data.size(), ui.index, "Helvetica Neue Bold 123"));
            CHECK(!dataCoversText(data.data(), data.size(), ui.index, "\xF0\x9F\x99\x82 \xF0\x9F\x98\x80\xF0\x9F\x98\x80"));
        }
    }
    CHECK(!dataCoversText(nullptr, 0, 0, "A"));
}

static void testResolveMac() {
    const std::string sys = "/System/Library/Fonts/";
    FakeMachine m;
    m.files = {sys + "SFNS.ttf", sys + "HelveticaNeue.ttc", sys + "SFNSMono.ttf", sys + "Menlo.ttc", sys + "Apple Symbols.ttf"};
    CHECK(resolve(Role::Ui, Os::Mac, m.probe()).path == sys + "SFNS.ttf");
    CHECK(resolve(Role::Mono, Os::Mac, m.probe()).path == sys + "SFNSMono.ttf");
    CHECK(resolve(Role::Fallback, Os::Mac, m.probe()).path == sys + "Apple Symbols.ttf");
    // SF missing (an old macOS): Helvetica Neue; both SF Mono and Menlo missing: Monaco
    m.files.erase(sys + "SFNS.ttf");
    m.files.erase(sys + "SFNSMono.ttf");
    CHECK(resolve(Role::Ui, Os::Mac, m.probe()).path == sys + "HelveticaNeue.ttc");
    CHECK(resolve(Role::Mono, Os::Mac, m.probe()).path == sys + "Menlo.ttc");
    m.files.erase(sys + "Menlo.ttc");
    m.files.insert(sys + "Monaco.ttf");
    CHECK(resolve(Role::Mono, Os::Mac, m.probe()).path == sys + "Monaco.ttf");
    // nothing at all
    FakeMachine none;
    CHECK(resolve(Role::Ui, Os::Mac, none.probe()).empty());
    CHECK(resolve(Role::Mono, Os::Mac, none.probe()).empty());
    CHECK(!candidates(Role::Ui, Os::Mac, none.probe()).empty());   // candidates do not depend on existence
}

static void testResolveWindows() {
    FakeMachine m;
    m.env["WINDIR"] = "D:\\Win";
    m.files = {"D:\\Win\\Fonts\\segoeui.ttf", "D:\\Win\\Fonts\\consola.ttf", "D:\\Win\\Fonts\\seguisym.ttf"};
    CHECK(resolve(Role::Ui, Os::Windows, m.probe()).path == "D:\\Win\\Fonts\\segoeui.ttf");
    CHECK(resolve(Role::Mono, Os::Windows, m.probe()).path == "D:\\Win\\Fonts\\consola.ttf");
    CHECK(resolve(Role::Fallback, Os::Windows, m.probe()).path == "D:\\Win\\Fonts\\seguisym.ttf");
    CHECK(displayName(resolve(Role::Ui, Os::Windows, m.probe())) == "segoeui");
    // WINDIR unset: SystemRoot, then C:\Windows
    FakeMachine m2;
    m2.env["SystemRoot"] = "E:\\NT";
    m2.files = {"E:\\NT\\Fonts\\segoeui.ttf"};
    CHECK(resolve(Role::Ui, Os::Windows, m2.probe()).path == "E:\\NT\\Fonts\\segoeui.ttf");
    FakeMachine m3;
    m3.files = {"C:\\Windows\\Fonts\\tahoma.ttf", "C:\\Windows\\Fonts\\cour.ttf"};   // Segoe / Consolas missing
    CHECK(resolve(Role::Ui, Os::Windows, m3.probe()).path == "C:\\Windows\\Fonts\\tahoma.ttf");
    CHECK(resolve(Role::Mono, Os::Windows, m3.probe()).path == "C:\\Windows\\Fonts\\cour.ttf");
    // a trailing separator in the variable is not doubled
    FakeMachine m4;
    m4.env["WINDIR"] = "C:\\Windows\\";
    m4.files = {"C:\\Windows\\Fonts\\segoeui.ttf"};
    CHECK(resolve(Role::Ui, Os::Windows, m4.probe()).path == "C:\\Windows\\Fonts\\segoeui.ttf");
    FakeMachine none;
    CHECK(resolve(Role::Ui, Os::Windows, none.probe()).empty());
}

static void testResolveLinux() {
    const std::string dv = "/usr/share/fonts/truetype/dejavu/";
    // fontconfig's answer leads
    FakeMachine m;
    m.fc["sans-serif"] = "/usr/share/fonts/cantarell/Cantarell-VF.otf|0";
    m.fc["monospace"] = "/usr/share/fonts/jetbrains/JetBrainsMono.ttc|2";
    m.files = {"/usr/share/fonts/cantarell/Cantarell-VF.otf", "/usr/share/fonts/jetbrains/JetBrainsMono.ttc",
               dv + "DejaVuSans.ttf", dv + "DejaVuSansMono.ttf"};
    FontFile ui = resolve(Role::Ui, Os::Linux, m.probe());
    CHECK(ui.path == "/usr/share/fonts/cantarell/Cantarell-VF.otf" && ui.index == 0);
    FontFile mono = resolve(Role::Mono, Os::Linux, m.probe());
    CHECK(mono.path == "/usr/share/fonts/jetbrains/JetBrainsMono.ttc" && mono.index == 2);   // face inside a collection
    // fontconfig answers with something that is not a usable file (a bitmap font) or not there: skip to the fixed paths
    m.fc["sans-serif"] = "/usr/share/fonts/misc/6x13.pcf.gz|0";
    CHECK(resolve(Role::Ui, Os::Linux, m.probe()).path == dv + "DejaVuSans.ttf");
    CHECK(!hasPath(candidates(Role::Ui, Os::Linux, m.probe()), "/usr/share/fonts/misc/6x13.pcf.gz"));
    m.fc["sans-serif"] = "/gone/Missing.ttf|0";
    CHECK(resolve(Role::Ui, Os::Linux, m.probe()).path == dv + "DejaVuSans.ttf");
    // no fontconfig at all: the distribution paths
    FakeMachine plain;
    plain.files = {"/usr/share/fonts/liberation/LiberationSans-Regular.ttf", "/usr/share/fonts/noto/NotoSansMono-Regular.ttf"};
    CHECK(resolve(Role::Ui, Os::Linux, plain.probe()).path == "/usr/share/fonts/liberation/LiberationSans-Regular.ttf");
    CHECK(resolve(Role::Mono, Os::Linux, plain.probe()).path == "/usr/share/fonts/noto/NotoSansMono-Regular.ttf");
    // DejaVu beats Liberation when both exist (wider Unicode coverage)
    plain.files.insert(dv + "DejaVuSans.ttf");
    CHECK(resolve(Role::Ui, Os::Linux, plain.probe()).path == dv + "DejaVuSans.ttf");
    FakeMachine none;
    CHECK(resolve(Role::Ui, Os::Linux, none.probe()).empty());
    CHECK(resolve(Role::Mono, Os::Linux, none.probe()).empty());
    // index junk from fontconfig is treated as 0
    FakeMachine odd;
    odd.fc["sans-serif"] = "/f/A.ttf|-1";
    odd.files = {"/f/A.ttf"};
    CHECK(resolve(Role::Ui, Os::Linux, odd.probe()).index == 0);
    odd.fc["sans-serif"] = "/f/B.ttf";   // no index field at all
    odd.files = {"/f/B.ttf"};
    CHECK(resolve(Role::Ui, Os::Linux, odd.probe()).path == "/f/B.ttf");
    // every OS offers fallbacks (symbols / arrows) for every role
    for (Os os : {Os::Mac, Os::Windows, Os::Linux})
        for (Role r : {Role::Ui, Role::Mono, Role::Fallback}) CHECK(!candidates(r, os, none.probe()).empty());
}

static void testSafeNames() {
    CHECK(safeFileName("Nice Font (v2).ttf") == "Nice Font (v2).ttf");
    CHECK(safeFileName("../../etc/passwd").find('/') == std::string::npos);
    CHECK(safeFileName("../x.ttf") == "_x.ttf");
    CHECK(safeFileName("").size() > 0);
    CHECK(safeFileName("...").size() > 0);
    CHECK(safeFileName("a:b*c?.ttf") == "a_b_c_.ttf");
    CHECK(safeFileName(std::string(500, 'x')).size() <= 120);
    CHECK(customPath("/d/fonts/", "Nice.ttf") == "/d/fonts/Nice.ttf");
    CHECK(customPath("/d/fonts/", "../Nice.ttf").empty());
    CHECK(customPath("/d/fonts/", "a/b.ttf").empty());
    CHECK(customPath("/d/fonts/", "a\\b.ttf").empty());
    CHECK(customPath("/d/fonts/", "").empty());
}

static void testCustomFiles(const fs::path& tmp, const std::string& imguiDir) {
    const std::string good = imguiDir + "/misc/fonts/ProggyClean.ttf";
    std::string err;
    CHECK(fileExists(good));
    CHECK(validateFontFile(good, 0, err));
    CHECK(!validateFontFile(good, 5, err) && !err.empty());   // not a collection: no such face

    // refusals, each with a message
    CHECK(!validateFontFile("", 0, err) && !err.empty());
    CHECK(!validateFontFile((tmp / "nope.ttf").string(), 0, err) && !err.empty());
    CHECK(!validateFontFile(tmp.string(), 0, err) && !err.empty());   // a directory
    spit(tmp / "empty.ttf", "");
    CHECK(!validateFontFile((tmp / "empty.ttf").string(), 0, err));
    spit(tmp / "text.ttf", std::string(2000, 'x') + "this is not a font");
    CHECK(!validateFontFile((tmp / "text.ttf").string(), 0, err));
    std::string rnd(4096, '\0');
    unsigned x = 12345;
    for (char& ch : rnd) { x = x * 1103515245u + 12345u; ch = (char)(x >> 16); }
    spit(tmp / "random.otf", rnd);
    CHECK(!validateFontFile((tmp / "random.otf").string(), 0, err));
    spit(tmp / "web.woff", std::string("wOFF") + std::string(4000, '\1'));
    CHECK(!validateFontFile((tmp / "web.woff").string(), 0, err) && err.find("Web fonts") != std::string::npos);
    // a real font cut short: its table directory announces tables that are not there
    const std::string whole = slurp(good);
    CHECK(whole.size() > 4000);
    spit(tmp / "cut.ttf", whole.substr(0, whole.size() / 3));
    CHECK(!validateFontFile((tmp / "cut.ttf").string(), 0, err));
    spit(tmp / "cut2.ttf", whole.substr(0, 400));
    CHECK(!validateFontFile((tmp / "cut2.ttf").string(), 0, err));
    // a font with its first glyph table zeroed: still refused or at least never crashes
    std::string mangled = whole;
    for (size_t i = 12; i < 60; i++) mangled[i] = (char)0xFF;
    spit(tmp / "mangled.ttf", mangled);
    (void)validateFontFile((tmp / "mangled.ttf").string(), 0, err);

    // importing keeps a copy under a safe name; a refusal leaves the folder alone
    // ("#", not "?": a question mark is not a legal file name character on Windows, so the copy would fail there)
    const std::string dir = (tmp / "fonts").string() + "/";
    std::string stored;
    CHECK(!importCustomFont((tmp / "text.ttf").string(), dir, stored, err));
    CHECK(!fs::exists(dir) || fs::is_empty(dir));
    fs::copy_file(good, tmp / "Pro ggy#.ttf", fs::copy_options::overwrite_existing);
    CHECK(importCustomFont((tmp / "Pro ggy#.ttf").string(), dir, stored, err));
    CHECK(stored == "Pro ggy_.ttf");
    CHECK(slurp(dir + stored) == whole);
    CHECK(validateFontFile(customPath(dir, stored), 0, err));
    // importing again over the same name is fine
    CHECK(importCustomFont((tmp / "Pro ggy#.ttf").string(), dir, stored, err));
}

static std::string readAll(const fs::path& p) { return slurp(p); }

static void testGlyphCoverage(const std::string& srcDir) {
    // the decoder
    const std::string sample = "a\xC3\xA9\xE2\x80\xA6\xE2\x98\x85\xF0\x9F\x98\x80" "z";
    std::vector<uint32_t> cps = nonAsciiCodepoints(sample);
    CHECK(cps.size() == 4 && cps[0] == 0xE9 && cps[1] == 0x2026 && cps[2] == 0x2605 && cps[3] == 0x1F600);
    CHECK(nonAsciiCodepoints("plain ascii").empty());
    CHECK(nonAsciiCodepoints("\xFF\xFE bad \xC3").empty());   // invalid / truncated sequences are skipped
    CHECK(nonAsciiCodepoints("\xE2\x28\xA1").empty());

    // the ranges the UI needs
    for (uint32_t cp : {0x20u, 0x41u, 0x7Eu, 0xE9u, 0xFCu, 0xDFu, 0x141u, 0x2013u, 0x2014u, 0x2018u, 0x2019u, 0x201Cu, 0x2026u,
                        0x2022u, 0x2190u, 0x2192u, 0x2605u, 0x2713u})
        CHECK(inRequiredRanges(cp));
    CHECK(!inRequiredRanges(0x4E2D));   // CJK is not promised by the fallback chain

    // every non-ASCII character in the app's own text is inside those ranges (so the font chain is asked for it)
    size_t files = 0, nonAscii = 0;
    for (const auto& e : fs::recursive_directory_iterator(srcDir)) {
        if (!e.is_regular_file()) continue;
        const std::string ext = e.path().extension().string();
        if (ext != ".cpp" && ext != ".h") continue;
        files++;
        for (uint32_t cp : nonAsciiCodepoints(readAll(e.path()))) {
            nonAscii++;
            if (!inRequiredRanges(cp)) {
                std::fprintf(stderr, "FAIL %s uses U+%04X, which is not in fonts::requiredRanges()\n", e.path().string().c_str(), cp);
                g_fail++;
            }
            g_checks++;
        }
    }
    CHECK(files > 20);
    std::fprintf(stderr, "[fonts] scanned %zu source files, %zu non-ASCII characters\n", files, nonAscii);

    // the strings the Settings preview draws are \x escapes in an ASCII file: decode them and check they are in range
    const std::string ui = readAll(fs::path(srcDir) / "fonts_ui.cpp");
    size_t pos = 0, escapes = 0;
    std::string decoded;
    while ((pos = ui.find("\\x", pos)) != std::string::npos) {
        const unsigned v = (unsigned)std::strtoul(ui.substr(pos + 2, 2).c_str(), nullptr, 16);
        decoded += (char)v;
        pos += 4;
        escapes++;
    }
    CHECK(escapes > 10);
    for (uint32_t cp : nonAsciiCodepoints(decoded)) CHECK(inRequiredRanges(cp));
}

// The fonts on THIS machine: the system face should cover Latin-1 + punctuation, and the chain with its fallbacks the
// arrows / star / check.  Informational unless FONTS_STRICT=1 (CI machines have whatever fonts they have).
static void testThisMachine() {
    const bool strict = std::getenv("FONTS_STRICT") != nullptr;
    std::vector<uint32_t> latin, marks;
    for (uint32_t cp = 0x20; cp <= 0x7E; cp++) latin.push_back(cp);
    for (uint32_t cp = 0xA0; cp <= 0xFF; cp++) latin.push_back(cp);
    for (uint32_t cp : {0x2013u, 0x2014u, 0x2018u, 0x2019u, 0x201Cu, 0x201Du, 0x2022u, 0x2026u}) latin.push_back(cp);
    for (uint32_t cp : {0x2190u, 0x2192u, 0x2605u, 0x2713u}) marks.push_back(cp);
    for (Role r : {Role::Ui, Role::Mono}) {
        const FontFile f = resolve(r);
        std::fprintf(stderr, "[fonts] this machine, %s: %s\n", r == Role::Ui ? "UI" : "mono",
                     f.empty() ? "(none)" : f.path.c_str());
        if (f.empty()) {
            CHECK(!strict);
            continue;
        }
        std::string err;
        const bool ok = validateFontFile(f.path, f.index, err);
        if (!ok) std::fprintf(stderr, "[fonts]   cannot use it: %s\n", err.c_str());
        CHECK(ok || !strict);
        if (!ok) continue;
        const std::vector<uint32_t> miss = missingGlyphs(f.path, f.index, latin);
        std::fprintf(stderr, "[fonts]   Latin-1 + punctuation: %zu of %zu missing\n", miss.size(), latin.size());
        CHECK(miss.size() < latin.size() / 4 || !strict);
        std::vector<uint32_t> stillMissing = missingGlyphs(f.path, f.index, marks);
        for (const FontFile& fb : candidates(Role::Fallback, hostOs(), systemProbe())) {
            if (stillMissing.empty() || !fileExists(fb.path)) continue;
            stillMissing = missingGlyphs(fb.path, fb.index, stillMissing);
        }
        std::fprintf(stderr, "[fonts]   arrows / star / check still missing after the fallbacks: %zu of %zu\n",
                     stillMissing.size(), marks.size());
    }
}

// UI scale vs pixel density: SDL's display scale already contains the density, so the UI scale divides it out
// (a Retina Mac is 2.0 / 2.0 = 1: the layout stays in points, the atlas rasterises at the density).
static void testUiScale() {
    using vj::uiscale::contentScale;
    auto near = [](float a, float b) { return std::fabs(a - b) < 1e-5f; };
    CHECK(near(contentScale(2.0f, 2.0f, 1.0f), 1.0f));     // Retina Mac
    CHECK(near(contentScale(1.0f, 1.0f, 1.0f), 1.0f));     // standard display
    CHECK(near(contentScale(2.0f, 1.0f, 1.0f), 2.0f));     // 4K Windows / Linux at 200%
    CHECK(near(contentScale(3.0f, 2.0f, 1.0f), 1.5f));     // Retina with a 150% content scale
    CHECK(near(contentScale(2.0f, 2.0f, 1.25f), 1.25f));   // the user's preference multiplies
    CHECK(near(contentScale(2.0f, 2.0f, 100.0f), 4.0f));   // preference clamped to 4
    CHECK(near(contentScale(2.0f, 2.0f, 0.01f), 0.5f));    // preference clamped to 0.5 (0.5 itself is allowed)
    CHECK(near(contentScale(0.8f, 2.0f, 1.0f), 1.0f));     // below 0.5 is nonsense: 1
    CHECK(near(contentScale(2.0f, 0.0f, 1.0f), 2.0f));     // no density reported: treated as 1
    CHECK(near(contentScale(2.0f, -1.0f, 1.0f), 2.0f));
}

int main(int argc, char** argv) {
    if (argc < 3) {
        std::fprintf(stderr, "usage: %s SRC_DIR IMGUI_DIR\n", argv[0]);
        return 2;
    }
    const std::string srcDir = argv[1], imguiDir = argv[2];
    const fs::path tmp = fs::temp_directory_path() / ("vj_fonts_test_" + std::to_string((long)SDL_GetTicks()) + "_" +
                                                      std::to_string(std::rand()));
    fs::create_directories(tmp);

    testNamesAndConfig(tmp);
    testChoices(tmp);
    testResolveMac();
    testResolveWindows();
    testResolveLinux();
    testSafeNames();
    testCustomFiles(tmp, imguiDir);
    testGlyphCoverage(srcDir);
    testUiScale();
    testThisMachine();

    std::error_code ec;
    fs::remove_all(tmp, ec);
    std::printf("fonts: %d checks, %d failures\n", g_checks, g_fail);
    return g_fail ? 1 : 0;
}
