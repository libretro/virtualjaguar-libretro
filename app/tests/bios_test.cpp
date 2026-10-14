// BIOS choice, import and classification (#838, #833) -- no window, no core, no real BIOS images (they cannot
// be committed, so the files here are synthetic and told apart by name, size and a registered checksum).
//
// What is pinned down:
//   * scanning: BIOS files in nested folders of a library and in the system folder, with upper / mixed-case names
//     and every common spelling, are recognised and put in the BIOS section, with the right role;
//   * "will the core's own search find it": case-insensitive on macOS and Windows volumes, exact elsewhere;
//   * what may be CHOSEN for a role (exact size), what "Boot" does for each kind, and why when it cannot;
//   * the choice persists per role in config.ini (and Auto = no key);
//   * importing a BIOS: copy into <system>/bios/, never over a different file, reuse an identical one, refuse
//     the wrong size or a non-BIOS;
//   * how the running BIOS is described.
#include <SDL3/SDL.h>

#include <cstdio>
#include <cstring>
#include <filesystem>
#include <map>
#include <set>
#include <fstream>
#include <string>
#include <vector>

#include "bios.h"
#include "config.h"
#include "library.h"
#include "paths.h"

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

static std::string g_tmp;

static void writeBytes(const std::string& path, const std::vector<unsigned char>& b) {
    std::error_code ec;
    fs::create_directories(fs::path(path).parent_path(), ec);
    std::ofstream f(path, std::ios::binary);
    f.write((const char*)b.data(), (std::streamsize)b.size());
}

static std::vector<unsigned char> noise(size_t n, uint32_t seed) {
    std::vector<unsigned char> v(n);
    uint32_t x = seed * 2654435761u + 12345u;
    for (size_t i = 0; i < n; i++) {
        x = x * 1664525u + 1013904223u;
        v[i] = (unsigned char)(x >> 24);
    }
    return v;
}

static const size_t K128 = 128 * 1024, K256 = 256 * 1024;

static std::map<std::string, lib::Entry> scanAndIdentify(const std::vector<std::string>& folders, const std::string& sys) {
    lib::ScanOptions opt;
    opt.folders = folders;
    opt.systemFolder = sys;
    std::vector<lib::ScanFile> files = lib::scanFolders(opt, lib::ScanCache(), nullptr, nullptr);
    std::map<std::string, lib::Entry> by;
    for (const lib::ScanFile& f : files) by[fs::path(f.path).filename().string()] = lib::identify(f, nullptr, nullptr);
    return by;
}

// ---- scanning and classification --------------------------------------------------------------------------
static void testScanNestedAndCase() {
    const std::string lib1 = g_tmp + "scan/lib/", sys = g_tmp + "scan/system/";
    // library folder: BIOS files in sub-folders, with the common spellings and odd case
    writeBytes(lib1 + "Dumps/Jaguar/[BIOS] Atari Jaguar (World).j64", noise(K128, 1));
    writeBytes(lib1 + "Dumps/Jaguar CD/[BIOS] Atari Jaguar CD (World).j64", noise(K256, 2));
    writeBytes(lib1 + "Dumps/Jaguar CD/[BIOS] Atari Jaguar Developer CD (World).j64", noise(K256, 3));
    writeBytes(lib1 + "extra/JAGBOOT.ROM", noise(K128, 4));
    writeBytes(lib1 + "extra/JagCD.BIN", noise(K256, 5));
    writeBytes(lib1 + "extra/Jaguar CD BIOS.ROM", noise(K256, 6));
    writeBytes(lib1 + "extra/Memory Track (World).j64", noise(K128, 7));
    writeBytes(lib1 + "extra/Game (World).j64", noise(1 << 20, 8));
    // the system folder: names the core searches for, in its sub-folders
    writeBytes(sys + "jagcd.bin", noise(K256, 9));
    writeBytes(sys + "Atari - Jaguar CD/JaguarCD_BIOS.bin", noise(K256, 10));
    writeBytes(sys + "bios/anything goes.bin", noise(K128, 11));   // an imported file: classified by size + name only if named so
    writeBytes(sys + "bios/My Jaguar Boot BIOS.rom", noise(K128, 12));

    auto by = scanAndIdentify({lib1}, sys);
    auto role = [&](const char* name) { return by.count(name) ? by[name].bios.role : lib::BiosRole::None; };
    CHECK(by["[BIOS] Atari Jaguar (World).j64"].section == lib::Section::Bios);
    CHECK(role("[BIOS] Atari Jaguar (World).j64") == lib::BiosRole::CartBoot);
    CHECK(role("[BIOS] Atari Jaguar CD (World).j64") == lib::BiosRole::CdBios);
    CHECK(role("[BIOS] Atari Jaguar Developer CD (World).j64") == lib::BiosRole::CdBios);
    CHECK(role("JAGBOOT.ROM") == lib::BiosRole::CartBoot);           // upper-case
    CHECK(role("JagCD.BIN") == lib::BiosRole::CdBios);               // mixed case
    CHECK(role("Jaguar CD BIOS.ROM") == lib::BiosRole::CdBios);
    CHECK(role("Memory Track (World).j64") == lib::BiosRole::MemoryTrack);
    CHECK(by["Game (World).j64"].section == lib::Section::Cart);
    CHECK(role("jagcd.bin") == lib::BiosRole::CdBios);               // from the system folder
    CHECK(role("JaguarCD_BIOS.bin") == lib::BiosRole::CdBios);       // a spelling the core would not search for
    CHECK(role("My Jaguar Boot BIOS.rom") == lib::BiosRole::CartBoot);
    CHECK(role("anything goes.bin") == lib::BiosRole::None);         // no BIOS-ish name, unknown checksum: not guessable
    // what may be chosen: exactly the size the core accepts, whatever the name
    for (const char* n : {"[BIOS] Atari Jaguar (World).j64", "JAGBOOT.ROM", "My Jaguar Boot BIOS.rom"})
        CHECK(bios::roleOf(by[n].bios, by[n].file.size) == bios::CartBoot);
    for (const char* n : {"[BIOS] Atari Jaguar CD (World).j64", "JagCD.BIN", "JaguarCD_BIOS.bin"})
        CHECK(bios::roleOf(by[n].bios, by[n].file.size) == bios::Cd);
    CHECK(bios::roleOf(by["Memory Track (World).j64"].bios, K128) == bios::None);
}

