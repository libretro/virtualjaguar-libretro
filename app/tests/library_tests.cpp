// Headless tests for the game library (#838).  No window, no GL, no real network:
// dat parsing, CRC identification, libretro-thumbnails naming, the cache layout, a scan of a
// temp folder, and (POSIX) the whole art pipeline against a loopback HTTP server.
//
//   vj_library_tests                       run every test
//   vj_library_tests --scan DIR [--db DIR]  dev tool: scan DIR, identify against DIR/*.dat, print COUNTS only
#include <SDL3/SDL.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>
#include <mutex>
#include <set>
#include <thread>

#include "config.h"
#include "disc_hash.h"
#include "http.h"
#include "library.h"
#include "library_service.h"
#include "m3u.h"
#include "paths.h"

#ifndef _WIN32
#include <arpa/inet.h>
#include <csignal>
#include <netinet/in.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

namespace fs = std::filesystem;
using namespace vj;

static int g_fail = 0, g_checks = 0;
#define CHECK(cond)                                                                     \
    do {                                                                                \
        g_checks++;                                                                     \
        if (!(cond)) {                                                                  \
            g_fail++;                                                                   \
            std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);        \
        }                                                                               \
    } while (0)
#define CHECK_EQ(a, b)                                                                  \
    do {                                                                                \
        g_checks++;                                                                     \
        std::string _a = (a), _b = (b);                                                 \
        if (_a != _b) {                                                                 \
            g_fail++;                                                                   \
            std::fprintf(stderr, "FAIL %s:%d: %s\n   got:  %s\n   want: %s\n", __FILE__, __LINE__, #a, _a.c_str(), _b.c_str()); \
        }                                                                               \
    } while (0)

static std::string hex8(uint32_t v) {
    char b[16];
    std::snprintf(b, sizeof b, "%08X", v);
    return b;
}

// ---- fixtures ---------------------------------------------------------------------------------------

static std::string g_tmp;   // our own temp root, removed at exit

static std::string mkTmp() {
    std::error_code ec;
    fs::path p = fs::temp_directory_path(ec);
    if (ec) p = ".";
    p /= "vj-library-test-" + std::to_string((unsigned long long)SDL_GetPerformanceCounter());
    fs::create_directories(p, ec);
    return p.string() + "/";
}

static void writeBytes(const std::string& path, const std::vector<unsigned char>& b) {
    std::error_code ec;
    fs::create_directories(fs::path(path).parent_path(), ec);
    std::ofstream f(path, std::ios::binary);
    f.write((const char*)b.data(), (std::streamsize)b.size());
}
static void writeText(const std::string& path, const std::string& s) {
    writeBytes(path, std::vector<unsigned char>(s.begin(), s.end()));
}

// Deterministic pseudo-random bytes.
static std::vector<unsigned char> noise(size_t n, uint32_t seed) {
    std::vector<unsigned char> v(n);
    uint32_t x = seed * 2654435761u + 12345u;
    for (size_t i = 0; i < n; i++) {
        x = x * 1664525u + 1013904223u;
        v[i] = (unsigned char)(x >> 24);
    }
    return v;
}

static void putBE32(std::vector<unsigned char>& v, size_t off, uint32_t x) {
    v[off] = (unsigned char)(x >> 24);
    v[off + 1] = (unsigned char)(x >> 16);
    v[off + 2] = (unsigned char)(x >> 8);
    v[off + 3] = (unsigned char)x;
}

static uint32_t crcOf(const std::vector<unsigned char>& v, size_t from = 0) {
    return lib::crc32Update(0, v.data() + from, v.size() - from);
}

// ---- dat text -----------------------------------------------------------------------------------------

static std::string datText(const std::string& version, const std::string& games) {
    return "clrmamepro (\n\tname \"Test\"\n\tversion \"" + version + "\"\n)\n\n" + games;
}
static std::string datGame(const std::string& name, const std::string& romName, uint32_t crc, const std::string& extra = "") {
    return "game (\n\tname \"" + name + "\"\n" + extra + "\trom ( name \"" + romName + "\" size 1048576 crc " + hex8(crc) +
           " md5 00 sha1 00 )\n)\n";
}

// ---- tests ---------------------------------------------------------------------------------------------------

static void testCrc() {
    CHECK_EQ(hex8(lib::crc32Update(0, "123456789", 9)), "CBF43926");
    uint32_t a = lib::crc32Update(0, "1234", 4);
    CHECK_EQ(hex8(lib::crc32Update(a, "56789", 5)), "CBF43926");   // incremental
}

static void testDatParsing() {
    // Rows in the shape of the real libretro-database files, including a quote-heavy name and a serial.
    std::string text =
        "clrmamepro (\n\tname \"Atari - Jaguar\"\n\tversion \"2026.10.07\"\n\textensions \"abs|cof|j64|jag|rom\"\n)\n\n"
        "game (\n\tname \"1943 (World)\"\n\trom ( name \"1943 (World) (Aftermarket) (Unl).j64\" size 4194304 crc ED88E869 md5 658E sha1 710D )\n)\n"
        "game (\n\tname \"1943 (World)\"\n\trom ( name \"1943 (World) (Aftermarket) (Unl).rom\" size 4186112 crc 95FD1E88 md5 CB10 sha1 F263 )\n)\n"
        "game (\n\tname \"Alice's Mom's Rescue! (Europe) (En,Fr,De,Es,It)\"\n\tregion \"Europe\"\n"
        "\trom ( name \"Alice's Mom's Rescue! (Europe) (En,Fr,De,Es,It) (Unl) (Track 1).bin\" size 29995056 crc 63E41DF2 md5 1E06 sha1 B6F5 )\n)\n"
        "game (\n\tname \"Baldies (USA) (Rev 1)\"\n\tregion \"USA\"\n\tserial \"J9055E\"\n"
        "\trom ( name \"Baldies (USA) (Rev 1) (Track 01).bin\" size 11207280 crc 2B34A071 md5 FB17 sha1 E414 serial \"J9055E\" )\n)\n";
    lib::Dat d;
    CHECK(d.parse(text));
    CHECK(d.size() == 4);
    CHECK_EQ(d.header("version"), "2026.10.07");
    CHECK_EQ(d.header("name"), "Atari - Jaguar");

    const lib::DatGame* g = d.byCrc(0x95FD1E88);   // the second rom of the first game name
    CHECK(g && g->name == "1943 (World)");
    g = d.byCrc(0x63E41DF2);
    CHECK(g && g->name == "Alice's Mom's Rescue! (Europe) (En,Fr,De,Es,It)" && g->region == "Europe");
    g = d.byCrc(0x2B34A071);
    CHECK(g && g->serial == "J9055E");
    CHECK(d.byCrc(0xDEADBEEF) == nullptr);
    CHECK(d.byCrc(0) == nullptr);

    // file-name matching: case, punctuation and a trailing "(Track N)" do not matter
    CHECK(d.byName("baldies (usa) (rev 1)") != nullptr);
    CHECK(d.byName("Baldies (USA) (Rev 1) (Track 01)") != nullptr);
    CHECK(d.byName("Baldies_USA_Rev_1") != nullptr);
    CHECK(d.byName("Baldies (USA)") == nullptr);   // a different pressing is not a match
    CHECK(d.byName("") == nullptr);

    lib::Dat bad;
    CHECK(!bad.parse("this is not a dat"));
    CHECK(!bad.parse("game ( name \"x\" "));   // unterminated
    lib::Dat empty;
    CHECK(!empty.parse(""));
}

static void testHeaderDetection() {
    const size_t MiB = 1048576;
    std::vector<unsigned char> v;

    v.assign(MiB + 512, 0);   // copier dump: N MiB + 512, marker at file $600
    putBE32(v, 512 + 0x400, 0x04040404);
    CHECK(lib::cartHeaderSize(v.data(), v.size()) == 512);

    v.assign(MiB, 0);          // exact megabyte: never stripped, even with the marker at $600
    putBE32(v, 512 + 0x400, 0x04040404);
    CHECK(lib::cartHeaderSize(v.data(), v.size()) == 0);

    v.assign(512 + 0x2000, 0);   // sub-1MiB homebrew with a copier header
    putBE32(v, 512 + 0x400, 0x04040404);
    CHECK(lib::cartHeaderSize(v.data(), v.size()) == 512);

    v.assign(512 + 0x2000, 0);   // sub-1MiB native cart: marker already at $400, left alone
    putBE32(v, 0x400, 0x04040404);
    putBE32(v, 512 + 0x400, 0x04040404);
    CHECK(lib::cartHeaderSize(v.data(), v.size()) == 0);

    v.assign(0x300, 0);          // too small to say
    CHECK(lib::cartHeaderSize(v.data(), v.size()) == 0);
}

static void testThumbnailNames() {
    // libretro replaces & * / : ` " < > ? \ | with '_'
    CHECK_EQ(lib::sanitizeThumbName("Ecco: The Tides of Time"), "Ecco_ The Tides of Time");
    CHECK_EQ(lib::sanitizeThumbName("Demolition Man & Commander Blood - Lost Video Footage for Jaguar 64bit (USA)"),
             "Demolition Man _ Commander Blood - Lost Video Footage for Jaguar 64bit (USA)");
    CHECK_EQ(lib::sanitizeThumbName("a&b*c/d:e`f\"g<h>i?j\\k|l"), "a_b_c_d_e_f_g_h_i_j_k_l");
    // names that exist upstream (Named_Boxarts/) are left alone, apostrophe and parentheses included
    CHECK_EQ(lib::sanitizeThumbName("Iron Soldier (World)"), "Iron Soldier (World)");
    CHECK_EQ(lib::sanitizeThumbName("Arena Football '95 (World)"), "Arena Football '95 (World)");
    CHECK_EQ(lib::sanitizeThumbName("Breakout 2000 (USA, Europe)"), "Breakout 2000 (USA, Europe)");

    CHECK_EQ(lib::urlEncode("Atari - Jaguar"), "Atari%20-%20Jaguar");
    CHECK_EQ(lib::thumbnailUrl("Named_Boxarts", "Iron Soldier (World)"),
             "https://thumbnails.libretro.com/Atari%20-%20Jaguar/Named_Boxarts/Iron%20Soldier%20%28World%29.png");
    CHECK_EQ(lib::thumbnailUrl("Named_Snaps", "Arena Football '95 (World)"),
             "https://thumbnails.libretro.com/Atari%20-%20Jaguar/Named_Snaps/Arena%20Football%20%2795%20%28World%29.png");
    CHECK_EQ(lib::thumbnailUrl("Named_Boxarts", "A: B", "http://127.0.0.1:1/"),
             "http://127.0.0.1:1/Atari%20-%20Jaguar/Named_Boxarts/A_%20B.png");

    // cache layout: <root>thumbnails/<system>/<type>/<sanitized title>.png, miss marker beside it
    CHECK_EQ(lib::thumbnailCachePath("/data/", "Named_Boxarts", "Ecco: The Tides"),
             "/data/thumbnails/Atari - Jaguar/Named_Boxarts/Ecco_ The Tides.png");
    CHECK_EQ(lib::thumbnailMissPath("/data/", "Named_Titles", "Iron Soldier (World)"),
             "/data/thumbnails/Atari - Jaguar/Named_Titles/Iron Soldier (World).png.miss");
    CHECK(std::strcmp(lib::kThumbTypes[0], "Named_Boxarts") == 0);   // boxart is tried first

    // the site mirror serves the same layout under "Atari - Jaguar CD"
    CHECK_EQ(lib::thumbnailUrl("Named_Boxarts", "Myst (USA)", lib::kMirrorBase, lib::kThumbSystemCd),
             "https://jaguar.provenance-emu.com/thumbnails/Atari%20-%20Jaguar%20CD/Named_Boxarts/Myst%20%28USA%29.png");
    CHECK_EQ(lib::thumbnailCachePath("/data/", "Named_Titles", "Myst (USA)", lib::kThumbSystemCd),
             "/data/thumbnails/Atari - Jaguar CD/Named_Titles/Myst (USA).png");   // the mirror has its own cache folder
    CHECK_EQ(lib::thumbnailMissPath("/data/", "Named_Titles", "Myst (USA)", lib::kThumbSystemCd),
             "/data/thumbnails/Atari - Jaguar CD/Named_Titles/Myst (USA).png.miss");

    // sources: libretro-thumbnails always first; the mirror only for discs and only when set
    std::vector<lib::ArtSource> s = lib::artSources(lib::Section::Cart, "http://up/", lib::kMirrorBase);
    CHECK(s.size() == 1 && s[0].base == "http://up/" && std::strcmp(s[0].system, "Atari - Jaguar") == 0);
    CHECK(s[0].missRetrySeconds == lib::kMissRetrySeconds);
    s = lib::artSources(lib::Section::Disc, "http://up/", "http://mirror/thumbs");   // trailing slash added
    CHECK(s.size() == 2);
    if (s.size() == 2) {
        CHECK(s[0].base == "http://up/" && std::strcmp(s[0].system, "Atari - Jaguar") == 0);
        CHECK(s[1].base == "http://mirror/thumbs/" && std::strcmp(s[1].system, "Atari - Jaguar CD") == 0);
        CHECK(std::string(s[0].system) != std::string(s[1].system));   // cache and misses are kept per source
        CHECK(s[1].sessionMisses && !s[0].sessionMisses);   // the mirror's misses last one session (#997)
    }
    CHECK(lib::artSources(lib::Section::Disc, "http://up/", "").size() == 1);   // mirror off
    CHECK(lib::artSources(lib::Section::Bios, "http://up/", lib::kMirrorBase).size() == 1);
}

static void testBiosClassification() {
    // ---- by checksum: the images the core itself knows (biosdb.c, CD_BIOS_CRC_*, Memory Track) ----
    struct { uint32_t crc; lib::BiosRole role; const char* title; } known[] = {
        {0xFB731AAAu, lib::BiosRole::CartBoot, "Series K boot ROM"},
        {0xAE25BDF5u, lib::BiosRole::CartBoot, "Model M boot ROM"},
        {0xE60277BBu, lib::BiosRole::CartBoot, "Stubulator '93 dev boot ROM"},
        {0x8D15DBC6u, lib::BiosRole::CartBoot, "Stubulator '94 dev boot ROM"},
        {0x687068D5u, lib::BiosRole::CdBios, "Jaguar CD BIOS (retail)"},
        {0x55A0669Cu, lib::BiosRole::CdBios, "Jaguar CD BIOS (developer)"},
        {0xFDF37F47u, lib::BiosRole::MemoryTrack, "Memory Track cartridge"},
    };
    for (const auto& k : known) {
        // the file name must not matter: a recognized image is recognized under any name, at any size
        lib::BiosInfo b = lib::classifyBios("/x/anything_at_all.j64", 131072, k.crc);
        CHECK(b.role == k.role && b.recognized && b.title == k.title);
    }
    CHECK(lib::classifyBios("/x/renamed.j64", 1 << 20, 0xFB731AAAu).recognized);   // size cap is for names only

    // ---- by name: unknown checksum, BIOS-ish name ----
    CHECK(lib::classifyBios("/s/jagboot.rom", 131072, 0x12345678).role == lib::BiosRole::CartBoot);
    CHECK(lib::classifyBios("/s/jagboot_m.rom", 131072, 0x12345678).role == lib::BiosRole::CartBoot);
    CHECK(lib::classifyBios("/s/boot0.rom", 131072, 1).role == lib::BiosRole::CartBoot);
    CHECK(lib::classifyBios("/s/jagcd_bios.bin", 262144, 1).role == lib::BiosRole::CdBios);
    CHECK(lib::classifyBios("/s/Jaguar CD BIOS.rom", 262144, 1).role == lib::BiosRole::CdBios);
    lib::BiosInfo tagged = lib::classifyBios("/s/[BIOS] Atari Jaguar CD (World).j64", 262144, 1);
    CHECK(tagged.role == lib::BiosRole::CdBios && !tagged.recognized && tagged.title == "Atari Jaguar CD (World)");
    CHECK(lib::classifyBios("/s/[BIOS] Atari Jaguar Stubulator '94 (World).j64", 131072, 1).role == lib::BiosRole::CartBoot);
    CHECK(lib::classifyBios("/s/[BIOS] Atari Jaguar (World).j64", 131072, 1).role == lib::BiosRole::CartBoot);
    CHECK(lib::classifyBios("/s/My Jaguar Boot BIOS v2.bin", 131072, 1).role == lib::BiosRole::CartBoot);   // unknown but BIOS-named
    CHECK(lib::classifyBios("/s/something bios.bin", 100000, 1).role == lib::BiosRole::Other);
    CHECK(lib::classifyBios("/s/Memory Track (World).j64", 131072, 1).role == lib::BiosRole::MemoryTrack);
    CHECK(lib::classifyBios("/s/memtrack.bin", 131072, 1).role == lib::BiosRole::MemoryTrack);
    // games are not BIOS, whatever the extension; and a BIOS-looking name on a game-sized file is a game
    CHECK(lib::classifyBios("/g/Iron Soldier (World).j64", 2 << 20, 0x99999999).role == lib::BiosRole::None);
    CHECK(lib::classifyBios("/g/Bios Fear.j64", 2 << 20, 0x99999999).role == lib::BiosRole::None);
    CHECK(lib::classifyBios("/g/boot.rom", 4 << 20, 0x99999999).role == lib::BiosRole::None);
    CHECK(lib::classifyBios("/g/Tempest 2000 (World).j64", 2 << 20, 1).role == lib::BiosRole::None);
    CHECK(lib::classifyBios("/g/Rebooted.j64", 100000, 1).role == lib::BiosRole::None);

    // ---- will the core read it from where it is?  (name + folder + size, from libretro.c; never the CRC) ----
    const uint64_t K128 = 0x20000, K256 = 0x40000;
    lib::BiosInfo cart = lib::classifyBios("/sys/jagboot.rom", K128, 1);
    CHECK(lib::biosUse(cart, "/sys/jagboot.rom", K128, "/sys/").core);
    CHECK(lib::biosUse(cart, "/sys/jaguar/jagboot.rom", K128, "/sys").core);
    CHECK(lib::biosUse(cart, "C:\\sys\\Atari - Jaguar\\jagboot.rom", K128, "C:\\sys\\").core);
    CHECK(lib::biosUse(cart, "/sys/jagboot.rom", K128, "/sys").text.find("Custom") != std::string::npos);
    CHECK(!lib::biosUse(cart, "/sys/jaguarcd/jagboot.rom", K128, "/sys").core);      // not a cart-boot sub-folder
    CHECK(!lib::biosUse(cart, "/downloads/jagboot.rom", K128, "/sys").core);         // wrong folder
    CHECK(!lib::biosUse(cart, "/sys/deep/er/jagboot.rom", K128, "/sys").core);       // the core does not recurse
    CHECK(!lib::biosUse(cart, "/sys/jagboot.rom", K128, "").core);                   // no system folder known
    CHECK(!lib::biosUse(cart, "/sys/jagboot.rom", 0x30000, "/sys").core);            // right place, wrong size: skipped
    CHECK(lib::biosUse(cart, "/sys/jagboot.rom", 0x30000, "/sys").text.find("131072") != std::string::npos);
    // jagboot_m.rom: the system folder itself only, 128 KiB, and not by the Custom search
    lib::BiosInfo mm = lib::classifyBios("/sys/jagboot_m.rom", K128, 1);
    CHECK(lib::biosUse(mm, "/sys/jagboot_m.rom", K128, "/sys").core);
    CHECK(lib::biosUse(mm, "/sys/jagboot_m.rom", K128, "/sys").text.find("Model M") != std::string::npos);
    CHECK(!lib::biosUse(mm, "/sys/jaguar/jagboot_m.rom", K128, "/sys").core);
    CHECK(!lib::biosUse(mm, "/sys/Atari - Jaguar/jagboot_m.rom", K128, "/sys").core);
    CHECK(!lib::biosUse(mm, "/sys/jagboot_m.rom", K256, "/sys").core);
    // a Series K dump under a name the core does not search for is not read; named jagboot.rom it is only the Custom path
    lib::BiosInfo k = lib::classifyBios("/sys/series_k.bin", K128, 0xFB731AAAu);
    CHECK(k.recognized && !lib::biosUse(k, "/sys/series_k.bin", K128, "/sys").core);
    lib::BiosInfo kj = lib::classifyBios("/sys/jagboot.rom", K128, 0xFB731AAAu);
    lib::BiosUse kju = lib::biosUse(kj, "/sys/jagboot.rom", K128, "/sys");
    CHECK(kju.core && kju.text.find("Custom") != std::string::npos && kju.text.find("built-in Series K") != std::string::npos);
    // CD BIOS: its own sub-folders, exactly 256 KiB
    lib::BiosInfo cdb = lib::classifyBios("/sys/jaguarcd/jagcd.bin", K256, 0x687068D5u);
    CHECK(lib::biosUse(cdb, "/sys/jaguarcd/jagcd.bin", K256, "/sys").core);
    CHECK(lib::biosUse(cdb, "/sys/Atari - Jaguar CD/jagcd.bin", K256, "/sys").core);
    CHECK(lib::biosUse(cdb, "/sys/jagcd.bin", K256, "/sys").text.find("CD Boot Mode") != std::string::npos);
    CHECK(!lib::biosUse(cdb, "/sys/jagcd.bin", 0x30000, "/sys").core);               // 192 KiB: rejected by the core
    CHECK(lib::biosUse(cdb, "/sys/jagcd.bin", 0x30000, "/sys").text.find("262144") != std::string::npos);
    CHECK(!lib::biosUse(cdb, "/sys/jagcd2.bin", K256, "/sys").core);                 // the core never opens that name
    CHECK(!lib::biosUse(cdb, "/downloads/jagcd.bin", K256, "/sys").core);
    lib::BiosInfo mt = lib::classifyBios("/sys/memtrack.bin", K128, 1);
    CHECK(!lib::biosUse(mt, "/sys/memtrack.bin", K128, "/sys").core);

    // ---- scanning and identify: sections ----
    const size_t K = 1024;
    std::string dir = g_tmp + "bios/lib/", sys = g_tmp + "bios/system/";
    std::vector<unsigned char> mt1 = noise(1024 * 1024, 11);
    writeBytes(dir + "Game One (World).j64", mt1);
    writeBytes(dir + "bioses/jagboot.rom", noise(128 * K, 12));
    writeBytes(dir + "bioses/[BIOS] Atari Jaguar CD (World).j64", noise(256 * K, 13));
    writeBytes(dir + "bioses/Memory Track.j64", noise(128 * K, 14));
    std::vector<unsigned char> weird = noise(128 * K, 15);
    writeBytes(dir + "bioses/blob.bin", weird);                         // only the dat says what this is
    writeBytes(dir + "Disc (USA) (Track 2).bin", noise(2352 * 30, 16)); // a loose Redump track, no cue sheet
    writeBytes(dir + "Disc (USA) (Track 02).BIN", noise(2352 * 30, 17));
    writeText(dir + "Real Disc (USA).cue", "FILE \"Real Disc (USA) (Track 1).bin\" BINARY\n  TRACK 01 MODE1/2352\n    INDEX 01 00:00:00\n");
    writeBytes(dir + "Real Disc (USA) (Track 1).bin", noise(2352 * 30, 18));
    // the core's system folder: BIOS files count, everything else there is not library content
    writeBytes(sys + "jagcd.bin", noise(256 * K, 19));
    writeBytes(sys + "jaguar/jagboot.rom", noise(128 * K, 20));
    writeBytes(sys + "stray game.j64", noise(1048576, 21));
    writeBytes(sys + "big.rom", noise(1048576, 22));
    writeText(sys + "notes.txt", "x");

    lib::ScanOptions opt;
    opt.folders = {dir};
    opt.systemFolder = sys;
    std::vector<lib::ScanFile> files = lib::scanFolders(opt, lib::ScanCache(), nullptr, nullptr);
    lib::Dat carts;
    CHECK(carts.parse(datText("1", datGame("[BIOS] Atari Jaguar Weird Boot (World)", "weird.bin", crcOf(weird)) +
                                       datGame("Game One (World)", "g.j64", crcOf(mt1)))));
    std::map<std::string, lib::Entry> by;
    for (const lib::ScanFile& f : files) by[fs::path(f.path).filename().string()] = lib::identify(f, &carts, nullptr);
    CHECK(by.count("Game One (World).j64") && by["Game One (World).j64"].section == lib::Section::Cart);
    CHECK(by["Game One (World).j64"].title == "Game One (World)");
    CHECK(by.count("Real Disc (USA).cue") && by["Real Disc (USA).cue"].section == lib::Section::Disc);
    CHECK(by["jagboot.rom"].section == lib::Section::Bios && by["jagboot.rom"].bios.role == lib::BiosRole::CartBoot);
    CHECK(by["[BIOS] Atari Jaguar CD (World).j64"].section == lib::Section::Bios);
    CHECK(by["[BIOS] Atari Jaguar CD (World).j64"].bios.role == lib::BiosRole::CdBios);
    CHECK(by["Memory Track.j64"].section == lib::Section::Bios && by["Memory Track.j64"].bios.role == lib::BiosRole::MemoryTrack);
    CHECK(by["jagcd.bin"].section == lib::Section::Bios && by["jagcd.bin"].bios.role == lib::BiosRole::CdBios);
    // the dat tags it "[BIOS]": it leaves the game grid even though its file name says nothing
    CHECK(by["blob.bin"].section == lib::Section::Bios && by["blob.bin"].bios.role == lib::BiosRole::CartBoot);
    CHECK(by["blob.bin"].title == "Atari Jaguar Weird Boot (World)");
    // disc tracks and non-BIOS files from the system folder never show
    CHECK(!by.count("Real Disc (USA) (Track 1).bin"));
    CHECK(!by.count("Disc (USA) (Track 2).bin") && !by.count("Disc (USA) (Track 02).BIN"));
    CHECK(!by.count("stray game.j64") && !by.count("big.rom") && !by.count("notes.txt"));
    int bios = 0, games = 0;
    for (const auto& kv : by) (kv.second.section == lib::Section::Bios ? bios : games)++;
    CHECK(bios == 5);   // jagboot.rom (two copies share a key), CD BIOS, Memory Track, jagcd.bin, blob.bin
    CHECK(games == 2);
}

static void testScanAndIdentify() {
    const size_t MiB = 1048576;
    std::string dir = g_tmp + "scan/";
    // carts
    std::vector<unsigned char> payload = noise(MiB, 1);
    putBE32(payload, 0x400, 0x04040404);
    std::vector<unsigned char> headered(512, 0);
    headered.insert(headered.end(), payload.begin(), payload.end());   // N MiB + 512 copier dump
    std::vector<unsigned char> other = noise(MiB, 2);
    std::vector<unsigned char> small = noise(200 * 1024, 3);
    std::vector<unsigned char> nested = noise(2 * MiB, 4);
    writeBytes(dir + "good.j64", payload);
    writeBytes(dir + "headered.j64", headered);
    writeBytes(dir + "Unlisted Game (World).rom", other);
    writeBytes(dir + "small.bin", small);
    writeBytes(dir + "deep/er/nested.JAG", nested);
    // not Jaguar content, or not content the core can read
    writeText(dir + "readme.txt", "hello");
    writeBytes(dir + "archive.zip", noise(1000, 5));
    writeBytes(dir + "huge-track.bin", std::vector<unsigned char>(9 * MiB, 0));   // a disc track, not a cart
    // a CUE set: Track 1 (audio) and Track 2, plus a CUE whose bin is gone, plus a CHD
    std::vector<unsigned char> t1 = noise(2352 * 40, 6), t2 = noise(2352 * 20, 7);
    writeBytes(dir + "Test Disc (USA) (Track 1).bin", t1);
    writeBytes(dir + "Test Disc (USA) (Track 2).bin", t2);
    writeText(dir + "Test Disc (USA).cue",
              "FILE \"test disc (usa) (track 1).BIN\" BINARY\r\n  TRACK 01 AUDIO\r\n    INDEX 01 00:00:00\r\n"
              "FILE \"Test Disc (USA) (Track 2).bin\" BINARY\r\n  TRACK 02 MODE1/2352\r\n    INDEX 01 00:00:00\r\n");
    writeText(dir + "Ghost (USA).cue", "FILE \"Ghost (USA) (Track 1).bin\" BINARY\n  TRACK 01 AUDIO\n    INDEX 01 00:00:00\n");
    writeBytes(dir + "Some Game (USA).chd", noise(4096, 8));

    lib::ScanOptions opt;
    opt.folders = {dir, dir};   // the same folder twice must not duplicate anything
    size_t hashedFirst = 0;
    std::vector<lib::ScanFile> files = lib::scanFolders(opt, lib::ScanCache(), nullptr, [&](size_t d, size_t t) {
        hashedFirst = t;
        (void)d;
    });
    int carts = 0, discs = 0;
    bool sawTrack = false, sawHuge = false, sawZip = false, sawTxt = false;
    for (const lib::ScanFile& f : files) {
        (f.kind == lib::Kind::Cart ? carts : discs)++;
        if (f.path.find("(Track") != std::string::npos && f.path.find(".bin") != std::string::npos) sawTrack = true;
        if (f.path.find("huge-track") != std::string::npos) sawHuge = true;
        if (f.path.find(".zip") != std::string::npos) sawZip = true;
        if (f.path.find(".txt") != std::string::npos) sawTxt = true;
    }
    CHECK(carts == 5);   // good, headered, Unlisted, small.bin, nested.JAG
    CHECK(discs == 3);   // Test Disc cue, Ghost cue, chd
    CHECK(!sawTrack && !sawHuge && !sawZip && !sawTxt);
    CHECK(hashedFirst == 8);

    // identification
    uint32_t payloadCrc = crcOf(payload), t1Crc = crcOf(t1);
    lib::Dat carts_dat, disc_dat;
    CHECK(carts_dat.parse(datText("1", datGame("Good Game (World)", "Good Game (World).j64", payloadCrc, "\tregion \"World\"\n") +
                                           datGame("Nested Game (World)", "n.jag", crcOf(nested)))));
    CHECK(disc_dat.parse(datText("1", datGame("Test Disc (USA)", "Test Disc (USA) (Track 1).bin", t1Crc) +
                                          datGame("Some Game (USA)", "Some Game (USA) (Track 1).bin", 0x11111111) +
                                          datGame("Ghost (USA)", "Ghost (USA) (Track 1).bin", 0x22222222))));
    std::map<std::string, lib::Entry> by;
    for (const lib::ScanFile& f : files) {
        lib::Entry e = lib::identify(f, &carts_dat, &disc_dat);
        by[fs::path(f.path).filename().string()] = e;
    }
    // whole-file CRC
    CHECK(by["good.j64"].match == lib::Match::Crc && by["good.j64"].title == "Good Game (World)" &&
          by["good.j64"].region == "World");
    // the copier-headered dump matches on its payload CRC
    CHECK(by["headered.j64"].file.crcStripped == payloadCrc);
    CHECK(by["headered.j64"].match == lib::Match::Crc && by["headered.j64"].datName == "Good Game (World)");
    CHECK(by["nested.JAG"].match == lib::Match::Crc && by["nested.JAG"].datName == "Nested Game (World)");
    // unknown: titled from the file name, never given art
    CHECK(by["Unlisted Game (World).rom"].match == lib::Match::Unknown && by["Unlisted Game (World).rom"].datName.empty() &&
          by["Unlisted Game (World).rom"].title == "Unlisted Game (World)");
    // disc: the CRC of the cue's first FILE (found case-insensitively) is the Redump track CRC
    CHECK(by["Test Disc (USA).cue"].file.crcWhole == t1Crc);
    CHECK(by["Test Disc (USA).cue"].match == lib::Match::Crc && by["Test Disc (USA).cue"].title == "Test Disc (USA)");
    // no checksum available: file name against the dat, flagged as a name match only
    CHECK(by["Ghost (USA).cue"].match == lib::Match::Name && by["Ghost (USA).cue"].datName == "Ghost (USA)");
    CHECK(by["Some Game (USA).chd"].match == lib::Match::Name);
    // a disc never matches the cartridge dat, nor the reverse
    lib::Entry crossed = lib::identify(by["good.j64"].file, nullptr, &disc_dat);
    CHECK(crossed.match == lib::Match::Unknown);
    // no dat at all: still a usable list
    lib::Entry nodat = lib::identify(by["good.j64"].file, nullptr, nullptr);
    CHECK(nodat.match == lib::Match::Unknown && nodat.title == "good");

    // the on-disk cache round-trips, and a rescan then hashes nothing
    lib::ScanCache cache = lib::parseCache(lib::serializeCache(files));
    CHECK(cache.size() == files.size());
    size_t hashedAgain = 99;
    std::vector<lib::ScanFile> again = lib::scanFolders(opt, cache, nullptr, [&](size_t, size_t t) { hashedAgain = t; });
    CHECK(again.size() == files.size());
    CHECK(hashedAgain == 99);   // never called: nothing needed hashing
    for (size_t i = 0; i < again.size() && i < files.size(); i++) CHECK(again[i].crcWhole == files[i].crcWhole);

    // a changed file is re-hashed (and only that one)
    std::this_thread::sleep_for(std::chrono::milliseconds(1100));   // mtime has 1 s resolution in the cache
    writeBytes(dir + "good.j64", noise(MiB, 99));
    size_t hashedThree = 0;
    again = lib::scanFolders(opt, cache, nullptr, [&](size_t, size_t t) { hashedThree = t; });
    CHECK(hashedThree == 1);

    // cancel
    std::atomic<bool> cancel(true);
    std::vector<lib::ScanFile> none = lib::scanFolders(opt, lib::ScanCache(), &cancel, nullptr);
    CHECK(none.size() < files.size());

    // a corrupt cache is ignored
    CHECK(lib::parseCache("garbage\nnot\ttabs").empty());
}

// A library folder may hold BIOS files in any layout: nested and with upper-case names.  They belong
// under BIOS & system (never with the games), wherever they sit under the folder, and the app can put them where
// the core reads them.
static void testBiosInLibraryFolders() {
    const size_t K = 1024;
    std::string root = g_tmp + "biosroot/", lib1 = root + "lib/", sys = root + "system/";
    std::error_code ec;
    // images the core knows only by checksum: injected, since a real BIOS cannot be committed
    std::vector<unsigned char> bootImg = noise(128 * K, 41), cdImg = noise(256 * K, 42), devImg = noise(256 * K, 43),
                               mImg = noise(128 * K, 51), kImg = noise(128 * K, 52);
    lib::addKnownBiosForTest(crcOf(bootImg), lib::BiosRole::CartBoot, "Test boot ROM");
    lib::addKnownBiosForTest(crcOf(cdImg), lib::BiosRole::CdBios, "Test CD BIOS (retail)");
    lib::addKnownBiosForTest(crcOf(devImg), lib::BiosRole::CdBios, "Test CD BIOS (developer)");
    lib::addKnownBiosForTest(crcOf(mImg), lib::BiosRole::CartBoot, "Test Model M boot ROM");
        writeBytes(lib1 + "Atari - Jaguar/BIOS/Boot ROMs/Rev B/dump-4711.bin", bootImg);   // nested, checksum only
    writeBytes(lib1 + "a/b/c/d/e/f/deep.rom", cdImg);                                    // 6 folders down, checksum only
    writeBytes(lib1 + "Sets/JAGCD.BIN", noise(256 * K, 44));                             // upper-case names
    writeBytes(lib1 + "Sets/JAGBOOT.ROM", noise(128 * K, 45));
    writeBytes(lib1 + "Sets/Jaguar CD BIOS.ROM", noise(256 * K, 46));
    writeBytes(lib1 + "Sets/some data.bin", noise(128 * K, 47));                          // unknown, not BIOS-named: a game
    writeBytes(lib1 + "Game (World).j64", noise(1024 * K, 48));
    writeBytes(lib1 + "Zipped/[BIOS] Atari Jaguar CD (World).zip", noise(100 * K, 49));  // archives are never read

    lib::ScanOptions opt;
    opt.folders = {lib1, lib1 + "Sets"};   // a folder inside another chosen one: nothing doubles
    opt.systemFolder = sys;
    std::vector<lib::ScanFile> files = lib::scanFolders(opt, lib::ScanCache(), nullptr, nullptr);
    std::map<std::string, lib::Entry> by;
    int total = 0;
    for (const lib::ScanFile& f : files) {
        std::string name = fs::path(f.path).filename().string();
        by[name] = lib::identify(f, nullptr, nullptr);
        total++;
    }
    auto isBios = [&](const char* n, lib::BiosRole role) {
        auto it = by.find(n);
        return it != by.end() && it->second.section == lib::Section::Bios && it->second.bios.role == role;
    };
    CHECK(isBios("dump-4711.bin", lib::BiosRole::CartBoot));
    CHECK(by.count("dump-4711.bin") && by["dump-4711.bin"].bios.recognized);
    CHECK(isBios("deep.rom", lib::BiosRole::CdBios));
    CHECK(isBios("JAGCD.BIN", lib::BiosRole::CdBios));
    CHECK(isBios("JAGBOOT.ROM", lib::BiosRole::CartBoot));
    CHECK(isBios("Jaguar CD BIOS.ROM", lib::BiosRole::CdBios));
    CHECK(by.count("Game (World).j64") && by["Game (World).j64"].section == lib::Section::Cart);
    CHECK(by.count("some data.bin") && by["some data.bin"].section == lib::Section::Cart);   // not mistaken for a BIOS
    CHECK(!by.count("[BIOS] Atari Jaguar CD (World).zip"));
    CHECK(total == 7);   // 5 BIOS + 2 games; nothing listed twice
    for (const auto& kv : by)
        if (kv.second.section == lib::Section::Cart) CHECK(kv.first == "Game (World).j64" || kv.first == "some data.bin");

    // ---- what the app copies into the system folder, and what the core then does with it ----
    const uint64_t S128 = 128 * K, S256 = 256 * K;
    struct { std::vector<unsigned char>* img; const char* name; const char* want; } plans[] = {
        {&bootImg, "dump-4711.bin", "jagboot.rom"},
        {&mImg, "m.bin", "jagboot_m.rom"},
        {&cdImg, "r.bin", "[BIOS] Atari Jaguar CD (World).bin"},
        {&devImg, "d.bin", "[BIOS] Atari Jaguar Developer CD (World).bin"},
    };
    for (const auto& pl : plans) {
        lib::BiosInfo bi = lib::classifyBios("/lib/" + std::string(pl.name), pl.img->size(), crcOf(*pl.img));
        std::string name = lib::biosInstallName(bi, pl.img->size());
        CHECK_EQ(name, pl.want);
        // installed under that name, the core reads it (name, folder and size are all it looks at)
        lib::BiosInfo inst = lib::classifyBios(sys + name, pl.img->size(), crcOf(*pl.img));
        CHECK(lib::biosUse(inst, sys + name, pl.img->size(), sys).core);
    }
    lib::BiosInfo unk = lib::classifyBios("/lib/My CD BIOS.bin", S256, 1);
    CHECK_EQ(lib::biosInstallName(unk, S256), "jagcd.bin");   // unrecognized: the generic name
    CHECK_EQ(lib::biosInstallName(unk, 0x30000), "");          // a size the core rejects: copying would not help
    lib::BiosInfo mt = lib::classifyBios("/lib/Memory Track.j64", S128, 1);
    CHECK_EQ(lib::biosInstallName(mt, S128), "");              // data, not read
    lib::BiosInfo realK;
    realK.role = lib::BiosRole::CartBoot;
    realK.recognized = true;
    realK.title = "Series K boot ROM";
    CHECK_EQ(lib::biosInstallName(realK, S128), "");           // built into the core, never read from a file

    // the service does the copy: into the system folder, once, never over a file that is there
    LibraryService svc;
    fs::create_directories(fs::u8path(sys), ec);
    svc.systemDir = sys;
    lib::Entry src = by["dump-4711.bin"];
    CHECK(svc.installBios(src));
    std::vector<unsigned char> got;
    {
        std::ifstream f(sys + "jagboot.rom", std::ios::binary);
        got.assign(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>());
    }
    CHECK(got == bootImg);
    CHECK(!svc.installBios(src));   // second time: already there
    CHECK(svc.status().find("already") != std::string::npos);
    CHECK(!svc.installBios(by["Game (World).j64"]));   // not a BIOS
}

// ---- tile zoom (pinch, Ctrl/Cmd+wheel, Ctrl/Cmd +/-/0 share this math) ------------------------------------------

static void testTileZoom() {
    const float eps = 1e-4f;
    auto near = [&](float a, float b) { return std::fabs(a - b) < eps; };
    CHECK(lib::kTileScaleMin < lib::kTileScaleDefault && lib::kTileScaleDefault < lib::kTileScaleMax);
    CHECK(near(lib::kTileScaleDefault, 1.0f));   // the shipped default stays 1.0x
    // clamp
    CHECK(near(lib::clampTileScale(1.0f), 1.0f));
    CHECK(near(lib::clampTileScale(-3.0f), lib::kTileScaleMin));
    CHECK(near(lib::clampTileScale(0.0f), lib::kTileScaleMin));
    CHECK(near(lib::clampTileScale(99.0f), lib::kTileScaleMax));
    CHECK(near(lib::clampTileScale(std::nanf("")), lib::kTileScaleDefault));
    CHECK(near(lib::clampTileScale(INFINITY), lib::kTileScaleDefault));
    // steps are multiplicative and reversible
    CHECK(near(lib::zoomTileScale(1.0f, 1.0f), lib::kTileScaleStep));
    CHECK(near(lib::zoomTileScale(lib::zoomTileScale(1.0f, 3.0f), -3.0f), 1.0f));
    CHECK(near(lib::zoomTileScale(1.0f, 0.5f), std::sqrt(lib::kTileScaleStep)));   // a fractional trackpad delta
    CHECK(near(lib::zoomTileScale(1.0f, 0.0f), 1.0f));
    // ... and stop at the ends, however far the wheel goes
    CHECK(near(lib::zoomTileScale(1.0f, 1000.0f), lib::kTileScaleMax));
    CHECK(near(lib::zoomTileScale(1.0f, -1000.0f), lib::kTileScaleMin));
    CHECK(near(lib::zoomTileScale(lib::kTileScaleMax, 1.0f), lib::kTileScaleMax));
    CHECK(near(lib::zoomTileScale(lib::kTileScaleMin, -1.0f), lib::kTileScaleMin));
    CHECK(near(lib::zoomTileScale(1.0f, std::nanf("")), 1.0f));
    // a pinch factor is applied as is (>1 zooms in), junk is ignored, a glitch cannot jump
    CHECK(near(lib::pinchTileScale(1.0f, 1.25f), 1.25f));
    CHECK(near(lib::pinchTileScale(1.0f, 0.8f), 0.8f));
    CHECK(near(lib::pinchTileScale(1.0f, 1.0f), 1.0f));
    CHECK(near(lib::pinchTileScale(1.0f, 0.0f), 1.0f));
    CHECK(near(lib::pinchTileScale(1.0f, -2.0f), 1.0f));
    CHECK(near(lib::pinchTileScale(1.0f, std::nanf("")), 1.0f));
    CHECK(near(lib::pinchTileScale(1.0f, 50.0f), 2.0f));
    CHECK(near(lib::pinchTileScale(2.0f, 1.5f), lib::kTileScaleMax));
    // the config value is clamped when read and written back after a zoom
    std::string root = g_tmp + "zoom-root/";
    Paths paths;
    paths.root = root;
    Config cfg;
    cfg.setPath(root + "config.ini");
    cfg.setFloat("library.size", 9.0f);   // hand-edited nonsense
    {
        LibraryService svc;
        svc.start(paths, cfg, false);
        CHECK(near(svc.cellScale, lib::kTileScaleMax));
        svc.cellScale = lib::pinchTileScale(svc.cellScale, 0.5f);
        svc.zoomChanged();
        svc.stop();   // the pending zoom is written on the way out
    }
    CHECK(near(cfg.getFloat("library.size", 0.0f), lib::kTileScaleMax * 0.5f));
    {
        Config again;
        again.setPath(root + "config.ini");
        CHECK(again.load(root + "config.ini"));
        LibraryService svc;
        svc.start(paths, again, false);
        CHECK(near(svc.cellScale, lib::kTileScaleMax * 0.5f));   // survives a restart
        svc.stop();
    }
}

// ---- loopback HTTP server (POSIX) ------------------------------------------------------------------------------

#ifndef _WIN32

struct Reply {
    int status = 200;
    std::string location;
    std::vector<unsigned char> body;
};

class TestServer {
public:
    std::function<Reply(const std::string&)> handler;
    int port = 0;

    bool start() {
        fd_ = ::socket(AF_INET, SOCK_STREAM, 0);
        if (fd_ < 0) return false;
        int one = 1;
        ::setsockopt(fd_, SOL_SOCKET, SO_REUSEADDR, &one, sizeof one);
        sockaddr_in a;
        std::memset(&a, 0, sizeof a);
        a.sin_family = AF_INET;
        a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        a.sin_port = 0;   // the OS picks a free port
        if (::bind(fd_, (sockaddr*)&a, sizeof a) != 0 || ::listen(fd_, 16) != 0) return false;
        socklen_t len = sizeof a;
        ::getsockname(fd_, (sockaddr*)&a, &len);
        port = ntohs(a.sin_port);
        running_ = true;
        th_ = std::thread([this] { loop(); });
        return true;
    }
    void stop() {
        running_ = false;
        if (th_.joinable()) th_.join();
        if (fd_ >= 0) ::close(fd_);
        fd_ = -1;
    }
    ~TestServer() { stop(); }
    int hits(const std::string& path) {
        std::lock_guard<std::mutex> g(m_);
        return hitCount_[path];
    }
    int totalHits() {
        std::lock_guard<std::mutex> g(m_);
        int n = 0;
        for (auto& kv : hitCount_) n += kv.second;
        return n;
    }
    std::string url(const std::string& path = "") const { return "http://127.0.0.1:" + std::to_string(port) + "/" + path; }

private:
    void loop() {
        while (running_) {
            pollfd p = {fd_, POLLIN, 0};
            if (::poll(&p, 1, 50) <= 0) continue;
            int c = ::accept(fd_, nullptr, nullptr);
            if (c < 0) continue;
            handle(c);
            ::close(c);
        }
    }
    void handle(int c) {
        std::string req;
        char buf[2048];
        while (req.find("\r\n\r\n") == std::string::npos && req.size() < 16384) {
            pollfd p = {c, POLLIN, 0};
            if (::poll(&p, 1, 2000) <= 0) return;
            ssize_t n = ::recv(c, buf, sizeof buf, 0);
            if (n <= 0) return;
            req.append(buf, (size_t)n);
        }
        size_t sp1 = req.find(' '), sp2 = req.find(' ', sp1 + 1);
        std::string path = req.substr(sp1 + 1, sp2 - sp1 - 1);
        {
            std::lock_guard<std::mutex> g(m_);
            hitCount_[path]++;
        }
        Reply r = handler ? handler(path) : Reply{404, "", {}};
        std::string head = "HTTP/1.1 " + std::to_string(r.status) + " X\r\nContent-Length: " + std::to_string(r.body.size()) +
                           "\r\nConnection: close\r\n";
        if (!r.location.empty()) head += "Location: " + r.location + "\r\n";
        head += "\r\n";
        send(c, head.data(), head.size());
        send(c, (const char*)r.body.data(), r.body.size());
    }
    static void send(int c, const char* d, size_t n) {
        while (n > 0) {
            ssize_t w = ::send(c, d, n, 0);
            if (w <= 0) return;
            d += w;
            n -= (size_t)w;
        }
    }
    int fd_ = -1;
    std::atomic<bool> running_{false};
    std::thread th_;
    std::mutex m_;
    std::map<std::string, int> hitCount_;
};

static std::vector<unsigned char> makePng(int w, int h, unsigned char shade) {
    SDL_Surface* s = SDL_CreateSurface(w, h, SDL_PIXELFORMAT_RGBA32);
    SDL_FillSurfaceRect(s, nullptr, SDL_MapSurfaceRGBA(s, shade, 100, 200, 255));
    std::string path = g_tmp + "mk-" + std::to_string(w) + "x" + std::to_string(h) + ".png";
    SDL_SavePNG(s, path.c_str());
    SDL_DestroySurface(s);
    std::vector<unsigned char> out;
    readFile(path, out);
    return out;
}

static void testHttp() {
    CHECK(http::available());
    TestServer srv;
    CHECK(srv.start());
    std::vector<unsigned char> big(3 * 1024 * 1024, 'x');
    srv.handler = [&](const std::string& p) {
        Reply r;
        if (p == "/ok") r.body = {'h', 'e', 'l', 'l', 'o'};
        else if (p == "/redir") { r.status = 302; r.location = "/ok"; }
        else if (p == "/big") r.body = big;
        else r.status = 404;
        return r;
    };
    http::Response r;
    CHECK(http::get(srv.url("ok"), r, 1 << 20, nullptr));
    CHECK(r.status == 200 && r.body.size() == 5 && std::memcmp(r.body.data(), "hello", 5) == 0);
    CHECK(http::get(srv.url("missing"), r, 1 << 20, nullptr));
    CHECK(r.status == 404);                     // a 404 is a response, not a transport error
    CHECK(http::get(srv.url("redir"), r, 1 << 20, nullptr));
    CHECK(r.status == 200 && r.body.size() == 5);   // redirects are followed
    CHECK(!http::get(srv.url("big"), r, 1 << 20, nullptr));
    CHECK(r.error.find("large") != std::string::npos);   // the size cap stops it
    CHECK(http::get(srv.url("big"), r, 8u << 20, nullptr) && r.body.size() == big.size());
    std::atomic<bool> cancel(true);
    CHECK(!http::get(srv.url("ok"), r, 1 << 20, &cancel) || r.status == 200);   // cancel may race a tiny reply; must not hang
    CHECK(!http::get("file:///etc/hosts", r, 1 << 20, nullptr));                   // only http(s)
    CHECK(!http::get("ftp://127.0.0.1/x", r, 1 << 20, nullptr));
    int deadPort = srv.port;
    srv.stop();
    auto t0 = std::chrono::steady_clock::now();
    CHECK(!http::get("http://127.0.0.1:" + std::to_string(deadPort) + "/ok", r, 1 << 20, nullptr));   // refused
    CHECK(!r.error.empty());
    CHECK(std::chrono::steady_clock::now() - t0 < std::chrono::seconds(10));
}

// The whole art pipeline: scan -> identify -> download -> cache -> decode -> texture, then offline.
static void testServicePipeline() {
    std::string root = g_tmp + "svc-root/";
    std::string games = g_tmp + "svc-games/";
    std::error_code ec;
    fs::create_directories(root + "library/db", ec);

    std::vector<unsigned char> romA = noise(1048576, 21), romB = noise(1048576, 22), romC = noise(1048576, 23);
    putBE32(romA, 0x400, 0x04040404);
    writeBytes(games + "a.j64", romA);
    writeBytes(games + "b.j64", romB);
    writeBytes(games + "c.j64", romC);
    std::string name = "Pipeline: Test & Co (World)";   // special characters exercise sanitising end to end
    std::string datA = datText("2099.01.01", datGame(name, "a.j64", crcOf(romA)) +
                                                 datGame("No Art Game (World)", "b.j64", crcOf(romB)) +
                                                 datGame("Offline Game (World)", "c.j64", crcOf(romC)));
    writeText(root + "library/db/Atari - Jaguar.dat", datA);

    std::vector<unsigned char> png = makePng(600, 800, 90);
    std::string sanitized = lib::sanitizeThumbName(name);
    std::string artPath = "/" + lib::urlEncode("Atari - Jaguar") + "/Named_Boxarts/" + lib::urlEncode(sanitized) + ".png";

    // what "Update database" downloads: newer dats (a rename of game A proves they were re-read)
    std::string updC = datText("2099.06.01", datGame("Renamed By Update (World)", "a.j64", crcOf(romA)) + datGame("Filler One", "x", 1) +
                                                 datGame("Filler Two", "x", 2) + datGame("Filler Three", "x", 3) +
                                                 datGame("Filler Four", "x", 4) + datGame("Filler Five", "x", 5));
    std::string updD = datText("2099.06.01", datGame("Disc One", "x", 1) + datGame("Disc Two", "x", 2) + datGame("Disc Three", "x", 3) +
                                                 datGame("Disc Four", "x", 4) + datGame("Disc Five", "x", 5));
    std::vector<unsigned char> updCarts(updC.begin(), updC.end()), updDiscs(updD.begin(), updD.end());

    TestServer srv;
    CHECK(srv.start());
    srv.handler = [&](const std::string& p) {
        Reply r;
        if (p == artPath) r.body = png;
        else if (p == "/no-intro/" + lib::urlEncode("Atari - Jaguar.dat")) r.body = updCarts;
        else if (p == "/redump/" + lib::urlEncode("Atari - Jaguar CD.dat")) r.body = updDiscs;
        else r.status = 404;
        return r;
    };

    Paths paths;
    paths.root = root;
    Config cfg;
    cfg.setPath(root + "config.ini");
    cfg.set("library.dir.0", games.substr(0, games.size() - 1));

    struct Fake {
        int created = 0, destroyed = 0, lastW = 0, lastH = 0;
    } fake;
    auto bind = [&](LibraryService& s) {
        s.createTexture = [&](const void*, int w, int h) { fake.created++; fake.lastW = w; fake.lastH = h; return (unsigned)fake.created; };
        s.destroyTexture = [&](unsigned) { fake.destroyed++; };
    };
    auto waitFor = [&](LibraryService& s, const std::function<bool()>& cond) {
        for (int i = 0; i < 400; i++) {   // up to ~10 s
            s.pump();
            if (cond()) return true;
            SDL_Delay(25);
        }
        return false;
    };
    auto find = [&](LibraryService& s, const std::string& title) -> const lib::Entry* {
        for (const lib::Entry& e : s.entries())
            if (e.title == title) return &e;
        return nullptr;
    };

    {
        LibraryService svc;
        bind(svc);
        svc.artBaseUrl = srv.url();
        svc.artMirrorUrl.clear();
        svc.dbBaseUrl = srv.url();
        svc.start(paths, cfg, true);
        CHECK(svc.show);   // a configured library opens at start when no game is loaded
        CHECK(waitFor(svc, [&] { return !svc.scanning() && svc.entries().size() == 3; }));
        const lib::Entry* a = find(svc, name);
        const lib::Entry* b = find(svc, "No Art Game (World)");
        CHECK(a && a->match == lib::Match::Crc);
        CHECK(b && b->match == lib::Match::Crc);
        CHECK(svc.identified() == 3);

        // art for A: downloaded, cached, decoded to <= 256 px keeping the 3:4 aspect, uploaded as a texture
        ArtView v;
        CHECK(waitFor(svc, [&] { v = svc.art(*a); return v.state == ArtView::Ready; }));
        CHECK(v.tex != 0 && v.w == 192 && v.h == 256);
        CHECK(fake.created == 1);
        CHECK(fs::exists(fs::u8path(lib::thumbnailCachePath(root, "Named_Boxarts", name))));
        CHECK(srv.hits(artPath) == 1);

        // art for B: the server says 404 for all three kinds -> Missing, remembered, not asked again
        ArtView vb;
        CHECK(waitFor(svc, [&] { vb = svc.art(*b); return vb.state == ArtView::Missing; }));
        CHECK(fs::exists(fs::u8path(lib::thumbnailMissPath(root, "Named_Boxarts", "No Art Game (World)"))));
        int hitsBefore = srv.totalHits();
        for (int i = 0; i < 20; i++) {
            svc.pump();
            svc.art(*b);
            SDL_Delay(10);
        }
        CHECK(srv.totalHits() == hitsBefore);
        // the library hint's count: B only (A has art, C was never drawn), and no discs in this library
        CHECK(svc.missingArt(lib::Section::Cart) == 1);
        CHECK(svc.missingArt(lib::Section::Disc) == 0);

        // an unidentified entry has no thumbnail name: it ends up Missing without ever touching the network
        lib::Entry unknown = *a;
        unknown.datName.clear();
        unknown.file.crcWhole = 0x0BADF00Du;   // its own slot (the tile key is the checksum, not the title)
        int hitsUnknown = srv.totalHits();
        ArtView vu;
        CHECK(waitFor(svc, [&] { vu = svc.art(unknown); return vu.state == ArtView::Missing; }));
        CHECK(srv.totalHits() == hitsUnknown);

        // launching posts the same "open this path" event the file dialog does
        SDL_FlushEvents(SDL_EVENT_USER, SDL_EVENT_USER);
        svc.launch(*a);
        SDL_Event ev;
        SDL_PumpEvents();
        bool got = SDL_PollEvent(&ev) && ev.type == SDL_EVENT_USER && ev.user.code == 0x56414A47 && ev.user.data1 &&
                   std::string((const char*)ev.user.data1) == a->file.path;
        if (got) SDL_free(ev.user.data1);
        CHECK(got);
        CHECK(!svc.show);   // "close on launch"
        // ... a plain launch carries no marker; a fresh one (the TV screens: the resume prompt needs a mouse) marks the event
        // so App::handleEvent opens the game without it (#991).
        SDL_PumpEvents();
        SDL_FlushEvents(SDL_EVENT_USER, SDL_EVENT_USER);
        svc.launch(*a);
        svc.launch(*a, /*fresh=*/true);
        SDL_PumpEvents();
        std::vector<SDL_Event> opens;   // the "open this path" events, in order
        {
            SDL_Event got[8];
            const int n = SDL_PeepEvents(got, 8, SDL_GETEVENT, SDL_EVENT_USER, SDL_EVENT_USER);
            for (int i = 0; i < n; i++)
                if (got[i].user.code == 0x56414A47 && got[i].user.data1) opens.push_back(got[i]);
        }
        const bool gotPlain = opens.size() == 2, gotFresh = opens.size() == 2;
        SDL_Event plain = opens.size() == 2 ? opens[0] : SDL_Event(), fresh = opens.size() == 2 ? opens[1] : SDL_Event();
        CHECK(gotPlain && gotFresh);
        if (gotPlain) {
            CHECK(plain.user.data2 == nullptr);
            SDL_free(plain.user.data1);
        }
        if (gotFresh) {
            CHECK(fresh.user.data2 != nullptr && std::string((const char*)fresh.user.data1) == a->file.path);
            SDL_free(fresh.user.data1);
        }

        // database update: both dats downloaded, validated, written, re-identified
        svc.updateDatabase();
        CHECK(waitFor(svc, [&] { return svc.status().find("updated") != std::string::npos; }));
        CHECK(waitFor(svc, [&] { return find(svc, "Renamed By Update (World)") != nullptr; }));
        CHECK(svc.cartDatInfo().find("2099.06.01") != std::string::npos);
        svc.stop();
    }
    CHECK(fake.destroyed >= 1);

    // Offline, a new session: the cached cover still shows, the miss marker still holds, and a
    // server that is gone is never contacted for them.
    srv.stop();
    {
        LibraryService svc;
        bind(svc);
        svc.artBaseUrl = "http://127.0.0.1:" + std::to_string(srv.port) + "/";   // closed port
        svc.artMirrorUrl.clear();
        svc.dbBaseUrl = svc.artBaseUrl;
        svc.start(paths, cfg, false);
        CHECK(!svc.show);
        CHECK(waitFor(svc, [&] { return !svc.scanning() && svc.entries().size() == 3; }));
        const lib::Entry* a = find(svc, "Renamed By Update (World)");   // the updated dat won (newer version)
        CHECK(a != nullptr);
        if (a) {
            // The renamed title has no cached art under its new name: Missing once the offline fetch fails.
            ArtView v;
            CHECK(waitFor(svc, [&] { v = svc.art(*a); return v.state == ArtView::Missing; }));
        }
        svc.stop();
    }
    {
        // Same root, the original dat restored: the cover downloaded earlier shows with the network off.
        writeText(root + "library/db/Atari - Jaguar.dat", datText("2099.12.31", datGame(name, "a.j64", crcOf(romA)) +
                                                                                    datGame("x", "x", 7) + datGame("y", "y", 8)));
        LibraryService svc;
        bind(svc);
        svc.artBaseUrl = "http://127.0.0.1:" + std::to_string(srv.port) + "/";
        svc.artMirrorUrl.clear();
        svc.dbBaseUrl = svc.artBaseUrl;
        svc.start(paths, cfg, false);
        CHECK(waitFor(svc, [&] { return !svc.scanning() && find(svc, name) != nullptr; }));
        const lib::Entry* a = find(svc, name);
        ArtView v;
        CHECK(a && waitFor(svc, [&] { v = svc.art(*a); return v.state == ArtView::Ready; }));
        CHECK(v.w == 192 && v.h == 256);

        // downloads switched off: a title with nothing cached goes Missing without any request
        svc.setDownloadArt(false);
        lib::Entry fresh = *a;
        fresh.datName = "Never Cached (World)";
        ArtView vf;
        CHECK(waitFor(svc, [&] { vf = svc.art(fresh); return vf.state == ArtView::Missing; }));
        CHECK(!svc.netBusy());
        svc.stop();
    }
}


// Disc art: libretro-thumbnails first, then the site mirror; per-source miss markers; carts never see the mirror.
static void testCdMirror() {
    std::string root = g_tmp + "mirror-root/";
    std::error_code ec;
    fs::create_directories(root + "library/db", ec);
    std::vector<unsigned char> png = makePng(300, 400, 70);

    const std::string UP = "up/", MIR = "mirror/";
    auto pathFor = [&](const std::string& prefix, const char* system, const std::string& type, const std::string& name) {
        return "/" + prefix + lib::urlEncode(system) + "/" + type + "/" + lib::urlEncode(lib::sanitizeThumbName(name)) + ".png";
    };
    auto upPath = [&](const std::string& type, const std::string& n) { return pathFor(UP, "Atari - Jaguar", type, n); };
    auto mirPath = [&](const std::string& type, const std::string& n) { return pathFor(MIR, "Atari - Jaguar CD", type, n); };

    std::mutex logM;
    std::vector<std::string> log;                 // request order
    std::map<std::string, std::vector<unsigned char>> have;   // paths that answer 200, with their body
    TestServer srv;
    CHECK(srv.start());
    srv.handler = [&](const std::string& p) {
        Reply r;
        std::lock_guard<std::mutex> g(logM);
        log.push_back(p);
        auto it = have.find(p);
        if (it != have.end()) r.body = it->second;
        else r.status = 404;
        return r;
    };
    auto logSize = [&] { std::lock_guard<std::mutex> g(logM); return log.size(); };
    auto logAt = [&](size_t i) { std::lock_guard<std::mutex> g(logM); return i < log.size() ? log[i] : std::string(); };
    auto setHave = [&](const std::string& p, const std::vector<unsigned char>* body = nullptr) {
        std::lock_guard<std::mutex> g(logM);
        have[p] = body ? *body : png;
    };
    std::vector<unsigned char> pngUp = makePng(150, 200, 40);   // upstream's copy: a different size from the mirror's

    Paths paths;
    paths.root = root;
    struct Fake {
        int created = 0;
    } fake;
    auto runSession = [&](const std::function<void(LibraryService&, Config&)>& body, const std::string& mirrorUrl) {
        Config cfg;
        cfg.setPath(root + "config.ini");
        LibraryService svc;
        svc.createTexture = [&](const void*, int, int) { return (unsigned)++fake.created; };
        svc.destroyTexture = [](unsigned) {};
        svc.artBaseUrl = srv.url(UP);
        svc.artMirrorUrl = mirrorUrl;
        svc.dbBaseUrl = srv.url();
        svc.start(paths, cfg, false);
        body(svc, cfg);
        svc.stop();
    };
    auto waitState = [&](LibraryService& s, const lib::Entry& e, ArtView::State want) {
        for (int i = 0; i < 400; i++) {
            s.pump();
            if (s.art(e).state == want) return true;
            SDL_Delay(25);
        }
        return false;
    };
    auto mkEntry = [&](const char* name, lib::Section sec) {
        lib::Entry e;
        e.section = sec;
        e.title = e.datName = name;
        return e;
    };
    auto exists = [&](const std::string& p) { return fs::exists(fs::u8path(p)); };
    auto miss = [&](const char* type, const std::string& name, const char* system) {
        return exists(lib::thumbnailMissPath(root, type, name, system));
    };
    auto ageFile = [&](const std::string& p, int days) {
        std::error_code e2;
        fs::last_write_time(fs::u8path(p), fs::file_time_type::clock::now() - std::chrono::hours(24 * days), e2);
        CHECK(!e2);
    };

    const char* kB = "Named_Boxarts";
    const char* kT = "Named_Titles";
    const char* kS = "Named_Snaps";
    lib::Entry onlyMirror = mkEntry("Mirror Only (USA)", lib::Section::Disc);      // upstream has nothing
    lib::Entry upstreamWins = mkEntry("Upstream Wins (USA)", lib::Section::Disc);  // both have it
    lib::Entry snapOnly = mkEntry("Snap Only (USA)", lib::Section::Disc);          // mirror has just a snap
    lib::Entry nothing = mkEntry("Nothing Anywhere (USA)", lib::Section::Disc);
    lib::Entry cart = mkEntry("Some Cart (World)", lib::Section::Cart);
    setHave(mirPath(kB, onlyMirror.datName));
    setHave(upPath(kB, upstreamWins.datName));
    setHave(mirPath(kB, upstreamWins.datName));
    setHave(mirPath(kS, snapOnly.datName));
    setHave(mirPath(kB, cart.datName));   // must never be asked for: carts do not use the mirror

    runSession([&](LibraryService& svc, Config&) {
        // upstream misses, the mirror answers: asked in that order, cached under the shared name, one marker
        // for the upstream miss and none for the mirror
        CHECK(waitState(svc, onlyMirror, ArtView::Ready));
        CHECK(logSize() == 2);
        CHECK_EQ(logAt(0), upPath(kB, onlyMirror.datName));
        CHECK_EQ(logAt(1), mirPath(kB, onlyMirror.datName));
        CHECK(exists(lib::thumbnailCachePath(root, kB, onlyMirror.datName, lib::kThumbSystemCd)));
        CHECK(!exists(lib::thumbnailCachePath(root, kB, onlyMirror.datName)));   // never in upstream's folder
        CHECK(miss(kB, onlyMirror.datName, lib::kThumbSystem));
        CHECK(!miss(kB, onlyMirror.datName, lib::kThumbSystemCd));

        // upstream has it: the mirror is never asked
        size_t before = logSize();
        CHECK(waitState(svc, upstreamWins, ArtView::Ready));
        CHECK(logSize() == before + 1);
        CHECK_EQ(logAt(before), upPath(kB, upstreamWins.datName));

        // box art first from both sources, then title from both, then snap: the mirror's snap is the last resort
        before = logSize();
        CHECK(waitState(svc, snapOnly, ArtView::Ready));
        CHECK(logSize() == before + 6);
        CHECK_EQ(logAt(before + 0), upPath(kB, snapOnly.datName));
        CHECK_EQ(logAt(before + 1), mirPath(kB, snapOnly.datName));
        CHECK_EQ(logAt(before + 2), upPath(kT, snapOnly.datName));
        CHECK_EQ(logAt(before + 3), mirPath(kT, snapOnly.datName));
        CHECK_EQ(logAt(before + 4), upPath(kS, snapOnly.datName));
        CHECK_EQ(logAt(before + 5), mirPath(kS, snapOnly.datName));
        // the mirror's misses are remembered for this session only, never written as files (#997)
        CHECK(!miss(kB, snapOnly.datName, lib::kThumbSystemCd) && !miss(kT, snapOnly.datName, lib::kThumbSystemCd));
        CHECK(exists(lib::thumbnailCachePath(root, kS, snapOnly.datName, lib::kThumbSystemCd)));

        // a cart: upstream only (3 types), the mirror untouched even though it has a file by that name
        before = logSize();
        CHECK(waitState(svc, cart, ArtView::Missing));
        CHECK(logSize() == before + 3);
        for (size_t i = before; i < logSize(); i++) CHECK(logAt(i).rfind("/up/", 0) == 0);

        // nothing anywhere: Missing, three upstream markers and no mirror ones, and it stays quiet afterwards
        before = logSize();
        CHECK(waitState(svc, nothing, ArtView::Missing));
        CHECK(logSize() == before + 6);
        for (const char* t : {kB, kT, kS}) CHECK(miss(t, nothing.datName, lib::kThumbSystem) && !miss(t, nothing.datName, lib::kThumbSystemCd));
        size_t quiet = logSize();
        for (int i = 0; i < 20; i++) {
            svc.pump();
            svc.art(nothing);
            svc.art(onlyMirror);
            SDL_Delay(10);
        }
        CHECK(logSize() == quiet);
    }, srv.url(MIR));

    // New session, nothing changed: upstream's misses still hold and cached art needs no request; the mirror is
    // asked once more about the disc it had nothing for, because its misses last one session.
    size_t base0 = logSize();
    runSession([&](LibraryService& svc, Config&) {
        CHECK(waitState(svc, nothing, ArtView::Missing));
        CHECK(waitState(svc, onlyMirror, ArtView::Ready));
        CHECK(waitState(svc, cart, ArtView::Missing));
        CHECK(logSize() == base0 + 3);
        for (size_t i = base0; i < logSize(); i++) CHECK(logAt(i).rfind("/mirror/", 0) == 0);
    }, srv.url(MIR));

    // The mirror gains art: the next session finds it, with no week-long wait; upstream's 30-day marker holds.
    setHave(mirPath(kT, nothing.datName));   // the mirror gained the title screen meanwhile
    base0 = logSize();
    runSession([&](LibraryService& svc, Config&) {
        CHECK(waitState(svc, nothing, ArtView::Ready));
        CHECK(logSize() >= base0 + 2);
        for (size_t i = base0; i < logSize(); i++) CHECK(logAt(i).rfind("/mirror/", 0) == 0);   // upstream untouched
        CHECK_EQ(logAt(base0), mirPath(kB, nothing.datName));
    }, srv.url(MIR));

    // #997: mirror miss markers an older version wrote (fresh, so their week has not run out) are ignored and
    // removed, and the mirror is asked: art the site gained after that miss shows on the next launch.
    lib::Entry stale = mkEntry("Stale Marker (USA)", lib::Section::Disc);
    for (const char* t : {kB, kT, kS}) {
        std::string m = lib::thumbnailMissPath(root, t, stale.datName, lib::kThumbSystemCd);
        fs::create_directories(fs::u8path(m.substr(0, m.find_last_of('/'))), ec);
        std::ofstream(fs::u8path(m)).flush();
        CHECK(exists(m));
    }
    setHave(mirPath(kB, stale.datName));
    base0 = logSize();
    runSession([&](LibraryService& svc, Config&) {
        CHECK(waitState(svc, stale, ArtView::Ready));
        CHECK(logSize() == base0 + 2);   // upstream's box art, then the mirror's
        CHECK_EQ(logAt(base0 + 1), mirPath(kB, stale.datName));
        CHECK(!miss(kB, stale.datName, lib::kThumbSystemCd));
    }, srv.url(MIR));

    // libretro-thumbnails gains the art later: its 30-day marker expires and upstream wins over the mirror's file.
    lib::Entry late = mkEntry("Late Upstream (USA)", lib::Section::Disc);
    runSession([&](LibraryService& svc, Config&) { CHECK(waitState(svc, late, ArtView::Missing)); }, srv.url(MIR));
    for (const char* t : {kB, kT, kS}) ageFile(lib::thumbnailMissPath(root, t, late.datName, lib::kThumbSystem), 31);
    setHave(upPath(kB, late.datName));
    base0 = logSize();
    runSession([&](LibraryService& svc, Config&) {
        CHECK(waitState(svc, late, ArtView::Ready));
        CHECK(logSize() == base0 + 1);
        CHECK_EQ(logAt(base0), upPath(kB, late.datName));
    }, srv.url(MIR));

    // Mirror art cached, then libretro-thumbnails gains the same picture: once its 30-day marker expires the
    // upstream copy is fetched, cached in its own folder, and shown from then on instead of the mirror's.
    ageFile(lib::thumbnailMissPath(root, kB, onlyMirror.datName), 31);
    setHave(upPath(kB, onlyMirror.datName), &pngUp);
    base0 = logSize();
    runSession([&](LibraryService& svc, Config&) {
        ArtView v;
        CHECK(waitState(svc, onlyMirror, ArtView::Ready));
        v = svc.art(onlyMirror);
        CHECK(v.w == 150 && v.h == 200);   // upstream's picture, not the mirror's 300x400 (256 px cap: 192x256)
        CHECK(logSize() == base0 + 1);
        CHECK_EQ(logAt(base0), upPath(kB, onlyMirror.datName));
        CHECK(exists(lib::thumbnailCachePath(root, kB, onlyMirror.datName)));
        CHECK(exists(lib::thumbnailCachePath(root, kB, onlyMirror.datName, lib::kThumbSystemCd)));   // kept, but outranked
    }, srv.url(MIR));
    base0 = logSize();
    runSession([&](LibraryService& svc, Config&) {   // and it stays that way offline
        CHECK(waitState(svc, onlyMirror, ArtView::Ready));
        ArtView v = svc.art(onlyMirror);
        CHECK(v.w == 150 && v.h == 200);
        CHECK(logSize() == base0);
    }, srv.url(MIR));

    // Mirror switched off (empty base): upstream only.  Downloads off: nothing asked at all.
    lib::Entry off = mkEntry("Mirror Off (USA)", lib::Section::Disc);
    setHave(mirPath(kB, off.datName));
    base0 = logSize();
    runSession([&](LibraryService& svc, Config&) {
        CHECK(waitState(svc, off, ArtView::Missing));
        CHECK(logSize() == base0 + 3);
        for (size_t i = base0; i < logSize(); i++) CHECK(logAt(i).rfind("/up/", 0) == 0);
    }, "");
    lib::Entry noNet = mkEntry("Downloads Off (USA)", lib::Section::Disc);
    base0 = logSize();
    runSession([&](LibraryService& svc, Config&) {
        svc.setDownloadArt(false);
        CHECK(waitState(svc, noNet, ArtView::Missing));
        CHECK(logSize() == base0);
    }, srv.url(MIR));

    // Config: absent = the site default, present = as written, empty = off.
    {
        Config cfg;
        cfg.setPath(root + "config2.ini");
        LibraryService a;
        CHECK_EQ(a.artMirrorUrl, std::string(lib::kMirrorBase));
        a.start(paths, cfg, false);
        CHECK_EQ(a.artMirrorUrl, std::string(lib::kMirrorBase));
        a.stop();
        cfg.set("library.art_mirror", "http://example.invalid/t/");
        LibraryService b;
        b.start(paths, cfg, false);
        CHECK_EQ(b.artMirrorUrl, std::string("http://example.invalid/t/"));
        b.stop();
        cfg.set("library.art_mirror", "");
        LibraryService c;
        c.start(paths, cfg, false);
        CHECK(c.artMirrorUrl.empty());
        c.stop();
    }
    srv.stop();
}

#endif  // !_WIN32

// ---- covers (#838): display names, tile keys, precedence, the service ---------------------------------------------------

static std::vector<unsigned char> makeCoverPng(const std::string& name, int w, int h, unsigned char shade) {
    SDL_Surface* s = SDL_CreateSurface(w, h, SDL_PIXELFORMAT_RGBA32);
    SDL_FillSurfaceRect(s, nullptr, SDL_MapSurfaceRGBA(s, shade, 100, 200, 255));
    std::string path = g_tmp + name;
    SDL_SavePNG(s, path.c_str());
    SDL_DestroySurface(s);
    std::vector<unsigned char> out;
    readFile(path, out);
    return out;
}

static void testDiscTitles() {
    CHECK_EQ(lib::titleFromDiscStem("Myst (USA) (Track 1)"), "Myst (USA)");
    CHECK_EQ(lib::titleFromDiscStem("Myst_(USA)_(Track_01)"), "Myst (USA)");
    CHECK_EQ(lib::titleFromDiscStem("Battlemorph [Track 1]"), "Battlemorph");
    CHECK_EQ(lib::titleFromDiscStem("Highlander - Track 01"), "Highlander");
    CHECK_EQ(lib::titleFromDiscStem("Space Ace.bin"), "Space Ace");
    CHECK_EQ(lib::titleFromDiscStem("Dragons Lair (Track 1).bin"), "Dragons Lair");
    CHECK_EQ(lib::titleFromDiscStem("Blue   Lightning"), "Blue Lightning");
    // kept: multi-disc and revision tags tell releases apart, and a title may end in "Track 2"
    CHECK_EQ(lib::titleFromDiscStem("Primal Rage (Disc 2)"), "Primal Rage (Disc 2)");
    CHECK_EQ(lib::titleFromDiscStem("Baldies (Rev 1)"), "Baldies (Rev 1)");
    CHECK_EQ(lib::titleFromDiscStem("Super Track 2"), "Super Track 2");
    CHECK_EQ(lib::titleFromDiscStem("Track 1"), "Track 1");        // never reduced to nothing
    CHECK_EQ(lib::titleFromDiscStem("(Track 1)"), "(Track 1)");

    lib::ScanFile f;
    f.path = "/rips/Unlisted Disc (USA) (Track 1).cue";
    f.kind = lib::Kind::Disc;
    lib::Entry e = lib::identify(f, nullptr, nullptr);
    CHECK_EQ(e.title, "Unlisted Disc (USA)");
    CHECK(e.datName.empty() && e.section == lib::Section::Disc);
    f.path = "/carts/Some_Cart_(Track 1).j64";   // carts keep the plain rule: "(Track 1)" is not noise there
    f.kind = lib::Kind::Cart;
    CHECK_EQ(lib::identify(f, nullptr, nullptr).title, "Some Cart (Track 1)");
}

static void testCoverLookup() {
    lib::Entry e;
    e.datName = "Pipeline: Test & Co (World)";
    e.title = e.datName;
    CHECK_EQ(lib::coverKey(e), e.datName);
    e.datName.clear();
    e.file.path = "/rips/a.cue";
    e.file.crcWhole = 0xABCDEF01u;
    CHECK_EQ(lib::coverKey(e), "crc-ABCDEF01");   // a content fingerprint: survives moving the folder
    e.file.path = "/elsewhere/renamed.cue";
    CHECK_EQ(lib::coverKey(e), "crc-ABCDEF01");
    e.file.crcWhole = 0;
    std::string k1 = lib::coverKey(e);
    CHECK(k1.rfind("path-", 0) == 0 && k1.size() == 5 + 8);
    e.file.path = "/elsewhere/other.chd";
    CHECK(lib::coverKey(e) != k1);

    CHECK_EQ(lib::customCoverPath("/d/", "Ecco: The Tides"), "/d/covers/custom/Ecco_ The Tides.png");
    CHECK_EQ(lib::localCoverPath("/d/", "A/B? (USA)"), "/d/covers/A_B_ (USA).png");
    CHECK_EQ(lib::localCoverPath("/d/", ".."), "/d/covers/_.png");   // no escaping the folder
    CHECK_EQ(lib::localCoverPath("/d/", "Game. "), "/d/covers/Game.png");

    // precedence: custom > local folder (dat name, then shown name) > downloaded > nothing
    std::set<std::string> have;
    auto exists = [&](const std::string& p) { return have.count(p) != 0; };
    const std::string root = "/d/", key = "Dat Name (USA)", title = "Shown Name", dat = "Dat Name (USA)";
    CHECK(lib::chooseCover(root, key, title, dat, exists).source == lib::CoverSource::None);
    have.insert(lib::thumbnailCachePath(root, "Named_Snaps", dat));
    lib::CoverChoice c = lib::chooseCover(root, key, title, dat, exists);
    CHECK(c.source == lib::CoverSource::Downloaded && c.path == lib::thumbnailCachePath(root, "Named_Snaps", dat));
    // a disc's copy from the site mirror counts too, and libretro-thumbnails' copy of the same type wins over it
    have.insert(lib::thumbnailCachePath(root, "Named_Boxarts", dat, lib::kThumbSystemCd));
    c = lib::chooseCover(root, key, title, dat, exists);
    CHECK(c.source == lib::CoverSource::Downloaded && c.path == lib::thumbnailCachePath(root, "Named_Boxarts", dat, lib::kThumbSystemCd));
    have.insert(lib::thumbnailCachePath(root, "Named_Boxarts", dat));
    c = lib::chooseCover(root, key, title, dat, exists);
    CHECK(c.source == lib::CoverSource::Downloaded && c.path == lib::thumbnailCachePath(root, "Named_Boxarts", dat));
    have.erase(lib::thumbnailCachePath(root, "Named_Boxarts", dat));
    have.erase(lib::thumbnailCachePath(root, "Named_Boxarts", dat, lib::kThumbSystemCd));
    have.insert(lib::localCoverPath(root, title));
    c = lib::chooseCover(root, key, title, dat, exists);
    CHECK(c.source == lib::CoverSource::LocalFolder && c.path == lib::localCoverPath(root, title));
    have.insert(lib::localCoverPath(root, dat));   // the dat name outranks the shown name
    c = lib::chooseCover(root, key, title, dat, exists);
    CHECK(c.source == lib::CoverSource::LocalFolder && c.path == lib::localCoverPath(root, dat));
    have.insert(lib::customCoverPath(root, key));
    c = lib::chooseCover(root, key, title, dat, exists);
    CHECK(c.source == lib::CoverSource::Custom && c.path == lib::customCoverPath(root, key));
    // an unidentified tile never counts a downloaded thumbnail, and has no dat-name local lookup
    have.clear();
    have.insert(lib::thumbnailCachePath(root, "Named_Boxarts", title));
    CHECK(lib::chooseCover(root, "crc-00000001", title, "", exists).source == lib::CoverSource::None);
    have.insert(lib::localCoverPath(root, title));
    CHECK(lib::chooseCover(root, "crc-00000001", title, "", exists).source == lib::CoverSource::LocalFolder);
}

static void testCoverService() {
    std::string root = g_tmp + "cover-root/";
    std::string games = g_tmp + "cover-games/";
    std::error_code ec;
    fs::create_directories(root + "library/db", ec);
    std::vector<unsigned char> u1 = noise(2352 * 30, 31), d1 = noise(2352 * 30, 32);
    writeBytes(games + "Unlisted Disc (USA) (Track 1).bin", u1);
    writeText(games + "Unlisted Disc (USA).cue",
              "FILE \"Unlisted Disc (USA) (Track 1).bin\" BINARY\n  TRACK 01 MODE1/2352\n    INDEX 01 00:00:00\n");
    writeBytes(games + "Listed Disc (USA) (Track 1).bin", d1);
    writeText(games + "Listed Disc (USA).cue",
              "FILE \"Listed Disc (USA) (Track 1).bin\" BINARY\n  TRACK 01 MODE1/2352\n    INDEX 01 00:00:00\n");
    writeBytes(games + "Image Only (USA).chd", noise(4096, 33));   // .chd: never hashed, keyed by path
    writeText(root + "library/db/Atari - Jaguar CD.dat",
              datText("2099.01.01", datGame("Listed Disc (USA)", "Listed Disc (USA) (Track 1).bin", crcOf(d1)) +
                                        datGame("F1", "x", 1) + datGame("F2", "x", 2) + datGame("F3", "x", 3) + datGame("F4", "x", 4)));
    writeText(root + "not-a-png.png", "this is not an image, just text that is long enough to pass for a file");

    Paths paths;
    paths.root = root;
    Config cfg;
    cfg.setPath(root + "config.ini");
    cfg.set("library.dir.0", games.substr(0, games.size() - 1));
    struct Fake {
        int created = 0, destroyed = 0, lastW = 0, lastH = 0;
    } fake;
    LibraryService svc;
    svc.createTexture = [&](const void*, int w, int h) { fake.created++; fake.lastW = w; fake.lastH = h; return (unsigned)fake.created; };
    svc.destroyTexture = [&](unsigned) { fake.destroyed++; };
    svc.artBaseUrl = "http://127.0.0.1:1/";   // nothing listens: any request would fail loudly
    svc.artMirrorUrl = svc.artBaseUrl;
    svc.dbBaseUrl = svc.artBaseUrl;
    svc.start(paths, cfg, false);
    svc.setDownloadArt(false);
    auto pumpUntil = [&](const std::function<bool()>& cond) {
        for (int i = 0; i < 400; i++) {
            svc.pump();
            if (cond()) return true;
            SDL_Delay(25);
        }
        return false;
    };
    auto find = [&](const std::string& title) {
        lib::Entry e;
        for (const lib::Entry& x : svc.entries())
            if (x.title == title) e = x;
        return e;   // a copy: a rescan rebuilds the list
    };
    CHECK(pumpUntil([&] { return !svc.scanning() && svc.entries().size() == 3; }));
    lib::Entry un = find("Unlisted Disc (USA)"), listed = find("Listed Disc (USA)"), chd = find("Image Only (USA)");
    CHECK(!un.file.path.empty() && !listed.file.path.empty() && !chd.file.path.empty());
    CHECK(un.datName.empty() && listed.match == lib::Match::Crc && chd.datName.empty());
    CHECK(lib::coverKey(un).rfind("crc-", 0) == 0 && lib::coverKey(chd).rfind("path-", 0) == 0);

    // nothing anywhere: Missing, for the unidentified and for the identified (downloads are off)
    ArtView v;
    CHECK(pumpUntil([&] { v = svc.art(un); return v.state == ArtView::Missing; }));
    CHECK(pumpUntil([&] { v = svc.art(listed); return v.state == ArtView::Missing; }));
    CHECK(!svc.netBusy());

    // 1. a PNG named after the game in <data>/covers/ (Rescan makes tiles look again)
    makeCoverPng("local.png", 40, 80, 10);
    fs::create_directories(fs::u8path(root + "covers"), ec);
    fs::copy_file(fs::u8path(g_tmp + "local.png"), fs::u8path(root + "covers/Unlisted Disc (USA).png"), fs::copy_options::overwrite_existing, ec);
    CHECK(!ec);
    svc.rescan();
    CHECK(pumpUntil([&] { v = svc.art(un); return v.state == ArtView::Ready; }));
    CHECK(v.w == 40 && v.h == 80);
    lib::Entry un2 = find("Unlisted Disc (USA)");   // a rescan rebuilds entries: the key is unchanged
    CHECK(pumpUntil([&] { return !svc.scanning(); }));
    un = find("Unlisted Disc (USA)");
    listed = find("Listed Disc (USA)");
    chd = find("Image Only (USA)");

    // 2. a cover set on the tile beats the folder; remove goes back to the folder's
    makeCoverPng("mine.png", 90, 30, 200);
    CHECK(!svc.hasCustomCover(un));
    CHECK(svc.setCover(un, g_tmp + "mine.png"));
    CHECK(svc.hasCustomCover(un));
    CHECK(fs::exists(fs::u8path(lib::customCoverPath(root, lib::coverKey(un)))));
    CHECK(pumpUntil([&] { v = svc.art(un); return v.state == ArtView::Ready && v.w == 90; }));
    CHECK(v.h == 30);
    int destroyedBefore = fake.destroyed;
    svc.removeCover(un);
    CHECK(!svc.hasCustomCover(un));
    CHECK(fake.destroyed > destroyedBefore);   // the old texture went away with the cover
    CHECK(pumpUntil([&] { v = svc.art(un); return v.state == ArtView::Ready && v.w == 40; }));
    CHECK(v.h == 80);
    CHECK(fs::exists(fs::u8path(root + "covers/Unlisted Disc (USA).png")));   // the user's own file is never deleted

    // 3. identified: custom > downloaded cache (downloads off, so only what is on disk counts)
    std::string cached = lib::thumbnailCachePath(root, "Named_Boxarts", listed.datName);
    makeCoverPng("cached.png", 64, 64, 50);
    fs::create_directories(fs::u8path(cached).parent_path(), ec);
    fs::copy_file(fs::u8path(g_tmp + "cached.png"), fs::u8path(cached), fs::copy_options::overwrite_existing, ec);
    CHECK(!ec);
    svc.rescan();   // the earlier Missing is forgotten
    CHECK(pumpUntil([&] { return !svc.scanning(); }));
    listed = find("Listed Disc (USA)");
    CHECK(pumpUntil([&] { v = svc.art(listed); return v.state == ArtView::Ready; }));
    CHECK(v.w == 64 && v.h == 64);   // downloaded
    makeCoverPng("tiny.png", 20, 20, 120);
    CHECK(svc.setCover(listed, g_tmp + "tiny.png"));
    CHECK(pumpUntil([&] { v = svc.art(listed); return v.state == ArtView::Ready && v.w == 20; }));
    CHECK(fs::exists(fs::u8path(lib::customCoverPath(root, "Listed Disc (USA)"))));
    svc.removeCover(listed);
    CHECK(pumpUntil([&] { v = svc.art(listed); return v.state == ArtView::Ready && v.w == 64; }));

    // 4. an image the decoder cannot read is refused and leaves nothing behind
    chd = find("Image Only (USA)");
    CHECK(!svc.setCover(chd, root + "not-a-png.png"));
    CHECK(svc.status().find("PNG") != std::string::npos);
    CHECK(!svc.hasCustomCover(chd));
    CHECK(!svc.setCover(chd, root + "does-not-exist.png"));
    CHECK(svc.setCover(chd, g_tmp + "mine.png"));   // .chd tiles are keyed by their path
    CHECK(pumpUntil([&] { v = svc.art(chd); return v.state == ArtView::Ready && v.w == 90; }));

    // 5. finding the tile under a dropped file
    svc.tiles.clear();
    svc.tilesFrame = 100;
    LibraryService::CoverTile t;
    t.x0 = 10; t.y0 = 20; t.x1 = 110; t.y1 = 160; t.key = "k1"; t.title = "T1";
    svc.tiles.push_back(t);
    t.x0 = 120; t.x1 = 220; t.key = "k2";
    svc.tiles.push_back(t);
    CHECK(svc.tileAt(50, 50, 100) && svc.tileAt(50, 50, 100)->key == "k1");
    CHECK(svc.tileAt(130, 159, 101) && svc.tileAt(130, 159, 101)->key == "k2");
    CHECK(!svc.tileAt(115, 50, 100));    // the gap between tiles
    CHECK(!svc.tileAt(50, 160, 100));    // the far edge is outside
    CHECK(!svc.tileAt(50, 50, 200));     // the grid has not been drawn for a while: nothing is under the pointer
    svc.stop();
}

// ---- disc identification: name matching, .chd and .cdi track-1 checksums --------------------------------------------

static void testLooseNames() {
    CHECK_EQ(lib::normalizeDiscName("Dragon's Lair (USA) (Track 1)"), "dragons lair");
    CHECK_EQ(lib::normalizeDiscName("Myst_(USA)_[Disc 1] - Redump"), "myst redump");
    CHECK_EQ(lib::normalizeDiscName("  Blue  Lightning!! (Europe)  "), "blue lightning");
    CHECK_EQ(lib::normalizeDiscName("Highlander - The Last of the MacLeods"), "highlander the last of the macleods");
    CHECK_EQ(lib::normalizeDiscName("(USA)"), "");
    CHECK_EQ(lib::normalizeDiscName("Unclosed (group"), "unclosed");

    lib::Dat dat;
    CHECK(dat.parse(datText("1", datGame("Myst (USA)", "a", 1) + datGame("Dragon's Lair (USA)", "b", 2) +
                                     datGame("Highlander - The Last of the MacLeods (USA)", "c", 3) +
                                     datGame("Primal Rage (USA) (Disc 1)", "d", 4) + datGame("Primal Rage (USA) (Disc 2)", "e", 5) +
                                     datGame("Space Ace (USA)", "f", 6) + datGame("Space Ace Gold (USA)", "g", 7) +
                                     datGame("Blue Lightning (USA)", "h", 8))));
    auto title = [&](const char* stem, const char* folder = "") {
        int how = 0;
        const lib::DatGame* g = dat.byLooseName(stem, folder, &how);
        return g ? g->name : std::string("-");
    };
    CHECK_EQ(title("Myst"), "Myst (USA)");                          // region and track tags do not matter
    CHECK_EQ(title("myst (europe) (track 01)"), "Myst (USA)");
    CHECK_EQ(title("Dragons Lair"), "Dragon's Lair (USA)");         // punctuation does not matter
    CHECK_EQ(title("DRAGON'S_LAIR"), "Dragon's Lair (USA)");
    CHECK_EQ(title("Space Ace"), "Space Ace (USA)");                // the exact title beats a longer one
    CHECK_EQ(title("Highlander"), "Highlander - The Last of the MacLeods (USA)");   // a unique start of a title
    CHECK_EQ(title("Primal Rage"), "-");                            // two discs share the title: no guess
    CHECK_EQ(title("Primal Rage (Disc 2)"), "-");
    CHECK_EQ(title("Space"), "-");                                  // starts two titles
    CHECK_EQ(title("Spa"), "-");                                    // too short for a prefix match
    CHECK_EQ(title("Myst Masterpiece Edition"), "-");               // longer than the dat title: not a match
    CHECK_EQ(title("disc", "Myst (USA)"), "Myst (USA)");            // the folder name when the file says nothing
    CHECK_EQ(title("disc", "Highlander"), "-");                     // a folder name must match exactly
    CHECK_EQ(title("Space Ace", "Myst"), "Space Ace (USA)");        // the file name wins over the folder
    int how = 0;
    dat.byLooseName("disc", "Myst", &how);
    CHECK(how == 2);
    dat.byLooseName("Highlander", "", &how);
    CHECK(how == 3);

    // identify(): a disc whose checksum is not in the dat is matched by name; a cart is not touched by it
    lib::ScanFile f;
    f.kind = lib::Kind::Disc;
    f.path = "/rips/Myst (USA)/disc.cue";
    f.crcWhole = 0x12345678u;   // not Redump's
    lib::Entry e = lib::identify(f, nullptr, &dat);
    CHECK(e.match == lib::Match::Name && e.datName == "Myst (USA)" && e.how == "folder name");
    f.path = "/rips/Space.chd";
    f.crcWhole = 0;
    e = lib::identify(f, nullptr, &dat);
    CHECK(e.match == lib::Match::Unknown && e.how == "not identified" && e.title == "Space");
    f.path = "/rips/x.cue";
    f.crcWhole = 6;   // the dat's crc for Space Ace
    e = lib::identify(f, nullptr, &dat);
    CHECK(e.match == lib::Match::Crc && e.datName == "Space Ace (USA)" && e.how.find("track 1") != std::string::npos);
    f.kind = lib::Kind::Cart;
    f.path = "/carts/Myst (USA).j64";
    f.crcWhole = 0;
    CHECK(lib::identify(f, &dat, nullptr).match == lib::Match::Name);   // the plain name rule
    f.path = "/carts/Highlander.j64";
    CHECK(lib::identify(f, &dat, nullptr).match == lib::Match::Unknown);   // carts do not get the loose rule
}

static void putBE(std::vector<unsigned char>& v, size_t off, uint64_t x, int bytes) {
    for (int i = 0; i < bytes; i++) v[off + (size_t)i] = (unsigned char)(x >> (8 * (bytes - 1 - i)));
}

struct SynthTrack {
    std::string type;                    // AUDIO, MODE1_RAW
    int frames = 0, pregap = 0;
    std::string pgtype = "MODE1";        // "VAUDIO" = virtual
    std::vector<unsigned char> stored;   // frames * 2352 bytes, in the order the CHD keeps them
};

// A minimal uncompressed CHD v5 for CDs: 4 frames per hunk, 2448-byte frames (the subcode is filled with 0xEE
// so a read that includes it shows), tracks padded to 4 frames, CHT2 track metadata and optionally CHSE.
static bool writeSynthChd(const std::string& path, const std::vector<SynthTrack>& tracks, bool chse) {
    const uint32_t hunkFrames = 4, hunkBytes = 2448 * hunkFrames;
    std::vector<unsigned char> frames;
    for (const SynthTrack& t : tracks) {
        uint32_t padded = ((uint32_t)t.frames + 3) / 4 * 4;
        for (uint32_t i = 0; i < padded; i++) {
            std::vector<unsigned char> fr(2448, 0xEE);
            if (i < (uint32_t)t.frames) std::memcpy(fr.data(), t.stored.data() + (size_t)i * 2352, 2352);
            else std::fill(fr.begin(), fr.begin() + 2352, 0);
            frames.insert(frames.end(), fr.begin(), fr.end());
        }
    }
    const uint32_t hunks = (uint32_t)(frames.size() / hunkBytes);
    std::vector<std::string> md;
    if (chse) md.push_back("SESSION:1");
    for (size_t i = 0; i < tracks.size(); i++) {
        char b[200];
        std::snprintf(b, sizeof b, "TRACK:%d TYPE:%s SUBTYPE:NONE FRAMES:%d PREGAP:%d PGTYPE:%s PGSUB:NONE POSTGAP:0", (int)i + 1,
                      tracks[i].type.c_str(), tracks[i].frames, tracks[i].pregap, tracks[i].pgtype.c_str());
        md.push_back(b);
    }
    const uint64_t mapOff = 124, metaOff = mapOff + (uint64_t)hunks * 4;
    uint64_t metaEnd = metaOff;
    for (const std::string& m : md) metaEnd += 16 + m.size() + 1;
    const uint64_t dataStart = (metaEnd + hunkBytes - 1) / hunkBytes * hunkBytes;
    std::vector<unsigned char> f((size_t)(dataStart + frames.size()), 0);
    std::memcpy(f.data(), "MComprHD", 8);
    putBE(f, 8, 124, 4);
    putBE(f, 12, 5, 4);
    putBE(f, 32, frames.size(), 8);
    putBE(f, 40, mapOff, 8);
    putBE(f, 48, metaOff, 8);
    putBE(f, 56, hunkBytes, 4);
    putBE(f, 60, 2448, 4);
    for (uint32_t h = 0; h < hunks; h++) putBE(f, (size_t)mapOff + h * 4, dataStart / hunkBytes + h, 4);
    uint64_t at = metaOff;
    for (size_t i = 0; i < md.size(); i++) {
        const uint32_t tag = md[i].compare(0, 7, "SESSION") == 0 ? 0x43485345u : 0x43485432u;   // CHSE / CHT2
        const uint64_t len = md[i].size() + 1, next = i + 1 < md.size() ? at + 16 + len : 0;
        putBE(f, (size_t)at, tag, 4);
        putBE(f, (size_t)at + 4, len, 4);   // flags 0, 24-bit length
        putBE(f, (size_t)at + 8, next, 8);
        std::memcpy(f.data() + at + 16, md[i].c_str(), len);
        at += 16 + len;
    }
    std::memcpy(f.data() + dataStart, frames.data(), frames.size());
    writeBytes(path, f);
    return true;
}

static std::vector<unsigned char> swapWords(std::vector<unsigned char> v) {
    for (size_t i = 0; i + 1 < v.size(); i += 2) std::swap(v[i], v[i + 1]);
    return v;
}

static void testChdAndCdiChecksums() {
    std::string dir = g_tmp + "discs/";
    std::atomic<bool> noCancel{false};

    // the Redump .bin of track 1 (an audio track: bin files hold word-swapped samples), 10 sectors
    std::vector<unsigned char> bin1 = noise(2352 * 10, 61);
    std::vector<unsigned char> data2 = noise(2352 * 5, 62);
    SynthTrack t1;
    t1.type = "AUDIO";
    t1.frames = 10;
    t1.stored = swapWords(bin1);   // the CHD keeps host-order PCM
    SynthTrack t2;
    t2.type = "MODE1_RAW";
    t2.frames = 5;
    t2.stored = data2;
    writeSynthChd(dir + "good.chd", {t1, t2}, true);
    uint32_t crc = 0;
    CHECK(lib::crcOfChdTrack1(dir + "good.chd", crc, &noCancel));
    CHECK_EQ(hex8(crc), hex8(crcOf(bin1)));   // exactly the Redump bin's CRC32

    // a virtual pregap is not part of the .bin: 3 silent frames the CHD counts but a bin does not have
    std::vector<unsigned char> real7 = noise(2352 * 7, 63);
    SynthTrack v1;
    v1.type = "AUDIO";
    v1.frames = 10;
    v1.pregap = 3;
    v1.pgtype = "VAUDIO";
    v1.stored = std::vector<unsigned char>(2352 * 3, 0);
    std::vector<unsigned char> sw = swapWords(real7);
    v1.stored.insert(v1.stored.end(), sw.begin(), sw.end());
    writeSynthChd(dir + "vpregap.chd", {v1, t2}, true);
    CHECK(lib::crcOfChdTrack1(dir + "vpregap.chd", crc, &noCancel));
    CHECK_EQ(hex8(crc), hex8(crcOf(real7)));

    // the core refuses several audio tracks with no session metadata, so it is not hashed either
    SynthTrack a2 = t1;
    writeSynthChd(dir + "nochse.chd", {t1, a2}, false);
    crc = 0xDEADBEEF;
    CHECK(!lib::crcOfChdTrack1(dir + "nochse.chd", crc, &noCancel) && crc == 0xDEADBEEF);
    writeBytes(dir + "junk.chd", noise(4096, 64));
    CHECK(!lib::crcOfChdTrack1(dir + "junk.chd", crc, &noCancel));
    CHECK(!lib::crcOfChdTrack1(dir + "missing.chd", crc, &noCancel));
    std::atomic<bool> cancelled{true};
    CHECK(!lib::crcOfChdTrack1(dir + "good.chd", crc, &cancelled));

    // .cdi (V3.5): track 1 data after its 2-sector pregap, then the descriptor and the trailer
    {
        std::vector<unsigned char> img = noise(2352 * 2, 65);   // pregap sectors
        img.insert(img.end(), bin1.begin(), bin1.end());
        std::vector<unsigned char> hdr;
        auto u16 = [&](unsigned x) { hdr.push_back((unsigned char)x); hdr.push_back((unsigned char)(x >> 8)); };
        auto u32 = [&](uint32_t x) { u16(x & 0xFFFF); u16(x >> 16); };
        u16(1);   // sessions
        u16(1);   // tracks in session 1
        u32(0);   // not the extended format
        const unsigned char marker[20] = {0, 0, 1, 0, 0, 0, 0xFF, 0xFF, 0xFF, 0xFF, 0, 0, 1, 0, 0, 0, 0xFF, 0xFF, 0xFF, 0xFF};
        hdr.insert(hdr.end(), marker, marker + 20);
        hdr.insert(hdr.end(), 4, 0);
        hdr.push_back(0);                // empty file name
        hdr.insert(hdr.end(), 19, 0);
        u32(0);
        hdr.insert(hdr.end(), 2, 0);     // index count
        std::vector<unsigned char> td(0x57, 0);
        auto put32 = [&](size_t o, uint32_t x) { for (int i = 0; i < 4; i++) td[o + (size_t)i] = (unsigned char)(x >> (8 * i)); };
        put32(0x00, 2);    // pregap
        put32(0x04, 10);   // length
        put32(0x0e, 0);    // mode: audio
        put32(0x22, 12);
        put32(0x36, 2);    // 2352-byte sectors
        hdr.insert(hdr.end(), td.begin(), td.end());
        hdr.insert(hdr.end(), 64, 0);    // the rest of the descriptor
        std::vector<unsigned char> file = img;
        const size_t hdrAt = file.size();
        file.insert(file.end(), hdr.begin(), hdr.end());
        const uint32_t total = (uint32_t)file.size() + 8;
        for (int i = 0; i < 4; i++) file.push_back((unsigned char)(0x80000006u >> (8 * i)));
        const uint32_t fromEnd = total - (uint32_t)hdrAt;
        for (int i = 0; i < 4; i++) file.push_back((unsigned char)(fromEnd >> (8 * i)));
        writeBytes(dir + "good.cdi", file);
        crc = 0;
        CHECK(lib::crcOfCdiTrack1(dir + "good.cdi", crc, &noCancel));
        CHECK_EQ(hex8(crc), hex8(crcOf(bin1)));
        writeBytes(dir + "junk.cdi", noise(8192, 66));
        CHECK(!lib::crcOfCdiTrack1(dir + "junk.cdi", crc, &noCancel));
    }

    // the scanner hashes both, identify() matches them to the dat by checksum, and the cache keeps the result
    lib::Dat discs;
    CHECK(discs.parse(datText("1", datGame("Synth Audio One (USA)", "Synth Audio One (USA) (Track 1).bin", crcOf(bin1)) +
                                       datGame("Other (USA)", "o", 9))));
    lib::ScanOptions opt;
    opt.folders = {dir};
    std::vector<lib::ScanFile> files = lib::scanFolders(opt, lib::ScanCache(), nullptr, nullptr);
    int hashed = 0;
    for (const lib::ScanFile& sf : files) {
        const std::string name = fs::path(sf.path).filename().string();
        lib::Entry e = lib::identify(sf, nullptr, &discs);
        if (name == "good.chd" || name == "good.cdi") {
            CHECK(sf.crcWhole == crcOf(bin1) && e.match == lib::Match::Crc && e.datName == "Synth Audio One (USA)");
            hashed++;
        } else {
            CHECK(sf.crcWhole == 0 || name == "vpregap.chd");
            CHECK(e.match != lib::Match::Crc);
        }
    }
    CHECK(hashed == 2);
    lib::ScanCache cache = lib::parseCache(lib::serializeCache(files));
    CHECK(cache.size() == files.size());
    for (const lib::ScanFile& sf : files)
        if (sf.path.size() > 8 && sf.path.substr(sf.path.size() - 8) == "good.chd")
            CHECK(cache[sf.path].crcWhole == sf.crcWhole && cache[sf.path].hashed);

    // an old (VJLIB1) cache: disc images without a checksum are hashed again, carts and hashed discs are kept
    std::string old = "VJLIB1\n/x/a.chd\tD\t10\t5\t00000000\t00000000\n/x/b.cue\tD\t10\t5\t0000ABCD\t00000000\n/x/c.j64\tC\t10\t5\t00000000\t00000000\n";
    lib::ScanCache oc = lib::parseCache(old);
    CHECK(oc.size() == 3 && !oc["/x/a.chd"].hashed && oc["/x/b.cue"].hashed && oc["/x/c.j64"].hashed);
    lib::ScanCache nc = lib::parseCache("VJLIB2\n/x/a.chd\tD\t10\t5\t00000000\t00000000\n");
    CHECK(nc.size() == 1 && nc["/x/a.chd"].hashed);   // an unhashable image is remembered as such
}

// ---- dev tool -------------------------------------------------------------------------------------------------------

// .m3u multi-disc playlists (m3u.h): the parser, and the scan listing the playlist in place of its discs.
static void testM3u() {
    // parsing: comments, blank lines, a BOM, CRLF, "|label", Windows separators, absolute entries
    std::vector<std::string> e = parseM3u("\xEF\xBB\xBF#EXTM3U\r\n\r\n#EXTINF:0,Disc 1\r\nGame (Disc 1).cue\r\n"
                                          "  sub\\Game (Disc 2).chd|Second disc  \r\n/abs/Game (Disc 3).cdi\nC:/win/d4.chd\n# end",
                                          "/set/");
    CHECK(e.size() == 4);
    if (e.size() == 4) {
        CHECK_EQ(e[0], std::string("/set/Game (Disc 1).cue"));
        CHECK_EQ(e[1], std::string("/set/sub/Game (Disc 2).chd"));
        CHECK_EQ(e[2], std::string("/abs/Game (Disc 3).cdi"));
        CHECK_EQ(e[3], std::string("C:/win/d4.chd"));
    }
    CHECK(parseM3u("", "/x/").empty() && parseM3u("#EXTM3U\n\n", "/x/").empty());
    CHECK(lib::isDiscExt("m3u"));

    // scanning: a cue set + a chd listed by one playlist, a chd-first playlist, and a loose chd no playlist names
    std::string dir = g_tmp + "m3u/";
    std::atomic<bool> noCancel{false};
    std::vector<unsigned char> d1t1 = noise(2352 * 12, 71);
    writeBytes(dir + "Set (Disc 1) (Track 1).bin", d1t1);
    writeText(dir + "Set (Disc 1).cue", "FILE \"Set (Disc 1) (Track 1).bin\" BINARY\n  TRACK 01 MODE1/2352\n    INDEX 01 00:00:00\n");
    std::vector<unsigned char> pcm = noise(2352 * 10, 72);
    SynthTrack t;
    t.type = "AUDIO";
    t.frames = 10;
    t.stored = swapWords(pcm);
    writeSynthChd(dir + "discs/Set (Disc 2).chd", {t}, true);
    writeText(dir + "Set.m3u", "#EXTM3U\nSet (Disc 1).cue\ndiscs\\Set (Disc 2).chd\n");
    writeText(dir + "Chd First.m3u", "discs/Set (Disc 2).chd\n");
    writeSynthChd(dir + "Loose.chd", {t}, true);
    lib::ScanOptions opt;
    opt.folders = {dir};
    std::vector<lib::ScanFile> files = lib::scanFolders(opt, lib::ScanCache(), &noCancel, nullptr);
    std::map<std::string, lib::ScanFile> by;
    for (const lib::ScanFile& f : files) by[fs::path(f.path).filename().string()] = f;
    CHECK(by.count("Set.m3u") && by["Set.m3u"].kind == lib::Kind::Disc);
    CHECK_EQ(hex8(by["Set.m3u"].crcWhole), hex8(crcOf(d1t1)));   // identified by its first disc's track 1
    CHECK(by.count("Chd First.m3u") && by["Chd First.m3u"].kind == lib::Kind::Disc);
    CHECK_EQ(hex8(by["Chd First.m3u"].crcWhole), hex8(crcOf(pcm)));
    CHECK(!by.count("Set (Disc 1).cue") && !by.count("Set (Disc 2).chd"));   // the playlists stand for them
    CHECK(!by.count("Set (Disc 1) (Track 1).bin"));
    CHECK(by.count("Loose.chd"));
    CHECK(files.size() == 3);

    // A playlist is all-or-nothing at load time. It must not claim playable files before every entry is validated.
    const char* bad[] = {"Missing.chd", "Not a disc.j64", "Nested.m3u", "Directory.chd"};
    for (size_t i = 0; i < 4; ++i) {
        std::string broken = g_tmp + "invalid-playlist-" + std::to_string(i) + "/";
        writeBytes(broken + "Keep.bin", d1t1);
        writeText(broken + "Keep.cue", "FILE \"Keep.bin\" BINARY\n TRACK 01 MODE1/2352\n INDEX 01 00:00:00\n");
        writeBytes(broken + "Not a disc.j64", noise(1024, 99));
        fs::create_directories(fs::u8path(broken + "Directory.chd"));
        // Only the nested-playlist case has another valid playlist: make that one invalid too.
        writeText(broken + "Nested.m3u", "Missing.chd\n");
        writeText(broken + "Broken.m3u", std::string("Keep.cue\n") + bad[i] + "\n");
        opt.folders = {broken};
        auto invalid = lib::scanFolders(opt, lib::ScanCache(), nullptr, nullptr);
        bool keptDisc = false, keptCart = false;
        for (const auto& f : invalid) {
            if (pathStem(f.path) == "Keep") keptDisc = true;
            if (pathStem(f.path) == "Not a disc") keptCart = true;
        }
        CHECK(keptDisc);
        CHECK(keptCart);
    }
}

static void testPlaylistStateOwners() {
    const std::string root = g_tmp + "playlist-states/", games = root + "games/";
    const std::string playlist = fs::u8path(games + "Game.m3u").lexically_normal().u8string();
    for (int disc = 1; disc <= 2; ++disc) {
        const std::string stem = disc == 1 ? "Game (Disc 1)" : "Game";
        writeBytes(games + stem + ".bin", noise(2352 * 8, 90 + disc));
        writeText(games + stem + ".cue", "FILE \"" + stem + ".bin\" BINARY\n TRACK 01 MODE1/2352\n INDEX 01 00:00:00\n");
        writeText(root + "states/" + stem + ".slot" + std::string("0.state"), "fixture");
    }
    writeText(games + "Game.m3u", "Game (Disc 1).cue\nGame.cue\n");
    Paths paths;
    paths.root = root;
    paths.states = root + "states/";
    Config cfg;
    cfg.set("library.dir.0", games);
    LibraryService svc;
    svc.artBaseUrl = svc.artMirrorUrl = svc.dbBaseUrl = "http://127.0.0.1:1/";
    svc.start(paths, cfg, false);
    svc.setDownloadArt(false);
    for (int i = 0; i < 400; ++i) {
        svc.pump();
        if (!svc.scanning() && svc.entries().size() == 1) break;
        SDL_Delay(25);
    }
    CHECK(svc.entries().size() == 1); // disc aliases must not reappear as duplicate tiles
    CHECK(svc.stateGames().size() == 2);
    if (svc.entries().size() == 1) {
        const auto* all = svc.statesFor(svc.entries()[0]);
        CHECK(all != nullptr && all->states.size() == 2);
        if (all) {
            for (const auto& state : all->states) {
                CHECK(!state.disc.empty() && state.slot == 0); // equal slots on different discs stay distinct
                svc.launchState(svc.entries()[0], state.slot, state.disc);
                SDL_PumpEvents();
                SDL_Event ev;
                const int got = SDL_PeepEvents(&ev, 1, SDL_GETEVENT, SDL_EVENT_USER, SDL_EVENT_USER);
                CHECK(got == 1 && ev.user.code == 0x56414A50);
                if (got == 1 && ev.user.code == 0x56414A50) {
                    std::unique_ptr<PlaylistStateLaunch> request((PlaylistStateLaunch*)ev.user.data1);
                    CHECK_EQ(request->playlist, playlist);
                    CHECK_EQ(request->disc, state.disc);
                    CHECK((int)(intptr_t)ev.user.data2 - 1000 == 0);
                } else if (got == 1 && ev.user.code == 0x56414A53) SDL_free(ev.user.data1);
            }
        }
    }
    for (const auto& group : svc.stateGames()) {
        const auto* owner = svc.entryForState(group);
        CHECK(owner != nullptr);
        if (owner) {
            CHECK_EQ(pathStem(owner->file.path), group.stem);
            CHECK_EQ(owner->statePlaylist, playlist);
            CHECK(svc.statesFor(*owner) != nullptr);
        }
    }
    svc.stop();
    svc.start(paths, cfg, false); // warm hash cache: associations must exist before the next scan completes
    CHECK(svc.entries().size() == 1);
    if (svc.entries().size() == 1) {
        const auto* all = svc.statesFor(svc.entries()[0]);
        CHECK(all != nullptr && all->states.size() == 2);
    }
    svc.stop();
}

static int scanTool(const std::string& dir, const std::string& dbDir) {
    lib::Dat carts, discs;
    bool haveCarts = carts.load(dbDir + "/Atari - Jaguar.dat");
    bool haveDiscs = discs.load(dbDir + "/Atari - Jaguar CD.dat");
    lib::ScanOptions opt;
    opt.folders = {dir};
    std::vector<lib::ScanFile> files = lib::scanFolders(opt, lib::ScanCache(), nullptr, nullptr);
    size_t nCart = 0, nDisc = 0, crc = 0, name = 0, unk = 0, cartCrc = 0, discCrc = 0, discNoHash = 0;
    for (const lib::ScanFile& f : files) {
        lib::Entry e = lib::identify(f, haveCarts ? &carts : nullptr, haveDiscs ? &discs : nullptr);
        (f.kind == lib::Kind::Cart ? nCart : nDisc)++;
        if (f.kind == lib::Kind::Disc && f.crcWhole == 0) discNoHash++;
        if (e.match == lib::Match::Crc) {
            crc++;
            (f.kind == lib::Kind::Cart ? cartCrc : discCrc)++;
        } else if (e.match == lib::Match::Name) {
            name++;
        } else {
            unk++;
        }
    }
    std::printf("dat: carts %s (%zu)  discs %s (%zu)\n", haveCarts ? "loaded" : "missing", carts.size(),
                haveDiscs ? "loaded" : "missing", discs.size());
    std::printf("files: %zu  (cartridges %zu, disc images %zu, of which %zu have no checksum: chd/cdi/missing bin)\n",
                files.size(), nCart, nDisc, discNoHash);
    std::printf("identified by CRC: %zu (cartridges %zu, discs %zu)\n", crc, cartCrc, discCrc);
    std::printf("matched by file name only: %zu\n", name);
    std::printf("unidentified: %zu\n", unk);
    return 0;
}


// ---- favorites, recently played, display options, tabs, save states (the library's personal lists) -----------

static lib::Entry fakeEntry(const std::string& path, const std::string& title, uint32_t crc, const std::string& datName = "",
                            lib::Kind kind = lib::Kind::Cart) {
    lib::Entry e;
    e.file.path = path;
    e.file.kind = kind;
    e.file.crcWhole = crc;
    e.title = title;
    e.datName = datName;
    e.section = kind == lib::Kind::Disc ? lib::Section::Disc : lib::Section::Cart;
    return e;
}

static void testDisplayOptionsAndTabs() {
    using namespace lib;
    CHECK(!showsTab(ShowHidden) && !showsStrip(ShowHidden));
    CHECK(showsTab(ShowTab) && !showsStrip(ShowTab));
    CHECK(!showsTab(ShowSections) && showsStrip(ShowSections));
    CHECK(showsTab(ShowBoth) && showsStrip(ShowBoth));
    CHECK(clampShowAs(7, ShowBoth) == ShowBoth && clampShowAs(-1, ShowTab) == ShowTab && clampShowAs(0, ShowBoth) == ShowHidden);
    CHECK(kFavoritesDefault == ShowBoth && kRecentsDefault == ShowBoth);
    for (int m = 0; m < kShowAsCount; m++) CHECK(std::strlen(showAsLabel(m)) > 0);

    // tab order: Cartridges, Jaguar CD, [Favorites], [Recently played], Save states, BIOS & system
    std::vector<int> v = visibleTabs(ShowBoth, ShowBoth);
    CHECK(v == std::vector<int>({TabCart, TabDisc, TabFavorites, TabRecents, TabStates, TabBios}));
    v = visibleTabs(ShowSections, ShowHidden);   // strips only: no tab for either
    CHECK(v == std::vector<int>({TabCart, TabDisc, TabStates, TabBios}));
    v = visibleTabs(ShowHidden, ShowTab);
    CHECK(v == std::vector<int>({TabCart, TabDisc, TabRecents, TabStates, TabBios}));
    // the remembered tab that is no longer shown falls back to Cartridges; the others stay
    CHECK(effectiveTab(TabFavorites, ShowHidden, ShowBoth) == TabCart);
    CHECK(effectiveTab(TabFavorites, ShowSections, ShowBoth) == TabCart);
    CHECK(effectiveTab(TabFavorites, ShowTab, ShowHidden) == TabFavorites);
    CHECK(effectiveTab(TabRecents, ShowBoth, ShowTab) == TabRecents);
    CHECK(effectiveTab(TabBios, ShowHidden, ShowHidden) == TabBios && effectiveTab(TabStates, ShowHidden, ShowHidden) == TabStates);
    CHECK(effectiveTab(99, ShowBoth, ShowBoth) == TabCart && effectiveTab(-3, ShowBoth, ShowBoth) == TabCart);
    CHECK_EQ(tabName(TabRecents), "Recently played");
    // the strips sit on the two media tabs only, and only when the option asks for them
    CHECK(stripOnTab(TabCart, ShowBoth) && stripOnTab(TabDisc, ShowSections));
    CHECK(!stripOnTab(TabCart, ShowTab) && !stripOnTab(TabDisc, ShowHidden));
    CHECK(!stripOnTab(TabFavorites, ShowBoth) && !stripOnTab(TabBios, ShowBoth) && !stripOnTab(TabStates, ShowBoth));
}

static void testPersonalListsLogic() {
    using namespace lib;
    std::vector<Entry> entries;
    entries.push_back(fakeEntry("/g/Alpha.j64", "Alpha", 0xA1A1A1A1u));
    entries.push_back(fakeEntry("/g/Beta.j64", "beta", 0xB2B2B2B2u, "Beta (World)"));
    entries.push_back(fakeEntry("/g/Gamma.cue", "Gamma", 0xC3C3C3C3u, "", Kind::Disc));
    entries.push_back(fakeEntry("/g/Delta.chd", "Delta", 0, "", Kind::Disc));
    Entry bios = fakeEntry("/sys/jagboot.rom", "Series K boot ROM", 0xD4D4D4D4u);
    bios.section = Section::Bios;
    entries.push_back(bios);

    UserLists l;
    UserItem a = userItemFor(entries[0], 100), b = userItemFor(entries[1], 101), g = userItemFor(entries[2], 102);
    CHECK_EQ(a.key, "crc-A1A1A1A1");
    CHECK_EQ(b.key, "Beta (World)");
    CHECK(userItemFor(entries[3], 0).key.rfind("path-", 0) == 0);

    // favorites: add, no duplicates, remove
    setFavoriteIn(l, b, true);
    setFavoriteIn(l, a, true);
    setFavoriteIn(l, b, true);
    CHECK(l.favorites.size() == 2 && l.favorites[0].path == "/g/Beta.j64" && l.favorites[1].path == "/g/Alpha.j64");
    CHECK(hasFavorite(l, a) && hasFavorite(l, b) && !hasFavorite(l, g));
    CHECK(l.favorites[0].when == 101);   // re-adding keeps the original date and place
    setFavoriteIn(l, a, false);
    CHECK(l.favorites.size() == 1 && !hasFavorite(l, a));
    setFavoriteIn(l, a, false);   // removing what is not there is fine
    CHECK(l.favorites.size() == 1);

    // recents: newest first, deduplicated, capped
    recordPlayIn(l, a);
    recordPlayIn(l, b);
    recordPlayIn(l, g);
    CHECK(l.recents.size() == 3 && l.recents[0].path == "/g/Gamma.cue" && l.recents[2].path == "/g/Alpha.j64");
    recordPlayIn(l, a);   // played again: moves to the front, not listed twice
    CHECK(l.recents.size() == 3 && l.recents[0].path == "/g/Alpha.j64" && l.recents[1].path == "/g/Gamma.cue");
    removeRecentIn(l, g);
    CHECK(l.recents.size() == 2 && !hasRecent(l, g) && hasRecent(l, a));
    UserLists many;
    for (int i = 0; i < 30; i++) {
        UserItem it;
        it.path = "/x/" + std::to_string(i);
        it.when = i;
        recordPlayIn(many, it);
    }
    CHECK(many.recents.size() == kMaxRecentGames && many.recents.front().path == "/x/29" && many.recents.back().path == "/x/10");
    CHECK(kMaxRecentGames == 20);

    // the file round-trips, tabs and newlines in names cannot break the format, junk lines are skipped
    UserItem odd = a;
    odd.title = "Tab\there\nand a line";
    odd.path = "/g/odd\tname.j64";
    UserLists rt = l;
    rt.favorites.push_back(odd);
    UserLists back = parseUserLists(serializeUserLists(rt));
    CHECK(back.favorites.size() == 2 && back.recents.size() == 2);
    CHECK(back.favorites[0].path == "/g/Beta.j64" && back.favorites[0].crc == 0xB2B2B2B2u && back.favorites[0].key == "Beta (World)");
    CHECK(back.favorites[1].title == "Tab here and a line" && back.favorites[1].path == "/g/odd name.j64");
    CHECK(back.recents[0].when == rt.recents[0].when);
    UserLists junk = parseUserLists("garbage\nfav\tnot enough\n\n# comment\nfav\t5\t0000000A\tk\t/p\tt\r\nrec\t6\tzz\t\t\t\nwho\t1\t1\tk\tp\tt\n");
    CHECK(junk.favorites.size() == 1 && junk.favorites[0].crc == 0xA && junk.favorites[0].title == "t" && junk.recents.empty());

    // resolving against the library: by path, by checksum when the folder moved, by key when the file changed
    UserLists r;
    setFavoriteIn(r, userItemFor(entries[1], 1), true);   // Beta
    setFavoriteIn(r, userItemFor(entries[0], 2), true);   // Alpha
    UserItem moved = userItemFor(entries[2], 3);          // Gamma, now somewhere else
    moved.path = "/old-folder/Gamma.cue";
    setFavoriteIn(r, moved, true);
    UserItem gone;
    gone.path = "/unplugged/Epsilon.j64";
    gone.crc = 0xE5E5E5E5u;
    gone.key = "crc-E5E5E5E5";
    setFavoriteIn(r, gone, true);
    std::vector<Resolved> res = resolveUserItems(r.favorites, entries);
    CHECK(res.size() == 3);   // Epsilon's drive is away: skipped, not forgotten
    CHECK(r.favorites.size() == 4);
    sortByTitle(res);
    CHECK(res.size() == 3 && res[0].entry->title == "Alpha" && res[1].entry->title == "beta" && res[2].entry->title == "Gamma");
    // a game whose handles all changed is a different game
    UserItem stranger;
    stranger.path = "/else/X.j64";
    stranger.crc = 0x99999999u;
    stranger.key = "X";
    CHECK(!sameGame(stranger, a) && sameGame(moved, userItemFor(entries[2], 0)));
    // BIOS files never resolve, even when listed
    std::vector<UserItem> biosItem;
    biosItem.push_back(userItemFor(bios, 1));
    CHECK(resolveUserItems(biosItem, entries).empty());
    // the same game twice in the list (an old and a new handle) shows once
    std::vector<UserItem> twice;
    twice.push_back(userItemFor(entries[0], 1));
    UserItem again = userItemFor(entries[0], 2);
    again.path = "/elsewhere/Alpha.j64";
    twice.push_back(again);
    CHECK(resolveUserItems(twice, entries).size() == 1);
    // strips: only the entries of that media
    res = resolveUserItems(r.favorites, entries);
    CHECK(filterBySection(res, Section::Cart).size() == 2 && filterBySection(res, Section::Disc).size() == 1);
    CHECK(filterBySection(res, Section::Bios).empty());

    // a favorite made while the game was unidentified (key crc-XXXX) is still a favorite once the database names it
    Entry unid = fakeEntry("/g/Zeta.j64", "Zeta", 0x2E7A2E7Au);
    UserLists dr;
    setFavoriteIn(dr, userItemFor(unid, 1), true);
    CHECK_EQ(userItemFor(unid, 0).key, "crc-2E7A2E7A");
    Entry ident = unid;
    ident.datName = "Zeta (USA)";
    ident.title = "Zeta (USA)";
    CHECK(hasFavorite(dr, userItemFor(ident, 0)));      // the checksum and the path still match
    UserItem renamedFile = userItemFor(ident, 0);
    renamedFile.path = "/moved/Zeta.j64";
    CHECK(hasFavorite(dr, renamedFile));                  // checksum alone
    renamedFile.crc = 0;                                  // an entry with no usable checksum: the key was the only handle
    UserLists kr;
    UserItem keyed = userItemFor(fakeEntry("/g/Img.chd", "Img", 0, "Img (USA)", Kind::Disc), 1);
    setFavoriteIn(kr, keyed, true);
    UserItem keyed2 = keyed;
    keyed2.path = "/moved/Img.chd";
    CHECK(hasFavorite(kr, keyed2));                       // dat name alone
    setFavoriteIn(dr, userItemFor(ident, 5), true);       // starring it again after the update changes nothing
    CHECK(dr.favorites.size() == 1 && dr.favorites[0].key == "Zeta (USA)");
}

static void testUserStoreAndInstances() {
    using namespace lib;
    const std::string dir = g_tmp + "userstore/";
    std::error_code ec;
    fs::create_directories(fs::u8path(dir), ec);
    const std::string file = dir + "user.tsv";
    UserItem a, b, c;
    a.path = "/g/A.j64"; a.crc = 0xA; a.key = "crc-0000000A"; a.title = "A";
    b.path = "/g/B.j64"; b.crc = 0xB; b.key = "crc-0000000B"; b.title = "B";
    c.path = "/g/C.j64"; c.crc = 0xC; c.key = "crc-0000000C"; c.title = "C";

    UserStore one, two;   // two running copies on one data folder
    one.open(file);
    two.open(file);
    CHECK(one.lists().favorites.empty() && !fs::exists(fs::u8path(file)));   // reading creates nothing
    CHECK(one.setFavorite(a, true));
    CHECK(fs::exists(fs::u8path(file)));
    // the second copy has not looked yet; it acts anyway, on top of what the file holds, not on its own snapshot
    CHECK(two.lists().favorites.empty());
    CHECK(two.setFavorite(b, true));
    CHECK(two.lists().favorites.size() == 2);   // it took copy one's favorite first
    CHECK(two.recordPlay(c));
    // copy one sees all of it after its next refresh, and refresh reports a change exactly once
    CHECK(one.lists().favorites.size() == 1);
    unsigned g0 = one.generation();
    CHECK(one.refresh());
    CHECK(one.generation() != g0);
    CHECK(one.lists().favorites.size() == 2 && one.lists().recents.size() == 1 && one.lists().recents[0].path == "/g/C.j64");
    CHECK(!one.refresh());
    // removing in one copy is not undone by the other copy's next unrelated action
    CHECK(one.setFavorite(a, false));
    CHECK(two.recordPlay(a));   // two still believes A is a favorite; it only records a play
    CHECK(two.lists().favorites.size() == 1 && two.lists().favorites[0].path == "/g/B.j64");
    CHECK(two.lists().recents.size() == 2 && two.lists().recents[0].path == "/g/A.j64");
    // a copy that restarts sees the same lists
    UserStore three;
    three.open(file);
    CHECK(three.lists().favorites.size() == 1 && three.lists().recents.size() == 2);
    CHECK(three.removeRecent(c));
    CHECK(three.lists().recents.size() == 1);
    CHECK(three.clearRecents());
    CHECK(three.lists().recents.empty() && three.lists().favorites.size() == 1);   // clearing recents leaves favorites
    CHECK(one.refresh() && one.lists().recents.empty());

    // a file that cannot be read is never overwritten: simulate by a directory standing where the file is
    const std::string bad = dir + "dir-as-file.tsv";
    fs::create_directories(fs::u8path(bad), ec);
    UserStore broken;
    broken.open(bad);
    CHECK(!broken.setFavorite(a, true));
    CHECK(fs::is_directory(fs::u8path(bad)));
    // not opened at all (no data folder): every operation is a harmless no
    UserStore none;
    CHECK(!none.isOpen() && !none.setFavorite(a, true) && !none.refresh() && !none.recordPlay(a));
    // a hand-edited file with a blank / missing list is fine
    writeText(dir + "empty.tsv", "");
    UserStore e;
    e.open(dir + "empty.tsv");
    CHECK(e.lists().favorites.empty() && e.setFavorite(a, true) && e.lists().favorites.size() == 1);
}

static void testStateNamesAndListing() {
    using namespace lib;
    std::string stem;
    int slot = -1;
    CHECK_EQ(stateFileName("Iron Soldier (World)", 3), "Iron Soldier (World).slot3.state");
    CHECK_EQ(thumbFileName("Iron Soldier (World)", 3), "Iron Soldier (World).slot3.png");
    CHECK(parseStateFileName("Iron Soldier (World).slot3.state", stem, slot) && stem == "Iron Soldier (World)" && slot == 3);
    CHECK(parseStateFileName("a.b.slot12.state", stem, slot) && stem == "a.b" && slot == 12);
    CHECK(!parseStateFileName("x.slot3.png", stem, slot));            // the screenshot
    CHECK(!parseStateFileName("x.slot3.state.rcp", stem, slot));      // the achievements sidecar
    CHECK(!parseStateFileName(".slot3.state", stem, slot));           // no game name
    CHECK(!parseStateFileName("x.slotA.state", stem, slot) && !parseStateFileName("x.slot.state", stem, slot));
    CHECK(!parseStateFileName("x.state", stem, slot) && !parseStateFileName("notes.txt", stem, slot));

    const std::string dir = g_tmp + "states/";
    std::error_code ec;
    fs::create_directories(fs::u8path(dir), ec);
    CHECK(listStates(dir).empty());
    CHECK(listStates(g_tmp + "no-such-states-dir/").empty());
    makeCoverPng("shot.png", 32, 24, 5);
    auto put = [&](const std::string& st, int sl, int ageMin, bool thumb) {
        writeText(dir + stateFileName(st, sl), "STATE" + st + std::to_string(sl));
        fs::last_write_time(fs::u8path(dir + stateFileName(st, sl)),
                            fs::file_time_type::clock::now() - std::chrono::minutes(ageMin), ec);
        if (thumb) fs::copy_file(fs::u8path(g_tmp + "shot.png"), fs::u8path(dir + thumbFileName(st, sl)), fs::copy_options::overwrite_existing, ec);
    };
    put("Cybermorph (USA)", 0, 600, true);
    put("Cybermorph (USA)", 4, 30, true);
    put("Cybermorph (USA)", 7, 90, false);       // a state with no screenshot
    put("Tempest 2000 (World)", 1, 5, true);
    put("Old Game", 2, 5000, false);
    writeText(dir + "Cybermorph (USA).slot4.state.rcp", "side");   // sidecar: not a state of its own
    writeText(dir + "readme.txt", "x");
    writeText(dir + "Stray.slot1.png", "x");                       // a screenshot with no state: not listed
    std::vector<StateGame> g = listStates(dir);
    CHECK(g.size() == 3);
    CHECK(g.size() == 3 && g[0].stem == "Tempest 2000 (World)" && g[1].stem == "Cybermorph (USA)" && g[2].stem == "Old Game");   // newest first
    CHECK(g.size() == 3 && g[1].states.size() == 3);
    if (g.size() == 3 && g[1].states.size() == 3) {
        CHECK(g[1].states[0].slot == 4 && g[1].states[1].slot == 7 && g[1].states[2].slot == 0);   // newest first within a game
        CHECK(!g[1].states[0].thumbPath.empty() && g[1].states[1].thumbPath.empty());
        CHECK(g[1].states[0].path == dir + "Cybermorph (USA).slot4.state" && g[1].states[0].size > 0 && g[1].states[0].mtime > 0);
        CHECK(g[1].newest == g[1].states[0].mtime);
    }

    // which library entry owns a stem: the file stem, BIOS files excluded
    std::vector<Entry> entries;
    entries.push_back(fakeEntry("/g/Tempest 2000 (World).j64", "Tempest 2000", 1));
    entries.push_back(fakeEntry("/g/Cybermorph (USA).cue", "Cybermorph", 2, "", Kind::Disc));
    Entry bios = fakeEntry("/sys/Old Game.rom", "Old Game", 3);
    bios.section = Section::Bios;
    entries.push_back(bios);
    CHECK(entryForStem(entries, "Cybermorph (USA)") == &entries[1] && entryForStem(entries, "Tempest 2000 (World)") == &entries[0]);
    CHECK(entryForStem(entries, "Old Game") == nullptr && entryForStem(entries, "Nope") == nullptr);

    // delete: the state, its screenshot and the achievements sidecar go; the others stay
    StateInfo victim = g[1].states[0];
    CHECK(deleteState(victim));
    CHECK(!fs::exists(fs::u8path(victim.path)) && !fs::exists(fs::u8path(victim.thumbPath)));
    CHECK(!fs::exists(fs::u8path(dir + "Cybermorph (USA).slot4.state.rcp")));
    CHECK(fs::exists(fs::u8path(dir + "Cybermorph (USA).slot7.state")) && fs::exists(fs::u8path(dir + "readme.txt")));
    CHECK(!deleteState(victim));   // already gone
    g = listStates(dir);
    CHECK(g.size() == 3 && g[1].stem == "Cybermorph (USA)" && g[1].states.size() == 2);

    // Show in Finder / Explorer: the folder's file:// URL
    CHECK_EQ(folderUrlOf("/Users/me/Jaguar Games/Iron Soldier.j64"), "file:///Users/me/Jaguar%20Games/");
    CHECK_EQ(folderUrlOf("/g/Dragon's Lair (USA).cue"), "file:///g/");
    CHECK_EQ(folderUrlOf("C:\\Games\\Jag #1\\x.j64"), "file:///C:/Games/Jag%20%231/");
    CHECK_EQ(folderUrlOf("/g/caf\xC3\xA9/x.j64"), "file:///g/caf%C3%A9/");
    CHECK_EQ(folderUrlOf("x.j64"), "");
}


// ---- resume where I left off: the policy, the auto-save slot's names and where each running copy keeps it --------

static void testResumePolicy() {
    using namespace lib;
    CHECK(kResumeDefault == ResumeAsk && kResumeModeCount == 3 && kAutoSaveMinSeconds >= 3.0 && kAutoSaveMinSeconds <= 10.0);
    CHECK(clampResumeMode(0) == ResumeOff && clampResumeMode(2) == ResumeAlways && clampResumeMode(3) == ResumeAsk && clampResumeMode(-1) == ResumeAsk);
    CHECK_EQ(resumeModeLabel(ResumeAlways), "Always");

    // writing: only with the option on, past the minimum runtime, and not in hardcore / netplay / after a crash
    const double ok = kAutoSaveMinSeconds + 1.0;
    CHECK(shouldAutoSave(ResumeAsk, false, false, ok, false));
    CHECK(shouldAutoSave(ResumeAlways, false, false, ok, false));
    CHECK(!shouldAutoSave(ResumeOff, false, false, ok, false));                       // Off writes nothing
    CHECK(!shouldAutoSave(ResumeAsk, true, false, ok, false));                        // hardcore refuses state loads: skip
    CHECK(!shouldAutoSave(ResumeAlways, true, false, 9999.0, false));
    CHECK(!shouldAutoSave(ResumeAsk, false, true, ok, false));                        // netplay: a state would desync the link
    CHECK(!shouldAutoSave(ResumeAsk, false, false, kAutoSaveMinSeconds - 0.01, false));   // ran too briefly
    CHECK(shouldAutoSave(ResumeAsk, false, false, kAutoSaveMinSeconds, false));       // exactly the minimum counts
    CHECK(!shouldAutoSave(ResumeAsk, false, false, 0.0, false));
    CHECK(!shouldAutoSave(ResumeAsk, false, false, ok, true));                        // the crash watchdog flagged the run

    // booting: Always loads, Ask asks, Off and anything blocked does nothing; no auto-save means nothing to do
    CHECK(resumeAction(ResumeAlways, false, false, true) == ResumeAction::Load);
    CHECK(resumeAction(ResumeAsk, false, false, true) == ResumeAction::Ask);
    CHECK(resumeAction(ResumeOff, false, false, true) == ResumeAction::None);
    CHECK(resumeAction(ResumeAlways, true, false, true) == ResumeAction::None);       // hardcore: skip, and do not prompt
    CHECK(resumeAction(ResumeAsk, true, false, true) == ResumeAction::None);
    CHECK(resumeAction(ResumeAlways, false, true, true) == ResumeAction::None);       // netplay
    CHECK(resumeAction(ResumeAlways, false, false, false) == ResumeAction::None);
    CHECK(resumeAction(ResumeAsk, false, false, false) == ResumeAction::None);

    // the slot: its own file pair per game, beside (never colliding with) the numbered ones
    std::string stem;
    int slot = 0;
    CHECK_EQ(stateFileName("Iron Soldier (World)", kAutoSlot), "Iron Soldier (World).auto.state");
    CHECK_EQ(thumbFileName("Iron Soldier (World)", kAutoSlot), "Iron Soldier (World).auto.png");
    CHECK_EQ(stateFileName("Iron Soldier (World)", 0), "Iron Soldier (World).slot0.state");   // unchanged
    CHECK(parseStateFileName("Iron Soldier (World).auto.state", stem, slot) && stem == "Iron Soldier (World)" && slot == kAutoSlot);
    CHECK(parseStateFileName("a.auto.auto.state", stem, slot) && stem == "a.auto" && slot == kAutoSlot);   // a game called "a.auto"
    CHECK(parseStateFileName("a.auto.slot2.state", stem, slot) && stem == "a.auto" && slot == 2);
    CHECK(!parseStateFileName(".auto.state", stem, slot) && !parseStateFileName("x.auto.png", stem, slot) && !parseStateFileName("x.auto.state.rcp", stem, slot));
    CHECK_EQ(stateLabel(kAutoSlot), "Auto-save");
    CHECK_EQ(stateLabel(4), "Slot 4");

    // where each running copy keeps it
    CHECK_EQ(autoStatesDir("/d/states/", 1), "/d/states/");
    CHECK_EQ(autoStatesDir("/d/states/", 0), "/d/states/");
    CHECK_EQ(autoStatesDir("/d/states/", 2), "/d/states/instance-2/");
    CHECK_EQ(autoStatesDir("/d/states/", 3), "/d/states/instance-3/");

    const std::string dir = g_tmp + "autostates/";
    std::error_code ec;
    fs::create_directories(fs::u8path(dir + "instance-2"), ec);
    auto put = [&](const std::string& d, const std::string& name, int ageMin) {
        writeText(d + name, "S");
        fs::last_write_time(fs::u8path(d + name), fs::file_time_type::clock::now() - std::chrono::minutes(ageMin), ec);
    };
    put(dir, "Alpha.slot1.state", 50);
    put(dir, "Alpha.auto.state", 10);                  // the first copy's auto-save
    put(dir, "Beta.auto.state", 20);                   // a game only the first copy auto-saved
    put(dir + "instance-2/", "Alpha.auto.state", 5);   // copy 2's own
    // copy 1: numbered states and its auto-saves in the states folder; copy 2's folder is not looked at
    std::vector<StateGame> one = listStatesFor(dir, 1);
    CHECK(one.size() == 2 && one[0].stem == "Alpha" && one[0].states.size() == 2 && one[0].states[0].slot == kAutoSlot);
    CHECK(one.size() == 2 && one[0].states[0].path == dir + "Alpha.auto.state");
    // copy 2: the shared numbered states, ITS auto-save, not copy 1's
    std::vector<StateGame> two = listStatesFor(dir, 2);
    CHECK(two.size() == 1 && two[0].stem == "Alpha" && two[0].states.size() == 2);
    if (two.size() == 1 && two[0].states.size() == 2) {
        CHECK(two[0].states[0].slot == kAutoSlot && two[0].states[0].path == dir + "instance-2/Alpha.auto.state");
        CHECK(two[0].states[1].slot == 1);
        CHECK(two[0].newest == two[0].states[0].mtime);
    }
    // an auto-save is deleted like any state (and its screenshot)
    writeText(dir + "Alpha.auto.png", "P");
    StateInfo a = one[0].states[0];
    a.thumbPath = dir + "Alpha.auto.png";
    CHECK(deleteState(a) && !fs::exists(fs::u8path(a.path)) && !fs::exists(fs::u8path(a.thumbPath)));
    CHECK(listStatesFor(dir, 1)[0].states.size() == 1 || listStatesFor(dir, 1).size() == 2);
}

// The service wiring: scan a folder, star and play games, see them resolve, share the file with a second copy,
// list and delete states, and the settings round-trip.
static void testPersonalListsService() {
    std::string root = g_tmp + "lists-root/", games = g_tmp + "lists-games/";
    std::error_code ec;
    fs::create_directories(fs::u8path(root + "library/db"), ec);
    fs::create_directories(fs::u8path(root + "states"), ec);
    writeBytes(games + "Alpha (World).j64", noise(1048576, 71));
    writeBytes(games + "Beta (World).j64", noise(1048576, 72));
    std::vector<unsigned char> d1 = noise(2352 * 30, 73);
    writeBytes(games + "Gamma Disc (USA) (Track 1).bin", d1);
    writeText(games + "Gamma Disc (USA).cue",
              "FILE \"Gamma Disc (USA) (Track 1).bin\" BINARY\n  TRACK 01 MODE1/2352\n    INDEX 01 00:00:00\n");

    Paths paths;
    paths.root = root;
    paths.states = root + "states/";
    Config cfg;
    cfg.setPath(root + "config.ini");
    cfg.set("library.dir.0", games.substr(0, games.size() - 1));
    LibraryService svc;
    svc.createTexture = [](const void*, int, int) { return 1u; };
    svc.destroyTexture = [](unsigned) {};
    svc.artBaseUrl = "http://127.0.0.1:1/";
    svc.artMirrorUrl = svc.artBaseUrl;
    svc.dbBaseUrl = svc.artBaseUrl;
    svc.start(paths, cfg, false);
    svc.setDownloadArt(false);
    auto pumpUntil = [&](const std::function<bool()>& cond) {
        for (int i = 0; i < 400; i++) {
            svc.pump();
            if (cond()) return true;
            SDL_Delay(25);
        }
        return false;
    };
    CHECK(pumpUntil([&] { return !svc.scanning() && svc.entries().size() == 3; }));
    auto find = [&](const std::string& title) {
        lib::Entry e;
        for (const lib::Entry& x : svc.entries())
            if (x.title == title) e = x;
        return e;
    };
    lib::Entry alpha = find("Alpha (World)"), beta = find("Beta (World)"), gamma = find("Gamma Disc (USA)");
    CHECK(!alpha.file.path.empty() && !beta.file.path.empty() && !gamma.file.path.empty());

    // defaults: both lists shown as a tab and in the sections
    CHECK(svc.favoritesShow == lib::ShowBoth && svc.recentsShow == lib::ShowBoth && svc.activeTab() == lib::TabCart);
    CHECK(svc.favoriteEntries().empty() && svc.recentEntries().empty());
    svc.setFavorite(beta, true);
    svc.setFavorite(gamma, true);
    CHECK(svc.isFavorite(beta) && svc.isFavorite(gamma) && !svc.isFavorite(alpha));
    CHECK(svc.favoriteEntries().size() == 2 && svc.favoriteEntries()[0].entry->title == "Beta (World)");   // by title
    svc.noteLaunched(alpha.file.path);
    svc.noteLaunched(beta.file.path);
    svc.noteLaunched("/not/in/the/library.j64");   // scanned, not one of ours: not listed
    CHECK(svc.recentEntries().size() == 2 && svc.recentEntries()[0].entry->title == "Beta (World)" && svc.recentEntries()[1].entry->title == "Alpha (World)");
    CHECK(svc.recentEntries()[0].when > 1700000000);
    CHECK(svc.isRecent(alpha) && !svc.isRecent(gamma));
    svc.removeRecent(beta);
    CHECK(svc.recentEntries().size() == 1 && svc.isFavorite(beta));   // not the same thing
    // the lists survive a restart of the service
    svc.setFavorite(gamma, false);
    svc.stop();
    LibraryService svc2;
    svc2.createTexture = svc.createTexture;
    svc2.destroyTexture = svc.destroyTexture;
    svc2.artBaseUrl = svc2.artMirrorUrl = svc2.dbBaseUrl = "http://127.0.0.1:1/";
    Paths p2 = paths;
    p2.instance = 2;   // a second copy on the same data folder
    Config cfg2;
    cfg2.setPath(root + "config-2.ini");
    cfg2.setSavePrefixes({"window.", "input.pads."});   // what App::loadConfig sets for instance 2+ (the #902 rule)
    cfg2.set("library.dir.0", games.substr(0, games.size() - 1));
    svc2.start(p2, cfg2, false);
    svc2.setDownloadArt(false);
    auto pump2 = [&](const std::function<bool()>& cond) {
        for (int i = 0; i < 400; i++) {
            svc2.pump();
            if (cond()) return true;
            SDL_Delay(25);
        }
        return false;
    };
    CHECK(pump2([&] { return !svc2.scanning() && svc2.entries().size() == 3; }));
    CHECK(svc2.instance == 2);
    CHECK(svc2.favoriteEntries().size() == 1 && svc2.favoriteEntries()[0].entry->title == "Beta (World)");
    CHECK(svc2.recentEntries().size() == 1 && svc2.recentEntries()[0].entry->title == "Alpha (World)");
    // ...and what the second copy does is in the shared file, for the first copy to read
    lib::Entry alpha2;
    for (const lib::Entry& x : svc2.entries())
        if (x.title == "Alpha (World)") alpha2 = x;
    svc2.setFavorite(alpha2, true);
    lib::UserStore reader;
    reader.open(svc2.userFile());
    CHECK(reader.lists().favorites.size() == 2);

    // display options persist (clamped on the way in)
    svc2.favoritesShow = lib::ShowSections;
    svc2.recentsShow = lib::ShowHidden;
    svc2.section = lib::TabRecents;   // remembered tab, now hidden
    svc2.saveSettings();
    CHECK_EQ(cfg2.get("library.favorites.show", ""), "2");
    CHECK_EQ(cfg2.get("library.recents.show", ""), "0");
    {   // ...but the display options are ordinary settings: copy 2 reads them and its config-2.ini never gets them
        std::vector<unsigned char> raw;
        std::string written;
        if (readFile(root + "config-2.ini", raw)) written.assign(raw.begin(), raw.end());
        CHECK(written.find("library.") == std::string::npos);
    }
    CHECK(svc2.activeTab() == lib::TabCart);
    svc2.stop();
    Config cfg3;
    cfg3.set("library.dir.0", games.substr(0, games.size() - 1));
    cfg3.set("library.favorites.show", "9");   // junk: back to the default
    cfg3.set("library.recents.show", "1");
    cfg3.setInt("library.section", 4);
    LibraryService svc3;
    svc3.artBaseUrl = svc3.artMirrorUrl = svc3.dbBaseUrl = "http://127.0.0.1:1/";
    svc3.start(paths, cfg3, false);
    CHECK(svc3.favoritesShow == lib::ShowBoth && svc3.recentsShow == lib::ShowTab && svc3.section == lib::TabRecents && svc3.activeTab() == lib::TabRecents);
    svc3.stop();
    LibraryService svc4;
    Config cfg4;
    cfg4.setInt("library.section", 77);
    svc4.artBaseUrl = svc4.artMirrorUrl = svc4.dbBaseUrl = "http://127.0.0.1:1/";
    svc4.start(paths, cfg4, false);
    CHECK(svc4.section == lib::kTabCount - 1);   // an out-of-range remembered tab is clamped, then falls back at draw time
    svc4.stop();

    // save states: listing, per-game lookup, screenshots, delete
    LibraryService s5;
    int created = 0, destroyed = 0;
    s5.createTexture = [&](const void*, int, int) { return (unsigned)++created; };
    s5.destroyTexture = [&](unsigned) { destroyed++; };
    s5.artBaseUrl = s5.artMirrorUrl = s5.dbBaseUrl = "http://127.0.0.1:1/";
    Config cfg5;
    cfg5.set("library.dir.0", games.substr(0, games.size() - 1));
    s5.start(paths, cfg5, false);
    s5.setDownloadArt(false);
    CHECK(s5.stateGames().empty());
    makeCoverPng("st.png", 48, 36, 9);
    writeText(paths.states + "Alpha (World).slot0.state", "S0");
    writeText(paths.states + "Alpha (World).slot2.state", "S2");
    fs::copy_file(fs::u8path(g_tmp + "st.png"), fs::u8path(paths.states + "Alpha (World).slot2.png"), fs::copy_options::overwrite_existing, ec);
    writeText(paths.states + "Removed Game.slot1.state", "S");
    s5.invalidateStates();
    CHECK(s5.stateGames().size() == 2);
    auto pump5 = [&](const std::function<bool()>& cond) {
        for (int i = 0; i < 400; i++) {
            s5.pump();
            if (cond()) return true;
            SDL_Delay(25);
        }
        return false;
    };
    CHECK(pump5([&] { return !s5.scanning() && s5.entries().size() == 3; }));
    lib::Entry a5;
    for (const lib::Entry& x : s5.entries())
        if (x.title == "Alpha (World)") a5 = x;
    const lib::StateGame* sg = s5.statesFor(a5);
    CHECK(sg != nullptr && sg->states.size() == 2);
    if (sg && sg->states.size() == 2) {
        lib::StateInfo withShot = sg->states[0].thumbPath.empty() ? sg->states[1] : sg->states[0];
        lib::StateInfo noShot = sg->states[0].thumbPath.empty() ? sg->states[0] : sg->states[1];
        CHECK(withShot.slot == 2 && noShot.slot == 0);
        ArtView t = s5.stateThumb(withShot);
        CHECK(t.state == ArtView::Ready && t.w == 48 && t.h == 36 && t.tex != 0);
        ArtView t2 = s5.stateThumb(withShot);
        CHECK(t2.state == ArtView::Ready && t2.tex == t.tex && created == 1);   // cached
        CHECK(s5.stateThumb(noShot).state == ArtView::Missing);
        const lib::Entry* owner = s5.entryForState(*sg);
        CHECK(owner && owner->title == "Alpha (World)");
        CHECK(s5.deleteState(withShot));
        CHECK(destroyed == 1);
        const lib::StateGame* again = s5.statesFor(a5);
        CHECK(again && again->states.size() == 1 && again->states[0].slot == 0);
    }
    const lib::StateGame* orphan = nullptr;
    for (const lib::StateGame& g : s5.stateGames())
        if (g.stem == "Removed Game") orphan = &g;
    CHECK(orphan != nullptr && s5.entryForState(*orphan) == nullptr);   // a game that left the library: listed, cannot be played from here
    s5.stop();
}

int main(int argc, char** argv) {
    if (argc >= 3 && !std::strcmp(argv[1], "--scan")) {
        std::string db = ".";
        for (int i = 3; i + 1 < argc; i++)
            if (!std::strcmp(argv[i], "--db")) db = argv[i + 1];
        return scanTool(argv[2], db);
    }
#ifndef _WIN32
    std::signal(SIGPIPE, SIG_IGN);   // the loopback server writes to clients that hang up on purpose
#endif
    SDL_Init(SDL_INIT_EVENTS);
    g_tmp = mkTmp();
    std::printf("library tests, HTTP backend: %s\n", http::backendName());
    testCrc();
    testDatParsing();
    testHeaderDetection();
    testThumbnailNames();
    testScanAndIdentify();
    testBiosClassification();
    testBiosInLibraryFolders();
    testTileZoom();
    testDiscTitles();
    testCoverLookup();
    testCoverService();
    testLooseNames();
    testChdAndCdiChecksums();
    testM3u();
    testPlaylistStateOwners();
    testDisplayOptionsAndTabs();
    testPersonalListsLogic();
    testUserStoreAndInstances();
    testStateNamesAndListing();
    testResumePolicy();
    testPersonalListsService();
#ifndef _WIN32
    if (http::available()) {
        testHttp();
        testServicePipeline();
        testCdMirror();
    } else {
        std::printf("no HTTP backend in this build: loopback HTTP tests skipped\n");
    }
#else
    std::printf("loopback HTTP tests are POSIX-only: skipped\n");
#endif
    std::error_code ec;
    fs::remove_all(fs::u8path(g_tmp), ec);   // our own temp dir only
    std::printf("%d checks, %d failed\n", g_checks, g_fail);
    return g_fail ? 1 : 0;
}
