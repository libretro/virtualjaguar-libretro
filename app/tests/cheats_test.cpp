// Unit tests for the cheat database parser, the conversion to the core's code
// format and the per-game persistence.  Links only cheats.cpp, config.cpp,
// paths.cpp, sha256.cpp and the core's own cheat.c (so every code we emit is run
// through the real parser): no SDL window, no GL, no emulation.
//
//   vj_cheats_test               the unit tests (fixtures are synthetic .cht files)
//   vj_cheats_test --corpus DIR  parse every .cht in DIR (the downloaded libretro database)
#include "cheats.h"

#include "cheat.h"
#include "paths.h"
#include "sha256.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#ifndef VJ_TEST_FIXTURES
#error VJ_TEST_FIXTURES must name the fixtures directory
#endif

using namespace vj;
namespace fs = std::filesystem;

static int g_fail = 0, g_checks = 0;

#define CHECK(cond)                                                                       \
    do {                                                                                  \
        g_checks++;                                                                       \
        if (!(cond)) {                                                                    \
            g_fail++;                                                                     \
            std::fprintf(stderr, "FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond);          \
        }                                                                                 \
    } while (0)

#define CHECK_EQ(a, b)                                                                    \
    do {                                                                                  \
        g_checks++;                                                                       \
        std::string va_ = (a), vb_ = (b);                                                 \
        if (va_ != vb_) {                                                                 \
            g_fail++;                                                                     \
            std::fprintf(stderr, "FAIL %s:%d  %s\n   got      '%s'\n   expected '%s'\n", __FILE__, __LINE__, #a, \
                         va_.c_str(), vb_.c_str());                                       \
        }                                                                                 \
    } while (0)

static std::string fixture(const std::string& name) { return std::string(VJ_TEST_FIXTURES) + "/" + name; }

static const ChtCheat* find(const ChtSet& s, const std::string& desc) {
    for (const ChtCheat& c : s.cheats)
        if (c.desc == desc) return &c;
    return nullptr;
}

// The emulated main RAM as the core stores it: byte i is the byte at bus address i.
struct Ram {
    std::vector<uint8_t> b = std::vector<uint8_t>(0x200000, 0);
};
static void ramWrite(uint32_t a, uint32_t v, uint8_t size, void* user) {
    Ram* r = (Ram*)user;
    for (unsigned i = 0; i < size; i++) r->b[(a + i) & 0x1FFFFF] = (uint8_t)(v >> (8 * (size - 1 - i)));   // Jaguar is big-endian
}

// Run `code` through the core's real cheat list and write it into `ram`.
static void play(Ram& ram, const std::string& code) {
    static cheat_list_t list;
    cheat_list_reset(&list);
    cheat_list_set(&list, 0, true, code.c_str());
    cheat_list_apply(&list, ramWrite, &ram);
}

struct Recorder {
    std::vector<std::string> log;
    void attach(CheatManager& m) {
        m.commit = [this](CheatBatch b) {
            batches++;
            for (const CheatOp& o : b) {
                if (o.reset) log.push_back("reset");
                else log.push_back("set " + std::to_string(o.index) + (o.enabled ? " on " : " off ") + o.code);
            }
        };
    }
    int batches = 0;
};

// ---------------------------------------------------------------------------------
static void testParse() {
    ChtSet s;
    CHECK(loadChtFile(fixture("Test Game.cht"), s));
    CHECK_EQ(s.stem, "Test Game");
    // 13 entries, the three "Score (Part N)" lines merge into one toggle.
    CHECK(s.cheats.size() == 11);

    const ChtCheat* c = find(s, "Lives");
    CHECK(c && c->supported);
    if (c) {
        CHECK_EQ(c->code, "00007D3D:05");   // 8-bit: the ':' stays, or the core reads 6+4 digits
        CHECK(c->segments == 1);
    }

    c = find(s, "Score");
    CHECK(c && c->supported);
    if (c) {
        CHECK(c->parts == 3);
        CHECK(c->segments == 3);
        CHECK_EQ(c->code, "0000C972:09090000+0000C974:0909+0000C976:0909");
    }
    CHECK(!find(s, "Score (Part 1)"));

    c = find(s, "Odd word");
    CHECK(c && c->supported);
    if (c) CHECK_EQ(c->code, "00092255:02+00092256:01");   // odd address: bytes, never a word

    c = find(s, "Odd long");
    CHECK(c && c->supported);
    if (c) CHECK_EQ(c->code, "000003E9:01+000003EA:0203+000003EC:04");

    c = find(s, "Big endian word");
    CHECK(c && c->supported);
    if (c) CHECK_EQ(c->code, "00001000:1234");

    c = find(s, "Embedded code");
    CHECK(c && c->supported);
    if (c) CHECK_EQ(c->code, "001CC84F:01");   // passed through unchanged
    c = find(s, "Two-write code");
    CHECK(c && c->supported && c->segments == 2);

    for (const char* name : {"Increase by value", "Past the end of RAM", "Garbage code", "Emulator handler"}) {
        c = find(s, name);
        CHECK(c && !c->supported && !c->note.empty() && c->code.empty());
    }

    // ids are stable across parses and unique within a set
    ChtSet s2;
    CHECK(loadChtFile(fixture("Test Game.cht"), s2));
    for (size_t i = 0; i < s.cheats.size(); i++) {
        CHECK_EQ(s.cheats[i].id, s2.cheats[i].id);
        for (size_t j = i + 1; j < s.cheats.size(); j++) CHECK(s.cheats[i].id != s.cheats[j].id);
    }

    // unquoted values, "enable = false", no cheat count line
    ChtSet u;
    CHECK(loadChtFile(fixture("Unquoted Style (World).cht"), u));
    CHECK(u.cheats.size() == 2);
    CHECK_EQ(u.cheats[0].code, "001CC80B:05");
    CHECK_EQ(u.cheats[1].desc, "Unquoted desc");
    CHECK_EQ(u.cheats[1].code, "00000100:7F");

    // cheatN_enable is ignored: nothing is on by itself
    CheatManager m;
    Recorder rec;
    rec.attach(m);
    m.setLibraryDirs({VJ_TEST_FIXTURES});
    m.rescan();
    GameIdentity g;
    g.key = "crc-00000001";
    g.title = "Test Game";
    m.setStateDir((fs::temp_directory_path() / "vj-cheats-enable-ignored/").string());
    fs::remove_all(fs::temp_directory_path() / "vj-cheats-enable-ignored");
    m.gameLoaded(g);
    CHECK(m.enabledCount() == 0);
    for (const std::string& l : rec.log) CHECK(l == "reset");
    fs::remove_all(fs::temp_directory_path() / "vj-cheats-enable-ignored");
}

// Every supported cheat, played through the core's real parser and list, must leave
// exactly the bytes RetroArch's own memory cheat would: the value little-endian
// (big_endian = false) at consecutive addresses.
static void testReplay() {
    ChtSet s;
    CHECK(loadChtFile(fixture("Test Game.cht"), s));
    Ram ram;
    for (const ChtCheat& c : s.cheats)
        if (c.supported) play(ram, c.code);
    CHECK(ram.b[0x7D3D] == 0x05);
    for (uint32_t a = 0xC972; a <= 0xC977; a++) CHECK(ram.b[a] == 0x09);   // 999999 as six digit bytes
    CHECK(ram.b[0x92255] == 0x02 && ram.b[0x92256] == 0x01);
    CHECK(ram.b[0x3E9] == 0x01 && ram.b[0x3EA] == 0x02 && ram.b[0x3EB] == 0x03 && ram.b[0x3EC] == 0x04);
    CHECK(ram.b[0x1000] == 0x12 && ram.b[0x1001] == 0x34);   // big_endian = true: most significant byte first
    CHECK(ram.b[0x1CC84F] == 0x01);
    CHECK(ram.b[0x58C87] == 0x03 && ram.b[0x58C88] == 0x04);
    // untouched
    CHECK(ram.b[0x7D3C] == 0 && ram.b[0x7D3E] == 0 && ram.b[0xC971] == 0 && ram.b[0xC978] == 0);

    // The conversion itself, for every width and alignment, against a byte-by-byte model.
    for (uint32_t addr : {0x100u, 0x101u, 0x1FFFF0u, 0x1FFFF1u}) {
        for (unsigned n = 1; n <= 4; n++) {
            uint8_t bytes[4] = {0xA1, 0xB2, 0xC3, 0xD4};
            std::string code = memoryCheatToCode(addr, bytes, n);
            unsigned segs = 0;
            std::string why;
            CHECK(validateCode(code, &segs, &why));
            Ram r;
            play(r, code);
            for (unsigned i = 0; i < n; i++) CHECK(r.b[addr + i] == bytes[i]);
            CHECK(r.b[addr + n] == 0);
            CHECK(addr == 0 || r.b[addr - 1] == 0);
        }
    }
    // The trap this guards: eight address digits + a byte with no separator reads as 6+4 to the core.
    uint32_t a, v;
    uint8_t sz;
    CHECK(cheat_parse_one("00007D3D05", &a, &v, &sz) && sz == 2);   // (documents the core's rule)
    CHECK(cheat_parse_one("00007D3D:05", &a, &v, &sz) && a == 0x7D3D && v == 5 && sz == 1);
}

static void testHelpers() {
    CHECK_EQ(normalizeTitle("Raiden (World) (Rev A)"), "raiden");
    CHECK_EQ(normalizeTitle("Val d'Isere Skiing and Snowboarding"), "valdisereskiingandsnowboarding");
    CHECK_EQ(normalizeTitle("Cybermorph [b]"), "cybermorph");
    CHECK_EQ(slugify("Tempest 2000 (World)"), "tempest-2000-world");
    CHECK_EQ(slugify("../../etc"), "etc");
    CHECK(slugify("???").empty());

    unsigned segs = 0;
    std::string why;
    CHECK(validateCode("00007D3D:05", &segs, &why) && segs == 1);
    CHECK(validateCode("00007D3D:05+00007D3E:06\n00007D3F:07", &segs, &why) && segs == 3);
    CHECK(!validateCode("", &segs, &why));
    CHECK(!validateCode("hello", &segs, &why) && !why.empty());
    CHECK(!validateCode("00007D3D:0", &segs, &why));
    CHECK(!validateCode(std::string(80, 'A'), &segs, &why));
}

static void testLibraryMatching() {
    fs::path tmp = fs::temp_directory_path() / "vj-cheats-lib";
    fs::remove_all(tmp);
    fs::create_directories(tmp / "Atari - Jaguar");
    fs::copy_file(fixture("Test Game.cht"), tmp / "Cybermorph (1mb) (Rev A) (1993).cht");
    fs::copy_file(fixture("Test Game.cht"), tmp / "Atari - Jaguar" / "Cybermorph (2mb) (Rev B) (1993).cht");
    fs::copy_file(fixture("Test Game.cht"), tmp / "Tempest 2000.cht");
    fs::copy_file(fixture("Test Game.cht"), tmp / "Tempest 2000 Plus Extras.cht");
    std::ofstream(tmp / "notes.txt") << "not a cheat file";
    fs::path state = tmp / "state";

    CheatManager m;
    Recorder rec;
    rec.attach(m);
    m.setStateDir(state.string() + "/");
    m.setLibraryDirs({tmp.string()});
    m.rescan();
    CHECK(m.library().size() == 4);   // the "Atari - Jaguar" subfolder is searched, notes.txt is not

    // one name match is chosen without asking
    GameIdentity t;
    t.key = "crc-1";
    t.title = "Tempest 2000";
    t.fileStem = "tempest_file";
    m.gameLoaded(t);
    CHECK(m.matches().size() == 1);
    CHECK_EQ(m.selectedStem(), "Tempest 2000");
    CHECK(m.selectedSet() != nullptr);
    CHECK(m.suggestions().size() == 1);   // "Tempest 2000 Plus Extras" contains it: offered, not chosen

    // the file stem matches too (an unrecognised ROM named after its game)
    GameIdentity f;
    f.key = "crc-2";
    f.fileStem = "Tempest 2000 (World)";
    m.gameLoaded(f);
    CHECK_EQ(m.selectedStem(), "Tempest 2000");

    // two matches: nothing is chosen silently
    GameIdentity c;
    c.key = "crc-3";
    c.title = "Cybermorph";
    m.gameLoaded(c);
    CHECK(m.matches().size() == 2);
    CHECK(m.selectedStem().empty());
    CHECK(m.selectedSet() == nullptr);
    CHECK(!m.setStatus().empty());
    CHECK(!m.selectSet("No such set"));
    CHECK(m.selectSet("Cybermorph (2mb) (Rev B) (1993)"));
    CHECK(m.selectedSet() != nullptr);

    // ...and the choice is remembered for that game only
    CheatManager m2;
    m2.setStateDir(state.string() + "/");
    m2.setLibraryDirs({tmp.string()});
    m2.rescan();
    m2.gameLoaded(c);
    CHECK_EQ(m2.selectedStem(), "Cybermorph (2mb) (Rev B) (1993)");
    m2.gameLoaded(t);
    CHECK_EQ(m2.selectedStem(), "Tempest 2000");

    // an explicit "no set" is remembered too, instead of the automatic match coming back
    m2.selectSet("");
    CheatManager m3;
    m3.setStateDir(state.string() + "/");
    m3.setLibraryDirs({tmp.string()});
    m3.rescan();
    m3.gameLoaded(t);
    CHECK(m3.selectedStem().empty());

    // the saved set vanished from the library
    fs::remove(tmp / "Tempest 2000.cht");
    m3.rescan();
    m3.gameLoaded(c);
    m3.selectSet("Cybermorph (1mb) (Rev A) (1993)");
    fs::remove(tmp / "Cybermorph (1mb) (Rev A) (1993).cht");
    m3.rescan();
    CHECK(m3.selectedSet() == nullptr);
    CHECK(!m3.setStatus().empty());

    fs::remove_all(tmp);
}

static void testPersistence() {
    fs::path tmp = fs::temp_directory_path() / "vj-cheats-persist";
    fs::remove_all(tmp);
    std::string state = tmp.string() + "/games/";

    GameIdentity g;
    g.key = "crc-deadbeef";
    g.title = "Test Game";
    g.fileStem = "Test Game (World)";

    std::string livesCode, scoreCode;
    {
        CheatManager m;
        Recorder rec;
        rec.attach(m);
        m.setStateDir(state);
        m.setLibraryDirs({VJ_TEST_FIXTURES});
        m.rescan();
        m.gameLoaded(g);
        CHECK_EQ(m.selectedStem(), "Test Game");
        const ChtSet* s = m.selectedSet();
        CHECK(s != nullptr);
        if (!s) return;
        const ChtCheat* lives = find(*s, "Lives");
        const ChtCheat* score = find(*s, "Score");
        const ChtCheat* bad = find(*s, "Garbage code");
        CHECK(lives && score && bad);
        livesCode = lives->code;
        scoreCode = score->code;

        rec.log.clear();
        m.setEnabled(*bad, true);   // unsupported: refused
        CHECK(!m.isEnabled(*bad));
        m.setEnabled(*score, true);
        m.setEnabled(*lives, true);
        CHECK(m.isEnabled(*lives) && m.isEnabled(*score));
        CHECK(m.enabledCount() == 2);
        CHECK(m.appliedSegments() == 4);
        // every change is a full reset + set-all in list order (Lives is entry 0, Score entry 1)
        CHECK(!rec.log.empty());
        CHECK_EQ(rec.log.back(), "set 1 on " + scoreCode);
        CHECK_EQ(rec.log[rec.log.size() - 2], "set 0 on " + livesCode);
        CHECK_EQ(rec.log[rec.log.size() - 3], "reset");

        std::string err;
        CHECK(!m.addUser("bad", "not a code", err) && !err.empty());
        CHECK(m.addUser("My lives", "00001000:07", err));
        CHECK(m.addUser("Two", "00001002:01\n00001003:02", err));
        CHECK(m.addUser("My lives", "00001000:07", err));   // same code again: no duplicate
        CHECK(m.userCheats().size() == 2);
        CHECK_EQ(m.userCheats()[1].code, "00001002:01+00001003:02");
        m.setUserEnabled(1, false);
        CHECK(m.enabledCount() == 3);
    }
    CHECK(fs::exists(tmp / "games" / "crc-deadbeef.ini"));

    // "restart": a new manager, nothing in memory
    {
        CheatManager m;
        Recorder rec;
        rec.attach(m);
        m.setStateDir(state);
        m.setLibraryDirs({VJ_TEST_FIXTURES});
        m.rescan();
        m.gameLoaded(g);
        const ChtSet* s = m.selectedSet();
        CHECK(s != nullptr);
        if (!s) return;
        CHECK(m.isEnabled(*find(*s, "Lives")));
        CHECK(m.isEnabled(*find(*s, "Score")));
        CHECK(!m.isEnabled(*find(*s, "Odd word")));
        CHECK(m.userCheats().size() == 2);
        CHECK(m.userCheats()[0].enabled && !m.userCheats()[1].enabled);
        CHECK(m.enabledCount() == 3);
        // the load itself re-applies, in a fixed order, to a core that just dropped its list
        std::vector<std::string> want = {"reset", "set 0 on " + livesCode, "set 1 on " + scoreCode,
                                         "set " + std::to_string(CheatManager::kUserIndexBase) + " on 00001000:07"};
        CHECK(rec.log == want);
        // ...and again after a state load
        rec.log.clear();
        m.apply();
        CHECK(rec.log == want);

        // a different game has none of it
        GameIdentity other;
        other.key = "crc-00000002";
        other.title = "Test Game";
        rec.log.clear();
        m.gameLoaded(other);
        CHECK(m.enabledCount() == 0);
        CHECK(rec.log.size() == 1 && rec.log[0] == "reset");

        // closing clears the core's list
        rec.log.clear();
        m.gameClosed();
        CHECK(!m.active());
        CHECK(rec.log.size() == 1 && rec.log[0] == "reset");

        // by name, the way the command line does it
        m.gameLoaded(g);
        m.disableAll();
        CHECK(m.enabledCount() == 0);
        CHECK(m.setEnabledByName("lives", true));
        CHECK(m.setEnabledByName("0", true));
        CHECK(!m.setEnabledByName("Garbage code", true));
        CHECK(!m.setEnabledByName("no such cheat", true));
        CHECK(m.setEnabledByName("My lives", true));
        CHECK(m.enabledCount() == 2);
    }
    {
        CheatManager m;
        m.setStateDir(state);
        m.setLibraryDirs({VJ_TEST_FIXTURES});
        m.rescan();
        m.gameLoaded(g);
        CHECK(m.enabledCount() == 2);
        m.disableAll();
    }
    {
        CheatManager m;
        m.setStateDir(state);
        m.setLibraryDirs({VJ_TEST_FIXTURES});
        m.rescan();
        m.gameLoaded(g);
        CHECK(m.enabledCount() == 0);
        CHECK(m.userCheats().size() == 2);   // turned off, not deleted
        m.removeUser(0);
        m.removeUser(0);
    }
    {
        CheatManager m;
        m.setStateDir(state);
        m.setLibraryDirs({VJ_TEST_FIXTURES});
        m.rescan();
        m.gameLoaded(g);
        CHECK(m.userCheats().empty());
    }

    // a game that never had a cheat touched leaves no file behind
    {
        CheatManager m;
        m.setStateDir(state);
        m.setLibraryDirs({VJ_TEST_FIXTURES});
        m.rescan();
        GameIdentity quiet;
        quiet.key = "crc-0000000f";
        quiet.title = "Test Game";
        m.gameLoaded(quiet);
        CHECK(!fs::exists(tmp / "games" / "crc-0000000f.ini"));
    }
    fs::remove_all(tmp);
}

// ---------------------------------------------------------------------------------
static int corpus(const std::string& dir) {
    unsigned files = 0, total = 0, supported = 0, merged = 0;
    std::vector<std::string> unsupported;
    std::error_code ec;
    for (const auto& e : fs::directory_iterator(dir, ec)) {
        if (e.path().extension() != ".cht") continue;
        ChtSet s;
        files++;
        if (!loadChtFile(e.path().string(), s)) {
            std::fprintf(stderr, "FAIL %s: no cheats parsed\n", e.path().string().c_str());
            g_fail++;
            continue;
        }
        std::vector<std::string> ids;
        for (const ChtCheat& c : s.cheats) {
            total++;
            ids.push_back(c.id);
            if (c.parts > 1) merged++;
            if (c.supported) {
                supported++;
                unsigned segs = 0;
                std::string why;
                if (!validateCode(c.code, &segs, &why) || segs != c.segments) {
                    std::fprintf(stderr, "FAIL %s: %s: %s\n", s.stem.c_str(), c.desc.c_str(), why.c_str());
                    g_fail++;
                }
                // The memory it writes must lie inside the 2 MB of RAM the database addresses.
                Ram r;
                play(r, c.code);
            } else {
                unsupported.push_back(s.stem + ": " + c.desc + " -- " + c.note);
                if (c.note.empty()) {
                    std::fprintf(stderr, "FAIL %s: %s is unsupported with no reason\n", s.stem.c_str(), c.desc.c_str());
                    g_fail++;
                }
            }
        }
        std::sort(ids.begin(), ids.end());
        if (std::adjacent_find(ids.begin(), ids.end()) != ids.end()) {
            std::fprintf(stderr, "FAIL %s: duplicate cheat ids\n", s.stem.c_str());
            g_fail++;
        }
    }
    std::printf("corpus: %u files, %u cheats (%u merged multi-part), %u supported, %zu unsupported\n", files, total,
                merged, supported, unsupported.size());
    for (const std::string& u : unsupported) std::printf("  unsupported: %s\n", u.c_str());
    if (files == 0) {
        std::fprintf(stderr, "FAIL no .cht files in %s\n", dir.c_str());
        return 1;
    }
    if (supported * 100 < total * 95) {
        std::fprintf(stderr, "FAIL only %u of %u cheats are supported\n", supported, total);
        g_fail++;
    }
    return g_fail ? 1 : 0;
}

// #967: one apply() is one command: the reset, then every set.
static void testCommitBatch() {
    fs::path tmp = fs::temp_directory_path() / "vj-cheats-commit";
    fs::remove_all(tmp);
    GameIdentity g;
    g.key = "crc-deadbeef";
    g.title = "Test Game";
    g.fileStem = "Test Game (World)";
    CheatManager m;
    Recorder r;
    r.attach(m);
    m.setStateDir(tmp.string() + "/games/");
    m.gameLoaded(g);
    std::string err;
    CHECK(m.addUser("My lives", "00001000:07", err));   // one enabled user cheat, so apply() has a set to send
    r.log.clear();
    r.batches = 0;
    m.apply();
    CHECK(r.batches == 1);                                  // one apply, one command
    CHECK(r.log.size() == 2 && r.log[0] == "reset" && r.log[1].compare(0, 3, "set") == 0);
    m.blocked = [] { return true; };
    r.log.clear();
    r.batches = 0;
    m.apply();
    CHECK(r.batches == 1 && r.log.size() == 1 && r.log[0] == "reset");   // blocked: the reset still goes
    fs::remove_all(tmp);
}

// Imported freezes must never follow a matching title onto the wrong revision.
static void testRevisionGuard() {
    fs::path tmp = fs::temp_directory_path() / "vj-cheats-revision-guard";
    fs::create_directories(tmp);
    const std::string entries = "cheat0_desc = \"Imported freeze\"\n"
        "cheat0_code = \"001F0000:AB+001F0002:1234+001F0004:89ABCDEF\"\n"
        "cheat0_enable = true\n";
    auto write = [&](const std::string& guard) {
        std::ofstream(tmp / "Imported.cht", std::ios::binary) << guard << entries;
    };
    write("vj_rom_crc32 = \"1234ABCD\"\n");
    GameIdentity g;
    g.key = "crc-1234abcd";
    g.fileStem = "Imported";
    g.crc = 0x1234ABCD;
    CheatManager m;
    Recorder recorder;
    recorder.attach(m);
    m.setLibraryDirs({tmp.string()});
    m.setStateDir((tmp / "games").string() + "/");
    m.rescan();
    m.gameLoaded(g);
    CHECK(m.selectedSet() && m.selectedSet()->revisionBound);
    CHECK(m.setStatus().empty());
    CHECK(m.appliedSegments() == 0); // source defaults never auto-enable
    CHECK(m.setEnabledByName("Imported freeze", true));
    CHECK(m.appliedSegments() == 3);
    Ram ram;
    play(ram, m.selectedSet()->cheats[0].code);
    CHECK(ram.b[0x1F0000] == 0xAB && ram.b[0x1F0002] == 0x12 && ram.b[0x1F0003] == 0x34);
    CHECK(ram.b[0x1F0004] == 0x89 && ram.b[0x1F0007] == 0xEF);
    m.blocked = [] { return true; };
    m.apply();
    CHECK(m.appliedSegments() == 0);
    m.blocked = nullptr;

    // Saved toggles cannot bypass the guard, even with the same saved key.
    for (uint32_t crc : {0u, 0x87654321u}) {
        g.crc = crc;
        m.gameLoaded(g);
        CHECK(m.selectedSet() && !m.selectedSet()->cheats[0].supported);
        CHECK(m.selectedSet()->cheats[0].code.empty());
        CHECK(!m.setStatus().empty());
        CHECK(!m.setEnabledByName("Imported freeze", true));
        CHECK(m.appliedSegments() == 0);
    }
    g.crc = 0x1234ABCD;
    m.gameLoaded(g);
    CHECK(m.appliedSegments() == 3); // switching back restores the saved toggle
    for (const std::string& guard : {
             "vj_rom_crc32\n", "vj_rom_crc32 = \"1234ABCG\"\n",
             "vj_rom_crc32 = \"1234ABCD\"\nvj_rom_crc32 = \"1234ABCD\"\n",
             "vj_rom_crc32 = \"0x1234ABCD\"\n", "vj_rom_crc32 = \"\"\n"}) {
        write(guard);
        m.gameLoaded(g);
        CHECK(!m.setStatus().empty());
        CHECK(m.appliedSegments() == 0);
        CHECK(!m.setEnabledByName("Imported freeze", true));
    }
    write("\xEF\xBB\xBFvj_rom_crc32 = \"87654321\"\r\n");
    m.gameLoaded(g);
    CHECK(m.appliedSegments() == 0); // BOM must not hide the guard
    write("");
    g.crc = 0;
    m.gameLoaded(g);
    CHECK(m.setStatus().empty());
    CHECK(m.appliedSegments() == 3); // legacy unbound .cht files remain usable
    fs::remove_all(tmp);
}

// ---- community pack (#1071) ----------------------------------------------------------------------
struct PackCheat {
    std::string desc, code;
};
// Write <dir>/<fileCrc>.cht: a revision-bound pack (guard `guardCrc`; "" = no guard line at all).
static void writePack(const fs::path& dir, const std::string& fileCrc, const std::string& guardCrc,
                      const std::vector<PackCheat>& cheats) {
    fs::create_directories(dir);
    std::ofstream f(dir / (fileCrc + ".cht"), std::ios::binary);
    if (!guardCrc.empty()) f << "vj_rom_crc32 = \"" << guardCrc << "\"\n";
    for (size_t i = 0; i < cheats.size(); i++)
        f << "cheat" << i << "_desc = \"" << cheats[i].desc << "\"\ncheat" << i << "_code = \"" << cheats[i].code
          << "\"\ncheat" << i << "_enable = true\n";
}
static std::string slurp(const fs::path& p) {
    std::ifstream f(p, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>());
}
static const std::vector<PackCheat> kPack = {
    {"Infinite lives", "001F0000:AB"}, {"Max score", "001F0010:1234"}, {"Gold", "001F0020:01+001F0021:02"}};
static GameIdentity cartGame(uint32_t crc, const char* title = "Unmatched Cart") {
    GameIdentity g;
    char key[32];
    std::snprintf(key, sizeof key, "crc-%08x", crc);
    g.key = key;
    g.title = title;
    g.fileStem = title;
    g.crc = crc;
    return g;
}

static void testCommunityLoad() {
    fs::path tmp = fs::temp_directory_path() / "vj-cheats-community-load";
    fs::remove_all(tmp);
    const fs::path packs = tmp / "packs";
    writePack(packs, "5E2CDBC0", "5E2CDBC0", kPack);
    CheatManager m;
    Recorder rec;
    rec.attach(m);
    m.setStateDir(tmp.string() + "/games/");
    m.setLibraryDirs({VJ_TEST_FIXTURES});
    m.setCommunityDir(packs.string());
    m.rescan();

    // Found by CRC alone: no library match, no selectSet.
    m.gameLoaded(cartGame(0x5E2CDBC0));
    CHECK(m.selectedSet() == nullptr);
    const ChtSet* c = m.communitySet();
    CHECK(c != nullptr);
    if (!c) return;
    CHECK(c->cheats.size() == 3 && c->restriction.empty() && c->revisionBound && c->romCrc == 0x5E2CDBC0);
    CHECK(c->cheats[0].supported && c->cheats[2].supported && c->cheats[2].segments == 2);
    CHECK(m.enabledCount() == 0 && m.appliedSegments() == 0);   // never auto-enabled
    // Independent of the database set: choosing one leaves the pack alone.
    CHECK(m.selectSet("Test Game"));
    CHECK(m.communitySet() && m.communitySet()->cheats.size() == 3);
    CHECK(m.selectSet(""));

    // Another cartridge: no file, no pack.
    m.gameLoaded(cartGame(0x12345678));
    CHECK(m.communitySet() == nullptr);

    // Closing the game forgets the pack.
    m.gameLoaded(cartGame(0x5E2CDBC0));
    CHECK(m.communitySet() != nullptr);
    m.gameClosed();
    CHECK(m.communitySet() == nullptr);

    // A disc (crc 0) never loads a pack, even when a 00000000.cht is lying there.
    writePack(packs, "00000000", "00000000", kPack);
    GameIdentity disc;
    disc.key = "title-some-disc";
    disc.title = "Some Disc";
    disc.fileStem = "Some Disc";
    m.gameLoaded(disc);
    CHECK(m.communitySet() == nullptr);

    // Guard for another CRC: loaded, every cheat unsupported, restriction set, nothing applied.
    writePack(packs, "5E2CDBC0", "11111111", kPack);
    m.gameLoaded(cartGame(0x5E2CDBC0));
    c = m.communitySet();
    CHECK(c != nullptr && !c->restriction.empty());
    if (c) {
        for (const ChtCheat& k : c->cheats) CHECK(!k.supported && k.code.empty() && k.segments == 0 && !k.note.empty());
        m.setEnabled(c->cheats[0], true, CheatOrigin::Community);   // refused: unsupported
        CHECK(!m.isEnabled(c->cheats[0], CheatOrigin::Community));
        CHECK(!m.setEnabledByName("Infinite lives", true));
    }
    CHECK(m.appliedSegments() == 0);
    // Unbound pack (no guard line) and a malformed guard are restricted the same way.
    for (const std::string& guard : {std::string(), std::string("5E2CDBCG")}) {
        writePack(packs, "5E2CDBC0", guard, kPack);
        m.gameLoaded(cartGame(0x5E2CDBC0));
        c = m.communitySet();
        CHECK(c != nullptr && !c->restriction.empty() && !c->cheats[0].supported);
        CHECK(m.appliedSegments() == 0);
    }
    // The matching guard is back to normal.
    writePack(packs, "5E2CDBC0", "5E2CDBC0", kPack);
    m.gameLoaded(cartGame(0x5E2CDBC0));
    CHECK(m.communitySet() && m.communitySet()->restriction.empty() && m.communitySet()->cheats[0].supported);
    // A file that holds no cheat at all is not a pack.
    writePack(packs, "5E2CDBC0", "5E2CDBC0", {});
    m.gameLoaded(cartGame(0x5E2CDBC0));
    CHECK(m.communitySet() == nullptr);
    fs::remove_all(tmp);
}

static void testCommunityTogglesAcrossUpdate() {
    fs::path tmp = fs::temp_directory_path() / "vj-cheats-community-toggles";
    fs::remove_all(tmp);
    const fs::path packs = tmp / "packs";
    const std::string state = tmp.string() + "/games/";
    const GameIdentity g = cartGame(0x5E2CDBC0);
    writePack(packs, "5E2CDBC0", "5E2CDBC0", kPack);
    std::string livesId, scoreId;
    {
        CheatManager m;
        Recorder rec;
        rec.attach(m);
        m.setStateDir(state);
        m.setCommunityDir(packs.string());
        m.gameLoaded(g);
        const ChtSet* c = m.communitySet();
        CHECK(c != nullptr);
        if (!c) return;
        livesId = c->cheats[0].id;
        scoreId = c->cheats[1].id;
        m.setEnabled(c->cheats[0], true, CheatOrigin::Community);
        m.setEnabled(c->cheats[1], true, CheatOrigin::Community);
        CHECK(m.isEnabled(c->cheats[0], CheatOrigin::Community) && m.isEnabled(c->cheats[1], CheatOrigin::Community));
        CHECK(!m.isEnabled(c->cheats[2], CheatOrigin::Community));
        // The database namespace is separate: same ids, other origin, still off.
        CHECK(!m.isEnabled(c->cheats[0], CheatOrigin::Database));
        CHECK(m.enabledCount() == 2 && m.appliedSegments() == 2);
    }
    const std::string ini = slurp(state + g.key + ".ini");
    CHECK(ini.find("cp." + livesId) != std::string::npos && ini.find("cp." + scoreId) != std::string::npos);
    CHECK(ini.find("on.") == std::string::npos);

    // A fresh manager (restart) still has both on.
    CheatManager m;
    Recorder rec;
    rec.attach(m);
    m.setStateDir(state);
    m.setCommunityDir(packs.string());
    m.gameLoaded(g);
    CHECK(m.communitySet() != nullptr && m.enabledCount() == 2);
    CHECK(m.isEnabled(m.communitySet()->cheats[0], CheatOrigin::Community));
    CHECK(rec.log.size() == 3 && rec.log[0] == "reset" && rec.log[1] == "set 5000 on 001F0000:AB" &&
          rec.log[2] == "set 5001 on 001F0010:1234");

    // The pack is updated: "Max score" has a new code, the others are untouched.
    writePack(packs, "5E2CDBC0", "5E2CDBC0",
              {kPack[0], {"Max score", "001F0010:5678"}, kPack[2]});
    rec.log.clear();
    m.reloadCommunity();
    const ChtSet* c = m.communitySet();
    CHECK(c != nullptr);
    if (!c) return;
    CHECK(c->cheats[0].id == livesId && c->cheats[1].id != scoreId);
    CHECK(m.isEnabled(c->cheats[0], CheatOrigin::Community));
    CHECK(!m.isEnabled(c->cheats[1], CheatOrigin::Community));   // changed: back off
    CHECK(m.enabledCount() == 1 && m.appliedSegments() == 1);
    CHECK(rec.log.size() == 2 && rec.log[0] == "reset" && rec.log[1] == "set 5000 on 001F0000:AB");
    const std::string ini2 = slurp(state + g.key + ".ini");
    CHECK(ini2.find("cp." + livesId) != std::string::npos);
    CHECK(ini2.find("cp." + scoreId) == std::string::npos);   // no stale key
    // ...and after a restart too.
    CheatManager m2;
    m2.setStateDir(state);
    m2.setCommunityDir(packs.string());
    m2.gameLoaded(g);
    CHECK(m2.enabledCount() == 1 && m2.isEnabled(m2.communitySet()->cheats[0], CheatOrigin::Community));
    CHECK(!m2.isEnabled(m2.communitySet()->cheats[1], CheatOrigin::Community));

    // A stale cp. key (pack replaced while the app was closed) is pruned at load time.
    writePack(packs, "5E2CDBC0", "5E2CDBC0", {kPack[1]});
    CheatManager m3;
    m3.setStateDir(state);
    m3.setCommunityDir(packs.string());
    m3.gameLoaded(g);
    CHECK(m3.enabledCount() == 0);
    CHECK(slurp(state + g.key + ".ini").find("cp.") == std::string::npos);
    fs::remove_all(tmp);
}

static void testApplyOrder() {
    fs::path tmp = fs::temp_directory_path() / "vj-cheats-community-order";
    fs::remove_all(tmp);
    const fs::path packs = tmp / "packs";
    GameIdentity g = cartGame(0x5E2CDBC0, "Test Game");
    g.fileStem = "Test Game (World)";
    writePack(packs, "5E2CDBC0", "5E2CDBC0", kPack);
    CheatManager m;
    Recorder rec;
    rec.attach(m);
    std::vector<std::string> warns;
    m.warn = [&](const std::string& w) { warns.push_back(w); };
    m.setStateDir(tmp.string() + "/games/");
    m.setLibraryDirs({VJ_TEST_FIXTURES});
    m.setCommunityDir(packs.string());
    m.rescan();
    m.gameLoaded(g);
    CHECK_EQ(m.selectedStem(), "Test Game");
    const ChtSet* db = m.selectedSet();
    const ChtSet* cp = m.communitySet();
    CHECK(db && cp);
    if (!db || !cp) return;
    const ChtCheat* lives = find(*db, "Lives");
    const ChtCheat* score = find(*db, "Score");
    CHECK(lives && score);
    if (!lives || !score) return;
    std::string err;
    // Enable in the "wrong" order: user first, then the pack, then the database.
    CHECK(m.addUser("Mine", "00002000:11", err));
    m.setEnabled(cp->cheats[2], true, CheatOrigin::Community);
    m.setEnabled(cp->cheats[0], true, CheatOrigin::Community);
    m.setEnabled(*score, true);
    m.setEnabled(*lives, true);
    const size_t livesIdx = size_t(lives - &db->cheats[0]), scoreIdx = size_t(score - &db->cheats[0]);
    CHECK(livesIdx < scoreIdx);
    rec.log.clear();
    m.apply();
    // reset, then database (set order), community (5000 + i), user (10000 + i).
    const std::vector<std::string> want = {
        "reset",
        "set " + std::to_string(livesIdx) + " on " + lives->code,
        "set " + std::to_string(scoreIdx) + " on " + score->code,
        "set 5000 on 001F0000:AB",
        "set 5002 on 001F0020:01+001F0021:02",
        "set 10000 on 00002000:11"};
    CHECK(rec.log.size() == want.size());
    for (size_t i = 0; i < want.size() && i < rec.log.size(); i++) CHECK_EQ(rec.log[i], want[i]);
    CHECK(m.appliedSegments() == lives->segments + score->segments + 1 + 2 + 1);
    CHECK(m.enabledCount() == 5);
    CHECK(warns.empty());

    // Replay the batch through the core's own list: the writes all land.
    Ram ram;
    static cheat_list_t list;
    cheat_list_reset(&list);
    CHECK(rec.log.size() == want.size());
    for (size_t i = 1; i < rec.log.size(); i++) {
        const size_t sp1 = rec.log[i].find(' '), sp2 = rec.log[i].find(' ', sp1 + 1), sp3 = rec.log[i].find(' ', sp2 + 1);
        cheat_list_set(&list, (uint32_t)std::strtoul(rec.log[i].substr(sp1 + 1, sp2 - sp1 - 1).c_str(), nullptr, 10), true,
                       rec.log[i].substr(sp3 + 1).c_str());
    }
    cheat_list_apply(&list, ramWrite, &ram);
    CHECK(ram.b[0x1F0000] == 0xAB && ram.b[0x1F0020] == 0x01 && ram.b[0x1F0021] == 0x02 && ram.b[0x2000] == 0x11);

    // disableAll clears the pack too, and names reach the pack (after the database, before the manual codes).
    m.disableAll();
    CHECK(m.enabledCount() == 0 && m.appliedSegments() == 0);
    CHECK(slurp(tmp / "games" / (g.key + ".ini")).find("cp.") == std::string::npos);
    CHECK(m.setEnabledByName("max score", true));          // community desc, case-insensitive
    CHECK(m.isEnabled(cp->cheats[1], CheatOrigin::Community) && !m.isEnabled(*score));
    CHECK(m.setEnabledByName("Lives", true));              // database wins over nothing else here
    CHECK(m.isEnabled(*lives) && !m.isEnabled(cp->cheats[0], CheatOrigin::Community));
    CHECK(m.setEnabledByName("Mine", true));
    CHECK(!m.setEnabledByName("No such cheat", true));
    CHECK(m.setEnabledByName(cp->cheats[2].id, true));
    CHECK(m.isEnabled(cp->cheats[2], CheatOrigin::Community));

    // Above the core's limit: every segment is counted and the single warning fires.
    m.disableAll();
    std::vector<PackCheat> big;
    for (int i = 0; i < 100; i++) {
        char code[64];
        std::snprintf(code, sizeof code, "%08X:01+%08X:02+%08X:03", 0x10000 + i * 4, 0x10001 + i * 4, 0x10002 + i * 4);
        big.push_back({"Big " + std::to_string(i), code});
    }
    writePack(packs, "5E2CDBC0", "5E2CDBC0", big);
    m.reloadCommunity();
    warns.clear();
    for (const ChtCheat& k : m.communitySet()->cheats) m.setEnabled(k, true, CheatOrigin::Community);
    CHECK(m.appliedSegments() == 300);
    CHECK(!warns.empty() && warns.back().find("300") != std::string::npos);
    fs::remove_all(tmp);
}

static void testCommunityHardcore() {
    fs::path tmp = fs::temp_directory_path() / "vj-cheats-community-hardcore";
    fs::remove_all(tmp);
    const fs::path packs = tmp / "packs";
    writePack(packs, "5E2CDBC0", "5E2CDBC0", kPack);
    CheatManager m;
    Recorder rec;
    rec.attach(m);
    m.setStateDir(tmp.string() + "/games/");
    m.setCommunityDir(packs.string());
    m.gameLoaded(cartGame(0x5E2CDBC0));
    const ChtSet* c = m.communitySet();
    CHECK(c != nullptr);
    if (!c) return;
    m.setEnabled(c->cheats[0], true, CheatOrigin::Community);
    CHECK(m.appliedSegments() == 1);
    m.blocked = [] { return true; };
    rec.log.clear();
    m.apply();
    CHECK(rec.log.size() == 1 && rec.log[0] == "reset");   // only the reset reaches the core
    CHECK(m.appliedSegments() == 0);
    // The saved selection is neither touched nor forgotten: it goes back in once the block lifts.
    CHECK(m.isEnabled(c->cheats[0], CheatOrigin::Community) && m.enabledCount() == 1);
    m.reloadCommunity();   // an install during hardcore is data-only
    CHECK(rec.log.size() == 2 && rec.log[0] == "reset" && rec.log[1] == "reset");
    m.blocked = nullptr;
    rec.log.clear();
    m.apply();
    CHECK(rec.log.size() == 2 && rec.log[1] == "set 5000 on 001F0000:AB");
    fs::remove_all(tmp);
}

static void testRemoveToggles() {
    fs::path tmp = fs::temp_directory_path() / "vj-cheats-community-remove";
    fs::remove_all(tmp);
    const fs::path packs = tmp / "packs";
    const std::string state = tmp.string() + "/games/";
    GameIdentity g = cartGame(0x5E2CDBC0, "Test Game");
    g.fileStem = "Test Game (World)";
    writePack(packs, "5E2CDBC0", "5E2CDBC0", kPack);
    CheatManager m;
    Recorder rec;
    rec.attach(m);
    m.setStateDir(state);
    m.setLibraryDirs({VJ_TEST_FIXTURES});
    m.setCommunityDir(packs.string());
    m.rescan();
    m.gameLoaded(g);
    const ChtSet* db = m.selectedSet();
    const ChtSet* cp = m.communitySet();
    CHECK(db && cp);
    if (!db || !cp) return;
    const ChtCheat* lives = find(*db, "Lives");
    CHECK(lives != nullptr);
    if (!lives) return;
    m.setEnabled(*lives, true);
    m.setEnabled(cp->cheats[0], true, CheatOrigin::Community);
    m.setEnabled(cp->cheats[1], true, CheatOrigin::Community);
    CHECK(slurp(state + g.key + ".ini").find("cp.") != std::string::npos);

    // The store deletes the pack, then asks the manager to forget it.
    fs::remove(packs / "5E2CDBC0.cht");
    m.reloadCommunity();
    CHECK(m.communitySet() == nullptr);
    rec.log.clear();
    m.removeCommunityToggles();
    CHECK(slurp(state + g.key + ".ini").find("cp.") == std::string::npos);
    CHECK(slurp(state + g.key + ".ini").find("on.") != std::string::npos);   // the database toggle survives
    CHECK(m.isEnabled(*lives) && m.enabledCount() == 1);
    CHECK(rec.log.size() == 2 && rec.log[0] == "reset" && rec.log[1].compare(0, 5, "set 0") == 0);

    // Reinstalling starts with everything off, also after a restart.
    writePack(packs, "5E2CDBC0", "5E2CDBC0", kPack);
    CheatManager m2;
    m2.setStateDir(state);
    m2.setLibraryDirs({VJ_TEST_FIXTURES});
    m2.setCommunityDir(packs.string());
    m2.rescan();
    m2.gameLoaded(g);
    CHECK(m2.communitySet() != nullptr);
    if (m2.communitySet())
        for (const ChtCheat& k : m2.communitySet()->cheats) CHECK(!m2.isEnabled(k, CheatOrigin::Community));
    // No game: nothing to do, no crash.
    CheatManager idle;
    idle.removeCommunityToggles();
    idle.reloadCommunity();
    CHECK(idle.communitySet() == nullptr);
    fs::remove_all(tmp);
}

// An unsupported database cheat does not shadow a same-named supported community cheat.
static void testSetEnabledByNameFallthrough() {
    fs::path tmp = fs::temp_directory_path() / "vj-cheats-community-fallthrough";
    fs::remove_all(tmp);
    const fs::path lib = tmp / "lib", packs = tmp / "packs";
    fs::create_directories(lib);
    std::ofstream(lib / "Fall Game.cht", std::ios::binary)
        << "cheat0_desc = \"Infinite Lives\"\ncheat0_code = \"not a code\"\ncheat0_enable = true\n";
    auto setup = [&](CheatManager& m, Recorder& rec) {
        rec.attach(m);
        m.setStateDir(tmp.string() + "/games/");
        m.setLibraryDirs({lib.string()});
        m.setCommunityDir(packs.string());
        m.rescan();
        m.gameLoaded(cartGame(0x5E2CDBC0, "Fall Game"));
    };
    // No community match: still false, nothing enabled.
    {
        CheatManager m;
        Recorder rec;
        setup(m, rec);
        CHECK(m.selectedSet() && !m.selectedSet()->cheats[0].supported);
        CHECK(!m.setEnabledByName("Infinite Lives", true));
        CHECK(m.enabledCount() == 0);
    }
    // Same name in the pack and supported: that one is enabled.
    writePack(packs, "5E2CDBC0", "5E2CDBC0", {{"Infinite Lives", "001F0000:AB"}});
    CheatManager m;
    Recorder rec;
    setup(m, rec);
    CHECK(m.setEnabledByName("Infinite Lives", true));
    CHECK(m.isEnabled(m.communitySet()->cheats[0], CheatOrigin::Community));
    CHECK(!m.isEnabled(m.selectedSet()->cheats[0]));
    CHECK(m.enabledCount() == 1 && m.appliedSegments() == 1);
    fs::remove_all(tmp);
}

// FIPS 180-4 / NIST vectors.  The one-million-'a' message crosses many blocks; the 55/56/64-byte
// lengths straddle the padding boundary (55: the length field still fits in the one block, 56: it forces an extra
// block, 64: exactly one full block of data).
static void testSha256() {
    CHECK_EQ(sha256Hex(nullptr, 0), "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
    CHECK_EQ(sha256Hex("", 0), "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
    CHECK_EQ(sha256Hex("abc", 3), "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
    const std::string two = "abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq";
    CHECK_EQ(sha256Hex(two.data(), two.size()), "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1");
    const std::string mil(1000000, 'a');
    CHECK_EQ(sha256Hex(mil.data(), mil.size()), "cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0");
    // Padding boundaries: 55 bytes = one block, 56 = length no longer fits, 64 = exactly one full block.
    CHECK_EQ(sha256Hex(mil.data(), 55), "9f4390f8d30c2dd92ec9f095b65e2b9ae9b0a925a5258e241c9f1e910f734318");
    CHECK_EQ(sha256Hex(mil.data(), 56), "b35439a4ac6f0948b6d6f9e3c6af0f5f590ce20f1bde7090ef7970686ec6738a");
    CHECK_EQ(sha256Hex(mil.data(), 64), "ffe054fe7ae0cb6dc65c3af9b61d5209f439851db43d0ba5997337df154668eb");
    // Bytes above 0x7F must not be sign-extended.
    const unsigned char hi[4] = {0xff, 0x80, 0x00, 0xfe};
    CHECK_EQ(sha256Hex(hi, 4), "eadfaeb12caa89eba3075f360d3d47938bd2fa6863a01e86bbb3b61b92086ce5");
}

int main(int argc, char** argv) {
    if (argc >= 3 && !std::strcmp(argv[1], "--corpus")) return corpus(argv[2]);
    testHelpers();
    testParse();
    testReplay();
    testLibraryMatching();
    testPersistence();
    testCommitBatch();
    testRevisionGuard();
    testCommunityLoad();
    testCommunityTogglesAcrossUpdate();
    testApplyOrder();
    testCommunityHardcore();
    testRemoveToggles();
    testSetEnabledByNameFallthrough();
    testSha256();
    std::printf("%d checks, %d failed\n", g_checks, g_fail);
    return g_fail ? 1 : 0;
}
