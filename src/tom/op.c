//
// Object Processor
//
// Original source by David Raingeard (Cal2)
// GCC/SDL port by Niels Wagenaar (Linux/WIN32) and Caz (BeOS)
// Extensive cleanups/fixes/rewrites by James Hammons
// (C) 2010 Underground Software
//
// JLH = James Hammons <jlhamm@acm.org>
//
// Who  When        What
// ---  ----------  -------------------------------------------------------------
// JLH  01/16/2010  Created this log ;-)
//

#include "op.h"

#include <stdlib.h>
#include <string.h>
#include "bus_arbiter.h"
#include "gpu.h"
#include "jaguar.h"
#include "m68000/m68kinterface.h"
#include "shadowfb.h"
#include "vjag_memory.h"
#include "tom.h"
#include "../core/vjtrace.h"
#include "op_simd_neon.h"

#define BLEND_Y(dst, src)	op_blend_y[(((uint16_t)dst<<8)) | ((uint16_t)(src))]
#define BLEND_CR(dst, src)	op_blend_cr[(((uint16_t)dst)<<8) | ((uint16_t)(src))]

/* Real Jaguar TOM RAM (tomRam8[] in tom.c) is a fixed 0x4000-byte array. */
#define TOM_RAM_BYTES 0x4000

/* Issue #565: in OPProcessFixedBitmap, a REFLECT-mode object walks
 * currentLineBuffer backward (lbufDelta < 0) with no lower bound once
 * inside the per-pixel store loops below.  The phrase-granular clipping
 * earlier in that function only promises "at most 8 bytes" of overstep for
 * the ordinary partially-offscreen case (see the comment above
 * lbufAddress); it does not protect against a garbage/transient
 * object-list phrase.  Observed via `virtualjaguar_risc_clock_scale=2x`
 * against Club Drive: the write pointer walked ~3 KB behind tomRam8[0] and
 * stomped an unrelated global, aborting the process on the next free() of
 * that corrupted pointer (dsp_pc_escape in the same log was a red herring
 * -- an unrelated, self-contained DSP PC excursion the DSP's own execute
 * loop already drains safely).  Real TOM's LBUF is a small, fixed-size
 * RAM; clamp writes to tomRam8's own bounds so a bad object can only
 * glitch a line, never corrupt host memory. */
#define OP_LBUF_IN_BOUNDS(ptr, nbytes) \
   ((ptr) >= tomRam8 && (ptr) + (nbytes) <= tomRam8 + TOM_RAM_BYTES)

#define OBJECT_TYPE_BITMAP	0					// 000
#define OBJECT_TYPE_SCALE	1					// 001
#define OBJECT_TYPE_GPU		2					// 010
#define OBJECT_TYPE_BRANCH	3					// 011
#define OBJECT_TYPE_STOP	4					// 100

#define CONDITION_EQUAL				0			// VC == YPOS
#define CONDITION_LESS_THAN			1			// VC < YPOS
#define CONDITION_GREATER_THAN		2			// VC > YPOS
#define CONDITION_OP_FLAG_SET		3
#define CONDITION_SECOND_HALF_LINE	4

#define OP_RUNAWAY_GUARD_OBJECTS   30000

/* GPU-object release wait: how many GPU cycles to run inline waiting for
 * the ISR's OBF write, and the step size between checks.  A real halfline
 * is ~845 GPU cycles; ISRs that take longer would break the display on
 * hardware too, so ~5 halflines of budget is generous. */
#define OP_GPU_RELEASE_GUARD_CYCLES   4000
#define OP_GPU_RELEASE_STEP_CYCLES    16

// Private function prototypes

void OPProcessFixedBitmap(uint64_t p0, uint64_t p1, bool render);
void OPProcessScaledBitmap(uint64_t p0, uint64_t p1, uint64_t p2, bool render);
void OPDiscoverObjects(uint32_t address);
uint64_t OPLoadPhrase(uint32_t offset);

// Local global variables

/* Set by the TOM write path on any write to OBF ($F00026) — the GPU's
 * "release the Object Processor" signal after servicing a GPU object. */
static bool op_obf_written = false;

// Blend tables (64K each)
static uint8_t op_blend_y[0x10000];
static uint8_t op_blend_cr[0x10000];
static uint8_t op_bitmap_bit_depth[8] = { 1, 2, 4, 8, 16, 24, 32, 0 };
static uint32_t op_pointer;

int32_t phraseWidthToPixels[8] = { 64, 32, 16, 8, 4, 2, 0, 0 };


static void OPAdvanceScaledSource(uint16_t *horizontalRemainder, uint16_t hscale,
      int bitsPerPixel, int *pixCount, uint64_t *pixels)
{
   *horizontalRemainder += 0x20;

   while (*horizontalRemainder >= hscale)
   {
      *horizontalRemainder -= hscale;
      (*pixCount)++;
      *pixels <<= bitsPerPixel;
   }
}


static void OPSkipScaledDestinationPixels(uint32_t destPixels, uint16_t hscale,
      int bitsPerPixel, int phrasePixels, uint32_t pitchBytes, uint32_t *data,
      uint32_t *iwidth, uint16_t *horizontalRemainder, int *pixCount,
      uint64_t *pixels)
{
   uint32_t phrasesToSkip;
   uint32_t pixelShift;

   while (destPixels-- && (int32_t)*iwidth > 0)
   {
      OPAdvanceScaledSource(horizontalRemainder, hscale, bitsPerPixel, pixCount, pixels);

      if (*pixCount >= phrasePixels)
      {
         phrasesToSkip = (uint32_t)*pixCount / (uint32_t)phrasePixels;
         pixelShift = (uint32_t)*pixCount % (uint32_t)phrasePixels;

         *data += pitchBytes * phrasesToSkip;
         *pixels = ((uint64_t)JaguarReadLong(*data, OP) << 32) | JaguarReadLong(*data + 4, OP);
         *pixels <<= bitsPerPixel * pixelShift;
         *iwidth -= phrasesToSkip;
         *pixCount = (int)pixelShift;
      }
   }
}


/* Hi-res Stage 3 (epic #338, docs/hires-upscaling-design.md section 6.4):
 * peek one HSCALE sub-step (0x20 / SHADOWFB_HIRES_MAX_N) beyond a caller-
 * supplied LOCAL snapshot of the scaling accumulator, to recover the
 * fractional source detail a non-1.0x HSCALE has that the 1x destination
 * throws away.  `data`/`pixCount`/`horizontalRemainder`/`iwidthRemaining`
 * are copies taken by the caller at the point it is about to write the
 * *current* stock pixel -- this function never touches the real
 * variables, which still drive the stock write and (via the caller in
 * the OBJECT_TYPE_SCALE dispatch in this file) the VSCALE REMAINDER
 * writeback to RAM.  Both must stay byte-identical; only reading a local
 * copy makes that guarantee structural rather than reviewed, the same
 * argument the blitter's shadow_hires_sub_mid (blitter.c) uses for its
 * own local ADDRGEN walk.
 *
 * Reads via JaguarReadWord, never JaguarReadLong: JaguarReadLong charges
 * bus_arbiter_op_charge() whenever `who == OP` (see jaguar.c), which
 * would make this shadow-only peek observable in emulated 68K/GPU
 * timing -- exactly the invisibility hires_state_digest gates on.
 * JaguarReadWord never charges bus arbiter for any `who`, so the extra
 * read is free of side effects on stock state.
 *
 * Falls back to `stockValue16` (the sample already computed for the
 * pixel this peek is refining) when: the local walk would need an
 * implausible run of source pixels (malformed/pathological HSCALE,
 * bounded rather than trusted); it would read past the object's
 * remaining phrase budget (`iwidthRemaining`, the same bound the stock
 * walk itself is guarded by); or the resolved address could carry a
 * read side effect (CD/TOM/JERRY register space -- object pixel data is
 * never legitimately there on real hardware, but bail rather than
 * assume). */
static uint16_t op_hires_scale_peek(uint32_t data, int pixCount,
      uint16_t horizontalRemainder, uint16_t hscale, uint32_t pitchBytes,
      int phrasePixels, uint32_t iwidthRemaining, uint16_t stockValue16)
{
   uint16_t rem;
   int count;
   int steps;
   uint32_t phrasesNeeded;
   uint32_t addr;

   rem = (uint16_t)(horizontalRemainder + (0x20 / SHADOWFB_HIRES_MAX_N));
   count = pixCount;
   steps = 0;

   while (rem >= hscale)
   {
      rem -= hscale;
      count++;

      if (++steps > 64)			// pathological hscale: bail, don't spin
         return stockValue16;
   }

   phrasesNeeded = (uint32_t)count / (uint32_t)phrasePixels;
   if (phrasesNeeded >= iwidthRemaining)
      return stockValue16;

   addr = (data + phrasesNeeded * pitchBytes
         + (uint32_t)(count % phrasePixels) * 2) & 0xFFFFFF;
   if (addr >= 0xDFFF00)		// CD/TOM/JERRY window: possible read side effect
      return stockValue16;

   return JaguarReadWord(addr, OP);
}

/* CLUT counterpart of op_hires_scale_peek (issue #367, design section 6.5).
 *
 * Section 6.5 left the CLUT depths on the stock box-replicated path in v1 on
 * the grounds that "a CLUT index carries no sub-pixel information anyway; the
 * only gain would come from scaled CLUT objects, which is a Stage-3+
 * extension".  Stage 3 shipped, so that condition is met -- and a corpus
 * census found scaled CLUT objects are the DOMINANT scaled traffic in exactly
 * the 2D titles Stage 3 targets: 81.1% of truly-scaled destination pixels in
 * International Sensible Soccer and 75.7% in Val d'Isere, against 0% in the
 * 3D titles (AvP, Atari Karts).  Almost all of it is 8bpp.
 *
 * Returns the palette INDEX one hi-res sub-step ahead, not a colour: the
 * caller owns the CLUT offset (`index |` for 1-4bpp) and the palette lookup,
 * because those differ per depth.  Falls back to stockIndex on every bail-out
 * path, so a peek can only ever degrade to the stock sample.
 *
 * Written generically over bpp (1/2/4/8).  Only 8bpp is wired up today --
 * 2bpp and 4bpp together are 0.5% of Val d'Isere's scaled CLUT pixels and
 * 1bpp never occurs, so the remaining depths are deliberately left unwired
 * rather than carrying untested bit-extraction paths. */
