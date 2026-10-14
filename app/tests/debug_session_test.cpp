// Unit tests for DebugSession (app/src/debug/debug_session.*) against the mock core interface
// (debug_mock.h).  No window, no GL, no core: this is the logic every tool window rests on --
// when the tools are available, the attach / halt / resume protocol, the breakpoint list, the poke
// policy as the user sees it, and the refresh cadence.
#include "debug_mock.h"
#include "debug_session.h"

#include <cstdio>
#include <string>
#include <vector>

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

static void testAvailability() {
    DebugSession s;
    CHECK(!s.enabled());
    CHECK(!s.hasCore());
    CHECK(!s.disabledReason().empty());
    CHECK(!s.attach());               // nothing to attach to
    MockDebugIface m;
    s.setIface(m.iface());
    CHECK(s.hasCore() && !s.enabled());          // no game yet
    CHECK(s.disabledReason().find("game") != std::string::npos);
    s.setContentLoaded(true);
    CHECK(s.enabled() && s.disabledReason().empty());
    s.setHardcore(true);
    CHECK(!s.enabled());
    CHECK(s.disabledReason().find("hardcore") != std::string::npos);
    CHECK(!s.attach());
    s.setHardcore(false);
    CHECK(s.enabled());
}

static void testAttachAndBreakpoints() {
    DebugSession s;
    MockDebugIface m;
    s.setIface(m.iface());
    s.setContentLoaded(true);

    // Adding a breakpoint attaches (the core needs a host before it can halt).
    CHECK(!s.attached());
    CHECK(s.addBreakpoint(VJ_DBG_TARGET_M68K, 0x4000));
    CHECK(s.attached() && m.attached());
    CHECK(!s.addBreakpoint(VJ_DBG_TARGET_M68K, 0x4000));            // duplicate
    CHECK(!s.addBreakpoint(7, 0x4000));                              // bad target
    CHECK(s.addBreakpoint(VJ_DBG_TARGET_GPU, 0xF03000));
    CHECK(m.breakpoints.size() == 2);
    CHECK(s.hasBreakpoint(VJ_DBG_TARGET_GPU, 0xF03000));
    CHECK(s.toggleBreakpoint(VJ_DBG_TARGET_GPU, 0xF03010));          // now set
    CHECK(!s.toggleBreakpoint(VJ_DBG_TARGET_GPU, 0xF03010));         // now cleared
    CHECK(m.breakpoints.size() == 2);

    s.setBreakpointEnabled(VJ_DBG_TARGET_M68K, 0x4000, false);
    CHECK(m.breakpoints.size() == 1 && s.hasBreakpoint(VJ_DBG_TARGET_M68K, 0x4000));   // kept in the list, off in the core
    s.setBreakpointEnabled(VJ_DBG_TARGET_M68K, 0x4000, true);
    CHECK(m.breakpoints.size() == 2);

    // detach clears the core's table but the session list is the source of truth...
    s.detach();
    CHECK(!s.attached() && !m.attached() && m.breakpoints.empty());
    CHECK(s.breakpoints().size() == 2);
    // ...and the next attach rebuilds it.
    CHECK(s.attach());
    CHECK(m.breakpoints.size() == 2);

    s.removeBreakpoint(VJ_DBG_TARGET_M68K, 0x4000);
    CHECK(m.breakpoints.size() == 1);
    s.clearBreakpoints();
    CHECK(s.breakpoints().empty() && m.breakpoints.empty());

    // A core that refuses the host.
    s.detach();
    m.refuseAttach = true;
    CHECK(!s.attach() && !s.attached());
    CHECK(!s.addBreakpoint(VJ_DBG_TARGET_M68K, 0x10));
    m.refuseAttach = false;

    // Unloading content drops the attachment without calling into the core, and closes the tools.
    CHECK(s.attach());
    std::string why;
    s.onForcedClose.push_back([&](const char* w) { why = w; });
    s.setContentLoaded(false);
    CHECK(!s.attached() && why == "closed");
    CHECK(!s.enabled());
}

