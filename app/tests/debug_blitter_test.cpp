// Unit tests for the Blitter inspector and the audio scope logic (app/src/debug/blitter_log.*,
// audio_ring.*): B_CMD / flags / count decoding against docs/jtrm-blitter.md, the register table,
// and the launch-ring tracker's wrap handling against the mock core interface.  No window, no core.
#include "audio_ring.h"
#include "blitter_log.h"
#include "debug_mock.h"

#include <atomic>
#include <thread>
#include <cmath>
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

static bool has(const std::vector<std::string>& v, const char* s) {
    for (const std::string& x : v)
        if (x == s) return true;
    return false;
}

static void testCmdDecode() {
    // LFU_REPLACE | SRCEN | UPDA1 | UPDA2 -- the common sprite copy (docs/jtrm-blitter.md).
    unsigned cmd = 0x01800000u | 1u | (1u << 9) | (1u << 10);
    std::vector<std::string> n = blit::cmdFlagNames(cmd);
    CHECK(n.size() == 3 && n[0] == "SRCEN" && n[1] == "UPDA1" && n[2] == "UPDA2");
    CHECK(blit::cmdLfu(cmd) == 0xC && std::string(blit::lfuName(0xC)) == "S");
    CHECK(blit::cmdZmode(cmd) == 0);
    CHECK(blit::cmdSummary(cmd) == "SRCEN UPDA1 UPDA2 LFU=S");
    CHECK(!blit::cmdDstIsA2(cmd) && blit::cmdDstIsA2(cmd | (1u << 11)));

    // Every documented single bit decodes to a distinct name; bits 7, 18-24 are not single flags.
    size_t nbits = 0;
    const blit::CmdBit* bits = blit::cmdBits(&nbits);
    unsigned seen = 0;
    for (size_t i = 0; i < nbits; i++) {
        CHECK(!(seen & bits[i].mask));
        seen |= bits[i].mask;
        std::vector<std::string> one = blit::cmdFlagNames(bits[i].mask);
        CHECK(one.size() == 1 && one[0] == bits[i].name);
    }
    CHECK((seen & 0x01FC0000u) == 0 && (seen & (1u << 7)) == 0);
    CHECK(has(blit::cmdFlagNames(0x7FFFFFFFu), "GOURD") && has(blit::cmdFlagNames(1u << 16), "PATDSEL"));
    CHECK(has(blit::cmdFlagNames(1u << 29), "BUSHI") && has(blit::cmdFlagNames(1u << 30), "SRCSHADE"));

    // LFU nibble: the JTRM table.
    CHECK(std::string(blit::lfuName(0)) == "0" && std::string(blit::lfuName(15)) == "1");
    CHECK(std::string(blit::lfuName(0xE)) == "S|D" && std::string(blit::lfuName(0x6)) == "S^D");
    CHECK(std::string(blit::lfuName(0x8)) == "S&D" && std::string(blit::lfuName(0x3)) == "~S");
    CHECK(blit::cmdLfu(0x01C00000u) == 0xE && blit::cmdLfu(0x00C00000u) == 0x6 && blit::cmdLfu(0x00600000u) == 0x3);

    // ZMODE (bits 18-20): ORed inhibit conditions.
    CHECK(blit::zmodeName(0) == "off" && blit::zmodeName(1) == "S<D" && blit::zmodeName(3) == "S<D|S=D");
    CHECK(blit::zmodeName(7) == "S<D|S=D|S>D");
    CHECK(blit::cmdZmode(1u << 18) == 1 && blit::cmdZmode(4u << 18) == 4);
    CHECK(blit::cmdSummary((1u << 12) | (1u << 18)).find("GOURD") == 0);
    CHECK(blit::cmdSummary((1u << 18)).find("ZMODE=S<D") != std::string::npos);
}

