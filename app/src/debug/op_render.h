// Renders one Object Processor bitmap object to an RGBA image from DRAM (through the core's
// side-effect-free peek), and maps a preview pixel back to the bits that hold it so the pixel editor
// can write it.  Pure: no ImGui, no GL; PNG export goes through SDL_SavePNG.
//
// What the preview does (all of it mirrors src/tom/op.c, OPProcessFixedBitmap):
//   1/2/4/8 bpp  pixel value (MSB first in the phrase) -> CLUT entry; for 1/2/4 bpp the entry is
//                (INDEX << 1) masked to the bits the pixel leaves free, ORed with the value; 8 bpp has no
//                INDEX.  CLUT = 256 big-endian words at $F00400.
//   16 bpp       the word is the physical colour itself (CRY, or RGB16 / mixed per the toggle)
//   24 bpp       32 bits per pixel, bytes G R x B (JTRM "LBUF": red is the low byte of the first word)
//   TRANS        raw value 0 (all-zero word / pixel) is transparent -> alpha 0
//   REFLECT      the picture is mirrored (XPOS is the right edge then)
//   FIRSTPIX     the first phrase of every line starts that many pixels in
//   RMW          not blended; flagged in the object's fields only
// Scaled objects are drawn unscaled (the scale is reported in the object's fields).
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "op_decode.h"
#include "vj_debug_iface.h"

namespace vj {

enum class OpColorMode {
    Auto,     // from VMODE: RGB16 / direct -> RGB16, CRY with VARMOD -> mixed (bit 0 picks RGB16), else CRY
    Cry,
    Rgb16,    // Jaguar RGB 5-5-6: RRRRR BBBBB GGGGGG
    Mixed,    // per pixel: bit 0 set -> RGB16, clear -> CRY (VARMOD)
};

struct OpRenderOptions {
    OpColorMode color = OpColorMode::Auto;
    bool honorTrans = true;      // TRANS objects get alpha 0 where the raw value is 0
    bool honorReflect = true;    // mirror REFLECT objects
    unsigned maxW = 4096;        // the preview is clamped (garbage lists are common)
    unsigned maxH = 1024;
    unsigned maxPixels = 4u * 1024u * 1024u;
};

struct OpImage {
    int w = 0, h = 0;
    std::vector<uint8_t> rgba;        // w * h * 4, bytes R G B A (SDL_PIXELFORMAT_RGBA32)
    std::vector<uint32_t> raw;        // w * h: the pixel's stored value (CLUT-less: 1-8 bpp value, 16 bpp word, 24 bpp 0xRRGGBB)
    std::vector<uint16_t> clut;       // the 256 entries read ($F00400), empty for 16/24 bpp
    OpColorMode mode = OpColorMode::Cry;   // the mode actually used
    bool mirrored = false;            // REFLECT applied: image x is the source x mirrored
    bool truncated = false;           // width / height / pixel count clamped
    bool unreadable = false;          // some source bytes could not be read (shown as 0)
    unsigned srcW = 0, srcH = 0;      // the full size before clamping
    std::string note;                 // one line for the UI: scaled-object notice, truncation, ...

    uint32_t rawAt(int x, int y) const { return raw[(size_t)y * (size_t)w + (size_t)x]; }
    const uint8_t* rgbaAt(int x, int y) const { return &rgba[((size_t)y * (size_t)w + (size_t)x) * 4]; }
};

// Resolves Auto against VMODE ($F00028).  Null iface or a failed peek: CRY.
OpColorMode opResolveColorMode(const vj_debug_iface* iface, OpColorMode requested);
// Physical colour -> 0x00RRGGBB, the way the core's renderer converts it (tom.c RGB16ToRGB32 / CRY16ToRGB32).
uint32_t opColorToRgb(const vj_debug_iface* iface, OpColorMode mode, unsigned v16);
// The same maths from a prebuilt 65536-entry CRY table (DebugSession::buildCryTable, 0x00RRGGBB), for the UI thread:
// a null or wrong-sized table reads CRY as black.
uint32_t opColorToRgb(const std::vector<uint32_t>* cryTable, OpColorMode mode, unsigned v16);

// Renders `o`.  False (with `err`) when it is not a drawable bitmap or the iface cannot read memory.
bool opRender(const vj_debug_iface* iface, const OpObject& o, const OpRenderOptions& opt, OpImage& out, std::string& err);

// Where preview pixel (x, y) lives.  `addr` is the first byte, `bytes` how many hold the pixel
// (1 for 1-8 bpp, 2 for 16, 4 for 24), `shift`/`mask` select the pixel inside a 1-8 bpp byte.
struct OpPixelLoc {
    bool ok = false;
    unsigned addr = 0;
    unsigned bytes = 0;
    unsigned shift = 0;
    unsigned mask = 0;       // value mask, 1-8 bpp: (1 << bpp) - 1
    unsigned bpp = 0;
};
OpPixelLoc opPixelLocation(const OpObject& o, const OpImage& img, int x, int y);
unsigned opPixelMax(const OpObject& o);   // largest storable value: 1, 3, 15, 255, 65535, 0xFFFFFF

// The bytes to write to store `value` at (x, y): reads the current byte(s) (sub-byte depths keep their
// neighbours).  False if the pixel is outside the image or the read failed.
struct OpPixelPatch {
    unsigned addr = 0;
    unsigned n = 0;
    uint8_t bytes[4] = {0, 0, 0, 0};
};
bool opPixelPatch(const vj_debug_iface* iface, const OpObject& o, const OpImage& img, int x, int y, uint32_t value,
                  OpPixelPatch& out);

// Writes the image as a PNG (RGBA).  False with `err` on failure.
bool opSavePng(const OpImage& img, const std::string& path, std::string& err);

}  // namespace vj
