#include "fonts.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include <SDL3/SDL.h>

#include "config.h"
#include "fonts_system.h"
#include "paths.h"

// Dear ImGui's stb_truetype, as a private (static) copy: used only to look inside font files, so a bad
// file is rejected here instead of tripping an assert inside the atlas.
#if defined(__GNUC__) || defined(__clang__)
#  pragma GCC diagnostic push
#  pragma GCC diagnostic ignored "-Wunused-function"
#  pragma GCC diagnostic ignored "-Wunused-parameter"
#  pragma GCC diagnostic ignored "-Wsign-compare"
#  pragma GCC diagnostic ignored "-Wmissing-field-initializers"
#endif
#define STBTT_STATIC
#define STB_TRUETYPE_IMPLEMENTATION
#include "imstb_truetype.h"
#if defined(__GNUC__) || defined(__clang__)
#  pragma GCC diagnostic pop
#endif

namespace vj {
namespace fonts {

// ---------------------------------------------------------------------------------------------
// names and config
// ---------------------------------------------------------------------------------------------
const char* familyKey(Family f) {
    switch (f) {
    case Family::Retro: return "retro";
    case Family::System: return "system";
    case Family::Mono: return "mono";
    case Family::Custom: return "custom";
    case Family::Installed: return "installed";
    }
    return "retro";
}

bool parseFamily(const std::string& s, Family& out) {
    static const Family all[] = {Family::Retro, Family::System, Family::Mono, Family::Custom, Family::Installed};
    for (Family f : all)
        if (s == familyKey(f)) {
            out = f;
            return true;
        }
    return false;
}

const char* familyLabel(Family f) {
    switch (f) {
    case Family::Retro: return "Retro (pixel font)";
    case Family::System: return "System UI";
    case Family::Mono: return "Monospace (system)";
    case Family::Custom: return "Custom file...";
    case Family::Installed: return "Installed font";
    }
    return "Retro";
}

const char* hintingKey(Hinting h) {
    switch (h) {
    case Hinting::None: return "none";
    case Hinting::Light: return "light";
    case Hinting::Full: return "full";
    }
    return "light";
}

bool parseHinting(const std::string& s, Hinting& out) {
    static const Hinting all[] = {Hinting::None, Hinting::Light, Hinting::Full};
    for (Hinting h : all)
        if (s == hintingKey(h)) {
            out = h;
            return true;
        }
    return false;
}

const char* hintingLabel(Hinting h) {
    switch (h) {
    case Hinting::None: return "None (smoothest)";
    case Hinting::Light: return "Light (recommended)";
    case Hinting::Full: return "Full (sharpest)";
    }
    return "Light";
}

namespace {

// Drops empties and repeats (keeping the first), then caps the length.
void tidyList(std::vector<std::string>& v, size_t cap) {
    std::vector<std::string> out;
    for (const std::string& e : v) {
        if (e.empty() || e.find('\n') != std::string::npos || e.find('\r') != std::string::npos) continue;
        bool dup = false;
        for (const std::string& o : out)
            if (o == e) {
                dup = true;
                break;
            }
        if (!dup) out.push_back(e);
        if (out.size() >= cap) break;
    }
    v.swap(out);
}

}  // namespace

Settings sanitized(Settings s) {
    if (!std::isfinite(s.size)) s.size = kDefaultSize;
    s.size = std::min(kMaxSize, std::max(kMinSize, s.size));
    if (s.family == Family::Custom && s.custom.empty()) s.family = Family::Retro;
    FontFile probe;
    if (s.family == Family::Installed && !parseFontId(s.installed, probe)) s.family = Family::Retro;
    if (!s.installed.empty() && !parseFontId(s.installed, probe)) s.installed.clear();
    if (s.debugFont != "@retro" && !s.debugFont.empty() && !parseFontId(s.debugFont, probe)) s.debugFont.clear();
    tidyList(s.favorites, kMaxFavorites);
    tidyList(s.recents, kMaxRecents);
    return s;
}

bool applySpec(const std::string& spec, Settings& s) {
    std::string fam = spec, arg;
    const size_t colon = spec.find(':');
    if (colon != std::string::npos) {
        fam = spec.substr(0, colon);
        arg = spec.substr(colon + 1);
    }
    Settings n = s;
    if (!parseFamily(fam, n.family)) return false;
    if (!arg.empty()) {
        if (n.family == Family::Custom && !std::isdigit((unsigned char)arg[0])) {
            n.custom = arg;
        } else if (n.family == Family::Installed && !std::isdigit((unsigned char)arg[0])) {
            FontFile ff;   // "installed:/path/Font.ttf" or "installed:/path/Fonts.ttc|1"
            n.installed = parseFontId(arg, ff) ? arg : makeFontId(arg, 0);
        } else {
            char* end = nullptr;
            const float v = (float)std::strtod(arg.c_str(), &end);
            if (end == arg.c_str() || !std::isfinite(v)) return false;
            n.size = v;
        }
    }
    s = sanitized(n);
    return true;
}

namespace {

// One key per entry (ids hold paths, so no separator is safe): <prefix>0, <prefix>1, ...
std::vector<std::string> loadList(const Config& c, const std::string& prefix) {
    std::vector<std::string> out;
    for (int i = 0; i < 4096; i++) {
        const std::string v = c.get(prefix + std::to_string(i), "");
        if (v.empty()) break;
        out.push_back(v);
    }
    return out;
}

void storeList(Config& c, const std::string& prefix, const std::vector<std::string>& v) {
    if (loadList(c, prefix) == v) return;   // untouched: do not dirty the file
    for (const auto& kv : c.withPrefix(prefix)) c.erase(prefix + kv.first);
    for (size_t i = 0; i < v.size(); i++) c.set(prefix + std::to_string(i), v[i]);
}

}  // namespace

Settings load(const Config& c) {
    Settings s;
    parseFamily(c.get("ui.font.family", "retro"), s.family);
    s.size = c.getFloat("ui.font.size", kDefaultSize);
    parseHinting(c.get("ui.font.hint", "light"), s.hinting);
    s.antialias = c.getBool("ui.font.aa", true);
    s.custom = c.get("ui.font.custom", "");
    s.installed = c.get("ui.font.os", "");
    s.debugFont = c.get("ui.font.debug", "");
    s.favorites = loadList(c, "ui.font.fav.");
    s.recents = loadList(c, "ui.font.recent.");
    return sanitized(s);
}

void store(Config& c, const Settings& in) {
    const Settings s = sanitized(in);
    c.set("ui.font.family", familyKey(s.family));
    c.setFloat("ui.font.size", s.size);
    c.set("ui.font.hint", hintingKey(s.hinting));
    c.setBool("ui.font.aa", s.antialias);
    // The file name stays in the config even while another face is selected, so switching back to
    // "Custom" finds the last file again.
    if (s.custom.empty()) c.erase("ui.font.custom");
    else c.set("ui.font.custom", s.custom);
    if (s.installed.empty()) c.erase("ui.font.os");
    else c.set("ui.font.os", s.installed);
    if (s.debugFont.empty()) c.erase("ui.font.debug");
    else c.set("ui.font.debug", s.debugFont);
    storeList(c, "ui.font.fav.", s.favorites);
    storeList(c, "ui.font.recent.", s.recents);
}

void erase(Config& c) {
    c.erase("ui.font.family");
    c.erase("ui.font.size");
    c.erase("ui.font.hint");
    c.erase("ui.font.aa");
    c.erase("ui.font.custom");
    c.erase("ui.font.os");
    c.erase("ui.font.debug");
    storeList(c, "ui.font.fav.", {});
    storeList(c, "ui.font.recent.", {});
}

// ---------------------------------------------------------------------------------------------
// the font list: choices, favorites, recents
// ---------------------------------------------------------------------------------------------
std::string choiceId(const Settings& s) {
    switch (s.family) {
    case Family::Retro: return "@retro";
    case Family::System: return "@system";
    case Family::Mono: return "@mono";
    case Family::Custom: return "@custom:" + s.custom;
    case Family::Installed: return s.installed;
    }
    return "@retro";
}

bool applyChoice(Settings& s, const std::string& id) {
    if (id == "@retro") s.family = Family::Retro;
    else if (id == "@system") s.family = Family::System;
    else if (id == "@mono") s.family = Family::Mono;
    else if (id.compare(0, 8, "@custom:") == 0 && id.size() > 8) {
        s.family = Family::Custom;
        s.custom = id.substr(8);
    } else {
        FontFile ff;
        if (!parseFontId(id, ff)) return false;
        s.family = Family::Installed;
        s.installed = id;
    }
    return true;
}

bool isFavorite(const Settings& s, const std::string& id) {
    return std::find(s.favorites.begin(), s.favorites.end(), id) != s.favorites.end();
}

void setFavorite(Settings& s, const std::string& id, bool on) {
    auto it = std::find(s.favorites.begin(), s.favorites.end(), id);
    if (on) {
        if (it == s.favorites.end() && !id.empty()) s.favorites.insert(s.favorites.begin(), id);
        if (s.favorites.size() > kMaxFavorites) s.favorites.resize(kMaxFavorites);
    } else if (it != s.favorites.end()) {
        s.favorites.erase(it);
    }
}

void noteRecent(Settings& s, const std::string& id) {
    if (id.empty() || id == "@retro" || id == "@system" || id == "@mono") return;
    auto it = std::find(s.recents.begin(), s.recents.end(), id);
    if (it != s.recents.end()) s.recents.erase(it);
    s.recents.insert(s.recents.begin(), id);
    if (s.recents.size() > kMaxRecents) s.recents.resize(kMaxRecents);
}

void forgetChoice(Settings& s, const std::string& id) {
    setFavorite(s, id, false);
    auto it = std::find(s.recents.begin(), s.recents.end(), id);
    if (it != s.recents.end()) s.recents.erase(it);
}

// ---------------------------------------------------------------------------------------------
// the machine
// ---------------------------------------------------------------------------------------------
Os hostOs() {
#if defined(_WIN32)
    return Os::Windows;
#elif defined(__APPLE__)
    return Os::Mac;
#else
    return Os::Linux;
#endif
}

namespace {

bool hasFontExtension(const std::string& p) {
    const size_t dot = p.find_last_of('.');
    if (dot == std::string::npos) return false;
    std::string e = p.substr(dot + 1);
    for (char& ch : e) ch = (char)std::tolower((unsigned char)ch);
    return e == "ttf" || e == "otf" || e == "ttc" || e == "otc";
}

#ifndef _WIN32
std::string runFcMatch(const std::string& pattern) {
    // The pattern only ever comes from our own constants ("sans-serif", "monospace").
    const std::string cmd = "fc-match -f '%{file}|%{index}' " + pattern + " 2>/dev/null";
    FILE* f = popen(cmd.c_str(), "r");
    if (!f) return "";
    char buf[1024];
    std::string out;
    while (std::fgets(buf, sizeof buf, f)) out += buf;
    pclose(f);
    while (!out.empty() && (out.back() == '\n' || out.back() == '\r')) out.pop_back();
    return out;
}
#endif

std::string join(Os os, const std::string& dir, const std::string& name) {
    if (dir.empty()) return name;
    const char sep = os == Os::Windows ? '\\' : '/';
    if (dir.back() == '/' || dir.back() == '\\') return dir + name;
    return dir + sep + name;
}

void add(std::vector<FontFile>& v, const std::string& path, int index = 0) {
    for (const FontFile& f : v)
        if (f.path == path && f.index == index) return;
    FontFile f;
    f.path = path;
    f.index = index;
    v.push_back(f);
}

void addDirs(std::vector<FontFile>& v, const char* const* dirs, size_t nd, const char* const* names, size_t nn) {
    for (size_t n = 0; n < nn; n++)
        for (size_t d = 0; d < nd; d++) add(v, std::string(dirs[d]) + names[n]);
}

}  // namespace

Probe systemProbe() {
    Probe p;
    p.exists = [](const std::string& path) { return fileExists(path); };
    p.env = [](const std::string& k) {
        const char* v = std::getenv(k.c_str());
        return std::string(v ? v : "");
    };
#ifdef _WIN32
    p.fcMatch = [](const std::string&) { return std::string(); };
#else
    p.fcMatch = runFcMatch;
#endif
    return p;
}

std::vector<FontFile> candidates(Role role, Os os, const Probe& p) {
    std::vector<FontFile> v;
    switch (os) {
    case Os::Mac: {
        const std::string sys = "/System/Library/Fonts/", sup = "/System/Library/Fonts/Supplemental/";
        if (role == Role::Ui) {
            // SF (the system font since 10.11) is SFNS.ttf: a variable font whose default instance is Regular.
            add(v, sys + "SFNS.ttf");
            add(v, sys + "HelveticaNeue.ttc");
            add(v, sys + "Helvetica.ttc");
            add(v, sup + "Arial.ttf");
            add(v, sys + "Geneva.ttf");
        } else if (role == Role::Mono) {
            add(v, sys + "SFNSMono.ttf");
            add(v, sys + "Menlo.ttc");
            add(v, sys + "Monaco.ttf");
            add(v, sys + "Courier.ttc");
        } else {
            add(v, sys + "Apple Symbols.ttf");
            add(v, sys + "ZapfDingbats.ttf");
        }
        break;
    }
    case Os::Windows: {
        std::string root = p.env ? p.env("WINDIR") : "";
        if (root.empty() && p.env) root = p.env("SystemRoot");
        if (root.empty()) root = "C:\\Windows";
        const std::string dir = join(os, root, "Fonts");
        auto at = [&](const char* name) { add(v, join(os, dir, name)); };
        if (role == Role::Ui) {
            at("segoeui.ttf");
            at("tahoma.ttf");
            at("arial.ttf");
            at("verdana.ttf");
        } else if (role == Role::Mono) {
            at("consola.ttf");
            at("lucon.ttf");
            at("cour.ttf");
        } else {
            at("seguisym.ttf");
            at("segoeui.ttf");
        }
        break;
    }
    case Os::Linux: {
        // Where the common distributions put DejaVu / Noto / Liberation.
        static const char* const dirs[] = {
            "/usr/share/fonts/truetype/dejavu/", "/usr/share/fonts/TTF/", "/usr/share/fonts/dejavu/",
            "/usr/share/fonts/dejavu-sans-fonts/", "/usr/share/fonts/truetype/noto/", "/usr/share/fonts/noto/",
            "/usr/share/fonts/google-noto/", "/usr/share/fonts/opentype/noto/", "/usr/share/fonts/truetype/liberation/",
            "/usr/share/fonts/liberation/", "/usr/share/fonts/liberation-sans/", "/usr/share/fonts/liberation-mono/"};
        const size_t nd = sizeof dirs / sizeof dirs[0];
        if (role == Role::Ui || role == Role::Mono) {
            // The desktop's own choice first (fontconfig honours the user's configuration).
            std::string m = p.fcMatch ? p.fcMatch(role == Role::Ui ? "sans-serif" : "monospace") : "";
            if (!m.empty()) {
                int idx = 0;
                std::string file = m;
                const size_t bar = m.rfind('|');
                if (bar != std::string::npos) {
                    file = m.substr(0, bar);
                    idx = std::atoi(m.c_str() + bar + 1);
                }
                if (!file.empty() && hasFontExtension(file)) add(v, file, idx < 0 ? 0 : idx);
            }
        }
        if (role == Role::Ui) {
            static const char* const names[] = {"DejaVuSans.ttf", "NotoSans-Regular.ttf", "LiberationSans-Regular.ttf",
                                                "Roboto-Regular.ttf"};
            addDirs(v, dirs, nd, names, sizeof names / sizeof names[0]);
        } else if (role == Role::Mono) {
            static const char* const names[] = {"DejaVuSansMono.ttf", "NotoSansMono-Regular.ttf",
                                                "LiberationMono-Regular.ttf"};
            addDirs(v, dirs, nd, names, sizeof names / sizeof names[0]);
        } else {
            static const char* const names[] = {"DejaVuSans.ttf", "NotoSansSymbols2-Regular.ttf",
                                                "NotoSansSymbols-Regular.ttf"};
            addDirs(v, dirs, nd, names, sizeof names / sizeof names[0]);
        }
        break;
    }
    }
    return v;
}

FontFile resolve(Role role, Os os, const Probe& p) {
    for (const FontFile& f : candidates(role, os, p))
        if (p.exists && p.exists(f.path)) return f;
    return FontFile();
}

std::string displayName(const FontFile& f) {
    std::string stem = pathStem(f.path);
    // pathStem copes with both separators; a Windows path seen from a test on another OS keeps its prefix
    const size_t bs = stem.find_last_of('\\');
    if (bs != std::string::npos) stem = stem.substr(bs + 1);
    return stem;
}

// ---------------------------------------------------------------------------------------------
// custom font files
// ---------------------------------------------------------------------------------------------
namespace {

uint32_t be16(const unsigned char* p) { return ((uint32_t)p[0] << 8) | p[1]; }
uint32_t be32(const unsigned char* p) { return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3]; }

// stb_truetype does not bounds-check what it reads, so a truncated or hostile file is checked here first:
// every table the font directory announces must lie inside the file, and the tables we rely on must exist.
bool sfntTablesSane(const unsigned char* data, size_t size, size_t off, std::string& err) {
    if (off + 12 > size) {
        err = "The font file is truncated.";
        return false;
    }
    const uint32_t n = be16(data + off + 4);
    if (n == 0 || n > 256 || off + 12 + (size_t)n * 16 > size) {
        err = "The font file is damaged.";
        return false;
    }
    bool cmap = false, head = false, glyf = false, loca = false, cff = false;
    for (uint32_t i = 0; i < n; i++) {
        const unsigned char* e = data + off + 12 + (size_t)i * 16;
        const uint32_t o = be32(e + 8), l = be32(e + 12);
        if ((size_t)o > size || (size_t)l > size - (size_t)o) {
            err = "The font file is truncated or damaged.";
            return false;
        }
        if (!std::memcmp(e, "cmap", 4)) cmap = true;
        else if (!std::memcmp(e, "head", 4)) head = true;
        else if (!std::memcmp(e, "glyf", 4)) glyf = true;
        else if (!std::memcmp(e, "loca", 4)) loca = true;
        else if (!std::memcmp(e, "CFF ", 4)) cff = true;
    }
    if (!cmap || !head || !((glyf && loca) || cff)) {
        err = "The font has no usable outlines (a bitmap or colour-only font?).";
        return false;
    }
    return true;
}

}  // namespace

bool validateFontData(const unsigned char* data, size_t size, int index, std::string& err) {
    if (!data || size < 256) {
        err = "The file is too small to be a font.";
        return false;
    }
    if (size > kMaxFontBytes) {
        err = "The file is larger than 48 MB.";
        return false;
    }
    // Web fonts ("wOFF" / "wOF2") and bitmap formats are not sfnt containers: stb_truetype would
    // not find tables in them, but say so in words.
    if (size >= 4 && (!std::memcmp(data, "wOFF", 4) || !std::memcmp(data, "wOF2", 4))) {
        err = "Web fonts (.woff / .woff2) are not supported; use a .ttf or .otf file.";
        return false;
    }
    const int off = stbtt_GetFontOffsetForIndex(data, index);
    if (off < 0) {
        err = index == 0 ? "This is not a TrueType / OpenType font file."
                         : "That font collection does not have such a face.";
        return false;
    }
    if (!sfntTablesSane(data, size, (size_t)off, err)) return false;
    stbtt_fontinfo info;
    std::memset(&info, 0, sizeof info);
    if (!stbtt_InitFont(&info, data, off)) {
        err = "The font file is damaged or uses an unsupported format.";
        return false;
    }
    if (stbtt_FindGlyphIndex(&info, 'A') == 0 || stbtt_FindGlyphIndex(&info, 'e') == 0) {
        err = "The font has no Latin letters, so the interface could not be drawn with it.";
        return false;
    }
    // The glyph has to have real outlines (a bitmap-only font has empty ones).
    stbtt_vertex* verts = nullptr;
    const int nv = stbtt_GetGlyphShape(&info, stbtt_FindGlyphIndex(&info, 'A'), &verts);
    if (verts) stbtt_FreeShape(&info, verts);
    if (nv <= 0) {
        err = "The font has no outlines (a bitmap font?).";
        return false;
    }
    return true;
}

bool validateFontFile(const std::string& path, int index, std::string& err) {
    if (path.empty()) {
        err = "No file chosen.";
        return false;
    }
    SDL_PathInfo info;
    if (!SDL_GetPathInfo(path.c_str(), &info) || info.type != SDL_PATHTYPE_FILE) {
        err = "That file does not exist.";
        return false;
    }
    if (info.size > kMaxFontBytes) {
        err = "The file is larger than 48 MB.";
        return false;
    }
    std::vector<unsigned char> data;
    if (!readFile(path, data)) {
        err = "The file cannot be read.";
        return false;
    }
    return validateFontData(data.data(), data.size(), index, err);
}

std::string safeFileName(const std::string& name) {
    std::string out;
    for (char ch : name) {
        const unsigned char u = (unsigned char)ch;
        if (std::isalnum(u) || ch == '.' || ch == '_' || ch == '-' || ch == ' ' || ch == '(' || ch == ')') out += ch;
        else out += '_';
    }
    size_t lead = 0;
    while (lead < out.size() && (out[lead] == '.' || out[lead] == ' ')) lead++;
    out.erase(0, lead);
    if (out.size() > 120) out.resize(120);
    if (out.empty()) out = "font.ttf";
    return out;
}

std::string customPath(const std::string& dir, const std::string& stored) {
    if (stored.empty() || stored.find('/') != std::string::npos || stored.find('\\') != std::string::npos ||
        stored.find("..") != std::string::npos)
        return "";
    return dir + stored;   // dir ends with a separator
}

bool importCustomFont(const std::string& src, const std::string& dir, std::string& stored, std::string& err) {
    if (!validateFontFile(src, 0, err)) return false;
    std::vector<unsigned char> data;
    if (!readFile(src, data)) {
        err = "The file cannot be read.";
        return false;
    }
    std::string base = src;
    const size_t slash = base.find_last_of("/\\");
    if (slash != std::string::npos) base = base.substr(slash + 1);
    base = safeFileName(base);
    if (!hasFontExtension(base)) base += ".ttf";
    SDL_CreateDirectory(dir.c_str());
    const std::string dst = dir + base;
    if (!writeFileAtomic(dst, data.data(), data.size())) {
        err = "Could not copy the font into the app's data folder.";
        return false;
    }
    stored = base;
    return true;
}

// ---------------------------------------------------------------------------------------------
// glyph coverage
// ---------------------------------------------------------------------------------------------
const std::vector<Range>& requiredRanges() {
    static const std::vector<Range> r = {
        {0x20, 0x7E},      // ASCII
        {0xA0, 0xFF},      // Latin-1 Supplement: Western European game titles and names
        {0x100, 0x17F},    // Latin Extended-A
        {0x2010, 0x2027},  // General Punctuation: dashes, quotes, bullets, ellipsis
        {0x2030, 0x203A},  // per mille, primes, guillemets
        {0x2190, 0x2193},  // arrows
        {0x2605, 0x2606},  // black / white star (favourites)
        {0x2713, 0x2713},  // check mark
        {0x25B6, 0x25B6},  // play triangle
    };
    return r;
}

bool inRequiredRanges(uint32_t cp) {
    for (const Range& r : requiredRanges())
        if (cp >= r.lo && cp <= r.hi) return true;
    return false;
}

std::vector<uint32_t> nonAsciiCodepoints(const std::string& s) {
    std::vector<uint32_t> out;
    size_t i = 0;
    while (i < s.size()) {
        const unsigned char c = (unsigned char)s[i];
        if (c < 0x80) {
            i++;
            continue;
        }
        int len = 0;
        uint32_t cp = 0;
        if ((c & 0xE0) == 0xC0) { len = 2; cp = c & 0x1F; }
        else if ((c & 0xF0) == 0xE0) { len = 3; cp = c & 0x0F; }
        else if ((c & 0xF8) == 0xF0) { len = 4; cp = c & 0x07; }
        else { i++; continue; }
        if (i + (size_t)len > s.size()) break;
        bool ok = true;
        for (int k = 1; k < len; k++) {
            const unsigned char cc = (unsigned char)s[i + (size_t)k];
            if ((cc & 0xC0) != 0x80) { ok = false; break; }
            cp = (cp << 6) | (cc & 0x3F);
        }
        if (ok) out.push_back(cp);
        i += ok ? (size_t)len : 1;
    }
    return out;
}

bool dataCoversText(const unsigned char* data, size_t size, int index, const std::string& utf8) {
    if (!data || size < 256) return false;
    const int off = stbtt_GetFontOffsetForIndex(data, index);
    stbtt_fontinfo info;
    std::memset(&info, 0, sizeof info);
    // The caller validated the file (validateFontData) before this is asked, so the tables are in bounds.
    if (off < 0 || !stbtt_InitFont(&info, data, off)) return false;
    for (unsigned char c : utf8)
        if (c > 0x20 && c < 0x7F && stbtt_FindGlyphIndex(&info, c) == 0) return false;
    for (uint32_t cp : nonAsciiCodepoints(utf8))
        if (stbtt_FindGlyphIndex(&info, (int)cp) == 0) return false;
    return true;
}

std::vector<uint32_t> missingGlyphs(const std::string& path, int index, const std::vector<uint32_t>& cps) {
    std::vector<uint32_t> missing;
    std::vector<unsigned char> data;
    stbtt_fontinfo info;
    std::memset(&info, 0, sizeof info);
    const int off = readFile(path, data) ? stbtt_GetFontOffsetForIndex(data.data(), index) : -1;
    if (off < 0 || !stbtt_InitFont(&info, data.data(), off)) return cps;
    for (uint32_t cp : cps)
        if (stbtt_FindGlyphIndex(&info, (int)cp) == 0) missing.push_back(cp);
    return missing;
}

}  // namespace fonts
}  // namespace vj
