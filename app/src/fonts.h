// User-selectable UI fonts (#833): which typeface the app draws its windows in, how big, and where the
// files for it come from.  This half has no ImGui and no GL, so the headless tests cover it; the half
// that talks to Dear ImGui's font atlas is fonts_imgui.h.
//
//   Retro    Dear ImGui's embedded ProggyClean / ProggyForever -- exactly what the app always drew.
//   System   the operating system's UI font (San Francisco, Segoe UI, the desktop's sans-serif).
//   Mono     the system monospace font (SF Mono / Menlo, Consolas, DejaVu Sans Mono) as the UI font.
//   Custom   a .ttf / .otf / .ttc the user picked; a copy is kept in <data>/fonts/.
//
// Independently of the choice, debug windows (hex dumps, disassembly) ask fonts_imgui.h for the
// monospace face, so they stay monospace whatever the UI font is.
#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace vj {

class Config;

namespace fonts {

enum class Family { Retro, System, Mono, Custom };
enum class Hinting { None, Light, Full };   // honoured by the FreeType rasteriser only
enum class Role { Ui, Mono, Fallback };     // Fallback: symbols / arrows merged behind the main face
enum class Os { Mac, Windows, Linux };

constexpr float kMinSize = 11.0f;
constexpr float kMaxSize = 24.0f;
constexpr float kDefaultSize = 13.0f;       // what the app has always used (ProggyClean's native size)

struct Settings {
    Family family = Family::Retro;
    float size = kDefaultSize;              // points at 100% scale; the display's content scale is applied on top
    Hinting hinting = Hinting::Light;
    bool antialias = true;
    std::string custom;                     // Family::Custom: file name inside <data>/fonts/

    bool operator==(const Settings& o) const {
        return family == o.family && size == o.size && hinting == o.hinting && antialias == o.antialias &&
               custom == o.custom;
    }
    bool operator!=(const Settings& o) const { return !(*this == o); }
};

// Dev / test spec "FAMILY[:SIZE]" ("system", "mono:16", "custom:My.ttf", "retro:13") applied over `s`.
bool applySpec(const std::string& spec, Settings& s);

// Config keys: ui.font.family|size|hint|aa|custom.
Settings load(const Config& c);
void store(Config& c, const Settings& s);
void erase(Config& c);                      // back to the defaults
Settings sanitized(Settings s);             // size clamped and finite; Custom with no file becomes Retro

const char* familyKey(Family f);            // "retro" ...
bool parseFamily(const std::string& s, Family& out);
const char* familyLabel(Family f);
const char* hintingKey(Hinting h);
bool parseHinting(const std::string& s, Hinting& out);
const char* hintingLabel(Hinting h);

// ---- finding the system fonts ------------------------------------------------------------------

struct FontFile {
    std::string path;
    int index = 0;                          // face inside a .ttc collection
    bool empty() const { return path.empty(); }
};

// Everything the lookup needs from the machine, injectable so each OS's rules are testable anywhere.
struct Probe {
    std::function<bool(const std::string&)> exists;          // is this a readable file?
    std::function<std::string(const std::string&)> env;      // environment variable ("" if unset)
    std::function<std::string(const std::string&)> fcMatch;  // Linux: `fc-match -f '%{file}|%{index}' PATTERN` ("" if none)
};
Probe systemProbe();
Os hostOs();

// Candidates for `role` on `os`, best first (fontconfig's answer, if it gave one, leads on Linux).
std::vector<FontFile> candidates(Role role, Os os, const Probe& p);
// The first candidate that exists, or an empty FontFile.
FontFile resolve(Role role, Os os, const Probe& p);
inline FontFile resolve(Role role) { return resolve(role, hostOs(), systemProbe()); }
// "SFNS" for /System/Library/Fonts/SFNS.ttf: what the Settings window shows.
std::string displayName(const FontFile& f);

// ---- custom font files -------------------------------------------------------------------------

constexpr size_t kMaxFontBytes = 48u * 1024u * 1024u;

// Can this file be used as the UI font?  Checks that it is a TrueType / OpenType font (not a web font
// or a bitmap one) with real outlines for basic Latin.  On false `err` says why in one sentence.
bool validateFontFile(const std::string& path, int index, std::string& err);
bool validateFontData(const unsigned char* data, size_t size, int index, std::string& err);
// Validates `src`, then keeps a copy in `dir` (created if needed) under a safe version of its name.
// `stored` is the file name to put in Settings::custom.
bool importCustomFont(const std::string& src, const std::string& dir, std::string& stored, std::string& err);
// Settings::custom -> the full path (the name must be a plain file name; no separators).
std::string customPath(const std::string& dir, const std::string& stored);
// Replaces anything that is not [A-Za-z0-9._ -()] and any leading dot; never empty.
std::string safeFileName(const std::string& name);

// ---- glyph coverage ----------------------------------------------------------------------------

struct Range {
    uint32_t lo, hi;                        // inclusive
};
// What the UI must be able to draw: ASCII, Latin-1, Latin Extended-A (game titles), General
// Punctuation, and the arrows / marks the interface uses.  Dear ImGui 1.92 rasterises glyphs on demand
// from whichever font in the chain has them, so this is a list to TEST against, not an atlas range.
const std::vector<Range>& requiredRanges();
bool inRequiredRanges(uint32_t cp);
// Non-ASCII code points in UTF-8 text (invalid bytes are skipped).
std::vector<uint32_t> nonAsciiCodepoints(const std::string& utf8);
// Of `cps`, those the font file has no glyph for.
std::vector<uint32_t> missingGlyphs(const std::string& path, int index, const std::vector<uint32_t>& cps);

}  // namespace fonts
}  // namespace vj
