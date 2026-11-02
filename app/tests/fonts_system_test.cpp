// Installed-font enumeration (fonts_system.h).  No window, no GL, no ImGui.  Fonts are synthesised (a valid sfnt
// directory with just the tables the reader looks at), so the result is the same on every machine.
//
//   * name table: English Windows names win, Mac names are read as ASCII, typographic family / style beat the legacy
//     pair, "Regular" is dropped, hidden ".SF" faces are skipped
//   * OS/2 weight + italic, post.isFixedPitch, .ttc collections, truncated / bitmap-only files refused
//   * the folder walk: recursive, de-duplicated by name, sorted, non-fonts ignored
//   * the folders worth scanning on macOS / Windows / Linux against fake environments
//   * the machine's own fonts (a smoke check; a slow host is reported, not failed)
//
//   vj_fonts_system_test
#include "fonts.h"
#include "fonts_system.h"

#include <SDL3/SDL.h>

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>
#include <string>
#include <vector>

namespace fs = std::filesystem;
using namespace vj;
using namespace vj::fonts;

static int g_checks = 0, g_fail = 0;
#define CHECK(c) do { g_checks++; if (!(c)) { g_fail++; std::fprintf(stderr, "FAIL %s:%d  %s\n", __FILE__, __LINE__, #c); } } while (0)

typedef std::vector<unsigned char> Bytes;

static void put16(Bytes& b, unsigned v) { b.push_back((unsigned char)(v >> 8)); b.push_back((unsigned char)v); }
static void put32(Bytes& b, unsigned v) { put16(b, v >> 16); put16(b, v & 0xFFFF); }

struct NameRec {
    unsigned platform, enc, lang, id;
    std::string text;   // ASCII; stored UTF-16BE for platform 3, bytes for platform 1
};

static Bytes nameTable(const std::vector<NameRec>& recs) {
    Bytes strings, entries;
    for (const NameRec& r : recs) {
        Bytes s;
        if (r.platform == 3) for (char c : r.text) { s.push_back(0); s.push_back((unsigned char)c); }
        else for (char c : r.text) s.push_back((unsigned char)c);
        put16(entries, r.platform); put16(entries, r.enc); put16(entries, r.lang); put16(entries, r.id);
        put16(entries, (unsigned)s.size()); put16(entries, (unsigned)strings.size());
        strings.insert(strings.end(), s.begin(), s.end());
    }
    Bytes t;
    put16(t, 0); put16(t, (unsigned)recs.size()); put16(t, (unsigned)(6 + entries.size()));
    t.insert(t.end(), entries.begin(), entries.end());
    t.insert(t.end(), strings.begin(), strings.end());
    return t;
}

struct FaceSpec {
    std::vector<NameRec> names;
    unsigned weight = 400;
    bool italicBit = false, fixed = false, outlines = true;
};

struct Table {
    char tag[5];
    Bytes data;
};

static std::vector<Table> faceTables(const FaceSpec& f) {
    std::vector<Table> t;
    Bytes cmap(64, 0), os2(78, 0), post(32, 0), junk(64, 0);
    os2[4] = (unsigned char)(f.weight >> 8); os2[5] = (unsigned char)f.weight;
    os2[62] = 0; os2[63] = f.italicBit ? 1 : 0;
    post[15] = f.fixed ? 1 : 0;
    t.push_back({"OS/2", os2});
    t.push_back({"cmap", cmap});
    if (f.outlines) { t.push_back({"glyf", junk}); t.push_back({"loca", junk}); }
    else t.push_back({"sbix", junk});
    t.push_back({"name", nameTable(f.names)});
    t.push_back({"post", post});
    return t;
}

