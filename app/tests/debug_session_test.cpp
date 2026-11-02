// Unit tests for DebugSession (app/src/debug/debug_session.*) against the mock core interface
// (debug_mock.h).  No window, no GL, no core: this is the logic every tool window rests on --
// when the tools are available, the attach / halt / resume protocol, the breakpoint list, the poke
// policy as the user sees it, and the refresh cadence.
#include "debug_mock.h"
#include "debug_core.h"
#include "debug_session.h"

#include <chrono>
#include <cstdarg>
#include <cstdio>
#include <string>
#include <thread>
#include <vector>

using namespace vj;

static int g_fail = 0, g_checks = 0;
static void failNote(const char* fmt, ...) {
    if (!*fmt) return;
    va_list ap;
    va_start(ap, fmt);
    std::fprintf(stderr, "    ");
    std::vfprintf(stderr, fmt, ap);
    std::fprintf(stderr, "\n");
    va_end(ap);
}
// CHECK(cond) or CHECK(cond, "what: %d", n).
#define CHECK(cond, ...)                                                           \
    do {                                                                           \
        g_checks++;                                                                \
        if (!(cond)) {                                                             \
            g_fail++;                                                              \
            std::fprintf(stderr, "FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond);   \
            failNote("" __VA_ARGS__);                                              \
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

    // Detach clears every breakpoint: the core's table AND the session list (a host that comes back -- the app's silent
    // gdb host -- finds both empty, and the next attach() has nothing stale to replay or skip)...
    s.detach();
    CHECK(!s.attached() && !m.attached() && m.breakpoints.empty());
    CHECK(s.breakpoints().empty() && !s.hasBreakpoint(VJ_DBG_TARGET_M68K, 0x4000));
    CHECK(s.attach() && m.breakpoints.empty());
    // ...and a breakpoint set afterwards reaches the core.
    CHECK(s.addBreakpoint(VJ_DBG_TARGET_M68K, 0x4000) && m.breakpoints.size() == 1);
    CHECK(s.addBreakpoint(VJ_DBG_TARGET_GPU, 0xF03000) && m.breakpoints.size() == 2);

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
    s.core().onHalt.push_back([&](int t, unsigned pc, int r) { halts++; lastTarget = t; lastPc = pc; lastReason = r; });
    s.core().onResume.push_back([&] { resumes++; });

    // Continue after three nested UI frames; halted() and the halt details are visible meanwhile.
    s.core().runUiFrame = [&] {
        s.syncHalt();
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
    s.syncHalt();
    CHECK(!s.halted() && s.haltedTarget() == -1);

    // Step: the verdict is STEP, it is not reported as a resume, and a second halt follows on the next pump.
    uiFrames = 0;
    s.core().runUiFrame = [&] { s.syncHalt(); uiFrames++; s.step(); };
    v = m.simulateHalt(VJ_DBG_TARGET_M68K, 0x4000, VJ_DBG_STOP_BREAKPOINT);
    CHECK(v == VJ_DBG_RESUME_STEP && m.haltedStepTarget == VJ_DBG_TARGET_M68K);
    CHECK(halts == 2 && resumes == 1);   // halted again (onHalt), no onResume for a step
    s.syncHalt();
    CHECK(s.halted());   // the STEP return keeps the serial: the bar stays up until the re-halt (or frameEnded(), see testStepWithoutRehaltClosesAtFrameEnd)

    // The reason comes from the core.
    s.core().runUiFrame = [&] { s.syncHalt(); s.resume(); };
    m.simulateHalt(VJ_DBG_TARGET_GPU, 0xF03004, VJ_DBG_STOP_USER);
    CHECK(lastTarget == VJ_DBG_TARGET_GPU && lastReason == VJ_DBG_STOP_USER);

    // resume()/step() outside a halt do nothing.
    s.syncHalt();
    s.resume();
    s.step();
    CHECK(s.core().pendingVerdict() == VJ_DBG_RESUME_NONE);

    // Quit while halted: abortHalt turns into a CONTINUE and the host STAYS attached (a detach would let the core re-hit a
    // gdb client's armed breakpoint with no host to pump; unloading the content drops the host).
    bool quit = false;
    s.core().abortHalt = [&] { return quit; };
    s.core().runUiFrame = [&] { quit = true; };
    v = m.simulateHalt(VJ_DBG_TARGET_M68K, 0x4000);
    CHECK(v == VJ_DBG_RESUME_CONTINUE);
    CHECK(m.attached() && s.attached());
}

static void testHardcoreWhileHalted() {
    DebugSession s;
    MockDebugIface m;
    s.setIface(m.iface());
    s.setContentLoaded(true);
    CHECK(s.attach());
    std::string why;
    s.onForcedClose.push_back([&](const char* w) { why = w; });
    s.core().runUiFrame = [&] { s.syncHalt(); s.setHardcore(true); };   // the player turned hardcore on from inside the halt
    int v = m.simulateHalt(VJ_DBG_TARGET_M68K, 0x1234);
    CHECK(v == VJ_DBG_RESUME_CONTINUE);   // never DETACH from inside a halt (#1069): the host goes at the frame end
    s.syncHalt();
    CHECK(m.attached() && !s.attached() && !s.halted() && s.core().detachPending());
    s.core().frameEnded();
    CHECK(!m.attached() && !s.attached() && !s.core().detachPending());
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

// Detach from inside a halt (#1069, the rule in docs/app-debug-tools.md): the list goes, its entries are disarmed in the
// core before the CONTINUE, the host stays until frameEnded() drops it, and the mirror reads detached meanwhile.
static void testDetachWhileHaltedClearsList() {
    DebugSession s;
    MockDebugIface m;
    s.setIface(m.iface());
    s.setContentLoaded(true);
    CHECK(s.addBreakpoint(VJ_DBG_TARGET_M68K, 0x4000) && s.breakpoints().size() == 1);
    int resumes = 0;
    s.core().onResume.push_back([&] { resumes++; });
    s.core().runUiFrame = [&] {
        s.syncHalt();
        s.detach();
        CHECK(m.breakpoints.empty() && m.attached());   // (inline: the post ran now) disarmed, host kept
        CHECK(!s.attached() && s.core().detachPending());
    };
    CHECK(m.simulateHalt(VJ_DBG_TARGET_M68K, 0x4000, VJ_DBG_STOP_BREAKPOINT) == VJ_DBG_RESUME_CONTINUE);
    CHECK(resumes == 1 && m.attached() && m.detachCalls == 0 && s.core().detachPending());
    s.syncHalt();
    CHECK(s.breakpoints().empty() && !s.attached() && !s.halted());
    s.resume();
    s.step();
    CHECK(s.core().pendingVerdict() == VJ_DBG_RESUME_NONE);   // no-ops while the Detach is in flight (it answered CONTINUE)
    s.core().frameEnded();        // retro_run() returned: the host goes here, outside the core
    CHECK(!m.attached() && m.detachCalls == 1 && !s.core().detachPending());
    s.syncHalt();
    CHECK(s.attach() && s.addBreakpoint(VJ_DBG_TARGET_M68K, 0x4000) && m.breakpoints.size() == 1);   // and it can be set again
}

// #1069: a new halt before the frame ends (a gdb client's own breakpoint, re-hit at once) needs the host: the deferred drop
// is cancelled, the halt reaches the mirror as gdb's, and frameEnded() later drops nothing.
static void testNewHaltCancelsDeferredDetach() {
    DebugSession s;
    MockDebugIface m;
    s.setIface(m.iface());
    s.setContentLoaded(true);
    CHECK(s.attach());
    int haltUi = 0;
    s.onHaltUi.push_back([&](int, unsigned, int) { haltUi++; });
    s.core().runUiFrame = [&] { s.syncHalt(); s.detach(); };
    CHECK(m.simulateHalt(VJ_DBG_TARGET_M68K, 0x5000, VJ_DBG_STOP_BREAKPOINT) == VJ_DBG_RESUME_CONTINUE);
    CHECK(s.core().detachPending() && m.attached());
    s.core().runUiFrame = [&] { s.syncHalt(); };
    CHECK(m.simulateHalt(VJ_DBG_TARGET_M68K, 0x5000, VJ_DBG_STOP_BREAKPOINT, 1) == VJ_DBG_RESUME_NONE);   // the re-hit
    CHECK(!s.core().detachPending() && m.attached());
    s.syncHalt();
    CHECK(s.halted() && s.haltByGdb() && haltUi == 2 && s.attached());
    s.core().frameEnded();   // (gdb continued) a foreign resume: still no drop
    CHECK(m.attached() && m.detachCalls == 0);
    s.syncHalt();
    CHECK(!s.halted() && s.attached());
}

// #1081 M1: the content commands' release keeps the list and the host.  The enabled entries are disarmed so the frame can
// end, rearmBreakpoints() puts them back (once; a disabled entry stays off), and a core swap in between drops both.
static void testReleaseHaltKeepsListAndRearms() {
    DebugSession s;
    MockDebugIface m;
    s.setIface(m.iface());
    s.setContentLoaded(true);
    CHECK(s.addBreakpoint(VJ_DBG_TARGET_M68K, 0x4000) && s.addBreakpoint(VJ_DBG_TARGET_M68K, 0x4010));
    s.setBreakpointEnabled(VJ_DBG_TARGET_M68K, 0x4010, false);
    CHECK(m.breakpoints.size() == 1);
    const uint64_t s0 = s.core().haltSerial();
    s.core().runUiFrame = [&] {
        s.syncHalt();
        s.releaseHalt();
        CHECK(m.breakpoints.empty() && m.attached());   // disarmed, host kept
    };
    CHECK(m.simulateHalt(VJ_DBG_TARGET_M68K, 0x4000, VJ_DBG_STOP_BREAKPOINT) == VJ_DBG_RESUME_CONTINUE);
    CHECK(s.core().releaseSerial() == s0 + 1 && !s.core().detachPending());
    s.syncHalt();
    CHECK(!s.halted() && s.attached() && s.breakpoints().size() == 2);
    s.setBreakpointEnabled(VJ_DBG_TARGET_M68K, 0x4010, true);   // toggled while disarmed: no early set
    CHECK(m.breakpoints.empty());
    s.rearmBreakpoints();
    CHECK(m.breakpoints.size() == 2 && m.attachCalls == 1);   // the enabled entries, no re-attach
    s.rearmBreakpoints();
    CHECK(m.breakpoints.size() == 2);                        // idempotent
    // A release with the core running (nothing live): records the current serial (the CONTINUE bumped it), keeps the host;
    // rearm is a no-op.
    s.releaseHalt();
    CHECK(s.core().releaseSerial() == s0 + 2 && m.attached() && m.breakpoints.size() == 2);
    s.rearmBreakpoints();
    CHECK(m.breakpoints.size() == 2);
    // The command replaced the core: the old list must not be armed in the new one.
    s.core().runUiFrame = [&] { s.syncHalt(); s.releaseHalt(); };
    CHECK(m.simulateHalt(VJ_DBG_TARGET_M68K, 0x4000, VJ_DBG_STOP_BREAKPOINT) == VJ_DBG_RESUME_CONTINUE);
    MockDebugIface m2;
    s.core().setIface(m2.iface());   // the emulation-thread half of the swap (the call the UI waits in)
    s.rearmBreakpoints();            // what App does right after the call, before syncCore()
    CHECK(m2.breakpoints.empty() && !m2.attached());
    s.syncCore();
    CHECK(s.breakpoints().empty());
    s.setContentLoaded(false);
}

// #1081 M5: the give-up predicate of the call that follows a release (App::releaseForCall).  A release made against the
// mirror while a STEP is in flight runs at the step's re-halt and releases THAT halt; the predicate waits for the release
// (releaseSerial() < s0) instead of giving up on the re-halt, and fires only on a halt newer than the released one.
static void testReleaseSerialAndStepInFlight() {
    DebugSession s;
    MockDebugIface m;
    s.setIface(m.iface());
    s.setContentLoaded(true);
    CHECK(s.attach());
    std::vector<std::function<void()>> lane;
    s.corePost = [&](std::function<void()> fn) { lane.push_back(std::move(fn)); };
    const auto drain = [&] {
        for (auto& f : lane) f();
        lane.clear();
    };
    s.core().waitVerdict = [&] { s.syncHalt(); s.step(); };
    CHECK(m.simulateHalt(VJ_DBG_TARGET_M68K, 0x4000, VJ_DBG_STOP_USER) == VJ_DBG_RESUME_STEP);
    s.syncHalt();
    const uint64_t s0 = s.haltSerial();
    CHECK(s.halted() && !s.core().halt().halted);   // STEP in flight
    s.releaseHalt();                                // the user pressed Reset against the mirrored halt
    const auto giveUp = [&] {
        const uint64_t rel = s.core().releaseSerial();
        const HaltState h = s.core().halt();
        return rel >= s0 && h.halted && h.serial > rel;
    };
    CHECK(!giveUp());                               // the release is still queued: wait
    s.core().waitVerdict = [&] { drain(); };        // the step re-halts; the pump serves the lane: the post releases it
    CHECK(m.simulateHalt(VJ_DBG_TARGET_M68K, 0x4002, VJ_DBG_STOP_STEP) == VJ_DBG_RESUME_CONTINUE);
    CHECK(s.core().releaseSerial() == s0 + 1 && !giveUp());   // the re-halt was the released one, not a give-up
    s.core().waitVerdict = [] {};
    CHECK(m.simulateHalt(VJ_DBG_TARGET_M68K, 0x4004, VJ_DBG_STOP_BREAKPOINT, 1) == VJ_DBG_RESUME_NONE);   // gdb's
    CHECK(giveUp());                                // a halt newer than the released one
    s.core().frameEnded();
    // A step that never re-halts: frameEnded() closes it before the boundary drains the post; the post finds the core
    // running, records the serial, and the predicate lets the call through.
    s.core().waitVerdict = [&] { s.syncHalt(); s.step(); };
    CHECK(m.simulateHalt(VJ_DBG_TARGET_M68K, 0x4006, VJ_DBG_STOP_USER) == VJ_DBG_RESUME_STEP);
    s.syncHalt();
    const uint64_t s1 = s.haltSerial();
    s.releaseHalt();
    s.core().frameEnded();
    drain();
    CHECK(s.core().releaseSerial() == s1 + 1 && m.attached());
    const uint64_t rel = s.core().releaseSerial();
    const HaltState h = s.core().halt();
    CHECK(rel >= s1 && !h.halted);
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
    CHECK(s.tools().size() == 8);
    CHECK(s.findTool("memory") && !s.findTool("memory")->toggle);   // greyed until a window registers
    bool opened = false;
    DebugTool t;
    t.id = "memory";
    t.label = "Memory viewer...";
    t.shortcut = "Ctrl+M";
    t.toggle = [&] { opened = !opened; };
    t.isOpen = [&] { return opened; };
    s.registerTool(t);
    CHECK(s.tools().size() == 8);                                   // replaced, not appended
    s.findTool("memory")->toggle();
    CHECK(s.findTool("memory")->isOpen());
    s.registerPlaceholderTools();                                   // never clobbers a real one
    CHECK(s.findTool("memory")->toggle);
    CHECK(s.findTool("nope") == nullptr);

    // The availability rule the in-window Tools menu and the native macOS menu share.
    DebugTool step;
    step.id = "cpu";
    step.label = "CPU debugger...";
    step.toggle = [] {};
    step.needsHalt = true;
    DebugTool* placeholder = s.findTool("scanner");   // still a greyed placeholder (no toggle)
    CHECK(!s.toolAvailable(*s.findTool("memory")));   // no core / no game
    MockDebugIface m;
    s.setIface(m.iface());
    s.setContentLoaded(true);
    CHECK(s.toolAvailable(*s.findTool("memory")));
    CHECK(!s.toolAvailable(*placeholder));            // enabled, but the window does not exist yet
    CHECK(!s.toolAvailable(step));                    // needs a halt
    s.setHardcore(true);
    CHECK(!s.toolAvailable(*s.findTool("memory")));   // hardcore switches them all off
    s.setHardcore(false);
    CHECK(s.toolAvailable(*s.findTool("memory")));
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

static void testCoreWrappers() {
    MockDebugIface m;
    DebugCore c;
    uint8_t buf[4] = {};
    vj_debug_regs none;
    CHECK(c.peek(0, buf, 4) == VJ_DBG_ERR_STATE && !c.loaded() && !c.attach() && !c.getRegs(VJ_DBG_TARGET_M68K, none),
          "a core with no interface: every wrapper returns its failure value");
    c.setIface(m.iface());
    CHECK(c.loaded() && c.iface() == m.iface() && c.ifaceGen() == 1, "setIface() stores the pointer and bumps the generation");
    c.setIface(m.iface());
    CHECK(c.ifaceGen() == 1, "the same pointer again is not a change");
    CHECK(c.peek(0x10, buf, 4) == 4 && buf[0] == 0x10 && buf[3] == 0x13, "peek: the bus bytes");
    const uint8_t w[2] = {0xAA, 0xBB};
    CHECK(c.poke(0x7000, w, 2, 0) == 2 && c.poke(0x800000, w, 2, 0) == VJ_DBG_ERR_READONLY, "poke: the iface's own policy and result codes");
    vj_debug_regs r;
    CHECK(c.getRegs(VJ_DBG_TARGET_M68K, r) && r.d[3] == 0x1003 && c.setReg(VJ_DBG_TARGET_M68K, VJ_DBG_REG_68K_D0 + 3, 7) &&
              c.getRegs(VJ_DBG_TARGET_M68K, r) && r.d[3] == 7, "registers read and write back");
    std::string text;
    unsigned len = 0;
    CHECK(c.disasmRisc(VJ_DBG_TARGET_GPU, 0xF03006, text, len) && len == 6 && !text.empty() && !c.disasmRisc(VJ_DBG_TARGET_M68K, 0, text, len),
          "disasmRisc: RISC only");
    CHECK(c.attach() && c.attached() && m.attached() && c.attach(), "attach is idempotent");
    CHECK(c.breakSet(VJ_DBG_TARGET_M68K, 0x4000) && m.breakpoints.size() == 1 && c.breakClear(VJ_DBG_TARGET_M68K, 0x4000), "break_set / break_clear");
    c.detach();
    CHECK(!c.attached() && !m.attached(), "detach() clears the core's side and ours");
    std::vector<vj_debug_op_obj> objs;
    unsigned hl = 0;
    m.opReady = false;
    CHECK(c.opSnapshotGet(objs, hl) == -1, "op snapshot: -1 while armed and not taken");
    m.opReady = true;
    c.opSnapshotArm();
    CHECK(c.opSnapshotGet(objs, hl) == 4 && objs.size() == 4 && hl == 41, "op snapshot: four objects");
    std::vector<uint32_t> cry;
    CHECK(c.buildCryTable(cry) && cry.size() == 65536 && cry[0xFFFF] == 0xFFFFFFu && c.cryToRgb(0xFFFF) == 0xFFFFFFu, "the CRY table");
    c.blitLogEnable(true);
    vj_debug_blit b;
    CHECK(m.blitLogOn && c.blitLogHead() == 5 && c.blitLogGet(4, b) && b.seq == 4 && !c.blitLogGet(5, b), "blit log");
    CHECK(c.texReplaceReload() == 1 && m.texReloads == 1 && c.frameNumber() == 1234, "reload and frame number");
    vj_debug_iface old = *m.iface();
    old.texreplace_reload = nullptr;
    DebugCore d;
    d.setIface(&old);
    CHECK(d.texReplaceReload() == -1, "texReplaceReload: -1 when the core does not offer it");
    vj_debug_region reg;
    CHECK(c.region(0, reg) && reg.name, "region");
    c.setIface(nullptr);
    CHECK(!c.loaded() && c.ifaceGen() == 2, "setIface(nullptr) drops the interface");
}

static void testMarshalling() {
    DebugSession s;
    MockDebugIface m;
    int calls = 0, posts = 0;
    s.coreCall = [&](std::function<void()> f) { calls++; f(); };
    s.corePost = [&](std::function<void()> f) { posts++; f(); };
    s.setIface(m.iface());
    s.setContentLoaded(true);
    s.registerPlaceholderTools();
    auto delta = [&](int dc, int dp, const char* what) {
        CHECK(calls == dc && posts == dp, "%s: %d coreCall / %d corePost (wanted %d / %d)", what, calls, posts, dc, dp);
        calls = posts = 0;
    };
    std::vector<uint8_t> out;
    unsigned u = 0;
    vj_debug_regs r;
    std::vector<uint32_t> cry;
    CHECK(s.read(0x10, out, 4) && out[0] == 0x10);       delta(1, 0, "read");
    CHECK(s.readU(0x10, 2, u) && u == 0x1011);           delta(1, 0, "readU");
    CHECK(s.pokeU(0x7000, 4, 0xCAFEF00D) == 4);          delta(1, 0, "poke");
    CHECK(s.getRegsNow(VJ_DBG_TARGET_M68K, r));          delta(1, 0, "getRegsNow");
    CHECK(s.setReg(VJ_DBG_TARGET_M68K, VJ_DBG_REG_68K_D0, 5)); delta(1, 0, "setReg");
    CHECK(s.buildCryTable(cry));                         delta(1, 0, "buildCryTable");
    s.opSnapshotArm();                                   delta(1, 0, "opSnapshotArm");
    s.blitLogEnable(true);                               delta(1, 0, "blitLogEnable");
    CHECK(s.texReplaceReload() == 1);                    delta(1, 0, "texReplaceReload");
    CHECK(!s.regions().empty());                         delta(1, 0, "regions (once per core)");
    s.regions();                                         delta(0, 0, "regions again: cached");
    CHECK(s.attach());                                   delta(1, 0, "attach");
    s.breakNow();                                        delta(0, 1, "breakNow when attached: only the halt_next post");
    CHECK(s.addBreakpoint(VJ_DBG_TARGET_M68K, 0x4000));  delta(1, 0, "addBreakpoint");
    s.removeBreakpoint(VJ_DBG_TARGET_M68K, 0x4000);      delta(0, 1, "removeBreakpoint");
    s.detach();                                          delta(0, 1, "detach");
    // What the windows use every frame costs the core nothing.
    s.tick(1.0, true);
    s.views().set(DebugViews::kNoId, ViewRequest());
    (void)s.refreshSerial(); (void)s.breakpoints(); (void)s.toolAvailable(*s.findTool("memory")); (void)s.enabled(); (void)s.attached();
    delta(0, 0, "tick / views / serial / enabled / attached: no core call");
}

static void testCoreSyncSplit() {
    // What the emulation thread does (onCoreIface) must not touch UI state; the UI adopts it in syncCore().
    DebugSession s;
    MockDebugIface m;
    s.setIface(m.iface());
    s.setContentLoaded(true);
    CHECK(s.addBreakpoint(VJ_DBG_TARGET_M68K, 0x4000));
    int closed = 0;
    s.onForcedClose.push_back([&](const char*) { closed++; });
    const uint64_t epoch = s.views().epoch();
    s.onCoreIface(nullptr);
    CHECK(!s.hasCore() && !s.enabled() && !s.attached(), "the atomics already say 'no core'");
    CHECK(s.breakpoints().size() == 1 && closed == 0, "...but the UI-side list and callbacks are untouched until syncCore()");
    CHECK(s.views().epoch() == epoch + 1, "the views' epoch moved on the emulation side");
    s.syncCore();
    CHECK(s.breakpoints().empty() && closed == 1, "syncCore() forgets the breakpoints and closes the tools once");
    s.syncCore();
    CHECK(closed == 1, "and is idempotent");
    m.iface()->detach();     // a torn-down core forgets its host (the mock is the same object)
    s.setIface(m.iface());   // the same static interface again, as a reload hands over
    CHECK(s.enabled() && s.attach() && m.attached(), "a new core attaches");
}

static void testReplacedCoreClosesTools() {
    // Threaded openContent() on a running game: the emulation thread publishes null and the new interface inside one
    // blocking call, so the UI's syncCore() sees only the final non-null interface.  That must still end where the
    // inline path does (null seen, then the new interface): the tools force-closed, the breakpoints and halt gone.
    DebugSession s;
    MockDebugIface m1, m2;
    s.setIface(m1.iface());
    s.setContentLoaded(true);
    CHECK(s.addBreakpoint(VJ_DBG_TARGET_M68K, 0x4000));
    std::string why;
    int closed = 0;
    s.onForcedClose.push_back([&](const char* w) { why = w; closed++; });
    s.onCoreIface(nullptr);              // both halves of the swap before the UI looks
    s.onCoreIface(m2.iface());
    CHECK(s.hasCore() && closed == 0, "the UI has not looked yet");
    s.syncCore();
    CHECK(closed == 1 && why == "no core", "a replaced core closes the tools like an unload");
    CHECK(s.breakpoints().empty() && s.enabled() && !s.attached(), "and starts the new game clean");
    s.syncCore();
    CHECK(closed == 1, "once");
    // The same interface pointer again (the core's struct is static): still a replacement.
    s.onCoreIface(nullptr);
    s.onCoreIface(m2.iface());
    s.syncCore();
    CHECK(closed == 2, "a reload of the same static struct too");
    // A pointer swapped directly (no null in between) is a replacement as well.
    s.onCoreIface(m1.iface());
    s.syncCore();
    CHECK(closed == 3, "so is a direct handover");
    // Inline: the two halves run apart, and the count is the same.
    DebugSession t;
    t.setIface(m1.iface());
    t.setContentLoaded(true);
    int tclosed = 0;
    t.onForcedClose.push_back([&](const char*) { tclosed++; });
    t.setIface(nullptr);
    t.setIface(m2.iface());
    CHECK(tclosed == 1, "inline: one close for the same content switch");
    // A first load (no previous core) closes nothing.
    DebugSession u;
    int uclosed = 0;
    u.onForcedClose.push_back([&](const char*) { uclosed++; });
    u.onCoreIface(m1.iface());
    u.syncCore();
    CHECK(uclosed == 0 && u.hasCore(), "the first core is not a replacement");
}

static void testViewsFill() {
    DebugSession s;
    MockDebugIface m;
    std::vector<std::function<void()>> posted;
    s.corePost = [&](std::function<void()> f) { posted.push_back(std::move(f)); };
    s.setIface(m.iface());
    s.setContentLoaded(true);
    ViewRequest r;
    r.kind = ViewRequest::Kind::Memory;
    r.addr = 0x1000;
    r.len = 8;
    const DebugViews::Id id = s.views().set(DebugViews::kNoId, r);
    s.wakeViews();
    s.wakeViews();
    CHECK(posted.size() == 1, "wakeViews() posts one fill at a time (%zu)", posted.size());
    posted[0]();
    ViewBuffer b;
    CHECK(s.views().read(id, b) && b.bytes[0] == 0x00 && b.serial == s.refreshSerial(), "the posted fill ran");
    s.wakeViews();
    CHECK(posted.size() == 1, "nothing pending: no post");
    s.requestRefresh();
    s.wakeViews();
    CHECK(posted.size() == 2, "a new serial posts again");
    posted[1]();
    DebugSession t;   // inline (no corePost): wakeViews() fills right away
    t.setIface(m.iface());
    t.setContentLoaded(true);
    const DebugViews::Id id2 = t.views().set(DebugViews::kNoId, r);
    t.wakeViews();
    CHECK(t.views().read(id2, b), "inline: filled on the spot");
}

// B1: the pump is bounded.  With nothing to say it calls waitVerdict once and returns NONE; the halt stays.
static void testBoundedPumpReturnsNone() {
    MockDebugIface m; DebugCore c; c.setIface(m.iface()); CHECK(c.attach());
    int waits = 0, halts = 0, resumes = 0;
    c.waitVerdict = [&] { waits++; };
    c.onHalt.push_back([&](int, unsigned, int) { halts++; });
    c.onResume.push_back([&] { resumes++; });
    const uint64_t s0 = c.haltSerial();
    CHECK(c.pump(VJ_DBG_TARGET_M68K, 0x4000) == VJ_DBG_RESUME_NONE);
    CHECK(waits == 1 && halts == 1 && resumes == 0);
    CHECK(c.halt().halted && c.halt().target == VJ_DBG_TARGET_M68K && c.halt().pc == 0x4000);
    CHECK(c.haltSerial() == s0 + 1);
    CHECK(c.pump(VJ_DBG_TARGET_M68K, 0x4000) == VJ_DBG_RESUME_NONE);   // the same halt: no second onHalt
    CHECK(waits == 2 && halts == 1 && c.haltSerial() == s0 + 1);
    c.offerVerdict(VJ_DBG_RESUME_CONTINUE, c.haltSerial());
    CHECK(c.pump(VJ_DBG_TARGET_M68K, 0x4000) == VJ_DBG_RESUME_CONTINUE);
    CHECK(waits == 2);                      // a pending verdict is taken before any wait
    CHECK(!c.halt().halted && resumes == 1 && c.haltSerial() == s0 + 2);
    CHECK(c.pendingVerdict() == VJ_DBG_RESUME_NONE);
}

// Review Focus 6: a detach verdict ends a bounded pump within one wait, through the mock's GDBHalt loop.
static void testDetachEndsPumpWithinOneWait() {
    MockDebugIface m; DebugCore c; c.setIface(m.iface()); CHECK(c.attach());
    int waits = 0;
    c.waitVerdict = [&] { if (++waits == 2) c.offerVerdict(VJ_DBG_RESUME_DETACH, c.haltSerial()); };
    int pumps = 0;
    CHECK(m.simulateHalt(VJ_DBG_TARGET_M68K, 0x4000, VJ_DBG_STOP_USER, 100, &pumps) == VJ_DBG_RESUME_DETACH);
    CHECK(pumps == 2 && waits == 2);         // pump 1: wait, NONE; pump 2: the verdict written inside its wait is taken
    CHECK(!c.attached() && !m.attached() && !c.halt().halted);
}

// The serial moves on every entry (a step's re-halt too) and on CONTINUE / DETACH, not on the STEP return.
static void testStepSerialAndReason() {
    MockDebugIface m; DebugCore c; c.setIface(m.iface()); CHECK(c.attach());
    int halts = 0, resumes = 0, lastReason = -1;
    c.onHalt.push_back([&](int, unsigned, int r) { halts++; lastReason = r; });
    c.onResume.push_back([&] { resumes++; });
    c.waitVerdict = [&] { c.offerVerdict(VJ_DBG_RESUME_STEP, c.haltSerial()); };
    const uint64_t s0 = c.haltSerial();
    CHECK(m.simulateHalt(VJ_DBG_TARGET_M68K, 0x4000, VJ_DBG_STOP_BREAKPOINT) == VJ_DBG_RESUME_STEP);
    CHECK(halts == 1 && resumes == 0 && c.haltSerial() == s0 + 1 && !c.halt().halted);
    c.waitVerdict = [&] { c.offerVerdict(VJ_DBG_RESUME_CONTINUE, c.haltSerial()); };
    CHECK(m.simulateHalt(VJ_DBG_TARGET_M68K, 0x4002, VJ_DBG_STOP_STEP) == VJ_DBG_RESUME_CONTINUE);
    CHECK(halts == 2 && lastReason == VJ_DBG_STOP_STEP && resumes == 1 && c.haltSerial() == s0 + 3);
}

// gdb continued (or the timeout fired): GDBHalt breaks without a verdict of ours.  frameEnded() ends the halt on our
// side and drops a verdict written for it meanwhile.
static void testFrameEndedClosesForeignResume() {
    MockDebugIface m; DebugCore c; c.setIface(m.iface()); CHECK(c.attach());
    int resumes = 0;
    c.onResume.push_back([&] { resumes++; });
    c.waitVerdict = [] {};
    int pumps = 0;
    CHECK(m.simulateHalt(VJ_DBG_TARGET_M68K, 0x4000, VJ_DBG_STOP_BREAKPOINT, 1, &pumps) == VJ_DBG_RESUME_NONE);
    CHECK(pumps == 1 && c.halt().halted);
    const uint64_t s1 = c.haltSerial();
    c.offerVerdict(VJ_DBG_RESUME_CONTINUE, s1);   // the UI pressed F9 against a halt gdb already ended
    c.frameEnded();
    CHECK(!c.halt().halted && resumes == 1 && c.haltSerial() == s1 + 1);
    CHECK(c.pendingVerdict() == VJ_DBG_RESUME_NONE);
    c.frameEnded();                            // not halted: a no-op
    CHECK(resumes == 1 && c.haltSerial() == s1 + 1);
    CHECK(c.attached());                       // a foreign continue is not a detach
}

// A gdb step re-halts at a new pc inside the same frame: a new halt (pc, onHalt, serial).
static void testForeignStepIsNewHalt() {
    MockDebugIface m; DebugCore c; c.setIface(m.iface()); CHECK(c.attach());
    int halts = 0;
    c.onHalt.push_back([&](int, unsigned, int) { halts++; });
    c.waitVerdict = [] {};
    CHECK(m.simulateHalt(VJ_DBG_TARGET_M68K, 0x4000, VJ_DBG_STOP_BREAKPOINT, 1) == VJ_DBG_RESUME_NONE);
    const uint64_t s1 = c.haltSerial();
    CHECK(m.simulateHalt(VJ_DBG_TARGET_M68K, 0x4002, VJ_DBG_STOP_STEP, 1) == VJ_DBG_RESUME_NONE);
    CHECK(halts == 2 && c.halt().pc == 0x4002 && c.halt().reason == VJ_DBG_STOP_STEP && c.haltSerial() == s1 + 1);
}

// Quit mid-halt: abortHalt wins over a pending verdict (a stale STEP or DETACH included) and answers CONTINUE with the
// host still attached; it re-answers every re-halt the same way (a gdb breakpoint that is still armed fires again).
static void testAbortHaltContinues() {
    MockDebugIface m; DebugCore c; c.setIface(m.iface()); CHECK(c.attach());
    bool quitting = false;
    c.abortHalt = [&] { return quitting; };
    c.waitVerdict = [&] { quitting = true; c.offerVerdict(VJ_DBG_RESUME_CONTINUE, c.haltSerial()); };
    CHECK(m.simulateHalt(VJ_DBG_TARGET_M68K, 0x4000) == VJ_DBG_RESUME_CONTINUE);
    CHECK(c.attached() && m.attached() && !c.halt().halted);
    c.waitVerdict = nullptr;
    c.offerVerdict(VJ_DBG_RESUME_DETACH, c.haltSerial() + 1);   // stale verdict for the next halt's serial: quitting still wins
    CHECK(m.simulateHalt(VJ_DBG_TARGET_M68K, 0x4000) == VJ_DBG_RESUME_CONTINUE);   // the same breakpoint again
    CHECK(c.attached() && m.attached() && !c.halt().halted);
}

// Inline: no waitVerdict; the pump runs runUiFrame once per call and takes the verdict that frame set (today's loop).
static void testInlinePumpRunsUiFrame() {
    MockDebugIface m; DebugCore c; c.setIface(m.iface()); CHECK(c.attach());
    int ui = 0;
    c.runUiFrame = [&] { if (++ui == 3) c.offerVerdict(VJ_DBG_RESUME_CONTINUE, c.haltSerial()); };
    int pumps = 0;
    CHECK(m.simulateHalt(VJ_DBG_TARGET_M68K, 0x4000, VJ_DBG_STOP_BREAKPOINT, 100, &pumps) == VJ_DBG_RESUME_CONTINUE);
    CHECK(ui == 3 && pumps == 3);
}

// The pump calls its wait hook exactly once per NONE iteration and never loops on its own (a pump that waited for a
// verdict itself would hang here or over-count).
static void testPumpCallsHookOncePerIteration() {
    MockDebugIface m; DebugCore c; c.setIface(m.iface()); CHECK(c.attach());
    int waits = 0;
    c.waitVerdict = [&] { waits++; std::this_thread::sleep_for(DebugCore::kPumpWait); };
    const auto t0 = std::chrono::steady_clock::now();
    for (int i = 0; i < 10; i++) CHECK(c.pump(VJ_DBG_TARGET_M68K, 0x4000) == VJ_DBG_RESUME_NONE);
    CHECK(waits == 10);
    CHECK(std::chrono::steady_clock::now() - t0 < std::chrono::seconds(2));
    c.waitVerdict = nullptr;                   // no hook at all: NONE at once, nothing called
    CHECK(c.pump(VJ_DBG_TARGET_M68K, 0x4000) == VJ_DBG_RESUME_NONE && waits == 10);
}

// A verdict tagged with another halt's serial is dropped; one for the current serial is taken.
static void testVerdictForAnotherSerialDropped() {
    MockDebugIface m; DebugCore c; c.setIface(m.iface()); CHECK(c.attach());
    c.waitVerdict = [] {};
    const uint64_t s0 = c.haltSerial();
    c.offerVerdict(VJ_DBG_RESUME_CONTINUE, s0);                 // written before the halt (its serial is s0)
    CHECK(m.simulateHalt(VJ_DBG_TARGET_M68K, 0x4000, VJ_DBG_STOP_USER, 1) == VJ_DBG_RESUME_NONE);   // dropped
    CHECK(c.halt().halted && c.pendingVerdict() == VJ_DBG_RESUME_NONE);
    c.offerVerdict(VJ_DBG_RESUME_CONTINUE, c.haltSerial());
    CHECK(m.simulateHalt(VJ_DBG_TARGET_M68K, 0x4000, VJ_DBG_STOP_USER) == VJ_DBG_RESUME_CONTINUE);
    CHECK(!c.halt().halted);
    c.offerVerdict(VJ_DBG_RESUME_STEP, s0);                     // a stale STEP (double F10) against the next halt
    CHECK(m.simulateHalt(VJ_DBG_TARGET_M68K, 0x4002, VJ_DBG_STOP_USER, 1) == VJ_DBG_RESUME_NONE);
    CHECK(c.pendingVerdict() == VJ_DBG_RESUME_NONE);
}

// A core swap forgets a live halt and a pending verdict; the serial moves so a mirror never confuses the two cores.
static void testSetIfaceDropsHaltAndVerdict() {
    MockDebugIface m; DebugCore c; c.setIface(m.iface()); CHECK(c.attach());
    int resumes = 0;
    c.onResume.push_back([&] { resumes++; });
    c.waitVerdict = [] {};
    CHECK(m.simulateHalt(VJ_DBG_TARGET_M68K, 0x4000, VJ_DBG_STOP_USER, 1) == VJ_DBG_RESUME_NONE);
    const uint64_t s1 = c.haltSerial();
    c.offerVerdict(VJ_DBG_RESUME_CONTINUE, s1);
    MockDebugIface m2;
    c.setIface(m2.iface());
    CHECK(!c.halt().halted && !c.attached() && c.pendingVerdict() == VJ_DBG_RESUME_NONE);
    CHECK(c.haltSerial() == s1 + 1 && resumes == 0);   // no callback: the old core is gone, not resumed
}

// The session's verdict methods tag core().offerVerdict with the mirrored serial and notify; outside a halt they do nothing.
static void testSessionVerdictsReachCore() {
    DebugSession s; MockDebugIface m; s.setIface(m.iface()); s.setContentLoaded(true); CHECK(s.attach());
    int notifies = 0;
    s.core().notifyVerdict = [&] { notifies++; };
    s.resume();
    CHECK(s.core().pendingVerdict() == VJ_DBG_RESUME_NONE && notifies == 0);
    s.core().waitVerdict = [&] { s.syncHalt(); s.step(); };   // the UI thread, one frame later
    CHECK(m.simulateHalt(VJ_DBG_TARGET_M68K, 0x4000) == VJ_DBG_RESUME_STEP);
    CHECK(notifies == 1);
    s.core().waitVerdict = [&] { s.syncHalt(); s.detach(); };   // Detach while halted = CONTINUE, the host dropped at the frame end
    CHECK(m.simulateHalt(VJ_DBG_TARGET_M68K, 0x4002, VJ_DBG_STOP_STEP) == VJ_DBG_RESUME_CONTINUE);
    CHECK(notifies == 2 && m.attached() && s.core().detachPending());
    s.core().frameEnded();
    CHECK(!m.attached());
    s.syncHalt();
    CHECK(!s.attached() && !s.halted());
}

// The UI mirror.  syncHalt() fires onHaltUi once per halt serial and onResumeUi once per resume; a step's re-halt
// fires onHaltUi again with reason STEP; a halt that came and went between two UI frames fires nothing.
static void testSyncHaltEdges() {
    DebugSession s; MockDebugIface m; s.setIface(m.iface()); s.setContentLoaded(true); CHECK(s.attach());
    int haltUi = 0, resumeUi = 0, lastReason = -1;
    s.onHaltUi.push_back([&](int, unsigned, int r) { haltUi++; lastReason = r; });
    s.onResumeUi.push_back([&] { resumeUi++; });
    s.core().waitVerdict = [] {};
    CHECK(m.simulateHalt(VJ_DBG_TARGET_M68K, 0x4000, VJ_DBG_STOP_USER, 1) == VJ_DBG_RESUME_NONE);
    CHECK(!s.halted());                        // not synced yet
    s.syncHalt();
    CHECK(s.halted() && s.haltedPc() == 0x4000 && haltUi == 1 && lastReason == VJ_DBG_STOP_USER);
    const uint64_t rs = s.refreshSerial();
    s.syncHalt();
    CHECK(haltUi == 1 && s.refreshSerial() == rs);   // the same serial: no second edge, no refill
    s.core().offerVerdict(VJ_DBG_RESUME_STEP, s.haltSerial());
    CHECK(m.simulateHalt(VJ_DBG_TARGET_M68K, 0x4000, VJ_DBG_STOP_USER) == VJ_DBG_RESUME_STEP);
    s.syncHalt();
    CHECK(s.halted());                         // the STEP return keeps the serial: the bar stays up until the re-halt
    CHECK(m.simulateHalt(VJ_DBG_TARGET_M68K, 0x4002, VJ_DBG_STOP_STEP, 1) == VJ_DBG_RESUME_NONE);
    s.syncHalt();
    CHECK(s.halted() && s.haltedPc() == 0x4002 && haltUi == 2 && lastReason == VJ_DBG_STOP_STEP && resumeUi == 0);
    s.core().offerVerdict(VJ_DBG_RESUME_CONTINUE, s.haltSerial());
    CHECK(m.simulateHalt(VJ_DBG_TARGET_M68K, 0x4002, VJ_DBG_STOP_STEP) == VJ_DBG_RESUME_CONTINUE);
    s.syncHalt();
    CHECK(!s.halted() && resumeUi == 1 && haltUi == 2);
    // Halt and continue inside one UI frame: the serial moved by two, the state is "not halted": no edge.
    s.core().waitVerdict = [&] { s.core().offerVerdict(VJ_DBG_RESUME_CONTINUE, s.core().haltSerial()); };
    CHECK(m.simulateHalt(VJ_DBG_TARGET_M68K, 0x4000, VJ_DBG_STOP_BREAKPOINT) == VJ_DBG_RESUME_CONTINUE);
    s.syncHalt();
    CHECK(haltUi == 2 && resumeUi == 1 && !s.halted());
}

// "by gdb" = a breakpoint halt at an address not in our list, or a USER / STEP halt we did not ask for.
static void testHaltByGdb() {
    DebugSession s; MockDebugIface m; s.setIface(m.iface()); s.setContentLoaded(true); CHECK(s.attach());
    CHECK(s.addBreakpoint(VJ_DBG_TARGET_M68K, 0x4000));
    s.core().waitVerdict = [] {};
    CHECK(m.simulateHalt(VJ_DBG_TARGET_M68K, 0x4000, VJ_DBG_STOP_BREAKPOINT, 1) == VJ_DBG_RESUME_NONE);
    s.syncHalt();
    CHECK(s.halted() && !s.haltByGdb());
    s.core().frameEnded();
    CHECK(m.simulateHalt(VJ_DBG_TARGET_M68K, 0x5000, VJ_DBG_STOP_BREAKPOINT, 1) == VJ_DBG_RESUME_NONE);
    s.syncHalt();
    CHECK(s.haltByGdb());                      // not our breakpoint
    s.core().frameEnded();
    CHECK(m.simulateHalt(VJ_DBG_TARGET_M68K, 0x5002, VJ_DBG_STOP_USER, 1) == VJ_DBG_RESUME_NONE);
    s.syncHalt();
    CHECK(s.haltByGdb());                      // a halt_next we did not issue (gdb_wait, or gdb's own interrupt)
    s.core().frameEnded();
    s.syncHalt();                              // the mirror must see the resume before breakNow() is allowed
    s.breakNow();                              // ours: userHaltPending_
    CHECK(m.haltNexts.size() == 1);
    CHECK(m.simulateHalt(VJ_DBG_TARGET_M68K, 0x5004, VJ_DBG_STOP_USER, 1) == VJ_DBG_RESUME_NONE);
    s.syncHalt();
    CHECK(!s.haltByGdb());
    // A pending "ours" that never halted is forgotten when the content closes.
    s.core().frameEnded();
    s.syncHalt();
    s.breakNow();
    s.setContentLoaded(false);
    m.iface()->detach();                       // the core drops the host on unload (the mock does not know about content)
    s.setContentLoaded(true);
    CHECK(s.attach());
    CHECK(m.simulateHalt(VJ_DBG_TARGET_M68K, 0x5006, VJ_DBG_STOP_USER, 1) == VJ_DBG_RESUME_NONE);
    s.syncHalt();
    CHECK(s.haltByGdb());
}

// A verdict for a halt that already ended is dropped (the mirror lags the core by one UI frame).
static void testStaleVerdictDropped() {
    DebugSession s; MockDebugIface m; s.setIface(m.iface()); s.setContentLoaded(true); CHECK(s.attach());
    s.core().waitVerdict = [] {};
    CHECK(m.simulateHalt(VJ_DBG_TARGET_M68K, 0x4000, VJ_DBG_STOP_USER, 1) == VJ_DBG_RESUME_NONE);
    s.syncHalt();
    CHECK(s.halted());
    s.core().frameEnded();                     // gdb continued; the UI has not synced yet
    s.resume();                                // F9 against the stale mirror
    CHECK(s.core().pendingVerdict() == VJ_DBG_RESUME_NONE);
    s.syncHalt();
    CHECK(!s.halted());
}

// Hardcore from the UI while halted: a lane post the pump serves (never iface->detach on the UI thread, never a DETACH
// verdict); while running: the same post, run at the boundary.  Inline (corePost empty) the post runs now.
static void testHardcoreGoesThroughTheLane() {
    DebugSession s; MockDebugIface m; s.setIface(m.iface()); s.setContentLoaded(true); CHECK(s.attach());
    int posts = 0;
    s.corePost = [&](std::function<void()> fn) { posts++; fn(); };
    s.setHardcore(true);
    CHECK(posts == 1 && !m.attached() && !s.attached() && !s.enabled());
    s.setHardcore(false);
    CHECK(s.attach() && s.attached());
    s.core().waitVerdict = [&] { s.syncHalt(); s.setHardcore(true); };
    CHECK(m.simulateHalt(VJ_DBG_TARGET_M68K, 0x4000) == VJ_DBG_RESUME_CONTINUE);
    CHECK(posts == 2 && m.attached() && !s.attached() && s.core().detachPending());   // the post; the host goes at the frame end
    s.core().frameEnded();
    CHECK(!m.attached());
}

// #1081 M6: a Detach whose frame-end host drop is still owed is superseded by the next attach() (Detach, then Break):
// the host stays, nothing is refused, and frameEnded() then drops nothing.  Inside the halt itself breakNow() is a no-op
// (the mirror says halted) until the UI has seen the resume.  A detach posted while running is ordered before a later
// attach by the lane.
static void testAttachWhileDetachPending() {
    DebugSession s; MockDebugIface m; s.setIface(m.iface()); s.setContentLoaded(true); CHECK(s.attach());
    s.core().waitVerdict = [] {};
    CHECK(m.simulateHalt(VJ_DBG_TARGET_M68K, 0x4000, VJ_DBG_STOP_USER, 1) == VJ_DBG_RESUME_NONE);
    s.syncHalt();
    s.detach();
    CHECK(!s.attached() && s.core().detachPending());
    s.breakNow();
    CHECK(m.haltNexts.empty());   // the mirror still says halted: "already halted"
    CHECK(m.simulateHalt(VJ_DBG_TARGET_M68K, 0x4000, VJ_DBG_STOP_USER) == VJ_DBG_RESUME_CONTINUE);
    s.syncHalt();
    CHECK(!s.halted() && !s.attached() && m.attached() && s.core().detachPending());
    s.breakNow();                 // Detach, then Break: the attach inside supersedes the owed drop
    CHECK(m.haltNexts.size() == 1 && s.attached() && !s.core().detachPending() && m.attachCalls == 1);
    s.core().frameEnded();
    CHECK(m.attached() && m.detachCalls == 0);
    s.detach();                   // running: the host goes now
    CHECK(!m.attached());
    s.syncHalt();
    CHECK(s.attach() && s.attached() && m.attached());
    // Running: a queued detach lands before the attach that follows it (both on the lane, in order).
    std::vector<std::function<void()>> lane;
    s.corePost = [&](std::function<void()> fn) { lane.push_back(std::move(fn)); };
    s.coreCall = [&](std::function<void()> fn) { for (auto& f : lane) f(); lane.clear(); fn(); };
    s.detach();
    CHECK(!s.attached());                      // the mirror says so at once
    s.syncHalt();                              // a UI frame lands between the detach and the attach: the queued detach keeps the mirror false
    CHECK(!s.attached());
    CHECK(s.attach() && s.attached() && m.attached() && m.attachCalls == 3);   // detach ran, then this attach
}

// A STEP the core did not answer with a re-halt before the frame ended (the stepped instruction ended it, or the target
// had stopped) must not leave the mirror halted: inline, stepEmulation() is gated on halted(), so the frame that would
// re-halt never ran.  frameEnded() closes a step in flight like a foreign resume; a step that did re-halt is not closed twice.
static void testStepWithoutRehaltClosesAtFrameEnd() {
    DebugSession s; MockDebugIface m; s.setIface(m.iface()); s.setContentLoaded(true); CHECK(s.attach());
    int resumes = 0, resumeUi = 0;
    s.core().onResume.push_back([&] { resumes++; });
    s.onResumeUi.push_back([&] { resumeUi++; });
    s.core().waitVerdict = [&] { s.syncHalt(); s.step(); };
    CHECK(m.simulateHalt(VJ_DBG_TARGET_M68K, 0x4000, VJ_DBG_STOP_USER) == VJ_DBG_RESUME_STEP);
    s.syncHalt();
    CHECK(s.halted() && resumes == 0);          // the bar stays up while the step is in flight
    const uint64_t s1 = s.core().haltSerial();
    s.core().frameEnded();                      // retro_run() returned without a re-halt
    CHECK(!s.core().halt().halted && resumes == 1 && s.core().haltSerial() == s1 + 1);
    s.syncHalt();
    CHECK(!s.halted() && resumeUi == 1);        // the mirror lets the frame loop run again
    s.core().frameEnded();
    CHECK(resumes == 1 && s.core().haltSerial() == s1 + 1);   // once
    // A step that re-halts in the same frame: the re-halt ends the step in flight; the frame end closes that halt once.
    CHECK(m.simulateHalt(VJ_DBG_TARGET_M68K, 0x4000, VJ_DBG_STOP_USER) == VJ_DBG_RESUME_STEP);
    s.core().waitVerdict = [] {};
    CHECK(m.simulateHalt(VJ_DBG_TARGET_M68K, 0x4002, VJ_DBG_STOP_STEP, 1) == VJ_DBG_RESUME_NONE);
    s.core().frameEnded();
    CHECK(resumes == 2);
    s.core().frameEnded();
    CHECK(resumes == 2);
    // A core swap forgets a step in flight too.
    s.core().waitVerdict = [&] { s.syncHalt(); s.step(); };
    CHECK(m.simulateHalt(VJ_DBG_TARGET_M68K, 0x4004, VJ_DBG_STOP_USER) == VJ_DBG_RESUME_STEP);
    MockDebugIface m2;
    s.core().setIface(m2.iface());
    s.core().frameEnded();
    CHECK(resumes == 2);                        // setIface() already forgot it (no onResume: the old core is gone)
}

// Detach with a halt that is not the core's live halt: a STEP in flight (the serial is unchanged but the core is
// running) or a mirror that lags a foreign resume.  The detach is a lane post that decides on the core's real state (here:
// not halted, so the host goes at once); nothing is left pending, and hardcore must really end up detached.
static void testDetachWithoutLiveHalt() {
    DebugSession s; MockDebugIface m; s.setIface(m.iface()); s.setContentLoaded(true); CHECK(s.attach());
    std::vector<std::function<void()>> lane;
    s.corePost = [&](std::function<void()> fn) { lane.push_back(std::move(fn)); };
    s.core().waitVerdict = [&] { s.syncHalt(); s.step(); };
    CHECK(m.simulateHalt(VJ_DBG_TARGET_M68K, 0x4000, VJ_DBG_STOP_USER) == VJ_DBG_RESUME_STEP);
    s.syncHalt();
    CHECK(s.halted() && !s.core().halt().halted);   // STEP in flight: the mirror is up, the core is running
    s.detach();
    CHECK(lane.size() == 1 && s.core().pendingVerdict() == VJ_DBG_RESUME_NONE, "a lane post, no dead verdict");
    CHECK(!s.attached());
    for (auto& f : lane) f();
    lane.clear();
    CHECK(!s.core().attached());
    // The step's re-halt arrives: the new halt is not blocked by a stale detach-pending state.
    s.core().waitVerdict = [] {};
    m.simulateHalt(VJ_DBG_TARGET_M68K, 0x4002, VJ_DBG_STOP_STEP, 1);
    s.syncHalt();
    CHECK(s.halted());
    s.core().frameEnded();

    // Hardcore in the same window: the core ends detached.
    DebugSession h; MockDebugIface m2; h.setIface(m2.iface()); h.setContentLoaded(true); CHECK(h.attach());
    h.corePost = [&](std::function<void()> fn) { lane.push_back(std::move(fn)); };
    h.core().waitVerdict = [&] { h.syncHalt(); h.step(); };
    CHECK(m2.simulateHalt(VJ_DBG_TARGET_M68K, 0x4000, VJ_DBG_STOP_USER) == VJ_DBG_RESUME_STEP);
    h.syncHalt();
    h.setHardcore(true);
    for (auto& f : lane) f();
    lane.clear();
    CHECK(!h.core().attached() && !m2.attached() && !h.attached());

    // A stale mirror: gdb continued (the core's serial moved), the UI has not synced.  detach() posts, F9 does nothing.
    DebugSession t; MockDebugIface m3; t.setIface(m3.iface()); t.setContentLoaded(true); CHECK(t.attach());
    t.corePost = [&](std::function<void()> fn) { lane.push_back(std::move(fn)); };
    t.core().waitVerdict = [] {};
    CHECK(m3.simulateHalt(VJ_DBG_TARGET_M68K, 0x4000, VJ_DBG_STOP_USER, 1) == VJ_DBG_RESUME_NONE);
    t.syncHalt();
    CHECK(t.halted());
    t.core().frameEnded();
    t.resume();
    t.step();
    CHECK(t.core().pendingVerdict() == VJ_DBG_RESUME_NONE);
    t.detach();
    CHECK(lane.size() == 1);
    for (auto& f : lane) f();
    lane.clear();
    CHECK(!t.core().attached() && !m3.attached());
    t.syncHalt();
    CHECK(!t.halted() && t.attach() == true);   // and nothing is left pending
}

int main() {
    testAvailability();
    testAttachAndBreakpoints();
    testHaltResume();
    testHardcoreWhileHalted();
    testDetachWhileHaltedClearsList();
    testNewHaltCancelsDeferredDetach();
    testReleaseHaltKeepsListAndRearms();
    testReleaseSerialAndStepInFlight();
    testBoundedPumpReturnsNone();
    testDetachEndsPumpWithinOneWait();
    testStepSerialAndReason();
    testFrameEndedClosesForeignResume();
    testForeignStepIsNewHalt();
    testAbortHaltContinues();
    testInlinePumpRunsUiFrame();
    testPumpCallsHookOncePerIteration();
    testVerdictForAnotherSerialDropped();
    testSetIfaceDropsHaltAndVerdict();
    testSessionVerdictsReachCore();
    testSyncHaltEdges();
    testHaltByGdb();
    testStaleVerdictDropped();
    testHardcoreGoesThroughTheLane();
    testAttachWhileDetachPending();
    testStepWithoutRehaltClosesAtFrameEnd();
    testDetachWithoutLiveHalt();
    testPokePolicy();
    testRefreshCadence();
    testTools();
    testProviders();
    testMockContent();
    testCoreWrappers();
    testMarshalling();
    testCoreSyncSplit();
    testReplacedCoreClosesTools();
    testViewsFill();
    testIfaceSwap();
    std::printf("debug_session_test: %d checks, %d failed\n", g_checks, g_fail);
    return g_fail ? 1 : 0;
}
