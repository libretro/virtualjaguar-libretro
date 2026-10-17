#include "library.h"

#include "disc_hash.h"
#include "m3u.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <set>

namespace fs = std::filesystem;

namespace vj {
namespace lib {

namespace {

std::string lowerStr(std::string s) {
    for (char& c : s) c = (char)std::tolower((unsigned char)c);
    return s;
}

std::string trimStr(const std::string& s) {
    size_t a = 0, b = s.size();
    while (a < b && std::isspace((unsigned char)s[a])) a++;
    while (b > a && std::isspace((unsigned char)s[b - 1])) b--;
    return s.substr(a, b - a);
}

fs::path toPath(const std::string& utf8) { return fs::u8path(utf8); }
std::string fromPath(const fs::path& p) { return p.u8string(); }

std::string extOf(const std::string& name) {
    size_t slash = name.find_last_of("/\\");
    size_t dot = name.find_last_of('.');
    if (dot == std::string::npos || (slash != std::string::npos && dot < slash)) return "";
    return lowerStr(name.substr(dot + 1));
}

std::string stemOf(const std::string& path) {
    size_t slash = path.find_last_of("/\\");
    std::string n = slash == std::string::npos ? path : path.substr(slash + 1);
    size_t dot = n.find_last_of('.');
    return dot == std::string::npos || dot == 0 ? n : n.substr(0, dot);
}

// ---- clrmamepro tokenizer / parser ---------------------------------------------------

struct Tok {
    enum Type { Word, Str, Open, Close, End } t = End;
    std::string s;
};

struct Lexer {
    const std::string& s;
    size_t i = 0;
    explicit Lexer(const std::string& text) : s(text) {}
    Tok next() {
        Tok k;
        while (i < s.size() && std::isspace((unsigned char)s[i])) i++;
        if (i >= s.size()) return k;
        char c = s[i];
        if (c == '(') { i++; k.t = Tok::Open; return k; }
        if (c == ')') { i++; k.t = Tok::Close; return k; }
        if (c == '"') {
            size_t j = i + 1;
            while (j < s.size() && s[j] != '"') j++;
            k.t = Tok::Str;
            k.s = s.substr(i + 1, j - i - 1);
            i = j < s.size() ? j + 1 : j;
            return k;
        }
        size_t j = i;
        while (j < s.size() && !std::isspace((unsigned char)s[j]) && s[j] != '(' && s[j] != ')') j++;
        k.t = Tok::Word;
        k.s = s.substr(i, j - i);
        i = j;
        return k;
    }
};

struct Node {
    std::string key, val;
    bool block = false;
    std::vector<Node> kids;
};

// Reads the body of a block (after its '(') up to and including the ')'.
bool parseBody(Lexer& lx, std::vector<Node>& out, int depth) {
    if (depth > 8) return false;
    for (;;) {
        Tok k = lx.next();
        if (k.t == Tok::Close) return true;
        if (k.t == Tok::End) return false;
        if (k.t == Tok::Open) return false;   // stray paren: not a dat
        Node n;
        n.key = k.s;
        Tok v = lx.next();
        if (v.t == Tok::Open) {
            n.block = true;
            if (!parseBody(lx, n.kids, depth + 1)) return false;
        } else if (v.t == Tok::Word || v.t == Tok::Str) {
            n.val = v.s;
        } else {
            return false;
        }
        out.push_back(std::move(n));
    }
}

const std::string* field(const std::vector<Node>& kids, const char* key) {
    for (const Node& n : kids)
        if (!n.block && n.key == key) return &n.val;
    return nullptr;
}

uint32_t parseHex32(const std::string& s) { return (uint32_t)std::strtoul(s.c_str(), nullptr, 16); }

// Case, punctuation and a trailing "(Track N)" do not distinguish a file name from a dat name.
std::string nameKey(const std::string& in) {
    std::string s = lowerStr(trimStr(in));
    for (;;) {
        // strip trailing "(track NN)" groups
        size_t close = s.size();
        while (close > 0 && std::isspace((unsigned char)s[close - 1])) close--;
        if (close == 0 || s[close - 1] != ')') break;
        size_t open = s.rfind('(', close - 1);
        if (open == std::string::npos) break;
        std::string inner = trimStr(s.substr(open + 1, close - open - 2));
        if (inner.compare(0, 5, "track") != 0) break;
        s.erase(open);
    }
    std::string out;
    for (char c : s)
        if (std::isalnum((unsigned char)c)) out += c;
    return out;
}

}  // namespace

std::string normalizeDiscName(const std::string& name) {
    std::string out;
    int depth = 0;
    char open = 0;
    bool pendingSpace = false;
    for (unsigned char c : name) {
        if (depth > 0) {   // inside a (...) or [...] group
            if (c == (unsigned char)open) depth++;
            else if (c == (unsigned char)(open == '(' ? ')' : ']')) depth--;
            continue;
        }
        if (c == '(' || c == '[') {
            open = (char)c;
            depth = 1;
            pendingSpace = true;
            continue;
        }
        if (c == '\'' || c == '`') continue;   // "Dragon's" == "Dragons"
        if (std::isalnum(c)) {
            if (pendingSpace && !out.empty()) out += ' ';
            pendingSpace = false;
            out += (char)std::tolower(c);
        } else {
            pendingSpace = true;
        }
    }
    return out;
}

// ---- Dat -----------------------------------------------------------------------------------------

bool Dat::parse(const std::string& text) {
    games_.clear();
    crc_.clear();
    names_.clear();
    loose_.clear();
    header_.clear();
    Lexer lx(text);
    for (;;) {
        Tok k = lx.next();
        if (k.t == Tok::End) break;
        if (k.t != Tok::Word) break;
        Tok o = lx.next();
        if (o.t != Tok::Open) break;
        std::vector<Node> body;
        if (!parseBody(lx, body, 0)) break;
        if (k.s == "clrmamepro" || k.s == "header") {
            for (const Node& n : body)
                if (!n.block) header_[n.key] = n.val;
        } else if (k.s == "game" || k.s == "machine" || k.s == "resource") {
            DatGame g;
            const std::string* v;
            if ((v = field(body, "name"))) g.name = *v;
            if ((v = field(body, "region"))) g.region = *v;
            if ((v = field(body, "serial"))) g.serial = *v;
            for (const Node& n : body) {
                if (!n.block || n.key != "rom") continue;
                DatRom r;
                if ((v = field(n.kids, "name"))) r.name = *v;
                if ((v = field(n.kids, "size"))) r.size = std::strtoull(v->c_str(), nullptr, 10);
                if ((v = field(n.kids, "crc"))) r.crc = parseHex32(*v);
                if (g.serial.empty() && (v = field(n.kids, "serial"))) g.serial = *v;
                if (r.crc || r.size) g.roms.push_back(std::move(r));
            }
            if (!g.name.empty()) games_.push_back(std::move(g));
        }
    }
    for (size_t i = 0; i < games_.size(); i++) {
        for (const DatRom& r : games_[i].roms)
            if (r.crc) crc_.emplace(r.crc, i);
        names_.emplace(nameKey(games_[i].name), i);   // first one wins
        std::string lk = normalizeDiscName(games_[i].name);
        if (!lk.empty()) loose_[lk].push_back(i);
    }
    return !games_.empty();
}

bool Dat::load(const std::string& path) {
    size_t size = 0;
    void* data = SDL_LoadFile(path.c_str(), &size);
    if (!data) return false;
    std::string text((const char*)data, size);
    SDL_free(data);
    return parse(text);
}

const std::string& Dat::header(const std::string& key) const {
    static const std::string none;
    auto it = header_.find(key);
    return it == header_.end() ? none : it->second;
}

const DatGame* Dat::byCrc(uint32_t crc) const {
    if (!crc) return nullptr;
    auto it = crc_.lower_bound(crc);
    if (it == crc_.end() || it->first != crc) return nullptr;
    // multimap keeps insertion order for equal keys: the first game in file order.
    return &games_[it->second];
}

const DatGame* Dat::byName(const std::string& fileStem) const {
    std::string k = nameKey(fileStem);
    if (k.empty()) return nullptr;
    auto it = names_.find(k);
    return it == names_.end() ? nullptr : &games_[it->second];
}

const DatGame* Dat::byLooseName(const std::string& fileStem, const std::string& folderName, int* how) const {
    const std::string names[2] = {normalizeDiscName(fileStem), normalizeDiscName(folderName)};
    for (int pass = 0; pass < 2; pass++) {
        if (names[pass].empty()) continue;
        auto it = loose_.find(names[pass]);
        if (it == loose_.end()) continue;
        if (it->second.size() != 1) return nullptr;   // two games with this title: guessing is worse than showing the file name
        if (how) *how = pass + 1;
        return &games_[it->second[0]];
    }
    const std::string& k = names[0];
    if (k.size() < 4) return nullptr;
    const DatGame* found = nullptr;
    for (auto it = loose_.lower_bound(k + " "); it != loose_.end() && it->first.compare(0, k.size() + 1, k + " ") == 0; ++it) {
        if (found || it->second.size() != 1) return nullptr;   // more than one longer title starts this way
        found = &games_[it->second[0]];
    }
    if (found && how) *how = 3;
    return found;
}

// ---- hashing -----------------------------------------------------------------------------------------

uint32_t crc32Update(uint32_t crc, const void* data, size_t len) {
    return (uint32_t)SDL_crc32(crc, data, len);
}

uint32_t cartHeaderSize(const uint8_t* b, size_t size) {
    // Mirrors DetectPrependedHeaderSize() in src/core/file.c.
    const size_t kSkip = 512, kMarkerOff = 0x400;
    const uint32_t kMarker = 0x04040404;
    auto be32 = [&](size_t off) -> uint32_t {
        return ((uint32_t)b[off] << 24) | ((uint32_t)b[off + 1] << 16) | ((uint32_t)b[off + 2] << 8) | b[off + 3];
    };
    if (size < kSkip + kMarkerOff + 4) return 0;
    if ((size % 1048576) == 0) return 0;
    size_t payload = size - kSkip;
    if (be32(kSkip + kMarkerOff) != kMarker) return 0;
    if ((payload % 1048576) == 0) return (uint32_t)kSkip;
    if (payload < 1048576 && be32(kMarkerOff) != kMarker) return (uint32_t)kSkip;
    return 0;
}

// ---- BIOS classification ------------------------------------------------------------------------------

namespace {

struct KnownBios {
    uint32_t crc;
    BiosRole role;
    const char* title;
};

// CRCs the core itself checks: biosdb.c (cart boot ROMs, issue #469), CD_BIOS_CRC_RETAIL / _DEV and
// the Memory Track cartridge dump (jaguarMainROMCRC32 test) in libretro.c / jaguar.c.
const KnownBios kKnownBios[] = {
    {0xFB731AAAu, BiosRole::CartBoot, "Series K boot ROM"},
    {0xAE25BDF5u, BiosRole::CartBoot, "Model M boot ROM"},
    {0xE60277BBu, BiosRole::CartBoot, "Stubulator '93 dev boot ROM"},
    {0x8D15DBC6u, BiosRole::CartBoot, "Stubulator '94 dev boot ROM"},
    {0x687068D5u, BiosRole::CdBios, "Jaguar CD BIOS (retail)"},
    {0x55A0669Cu, BiosRole::CdBios, "Jaguar CD BIOS (developer)"},
    {0xFDF37F47u, BiosRole::MemoryTrack, "Memory Track cartridge"},
};

const uint64_t kMaxBiosBytes = 512ull * 1024;

// File names the core searches for (libretro.c), in the sub-folders it searches.
const char* const kCartBootNames[] = {"jagboot.rom", "boot.rom", "boot0.rom", "jagboot_m.rom",
                                      "[BIOS] Atari Jaguar (World).j64",
                                      "[BIOS] Atari Jaguar Stubulator '94 (World).j64",
                                      "[BIOS] Atari Jaguar Stubulator '93 (World).j64", nullptr};
const char* const kCdBiosNames[] = {"[BIOS] Atari Jaguar CD (World).j64", "[BIOS] Atari Jaguar CD (World).rom",
                                    "[BIOS] Atari Jaguar CD (World).bin", "[BIOS] Atari Jaguar Developer CD (World).j64",
                                    "[BIOS] Atari Jaguar Developer CD (World).rom",
                                    "[BIOS] Atari Jaguar Developer CD (World).bin", "jaguarcd_bios.bin", "jagcd_bios.bin",
                                    "jaguarcd.bin", "jagcd.bin", "Jaguar CD BIOS.rom", "Jaguar CD BIOS.bin", nullptr};
const char* const kCartBootDirs[] = {"", "Atari - Jaguar", "jaguar", nullptr};
const char* const kCdBiosDirs[] = {"", "Atari - Jaguar", "Atari - Jaguar CD", "jaguar", "jaguarcd", nullptr};

std::string baseName(const std::string& path) {
    size_t sl = path.find_last_of("/\\");
    return sl == std::string::npos ? path : path.substr(sl + 1);
}

std::string dirName(const std::string& path) {
    size_t sl = path.find_last_of("/\\");
    return sl == std::string::npos ? std::string() : path.substr(0, sl);
}

std::string normDir(std::string d) {
    for (char& c : d)
        if (c == '\\') c = '/';
    while (d.size() > 1 && d.back() == '/') d.pop_back();
    return d;
}

// The same, ignoring case: a dump named JAGCD.BIN is as much a CD BIOS as jagcd.bin (macOS and Windows
// volumes are case-insensitive, and archives of dumps are full of upper-case names).
bool inListCI(const char* const* list, const std::string& name) {
    const std::string ln = lowerStr(name);
    for (; *list; list++)
        if (ln == lowerStr(*list)) return true;
    return false;
}

bool hasWord(const std::string& lowerName, const char* w) { return lowerName.find(w) != std::string::npos; }

// Does the core's search find this spelling?  It opens "<system>/<sub-folder>/<name>" with fopen, so on a
// case-insensitive volume (the default on macOS and Windows) JAGCD.BIN in "ATARI - JAGUAR CD" is found as well as
// jagcd.bin in "Atari - Jaguar CD"; on Linux the case must match exactly.
#if defined(__APPLE__) || defined(_WIN32)
const bool kCaseInsensitiveFs = true;
#else
const bool kCaseInsensitiveFs = false;
#endif
bool sameName(const std::string& a, const std::string& b) { return kCaseInsensitiveFs ? lowerStr(a) == lowerStr(b) : a == b; }
bool listHas(const char* const* list, const std::string& name) {
    for (; *list; list++)
        if (sameName(name, *list)) return true;
    return false;
}

}  // namespace

const char* biosRoleLabel(BiosRole r) {
    switch (r) {
    case BiosRole::CartBoot: return "Cart boot ROM";
    case BiosRole::CdBios: return "CD BIOS";
    case BiosRole::MemoryTrack: return "Memory Track";
    case BiosRole::Other: return "System file";
    default: return "";
    }
}

namespace {
struct ExtraBios {
    uint32_t crc;
    BiosRole role;
    std::string title;
};
std::vector<ExtraBios>& extraKnownBios() {   // tests only
    static std::vector<ExtraBios> v;
    return v;
}
}  // namespace

void addKnownBiosForTest(uint32_t crc, BiosRole role, const char* title) { extraKnownBios().push_back({crc, role, title}); }

BiosInfo classifyBios(const std::string& path, uint64_t size, uint32_t crcWhole) {
    BiosInfo b;
    if (crcWhole) {
        for (const ExtraBios& k : extraKnownBios()) {
            if (k.crc != crcWhole) continue;
            b.role = k.role;
            b.recognized = true;
            b.title = k.title;
            return b;
        }
        for (const KnownBios& k : kKnownBios) {
            if (k.crc != crcWhole) continue;
            b.role = k.role;
            b.recognized = true;
            b.title = k.title;
            return b;
        }
    }
    if (size > kMaxBiosBytes) return b;
    const std::string base = baseName(path);
    const std::string lname = lowerStr(base);
    const std::string stem = stemOf(path);
    const bool tagged = lname.rfind("[bios]", 0) == 0;
    if (inListCI(kCdBiosNames, base)) {
        b.role = BiosRole::CdBios;
    } else if (inListCI(kCartBootNames, base) || lname == "jagboot.rom" || lname == "jagboot_m.rom" ||
               lname == "boot.rom" || lname == "boot0.rom") {
        b.role = BiosRole::CartBoot;
    } else if (tagged || hasWord(lname, "bios")) {
        // Named a BIOS but not one the core searches for: say which kind when the name does.
        if (hasWord(lname, " cd") || hasWord(lname, "jagcd") || hasWord(lname, "jaguarcd")) b.role = BiosRole::CdBios;
        else if (tagged || hasWord(lname, "jag") || hasWord(lname, "boot")) b.role = BiosRole::CartBoot;
        else b.role = BiosRole::Other;
    } else if (hasWord(lname, "memory track") || hasWord(lname, "memtrack") || hasWord(lname, "memorytrack")) {
        b.role = BiosRole::MemoryTrack;
    } else {
        return b;
    }
    std::string t = stem;
    if (lowerStr(t).rfind("[bios]", 0) == 0) t = trimStr(t.substr(6));
    b.title = titleFromFileStem(t);
    return b;
}

BiosUse biosUse(const BiosInfo& b, const std::string& path, uint64_t size, const std::string& systemDir) {
    BiosUse u;
    const std::string base = baseName(path);
    const std::string dir = normDir(dirName(path));
    const std::string sys = normDir(systemDir);
    auto inDirs = [&](const char* const* dirs) {
        for (; *dirs; dirs++)
            if (!sys.empty() && sameName(dir, **dirs ? sys + "/" + *dirs : sys)) return true;
        return false;
    };
    auto sizeMsg = [&](uint64_t want) {
        return std::string("It is ") + std::to_string((unsigned long long)size) + " bytes and the core only accepts exactly " +
               std::to_string((unsigned long long)want) + ", so it skips this file.";
    };
    switch (b.role) {
    case BiosRole::CdBios:
        if (!listHas(kCdBiosNames, base)) {
            u.text = "The core's own search only looks for CD BIOS files by a fixed set of names (jagcd.bin, jaguarcd.bin, "
                     "jagcd_bios.bin, jaguarcd_bios.bin, Jaguar CD BIOS.rom/.bin, or '[BIOS] Atari Jaguar CD (World)' / "
                     "'Developer CD' with .j64, .rom or .bin). Use this BIOS (below) makes the app hand the core this file under any name.";
        } else if (!inDirs(kCdBiosDirs)) {
            u.text = "Not in the system folder or its Atari - Jaguar, Atari - Jaguar CD, jaguar or jaguarcd sub-folders, "
                     "so the core does not look at it here.";
        } else if (size != 0x40000) {
            u.text = sizeMsg(0x40000);
        } else {
            u.core = true;
            u.text = "Read when a CD boots with CD Boot Mode = Real BIOS or Auto: it is preferred over the built-in CD BIOS. "
                     "With both a retail and a developer file present, CD BIOS Type picks which one.";
        }
        break;
    case BiosRole::CartBoot:
        if (sameName(base, "jagboot_m.rom")) {
            if (!sameName(dir, sys) || sys.empty()) {
                u.text = "The core only reads jagboot_m.rom from the system folder itself, not from a sub-folder or anywhere else.";
            } else if (size != 0x20000) {
                u.text = sizeMsg(0x20000);
            } else {
                u.core = true;
                u.text = "Read when Cart BIOS Type = Model M (and the boot ROM runs, BIOS (Cartridges) = Real): it replaces "
                         "the built-in Model M image.";
            }
        } else if (!listHas(kCartBootNames, base)) {
            u.text = "The core's own search only looks for a custom boot ROM by name (jagboot.rom, boot.rom, boot0.rom or a "
                     "'[BIOS] Atari Jaguar...' name). Use this BIOS (below) makes the app hand the core this file under any name.";
        } else if (!inDirs(kCartBootDirs)) {
            u.text = "Not in the system folder or its Atari - Jaguar or jaguar sub-folders, so the core does not look at it here.";
        } else if (size != 0x20000) {
            u.text = sizeMsg(0x20000);
        } else {
            u.core = true;
            u.text = "Read only when Cart BIOS Type = Custom (and the boot ROM runs, BIOS (Cartridges) = Real, or a "
                     "GPU-only cart). Otherwise the built-in Series K / Model M image is used.";
        }
        if (b.recognized && b.title.find("Series K") != std::string::npos)
            u.text += " This is the same image as the built-in Series K, which never reads a file.";
        break;
    case BiosRole::MemoryTrack:
        u.text = "The core emulates the Memory Track itself (its data is the .srm save): this image is not read.";
        break;
    default: u.text = "The core does not read this file."; break;
    }
    return u;
}

std::string biosInstallName(const BiosInfo& b, uint64_t size) {
    const std::string lt = lowerStr(b.title);
    if (b.role == BiosRole::CartBoot) {
        if (size != 0x20000) return "";
        if (b.recognized && lt.find("series k") != std::string::npos) return "";   // the built-in image: never read
        if (b.recognized && lt.find("model m") != std::string::npos) return "jagboot_m.rom";
        if (b.recognized && lt.find("stubulator '93") != std::string::npos)
            return "[BIOS] Atari Jaguar Stubulator '93 (World).j64";
        if (b.recognized && lt.find("stubulator '94") != std::string::npos)
            return "[BIOS] Atari Jaguar Stubulator '94 (World).j64";
        return "jagboot.rom";
    }
    if (b.role == BiosRole::CdBios) {
        if (size != 0x40000) return "";
        if (b.recognized && lt.find("developer") != std::string::npos) return "[BIOS] Atari Jaguar Developer CD (World).bin";
        if (b.recognized) return "[BIOS] Atari Jaguar CD (World).bin";
        return "jagcd.bin";
    }
    return "";
}

// ---- scanning ----------------------------------------------------------------------------------------------

bool isCartExt(const std::string& e) {
    // JAGUAR_VALID_EXTENSIONS in libretro.c, minus the disc formats.  No zip: the core cannot read one.
    return e == "j64" || e == "jag" || e == "rom" || e == "abs" || e == "cof" || e == "bin" || e == "prg";
}
// m3u: a multi-disc playlist (m3u.h).  The scan lists it in place of the discs it names.
bool isDiscExt(const std::string& e) { return e == "cue" || e == "cdi" || e == "chd" || e == "m3u"; }

namespace {

const uint64_t kMaxCartBytes = 32ull << 20;     // far above any Jaguar cart; stops a stray big .bin
const uint64_t kMaxBinCartBytes = 8ull << 20;   // a .bin above this is a disc track, not a cartridge

// FILE names of a cue sheet, in order.
std::vector<std::string> cueFiles(const std::string& cuePath) {
    std::vector<std::string> out;
    size_t size = 0;
    void* data = SDL_LoadFile(cuePath.c_str(), &size);
    if (!data) return out;
    if (size > (1u << 20)) size = 1u << 20;
    std::string text((const char*)data, size);
    SDL_free(data);
    size_t pos = 0;
    while (pos < text.size()) {
        size_t nl = text.find('\n', pos);
        std::string line = trimStr(text.substr(pos, nl == std::string::npos ? std::string::npos : nl - pos));
        pos = nl == std::string::npos ? text.size() : nl + 1;
        if (line.size() < 6 || lowerStr(line.substr(0, 4)) != "file" || !std::isspace((unsigned char)line[4])) continue;
        std::string rest = trimStr(line.substr(5));
        std::string name;
        if (!rest.empty() && rest[0] == '"') {
            size_t q = rest.find('"', 1);
            name = rest.substr(1, q == std::string::npos ? std::string::npos : q - 1);
        } else {
            size_t sp = rest.find_first_of(" \t");
            name = rest.substr(0, sp);
        }
        if (!name.empty()) out.push_back(name);
    }
    return out;
}

// cue FILE name -> the real path next to the cue.  The directory listing supplies the
// spelling (a cue says "track 1.BIN", the file is "Track 1.bin"; on a case-insensitive
// filesystem both open, but only the real spelling can be compared with the scan's paths).
std::string resolveCueFile(const std::string& cueDir, std::string name) {
    for (char& c : name)
        if (c == '\\') c = '/';
    std::error_code ec;
    fs::path direct = (toPath(cueDir) / toPath(name)).lexically_normal();
    fs::path parent = direct.parent_path();
    std::string want = fromPath(direct.filename());
    std::string wantLower = lowerStr(want);
    std::string caseless;
    for (fs::directory_iterator it(parent, fs::directory_options::skip_permission_denied, ec), end; !ec && it != end;
         it.increment(ec)) {
        std::string have = fromPath(it->path().filename());
        if (have == want) return fromPath(it->path().lexically_normal());
        if (caseless.empty() && lowerStr(have) == wantLower) caseless = fromPath(it->path().lexically_normal());
    }
    return caseless;
}

struct Candidate {
    std::string path;
    Kind kind;
    uint64_t size = 0;
    int64_t mtime = 0;
    std::string track1;   // disc (.cue): resolved first FILE, or ""
    std::string disc1;    // .m3u: its first disc (hashed in its place), or ""
    bool sysOnly = false; // found in the core's system folder: kept only if it is a BIOS / system file
};

bool statFile(const std::string& path, uint64_t& size, int64_t& mtime) {
    SDL_PathInfo info;
    if (!SDL_GetPathInfo(path.c_str(), &info) || info.type != SDL_PATHTYPE_FILE) return false;
    size = (uint64_t)info.size;
    mtime = (int64_t)(info.modify_time / 1000000000LL);
    return true;
}

bool crcOfFile(const std::string& path, uint32_t& crc, const std::atomic<bool>* cancel) {
    SDL_IOStream* io = SDL_IOFromFile(path.c_str(), "rb");
    if (!io) return false;
    static const size_t kChunk = 1u << 20;
    std::vector<unsigned char> buf(kChunk);
    uint32_t c = 0;
    for (;;) {
        if (cancel && cancel->load()) {
            SDL_CloseIO(io);
            return false;
        }
        size_t n = SDL_ReadIO(io, buf.data(), buf.size());
        if (n == 0) break;
        c = crc32Update(c, buf.data(), n);
    }
    SDL_CloseIO(io);
    crc = c;
    return true;
}

}  // namespace

std::vector<ScanFile> scanFolders(const ScanOptions& opt, const ScanCache& cache, const std::atomic<bool>* cancel,
                                  const std::function<void(size_t, size_t)>& progress) {
    std::vector<ScanFile> out;
    std::vector<Candidate> cands;
    std::set<std::string> claimed;   // files a cue sheet owns (its tracks): not cartridges
    std::set<std::string> seen;      // the same file reached through two chosen folders
    std::error_code ec;

    // 1. enumerate
    std::vector<std::pair<std::string, bool>> roots;
    for (const std::string& folder : opt.folders) roots.emplace_back(folder, false);
    if (!opt.systemFolder.empty()) roots.emplace_back(opt.systemFolder, true);
    for (const auto& rt : roots) {
        const std::string& folder = rt.first;
        const bool sysRoot = rt.second;
        fs::path root = toPath(folder);
        if (!fs::is_directory(root, ec)) continue;
        fs::recursive_directory_iterator it(root, fs::directory_options::skip_permission_denied, ec), end;
        for (; !ec && it != end; it.increment(ec)) {
            if (cancel && cancel->load()) return out;
            if (it.depth() >= (sysRoot ? 3 : opt.maxDepth)) it.disable_recursion_pending();
            std::error_code e2;
            if (!it->is_regular_file(e2)) continue;
            std::string p = fromPath(it->path().lexically_normal());
            std::string e = extOf(p);
            bool disc = isDiscExt(e);
            if (sysRoot && disc) continue;   // the system folder is only read for BIOS files
            if (!disc && !isCartExt(e)) continue;
            if (!seen.insert(p).second) continue;
            Candidate c;
            c.path = p;
            c.kind = disc ? Kind::Disc : Kind::Cart;
            c.sysOnly = sysRoot;
            if (!statFile(p, c.size, c.mtime)) continue;
            if (e == "m3u") {
                // The playlist stands for its discs: they leave the grid (step 2), and it is identified by its first.
                size_t sz = 0;
                void* data = SDL_LoadFile(p.c_str(), &sz);
                std::string text = data ? std::string((const char*)data, std::min(sz, (size_t)(1u << 20))) : std::string();
                SDL_free(data);
                std::vector<std::string> discs = vj::parseM3u(text, p.substr(0, p.find_last_of("/\\") + 1));
                for (size_t i = 0; i < discs.size(); i++) {
                    std::string r = resolveCueFile("", discs[i]);   // the entry is already joined to the folder
                    if (r.empty()) continue;
                    claimed.insert(r);
                    if (i == 0) c.disc1 = r;
                }
                if (extOf(c.disc1) == "cue") {   // a cue sheet's track 1, exactly as for the cue itself
                    std::vector<std::string> files = cueFiles(c.disc1);
                    if (!files.empty()) c.track1 = resolveCueFile(c.disc1.substr(0, c.disc1.find_last_of("/\\") + 1), files[0]);
                    c.disc1.clear();
                }
                uint64_t bs;
                int64_t bm;
                const std::string& src = !c.track1.empty() ? c.track1 : c.disc1;
                if (!src.empty() && statFile(src, bs, bm)) {   // the fingerprint covers what the hash was taken from
                    c.size = bs;
                    c.mtime = std::max(c.mtime, bm);
                }
            }
            if (e == "cue") {
                std::vector<std::string> files = cueFiles(p);
                std::string dir = p.substr(0, p.find_last_of("/\\") + 1);
                for (size_t i = 0; i < files.size(); i++) {
                    std::string r = resolveCueFile(dir, files[i]);
                    if (r.empty()) continue;
                    claimed.insert(r);
                    if (i == 0) c.track1 = r;
                }
                if (!c.track1.empty()) {
                    uint64_t bs;
                    int64_t bm;
                    if (statFile(c.track1, bs, bm)) {
                        // the fingerprint covers what the hash was taken from
                        c.size = bs;
                        c.mtime = std::max(c.mtime, bm);
                    }
                }
            }
            cands.push_back(std::move(c));
        }
    }

    // 2. drop disc tracks and anything too large to be a cartridge
    std::vector<Candidate> keep;
    for (Candidate& c : cands) {
        if (c.kind == Kind::Disc && claimed.count(c.path)) continue;   // a disc an .m3u lists: the playlist is its entry
        if (c.kind == Kind::Cart) {
            if (claimed.count(c.path)) continue;
            if (extOf(c.path) == "bin" && c.size > kMaxBinCartBytes) continue;
            // A loose Redump track file ("Game (Track 2).bin") is disc data even without its cue sheet.
            if (extOf(c.path) == "bin" && lowerStr(stemOf(c.path)).find("(track ") != std::string::npos) continue;
            if (c.sysOnly && c.size > kMaxBiosBytes) continue;
        }
        keep.push_back(std::move(c));
    }

    // 3. hash what changed
    size_t total = 0;
    std::vector<char> need(keep.size(), 0);
    for (size_t i = 0; i < keep.size(); i++) {
        auto it = cache.find(keep[i].path);
        bool fresh = it != cache.end() && it->second.hashed && it->second.size == keep[i].size &&
                     it->second.mtime == keep[i].mtime && it->second.kind == keep[i].kind;
        if (!fresh) {
            need[i] = 1;
            total++;
        }
    }
    size_t done = 0;
    for (size_t i = 0; i < keep.size(); i++) {
        if (cancel && cancel->load()) return out;
        const Candidate& c = keep[i];
        ScanFile f;
        if (!need[i]) {
            f = cache.find(c.path)->second;
        } else {
            f.path = c.path;
            f.kind = c.kind;
            f.size = c.size;
            f.mtime = c.mtime;
            if (c.kind == Kind::Cart) {
                if (c.size > 0 && c.size <= kMaxCartBytes) {
                    size_t sz = 0;
                    void* data = SDL_LoadFile(c.path.c_str(), &sz);
                    if (data) {
                        f.crcWhole = crc32Update(0, data, sz);
                        uint32_t hdr = cartHeaderSize((const uint8_t*)data, sz);
                        if (hdr) f.crcStripped = crc32Update(0, (const uint8_t*)data + hdr, sz - hdr);
                        SDL_free(data);
                    }
                }
                f.hashed = true;
            } else if (!c.track1.empty()) {
                uint32_t crc = 0;
                if (crcOfFile(c.track1, crc, cancel)) {
                    f.crcWhole = crc;
                    f.hashed = true;
                } else if (cancel && cancel->load()) {
                    return out;
                }
            } else {
                // .chd / .cdi (or an .m3u's first one): track 1 straight from the image (0 = not hashable; the file
                // name is all there is then)
                const std::string& img = c.disc1.empty() ? c.path : c.disc1;
                const std::string ext = extOf(img);
                uint32_t crc = 0;
                if (ext == "chd" ? crcOfChdTrack1(img, crc, cancel) : ext == "cdi" ? crcOfCdiTrack1(img, crc, cancel) : false)
                    f.crcWhole = crc;
                else if (cancel && cancel->load())
                    return out;
                f.hashed = true;
            }
            done++;
            if (progress) progress(done, total);
        }
        if (c.sysOnly && classifyBios(f.path, f.size, f.crcWhole).role == BiosRole::None) continue;
        out.push_back(std::move(f));
    }
    std::sort(out.begin(), out.end(), [](const ScanFile& a, const ScanFile& b) { return a.path < b.path; });
    return out;
}

// ---- identification --------------------------------------------------------------------------------------

std::string titleFromFileStem(const std::string& stem) {
    std::string s = stem;
    for (char& c : s)
        if (c == '_') c = ' ';
    return trimStr(s);
}

std::string titleFromDiscStem(const std::string& stem) {
    std::string s = titleFromFileStem(stem);
    auto endsWithCI = [](const std::string& str, const char* suf) {
        size_t n = std::strlen(suf);
        if (str.size() < n) return false;
        for (size_t i = 0; i < n; i++)
            if (std::tolower((unsigned char)str[str.size() - n + i]) != suf[i]) return false;
        return true;
    };
    for (int pass = 0; pass < 4; pass++) {   // a few markers can stack: "Game (Track 1).bin"
        std::string before = s;
        for (const char* ext : {".bin", ".img", ".iso", ".cue"})
            if (endsWithCI(s, ext)) s = trimStr(s.substr(0, s.size() - 4));
        // "(Track N)" / "[Track N]" with one to three digits
        if (!s.empty() && (s.back() == ')' || s.back() == ']')) {
            const char open = s.back() == ')' ? '(' : '[';
            size_t o = s.find_last_of(open);
            if (o != std::string::npos) {
                std::string inner = lowerStr(trimStr(s.substr(o + 1, s.size() - o - 2)));
                bool ok = inner.size() >= 7 && inner.size() <= 9 && inner.compare(0, 6, "track ") == 0;
                for (size_t i = 6; ok && i < inner.size(); i++) ok = std::isdigit((unsigned char)inner[i]) != 0;
                if (ok) s = trimStr(s.substr(0, o));
            }
        }
        // " - Track N" at the very end (the hyphen is what makes it safe: a title may end in "Track 2")
        {
            size_t t = s.size();
            while (t > 0 && std::isdigit((unsigned char)s[t - 1])) t--;
            if (t < s.size() && s.size() - t <= 3 && t >= 6 && lowerStr(s.substr(t - 6, 6)) == "track ") {
                std::string head = trimStr(s.substr(0, t - 6));
                if (head.size() > 1 && head.back() == '-') s = trimStr(head.substr(0, head.size() - 1));
            }
        }
        if (s == before) break;
    }
    std::string out;   // collapse runs of spaces
    for (char c : s) {
        if (c == ' ' && !out.empty() && out.back() == ' ') continue;
        out += c;
    }
    out = trimStr(out);
    return out.empty() ? titleFromFileStem(stem) : out;
}

Entry identify(const ScanFile& f, const Dat* carts, const Dat* discs) {
    Entry e;
    e.file = f;
    const Dat* dat = f.kind == Kind::Cart ? carts : discs;
    const DatGame* g = nullptr;
    if (dat && !dat->empty()) {
        g = dat->byCrc(f.crcWhole);
        if (!g && f.crcStripped) g = dat->byCrc(f.crcStripped);
        if (g) {
            e.match = Match::Crc;
            e.how = f.kind == Kind::Disc ? "CRC32 of track 1 (Redump)" : "CRC32 of the file";
        } else {
            g = dat->byName(stemOf(f.path));
            if (g) {
                e.match = Match::Name;
                e.how = "file name";
            } else if (f.kind == Kind::Disc) {
                // Rips that are not Redump-exact (other offsets, another dump) have another track-1 checksum:
                // the file name, then the name of the folder it sits in, against the dat's titles.
                int how = 0;
                std::string dir = normDir(dirName(f.path));
                g = dat->byLooseName(stemOf(f.path), baseName(dir), &how);
                if (g) {
                    e.match = Match::Name;
                    e.how = how == 2 ? "folder name" : how == 3 ? "file name (start of a longer title)" : "file name";
                }
            }
        }
    }
    if (!g) e.how = "not identified";
    if (g) {
        e.title = g->name;
        e.datName = g->name;
        e.region = g->region;
    } else {
        e.title = f.kind == Kind::Disc ? titleFromDiscStem(stemOf(f.path)) : titleFromFileStem(stemOf(f.path));
    }
    e.section = f.kind == Kind::Disc ? Section::Disc : Section::Cart;
    if (f.kind == Kind::Cart) {
        // BIOS and system files are not games: by checksum / name, or tagged "[BIOS]" in the dat.
        BiosInfo b = classifyBios(f.path, f.size, f.crcWhole);
        if (b.role == BiosRole::None && e.datName.rfind("[BIOS]", 0) == 0) {
            b = classifyBios("[BIOS] " + e.datName.substr(6), 0, 0);   // the dat's name through the name rules
            if (b.role == BiosRole::None) {
                b.role = BiosRole::Other;
                b.title = trimStr(e.datName.substr(6));
            }
        }
        if (b.role != BiosRole::None) {
            e.section = Section::Bios;
            if (!b.recognized && !e.datName.empty() && e.datName.rfind("[BIOS]", 0) == 0) b.title = trimStr(e.datName.substr(6));
            e.title = b.title;
            e.bios = b;
        }
    }
    return e;
}

// ---- tile zoom -----------------------------------------------------------------------------------------

float clampTileScale(float s) {
    if (!std::isfinite(s)) return kTileScaleDefault;
    return std::min(kTileScaleMax, std::max(kTileScaleMin, s));
}

float zoomTileScale(float cur, float steps) {
    if (!std::isfinite(steps)) return clampTileScale(cur);
    steps = std::min(20.0f, std::max(-20.0f, steps));   // a runaway wheel delta cannot overflow the power
    return clampTileScale(clampTileScale(cur) * std::pow(kTileScaleStep, steps));
}

float pinchTileScale(float cur, float factor) {
    if (!std::isfinite(factor) || factor <= 0.0f) return clampTileScale(cur);
    factor = std::min(2.0f, std::max(0.5f, factor));    // one update is a small change; a glitch is not a jump
    return clampTileScale(clampTileScale(cur) * factor);
}

// ---- scan cache serialisation --------------------------------------------------------------------------------

std::string serializeCache(const std::vector<ScanFile>& files) {
    std::string out = "VJLIB2\n";
    char buf[160];
    for (const ScanFile& f : files) {
        if (!f.hashed || f.path.find_first_of("\t\r\n") != std::string::npos) continue;
        std::snprintf(buf, sizeof buf, "\t%c\t%llu\t%lld\t%08X\t%08X\n", f.kind == Kind::Cart ? 'C' : 'D',
                      (unsigned long long)f.size, (long long)f.mtime, f.crcWhole, f.crcStripped);
        out += f.path;
        out += buf;
    }
    return out;
}

ScanCache parseCache(const std::string& text) {
    ScanCache out;
    size_t pos = 0;
    bool first = true, v1 = false;
    while (pos < text.size()) {
        size_t nl = text.find('\n', pos);
        std::string line = text.substr(pos, nl == std::string::npos ? std::string::npos : nl - pos);
        pos = nl == std::string::npos ? text.size() : nl + 1;
        if (first) {
            first = false;
            // VJLIB1 predates .chd / .cdi checksums: its disc images are hashed again, the rest is kept.
            if (line == "VJLIB1") v1 = true;
            else if (line != "VJLIB2") return out;
            continue;
        }
        std::vector<std::string> col;
        size_t a = 0;
        for (;;) {
            size_t t = line.find('\t', a);
            col.push_back(line.substr(a, t == std::string::npos ? std::string::npos : t - a));
            if (t == std::string::npos) break;
            a = t + 1;
        }
        if (col.size() != 6 || col[0].empty() || (col[1] != "C" && col[1] != "D")) continue;
        ScanFile f;
        f.path = col[0];
        f.kind = col[1] == "C" ? Kind::Cart : Kind::Disc;
        f.size = std::strtoull(col[2].c_str(), nullptr, 10);
        f.mtime = std::strtoll(col[3].c_str(), nullptr, 10);
        f.crcWhole = parseHex32(col[4]);
        f.crcStripped = parseHex32(col[5]);
        f.hashed = !(v1 && f.kind == Kind::Disc && f.crcWhole == 0);
        out[f.path] = f;
    }
    return out;
}

// ---- libretro-thumbnails ---------------------------------------------------------------------------------------

const char* const kThumbBase = "https://thumbnails.libretro.com/";
const char* const kThumbSystem = "Atari - Jaguar";
const char* const kThumbSystemCd = "Atari - Jaguar CD";
const char* const kMirrorBase = "https://jaguar.provenance-emu.com/thumbnails/";
const char* const kThumbTypes[3] = {"Named_Boxarts", "Named_Titles", "Named_Snaps"};

std::string sanitizeThumbName(const std::string& title) {
    std::string out = title;
    for (char& c : out) {
        switch (c) {
        case '&': case '*': case '/': case ':': case '`': case '"':
        case '<': case '>': case '?': case '\\': case '|':
            c = '_';
            break;
        default: break;
        }
    }
    return out;
}

std::string urlEncode(const std::string& s) {
    static const char* hex = "0123456789ABCDEF";
    std::string out;
    for (unsigned char c : s) {
        if (std::isalnum(c) || c == '-' || c == '.' || c == '_' || c == '~') {
            out += (char)c;
        } else {
            out += '%';
            out += hex[c >> 4];
            out += hex[c & 15];
        }
    }
    return out;
}

std::string thumbnailUrl(const std::string& type, const std::string& title, const std::string& base,
                         const char* system) {
    return base + urlEncode(system) + "/" + type + "/" +
           urlEncode(sanitizeThumbName(title)) + ".png";
}

std::string thumbnailCachePath(const std::string& root, const std::string& type, const std::string& title,
                               const char* system) {
    return root + "thumbnails/" + system + "/" + type + "/" + sanitizeThumbName(title) + ".png";
}

std::string thumbnailMissPath(const std::string& root, const std::string& type, const std::string& title,
                              const char* system) {
    return thumbnailCachePath(root, type, title, system) + ".miss";
}

std::vector<ArtSource> artSources(Section section, const std::string& upstreamBase, const std::string& mirrorBase) {
    std::vector<ArtSource> v;
    v.push_back({upstreamBase, kThumbSystem, kMissRetrySeconds, false});
    if (section == Section::Disc && !mirrorBase.empty()) {
        std::string b = mirrorBase;
        if (b.back() != '/') b += '/';
        v.push_back({b, kThumbSystemCd, 0, true});
    }
    return v;
}

// ---- covers ------------------------------------------------------------------------------------------------------

namespace {

// A file name from arbitrary text: the thumbnail replacements plus the control characters and trailing dots /
// spaces Windows refuses; never empty.
std::string coverFileName(const std::string& name) {
    std::string s = sanitizeThumbName(name);
    for (char& c : s)
        if ((unsigned char)c < 32) c = '_';
    while (!s.empty() && (s.back() == '.' || s.back() == ' ')) s.pop_back();
    return s.empty() ? std::string("_") : s;
}

}  // namespace

std::string coverKey(const Entry& e) {
    char buf[32];
    if (!e.datName.empty()) return e.datName;
    if (e.file.crcWhole) {
        std::snprintf(buf, sizeof buf, "crc-%08X", (unsigned)e.file.crcWhole);
        return buf;
    }
    std::snprintf(buf, sizeof buf, "path-%08X", (unsigned)crc32Update(0, e.file.path.data(), e.file.path.size()));
    return buf;
}

std::string customCoverPath(const std::string& root, const std::string& key) {
    return root + "covers/custom/" + coverFileName(key) + ".png";
}

std::string localCoverPath(const std::string& root, const std::string& name) {
    return root + "covers/" + coverFileName(name) + ".png";
}

std::vector<std::string> localCoverPaths(const std::string& root, const std::string& key, const std::string& title,
                                         const std::string& datName) {
    std::vector<std::string> out;
    auto add = [&](const std::string& p) {
        if (std::find(out.begin(), out.end(), p) == out.end()) out.push_back(p);
    };
    add(customCoverPath(root, key));
    if (!datName.empty()) add(localCoverPath(root, datName));
    if (!title.empty()) add(localCoverPath(root, title));
    return out;
}

CoverChoice chooseCover(const std::string& root, const std::string& key, const std::string& title,
                        const std::string& datName, const std::function<bool(const std::string&)>& exists) {
    CoverChoice c;
    const std::vector<std::string> local = localCoverPaths(root, key, title, datName);
    for (size_t i = 0; i < local.size(); i++) {
        if (exists(local[i])) {
            c.source = i == 0 ? CoverSource::Custom : CoverSource::LocalFolder;
            c.path = local[i];
            return c;
        }
    }
    if (!datName.empty()) {
        for (const char* type : kThumbTypes) {
            // the same order the service fetches in: libretro-thumbnails' copy, then the site mirror's (discs only)
            for (const char* system : {kThumbSystem, kThumbSystemCd}) {
                std::string p = thumbnailCachePath(root, type, datName, system);
                if (exists(p)) {
                    c.source = CoverSource::Downloaded;
                    c.path = p;
                    return c;
                }
            }
        }
    }
    return c;
}

}  // namespace lib
}  // namespace vj