// An sfnt (or a collection of several) with the table payloads placed after the directories.
static Bytes buildFont(const std::vector<FaceSpec>& faces) {
    const bool ttc = faces.size() > 1;
    std::vector<std::vector<Table>> tabs;
    for (const FaceSpec& f : faces) tabs.push_back(faceTables(f));
    size_t pos = ttc ? 12 + 4 * faces.size() : 0;
    std::vector<size_t> dirAt;
    for (auto& tt : tabs) { dirAt.push_back(pos); pos += 12 + 16 * tt.size(); }
    Bytes out;
    if (ttc) {
        out.insert(out.end(), {'t', 't', 'c', 'f'});
        put32(out, 0x00010000); put32(out, (unsigned)faces.size());
        for (size_t d : dirAt) put32(out, (unsigned)d);
    }
    std::vector<Bytes> payload;
    size_t dataAt = pos;
    for (size_t i = 0; i < tabs.size(); i++) {
        put32(out, 0x00010000); put16(out, (unsigned)tabs[i].size()); put16(out, 0); put16(out, 0); put16(out, 0);
        for (Table& t : tabs[i]) {
            out.insert(out.end(), t.tag, t.tag + 4);
            put32(out, 0); put32(out, (unsigned)dataAt); put32(out, (unsigned)t.data.size());
            payload.push_back(t.data);
            dataAt += (t.data.size() + 3) & ~(size_t)3;
        }
    }
    for (const Bytes& p : payload) {
        out.insert(out.end(), p.begin(), p.end());
        while (out.size() % 4) out.push_back(0);
    }
    while (out.size() < 300) out.push_back(0);   // the reader refuses files smaller than any real font
    return out;
}

static FaceSpec simple(const std::string& family, const std::string& style = "Regular") {
    FaceSpec f;
    f.names = {{3, 1, 0x409, 1, family}, {3, 1, 0x409, 2, style}};
    return f;
}

static void spit(const fs::path& p, const Bytes& b) {
    fs::create_directories(p.parent_path());
    std::ofstream f(p, std::ios::binary);
    f.write((const char*)b.data(), (std::streamsize)b.size());
}

static void testReadFace(const fs::path& tmp) {
    // A plain Windows-named face: family + style joined, "Regular" dropped.
    spit(tmp / "a.ttf", buildFont({simple("Plain Sans")}));
    SystemFont f;
    int n = 0;
    CHECK(readFaceInfo((tmp / "a.ttf").string(), 0, f, &n) && n == 1);
    CHECK(f.family == "Plain Sans" && f.style.empty() && f.name == "Plain Sans");
    CHECK(f.weight == 400 && !f.italic && !f.mono);
    CHECK(f.id() == makeFontId((tmp / "a.ttf").string(), 0));

    // Bold italic, fixed pitch, with a full name (nameID 4) that wins as the label.
    FaceSpec bi = simple("Code Face", "Bold Italic");
    bi.names.push_back({3, 1, 0x409, 4, "Code Face Bold Italic Full"});
    bi.weight = 700; bi.italicBit = true; bi.fixed = true;
    spit(tmp / "b.otf", buildFont({bi}));
    CHECK(readFaceInfo((tmp / "b.otf").string(), 0, f));
    CHECK(f.weight == 700 && f.italic && f.mono && f.style == "Bold Italic" && f.name == "Code Face Bold Italic Full");

    // Typographic family / style (16 / 17) beat the legacy pair; an English Windows record beats a German one.
    FaceSpec typo;
    typo.names = {{3, 1, 0x407, 1, "Deutsch"}, {3, 1, 0x409, 1, "Legacy Light"}, {3, 1, 0x409, 2, "Regular"},
                  {3, 1, 0x409, 16, "Typo Family"}, {3, 1, 0x409, 17, "Light"}};
    spit(tmp / "c.ttf", buildFont({typo}));
    CHECK(readFaceInfo((tmp / "c.ttf").string(), 0, f) && f.family == "Typo Family" && f.style == "Light" &&
          f.name == "Typo Family Light");

    // Mac-only names (platform 1) are read as ASCII.
    FaceSpec mac;
    mac.names = {{1, 0, 0, 1, "Mac Name"}, {1, 0, 0, 2, "Regular"}};
    spit(tmp / "d.ttf", buildFont({mac}));
    CHECK(readFaceInfo((tmp / "d.ttf").string(), 0, f) && f.name == "Mac Name");

    // Hidden system faces, bitmap-only fonts, junk and truncated files are refused.
    spit(tmp / "hidden.ttf", buildFont({simple(".SF Hidden")}));
    CHECK(!readFaceInfo((tmp / "hidden.ttf").string(), 0, f));
    FaceSpec bmp = simple("Bitmap Only");
    bmp.outlines = false;
    spit(tmp / "bmp.ttf", buildFont({bmp}));
    CHECK(!readFaceInfo((tmp / "bmp.ttf").string(), 0, f));
    spit(tmp / "junk.ttf", Bytes(4096, 0x41));
    CHECK(!readFaceInfo((tmp / "junk.ttf").string(), 0, f));
    Bytes cut = buildFont({simple("Cut Short")});
    cut.resize(cut.size() - 80);   // a table now ends past the end of the file
    spit(tmp / "cut.ttf", cut);
    CHECK(!readFaceInfo((tmp / "cut.ttf").string(), 0, f));
    CHECK(!readFaceInfo((tmp / "missing.ttf").string(), 0, f));
    CHECK(!readFaceInfo((tmp / "a.ttf").string(), 1, f));   // no second face in a plain font

    // A collection: every face is readable, an index past the end is not.
    spit(tmp / "multi.ttc", buildFont({simple("Collection", "Regular"), simple("Collection", "Bold")}));
    int faces = 0;
    SystemFont f0, f1;
    CHECK(readFaceInfo((tmp / "multi.ttc").string(), 0, f0, &faces) && faces == 2);
    CHECK(readFaceInfo((tmp / "multi.ttc").string(), 1, f1) && f1.style == "Bold" && f1.index == 1);
    CHECK(!readFaceInfo((tmp / "multi.ttc").string(), 2, f1));
}

