// Unit tests for the CPU debugger's logic (app/src/debug/cpu_view.*) against the mock core interface:
// the vendored Musashi disassembler on fixed opcode bytes, the GPU/DSP path, backwards disassembly, the
// SR / FLAGS / CTRL decoders, the step-over decision, number parsing, and the run controls (break, continue,
// step, step over, run to cursor and their temporary breakpoint).  No window, no GL, no core.
#include "cpu_view.h"
#include "debug_mock.h"
#include "debug_ui_fixture.h"

#include <cstdio>
#include <cstring>
#include <functional>
#include <string>
#include <vector>

using namespace vj;
using namespace vj::cpuview;

static int g_fail = 0, g_checks = 0;
#define CHECK(cond)                                                                \
    do {                                                                           \
        g_checks++;                                                                \
        if (!(cond)) {                                                             \
            g_fail++;                                                              \
            std::fprintf(stderr, "FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond);   \
        }                                                                          \
    } while (0)
#define CHECK_STR(got, want)                                                                                    \
    do {                                                                                                        \
        g_checks++;                                                                                             \
        std::string g_ = (got);                                                                                 \
        if (g_ != (want)) {                                                                                     \
            g_fail++;                                                                                           \
            std::fprintf(stderr, "FAIL %s:%d  got [%s] want [%s]\n", __FILE__, __LINE__, g_.c_str(), (want)); \
        }                                                                                                       \
    } while (0)

static void put(MockDebugIface& m, unsigned addr, std::initializer_list<unsigned> bytes) {
    for (unsigned b : bytes) m.dram()[addr++] = (uint8_t)b;
}

static void testMusashi() {
    MockDebugIface m;
    const vj_debug_iface* i = m.iface();
    struct Case {
        std::vector<unsigned> bytes;
        unsigned len;
        const char* text;
        const char* hex;
    };
    const Case cases[] = {
        {{0x30, 0x3C, 0x12, 0x34}, 4, "move.w  #$1234, D0", "303C 1234"},
        {{0x4E, 0xB9, 0x00, 0x00, 0x12, 0x34}, 6, "jsr     $1234.l", "4EB9 0000 1234"},
        {{0x60, 0xFE}, 2, "bra     $1000", "60FE"},
        {{0x51, 0xC8, 0xFF, 0xFE}, 4, "dbra    D0, $1000", "51C8 FFFE"},
        {{0x48, 0xE7, 0xFF, 0xFE}, 4, "movem.l D0-D7/A0-A6, -(A7)", "48E7 FFFE"},
        {{0x61, 0x00, 0x00, 0x10}, 4, "bsr     $1012", "6100 0010"},
        {{0x4E, 0x75}, 2, "rts", "4E75"},
        {{0x4E, 0x71}, 2, "nop", "4E71"},
        {{0x4E, 0x73}, 2, "rte", "4E73"},
        {{0x23, 0xFC, 0xDE, 0xAD, 0xBE, 0xEF, 0x00, 0x01, 0x00, 0x00}, 10, "move.l  #$deadbeef, $10000.l",
         "23FC DEAD BEEF 0001 0000"},
    };
    for (const Case& c : cases) {
        for (size_t k = 0; k < c.bytes.size(); k++) m.dram()[0x1000 + k] = (uint8_t)c.bytes[k];
        Line l = disasmOne(i, VJ_DBG_TARGET_M68K, 0x1000);
        CHECK(l.valid);
        CHECK_STR(l.text, c.text);
        CHECK(l.len == c.len);
        CHECK_STR(l.bytes, c.hex);
        CHECK(l.addr == 0x1000);
    }
    // An odd address is rounded down; the end of the bus is clipped, not an error.
    put(m, 0x1000, {0x4E, 0x75});
    CHECK(disasmOne(i, VJ_DBG_TARGET_M68K, 0x1001).addr == 0x1000);
    Line end = disasmOne(i, VJ_DBG_TARGET_M68K, 0xFFFFFE);
    CHECK(end.valid && end.len == 2);
    // No core: a placeholder, never a crash.
    Line none = disasmOne(nullptr, VJ_DBG_TARGET_M68K, 0x1000);
    CHECK(!none.valid && none.len == 2);

    // forward / next
    put(m, 0x2000, {0x4E, 0x71, 0x4E, 0x71, 0x30, 0x3C, 0x12, 0x34, 0x4E, 0x75});
    std::vector<Line> v = disasmForward(i, VJ_DBG_TARGET_M68K, 0x2000, 4);
    CHECK(v.size() == 4 && v[0].addr == 0x2000 && v[1].addr == 0x2002 && v[2].addr == 0x2004 && v[3].addr == 0x2008);
    CHECK(nextAddr(i, VJ_DBG_TARGET_M68K, 0x2004) == 0x2008);
}

static void testRisc() {
    MockDebugIface m;
    const vj_debug_iface* i = m.iface();
    Line a = disasmOne(i, VJ_DBG_TARGET_GPU, 0xF03000);
    CHECK(a.valid && a.len == 2);
    CHECK_STR(a.text, "fake    gpu $F03000");   // (the mnemonic column is padded to 8)
    Line b = disasmOne(i, VJ_DBG_TARGET_DSP, 0xF1B006);   // the mock reports a 6-byte movei at addr % 8 == 6
    CHECK(b.valid && b.len == 6);
    CHECK(b.text.find("movei") == 0);
    CHECK(nextAddr(i, VJ_DBG_TARGET_DSP, 0xF1B006) == 0xF1B00C);
    CHECK(!disasmOne(i, 7, 0xF03000).valid);
    // 68K-only helpers say nothing about the RISCs.
    CHECK(!stepOverTarget(i, VJ_DBG_TARGET_GPU, 0xF03000).overCall);
}

static void testStrip() {
    CHECK_STR(stripRiscPrefix("$F03192: D7E2  jr             Z, -1 -> $F03192", 2), "jr             Z, -1 -> $F03192");
    CHECK_STR(stripRiscPrefix("$F0319E: 9801 88FF 0000 movei          #$00008FF, r1", 6), "movei          #$00008FF, r1");
    CHECK_STR(stripRiscPrefix("$F0319E: 9801  movei          #$00008FF, r1", 6), "movei          #$00008FF, r1");   // what the core prints
    // "addc" is four hex digits: only len/2 words are removed
    CHECK_STR(stripRiscPrefix("$F03000: 0123  addc           r1, r2", 2), "addc           r1, r2");
    CHECK_STR(stripRiscPrefix("fake gpu $F03000", 2), "fake gpu $F03000");
    CHECK_STR(stripRiscPrefix("$F03000: 12 nop", 2), "$F03000: 12 nop");   // not the core's format: left alone
    CHECK_STR(stripRiscPrefix("$F03000: nop", 2), "$F03000: nop");
    CHECK_STR(stripRiscPrefix("", 2), "");
}

static void testTidy() {
    CHECK_STR(tidyRiscSpacing("neg            r8, r20"), "neg     r8, r20");
    CHECK_STR(tidyRiscSpacing("movei          #$000088FF, r1"), "movei   #$000088FF, r1");
    CHECK_STR(tidyRiscSpacing("nop"), "nop");
    CHECK_STR(tidyRiscSpacing("nop     "), "nop");
    CHECK_STR(tidyRiscSpacing("moveq_long_name x"), "moveq_long_name x");
    CHECK_STR(tidyRiscSpacing("jr             Z, -1 -> $F03192"), "jr      Z, -1 -> $F03192");
}

static void testBackStart() {
    MockDebugIface m;
    const vj_debug_iface* i = m.iface();
    for (unsigned k = 0; k < 0x40; k++) m.dram()[0x3000 + k] = 0x00;
    // nop nop move.w #$1234,D0 jsr $1234.l nop rts
    put(m, 0x3000, {0x4E, 0x71, 0x4E, 0x71, 0x30, 0x3C, 0x12, 0x34, 0x4E, 0xB9, 0x00, 0x00, 0x12, 0x34, 0x4E, 0x71, 0x4E, 0x75});
    const unsigned rts = 0x3010;
    CHECK(nextAddr(i, VJ_DBG_TARGET_M68K, 0x3008) == 0x300E);
    CHECK(backStart(i, VJ_DBG_TARGET_M68K, rts, 2) == 0x3008);   // jsr, nop
    CHECK(backStart(i, VJ_DBG_TARGET_M68K, rts, 1) == 0x300E);
    CHECK(prevAddr(i, VJ_DBG_TARGET_M68K, rts) == 0x300E);
    unsigned s3 = backStart(i, VJ_DBG_TARGET_M68K, rts, 3);
    CHECK(s3 == 0x3004);                                        // move.w, jsr, nop
    // Whatever it picks, a linear decode from it lands exactly on pc.
    for (int n = 1; n <= 6; n++) {
        unsigned a = backStart(i, VJ_DBG_TARGET_M68K, rts, n);
        int steps = 0;
        while (a < rts && steps < 20) {
            a = nextAddr(i, VJ_DBG_TARGET_M68K, a);
            steps++;
        }
        CHECK(a == rts && steps <= n);
    }
    CHECK(backStart(i, VJ_DBG_TARGET_M68K, 0, 5) == 0);
    CHECK(backStart(i, VJ_DBG_TARGET_M68K, 4, 5) <= 4);
    // RISC (the mock's 2-byte instructions, a 6-byte one every 8 bytes at offset 6)
    CHECK(backStart(i, VJ_DBG_TARGET_GPU, 0xF0300C, 1) == 0xF0300A);   // nearest instruction that ends at pc
    for (int n = 1; n <= 6; n++) {
        unsigned a = backStart(i, VJ_DBG_TARGET_GPU, 0xF0300C, n);
        int steps = 0;
        while (a < 0xF0300C && steps < 20) {
            a = nextAddr(i, VJ_DBG_TARGET_GPU, a);
            steps++;
        }
        CHECK(a == 0xF0300C && steps <= n);
    }
}

static void testStepOver() {
    MockDebugIface m;
    const vj_debug_iface* i = m.iface();
    put(m, 0x4000, {0x4E, 0xB9, 0x00, 0x00, 0x12, 0x34});   // jsr $1234.l
    StepOver s = stepOverTarget(i, VJ_DBG_TARGET_M68K, 0x4000);
    CHECK(s.overCall && s.next == 0x4006 && s.why == "jsr");
    put(m, 0x4100, {0x4E, 0x90});                            // jsr (A0)
    s = stepOverTarget(i, VJ_DBG_TARGET_M68K, 0x4100);
    CHECK(s.overCall && s.next == 0x4102);
    put(m, 0x4200, {0x61, 0x00, 0x00, 0x10});                // bsr.w
    s = stepOverTarget(i, VJ_DBG_TARGET_M68K, 0x4200);
    CHECK(s.overCall && s.next == 0x4204 && s.why == "bsr");
    put(m, 0x4300, {0x61, 0x10});                            // bsr.b
    s = stepOverTarget(i, VJ_DBG_TARGET_M68K, 0x4300);
    CHECK(s.overCall && s.next == 0x4302);
    put(m, 0x4400, {0x4E, 0x41});                            // trap #1
    s = stepOverTarget(i, VJ_DBG_TARGET_M68K, 0x4400);
    CHECK(s.overCall && s.next == 0x4402 && s.why == "trap");
    // not calls: bra, bcc, jmp, dbra, rts, move
    const std::vector<std::vector<unsigned>> plain = {{0x60, 0x10}, {0x66, 0x10}, {0x4E, 0xF9, 0, 0, 0x10, 0}, {0x51, 0xC8, 0xFF, 0xFE}, {0x4E, 0x75}, {0x30, 0x3C, 0, 1}};
    for (const auto& p : plain) {
        for (size_t k = 0; k < p.size(); k++) m.dram()[0x4500 + k] = (uint8_t)p[k];
        CHECK(!stepOverTarget(i, VJ_DBG_TARGET_M68K, 0x4500).overCall);
    }
}

static void testDecoders() {
    // SR
    std::vector<Flag> f = decodeSr(0x2704);
    auto flag = [&](const char* n) {
        for (const Flag& x : f)
            if (std::strcmp(x.name, n) == 0) return x.set;
        return false;
    };
    CHECK(flag("S") && flag("Z") && !flag("T") && !flag("X") && !flag("N") && !flag("V") && !flag("C"));
    CHECK(srIpl(0x2704) == 7 && srIpl(0x2004) == 0 && srSupervisor(0x2704) && !srSupervisor(0x0004));
    CHECK_STR(srFlagString(0x201F), "X N Z V C");
    CHECK_STR(srFlagString(0x2000), "-");
    CHECK_STR(srFlagString(0x8005), "Z C");
    f = decodeSr(0x8000);
    CHECK(flag("T"));
    CHECK(decodeSr(0)[3].mask == 0x08u);   // N toggles bit 3

    // G_FLAGS / D_FLAGS
    RiscFlags g = decodeRiscFlags(0x4005, false);
    CHECK(g.zero && !g.carry && g.nega && !g.imask && g.regpage && !g.dmaen);
    g = decodeRiscFlags(0x0002 | 0x0008 | (0x15u << 4), false);
    CHECK(g.carry && g.imask && !g.regpage && g.intEnable == 0x15u);
    CHECK_STR(intList(g.intEnable, false), "0 2 4");
    RiscFlags d = decodeRiscFlags(0x10000u | (1u << 4) | 0x8000u, true);
    CHECK(d.intEnable == 0x21u && d.dmaen);
    CHECK_STR(intList(d.intEnable, true), "0 5");
    CHECK_STR(intList(0, true), "none");
    // the GPU has no int 5: bit 16 means nothing there
    CHECK(decodeRiscFlags(0x10000u, false).intEnable == 0);
    CHECK_STR(intList(0x20u, false), "none");

    CHECK(riscActiveBank(0x4000) == 1 && riscActiveBank(0x0000) == 0);
    CHECK(riscActiveBank(0x4008) == 0);   // IMASK forces bank 0 even with REGPAGE set

    // G_CTRL / D_CTRL
    RiscCtrl c = decodeRiscCtrl(0x2819u, false);   // go + single step + single go + bus hog + version 2
    CHECK(c.go && c.singleStep && c.singleGo && c.busHog && c.version == 2);
    c = decodeRiscCtrl((1u << 6) | (1u << 8), false);
    CHECK(!c.go && c.latch == 0x5u);
    c = decodeRiscCtrl(0x10000u | (1u << 10), true);
    CHECK(c.latch == 0x30u);
    CHECK(decodeRiscCtrl(0x10000u, false).latch == 0);

    CHECK_STR(reg68kName(0), "D0");
    CHECK_STR(reg68kName(15), "A7");
    CHECK_STR(reg68kName(16), "PC");
    CHECK_STR(reg68kName(19), "SSP");
    CHECK_STR(reg68kName(20), "?");
    CHECK(regRiscBank(0, 5) == 5 && regRiscBank(1, 5) == 37);
}

static void testParse() {
    unsigned v = 0;
    CHECK(parseHex("F03000", v) && v == 0xF03000);
    CHECK(parseHex("$f03000", v) && v == 0xF03000);
    CHECK(parseHex("  0x1000  ", v) && v == 0x1000);
    CHECK(parseHex("#$10", v) && v == 0x10);
    CHECK(parseHex("DEADBEEF", v) && v == 0xDEADBEEF);
    CHECK(parseHex("DEADBEEF", v, true) && v == 0xADBEEF);
    CHECK(!parseHex("", v) && !parseHex("$", v) && !parseHex("12G4", v) && !parseHex("123456789", v) && !parseHex(nullptr, v));
}

// ---- run controls ---------------------------------------------------------------------------------------------

static void testControls() {
    DebugSession s;
    MockDebugIface m;
    s.setIface(m.iface());
    s.setContentLoaded(true);
    CpuControl c(s);
    s.onHalt.push_back([&](int t, unsigned pc, int r) { c.onHalt(t, pc, r); });

    // Break arms halt_next on that target and remembers it is pending until the halt.
    CHECK(c.breakIn(VJ_DBG_TARGET_GPU));
    CHECK(m.haltNexts.size() == 1 && m.haltNexts[0] == VJ_DBG_TARGET_GPU && c.pending(VJ_DBG_TARGET_GPU));
    CHECK(!c.pending(VJ_DBG_TARGET_M68K));
    s.runUiFrame = [&] { s.resume(); };
    m.simulateHalt(VJ_DBG_TARGET_GPU, 0xF03000, VJ_DBG_STOP_USER);
    CHECK(!c.pending(VJ_DBG_TARGET_GPU));

    // Not halted: step buttons do nothing and say so.
    CHECK(!c.stepIn(VJ_DBG_TARGET_M68K) && c.note() == "Not halted");
    CHECK(!c.cont() && !c.stepOver(VJ_DBG_TARGET_M68K) && !c.runTo(VJ_DBG_TARGET_M68K, 0x5000));

    // Step on the halted tab is a STEP verdict.
    s.runUiFrame = [&] { CHECK(c.stepIn(VJ_DBG_TARGET_M68K)); };
    int v = m.simulateHalt(VJ_DBG_TARGET_M68K, 0x4000);
    CHECK(v == VJ_DBG_RESUME_STEP);

    // Step on another tab: the halted 68K resumes and the GPU is told to halt at its next instruction.
    m.haltNexts.clear();
    s.runUiFrame = [&] { CHECK(c.stepIn(VJ_DBG_TARGET_GPU)); };
    v = m.simulateHalt(VJ_DBG_TARGET_M68K, 0x4000);
    CHECK(v == VJ_DBG_RESUME_CONTINUE && m.haltNexts.size() == 1 && m.haltNexts[0] == VJ_DBG_TARGET_GPU);
    CHECK(c.pending(VJ_DBG_TARGET_GPU));
    c.forget();
    CHECK(!c.pending(VJ_DBG_TARGET_GPU));

    // Step over a JSR: temporary breakpoint at the next instruction, then a CONTINUE.
    put(m, 0x4000, {0x4E, 0xB9, 0x00, 0x00, 0x12, 0x34});
    m.breakpoints.clear();
    s.runUiFrame = [&] {
        CHECK(c.stepOver(VJ_DBG_TARGET_M68K));
        CHECK(c.temp().active() && c.temp().addr() == 0x4006);
        bool has = false;
        for (auto& b : m.breakpoints) has |= (b.first == VJ_DBG_TARGET_M68K && b.second == 0x4006);
        CHECK(has);
        CHECK(s.breakpoints().empty());            // the session's list stays the user's
    };
    v = m.simulateHalt(VJ_DBG_TARGET_M68K, 0x4000);
    CHECK(v == VJ_DBG_RESUME_CONTINUE);
    // The next halt (here: the temp breakpoint being hit) clears it.
    s.runUiFrame = [&] { s.resume(); };
    m.simulateHalt(VJ_DBG_TARGET_M68K, 0x4006, VJ_DBG_STOP_BREAKPOINT);
    CHECK(!c.temp().active());
    CHECK(m.breakpoints.empty());

    // Step over a plain instruction is a plain step.
    put(m, 0x4000, {0x4E, 0x71});
    s.runUiFrame = [&] { CHECK(!c.stepOver(VJ_DBG_TARGET_M68K)); };
    v = m.simulateHalt(VJ_DBG_TARGET_M68K, 0x4000);
    CHECK(v == VJ_DBG_RESUME_STEP && !c.temp().active());
    // ...and so is every GPU/DSP instruction.
    s.runUiFrame = [&] { CHECK(!c.stepOver(VJ_DBG_TARGET_GPU)); };
    v = m.simulateHalt(VJ_DBG_TARGET_GPU, 0xF03000);
    CHECK(v == VJ_DBG_RESUME_STEP);

    // Run to cursor.
    s.runUiFrame = [&] { CHECK(c.runTo(VJ_DBG_TARGET_M68K, 0x5000)); };
    v = m.simulateHalt(VJ_DBG_TARGET_M68K, 0x4000);
    CHECK(v == VJ_DBG_RESUME_CONTINUE && c.temp().active() && c.temp().addr() == 0x5000);
    CHECK(m.breakpoints.size() == 1);
    // Continue drops a pending run-to.
    s.runUiFrame = [&] { CHECK(c.cont()); };
    v = m.simulateHalt(VJ_DBG_TARGET_M68K, 0x4002);
    CHECK(v == VJ_DBG_RESUME_CONTINUE && !c.temp().active());
    CHECK(m.breakpoints.empty());

    // A temporary breakpoint where the user has an enabled one: theirs stays when the temp goes.
    CHECK(s.addBreakpoint(VJ_DBG_TARGET_M68K, 0x6000));
    s.runUiFrame = [&] { CHECK(c.runTo(VJ_DBG_TARGET_M68K, 0x6000)); };
    m.simulateHalt(VJ_DBG_TARGET_M68K, 0x4000);
    CHECK(c.temp().active() && m.breakpoints.size() == 1);
    s.runUiFrame = [&] { s.resume(); };
    m.simulateHalt(VJ_DBG_TARGET_M68K, 0x6000);
    CHECK(!c.temp().active() && m.breakpoints.size() == 1);
    // A disabled user breakpoint is not in the core: the temp owns the slot and clears it.
    s.setBreakpointEnabled(VJ_DBG_TARGET_M68K, 0x6000, false);
    CHECK(m.breakpoints.empty());
    s.runUiFrame = [&] { CHECK(c.runTo(VJ_DBG_TARGET_M68K, 0x6000)); };
    m.simulateHalt(VJ_DBG_TARGET_M68K, 0x4000);
    CHECK(m.breakpoints.size() == 1);
    s.runUiFrame = [&] { s.resume(); };
    m.simulateHalt(VJ_DBG_TARGET_M68K, 0x6000);
    CHECK(m.breakpoints.empty());

    // The tools off: Break explains why.
    s.setHardcore(true);
    CHECK(!c.breakIn(VJ_DBG_TARGET_M68K) && !c.note().empty());
}

// #967 3a: the window's feed adopts published buffers and never reaches the core itself.
static void testCpuFeedReadsBuffersOnly() {
    UiFixture f;
    f.s.views().setDisasm(&disasmRows);
    CpuFeed feed;
    feed.want(f.s, VJ_DBG_TARGET_M68K, 0x1000, 8);
    f.s.wakeViews();                                     // inline: the copier runs now
    const UiFixture::Mark m = f.mark();
    CHECK(feed.update(f.s) && feed.haveRegs() && feed.regsTarget() == VJ_DBG_TARGET_M68K);
    CHECK(feed.regs().pc == 0x4000 && feed.regs().d[3] == 0x1003);
    CHECK(feed.rowsFor(VJ_DBG_TARGET_M68K, 0x1000, 8) && feed.rows().size() == 8);
    CHECK(feed.rows()[0].addr == 0x1000 && !feed.rows()[0].bytes.empty());
    for (size_t i = 1; i < 8; i++) CHECK(feed.rows()[i].addr == feed.rows()[i - 1].addr + feed.rows()[i - 1].len);
    CHECK(!feed.update(f.s) && !feed.update(f.s));         // nothing new
    CHECK(f.quietSince(m));                                // update() never reaches the core
    feed.want(f.s, VJ_DBG_TARGET_GPU, 0xF03000, 4);        // a tab switch: the old answer is not the new one
    CHECK(!feed.rowsFor(VJ_DBG_TARGET_GPU, 0xF03000, 4) && feed.regsTarget() == VJ_DBG_TARGET_M68K);
    f.s.wakeViews();
    CHECK(feed.update(f.s) && feed.regsTarget() == VJ_DBG_TARGET_GPU && feed.regs().risc_pc == 0xF03000);
    CHECK(feed.rowsFor(VJ_DBG_TARGET_GPU, 0xF03000, 4) && feed.rows()[0].text.find("fake") == 0);
    // The same request restated changes nothing; a new refresh serial adopts a fresh buffer.
    feed.want(f.s, VJ_DBG_TARGET_GPU, 0xF03000, 4);
    CHECK(!feed.update(f.s));
    f.s.requestRefresh();
    f.s.wakeViews();
    CHECK(feed.update(f.s));
    // A new core forgets what was adopted; releasing drops the requests.
    f.s.views().bumpEpoch();
    CHECK(feed.update(f.s) && !feed.answered() && !feed.haveRegs() && feed.regsTarget() == -1);
    f.s.wakeViews();
    CHECK(feed.update(f.s) && feed.answered() && feed.haveRegs());
    feed.release(f.s);
    CHECK(f.s.views().count() == 0 && !feed.answered() && !feed.rowsFor(VJ_DBG_TARGET_GPU, 0xF03000, 4));
}

static void testDisasmNav() {
    UiFixture f;
    std::vector<std::function<void()>> q;
    f.s.corePost = [&](std::function<void()> fn) { q.push_back(std::move(fn)); };
    DisasmNav nav;
    nav.back(f.s, VJ_DBG_TARGET_M68K, 0x1010, 4);
    unsigned a = 0;
    CHECK(nav.pending() && !nav.take(a));
    std::vector<std::function<void()>> work = std::move(q);
    q.clear();
    for (auto& fn : work) {
        CountingIface::OnCore g;
        fn();
    }
    CHECK(nav.take(a) && a == backStart(f.mock.iface(), VJ_DBG_TARGET_M68K, 0x1010, 4));
    CHECK(!nav.pending());
    // prev walks one instruction back `count` times.
    nav.prev(f.s, VJ_DBG_TARGET_M68K, 0x1010, 3);
    work = std::move(q);
    q.clear();
    for (auto& fn : work) {
        CountingIface::OnCore g;
        fn();
    }
    unsigned want = 0x1010;
    for (int k = 0; k < 3; k++) want = prevAddr(f.mock.iface(), VJ_DBG_TARGET_M68K, want);
    CHECK(nav.take(a) && a == want);
}

// #967 3a: the run controls reach the core only through callCore.
static void testControlsMarshalled() {
    UiFixture f;
    CpuControl c(f.s);
    TempBreak tb;
    const UiFixture::Mark m = f.mark();
    CHECK(tb.arm(f.s, VJ_DBG_TARGET_M68K, 0x5000) && f.mock.breakpoints.size() == 1);
    CHECK(f.mark().coreCalls > m.coreCalls);
    tb.disarm(f.s);
    CHECK(f.mock.breakpoints.empty());
    CHECK(CountingIface::offCore() == 0);
}

int main() {
    CountingIface::reset();
    testMusashi();
    testRisc();
    testStrip();
    testTidy();
    testBackStart();
    testStepOver();
    testDecoders();
    testParse();
    testControls();
    testCpuFeedReadsBuffersOnly();
    testDisasmNav();
    testControlsMarshalled();
    std::printf("debug_cpu_unit: %d checks, %d failed\n", g_checks, g_fail);
    return g_fail ? 1 : 0;
}
