// Texture browser logic (pure: files and bytes, no SDL / ImGui / GL) -- the part of the Textures
// window that unit-tests on a synthetic directory.  Formats are the core's (docs/texture-dump.md):
//
//   dumps   <system>/vj_texdump/<CRC32 8 hex>/manifest.tsv  +  <hash16>.png
//                                                               (16bpp 'both' mode: <hash16>-cry.png / -rgb.png)
//   packs   <system>/vj_texpacks/<CRC32 8 hex>/<hash16>.png
//
// manifest.tsv is append-only and tab-separated:
//   # texdump v1<TAB>crc=<8 hex><TAB>title=<name>
//   <hash16><TAB>WxH<TAB>bpp<TAB>frame=<n><TAB>src=<6 hex><TAB>clut=<8 hex|-><TAB>flags=<bcompen,dcompen|->
//   <hash16><TAB>clut=<8 hex><TAB>frame=<n>                  (an extra palette sighting of a known tile)
// The core writes it through a buffered file, so the reader only consumes COMPLETE lines and
// picks up the rest next time.
#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <list>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "vj_debug_iface.h"

namespace vj {
namespace tex {

// ---- names and paths ---------------------------------------------------------------------------
std::string crcHex(uint32_t crc);                          // "0e4ae2eb"
bool isHash16(const std::string& s);                       // exactly 16 lower-case hex digits
std::string joinPath(const std::string& a, const std::string& b);   // one '/' between, whatever a ends with
std::string dumpDir(const std::string& systemDir, uint32_t crc);    // <system>/vj_texdump/<crc>
std::string packDir(const std::string& systemDir, uint32_t crc);    // <system>/vj_texpacks/<crc>
std::string packFile(const std::string& systemDir, uint32_t crc, const std::string& hash);
// The preview PNG a tile dumped as: <hash>.png, else <hash>-cry.png, else <hash>-rgb.png; "" if none.
std::string previewFile(const std::string& dumpDirPath, const std::string& hash);
// A file:// URL for a local path (percent-encoded; drive letters and backslashes handled), for SDL_OpenURL.
std::string fileUrl(const std::string& path);
// "5f3a...png" / "5f3a...-cry.png" / "-rgb.png" -> the hash; "" for anything else.
std::string hashOfDumpFile(const std::string& fileName);

// ---- the manifest ------------------------------------------------------------------------------
struct Entry {
    std::string hash;      // 16 hex
    unsigned w = 0, h = 0; // tile size in pixels
    unsigned bpp = 0;      // 1/2/4/8/16/32
    unsigned frame = 0;    // frame the dump session first saw it
    unsigned src = 0;      // bus address of the window
    std::string clut;      // 8 hex, or "" (16bpp+, or unknown)
    std::string flags;     // "bcompen,dcompen" / "-" / ""
    unsigned sightings = 0;     // extra palette rows
    bool inManifest = false;    // false: found only as a PNG on disk (size from its header)
};

struct Manifest {
    bool haveHeader = false;
    unsigned version = 0;
    std::string crc, title;
    std::vector<Entry> entries;                       // first-seen order
    std::unordered_map<std::string, size_t> index;    // hash -> entries[]
    unsigned badLines = 0;
    const Entry* find(const std::string& hash) const;
};

// Appends the complete lines in data[0..len) to m.  Returns how many bytes were consumed (up to the
// last '\n'); the caller keeps the rest for the next call.  The first first-sight row of a hash wins
// (the file is append-only across sessions and the dedupe set is per session).
size_t parseManifest(Manifest& m, const char* data, size_t len);
inline size_t parseManifest(Manifest& m, const std::string& s) { return parseManifest(m, s.data(), s.size()); }

// Incremental reader over manifest.tsv plus a scan of the dump folder: poll() is cheap when nothing
// changed (stat + size compare) and returns true when the entry list did.
class DumpIndex {
public:
    void setDir(const std::string& dumpDirPath);   // switches game: clears everything
    const std::string& dir() const { return dir_; }
    bool poll();                                   // re-read the new manifest bytes and (slow path) the folder
    const Manifest& manifest() const { return m_; }
    size_t size() const { return m_.entries.size(); }
    bool dirExists() const { return dirExists_; }
private:
    void scanFolder();
    std::string dir_;
    Manifest m_;
    uint64_t offset_ = 0;
    std::string carry_;          // an incomplete last line
    bool dirExists_ = false;
};

// Names (hashes) of the pack files present in a pack folder: only exact "<16 hex>.png" count, as in the core.
std::unordered_set<std::string> listPack(const std::string& packDirPath);

// ---- PNG headers / pack-art rules -----------------------------------------------------------------
struct PngInfo {
    bool ok = false;               // a PNG signature + IHDR
    unsigned w = 0, h = 0;
    int depth = 0, colorType = 0, interlace = 0;
};
PngInfo parsePngHeader(const uint8_t* bytes, size_t n);
PngInfo readPngHeader(const std::string& path);    // first 33 bytes of a file

constexpr int kMaxPackScale = 2;   // SHADOWFB_HIRES_MAX_N: the core's internal resolution tops out at 2x

// 1 when pngW/H are exactly the tile size, N (2..maxN) when exactly N times it in both axes, else 0.
int packScale(unsigned tileW, unsigned tileH, unsigned pngW, unsigned pngH, int maxN = kMaxPackScale);

struct PackVerdict {
    bool ok = false;        // the core will load it (and present it when the tile size rule holds)
    int scale = 0;          // 1 or N; 0 = size mismatch
    bool warn = false;      // installable, but with a caveat
    std::string message;    // why not / the caveat / "ok" text
};
// The rules texreplace.c enforces: 8-bit, non-interlaced, colour type 0/2/3/4/6, and the tile size
// (1x, or exactly Nx with Internal Resolution at Nx).  bpp is the tile's (only 16bpp tiles present).
PackVerdict checkPack(const PngInfo& png, unsigned tileW, unsigned tileH, unsigned tileBpp);

// Copies a validated PNG into <system>/vj_texpacks/<crc>/<hash16>.png (the folder is created; written to a
// temporary name first, then renamed over the target).
struct InstallResult {
    bool ok = false;
    std::string dest, error;
};
InstallResult installPack(const std::string& systemDir, uint32_t crc, const std::string& hash,
                          const std::string& srcPng);
// Copies one preview PNG into a folder the user chose, keeping the file's name.
InstallResult exportPreview(const std::string& previewPath, const std::string& destFolder);

// ---- the actions the window offers ---------------------------------------------------------------------------
// Core option keys (libretro_core_options.h; values "disabled" / "enabled").
extern const char* const kOptDump;      // virtualjaguar_texture_dump
extern const char* const kOptReplace;   // virtualjaguar_texture_replace

struct OptionIO {
    std::function<std::string(const std::string& key)> get;                       // current value
    std::function<void(const std::string& key, const std::string& value)> set;    // CoreHost::setOptionValue
};
bool optionOn(const OptionIO& io, const char* key);

// The dump manifest is shared and append-only, so a second copy of the app (paths.instance > 1)
// running the same game must not dump into the same folder.
inline bool dumpingAllowed(int instance) { return instance <= 1; }
// Returns false (and changes nothing) when dumping is not allowed for this instance.
bool setDumping(const OptionIO& io, bool on, int instance);

struct ReplaceOutcome {
    bool ok = false;
    PackVerdict verdict;
    std::string dest;
    std::string message;       // for the status line
    bool reloadCalled = false;
    bool reloadActive = false; // what texreplace_reload() answered (0 until the option has taken effect)
};
// Replace one tile's art: validate srcPng against the tile (`tileW/H/bpp`), copy it to
// <system>/vj_texpacks/<crc>/<hash>.png, ask the core to re-read the packs, and make sure the
// replacement option is on.  The option is set AFTER the reload: a reload tears the pack state down
// and the core re-latches the option on its next frame.
ReplaceOutcome replaceTile(const OptionIO& io, const vj_debug_iface* iface, const std::string& systemDir,
                           uint32_t crc, const std::string& hash, unsigned tileW, unsigned tileH, unsigned tileBpp,
                           const std::string& srcPng);

// ---- a bounded set of cached thumbnails --------------------------------------------------------------
// The window owns the textures; this only decides who gets evicted.  touch() marks a key most recently
// used and returns the keys that fell off the end (their textures are to be destroyed).
class LruKeys {
public:
    explicit LruKeys(size_t capacity) : cap_(capacity ? capacity : 1) {}
    std::vector<std::string> touch(const std::string& key);
    void erase(const std::string& key);
    void clear() { order_.clear(); pos_.clear(); }
    bool contains(const std::string& key) const { return pos_.count(key) != 0; }
    size_t size() const { return order_.size(); }
    size_t capacity() const { return cap_; }
private:
    size_t cap_;
    std::list<std::string> order_;   // front = most recent
    std::unordered_map<std::string, std::list<std::string>::iterator> pos_;
};

}  // namespace tex
}  // namespace vj
