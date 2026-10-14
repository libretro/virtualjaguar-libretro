#include "fonts_system.h"

#include <algorithm>
#include <atomic>
#include <cctype>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <set>
#include <thread>

#include <SDL3/SDL.h>

namespace vj {
namespace fonts {

std::string makeFontId(const std::string& path, int index) { return path + "|" + std::to_string(index < 0 ? 0 : index); }

bool parseFontId(const std::string& id, FontFile& out) {
    const size_t bar = id.rfind('|');
    if (bar == std::string::npos || bar == 0) return false;
    char* end = nullptr;
    const long idx = std::strtol(id.c_str() + bar + 1, &end, 10);
    if (end == id.c_str() + bar + 1 || *end != '\0' || idx < 0 || idx > 1023) return false;
    out.path = id.substr(0, bar);
    out.index = (int)idx;
    return true;
}

std::string SystemFont::id() const { return makeFontId(path, index); }

// ---------------------------------------------------------------------------------------------
// where the fonts live
// ---------------------------------------------------------------------------------------------
namespace {

std::string withSlash(std::string d) {
    if (!d.empty() && d.back() != '/' && d.back() != '\\') d += '/';
    return d;
}

void addDir(std::vector<std::string>& v, const std::string& d) {
    if (d.empty()) return;
    for (const std::string& e : v)
        if (e == d) return;
    v.push_back(d);
}

}  // namespace

std::vector<std::string> systemFontDirs(Os os, const Probe& p) {
    std::vector<std::string> v;
    auto env = [&](const char* k) { return p.env ? p.env(k) : std::string(); };
    switch (os) {
    case Os::Mac: {
        addDir(v, "/System/Library/Fonts/");
        addDir(v, "/System/Library/Fonts/Supplemental/");
        addDir(v, "/Library/Fonts/");
        const std::string home = env("HOME");
        if (!home.empty()) addDir(v, withSlash(home) + "Library/Fonts/");
        break;
    }
    case Os::Windows: {
        std::string root = env("WINDIR");
        if (root.empty()) root = env("SystemRoot");
        if (root.empty()) root = "C:\\Windows";
        addDir(v, withSlash(root) + "Fonts/");
        const std::string local = env("LOCALAPPDATA");
        if (!local.empty()) addDir(v, withSlash(local) + "Microsoft/Windows/Fonts/");
        break;
    }
    case Os::Linux: {
        addDir(v, "/usr/share/fonts/");
        addDir(v, "/usr/local/share/fonts/");
        const std::string home = env("HOME");
        std::string xdg = env("XDG_DATA_HOME");
        if (xdg.empty() && !home.empty()) xdg = withSlash(home) + ".local/share";
        if (!xdg.empty()) addDir(v, withSlash(xdg) + "fonts/");
        if (!home.empty()) addDir(v, withSlash(home) + ".fonts/");
        std::string dirs = env("XDG_DATA_DIRS");
        size_t pos = 0;
        while (pos < dirs.size()) {
            size_t colon = dirs.find(':', pos);
            if (colon == std::string::npos) colon = dirs.size();
            const std::string d = dirs.substr(pos, colon - pos);
            if (!d.empty()) addDir(v, withSlash(d) + "fonts/");
            pos = colon + 1;
        }
        break;
    }
    }
    return v;
}

// ---------------------------------------------------------------------------------------------
// reading one font's header
// ---------------------------------------------------------------------------------------------
namespace {

uint32_t rd16(const unsigned char* p) { return ((uint32_t)p[0] << 8) | p[1]; }
uint32_t rd32(const unsigned char* p) { return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3]; }

struct IoCloser {
    SDL_IOStream* io;
    ~IoCloser() {
        if (io) SDL_CloseIO(io);
    }
};

bool readAt(SDL_IOStream* io, Sint64 off, void* buf, size_t n) {
    if (SDL_SeekIO(io, off, SDL_IO_SEEK_SET) < 0) return false;
    return SDL_ReadIO(io, buf, n) == n;
}

struct TableRec {
    uint32_t off = 0, len = 0;
    bool present = false;
};

void appendUtf8(std::string& s, uint32_t cp) {
    if (cp < 0x80) s += (char)cp;
    else if (cp < 0x800) {
        s += (char)(0xC0 | (cp >> 6));
        s += (char)(0x80 | (cp & 0x3F));
    } else if (cp < 0x10000) {
        s += (char)(0xE0 | (cp >> 12));
        s += (char)(0x80 | ((cp >> 6) & 0x3F));
        s += (char)(0x80 | (cp & 0x3F));
    } else {
        s += (char)(0xF0 | (cp >> 18));
        s += (char)(0x80 | ((cp >> 12) & 0x3F));
        s += (char)(0x80 | ((cp >> 6) & 0x3F));
        s += (char)(0x80 | (cp & 0x3F));
    }
}

// One entry of the `name` table, decoded.  Windows / Unicode strings are UTF-16BE; Mac Roman is read as ASCII
// (the English names of fonts are ASCII; anything else is dropped).
std::string decodeName(const unsigned char* s, size_t n, uint32_t platform) {
    std::string out;
    if (platform == 3 || platform == 0) {
        for (size_t i = 0; i + 1 < n; i += 2) {
            uint32_t cp = rd16(s + i);
            if (cp >= 0xD800 && cp < 0xDC00 && i + 3 < n) {
                const uint32_t lo = rd16(s + i + 2);
                if (lo >= 0xDC00 && lo < 0xE000) {
                    cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
                    i += 2;
                }
            }
            if (cp >= 0x20) appendUtf8(out, cp);
        }
    } else {
        for (size_t i = 0; i < n; i++)
            if (s[i] >= 0x20 && s[i] < 0x7F) out += (char)s[i];
    }
    return out;
}

// Preference for a name record: English Windows names first, then English Mac names, then any Windows/Unicode name.
int nameScore(uint32_t platform, uint32_t lang) {
    if (platform == 3 && lang == 0x409) return 4;
    if (platform == 1 && lang == 0) return 3;
    if (platform == 3 || platform == 0) return 2;
    return 1;
}

struct Names {
    std::string v[5];   // 0: family(1) 1: style(2) 2: full(4) 3: typographic family(16) 4: typographic style(17)
    int score[5] = {0, 0, 0, 0, 0};
};

void parseNameTable(const unsigned char* t, size_t len, Names& out) {
    if (len < 6) return;
    const uint32_t count = rd16(t + 2), strOff = rd16(t + 4);
    for (uint32_t i = 0; i < count && 6 + (size_t)(i + 1) * 12 <= len; i++) {
        const unsigned char* r = t + 6 + (size_t)i * 12;
        const uint32_t platform = rd16(r), lang = rd16(r + 4), id = rd16(r + 6), n = rd16(r + 8), o = rd16(r + 10);
        int slot = -1;
        switch (id) {
        case 1: slot = 0; break;
        case 2: slot = 1; break;
        case 4: slot = 2; break;
        case 16: slot = 3; break;
        case 17: slot = 4; break;
        default: break;
        }
        if (slot < 0 || (size_t)strOff + o + n > len) continue;
        const int sc = nameScore(platform, lang);
        if (sc <= out.score[slot]) continue;
        const std::string s = decodeName(t + strOff + o, n, platform);
        if (s.empty()) continue;
        out.v[slot] = s;
        out.score[slot] = sc;
    }
}

}  // namespace

bool readFaceInfo(const std::string& path, int faceIndex, SystemFont& out, int* numFaces) {
    IoCloser io{SDL_IOFromFile(path.c_str(), "rb")};
    if (!io.io) return false;
    const Sint64 fileSize = SDL_GetIOSize(io.io);
    if (fileSize < 256 || (uint64_t)fileSize > kMaxFontBytes) return false;

    unsigned char hdr[12];
    if (!readAt(io.io, 0, hdr, sizeof hdr)) return false;
    uint32_t base = 0;
    uint32_t faces = 1;
    if (!std::memcmp(hdr, "ttcf", 4)) {
        faces = rd32(hdr + 8);
        if (faces == 0 || faces > 64) return false;
        if (faceIndex < 0 || (uint32_t)faceIndex >= faces) return false;
        unsigned char o[4];
        if (!readAt(io.io, 12 + (Sint64)faceIndex * 4, o, 4)) return false;
        base = rd32(o);
        if ((Sint64)base + 12 > fileSize) return false;
        if (!readAt(io.io, base, hdr, sizeof hdr)) return false;
    } else if (faceIndex != 0) {
        return false;
    }
    if (numFaces) *numFaces = (int)faces;
    const uint32_t ver = rd32(hdr);
    if (ver != 0x00010000 && std::memcmp(hdr, "OTTO", 4) != 0 && std::memcmp(hdr, "true", 4) != 0) return false;
    const uint32_t nt = rd16(hdr + 4);
    if (nt == 0 || nt > 64) return false;

    std::vector<unsigned char> dir((size_t)nt * 16);
    if (!readAt(io.io, (Sint64)base + 12, dir.data(), dir.size())) return false;
    TableRec cmap, glyf, loca, cff, name, os2, post;
    for (uint32_t i = 0; i < nt; i++) {
        const unsigned char* e = dir.data() + (size_t)i * 16;
        TableRec t;
        t.off = rd32(e + 8);
        t.len = rd32(e + 12);
        t.present = true;
        if ((Sint64)t.off > fileSize || (Sint64)t.len > fileSize - (Sint64)t.off) return false;
        if (!std::memcmp(e, "cmap", 4)) cmap = t;
        else if (!std::memcmp(e, "glyf", 4)) glyf = t;
        else if (!std::memcmp(e, "loca", 4)) loca = t;
        else if (!std::memcmp(e, "CFF ", 4)) cff = t;
        else if (!std::memcmp(e, "name", 4)) name = t;
        else if (!std::memcmp(e, "OS/2", 4)) os2 = t;
        else if (!std::memcmp(e, "post", 4)) post = t;
    }
    // Real outlines (not bitmap / colour-only) and a character map; CFF2 (variable CFF) is not read by stb_truetype.
    if (!cmap.present || !name.present || !((glyf.present && loca.present) || cff.present)) return false;
    if (name.len < 6 || name.len > (1u << 20)) return false;

    std::vector<unsigned char> nm(name.len);
    if (!readAt(io.io, name.off, nm.data(), nm.size())) return false;
    Names n;
    parseNameTable(nm.data(), nm.size(), n);
    std::string family = !n.v[3].empty() ? n.v[3] : n.v[0];
    std::string style = !n.v[4].empty() ? n.v[4] : n.v[1];
    if (family.empty() || family[0] == '.') return false;   // hidden system faces (".SF NS")
    if (style == "Regular") style.clear();

    SystemFont f;
    f.path = path;
    f.index = faceIndex;
    f.family = family;
    f.style = style;
    if (!n.v[2].empty()) f.name = n.v[2];
    else f.name = style.empty() ? family : family + " " + style;
    if (os2.present && os2.len >= 6) {
        unsigned char b[64] = {0};
        const size_t take = std::min<size_t>(os2.len, sizeof b);
        if (readAt(io.io, os2.off, b, take)) {
            f.weight = (int)rd16(b + 4);
            if (take >= 64) f.italic = (rd16(b + 62) & 1u) != 0;
        }
    }
    if (f.weight < 1 || f.weight > 1000) f.weight = 400;
    if (!f.italic) {
        const std::string s = f.style;
        f.italic = s.find("Italic") != std::string::npos || s.find("Oblique") != std::string::npos;
    }
    if (post.present && post.len >= 16) {
        unsigned char b[16];
        if (readAt(io.io, post.off, b, sizeof b)) f.mono = rd32(b + 12) != 0;
    }
    out = std::move(f);
    return true;
}

// ---------------------------------------------------------------------------------------------
// walking the folders
// ---------------------------------------------------------------------------------------------
namespace {

bool hasFontExt(const std::string& p) {
    const size_t dot = p.find_last_of('.');
    if (dot == std::string::npos) return false;
    std::string e = p.substr(dot + 1);
    for (char& c : e) c = (char)std::tolower((unsigned char)c);
    return e == "ttf" || e == "otf" || e == "ttc" || e == "otc";
}

struct WalkCtx {
    std::string dir;
    std::vector<std::string> names;
};

void walk(const std::string& dir, int depth, size_t maxFiles, std::vector<std::string>& files, std::set<std::string>& seen) {
    if (depth > 4 || files.size() >= maxFiles) return;
    WalkCtx ctx;
    ctx.dir = withSlash(dir);
    SDL_EnumerateDirectory(
        dir.c_str(),
        [](void* ud, const char*, const char* fname) -> SDL_EnumerationResult {
            static_cast<WalkCtx*>(ud)->names.push_back(fname);
            return SDL_ENUM_CONTINUE;
        },
        &ctx);
    std::sort(ctx.names.begin(), ctx.names.end());
    std::vector<std::string> subdirs;
    for (const std::string& nm : ctx.names) {
        if (files.size() >= maxFiles) break;
        if (nm.empty() || nm[0] == '.') continue;
        const std::string full = ctx.dir + nm;
        SDL_PathInfo info;
        if (!SDL_GetPathInfo(full.c_str(), &info)) continue;
        if (info.type == SDL_PATHTYPE_DIRECTORY) subdirs.push_back(full);
        else if (info.type == SDL_PATHTYPE_FILE && hasFontExt(nm) && seen.insert(full).second) files.push_back(full);
    }
    for (const std::string& sd : subdirs) walk(sd, depth + 1, maxFiles, files, seen);
}

std::string lower(std::string s) {
    for (char& c : s) c = (char)std::tolower((unsigned char)c);
    return s;
}

}  // namespace

std::vector<SystemFont> scanFontDirs(const std::vector<std::string>& dirs, size_t maxFiles) {
    std::vector<std::string> files;
    std::set<std::string> seen;
    for (const std::string& d : dirs) walk(d, 0, maxFiles, files, seen);

    std::vector<SystemFont> out;
    std::set<std::string> names;   // the same face installed twice (system + user folder) is listed once
    for (const std::string& f : files) {
        int faces = 1;
        SystemFont first;
        if (!readFaceInfo(f, 0, first, &faces)) continue;
        for (int i = 0; i < faces; i++) {
            SystemFont sf;
            if (i == 0) sf = first;
            else if (!readFaceInfo(f, i, sf)) continue;
            if (!names.insert(lower(sf.name)).second) continue;
            out.push_back(std::move(sf));
        }
    }
    std::sort(out.begin(), out.end(), [](const SystemFont& a, const SystemFont& b) {
        const std::string fa = lower(a.family), fb = lower(b.family);
        if (fa != fb) return fa < fb;
        if (a.italic != b.italic) return !a.italic;
        if (a.weight != b.weight) return a.weight < b.weight;
        return lower(a.name) < lower(b.name);
    });
    return out;
}

const SystemFont* findSystemFont(const std::vector<SystemFont>& list, const std::string& id) {
    FontFile ff;
    if (!parseFontId(id, ff)) return nullptr;
    for (const SystemFont& f : list)
        if (f.index == ff.index && f.path == ff.path) return &f;
    return nullptr;
}

// ---------------------------------------------------------------------------------------------
// the machine's list
// ---------------------------------------------------------------------------------------------
namespace {

struct Index {
    std::mutex mu;
    std::thread th;
    bool started = false;
    std::shared_ptr<const std::vector<SystemFont>> list;
    ~Index() {
        if (th.joinable()) th.join();   // the scan is short; wait rather than race the process teardown
    }
};
Index& index() {
    static Index i;
    return i;
}

}  // namespace

void systemFontsStart() {
    Index& ix = index();
    std::lock_guard<std::mutex> lk(ix.mu);
    if (ix.started) return;
    ix.started = true;
    ix.th = std::thread([&ix]() {
        auto list = std::make_shared<const std::vector<SystemFont>>(scanFontDirs(systemFontDirs(hostOs(), systemProbe())));
        std::lock_guard<std::mutex> lk2(ix.mu);
        ix.list = std::move(list);
    });
}

bool systemFontsReady() {
    Index& ix = index();
    std::lock_guard<std::mutex> lk(ix.mu);
    return ix.list != nullptr;
}

std::shared_ptr<const std::vector<SystemFont>> systemFonts() {
    Index& ix = index();
    std::lock_guard<std::mutex> lk(ix.mu);
    return ix.list;
}

}  // namespace fonts
}  // namespace vj
