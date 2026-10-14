#include "theme_custom.h"

#include "paths.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <map>
#include <set>

namespace vj {
namespace theme {

namespace fs = std::filesystem;

static const KeyInfo kKeys[KeyCount] = {
    {"background", "Home background"}, {"window", "Window"},   {"frame", "Input fields"},
    {"text", "Text"},                         {"muted", "Muted text"}, {"accent", "Accent"},
    {"warning", "Warning"},                   {"error", "Error"},      {"success", "Success"},
    {"gold", "Hardcore / done"},              {"card", "Game card"},
};

const KeyInfo& keyInfo(Key k) { return kKeys[k]; }

static std::string lower(std::string s) {
    for (char& c : s) c = (char)std::tolower((unsigned char)c);
    return s;
}

static Color mix(Color a, Color b, double t) {
    auto m = [&](int x, int y) { return (int)std::lround(x * (1.0 - t) + y * t); };
    return Color(m(a.r, b.r), m(a.g, b.g), m(a.b, b.b));
}

Custom Custom::fromBase(Scheme base, const std::string& name) {
    const Palette& p = palette(base);
    Custom c;
    c.name = name;
    c.base = base;
    const bool dark = base == Scheme::Dark;
    c.colors[KeyBackground] = p.homeBg;
    c.colors[KeyWindow] = dark ? Color(15, 15, 15) : Color(246, 247, 250);
    c.colors[KeyFrame] = dark ? Color(29, 47, 73) : Color(226, 231, 241);
    c.colors[KeyText] = dark ? Color(255, 255, 255) : p.text;   // Dear ImGui's own white
    c.colors[KeyMuted] = dark ? Color(171, 171, 175) : Color(86, 90, 100);
    c.colors[KeyAccent] = dark ? Color(66, 150, 250) : Color(37, 99, 235);
    c.colors[KeyWarning] = p.warning;
    c.colors[KeyError] = p.error;
    c.colors[KeySuccess] = p.success;
    c.colors[KeyGold] = p.gold;
    c.colors[KeyCard] = p.cardBg;
    return c;
}

bool Custom::operator==(const Custom& o) const {
    if (name != o.name || base != o.base || std::fabs(rounding - o.rounding) > 1e-4f || std::fabs(spacing - o.spacing) > 1e-4f)
        return false;
    for (int i = 0; i < KeyCount; i++)
        if (colors[i].r != o.colors[i].r || colors[i].g != o.colors[i].g || colors[i].b != o.colors[i].b) return false;
    return true;
}

Palette paletteFor(const Custom& c) {
    Palette p = palette(c.base);   // the chips over art, and anything not edited, keep the base's values
    const Color* k = c.colors;
    p.homeBg = k[KeyBackground];
    p.text = k[KeyText];
    p.textSecondary = mix(k[KeyText], k[KeyMuted], 0.35);
    p.textMuted = k[KeyMuted];
    p.warning = k[KeyWarning];
    p.error = k[KeyError];
    p.success = k[KeySuccess];
    p.gold = k[KeyGold];
    p.goldFill = k[KeyGold];
    // Dark text or white on the gold fill, whichever reads better.
    const Color ink(24, 24, 28), paper(255, 255, 255);
    p.goldFillText = contrastRatio(ink, k[KeyGold]) >= contrastRatio(paper, k[KeyGold]) ? ink : paper;
    p.hoverEdge = k[KeyAccent];
    p.cardBg = k[KeyCard];
    p.cardBgHover = mix(k[KeyCard], k[KeyText], 0.08);
    p.stateBg = k[KeyCard];
    p.stateBgHover = p.cardBgHover;
    p.emptyEdge = mix(k[KeyBackground], k[KeyText], 0.45);
    p.pictureBg = mix(k[KeyBackground], k[KeyText], 0.35);
    p.cursorButton = k[KeyAccent];
    return p;
}

Scheme schemeOf(const Custom& c) { return relativeLuminance(c.colors[KeyWindow]) < 0.18 ? Scheme::Dark : Scheme::Light; }

std::string sanitizeName(const std::string& s) {
    size_t a = 0, b = s.size();
    while (a < b && (s[a] == ' ' || s[a] == '\t')) a++;
    while (b > a && (s[b - 1] == ' ' || s[b - 1] == '\t')) b--;
    std::string out;
    for (size_t i = a; i < b && out.size() < 40; i++) {
        unsigned char ch = (unsigned char)s[i];
        const bool ok = (ch < 128 && std::isalnum(ch)) || ch == ' ' || ch == '-' || ch == '_' || ch == '.' || ch == '(' || ch == ')';
        out += ok ? (char)ch : '_';
    }
    while (!out.empty() && (out.front() == '.' || out.front() == ' ')) out.erase(out.begin());   // no hidden files, no "..".
    while (!out.empty() && (out.back() == ' ' || out.back() == '.')) out.pop_back();             // Windows drops these
    return out;
}

// ---- files -------------------------------------------------------------------------------------------------------
static std::string hex(Color c) {
    char b[16];
    std::snprintf(b, sizeof b, "#%02x%02x%02x", c.r, c.g, c.b);
    return b;
}

static bool parseHex(const std::string& v, Color& out) {
    if (v.size() != 7 || v[0] != '#') return false;
    for (size_t i = 1; i < 7; i++)
        if (!std::isxdigit((unsigned char)v[i])) return false;
    unsigned x = (unsigned)std::strtoul(v.c_str() + 1, nullptr, 16);
    out = Color((int)((x >> 16) & 255), (int)((x >> 8) & 255), (int)(x & 255));
    return true;
}

static bool parseNumber(const std::string& v, float& out) {
    if (v.empty()) return false;
    char* end = nullptr;
    double d = std::strtod(v.c_str(), &end);
    if (!end || *end != 0 || !std::isfinite(d)) return false;
    out = (float)d;
    return true;
}

std::string serializeTheme(const Custom& c) {
    std::string s = "# Virtual Jaguar theme\nvjtheme=1\nname=" + c.name + "\nbase=" + (c.base == Scheme::Light ? "light" : "dark") + "\n";
    for (int i = 0; i < KeyCount; i++) s += std::string("color.") + kKeys[i].id + "=" + hex(c.colors[i]) + "\n";
    char b[64];
    std::snprintf(b, sizeof b, "style.rounding=%.2f\nstyle.spacing=%.2f\n", (double)c.rounding, (double)c.spacing);
    return s + b;
}

bool parseTheme(const std::string& text, Custom& out, std::string& err) {
    std::map<std::string, std::string> kv;
    size_t pos = 0;
    auto trim = [](std::string s) {
        size_t a = 0, b = s.size();
        while (a < b && (s[a] == ' ' || s[a] == '\t' || s[a] == '\r')) a++;
        while (b > a && (s[b - 1] == ' ' || s[b - 1] == '\t' || s[b - 1] == '\r')) b--;
        return s.substr(a, b - a);
    };
    while (pos < text.size()) {
        size_t nl = text.find('\n', pos);
        std::string line = trim(text.substr(pos, nl == std::string::npos ? std::string::npos : nl - pos));
        pos = nl == std::string::npos ? text.size() : nl + 1;
        if (line.empty() || line[0] == '#') continue;
        size_t eq = line.find('=');
        if (eq == std::string::npos) continue;
        kv[trim(line.substr(0, eq))] = trim(line.substr(eq + 1));
    }
    if (!kv.count("vjtheme")) { err = "This is not a Virtual Jaguar theme file (no vjtheme line)."; return false; }
    if (kv["vjtheme"] != "1") { err = "This theme file is from a newer version (vjtheme=" + kv["vjtheme"] + ")."; return false; }
    if (!kv.count("name")) { err = "The theme has no name."; return false; }
    if (!kv.count("base")) { err = "The theme does not say whether it is based on dark or light."; return false; }
    Custom c;
    c.name = sanitizeName(kv["name"]);
    if (c.name.empty()) { err = "The theme's name is empty or unusable."; return false; }
    const std::string& base = kv["base"];
    if (base != "dark" && base != "light") { err = "base must be dark or light, not '" + base.substr(0, 20) + "'."; return false; }
    c = Custom::fromBase(base == "light" ? Scheme::Light : Scheme::Dark, c.name);
    for (int i = 0; i < KeyCount; i++) {   // a colour that is missing keeps the base's; one that is there must be valid
        auto it = kv.find(std::string("color.") + kKeys[i].id);
        if (it == kv.end()) continue;
        if (!parseHex(it->second, c.colors[i])) {
            err = std::string("color.") + kKeys[i].id + " must look like #RRGGBB, not '" + it->second.substr(0, 20) + "'.";
            return false;
        }
    }
    auto num = [&](const char* key, float lo, float hi, float& dst) {
        auto it = kv.find(key);
        if (it == kv.end()) return true;
        float v = 0;
        if (!parseNumber(it->second, v) || v < lo || v > hi) {
            char b[160];
            std::snprintf(b, sizeof b, "%s must be a number from %g to %g, not '%s'.", key, (double)lo, (double)hi, it->second.substr(0, 20).c_str());
            err = b;
            return false;
        }
        dst = v;
        return true;
    };
    if (!num("style.rounding", kMinRounding, kMaxRounding, c.rounding)) return false;
    if (!num("style.spacing", kMinSpacing, kMaxSpacing, c.spacing)) return false;
    out = c;
    return true;
}

static const uintmax_t kMaxThemeFile = 64 * 1024;

bool loadThemeFile(const std::string& path, Custom& out, std::string& err) {
    std::error_code ec;
    uintmax_t sz = fs::file_size(fs::u8path(path), ec);
    if (ec) { err = "Cannot read " + path + "."; return false; }
    if (sz > kMaxThemeFile) { err = "That file is too big to be a theme."; return false; }
    std::vector<unsigned char> bytes;
    if (!readFile(path, bytes)) { err = "Cannot read " + path + "."; return false; }
    return parseTheme(std::string(bytes.begin(), bytes.end()), out, err);
}

bool saveThemeFile(const Custom& c, const std::string& path, std::string& err) {
    const std::string s = serializeTheme(c);
    if (!writeFileAtomic(path, s.data(), s.size())) { err = "Cannot write " + path + "."; return false; }
    return true;
}

// ---- store ---------------------------------------------------------------------------------------------------------
std::string Store::fileFor(const std::string& name) const {
    std::string d = dir_;
    if (!d.empty() && d.back() != '/' && d.back() != '\\') d += '/';
    return d + sanitizeName(name) + ".ini";
}

std::vector<Custom> Store::list() const {
    std::vector<Custom> v;
    std::error_code ec;
    if (dir_.empty()) return v;
    for (fs::directory_iterator it(fs::u8path(dir_), ec), end; !ec && it != end; it.increment(ec)) {
        if (!it->is_regular_file(ec) || lower(it->path().extension().string()) != ".ini") continue;
        Custom c;
        std::string err;
        if (loadThemeFile(it->path().u8string(), c, err)) v.push_back(c);
    }
    std::sort(v.begin(), v.end(), [](const Custom& a, const Custom& b) { return lower(a.name) < lower(b.name); });
    // Two files can carry one name (a hand-copied file): the first wins.
    std::set<std::string> seen;
    std::vector<Custom> uniq;
    for (const Custom& c : v)
        if (seen.insert(lower(c.name)).second) uniq.push_back(c);
    return uniq;
}

bool Store::load(const std::string& name, Custom& out) const {
    for (const Custom& c : list())
        if (lower(c.name) == lower(name)) { out = c; return true; }
    return false;
}

bool Store::exists(const std::string& name) const {
    Custom c;
    return load(name, c);
}

std::string Store::uniqueName(const std::string& wanted) const {
    std::string base = sanitizeName(wanted);
    if (base.empty()) base = "Custom theme";
    if (!exists(base)) return base;
    for (int i = 2; i < 1000; i++) {
        std::string n = sanitizeName(base.substr(0, 34) + " " + std::to_string(i));
        if (!exists(n)) return n;
    }
    return base;
}

bool Store::save(const Custom& c, std::string& err) const {
    if (readOnly_) { err = "This window is a secondary copy of the app: it cannot change the shared themes."; return false; }
    Custom k = c;
    k.name = sanitizeName(c.name);
    if (k.name.empty()) { err = "Give the theme a name first."; return false; }
    std::error_code ec;
    fs::create_directories(fs::u8path(dir_), ec);
    if (ec) { err = "Cannot create the themes folder " + dir_ + "."; return false; }
    // A theme renamed to another case keeps one file.
    return saveThemeFile(k, fileFor(k.name), err);
}

bool Store::remove(const std::string& name, std::string& err) const {
    if (readOnly_) { err = "This window is a secondary copy of the app: it cannot change the shared themes."; return false; }
    Custom c;
    if (!load(name, c)) { err = "There is no saved theme called '" + name + "'."; return false; }
    std::error_code ec;
    fs::remove(fs::u8path(fileFor(c.name)), ec);   // one file, never a folder
    if (ec) { err = "Cannot delete the theme file."; return false; }
    return true;
}

bool Store::duplicate(const std::string& name, Custom& out, std::string& err) const {
    Custom c;
    if (!load(name, c)) { err = "There is no saved theme called '" + name + "'."; return false; }
    c.name = uniqueName(c.name + " copy");
    if (!save(c, err)) return false;
    out = c;
    return true;
}

bool Store::importFile(const std::string& path, Custom& out, std::string& err) const {
    Custom c;
    if (!loadThemeFile(path, c, err)) return false;
    c.name = uniqueName(c.name);
    if (!save(c, err)) return false;
    out = c;
    return true;
}

// ---- contrast ------------------------------------------------------------------------------------------------------
std::vector<Warning> contrastWarnings(const Custom& c) {
    std::vector<Warning> w;
    const Color* k = c.colors;
    auto check = [&](const char* what, Color fg, Color bg, double need) {
        double r = contrastRatio(fg, bg);
        if (r + 1e-9 < need) w.push_back({what, r, need});
    };
    const Palette p = paletteFor(c);
    check("Text on window", k[KeyText], k[KeyWindow], 4.5);
    check("Text on input fields", k[KeyText], k[KeyFrame], 4.5);
    check("Muted text on window", k[KeyMuted], k[KeyWindow], 4.5);
    check("Muted text on input fields", k[KeyMuted], k[KeyFrame], 4.5);
    // A button is the accent at 40% over the window; hovered and pressed it is solid.
    check("Text on buttons", k[KeyText], over(Color(k[KeyAccent].r, k[KeyAccent].g, k[KeyAccent].b, 102), k[KeyWindow]), 4.5);
    check("Text on a hovered button", k[KeyText], k[KeyAccent], 3.0);
    check("Text on the home screen", k[KeyText], k[KeyBackground], 4.5);
    check("Muted text on the home screen", k[KeyMuted], k[KeyBackground], 4.5);
    check("Text on game cards", k[KeyText], k[KeyCard], 4.5);
    check("Muted text on game cards", k[KeyMuted], p.cardBgHover, 4.5);
    const struct { const char* n; Key key; } sem[] = {{"Warning", KeyWarning}, {"Error", KeyError}, {"Success", KeySuccess}, {"Hardcore", KeyGold}};
    for (const auto& s : sem) {
        check((std::string(s.n) + " text on window").c_str(), k[s.key], k[KeyWindow], 4.5);
        check((std::string(s.n) + " text on the home screen").c_str(), k[s.key], k[KeyBackground], 4.5);
    }
    check("Dark or white text on the hardcore bar", p.goldFillText, p.goldFill, 4.5);
    return w;
}

}  // namespace theme
}  // namespace vj