static void testHaltResume() {
    DebugSession s;
    MockDebugIface m;
    s.setIface(m.iface());
    s.setContentLoaded(true);
    CHECK(s.addBreakpoint(VJ_DBG_TARGET_M68K, 0x4000));

    int halts = 0, resumes = 0, uiFrames = 0;
    int lastTarget = -1, lastReason = -1;
    unsigned lastPc = 0;
    s.onHalt.push_back([&](int t, unsigned pc, int r) { halts++; lastTarget = t; lastPc = pc; lastReason = r; });
    s.onResume.push_back([&] { resumes++; });

    // Continue after three nested UI frames; halted() and the halt details are visible meanwhile.
    s.runUiFrame = [&] {
        uiFrames++;
        CHECK(s.halted());
        CHECK(s.haltedTarget() == VJ_DBG_TARGET_M68K && s.haltedPc() == 0x4000);
        CHECK(m.iface()->halted_target() == VJ_DBG_TARGET_M68K);   // the core agrees
        if (uiFrames == 3) s.resume();
    };
    int pumps = 0;
    int v = m.simulateHalt(VJ_DBG_TARGET_M68K, 0x4000, VJ_DBG_STOP_BREAKPOINT, 100, &pumps);
    CHECK(v == VJ_DBG_RESUME_CONTINUE);
    CHECK(uiFrames == 3 && pumps == 3);
    CHECK(halts == 1 && resumes == 1);
    CHECK(lastTarget == VJ_DBG_TARGET_M68K && lastPc == 0x4000 && lastReason == VJ_DBG_STOP_BREAKPOINT);
    CHECK(!s.halted() && s.haltedTarget() == -1);

    // Step: the verdict is STEP, it is not reported as a resume, and a second halt follows on the next pump.
    uiFrames = 0;
    s.runUiFrame = [&] { uiFrames++; s.step(); };
    v = m.simulateHalt(VJ_DBG_TARGET_M68K, 0x4000, VJ_DBG_STOP_BREAKPOINT);
    CHECK(v == VJ_DBG_RESUME_STEP && m.haltedStepTarget == VJ_DBG_TARGET_M68K);
    CHECK(halts == 2 && resumes == 1);   // halted again (onHalt), no onResume for a step
    CHECK(!s.halted());

    // The reason comes from the core.
    s.runUiFrame = [&] { s.resume(); };
    m.simulateHalt(VJ_DBG_TARGET_GPU, 0xF03004, VJ_DBG_STOP_USER);
    CHECK(lastTarget == VJ_DBG_TARGET_GPU && lastReason == VJ_DBG_STOP_USER);

    // resume()/step() outside a halt do nothing.
    s.resume();
    CHECK(!s.resumeQueued());

    // Quit while halted: abortHalt turns into a detach.
    bool quit = false;
    s.abortHalt = [&] { return quit; };
    s.runUiFrame = [&] { quit = true; };
    v = m.simulateHalt(VJ_DBG_TARGET_M68K, 0x4000);
    CHECK(v == VJ_DBG_RESUME_DETACH);
    CHECK(!m.attached() && !s.attached());
}

static void testHardcoreWhileHalted() {
    DebugSession s;
    MockDebugIface m;
    s.setIface(m.iface());
    s.setContentLoaded(true);
    CHECK(s.attach());
    std::string why;
    s.onForcedClose.push_back([&](const char* w) { why = w; });
    s.runUiFrame = [&] { s.setHardcore(true); };   // the player turned hardcore on from inside the halt
    int v = m.simulateHalt(VJ_DBG_TARGET_M68K, 0x1234);
    CHECK(v == VJ_DBG_RESUME_DETACH);
    CHECK(!m.attached() && !s.attached() && !s.halted());
    CHECK(why == "hardcore");
    CHECK(!s.enabled());
    CHECK(!s.addBreakpoint(VJ_DBG_TARGET_M68K, 0x10));

    // Hardcore while running (not halted): detaches immediately.
    DebugSession s2;
    MockDebugIface m2;
    s2.setIface(m2.iface());
    s2.setContentLoaded(true);
    CHECK(s2.attach());
    s2.setHardcore(true);
    CHECK(!m2.attached() && !s2.attached());
}