static int op_hires_scale_peek_clut(uint32_t data, int pixCount,
      uint16_t horizontalRemainder, uint16_t hscale, uint32_t pitchBytes,
      int bpp, uint32_t iwidthRemaining, int stockIndex)
{
   uint16_t rem;
   int count;
   int steps;
   int pixelsPerPhrase;
   int bitOff;
   int shift;
   uint32_t phrasesNeeded;
   uint32_t addr;
   uint8_t byte;

   rem = (uint16_t)(horizontalRemainder + (0x20 / SHADOWFB_HIRES_MAX_N));
   count = pixCount;
   steps = 0;

   while (rem >= hscale)
   {
      rem -= hscale;
      count++;

      if (++steps > 64)			/* pathological hscale: bail, don't spin */
         return stockIndex;
   }

   pixelsPerPhrase = 64 / bpp;
   phrasesNeeded = (uint32_t)count / (uint32_t)pixelsPerPhrase;
   if (phrasesNeeded >= iwidthRemaining)
      return stockIndex;

   /* Pixels are packed MSB-first within the phrase, so the first pixel of a
    * byte occupies its high bits. */
   bitOff = (count % pixelsPerPhrase) * bpp;
   addr = (data + phrasesNeeded * pitchBytes + (uint32_t)(bitOff >> 3)) & 0xFFFFFF;
   if (addr >= 0xDFFF00)		/* CD/TOM/JERRY window: possible read side effect */
      return stockIndex;

   byte = JaguarReadByte(addr, OP);
   shift = 8 - bpp - (bitOff & 7);
   return (int)((byte >> shift) & ((1 << bpp) - 1));
}


//
// Object Processor initialization
//
void OPInit(void)
{
   unsigned i;

   // Here we calculate the saturating blend of a signed 4-bit value and an
   // existing Cyan/Red value as well as a signed 8-bit value and an existing intensity...
   // Note: CRY is 4 bits Cyan, 4 bits Red, 16 bits intensitY
   for(i=0; i<256*256; i++)
   {
      int y = (i >> 8) & 0xFF;
      int dy = (int8_t)i;					// Sign extend the Y index
      int c1 = (i >> 8) & 0x0F;
      int dc1 = (int8_t)(i << 4) >> 4;		// Sign extend the R index
      int c2 = (i >> 12) & 0x0F;
      int dc2 = (int8_t)(i & 0xF0) >> 4;	// Sign extend the C index

      y += dy;

      if (y < 0)
         y = 0;
      else if (y > 0xFF)
         y = 0xFF;

      op_blend_y[i] = y;

      c1 += dc1;

      if (c1 < 0)
         c1 = 0;
      else if (c1 > 0x0F)
         c1 = 0x0F;

      c2 += dc2;

      if (c2 < 0)
         c2 = 0;
      else if (c2 > 0x0F)
         c2 = 0x0F;

      op_blend_cr[i] = (c2 << 4) | c1;
   }

   OPReset();
}


//
// Object Processor reset
//
void OPReset(void)
{
}


static const char * opType[8] =
{ "(BITMAP)", "(SCALED BITMAP)", "(GPU INT)", "(BRANCH)", "(STOP)", "???", "???", "???" };
static const char * ccType[8] =
{ "==", "<", ">", "(opflag set)", "(second half line)", "?", "?", "?" };
static uint32_t object[8192];
static uint32_t numberOfObjects;

void OPDone(void)
{
   uint32_t olp = OPGetListPointer();

   numberOfObjects = 0;
   OPDiscoverObjects(olp);
}


bool OPObjectExists(uint32_t address)
{
   unsigned i;

   // Yes, we really do a linear search, every time. :-/
   for(i=0; i<numberOfObjects; i++)
   {
      if (address == object[i])
         return true;
   }

   return false;
}


void OPDiscoverObjects(uint32_t address)
{
   uint8_t objectType = 0;

   do
   {
      uint32_t hi, lo, link;

      // If we've seen this object already, bail out!
      // Otherwise, add it to the list
      if (OPObjectExists(address))
         return;

      object[numberOfObjects++] = address;

      // Get the object & decode its type, link address
      hi = JaguarReadLong(address + 0, OP);
      lo = JaguarReadLong(address + 4, OP);
      objectType = lo & 0x07;
      link = ((hi << 11) | (lo >> 21)) & 0x3FFFF8;

      if (objectType == 3)
      {
         // Branch if YPOS < 2047 can be treated as a GOTO, so don't do any
         // discovery in that case. Otherwise, have at it:
         if ((lo & 0xFFFF) != 0x7FFB)
            // Recursion needed to follow all links! This does depth-first
            // recursion on the not-taken objects
            OPDiscoverObjects(address + 8);
      }

      // Get the next object...
      address = link;
   }
   while (objectType != 4);
}

//
// Object Processor memory access
// Memory range: F00010 - F00027
//
//	F00010-F00017   R     xxxxxxxx xxxxxxxx   OB - current object code from the graphics processor
//	F00020-F00023     W   xxxxxxxx xxxxxxxx   OLP - start of the object list
//	F00026            W   -------- -------x   OBF - object processor flag
//

uint32_t OPGetListPointer(void)
{
   // Note: This register is LO / HI WORD, hence the funky look of this...
   return GET16(tomRam8, 0x20) | (GET16(tomRam8, 0x22) << 16);
}


uint32_t OPGetStatusRegister(void)
{
   return GET16(tomRam8, 0x26);
}


void OPSetStatusRegister(uint32_t data)
{
   tomRam8[0x26] = (data & 0x0000FF00) >> 8;
   tomRam8[0x27] |= (data & 0xFE);
}


/* Called by the TOM write path on any write to OBF ($F00026).  Writing the
 * object flag is how the GPU releases a halted OP after servicing a GPU
 * object. */
void OPNotifyOBFWrite(void)
{
   op_obf_written = true;
}


/* Latch the current object phrase into the read-only OB registers
 * ($F00010-$F00017), which the GPU's IRQ3 handler reads to find out which
 * object interrupted it.
 *
 * The phrase's LEAST significant word sits at the LOWEST address; each 16-bit
 * register is itself big-endian:
 *
 *   OB0 $F00010 = phrase[15:0]     OB2 $F00014 = phrase[47:32]
 *   OB1 $F00012 = phrase[31:16]    OB3 $F00016 = phrase[63:48]
 *
 * This is NOT a straight big-endian store of the phrase, and it is not
 * documented in the JTRM (Rev 8 gives only the register row "OB[0-3] Object
 * Code F00010-16 RO" and never says which phrase bits land where). It is read
 * directly off the original Flare/Atari TOM design source, netlists/tom/OB.NET
 * lines 55-67, under the comment "the first phrase can be read as four words":
 *
 *   Ob0rd[0-2]   := TS (dr[0-2],  type[0-2],      ob0r);
 *   Ob0rd[3-13]  := TS (dr[3-13], ypos[0-10],     ob0r);
 *   Ob0rd[14-15] := TS (dr[14-15],newheight[0-1], ob0r);
 *   Ob1rd[0-7]   := TS (dr[0-7],  newheight[2-9], ob1r);
 *   Ob1rd[8-15]  := TS (dr[8-15], link[0-7],      ob1r);
 *   Ob2rd[0-10]  := TS (dr[0-10], link[8-18],     ob2r);
 *   Ob2rd[11-15] := TS (dr[11-15],data[0-4],      ob2r);
 *   Ob3rd[0-15]  := TS (dr[0-15], data[5-20],     ob3r);
 *
 * TYPE (phrase bits 0-2) is driven onto dr[0-2] under ob0r, and IODEC.NET
 * lines 85-88 decode ob0r at offset $0 through ob3r at offset $6 — so the
 * phrase's low word is at $F00010.  See docs/jtrm-object-processor.md for the
 * full derivation, including the checks that pin dr[0] as D0.
 *
 * Consequence (issue #354): a 32-bit read at $F00014 returns
 * (phrase[47:32] << 16) | phrase[63:48] — all DATA for a GPU object, never
 * TYPE.  Under a straight big-endian store that read returned the low long,
 * which always carries TYPE in bits 2-0 and so could never read zero. */
void OPSetCurrentObject(uint64_t object)
{
   tomRam8[0x10] = (uint8_t)(object >>  8);
   tomRam8[0x11] = (uint8_t)(object      );

   tomRam8[0x12] = (uint8_t)(object >> 24);
   tomRam8[0x13] = (uint8_t)(object >> 16);

   tomRam8[0x14] = (uint8_t)(object >> 40);
   tomRam8[0x15] = (uint8_t)(object >> 32);

   tomRam8[0x16] = (uint8_t)(object >> 56);
   tomRam8[0x17] = (uint8_t)(object >> 48);
}


uint64_t OPLoadPhrase(uint32_t offset)
{
   offset &= ~0x07;						// 8 byte alignment
   return ((uint64_t)JaguarReadLong(offset, OP) << 32) | (uint64_t)JaguarReadLong(offset+4, OP);
}


void OPStorePhrase(uint32_t offset, uint64_t p)
{
   offset &= ~0x07;						// 8 byte alignment
   JaguarWriteLong(offset, p >> 32, OP);
   JaguarWriteLong(offset + 4, p & 0xFFFFFFFF, OP);
}

