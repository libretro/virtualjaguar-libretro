// Game library, pure logic: no ImGui, no GL, no threads, no network.  Everything
// here is unit-tested headlessly (app/tests/library_tests.cpp).
//
//   * Dat           parser for the clrmamepro .dat files libretro-database ships
//                   (No-Intro "Atari - Jaguar", Redump "Atari - Jaguar CD")
//   * scanFolders   walks user folders for cartridge and disc images, hashes them
//                   (cached by path + size + mtime so a rescan only stats files)
//   * identify      CRC32 -> dat game; falls back to the file name, flagged as such
//   * thumbnail*    libretro-thumbnails file naming, URLs and the on-disk cache layout
#pragma once

#include <atomic>
#include <cstdint>
#include <functional>
#include <map>
#include <string>
#include <vector>

namespace vj {
namespace lib {

// ---- dat files ----------------------------------------------------------------------

struct DatRom {
    std::string name;
    uint64_t size = 0;
    uint32_t crc = 0;
};

struct DatGame {
    std::string name;     // the title, also the libretro-thumbnails file name
    std::string region;
    std::string serial;
    std::vector<DatRom> roms;
};

class Dat {
public:
    // Parses clrmamepro text.  Returns false when it holds no games at all.
    bool parse(const std::string& text);
    bool load(const std::string& path);
    bool empty() const { return games_.empty(); }
    size_t size() const { return games_.size(); }
    const std::string& header(const std::string& key) const;   // "name", "version", ...

