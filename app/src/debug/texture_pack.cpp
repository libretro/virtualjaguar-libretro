#include "texture_pack.h"

#include <algorithm>
#include <cstdio>
#include <cstddef>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <system_error>

namespace fs = std::filesystem;

namespace vj {
namespace tex {

namespace {

fs::path toPath(const std::string& s) { return fs::u8path(s); }

bool isHexLower(char c) { return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'); }

std::vector<std::string> splitTabs(const std::string& line) {
    std::vector<std::string> out;
    size_t start = 0;
    for (;;) {
        size_t t = line.find('\t', start);
        if (t == std::string::npos) {
            out.push_back(line.substr(start));
            break;
        }
        out.push_back(line.substr(start, t - start));
        start = t + 1;
    }
    return out;
}

bool startsWith(const std::string& s, const char* p) { return s.compare(0, std::strlen(p), p) == 0; }

// "key=value" -> value, or "" when the key does not match.
std::string valueOf(const std::string& field, const char* key) {
    size_t n = std::strlen(key);
    if (field.size() > n && field.compare(0, n, key) == 0 && field[n] == '=') return field.substr(n + 1);
    return std::string();
}

bool parseUnsigned(const std::string& s, int base, unsigned& v) {
    if (s.empty()) return false;
    char* end = nullptr;
    unsigned long x = std::strtoul(s.c_str(), &end, base);
    if (!end || *end != '\0') return false;
    v = (unsigned)x;
    return true;
}

bool parseSize(const std::string& s, unsigned& w, unsigned& h) {
    size_t x = s.find('x');
    if (x == std::string::npos) return false;
    return parseUnsigned(s.substr(0, x), 10, w) && parseUnsigned(s.substr(x + 1), 10, h);
}

bool readWholeFile(const std::string& path, std::vector<uint8_t>& out, size_t maxBytes) {
    std::ifstream f(toPath(path), std::ios::binary | std::ios::ate);
    if (!f) return false;
    std::streamoff n = f.tellg();
    if (n < 0 || (uint64_t)n > maxBytes) return false;
    out.resize((size_t)n);
    f.seekg(0);
    if (n > 0) f.read((char*)out.data(), n);
    return (bool)f;
}

// Write to <dest>.tmp, then rename over dest.
bool writeAtomic(const std::string& dest, const std::vector<uint8_t>& bytes, std::string& err) {
    fs::path d = toPath(dest);
    fs::path tmp = d;
    tmp += ".tmp";
    {
        std::ofstream o(tmp, std::ios::binary | std::ios::trunc);
        if (!o) {
            err = "cannot write " + dest;
            return false;
        }
        if (!bytes.empty()) o.write((const char*)bytes.data(), (std::streamsize)bytes.size());
        o.flush();
        if (!o) {
            err = "write failed: " + dest;
            std::error_code ec;
            fs::remove(tmp, ec);
            return false;
        }
    }
    std::error_code ec;
    fs::rename(tmp, d, ec);
    if (ec) {   // Windows: rename onto an existing file
        fs::remove(d, ec);
        ec.clear();
        fs::rename(tmp, d, ec);
        if (ec) {
            err = "cannot replace " + dest + ": " + ec.message();
            fs::remove(tmp, ec);
            return false;
        }
    }
    return true;
}

bool isPngFile(const uint8_t* b, size_t n) {
    static const uint8_t sig[8] = {0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A};
    return n >= 8 && std::memcmp(b, sig, 8) == 0;
}

}  // namespace

// ---- names and paths -----------------------------------------------------------------------------

std::string crcHex(uint32_t crc) {
    char b[16];
    std::snprintf(b, sizeof b, "%08x", (unsigned)crc);
    return b;
}

bool isHash16(const std::string& s) {
    if (s.size() != 16) return false;
    for (char c : s)
        if (!isHexLower(c)) return false;
    return true;
}

std::string joinPath(const std::string& a, const std::string& b) {
    if (a.empty()) return b;
    if (b.empty()) return a;
    char last = a.back();
    if (last == '/' || last == '\\') return a + b;
    return a + "/" + b;
}

std::string dumpDir(const std::string& systemDir, uint32_t crc) {
    return joinPath(joinPath(systemDir, "vj_texdump"), crcHex(crc));
}
std::string packDir(const std::string& systemDir, uint32_t crc) {
    return joinPath(joinPath(systemDir, "vj_texpacks"), crcHex(crc));
}
std::string packFile(const std::string& systemDir, uint32_t crc, const std::string& hash) {
    return joinPath(packDir(systemDir, crc), hash + ".png");
}

std::string fileUrl(const std::string& path) {
    std::string p = path;
    for (char& c : p)
        if (c == '\\') c = '/';
    std::string out = "file://";
    if (p.empty() || p[0] != '/') out += '/';   // C:/dir -> file:///C:/dir
    static const char* const hex = "0123456789ABCDEF";
    for (unsigned char c : p) {
        const bool plain = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '/' ||
                           c == '-' || c == '_' || c == '.' || c == '~' || c == ':';
        if (plain) {
            out += (char)c;
        } else {
            out += '%';
            out += hex[c >> 4];
            out += hex[c & 15];
        }
    }
    return out;
}

std::string previewFile(const std::string& dumpDirPath, const std::string& hash) {
    static const char* const suffix[] = {".png", "-cry.png", "-rgb.png"};
    for (const char* s : suffix) {
        std::string p = joinPath(dumpDirPath, hash + s);
        std::error_code ec;
        if (fs::is_regular_file(toPath(p), ec)) return p;
    }
    return std::string();
}

std::string hashOfDumpFile(const std::string& name) {
    static const char* const suffix[] = {".png", "-cry.png", "-rgb.png"};
    for (const char* s : suffix) {
        size_t sl = std::strlen(s);
        if (name.size() == 16 + sl && name.compare(16, sl, s) == 0) {
            std::string h = name.substr(0, 16);
            if (isHash16(h)) return h;
        }
    }
    return std::string();
}

// ---- the manifest ----------------------------------------------------------------------------------

const Entry* Manifest::find(const std::string& hash) const {
    auto it = index.find(hash);
    return it == index.end() ? nullptr : &entries[it->second];
}

static void parseLine(Manifest& m, std::string line) {
    while (!line.empty() && (line.back() == '\r' || line.back() == ' ')) line.pop_back();
    if (line.empty()) return;
    if (line[0] == '#') {
        if (startsWith(line, "# texdump")) {
            std::vector<std::string> f = splitTabs(line);
            m.haveHeader = true;
            unsigned v = 0;
            size_t vp = f[0].find(" v");
            if (vp != std::string::npos && parseUnsigned(f[0].substr(vp + 2), 10, v)) m.version = v;
            for (size_t i = 1; i < f.size(); i++) {
                std::string c = valueOf(f[i], "crc");
                if (!c.empty()) m.crc = c;
                if (startsWith(f[i], "title=")) m.title = f[i].substr(6);
            }
        }
        return;
    }
    std::vector<std::string> f = splitTabs(line);
    if (!isHash16(f[0]) || f.size() < 3) {
        m.badLines++;
        return;
    }
    // An additional palette sighting: hash <TAB> clut=... <TAB> frame=...
    if (startsWith(f[1], "clut=")) {
        auto it = m.index.find(f[0]);
        if (it != m.index.end()) m.entries[it->second].sightings++;
        return;
    }
    Entry e;
    e.hash = f[0];
    unsigned bpp = 0;
    if (!parseSize(f[1], e.w, e.h) || !parseUnsigned(f[2], 10, bpp)) {
        m.badLines++;
        return;
    }
    e.bpp = bpp;
    for (size_t i = 3; i < f.size(); i++) {
        std::string v;
        if (!(v = valueOf(f[i], "frame")).empty()) parseUnsigned(v, 10, e.frame);
        else if (!(v = valueOf(f[i], "src")).empty()) parseUnsigned(v, 16, e.src);
        else if (!(v = valueOf(f[i], "clut")).empty()) e.clut = v == "-" ? std::string() : v;
        else if (!(v = valueOf(f[i], "flags")).empty()) e.flags = v;
    }
    e.inManifest = true;
    auto it = m.index.find(e.hash);
    if (it == m.index.end()) {
        m.index[e.hash] = m.entries.size();
        m.entries.push_back(e);
        return;
    }
    Entry& old = m.entries[it->second];
    if (old.inManifest) return;   // a later session saw it again: the first row stays
    e.sightings = old.sightings;  // a PNG found on disk before its row arrived: take the row's facts
    old = e;
}

size_t parseManifest(Manifest& m, const char* data, size_t len) {
    size_t consumed = 0;
    while (consumed < len) {
        const char* nl = (const char*)std::memchr(data + consumed, '\n', len - consumed);
        if (!nl) break;   // an incomplete last line: leave it for the next call
        parseLine(m, std::string(data + consumed, nl));
        consumed = (size_t)(nl - data) + 1;
    }
    return consumed;
}

// ---- DumpIndex -----------------------------------------------------------------------------------------

void DumpIndex::setDir(const std::string& d) {
    dir_ = d;
    m_ = Manifest();
    offset_ = 0;
    carry_.clear();
    dirExists_ = false;
}

void DumpIndex::scanFolder() {
    std::error_code ec;
    std::vector<std::string> fresh;
    for (fs::directory_iterator it(toPath(dir_), ec), end; !ec && it != end; it.increment(ec)) {
        std::string name = it->path().filename().string();
        std::string h = hashOfDumpFile(name);
        if (h.empty() || m_.index.count(h)) continue;
        fresh.push_back(h);
    }
    std::sort(fresh.begin(), fresh.end());
    fresh.erase(std::unique(fresh.begin(), fresh.end()), fresh.end());
    for (const std::string& h : fresh) {
        Entry e;
        e.hash = h;
        std::string pf = previewFile(dir_, h);
        PngInfo pi = pf.empty() ? PngInfo() : readPngHeader(pf);
        e.w = pi.w;
        e.h = pi.h;
        e.bpp = 0;   // not known without the manifest row
        m_.index[h] = m_.entries.size();
        m_.entries.push_back(e);
    }
}

bool DumpIndex::poll() {
    if (dir_.empty()) return false;
    const size_t before = m_.entries.size();
    const unsigned badBefore = m_.badLines;
    std::error_code ec;
    dirExists_ = fs::is_directory(toPath(dir_), ec);
    if (!dirExists_) return false;
    bool changed = false;

    fs::path mf = toPath(joinPath(dir_, "manifest.tsv"));
    uint64_t size = (uint64_t)fs::file_size(mf, ec);
    const uint64_t offBefore = offset_;
    if (!ec) {
        if (size < offset_) {   // truncated or replaced: start over
            Manifest fresh;
            m_ = fresh;
            offset_ = 0;
            carry_.clear();
            changed = true;
        }
        if (size > offset_) {
            std::ifstream f(mf, std::ios::binary);
            if (f) {
                f.seekg((std::streamoff)offset_);
                std::string buf((size_t)(size - offset_), '\0');
                f.read(&buf[0], (std::streamsize)buf.size());
                buf.resize((size_t)f.gcount());
                offset_ += buf.size();
                carry_ += buf;
                size_t used = parseManifest(m_, carry_.data(), carry_.size());
                carry_.erase(0, used);
            }
        }
    }
    // The folder too, every poll (PNGs are written at once, the manifest is buffered; a dump folder is a
    // few thousand names at most and polls are about once a second).
    scanFolder();
    return changed || offset_ != offBefore || m_.entries.size() != before || m_.badLines != badBefore;
}

std::unordered_set<std::string> listPack(const std::string& packDirPath) {
    std::unordered_set<std::string> out;
    std::error_code ec;
    for (fs::directory_iterator it(toPath(packDirPath), ec), end; !ec && it != end; it.increment(ec)) {
        std::string name = it->path().filename().string();
        if (name.size() == 20 && name.compare(16, 4, ".png") == 0 && isHash16(name.substr(0, 16)))
            out.insert(name.substr(0, 16));
    }
    return out;
}

// ---- PNG ----------------------------------------------------------------------------------------------------

PngInfo parsePngHeader(const uint8_t* b, size_t n) {
    PngInfo p;
    if (n < 33 || !isPngFile(b, n)) return p;
    // chunk 1: length(4) 'IHDR' data(13) crc(4)
    if (std::memcmp(b + 12, "IHDR", 4) != 0) return p;
    auto be32 = [&](size_t o) { return ((unsigned)b[o] << 24) | ((unsigned)b[o + 1] << 16) | ((unsigned)b[o + 2] << 8) | b[o + 3]; };
    if (be32(8) != 13) return p;
    p.w = be32(16);
    p.h = be32(20);
    p.depth = b[24];
    p.colorType = b[25];
    p.interlace = b[28];
    p.ok = p.w > 0 && p.h > 0;
    return p;
}

PngInfo readPngHeader(const std::string& path) {
    std::ifstream f(toPath(path), std::ios::binary);
    uint8_t b[33];
    if (!f) return PngInfo();
    f.read((char*)b, sizeof b);
    return parsePngHeader(b, (size_t)f.gcount());
}

int packScale(unsigned tileW, unsigned tileH, unsigned pngW, unsigned pngH, int maxN) {
    if (tileW == 0 || tileH == 0) return 0;
    if (pngW == tileW && pngH == tileH) return 1;
    for (int n = 2; n <= maxN; n++)
        if (pngW == tileW * (unsigned)n && pngH == tileH * (unsigned)n) return n;
    return 0;
}

PackVerdict checkPack(const PngInfo& png, unsigned tileW, unsigned tileH, unsigned tileBpp) {
    PackVerdict v;
    if (!png.ok) {
        v.message = "Not a PNG file.";
        return v;
    }
    if (png.depth != 8) {
        v.message = "The core reads 8-bit PNGs only (this one is " + std::to_string(png.depth) + "-bit).";
        return v;
    }
    if (png.interlace != 0) {
        v.message = "Interlaced PNGs are not supported; save it non-interlaced.";
        return v;
    }
    if (!(png.colorType == 0 || png.colorType == 2 || png.colorType == 3 || png.colorType == 4 || png.colorType == 6)) {
        v.message = "Unsupported PNG colour type.";
        return v;
    }
    if (tileW == 0 || tileH == 0) {
        // The tile's size is unknown (no manifest row yet): the core re-checks at blit time.
        v.ok = true;
        v.scale = 1;
        v.warn = true;
        v.message = "The tile's size is not known yet, so the size could not be checked.";
        return v;
    }
    v.scale = packScale(tileW, tileH, png.w, png.h);
    if (v.scale == 0) {
        v.message = "Size " + std::to_string(png.w) + "x" + std::to_string(png.h) + " does not fit the " +
                    std::to_string(tileW) + "x" + std::to_string(tileH) + " tile: use exactly that, or exactly " +
                    std::to_string(tileW * 2) + "x" + std::to_string(tileH * 2) + " with Internal Resolution at 2x.";
        return v;
    }
    v.ok = true;
    if (v.scale > 1) {
        v.warn = true;
        v.message = std::to_string(v.scale) + "x art only shows while Internal Resolution is set to " +
                    std::to_string(v.scale) + "x.";
    } else if (tileBpp != 0 && tileBpp != 16) {
        v.warn = true;
        v.message = std::to_string(tileBpp) + "bpp tiles are not presented yet (only 16bpp blitter tiles are); "
                    "the file is accepted for when they are.";
    } else {
        v.message = "ok";
    }
    return v;
}

InstallResult installPack(const std::string& systemDir, uint32_t crc, const std::string& hash,
                          const std::string& srcPng) {
    InstallResult r;
    if (!isHash16(hash)) {
        r.error = "not a tile hash";
        return r;
    }
    std::vector<uint8_t> bytes;
    if (!readWholeFile(srcPng, bytes, 64u << 20)) {
        r.error = "cannot read " + srcPng;
        return r;
    }
    if (!parsePngHeader(bytes.data(), bytes.size()).ok) {
        r.error = "not a PNG file";
        return r;
    }
    std::error_code ec;
    fs::create_directories(toPath(packDir(systemDir, crc)), ec);
    if (ec) {
        r.error = "cannot create " + packDir(systemDir, crc) + ": " + ec.message();
        return r;
    }
    r.dest = packFile(systemDir, crc, hash);
    if (!writeAtomic(r.dest, bytes, r.error)) return r;
    r.ok = true;
    return r;
}

InstallResult exportPreview(const std::string& previewPath, const std::string& destFolder) {
    InstallResult r;
    std::vector<uint8_t> bytes;
    if (previewPath.empty() || !readWholeFile(previewPath, bytes, 64u << 20)) {
        r.error = "no preview file for this tile";
        return r;
    }
    std::error_code ec;
    if (!fs::is_directory(toPath(destFolder), ec)) {
        r.error = "not a folder: " + destFolder;
        return r;
    }
    r.dest = joinPath(destFolder, toPath(previewPath).filename().string());
    if (!writeAtomic(r.dest, bytes, r.error)) return r;
    r.ok = true;
    return r;
}

// ---- actions ----------------------------------------------------------------------------------------------------------------

const char* const kOptDump = "virtualjaguar_texture_dump";
const char* const kOptReplace = "virtualjaguar_texture_replace";

bool optionOn(const OptionIO& io, const char* key) { return io.get && io.get(key) == "enabled"; }

bool setDumping(const OptionIO& io, bool on, int instance) {
    if (on && !dumpingAllowed(instance)) return false;
    if (io.set) io.set(kOptDump, on ? "enabled" : "disabled");
    return true;
}

std::vector<std::pair<std::string, std::string>> sessionOverrides(int instance) {
    std::vector<std::pair<std::string, std::string>> o;
    if (!dumpingAllowed(instance)) o.emplace_back(kOptDump, "disabled");
    return o;
}

ReplaceOutcome replaceTile(const OptionIO& io, const vj_debug_iface* iface, const std::string& systemDir,
                           uint32_t crc, const std::string& hash, unsigned tileW, unsigned tileH, unsigned tileBpp,
                           const std::string& srcPng) {
    ReplaceOutcome o;
    o.verdict = checkPack(readPngHeader(srcPng), tileW, tileH, tileBpp);
    if (!o.verdict.ok) {
        o.message = o.verdict.message;
        return o;
    }
    InstallResult ir = installPack(systemDir, crc, hash, srcPng);
    if (!ir.ok) {
        o.message = ir.error;
        return o;
    }
    o.dest = ir.dest;
    if (iface && iface->size >= offsetof(vj_debug_iface, texreplace_reload) + sizeof iface->texreplace_reload &&
        iface->texreplace_reload) {
        o.reloadActive = iface->texreplace_reload() != 0;
        o.reloadCalled = true;
    }
    if (io.set) io.set(kOptReplace, "enabled");
    o.ok = true;
    o.message = "Installed " + hash + ".png" + (o.verdict.warn ? "  (" + o.verdict.message + ")" : std::string()) +
                (o.reloadCalled ? "" : "  (pack reload unavailable: restart the game)");
    return o;
}

// ---- LruKeys --------------------------------------------------------------------------------------------------------

std::vector<std::string> LruKeys::touch(const std::string& key) {
    std::vector<std::string> evicted;
    auto it = pos_.find(key);
    if (it != pos_.end()) {
        order_.splice(order_.begin(), order_, it->second);
        return evicted;
    }
    order_.push_front(key);
    pos_[key] = order_.begin();
    while (order_.size() > cap_) {
        evicted.push_back(order_.back());
        pos_.erase(order_.back());
        order_.pop_back();
    }
    return evicted;
}

void LruKeys::erase(const std::string& key) {
    auto it = pos_.find(key);
    if (it == pos_.end()) return;
    order_.erase(it->second);
    pos_.erase(it);
}

}  // namespace tex
}  // namespace vj