//
// Object Processor main routine
//
void OPProcessList(int halfline, bool render)
{
   bool inhibit;
   uint32_t opObjectsToRun = OP_RUNAWAY_GUARD_OBJECTS;

//#warning "!!! NEED TO HANDLE MULTIPLE FIELDS PROPERLY !!!"
   // We ignore them, for now; not good D-:
   // N.B.: Half-lines are exactly that, half-lines. When in interlaced mode, it
   //       draws the screen exactly the same way as it does in non, one line at a
   //       time. The only way you know you're in field #2 is that the topmost bit
   //       of VC is set. Half-line mode is so you can draw higher horizontal
   //       resolutions than you normally could, as the line buffer is only 720
   //       pixels wide...
   halfline &= 0x7FF;

   op_pointer = OPGetListPointer();
   VJT_EMIT(VJT_EV_OP_LIST_START, OP, op_pointer, (uint32_t)halfline);

   // *** BEGIN OP PROCESSOR TESTING ONLY ***
   // *** END OP PROCESSOR TESTING ONLY ***

   while (op_pointer)
   {
      uint64_t p0;
      // *** BEGIN OP PROCESSOR TESTING ONLY ***
      inhibit     = false;
      // *** END OP PROCESSOR TESTING ONLY ***

      p0          = OPLoadPhrase(op_pointer);
      op_pointer += 8;

      VJT_EMIT(VJT_EV_OP_OBJECT, OP, op_pointer - 8, (uint32_t)((uint8_t)p0 & 0x07));

      switch ((uint8_t)p0 & 0x07)
      {
         case OBJECT_TYPE_BITMAP:
            {
               uint16_t ypos = (p0 >> 3) & 0x7FF;
               // It seems that if the YPOS is zero, then bump the YPOS value so that it
               // coincides with the VDB value. With interlacing, this would be slightly more
               // tricky. There's probably another bit somewhere that enables this mode--but
               // so far, doesn't seem to affect any other game in a negative way (that I've
               // seen). Either that, or it's an undocumented bug...

               //No, the reason this was needed is that the OP code before was wrong. Any value
               //less than VDB will get written to the top line of the display!
               // Actually, no. Any item less than VDB will get only the lines that hang over
               // VDB displayed. Actually, this is incorrect. It seems that VDB value is wrong
               // somewhere and that's what's causing things to fuck up. Still no idea why.

               uint32_t height = (p0 & 0xFFC000) >> 14;
               uint32_t oldOPP = op_pointer - 8;
               if (!inhibit)	// For OP testing only!
                  if (halfline >= ypos && height > 0)
                  {
                     uint64_t data, dwidth;
                     // Believe it or not, this is what the OP actually does...
                     // which is why they're required to be on a dphrase boundary!
                     uint64_t p1 = OPLoadPhrase(oldOPP | 0x08);
                     uint64_t p2 = OPLoadPhrase(oldOPP | 0x10);
                     op_pointer += 16;
                     /* Streaming this object's pixel data moves the
                      * DRAM row off the object list and back once per
                      * rendered object per line; the phrase fetches
                      * themselves are charged page-mode centrally in
                      * JaguarReadLong/JaguarWriteLong. */
                     if (busArbiter.enabled)
                        bus_arbiter_op_charge(2u * busArbiter.dram_row_miss);
                     OPProcessFixedBitmap(p0, p1, render);

                     // OP write-backs

                     height--;

                     data = (p0 & 0xFFFFF80000000000LL) >> 40;
                     dwidth = (p1 & 0xFFC0000) >> 15;
                     data += dwidth;

                     p0 &= ~0xFFFFF80000FFC000LL;		// Mask out old data...
                     p0 |= (uint64_t)height << 14;
                     p0 |= data << 40;
                     OPStorePhrase(oldOPP, p0);
                  }

               // OP bottom 3 bits are hardwired to zero. The link address reflects
               // this, so we only need the top 19 bits of the address (which is
               // why we only shift 21, and not 24).
               op_pointer = (p0 & 0x000007FFFF000000LL) >> 21;

               //kludge: Seems that memory access is mirrored in the first 8MB of
               // memory...
               if (op_pointer > 0x1FFFFF && op_pointer < 0x800000)
                  op_pointer &= 0xFF1FFFFF;	// Knock out bits 21-23


               break;
            }
         case OBJECT_TYPE_SCALE:
            {
               //WAS:			uint16_t ypos = (p0 >> 3) & 0x3FF;
               uint16_t ypos = (p0 >> 3) & 0x7FF;
               uint32_t height = (p0 & 0xFFC000) >> 14;
               uint32_t oldOPP = op_pointer - 8;
               if (!inhibit)	// For OP testing only!
                  if (halfline >= ypos && height > 0)
                  {
                     uint16_t remainder;
                     uint8_t vscale;
                     uint64_t p2;
                     uint64_t p1 = OPLoadPhrase(op_pointer);
                     op_pointer += 8;
                     p2 = OPLoadPhrase(op_pointer);
                     op_pointer += 8;
                     /* Streaming this object's pixel data moves the
                      * DRAM row off the object list and back once per
                      * rendered object per line; the phrase fetches
                      * themselves are charged page-mode centrally in
                      * JaguarReadLong/JaguarWriteLong. */
                     if (busArbiter.enabled)
                        bus_arbiter_op_charge(2u * busArbiter.dram_row_miss);
                     OPProcessScaledBitmap(p0, p1, p2, render);

                     // OP write-backs

                     remainder = (p2 >> 16) & 0xFF;//, vscale = p2 >> 8;
                     vscale = p2 >> 8;
                     //Actually, we should skip this object if it has a vscale of zero.
                     //Or do we? Not sure... Atari Karts has a few lines that look like:
                     // (SCALED BITMAP)
                     //000E8268 --> phrase 00010000 7000B00D
                     //    [7 (0) x 1 @ (13, 0) (8 bpp), l: 000E82A0, p: 000E0FC0 fp: 00, fl:RELEASE, idx:00, pt:01]
                     //    [hsc: 9A, vsc: 00, rem: 00]
                     // Could it be the vscale is overridden if the DWIDTH is zero? Hmm...

                     if (vscale == 0)
                        vscale = 0x20;					// OP bug??? Nope, it isn't...! Or is it?

                     // I.e., it's < 1.0f -> means it'll go negative when we subtract 1.0f.
                     if (remainder < 0x20)
                     {
                        uint64_t data = (p0 & 0xFFFFF80000000000LL) >> 40;
                        uint64_t dwidth = (p1 & 0xFFC0000) >> 15;

                        while (remainder < 0x20)
                        {
                           remainder += vscale;

                           if (height)
                              height--;

                           data += dwidth;
                        }

                        p0 &= ~0xFFFFF80000FFC000LL;	// Mask out old data...
                        p0 |= (uint64_t)height << 14;
                        p0 |= data << 40;
                        OPStorePhrase(oldOPP, p0);
                     }

                     remainder -= 0x20;					// 1.0f in [3.5] fixed point format

                     p2 &= ~0x0000000000FF0000LL;
                     p2 |= (uint64_t)remainder << 16;
                     OPStorePhrase(oldOPP + 16, p2);
                  }

               // OP bottom 3 bits are hardwired to zero. The link address reflects
               // this, so we only need the top 19 bits of the address (which is
               // why we only shift 21, and not 24).
               op_pointer = (p0 & 0x000007FFFF000000LL) >> 21;

               //kludge: Seems that memory access is mirrored in the first 8MB of
               // memory...
               if (op_pointer > 0x1FFFFF && op_pointer < 0x800000)
                  op_pointer &= 0xFF1FFFFF;	// Knock out bits 21-23

               break;
            }
         case OBJECT_TYPE_GPU:
            {
               /* The GPU object fires whenever the OP reaches it — the
                * silicon does not honor the YPOS field the JTRM describes
                * (games gate the object with BRANCH objects instead, and
                * carry non-matching YPOS values: yarc uses a BRANCH
                * VC==506 in front of a stale-YPOS object, Primal Rage
                * gates a YPOS=0 object to halflines >= 352).  The object
                * is latched into OB, the GPU is interrupted, and the OP
                * halts until the GPU's ISR writes OBF ($F00026), then
                * continues with the next sequential phrase (single-phrase
                * object, no link — MAME resumes at +8 the same way).
                *
                * Our OP is synchronous inside the halfline callback, so
                * "halt until OBF" is modeled by running the GPU inline,
                * bounded so a title whose ISR never releases the OP keeps
                * the old stop-for-this-line behavior instead of wedging.
                * Without the resume, every object after the GPU object
                * was dropped for the rest of the frame — Primal Rage
                * rendered the bottom third of fight scenes black. */
               int32_t guard = OP_GPU_RELEASE_GUARD_CYCLES;

               VJT_EMIT(VJT_EV_OP_GPU_OBJ, OP, op_pointer - 8, 0);

               OPSetCurrentObject(p0);
               op_obf_written = false;
               GPUSetIRQLine(3, ASSERT_LINE);

               /* Waiting is only meaningful if the GPU can actually take
                * the interrupt — don't burn inline cycles for games that
                * never enabled IRQ3. */
               while (!op_obf_written && guard > 0 && GPUIsRunning()
                      && GPUOPInterruptEnabled())
               {
                  GPUExec(OP_GPU_RELEASE_STEP_CYCLES);
                  guard -= OP_GPU_RELEASE_STEP_CYCLES;
               }

               if (!op_obf_written)
               {
                  /* GPU idle, ISR missing, or no release: stop here so
                   * the GPU still sees this object in OB. */
                  return;
               }
               break;
            }
         case OBJECT_TYPE_BRANCH:
            {
               uint16_t ypos = (p0 >> 3) & 0x7FF;
               // JTRM is wrong: CC is bits 14-16 (3 bits, *not* 2)
               uint8_t  cc   = (p0 >> 14) & 0x07;
               uint32_t link = (p0 >> 21) & 0x3FFFF8;
               uint32_t branchObjAddr = op_pointer - 8;

               switch (cc)
               {
                  case CONDITION_EQUAL:
                     if (halfline == ypos || ypos == 0x7FF)
                     {
                        VJT_EMIT(VJT_EV_OP_BRANCH, OP, branchObjAddr, link);
                        op_pointer = link;
                     }
                     break;
                  case CONDITION_LESS_THAN:
                     if (halfline < ypos)
                     {
                        VJT_EMIT(VJT_EV_OP_BRANCH, OP, branchObjAddr, link);
                        op_pointer = link;
                     }
                     break;
                  case CONDITION_GREATER_THAN:
                     if (halfline > ypos)
                     {
                        VJT_EMIT(VJT_EV_OP_BRANCH, OP, branchObjAddr, link);
                        op_pointer = link;
                     }
                     break;
                  case CONDITION_OP_FLAG_SET:
                     if (OPGetStatusRegister() & 0x01)
                     {
                        VJT_EMIT(VJT_EV_OP_BRANCH, OP, branchObjAddr, link);
                        op_pointer = link;
                     }
                     break;
                  case CONDITION_SECOND_HALF_LINE:
                     // Branch if bit 10 of HC is set...
                     if (TOMGetHC() & 0x0400)
                     {
                        VJT_EMIT(VJT_EV_OP_BRANCH, OP, branchObjAddr, link);
                        op_pointer = link;
                     }
                     break;
                  default:
                     // Basically, if you do this, the OP does nothing. :-)
		     break;
               }
               break;
            }
         case OBJECT_TYPE_STOP:
            {
               OPSetCurrentObject(p0);

               if ((p0 & 0x08) && TOMIRQEnabled(IRQ_OPFLAG))
               {
                  TOMSetPendingObjectInt();
                  m68k_set_irq(2);				// Cause a 68K IPL 2 to occur...
               }

               /* Bail out, we're done... */
               return;
            }
         default:
	    break;
      }

      /* Keep malformed lists from hanging the emulator. This is not a hardware
       * cycle model; overloaded-list timing still needs a real OP scheduler. */
      opObjectsToRun--;

      if (!opObjectsToRun)
         return;
   }
}


