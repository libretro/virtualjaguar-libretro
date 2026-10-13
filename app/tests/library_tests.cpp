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
#ifndef _WIN32
    if (http::available()) {
        testHttp();
        testServicePipeline();
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