static void testPokePolicy() {
    DebugSession s;
    MockDebugIface m;
    s.setIface(m.iface());
    s.setContentLoaded(true);
    unsigned v = 0;

    // RAM: free.
    CHECK(s.pokeU(0x1000, 4, 0xDEADBEEF) == 4);
    CHECK(s.readU(0x1000, 4, v) && v == 0xDEADBEEF);
    CHECK(m.dram()[0x1000] == 0xDE && m.dram()[0x1003] == 0xEF);   // big-endian bytes
    CHECK(s.readU(0x1001, 2, v) && v == 0xADBE);
    CHECK(s.readU(0x1001, 1, v) && v == 0xAD);
    CHECK(!s.readU(0x1001, 3, v));
    // CLUT, GPU RAM and DSP RAM are plain RAM.
    CHECK(s.pokeU(0xF00400, 2, 0x1234) == 2);
    CHECK(s.pokeU(0xF03000, 4, 0x11223344) == 4);
    CHECK(s.pokeU(0xF1B000, 4, 0x55667788) == 4);

    // Registers: refused until the user allows them.
    CHECK(s.pokeU(0xF00028, 2, 1) == VJ_DBG_ERR_POLICY);
    CHECK(s.lastPokeError() == VJ_DBG_ERR_POLICY);
    CHECK(std::string(DebugSession::errorText(VJ_DBG_ERR_POLICY)).find("register") != std::string::npos);
    s.allowRegisterWrites = true;
    CHECK(s.pokeU(0xF00028, 2, 1) == 2 && s.lastPokeError() == 0);
    // The side-effecting set needs the second switch.
    const unsigned kDanger[] = {0xF02238, 0xF02114, 0xF1A114, 0xF00020, 0xF1A148, 0xF1A14C};
    for (unsigned a : kDanger) CHECK(s.pokeU(a, 4, 0) == VJ_DBG_ERR_POLICY);
    CHECK(s.pokeU(0xF02239, 1, 0) == VJ_DBG_ERR_POLICY);   // any byte of the register counts
    s.allowDangerousWrites = true;
    for (unsigned a : kDanger) CHECK(s.pokeU(a, 4, 0) == 4);
    // BUTCH is never writable, whatever the switches say.
    CHECK(s.pokeU(0xDFFF00, 4, 0) == VJ_DBG_ERR_POLICY);
    CHECK(s.pokeU(0xDFFFFF, 1, 0) == VJ_DBG_ERR_POLICY);
    // ROM, and nowhere.
    CHECK(s.pokeU(0x800000, 4, 0) == VJ_DBG_ERR_READONLY);
    CHECK(s.pokeU(0xE00000, 4, 0) == VJ_DBG_ERR_READONLY);
    CHECK(s.pokeU(0x400000, 4, 0) == VJ_DBG_ERR_RANGE);
    CHECK(s.pokeU(0xFFFFFF, 4, 0) == VJ_DBG_ERR_RANGE);          // runs off the bus
    // All or nothing: a write that starts in RAM and runs into the unmapped hole changes nothing.
    m.dram()[0x1FFFFE] = 0x77; m.dram()[0x1FFFFF] = 0x88;
    CHECK(s.pokeU(0x1FFFFE, 4, 0xAAAAAAAA) == VJ_DBG_ERR_RANGE);
    CHECK(m.dram()[0x1FFFFE] == 0x77 && m.dram()[0x1FFFFF] == 0x88);

    // Hardcore / no game: everything refused.
    s.setHardcore(true);
    CHECK(s.pokeU(0x1000, 4, 0) == VJ_DBG_ERR_STATE);
    s.setHardcore(false);

    // Reads of unmapped addresses are $FF; past the bus is an error.
    std::vector<uint8_t> b;
    CHECK(s.read(0x400000, b, 4) && b[0] == 0xFF);
    CHECK(!s.read(0xFFFFFF, b, 2));
    CHECK(s.read(0x800000, b, 4) && b[0] == 0xC0 && b[1] == 0xC1);   // the cartridge image
    CHECK(s.snapshotDram(b) && b.size() == DebugSession::kDramSize && b[0x1000] == 0xDE);

    // Regions: the fixed table, names present, ordered by address within a block.
    const std::vector<vj_debug_region>& r = s.regions();
    CHECK(r.size() == 11);
    CHECK(std::string(r[0].name) == "DRAM" && r[0].start == 0 && r[0].len == 0x200000);
    bool sawClut = false;
    for (const auto& x : r) if (std::string(x.name) == "CLUT") sawClut = x.start == 0xF00400 && x.len == 0x400 && (x.flags & VJ_DBG_REGION_RAM);
    CHECK(sawClut);
}