// Store fixed size bitmap in line buffer
void OPProcessFixedBitmap(uint64_t p0, uint64_t p1, bool render)
{
   uint32_t lbufAddress;
   uint8_t * currentLineBuffer;
   int32_t startPos,endPos;
   // This is correct, the OP line buffer is a constant size...
   int32_t limit = 720;
   int32_t lbufWidth = 719;
   uint32_t clippedWidth = 0, phraseClippedWidth = 0, dataClippedWidth = 0;//, phrasePixel = 0;
   // Need to make sure that when writing that it stays within the line buffer...
   // LBUF ($F01800 - $F01D9E) 360 x 32-bit RAM
   uint8_t depth = (p1 >> 12) & 0x07;				// Color depth of image
   int32_t xpos = ((int16_t)((p1 << 4) & 0xFFFF)) >> 4;// Image xpos in LBUF
   uint32_t iwidth = (p1 >> 28) & 0x3FF;				// Image width in *phrases*
   uint32_t data = (p0 >> 40) & 0xFFFFF8;			// Pixel data address
   uint32_t firstPix = (p1 >> 49) & 0x3F;
   // We can ignore the RELEASE (high order) bit for now--probably forever...!
   //	uint8_t flags = (p1 >> 45) & 0x0F;	// REFLECT, RMW, TRANS, RELEASE
   //Optimize: break these out to their own BOOL values
   uint8_t flags = (p1 >> 45) & 0x07;				// REFLECT (0), RMW (1), TRANS (2)
   bool flagREFLECT = ((flags & OPFLAG_REFLECT) ? true : false),
        flagRMW = ((flags & OPFLAG_RMW) ? true : false),
        flagTRANS = ((flags & OPFLAG_TRANS) ? true : false);
   // "For images with 1 to 4 bits/pixel the top 7 to 4 bits of the index
   //  provide the most significant bits of the palette address."
   uint8_t index = (p1 >> 37) & 0xFE;				// CLUT index offset (upper pix, 1-4 bpp)
   uint32_t pitch = (p1 >> 15) & 0x07;				// Phrase pitch

   uint8_t * tomRam8 = TOMGetRamPointer();
   uint8_t * paletteRAM = &tomRam8[0x400];
   // This is OK as long as it's used correctly: For 16-bit RAM to RAM direct copies--NOT
   // for use when using endian-corrected data (i.e., any of the *_word_read functions!)
   uint16_t * paletteRAM16 = (uint16_t *)paletteRAM;

   // "The LSB is significant only for scaled objects..." -JTRM
   // "In 1 BPP mode, all five bits are significant. In 2 BPP mode, the top four are significant..."
   firstPix &= 0x3E;

   pitch <<= 3;									// Optimization: Multiply pitch by 8

   /* dwidth and pitch can be zero; current behavior treats iwidth == 0 as one
    * phrase, but that still needs hardware or ROM-specific validation. */
   if (iwidth == 0)
      iwidth = 1;

   if (!render)
      return;

   startPos = xpos;
   endPos = xpos +
      (!flagREFLECT ? (phraseWidthToPixels[depth] * iwidth) - 1
       : -((phraseWidthToPixels[depth] * iwidth) + 1));

   // If the image is completely to the left or right of the line buffer, then bail.
   // In REFLECT mode these edge cases are mirrored.
   //There are four possibilities:
   //  1. image sits on left edge and no REFLECT; starts out of bounds but ends in bounds.
   //  2. image sits on left edge and REFLECT; starts in bounds but ends out of bounds.
   //  3. image sits on right edge and REFLECT; starts out of bounds but ends in bounds.
   //  4. image sits on right edge and no REFLECT; starts in bounds but ends out of bounds.
   //Numbers 2 & 4 can be caught by checking the LBUF clip while in the inner loop,
   // numbers 1 & 3 are of concern.
   // This *indirectly* handles only cases 2 & 4! And is WRONG is REFLECT is set...!
   //	if (rightMargin < 0 || leftMargin > lbufWidth)

   // It might be easier to swap these (if REFLECTed) and just use XPOS down below...
   // That way, you could simply set XPOS to leftMargin if !REFLECT and to rightMargin otherwise.
   // Still have to be careful with the DATA and IWIDTH values though...

   //	if ((!flagREFLECT && (rightMargin < 0 || leftMargin > lbufWidth))
   //		|| (flagREFLECT && (leftMargin < 0 || rightMargin > lbufWidth)))
   //		return;
   if ((!flagREFLECT && (endPos < 0 || startPos > lbufWidth))
         || (flagREFLECT && (startPos < 0 || endPos > lbufWidth)))
      return;

   // Otherwise, find the clip limits and clip the phrase as well...
   // NOTE: I'm fudging here by letting the actual blit overstep the bounds of the
   //       line buffer, but it shouldn't matter since there are two unused line
   //       buffers below and nothing above and I'll at most write 8 bytes outside
   //       the line buffer... I could use a fractional clip begin/end value, but
   //       this makes the blit a *lot* more hairy. I might fix this in the future
   //       if it becomes necessary. (JLH)
   //       Probably wouldn't be *that* hairy. Just use a delta that tells the inner loop
   //       which pixel in the phrase is being written, and quit when either end of phrases
   //       is reached or line buffer extents are surpassed.

   /* Fixed-bitmap clipping is phrase-granular. It skips whole source phrases
    * for start-out/end-in cases and lets the inner loop handle final LBUF
    * bounds. */
   if (startPos < 0)			// Case #1: Begin out, end in, L to R
      clippedWidth = 0 - startPos,
                   dataClippedWidth = phraseClippedWidth = clippedWidth / phraseWidthToPixels[depth],
                   startPos = 0 - (clippedWidth % phraseWidthToPixels[depth]);

   if (endPos < 0)				// Case #2: Begin in, end out, R to L
      clippedWidth = 0 - endPos,
                   phraseClippedWidth = clippedWidth / phraseWidthToPixels[depth];

   if (endPos > lbufWidth)		// Case #3: Begin in, end out, L to R
      clippedWidth = endPos - lbufWidth,
                   phraseClippedWidth = clippedWidth / phraseWidthToPixels[depth];

   if (startPos > lbufWidth)	// Case #4: Begin out, end in, R to L
      clippedWidth = startPos - lbufWidth,
                   dataClippedWidth = phraseClippedWidth = clippedWidth / phraseWidthToPixels[depth],
                   startPos = lbufWidth + (clippedWidth % phraseWidthToPixels[depth]);

   // If the image is sitting on the line buffer left or right edge, we need to compensate
   // by decreasing the image phrase width accordingly.
   iwidth -= phraseClippedWidth;

   // Also, if we're clipping the phrase we need to make sure we're in the correct part of
   // the pixel data.
   //	data += phraseClippedWidth * (pitch << 3);
   data += dataClippedWidth * pitch;
   if (dataClippedWidth > 0)
      firstPix = 0;

   // NOTE: When the bitmap is in REFLECT mode, the XPOS marks the *right* side of the
   //       bitmap! This makes clipping & etc. MUCH, much easier...!
   //	uint32_t lbufAddress = 0x1800 + (!in24BPPMode ? leftMargin * 2 : leftMargin * 4);
   //Why does this work right when multiplying startPos by 2 (instead of 4) for 24 BPP mode?
   //Is this a bug in the OP?
   //It's because in 24bpp mode, each pixel takes *4* bytes, instead of the usual 2.
   //Though it looks like we're doing it here no matter what...
   //	uint32_t lbufAddress = 0x1800 + (!in24BPPMode ? startPos * 2 : startPos * 2);
   //Let's try this:
   lbufAddress = 0x1800 + (startPos * 2);
   currentLineBuffer = &tomRam8[lbufAddress];

   // Render.

   // Hmm. We check above for 24 BPP mode, but don't do anything about it below...
   // If we *were* in 24 BPP mode, how would you convert CRY to RGB24? Seems to me
   // that if you're in CRY mode then you wouldn't be able to use 24 BPP bitmaps
   // anyway.
   // This seems to be the case (at least according to the Midsummer docs)...!

   // This is to test using palette zeroes instead of bit zeroes...
   // And it seems that this is wrong, index == 0 is transparent apparently... :-/
   //#define OP_USES_PALETTE_ZERO

   if (depth == 0)									// 1 BPP
   {
      int i;
      // The LSB of flags is OPFLAG_REFLECT, so sign extend it and or 2 into it.
      int32_t lbufDelta = ((int8_t)((flags << 7) & 0xFF) >> 5) | 0x02;

      // Fetch 1st phrase...
      uint64_t pixels = ((uint64_t)JaguarReadLong(data, OP) << 32) | JaguarReadLong(data + 4, OP);
      /* firstPix is applied to the initial source phrase. If clipping skipped
       * phrases, this may need hardware-specific adjustment. */
      pixels <<= firstPix;						// Skip first N pixels (N=firstPix)...
      i        = firstPix;							// Start counter at right spot...

      while (iwidth--)
      {
         while (i++ < 64)
         {
            uint8_t bit = pixels >> 63;
#ifndef OP_USES_PALETTE_ZERO
            if (flagTRANS && bit == 0)
#else
               if (flagTRANS && (paletteRAM16[index | bit] == 0))
#endif
                  ;	// Do nothing...
               else if (OP_LBUF_IN_BOUNDS(currentLineBuffer, 2))
               {
                  if (!flagRMW)
                     //Optimize: Set palleteRAM16 to beginning of palette RAM + index*2 and use only [bit] as index...
                     //Won't optimize RMW case though...
                     // This is the *only* correct use of endian-dependent code
                     // (i.e., mem-to-mem direct copying)!
                     *(uint16_t *)currentLineBuffer = paletteRAM16[index | bit];
                  else
                     *currentLineBuffer =
                        BLEND_CR(*currentLineBuffer, paletteRAM[(index | bit) << 1]),
                        *(currentLineBuffer + 1) =
                           BLEND_Y(*(currentLineBuffer + 1), paletteRAM[((index | bit) << 1) + 1]);
               }

            currentLineBuffer += lbufDelta;
            pixels <<= 1;
         }
         i = 0;
         // Fetch next phrase...
         data += pitch;
         pixels = ((uint64_t)JaguarReadLong(data, OP) << 32) | JaguarReadLong(data + 4, OP);
      }
   }
   else if (depth == 1)							// 2 BPP
   {
      int i;
      int32_t lbufDelta;

      index &= 0xFC;								// Top six bits form CLUT index
      // The LSB is OPFLAG_REFLECT, so sign extend it and or 2 into it.
      lbufDelta = ((int8_t)((flags << 7) & 0xFF) >> 5) | 0x02;

      firstPix &= 0x3C;
      i = firstPix >> 1;

      while (iwidth--)
      {
         // Fetch phrase...
         uint64_t pixels = ((uint64_t)JaguarReadLong(data, OP) << 32) | JaguarReadLong(data + 4, OP);
         pixels <<= firstPix;
         data += pitch;

         while (i++ < 32)
         {
            uint8_t bits = pixels >> 62;
            // Seems to me that both of these are in the same endian, so we could cast it as
            // uint16_t * and do straight across copies (what about 24 bpp? Treat it differently...)
            // This only works for the palettized modes (1 - 8 BPP), since we actually have to
            // copy data from memory in 16 BPP mode (or does it? Isn't this the same as the CLUT case?)
            // No, it isn't because we read the memory in an endian safe way--this *won't* work...
#ifndef OP_USES_PALETTE_ZERO
            if (flagTRANS && bits == 0)
#else
               if (flagTRANS && (paletteRAM16[index | bits] == 0))
#endif
                  ;	// Do nothing...
               else if (OP_LBUF_IN_BOUNDS(currentLineBuffer, 2))
               {
                  if (!flagRMW)
                     *(uint16_t *)currentLineBuffer = paletteRAM16[index | bits];
                  else
                     *currentLineBuffer =
                        BLEND_CR(*currentLineBuffer, paletteRAM[(index | bits) << 1]),
                        *(currentLineBuffer + 1) =
                           BLEND_Y(*(currentLineBuffer + 1), paletteRAM[((index | bits) << 1) + 1]);
               }

            currentLineBuffer += lbufDelta;
            pixels <<= 2;
         }

         firstPix = 0;
         i = 0;
      }
   }
   else if (depth == 2)							// 4 BPP
   {
      int i;
      int32_t lbufDelta;
      index &= 0xF0;								// Top four bits form CLUT index
      // The LSB is OPFLAG_REFLECT, so sign extend it and or 2 into it.
      lbufDelta = ((int8_t)((flags << 7) & 0xFF) >> 5) | 0x02;

      firstPix &= 0x38;
      i = firstPix >> 2;

      while (iwidth--)
      {
         // Fetch phrase...
         uint64_t pixels = ((uint64_t)JaguarReadLong(data, OP) << 32) | JaguarReadLong(data + 4, OP);
         pixels <<= firstPix;
         data += pitch;

         while (i++ < 16)
         {
            uint8_t bits = pixels >> 60;
            // Seems to me that both of these are in the same endian, so we could cast it as
            // uint16_t * and do straight across copies (what about 24 bpp? Treat it differently...)
            // This only works for the palettized modes (1 - 8 BPP), since we actually have to
            // copy data from memory in 16 BPP mode (or does it? Isn't this the same as the CLUT case?)
            // No, it isn't because we read the memory in an endian safe way--this *won't* work...
#ifndef OP_USES_PALETTE_ZERO
            if (flagTRANS && bits == 0)
#else
               if (flagTRANS && (paletteRAM16[index | bits] == 0))
#endif
                  ;	// Do nothing...
               else if (OP_LBUF_IN_BOUNDS(currentLineBuffer, 2))
               {
                  if (!flagRMW)
                     *(uint16_t *)currentLineBuffer = paletteRAM16[index | bits];
                  else
                     *currentLineBuffer =
                        BLEND_CR(*currentLineBuffer, paletteRAM[(index | bits) << 1]),
                        *(currentLineBuffer + 1) =
                           BLEND_Y(*(currentLineBuffer + 1), paletteRAM[((index | bits) << 1) + 1]);
               }

            currentLineBuffer += lbufDelta;
            pixels <<= 4;
         }

         firstPix = 0;
         i = 0;
      }
   }
   else if (depth == 3)							// 8 BPP
   {
      int i;
      // The LSB is OPFLAG_REFLECT, so sign extend it and or 2 into it.
      int32_t lbufDelta = ((int8_t)((flags << 7) & 0xFF) >> 5) | 0x02;

      // Fetch 1st phrase...
      uint64_t pixels = ((uint64_t)JaguarReadLong(data, OP) << 32) | JaguarReadLong(data + 4, OP);
      /* firstPix is applied to the initial source phrase. If clipping skipped
       * phrases, this may need hardware-specific adjustment. */
      firstPix &= 0x30;							// Only top two bits are valid for 8 BPP
      pixels <<= firstPix;						// Skip first N pixels (N=firstPix)...
      i = firstPix >> 3;						// Start counter at right spot...

      while (iwidth--)
      {
         while (i++ < 8)
         {
            uint8_t bits = pixels >> 56;
            // Seems to me that both of these are in the same endian, so we could cast it as
            // uint16_t * and do straight across copies (what about 24 bpp? Treat it differently...)
            // This only works for the palettized modes (1 - 8 BPP), since we actually have to
            // copy data from memory in 16 BPP mode (or does it? Isn't this the same as the CLUT case?)
            // No, it isn't because we read the memory in an endian safe way--this *won't* work...
            //This would seem to be problematic...
            //Because it's the palette entry being zero that makes the pixel transparent...
            //Let's try it and see.
#ifndef OP_USES_PALETTE_ZERO
            if (flagTRANS && bits == 0)
#else
               if (flagTRANS && (paletteRAM16[bits] == 0))
#endif
                  ;	// Do nothing...
               else if (OP_LBUF_IN_BOUNDS(currentLineBuffer, 2))
               {
                  if (!flagRMW)
                     *(uint16_t *)currentLineBuffer = paletteRAM16[bits];
                  else
                     *currentLineBuffer =
                        BLEND_CR(*currentLineBuffer, paletteRAM[bits << 1]),
                        *(currentLineBuffer + 1) =
                           BLEND_Y(*(currentLineBuffer + 1), paletteRAM[(bits << 1) + 1]);
               }

            currentLineBuffer += lbufDelta;
            pixels <<= 8;
         }
         i = 0;
         // Fetch next phrase...
         data += pitch;
         pixels = ((uint64_t)JaguarReadLong(data, OP) << 32) | JaguarReadLong(data + 4, OP);
      }
   }
   else if (depth == 4)							// 16 BPP
   {
      // The LSB is OPFLAG_REFLECT, so sign extend it and or 2 into it.
      int i;
      int32_t lbufDelta = ((int8_t)((flags << 7) & 0xFF) >> 5) | 0x02;

      firstPix &= 0x30;
      i = firstPix >> 4;

      while (iwidth--)
      {
         // Fetch phrase...
         uint64_t pixels = ((uint64_t)JaguarReadLong(data, OP) << 32) | JaguarReadLong(data + 4, OP);
         /* Source phrase address for the true-color shadow lookup;
          * captured before the pitch advance below. */
         uint32_t sfbPhrase = data;
         pixels <<= firstPix;
         data += pitch;

#if defined(BLITTER_SIMD_HAVE_NEON)
         /* Full 4-pixel phrase, no RMW / REFLECT / shadow hooks, wholly
          * inside LBUF. Partial firstPix/i phrases and every other mode
          * keep the scalar loop below. */
         if (!flagRMW && lbufDelta == 2
               && !shadowFBActive && !shadowHiresActive
               && OP_LBUF_IN_BOUNDS(currentLineBuffer, 8)
               && i == 0 && firstPix == 0)
         {
            op_store_phrase_16bpp_neon(currentLineBuffer, pixels, flagTRANS);
            currentLineBuffer += 8;
            firstPix = 0;
            i = 0;
            continue;
         }
#endif

         while (i++ < 4)
         {
            uint8_t bitsHi = pixels >> 56, bitsLo = pixels >> 48;
            // Seems to me that both of these are in the same endian, so we could cast it as
            // uint16_t * and do straight across copies (what about 24 bpp? Treat it differently...)
            // This only works for the palettized modes (1 - 8 BPP), since we actually have to
            // copy data from memory in 16 BPP mode (or does it? Isn't this the same as the CLUT case?)
            // No, it isn't because we read the memory in an endian safe way--it *won't* work...
            //This doesn't seem right... Let's try the encoded black value ($8800):
            //Apparently, CRY 0 maps to $8800...
            if (flagTRANS && ((bitsLo | bitsHi) == 0))
               //				if (flagTRANS && (bitsHi == 0x88) && (bitsLo == 0x00))
               ;	// Do nothing...
            else if (OP_LBUF_IN_BOUNDS(currentLineBuffer, 2))
            {
               if (!flagRMW)
               {
                  *currentLineBuffer = bitsHi;
                  *(currentLineBuffer + 1) = bitsLo;
                  /* True-color: resolve the source RAM word against the
                   * shadow framebuffer and mirror it into the shadow
                   * line buffer at the same pixel index (see shadowfb.h). */
                  if (shadowFBActive)
                     ShadowFBLineFromRAM(
                           (int)((currentLineBuffer - &tomRam8[0x1800]) >> 1),
                           sfbPhrase + (((uint32_t)(i - 1)) << 1),
                           (uint16_t)(((uint16_t)bitsHi << 8) | bitsLo));
                  /* Hi-res: same resolve against the Nx shadow surface,
                   * producing all N sub-rows inside this single OP pass
                   * (see shadowfb.h). */
                  if (shadowHiresActive)
                     ShadowHiresLineFromRAM(
                           (int)((currentLineBuffer - &tomRam8[0x1800]) >> 1),
                           sfbPhrase + (((uint32_t)(i - 1)) << 1),
                           (uint16_t)(((uint16_t)bitsHi << 8) | bitsLo));
               }
               else
                  *currentLineBuffer =
                     BLEND_CR(*currentLineBuffer, bitsHi),
                     *(currentLineBuffer + 1) =
                        BLEND_Y(*(currentLineBuffer + 1), bitsLo);
            }

            currentLineBuffer += lbufDelta;
            pixels <<= 16;
         }

         firstPix = 0;
         i = 0;
      }
   }
   else if (depth == 5)							// 24 BPP
   {
      //Looks like Iron Soldier is the only game that uses 24BPP mode...
      //There *might* be others...
      // Not sure, but I think RMW only works with 16 BPP and below, and only in CRY mode...
      // The LSB of flags is OPFLAG_REFLECT, so sign extend it and OR 4 into it.
      int i;
      int32_t lbufDelta = ((int8_t)((flags << 7) & 0xFF) >> 4) | 0x04;

      firstPix &= 0x20;
      i = firstPix >> 5;

      while (iwidth--)
      {
         // Fetch phrase...
         uint64_t pixels = ((uint64_t)JaguarReadLong(data, OP) << 32) | JaguarReadLong(data + 4, OP);
         pixels <<= firstPix;
         data += pitch;

#if defined(BLITTER_SIMD_HAVE_NEON)
         /* Full 2-pixel phrase, no REFLECT, wholly inside LBUF.
          * 24bpp has no RMW path and no shadow-FB hooks. Partial
          * firstPix/i phrases keep the scalar loop below. */
         if (!flagRMW && lbufDelta == 4
               && OP_LBUF_IN_BOUNDS(currentLineBuffer, 8)
               && i == 0 && firstPix == 0)
         {
            op_store_phrase_24bpp_neon(currentLineBuffer, pixels, flagTRANS);
            currentLineBuffer += 8;
            firstPix = 0;
            i = 0;
            continue;
         }
#endif

         while (i++ < 2)
         {
            // We don't use a 32-bit var here because of endian issues...!
            uint8_t bits3 = pixels >> 56, bits2 = pixels >> 48,
                    bits1 = pixels >> 40, bits0 = pixels >> 32;

            if (flagTRANS && (bits3 | bits2 | bits1 | bits0) == 0)
               ;	// Do nothing...
            else if (OP_LBUF_IN_BOUNDS(currentLineBuffer, 4))
               *currentLineBuffer = bits3,
                  *(currentLineBuffer + 1) = bits2,
                  *(currentLineBuffer + 2) = bits1,
                  *(currentLineBuffer + 3) = bits0;

            currentLineBuffer += lbufDelta;
            pixels <<= 32;
         }

         firstPix = 0;
         i = 0;
      }
   }
}