static void testFlagsDecode() {
    // JTRM p.66/70: width 640 = 1.01 x 2^9 -> exponent 1001, mantissa 01 -> field 100101.
    CHECK(blit::windowWidth(0x25) == 640);                    // 100101b
    CHECK(blit::windowWidth(0x0C) == 8 && blit::windowWidth(0x14) == 32 && blit::windowWidth(0x1A) == 96);
    CHECK(blit::windowWidth(0x20) == 256 && blit::windowWidth(0x21) == 320 && blit::windowWidth(0x28) == 1024);
    // 16bpp (pixel field 4), width 320 (E=8 M=01: flags bits 9-14 = 100001), pitch 0, +pixel in X.
    unsigned f = (4u << 3) | (0x21u << 9) | (1u << 16);
    blit::Channel c = blit::decodeFlags(f);
    CHECK(c.bpp == 16 && c.width == 320 && c.pitch == 1 && c.xAddCtl == 1 && !c.xSub && !c.ySub);
    // pitch field: 0 -> 1, 1 -> 2, 2 -> 4, 3 -> 3 (the 2-phrase-gap special case)
    CHECK(blit::decodeFlags(0).pitch == 1 && blit::decodeFlags(1).pitch == 2 &&
          blit::decodeFlags(2).pitch == 4 && blit::decodeFlags(3).pitch == 3);
    // pixel sizes
    const unsigned bpps[] = {1, 2, 4, 8, 16, 32};
    for (unsigned i = 0; i < 6; i++) CHECK(blit::decodeFlags(i << 3).bpp == bpps[i]);
    CHECK(blit::decodeFlags(6u << 3).bpp == 0 && blit::decodeFlags(7u << 3).bpp == 0);   // reserved
    // sign bits and Y control, zoffs
    c = blit::decodeFlags((1u << 19) | (1u << 20) | (1u << 18) | (5u << 6) | (3u << 16));
    CHECK(c.xSub && c.ySub && c.yAddCtl == 1 && c.zoffs == 5 && c.xAddCtl == 3);
    CHECK(blit::describeChannel(blit::decodeFlags(f)) == "16bpp  w=320  pitch 1  x:+pixel  y:+0");
    CHECK(blit::describeChannel(blit::decodeFlags(6u << 3)).find("bpp?") == 0);
}

static void testPointersAndCount() {
    CHECK(blit::pixelX(0x00040010) == 16 && blit::pixelY(0x00040010) == 4);
    CHECK(blit::pixelX(0xFFFFFFFE) == -2 && blit::pixelY(0xFFFFFFFE) == -1);       // signed halves
    CHECK(blit::pixelX(0x8000) == -32768 && blit::pixelY(0x7FFF0000) == 32767);
    blit::Count c = blit::decodeCount(0x00040010);
    CHECK(c.inner == 16 && c.outer == 4);
    c = blit::decodeCount(0);                           // 0 means 65536 in both fields
    CHECK(c.inner == 65536 && c.outer == 65536);
    c = blit::decodeCount(0x00010000);
    CHECK(c.inner == 65536 && c.outer == 1);
    CHECK(std::string(blit::engineName(0)) == "fast" && std::string(blit::engineName(1)) == "accurate");

    vj_debug_blit b{};
    b.cmd = 0x01800001u | (1u << 11);
    b.a1_flags = 4u << 3; b.a2_flags = 3u << 3; b.count = 0x00080020;
    blit::Row r = blit::decodeRow(b);
    CHECK(r.dstA2 && r.lfu == 0xC && r.a1.bpp == 16 && r.a2.bpp == 8 && r.count.inner == 32 && r.count.outer == 8);
}

static void testRegisters() {
    size_t n = 0;
    const blit::Reg* r = blit::registers(&n);
    CHECK(n == 33);
    CHECK(r[0].addr == 0xF02200 && std::string(r[0].name) == "A1_BASE");
    CHECK(std::string(r[14].name) == "B_CMD" && r[14].addr == 0xF02238);
    CHECK(std::string(r[15].name) == "B_COUNT" && r[15].addr == 0xF0223C);
    // contiguous, 4-byte registers advance by 4, 64-bit ones by 8, and the table ends at the window end
    unsigned expect = blit::kRegBase;
    for (size_t i = 0; i < n; i++) {
        CHECK(r[i].addr == expect);
        expect += r[i].bytes;
    }
    CHECK(expect == blit::kRegBase + blit::kRegBytes);
    // decoding a window image
    std::vector<uint8_t> img(blit::kRegBytes, 0);
    auto put = [&](unsigned addr, unsigned v) {
        for (int i = 0; i < 4; i++) img[addr - blit::kRegBase + i] = (uint8_t)(v >> (24 - 8 * i));
    };
    put(0xF02200, 0x00010000);
    put(0xF0223C, 0x00040010);
    put(0xF02240, 0xDEADBEEF); put(0xF02244, 0x01234567);          // B_SRCD hi, lo
    std::vector<blit::RegValue> v = blit::decodeRegisters(img.data());
    CHECK(v.size() == n && v[0].lo == 0x00010000 && v[15].lo == 0x00040010);
    CHECK(std::string(v[16].reg->name) == "B_SRCD" && v[16].hi == 0xDEADBEEF && v[16].lo == 0x01234567);
    CHECK(blit::statusText(1) == "IDLE" && blit::statusText(0) == "BUSY" && blit::statusText(3) == "IDLE STOPPED");
}

