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
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>
#include <mutex>
#include <thread>

#include "config.h"
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

        // an unidentified entry has no art key and never touches the network
        lib::Entry unknown = *a;
        unknown.datName.clear();
        CHECK(svc.art(unknown).state == ArtView::Missing);

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
