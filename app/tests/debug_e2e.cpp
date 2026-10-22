// The debug tools end to end: the REAL core, through CoreHost and DebugSession (no window).  What the
// standalone app does when the player breaks into a game -- minus the pixels:
//
//   1. a frontend that does not announce VJ_CAP_DEBUG (RetroArch's behaviour) is never handed the interface;
//   2. the app is: CoreHost exposes it, the session attaches, a breakpoint-less "break now" parks the 68000 inside
//      retro_run() and the session's halt pump runs a nested "UI frame" (here a counter) until told to go on;
//   3. while parked, CoreHost refuses everything that would re-enter the core (state load, reset, another load, an
//      option change) and the session can read and write memory;
//   4. step, then continue, then a breakpoint at a PC the game revisits;
//   5. hardcore switched on from inside a halt resumes the core and detaches (the Tools menu goes grey);
//   6. a second game loads cleanly afterwards (the session and the core forget the first one).
//
// usage: vj_debug_e2e <rom.j64>
#include "core_host.h"
#include "debug/debug_session.h"
#include "paths.h"

#include <cstdio>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <string>
#include <vector>

namespace fs = std::filesystem;
using namespace vj;

static int g_fail = 0, g_checks = 0;
#define CHECK(cond)                                                                    \
    do {                                                                               \
        g_checks++;                                                                    \
        if (!(cond)) {                                                                 \
            g_fail++;                                                                  \
            std::fprintf(stderr, "FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond);       \
        }                                                                              \
    } while (0)

int main(int argc, char** argv) {
    if (argc < 2) {
        std::fprintf(stderr, "usage: vj_debug_e2e <rom.j64>\n");
        return 2;
    }
    const std::string rom = argv[1];
    std::error_code ec;
    fs::path tmp = fs::temp_directory_path(ec);
    if (ec) tmp = ".";
    tmp /= "vj-debug-e2e-" + std::to_string((long long)std::time(nullptr)) + "-" + std::to_string((long long)clock());
    fs::create_directories(tmp / "system", ec);
    fs::create_directories(tmp / "saves", ec);
    const std::string t = tmp.string() + "/";

    CoreHost& host = CoreHost::instance();
    host.setDirectories(t + "system/", t + "saves/");
    std::string err;

    DebugSession dbg;
    host.onDebugIface = [&](const vj_debug_iface* i) { dbg.setIface(i); };

    // 1. not announced: no interface, the tools stay off.
    host.announceDebug = false;
    CHECK(host.loadContent(rom, err));
    CHECK(host.debugIface() == nullptr && !dbg.hasCore());
    dbg.setContentLoaded(true);
    CHECK(!dbg.enabled() && !dbg.attach());
    for (int i = 0; i < 5; i++) host.runFrame();
    host.announceDebug = true;

    // 2. announced: the interface is there, and so is the session.
    CHECK(host.loadContent(rom, err));
    CHECK(host.debugIface() != nullptr && dbg.hasCore());
    CHECK(dbg.core().iface() == host.debugIface());
    dbg.setContentLoaded(true);
    CHECK(dbg.enabled());
    for (int i = 0; i < 30; i++) host.runFrame();
    CHECK(!host.inFrame());

    // Memory through the session (no halt needed).
    std::vector<uint8_t> ram;
    CHECK(dbg.snapshotDram(ram) && ram.size() == DebugSession::kDramSize);
    uint8_t* core = nullptr;
    size_t coreSize = 0;
    CHECK(host.memoryRegion(RETRO_MEMORY_SYSTEM_RAM, core, coreSize) && coreSize == ram.size());
    CHECK(core && std::memcmp(core, ram.data(), ram.size()) == 0);   // the app's own view == the debug view
    CHECK(dbg.pokeU(0x7000, 4, 0xCAFEF00D) == 4);
    unsigned v = 0;
    CHECK(dbg.readU(0x7000, 4, v) && v == 0xCAFEF00D);
    CHECK(dbg.pokeU(0xF02238, 4, 0) == VJ_DBG_ERR_POLICY);           // B_CMD: needs both switches
    CHECK(dbg.pokeU(0x800000, 4, 0) == VJ_DBG_ERR_READONLY);
    CHECK(dbg.regions().size() >= 8);

    // 3. break now: the nested UI frame runs inside runFrame().
    int ui = 0, halts = 0, resumes = 0;
    bool refusedWhileHalted = false;
    unsigned haltPc = 0;
    bool stepped = false;
    dbg.core().onHalt.push_back([&](int target, unsigned pc, int reason) {
        halts++;
        if (halts == 1) {
            CHECK(target == VJ_DBG_TARGET_M68K);
            CHECK(reason == VJ_DBG_STOP_USER);
            haltPc = pc;
        }
    });
    dbg.core().onResume.push_back([&] { resumes++; });
    dbg.core().runUiFrame = [&] {
        dbg.syncHalt();
        ui++;
        CHECK(dbg.halted() && host.inFrame());
        if (ui == 1) {
            // Everything that would re-enter the core is refused while it is parked in retro_run().
            std::string e2;
            CHECK(!host.saveStateToFile(t + "x.state", e2) && !e2.empty());
            CHECK(!host.loadStateFromFile(t + "x.state", e2));
            CHECK(!host.loadContent(rom, e2) && !e2.empty());
            CHECK(!host.loadNoContent(e2));
            const unsigned frames = (unsigned)host.frameCount();
            host.runFrame();                          // a second retro_run() inside the first: ignored
            CHECK(host.frameCount() == frames);
            host.reset();                             // retro_reset(): ignored
            host.setOptionValue("virtualjaguar_blitter_timing", "enabled");
            CHECK(host.optionValueFor("virtualjaguar_blitter_timing", "unset") == "unset");
            host.unloadContent();                     // would pull the core out from under itself
            CHECK(host.isLoaded());
            refusedWhileHalted = true;
            // The session reads and writes the live machine meanwhile.
            std::vector<uint8_t> b;
            CHECK(dbg.read(0x7000, b, 4) && b[0] == 0xCA);
            vj_debug_regs r;
            CHECK(dbg.core().iface()->get_regs(VJ_DBG_TARGET_M68K, &r) && r.pc == haltPc);
        }
        if (ui == 2) dbg.step();                      // single-step: a second halt follows at once
        if (ui == 4) dbg.resume();
    };
    dbg.breakNow(VJ_DBG_TARGET_M68K);
    CHECK(dbg.attached());
    const uint64_t before = host.frameCount();
    host.runFrame();
    CHECK(host.frameCount() == before + 1);           // the frame finished
    CHECK(refusedWhileHalted);
    CHECK(ui >= 4 && halts >= 2);                     // the step halted again
    CHECK(resumes == 1);                              // only the final continue counts as a resume
    dbg.syncHalt();
    CHECK(!dbg.halted() && !host.inFrame());
    stepped = halts >= 2;
    CHECK(stepped);
    // Back to normal: option changes work again.
    host.setOptionValue("virtualjaguar_blitter_timing", "enabled");
    CHECK(host.optionValueFor("virtualjaguar_blitter_timing", "unset") == "enabled");
    host.resetOptionToDefault("virtualjaguar_blitter_timing");

    // 4. a breakpoint at a PC the game revisits halts there.
    unsigned bpPc = haltPc;
    CHECK(dbg.addBreakpoint(VJ_DBG_TARGET_M68K, bpPc));
    CHECK(!dbg.addBreakpoint(VJ_DBG_TARGET_M68K, bpPc));
    int bpHits = 0;
    dbg.core().onHalt.clear();
    dbg.core().onHalt.push_back([&](int target, unsigned pc, int reason) {
        if (target == VJ_DBG_TARGET_M68K && pc == bpPc && reason == VJ_DBG_STOP_BREAKPOINT) bpHits++;
    });
    dbg.core().runUiFrame = [&] { dbg.syncHalt(); dbg.resume(); };
    for (int i = 0; i < 90 && bpHits == 0; i++) host.runFrame();
    CHECK(bpHits >= 1);
    dbg.clearBreakpoints();
    const int hitsBefore = bpHits;
    for (int i = 0; i < 60; i++) host.runFrame();
    CHECK(bpHits == hitsBefore);                      // cleared: nothing halts any more

    // 5. hardcore turned on from inside a halt: the core resumes, the session detaches.
    std::string closed;
    dbg.onForcedClose.push_back([&](const char* why) { closed = why; });
    int ui5 = 0;
    dbg.core().runUiFrame = [&] {
        dbg.syncHalt();
        if (++ui5 == 2) dbg.setHardcore(true);        // what App::syncDebug does when ach.hardcoreActive() flips
    };
    dbg.syncHalt();                                   // the mirror must see the last resume before breakNow() is allowed
    dbg.breakNow(VJ_DBG_TARGET_M68K);
    host.runFrame();
    CHECK(ui5 >= 2);
    dbg.syncHalt();
    CHECK(!dbg.halted() && !dbg.attached() && !dbg.enabled());
    CHECK(closed == "hardcore");
    CHECK(!host.inFrame());
    for (int i = 0; i < 10; i++) host.runFrame();     // and the game simply keeps running
    CHECK(dbg.pokeU(0x7000, 4, 0) == VJ_DBG_ERR_STATE);
    dbg.setHardcore(false);
    CHECK(dbg.enabled());

    // 6. the session forgets a closed game; a new load starts clean.
    host.unloadContent();
    CHECK(!host.isLoaded() && host.debugIface() == nullptr && !dbg.hasCore());
    CHECK(host.loadContent(rom, err));
    CHECK(dbg.hasCore());
    dbg.setContentLoaded(true);
    CHECK(dbg.enabled() && !dbg.attached());
    for (int i = 0; i < 5; i++) host.runFrame();
    CHECK(dbg.attach());                               // a fresh core instance accepts a fresh host

    host.unloadContent();
    std::filesystem::remove_all(tmp, ec);
    std::printf("debug_e2e: %d checks, %d failed\n", g_checks, g_fail);
    return g_fail ? 1 : 0;
}
