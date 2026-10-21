// DebugViews (src/debug/debug_views.h): the request table and per-request double buffers the emulation thread fills
// and the tool windows read (#967 phase 3a).  Mock core, no window.
//
//   vj_debug_views_test
#include "debug_mock.h"
#include "debug_session.h"   // DebugSession::kDramSize
#include "debug_views.h"

#include <atomic>
#include <chrono>
#include <cstdio>
#include <functional>
#include <thread>

using namespace vj;
using Id = DebugViews::Id;

static int g_fail = 0;
#define CHECK(c, ...)                                        \
    do {                                                     \
        if (!(c)) {                                          \
            std::printf("FAIL %s:%d: ", __FILE__, __LINE__); \
            std::printf(__VA_ARGS__);                        \
            std::printf("\n");                               \
            g_fail++;                                        \
        }                                                    \
    } while (0)

static ViewRequest mem(unsigned addr, unsigned len) {
    ViewRequest r;
    r.kind = ViewRequest::Kind::Memory;
    r.addr = addr;
    r.len = len;
    return r;
}

// An iface whose peek() runs a hook: lets a test act "during the fill", with no lock held.
struct HookIface {
    static inline std::function<void()> onPeek;
    static int peek(unsigned, unsigned char* d, unsigned n) {
        if (onPeek) onPeek();
        for (unsigned i = 0; i < n; i++) d[i] = 0xAB;
        return (int)n;
    }
    static unsigned frame() { return 7; }
    static vj_debug_iface make() {
        vj_debug_iface i{};
        i.size = sizeof i;
        i.version = VJ_DEBUG_IFACE_VERSION;
        i.peek = &HookIface::peek;
        i.frame_number = &HookIface::frame;
        return i;
    }
};

static void testSetClampsAndIds() {
    DebugViews v;
    const Id a = v.set(DebugViews::kNoId, mem(0x1000, 1u << 20)), b = v.set(DebugViews::kNoId, mem(0x2000, 16));
    CHECK(a != DebugViews::kNoId && b != DebugViews::kNoId && a != b && v.count() == 2, "two requests, two ids");
    CHECK(v.set(a, mem(0x1000, 1u << 20)) == a && v.count() == 2, "an existing id is replaced, not added");
    ViewRequest d;
    d.kind = ViewRequest::Kind::Disasm;
    d.target = VJ_DBG_TARGET_GPU;
    d.addr = 0xF03000;
    d.rows = 9999;
    const Id c = v.set(DebugViews::kNoId, d);
    MockDebugIface m;
    v.fill(m.iface(), 1);
    ViewBuffer out;
    CHECK(v.read(a, out) && out.bytes.size() == DebugViews::kMaxMemoryBytes, "len is clamped to 64 KiB (%zu)", out.bytes.size());
    CHECK(v.read(c, out) && out.lines.size() == DebugViews::kMaxDisasmRows, "rows are clamped to 512 (%zu)", out.lines.size());
    v.clear(b);
    v.clear(12345);   // unknown: no-op
    CHECK(v.count() == 2 && !v.read(b, out), "clear() removes one request; an unknown id is a no-op");
}

