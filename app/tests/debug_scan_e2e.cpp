// Cheat scanner end to end: the REAL core (through CoreHost) running a public ROM headlessly.
//
// A "game" counter is found the way a person would: take a snapshot, let a few frames run, keep what changed,
// keep what went up by one, again.  The survivor is then kept as a cheat through the same CheatManager the
// Cheats window uses, wired to the core's retro_cheat_* entry points, and the core must hold the value:
//   * with no cheat the candidate keeps counting (so the check can fail),
//   * with the cheat it stays at the frozen value over many frames,
//   * switching the cheat off lets it count again,
//   * with hardcore blocking cheats the same "Add as cheat" is refused and the counter keeps counting.
// Needs no phase-B core support: the RAM comes from retro_get_memory_data, the cheat from retro_cheat_set.
//
// usage: vj_debug_scan_e2e <rom.j64>
#include "cheat_scan.h"
#include "cheats.h"
#include "core_host.h"
#include "debug_mock.h"
#include "debug_session.h"
#include "scanner_actions.h"

#include "libretro.h"

#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <filesystem>
#include <set>
#include <string>
#include <vector>

namespace fs = std::filesystem;
using namespace vj;

static int g_fail = 0, g_checks = 0;
#define CHECK(cond)                                                                \
    do {                                                                           \
        g_checks++;                                                                \
        if (!(cond)) {                                                             \
            g_fail++;                                                              \
            std::fprintf(stderr, "FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond);   \
        }                                                                          \
    } while (0)

static bool ramOf(CoreHost& host, const uint8_t*& p, size_t& n) {
    uint8_t* d = nullptr;
    if (!host.memoryRegion(RETRO_MEMORY_SYSTEM_RAM, d, n) || !d) return false;
    p = d;
    return true;
}

int main(int argc, char** argv) {
    if (argc < 2) {
        std::fprintf(stderr, "usage: vj_debug_scan_e2e <rom.j64>\n");
        return 2;
    }
    const std::string rom = argv[1];
    std::error_code ec;
    fs::path tmp = fs::temp_directory_path(ec);
    if (ec) tmp = ".";
    tmp /= "vj-scan-e2e-" + std::to_string((long long)std::time(nullptr)) + "-" + std::to_string((long long)clock());
    fs::create_directories(tmp / "system", ec);
    fs::create_directories(tmp / "saves", ec);
    fs::create_directories(tmp / "cheats", ec);
    const std::string t = tmp.string() + "/";

    CoreHost& host = CoreHost::instance();
    host.setDirectories(t + "system/", t + "saves/");
    std::string err;
    CHECK(host.loadContent(rom, err));
    for (int i = 0; i < 30; i++) host.runFrame();   // past the boot

    const uint8_t* ram = nullptr;
    size_t size = 0;
    CHECK(ramOf(host, ram, size));
    CHECK(size >= 0x200000);
    if (!ram || !size) return 1;

    // ---- find a counter: changed, then increased, then increased by exactly one ------------------------
    CheatScan sc;
    ScanOptions o;      // 8-bit, aligned
    ScanSpec unk;
    CHECK(sc.begin(ram, size, o, unk, &err));
    CHECK(sc.count() == size);
    std::vector<size_t> trace;
    trace.push_back(sc.count());
    ScanSpec ch;
    ch.mode = ScanMode::Changed;
    for (int i = 0; i < 3; i++) {
        host.runFrame();
        CHECK(sc.next(ram, size, ch, &err));
        trace.push_back(sc.count());
    }
    CHECK(sc.count() > 0 && sc.count() < size);
    ScanSpec inc;
    inc.mode = ScanMode::Increased;
    for (int i = 0; i < 2; i++) {
        host.runFrame();
        CHECK(sc.next(ram, size, inc, &err));
        trace.push_back(sc.count());
    }
    ScanSpec by1;
    by1.mode = ScanMode::IncreasedBy;
    by1.value = 1;
    for (int i = 0; i < 3 && sc.count() > 1; i++) {
        host.runFrame();
        CHECK(sc.next(ram, size, by1, &err));
        trace.push_back(sc.count());
    }
    std::printf("scan narrowing:");
    for (size_t c : trace) std::printf(" %zu", c);
    std::printf("\n");
    CHECK(sc.count() >= 1);
    std::vector<uint32_t> found;
    sc.results(0, 1000, found);
    CHECK(!found.empty());
    if (found.empty()) return 1;
    const uint32_t addr = found[0];
    std::printf("counter candidate: 0x%06X\n", (unsigned)addr);

    // ---- control: with no cheat the counter counts --------------------------------------------------------
    auto at = [&] { return (unsigned)ram[addr]; };
    std::set<unsigned> seen;
    unsigned lastV = at();
    unsigned moves = 0;
    for (int i = 0; i < 12; i++) {
        host.runFrame();
        seen.insert(at());
        if (at() != lastV) moves++;
        lastV = at();
    }
    CHECK(moves >= 8);            // it moves nearly every frame
    CHECK(seen.size() >= 6);

    // ---- keep it as a cheat, exactly as the window's "Add as cheat" does ----------------------------------------
    // The Cheats window's CheatManager, wired to the core's own entry points.  The debug tools' session is gated
    // by the mock interface (the real one arrives with the core's debug interface); nothing else of it is used.
    MockDebugIface mock;
    DebugSession session;
    session.setIface(mock.iface());
    session.setContentLoaded(true);
    CheatManager cm;
    bool hardcore = false;
    cm.commit = [](CheatBatch b) {
        for (const CheatOp& o : b) {
            if (o.reset) retro_cheat_reset();
            else retro_cheat_set(o.index, o.enabled, o.code.c_str());
        }
    };
    cm.blocked = [&] { return hardcore; };
    cm.setStateDir(t + "cheats/");
    GameIdentity id;
    id.key = "crc-5ca00001";
    id.title = "scan e2e";
    cm.gameLoaded(id);

    // Hardcore first: the same click is refused, nothing reaches the core, the counter keeps counting.
    hardcore = true;
    CHECK(!scanAddCheat(cm, session, addr, 42, 1, "Counter", err));
    CHECK(cm.userCheats().empty());
    {
        unsigned v0 = at();
        unsigned diff = 0;
        for (int i = 0; i < 6; i++) {
            host.runFrame();
            if (at() != v0) diff++;
        }
        CHECK(diff >= 5);
    }
    hardcore = false;

    CHECK(scanAddCheat(cm, session, addr, 42, 1, "Counter", err));
    CHECK(cm.userCheats().size() == 1);
    CHECK(cm.userCheats()[0].code == scanCheatCode(addr, 42, 1));
    for (int i = 0; i < 10; i++) {
        host.runFrame();
        CHECK(at() == 42);        // the core writes it after every frame, whatever the game did
    }

    // Hardcore starting while the cheat is on: the app re-applies the list (empty), the counter is free again.
    hardcore = true;
    cm.apply();
    {
        unsigned diff = 0;
        for (int i = 0; i < 6; i++) {
            host.runFrame();
            if (at() != 42) diff++;
        }
        CHECK(diff >= 4);
    }
    hardcore = false;
    cm.apply();                   // hardcore over: the saved cheat goes back in
    host.runFrame();
    CHECK(at() == 42);

    // Switched off in the Cheats window: it counts again.
    cm.setUserEnabled(0, false);
    host.runFrame();
    host.runFrame();
    host.runFrame();
    CHECK(at() != 42);

    cm.gameClosed();
    host.unloadContent();
    fs::remove_all(tmp, ec);      // our own temp dir only
    std::printf("scan e2e: %d checks, %d failed\n", g_checks, g_fail);
    return g_fail ? 1 : 0;
}