static void testTrackerAgainstMock() {
    MockDebugIface m;                 // 5 launches pre-seeded
    blit::LogTracker t;
    CHECK(t.sync(m.iface()) == 5);
    CHECK(t.rows().size() == 5 && t.total() == 5 && t.lost() == 0);
    CHECK(t.rows().front().seq == 0 && t.rows().back().seq == 4);
    CHECK(t.sync(m.iface()) == 0);                                     // nothing new

    m.addBlit(0x01800001u, 0x00010002);
    m.addBlit(0x01800009u, 0x00010003);
    CHECK(t.sync(m.iface()) == 2 && t.rows().size() == 7 && t.rows().back().cmd == 0x01800009u);
    CHECK(t.rows().back().seq == 6 && t.lost() == 0);

    // The local list is capped (a long session): the oldest rows go.
    blit::LogTracker small(4);
    small.sync(m.iface());
    CHECK(small.rows().size() == 4 && small.rows().front().seq == 3 && small.rows().back().seq == 6);
    small.reset();
    CHECK(small.rows().empty() && small.total() == 0);
}

static void testTrackerRingWrap() {
    // The core keeps the last 256 launches.  With a shrunk ring the mock models launches falling off.
    MockDebugIface m;
    m.setBlitRingForTest(4);
    blit::LogTracker t;
    // 5 launches exist, the ring holds the last 4: launch 0 is already gone when we first look.
    t.sync(m.iface());
    CHECK(t.rows().size() == 4 && t.rows().front().seq == 1 && t.lost() == 1 && t.total() == 5);

    // Now 6 more launches before the next look: 4 survive, 2 are lost; no duplicates, no gaps in the survivors.
    for (unsigned i = 0; i < 6; i++) m.addBlit(0x100 + i, 1);
    size_t added = t.sync(m.iface());
    CHECK(added == 4 && t.lost() == 3 && t.total() == 11);
    CHECK(t.rows().size() == 8);
    unsigned prev = 0;
    bool first = true, mono = true;
    for (const vj_debug_blit& b : t.rows()) {
        if (!first && b.seq <= prev) mono = false;
        prev = b.seq;
        first = false;
    }
    CHECK(mono && t.rows().back().seq == 10 && t.rows().back().cmd == 0x105);
    CHECK(t.rows()[4].seq == 7);                                       // the first of the surviving four

    // The default ring is 256: with 300 launches only the last 256 are readable.
    MockDebugIface big;
    for (unsigned i = 0; i < 295; i++) big.addBlit(i, 1);              // 5 seeded + 295 = 300
    blit::LogTracker bt(1024);
    bt.sync(big.iface());
    CHECK(bt.rows().size() == VJ_DBG_BLIT_LOG_SIZE && bt.lost() == 300 - VJ_DBG_BLIT_LOG_SIZE);
    CHECK(bt.rows().front().seq == 44 && bt.rows().back().seq == 299);
}

static unsigned g_fakeHead = 0;
static unsigned fakeHead() { return g_fakeHead; }
static int fakeGet(unsigned i, vj_debug_blit* o) {
    if (i >= g_fakeHead) return 0;
    *o = vj_debug_blit();
    o->seq = i;
    o->cmd = 0xA000 + i;
    return 1;
}

static void testTrackerRestart() {
    // blit_log_enable(0) then (1) restarts the numbering: the head falls below what was consumed.
    vj_debug_iface f{};
    f.size = sizeof f;
    f.blit_log_head = &fakeHead;
    f.blit_log_get = &fakeGet;
    blit::LogTracker t;
    g_fakeHead = 10;
    CHECK(t.sync(&f) == 10 && t.total() == 10);
    g_fakeHead = 3;                                  // restarted, 3 launches so far
    CHECK(t.sync(&f) == 3 && t.total() == 3 && t.rows().size() == 3 && t.rows().front().cmd == 0xA000);
    CHECK(t.lost() == 0);
    g_fakeHead = 0;
    CHECK(t.sync(&f) == 0 && t.rows().empty());

    // null / incomplete interfaces
    CHECK(t.sync(nullptr) == 0);
    vj_debug_iface empty{};
    CHECK(t.sync(&empty) == 0);

    // enabling goes through the interface (the window turns the core log on only while it is open)
    MockDebugIface c;
    c.iface()->blit_log_enable(1);
    CHECK(c.blitLogOn);
    c.iface()->blit_log_enable(0);
    CHECK(!c.blitLogOn);
}

