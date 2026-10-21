// Unit tests for the Memory viewer / CLUT viewer logic (app/src/debug/memory_view.*) against the mock
// core interface (debug_mock.h).  No window, no GL, no core.
#include "debug_mock.h"
#include "debug_session.h"
#include "debug_ui_fixture.h"
#include "memory_view.h"

#include <cstdio>
#include <string>
#include <vector>

using namespace vj;
using namespace vj::memview;

static int g_fail = 0, g_checks = 0;
#define CHECK(cond)                                                                \
    do {                                                                           \
        g_checks++;                                                                \
        if (!(cond)) {                                                             \
            g_fail++;                                                              \
            std::fprintf(stderr, "FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond);   \
        }                                                                          \
    } while (0)
#define CHECK_EQ(a, b)                                                                                     \
    do {                                                                                                   \
        g_checks++;                                                                                        \
        std::string va_ = (a), vb_ = (b);                                                                  \
        if (va_ != vb_) {                                                                                  \
            g_fail++;                                                                                      \
            std::fprintf(stderr, "FAIL %s:%d  %s\n   got      '%s'\n   expected '%s'\n", __FILE__, __LINE__, \
                         #a, va_.c_str(), vb_.c_str());                                                    \
        }                                                                                                  \
    } while (0)

struct Rig {
    MockDebugIface mock;
    DebugSession s;
    Rig() {
        s.setIface(mock.iface());
        s.setContentLoaded(true);
    }
};

static void testParsingAndFormatting() {
    unsigned v = 0;
    CHECK(parseHex("1F0000", v) && v == 0x1F0000);
    CHECK(parseHex("  $F00400 ", v) && v == 0xF00400);
    CHECK(parseHex("0xdeadBEEF", v) && v == 0xDEADBEEF);
    CHECK(!parseHex("", v) && !parseHex("$", v) && !parseHex("12G", v) && !parseHex("123456789", v));
    CHECK(parseAddress("FFFFFF", v) && v == 0xFFFFFF);
    CHECK(!parseAddress("1000000", v));

    std::vector<uint8_t> b;
    CHECK(parseHexBytes("DE AD be ef", b) && b.size() == 4 && b[0] == 0xDE && b[3] == 0xEF);
    CHECK(parseHexBytes("deadbeef", b) && b.size() == 4 && b[1] == 0xAD);
    CHECK(parseHexBytes("0xDE,0xAD", b) && b.size() == 2 && b[1] == 0xAD);
    CHECK(!parseHexBytes("D", b) && !parseHexBytes("", b) && !parseHexBytes("DE XX", b));

    CHECK(parseCell("ff", 1, v) && v == 0xFF);
    CHECK(!parseCell("100", 1, v));
    CHECK(parseCell("0000ABCD", 2, v) && v == 0xABCD);   // leading zeros are free
    CHECK(!parseCell("12345", 2, v));
    CHECK(parseCell("DEADBEEF", 4, v) && v == 0xDEADBEEF);
    CHECK(!parseCell("zz", 1, v) && !parseCell("", 1, v));

    // Big-endian cells.
    const uint8_t m[8] = {0x12, 0x34, 0x56, 0x78, 0x9A, 0xBC, 0xDE, 0xF0};
    CHECK(decodeBE(m, 1) == 0x12 && decodeBE(m, 2) == 0x1234 && decodeBE(m, 4) == 0x12345678);
    uint8_t o[4] = {};
    encodeBE(o, 4, 0xA1B2C3D4);
    CHECK(o[0] == 0xA1 && o[1] == 0xB2 && o[2] == 0xC3 && o[3] == 0xD4);
    encodeBE(o, 2, 0xBEEF);
    CHECK(o[0] == 0xBE && o[1] == 0xEF);
    CHECK_EQ(hexCell(0xA, 1), "0A");
    CHECK_EQ(hexCell(0xA0B, 2), "0A0B");
    CHECK_EQ(hexCell(0xA0B0C0D, 4), "0A0B0C0D");
    CHECK_EQ(hexCell(0x1FF, 1), "FF");   // never wider than the cell
    CHECK(asciiChar('A') == 'A' && asciiChar(0x1F) == '.' && asciiChar(0x7F) == '.' && asciiChar(0xC0) == '.');

    const uint8_t t[8] = {'J', 'a', 'g', 0, 0x12, 0x34, 0x56, 0x78};
    CHECK_EQ(formatDumpLine(0x1000, t, 8, 1, true), "001000  4A 61 67 00 12 34 56 78  |Jag..4Vx|");
    CHECK_EQ(formatDumpLine(0x1000, t, 8, 2, false), "001000  4A61 6700 1234 5678");
    CHECK_EQ(formatDumpLine(0x1000, t, 8, 4, false), "001000  4A616700 12345678");
    CHECK_EQ(formatDumpLine(0x1000, t, 7, 4, false), "001000  4A616700 12 34 56");   // trailing partial cell by bytes
}

static void testRegionsAndClamps() {
    Rig r;
    const auto& regs = r.s.regions();
    CHECK(regs.size() == 11);
    int dram = regionIndexAt(regs, 0x1FFFFF);
    CHECK(dram == 0);
    CHECK(regionIndexAt(regs, 0x200000) == -1);         // the hole after DRAM
    CHECK(regionIndexAt(regs, 0xF04000) == -1);         // between GPU RAM and JERRY
    CHECK(regionIndexAt(regs, 0xF03FFF) >= 0);
    CHECK(regs[(size_t)regionIndexAt(regs, 0xF00400)].name == std::string("CLUT"));

    const vj_debug_region& d = regs[0];
    CHECK(clampToRegion(d, 0x1FFFF0, 64) == 16);        // a partial last row
    CHECK(clampToRegion(d, 0x1000, 64) == 64);
    CHECK(clampToRegion(d, 0x200000, 16) == 0);         // outside
    CHECK(rowCount(d, 16) == 0x20000);
    vj_debug_region odd{"odd", 0x800000, 0x25, VJ_DBG_REGION_ROM};
    CHECK(rowCount(odd, 16) == 3);                      // 0x25 bytes -> 3 rows, last one 5 bytes
    CHECK(clampToRegion(odd, 0x800020, 16) == 5);

    CHECK(bytesPerRowFor(1, 1000, 30) == 16);
    CHECK(bytesPerRowFor(1, 150, 30) == 4);             // 5 cells fit -> 4
    CHECK(bytesPerRowFor(1, 10, 30) == 1);
    CHECK(bytesPerRowFor(2, 1000, 50) == 16);
    CHECK(bytesPerRowFor(4, 1000, 80) == 16);
    CHECK(bytesPerRowFor(4, 100, 80) == 4);

    unsigned a = 0;
    int idx = -1;
    std::string err;
    CHECK(resolveGoto(regs, "$F03000", a, idx, err) && a == 0xF03000 && regs[(size_t)idx].name == std::string("GPU RAM"));
    CHECK(!resolveGoto(regs, "300000", a, idx, err) && err.find("not mapped") != std::string::npos);
    CHECK(!resolveGoto(regs, "1000000", a, idx, err) && err.find("24 bit") != std::string::npos);
    CHECK(!resolveGoto(regs, "hello", a, idx, err) && !err.empty());
    CHECK(pointerTarget(0xFF123456u) == 0x123456u);
}

static void testChangeTracker() {
    ChangeTracker t(3);
    uint8_t a[8] = {1, 2, 3, 4, 5, 6, 7, 8};
    t.update(0x100, a, 8);
    for (unsigned i = 0; i < 8; i++) CHECK(!t.changed(0x100 + i));   // first read: no baseline, nothing lit
    a[2] = 0x99;
    t.update(0x100, a, 8);
    CHECK(t.changed(0x102) && !t.changed(0x101) && !t.changed(0x103));
    // the mark fades after `hold` updates
    t.update(0x100, a, 8);
    CHECK(t.changed(0x102));
    t.update(0x100, a, 8);
    CHECK(t.changed(0x102));
    t.update(0x100, a, 8);
    CHECK(!t.changed(0x102));

    // Scrolling: bytes that carry over keep their baseline, new ones have none, nothing lights up.
    uint8_t b[8] = {5, 6, 7, 8, 9, 10, 11, 12};
    t.update(0x104, b, 8, false);
    for (unsigned i = 0; i < 8; i++) CHECK(!t.changed(0x104 + i));
    // ...and a real change in the carried bytes is still seen on the next refresh.
    b[1] = 0x77;
    t.update(0x104, b, 8, true);
    CHECK(t.changed(0x105) && !t.changed(0x104) && !t.changed(0x108));
    // A jump to elsewhere (go-to / other region) has no baseline at all.
    uint8_t c[4] = {9, 9, 9, 9};
    t.update(0xF00400, c, 4);
    CHECK(!t.changed(0xF00400) && !t.changed(0x105));
    t.reset();
    CHECK(t.window() == 0);
}

static void testSearch() {
    Rig r;
    // Plant a pattern in DRAM at 0x3000 and 0x5000 and text at 0x4000 and near the region end.
    auto& d = r.mock.dram();
    const char* txt = "Virtual Jaguar";
    for (unsigned i = 0; txt[i]; i++) d[0x4000 + i] = (uint8_t)txt[i];
    const uint8_t pat[] = {0xDE, 0xAD, 0xBE, 0xEF};
    for (unsigned i = 0; i < 4; i++) d[0x3000 + i] = d[0x5000 + i] = pat[i];
    const vj_debug_region dram = r.s.regions()[0];

    std::vector<uint8_t> needle(pat, pat + 4);
    SearchJob j(r.s, dram, 0, needle);
    while (!j.step(0x10000)) {}
    CHECK(j.done() && j.found() && j.address() == 0x3000);
    // "find next" starts after the last hit...
    SearchJob j2(r.s, dram, 0x3000, needle);
    while (!j2.step(0x8000)) {}
    CHECK(j2.found() && j2.address() == 0x5000);
    // ...and wraps around the region.
    SearchJob j3(r.s, dram, 0x5000, needle);
    while (!j3.step(0x100000)) {}
    CHECK(j3.found() && j3.address() == 0x3000);
    CHECK(j3.progress() == 1.0f);

    // Text, case-sensitive and not.
    SearchJob t1(r.s, dram, 0, textNeedle("Jaguar"));
    while (!t1.step(0x10000)) {}
    CHECK(t1.found() && t1.address() == 0x4000 + 8);
    SearchJob t2(r.s, dram, 0, textNeedle("jaguar"));
    while (!t2.step(0x10000)) {}
    CHECK(!t2.found());
    SearchJob t3(r.s, dram, 0, textNeedle("jaguar"), true);
    while (!t3.step(0x10000)) {}
    CHECK(t3.found() && t3.address() == 0x4008);

    // Not there; empty / oversized needles finish at once.
    SearchJob n1(r.s, dram, 0, std::vector<uint8_t>{0xFA, 0xCE, 0xFE, 0xED, 0x13, 0x37});
    while (!n1.step(0x40000)) {}
    CHECK(n1.done() && !n1.found() && !n1.readFailed());
    SearchJob n2(r.s, dram, 0, std::vector<uint8_t>{});
    CHECK(n2.done() && !n2.found());

    // A match is found even when it straddles a 64 KB read boundary, and the last bytes of the region count.
    for (unsigned i = 0; i < 4; i++) d[0x3000 + i] = d[0x5000 + i] = 0;
    for (unsigned i = 0; i < 4; i++) d[0x2FFFE + i] = pat[i];   // 0x30000 is a 64 KB read boundary
    for (unsigned i = 0; i < 4; i++) d[0x1FFFFC + i] = pat[i];
    SearchJob s1(r.s, dram, 0, needle);
    while (!s1.step(0x100000)) {}
    CHECK(s1.found() && s1.address() == 0x2FFFE);
    SearchJob s2(r.s, dram, 0x2FFFE, needle);
    while (!s2.step(0x100000)) {}
    CHECK(s2.found() && s2.address() == 0x1FFFFC);
    SearchJob s3(r.s, dram, 0x1FFFFC, needle);   // after the last hit: wraps back to the first
    while (!s3.step(0x100000)) {}
    CHECK(s3.found() && s3.address() == 0x2FFFE);

    // The step budget really bounds the work.
    SearchJob b1(r.s, dram, 0, std::vector<uint8_t>{0xFA, 0xCE, 0xFE, 0xED, 0x13, 0x37});
    CHECK(!b1.step(0x1000));
    CHECK(b1.progress() > 0.0f && b1.progress() < 0.1f);

    // The cartridge (read-only region) searches too.
    const vj_debug_region cart = r.s.regions()[1];
    SearchJob c1(r.s, cart, 0x800000, std::vector<uint8_t>{0xC1, 0xC2});
    while (!c1.step(0x100000)) {}
    CHECK(c1.found() && c1.address() == 0x800001);
}

static void testBlockedRegisterTable() {
    int n = 0;
    const BlockedRegister* t = blockedRegisters(&n);
    CHECK(n == 6);
    for (int i = 0; i < n; i++) CHECK(t[i].name && t[i].why && t[i].len == 4);
    CHECK(blockedRegisterAt(0xF02238, 1) && blockedRegisterAt(0xF02238, 1)->addr == 0xF02238);
    CHECK(blockedRegisterAt(0xF0223B, 1) != nullptr);
    CHECK(blockedRegisterAt(0xF0223C, 4) == nullptr);
    CHECK(blockedRegisterAt(0xF02236, 4) != nullptr);   // a long write that clips B_CMD's first bytes
    CHECK(blockedRegisterAt(0xF1A14C, 4) && std::string(blockedRegisterAt(0xF1A14C, 4)->name) == "RTXD");
}

static void testWritePolicyExamples() {
    Rig r;
    const auto& regs = r.s.regions();
    WriteDecision d;

    d = decideWrite(regs, 0x1000, 4, false);
    CHECK(d.ok);
    d = decideWrite(regs, 0xF03000, 4, false);   // GPU RAM is plain RAM
    CHECK(d.ok);
    d = decideWrite(regs, 0xF1B000, 2, false);
    CHECK(d.ok);

    d = decideWrite(regs, 0xF00010, 2, false);   // TOM register
    CHECK(!d.ok && d.err == VJ_DBG_ERR_POLICY && d.reason.find("Allow register writes") != std::string::npos);
    d = decideWrite(regs, 0xF00010, 2, true);
    CHECK(d.ok);

    d = decideWrite(regs, 0xDFFF00, 1, true);    // BUTCH, even with registers on
    CHECK(!d.ok && d.err == VJ_DBG_ERR_POLICY && d.reason.find("never writable") != std::string::npos);

    d = decideWrite(regs, 0xF02238, 4, true);    // B_CMD
    CHECK(!d.ok && d.err == VJ_DBG_ERR_POLICY && d.reason.find("B_CMD") != std::string::npos &&
          d.reason.find("blit") != std::string::npos);
    d = decideWrite(regs, 0xF02238, 4, false);   // ...and the reason names the register either way
    CHECK(!d.ok && d.reason.find("B_CMD") != std::string::npos);
    d = decideWrite(regs, 0xF02114, 2, true);    // G_CTRL
    CHECK(!d.ok && d.reason.find("G_CTRL") != std::string::npos);

    d = decideWrite(regs, 0x800000, 1, true);    // cartridge
    CHECK(!d.ok && d.err == VJ_DBG_ERR_READONLY && d.reason.find("read-only") != std::string::npos);
    d = decideWrite(regs, 0xE00010, 4, true);    // BIOS
    CHECK(!d.ok && d.err == VJ_DBG_ERR_READONLY);

    d = decideWrite(regs, 0x400000, 4, true);    // unmapped
    CHECK(!d.ok && d.err == VJ_DBG_ERR_RANGE);
    d = decideWrite(regs, 0x1000, 0, true);
    CHECK(!d.ok && d.err == VJ_DBG_ERR_RANGE);
    d = decideWrite(regs, 0xFFFFFE, 4, true);    // off the end of the bus
    CHECK(!d.ok && d.err == VJ_DBG_ERR_RANGE);

    // A 32-bit write straddling two regions is all-or-nothing.
    d = decideWrite(regs, 0x1FFFFE, 4, true);    // last 2 bytes of DRAM + 2 unmapped
    CHECK(!d.ok && d.err == VJ_DBG_ERR_RANGE);
    d = decideWrite(regs, 0xF03FFE, 4, true);    // end of GPU RAM into the unmapped hole
    CHECK(!d.ok && d.err == VJ_DBG_ERR_RANGE);
    d = decideWrite(regs, 0xF01FFE, 4, false);   // TOM LBUF (RAM) -> GPU/blitter regs: the register half refuses
    CHECK(!d.ok && d.err == VJ_DBG_ERR_POLICY);
    d = decideWrite(regs, 0xF01FFE, 4, true);
    CHECK(d.ok);
    d = decideWrite(regs, 0xF02236, 4, true);    // clips B_CMD
    CHECK(!d.ok && d.reason.find("B_CMD") != std::string::npos);

    // The CLUT is RAM to the interface but a deliberate edit here: it needs the toggle.
    d = decideWrite(regs, 0xF00400, 2, false);
    CHECK(!d.ok && d.err == VJ_DBG_ERR_POLICY && d.reason.find("palette") != std::string::npos);
    d = decideWrite(regs, 0xF00400, 2, true);
    CHECK(d.ok);
    d = decideWrite(regs, 0xF003FE, 4, false);   // TOM regs -> CLUT
    CHECK(!d.ok);
}

// The strongest check: for many addresses x lengths x flag combos, the app's decision agrees with what the
// interface (the mock, which follows vj_debug_iface.h) really does -- a write we allow lands, a write we
// refuse for a hardware reason is refused by the interface with the same code.  The one place we are stricter
// on purpose is the CLUT without the toggle (the interface calls it plain RAM).
static void testPolicyMatchesInterface() {
    Rig r;
    const auto& regs = r.s.regions();
    std::vector<unsigned> addrs;
    for (unsigned a : {0u, 0x1000u, 0x1FFFFCu, 0x1FFFFEu, 0x200000u, 0x7FFFFFu, 0x800000u, 0x80FFFEu, 0xDFFEFEu, 0xDFFEFFu,
                       0xDFFF00u, 0xDFFFFEu, 0xE00000u, 0xE1FFFEu, 0xE20000u, 0xF00000u, 0xF00010u, 0xF0001Eu, 0xF00020u,
                       0xF003FEu, 0xF00400u, 0xF005FEu, 0xF00600u, 0xF007FEu, 0xF007FFu, 0xF00800u, 0xF01FFEu, 0xF02000u,
                       0xF02110u, 0xF02112u, 0xF02114u, 0xF02116u, 0xF02118u, 0xF02234u, 0xF02236u, 0xF02238u, 0xF0223Au,
                       0xF0223Cu, 0xF02FFEu, 0xF03000u, 0xF03FFEu, 0xF04000u, 0xF0FFFEu, 0xF10000u, 0xF1A110u, 0xF1A112u,
                       0xF1A114u, 0xF1A146u, 0xF1A148u, 0xF1A14Au, 0xF1A14Cu, 0xF1A14Eu, 0xF1AFFEu, 0xF1B000u, 0xF1CFFEu,
                       0xF1D000u, 0xFFFFFEu, 0xFFFFFFu})
        addrs.push_back(a);
    const unsigned lens[] = {1, 2, 4, 8};
    int compared = 0, clutStricter = 0;
    for (unsigned a : addrs)
        for (unsigned n : lens)
            for (int allow = 0; allow < 2; allow++) {
                std::vector<uint8_t> buf(n, 0x5A);
                WriteDecision mine = decideWrite(regs, a, n, allow != 0);
                // what the interface does (never with VJ_DBG_POKE_DANGEROUS: the app never sets it)
                int before = (int)r.mock.pokesAccepted;
                int rc = r.mock.iface()->poke(a, buf.data(), n, allow ? VJ_DBG_POKE_REGS : 0u);
                bool landed = (int)r.mock.pokesAccepted > before;
                if (mine.ok) {
                    CHECK(rc == (int)n && landed);
                } else if (!allow && a < kClutEnd && a + n > kClutA && rc == (int)n) {
                    clutStricter++;   // app gate on top of the interface
                    CHECK(mine.err == VJ_DBG_ERR_POLICY);
                } else {
                    CHECK(rc == mine.err);
                }
                CHECK(mine.ok || !mine.reason.empty());
                compared++;
            }
    CHECK(compared == (int)(addrs.size() * 8));
    CHECK(clutStricter > 0);
}

static void testWriteThroughSession() {
    Rig r;
    std::string why;

    // RAM: plain write, big-endian.
    CHECK(writeCell(r.s, 0x2000, 4, 0xCAFEBABE, &why) == 4 && why.empty());
    CHECK(r.mock.dram()[0x2000] == 0xCA && r.mock.dram()[0x2003] == 0xBE);
    CHECK(writeCell(r.s, 0x2004, 2, 0x1234, &why) == 2);
    CHECK(r.mock.dram()[0x2004] == 0x12 && r.mock.dram()[0x2005] == 0x34);
    unsigned v = 0;
    CHECK(r.s.readU(0x2000, 4, v) && v == 0xCAFEBABE);

    // Registers off: refused with a reason, nothing written.
    unsigned acc = r.mock.pokesAccepted;
    CHECK(writeCell(r.s, 0xF00010, 2, 0xFFFF, &why) == VJ_DBG_ERR_POLICY && !why.empty());
    CHECK(r.mock.pokesAccepted == acc);
    r.s.allowRegisterWrites = true;
    CHECK(writeCell(r.s, 0xF00010, 2, 0xFFFF, &why) == 2);
    CHECK(r.mock.regwin()[0x10] == 0xFF);

    // Even with registers on: B_CMD, BUTCH, ROM stay refused and the mock never sees the poke.
    acc = r.mock.pokesAccepted;
    unsigned refusedAtIface = r.mock.pokesRefused;
    CHECK(writeCell(r.s, 0xF02238, 4, 1, &why) == VJ_DBG_ERR_POLICY && why.find("B_CMD") != std::string::npos);
    CHECK(writeCell(r.s, 0xDFFF00, 1, 1, &why) == VJ_DBG_ERR_POLICY);
    CHECK(writeCell(r.s, 0x800000, 1, 1, &why) == VJ_DBG_ERR_READONLY);
    CHECK(writeCell(r.s, 0x300000, 1, 1, &why) == VJ_DBG_ERR_RANGE);
    CHECK(r.mock.pokesAccepted == acc && r.mock.pokesRefused == refusedAtIface);   // refused before the interface
    CHECK(writeCell(r.s, 0x1000, 3, 0, &why) == VJ_DBG_ERR_RANGE);                 // not a cell width

    // allowDangerousWrites on the session does not open B_CMD: the app policy is stricter than the toggle.
    r.s.allowDangerousWrites = true;
    CHECK(writeCell(r.s, 0xF02238, 4, 1, &why) == VJ_DBG_ERR_POLICY);
    r.s.allowDangerousWrites = false;
    r.s.allowRegisterWrites = false;

    // Tools off: STATE.
    r.s.setHardcore(true);
    CHECK(writeCell(r.s, 0x2000, 1, 0, &why) == VJ_DBG_ERR_STATE && !why.empty());
}

static void testClutMirrorsAndPlan() {
    // planWrite mirrors CLUT bytes into the other table and coalesces runs.
    uint8_t px[2] = {0xAB, 0xCD};
    std::vector<WriteOp> ops = planWrite(clutAddrA(5), px, 2);
    CHECK(ops.size() == 2);
    CHECK(ops[0].addr == 0xF0040A && ops[0].data.size() == 2 && ops[0].data[0] == 0xAB);
    CHECK(ops[1].addr == 0xF0060A && ops[1].data[1] == 0xCD);
    ops = planWrite(clutAddrB(5), px, 2);            // writing the B table lands in A as well
    CHECK(ops.size() == 2 && ops[0].addr == 0xF0040A && ops[1].addr == 0xF0060A);
    uint8_t four[4] = {1, 2, 3, 4};
    ops = planWrite(0x2000, four, 4);                // ordinary RAM: one op
    CHECK(ops.size() == 1 && ops[0].addr == 0x2000 && ops[0].data.size() == 4);
    ops = planWrite(0xF003FE, four, 4);              // TOM regs straddling into the CLUT: runs F003FE-F00401, F00600-F00601
    CHECK(ops.size() == 2 && ops[0].addr == 0xF003FE && ops[0].data.size() == 4);
    CHECK(ops[1].addr == 0xF00600 && ops[1].data.size() == 2 && ops[1].data[0] == 3 && ops[1].data[1] == 4);
    CHECK(clutAddrA(255) == 0xF005FE && clutAddrB(255) == 0xF007FE);

    Rig r;
    // Gate: without the toggle the CLUT window cannot edit.
    std::string why;
    CHECK(writeCell(r.s, clutAddrA(7), 2, 0x1234, &why) == VJ_DBG_ERR_POLICY && why.find("palette") != std::string::npos);
    CHECK(r.mock.regwin()[0x400 + 14] != 0x12);
    // With it, BOTH tables are written (the mock does not mirror by itself, the core does).
    r.s.allowRegisterWrites = true;
    CHECK(writeCell(r.s, clutAddrA(7), 2, 0x1234, &why) == 2);
    CHECK(r.mock.regwin()[0x400 + 14] == 0x12 && r.mock.regwin()[0x400 + 15] == 0x34);
    CHECK(r.mock.regwin()[0x600 + 14] == 0x12 && r.mock.regwin()[0x600 + 15] == 0x34);
    // The memory viewer poking one CLUT byte also keeps A and B equal.
    CHECK(writeCell(r.s, 0xF00431, 1, 0x77, &why) == 1);
    CHECK(r.mock.regwin()[0x431] == 0x77 && r.mock.regwin()[0x631] == 0x77);
}

static void testColour() {
    // RGB16 (JTRM v8: bits 0-5 green, 6-10 blue, 11-15 red): hand-checked entries.
    CHECK(rgb16ToRgb888(0xF800) == 0xF80000);    // pure red
    CHECK(rgb16ToRgb888(0x07C0) == 0x0000F8);    // pure blue
    CHECK(rgb16ToRgb888(0x003F) == 0x00FC00);    // pure green
    CHECK(rgb16ToRgb888(0xFFFF) == 0xF8FCF8);
    CHECK(rgb16ToRgb888(0) == 0);
    CHECK(rgb888ToRgb16(0xFF0000) == 0xF800);
    CHECK(rgb888ToRgb16(0x0000FF) == 0x07C0);
    CHECK(rgb888ToRgb16(0x00FF00) == 0x003F);
    // Exact round trip for every one of the 65536 values.
    bool exact = true;
    for (unsigned v = 0; v < 0x10000; v++)
        if (rgb888ToRgb16(rgb16ToRgb888(v)) != v) exact = false;
    CHECK(exact);

    // CRY through the (mock) core table: decode == iface->cry_to_rgb; encode returns a value that decodes to the same colour.
    MockDebugIface mock;
    CryCodec cry;
    CHECK(!cry.ready() && cry.decode(0x1234) == 0);
    CHECK(cry.build(mock.iface()) && cry.ready());
    CHECK(cry.decode(0xABCD) == (mock.iface()->cry_to_rgb(0xABCD) & 0xFFFFFF));
    bool roundTrip = true;
    for (unsigned v = 0; v < 0x10000; v += 37) {
        uint32_t c = cry.decode(v);
        unsigned e = cry.encode(c);
        if (cry.decode(e) != c) roundTrip = false;
    }
    CHECK(roundTrip);
    // The mock table is injective, so there the value itself comes back.
    CHECK(cry.encode(cry.decode(0x4321)) == 0x4321);
    // An off-table colour maps to a nearest entry (never farther than any other).
    uint32_t odd = 0x123457;
    unsigned e = cry.encode(odd);
    CHECK(e < 0x10000);
    // A non-injective table (like the real CRY: every intensity-0 entry is black) -> lowest value wins, round trip holds.
    static const vj_debug_iface* flat = nullptr;
    static vj_debug_iface fi{};
    fi.size = sizeof fi;
    fi.version = VJ_DEBUG_IFACE_VERSION;
    fi.cry_to_rgb = [](unsigned v) -> unsigned { return (v & 0xFF) ? ((v & 0xFF) * 0x010101u) : 0u; };   // greys by intensity only
    flat = &fi;
    CryCodec fc;
    CHECK(fc.build(flat));
    CHECK(fc.decode(0xF000) == 0 && fc.decode(0x0001) == 0x010101);
    CHECK(fc.encode(0x000000) == 0);
    CHECK(fc.encode(0x808080) == 0x80);                // first entry with that colour
    CHECK(fc.decode(fc.encode(0x7F7F7F)) == 0x7F7F7F);
    CHECK(fc.build(flat));                             // idempotent, no rebuild needed
    CryCodec none;
    vj_debug_iface bare{};
    CHECK(!none.build(&bare) && !none.ready());

    // Mode switch.
    CHECK(swatchColor(ColorMode::Rgb16, 0xF800, cry) == 0xF80000);
    CHECK(swatchColor(ColorMode::Cry, 0x4321, cry) == cry.decode(0x4321));
    CHECK(encodeSwatch(ColorMode::Rgb16, 0xF80000, cry) == 0xF800);
    CHECK(encodeSwatch(ColorMode::Cry, cry.decode(0x4321), cry) == 0x4321);
}

static void testClutReadBack() {
    // The mock's CLUT: entry i = bytes (i, 255-i).  Read all 256 entries the way the window does.
    Rig r;
    std::vector<uint8_t> raw;
    CHECK(r.s.read(kClutA, raw, kClutEntries * 2));
    CHECK(raw.size() == 512);
    for (unsigned i = 0; i < kClutEntries; i++) {
        CHECK(decodeBE(&raw[i * 2], 2) == ((i << 8) | (255 - i)));
    }
    // After editing entry 3 the new value reads back through both tables.
    r.s.allowRegisterWrites = true;
    CHECK(writeCell(r.s, clutAddrA(3), 2, 0xBEEF) == 2);
    unsigned a = 0, b = 0;
    CHECK(r.s.readU(clutAddrA(3), 2, a) && r.s.readU(clutAddrB(3), 2, b) && a == 0xBEEF && b == 0xBEEF);
}

static void testCryBuildFrom() {
    CountingIface::reset();
    UiFixture f;
    CryCodec c;
    CHECK(c.buildFrom(f.s) && c.ready());
    CHECK(CountingIface::count(CountingIface::CryToRgb) == 0x10000);
    CHECK(c.decode(0xABCD) == (f.mock.iface()->cry_to_rgb(0xABCD) & 0xFFFFFF));
    CHECK(c.buildFrom(f.s));                                  // same epoch: no rebuild
    CHECK(CountingIface::count(CountingIface::CryToRgb) == 0x10000);
    f.s.views().bumpEpoch();                                  // a new core / content
    CHECK(c.buildFrom(f.s) && CountingIface::count(CountingIface::CryToRgb) == 2 * 0x10000);
    vj_debug_iface noCry = *f.mock.iface();                   // a core without cry_to_rgb
    noCry.cry_to_rgb = nullptr;
    f.s.setIface(&noCry);                                     // (epoch bump)
    CryCodec none;
    CHECK(!none.buildFrom(f.s) && !none.ready());
    const unsigned k = f.coreCalls;
    CHECK(!none.buildFrom(f.s) && f.coreCalls == k);          // the refusal is remembered for the epoch
}

static void testSearchIsOneCoreCall() {
    UiFixture f;
    const std::vector<uint8_t> needle = {0xFA, 0xCE, 0xFE, 0xED};
    for (size_t i = 0; i < needle.size(); i++) f.mock.dram()[0x180000 + i] = needle[i];
    const vj_debug_region dram = f.s.regions()[0];
    SearchJob j(f.s, dram, 0, needle);
    const unsigned k = f.coreCalls;
    CHECK(j.step(4u << 20) && j.found() && j.address() == 0x180000);
    CHECK(f.coreCalls == k + 1);          // 4 MiB of 64 KiB chunks, one wait
}

int main() {
    testParsingAndFormatting();
    testRegionsAndClamps();
    testChangeTracker();
    testSearch();
    testBlockedRegisterTable();
    testWritePolicyExamples();
    testPolicyMatchesInterface();
    testWriteThroughSession();
    testClutMirrorsAndPlan();
    testColour();
    testClutReadBack();
    testCryBuildFrom();
    testSearchIsOneCoreCall();
    std::printf("debug_memory_unit: %d checks, %d failed\n", g_checks, g_fail);
    return g_fail ? 1 : 0;
}
