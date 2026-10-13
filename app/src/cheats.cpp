#include "cheats.h"

#include "paths.h"

#include "cheat.h"   // the core's own parser: what it accepts is what we may emit

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <filesystem>

namespace vj {

namespace {

namespace fs = std::filesystem;

std::string trim(const std::string& s) {
    size_t a = 0, b = s.size();
    while (a < b && std::isspace((unsigned char)s[a])) a++;
    while (b > a && std::isspace((unsigned char)s[b - 1])) b--;
    return s.substr(a, b - a);
}

std::string lowerStr(std::string s) {
    for (char& c : s) c = (char)std::tolower((unsigned char)c);
    return s;
}

std::string unquote(const std::string& v) {
    std::string t = trim(v);
    if (t.size() >= 2 && t.front() == '"' && t.back() == '"') t = t.substr(1, t.size() - 2);
    return t;
}

// Decimal, or 0x-prefixed hex.  (RetroArch writes decimal; a bare "0123" is not octal.)
bool parseU64(const std::string& s, uint64_t& out) {
    std::string t = trim(s);
    if (t.empty()) return false;
    int base = 10;
    size_t i = 0;
    if (t.size() > 2 && t[0] == '0' && (t[1] == 'x' || t[1] == 'X')) {
        base = 16;
        i = 2;
    }
    uint64_t v = 0;
    for (; i < t.size(); i++) {
        int d;
        char c = t[i];
        if (c >= '0' && c <= '9') d = c - '0';
        else if (base == 16 && c >= 'a' && c <= 'f') d = c - 'a' + 10;
        else if (base == 16 && c >= 'A' && c <= 'F') d = c - 'A' + 10;
        else return false;
        if (v > (UINT64_MAX - (uint64_t)d) / (uint64_t)base) return false;
        v = v * (uint64_t)base + (uint64_t)d;
    }
    out = v;
    return true;
}

std::string makeId(const std::string& desc, const std::string& code) {
    uint32_t h = 2166136261u;
    auto feed = [&](const std::string& s) {
        for (unsigned char c : s) {
            h ^= c;
            h *= 16777619u;
        }
    };
    feed(desc);
    h ^= '\n';
    h *= 16777619u;
    feed(code);
    char b[16];
    std::snprintf(b, sizeof b, "%08x", h);
    return b;
}

// "Base (Part 3)" -> true, base = "Base".
bool splitPart(const std::string& desc, std::string& base) {
    size_t pos = desc.rfind(" (");
    if (pos == std::string::npos || desc.empty() || desc.back() != ')') return false;
    std::string inner = lowerStr(desc.substr(pos + 2, desc.size() - pos - 3));
    if (inner.compare(0, 5, "part ") != 0) return false;
    if (inner.size() <= 5) return false;
    for (size_t i = 5; i < inner.size(); i++)
        if (!std::isdigit((unsigned char)inner[i])) return false;
    base = trim(desc.substr(0, pos));
    return true;
}

struct Raw {
    std::string desc;
    std::string code;
    std::string note;
    bool supported = false;
    unsigned segments = 0;
};

Raw convertEntry(const std::map<std::string, std::string>& f, size_t index) {
    Raw r;
    auto get = [&](const char* k) {
        auto it = f.find(k);
        return it == f.end() ? std::string() : it->second;
    };
    r.desc = get("desc");
    if (r.desc.empty()) r.desc = "Cheat " + std::to_string(index + 1);

    std::string code = get("code");
    if (!code.empty()) {
        std::string why;
        if (validateCode(code, &r.segments, &why)) {
            r.code = code;
            r.supported = true;
        } else {
            r.note = why;
        }
        return r;
    }

    if (get("address").empty()) {
        r.note = "no code or address";
        return r;
    }
    std::string handler = get("handler"), type = get("cheat_type"), rep = get("repeat_count");
    if (!handler.empty() && handler != "1") {
        r.note = "cheat handler " + handler + " is not a memory cheat";
        return r;
    }
    if (!type.empty() && type != "1") {
        r.note = "cheat type " + type + " is not supported (only 'set to value')";
        return r;
    }
    if (!rep.empty() && rep != "1" && rep != "0") {
        r.note = "repeat count " + rep + " is not supported";
        return r;
    }
    unsigned n = 0;
    std::string sz = get("memory_search_size");
    if (sz == "3") n = 1;
    else if (sz == "4") n = 2;
    else if (sz == "5") n = 4;
    else {
        r.note = "memory width '" + sz + "' is not supported (8, 16 and 32 bit are)";
        return r;
    }
    uint64_t addr = 0, val = 0;
    if (!parseU64(get("address"), addr) || !parseU64(get("value"), val)) {
        r.note = "address or value is not a number";
        return r;
    }
    if (addr + n > 0x200000u) {
        r.note = "address is outside the 2 MB main RAM";
        return r;
    }
    if (n < 8 && val >> (8 * n)) {
        r.note = "value does not fit the memory width";
        return r;
    }
    // Byte order.  RetroArch's cheat "big_endian" says how the VALUE sits in
    // memory: false (every Jaguar entry) = least significant byte at the lowest
    // address, i.e. the value is written to memory as-is on a little-endian host,
    // whatever the emulated CPU's byte order.  Evidence from the data itself:
    // Tempest 2000's "Start with 999999 Score" is a 32-bit 0x0909 at 51570
    // followed by 16-bit 0x0909 at 51572 and 51574.  Read little-endian that is
    // 09 09 00 00 | 09 09 | 09 09 -- six digit bytes of 9, the second and third
    // writes clobbering the first's zero padding.  Read big-endian the first two
    // bytes would stay 00.
    bool be = lowerStr(get("big_endian")) == "true" || get("big_endian") == "1";
    uint8_t b[4] = {0, 0, 0, 0};
    for (unsigned k = 0; k < n; k++)
        b[k] = (uint8_t)((val >> (8 * (be ? n - 1 - k : k))) & 0xFF);
    r.code = memoryCheatToCode((uint32_t)addr, b, n);
    r.supported = validateCode(r.code, &r.segments, &r.note);   // cannot fail; belt and braces
    return r;
}

}  // namespace

// ---------------------------------------------------------------------------------
// format
// ---------------------------------------------------------------------------------
std::string memoryCheatToCode(uint32_t addr, const uint8_t* bytes, unsigned n) {
    std::string out;
    unsigned i = 0;
    while (i < n) {
        uint32_t a = (addr + i) & 0xFFFFFFu;
        unsigned rem = n - i;
        char buf[40];
        if (rem >= 4 && !(a & 1)) {
            std::snprintf(buf, sizeof buf, "%08X:%02X%02X%02X%02X", a, bytes[i], bytes[i + 1], bytes[i + 2], bytes[i + 3]);
            i += 4;
        } else if (rem >= 2 && !(a & 1)) {
            std::snprintf(buf, sizeof buf, "%08X:%02X%02X", a, bytes[i], bytes[i + 1]);
            i += 2;
        } else {
            std::snprintf(buf, sizeof buf, "%08X:%02X", a, bytes[i]);
            i += 1;
        }
        if (!out.empty()) out += '+';
        out += buf;
    }
    return out;
}

bool validateCode(const std::string& code, unsigned* segments, std::string* why) {
    unsigned count = 0;
    size_t start = 0;
    for (size_t p = 0; p <= code.size(); p++) {
        if (p == code.size() || code[p] == '+' || code[p] == '\n' || code[p] == '\r') {
            std::string seg = trim(code.substr(start, p - start));
            start = p + 1;
            if (seg.empty()) continue;
            if (seg.size() > CHEAT_SEGMENT_INPUT_MAX) {
                if (why) *why = "a code segment is longer than " + std::to_string(CHEAT_SEGMENT_INPUT_MAX) + " characters";
                return false;
            }
            uint32_t a, v;
            uint8_t sz;
            if (!cheat_parse_one(seg.c_str(), &a, &v, &sz)) {
                if (why) *why = "the core cannot read '" + seg + "' (expected hex ADDRESS:VALUE, e.g. 00007D3D:05)";
                return false;
            }
            count++;
        }
    }
    if (count == 0) {
        if (why) *why = "empty code";
        return false;
    }
    if (segments) *segments = count;
    return true;
}

std::string normalizeTitle(const std::string& s) {
    std::string out;
    int depth = 0;
    for (char c : s) {
        if (c == '(' || c == '[') depth++;
        else if (c == ')' || c == ']') { if (depth > 0) depth--; }
        else if (depth == 0 && std::isalnum((unsigned char)c)) out += (char)std::tolower((unsigned char)c);
    }
    return out;
}

std::string slugify(const std::string& s) {
    std::string out;
    bool dash = false;
    for (char c : s) {
        if (std::isalnum((unsigned char)c) && (unsigned char)c < 128) {
            if (dash && !out.empty()) out += '-';
            dash = false;
            out += (char)std::tolower((unsigned char)c);
        } else {
            dash = true;
        }
    }
    return out;
}

bool parseCht(const std::string& textIn, std::vector<ChtCheat>& out) {
    out.clear();
    std::string text = textIn;
    if (text.compare(0, 3, "\xEF\xBB\xBF") == 0) text.erase(0, 3);

    // cheatN_field = value   (value quoted or not)
    std::map<size_t, std::map<std::string, std::string>> by;
    size_t pos = 0;
    while (pos < text.size()) {
        size_t nl = text.find('\n', pos);
        std::string line = trim(text.substr(pos, nl == std::string::npos ? std::string::npos : nl - pos));
        pos = nl == std::string::npos ? text.size() : nl + 1;
        if (line.empty() || line[0] == '#') continue;
        size_t eq = line.find('=');
        if (eq == std::string::npos) continue;
        std::string key = trim(line.substr(0, eq));
        if (key.compare(0, 5, "cheat") != 0 || key.size() < 7) continue;
        size_t i = 5;
        size_t idx = 0;
        while (i < key.size() && std::isdigit((unsigned char)key[i]) && idx < 100000) idx = idx * 10 + (size_t)(key[i++] - '0');
        if (i == 5 || i >= key.size() || key[i] != '_') continue;
        by[idx][key.substr(i + 1)] = unquote(line.substr(eq + 1));
    }

    // Convert, then merge consecutive "Base (Part N)" / identically named entries
    // into one toggle: the database splits one logical cheat (a score, a bit
    // field wider than the three widths) into several lines.
    struct Group {
        std::string desc, base;
        bool part = false;
        Raw raw;
        unsigned parts = 1;
    };
    std::vector<Group> groups;
    for (const auto& kv : by) {
        const auto& f = kv.second;
        bool hasPayload = f.count("code") || f.count("address");
        if (!hasPayload && !f.count("desc")) continue;
        Raw r = convertEntry(f, kv.first);
        std::string base;
        bool isPart = splitPart(r.desc, base);
        if (!isPart) base = r.desc;
        if (!groups.empty()) {
            Group& g = groups.back();
            if (lowerStr(g.base) == lowerStr(base) && (g.part || isPart || g.desc == r.desc)) {
                if (g.raw.supported && r.supported) {
                    g.raw.code += "+" + r.code;
                    g.raw.segments += r.segments;
                } else {
                    if (g.raw.supported) g.raw.note = "a later part is not supported: " + r.note;
                    g.raw.supported = false;
                    g.raw.code.clear();
                }
                g.parts++;
                g.part = g.part || isPart;
                if (g.part) g.desc = g.base;
                continue;
            }
        }
        Group g;
        g.desc = r.desc;
        g.base = base;
        g.part = isPart;
        g.raw = r;
        if (isPart) g.desc = base;
        groups.push_back(g);
    }
    for (const Group& g : groups) {
        ChtCheat c;
        c.desc = g.desc;
        c.parts = g.parts;
        c.supported = g.raw.supported;
        c.note = g.raw.note;
        c.code = g.raw.supported ? g.raw.code : std::string();
        c.segments = g.raw.supported ? g.raw.segments : 0;
        c.id = makeId(c.desc, c.supported ? c.code : "unsupported:" + g.raw.note);
        out.push_back(std::move(c));
    }
    return !out.empty();
}

bool loadChtFile(const std::string& path, ChtSet& out) {
    std::vector<unsigned char> bytes;
    if (!readFile(path, bytes)) return false;
    out.stem = pathStem(path);
    out.path = path;
    return parseCht(std::string(bytes.begin(), bytes.end()), out.cheats);
}

// ---------------------------------------------------------------------------------
// library
// ---------------------------------------------------------------------------------
void CheatManager::setLibraryDirs(const std::vector<std::string>& dirs) { dirs_ = dirs; }

void CheatManager::rescan() {
    library_.clear();
    std::map<std::string, bool> seen;
    for (const std::string& d : dirs_) {
        if (d.empty()) continue;
        std::vector<fs::path> roots;
        std::error_code ec;
        fs::path base(d);
        if (!fs::is_directory(base, ec)) continue;
        roots.push_back(base);
        fs::path sub = base / "Atari - Jaguar";
        if (fs::is_directory(sub, ec)) roots.push_back(sub);
        for (const fs::path& r : roots) {
            for (fs::directory_iterator it(r, ec), end; !ec && it != end; it.increment(ec)) {
                std::error_code ec2;
                if (!it->is_regular_file(ec2)) continue;
                if (lowerStr(it->path().extension().string()) != ".cht") continue;
                ChtRef ref;
                ref.stem = it->path().stem().string();
                ref.path = it->path().string();
                if (seen.count(ref.stem)) continue;
                seen[ref.stem] = true;
                library_.push_back(ref);
            }
        }
    }
    std::sort(library_.begin(), library_.end(), [](const ChtRef& a, const ChtRef& b) {
        std::string x = lowerStr(a.stem), y = lowerStr(b.stem);
        return x != y ? x < y : a.stem < b.stem;
    });
    if (game_.valid()) {
        // Matches may have changed; an explicit choice stays, the automatic one is redone.
        pickSet();
        apply();
    }
}

// ---------------------------------------------------------------------------------
// game state
// ---------------------------------------------------------------------------------
std::string CheatManager::statePath() const { return stateDir_ + game_.key + ".ini"; }

void CheatManager::loadState() {
    state_.load(statePath());
    enabled_.clear();
    for (const auto& kv : state_.withPrefix("on."))
        if (kv.second == "1") enabled_[kv.first] = true;
    user_.clear();
    int n = state_.getInt("user.count", 0);
    for (int i = 0; i < n && i < 500; i++) {
        std::string k = "user." + std::to_string(i) + ".";
        UserCheat u;
        u.desc = state_.get(k + "desc", "");
        u.code = state_.get(k + "code", "");
        u.enabled = state_.getBool(k + "on", false);
        if (!u.code.empty()) user_.push_back(u);
    }
    selectedSaved_ = state_.has("set");
    selected_ = selectedSaved_ ? state_.get("set", "") : std::string();
}

void CheatManager::pickSet() {
    matches_.clear();
    suggestions_.clear();
    std::string t = normalizeTitle(game_.title), f = normalizeTitle(game_.fileStem);
    for (size_t i = 0; i < library_.size(); i++) {
        std::string n = normalizeTitle(library_[i].stem);
        if (n.empty()) continue;
        if ((!t.empty() && n == t) || (!f.empty() && n == f)) {
            matches_.push_back(i);
        } else {
            auto loose = [&](const std::string& g) {
                return g.size() >= 5 && n.size() >= 5 && (g.find(n) != std::string::npos || n.find(g) != std::string::npos);
            };
            if (loose(t) || loose(f)) suggestions_.push_back(i);
        }
    }
    if (!selectedSaved_) selected_ = matches_.size() == 1 ? library_[matches_[0]].stem : std::string();
    loadSelectedSet();
}

void CheatManager::loadSelectedSet() {
    set_ = ChtSet();
    setLoaded_ = false;
    if (selected_.empty()) return;
    for (const ChtRef& r : library_) {
        if (r.stem != selected_) continue;
        setLoaded_ = loadChtFile(r.path, set_);
        if (!setLoaded_ && warn) warn("cheats: could not read " + r.path);
        return;
    }
}

void CheatManager::gameLoaded(const GameIdentity& id) {
    game_ = id;
    if (!game_.valid()) {
        gameClosed();
        return;
    }
    loadState();
    pickSet();
    apply();
}

void CheatManager::gameClosed() {
    game_ = GameIdentity();
    state_ = Config();
    matches_.clear();
    suggestions_.clear();
    selected_.clear();
    selectedSaved_ = false;
    set_ = ChtSet();
    setLoaded_ = false;
    enabled_.clear();
    user_.clear();
    apply();
}

std::string CheatManager::setStatus() const {
    if (library_.empty()) return "No cheat files found.";
    if (!selected_.empty() && !setLoaded_) return "The cheat set '" + selected_ + "' is not in the library.";
    if (selected_.empty() && matches_.size() > 1)
        return std::to_string(matches_.size()) + " cheat sets match this game; choose one.";
    if (selected_.empty() && matches_.empty()) return "No cheat set matches this game by name; choose one if it has cheats.";
    return "";
}

void CheatManager::touch() {
    if (!game_.valid() || stateDir_.empty()) return;
    std::error_code ec;
    fs::create_directories(fs::path(stateDir_), ec);
    state_.set("title", game_.title.empty() ? game_.fileStem : game_.title);
    if (!game_.fileStem.empty()) state_.set("file", game_.fileStem);
    if (!state_.save() && warn) warn("cheats: could not write " + statePath());
}

bool CheatManager::selectSet(const std::string& stem) {
    if (!game_.valid()) return false;
    if (!stem.empty()) {
        bool found = false;
        for (const ChtRef& r : library_) found = found || r.stem == stem;
        if (!found) return false;
    }
    selected_ = stem;
    selectedSaved_ = true;
    state_.set("set", stem);
    loadSelectedSet();
    touch();
    apply();
    return true;
}

void CheatManager::setEnabled(const ChtCheat& c, bool on) {
    if (!game_.valid()) return;
    if (on && !c.supported) return;
    if (!selectedSaved_) {   // an automatic choice becomes explicit once the user relies on it
        state_.set("set", selected_);
        selectedSaved_ = true;
    }
    if (on) {
        enabled_[c.id] = true;
        state_.set("on." + c.id, "1");
    } else {
        enabled_.erase(c.id);
        state_.erase("on." + c.id);
    }
    touch();
    apply();
}

bool CheatManager::setEnabledByName(const std::string& name, bool on) {
    if (!game_.valid()) return false;
    std::string want = lowerStr(trim(name));
    if (setLoaded_) {
        for (size_t i = 0; i < set_.cheats.size(); i++) {
            const ChtCheat& c = set_.cheats[i];
            if (lowerStr(c.desc) == want || c.id == want) {
                if (on && !c.supported) return false;
                setEnabled(c, on);
                return true;
            }
        }
        uint64_t idx;
        if (parseU64(want, idx) && idx < set_.cheats.size() && (!on || set_.cheats[idx].supported)) {
            setEnabled(set_.cheats[idx], on);
            return true;
        }
    }
    for (size_t i = 0; i < user_.size(); i++) {
        if (lowerStr(user_[i].desc) == want) {
            setUserEnabled(i, on);
            return true;
        }
    }
    return false;
}

void CheatManager::disableAll() {
    if (!game_.valid()) return;
    for (const auto& kv : std::vector<std::pair<std::string, bool>>(enabled_.begin(), enabled_.end()))
        state_.erase("on." + kv.first);
    enabled_.clear();
    for (size_t i = 0; i < user_.size(); i++) {
        user_[i].enabled = false;
        state_.set("user." + std::to_string(i) + ".on", "0");
    }
    touch();
    apply();
}

unsigned CheatManager::enabledCount() const {
    unsigned n = 0;
    if (setLoaded_)
        for (const ChtCheat& c : set_.cheats)
            if (c.supported && enabled_.count(c.id)) n++;
    for (const UserCheat& u : user_)
        if (u.enabled) n++;
    return n;
}

// ---------------------------------------------------------------------------------
// manual codes
// ---------------------------------------------------------------------------------
static void saveUserCheats(Config& cfg, const std::vector<UserCheat>& list, int previousCount) {
    for (int i = 0; i < previousCount; i++) {
        std::string k = "user." + std::to_string(i) + ".";
        cfg.erase(k + "desc");
        cfg.erase(k + "code");
        cfg.erase(k + "on");
    }
    cfg.setInt("user.count", (int)list.size());
    for (size_t i = 0; i < list.size(); i++) {
        std::string k = "user." + std::to_string(i) + ".";
        cfg.set(k + "desc", list[i].desc);
        cfg.set(k + "code", list[i].code);
        cfg.set(k + "on", list[i].enabled ? "1" : "0");
    }
}

bool CheatManager::addUser(const std::string& descIn, const std::string& codeIn, std::string& err) {
    if (!game_.valid()) {
        err = "no game loaded";
        return false;
    }
    // One line per code in the settings file: newlines become the '+' separator.
    std::string code;
    for (char c : codeIn) code += (c == '\n' || c == '\r') ? '+' : c;
    std::string cleaned;   // collapse "++" and trim stray separators
    for (char c : code)
        if (!(c == '+' && (cleaned.empty() || cleaned.back() == '+'))) cleaned += c;
    while (!cleaned.empty() && cleaned.back() == '+') cleaned.pop_back();
    code = trim(cleaned);
    if (!validateCode(code, nullptr, &err)) return false;
    std::string desc;
    for (char c : trim(descIn)) desc += (unsigned char)c < 32 ? ' ' : c;
    if (desc.empty()) desc = "Manual code";
    int previous = (int)user_.size();
    for (size_t i = 0; i < user_.size(); i++) {
        if (user_[i].code == code) {   // same code again: just turn it back on
            user_[i].enabled = true;
            if (!desc.empty() && user_[i].desc.empty()) user_[i].desc = desc;
            saveUserCheats(state_, user_, previous);
            touch();
            apply();
            return true;
        }
    }
    UserCheat u;
    u.desc = desc;
    u.code = code;
    u.enabled = true;
    user_.push_back(u);
    saveUserCheats(state_, user_, previous);
    touch();
    apply();
    return true;
}

void CheatManager::removeUser(size_t i) {
    if (i >= user_.size()) return;
    int previous = (int)user_.size();
    user_.erase(user_.begin() + (std::ptrdiff_t)i);
    saveUserCheats(state_, user_, previous);
    touch();
    apply();
}

void CheatManager::setUserEnabled(size_t i, bool on) {
    if (i >= user_.size()) return;
    user_[i].enabled = on;
    state_.set("user." + std::to_string(i) + ".on", on ? "1" : "0");
    touch();
    apply();
}

// ---------------------------------------------------------------------------------
// core sync
// ---------------------------------------------------------------------------------
void CheatManager::apply() {
    appliedSegments_ = 0;
    if (resetSink) resetSink();
    if (blockedNow()) return;   // hardcore: the core's list stays empty
    if (setLoaded_) {
        for (size_t i = 0; i < set_.cheats.size(); i++) {
            const ChtCheat& c = set_.cheats[i];
            if (!c.supported || !enabled_.count(c.id)) continue;
            if (setSink) setSink((unsigned)i, true, c.code.c_str());
            appliedSegments_ += c.segments;
        }
    }
    for (size_t i = 0; i < user_.size(); i++) {
        if (!user_[i].enabled) continue;
        unsigned segs = 0;
        validateCode(user_[i].code, &segs, nullptr);
        if (setSink) setSink(kUserIndexBase + (unsigned)i, true, user_[i].code.c_str());
        appliedSegments_ += segs;
    }
    if (appliedSegments_ > kCoreLimit && warn)
        warn("cheats: " + std::to_string(appliedSegments_) + " writes exceed the core's limit of " +
             std::to_string(kCoreLimit) + "; the excess is ignored");
}

}  // namespace vj
