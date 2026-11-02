#include "op_render.h"

#include <algorithm>
#include <cstdio>
#include <cstring>

#include <SDL3/SDL.h>

namespace vj {

namespace {

const unsigned kVmode = 0xF00028;

bool peekN(const vj_debug_iface* i, unsigned addr, uint8_t* dst, unsigned n) {
    if (!i || !i->peek) return false;
    return i->peek(addr & 0xFFFFFF, dst, n) == (int)n;
}

// Reads the phrases of one source line into `buf` (iwidth * 8 bytes, phrase k at k*8).  Unreadable
// phrases stay zero; returns false if any failed.
bool readLinePhrases(const vj_debug_iface* iface, unsigned lineAddr, unsigned iwidth, unsigned pitch, uint8_t* buf) {
    bool ok = true;
    if (pitch == 1) {
        if (peekN(iface, lineAddr, buf, iwidth * 8)) return true;
    } else if (pitch == 0) {
        uint8_t one[8] = {0};
        bool g = peekN(iface, lineAddr, one, 8);
        for (unsigned k = 0; k < iwidth; k++) std::memcpy(buf + k * 8, one, 8);
        return g;
    }
    for (unsigned k = 0; k < iwidth; k++) {
        uint8_t* d = buf + k * 8;
        if (!peekN(iface, lineAddr + k * pitch * 8, d, 8)) {
            std::memset(d, 0, 8);
            ok = false;
        }
    }
    return ok;
}

}  // namespace

OpColorMode opResolveColorMode(const vj_debug_iface* iface, OpColorMode req) {
    if (req != OpColorMode::Auto) return req;
    uint8_t b[2];
    if (!peekN(iface, kVmode, b, 2)) return OpColorMode::Cry;
    unsigned v = ((unsigned)b[0] << 8) | b[1];
    unsigned mode = (v >> 1) & 3;
    if (mode == 3 || mode == 2) return OpColorMode::Rgb16;
    if (mode == 0 && (v & 0x100)) return OpColorMode::Mixed;
    return OpColorMode::Cry;
}

namespace {
// tom.c TOMFillLookupTables: RRRRR BBBBB GGGGGG
uint32_t rgb16ToRgb(unsigned x) {
    return ((x & 0xF800u) << 8) | ((x & 0x003Fu) << 10) | ((x & 0x07C0u) >> 3);
}
}  // namespace

uint32_t opColorToRgb(const std::vector<uint32_t>* cryTable, OpColorMode mode, unsigned v) {
    v &= 0xFFFF;
    auto cry = [&](unsigned x) -> uint32_t {
        return cryTable && cryTable->size() == 0x10000 ? ((*cryTable)[x] & 0xFFFFFFu) : 0u;
    };
    switch (mode) {
        case OpColorMode::Rgb16: return rgb16ToRgb(v);
        case OpColorMode::Mixed: return (v & 1) ? rgb16ToRgb(v) : cry(v);
        default: return cry(v);
    }
}

uint32_t opColorToRgb(const vj_debug_iface* iface, OpColorMode mode, unsigned v) {
    v &= 0xFFFF;
    auto cry = [&](unsigned x) -> uint32_t {
        return iface && iface->cry_to_rgb ? (iface->cry_to_rgb(x) & 0xFFFFFFu) : 0u;
    };
    switch (mode) {
        case OpColorMode::Rgb16: return rgb16ToRgb(v);
        case OpColorMode::Mixed: return (v & 1) ? rgb16ToRgb(v) : cry(v);
        default: return cry(v);
    }
}

unsigned opPixelMax(const OpObject& o) {
    switch (o.bpp) {
        case 1: return 1;
        case 2: return 3;
        case 4: return 15;
        case 8: return 255;
        case 16: return 0xFFFF;
        case 24: return 0xFFFFFF;
        default: return 0;
    }
}

bool opRender(const vj_debug_iface* iface, const OpObject& o, const OpRenderOptions& opt, OpImage& out, std::string& err) {
    out = OpImage();
    if (!iface || !iface->peek) { err = "no core interface"; return false; }
    if (!o.isBitmap()) { err = "not a bitmap object"; return false; }
    if (!o.havePhrase1) { err = "the object's second phrase was not captured"; return false; }
    if (!o.bpp) { err = "invalid DEPTH"; return false; }
    if (o.height == 0) { err = "HEIGHT is 0: nothing to draw"; return false; }

    const unsigned ppp = o.pixelsPerPhrase;
    const unsigned fullW = o.widthPixels;
    const unsigned fullH = o.height;
    if (fullW == 0) { err = "zero-width image"; return false; }

    unsigned w = std::min(fullW, opt.maxW);
    unsigned h = std::min(fullH, opt.maxH);
    if ((uint64_t)w * h > opt.maxPixels) h = std::max(1u, opt.maxPixels / w);
    out.srcW = fullW;
    out.srcH = fullH;
    out.truncated = w < fullW || h < fullH;
    out.w = (int)w;
    out.h = (int)h;
    out.mode = opResolveColorMode(iface, opt.color);
    out.mirrored = o.reflect && opt.honorReflect;
    out.rgba.assign((size_t)w * h * 4, 0);
    out.raw.assign((size_t)w * h, 0);

    // CLUT for the indexed depths.
    if (o.bpp <= 8) {
        uint8_t cb[512];
        if (!peekN(iface, opt.clutAddress, cb, 512)) { std::memset(cb, 0, sizeof cb); out.unreadable = true; }
        out.clut.resize(256);
        for (unsigned i = 0; i < 256; i++) out.clut[i] = (uint16_t)((cb[i * 2] << 8) | cb[i * 2 + 1]);
    }

    // Phrases per line that the clamped width needs (the first one starts skipPixels in).
    unsigned needPixels = o.skipPixels + w;
    unsigned phrasesNeeded = std::min(o.iwidthEff, (needPixels + ppp - 1) / ppp);
    std::vector<uint8_t> line((size_t)phrasesNeeded * 8);

    for (unsigned y = 0; y < h; y++) {
        if (opt.rowStrideBytes) {
            // The final phrase can be partial. Only fetch the bytes belonging to this raw row.
            std::fill(line.begin(), line.end(), 0);
            const uint64_t addr = (uint64_t)o.data + (uint64_t)y * opt.rowStrideBytes;
            const unsigned n = std::min((unsigned)line.size(), opt.rowStrideBytes);
            if (addr >= opt.sourceEnd || n > opt.sourceEnd - addr ||
                !peekN(iface, (unsigned)addr, line.data(), n)) out.unreadable = true;
        } else {
            unsigned lineAddr = (o.data + y * o.dwidth * 8) & 0xFFFFFF;
            if (!readLinePhrases(iface, lineAddr, phrasesNeeded, o.pitch, line.data())) out.unreadable = true;
        }
        for (unsigned x = 0; x < w; x++) {
            unsigned s = x + o.skipPixels;
            unsigned k = s / ppp, p = s % ppp;
            const uint8_t* ph = &line[(size_t)k * 8];
            uint32_t rawv;
            uint32_t rgb;
            bool transparent = false;
            if (o.bpp <= 8) {
                unsigned bitpos = p * o.bpp;
                unsigned byte = ph[bitpos >> 3];
                unsigned v = (byte >> (8 - o.bpp - (bitpos & 7))) & ((1u << o.bpp) - 1);
                rawv = v;
                transparent = (v == 0);
                unsigned pal = (o.clutBase | v) & 0xFF;
                rgb = opColorToRgb(iface, out.mode, out.clut[pal]);
            } else if (o.bpp == 16) {
                unsigned v = ((unsigned)ph[p * 2] << 8) | ph[p * 2 + 1];
                rawv = v;
                transparent = (v == 0);
                rgb = opColorToRgb(iface, out.mode, v);
            } else {   // 24 bpp: bytes G R x B
                const uint8_t* q = ph + p * 4;
                unsigned g = q[0], r = q[1], b = q[3];
                rawv = (r << 16) | (g << 8) | b;
                transparent = (q[0] | q[1] | q[2] | q[3]) == 0;
                rgb = rawv;
            }
            unsigned dx = out.mirrored ? (w - 1 - x) : x;
            size_t at = (size_t)y * w + dx;
            out.raw[at] = rawv;
            uint8_t* px = &out.rgba[at * 4];
            px[0] = (uint8_t)(rgb >> 16);
            px[1] = (uint8_t)(rgb >> 8);
            px[2] = (uint8_t)rgb;
            px[3] = (o.trans && opt.honorTrans && transparent) ? 0 : 255;
        }
    }

    char nb[200];
    nb[0] = 0;
    if (o.type == OpType::Scaled && o.havePhrase2) {
        std::snprintf(nb, sizeof nb, "Scaled object (HSCALE %.2f, VSCALE %.2f): shown unscaled, %u px would draw %u px wide. ",
                      o.hscale / 32.0, o.vscale / 32.0, o.widthPixels, o.scaledWidth);
        out.note = nb;
    }
    if (out.truncated) {
        std::snprintf(nb, sizeof nb, "Preview clamped to %dx%d of %ux%u. ", out.w, out.h, fullW, fullH);
        out.note += nb;
    }
    if (out.unreadable) out.note += "Some data could not be read. ";
    while (!out.note.empty() && out.note.back() == ' ') out.note.pop_back();
    return true;
}

OpPixelLoc opPixelLocation(const OpObject& o, const OpImage& img, int x, int y) {
    OpPixelLoc L;
    if (!o.isBitmap() || !o.bpp || x < 0 || y < 0 || x >= img.w || y >= img.h) return L;
    unsigned sx = (unsigned)(img.mirrored ? (img.w - 1 - x) : x);
    unsigned s = sx + o.skipPixels;
    unsigned k = s / o.pixelsPerPhrase, p = s % o.pixelsPerPhrase;
    unsigned phrase = (o.data + (unsigned)y * o.dwidth * 8 + k * o.pitch * 8) & 0xFFFFFF;
    L.bpp = o.bpp;
    if (o.bpp <= 8) {
        unsigned bitpos = p * o.bpp;
        L.addr = (phrase + (bitpos >> 3)) & 0xFFFFFF;
        L.bytes = 1;
        L.shift = 8 - o.bpp - (bitpos & 7);
        L.mask = (1u << o.bpp) - 1;
    } else if (o.bpp == 16) {
        L.addr = (phrase + p * 2) & 0xFFFFFF;
        L.bytes = 2;
    } else {
        L.addr = (phrase + p * 4) & 0xFFFFFF;
        L.bytes = 4;
    }
    L.ok = true;
    return L;
}

bool opPixelPatch(const vj_debug_iface* iface, const OpObject& o, const OpImage& img, int x, int y, uint32_t value,
                  OpPixelPatch& out) {
    OpPixelLoc L = opPixelLocation(o, img, x, y);
    if (!L.ok) return false;
    out = OpPixelPatch();
    out.addr = L.addr;
    out.n = L.bytes;
    if (L.bpp <= 8) {
        uint8_t cur;
        if (!peekN(iface, L.addr, &cur, 1)) return false;
        unsigned v = value & L.mask;
        out.bytes[0] = (uint8_t)((cur & ~(L.mask << L.shift)) | (v << L.shift));
    } else if (L.bpp == 16) {
        out.bytes[0] = (uint8_t)(value >> 8);
        out.bytes[1] = (uint8_t)value;
    } else {   // 24 bpp: G R x B; the unused byte keeps its value
        uint8_t cur[4];
        if (!peekN(iface, L.addr, cur, 4)) return false;
        out.bytes[0] = (uint8_t)(value >> 8);    // G
        out.bytes[1] = (uint8_t)(value >> 16);   // R
        out.bytes[2] = cur[2];
        out.bytes[3] = (uint8_t)value;           // B
    }
    return true;
}

bool opSavePng(const OpImage& img, const std::string& path, std::string& err) {
    if (img.w <= 0 || img.h <= 0 || img.rgba.size() < (size_t)img.w * (size_t)img.h * 4) {
        err = "nothing to save";
        return false;
    }
    SDL_Surface* s = SDL_CreateSurfaceFrom(img.w, img.h, SDL_PIXELFORMAT_RGBA32,
                                           const_cast<uint8_t*>(img.rgba.data()), img.w * 4);
    if (!s) { err = SDL_GetError(); return false; }
    bool ok = SDL_SavePNG(s, path.c_str());
    if (!ok) err = SDL_GetError();
    SDL_DestroySurface(s);
    return ok;
}

}  // namespace vj