static void testRefreshCadence() {
    DebugSession s;
    MockDebugIface m;
    s.setIface(m.iface());
    s.setContentLoaded(true);

    uint64_t last = s.refreshSerial();
    s.tick(10.00, true);
    CHECK(s.refreshSerial() != last);       // first live tick refreshes
    last = s.refreshSerial();
    s.tick(10.03, true);
    s.tick(10.06, true);
    CHECK(s.refreshSerial() == last);       // inside the 100 ms period
    s.tick(10.11, true);
    CHECK(s.refreshSerial() != last);
    last = s.refreshSerial();
    s.tick(10.30, false);                   // paused: one last refresh...
    CHECK(s.refreshSerial() != last);
    last = s.refreshSerial();
    s.tick(10.40, false);
    s.tick(10.50, false);
    CHECK(s.refreshSerial() == last);       // ...then quiet
    s.requestRefresh();
    CHECK(s.refreshSerial() != last);
    last = s.refreshSerial();
    s.pokeU(0x1000, 1, 5);                  // an edit refreshes
    CHECK(s.refreshSerial() != last);
}

static void testTools() {
    DebugSession s;
    s.registerPlaceholderTools();
    CHECK(s.tools().size() == 7);
    CHECK(s.findTool("memory") && !s.findTool("memory")->toggle);   // greyed until a window registers
    bool opened = false;
    DebugTool t;
    t.id = "memory";
    t.label = "Memory viewer...";
    t.shortcut = "Ctrl+M";
    t.toggle = [&] { opened = !opened; };
    t.isOpen = [&] { return opened; };
    s.registerTool(t);
    CHECK(s.tools().size() == 7);                                   // replaced, not appended
    s.findTool("memory")->toggle();
    CHECK(s.findTool("memory")->isOpen());
    s.registerPlaceholderTools();                                   // never clobbers a real one
    CHECK(s.findTool("memory")->toggle);
    CHECK(s.findTool("nope") == nullptr);
}

