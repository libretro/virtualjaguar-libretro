// Custom themes (#833): a handful of semantic colours and two style values over Dark or Light, kept as files,
// shared between copies of the app, exported and imported.
//
// A theme is a `Custom`: eleven colours (Key), a rounding and a spacing scale.  `paletteFor` turns it into the
// palette the windows draw with (theme.h) and theme_style.cpp turns it into the Dear ImGui style, deriving the
// many widget colours from the few the user edits.  Nothing here knows about fonts: sizes are scaled by the
// caller's UI scale, so a font-size setting elsewhere composes with it.
//
// File: <data>/themes/<name>.ini, the app's plain key=value format (config.h), written atomically:
//     vjtheme=1
//     name=My theme
//     base=dark | light
//     color.<key>=#RRGGBB        (every Key; one that is missing falls back to the base theme's)
//     style.rounding=4           (0..12 px at a UI scale of 1)
//     style.spacing=1.00         (0.75..1.5, a multiple of the stock padding and spacing)
// Unknown keys are ignored; a file without vjtheme / name / base, a colour that is not #RRGGBB, or a number out
// of range is refused with a message.  A copy of the app that is not the first one on its data folder (#902)
// only reads the folder.
#pragma once

#include <string>
#include <vector>

#include "theme.h"

namespace vj {
namespace theme {

enum Key {
    KeyBackground,   // the library home screen and the welcome screen
    KeyWindow,       // window and menu surfaces
    KeyFrame,        // inputs, check boxes, sliders
    KeyText,
    KeyMuted,        // hints, status, "empty"
    KeyAccent,       // buttons, selections, check marks, hover outlines
    KeyWarning,
    KeyError,
    KeySuccess,
    KeyGold,         // hardcore / done
    KeyCard,         // a game tile's card, a save state's box
    KeyCount
};

struct KeyInfo {
    const char* id;      // the file key: color.<id>
    const char* label;   // the editor's label
};
const KeyInfo& keyInfo(Key k);

constexpr float kDefaultRounding = 4.0f, kMinRounding = 0.0f, kMaxRounding = 12.0f;
constexpr float kDefaultSpacing = 1.0f, kMinSpacing = 0.75f, kMaxSpacing = 1.5f;

struct Custom {
    std::string name;
    Scheme base = Scheme::Dark;
    Color colors[KeyCount];
    float rounding = kDefaultRounding;
    float spacing = kDefaultSpacing;

    Custom() : colors{Color(0, 0, 0), Color(0, 0, 0), Color(0, 0, 0), Color(0, 0, 0), Color(0, 0, 0), Color(0, 0, 0),
                      Color(0, 0, 0), Color(0, 0, 0), Color(0, 0, 0), Color(0, 0, 0), Color(0, 0, 0)} {}
    // Dark or Light as a starting point: its colours and the stock style values.
    static Custom fromBase(Scheme base, const std::string& name);
    bool operator==(const Custom& o) const;
    bool operator!=(const Custom& o) const { return !(*this == o); }
};

// The palette the windows draw with, and whether the theme reads as dark (by the luminance of its window colour).
Palette paletteFor(const Custom& c);
Scheme schemeOf(const Custom& c);

// A name usable as a file name and a combo entry: trimmed, at most 40 characters of letters, digits, space and
// - _ . ( ); anything else becomes '_'.  "" when nothing usable is left.
std::string sanitizeName(const std::string& s);

// ---- files ----------------------------------------------------------------------------------------------
// `text` is a theme file's content.  False with `err` (one line, shown to the user) when it is not a valid theme.
bool parseTheme(const std::string& text, Custom& out, std::string& err);
std::string serializeTheme(const Custom& c);
// Reads / writes one file (atomic write).  Files over 64 KiB are refused.
bool loadThemeFile(const std::string& path, Custom& out, std::string& err);
bool saveThemeFile(const Custom& c, const std::string& path, std::string& err);

// <dir>/<name>.ini for each theme.  readOnly (a secondary copy, #902): every write is refused.
class Store {
public:
    Store(const std::string& dir, bool readOnly) : dir_(dir), readOnly_(readOnly) {}
    const std::string& dir() const { return dir_; }
    bool readOnly() const { return readOnly_; }
    std::vector<Custom> list() const;                       // valid files only, by name
    bool load(const std::string& name, Custom& out) const;
    bool exists(const std::string& name) const;             // case-insensitive
    std::string uniqueName(const std::string& wanted) const;   // wanted, or "wanted 2", "wanted 3", ...
    bool save(const Custom& c, std::string& err) const;     // creates the folder; replaces the theme of that name
    bool remove(const std::string& name, std::string& err) const;
    // Duplicate: a copy named "<name> copy" (made unique), saved; `out` is the copy.
    bool duplicate(const std::string& name, Custom& out, std::string& err) const;
    // Import: validates the file at `path`, saves it under a name that does not clash with a theme already here.
    bool importFile(const std::string& path, Custom& out, std::string& err) const;
private:
    std::string fileFor(const std::string& name) const;
    std::string dir_;
    bool readOnly_;
};

// ---- contrast warnings --------------------------------------------------------------------------------------
struct Warning {
    std::string what;   // "Text on window"
    double ratio;
    double needed;
};
// Every text pair the app draws with, measured on what the theme would produce: below 4.5:1 for text (3:1 for
// text on a hovered control, as in the built-in themes) is reported.
std::vector<Warning> contrastWarnings(const Custom& c);

}  // namespace theme
}  // namespace vj