static void testCaseInsensitiveUse() {
    // The core's search opens "<system>/<folder>/<name>": a case-insensitive volume finds JAGCD.BIN as well.
    lib::BiosInfo cd = lib::classifyBios("/sys/JAGCD.BIN", K256, 1);
    lib::BiosUse u = lib::biosUse(cd, "/sys/JAGCD.BIN", K256, "/sys");
    lib::BiosUse dirs = lib::biosUse(cd, "/sys/ATARI - JAGUAR CD/jagcd.bin", K256, "/sys");
    lib::BiosInfo boot = lib::classifyBios("/sys/JagBoot_M.ROM", K128, 1);
    lib::BiosUse mm = lib::biosUse(boot, "/sys/JagBoot_M.ROM", K128, "/sys");
#if defined(__APPLE__) || defined(_WIN32)
    CHECK(u.core && dirs.core && mm.core);
#else
    CHECK(!u.core && !dirs.core && !mm.core);   // Linux: the spelling must match exactly
#endif
    // either way the tooltip points at the way that works under any name
    CHECK(u.core || u.text.find("Use this BIOS") != std::string::npos);
    // a wrong size is skipped by the core on every platform
    CHECK(!lib::biosUse(cd, "/sys/jagcd.bin", 0x30000, "/sys").core);
}

static void testBootInfo() {
    lib::BiosInfo cart = lib::classifyBios("/x/jagboot.rom", K128, 1);
    lib::BiosInfo cd = lib::classifyBios("/x/jagcd.bin", K256, 1);
    lib::BiosInfo mt = lib::classifyBios("/x/Memory Track.j64", K128, 1);
    CHECK(bios::bootInfo(cart, K128).kind == bios::BootKind::CartRom);
    CHECK(bios::bootInfo(cd, K256).kind == bios::BootKind::CdBios);
    CHECK(bios::bootInfo(cart, 100000).kind == bios::BootKind::None);              // wrong size: the core would refuse it
    CHECK(bios::bootInfo(cart, 100000).text.find("131072") != std::string::npos);
    CHECK(bios::bootInfo(cd, K128).kind == bios::BootKind::None);
    // The Memory Track boots only as the known dump; an unrecognised image says why not, and what to do instead.
    CHECK(bios::bootInfo(mt, K128).kind == bios::BootKind::None);
    CHECK(bios::bootInfo(mt, K128).text.find("CD BIOS") != std::string::npos);
    lib::addKnownBiosForTest(0xFDF37F47u, lib::BiosRole::MemoryTrack, "Memory Track cartridge");
    lib::BiosInfo mtk = lib::classifyBios("/x/whatever.j64", K128, 0xFDF37F47u);
    CHECK(mtk.recognized && bios::bootInfo(mtk, K128).kind == bios::BootKind::MemoryTrack);
    lib::BiosInfo other = lib::classifyBios("/x/something bios.bin", 100000, 1);
    CHECK(other.role == lib::BiosRole::Other && bios::bootInfo(other, 100000).kind == bios::BootKind::None);
}