static void testFillKinds() {
    MockDebugIface m;   // DRAM byte i = i & 0xFF, frame_number 1234, 4 OP objects at $1000, 5 blits
    DebugViews v;
    const Id mm = v.set(DebugViews::kNoId, mem(0x1000, 16));
    ViewRequest dr;
    dr.kind = ViewRequest::Kind::Dram;
    const Id dram = v.set(DebugViews::kNoId, dr);
    ViewRequest rg;
    rg.kind = ViewRequest::Kind::Regs;
    rg.target = VJ_DBG_TARGET_GPU;
    const Id regs = v.set(DebugViews::kNoId, rg);
    ViewRequest di;
    di.kind = ViewRequest::Kind::Disasm;
    di.target = VJ_DBG_TARGET_GPU;
    di.addr = 0xF03006;   // the mock says a 6-byte "movei" when addr % 8 == 6
    di.rows = 3;
    const Id dis = v.set(DebugViews::kNoId, di);
    di.target = VJ_DBG_TARGET_M68K;
    const Id dis68 = v.set(DebugViews::kNoId, di);
    ViewRequest op;
    op.kind = ViewRequest::Kind::OpSnapshot;
    const Id ops = v.set(DebugViews::kNoId, op);
    ViewRequest bl;
    bl.kind = ViewRequest::Kind::BlitLog;
    const Id blit = v.set(DebugViews::kNoId, bl);
    CHECK(v.fill(m.iface(), 9) == 7, "all seven are filled");
    ViewBuffer b;
    CHECK(v.read(mm, b) && b.ok && b.serial == 9 && b.frame == 1234 && b.bytes.size() == 16 && b.bytes[0] == 0x00 &&
              b.bytes[15] == 0x0F, "Memory: the bus bytes at the frame the fill ran");
    CHECK(v.read(dram, b) && b.ok && b.bytes.size() == DebugSession::kDramSize && b.bytes[0x1234] == 0x34, "Dram: 2 MiB from 0");
    CHECK(v.read(regs, b) && b.ok && b.regs.risc_pc == 0xF03000, "Regs: GPU pc");
    CHECK(v.read(dis, b) && b.ok && b.lines.size() == 3 && b.lines[0].addr == 0xF03006 && b.lines[0].len == 6 &&
              b.lines[1].addr == 0xF0300C, "Disasm: a 6-byte MOVEI advances by 6");
    CHECK(v.read(dis68, b) && !b.ok && b.lines.empty(), "Disasm of the 68000 needs the injected DisasmFn (A5 installs it)");
    v.setDisasm([](const vj_debug_iface*, int t, unsigned a, unsigned n) {
        std::vector<DisasmLine> out;
        for (unsigned i = 0; i < n; i++) out.push_back({a + 2 * i, 2, t == VJ_DBG_TARGET_M68K ? "nop" : "fake", "4E71"});
        return out;
    });
    v.set(dis68, di);   // unchanged request: the buffer stays; a new serial refills it through the fn
    v.fill(m.iface(), 10);
    CHECK(v.read(dis68, b) && b.ok && b.lines.size() == 3 && b.lines[0].text == "nop" && b.lines[0].bytes == "4E71",
          "with a DisasmFn every target is served by it");
    v.setDisasm(nullptr);
    m.opReady = false;
    v.fill(m.iface(), 11);
    CHECK(v.read(ops, b) && b.ok && b.opCount == -1, "OpSnapshot: armed, not taken yet is -1");
    m.opReady = true;
    m.iface()->op_snapshot_arm();
    v.fill(m.iface(), 12);
    CHECK(v.read(ops, b) && b.opCount == 4 && b.objs.size() == 4 && b.objs[0].address == MockDebugIface::kOpListBase &&
              b.halfline == 41, "OpSnapshot: four objects, halfline 41");
    CHECK(v.read(blit, b) && b.ok && b.blitHead == 5 && b.blits.size() == 5 && b.blitLost == 0, "BlitLog since 0: the five launches");
    m.setBlitRingForTest(4);   // launch 0 has scrolled out
    v.fill(m.iface(), 13);
    CHECK(v.read(blit, b) && b.blits.size() == 4 && b.blits.front().seq == 1 && b.blitLost == 1, "BlitLog: lost launches are counted, never replayed");
    bl.since = 9;   // past the head: the log was switched off and on again
    v.set(blit, bl);
    v.fill(m.iface(), 13);
    CHECK(v.read(blit, b) && b.blits.empty() && b.blitHead == 5 && b.blitLost == 0, "BlitLog: since > head copies nothing and reports the head");
}

static void testSerialAndPending() {
    MockDebugIface m;
    DebugViews v;
    const Id a = v.set(DebugViews::kNoId, mem(0, 4));
    CHECK(v.pending(1), "a new request is pending");
    CHECK(v.fill(m.iface(), 1) == 1 && !v.pending(1), "filled at serial 1");
    CHECK(v.fill(m.iface(), 1) == 0, "fill() runs once per serial");
    CHECK(v.pending(2) && v.fill(m.iface(), 2) == 1, "a new serial refills");
    ViewBuffer b;
    CHECK(v.read(a, b) && b.serial == 2, "the buffer carries the serial it was filled under");
    CHECK(v.fill(nullptr, 3) == 0 && v.pending(3), "a null iface fills nothing and stays pending");
}