// Store scaled bitmap in line buffer
void OPProcessScaledBitmap(uint64_t p0, uint64_t p1, uint64_t p2, bool render)
{
   uint32_t lbufAddress;
   uint8_t * currentLineBuffer;
   uint32_t scaledPhrasePixelsUS;
   uint32_t clippedWidthUS;
   uint32_t phraseClippedWidth = 0, dataClippedWidth = 0;
   uint32_t clippedDestPixels = 0;
   uint32_t visibleDestPixels;
   // Not sure if this is Jaguar Two only location or what...
   // From the docs, it is... If we want to limit here we should think of something else.
   //	int32_t limit = GET16(tom_ram_8, 0x0008);			// LIMIT
   int32_t limit = 720;
   int32_t lbufWidth = 719;	// Zero based limit...
   // Need to make sure that when writing that it stays within the line buffer...
   // LBUF ($F01800 - $F01D9E) 360 x 32-bit RAM
   uint8_t depth = (p1 >> 12) & 0x07;				// Color depth of image
   int32_t xpos = ((int16_t)((p1 << 4) & 0xFFFF)) >> 4;// Image xpos in LBUF
   uint32_t iwidth = (p1 >> 28) & 0x3FF;				// Image width in *phrases*
   uint32_t data = (p0 >> 40) & 0xFFFFF8;			// Pixel data address
   uint32_t firstPix = (p1 >> 49) & 0x3F;
   // We can ignore the RELEASE (high order) bit for now--probably forever...!
   //	uint8_t flags = (p1 >> 45) & 0x0F;	// REFLECT, RMW, TRANS, RELEASE
   uint8_t flags = (p1 >> 45) & 0x07;				// REFLECT (0), RMW (1), TRANS (2)
   bool flagREFLECT = ((flags & OPFLAG_REFLECT) ? true : false),
        flagRMW = ((flags & OPFLAG_RMW) ? true : false),
        flagTRANS = ((flags & OPFLAG_TRANS) ? true : false);
   uint8_t index = (p1 >> 37) & 0xFE;				// CLUT index offset (upper pix, 1-4 bpp)
   uint32_t pitch = (p1 >> 15) & 0x07;				// Phrase pitch

   uint8_t * tomRam8 = TOMGetRamPointer();
   uint8_t * paletteRAM = &tomRam8[0x400];
   // This is OK as long as it's used correctly: For 16-bit RAM to RAM direct copies--NOT
   // for use when using endian-corrected data (i.e., any of the *ReadWord functions!)
   uint16_t * paletteRAM16 = (uint16_t *)paletteRAM;

   uint16_t hscale = p2 & 0xFF;
   uint16_t horizontalRemainder = 0;
   uint32_t firstPixShift = 0;
   uint32_t firstPixPixels = 0;
   int32_t scaledWidthInPixels;
   int32_t startPos;
   int32_t endPos;
   int32_t visibleStart;
   int32_t visibleEnd;

   // Looks like an hscale of zero means don't draw!
   if (!render || hscale == 0)
      return;

   if (iwidth == 0)
      iwidth = 1;

   scaledWidthInPixels = (iwidth * phraseWidthToPixels[depth] * hscale) >> 5;
   startPos = xpos;
   endPos = xpos + (!flagREFLECT ? scaledWidthInPixels - 1 : -(scaledWidthInPixels - 1));

   switch (depth)
   {
      case 0:
         firstPixShift = firstPix & 0x3E;
         firstPixPixels = firstPixShift;
         break;
      case 1:
         firstPixShift = firstPix & 0x3C;
         firstPixPixels = firstPixShift >> 1;
         break;
      case 2:
         firstPixShift = firstPix & 0x38;
         firstPixPixels = firstPixShift >> 2;
         break;
      case 3:
         firstPixShift = firstPix & 0x30;
         firstPixPixels = firstPixShift >> 3;
         break;
      case 4:
         firstPixShift = firstPix & 0x30;
         firstPixPixels = firstPixShift >> 4;
         break;
      default:
         firstPixShift = 0;
         firstPixPixels = 0;
         break;
   }

   // If the image is completely to the left or right of the line buffer, then bail.
   // In REFLECT mode these edge cases are mirrored.
   //There are four possibilities:
   //  1. image sits on left edge and no REFLECT; starts out of bounds but ends in bounds.
   //  2. image sits on left edge and REFLECT; starts in bounds but ends out of bounds.
   //  3. image sits on right edge and REFLECT; starts out of bounds but ends in bounds.
   //  4. image sits on right edge and no REFLECT; starts in bounds but ends out of bounds.
   //Numbers 2 & 4 can be caught by checking the LBUF clip while in the inner loop,
   // numbers 1 & 3 are of concern.
   // This *indirectly* handles only cases 2 & 4! And is WRONG if REFLECT is set...!
   //	if (rightMargin < 0 || leftMargin > lbufWidth)

   // It might be easier to swap these (if REFLECTed) and just use XPOS down below...
   // That way, you could simply set XPOS to leftMargin if !REFLECT and to rightMargin otherwise.
   // Still have to be careful with the DATA and IWIDTH values though...

   if ((!flagREFLECT && (endPos < 0 || startPos > lbufWidth))
         || (flagREFLECT && (startPos < 0 || endPos > lbufWidth)))
      return;

   // Otherwise, find the clip limits and clip the phrase as well...
   // NOTE: I'm fudging here by letting the actual blit overstep the bounds of the
   //       line buffer, but it shouldn't matter since there are two unused line
   //       buffers below and nothing above and I'll at most write 40 bytes outside
   //       the line buffer... I could use a fractional clip begin/end value, but
   //       this makes the blit a *lot* more hairy. I might fix this in the future
   //       if it becomes necessary. (JLH)
   //       Probably wouldn't be *that* hairy. Just use a delta that tells the inner loop
   //       which pixel in the phrase is being written, and quit when either end of phrases
   //       is reached or line buffer extents are surpassed.

   /* Scaled clipping is phrase-granular. Keep the unscaled numerator in
    * scaledPhrasePixelsUS so small hscale values do not truncate each phrase
    * to zero visible pixels before the clip calculation. */
   scaledPhrasePixelsUS = phraseWidthToPixels[depth] * hscale;
   if (scaledPhrasePixelsUS == 0)
      return;

   if (startPos < 0)			// Case #1: Begin out, end in, L to R
   {
      clippedWidthUS = (0 - startPos) << 5;
      dataClippedWidth = phraseClippedWidth = clippedWidthUS / scaledPhrasePixelsUS;
      startPos += (dataClippedWidth * scaledPhrasePixelsUS) >> 5;
      if (startPos < 0)
      {
         clippedDestPixels = (uint32_t)-startPos;
         startPos = 0;
      }
   }

   if (endPos < 0)				// Case #2: Begin in, end out, R to L
   {
      clippedWidthUS = (0 - endPos) << 5;
      phraseClippedWidth = clippedWidthUS / scaledPhrasePixelsUS;
   }

   if (endPos > lbufWidth)		// Case #3: Begin in, end out, L to R
   {
      clippedWidthUS = (endPos - lbufWidth) << 5;
      phraseClippedWidth = clippedWidthUS / scaledPhrasePixelsUS;
   }

   if (startPos > lbufWidth)	// Case #4: Begin out, end in, R to L
   {
      clippedWidthUS = (startPos - lbufWidth) << 5;
      dataClippedWidth = phraseClippedWidth = clippedWidthUS / scaledPhrasePixelsUS;
      startPos = lbufWidth + ((clippedWidthUS % scaledPhrasePixelsUS) >> 5);
      if (startPos > lbufWidth)
      {
         clippedDestPixels = (uint32_t)(startPos - lbufWidth);
         startPos = lbufWidth;
      }
   }

   // If the image is sitting on the line buffer left or right edge, we need to compensate
   // by decreasing the image phrase width accordingly.
   iwidth -= phraseClippedWidth;

   // Also, if we're clipping the phrase we need to make sure we're in the correct part of
   // the pixel data.
   //	data += phraseClippedWidth * (pitch << 3);
   data += dataClippedWidth * (pitch << 3);
   if (dataClippedWidth > 0)
   {
      firstPixShift = 0;
      firstPixPixels = 0;
   }

   if (!flagREFLECT)
   {
      visibleStart = startPos;
      visibleEnd = endPos;
   }
   else
   {
      visibleStart = endPos;
      visibleEnd = startPos;
   }

   if (visibleStart < 0)
      visibleStart = 0;
   if (visibleEnd > lbufWidth)
      visibleEnd = lbufWidth;
   if (visibleEnd < visibleStart)
      return;

   visibleDestPixels = (uint32_t)(visibleEnd - visibleStart + 1);

   // NOTE: When the bitmap is in REFLECT mode, the XPOS marks the *right* side of the
   //       bitmap! This makes clipping & etc. MUCH, much easier...!
   lbufAddress = 0x1800 + startPos * 2;
   currentLineBuffer = &tomRam8[lbufAddress];

   // Render.

   // Hmm. We check above for 24 BPP mode, but don't do anything about it below...
   // If we *were* in 24 BPP mode, how would you convert CRY to RGB24? Seems to me
   // that if you're in CRY mode then you wouldn't be able to use 24 BPP bitmaps
   // anyway.
   // This seems to be the case (at least according to the Midsummer docs)...!

   if (depth == 0)									// 1 BPP
   {
      // The LSB of flags is OPFLAG_REFLECT, so sign extend it and or 2 into it.
      int32_t lbufDelta = ((int8_t)((flags << 7) & 0xFF) >> 5) | 0x02;

      int pixCount = (int)firstPixPixels;
      uint64_t pixels = ((uint64_t)JaguarReadLong(data, OP) << 32) | JaguarReadLong(data + 4, OP);

      pixels <<= firstPixShift;
      OPSkipScaledDestinationPixels(clippedDestPixels, hscale, 1, 64, pitch << 3,
            &data, &iwidth, &horizontalRemainder, &pixCount, &pixels);

      while ((int32_t)iwidth > 0 && visibleDestPixels > 0)
      {
         uint8_t bits = pixels >> 63;

#ifndef OP_USES_PALETTE_ZERO
         if (flagTRANS && bits == 0)
#else
            if (flagTRANS && (paletteRAM16[index | bits] == 0))
#endif
               ;	// Do nothing...
            else
            {
               if (!flagRMW)
                  // This is the *only* correct use of endian-dependent code
                  // (i.e., mem-to-mem direct copying)!
                  *(uint16_t *)currentLineBuffer = paletteRAM16[index | bits];
               else
                  *currentLineBuffer =
                     BLEND_CR(*currentLineBuffer, paletteRAM[(index | bits) << 1]),
                     *(currentLineBuffer + 1) =
                        BLEND_Y(*(currentLineBuffer + 1), paletteRAM[((index | bits) << 1) + 1]);
            }

         currentLineBuffer += lbufDelta;
         visibleDestPixels--;

         OPAdvanceScaledSource(&horizontalRemainder, hscale, 1, &pixCount, &pixels);

         if (pixCount > 63)
         {
            int phrasesToSkip = pixCount / 64, pixelShift = pixCount % 64;

            data += (pitch << 3) * phrasesToSkip;
            pixels = ((uint64_t)JaguarReadLong(data, OP) << 32) | JaguarReadLong(data + 4, OP);
            pixels <<= 1 * pixelShift;
            iwidth -= phrasesToSkip;
            pixCount = pixelShift;
         }
      }
   }
   else if (depth == 1)							// 2 BPP
   {
      int32_t lbufDelta;
      int pixCount = (int)firstPixPixels;
      uint64_t pixels;

      index &= 0xFC;								// Top six bits form CLUT index
      // The LSB is OPFLAG_REFLECT, so sign extend it and or 2 into it.
      lbufDelta = ((int8_t)((flags << 7) & 0xFF) >> 5) | 0x02;

      pixels = ((uint64_t)JaguarReadLong(data, OP) << 32) | JaguarReadLong(data + 4, OP);
      pixels <<= firstPixShift;
      OPSkipScaledDestinationPixels(clippedDestPixels, hscale, 2, 32, pitch << 3,
            &data, &iwidth, &horizontalRemainder, &pixCount, &pixels);

      while ((int32_t)iwidth > 0 && visibleDestPixels > 0)
      {
         uint8_t bits = pixels >> 62;

#ifndef OP_USES_PALETTE_ZERO
         if (flagTRANS && bits == 0)
#else
            if (flagTRANS && (paletteRAM16[index | bits] == 0))
#endif
               ;	// Do nothing...
            else
            {
               if (!flagRMW)
                  // This is the *only* correct use of endian-dependent code
                  // (i.e., mem-to-mem direct copying)!
                  *(uint16_t *)currentLineBuffer = paletteRAM16[index | bits];
               else
                  *currentLineBuffer =
                     BLEND_CR(*currentLineBuffer, paletteRAM[(index | bits) << 1]),
                     *(currentLineBuffer + 1) =
                        BLEND_Y(*(currentLineBuffer + 1), paletteRAM[((index | bits) << 1) + 1]);
            }

         currentLineBuffer += lbufDelta;
         visibleDestPixels--;

         OPAdvanceScaledSource(&horizontalRemainder, hscale, 2, &pixCount, &pixels);

         if (pixCount > 31)
         {
            int phrasesToSkip = pixCount / 32, pixelShift = pixCount % 32;

            data += (pitch << 3) * phrasesToSkip;
            pixels = ((uint64_t)JaguarReadLong(data, OP) << 32) | JaguarReadLong(data + 4, OP);
            pixels <<= 2 * pixelShift;
            iwidth -= phrasesToSkip;
            pixCount = pixelShift;
         }
      }
   }
   else if (depth == 2)							// 4 BPP
   {
      int pixCount = (int)firstPixPixels;
      int32_t lbufDelta;
      uint64_t pixels;

      index &= 0xF0;								// Top four bits form CLUT index
      // The LSB is OPFLAG_REFLECT, so sign extend it and or 2 into it.
      lbufDelta = ((int8_t)((flags << 7) & 0xFF) >> 5) | 0x02;

      pixels = ((uint64_t)JaguarReadLong(data, OP) << 32) | JaguarReadLong(data + 4, OP);
      pixels <<= firstPixShift;
      OPSkipScaledDestinationPixels(clippedDestPixels, hscale, 4, 16, pitch << 3,
            &data, &iwidth, &horizontalRemainder, &pixCount, &pixels);

      while ((int32_t)iwidth > 0 && visibleDestPixels > 0)
      {
         uint8_t bits = pixels >> 60;

#ifndef OP_USES_PALETTE_ZERO
         if (flagTRANS && bits == 0)
#else
            if (flagTRANS && (paletteRAM16[index | bits] == 0))
#endif
               ;	// Do nothing...
            else
            {
               if (!flagRMW)
                  // This is the *only* correct use of endian-dependent code
                  // (i.e., mem-to-mem direct copying)!
                  *(uint16_t *)currentLineBuffer = paletteRAM16[index | bits];
               else
                  *currentLineBuffer =
                     BLEND_CR(*currentLineBuffer, paletteRAM[(index | bits) << 1]),
                     *(currentLineBuffer + 1) =
                        BLEND_Y(*(currentLineBuffer + 1), paletteRAM[((index | bits) << 1) + 1]);
            }

         currentLineBuffer += lbufDelta;
         visibleDestPixels--;

         OPAdvanceScaledSource(&horizontalRemainder, hscale, 4, &pixCount, &pixels);

         if (pixCount > 15)
         {
            int phrasesToSkip = pixCount / 16, pixelShift = pixCount % 16;

            data += (pitch << 3) * phrasesToSkip;
            pixels = ((uint64_t)JaguarReadLong(data, OP) << 32) | JaguarReadLong(data + 4, OP);
            pixels <<= 4 * pixelShift;
            iwidth -= phrasesToSkip;
            pixCount = pixelShift;
         }
      }
   }
   else if (depth == 3)							// 8 BPP
   {
      // The LSB is OPFLAG_REFLECT, so sign extend it and or 2 into it.
      int32_t lbufDelta = ((int8_t)((flags << 7) & 0xFF) >> 5) | 0x02;

      int pixCount = (int)firstPixPixels;
      uint64_t pixels = ((uint64_t)JaguarReadLong(data, OP) << 32) | JaguarReadLong(data + 4, OP);

      pixels <<= firstPixShift;
      OPSkipScaledDestinationPixels(clippedDestPixels, hscale, 8, 8, pitch << 3,
            &data, &iwidth, &horizontalRemainder, &pixCount, &pixels);

      while ((int32_t)iwidth > 0 && visibleDestPixels > 0)
      {
         uint8_t bits = pixels >> 56;

#ifndef OP_USES_PALETTE_ZERO
         if (flagTRANS && bits == 0)
#else
            if (flagTRANS && (paletteRAM16[bits] == 0))
#endif
               ;	// Do nothing...
            else
            {
               if (!flagRMW)
               {
                  // This is the *only* correct use of endian-dependent code
                  // (i.e., mem-to-mem direct copying)!
                  *(uint16_t *)currentLineBuffer = paletteRAM16[bits];

                  /* Hi-res Stage 3 for scaled CLUT objects (issue #367,
                   * promoting design section 6.5's CLUT deferral on corpus
                   * evidence -- see op_hires_scale_peek_clut).  Same shape as
                   * the 16bpp branch below: a non-1.0x HSCALE means this one
                   * stock pixel was sampled from a source with more
                   * horizontal detail than the destination kept, so peek one
                   * sub-step ahead and place the two point samples in output
                   * column order (REFLECT flips only the destination step, so
                   * the physically-left sub-column holds the later source
                   * sample).
                   *
                   * No RAM-shadow resolve here, unlike 16bpp: the source word
                   * holds packed palette INDICES, not colour, so a shadow
                   * lookup keyed on the stock 16-bit value would be
                   * meaningless.  The peek is the only path.
                   *
                   * Colours are composed from the palette BYTE array rather
                   * than paletteRAM16: the uint16 read above is deliberately
                   * host-endian for the mem-to-mem copy, whereas the shadow
                   * surface stores values in the same (hi << 8) | lo form the
                   * 16bpp branch uses.  Reusing paletteRAM16 here would
                   * byte-swap every sub-sample on a little-endian host. */
                  if (shadowHiresActive && shadowHiresN == 2 && hscale != 0x20)
                  {
                     shadowfb_sub cols[SHADOWFB_HIRES_MAX_N];
                     shadowfb_sub s0, s1;
                     uint16_t stockVal;
                     int lbIdx;
                     int peekIdx;

                     stockVal = ((uint16_t)paletteRAM[bits << 1] << 8)
                              | paletteRAM[(bits << 1) + 1];
                     lbIdx = (int)((currentLineBuffer - &tomRam8[0x1800]) >> 1);

                     peekIdx = op_hires_scale_peek_clut(data, pixCount,
                           horizontalRemainder, hscale, (uint32_t)(pitch << 3),
                           8, iwidth, (int)bits);

                     s0.value16 = stockVal;
                     s0.frac16  = 0;
                     /* The stock pixel already passed the TRANS test; the
                      * peeked index never did.  A transparent peek is padding
                      * the stock walk would draw nothing for -- degrade that
                      * sub-column to the stock sample rather than to whatever
                      * palette entry 0 happens to hold. */
#ifndef OP_USES_PALETTE_ZERO
                     if (flagTRANS && peekIdx == 0)
#else
                     if (flagTRANS && paletteRAM16[peekIdx] == 0)
#endif
                        s1.value16 = stockVal;
                     else
                        s1.value16 = ((uint16_t)paletteRAM[peekIdx << 1] << 8)
                                   | paletteRAM[(peekIdx << 1) + 1];
                     s1.frac16 = 0;

                     if (flagREFLECT)
                     {
                        cols[0] = s1;
                        cols[1] = s0;
                     }
                     else
                     {
                        cols[0] = s0;
                        cols[1] = s1;
                     }

                     ShadowHiresLineFromScaledSamples(lbIdx, cols, stockVal);
                  }
               }
               else
               {
                  *currentLineBuffer =
                     BLEND_CR(*currentLineBuffer, paletteRAM[bits << 1]),
                     *(currentLineBuffer + 1) =
                        BLEND_Y(*(currentLineBuffer + 1), paletteRAM[(bits << 1) + 1]);
               }
            }

         currentLineBuffer += lbufDelta;
         visibleDestPixels--;

         OPAdvanceScaledSource(&horizontalRemainder, hscale, 8, &pixCount, &pixels);

         if (pixCount > 7)
         {
            int phrasesToSkip = pixCount / 8, pixelShift = pixCount % 8;

            data += (pitch << 3) * phrasesToSkip;
            pixels = ((uint64_t)JaguarReadLong(data, OP) << 32) | JaguarReadLong(data + 4, OP);
            pixels <<= 8 * pixelShift;
            iwidth -= phrasesToSkip;
            pixCount = pixelShift;
         }
      }
   }
   else if (depth == 4)							// 16 BPP
   {
      // The LSB is OPFLAG_REFLECT, so sign extend it and OR 2 into it.
      int32_t lbufDelta = ((int8_t)((flags << 7) & 0xFF) >> 5) | 0x02;

      int pixCount = (int)firstPixPixels;
      uint64_t pixels = ((uint64_t)JaguarReadLong(data, OP) << 32) | JaguarReadLong(data + 4, OP);

      pixels <<= firstPixShift;
      OPSkipScaledDestinationPixels(clippedDestPixels, hscale, 16, 4, pitch << 3,
            &data, &iwidth, &horizontalRemainder, &pixCount, &pixels);

      while ((int32_t)iwidth > 0 && visibleDestPixels > 0)
      {
         uint8_t bitsHi = pixels >> 56, bitsLo = pixels >> 48;

         //This doesn't seem right... Let's try the encoded black value ($8800):
         //Apparently, CRY 0 maps to $8800...
         if (flagTRANS && ((bitsLo | bitsHi) == 0))
            //				if (flagTRANS && (bitsHi == 0x88) && (bitsLo == 0x00))
            ;	// Do nothing...
         else
         {
            if (!flagRMW)
            {
               *currentLineBuffer = bitsHi;
               *(currentLineBuffer + 1) = bitsLo;
               /* True-color: resolve the source RAM word against the
                * shadow framebuffer and mirror it into the shadow line
                * buffer at the same pixel index (see shadowfb.h).
                * pixCount is the pixel index within the phrase at data. */
               if (shadowFBActive)
                  ShadowFBLineFromRAM(
                        (int)((currentLineBuffer - &tomRam8[0x1800]) >> 1),
                        data + ((uint32_t)pixCount << 1),
                        (uint16_t)(((uint16_t)bitsHi << 8) | bitsLo));
               /* Hi-res: same resolve against the Nx shadow surface,
                * inside the OP's single per-scanline pass (shadowfb.h).
                *
                * Stage 3 (design section 6.4): a non-1.0x HSCALE means
                * this one stock pixel was sampled from a source bitmap
                * that has more horizontal detail than the destination
                * kept.  Peek one local HSCALE sub-step ahead
                * (op_hires_scale_peek, N=2 only -- the Stage 1 scope
                * fence) and place the two point samples in output
                * column order: source consumption is always forward
                * regardless of REFLECT (only the destination step
                * direction flips, see lbufDelta above), so under
                * REFLECT the physically-left sub-column holds the
                * *later* source sample to keep the block's own
                * left/right consistent with the mirrored image.
                * Every pixel -- scaled or not -- still tries the
                * RAM-shadow resolve first (a hit means this source
                * address was itself a prior blit's supersampled write;
                * Stage 1/2 semantics, unchanged); the peek is only the
                * miss fallback.  hscale==0x20 (no scaling -- nothing
                * to recover) never peeks.  Sub-rows are not
                * supersampled here (only
                * one source row is ever visible inside one
                * OPProcessScaledBitmap call -- VSCALE's row selection
                * lives in the OBJECT_TYPE_SCALE dispatch outside this
                * function, and its REMAINDER writeback must stay
                * untouched), so all N sub-rows repeat the same N
                * columns. */
               if (shadowHiresActive)
               {
                  uint16_t stockVal;
                  int lbIdx;

                  stockVal = (uint16_t)(((uint16_t)bitsHi << 8) | bitsLo);
                  lbIdx = (int)((currentLineBuffer - &tomRam8[0x1800]) >> 1);

                  if (shadowHiresN == 2 && hscale != 0x20)
                  {
                     /* A RAM-shadow hit (this source word was itself a
                      * supersampled blit destination -- render-to-
                      * texture is a normal Jaguar idiom) carries real
                      * Stage 1/2 per-subpixel content and always beats
                      * two point samples: resolve it FIRST, and peek
                      * only on a miss.  This also keeps the resolve
                      * counters (shadowfb.h) counting every scaled
                      * pixel exactly once. */
                     if (!ShadowHiresLineFromRAM(lbIdx,
                              data + ((uint32_t)pixCount << 1), stockVal))
                     {
                        shadowfb_sub cols[SHADOWFB_HIRES_MAX_N];
                        shadowfb_sub s0, s1;

                        s0.value16 = stockVal;
                        s0.frac16 = 0;
                        s1.value16 = op_hires_scale_peek(data, pixCount,
                              horizontalRemainder, hscale,
                              (uint32_t)(pitch << 3), 4, iwidth, stockVal);
                        /* The stock pixel above already passed the TRANS
                         * test; the peeked word never did.  A zero word
                         * one half-step ahead on a TRANS object is
                         * transparent padding the stock walk would draw
                         * nothing for -- degrade that sub-column to the
                         * stock sample, never to encoded-black CRY 0. */
                        if (flagTRANS && s1.value16 == 0)
                           s1.value16 = stockVal;
                        s1.frac16 = 0;

                        if (flagREFLECT)
                        {
                           cols[0] = s1;
                           cols[1] = s0;
                        }
                        else
                        {
                           cols[0] = s0;
                           cols[1] = s1;
                        }

                        ShadowHiresLineFromScaledSamples(lbIdx, cols, stockVal);
                     }
                  }
                  else
                     ShadowHiresLineFromRAM(lbIdx,
                           data + ((uint32_t)pixCount << 1), stockVal);
               }
            }
            else
               *currentLineBuffer =
                  BLEND_CR(*currentLineBuffer, bitsHi),
                  *(currentLineBuffer + 1) =
                     BLEND_Y(*(currentLineBuffer + 1), bitsLo);
         }

         currentLineBuffer += lbufDelta;
         visibleDestPixels--;

         OPAdvanceScaledSource(&horizontalRemainder, hscale, 16, &pixCount, &pixels);
         if (pixCount > 3)
         {
            int phrasesToSkip = pixCount / 4, pixelShift = pixCount % 4;

            data += (pitch << 3) * phrasesToSkip;
            pixels = ((uint64_t)JaguarReadLong(data, OP) << 32) | JaguarReadLong(data + 4, OP);
            pixels <<= 16 * pixelShift;

            iwidth -= phrasesToSkip;

            pixCount = pixelShift;
         }
      }
   }
   else if (depth == 5)							// 24 BPP
   {
      // Not sure, but I think RMW only works with 16 BPP and below, and only in CRY mode...
      // The LSB is OPFLAG_REFLECT, so sign extend it and or 4 into it.
      int32_t lbufDelta = ((int8_t)((flags << 7) & 0xFF) >> 4) | 0x04;

      while (iwidth--)
      {
         unsigned i;
         // Fetch phrase...
         uint64_t pixels = ((uint64_t)JaguarReadLong(data, OP) << 32) | JaguarReadLong(data + 4, OP);
         data += pitch << 3;						// Multiply pitch * 8 (optimize: precompute this value)

         for(i=0; i<2; i++)
         {
            uint8_t bits3 = pixels >> 56, bits2 = pixels >> 48,
                    bits1 = pixels >> 40, bits0 = pixels >> 32;

            /* Bounds-clamped for the same reason the fixed-bitmap stores
             * are (#565, and the OP_LBUF_IN_BOUNDS comment above): this
             * loop runs on the raw phrase `iwidth`, not on the clamped
             * visibleDestPixels the 1-16 BPP branches above bound
             * themselves with, and lbufDelta sign-extends OPFLAG_REFLECT
             * -- so a reflected object with a garbage iwidth walks
             * backward out of tomRam8 with nothing to stop it.  Four
             * bytes per store here rather than two, so it leaves the
             * buffer faster than the case that actually crashed. */
            if (flagTRANS && (bits3 | bits2 | bits1 | bits0) == 0)
               ;	// Do nothing...
            else if (OP_LBUF_IN_BOUNDS(currentLineBuffer, 4))
               *currentLineBuffer = bits3,
                  *(currentLineBuffer + 1) = bits2,
                  *(currentLineBuffer + 2) = bits1,
                  *(currentLineBuffer + 3) = bits0;

            currentLineBuffer += lbufDelta;
            pixels <<= 32;
         }
      }
   }
}