static void testScan(const fs::path& tmp) {
    const fs::path root = tmp / "scan";
    spit(root / "sys" / "Zeta.ttf", buildFont({simple("Zeta")}));
    FaceSpec bold = simple("Alpha", "Bold");
    bold.weight = 700;
    spit(root / "sys" / "deep" / "er" / "AlphaBold.otf", buildFont({bold}));
    spit(root / "sys" / "Alpha.ttf", buildFont({simple("Alpha")}));
    spit(root / "sys" / "readme.txt", Bytes(500, 'x'));
    spit(root / "sys" / ".hidden" / "Ghost.ttf", buildFont({simple("Ghost")}));
    spit(root / "sys" / "multi.ttc", buildFont({simple("Mid"), simple("Mid", "Italic")}));
    spit(root / "user" / "AlphaCopy.ttf", buildFont({simple("Alpha")}));   // same face again: listed once

    const std::vector<SystemFont> v = scanFontDirs({(root / "sys").string(), (root / "user").string()});
    std::vector<std::string> names;
    for (const SystemFont& f : v) names.push_back(f.name);
    const std::vector<std::string> want = {"Alpha", "Alpha Bold", "Mid", "Mid Italic", "Zeta"};
    CHECK(names == want);
    CHECK(findSystemFont(v, v[1].id()) == &v[1]);
    CHECK(findSystemFont(v, "/no/such.ttf|0") == nullptr);
    CHECK(findSystemFont(v, "garbage") == nullptr);
    CHECK(scanFontDirs({(root / "nothing").string()}).empty());
    CHECK(scanFontDirs({(root / "sys").string()}, 2).size() < v.size());   // the file cap bounds the walk
}

static void testIds() {
    FontFile f;
    CHECK(parseFontId("/a/b c/d.ttc|3", f) && f.path == "/a/b c/d.ttc" && f.index == 3);
    CHECK(parseFontId("C:\\Windows\\Fonts\\a|b.ttf|0", f) && f.path == "C:\\Windows\\Fonts\\a|b.ttf" && f.index == 0);
    CHECK(!parseFontId("", f) && !parseFontId("|1", f) && !parseFontId("/a.ttf", f) && !parseFontId("/a.ttf|x", f) &&
          !parseFontId("/a.ttf|-1", f) && !parseFontId("/a.ttf|99999", f));
    CHECK(makeFontId("/x.ttf", -4) == "/x.ttf|0");
}

