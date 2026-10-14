// Object Processor object decoder -- pure, no ImGui, no SDL, no core.
//
// Decodes the raw phrases the core captured mid-list (vj_debug_op_obj, taken BEFORE the OP rewrites
// HEIGHT / DATA / REMAINDER) into JTRM fields.  Layouts: JTRM Rev 8 "Object definitions" (pp.18-20)
// and docs/jtrm-object-processor.md; where the emulator deliberately differs from the manual that
// is noted on the field (STOP bit 3, GPU YPOS, branch CC width, iwidth == 0).
//
//   phrase 0  (all types)   TYPE 0-2, YPOS 3-13
//     bitmap/scaled         HEIGHT 14-23, LINK 24-42 (bits 21..3 of the next object address), DATA 43-63
//     GPU                   DATA 14-63 (no LINK: execution continues at the next phrase)
//     branch                CC 14-16 (three bits, the manual prints two), LINK 24-42
//     stop                  DATA 3-63
//   phrase 1  (bitmap/scaled)  XPOS 0-11 (signed), DEPTH 12-14, PITCH 15-17, DWIDTH 18-27, IWIDTH 28-37,
//                              INDEX 38-44, REFLECT 45, RMW 46, TRANS 47, RELEASE 48, FIRSTPIX 49-54
//   phrase 2  (scaled)         HSCALE 0-7, VSCALE 8-15, REMAINDER 16-23   (3.5 fixed point; $20 == 1.0)
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "vj_debug_iface.h"

namespace vj {

enum class OpType { Bitmap = 0, Scaled = 1, Gpu = 2, Branch = 3, Stop = 4, Unknown = 7 };

struct OpObject {
    // ---- as captured --------------------------------------------------------------------------
    unsigned address = 0;       // bus address of the first phrase
    unsigned rawType = 0;       // p0 bits 0-2
    OpType type = OpType::Unknown;
    unsigned phrases = 0;       // phrases captured by the core
    uint64_t p0 = 0, p1 = 0, p2 = 0;
    bool havePhrase1 = false;   // p1 / p2 were captured (bitmap needs 2, scaled 3)
    bool havePhrase2 = false;

    // ---- phrase 0 -------------------------------------------------------------------------------
    unsigned ypos = 0;          // 11 bits, in half-lines
    unsigned height = 0;        // 10 bits (bitmap / scaled): data lines still to draw
    unsigned link = 0;          // byte address of the next object (bitmap / scaled / branch)
    unsigned data = 0;          // byte address of the pixel data (bitmap / scaled)
    uint64_t gpuData = 0;       // GPU object: bits 14-63
    uint64_t stopData = 0;      // stop object: bits 3-63
    bool stopInterrupt = false; // the emulator raises the CPU interrupt only when bit 3 is set (op.c)
    unsigned cc = 0;            // branch condition (3 bits)

    // ---- phrase 1 (bitmap / scaled) --------------------------------------------------------------
    int xpos = 0;               // sign-extended 12 bit
    unsigned depthCode = 0;     // DEPTH field 0..7
    unsigned bpp = 0;           // 1,2,4,8,16,24; 0 for the invalid codes 6 / 7
    unsigned pitch = 0;         // PITCH field (phrases between fetches; 0 repeats the phrase)
    unsigned dwidth = 0;        // phrases per line of data
    unsigned iwidth = 0;        // phrases drawn per line, as written (0 is illegal)
    unsigned iwidthEff = 0;     // what the emulator draws (op.c treats 0 as 1)
    unsigned index = 0;         // INDEX field (7 bits)
    bool reflect = false, rmw = false, trans = false, release = false;
    unsigned firstpix = 0;      // FIRSTPIX field (6 bits, a bit offset into the first phrase)
    unsigned pixelsPerPhrase = 0;   // 64 / bpp
    unsigned clutBase = 0;      // CLUT entry added to a 1-4 bpp pixel value (INDEX << 1, masked per depth)
    unsigned skipPixels = 0;    // pixels of the first phrase FIRSTPIX skips (after the per-depth mask)
    unsigned widthPixels = 0;   // iwidthEff * pixelsPerPhrase - skipPixels, before any scaling

    // ---- phrase 2 (scaled) --------------------------------------------------------------------------
    unsigned hscale = 0, vscale = 0, remainder = 0;   // raw 3.5 fixed point bytes
    unsigned scaledWidth = 0;   // widthPixels * hscale / 32, the width on screen (op.c)

    // ---- verdicts --------------------------------------------------------------------------------------
    bool isBitmap() const { return type == OpType::Bitmap || type == OpType::Scaled; }
    // A bitmap the viewer can draw: phrases captured, a legal DEPTH, HEIGHT > 0.
    bool drawable() const { return isBitmap() && havePhrase1 && bpp != 0 && height > 0; }
    bool terminatesList() const { return type == OpType::Stop; }
    std::string warning;        // "" or why the object looks wrong / cannot be drawn (one line)
};

// Decodes one phrase set.  `phrases` is how many of p0/p1/p2 are valid.
OpObject opDecode(unsigned address, unsigned phrases, uint64_t p0, uint64_t p1, uint64_t p2);
OpObject opDecode(const vj_debug_op_obj& o);
std::vector<OpObject> opDecodeAll(const vj_debug_op_obj* objs, int n);

// Names for the table / tooltips.
const char* opTypeName(OpType t);        // "Bitmap", "Scaled bitmap", "GPU", "Branch", "Stop"
const char* opTypeShort(OpType t);       // "BIT", "SCB", "GPU", "BRA", "STOP"
const char* opDepthName(unsigned depthCode);       // "8 bpp", "24 bpp", "invalid (6)"
const char* opBranchCondName(unsigned cc);          // "YPOS == VC or $7FF", ...
// One line for logs and tests: "BIT @001000 y=100 h=4 x=40 8bpp 16px".
std::string opSummary(const OpObject& o);

// Pixels per phrase and the FIRSTPIX mask the emulator applies, per DEPTH code (0 for invalid codes).
unsigned opBitsPerPixel(unsigned depthCode);
unsigned opFirstPixMask(unsigned depthCode);

// ---- where the object lands on the game picture (approximate) -------------------------------------------
// Inputs are TOM registers, read with side-effect-free peeks by the UI; the mapping mirrors tom.c's
// scanline renderers (framebuffer column 0 is the fixed left HC, row 0 is the top visible half-line).
struct OpScreenGeom {
    unsigned hdb1 = 0;          // $F00038
    unsigned vdb = 0;           // $F00046
    unsigned vp = 0;            // $F0003E
    unsigned vmode = 0;         // $F00028 (PWIDTH bits 9-11)
    unsigned frameW = 0, frameH = 0;   // the core framebuffer size the picture shows
    bool pal() const { return vp > 570; }     // NTSC VP ~523, PAL ~623
    unsigned pwidth() const { return ((vmode >> 9) & 7) + 1; }
};
struct OpScreenRect {
    bool valid = false;
    float x = 0, y = 0, w = 0, h = 0;   // framebuffer pixels (may extend outside [0,frameW) x [0,frameH))
};
// First visible half-line / left HC exactly as tom.c resolves them.
unsigned opTopVisible(const OpScreenGeom& g);
unsigned opLeftVisibleHC(const OpScreenGeom& g);
OpScreenRect opScreenRect(const OpObject& o, const OpScreenGeom& g);

}  // namespace vj
