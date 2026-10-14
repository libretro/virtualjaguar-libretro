// Unit tests for the cheat database parser, the conversion to the core's code
// format and the per-game persistence.  Links only cheats.cpp, config.cpp,
// paths.cpp and the core's own cheat.c (so every code we emit is run through the
// real parser): no SDL window, no GL, no emulation.
//
//   vj_cheats_test               the unit tests (fixtures are synthetic .cht files)
//   vj_cheats_test --corpus DIR  parse every .cht in DIR (the downloaded libretro database)
#include "cheats.h"

#include "cheat.h"
#include "paths.h"

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
        m.resetSink = [this] { log.push_back("reset"); };
        m.setSink = [this](unsigned i, bool on, const char* code) {
            log.push_back("set " + std::to_string(i) + (on ? " on " : " off ") + code);
        };
    }
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

int main(int argc, char** argv) {
    if (argc >= 3 && !std::strcmp(argv[1], "--corpus")) return corpus(argv[2]);
    testHelpers();
    testParse();
    testReplay();
    testLibraryMatching();
    testPersistence();
    std::printf("%d checks, %d failed\n", g_checks, g_fail);
    return g_fail ? 1 : 0;
}