static void testSelectionPersistence() {
    const std::string path = g_tmp + "cfg/config.ini";
    fs::create_directories(g_tmp + "cfg");
    {
        Config c;
        c.setPath(path);
        bios::Selection s;
        CHECK(bios::loadSelection(c) == s);   // nothing saved: Auto for both
        s.cd = "/bios/jagcd dev.bin";
        bios::storeSelection(c, s);
        s.cart = "/bios/Stubulator.j64";
        bios::storeSelection(c, s);
        CHECK(c.save());
    }
    {
        Config c;
        c.load(path);
        bios::Selection s = bios::loadSelection(c);
        CHECK(s.cd == "/bios/jagcd dev.bin" && s.cart == "/bios/Stubulator.j64");
        CHECK(s.of(bios::Cd) == s.cd && s.of(bios::CartBoot) == s.cart);
        s.cart.clear();   // back to Auto: the key goes away rather than lingering as an empty value
        bios::storeSelection(c, s);
        CHECK(!c.has("bios.cart") && c.has("bios.cd"));
        CHECK(c.save());
    }
    {
        Config c;
        c.load(path);
        CHECK(bios::loadSelection(c).cart.empty() && !bios::loadSelection(c).cd.empty());
    }
}

static void testImport() {
    const std::string sys = g_tmp + "import/system/", src = g_tmp + "import/downloads/";
    std::vector<unsigned char> cdImg = noise(K256, 21), bootImg = noise(K128, 22), other = noise(K256, 23);
    writeBytes(src + "JAGCD.BIN", cdImg);
    writeBytes(src + "Boot ROM (World) BIOS.rom", bootImg);
    writeBytes(src + "[BIOS] Atari Jaguar CD (World).j64", other);
    writeBytes(src + "jagcd.bin.txt", noise(K256, 24));
    writeBytes(src + "short bios.bin", noise(100000, 25));
    writeBytes(src + "Game.j64", noise(1 << 20, 26));

    CHECK(bios::importRole(src + "JAGCD.BIN") == bios::Cd);
    CHECK(bios::importRole(src + "Boot ROM (World) BIOS.rom") == bios::CartBoot);
    CHECK(bios::importRole(src + "short bios.bin") == bios::None);   // not a size the core takes
    CHECK(bios::importRole(src + "Game.j64") == bios::None);          // a game stays a game
    CHECK(bios::importRole(src + "missing.bin") == bios::None);

    bios::Imported a = bios::importFile(src + "JAGCD.BIN", sys);
    CHECK(a.ok && a.role == bios::Cd);
    CHECK(a.path == sys + "bios/JAGCD.BIN");
    CHECK(fs::exists(a.path) && fs::file_size(a.path) == K256);
    bios::Imported again = bios::importFile(src + "JAGCD.BIN", sys);   // the same file: reused, not copied twice
    CHECK(again.ok && again.path == a.path);
    // a different file under the same name never overwrites: it gets a numbered name
    writeBytes(g_tmp + "import/other/JAGCD.BIN", noise(K256, 27));
    bios::Imported b = bios::importFile(g_tmp + "import/other/JAGCD.BIN", sys);
    CHECK(b.ok && b.path == sys + "bios/JAGCD (2).BIN");
    CHECK(fs::file_size(a.path) == K256);
    bios::Imported c = bios::importFile(src + "Boot ROM (World) BIOS.rom", sys);
    CHECK(c.ok && c.role == bios::CartBoot);
    // refusals say why and write nothing
    bios::Imported bad = bios::importFile(src + "short bios.bin", sys);
    CHECK(!bad.ok && !bad.message.empty() && !fs::exists(sys + "bios/short bios.bin"));
    CHECK(!bios::importFile(src + "Game.j64", sys).ok);
    CHECK(!bios::importFile(src + "JAGCD.BIN", "").ok);
    // an explicit import takes an unnamed dump by its size, and names it so the library scan lists it
    writeBytes(src + "dump123.bin", noise(K256, 28));
    writeBytes(src + "dump456.bin", noise(K128, 29));
    CHECK(!bios::importFile(src + "dump123.bin", sys).ok);   // a drop is not trusted
    bios::Imported t = bios::importFile(src + "dump123.bin", sys, true);
    CHECK(t.ok && t.role == bios::Cd && t.path == sys + "bios/[BIOS] Atari Jaguar CD - dump123.bin");
    bios::Imported t2 = bios::importFile(src + "dump456.bin", sys, true);
    CHECK(t2.ok && t2.role == bios::CartBoot);
    lib::BiosInfo ti = lib::classifyBios(t.path, K256, 1), ti2 = lib::classifyBios(t2.path, K128, 1);
    CHECK(bios::roleOf(ti, K256) == bios::Cd && bios::roleOf(ti2, K128) == bios::CartBoot);   // the scan sees them
}