static void testSetInvalidates() {
    MockDebugIface m;
    DebugViews v;
    const Id a = v.set(DebugViews::kNoId, mem(0x10, 4));
    v.fill(m.iface(), 1);
    ViewBuffer b;
    v.set(a, mem(0x10, 4));
    CHECK(v.read(a, b) && !v.pending(1), "an unchanged set() keeps the filled buffer (windows re-state their request every frame)");
    v.set(a, mem(0x20, 4));
    CHECK(!v.read(a, b) && v.pending(1), "a changed set() clears the buffer and is pending at the SAME serial (a paused window that scrolls)");
    v.fill(m.iface(), 1);
    CHECK(v.read(a, b) && b.bytes[0] == 0x20, "and the refill carries the new range");
}

static void testFillHoldsNoLock() {
    DebugViews v;
    const vj_debug_iface hi = HookIface::make();
    const Id a = v.set(DebugViews::kNoId, mem(0, 4));
    HookIface::onPeek = [&] { v.set(a, mem(0x100, 4)); };   // would deadlock if the lock were held across the iface call
    v.fill(&hi, 1);
    HookIface::onPeek = nullptr;
    ViewBuffer b;
    CHECK(!v.read(a, b) && v.pending(1), "a set() made during the fill is not overwritten by the stale result");
    v.fill(&hi, 1);
    CHECK(v.read(a, b) && b.ok, "the next fill serves the new request");
}

static void testEpoch() {
    MockDebugIface m;
    DebugViews v;
    const Id a = v.set(DebugViews::kNoId, mem(0, 4));
    v.fill(m.iface(), 1);
    ViewBuffer b;
    CHECK(v.read(a, b), "readable after the fill");
    const uint64_t e0 = v.epoch();
    v.bumpEpoch();
    CHECK(v.epoch() == e0 + 1 && !v.read(a, b), "a buffer filled before bumpEpoch() is not readable after it");
    CHECK(v.pending(1) && v.fill(m.iface(), 1) == 1 && v.read(a, b) && b.epoch == v.epoch(),
          "fill() at the same serial refills a stale-epoch buffer");
    const vj_debug_iface hi = HookIface::make();
    DebugViews w;
    const Id c = w.set(DebugViews::kNoId, mem(0, 4));
    HookIface::onPeek = [&] { w.bumpEpoch(); };
    w.fill(&hi, 1);
    HookIface::onPeek = nullptr;
    CHECK(!w.read(c, b), "an epoch bump between the table copy and the swap leaves the buffer unreadable");
}

// set() and read() on one thread, fill() on another: every buffer matches the request of its own id (never torn).
static void testConcurrent() {
    MockDebugIface m;
    DebugViews v;
    const Id a = v.set(DebugViews::kNoId, mem(0x100, 64)), b = v.set(DebugViews::kNoId, mem(0x400, 64));
    const Id churn = v.set(DebugViews::kNoId, mem(0, 8));
    std::atomic<bool> stop{false};
    std::thread filler([&] {
        for (uint64_t s = 1; !stop.load(); s++) v.fill(m.iface(), s);
    });
    std::thread churner([&] {
        for (unsigned i = 0; !stop.load(); i++) v.set(churn, mem(0x1000 + (i & 7) * 16, 8 + (i & 3)));
    });
    int reads = 0, torn = 0;
    const auto end = std::chrono::steady_clock::now() + std::chrono::milliseconds(300);
    while (std::chrono::steady_clock::now() < end) {
        ViewBuffer x;
        if (v.read(a, x)) {
            reads++;
            if (x.bytes.size() != 64 || x.bytes[0] != 0x00 || x.bytes[63] != 0x3F) torn++;   // DRAM i & 0xFF: $100.. -> 00..3F
        }
        if (v.read(b, x)) {
            reads++;
            if (x.bytes.size() != 64 || x.bytes[0] != 0x00 || x.bytes[63] != 0x3F) torn++;
        }
    }
    stop = true;
    filler.join();
    churner.join();
    CHECK(reads > 0 && torn == 0, "reads are never torn (%d reads, %d torn)", reads, torn);
}

int main() {
    testSetClampsAndIds();
    testFillKinds();
    testSerialAndPending();
    testSetInvalidates();
    testFillHoldsNoLock();
    testEpoch();
    testConcurrent();
    std::printf(g_fail ? "debug_views_test: %d FAILED\n" : "debug_views_test: OK\n", g_fail);
    return g_fail ? 1 : 0;
}
