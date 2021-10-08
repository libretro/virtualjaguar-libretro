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
#include "gpu.h"
#include "jaguar.h"
#include "m68000/m68kinterface.h"
#include "vjag_memory.h"
#include "tom.h"

#define BLEND_Y(dst, src)	op_blend_y[(((uint16_t)dst<<8)) | ((uint16_t)(src))]
#define BLEND_CR(dst, src)	op_blend_cr[(((uint16_t)dst)<<8) | ((uint16_t)(src))]

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

// Private function prototypes

void OPProcessFixedBitmap(uint64_t p0, uint64_t p1, bool render);
void OPProcessScaledBitmap(uint64_t p0, uint64_t p1, uint64_t p2, bool render);
void OPDiscoverObjects(uint32_t address);
uint64_t OPLoadPhrase(uint32_t offset);

// Local global variables

// Blend tables (64K each)
static uint8_t op_blend_y[0x10000];
static uint8_t op_blend_cr[0x10000];
// There may be a problem with this "RAM" overlapping (and thus being independent of)
// some of the regular TOM RAM...
uint8_t objectp_running = 0;

static uint8_t op_bitmap_bit_depth[8] = { 1, 2, 4, 8, 16, 24, 32, 0 };
static uint32_t op_pointer;

int32_t phraseWidthToPixels[8] = { 64, 32, 16, 8, 4, 2, 0, 0 };


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
   objectp_running = 0;
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


// This is WRONG, since the OBF is only 16 bits wide!!! [FIXED]

uint32_t OPGetStatusRegister(void)
{
   return GET16(tomRam8, 0x26);
}


// This is WRONG, since the OBF is only 16 bits wide!!! [FIXED]

void OPSetStatusRegister(uint32_t data)
{
   tomRam8[0x26] = (data & 0x0000FF00) >> 8;
   tomRam8[0x27] |= (data & 0xFE);
}


