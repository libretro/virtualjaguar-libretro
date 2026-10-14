#include "op_decode.h"

#include <cstdio>

namespace vj {

namespace {
const unsigned kBpp[8] = {1, 2, 4, 8, 16, 24, 0, 0};
// op.c: firstPix &= 0x3E first, then per depth.  1bpp 0x3E, 2bpp 0x3C, 4bpp 0x38, 8bpp 0x30, 16bpp 0x30, 24bpp 0x20.
const unsigned kFirstMask[8] = {0x3E, 0x3C, 0x38, 0x30, 0x30, 0x20, 0, 0};
}  // namespace

unsigned opBitsPerPixel(unsigned d) { return d < 8 ? kBpp[d] : 0; }
unsigned opFirstPixMask(unsigned d) { return d < 8 ? kFirstMask[d] : 0; }

const char* opTypeName(OpType t) {
    switch (t) {
        case OpType::Bitmap: return "Bitmap";
        case OpType::Scaled: return "Scaled bitmap";
        case OpType::Gpu: return "GPU";
        case OpType::Branch: return "Branch";
        case OpType::Stop: return "Stop";
        default: return "Unknown";
    }
}
const char* opTypeShort(OpType t) {
    switch (t) {
        case OpType::Bitmap: return "BIT";
        case OpType::Scaled: return "SCB";
        case OpType::Gpu: return "GPU";
        case OpType::Branch: return "BRA";
        case OpType::Stop: return "STOP";
        default: return "???";
    }
}
const char* opDepthName(unsigned d) {
    switch (d) {
        case 0: return "1 bpp";
        case 1: return "2 bpp";
        case 2: return "4 bpp";
        case 3: return "8 bpp";
        case 4: return "16 bpp";
        case 5: return "24 bpp";
        case 6: return "invalid (6)";
        default: return "invalid (7)";
    }
}
const char* opBranchCondName(unsigned cc) {
    switch (cc) {
        case 0: return "YPOS == VC or YPOS == $7FF";
        case 1: return "YPOS > VC";
        case 2: return "YPOS < VC";
        case 3: return "OP flag (OBF) set";
        case 4: return "second half of the line (HC10)";
        default: return "undefined";
    }
}

OpObject opDecode(unsigned address, unsigned phrases, uint64_t p0, uint64_t p1, uint64_t p2) {
    OpObject o;
    o.address = address;
    o.phrases = phrases;
    o.p0 = p0;
    o.p1 = p1;
    o.p2 = p2;
    o.rawType = (unsigned)(p0 & 7);
    o.type = o.rawType <= 4 ? (OpType)o.rawType : OpType::Unknown;
    o.ypos = (unsigned)((p0 >> 3) & 0x7FF);

    switch (o.type) {
        case OpType::Bitmap:
        case OpType::Scaled: {
            o.height = (unsigned)((p0 >> 14) & 0x3FF);
            o.link = (unsigned)((p0 >> 21) & 0x3FFFF8u);   // bits 24-42 land on address bits 3-21
            o.data = (unsigned)((p0 >> 40) & 0xFFFFF8u);   // bits 43-63 land on address bits 3-23
            o.havePhrase1 = phrases >= 2;
            o.havePhrase2 = o.type == OpType::Scaled && phrases >= 3;
            if (o.havePhrase1) {
                o.xpos = ((int)((p1 & 0xFFF) << 20)) >> 20;   // sign-extend 12 bits
                o.depthCode = (unsigned)((p1 >> 12) & 7);
                o.bpp = opBitsPerPixel(o.depthCode);
                o.pitch = (unsigned)((p1 >> 15) & 7);
                o.dwidth = (unsigned)((p1 >> 18) & 0x3FF);
                o.iwidth = (unsigned)((p1 >> 28) & 0x3FF);
                o.iwidthEff = o.iwidth ? o.iwidth : 1;
                o.index = (unsigned)((p1 >> 38) & 0x7F);
                o.reflect = (p1 >> 45) & 1;
                o.rmw = (p1 >> 46) & 1;
                o.trans = (p1 >> 47) & 1;
                o.release = (p1 >> 48) & 1;
                o.firstpix = (unsigned)((p1 >> 49) & 0x3F);
                if (o.bpp) {
                    // A 24 bpp pixel occupies 32 bits of the phrase (two per phrase), so FIRSTPIX counts in 32s there.
                    unsigned storeBits = o.bpp == 24 ? 32 : o.bpp;
                    o.pixelsPerPhrase = 64 / storeBits;
                    unsigned mask = opFirstPixMask(o.depthCode);
                    o.skipPixels = (o.firstpix & mask) / storeBits;
                    // INDEX supplies the top bits of the CLUT address for 1-4 bpp only (op.c: index = INDEX << 1,
                    // masked to the bits the pixel value leaves free).  8 bpp uses the pixel value alone.
                    unsigned idx = (o.index << 1) & 0xFE;
                    o.clutBase = o.bpp == 1 ? idx : o.bpp == 2 ? (idx & 0xFC) : o.bpp == 4 ? (idx & 0xF0) : 0;
                    unsigned total = o.iwidthEff * o.pixelsPerPhrase;
                    o.widthPixels = total > o.skipPixels ? total - o.skipPixels : 0;
                }
            }
            if (o.havePhrase2) {
                o.hscale = (unsigned)(p2 & 0xFF);
                o.vscale = (unsigned)((p2 >> 8) & 0xFF);
                o.remainder = (unsigned)((p2 >> 16) & 0xFF);
                o.scaledWidth = (o.widthPixels * o.hscale) >> 5;
            }
            if (!o.havePhrase1) o.warning = "second phrase not captured";
            else if (o.type == OpType::Scaled && !o.havePhrase2) o.warning = "scaling phrase not captured";
            else if (!o.bpp) o.warning = "invalid DEPTH";
            else if (o.height == 0) o.warning = "HEIGHT is 0: the OP draws nothing";
            else if (o.iwidth == 0) o.warning = "IWIDTH is 0 (illegal): the emulator draws one phrase";
            break;
        }
        case OpType::Gpu:
            o.gpuData = (p0 >> 14);
            break;
        case OpType::Branch:
            o.cc = (unsigned)((p0 >> 14) & 7);
            o.link = (unsigned)((p0 >> 21) & 0x3FFFF8u);
            if (o.cc > 4) o.warning = "undefined branch condition";
            break;
        case OpType::Stop:
            o.stopData = (p0 >> 3);
            o.stopInterrupt = (p0 >> 3) & 1;
            break;
        default:
            o.warning = "undefined object type";
            break;
    }
    return o;
}

bool opReadObject(OpMemReader rd, void* ctx, unsigned addr, OpObject& out) {
    addr &= 0xFFFFF8u;
    uint8_t b[8];
    auto phrase = [&](unsigned a, uint64_t& v) {
        if (!rd(ctx, a & 0xFFFFFFu, b, 8)) return false;
        v = 0;
        for (int i = 0; i < 8; i++) v = (v << 8) | b[i];
        return true;
    };
    uint64_t p0 = 0, p1 = 0, p2 = 0;
    if (!phrase(addr, p0)) return false;
    unsigned type = (unsigned)(p0 & 7), phrases = 1;
    if (type == 0 || type == 1) {
        if (!phrase(addr + 8, p1)) return false;
        phrases = 2;
        if (type == 1) {
            if (!phrase(addr + 16, p2)) return false;
            phrases = 3;
        }
    }
    out = opDecode(addr, phrases, p0, p1, p2);
    return true;
}

std::vector<OpObject> opWalkList(OpMemReader rd, void* ctx, unsigned head, unsigned maxObjects) {
    std::vector<OpObject> out;
    std::vector<unsigned> todo;
    std::vector<unsigned> seen;
    if (!rd || head == 0) return out;
    todo.push_back(head & 0xFFFFF8u);
    auto fold = [](unsigned link) {   // op.c: "memory access is mirrored in the first 8MB"
        if (link > 0x1FFFFF && link < 0x800000) link &= 0xFF1FFFFFu;
        return link;
    };
    while (!todo.empty() && out.size() < maxObjects) {
        unsigned a = todo.back();
        todo.pop_back();
        bool dup = false;
        for (unsigned s : seen)
            if (s == a) { dup = true; break; }
        if (dup) continue;
        seen.push_back(a);
        OpObject o;
        if (!opReadObject(rd, ctx, a, o)) continue;
        o.fromRam = true;
        out.push_back(o);
        switch (o.type) {
            case OpType::Bitmap:
            case OpType::Scaled:
                if (o.link) todo.push_back(fold(o.link));
                break;
            case OpType::Gpu: todo.push_back(a + 8); break;
            case OpType::Branch:
                // Depth first with the fall-through visited first: push it last.
                if (o.link) todo.push_back(fold(o.link));
                todo.push_back(a + 8);
                break;
            default: break;   // stop (and undefined types) end the path
        }
    }
    return out;
}

OpObject opDecode(const vj_debug_op_obj& s) {
    uint64_t p0 = ((uint64_t)s.p0_hi << 32) | s.p0_lo;
    uint64_t p1 = ((uint64_t)s.p1_hi << 32) | s.p1_lo;
    uint64_t p2 = ((uint64_t)s.p2_hi << 32) | s.p2_lo;
    return opDecode(s.address, s.phrases, p0, p1, p2);
}

std::vector<OpObject> opDecodeAll(const vj_debug_op_obj* objs, int n) {
    std::vector<OpObject> v;
    if (!objs || n <= 0) return v;
    v.reserve((size_t)n);
    for (int i = 0; i < n; i++) v.push_back(opDecode(objs[i]));
    return v;
}

std::string opSummary(const OpObject& o) {
    char b[160];
    switch (o.type) {
        case OpType::Bitmap:
        case OpType::Scaled:
            if (!o.havePhrase1) {
                std::snprintf(b, sizeof b, "%s @%06X y=%u h=%u (phrase 1 missing)", opTypeShort(o.type), o.address, o.ypos, o.height);
            } else {
                std::snprintf(b, sizeof b, "%s @%06X y=%u h=%u x=%d %ubpp %upx", opTypeShort(o.type), o.address, o.ypos,
                              o.height, o.xpos, o.bpp, o.widthPixels);
            }
            break;
        case OpType::Branch:
            std::snprintf(b, sizeof b, "BRA @%06X y=%u cc=%u link=%06X", o.address, o.ypos, o.cc, o.link);
            break;
        case OpType::Gpu: std::snprintf(b, sizeof b, "GPU @%06X y=%u", o.address, o.ypos); break;
        case OpType::Stop: std::snprintf(b, sizeof b, "STOP @%06X", o.address); break;
        default: std::snprintf(b, sizeof b, "??? @%06X type=%u", o.address, o.rawType); break;
    }
    return b;
}

// ---- screen mapping ---------------------------------------------------------------------------------------
unsigned opTopVisible(const OpScreenGeom& g) {
    const unsigned fallback = g.pal() ? 67u : 31u;     // tom.c DEFAULT_TOP_VISIBLE_VC(_PAL)
    if (g.vdb == 0 || g.vdb == 38) return fallback;
    if (g.vp == 0 || g.vdb > g.vp) return fallback;
    return g.vdb;
}
unsigned opLeftVisibleHC(const OpScreenGeom& g) {
    return g.pal() ? (208u - 16u + 12u) : (208u - 16u - 4u);   // DEFAULT_LEFT_VISIBLE_HC(_PAL): 204 / 188
}

OpScreenRect opScreenRect(const OpObject& o, const OpScreenGeom& g) {
    OpScreenRect r;
    if (!o.drawable()) return r;
    unsigned pw = g.pwidth();
    unsigned scale = pw >= 8 ? pw / 4 : 1;
    // Line buffer pixel 0 is drawn at (HDB1 - leftHC) / pwidth, in framebuffer columns (tom.c startPos).
    int start = ((int)g.hdb1 - (int)opLeftVisibleHC(g)) / (int)pw;
    float wpx = (float)(o.type == OpType::Scaled && o.havePhrase2 ? o.scaledWidth : o.widthPixels);
    float x0 = (float)(o.xpos + start);
    if (o.reflect) x0 -= wpx - 1;           // XPOS marks the right edge of a reflected bitmap (op.c)
    r.x = x0 * (float)scale;
    r.w = wpx * (float)scale;
    // Row r shows half-line top + 2r.
    r.y = ((float)o.ypos - (float)opTopVisible(g)) * 0.5f;
    float lines = (float)o.height;
    if (o.type == OpType::Scaled && o.havePhrase2 && o.vscale) lines = lines * (float)o.vscale / 32.0f;
    r.h = lines;
    r.valid = true;
    return r;
}

}  // namespace vj