static void testMockContent() {
    // The fake OP snapshot has the real bit layout: decode it the way the JTRM says.
    DebugSession s;
    MockDebugIface m;
    s.setIface(m.iface());
    s.setContentLoaded(true);
    vj_debug_op_obj o[VJ_DBG_OP_MAX_OBJECTS];
    unsigned halfline = 0;
    m.opReady = false;
    CHECK(m.iface()->op_snapshot_get(o, VJ_DBG_OP_MAX_OBJECTS, &halfline) == -1);
    m.opReady = true;
    m.iface()->op_snapshot_arm();
    CHECK(m.opArms == 1);
    int n = m.iface()->op_snapshot_get(o, VJ_DBG_OP_MAX_OBJECTS, &halfline);
    CHECK(n == 4 && halfline == 41);
    CHECK(o[0].type == 0 && o[0].phrases == 2 && o[0].address == MockDebugIface::kOpListBase);
    const uint64_t p0 = ((uint64_t)o[0].p0_hi << 32) | o[0].p0_lo, p1 = ((uint64_t)o[0].p1_hi << 32) | o[0].p1_lo;
    CHECK(((p0 >> 3) & 0x7FF) == 100);                                   // YPOS
    CHECK(((p0 >> 14) & 0x3FF) == MockDebugIface::kBitmapH);             // HEIGHT
    CHECK(((p0 >> 43) << 3) == MockDebugIface::kBitmapData);             // DATA
    CHECK((p1 & 0xFFF) == 40 && ((p1 >> 12) & 7) == 3);                  // XPOS, DEPTH = 8bpp
    CHECK(((p1 >> 18) & 0x3FF) == 2 && ((p1 >> 28) & 0x3FF) == 2);       // DWIDTH, IWIDTH
    CHECK(o[1].type == 1 && o[1].phrases == 3 && o[2].type == 3 && o[3].type == 4);
    // Its pixels and CLUT are reachable through peek, like the real thing.
    std::vector<uint8_t> px;
    CHECK(s.read(MockDebugIface::kBitmapData, px, 16) && px[5] == 5);
    CHECK(s.read(0xF00400, px, 4) && px[0] == 0 && px[1] == 255 && px[2] == 1);
    CHECK(m.iface()->cry_to_rgb(0xFFFF) == 0xFFFFFFu);

    // Blit log ring.
    CHECK(m.iface()->blit_log_head() == 5);
    vj_debug_blit b;
    CHECK(m.iface()->blit_log_get(4, &b) && b.seq == 4 && b.cmd == 0x01800005u);
    CHECK(!m.iface()->blit_log_get(5, &b));
    m.setBlitRingForTest(3);
    CHECK(!m.iface()->blit_log_get(1, &b) && m.iface()->blit_log_get(2, &b));
    m.iface()->blit_log_enable(1);
    CHECK(m.blitLogOn);

    // Disassembly and registers.
    char text[64];
    unsigned len = 0;
    CHECK(m.iface()->disasm_risc(VJ_DBG_TARGET_GPU, 0xF03000, text, sizeof text, &len) && len == 2);
    CHECK(m.iface()->disasm_risc(VJ_DBG_TARGET_DSP, 0xF1B006, text, sizeof text, &len) && len == 6);
    CHECK(!m.iface()->disasm_risc(VJ_DBG_TARGET_M68K, 0, text, sizeof text, &len));
    vj_debug_regs r;
    CHECK(m.iface()->get_regs(VJ_DBG_TARGET_M68K, &r) && r.d[3] == 0x1003 && r.pc == 0x4000);
    CHECK(m.iface()->set_reg(VJ_DBG_TARGET_M68K, VJ_DBG_REG_68K_D0 + 3, 7));
    CHECK(m.iface()->get_regs(VJ_DBG_TARGET_M68K, &r) && r.d[3] == 7);
    CHECK(m.iface()->get_regs(VJ_DBG_TARGET_GPU, &r) && r.bank1[2] == 0x102);
    CHECK(m.iface()->set_reg(VJ_DBG_TARGET_GPU, VJ_DBG_REG_RISC_BANK1 + 2, 9));
    CHECK(m.iface()->get_regs(VJ_DBG_TARGET_GPU, &r) && r.bank1[2] == 9 && r.bank0[2] == 2);
    CHECK(!m.iface()->set_reg(VJ_DBG_TARGET_GPU, 99, 0));
    CHECK(m.iface()->texreplace_reload() == 1 && m.texReloads == 1);
    CHECK(m.iface()->frame_number() == 1234);
}

static void testProviders() {
    DebugSession s;
    int regs = 0, draws = 0, toolDraws = 0;
    DebugProvider p;
    p.registerTools = [&](DebugSession& d) {
        regs++;
        DebugTool t;
        t.id = "memory";
        t.label = "Memory viewer...";
        t.toggle = [] {};
        t.draw = [&] { toolDraws++; };
        d.registerTool(t);
    };
    p.draw = [&](DebugSession&) { draws++; };
    s.addProvider(p);
    CHECK(regs == 1 && s.findTool("memory") && s.findTool("memory")->toggle);
    s.drawTools();
    s.drawTools();
    CHECK(draws == 2 && toolDraws == 2 && regs == 1);
}

static void testIfaceSwap() {
    // A new core (content reload) replaces the interface: the old attachment is forgotten, not called.
    DebugSession s;
    {
        MockDebugIface m1;
        s.setIface(m1.iface());
        s.setContentLoaded(true);
        CHECK(s.attach());
        CHECK(s.addBreakpoint(VJ_DBG_TARGET_M68K, 0x4000));
        s.setIface(nullptr);                  // core torn down
        CHECK(!s.attached() && !s.enabled());
    }
    MockDebugIface m2;
    s.setIface(m2.iface());
    CHECK(s.enabled());
    CHECK(s.breakpoints().empty());   // a breakpoint never lingers into the next core instance
    CHECK(s.attach() && m2.attached());
}

int main() {
    testAvailability();
    testAttachAndBreakpoints();
    testHaltResume();
    testHardcoreWhileHalted();
    testPokePolicy();
    testRefreshCadence();
    testTools();
    testProviders();
    testMockContent();
    testIfaceSwap();
    std::printf("debug_session_test: %d checks, %d failed\n", g_checks, g_fail);
    return g_fail ? 1 : 0;
}