static void testAudioRing() {
    AudioRing r(8);
    CHECK(r.capacity() == 8 && r.available() == 0 && r.totalFrames() == 0);
    std::vector<int16_t> l, rr;
    CHECK(r.latest(4, l, rr) == 0 && l.empty());
    int16_t a[] = {1, -1, 2, -2, 3, -3};                       // 3 frames
    r.push(a, 3);
    CHECK(r.available() == 3 && r.totalFrames() == 3);
    CHECK(r.latest(10, l, rr) == 3 && l[0] == 1 && l[2] == 3 && rr[0] == -1 && rr[2] == -3);
    CHECK(r.latest(2, l, rr) == 2 && l[0] == 2 && l[1] == 3);   // newest two, oldest first

    // wrap: 7 more frames -> 10 pushed, only the newest 8 kept
    int16_t b[14];
    for (int i = 0; i < 7; i++) { b[i * 2] = (int16_t)(10 + i); b[i * 2 + 1] = (int16_t)(-(10 + i)); }
    r.push(b, 7);
    CHECK(r.available() == 8 && r.totalFrames() == 10);
    CHECK(r.latest(8, l, rr) == 8);
    CHECK(l[0] == 3 && l[1] == 10 && l[7] == 16 && rr[0] == -3 && rr[7] == -16);

    // a block larger than the ring: the tail wins
    int16_t big[40];
    for (int i = 0; i < 20; i++) { big[i * 2] = (int16_t)(100 + i); big[i * 2 + 1] = (int16_t)(200 + i); }
    r.push(big, 20);
    CHECK(r.available() == 8 && r.totalFrames() == 30);
    CHECK(r.latest(8, l, rr) == 8 && l[0] == 112 && l[7] == 119 && rr[0] == 212 && rr[7] == 219);
    r.push(nullptr, 5);
    r.push(big, 0);
    CHECK(r.totalFrames() == 30);

    // peaks
    AudioRing p(16);
    int16_t s[] = {100, -50, -3000, 20, 40, 32767, 5, -32768};
    p.push(s, 4);
    AudioRing::Peak pk = p.peak(4);
    CHECK(pk.left == 3000 && pk.right == 32768);
    CHECK(p.peak(1).left == 5 && p.peak(1).right == 32768);          // the newest frame only
    CHECK(p.peak(2).left == 40 && p.peak(2).right == 32768);
    CHECK(p.peak(100).left == 3000);
    CHECK(AudioRing(4).peak(4).left == 0);
    p.clear();
    CHECK(p.available() == 0 && p.peak(4).left == 0);

    CHECK(peakLevel(0) == 0.0f && peakLevel(32768) == 1.0f && std::fabs(peakLevel(16384) - 0.5f) < 1e-6f);
    CHECK(std::fabs(peakDb(32768)) < 1e-4f && std::fabs(peakDb(16384) - (-6.0206f)) < 1e-3f);
    CHECK(peakDb(0) == -96.0f && std::fabs(peakDb(1) - (-90.309f)) < 1e-2f && peakDb(1, -80.0f) == -80.0f);
}

// The UI may inspect/clear the capture while the emulation thread delivers stereo blocks.
// Removing capture synchronization must fail under TSan (and can corrupt the paired channels).
static void testAudioRingConcurrent() {
    AudioRing ring(128);
    std::atomic<bool> start{false}, done{false};
    std::thread producer([&] {
        while (!start.load()) std::this_thread::yield();
        for (int i = 1; i <= 20000; ++i) {
            const int16_t samples[] = {(int16_t)i, (int16_t)-i};
            ring.push(samples, 1);
        }
        done = true;
    });
    start = true;
    bool paired = true, bounded = true;
    std::vector<int16_t> left, right;
    do {
        ring.latest(128, left, right);
        bounded = bounded && left.size() <= 128 && ring.available() <= 128;
        for (size_t i = 0; i < left.size(); ++i)
            paired = paired && left[i] == -right[i];
        ring.peak(128);
        ring.totalFrames();
        ring.clear();
    } while (!done.load());
    producer.join();
    CHECK(paired && bounded);
    ring.clear();
    const int16_t final[] = {123, -456};
    ring.push(final, 1);
    CHECK(ring.latest(1, left, right) == 1 && left[0] == 123 && right[0] == -456);
    CHECK(ring.totalFrames() == 1);
}

static void testPeakMeter() {
    PeakMeter m;
    m.update(16384, 0.016f);                       // up at once
    CHECK(std::fabs(m.level - 0.5f) < 1e-5f && std::fabs(m.hold - 0.5f) < 1e-5f && !m.clipped);
    m.update(0, 0.1f);                             // falls at 1.5/s
    CHECK(m.level < 0.5f && m.level > 0.3f && std::fabs(m.hold - 0.5f) < 1e-5f);
    for (int i = 0; i < 100; i++) m.update(0, 0.1f);   // 10 s of silence: down to 0, hold released
    CHECK(m.level == 0.0f && m.hold == 0.0f);
    m.update(32767, 0.016f);
    CHECK(m.clipped);
    m.reset();
    CHECK(!m.clipped && m.level == 0.0f);
}

int main() {
    testCmdDecode();
    testFlagsDecode();
    testPointersAndCount();
    testRegisters();
    testTrackerAgainstMock();
    testTrackerRingWrap();
    testTrackerRestart();
    testAudioRing();
    testAudioRingConcurrent();
    testPeakMeter();
    std::printf("debug_blitter_test: %d checks, %d failed\n", g_checks, g_fail);
    return g_fail ? 1 : 0;
}