void OPSetCurrentObject(uint64_t object)
{
   //Not sure this is right... Wouldn't it just be stored 64 bit BE?
   // Stored as least significant 32 bits first, ms32 last in big endian
   /*	objectp_ram[0x13] = object & 0xFF; object >>= 8;
      objectp_ram[0x12] = object & 0xFF; object >>= 8;
      objectp_ram[0x11] = object & 0xFF; object >>= 8;
      objectp_ram[0x10] = object & 0xFF; object >>= 8;

      objectp_ram[0x17] = object & 0xFF; object >>= 8;
      objectp_ram[0x16] = object & 0xFF; object >>= 8;
      objectp_ram[0x15] = object & 0xFF; object >>= 8;
      objectp_ram[0x14] = object & 0xFF;*/
   // Let's try regular good old big endian...
   tomRam8[0x17] = object & 0xFF; object >>= 8;
   tomRam8[0x16] = object & 0xFF; object >>= 8;
   tomRam8[0x15] = object & 0xFF; object >>= 8;
   tomRam8[0x14] = object & 0xFF; object >>= 8;

   tomRam8[0x13] = object & 0xFF; object >>= 8;
   tomRam8[0x12] = object & 0xFF; object >>= 8;
   tomRam8[0x11] = object & 0xFF; object >>= 8;
   tomRam8[0x10] = object & 0xFF;
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
//#warning "Need to fix this so that when an GPU object IRQ happens, we can pick up OP processing where we left off. !!! FIX !!!"
void OPProcessList(int halfline, bool render)
{
   bool inhibit;
   int bitmapCounter = 0;
   uint32_t opCyclesToRun = 30000;					// This is a pulled-out-of-the-air value (will need to be fixed, obviously!)

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
               // *** BEGIN OP PROCESSOR TESTING ONLY ***
               bitmapCounter++;
               if (!inhibit)	// For OP testing only!
                  // *** END OP PROCESSOR TESTING ONLY ***
                  if (halfline >= ypos && height > 0)
                  {
                     uint64_t data, dwidth;
                     // Believe it or not, this is what the OP actually does...
                     // which is why they're required to be on a dphrase boundary!
                     uint64_t p1 = OPLoadPhrase(oldOPP | 0x08);
                     uint64_t p2 = OPLoadPhrase(oldOPP | 0x10);
                     op_pointer += 16;
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
               // *** BEGIN OP PROCESSOR TESTING ONLY ***
               bitmapCounter++;
               if (!inhibit)	// For OP testing only!
                  // *** END OP PROCESSOR TESTING ONLY ***
                  if (halfline >= ypos && height > 0)
                  {
                     uint16_t remainder;
                     uint8_t vscale;
                     uint64_t p2;
                     uint64_t p1 = OPLoadPhrase(op_pointer);
                     op_pointer += 8;
                     p2 = OPLoadPhrase(op_pointer);
                     op_pointer += 8;
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
//#warning "Need to fix OP GPU IRQ handling! !!! FIX !!!"
               OPSetCurrentObject(p0);
               GPUSetIRQLine(3, ASSERT_LINE);
               //Also, OP processing is suspended from this point until OBF (F00026) is written to...
               // !!! FIX !!!
               //Do something like:
               //OPSuspendedByGPU = true;
               //Dunno if the OP keeps processing from where it was interrupted, or if it just continues
               //on the next halfline...
               // --> It continues from where it was interrupted! !!! FIX !!!
               break;
            }
         case OBJECT_TYPE_BRANCH:
            {
               uint16_t ypos = (p0 >> 3) & 0x7FF;
               // JTRM is wrong: CC is bits 14-16 (3 bits, *not* 2)
               uint8_t  cc   = (p0 >> 14) & 0x07;
               uint32_t link = (p0 >> 21) & 0x3FFFF8;

               switch (cc)
               {
                  case CONDITION_EQUAL:
                     if (halfline == ypos || ypos == 0x7FF)
                        op_pointer = link;
                     break;
                  case CONDITION_LESS_THAN:
                     if (halfline < ypos)
                        op_pointer = link;
                     break;
                  case CONDITION_GREATER_THAN:
                     if (halfline > ypos)
                        op_pointer = link;
                     break;
                  case CONDITION_OP_FLAG_SET:
                     if (OPGetStatusRegister() & 0x01)
                        op_pointer = link;
                     break;
                  case CONDITION_SECOND_HALF_LINE:
                     // Branch if bit 10 of HC is set...
                     if (TOMGetHC() & 0x0400)
                        op_pointer = link;
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

      // Here is a little sanity check to keep the OP from locking up the machine
      // when fed bad data. Better would be to count how many actual cycles it used
      // and bail out/reenter to properly simulate an overloaded OP... !!! FIX !!!
//#warning "Better would be to count how many actual cycles it used and bail out/reenter to properly simulate an overloaded OP... !!! FIX !!!"
      opCyclesToRun--;

      if (!opCyclesToRun)
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

   // Is it OK to have a 0 for the data width??? (i.e., undocumented?)
   // Seems to be... Seems that dwidth *can* be zero (i.e., reuse same line) as well.
   // Pitch == 0 is OK too...

   //kludge: Seems that the OP treats iwidth == 0 as iwidth == 1... Need to investigate
   //        on real hardware...
//#warning "!!! Need to investigate iwidth == 0 behavior on real hardware !!!"
   if (iwidth == 0)
      iwidth = 1;

   //	if (!render || op_pointer == 0 || ptr == 0 || pitch == 0)
   //I'm not convinced that we need to concern ourselves with data & op_pointer here either!
   if (!render || iwidth == 0)
      return;

   startPos = xpos;
   endPos = xpos +
      (!flagREFLECT ? (phraseWidthToPixels[depth] * iwidth) - 1
       : -((phraseWidthToPixels[depth] * iwidth) + 1));

   // If the image is completely to the left or right of the line buffer, then bail.
   //If in REFLECT mode, then these values are swapped! !!! FIX !!! [DONE]
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

   //This stuff is probably wrong as well... !!! FIX !!!
   //The strange thing is that it seems to work, but that's no guarantee that it's bulletproof!
   //Yup. Seems that JagMania doesn't work correctly with this...
   //Dunno if this is the problem, but Atari Karts is showing *some* of the road now...
   //	if (!flagREFLECT)

   // NOTE: We're just using endPos to figure out how much, if any, to clip by.
   // ALSO: There may be another case where we start out of bounds and end out of bounds...!
   // !!! FIX !!!
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
      //Note that firstPix should only be honored *if* we start with the 1st phrase of the bitmap
      //i.e., we didn't clip on the margin... !!! FIX !!!
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
               else
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
      int32_t lbufDelta;

      index &= 0xFC;								// Top six bits form CLUT index
      // The LSB is OPFLAG_REFLECT, so sign extend it and or 2 into it.
      lbufDelta = ((int8_t)((flags << 7) & 0xFF) >> 5) | 0x02;

      while (iwidth--)
      {
         unsigned i;
         // Fetch phrase...
         uint64_t pixels = ((uint64_t)JaguarReadLong(data, OP) << 32) | JaguarReadLong(data + 4, OP);
         data += pitch;

         for(i=0; i<32; i++)
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
               else
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
      }
   }
   else if (depth == 2)							// 4 BPP
   {
      int32_t lbufDelta;
      index &= 0xF0;								// Top four bits form CLUT index
      // The LSB is OPFLAG_REFLECT, so sign extend it and or 2 into it.
      lbufDelta = ((int8_t)((flags << 7) & 0xFF) >> 5) | 0x02;

      while (iwidth--)
      {
         unsigned i;
         // Fetch phrase...
         uint64_t pixels = ((uint64_t)JaguarReadLong(data, OP) << 32) | JaguarReadLong(data + 4, OP);
         data += pitch;

         for(i=0; i<16; i++)
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
               else
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
      }
   }
   else if (depth == 3)							// 8 BPP
   {
      int i;
      // The LSB is OPFLAG_REFLECT, so sign extend it and or 2 into it.
      int32_t lbufDelta = ((int8_t)((flags << 7) & 0xFF) >> 5) | 0x02;

      // Fetch 1st phrase...
      uint64_t pixels = ((uint64_t)JaguarReadLong(data, OP) << 32) | JaguarReadLong(data + 4, OP);
      //Note that firstPix should only be honored *if* we start with the 1st phrase of the bitmap
      //i.e., we didn't clip on the margin... !!! FIX !!!
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
               else
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
      int32_t lbufDelta = ((int8_t)((flags << 7) & 0xFF) >> 5) | 0x02;

      while (iwidth--)
      {
         unsigned i;
         // Fetch phrase...
         uint64_t pixels = ((uint64_t)JaguarReadLong(data, OP) << 32) | JaguarReadLong(data + 4, OP);
         data += pitch;

         for(i=0; i<4; i++)
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
            else
            {
               if (!flagRMW)
                  *currentLineBuffer = bitsHi,
                     *(currentLineBuffer + 1) = bitsLo;
               else
                  *currentLineBuffer =
                     BLEND_CR(*currentLineBuffer, bitsHi),
                     *(currentLineBuffer + 1) =
                        BLEND_Y(*(currentLineBuffer + 1), bitsLo);
            }

            currentLineBuffer += lbufDelta;
            pixels <<= 16;
         }
      }
   }
   else if (depth == 5)							// 24 BPP
   {
      //Looks like Iron Soldier is the only game that uses 24BPP mode...
      //There *might* be others...
      // Not sure, but I think RMW only works with 16 BPP and below, and only in CRY mode...
      // The LSB of flags is OPFLAG_REFLECT, so sign extend it and OR 4 into it.
      int32_t lbufDelta = ((int8_t)((flags << 7) & 0xFF) >> 4) | 0x04;

      while (iwidth--)
      {
         unsigned i;
         // Fetch phrase...
         uint64_t pixels = ((uint64_t)JaguarReadLong(data, OP) << 32) | JaguarReadLong(data + 4, OP);
         data += pitch;

         for(i=0; i<2; i++)
         {
            // We don't use a 32-bit var here because of endian issues...!
            uint8_t bits3 = pixels >> 56, bits2 = pixels >> 48,
                    bits1 = pixels >> 40, bits0 = pixels >> 32;

            if (flagTRANS && (bits3 | bits2 | bits1 | bits0) == 0)
               ;	// Do nothing...
            else
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

// Store scaled bitmap in line buffer
void OPProcessScaledBitmap(uint64_t p0, uint64_t p1, uint64_t p2, bool render)
{
   uint32_t lbufAddress;
   uint8_t * currentLineBuffer;
   uint32_t scaledPhrasePixelsUS;
   uint32_t clippedWidth = 0, phraseClippedWidth = 0, dataClippedWidth = 0;
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
   //Optimize: break these out to their own BOOL values [DONE]
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
   // Hmm. It seems that fixing the horizontal scale necessitated re-fixing this. Not sure why,
   // but seems to be consistent with the vertical scaling now (and it may turn out to be wrong!)...
   uint16_t horizontalRemainder = hscale;				// Not sure if it starts full, but seems reasonable [It's not!]
   //	uint8_t horizontalRemainder = 0;					// Let's try zero! Seems to work! Yay! [No, it doesn't!]
   int32_t scaledWidthInPixels = (iwidth * phraseWidthToPixels[depth] * hscale) >> 5;
   uint32_t scaledPhrasePixels = (phraseWidthToPixels[depth] * hscale) >> 5;
   int32_t startPos = xpos;
   int32_t endPos = xpos +
      (!flagREFLECT ? scaledWidthInPixels - 1 : -(scaledWidthInPixels + 1));

   // Looks like an hscale of zero means don't draw!
   if (!render || iwidth == 0 || hscale == 0)
      return;

   // If the image is completely to the left or right of the line buffer, then bail.
   //If in REFLECT mode, then these values are swapped! !!! FIX !!! [DONE]
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

   //This stuff is probably wrong as well... !!! FIX !!!
   //The strange thing is that it seems to work, but that's no guarantee that it's bulletproof!
   //Yup. Seems that JagMania doesn't work correctly with this...
   //Dunno if this is the problem, but Atari Karts is showing *some* of the road now...
   //Actually, it is! Or, it was. It doesn't seem to be clipping here, so the problem lies
   //elsewhere! Hmm. Putting the scaling code into the 1/2/8 BPP cases seems to draw the ground
   // a bit more accurately... Strange!
   //It's probably a case of the REFLECT flag being set and the background being written
   //from the right side of the screen...
   //But no, it isn't... At least if the diagnostics are telling the truth!

   // NOTE: We're just using endPos to figure out how much, if any, to clip by.
   // ALSO: There may be another case where we start out of bounds and end out of bounds...!
   // !!! FIX !!!

   //There's a problem here with scaledPhrasePixels in that it can be forced to zero when
   //the scaling factor is small. So fix it already! !!! FIX !!!
   //NOTE: I'm almost 100% sure that this is wrong... And it is! :-p

   //Try a simple example...
   // Let's say we have a 8 BPP scanline with an hscale of $80 (4). Our xpos is -10,
   // non-flipped. Pixels in the bitmap are XYZXYZXYZXYZXYZ.
   // Scaled up, they would be XXXXYYYYZZZZXXXXYYYYZZZZXXXXYYYYZZZZ...
   //
   // Normally, we would expect this in the line buffer:
   // ZZXXXXYYYYZZZZXXXXYYYYZZZZ...
   //
   // But instead we're getting:
   // XXXXYYYYZZZZXXXXYYYYZZZZ...
   //
   // or are we??? It would seem so, simply by virtue of the fact that we're NOT starting
   // on negative boundary--or are we? Hmm...
   // cw = 10, dcw = pcw = 10 / ([8 * 4 = 32] 32) = 0, sp = -10
   //
   // Let's try a real world example:
   //
   //OP: Scaled bitmap (70, 8 BPP, spp=28) sp (-400) < 0... [new sp=-8, cw=400, dcw=pcw=14]
   //OP: Scaled bitmap (6F, 8 BPP, spp=27) sp (-395) < 0... [new sp=-17, cw=395, dcw=pcw=14]
   //
   // Really, spp is 27.75 in the second case...
   // So... If we do 395 / 27.75, we get 14. Ok so far... If we scale that against the
   // start position (14 * 27.75), we get -6.5... NOT -17!

   //Now it seems we're working OK, at least for the first case...
   scaledPhrasePixelsUS = phraseWidthToPixels[depth] * hscale;

   if (startPos < 0)			// Case #1: Begin out, end in, L to R
   {
      clippedWidth = (0 - startPos) << 5,
                   //		dataClippedWidth = phraseClippedWidth = clippedWidth / scaledPhrasePixels,
                   dataClippedWidth = phraseClippedWidth = (clippedWidth / scaledPhrasePixelsUS) >> 5,
                   //		startPos = 0 - (clippedWidth % scaledPhrasePixels);
                   startPos += (dataClippedWidth * scaledPhrasePixelsUS) >> 5;
   }

   if (endPos < 0)				// Case #2: Begin in, end out, R to L
      clippedWidth = 0 - endPos,
                   phraseClippedWidth = clippedWidth / scaledPhrasePixels;

   if (endPos > lbufWidth)		// Case #3: Begin in, end out, L to R
      clippedWidth = endPos - lbufWidth,
                   phraseClippedWidth = clippedWidth / scaledPhrasePixels;

   if (startPos > lbufWidth)	// Case #4: Begin out, end in, R to L
      clippedWidth = startPos - lbufWidth,
                   dataClippedWidth = phraseClippedWidth = clippedWidth / scaledPhrasePixels,
                   startPos = lbufWidth + (clippedWidth % scaledPhrasePixels);

   // If the image is sitting on the line buffer left or right edge, we need to compensate
   // by decreasing the image phrase width accordingly.
   iwidth -= phraseClippedWidth;

   // Also, if we're clipping the phrase we need to make sure we're in the correct part of
   // the pixel data.
   //	data += phraseClippedWidth * (pitch << 3);
   data += dataClippedWidth * (pitch << 3);

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

      int pixCount = 0;
      uint64_t pixels = ((uint64_t)JaguarReadLong(data, OP) << 32) | JaguarReadLong(data + 4, OP);

      while ((int32_t)iwidth > 0)
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

         /*
            The reason we subtract the horizontalRemainder *after* the test is because we had too few
            bytes for horizontalRemainder to properly recognize a negative number. But now it's 16 bits
            wide, so we could probably go back to that (as long as we make it an int16_t and not a uint16!)
            */
         while (horizontalRemainder < 0x20)		// I.e., it's <= 1.0 (*before* subtraction)
         {
            horizontalRemainder += hscale;
            pixCount++;
            pixels <<= 1;
         }
         horizontalRemainder -= 0x20;		// Subtract 1.0f in [3.5] fixed point format

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
      int pixCount = 0;
      uint64_t pixels;

      index &= 0xFC;								// Top six bits form CLUT index
      // The LSB is OPFLAG_REFLECT, so sign extend it and or 2 into it.
      lbufDelta = ((int8_t)((flags << 7) & 0xFF) >> 5) | 0x02;

      pixels = ((uint64_t)JaguarReadLong(data, OP) << 32) | JaguarReadLong(data + 4, OP);

      while ((int32_t)iwidth > 0)
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

         while (horizontalRemainder < 0x20)		// I.e., it's <= 1.0 (*before* subtraction)
         {
            horizontalRemainder += hscale;
            pixCount++;
            pixels <<= 2;
         }
         horizontalRemainder -= 0x20;		// Subtract 1.0f in [3.5] fixed point format

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
      int pixCount = 0;
      int32_t lbufDelta;
      uint64_t pixels;

      index &= 0xF0;								// Top four bits form CLUT index
      // The LSB is OPFLAG_REFLECT, so sign extend it and or 2 into it.
      lbufDelta = ((int8_t)((flags << 7) & 0xFF) >> 5) | 0x02;

      pixels = ((uint64_t)JaguarReadLong(data, OP) << 32) | JaguarReadLong(data + 4, OP);

      while ((int32_t)iwidth > 0)
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

         while (horizontalRemainder < 0x20)		// I.e., it's <= 0 (*before* subtraction)
         {
            horizontalRemainder += hscale;
            pixCount++;
            pixels <<= 4;
         }
         horizontalRemainder -= 0x20;		// Subtract 1.0f in [3.5] fixed point format

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

      int pixCount = 0;
      uint64_t pixels = ((uint64_t)JaguarReadLong(data, OP) << 32) | JaguarReadLong(data + 4, OP);

      while ((int32_t)iwidth > 0)
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

         while (horizontalRemainder < 0x20)		// I.e., it's <= 1.0 (*before* subtraction)
         {
            horizontalRemainder += hscale;
            pixCount++;
            pixels <<= 8;
         }
         horizontalRemainder -= 0x20;		// Subtract 1.0f in [3.5] fixed point format

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

      int pixCount = 0;
      uint64_t pixels = ((uint64_t)JaguarReadLong(data, OP) << 32) | JaguarReadLong(data + 4, OP);

      while ((int32_t)iwidth > 0)
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
               *currentLineBuffer = bitsHi,
                  *(currentLineBuffer + 1) = bitsLo;
            else
               *currentLineBuffer =
                  BLEND_CR(*currentLineBuffer, bitsHi),
                  *(currentLineBuffer + 1) =
                     BLEND_Y(*(currentLineBuffer + 1), bitsLo);
         }

         currentLineBuffer += lbufDelta;

         while (horizontalRemainder < 0x20)		// I.e., it's <= 1.0 (*before* subtraction)
         {
            horizontalRemainder += hscale;
            pixCount++;
            pixels <<= 16;
         }
         horizontalRemainder -= 0x20;		// Subtract 1.0f in [3.5] fixed point format
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

            if (flagTRANS && (bits3 | bits2 | bits1 | bits0) == 0)
               ;	// Do nothing...
            else
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