static void testDirs() {
    std::map<std::string, std::string> env;
    Probe p;
    p.env = [&env](const std::string& k) { auto it = env.find(k); return it == env.end() ? std::string() : it->second; };

    env["HOME"] = "/Users/me";
    auto mac = systemFontDirs(Os::Mac, p);
    CHECK(mac.size() == 4 && mac[0] == "/System/Library/Fonts/" && mac[3] == "/Users/me/Library/Fonts/");

    env.clear();
    env["WINDIR"] = "D:\\Win";
    env["LOCALAPPDATA"] = "C:\\Users\\me\\AppData\\Local";
    auto win = systemFontDirs(Os::Windows, p);
    CHECK(win.size() == 2 && win[0] == "D:\\Win/Fonts/" && win[1] == "C:\\Users\\me\\AppData\\Local/Microsoft/Windows/Fonts/");
    env.clear();
    CHECK(systemFontDirs(Os::Windows, p)[0] == "C:\\Windows/Fonts/");

    env.clear();
    env["HOME"] = "/home/me";
    env["XDG_DATA_DIRS"] = "/usr/local/share:/usr/share:/opt/x";
    auto lin = systemFontDirs(Os::Linux, p);
    auto has = [&](const char* d) { for (auto& s : lin) if (s == d) return true; return false; };
    CHECK(has("/usr/share/fonts/") && has("/home/me/.local/share/fonts/") && has("/home/me/.fonts/") && has("/opt/x/fonts/"));
    int shareCount = 0;
    for (auto& s : lin) if (s == "/usr/share/fonts/") shareCount++;
    CHECK(shareCount == 1);   // no duplicates
}

// The machine's own fonts: a smoke check of the worker-thread plumbing (start, idempotence, ready, list).  The
// correctness of the scan (names, weights, dedupe, sorting) is asserted above on synthesised fonts, so this never
// needs a fast host: the full scan takes ~2 s on a normal machine but 32-53 s on GitHub's hosted Windows runners (#1078).
// When the scan has not finished within the wait bound it is reported as slow, not failed -- the picker never blocks on
// it either (fonts_ui.cpp draws what it has) -- and the process still waits for the worker at exit.  FONTS_STRICT=1
// (a developer machine) restores the hard check.
static void testMachine() {
    const bool strict = std::getenv("FONTS_STRICT") != nullptr;
    const int boundMs = strict ? 120000 : 30000;
    const auto t0 = std::chrono::steady_clock::now();
    systemFontsStart();
    systemFontsStart();   // idempotent
    for (int i = 0; i < boundMs / 50 && !systemFontsReady(); i++) SDL_Delay(50);
    const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
    auto list = systemFonts();
    if (!list) {
        std::printf("this machine: font scan still running after %.0f ms (slow host) -- not a failure, see #1078\n", ms);
        CHECK(!strict);
        return;
    }
    int mono = 0;
    for (const SystemFont& f : *list) {
        CHECK(!f.name.empty() && !f.path.empty());
        if (f.mono) mono++;
    }
    std::printf("this machine: %zu faces (%d fixed-pitch) in %.0f ms\n", list->size(), mono, ms);
    if (strict) CHECK(!list->empty());
}

int main() {
    SDL_Init(0);
    const fs::path tmp = fs::temp_directory_path() / ("vj_fonts_system_test_" + std::to_string(std::rand()));
    fs::create_directories(tmp);
    testReadFace(tmp);
    testScan(tmp);
    testIds();
    testDirs();
    testMachine();
    std::error_code ec;
    fs::remove_all(tmp, ec);
    std::printf("fonts_system: %d checks, %d failed\n", g_checks, g_fail);
    return g_fail ? 1 : 0;
}
