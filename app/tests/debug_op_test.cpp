// Unit tests for the Object list tool's logic (app/src/debug/op_decode, op_render, op_view): the
// field decoder against JTRM-layout fixtures, the bitmap renderer and pixel editor against the mock
// core interface (debug_mock.h), the snapshot lifecycle, the screen mapping and PNG export.  No
// window, no GL, no core.
#include "debug_mock.h"
#include "debug_session.h"
#include "debug_ui_fixture.h"
#include "op_decode.h"
#include "op_render.h"
#include "op_view.h"

#include <SDL3/SDL.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <functional>
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
#define CHECK_EQ(a, b)                                                                                           \
    do {                                                                                                         \
        g_checks++;                                                                                              \
        unsigned long long va_ = (unsigned long long)(a), vb_ = (unsigned long long)(b);                         \
        if (va_ != vb_) {                                                                                        \
            g_fail++;                                                                                            \
            std::fprintf(stderr, "FAIL %s:%d  %s == %s  (%llx vs %llx)\n", __FILE__, __LINE__, #a, #b, va_, vb_); \
        }                                                                                                        \
    } while (0)

// ---- an independent encoder: straight from the JTRM Rev 8 tables (pp.18-20) -----------------------------------
struct Bm {
    unsigned type = 0, ypos = 0, height = 1, link = 0, data = 0;
    int xpos = 0;
    unsigned depth = 3, pitch = 1, dwidth = 1, iwidth = 1, index = 0, firstpix = 0;
    bool reflect = false, rmw = false, trans = false, release = false;
    unsigned hscale = 0x20, vscale = 0x20, remainder = 0;
    uint64_t p0() const {
        return (uint64_t)type | ((uint64_t)ypos << 3) | ((uint64_t)height << 14) | ((uint64_t)(link >> 3) << 24) |
               ((uint64_t)(data >> 3) << 43);
    }
    uint64_t p1() const {
        return (uint64_t)((unsigned)xpos & 0xFFF) | ((uint64_t)depth << 12) | ((uint64_t)pitch << 15) |
               ((uint64_t)dwidth << 18) | ((uint64_t)iwidth << 28) | ((uint64_t)index << 38) | ((uint64_t)reflect << 45) |
               ((uint64_t)rmw << 46) | ((uint64_t)trans << 47) | ((uint64_t)release << 48) | ((uint64_t)firstpix << 49);
    }
    uint64_t p2() const { return (uint64_t)hscale | ((uint64_t)vscale << 8) | ((uint64_t)remainder << 16); }
    OpObject decode(unsigned addr = 0x1000) const { return opDecode(addr, type == 1 ? 3 : 2, p0(), p1(), p2()); }
};

// ---- decoder ---------------------------------------------------------------------------------------------------------------
static void testDecodeFields() {
    Bm b;
    b.ypos = 0x2A5;
    b.height = 0x155;
    b.link = 0x1ABCD8;
    b.data = 0xA5A5A8;
    b.xpos = -8;
    b.depth = 4;
    b.pitch = 3;
    b.dwidth = 0x155;
    b.iwidth = 0x2AA;
    b.index = 0x55;
    b.firstpix = 0x2A;
    b.reflect = true;
    b.trans = true;
    b.release = true;
    OpObject o = b.decode();
    CHECK(o.type == OpType::Bitmap);
    CHECK_EQ(o.ypos, 0x2A5);
    CHECK_EQ(o.height, 0x155);
    CHECK_EQ(o.link, 0x1ABCD8);
    CHECK_EQ(o.data, 0xA5A5A8);
    CHECK_EQ(o.xpos, -8);
    CHECK_EQ(o.depthCode, 4);
    CHECK_EQ(o.bpp, 16);
    CHECK_EQ(o.pitch, 3);
    CHECK_EQ(o.dwidth, 0x155);
    CHECK_EQ(o.iwidth, 0x2AA);
    CHECK_EQ(o.index, 0x55);
    CHECK_EQ(o.firstpix, 0x2A);
    CHECK(o.reflect && !o.rmw && o.trans && o.release);
    CHECK_EQ(o.pixelsPerPhrase, 4);
    CHECK_EQ(o.skipPixels, 2);                  // 0x2A & 0x30 = 0x20 bits = 2 pixels of 16
    CHECK_EQ(o.widthPixels, 0x2AA * 4 - 2);
    CHECK(o.drawable());
    CHECK(o.warning.empty());

    // XPOS extremes (12 bit signed): -2048, +2047.
    b.xpos = -2048;
    CHECK_EQ(b.decode().xpos, -2048);
    b.xpos = 2047;
    CHECK_EQ(b.decode().xpos, 2047);
}

static void testDecodeDepths() {
    // pixels per phrase, FIRSTPIX mask (op.c) and the INDEX -> CLUT base rule, per depth.
    struct { unsigned depth, bpp, ppp, fp, skip, base; } t[] = {
        {0, 1, 64, 0x3F, 0x3E, 0x6A},       // INDEX 0x35 -> (0x35 << 1) = 0x6A; FIRSTPIX 0x3F & 0x3E = 62 px
        {1, 2, 32, 0x3F, 0x3C / 2, 0x68},   // & 0xFC; 0x3C bits = 30 px
        {2, 4, 16, 0x3F, 0x38 / 4, 0x60},   // & 0xF0; 0x38 bits = 14 px
        {3, 8, 8, 0x3F, 0x30 / 8, 0},       // 8 bpp: no INDEX; 0x30 bits = 6 px
        {4, 16, 4, 0x3F, 0x30 / 16, 0},     // 3 px
        {5, 24, 2, 0x3F, 1, 0},             // 24 bpp pixels are 32 bits: 0x20 bits = 1 px
    };
    for (auto& c : t) {
        Bm b;
        b.depth = c.depth;
        b.index = 0x35;
        b.firstpix = c.fp;
        b.iwidth = 10;
        OpObject o = b.decode();
        CHECK_EQ(o.bpp, c.bpp);
        CHECK_EQ(o.pixelsPerPhrase, c.ppp);
        CHECK_EQ(o.clutBase, c.base);
        CHECK_EQ(o.skipPixels, c.skip);
    }
    Bm b;
    b.depth = 6;
    OpObject o = b.decode();
    CHECK_EQ(o.bpp, 0);
    CHECK(!o.drawable());
    CHECK(o.warning.find("DEPTH") != std::string::npos);
    CHECK(std::string(opDepthName(6)).find("invalid") != std::string::npos);
    CHECK(std::string(opDepthName(5)) == "24 bpp");
}

static void testDecodeOtherTypes() {
    // Val d'Isere Skiing's GPU object (docs/jtrm-object-processor.md): TYPE 2, YPOS 285, DATA 0.
    OpObject g = opDecode(0x1000, 1, 0x00000000000008EAull, 0, 0);
    CHECK(g.type == OpType::Gpu);
    CHECK_EQ(g.ypos, 285);
    CHECK_EQ(g.gpuData, 0);
    // Doom's is $CD2, Atari Karts' $2.
    CHECK(opDecode(0, 1, 0xCD2, 0, 0).type == OpType::Gpu);
    CHECK_EQ(opDecode(0, 1, 0xCD2, 0, 0).ypos, 0x19A);
    // GPU DATA is bits 14-63.
    CHECK_EQ(opDecode(0, 1, (0x123456789ABCull << 14) | 2, 0, 0).gpuData, 0x123456789ABCull);

    // Branch: CC is three bits (14-16), LINK 24-42; CC 4 = second half line.
    uint64_t br = 3ull | (400ull << 3) | (4ull << 14) | ((uint64_t)(0x14BA8u >> 3) << 24);
    OpObject b = opDecode(0x2000, 1, br, 0, 0);
    CHECK(b.type == OpType::Branch);
    CHECK_EQ(b.cc, 4);
    CHECK_EQ(b.ypos, 400);
    CHECK_EQ(b.link, 0x14BA8);
    CHECK(std::string(opBranchCondName(4)).find("second half") != std::string::npos);
    CHECK(std::string(opBranchCondName(0)).find("$7FF") != std::string::npos);
    CHECK(!opDecode(0, 1, 3ull | (5ull << 14), 0, 0).warning.empty());   // CC 5-7 undefined

    // Stop: the emulator interrupts only with bit 3 set.
    OpObject s = opDecode(0x3000, 1, 4, 0, 0);
    CHECK(s.type == OpType::Stop && !s.stopInterrupt && s.terminatesList());
    CHECK(opDecode(0x3000, 1, 4 | 8, 0, 0).stopInterrupt);
    CHECK_EQ(opDecode(0x3000, 1, 4 | (0x1234ull << 3), 0, 0).stopData, 0x1234);

    // Undefined types.
    for (unsigned t = 5; t < 8; t++) {
        OpObject u = opDecode(0, 1, t, 0, 0);
        CHECK(u.type == OpType::Unknown && !u.warning.empty());
    }
}

static void testDecodeScaled() {
    // Atari Karts' scaled object, quoted in src/tom/op.c (OPProcessList): second phrase 00010000 7000B00D,
    // "[7 (0) x 1 @ (13, 0) (8 bpp), l: 000E82A0, p: 000E0FC0 fp: 00, fl:RELEASE, idx:00, pt:01] [hsc: 9A, vsc: 00, rem: 00]".
    Bm b;
    b.type = 1;
    b.ypos = 0;
    b.height = 1;
    b.link = 0xE82A0;
    b.data = 0xE0FC0;
    OpObject o = opDecode(0xE8268 & ~0x1F, 3, b.p0(), 0x000100007000B00Dull, 0x9A);
    CHECK(o.type == OpType::Scaled);
    CHECK_EQ(o.xpos, 13);
    CHECK_EQ(o.bpp, 8);
    CHECK_EQ(o.iwidth, 7);
    CHECK_EQ(o.dwidth, 0);
    CHECK_EQ(o.pitch, 1);
    CHECK_EQ(o.index, 0);
    CHECK_EQ(o.firstpix, 0);
    CHECK(o.release && !o.trans && !o.reflect);
    CHECK_EQ(o.link, 0xE82A0);
    CHECK_EQ(o.data, 0xE0FC0);
    CHECK_EQ(o.hscale, 0x9A);
    CHECK_EQ(o.vscale, 0);
    CHECK_EQ(o.remainder, 0);
    CHECK_EQ(o.widthPixels, 56);
    CHECK_EQ(o.scaledWidth, (56u * 0x9A) >> 5);

    // Val d'Isere's snow plane (op_list_dump.c / docs): IWIDTH 40 phrases at 8 bpp, HSCALE $10 -> 160 px.
    Bm v;
    v.type = 1;
    v.iwidth = 40;
    v.hscale = 0x10;
    v.vscale = 0x10;
    v.remainder = 0x14;
    OpObject s = v.decode();
    CHECK_EQ(s.widthPixels, 320);
    CHECK_EQ(s.scaledWidth, 160);
    CHECK_EQ(s.remainder, 0x14);

    // Missing phrases are reported, not guessed.
    OpObject m = opDecode(0, 1, v.p0(), 0, 0);
    CHECK(!m.havePhrase1 && !m.drawable() && !m.warning.empty());
    OpObject m2 = opDecode(0, 2, v.p0(), v.p1(), 0);
    CHECK(m2.havePhrase1 && !m2.havePhrase2 && m2.warning.find("scaling") != std::string::npos);
}

static void testDecodeEdgeCases() {
    Bm b;
    b.height = 0;
    OpObject o = b.decode();
    CHECK(!o.drawable());
    CHECK(o.warning.find("HEIGHT") != std::string::npos);
    Bm z;
    z.iwidth = 0;   // illegal; op.c draws one phrase
    OpObject w = z.decode();
    CHECK_EQ(w.iwidth, 0);
    CHECK_EQ(w.iwidthEff, 1);
    CHECK_EQ(w.widthPixels, 8);
    CHECK(w.warning.find("IWIDTH") != std::string::npos);
    CHECK(opSummary(w).find("@001000") != std::string::npos);
}

// Cross-check every field against the formulas of test/tools/op_list_dump.c, over pseudo-random phrases.
static void testDecodeMatchesOpListDump() {
    uint64_t x = 0x9E3779B97F4A7C15ull;
    auto rnd = [&]() {
        x ^= x << 13;
        x ^= x >> 7;
        x ^= x << 17;
        return x;
    };
    for (int i = 0; i < 2000; i++) {
        uint64_t p0 = rnd(), p1 = rnd(), p2 = rnd();
        p0 = (p0 & ~7ull) | (rnd() % 2);   // bitmap or scaled
        OpObject o = opDecode(0x4000, 3, p0, p1, p2);
        CHECK_EQ(o.ypos, (unsigned)((p0 >> 3) & 0x7FF));
        CHECK_EQ(o.height, (unsigned)((p0 >> 14) & 0x3FF));
        CHECK_EQ(o.xpos, (int)(((int16_t)((p1 << 4) & 0xFFFF)) >> 4));
        CHECK_EQ(o.iwidth, (unsigned)((p1 >> 28) & 0x3FF));
        CHECK_EQ(o.dwidth, (unsigned)((p1 >> 18) & 0x3FF));
        CHECK_EQ(o.depthCode, (unsigned)((p1 >> 12) & 0x07));
        CHECK_EQ(o.pitch, (unsigned)((p1 >> 15) & 0x07));
        if (o.type == OpType::Scaled) {
            CHECK_EQ(o.hscale, (unsigned)(p2 & 0xFF));
            CHECK_EQ(o.vscale, (unsigned)((p2 >> 8) & 0xFF));
            CHECK_EQ(o.remainder, (unsigned)((p2 >> 16) & 0xFF));
        } else {
            CHECK(!o.havePhrase2);
        }
        CHECK_EQ(o.data, (unsigned)((p0 >> 40) & 0xFFFFF8));
        CHECK_EQ(o.link, (unsigned)((p0 & 0x000007FFFF000000ULL) >> 21));
        // op.c: index = (p1 >> 37) & 0xFE, flags = (p1 >> 45) & 7, firstPix = (p1 >> 49) & 0x3F
        CHECK_EQ((o.index << 1) & 0xFE, (unsigned)((p1 >> 37) & 0xFE));
        CHECK_EQ(o.reflect | (o.rmw << 1) | (o.trans << 2), (unsigned)((p1 >> 45) & 7));
        CHECK_EQ(o.firstpix, (unsigned)((p1 >> 49) & 0x3F));
    }
    // The mock's own snapshot decodes consistently (same bit layout as docs/jtrm-object-processor.md).
    MockDebugIface m;
    vj_debug_op_obj raw[8];
    unsigned hl = 0;
    int n = m.iface()->op_snapshot_get(raw, 8, &hl);
    CHECK_EQ(n, 4);
    std::vector<OpObject> v = opDecodeAll(raw, n);
    CHECK(v[0].type == OpType::Bitmap && v[1].type == OpType::Scaled && v[2].type == OpType::Branch && v[3].type == OpType::Stop);
    CHECK_EQ(v[0].ypos, 100);
    CHECK_EQ(v[0].height, MockDebugIface::kBitmapH);
    CHECK_EQ(v[0].xpos, 40);
    CHECK_EQ(v[0].bpp, 8);
    CHECK_EQ(v[0].data, MockDebugIface::kBitmapData);
    CHECK(v[0].trans);
    CHECK_EQ(v[1].xpos, -8);
    CHECK_EQ(v[1].bpp, 16);
    CHECK_EQ(v[1].hscale, 0x40);
    CHECK_EQ(v[1].scaledWidth, 32);
    CHECK_EQ(v[2].cc, 0);
    CHECK_EQ(v[2].ypos, 400);
    CHECK_EQ(v[2].link, MockDebugIface::kOpListBase + 0x48);
}

// ---- screen mapping ---------------------------------------------------------------------------------------------------
static void testScreenMapping() {
    OpScreenGeom g;
    g.hdb1 = 208;
    g.vdb = 31;
    g.vp = 523;
    g.vmode = 0;   // PWIDTH 1
    CHECK(!g.pal());
    CHECK_EQ(opLeftVisibleHC(g), 188);
    CHECK_EQ(opTopVisible(g), 31);
    Bm b;
    b.ypos = 51;
    b.height = 20;
    b.xpos = 10;
    b.iwidth = 4;   // 32 px at 8 bpp
    OpScreenRect r = opScreenRect(b.decode(), g);
    CHECK(r.valid);
    CHECK_EQ((int)r.x, 30);     // (208-188)/1 + 10
    CHECK_EQ((int)r.y, 10);     // (51-31)/2
    CHECK_EQ((int)r.w, 32);
    CHECK_EQ((int)r.h, 20);

    // VDB of 0 / 38 / beyond VP -> the fallback; PAL detected from VP.
    g.vdb = 0;
    CHECK_EQ(opTopVisible(g), 31);
    g.vdb = 38;
    CHECK_EQ(opTopVisible(g), 31);
    g.vdb = 600;
    CHECK_EQ(opTopVisible(g), 31);
    g.vp = 623;
    g.vdb = 0;
    CHECK(g.pal());
    CHECK_EQ(opTopVisible(g), 67);
    CHECK_EQ(opLeftVisibleHC(g), 204);

    // REFLECT: XPOS is the right edge.  PWIDTH 8 (vmode bits 9-11 = 7) doubles columns (scale = 8/4).
    Bm rf;
    rf.reflect = true;
    rf.xpos = 100;
    rf.iwidth = 1;
    rf.ypos = 40;
    OpScreenGeom n;
    n.hdb1 = 188;
    n.vdb = 40;
    n.vp = 523;
    CHECK_EQ((int)opScreenRect(rf.decode(), n).x, 100 - 7);
    n.vmode = 7u << 9;
    CHECK_EQ(n.pwidth(), 8);
    CHECK_EQ((int)opScreenRect(rf.decode(), n).w, 16);

    // Not drawable -> no rectangle.
    CHECK(!opScreenRect(opDecode(0, 1, 4, 0, 0), g).valid);
    Bm h0;
    h0.height = 0;
    CHECK(!opScreenRect(h0.decode(), g).valid);
}

// ---- renderer --------------------------------------------------------------------------------------------------------------
static OpObject mockObject(MockDebugIface& m, int idx) {
    vj_debug_op_obj raw[8];
    unsigned hl = 0;
    int n = m.iface()->op_snapshot_get(raw, 8, &hl);
    return opDecode(raw[idx < n ? idx : 0]);
}

static void testRenderMock8bpp() {
    MockDebugIface m;
    OpObject o = mockObject(m, 0);
    OpImage img;
    std::string err;
    CHECK(opRender(m.iface(), o, OpRenderOptions(), img, err));
    CHECK_EQ(img.w, 16);
    CHECK_EQ(img.h, 4);
    CHECK(!img.truncated && !img.mirrored);
    CHECK(img.mode == OpColorMode::Cry);
    CHECK_EQ(img.clut.size(), 256);
    for (int y = 0; y < img.h; y++)
        for (int x = 0; x < img.w; x++) {
            unsigned v = (unsigned)(y * 16 + x);
            CHECK_EQ(img.rawAt(x, y), v);
            // CLUT entry v = (v << 8) | (255 - v), through the mock's cry_to_rgb.
            unsigned rgb = m.iface()->cry_to_rgb((v << 8) | (255 - v));
            const uint8_t* p = img.rgbaAt(x, y);
            CHECK_EQ(p[0], (rgb >> 16) & 255);
            CHECK_EQ(p[1], (rgb >> 8) & 255);
            CHECK_EQ(p[2], rgb & 255);
            CHECK_EQ(p[3], v == 0 ? 0 : 255);   // TRANS: value 0 is transparent
        }
    // TRANS switched off in the preview: opaque.
    OpRenderOptions no;
    no.honorTrans = false;
    CHECK(opRender(m.iface(), o, no, img, err));
    CHECK_EQ(img.rgbaAt(0, 0)[3], 255);
    // The RGB16 toggle: entry 0x0102 -> RRRRR BBBBB GGGGGG.
    OpRenderOptions rg;
    rg.color = OpColorMode::Rgb16;
    CHECK(opRender(m.iface(), o, rg, img, err));
    CHECK(img.mode == OpColorMode::Rgb16);
    unsigned w1 = (1u << 8) | 254;   // pixel value 1
    uint32_t want = ((w1 & 0xF800u) << 8) | ((w1 & 0x3Fu) << 10) | ((w1 & 0x7C0u) >> 3);
    CHECK_EQ(img.rgbaAt(1, 0)[0], (want >> 16) & 255);
    CHECK_EQ(img.rgbaAt(1, 0)[1], (want >> 8) & 255);
    CHECK_EQ(img.rgbaAt(1, 0)[2], want & 255);
    CHECK_EQ(opColorToRgb(m.iface(), OpColorMode::Rgb16, 0xFFFF), 0xF8FCF8u | 0u);   // R5 B5 G6: G is 6 bits at 15..10
    // Auto follows VMODE: MODE bits 1-2 = 3 is RGB16; with VARMOD in CRY mode it is mixed.
    m.regwin()[0x29] = 0x06;
    CHECK(opResolveColorMode(m.iface(), OpColorMode::Auto) == OpColorMode::Rgb16);
    m.regwin()[0x28] = 0x01;
    m.regwin()[0x29] = 0x00;
    CHECK(opResolveColorMode(m.iface(), OpColorMode::Auto) == OpColorMode::Mixed);
    m.regwin()[0x28] = 0;
    CHECK(opResolveColorMode(m.iface(), OpColorMode::Auto) == OpColorMode::Cry);
    CHECK(opResolveColorMode(m.iface(), OpColorMode::Rgb16) == OpColorMode::Rgb16);
    // Mixed: bit 0 picks RGB16.
    CHECK_EQ(opColorToRgb(m.iface(), OpColorMode::Mixed, 0x1235), opColorToRgb(m.iface(), OpColorMode::Rgb16, 0x1235));
    CHECK_EQ(opColorToRgb(m.iface(), OpColorMode::Mixed, 0x1234), opColorToRgb(m.iface(), OpColorMode::Cry, 0x1234));
}

static void testRenderMockScaled16() {
    MockDebugIface m;
    OpObject o = mockObject(m, 1);
    OpImage img;
    std::string err;
    CHECK(opRender(m.iface(), o, OpRenderOptions(), img, err));
    CHECK_EQ(img.w, 16);   // 4 phrases * 4 px
    CHECK_EQ(img.h, 8);
    CHECK(img.note.find("unscaled") != std::string::npos);
    CHECK(img.note.find("2.00") != std::string::npos);
    // DRAM byte i = i & 0xFF; dwidth 4 phrases = 32 bytes per line; no TRANS.
    for (int y = 0; y < img.h; y++)
        for (int x = 0; x < img.w; x++) {
            unsigned off = 0x20000u + (unsigned)y * 32 + (unsigned)x * 2;
            unsigned v = (((off) & 0xFF) << 8) | ((off + 1) & 0xFF);
            CHECK_EQ(img.rawAt(x, y), v);
            CHECK_EQ(img.rgbaAt(x, y)[3], 255);
        }
}

// Writes one phrase-organised bitmap into the mock's DRAM.
static void setBytes(MockDebugIface& m, unsigned at, std::initializer_list<unsigned> b) {
    unsigned i = 0;
    for (unsigned v : b) m.dram()[at + i++] = (uint8_t)v;
}
static void setClut(MockDebugIface& m, unsigned i, unsigned w) {
    m.regwin()[0x400 + i * 2] = (uint8_t)(w >> 8);
    m.regwin()[0x400 + i * 2 + 1] = (uint8_t)w;
}

static void testRenderIndexedDepths() {
    MockDebugIface m;
    std::string err;
    // CLUT: entry n -> distinctive word so a wrong base shows up.
    for (unsigned i = 0; i < 256; i++) setClut(m, i, 0x1000 + i);

    // 4 bpp, INDEX 0x35: base = (0x35 << 1) & 0xF0 = 0x60; pixel nibbles 0..15 MSB first in one phrase.
    setBytes(m, 0x30000, {0x01, 0x23, 0x45, 0x67, 0x89, 0xAB, 0xCD, 0xEF});
    Bm b;
    b.depth = 2;
    b.index = 0x35;
    b.data = 0x30000;
    b.iwidth = 1;
    b.dwidth = 1;
    b.height = 1;
    OpImage img;
    CHECK(opRender(m.iface(), b.decode(), OpRenderOptions(), img, err));
    CHECK_EQ(img.w, 16);
    for (int x = 0; x < 16; x++) {
        CHECK_EQ(img.rawAt(x, 0), (unsigned)x);
        unsigned want = m.iface()->cry_to_rgb(0x1000 + (0x60 | x));
        CHECK_EQ(((unsigned)img.rgbaAt(x, 0)[0] << 16) | ((unsigned)img.rgbaAt(x, 0)[1] << 8) | img.rgbaAt(x, 0)[2], want);
    }

    // 2 bpp, INDEX 0x35: base = 0x6A & 0xFC = 0x68; byte 0xE4 = pixels 3,2,1,0.
    setBytes(m, 0x30100, {0xE4, 0, 0, 0, 0, 0, 0, 0});
    Bm c = b;
    c.depth = 1;
    c.data = 0x30100;
    CHECK(opRender(m.iface(), c.decode(), OpRenderOptions(), img, err));
    CHECK_EQ(img.w, 32);
    CHECK_EQ(img.rawAt(0, 0), 3);
    CHECK_EQ(img.rawAt(1, 0), 2);
    CHECK_EQ(img.rawAt(2, 0), 1);
    CHECK_EQ(img.rawAt(3, 0), 0);
    CHECK_EQ(m.iface()->cry_to_rgb(0x1000 + (0x68 | 3)),
             ((unsigned)img.rgbaAt(0, 0)[0] << 16) | ((unsigned)img.rgbaAt(0, 0)[1] << 8) | img.rgbaAt(0, 0)[2]);

    // 1 bpp, INDEX 0x35: base = 0x6A; bit 1 -> CLUT[0x6B], bit 0 -> CLUT[0x6A]; 0x80 = first pixel set.
    setBytes(m, 0x30200, {0x80, 0, 0, 0, 0, 0, 0, 0x01});
    Bm d = b;
    d.depth = 0;
    d.data = 0x30200;
    CHECK(opRender(m.iface(), d.decode(), OpRenderOptions(), img, err));
    CHECK_EQ(img.w, 64);
    CHECK_EQ(img.rawAt(0, 0), 1);
    CHECK_EQ(img.rawAt(1, 0), 0);
    CHECK_EQ(img.rawAt(63, 0), 1);
    CHECK_EQ(m.iface()->cry_to_rgb(0x1000 + 0x6B),
             ((unsigned)img.rgbaAt(0, 0)[0] << 16) | ((unsigned)img.rgbaAt(0, 0)[1] << 8) | img.rgbaAt(0, 0)[2]);
    CHECK_EQ(m.iface()->cry_to_rgb(0x1000 + 0x6A),
             ((unsigned)img.rgbaAt(1, 0)[0] << 16) | ((unsigned)img.rgbaAt(1, 0)[1] << 8) | img.rgbaAt(1, 0)[2]);

    // 8 bpp ignores INDEX entirely.
    setBytes(m, 0x30300, {5, 6, 7, 8, 9, 10, 11, 12});
    Bm e = b;
    e.depth = 3;
    e.index = 0x7F;
    e.data = 0x30300;
    CHECK(opRender(m.iface(), e.decode(), OpRenderOptions(), img, err));
    CHECK_EQ(m.iface()->cry_to_rgb(0x1000 + 5),
             ((unsigned)img.rgbaAt(0, 0)[0] << 16) | ((unsigned)img.rgbaAt(0, 0)[1] << 8) | img.rgbaAt(0, 0)[2]);
}

static void testRender16And24() {
    MockDebugIface m;
    std::string err;
    setBytes(m, 0x31000, {0x12, 0x34, 0x00, 0x00, 0xAB, 0xCD, 0xFF, 0xFF});
    Bm b;
    b.depth = 4;
    b.data = 0x31000;
    b.trans = true;
    b.height = 1;
    OpImage img;
    CHECK(opRender(m.iface(), b.decode(), OpRenderOptions(), img, err));
    CHECK_EQ(img.w, 4);
    CHECK_EQ(img.rawAt(0, 0), 0x1234);
    CHECK_EQ(img.rawAt(1, 0), 0);
    CHECK_EQ(img.rgbaAt(1, 0)[3], 0);          // TRANS: a zero word is transparent
    CHECK_EQ(img.rgbaAt(0, 0)[3], 255);
    CHECK_EQ(img.rawAt(2, 0), 0xABCD);
    // 24 bpp: per pixel bytes G R x B -> rgb(R, G, B); two pixels per phrase.
    setBytes(m, 0x31100, {0x22, 0x11, 0x99, 0x33, 0x00, 0x00, 0x00, 0x00});
    Bm c;
    c.depth = 5;
    c.data = 0x31100;
    c.trans = true;
    c.height = 1;
    CHECK(opRender(m.iface(), c.decode(), OpRenderOptions(), img, err));
    CHECK_EQ(img.w, 2);
    CHECK_EQ(img.rgbaAt(0, 0)[0], 0x11);
    CHECK_EQ(img.rgbaAt(0, 0)[1], 0x22);
    CHECK_EQ(img.rgbaAt(0, 0)[2], 0x33);
    CHECK_EQ(img.rgbaAt(0, 0)[3], 255);
    CHECK_EQ(img.rawAt(0, 0), 0x112233);
    CHECK_EQ(img.rgbaAt(1, 0)[3], 0);          // an all-zero 24 bpp pixel is transparent under TRANS
}

static void testRenderPitchFirstpixReflect() {
    MockDebugIface m;
    std::string err;
    // Two lines, DWIDTH 3 phrases (24 bytes) per line; pixels = byte index & 0xFF + 1, 8 bpp.
    for (unsigned i = 0; i < 128; i++) m.dram()[0x32000 + i] = (uint8_t)(i + 1);
    Bm b;
    b.depth = 3;
    b.data = 0x32000;
    b.dwidth = 3;
    b.iwidth = 2;
    b.height = 2;
    OpImage img;
    CHECK(opRender(m.iface(), b.decode(), OpRenderOptions(), img, err));
    CHECK_EQ(img.w, 16);
    CHECK_EQ(img.rawAt(0, 1), 24 + 1);       // line 1 starts DWIDTH * 8 bytes on
    CHECK_EQ(img.rawAt(15, 0), 16);
    // PITCH 2: the second phrase is 16 bytes on (8 * PITCH), not 8.
    b.pitch = 2;
    CHECK(opRender(m.iface(), b.decode(), OpRenderOptions(), img, err));
    CHECK_EQ(img.rawAt(8, 0), 16 + 1);
    // PITCH 0 repeats the same phrase.
    b.pitch = 0;
    CHECK(opRender(m.iface(), b.decode(), OpRenderOptions(), img, err));
    CHECK_EQ(img.rawAt(8, 0), 1);
    CHECK_EQ(img.rawAt(15, 0), 8);
    // FIRSTPIX 16 bits skips two 8-bpp pixels of every line's first phrase.
    b.pitch = 1;
    b.firstpix = 16;
    OpObject o = b.decode();
    CHECK_EQ(o.skipPixels, 2);
    CHECK(opRender(m.iface(), o, OpRenderOptions(), img, err));
    CHECK_EQ(img.w, 14);
    CHECK_EQ(img.rawAt(0, 0), 3);
    CHECK_EQ(img.rawAt(13, 0), 16);
    CHECK_EQ(img.rawAt(0, 1), 24 + 3);
    // REFLECT mirrors the image; the REFLECT preview switch undoes it.
    b.firstpix = 0;
    b.reflect = true;
    CHECK(opRender(m.iface(), b.decode(), OpRenderOptions(), img, err));
    CHECK(img.mirrored);
    CHECK_EQ(img.rawAt(15, 0), 1);
    CHECK_EQ(img.rawAt(0, 0), 16);
    OpRenderOptions nr;
    nr.honorReflect = false;
    CHECK(opRender(m.iface(), b.decode(), nr, img, err));
    CHECK(!img.mirrored);
    CHECK_EQ(img.rawAt(0, 0), 1);
}

static void testRenderLimitsAndErrors() {
    MockDebugIface m;
    std::string err;
    OpImage img;
    // A garbage 1 bpp object: 1023 phrases * 64 px wide, 1000 lines -> clamped.
    Bm b;
    b.depth = 0;
    b.iwidth = 1023;
    b.dwidth = 1023;
    b.height = 1000;
    b.data = 0;
    OpRenderOptions opt;
    CHECK(opRender(m.iface(), b.decode(), opt, img, err));
    CHECK(img.truncated);
    CHECK_EQ(img.srcW, 1023u * 64);
    CHECK_EQ(img.srcH, 1000);
    CHECK(img.w <= (int)opt.maxW && img.h <= (int)opt.maxH);
    CHECK((uint64_t)img.w * (uint64_t)img.h <= opt.maxPixels);
    CHECK(img.note.find("clamped") != std::string::npos);

    // Errors: not a bitmap, HEIGHT 0, bad depth, missing phrase, no iface.
    CHECK(!opRender(m.iface(), opDecode(0, 1, 4, 0, 0), opt, img, err) && !err.empty());
    Bm h0;
    h0.height = 0;
    CHECK(!opRender(m.iface(), h0.decode(), opt, img, err));
    Bm bd;
    bd.depth = 7;
    CHECK(!opRender(m.iface(), bd.decode(), opt, img, err));
    CHECK(!opRender(m.iface(), opDecode(0, 1, Bm().p0(), 0, 0), opt, img, err));
    CHECK(!opRender(nullptr, Bm().decode(), opt, img, err));
}

// ---- pixel editor -------------------------------------------------------------------------------------------------------------
static void testPixelPatch() {
    MockDebugIface m;
    std::string err;
    // 4 bpp: edit a nibble without disturbing its neighbour.
    setBytes(m, 0x33000, {0x01, 0x23, 0x45, 0x67, 0x89, 0xAB, 0xCD, 0xEF});
    Bm b;
    b.depth = 2;
    b.data = 0x33000;
    b.height = 1;
    OpObject o = b.decode();
    OpImage img;
    CHECK(opRender(m.iface(), o, OpRenderOptions(), img, err));
    OpPixelLoc L = opPixelLocation(o, img, 3, 0);
    CHECK(L.ok);
    CHECK_EQ(L.addr, 0x33001);
    CHECK_EQ(L.bytes, 1);
    CHECK_EQ(L.mask, 0xF);
    CHECK_EQ(L.shift, 0);   // pixel 3 is the low nibble of byte 1
    CHECK_EQ(opPixelLocation(o, img, 2, 0).shift, 4);
    OpPixelPatch p;
    CHECK(opPixelPatch(m.iface(), o, img, 3, 0, 0xC, p));
    CHECK_EQ(p.addr, 0x33001);
    CHECK_EQ(p.n, 1);
    CHECK_EQ(p.bytes[0], 0x2C);
    CHECK_EQ(m.iface()->poke(p.addr, p.bytes, p.n, 0), 1);
    CHECK_EQ(m.dram()[0x33001], 0x2C);
    CHECK_EQ(m.dram()[0x33000], 0x01);   // neighbour byte untouched
    CHECK(opRender(m.iface(), o, OpRenderOptions(), img, err));
    CHECK_EQ(img.rawAt(3, 0), 0xC);
    CHECK_EQ(img.rawAt(2, 0), 0x2);
    // Value is masked to the depth.
    CHECK(opPixelPatch(m.iface(), o, img, 0, 0, 0xFF, p));
    CHECK_EQ(p.bytes[0], 0xF1);
    CHECK(!opPixelPatch(m.iface(), o, img, 16, 0, 1, p));    // outside the image
    CHECK(!opPixelLocation(o, img, -1, 0).ok);
    CHECK_EQ(opPixelMax(o), 15);

    // 1 bpp, 2 bpp bit positions.
    Bm one = b;
    one.depth = 0;
    OpObject o1 = one.decode();
    CHECK(opRender(m.iface(), o1, OpRenderOptions(), img, err));
    OpPixelLoc l1 = opPixelLocation(o1, img, 10, 0);
    CHECK_EQ(l1.addr, 0x33001);
    CHECK_EQ(l1.shift, 5);   // bit 7-2
    Bm two = b;
    two.depth = 1;
    OpObject o2 = two.decode();
    CHECK(opRender(m.iface(), o2, OpRenderOptions(), img, err));
    OpPixelLoc l2 = opPixelLocation(o2, img, 5, 0);
    CHECK_EQ(l2.addr, 0x33001);
    CHECK_EQ(l2.shift, 4);   // pixel 5: bits 5-4 of byte 1

    // 16 bpp word, REFLECT maps the displayed column back to the source column.
    Bm w;
    w.depth = 4;
    w.data = 0x33100;
    w.height = 1;
    w.reflect = true;
    OpObject ow = w.decode();
    CHECK(opRender(m.iface(), ow, OpRenderOptions(), img, err));
    OpPixelLoc lw = opPixelLocation(ow, img, 0, 0);   // displayed column 0 = source pixel 3
    CHECK_EQ(lw.addr, 0x33106);
    CHECK_EQ(lw.bytes, 2);
    CHECK(opPixelPatch(m.iface(), ow, img, 0, 0, 0xBEEF, p));
    CHECK_EQ(p.n, 2);
    CHECK_EQ(p.bytes[0], 0xBE);
    CHECK_EQ(p.bytes[1], 0xEF);
    // PITCH moves the phrase.
    w.pitch = 3;
    w.reflect = false;
    w.iwidth = 2;
    w.dwidth = 6;
    w.height = 2;
    OpObject op = w.decode();
    CHECK(opRender(m.iface(), op, OpRenderOptions(), img, err));
    CHECK_EQ(opPixelLocation(op, img, 5, 1).addr, 0x33100u + 48 + 3 * 8 + 2);

    // 24 bpp: bytes G R x B, the unused byte is preserved.
    setBytes(m, 0x33200, {0, 0, 0x77, 0, 0, 0, 0x66, 0});
    Bm t;
    t.depth = 5;
    t.data = 0x33200;
    t.height = 1;
    OpObject o24 = t.decode();
    CHECK(opRender(m.iface(), o24, OpRenderOptions(), img, err));
    CHECK(opPixelPatch(m.iface(), o24, img, 0, 0, 0x112233, p));
    CHECK_EQ(p.n, 4);
    CHECK_EQ(p.bytes[0], 0x22);
    CHECK_EQ(p.bytes[1], 0x11);
    CHECK_EQ(p.bytes[2], 0x77);
    CHECK_EQ(p.bytes[3], 0x33);
    CHECK(opPixelPatch(m.iface(), o24, img, 1, 0, 0x445566, p));
    CHECK_EQ(p.addr, 0x33204);
    CHECK_EQ(p.bytes[2], 0x66);
}

// ---- list walking -----------------------------------------------------------------------------------------------------------------
static bool mockReader(void* ctx, unsigned addr, uint8_t* dst, unsigned n) {
    const vj_debug_iface* i = static_cast<const vj_debug_iface*>(ctx);
    return i->peek(addr, dst, n) == (int)n;
}
static void putPhrase(MockDebugIface& m, unsigned at, uint64_t v) {
    for (int i = 0; i < 8; i++) m.dram()[at + (unsigned)i] = (uint8_t)(v >> (56 - 8 * i));
}
// A list in DRAM:  1000 bitmap -> 1020 scaled -> 1040 branch (link 1100, falls to 1048)
//                  1048 gpu -> 1050 bitmap -> 1070 stop        1100 bitmap -> 1070
static void buildRamList(MockDebugIface& m) {
    Bm a;   a.link = 0x1020;  a.height = 2;
    Bm s;   s.type = 1; s.link = 0x1040; s.height = 2;
    putPhrase(m, 0x1000, a.p0());  putPhrase(m, 0x1008, a.p1());
    putPhrase(m, 0x1020, s.p0());  putPhrase(m, 0x1028, s.p1());  putPhrase(m, 0x1030, s.p2());
    putPhrase(m, 0x1040, 3ull | (1ull << 14) | ((uint64_t)(0x1100u >> 3) << 24));
    putPhrase(m, 0x1048, 2ull);
    Bm c;   c.link = 0x1070;  c.height = 3;
    putPhrase(m, 0x1050, c.p0());  putPhrase(m, 0x1058, c.p1());
    putPhrase(m, 0x1070, 4ull);
    Bm d;   d.link = 0x1070;  d.height = 4;
    putPhrase(m, 0x1100, d.p0());  putPhrase(m, 0x1108, d.p1());
}

static void testWalkList() {
    MockDebugIface m;
    buildRamList(m);
    std::vector<OpObject> w = opWalkList(&mockReader, const_cast<vj_debug_iface*>(m.iface()), 0x1000, 100);
    // Depth first, a branch's fall-through before its link, every address once.
    unsigned want[] = {0x1000, 0x1020, 0x1040, 0x1048, 0x1050, 0x1070, 0x1100};
    CHECK_EQ(w.size(), 7);
    for (size_t i = 0; i < w.size() && i < 7; i++) CHECK_EQ(w[i].address, want[i]);
    for (const OpObject& o : w) CHECK(o.fromRam);
    CHECK(w.size() == 7 && w[3].type == OpType::Gpu && w[5].type == OpType::Stop && w[6].height == 4);
    // The cap, a bad head, an unreadable head.
    CHECK_EQ(opWalkList(&mockReader, const_cast<vj_debug_iface*>(m.iface()), 0x1000, 3).size(), 3);
    CHECK(opWalkList(&mockReader, const_cast<vj_debug_iface*>(m.iface()), 0, 10).empty());
    CHECK(opWalkList(nullptr, nullptr, 0x1000, 10).empty());
    // A loop (link back to the head) terminates.
    Bm loop;
    loop.link = 0x2000;
    putPhrase(m, 0x2000, loop.p0());
    putPhrase(m, 0x2008, loop.p1());
    CHECK_EQ(opWalkList(&mockReader, const_cast<vj_debug_iface*>(m.iface()), 0x2000, 100).size(), 1);
    // The 4 MB mirror folds back like op.c: a link of 0x201000 is 0x001000.
    Bm mir;
    mir.link = 0x201000;
    putPhrase(m, 0x2100, mir.p0());
    putPhrase(m, 0x2108, mir.p1());
    std::vector<OpObject> mw = opWalkList(&mockReader, const_cast<vj_debug_iface*>(m.iface()), 0x2100, 100);
    CHECK(mw.size() > 1 && mw[1].address == 0x1000);
}

// ---- view model ---------------------------------------------------------------------------------------------------------------
static void testViewModelLifecycle() {
    DebugSession s;
    MockDebugIface m;
    s.setIface(m.iface());
    s.setContentLoaded(true);
    OpViewModel vm;
    CHECK(!vm.haveSnapshot());
    vm.update(s);                                    // opening the window arms the first snapshot...
    CHECK_EQ(m.opArms, 1);
    CHECK(vm.waiting());
    vm.update(s);                                    // ...which the mock completes at once
    CHECK(vm.haveSnapshot() && !vm.waiting());
    CHECK_EQ(vm.objects().size(), 4);
    CHECK_EQ(vm.halfline(), 41);
    CHECK_EQ(vm.selected(), 0);                      // the first drawable bitmap is selected for you
    CHECK(vm.image() != nullptr && vm.image()->w == 16);

    // Selecting a bitmap renders it; selecting a branch does not.
    vm.select(0);
    vm.update(s);
    CHECK(vm.image() != nullptr);
    CHECK_EQ(vm.image()->w, 16);
    uint64_t ser = vm.imageSerial();
    vm.update(s);                                    // nothing changed: the serial must not churn
    CHECK_EQ(vm.imageSerial(), ser);
    vm.select(2);
    vm.update(s);
    CHECK(vm.image() == nullptr);
    CHECK(vm.imageError().empty());                  // a branch is not an error, just not drawable
    vm.select(1);
    vm.update(s);
    CHECK(vm.image() != nullptr);
    CHECK_EQ(vm.image()->h, 8);

    // Live: a refresh tick re-arms; with live off it does not.
    unsigned arms = m.opArms;
    s.requestRefresh();
    vm.update(s);
    CHECK_EQ(m.opArms, arms + 1);
    vm.live = false;
    s.requestRefresh();
    vm.update(s);
    CHECK_EQ(m.opArms, arms + 1);
    // ...but a manual arm always works, and a walk that has not happened yet leaves the table alone.
    m.opReady = false;
    vm.arm(s);
    CHECK_EQ(m.opArms, arms + 2);
    vm.update(s);
    CHECK(vm.waiting());
    CHECK_EQ(vm.objects().size(), 4);                // old snapshot stays on screen
    m.opReady = true;
    vm.update(s);
    CHECK(!vm.waiting());
    CHECK_EQ(vm.selected(), 1);                      // selection followed the object (by address)

    // The tools being off (hardcore) freezes it: no iface calls.
    s.setHardcore(true);
    arms = m.opArms;
    vm.arm(s);
    vm.update(s);
    CHECK_EQ(m.opArms, arms);
    vm.clear();
    CHECK(!vm.haveSnapshot() && vm.selected() == -1 && vm.image() == nullptr);
}

static void testViewModelFollowLinks() {
    DebugSession s;
    MockDebugIface m;
    buildRamList(m);
    s.setIface(m.iface());
    s.setContentLoaded(true);
    OpViewModel vm;
    CHECK(vm.followLinks);
    vm.update(s);
    vm.update(s);
    // The core's snapshot is the whole list: the RAM walk is NOT consulted while it has objects, even though
    // the mock's RAM holds three more (0x1050, 0x1070, 0x1100).
    CHECK_EQ(vm.snapshotCount(), 4);
    CHECK_EQ(vm.objects().size(), 4);
    for (const OpObject& o : vm.objects()) CHECK(!o.fromRam);
    // An empty snapshot (older core): the fallback walks the list from OLP ($F00020, LO / HI word) through RAM.
    m.clearOpSnapshot();
    m.regwin()[0x20] = 0x10;   // OLP = $001000: low word $1000 at $F00020, high word 0 at $F00022
    m.regwin()[0x21] = 0x00;
    m.regwin()[0x22] = 0x00;
    m.regwin()[0x23] = 0x00;
    vm.arm(s);
    vm.update(s);
    CHECK_EQ(vm.snapshotCount(), 0);
    CHECK_EQ(vm.objects().size(), 7);
    for (const OpObject& o : vm.objects()) CHECK(o.fromRam);
    CHECK_EQ(vm.objects()[4].address, 0x1050);
    CHECK(!vm.objects()[4].warning.empty());
    // A selected RAM-only object stays selected across the next snapshot (by address).
    vm.select(5);
    CHECK_EQ(vm.objects()[5].address, 0x1070);
    s.requestRefresh();
    vm.update(s);
    vm.update(s);
    CHECK_EQ(vm.selected(), 5);
    // Fallback off: just the (empty) snapshot.
    vm.followLinks = false;
    vm.arm(s);
    vm.update(s);
    CHECK_EQ(vm.objects().size(), 0);
}

static void testViewModelPaint() {
    DebugSession s;
    MockDebugIface m;
    s.setIface(m.iface());
    s.setContentLoaded(true);
    OpViewModel vm;
    vm.update(s);
    vm.update(s);
    vm.select(0);
    vm.update(s);
    std::string st;
    CHECK(vm.image() != nullptr);
    // Paint pixel (3, 2) of the mock's 8 bpp bitmap (DRAM $10000 + 2*16 + 3).
    CHECK(vm.paint(s, 3, 2, 200, st));
    CHECK_EQ(m.dram()[MockDebugIface::kBitmapData + 2 * 16 + 3], 200);
    CHECK_EQ(vm.image()->rawAt(3, 2), 200);          // the preview re-read DRAM
    CHECK(vm.hasLastEdit());
    CHECK_EQ(vm.lastEdit().addr, MockDebugIface::kBitmapData + 35);
    CHECK_EQ(vm.lastEdit().n, 1);
    CHECK_EQ(vm.lastEdit().bytes[0], 200);
    CHECK(st.find("wrote 1 byte") != std::string::npos);
    CHECK_EQ(m.pokesAccepted, 1);
    // Painting a pixel outside the image fails cleanly.
    CHECK(!vm.paint(s, 99, 0, 1, st));

    // Cartridge data is read-only: a clear message, no change.
    Bm b;
    b.depth = 3;
    b.data = 0x800000;
    b.height = 1;
    b.iwidth = 1;
    OpObject o = b.decode();
    OpImage img;
    std::string err;
    CHECK(opRender(m.iface(), o, OpRenderOptions(), img, err));
    OpPixelPatch p;
    CHECK(opPixelPatch(m.iface(), o, img, 0, 0, 1, p));
    int rc = s.poke(p.addr, p.bytes, p.n);
    CHECK_EQ(rc, VJ_DBG_ERR_READONLY);
    CHECK(std::string(DebugSession::errorText(rc)).find("read-only") != std::string::npos);

    // The painter is RAM-only even with the memory viewer's register switches on: an object whose data sits in
    // TOM register space is refused, not poked.  (Control: the same object in DRAM paints.)
    {
        s.allowRegisterWrites = true;
        s.allowDangerousWrites = true;
        OpViewModel v2;
        v2.live = false;
        Bm rg;
        rg.depth = 3;
        rg.data = 0xF00200;
        rg.height = 1;
        rg.iwidth = 1;
        rg.dwidth = 1;
        v2.setObjects({rg.decode(0x4000)});
        v2.select(0);
        v2.update(s);
        CHECK(v2.image() != nullptr);
        unsigned before = m.pokesAccepted + m.pokesRefused;
        uint8_t was = m.regwin()[0x200];
        std::string st2;
        CHECK(!v2.paint(s, 0, 0, 0x5A, st2));
        CHECK(st2.find("only writes RAM") != std::string::npos);
        CHECK_EQ(m.pokesAccepted + m.pokesRefused, before);     // never even reached the core
        CHECK_EQ(m.regwin()[0x200], was);
        rg.data = 0x40000;
        v2.setObjects({rg.decode(0x4000)});
        v2.select(0);
        v2.update(s);
        CHECK(v2.paint(s, 0, 0, 0x5A, st2));
        CHECK_EQ(m.dram()[0x40000], 0x5A);
        s.allowRegisterWrites = false;
        s.allowDangerousWrites = false;
    }

    // Painting with the tools off is refused.
    s.setHardcore(true);
    CHECK(!vm.paint(s, 0, 0, 1, st));
    CHECK(st.find("not written") != std::string::npos);
}

static void testViewModelGeom() {
    DebugSession s;
    MockDebugIface m;
    s.setIface(m.iface());
    s.setContentLoaded(true);
    m.regwin()[0x38] = 0x00; m.regwin()[0x39] = 0xD0;   // HDB1 = 208
    m.regwin()[0x46] = 0x00; m.regwin()[0x47] = 0x1F;   // VDB = 31
    m.regwin()[0x3E] = 0x02; m.regwin()[0x3F] = 0x0B;   // VP = 523
    m.regwin()[0x28] = 0x0E; m.regwin()[0x29] = 0x01;   // VMODE: PWIDTH 7+1... bits 9-11 = 7, VARMOD no, MODE 0
    OpViewModel vm;
    vm.update(s);                                       // adopts the $F00028 view buffer
    OpScreenGeom g = vm.readGeom(320, 240);
    CHECK_EQ(g.hdb1, 208);
    CHECK_EQ(g.vdb, 31);
    CHECK_EQ(g.vp, 523);
    CHECK_EQ(g.frameW, 320);
    CHECK_EQ(g.frameH, 240);
    CHECK_EQ(g.pwidth(), 8);
}

// ---- the Object list on the OpSnapshot request (#967 3a) ----------------------------------------------------------------------
static void testOpStepGate() {
    OpStepGate g{};                                          // all false
    g.loaded = true; g.paused = true;
    CHECK(opStepFor(g) == OpStep::Inline);
    g.attached = true;  CHECK(opStepFor(g) == OpStep::None);  g.attached = false;
    g.inFrame = true;   CHECK(opStepFor(g) == OpStep::None);  g.inFrame = false;
    g.threaded = true;  CHECK(opStepFor(g) == OpStep::Advance);   // inFrame is not consulted threaded
    g.attached = true;  CHECK(opStepFor(g) == OpStep::Advance);   g.attached = false;   // advance() gives up on a halt (3b)
    g.inFrame = true;   CHECK(opStepFor(g) == OpStep::Advance);   g.inFrame = false;
    g.halted = true;    CHECK(opStepFor(g) == OpStep::None);      g.halted = false;
    g.netplay = true;   CHECK(opStepFor(g) == OpStep::None);      g.netplay = false;
    g.paused = false;   CHECK(opStepFor(g) == OpStep::None);
}
static void testViewModelNeverTouchesTheCore() {
    UiFixture f;
    OpViewModel vm;
    vm.update(f.s);  vm.update(f.s);                          // arm + first absorb, as testViewModelLifecycle
    CHECK(vm.haveSnapshot() && vm.objects().size() == 4);
    CHECK(CountingIface::offCore() == 0);
    const UiFixture::Mark m = f.mark();
    for (int i = 0; i < 30; i++) vm.update(f.s);              // idle: no new serial, nothing waiting
    CHECK(f.quietSince(m));
    f.s.requestRefresh();
    vm.update(f.s);                                           // a live tick: one arm (post) and one fill, off-core zero
    CHECK(f.corePosts > m.corePosts && CountingIface::offCore() == 0);
}
// A deferred emulation thread over the fixture's session (what a threaded App does): posts queue, drain() runs them.
struct DeferredCore {
    std::vector<std::function<void()>> q;
    explicit DeferredCore(UiFixture& f) {
        f.s.corePost = [this](std::function<void()> fn) { q.push_back(std::move(fn)); };
    }
    void drain() {
        std::vector<std::function<void()>> work = std::move(q);
        q.clear();
        for (auto& fn : work) { CountingIface::OnCore g; fn(); }
    }
};
static void testRenderLeavesTheUiThread() {
    UiFixture f;
    DeferredCore d(f);                                        // what the emulation thread does at its next boundary: drain()
    OpViewModel vm;
    vm.update(f.s);                                           // queues the fill (snapshot request not answered yet), then the arm
    d.drain();                                                // fill, then the arm applied (the request is reset after it)
    vm.update(f.s);                                           // a fill is pending again: queued
    d.drain();                                                // the snapshot is filled
    vm.update(f.s);                                           // absorbs; queues the first render
    const unsigned peeks = CountingIface::count(CountingIface::Peek);
    CHECK(vm.haveSnapshot() && vm.image() == nullptr);        // not rendered yet: nothing ran on this thread
    CHECK(CountingIface::offCore() == 0);
    d.drain();
    CHECK(CountingIface::count(CountingIface::Peek) > peeks);
    vm.update(f.s);
    CHECK(vm.image() != nullptr && vm.image()->w == 16);
}
static void testPaintIsOneCoreCall() {
    UiFixture f;
    OpViewModel vm;
    vm.update(f.s);  vm.update(f.s);
    std::string st;
    const unsigned k = f.coreCalls;
    CHECK(vm.paint(f.s, 3, 2, 200, st));
    CHECK(f.coreCalls == k + 1 && f.mock.dram()[MockDebugIface::kBitmapData + 35] == 200);
    CHECK(vm.image()->rawAt(3, 2) == 200 && CountingIface::offCore() == 0);
}
static void testArmAnswerIsNeverThePreArmSnapshot() {
    UiFixture f;
    DeferredCore d(f);
    OpViewModel vm;
    vm.update(f.s);  d.drain();  vm.update(f.s);  d.drain();  vm.update(f.s);   // the first snapshot, as the test above
    CHECK(vm.haveSnapshot() && !vm.waiting());
    d.drain();
    vm.update(f.s);                                           // the first render lands
    f.mock.clearOpSnapshot();                                 // the next snapshot is distinguishable: empty
    vm.live = false;
    vm.arm(f.s);                                              // queued, not applied: the buffer still holds the old snapshot
    for (int i = 0; i < 3; i++) vm.update(f.s);
    CHECK(vm.waiting() && vm.objects().size() == 4);          // the old answer was NOT taken for the new arm
    d.drain();                                                // the arm applies and resets the request...
    vm.update(f.s);  d.drain();                               // ...a fill answers it...
    vm.update(f.s);                                           // ...and only now is it absorbed (empty: the RAM walk is queued)
    CHECK(!vm.waiting() && vm.snapshotCount() == 4);          // the old list stays up until the walk answers
    d.drain();
    vm.update(f.s);
    CHECK(vm.snapshotCount() == 0);
    CHECK(CountingIface::offCore() == 0);
}
static void testArmedEmptyAsksAgainOnlyWhileRunning() {
    UiFixture f;
    DeferredCore d(f);
    OpViewModel vm;
    f.mock.opReady = false;                                   // the walk has not happened: the core answers "armed"
    vm.update(f.s);  d.drain();  vm.update(f.s);  d.drain();  // arm applied, the request answered -1
    vm.update(f.s, /*running=*/false);
    const size_t idle = d.q.size();
    vm.update(f.s, false);
    CHECK(d.q.size() == idle && vm.waiting());                // paused: no polling
    vm.update(f.s, true);                                     // running: the request is re-stated, one fill queued
    CHECK(d.q.size() == idle + 1);
    d.drain();
    f.mock.opReady = true;
    vm.update(f.s, true);  d.drain();                         // asked again after the frame ran
    vm.update(f.s, true);
    CHECK(!vm.waiting() && vm.haveSnapshot());
}
static void testStepRefillsAfterTheFrame() {
    // The window's "Step a frame" while paused (threaded): a fill queued before the frame runs at the pre-frame
    // serial and sees the snapshot still armed; the serial bump AFTER the frame is what refills it.
    UiFixture f;
    DeferredCore d(f);
    OpViewModel vm;
    vm.update(f.s);  d.drain();  vm.update(f.s);  d.drain();  vm.update(f.s);  d.drain();   // first snapshot, rendered
    vm.update(f.s);
    CHECK(vm.haveSnapshot() && !vm.waiting());
    vm.live = false;
    f.mock.opReady = false;                                   // the walk has not happened: "armed"
    vm.arm(f.s);  d.drain();                                  // the Arm snapshot button (the arm applies at once)
    vm.update(f.s, false);                                    // paused: queues a fill at the current serial
    CHECK(!d.q.empty());
    f.s.requestRefresh();                                     // the OLD order: bump first...
    d.drain();                                                // ...the queued fill runs before the frame: still armed
    f.mock.opReady = true;                                    // the frame ran
    vm.update(f.s, false);  d.drain();  vm.update(f.s, false);
    CHECK(vm.waiting());                                      // nothing was stale at the new serial: stuck (what the fix avoids)
    f.s.requestRefresh();                                     // the fix: bump AFTER the frame
    vm.update(f.s, false);  d.drain();  vm.update(f.s, false);
    CHECK(!vm.waiting() && vm.objects().size() == 4);
}
static void testReleaseWithAnArmQueuedLeavesNoRequest() {
    UiFixture f;
    DeferredCore d(f);
    OpViewModel vm;
    vm.update(f.s);                                           // queues the fill and the arm
    CHECK(f.s.views().count() == 2);
    vm.releaseViews(f.s);                                     // the window closes before the arm applied
    d.drain();                                                // fill, arm (resets the request), then the posted clear
    CHECK(f.s.views().count() == 0);
    vm.releaseViews(f.s);  vm.releaseViews(f.s);              // closed windows call it every frame: nothing to do
    CHECK(d.q.empty());
    vm.update(f.s);  d.drain();  vm.update(f.s);  d.drain();  // reopening works from scratch
    vm.update(f.s);
    CHECK(vm.haveSnapshot() && f.s.views().count() == 2);
}
static void testStaleRenderDroppedAcrossACoreSwitch() {
    UiFixture f;
    DeferredCore d(f);
    OpViewModel vm;
    vm.update(f.s);  d.drain();  vm.update(f.s);  d.drain();  vm.update(f.s);   // absorbed; the first render is queued
    CHECK(vm.haveSnapshot() && !d.q.empty());
    f.s.views().bumpEpoch();                                  // the content switched while the render was queued
    d.drain();                                                // the render runs against the old core's request...
    vm.update(f.s);
    CHECK(!vm.haveSnapshot() && vm.image() == nullptr);       // ...and nothing of the old content is shown
}
static void testGeomAndColourFromBuffers() {
    UiFixture f;
    f.mock.regwin()[0x28] = 0x00; f.mock.regwin()[0x29] = 0x07;            // VMODE
    f.mock.regwin()[0x38] = 0x01; f.mock.regwin()[0x39] = 0x80;            // HDB1
    OpViewModel vm;
    vm.update(f.s);  vm.update(f.s);
    OpScreenGeom g = vm.readGeom(320, 240);
    CHECK(g.vmode == 0x0007 && g.hdb1 == 0x180 && g.frameW == 320);
    std::vector<uint32_t> table;
    CHECK(f.s.buildCryTable(table));
    for (OpColorMode m : {OpColorMode::Cry, OpColorMode::Rgb16, OpColorMode::Mixed})
        for (unsigned v : {0x0000u, 0x1234u, 0x1235u, 0xFFFFu})
            CHECK(opColorToRgb(&table, m, v) == opColorToRgb(f.mock.iface(), m, v));
}

// ---- PNG export ---------------------------------------------------------------------------------------------------------------
static void testPngExport() {
    MockDebugIface m;
    OpObject o = mockObject(m, 0);
    OpImage img;
    std::string err;
    CHECK(opRender(m.iface(), o, OpRenderOptions(), img, err));
    std::filesystem::path dir = std::filesystem::temp_directory_path() / "vj_debug_op_test";
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    std::string path = (dir / "object.png").string();
    CHECK(opSavePng(img, path, err));
    SDL_Surface* png = SDL_LoadPNG(path.c_str());
    CHECK(png != nullptr);
    if (png) {
        SDL_Surface* rgba = SDL_ConvertSurface(png, SDL_PIXELFORMAT_RGBA32);
        SDL_DestroySurface(png);
        CHECK(rgba != nullptr);
        if (rgba) {
            CHECK_EQ(rgba->w, img.w);
            CHECK_EQ(rgba->h, img.h);
            bool same = true;
            for (int y = 0; y < img.h && same; y++) {
                const uint8_t* row = (const uint8_t*)rgba->pixels + (size_t)y * rgba->pitch;
                for (int x = 0; x < img.w; x++) {
                    const uint8_t* want = img.rgbaAt(x, y);
                    // Fully transparent pixels may lose their colour in a PNG round trip; alpha must survive.
                    if (row[x * 4 + 3] != want[3]) same = false;
                    if (want[3] && std::memcmp(row + x * 4, want, 4) != 0) same = false;
                }
            }
            CHECK(same);
            SDL_DestroySurface(rgba);
        }
    }
    // Failure paths.
    OpImage empty;
    CHECK(!opSavePng(empty, path, err) && !err.empty());
    CHECK(!opSavePng(img, (dir / "no_such_dir" / "x.png").string(), err));
    std::filesystem::remove_all(dir, ec);
}

static void testSyntheticPreviewAfterContentSwitch() {
    UiFixture fx;
    OpViewModel vm;
    vm.update(fx.s, false); // records the first game's epoch
    vm.releaseViews(fx.s);
    vm.clear();
    const auto oldEpoch = fx.s.views().epoch();
    fx.s.setIface(nullptr);
    fx.s.setIface(fx.counted.iface());
    fx.s.setContentLoaded(true);
    CHECK(fx.s.views().epoch() != oldEpoch);
    Bm b;
    b.data = MockDebugIface::kBitmapData;
    b.height = 4; b.iwidth = b.dwidth = 2;
    const unsigned arms = vm.armCount();
    vm.showPreview(fx.s, b.decode(), {});
    vm.update(fx.s, false);
    CHECK_EQ(vm.objects().size(), 1);
    CHECK(vm.selectedObject() && vm.selectedObject()->data == b.data);
    CHECK(vm.image() != nullptr);
    CHECK(!vm.waiting());
    CHECK_EQ(vm.armCount(), arms);
    // A subsequent content switch must still invalidate the synthetic object.
    fx.s.setIface(nullptr);
    fx.s.setIface(fx.counted.iface());
    fx.s.setContentLoaded(true);
    vm.update(fx.s, false);
    CHECK(vm.objects().empty());
    CHECK(CountingIface::offCore() == 0);
}

int main() {
    testSyntheticPreviewAfterContentSwitch();
    testDecodeFields();
    testDecodeDepths();
    testDecodeOtherTypes();
    testDecodeScaled();
    testDecodeEdgeCases();
    testDecodeMatchesOpListDump();
    testScreenMapping();
    testRenderMock8bpp();
    testRenderMockScaled16();
    testRenderIndexedDepths();
    testRender16And24();
    testRenderPitchFirstpixReflect();
    testRenderLimitsAndErrors();
    testPixelPatch();
    testViewModelLifecycle();
    testViewModelFollowLinks();
    testViewModelPaint();
    testWalkList();
    testViewModelGeom();
    testOpStepGate();
    testViewModelNeverTouchesTheCore();
    testRenderLeavesTheUiThread();
    testPaintIsOneCoreCall();
    testGeomAndColourFromBuffers();
    testArmAnswerIsNeverThePreArmSnapshot();
    testArmedEmptyAsksAgainOnlyWhileRunning();
    testStaleRenderDroppedAcrossACoreSwitch();
    testStepRefillsAfterTheFrame();
    testReleaseWithAnArmQueuedLeavesNoRequest();
    testPngExport();
    std::printf("debug_op_test: %d checks, %d failed\n", g_checks, g_fail);
    return g_fail ? 1 : 0;
}