    // A game with a rom of this CRC32 (the first, in file order), or nullptr.
    const DatGame* byCrc(uint32_t crc) const;
    // A game whose name matches `fileStem` once case, punctuation and trailing
    // "(Track N)" are ignored, or nullptr.
    const DatGame* byName(const std::string& fileStem) const;

private:
    std::vector<DatGame> games_;
    std::multimap<uint32_t, size_t> crc_;
    std::map<std::string, size_t> names_;
    std::map<std::string, std::string> header_;
};

// ---- hashing --------------------------------------------------------------------------

uint32_t crc32Update(uint32_t crc, const void* data, size_t len);   // zlib CRC-32, start with 0
// Length of a copier header the core's DetectPrependedHeaderSize would strip from this
// image (0 or 512).  Reimplemented here (the app only talks to the core through
// retro_*); app/tests/library_tests.cpp pins the rules.
uint32_t cartHeaderSize(const uint8_t* data, size_t size);

// ---- scanning -----------------------------------------------------------------------------

enum class Kind { Cart, Disc };
enum class Match { Unknown, Name, Crc };   // Crc = verified against a dat by checksum

// What we know about a file without any dat: the expensive part, cached on disk.
struct ScanFile {
    std::string path;      // absolute, as scanned
    Kind kind = Kind::Cart;
    uint64_t size = 0;
    int64_t mtime = 0;     // seconds
    uint32_t crcWhole = 0; // cart: whole file.  disc (.cue): first FILE referenced.  0 = none
    uint32_t crcStripped = 0;   // cart with a copier header: CRC of the payload; else 0
    bool hashed = false;
};

// Which part of the library a file belongs to.
enum class Section { Cart = 0, Disc = 1, Bios = 2 };

// ---- BIOS and system files -------------------------------------------------------------------
// The core embeds the Series K and Model M boot ROMs and the retail and developer CD BIOS, so
// none of these is needed; a file is read only in the cases the notes below spell out (all from
// libretro.c: load_external_cd_bios, load_external_cart_boot_rom, stage_cart_boot_rom).
enum class BiosRole { None, CartBoot, CdBios, MemoryTrack, Other };

struct BiosInfo {
    BiosRole role = BiosRole::None;
    bool recognized = false;   // the checksum is one of the core's known images
    std::string title;         // "Series K boot ROM", or the file's own name
};

// Classify a cartridge-type file as BIOS / system data, by CRC32 of the whole file first and
// then by file name (a "[BIOS]" prefix, the names the core searches for, "bios" or "memory
// track" in the name).  Names only count for small files (<= 512 KiB): no BIOS is larger.
BiosInfo classifyBios(const std::string& path, uint64_t size, uint32_t crcWhole);
const char* biosRoleLabel(BiosRole r);   // "Cart boot ROM", "CD BIOS", ...
// Tests only: teach classifyBios one more checksum (the real BIOS images cannot be committed, so a
// fixture's checksum is registered instead).
void addKnownBiosForTest(uint32_t crc, BiosRole role, const char* title);
// Will the core read this file where it is, and when?  Decided from what libretro.c does (file
// NAME, FOLDER and SIZE; the checksum only names the image): the cart boot ROM search
// (load_external_cart_boot_rom: jagboot.rom, boot.rom, boot0.rom and the "[BIOS] Atari Jaguar..." names, in the
// system folder or its "Atari - Jaguar" / "jaguar" sub-folders, exactly 128 KiB, and only with
// Cart BIOS Type = Custom), jagboot_m.rom (system folder itself only, 128 KiB, Cart BIOS Type = Model M),
// and the CD BIOS search (load_external_cd_bios: its own names and sub-folders, exactly 256 KiB, preferred over
// the built-in image whenever a real CD BIOS boots).  Series K is always the built-in image.
struct BiosUse {
    bool core = false;     // the core reads this file from where it is
    std::string text;      // when / why not, a sentence or two for the tooltip
};
BiosUse biosUse(const BiosInfo& b, const std::string& path, uint64_t size, const std::string& systemDir);
// The core reads BIOS files only from its system folder, under fixed names.  A BIOS found in a library
// folder can be copied there: this is the file name (relative to the system folder) the copy must have for the
// core to pick it up, or "" when no copy would help (the Series K image, which is built in and never read; the
// Memory Track and other data files; a size the core rejects).  Retail and developer CD BIOS, and the Model M
// boot ROM, get the names that select them; any other boot ROM becomes jagboot.rom (Cart BIOS Type = Custom).
std::string biosInstallName(const BiosInfo& b, uint64_t size);

// A scanned file plus what the dats say about it.
struct Entry {
    ScanFile file;
    Section section = Section::Cart;
    BiosInfo bios;         // when section == Bios
    std::string title;     // dat game name, or the cleaned file stem
    std::string datName;   // dat game name when matched (also the thumbnail key), else ""
    std::string region;
    Match match = Match::Unknown;
};

struct ScanOptions {
    std::vector<std::string> folders;
    // The core's system folder: only BIOS / system files found there are kept (anything else is
    // not part of the library), so the BIOS section shows what the core can actually read.
    std::string systemFolder;
    int maxDepth = 8;
};

typedef std::map<std::string, ScanFile> ScanCache;   // by path

// Walks the folders.  Files whose size and mtime match `cache` are not re-read.
// `progress(done, total)` is called while hashing; `cancel` aborts early.
std::vector<ScanFile> scanFolders(const ScanOptions& opt, const ScanCache& cache,
                                  const std::atomic<bool>* cancel,
                                  const std::function<void(size_t, size_t)>& progress);

bool isCartExt(const std::string& lowerExt);
bool isDiscExt(const std::string& lowerExt);

// Identification against the dats (either may be null/empty).
Entry identify(const ScanFile& f, const Dat* carts, const Dat* discs);

// "Iron_Soldier.v1.04.j64" is not a title, but "Iron Soldier (World).j64" nearly is.
std::string titleFromFileStem(const std::string& stem);

// ---- tile zoom ----------------------------------------------------------------------------
// The library grid's tile size is one multiplier (the toolbar slider, the config key library.size, pinch,
// Ctrl/Cmd+wheel and Ctrl/Cmd +/-/0 all drive it).  One clamp and one step, so they cannot disagree.
const float kTileScaleMin = 0.5f;
const float kTileScaleMax = 2.5f;
const float kTileScaleDefault = 1.0f;
const float kTileScaleStep = 1.1f;   // one wheel notch / key press multiplies the size by this
float clampTileScale(float s);                           // into [min, max]; NaN / infinite -> the default
float zoomTileScale(float cur, float steps);             // cur * 1.1^steps, clamped; steps may be fractional (trackpad)
float pinchTileScale(float cur, float factor);           // factor = SDL's per-update pinch scale (>1 zooms in); junk is ignored

// ---- scan cache on disk ---------------------------------------------------------------------

std::string serializeCache(const std::vector<ScanFile>& files);
ScanCache parseCache(const std::string& text);

// ---- libretro-thumbnails ------------------------------------------------------------------------

// libretro replaces & * / : ` " < > ? \ | with '_' in thumbnail file names.
std::string sanitizeThumbName(const std::string& title);
std::string urlEncode(const std::string& s);   // RFC 3986 unreserved kept, the rest %XX

extern const char* const kThumbBase;           // "https://thumbnails.libretro.com/"
extern const char* const kThumbSystem;         // "Atari - Jaguar"
extern const char* const kThumbTypes[3];       // Named_Boxarts, Named_Titles, Named_Snaps
std::string thumbnailUrl(const std::string& type, const std::string& title, const std::string& base = kThumbBase);
// <root>thumbnails/<system>/<type>/<sanitized title>.png ; root ends with a separator.
std::string thumbnailCachePath(const std::string& root, const std::string& type, const std::string& title);
// Marker next to it recording "the server said 404" (retried after kMissRetrySeconds).
std::string thumbnailMissPath(const std::string& root, const std::string& type, const std::string& title);
constexpr int64_t kMissRetrySeconds = 30LL * 24 * 3600;

}  // namespace lib
}  // namespace vj