static void testDescribe() {
    CHECK(bios::describeStaged(bios::SrcBuiltin, "Series K boot ROM", 0xFB731AAAu) == "built-in Series K boot ROM (CRC32 FB731AAA)");
    const std::string f = bios::describeStaged(bios::SrcFile, "/home/me/bios/jagcd dev.bin", 0x12345678u);
    CHECK(f.find("your file jagcd dev.bin") == 0 && f.find("12345678") != std::string::npos);
    const std::string s = bios::describeStaged(bios::SrcSystem, "/sys/jagcd.bin", 0x687068D5u);
    CHECK(s.find("system-folder file jagcd.bin") == 0 && s.find("Jaguar CD BIOS (retail)") != std::string::npos);
}

// #932: one BIOS dump in several folders, or two folders holding same-named files, gave several combo rows with
// the same text, which is the same ImGui ID, so clicking a later copy picked the first.  The rows the combo is
// built from must have distinct labels, one per file on disk, and a stored choice must find its row.
static void testPicksAreDistinct() {
    const std::string a = g_tmp + "picks/a/", b = g_tmp + "picks/b/", sys = g_tmp + "picks/system/";
    const std::vector<unsigned char> boot = noise(K128, 21), cd = noise(K256, 22);
    writeBytes(a + "jagboot.rom", boot);
    writeBytes(b + "jagboot.rom", boot);              // the same dump in another folder
    writeBytes(sys + "jagboot.rom", boot);            // ... and in the system folder
    writeBytes(a + "jagcd.bin", cd);
    writeBytes(b + "JaguarCD_BIOS.bin", cd);          // a differently named copy keeps its own name
    // the system folder is also a library folder: every file there is reached twice
    lib::ScanOptions opt;
    opt.folders = {a, b, sys};
    opt.systemFolder = sys;
    std::vector<lib::Entry> entries;
    for (const lib::ScanFile& f : lib::scanFolders(opt, lib::ScanCache(), nullptr, nullptr)) entries.push_back(lib::identify(f, nullptr, nullptr));

    const std::vector<bios::Pick> carts = bios::picksFor(entries, bios::CartBoot);
    CHECK(carts.size() == 3);                          // one row per file, none for the double scan of the system folder
    std::set<std::string> labels, paths;
    for (const bios::Pick& p : carts) {
        labels.insert(p.label);
        paths.insert(p.path);
        CHECK(p.label.find("jagboot.rom") == 0);
    }
    CHECK(labels.size() == carts.size() && paths.size() == carts.size());   // no two rows share a label (= an ImGui ID)
    for (const bios::Pick& p : carts) CHECK(p.label.find(fs::path(p.path).parent_path().string()) != std::string::npos);   // each says its folder
    const std::vector<bios::Pick> cds = bios::picksFor(entries, bios::Cd);
    CHECK(cds.size() == 2);
    CHECK(cds[0].label != cds[1].label);
    // a role takes only what it can run
    for (const bios::Pick& p : cds) CHECK(fs::file_size(p.path) == K256);

    // the stored choice finds its own row, and only that one
    int hit = 0;
    for (const bios::Pick& p : carts)
        if (bios::samePath(p.path, absolutePath(b + "jagboot.rom"))) hit++;
    CHECK(hit == 1);
    CHECK(bios::samePath("/x/y.rom", "/x/y.rom") && !bios::samePath("/x/y.rom", "") && bios::samePath("", ""));
    // one file reached through a second spelling of its folder (a symlink, or "a/../a") is listed once
    std::vector<lib::Entry> twice = entries;
    for (lib::Entry e : entries)
        if (e.file.path == a + "jagboot.rom" || fs::path(e.file.path) == fs::path(a) / "jagboot.rom") {
            e.file.path = a + "../a/jagboot.rom";
            twice.push_back(e);
        }
    CHECK(bios::picksFor(twice, bios::CartBoot).size() == 3);
    CHECK(bios::samePath(a + "../a/jagboot.rom", a + "jagboot.rom"));
}

int main() {
    SDL_Init(SDL_INIT_EVENTS);
    std::error_code ec;
    fs::path p = fs::temp_directory_path(ec);
    if (ec) p = ".";
    p /= "vj-bios-test-" + std::to_string((unsigned long long)SDL_GetPerformanceCounter());
    fs::create_directories(p, ec);
    g_tmp = p.string() + "/";
    testScanNestedAndCase();
    testCaseInsensitiveUse();
    testBootInfo();
    testSelectionPersistence();
    testImport();
    testDescribe();
    testPicksAreDistinct();
    fs::remove_all(p, ec);   // our own temp dir only
    std::printf("bios tests: %d checks, %d failed\n", g_checks, g_fail);
    return g_fail ? 1 : 0;
}
