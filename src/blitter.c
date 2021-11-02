//
// Blitter core
//
// by James Hammons
// (C) 2010 Underground Software
//
// JLH = James Hammons <jlhamm@acm.org>
//
// Who  When        What
// ---  ----------  -------------------------------------------------------------
// JLH  01/16/2010  Created this log ;-)
//

//
// I owe a debt of gratitude to Curt Vendel and to John Mathieson--to Curt
// for supplying the Oberon ASIC nets and to John for making them available
// to Curt. ;-) Without that excellent documentation which shows *exactly*
// what's going on inside the TOM chip, we'd all still be guessing as to how
// the wily blitter and other pieces of the Jaguar puzzle actually work.
// Now how about those JERRY ASIC nets gentlemen...? [We have those now!] ;-)
//

#include "blitter.h"

#include <stdlib.h>
#include <string.h>
#include "jaguar.h"
#include "settings.h"

// Various conditional compilation goodies...

#define USE_ORIGINAL_BLITTER
#define USE_MIDSUMMER_BLITTER_MKII

// Local global variables

// Blitter register RAM (most of it is hidden from the user)

static uint8_t blitter_ram[0x100];

// Other crapola

void BlitterMidsummer(uint32_t cmd);
void BlitterMidsummer2(void);

#define REG(A)	(((uint32_t)blitter_ram[(A)] << 24) | ((uint32_t)blitter_ram[(A)+1] << 16) \
				| ((uint32_t)blitter_ram[(A)+2] << 8) | (uint32_t)blitter_ram[(A)+3])
#define WREG(A,D)	(blitter_ram[(A)] = ((D)>>24)&0xFF, blitter_ram[(A)+1] = ((D)>>16)&0xFF, \
					blitter_ram[(A)+2] = ((D)>>8)&0xFF, blitter_ram[(A)+3] = (D)&0xFF)

// Blitter registers (offsets from F02200)

#define A1_BASE			((uint32_t)0x00)
#define A1_FLAGS		   ((uint32_t)0x04)
#define A1_CLIP			((uint32_t)0x08)	// Height and width values for clipping
#define A1_PIXEL		   ((uint32_t)0x0C)	// Integer part of the pixel (Y.i and X.i)
#define A1_STEP			((uint32_t)0x10)	// Integer part of the step
#define A1_FSTEP		   ((uint32_t)0x14)	// Fractional part of the step
#define A1_FPIXEL		   ((uint32_t)0x18)	// Fractional part of the pixel (Y.f and X.f)
#define A1_INC			   ((uint32_t)0x1C)	// Integer part of the increment
#define A1_FINC			((uint32_t)0x20)	// Fractional part of the increment
#define A2_BASE			((uint32_t)0x24)
#define A2_FLAGS		   ((uint32_t)0x28)
#define A2_MASK			((uint32_t)0x2C)	// Modulo values for x and y (M.y  and M.x)
#define A2_PIXEL		   ((uint32_t)0x30)	// Integer part of the pixel (no fractional part for A2)
#define A2_STEP			((uint32_t)0x34)	// Integer part of the step (no fractional part for A2)
#define COMMAND			((uint32_t)0x38)
#define PIXLINECOUNTER	((uint32_t)0x3C)	// Inner & outer loop values
#define SRCDATA			((uint32_t)0x40)
#define DSTDATA			((uint32_t)0x48)
#define DSTZ			   ((uint32_t)0x50)
#define SRCZINT			((uint32_t)0x58)
#define SRCZFRAC		   ((uint32_t)0x60)
#define PATTERNDATA		((uint32_t)0x68)
#define INTENSITYINC	   ((uint32_t)0x70)
#define ZINC			   ((uint32_t)0x74)
#define COLLISIONCTRL	((uint32_t)0x78)
#define PHRASEINT0		((uint32_t)0x7C)
#define PHRASEINT1	   ((uint32_t)0x80)
#define PHRASEINT2	   ((uint32_t)0x84)
#define PHRASEINT3	   ((uint32_t)0x88)
#define PHRASEZ0		   ((uint32_t)0x8C)
#define PHRASEZ1		   ((uint32_t)0x90)
#define PHRASEZ2		   ((uint32_t)0x94)
#define PHRASEZ3		   ((uint32_t)0x98)

// Blitter command bits

#define SRCEN			(cmd & 0x00000001)
#define SRCENZ			(cmd & 0x00000002)
#define SRCENX			(cmd & 0x00000004)
#define DSTEN			(cmd & 0x00000008)
#define DSTENZ			(cmd & 0x00000010)
#define DSTWRZ			(cmd & 0x00000020)
#define CLIPA1			(cmd & 0x00000040)

#define UPDA1F			(cmd & 0x00000100)
#define UPDA1			(cmd & 0x00000200)
#define UPDA2			(cmd & 0x00000400)

#define DSTA2			(cmd & 0x00000800)

#define Z_OP_INF		(cmd & 0x00040000)
#define Z_OP_EQU		(cmd & 0x00080000)
#define Z_OP_SUP		(cmd & 0x00100000)

#define LFU_NAN		(cmd & 0x00200000)
#define LFU_NA			(cmd & 0x00400000)
#define LFU_AN			(cmd & 0x00800000)
#define LFU_A			(cmd & 0x01000000)

#define CMPDST			(cmd & 0x02000000)
#define BCOMPEN		(cmd & 0x04000000)
#define DCOMPEN		(cmd & 0x08000000)

#define PATDSEL		(cmd & 0x00010000)
#define ADDDSEL		(cmd & 0x00020000)
#define TOPBEN			(cmd & 0x00004000)
#define TOPNEN			(cmd & 0x00008000)
#define BKGWREN		(cmd & 0x10000000)
#define GOURD			(cmd & 0x00001000)
#define GOURZ			(cmd & 0x00002000)
#define SRCSHADE		(cmd & 0x40000000)


#define XADDPHR      0
#define XADDPIX      1
#define XADD0        2
#define XADDINC      3

#define XSIGNSUB_A1		(REG(A1_FLAGS)&0x080000)
#define XSIGNSUB_A2		(REG(A2_FLAGS)&0x080000)

#define YSIGNSUB_A1		(REG(A1_FLAGS)&0x100000)
#define YSIGNSUB_A2		(REG(A2_FLAGS)&0x100000)

#define YADD1_A1		(REG(A1_FLAGS)&0x040000)
#define YADD1_A2		(REG(A2_FLAGS)&0x040000)

/*******************************************************************************
********************** STUFF CUT BELOW THIS LINE! ******************************
*******************************************************************************/
#ifdef USE_ORIGINAL_BLITTER										// We're ditching this crap for now...

//Put 'em back, once we fix the problem!!! [KO]
// 1 bpp pixel read
#define PIXEL_SHIFT_1(a)      (((~a##_x) >> 16) & 7)
#define PIXEL_OFFSET_1(a)     (((((uint32_t)a##_y >> 16) * a##_width / 8) + (((uint32_t)a##_x >> 19) & ~7)) * (1 + a##_pitch) + (((uint32_t)a##_x >> 19) & 7))
#define READ_PIXEL_1(a)       ((JaguarReadByte(a##_addr+PIXEL_OFFSET_1(a), BLITTER) >> PIXEL_SHIFT_1(a)) & 0x01)

// 2 bpp pixel read
#define PIXEL_SHIFT_2(a)      (((~a##_x) >> 15) & 6)
#define PIXEL_OFFSET_2(a)     (((((uint32_t)a##_y >> 16) * a##_width / 4) + (((uint32_t)a##_x >> 18) & ~7)) * (1 + a##_pitch) + (((uint32_t)a##_x >> 18) & 7))
#define READ_PIXEL_2(a)       ((JaguarReadByte(a##_addr+PIXEL_OFFSET_2(a), BLITTER) >> PIXEL_SHIFT_2(a)) & 0x03)

// 4 bpp pixel read
#define PIXEL_SHIFT_4(a)      (((~a##_x) >> 14) & 4)
#define PIXEL_OFFSET_4(a)     (((((uint32_t)a##_y >> 16) * (a##_width/2)) + (((uint32_t)a##_x >> 17) & ~7)) * (1 + a##_pitch) + (((uint32_t)a##_x >> 17) & 7))
#define READ_PIXEL_4(a)       ((JaguarReadByte(a##_addr+PIXEL_OFFSET_4(a), BLITTER) >> PIXEL_SHIFT_4(a)) & 0x0f)

// 8 bpp pixel read
#define PIXEL_OFFSET_8(a)     (((((uint32_t)a##_y >> 16) * a##_width) + (((uint32_t)a##_x >> 16) & ~7)) * (1 + a##_pitch) + (((uint32_t)a##_x >> 16) & 7))
#define READ_PIXEL_8(a)       (JaguarReadByte(a##_addr+PIXEL_OFFSET_8(a), BLITTER))

// 16 bpp pixel read
#define PIXEL_OFFSET_16(a)    (((((uint32_t)a##_y >> 16) * a##_width) + (((uint32_t)a##_x >> 16) & ~3)) * (1 + a##_pitch) + (((uint32_t)a##_x >> 16) & 3))
#define READ_PIXEL_16(a)       (JaguarReadWord(a##_addr+(PIXEL_OFFSET_16(a)<<1), BLITTER))

// 32 bpp pixel read
#define PIXEL_OFFSET_32(a)    (((((uint32_t)a##_y >> 16) * a##_width) + (((uint32_t)a##_x >> 16) & ~1)) * (1 + a##_pitch) + (((uint32_t)a##_x >> 16) & 1))
#define READ_PIXEL_32(a)      (JaguarReadLong(a##_addr+(PIXEL_OFFSET_32(a)<<2), BLITTER))

// pixel read
#define READ_PIXEL(a,f) (\
	 (((f>>3)&0x07) == 0) ? (READ_PIXEL_1(a)) : \
	 (((f>>3)&0x07) == 1) ? (READ_PIXEL_2(a)) : \
	 (((f>>3)&0x07) == 2) ? (READ_PIXEL_4(a)) : \
	 (((f>>3)&0x07) == 3) ? (READ_PIXEL_8(a)) : \
	 (((f>>3)&0x07) == 4) ? (READ_PIXEL_16(a)) : \
	 (((f>>3)&0x07) == 5) ? (READ_PIXEL_32(a)) : 0)

// 16 bpp z data read
#define ZDATA_OFFSET_16(a)     (PIXEL_OFFSET_16(a) + a##_zoffs * 4)
#define READ_ZDATA_16(a)       (JaguarReadWord(a##_addr+(ZDATA_OFFSET_16(a)<<1), BLITTER))

// z data read
#define READ_ZDATA(a,f) (READ_ZDATA_16(a))

// 16 bpp z data write
#define WRITE_ZDATA_16(a,d)     {  JaguarWriteWord(a##_addr+(ZDATA_OFFSET_16(a)<<1), d, BLITTER); }

// z data write
#define WRITE_ZDATA(a,f,d) WRITE_ZDATA_16(a,d);

// 1 bpp r data read
#define READ_RDATA_1(r,a,p)  ((p) ?  ((REG(r+(((uint32_t)a##_x >> 19) & 0x04))) >> (((uint32_t)a##_x >> 16) & 0x1F)) & 0x0001 : (REG(r) & 0x0001))

// 2 bpp r data read
#define READ_RDATA_2(r,a,p)  ((p) ?  ((REG(r+(((uint32_t)a##_x >> 18) & 0x04))) >> (((uint32_t)a##_x >> 15) & 0x3E)) & 0x0003 : (REG(r) & 0x0003))

// 4 bpp r data read
#define READ_RDATA_4(r,a,p)  ((p) ?  ((REG(r+(((uint32_t)a##_x >> 17) & 0x04))) >> (((uint32_t)a##_x >> 14) & 0x28)) & 0x000F : (REG(r) & 0x000F))

// 8 bpp r data read
#define READ_RDATA_8(r,a,p)  ((p) ?  ((REG(r+(((uint32_t)a##_x >> 16) & 0x04))) >> (((uint32_t)a##_x >> 13) & 0x18)) & 0x00FF : (REG(r) & 0x00FF))

// 16 bpp r data read
#define READ_RDATA_16(r,a,p)  ((p) ? ((REG(r+(((uint32_t)a##_x >> 15) & 0x04))) >> (((uint32_t)a##_x >> 12) & 0x10)) & 0xFFFF : (REG(r) & 0xFFFF))

// 32 bpp r data read
#define READ_RDATA_32(r,a,p)  ((p) ? REG(r+(((uint32_t)a##_x >> 14) & 0x04)) : REG(r))

// register data read
#define READ_RDATA(r,a,f,p) (\
	 (((f>>3)&0x07) == 0) ? (READ_RDATA_1(r,a,p)) : \
	 (((f>>3)&0x07) == 1) ? (READ_RDATA_2(r,a,p)) : \
	 (((f>>3)&0x07) == 2) ? (READ_RDATA_4(r,a,p)) : \
	 (((f>>3)&0x07) == 3) ? (READ_RDATA_8(r,a,p)) : \
	 (((f>>3)&0x07) == 4) ? (READ_RDATA_16(r,a,p)) : \
	 (((f>>3)&0x07) == 5) ? (READ_RDATA_32(r,a,p)) : 0)

// 1 bpp pixel write
#define WRITE_PIXEL_1(a,d)       { JaguarWriteByte(a##_addr+PIXEL_OFFSET_1(a), (JaguarReadByte(a##_addr+PIXEL_OFFSET_1(a), BLITTER)&(~(0x01 << PIXEL_SHIFT_1(a))))|(d<<PIXEL_SHIFT_1(a)), BLITTER); }

// 2 bpp pixel write
#define WRITE_PIXEL_2(a,d)       { JaguarWriteByte(a##_addr+PIXEL_OFFSET_2(a), (JaguarReadByte(a##_addr+PIXEL_OFFSET_2(a), BLITTER)&(~(0x03 << PIXEL_SHIFT_2(a))))|(d<<PIXEL_SHIFT_2(a)), BLITTER); }

// 4 bpp pixel write
#define WRITE_PIXEL_4(a,d)       { JaguarWriteByte(a##_addr+PIXEL_OFFSET_4(a), (JaguarReadByte(a##_addr+PIXEL_OFFSET_4(a), BLITTER)&(~(0x0f << PIXEL_SHIFT_4(a))))|(d<<PIXEL_SHIFT_4(a)), BLITTER); }

// 8 bpp pixel write
#define WRITE_PIXEL_8(a,d)       { JaguarWriteByte(a##_addr+PIXEL_OFFSET_8(a), d, BLITTER); }

// 16 bpp pixel write
#define WRITE_PIXEL_16(a,d)     {  JaguarWriteWord(a##_addr+(PIXEL_OFFSET_16(a)<<1), d, BLITTER); }

// 32 bpp pixel write
#define WRITE_PIXEL_32(a,d)		{ JaguarWriteLong(a##_addr+(PIXEL_OFFSET_32(a)<<2), d, BLITTER); }

// pixel write
#define WRITE_PIXEL(a,f,d) {\
	switch ((f>>3)&0x07) { \
	case 0: WRITE_PIXEL_1(a,d);  break;  \
	case 1: WRITE_PIXEL_2(a,d);  break;  \
	case 2: WRITE_PIXEL_4(a,d);  break;  \
	case 3: WRITE_PIXEL_8(a,d);  break;  \
	case 4: WRITE_PIXEL_16(a,d); break;  \
	case 5: WRITE_PIXEL_32(a,d); break;  \
	}}

static uint8_t src;
static uint8_t dst;
static uint8_t misc;
static uint8_t a1ctl;
static uint8_t mode;
static uint8_t ity;
static uint8_t zop;
static uint8_t op;
static uint8_t ctrl;
static uint32_t a1_addr;
static uint32_t a2_addr;
static int32_t a1_zoffs;
static int32_t a2_zoffs;
static uint32_t xadd_a1_control;
static uint32_t xadd_a2_control;
static int32_t a1_pitch;
static int32_t a2_pitch;
static uint32_t n_pixels;
static uint32_t n_lines;
static int32_t a1_x;
static int32_t a1_y;
static int32_t a1_width;
static int32_t a2_x;
static int32_t a2_y;
static int32_t a2_width;
static int32_t a2_mask_x;
static int32_t a2_mask_y;
static int32_t a1_xadd;
static int32_t a1_yadd;
static int32_t a2_xadd;
static int32_t a2_yadd;
static uint8_t a1_phrase_mode;
static uint8_t a2_phrase_mode;
static int32_t a1_step_x = 0;
static int32_t a1_step_y = 0;
static int32_t a2_step_x = 0;
static int32_t a2_step_y = 0;
static uint32_t outer_loop;
static uint32_t inner_loop;
static uint32_t a2_psize;
static uint32_t a1_psize;
static uint32_t gouraud_add;
static int gd_i[4];
static int gd_c[4];
static int gd_ia, gd_ca;
static int colour_index = 0;
static int32_t zadd;
static uint32_t z_i[4];

static int32_t a1_clip_x, a1_clip_y;

// In the spirit of "get it right first, *then* optimize" I've taken the liberty
// of removing all the unnecessary code caching. If it turns out to be a good way
// to optimize the blitter, then we may revisit it in the future...

// Generic blit handler
void blitter_generic(uint32_t cmd)
{
   uint32_t srcdata, srczdata, dstdata, dstzdata, writedata, inhibit;
   uint32_t bppSrc = (DSTA2 ? 1 << ((REG(A1_FLAGS) >> 3) & 0x07) : 1 << ((REG(A2_FLAGS) >> 3) & 0x07));

   while (outer_loop--)
   {
      uint32_t a1_start = a1_x, a2_start = a2_x, bitPos = 0;

      //Kludge for Hover Strike...
      //I wonder if this kludge is in conjunction with the SRCENX down below...
      // This isn't so much a kludge but the way things work in BCOMPEN mode...!
      if (BCOMPEN && SRCENX)
      {
         if (n_pixels < bppSrc)
            bitPos = bppSrc - n_pixels;
      }

      inner_loop = n_pixels;
      while (inner_loop--)
      {
         srcdata = srczdata = dstdata = dstzdata = writedata = inhibit = 0;

         if (!DSTA2)							// Data movement: A1 <- A2
         {
            // load src data and Z
            //				if (SRCEN)
            if (SRCEN || SRCENX)	// Not sure if this is correct... (seems to be...!)
            {
               srcdata = READ_PIXEL(a2, REG(A2_FLAGS));

               if (SRCENZ)
                  srczdata = READ_ZDATA(a2, REG(A2_FLAGS));
               else if (cmd & 0x0001C020)	// PATDSEL | TOPBEN | TOPNEN | DSTWRZ
                  srczdata = READ_RDATA(SRCZINT, a2, REG(A2_FLAGS), a2_phrase_mode);
            }
            else	// Use SRCDATA register...
            {
               srcdata = READ_RDATA(SRCDATA, a2, REG(A2_FLAGS), a2_phrase_mode);

               if (cmd & 0x0001C020)		// PATDSEL | TOPBEN | TOPNEN | DSTWRZ
                  srczdata = READ_RDATA(SRCZINT, a2, REG(A2_FLAGS), a2_phrase_mode);
            }

            // load dst data and Z
            if (DSTEN)
            {
               dstdata = READ_PIXEL(a1, REG(A1_FLAGS));

               if (DSTENZ)
                  dstzdata = READ_ZDATA(a1, REG(A1_FLAGS));
               else
                  dstzdata = READ_RDATA(DSTZ, a1, REG(A1_FLAGS), a1_phrase_mode);
            }
            else
            {
               dstdata = READ_RDATA(DSTDATA, a1, REG(A1_FLAGS), a1_phrase_mode);

               if (DSTENZ)
                  dstzdata = READ_RDATA(DSTZ, a1, REG(A1_FLAGS), a1_phrase_mode);
            }

            if (GOURZ)
               srczdata = z_i[colour_index] >> 16;

            // apply z comparator
            if (Z_OP_INF && srczdata <  dstzdata)	inhibit = 1;
            if (Z_OP_EQU && srczdata == dstzdata)	inhibit = 1;
            if (Z_OP_SUP && srczdata >  dstzdata)	inhibit = 1;

            // apply data comparator
            // Note: DCOMPEN only works in 8/16 bpp modes! !!! FIX !!!
            // Does BCOMPEN only work in 1 bpp mode???
            //   No, but it always does a 1 bit expansion no matter what the BPP of the channel is set to. !!! FIX !!!
            //   This is bit tricky... We need to fix the XADD value so that it acts like a 1BPP value while inside
            //   an 8BPP space.
            if (DCOMPEN | BCOMPEN)
            {
               //Temp, for testing Hover Strike
               //Doesn't seem to do it... Why?
               //What needs to happen here is twofold. First, the address generator in the outer loop has
               //to honor the BPP when calculating the start address (which it kinda does already). Second,
               //it has to step bit by bit when using BCOMPEN. How to do this???
               if (BCOMPEN)
                  //small problem with this approach: it's not accurate... We need a proper address to begin with
                  //and *then* we can do the bit stepping from there the way it's *supposed* to be done... !!! FIX !!!
                  //[DONE]
               {
                  uint32_t pixShift = (~bitPos) & (bppSrc - 1);
                  srcdata = (srcdata >> pixShift) & 0x01;

                  bitPos++;
               }

               if (!CMPDST)
               {
                  if (srcdata == 0)
                     inhibit = 1;//*/
               }
               else
               {
                  // compare destination pixel with pattern pixel
                  if (dstdata == READ_RDATA(PATTERNDATA, a1, REG(A1_FLAGS), a1_phrase_mode))
                     //						if (dstdata != READ_RDATA(PATTERNDATA, a1, REG(A1_FLAGS), a1_phrase_mode))
                     inhibit = 1;
               }
            }

            if (CLIPA1)
            {
               inhibit |= (((a1_x >> 16) < a1_clip_x && (a1_x >> 16) >= 0
                        && (a1_y >> 16) < a1_clip_y && (a1_y >> 16) >= 0) ? 0 : 1);
            }

            // compute the write data and store
            if (!inhibit)
            {
               // Houston, we have a problem...
               // Look here, at PATDSEL and GOURD. If both are active (as they are on the BIOS intro), then there's
               // a conflict! E.g.:
               //Blit! (00100000 <- 000095D0) count: 3 x 1, A1/2_FLAGS: 00014220/00004020 [cmd: 00011008]
               // CMD -> src:  dst: DSTEN  misc:  a1ctl:  mode: GOURD  ity: PATDSEL z-op:  op: LFU_CLEAR ctrl:
               //  A1 -> pitch: 1 phrases, depth: 16bpp, z-off: 0, width: 320 (21), addctl: XADDPIX YADD0 XSIGNADD YSIGNADD
               //  A2 -> pitch: 1 phrases, depth: 16bpp, z-off: 0, width: 256 (20), addctl: XADDPHR YADD0 XSIGNADD YSIGNADD
               //        A1 x/y: 90/171, A2 x/y: 808/0 Pattern: 776D770077007700

               if (PATDSEL)
               {
                  // use pattern data for write data
                  writedata = READ_RDATA(PATTERNDATA, a1, REG(A1_FLAGS), a1_phrase_mode);
               }
               else if (ADDDSEL)
               {
                  writedata = (srcdata & 0xFF) + (dstdata & 0xFF);

                  if (!TOPBEN)
                  {
                     //This is correct now, but slow...
                     int16_t s = (srcdata & 0xFF) | ((srcdata & 0x80) ? 0xFF00 : 0x0000),
                             d = dstdata & 0xFF;
                     int16_t sum = s + d;

                     if (sum < 0)
                        writedata = 0x00;
                     else if (sum > 0xFF)
                        writedata = 0xFF;
                     else
                        writedata = (uint32_t)sum;
                  }

                  //This doesn't seem right... Looks like it would muck up the low byte... !!! FIX !!!
                  writedata |= (srcdata & 0xF00) + (dstdata & 0xF00);

                  if (!TOPNEN && writedata > 0xFFF)
                     writedata &= 0xFFF;

                  writedata |= (srcdata & 0xF000) + (dstdata & 0xF000);
               }
               else
               {
                  if (LFU_NAN) writedata |= ~srcdata & ~dstdata;
                  if (LFU_NA)  writedata |= ~srcdata & dstdata;
                  if (LFU_AN)  writedata |= srcdata  & ~dstdata;
                  if (LFU_A) 	 writedata |= srcdata  & dstdata;
               }

               //Although, this looks like it's OK... (even if it is shitty!)
               //According to JTRM, this is part of the four things the blitter does with the write data (the other
               //three being PATDSEL, ADDDSEL, and LFU (default). I'm not sure which gets precedence, this or PATDSEL
               //(see above blit example)...
               if (GOURD)
                  writedata = ((gd_c[colour_index]) << 8) | (gd_i[colour_index] >> 16);

               if (SRCSHADE)
               {
                  int intensity = srcdata & 0xFF;
                  int ia = gd_ia >> 16;
                  if (ia & 0x80)
                     ia = 0xFFFFFF00 | ia;
                  intensity += ia;
                  if (intensity < 0)
                     intensity = 0;
                  if (intensity > 0xFF)
                     intensity = 0xFF;
                  writedata = (srcdata & 0xFF00) | intensity;
               }
            }
            else
            {
               writedata = dstdata;
               srczdata = dstzdata;
            }

            //Tried 2nd below for Hover Strike: No dice.
            if (/*a1_phrase_mode || */BKGWREN || !inhibit)
               //				if (/*a1_phrase_mode || BKGWREN ||*/ !inhibit)
            {
               // write to the destination
               WRITE_PIXEL(a1, REG(A1_FLAGS), writedata);
               if (DSTWRZ)
                  WRITE_ZDATA(a1, REG(A1_FLAGS), srczdata);
            }
         }
         else	// if (DSTA2) 							// Data movement: A1 -> A2
         {
            // load src data and Z
            if (SRCEN)
            {
               srcdata = READ_PIXEL(a1, REG(A1_FLAGS));
               if (SRCENZ)
                  srczdata = READ_ZDATA(a1, REG(A1_FLAGS));
               else if (cmd & 0x0001C020)	// PATDSEL | TOPBEN | TOPNEN | DSTWRZ
                  srczdata = READ_RDATA(SRCZINT, a1, REG(A1_FLAGS), a1_phrase_mode);
            }
            else
            {
               srcdata = READ_RDATA(SRCDATA, a1, REG(A1_FLAGS), a1_phrase_mode);
               if (cmd & 0x001C020)	// PATDSEL | TOPBEN | TOPNEN | DSTWRZ
                  srczdata = READ_RDATA(SRCZINT, a1, REG(A1_FLAGS), a1_phrase_mode);
            }

            // load dst data and Z
            if (DSTEN)
            {
               dstdata = READ_PIXEL(a2, REG(A2_FLAGS));
               if (DSTENZ)
                  dstzdata = READ_ZDATA(a2, REG(A2_FLAGS));
               else
                  dstzdata = READ_RDATA(DSTZ, a2, REG(A2_FLAGS), a2_phrase_mode);
            }
            else
            {
               dstdata = READ_RDATA(DSTDATA, a2, REG(A2_FLAGS), a2_phrase_mode);
               if (DSTENZ)
                  dstzdata = READ_RDATA(DSTZ, a2, REG(A2_FLAGS), a2_phrase_mode);
            }

            if (GOURZ)
               srczdata = z_i[colour_index] >> 16;

            // apply z comparator
            if (Z_OP_INF && srczdata < dstzdata)	inhibit = 1;
            if (Z_OP_EQU && srczdata == dstzdata)	inhibit = 1;
            if (Z_OP_SUP && srczdata > dstzdata)	inhibit = 1;

            // apply data comparator
            //NOTE: The bit comparator (BCOMPEN) is NOT the same at the data comparator!
            if (DCOMPEN | BCOMPEN)
            {
               if (!CMPDST)
               {
                  if (srcdata == 0)
                     inhibit = 1;//*/
               }
               else
               {
                  // compare destination pixel with pattern pixel
                  if (dstdata == READ_RDATA(PATTERNDATA, a2, REG(A2_FLAGS), a2_phrase_mode))
                     //						if (dstdata != READ_RDATA(PATTERNDATA, a2, REG(A2_FLAGS), a2_phrase_mode))
                     inhibit = 1;
               }
            }

            if (CLIPA1)
            {
               inhibit |= (((a1_x >> 16) < a1_clip_x && (a1_x >> 16) >= 0
                        && (a1_y >> 16) < a1_clip_y && (a1_y >> 16) >= 0) ? 0 : 1);
            }

            // compute the write data and store
            if (!inhibit)
            {
               if (PATDSEL)
               {
                  // use pattern data for write data
                  writedata = READ_RDATA(PATTERNDATA, a2, REG(A2_FLAGS), a2_phrase_mode);
               }
               else if (ADDDSEL)
               {
                  // intensity addition
                  writedata = (srcdata & 0xFF) + (dstdata & 0xFF);
                  if (!(TOPBEN) && writedata > 0xFF)
                     writedata = 0xFF;
                  writedata |= (srcdata & 0xF00) + (dstdata & 0xF00);
                  if (!(TOPNEN) && writedata > 0xFFF)
                     writedata = 0xFFF;
                  writedata |= (srcdata & 0xF000) + (dstdata & 0xF000);
               }
               else
               {
                  if (LFU_NAN)
                     writedata |= ~srcdata & ~dstdata;
                  if (LFU_NA)
                     writedata |= ~srcdata & dstdata;
                  if (LFU_AN)
                     writedata |= srcdata & ~dstdata;
                  if (LFU_A)
                     writedata |= srcdata & dstdata;
               }

               if (GOURD)
                  writedata = ((gd_c[colour_index]) << 8) | (gd_i[colour_index] >> 16);

               if (SRCSHADE)
               {
                  int intensity = srcdata & 0xFF;
                  int ia = gd_ia >> 16;
                  if (ia & 0x80)
                     ia = 0xFFFFFF00 | ia;
                  intensity += ia;
                  if (intensity < 0)
                     intensity = 0;
                  if (intensity > 0xFF)
                     intensity = 0xFF;
                  writedata = (srcdata & 0xFF00) | intensity;
               }
            }
            else
            {
               writedata = dstdata;
               srczdata = dstzdata;
            }

            if (/*a2_phrase_mode || */BKGWREN || !inhibit)
            {
               // write to the destination
               WRITE_PIXEL(a2, REG(A2_FLAGS), writedata);

               if (DSTWRZ)
                  WRITE_ZDATA(a2, REG(A2_FLAGS), srczdata);
            }
         }

         // Update x and y (inner loop)
         //Now it does! But crappy, crappy, crappy! !!! FIX !!! [DONE]
         //This is less than ideal, but it works...
         if (!BCOMPEN)
         {//*/
            a1_x += a1_xadd, a1_y += a1_yadd;
            a2_x = (a2_x + a2_xadd) & a2_mask_x, a2_y = (a2_y + a2_yadd) & a2_mask_y;
         }
         else
         {
            a1_y += a1_yadd, a2_y = (a2_y + a2_yadd) & a2_mask_y;
            if (!DSTA2)
            {
               a1_x += a1_xadd;
               if (bitPos % bppSrc == 0)
                  a2_x = (a2_x + a2_xadd) & a2_mask_x;
            }
            else
            {
               a2_x = (a2_x + a2_xadd) & a2_mask_x;
               if (bitPos % bppSrc == 0)
                  a1_x += a1_xadd;
            }
         }//*/

         if (GOURZ)
            z_i[colour_index] += zadd;

         if (GOURD || SRCSHADE)
         {
            gd_i[colour_index] += gd_ia;
            //Hmm, this doesn't seem to do anything...
            //But it is correct according to the JTRM...!
            if ((int32_t)gd_i[colour_index] < 0)
               gd_i[colour_index] = 0;
            if (gd_i[colour_index] > 0x00FFFFFF)
               gd_i[colour_index] = 0x00FFFFFF;//*/

            gd_c[colour_index] += gd_ca;
            if ((int32_t)gd_c[colour_index] < 0)
               gd_c[colour_index] = 0;
            if (gd_c[colour_index] > 0x000000FF)
               gd_c[colour_index] = 0x000000FF;//*/
         }

         if (GOURD || SRCSHADE || GOURZ)
         {
            if (a1_phrase_mode)
               //This screws things up WORSE (for the BIOS opening screen)
               //				if (a1_phrase_mode || a2_phrase_mode)
               colour_index = (colour_index + 1) & 0x03;
         }
      }

      //NOTE: The way to fix the CD BIOS is to uncomment below and comment the stuff after
      //      the phrase mode mucking around. But it fucks up everything else...
      //#define SCREWY_CD_DEPENDENT
#ifdef SCREWY_CD_DEPENDENT
      a1_x += a1_step_x;
      a1_y += a1_step_y;
      a2_x += a2_step_x;
      a2_y += a2_step_y;//*/
#endif

      //New: Phrase mode taken into account! :-p
      if (a1_phrase_mode)			// v2
      {
         uint32_t pixelSize;
         // Bump the pointer to the next phrase boundary
         // Even though it works, this is crappy... Clean it up!
         uint32_t size = 64 / a1_psize;

         // Crappy kludge... ('aligning' source to destination)
         if (a2_phrase_mode && DSTA2)
         {
            uint32_t extra = (a2_start >> 16) % size;
            a1_x += extra << 16;
         }

         pixelSize = (size - 1) << 16;
         a1_x = (a1_x + pixelSize) & ~pixelSize;
      }

      if (a2_phrase_mode)			// v1
      {
         uint32_t pixelSize;
         // Bump the pointer to the next phrase boundary
         // Even though it works, this is crappy... Clean it up!
         uint32_t size = 64 / a2_psize;

         // Crappy kludge... ('aligning' source to destination)
         // Prolly should do this for A1 channel as well... [DONE]
         if (a1_phrase_mode && !DSTA2)
         {
            uint32_t extra = (a1_start >> 16) % size;
            a2_x += extra << 16;
         }

         pixelSize = (size - 1) << 16;
         a2_x = (a2_x + pixelSize) & ~pixelSize;
      }

      //Not entirely: This still mucks things up... !!! FIX !!!
      //Should this go before or after the phrase mode mucking around?
#ifndef SCREWY_CD_DEPENDENT
      a1_x += a1_step_x;
      a1_y += a1_step_y;
      a2_x += a2_step_x;
      a2_y += a2_step_y;//*/
#endif
   }

   // write values back to registers
   WREG(A1_PIXEL,  (a1_y & 0xFFFF0000) | ((a1_x >> 16) & 0xFFFF));
   WREG(A1_FPIXEL, (a1_y << 16) | (a1_x & 0xFFFF));
   WREG(A2_PIXEL,  (a2_y & 0xFFFF0000) | ((a2_x >> 16) & 0xFFFF));
}

void blitter_blit(uint32_t cmd)
{
   uint32_t m, e;
   uint32_t pitchValue[4] = { 0, 1, 3, 2 };
   colour_index = 0;
   src = cmd & 0x07;
   dst = (cmd >> 3) & 0x07;
   misc = (cmd >> 6) & 0x03;
   a1ctl = (cmd >> 8) & 0x7;
   mode = (cmd >> 11) & 0x07;
   ity = (cmd >> 14) & 0x0F;
   zop = (cmd >> 18) & 0x07;
   op = (cmd >> 21) & 0x0F;
   ctrl = (cmd >> 25) & 0x3F;

   // Addresses in A1/2_BASE are *phrase* aligned, i.e., bottom three bits are ignored!
   // NOTE: This fixes Rayman's bad collision detection AND keeps T2K working!
   a1_addr = REG(A1_BASE) & 0xFFFFFFF8;
   a2_addr = REG(A2_BASE) & 0xFFFFFFF8;

   a1_zoffs = (REG(A1_FLAGS) >> 6) & 7;
   a2_zoffs = (REG(A2_FLAGS) >> 6) & 7;

   xadd_a1_control = (REG(A1_FLAGS) >> 16) & 0x03;
   xadd_a2_control = (REG(A2_FLAGS) >> 16) & 0x03;

   a1_pitch = pitchValue[(REG(A1_FLAGS) & 0x03)];
   a2_pitch = pitchValue[(REG(A2_FLAGS) & 0x03)];

   n_pixels = REG(PIXLINECOUNTER) & 0xFFFF;
   n_lines = (REG(PIXLINECOUNTER) >> 16) & 0xFFFF;

   a1_x = (REG(A1_PIXEL) << 16) | (REG(A1_FPIXEL) & 0xFFFF);
   a1_y = (REG(A1_PIXEL) & 0xFFFF0000) | (REG(A1_FPIXEL) >> 16);

   // According to JTRM, this must give a *whole number* of phrases in the current
   // pixel size (this means the lookup above is WRONG)... !!! FIX !!!
   m = (REG(A1_FLAGS) >> 9) & 0x03, e = (REG(A1_FLAGS) >> 11) & 0x0F;
   a1_width = ((0x04 | m) << e) >> 2;//*/

   a2_x = (REG(A2_PIXEL) & 0x0000FFFF) << 16;
   a2_y = (REG(A2_PIXEL) & 0xFFFF0000);

   // According to JTRM, this must give a *whole number* of phrases in the current
   // pixel size (this means the lookup above is WRONG)... !!! FIX !!!
   m = (REG(A2_FLAGS) >> 9) & 0x03, e = (REG(A2_FLAGS) >> 11) & 0x0F;
   a2_width = ((0x04 | m) << e) >> 2;//*/
   a2_mask_x = ((REG(A2_MASK) & 0x0000FFFF) << 16) | 0xFFFF;
   a2_mask_y = (REG(A2_MASK) & 0xFFFF0000) | 0xFFFF;

   // Check for "use mask" flag
   if (!(REG(A2_FLAGS) & 0x8000))
   {
      a2_mask_x = 0xFFFFFFFF; // must be 16.16
      a2_mask_y = 0xFFFFFFFF; // must be 16.16
   }

   a1_phrase_mode = 0;

   // According to the official documentation, a hardware bug ties A2's yadd bit to A1's...
   a2_yadd = a1_yadd = (YADD1_A1 ? 1 << 16 : 0);

   if (YSIGNSUB_A1)
      a1_yadd = -a1_yadd;

   // determine a1_xadd
   switch (xadd_a1_control)
   {
      case XADDPHR:
         // This is a documented Jaguar bug relating to phrase mode and truncation... Look into it!
         // add phrase offset to X and truncate
         a1_xadd = 1 << 16;
         a1_phrase_mode = 1;
         break;
      case XADDPIX:
         // add pixelsize (1) to X
         a1_xadd = 1 << 16;
         break;
      case XADD0:
         // add zero (for those nice vertical lines)
         a1_xadd = 0;
         break;
      case XADDINC:
         // add the contents of the increment register
         a1_xadd = (REG(A1_INC) << 16)		 | (REG(A1_FINC) & 0x0000FFFF);
         a1_yadd = (REG(A1_INC) & 0xFFFF0000) | (REG(A1_FINC) >> 16);
         break;
   }

   if (XSIGNSUB_A1)
      a1_xadd = -a1_xadd;

   if (YSIGNSUB_A2)
      a2_yadd = -a2_yadd;

   a2_phrase_mode = 0;

   // determine a2_xadd
   switch (xadd_a2_control)
   {
      case XADDPHR:
         // add phrase offset to X and truncate
         a2_xadd = 1 << 16;
         a2_phrase_mode = 1;
         break;
      case XADDPIX:
         // add pixelsize (1) to X
         a2_xadd = 1 << 16;
         break;
      case XADD0:
         // add zero (for those nice vertical lines)
         a2_xadd = 0;
         break;
         //This really isn't a valid bit combo for A2... Shouldn't this cause the blitter to just say no?
      case XADDINC:
         // add the contents of the increment register
         // since there is no register for a2 we just add 1
         //Let's do nothing, since it's not listed as a valid bit combo...
         break;
   }

   if (XSIGNSUB_A2)
      a2_xadd = -a2_xadd;

   // Modify outer loop steps based on blitter command

   a1_step_x = 0;
   a1_step_y = 0;
   a2_step_x = 0;
   a2_step_y = 0;

   if (UPDA1F)
      a1_step_x = (REG(A1_FSTEP) & 0xFFFF),
                a1_step_y = (REG(A1_FSTEP) >> 16);

   if (UPDA1)
      a1_step_x |= ((REG(A1_STEP) & 0x0000FFFF) << 16),
                a1_step_y |= ((REG(A1_STEP) & 0xFFFF0000));

   if (UPDA2)
      a2_step_x = (REG(A2_STEP) & 0x0000FFFF) << 16,
                a2_step_y = (REG(A2_STEP) & 0xFFFF0000);

   outer_loop = n_lines;

   // Clipping...

   if (CLIPA1)
      a1_clip_x = REG(A1_CLIP) & 0x7FFF,
                a1_clip_y = (REG(A1_CLIP) >> 16) & 0x7FFF;

   // This phrase sizing is incorrect as well... !!! FIX !!! [NOTHING TO FIX]
   // Err, this is pixel size... (and it's OK)
   a2_psize = 1 << ((REG(A2_FLAGS) >> 3) & 0x07);
   a1_psize = 1 << ((REG(A1_FLAGS) >> 3) & 0x07);

   // Z-buffering
   if (GOURZ)
   {
      unsigned v;
      zadd = REG(ZINC);

      for(v = 0; v < 4; v++)
         z_i[v] = REG(PHRASEZ0 + v*4);
   }

   // Gouraud shading
   if (GOURD || GOURZ || SRCSHADE)
   {
      gd_c[0] = blitter_ram[PATTERNDATA + 6];
      gd_i[0]	= ((uint32_t)blitter_ram[PATTERNDATA + 7] << 16)
         | ((uint32_t)blitter_ram[SRCDATA + 6] << 8) | blitter_ram[SRCDATA + 7];

      gd_c[1] = blitter_ram[PATTERNDATA + 4];
      gd_i[1]	= ((uint32_t)blitter_ram[PATTERNDATA + 5] << 16)
         | ((uint32_t)blitter_ram[SRCDATA + 4] << 8) | blitter_ram[SRCDATA + 5];

      gd_c[2] = blitter_ram[PATTERNDATA + 2];
      gd_i[2]	= ((uint32_t)blitter_ram[PATTERNDATA + 3] << 16)
         | ((uint32_t)blitter_ram[SRCDATA + 2] << 8) | blitter_ram[SRCDATA + 3];

      gd_c[3] = blitter_ram[PATTERNDATA + 0];
      gd_i[3]	= ((uint32_t)blitter_ram[PATTERNDATA + 1] << 16)
         | ((uint32_t)blitter_ram[SRCDATA + 0] << 8) | blitter_ram[SRCDATA + 1];

      gouraud_add = REG(INTENSITYINC);

      gd_ia = gouraud_add & 0x00FFFFFF;
      if (gd_ia & 0x00800000)
         gd_ia = 0xFF000000 | gd_ia;

      gd_ca = (gouraud_add >> 24) & 0xFF;
      if (gd_ca & 0x00000080)
         gd_ca = 0xFFFFFF00 | gd_ca;
   }

   blitter_generic(cmd);
}
#endif
/*******************************************************************************
********************** STUFF CUT ABOVE THIS LINE! ******************************
*******************************************************************************/


void BlitterInit(void)
{
	BlitterReset();
}


void BlitterReset(void)
{
	memset(blitter_ram, 0x00, 0xA0);
}


void BlitterDone(void)
{
}


uint8_t BlitterReadByte(uint32_t offset, uint32_t who/*=UNKNOWN*/)
{
	offset &= 0xFF;

	// status register
//This isn't cycle accurate--how to fix? !!! FIX !!!
//Probably have to do some multi-threaded implementation or at least a reentrant safe implementation...
//Real hardware returns $00000805, just like the JTRM says.
	if (offset == (0x38 + 0))
		return 0x00;
	if (offset == (0x38 + 1))
		return 0x00;
	if (offset == (0x38 + 2))
		return 0x08;
	if (offset == (0x38 + 3))
		return 0x05;	// always idle/never stopped (collision detection ignored!)

// CHECK HERE ONCE THIS FIX HAS BEEN TESTED: [X]
//Fix for AvP:
	if (offset >= 0x04 && offset <= 0x07)
//This is it. I wonder if it just ignores the lower three bits?
//No, this is a documented Jaguar I bug. It also bites the read at $F02230 as well...
		return blitter_ram[offset + 0x08];		// A1_PIXEL ($F0220C) read at $F02204

	if (offset >= 0x2C && offset <= 0x2F)
		return blitter_ram[offset + 0x04];		// A2_PIXEL ($F02230) read at $F0222C

	return blitter_ram[offset];
}


//Crappy!
uint16_t BlitterReadWord(uint32_t offset, uint32_t who/*=UNKNOWN*/)
{
	return ((uint16_t)BlitterReadByte(offset, who) << 8) | (uint16_t)BlitterReadByte(offset+1, who);
}


//Crappy!
uint32_t BlitterReadLong(uint32_t offset, uint32_t who/*=UNKNOWN*/)
{
	return (BlitterReadWord(offset, who) << 16) | BlitterReadWord(offset+2, who);
}


void BlitterWriteByte(uint32_t offset, uint8_t data, uint32_t who/*=UNKNOWN*/)
{
	offset &= 0xFF;

	// This handles writes to INTENSITY0-3 by also writing them to their proper places in
	// PATTERNDATA & SOURCEDATA (should do the same for the Z registers! !!! FIX !!! [DONE])
	if ((offset >= 0x7C) && (offset <= 0x9B))
	{
		switch (offset)
		{
		// INTENSITY registers 0-3
		case 0x7C: break;
		case 0x7D: blitter_ram[PATTERNDATA + 7] = data; break;
		case 0x7E: blitter_ram[SRCDATA + 6] = data; break;
		case 0x7F: blitter_ram[SRCDATA + 7] = data; break;

		case 0x80: break;
		case 0x81: blitter_ram[PATTERNDATA + 5] = data; break;
		case 0x82: blitter_ram[SRCDATA + 4] = data; break;
		case 0x83: blitter_ram[SRCDATA + 5] = data; break;

		case 0x84: break;
		case 0x85: blitter_ram[PATTERNDATA + 3] = data; break;
		case 0x86: blitter_ram[SRCDATA + 2] = data; break;
		case 0x87: blitter_ram[SRCDATA + 3] = data; break;

		case 0x88: break;
		case 0x89: blitter_ram[PATTERNDATA + 1] = data; break;
		case 0x8A: blitter_ram[SRCDATA + 0] = data; break;
		case 0x8B: blitter_ram[SRCDATA + 1] = data; break;


		// Z registers 0-3
		case 0x8C: blitter_ram[SRCZINT + 6] = data; break;
		case 0x8D: blitter_ram[SRCZINT + 7] = data; break;
		case 0x8E: blitter_ram[SRCZFRAC + 6] = data; break;
		case 0x8F: blitter_ram[SRCZFRAC + 7] = data; break;

		case 0x90: blitter_ram[SRCZINT + 4] = data; break;
		case 0x91: blitter_ram[SRCZINT + 5] = data; break;
		case 0x92: blitter_ram[SRCZFRAC + 4] = data; break;
		case 0x93: blitter_ram[SRCZFRAC + 5] = data; break;

		case 0x94: blitter_ram[SRCZINT + 2] = data; break;
		case 0x95: blitter_ram[SRCZINT + 3] = data; break;
		case 0x96: blitter_ram[SRCZFRAC + 2] = data; break;
		case 0x97: blitter_ram[SRCZFRAC + 3] = data; break;

		case 0x98: blitter_ram[SRCZINT + 0] = data; break;
		case 0x99: blitter_ram[SRCZINT + 1] = data; break;
		case 0x9A: blitter_ram[SRCZFRAC + 0] = data; break;
		case 0x9B: blitter_ram[SRCZFRAC + 1] = data; break;
		}
	}

	// It looks weird, but this is how the 64 bit registers are actually handled...!

	else if (((offset >= SRCDATA + 0) && (offset <= SRCDATA + 3))
		|| ((offset >= DSTDATA + 0) && (offset <= DSTDATA + 3))
		|| ((offset >= DSTZ + 0) && (offset <= DSTZ + 3))
		|| ((offset >= SRCZINT + 0) && (offset <= SRCZINT + 3))
		|| ((offset >= SRCZFRAC + 0) && (offset <= SRCZFRAC + 3))
		|| ((offset >= PATTERNDATA + 0) && (offset <= PATTERNDATA + 3))
      )
	{
		blitter_ram[offset + 4] = data;
	}
	else if (((offset >= SRCDATA + 4) && (offset <= SRCDATA + 7))
		|| ((offset >= DSTDATA + 4) && (offset <= DSTDATA + 7))
		|| ((offset >= DSTZ + 4) && (offset <= DSTZ + 7))
		|| ((offset >= SRCZINT + 4) && (offset <= SRCZINT + 7))
		|| ((offset >= SRCZFRAC + 4) && (offset <= SRCZFRAC + 7))
		|| ((offset >= PATTERNDATA + 4) && (offset <= PATTERNDATA + 7))
      )
	{
		blitter_ram[offset - 4] = data;
	}
	else
		blitter_ram[offset] = data;
}


void BlitterWriteWord(uint32_t offset, uint16_t data, uint32_t who/*=UNKNOWN*/)
{
	BlitterWriteByte(offset + 0, data >> 8, who);
	BlitterWriteByte(offset + 1, data & 0xFF, who);

	if ((offset & 0xFF) == 0x3A)
	// I.e., the second write of 32-bit value--not convinced this is the best way to do this!
	// But then again, according to the Jaguar docs, this is correct...!
	{
		if (vjs.useFastBlitter)
			blitter_blit(GET32(blitter_ram, 0x38));
		else
			BlitterMidsummer2();
	}
}
//F02278,9,A,B


void BlitterWriteLong(uint32_t offset, uint32_t data, uint32_t who)
{
	BlitterWriteWord(offset + 0, data >> 16, who);
	BlitterWriteWord(offset + 2, data & 0xFFFF, who);
}

// Here's attempt #2--taken from the Oberon chip specs!

#ifdef USE_MIDSUMMER_BLITTER_MKII

void ADDRGEN(uint32_t *, uint32_t *, bool, bool,
	uint16_t, uint16_t, uint32_t, uint8_t, uint8_t, uint8_t, uint8_t,
	uint16_t, uint16_t, uint32_t, uint8_t, uint8_t, uint8_t, uint8_t);
void ADDARRAY(uint16_t * addq, uint8_t daddasel, uint8_t daddbsel, uint8_t daddmode,
	uint64_t dstd, uint32_t iinc, uint8_t initcin[], uint64_t initinc, uint16_t initpix,
	uint32_t istep, uint64_t patd, uint64_t srcd, uint64_t srcz1, uint64_t srcz2,
	uint32_t zinc, uint32_t zstep);
void ADD16SAT(uint16_t *r, uint8_t *co, uint16_t a, uint16_t b, uint8_t cin, bool sat, bool eightbit, bool hicinh);
void ADDAMUX(int16_t *adda_x, int16_t *adda_y, uint8_t addasel, int16_t a1_step_x, int16_t a1_step_y,
	int16_t a1_stepf_x, int16_t a1_stepf_y, int16_t a2_step_x, int16_t a2_step_y,
	int16_t a1_inc_x, int16_t a1_inc_y, int16_t a1_incf_x, int16_t a1_incf_y, uint8_t adda_xconst,
	bool adda_yconst, bool addareg, bool suba_x, bool suba_y);
void ADDBMUX(int16_t *addb_x, int16_t *addb_y, uint8_t addbsel, int16_t a1_x, int16_t a1_y,
	int16_t a2_x, int16_t a2_y, int16_t a1_frac_x, int16_t a1_frac_y);
void DATAMUX(int16_t *data_x, int16_t *data_y, uint32_t gpu_din, int16_t addq_x, int16_t addq_y, bool addqsel);
void ADDRADD(int16_t *addq_x, int16_t *addq_y, bool a1fracldi,
	uint16_t adda_x, uint16_t adda_y, uint16_t addb_x, uint16_t addb_y, uint8_t modx, bool suba_x, bool suba_y);
void DATA(uint64_t *wdata, uint8_t *dcomp, uint8_t *zcomp, bool *nowrite,
	bool big_pix, bool cmpdst, uint8_t daddasel, uint8_t daddbsel, uint8_t daddmode, bool daddq_sel, uint8_t data_sel,
	uint8_t dbinh, uint8_t dend, uint8_t dstart, uint64_t dstd, uint32_t iinc, uint8_t lfu_func, uint64_t *patd, bool patdadd,
	bool phrase_mode, uint64_t srcd, bool srcdread, bool srczread, bool srcz2add, uint8_t zmode,
	bool bcompen, bool bkgwren, bool dcompen, uint8_t icount, uint8_t pixsize,
	uint64_t *srcz, uint64_t dstz, uint32_t zinc);
void COMP_CTRL(uint8_t *dbinh, bool *nowrite,
	bool bcompen, bool big_pix, bool bkgwren, uint8_t dcomp, bool dcompen, uint8_t icount,
	uint8_t pixsize, bool phrase_mode, uint8_t srcd, uint8_t zcomp);

void BlitterMidsummer2(void)
{
   // Here's what the specs say the state machine does. Note that this can probably be
   // greatly simplified (also, it's different from what John has in his Oberon docs):
   //Will remove stuff that isn't in Jaguar I once fully described (stuff like texture won't
   //be described here at all)...

   uint32_t cmd = GET32(blitter_ram, COMMAND);

   // Line states passed in via the command register

   bool srcen = (SRCEN), srcenx = (SRCENX), srcenz = (SRCENZ),
        dsten = (DSTEN), dstenz = (DSTENZ), dstwrz = (DSTWRZ), clip_a1 = (CLIPA1),
        upda1 = (UPDA1), upda1f = (UPDA1F), upda2 = (UPDA2), dsta2 = (DSTA2),
        gourd = (GOURD), gourz = (GOURZ), topben = (TOPBEN), topnen = (TOPNEN),
        patdsel = (PATDSEL), adddsel = (ADDDSEL), cmpdst = (CMPDST), bcompen = (BCOMPEN),
        dcompen = (DCOMPEN), bkgwren = (BKGWREN), srcshade = (SRCSHADE);

   uint8_t zmode = (cmd & 0x01C0000) >> 18, lfufunc = (cmd & 0x1E00000) >> 21;
   //Missing: BUSHI
   //Where to find various lines:
   // clip_a1  -> inner
   // gourd    -> dcontrol, inner, outer, state
   // gourz    -> dcontrol, inner, outer, state
   // cmpdst   -> blit, data, datacomp, state
   // bcompen  -> acontrol, inner, mcontrol, state
   // dcompen  -> inner, state
   // bkgwren  -> inner, state
   // srcshade -> dcontrol, inner, state
   // adddsel  -> dcontrol
   //NOTE: ADDDSEL takes precedence over PATDSEL, PATDSEL over LFU_FUNC

   // Lines that don't exist in Jaguar I (and will never be asserted)

   bool polygon = false, datinit = false, a1_stepld = false, a2_stepld = false, ext_int = false;
   bool istepadd = false, istepfadd = false;
   bool zstepfadd = false, zstepadd = false;

   // Various state lines (initial state--basically the reset state of the FDSYNCs)

   bool go = true, idle = true, inner = false, a1fupdate = false, a1update = false,
        zfupdate = false, zupdate = false, a2update = false, init_if = false, init_ii = false,
        init_zf = false, init_zi = false;

   bool outer0 = false, indone = false;

   bool idlei, inneri, a1fupdatei, a1updatei, zfupdatei, zupdatei, a2updatei, init_ifi, init_iii,
        init_zfi, init_zii;

   bool notgzandp = !(gourz && polygon);

   // Various registers set up by user

   uint16_t ocount = GET16(blitter_ram, PIXLINECOUNTER);
   uint8_t a1_pitch = blitter_ram[A1_FLAGS + 3] & 0x03;
   uint8_t a2_pitch = blitter_ram[A2_FLAGS + 3] & 0x03;
   uint8_t a1_pixsize = (blitter_ram[A1_FLAGS + 3] & 0x38) >> 3;
   uint8_t a2_pixsize = (blitter_ram[A2_FLAGS + 3] & 0x38) >> 3;
   uint8_t a1_zoffset = (GET16(blitter_ram, A1_FLAGS + 2) >> 6) & 0x07;
   uint8_t a2_zoffset = (GET16(blitter_ram, A2_FLAGS + 2) >> 6) & 0x07;
   uint8_t a1_width = (blitter_ram[A1_FLAGS + 2] >> 1) & 0x3F;
   uint8_t a2_width = (blitter_ram[A2_FLAGS + 2] >> 1) & 0x3F;
   uint8_t a1addx = blitter_ram[A1_FLAGS + 1] & 0x03, a2addx = blitter_ram[A2_FLAGS + 1] & 0x03;
   bool a1addy = blitter_ram[A1_FLAGS + 1] & 0x04, a2addy = blitter_ram[A2_FLAGS + 1] & 0x04;
   bool a1xsign = blitter_ram[A1_FLAGS + 1] & 0x08, a2xsign = blitter_ram[A2_FLAGS + 1] & 0x08;
   bool a1ysign = blitter_ram[A1_FLAGS + 1] & 0x10, a2ysign = blitter_ram[A2_FLAGS + 1] & 0x10;
   uint32_t a1_base = GET32(blitter_ram, A1_BASE) & 0xFFFFFFF8;	// Phrase aligned by ignoring bottom 3 bits
   uint32_t a2_base = GET32(blitter_ram, A2_BASE) & 0xFFFFFFF8;

   uint16_t a1_win_x = GET16(blitter_ram, A1_CLIP + 2) & 0x7FFF;
   uint16_t a1_win_y = GET16(blitter_ram, A1_CLIP + 0) & 0x7FFF;
   int16_t a1_x = (int16_t)GET16(blitter_ram, A1_PIXEL + 2);
   int16_t a1_y = (int16_t)GET16(blitter_ram, A1_PIXEL + 0);
   int16_t a1_step_x = (int16_t)GET16(blitter_ram, A1_STEP + 2);
   int16_t a1_step_y = (int16_t)GET16(blitter_ram, A1_STEP + 0);
   uint16_t a1_stepf_x = GET16(blitter_ram, A1_FSTEP + 2);
   uint16_t a1_stepf_y = GET16(blitter_ram, A1_FSTEP + 0);
   uint16_t a1_frac_x = GET16(blitter_ram, A1_FPIXEL + 2);
   uint16_t a1_frac_y = GET16(blitter_ram, A1_FPIXEL + 0);
   int16_t a1_inc_x = (int16_t)GET16(blitter_ram, A1_INC + 2);
   int16_t a1_inc_y = (int16_t)GET16(blitter_ram, A1_INC + 0);
   uint16_t a1_incf_x = GET16(blitter_ram, A1_FINC + 2);
   uint16_t a1_incf_y = GET16(blitter_ram, A1_FINC + 0);

   int16_t a2_x = (int16_t)GET16(blitter_ram, A2_PIXEL + 2);
   int16_t a2_y = (int16_t)GET16(blitter_ram, A2_PIXEL + 0);
#if 0
   bool a2_mask = blitter_ram[A2_FLAGS + 2] & 0x80;
   uint16_t a2_mask_x = GET16(blitter_ram, A2_MASK + 2);
   uint16_t a2_mask_y = GET16(blitter_ram, A2_MASK + 0);
   uint32_t collision = GET32(blitter_ram, COLLISIONCTRL);// 0=RESUME, 1=ABORT, 2=STOPEN
#endif
   int16_t a2_step_x = (int16_t)GET16(blitter_ram, A2_STEP + 2);
   int16_t a2_step_y = (int16_t)GET16(blitter_ram, A2_STEP + 0);

   uint64_t srcd1 = GET64(blitter_ram, SRCDATA);
   uint64_t srcd2 = 0;
   uint64_t dstd = GET64(blitter_ram, DSTDATA);
   uint64_t patd = GET64(blitter_ram, PATTERNDATA);
   uint32_t iinc = GET32(blitter_ram, INTENSITYINC);
   uint64_t srcz1 = GET64(blitter_ram, SRCZINT);
   uint64_t srcz2 = GET64(blitter_ram, SRCZFRAC);
   uint64_t dstz = GET64(blitter_ram, DSTZ);
   uint32_t zinc = GET32(blitter_ram, ZINC);

   uint8_t pixsize = (dsta2 ? a2_pixsize : a1_pixsize);	// From ACONTROL

   bool phrase_mode;
   uint16_t a1FracCInX = 0, a1FracCInY = 0;

   // Bugs in Jaguar I

   a2addy = a1addy;							// A2 channel Y add bit is tied to A1's

   // Various state lines set up by user

   phrase_mode = ((!dsta2 && a1addx == 0) || (dsta2 && a2addx == 0) ? true : false);	// From ACONTROL

   // Stopgap vars to simulate various lines


   while (true)
   {
      // IDLE

      if ((idle && !go) || (inner && outer0 && indone))
      {
         idlei = true;

         //Instead of a return, let's try breaking out of the loop...
         break;
      }
      else
         idlei = false;

      // INNER LOOP ACTIVE

      if ((idle && go && !datinit)
            || (inner && !indone)
            || (inner && indone && !outer0 && !upda1f && !upda1 && notgzandp && !upda2 && !datinit)
            || (a1update && !upda2 && notgzandp && !datinit)
            || (zupdate && !upda2 && !datinit)
            || (a2update && !datinit)
            || (init_ii && !gourz)
            || (init_zi))
         inneri = true;
      else
         inneri = false;

      // A1 FRACTION UPDATE

      if (inner && indone && !outer0 && upda1f)
         a1fupdatei = true;
      else
         a1fupdatei = false;

      // A1 POINTER UPDATE

      if ((a1fupdate)
            || (inner && indone && !outer0 && !upda1f && upda1))
         a1updatei = true;
      else
         a1updatei = false;

      // Z FRACTION UPDATE

      if ((a1update && gourz && polygon)
            || (inner && indone && !outer0 && !upda1f && !upda1 && gourz && polygon))
         zfupdatei = true;
      else
         zfupdatei = false;

      // Z INTEGER UPDATE

      if (zfupdate)
         zupdatei = true;
      else
         zupdatei = false;

      // A2 POINTER UPDATE

      if ((a1update && upda2 && notgzandp)
            || (zupdate && upda2)
            || (inner && indone && !outer0 && !upda1f && notgzandp && !upda1 && upda2))
         a2updatei = true;
      else
         a2updatei = false;

      // INITIALIZE INTENSITY FRACTION

      if ((zupdate && !upda2 && datinit)
            || (a1update && !upda2 && datinit && notgzandp)
            || (inner && indone && !outer0 && !upda1f && !upda1 && notgzandp && !upda2 && datinit)
            || (a2update && datinit)
            || (idle && go && datinit))
         init_ifi = true;
      else
         init_ifi = false;

      // INITIALIZE INTENSITY INTEGER

      if (init_if)
         init_iii = true;
      else
         init_iii = false;

      // INITIALIZE Z FRACTION

      if (init_ii && gourz)
         init_zfi = true;
      else
         init_zfi = false;

      // INITIALIZE Z INTEGER

      if (init_zf)
         init_zii = true;
      else
         init_zii = false;

      // Here we move the fooi into their foo counterparts in order to simulate the moving
      // of data into the various FDSYNCs... Each time we loop we simulate one clock cycle...

      idle = idlei;
      inner = inneri;
      a1fupdate = a1fupdatei;
      a1update = a1updatei;
      zfupdate = zfupdatei;		// *
      zupdate = zupdatei;			// *
      a2update = a2updatei;
      init_if = init_ifi;			// *
      init_ii = init_iii;			// *
      init_zf = init_zfi;			// *
      init_zi = init_zii;			// *
      // * denotes states that will never assert for Jaguar I

      // Now, depending on how we want to handle things, we could either put the implementation
      // of the various pieces up above, or handle them down below here.

      // Let's try postprocessing for now...

      if (inner)
      {
         bool idle_inner = true, step = true, sreadx = false, szreadx = false, sread = false,
              szread = false, dread = false, dzread = false, dwrite = false, dzwrite = false;
         bool inner0 = false;
         bool idle_inneri, sreadxi, szreadxi, sreadi, szreadi, dreadi, dzreadi, dwritei, dzwritei;
         // State lines that will never assert in Jaguar I
         bool textext = false, txtread = false;
         //other stuff
         uint8_t srcshift = 0;
         uint16_t icount = GET16(blitter_ram, PIXLINECOUNTER + 2);
         bool srca_addi, dsta_addi, gensrc, gendst, gena2i, zaddr, fontread, justify, a1_add, a2_add;
         bool adda_yconst, addareg, suba_x, suba_y, a1fracldi, srcdreadd, shadeadd;
         uint8_t addasel, a1_xconst, a2_xconst, adda_xconst, addbsel, maska1, maska2, modx, daddasel;
         uint8_t daddbsel, daddmode;
         bool patfadd, patdadd, srcz1add, srcz2add, srcshadd, daddq_sel;
         uint8_t data_sel;
         uint32_t address, pixAddr;
         uint8_t dstxp, srcxp, shftv, pobb;
         bool pobbsel;
         uint8_t loshd, shfti;
         uint64_t srcz;
         bool winhibit;

         indone = false;

         //			while (!idle_inner)
         while (true)
         {
            bool sshftld; // D flipflop (D -> Q): instart -> sshftld
            uint16_t dstxwr, pseq;
            bool penden;
            uint8_t window_mask;
            uint8_t inner_mask = 0;
            uint8_t emask, pma, dend;
            uint64_t srcd;
            uint8_t zSrcShift;
            uint64_t wdata;
            uint8_t dcomp, zcomp;

            //NOTE: sshftld probably is only asserted at the beginning of the inner loop. !!! FIX !!!
            // IDLE

            if ((idle_inner && !step)
                  || (dzwrite && step && inner0)
                  || (dwrite && step && !dstwrz && inner0))
            {
               idle_inneri = true;
               break;
            }
            else
               idle_inneri = false;

            // EXTRA SOURCE DATA READ

            if ((idle_inner && step && srcenx)
                  || (sreadx && !step))
               sreadxi = true;
            else
               sreadxi = false;

            // EXTRA SOURCE ZED READ

            if ((sreadx && step && srcenz)
                  || (szreadx && !step))
               szreadxi = true;
            else
               szreadxi = false;

            // TEXTURE DATA READ (not implemented because not in Jaguar I)

            // SOURCE DATA READ

            if ((szreadx && step && !textext)
                  || (sreadx && step && !srcenz && srcen)
                  || (idle_inner && step && !srcenx && !textext && srcen)
                  || (dzwrite && step && !inner0 && !textext && srcen)
                  || (dwrite && step && !dstwrz && !inner0 && !textext && srcen)
                  || (txtread && step && srcen)
                  || (sread && !step))
               sreadi = true;
            else
               sreadi = false;

            // SOURCE ZED READ

            if ((sread && step && srcenz)
                  || (szread && !step))
               szreadi = true;
            else
               szreadi = false;

            // DESTINATION DATA READ

            if ((szread && step && dsten)
                  || (sread && step && !srcenz && dsten)
                  || (sreadx && step && !srcenz && !textext && !srcen && dsten)
                  || (idle_inner && step && !srcenx && !textext && !srcen && dsten)
                  || (dzwrite && step && !inner0 && !textext && !srcen && dsten)
                  || (dwrite && step && !dstwrz && !inner0 && !textext && !srcen && dsten)
                  || (txtread && step && !srcen && dsten)
                  || (dread && !step))
               dreadi = true;
            else
               dreadi = false;

            // DESTINATION ZED READ

            if ((dread && step && dstenz)
                  || (szread && step && !dsten && dstenz)
                  || (sread && step && !srcenz && !dsten && dstenz)
                  || (sreadx && step && !srcenz && !textext && !srcen && !dsten && dstenz)
                  || (idle_inner && step && !srcenx && !textext && !srcen && !dsten && dstenz)
                  || (dzwrite && step && !inner0 && !textext && !srcen && !dsten && dstenz)
                  || (dwrite && step && !dstwrz && !inner0 && !textext && !srcen && !dsten && dstenz)
                  || (txtread && step && !srcen && !dsten && dstenz)
                  || (dzread && !step))
               dzreadi = true;
            else
               dzreadi = false;

            // DESTINATION DATA WRITE

            if ((dzread && step)
                  || (dread && step && !dstenz)
                  || (szread && step && !dsten && !dstenz)
                  || (sread && step && !srcenz && !dsten && !dstenz)
                  || (txtread && step && !srcen && !dsten && !dstenz)
                  || (sreadx && step && !srcenz && !textext && !srcen && !dsten && !dstenz)
                  || (idle_inner && step && !srcenx && !textext && !srcen && !dsten && !dstenz)
                  || (dzwrite && step && !inner0 && !textext && !srcen && !dsten && !dstenz)
                  || (dwrite && step && !dstwrz && !inner0 && !textext && !srcen && !dsten && !dstenz)
                  || (dwrite && !step))
               dwritei = true;
            else
               dwritei = false;

            // DESTINATION ZED WRITE

            if ((dzwrite && !step)
                  || (dwrite && step && dstwrz))
               dzwritei = true;
            else
               dzwritei = false;

            //Kludge: A QnD way to make sure that sshftld is asserted only for the first
            //        cycle of the inner loop...
            sshftld = idle_inner;

            // Here we move the fooi into their foo counterparts in order to simulate the moving
            // of data into the various FDSYNCs... Each time we loop we simulate one clock cycle...

            idle_inner = idle_inneri;
            sreadx = sreadxi;
            szreadx = szreadxi;
            sread = sreadi;
            szread = szreadi;
            dread = dreadi;
            dzread = dzreadi;
            dwrite = dwritei;
            dzwrite = dzwritei;

            // Here's a few more decodes--not sure if they're supposed to go here or not...


            srca_addi = (sreadxi && !srcenz) || (sreadi && !srcenz) || szreadxi || szreadi;

            dsta_addi = (dwritei && !dstwrz) || dzwritei;

            gensrc = sreadxi || szreadxi || sreadi || szreadi;
            gendst = dreadi || dzreadi || dwritei || dzwritei;
            gena2i = (gensrc && !dsta2) || (gendst && dsta2);

            zaddr = szreadx || szread || dzread || dzwrite;

            // Some stuff from MCONTROL.NET--not sure if this is the correct use of this decode or not...
            /*Fontread\	:= OND1 (fontread\, sread[1], sreadx[1], bcompen);
Fontread	:= INV1 (fontread, fontread\);
Justt		:= NAN3 (justt, fontread\, phrase_mode, tactive\);
Justify		:= TS (justify, justt, busen);*/
            fontread = (sread || sreadx) && bcompen;
            justify = !(!fontread && phrase_mode /*&& tactive*/);

            /* Generate inner loop update enables */
            /*
A1_addi		:= MX2 (a1_addi, dsta_addi, srca_addi, dsta2);
A2_addi		:= MX2 (a2_addi, srca_addi, dsta_addi, dsta2);
A1_add		:= FD1 (a1_add, a1_add\, a1_addi, clk);
A2_add		:= FD1 (a2_add, a2_add\, a2_addi, clk);
A2_addb		:= BUF1 (a2_addb, a2_add);
*/
            a1_add = (dsta2 ? srca_addi : dsta_addi);
            a2_add = (dsta2 ? dsta_addi : srca_addi);

            /* Address adder input A register selection
               000	A1 step integer part
               001	A1 step fraction part
               010	A1 increment integer part
               011	A1 increment fraction part
               100	A2 step

               bit 2 = a2update
               bit 1 = /a2update . (a1_add . a1addx[0..1])
               bit 0 = /a2update . ( a1fupdate
               + a1_add . atick[0] . a1addx[0..1])
               The /a2update term on bits 0 and 1 is redundant.
               Now look-ahead based
               */

            addasel = (a1fupdate || (a1_add && a1addx == 3) ? 0x01 : 0x00);
            addasel |= (a1_add && a1addx == 3 ? 0x02 : 0x00);
            addasel |= (a2update ? 0x04 : 0x00);
            /* Address adder input A X constant selection
               adda_xconst[0..2] generate a power of 2 in the range 1-64 or all
               zeroes when they are all 1
               Remember - these are pixels, so to add one phrase the pixel size
               has to be taken into account to get the appropriate value.
               for A1
               if a1addx[0..1] are 00 set 6 - pixel size
               if a1addx[0..1] are 01 set the value 000
               if a1addx[0..1] are 10 set the value 111
               similarly for A2
JLH: Also, 11 will likewise set the value to 111
*/
            a1_xconst = 6 - a1_pixsize;
            a2_xconst = 6 - a2_pixsize;

            if (a1addx == 1)
               a1_xconst = 0;
            else if (a1addx & 0x02)
               a1_xconst = 7;

            if (a2addx == 1)
               a2_xconst = 0;
            else if (a2addx & 0x02)
               a2_xconst = 7;

            adda_xconst = (a2_add ? a2_xconst : a1_xconst);
            /* Address adder input A Y constant selection
               22 June 94 - This was erroneous, because only the a1addy bit was reflected here.
               Therefore, the selection has to be controlled by a bug fix bit.
JLH: Bug fix bit in Jaguar II--not in Jaguar I!
*/
            adda_yconst = a1addy;
            /* Address adder input A register versus constant selection
               given by	  a1_add . a1addx[0..1]
               + a1update
               + a1fupdate
               + a2_add . a2addx[0..1]
               + a2update
               */
            addareg = ((a1_add && a1addx == 3) || a1update || a1fupdate
                  || (a2_add && a2addx == 3) || a2update ? true : false);
            /* The adders can be put into subtract mode in add pixel size
               mode when the corresponding flags are set */
            suba_x = ((a1_add && a1xsign && a1addx == 1) || (a2_add && a2xsign && a2addx == 1) ? true : false);
            suba_y = ((a1_add && a1addy && a1ysign) || (a2_add && a2addy && a2ysign) ? true : false);
            /* Address adder input B selection
               00	A1 pointer
               01	A2 pointer
               10	A1 fraction
               11	Zero

               Bit 1 =   a1fupdate
               + (a1_add . atick[0] . a1addx[0..1])
               + a1fupdate . a1_stepld
               + a1update . a1_stepld
               + a2update . a2_stepld
               Bit 0 =   a2update + a2_add
               + a1fupdate . a1_stepld
               + a1update . a1_stepld
               + a2update . a2_stepld
               */
            addbsel = (a2update || a2_add || (a1fupdate && a1_stepld)
                  || (a1update && a1_stepld) || (a2update && a2_stepld) ? 0x01 : 0x00);
            addbsel |= (a1fupdate || (a1_add && a1addx == 3) || (a1fupdate && a1_stepld)
                  || (a1update && a1_stepld) || (a2update && a2_stepld) ? 0x02 : 0x00);

            /* The modulo bits are used to align X onto a phrase boundary when
               it is being updated by one phrase
               000	no mask
               001	mask bit 0
               010	mask bits 1-0
               ..
               110  	mask bits 5-0

               Masking is enabled for a1 when a1addx[0..1] is 00, and the value
               is 6 - the pixel size (again!)
               */
            maska1 = (a1_add && a1addx == 0 ? 6 - a1_pixsize : 0);
            maska2 = (a2_add && a2addx == 0 ? 6 - a2_pixsize : 0);
            modx = (a2_add ? maska2 : maska1);
            /* Generate load strobes for the increment updates */

            /*A1pldt		:= NAN2 (a1pldt, atick[1], a1_add);
A1ptrldi	:= NAN2 (a1ptrldi, a1update\, a1pldt);

A1fldt		:= NAN4 (a1fldt, atick[0], a1_add, a1addx[0..1]);
A1fracldi	:= NAN2 (a1fracldi, a1fupdate\, a1fldt);

A2pldt		:= NAN2 (a2pldt, atick[1], a2_add);
A2ptrldi	:= NAN2 (a2ptrldi, a2update\, a2pldt);*/

            a1fracldi = a1fupdate || (a1_add && a1addx == 3);

            // Some more from DCONTROL...
            // atick[] just MAY be important here! We're assuming it's true and dropping the term...
            // That will probably screw up some of the lower terms that seem to rely on the timing of it...
//#warning srcdreadd is not properly initialized!
            srcdreadd = false;						// Set in INNER.NET
            //Shadeadd\	:= NAN2H (shadeadd\, dwrite, srcshade);
            //Shadeadd	:= INV2 (shadeadd, shadeadd\);
            shadeadd = dwrite && srcshade;
            /* Data adder control, input A selection
               000   Destination data
               001   Initialiser pixel value
               100   Source data      - computed intensity fraction
               101   Pattern data     - computed intensity
               110   Source zed 1     - computed zed
               111   Source zed 2     - computed zed fraction

               Bit 0 =   dwrite  . gourd . atick[1]
               + dzwrite . gourz . atick[0]
               + istepadd
               + zstepfadd
               + init_if + init_ii + init_zf + init_zi
               Bit 1 =   dzwrite . gourz . (atick[0] + atick[1])
               + zstepadd
               + zstepfadd
               Bit 2 =   (gourd + gourz) . /(init_if + init_ii + init_zf + init_zi)
               + dwrite  . srcshade
               */
            daddasel = ((dwrite && gourd) || (dzwrite && gourz) || istepadd || zstepfadd
                  || init_if || init_ii || init_zf || init_zi ? 0x01 : 0x00);
            daddasel |= ((dzwrite && gourz) || zstepadd || zstepfadd ? 0x02 : 0x00);
            daddasel |= (((gourd || gourz) && !(init_if || init_ii || init_zf || init_zi))
                  || (dwrite && srcshade) ? 0x04 : 0x00);
            /* Data adder control, input B selection
               0000	Source data
               0001	Data initialiser increment
               0100	Bottom 16 bits of I increment repeated four times
               0101	Top 16 bits of I increment repeated four times
               0110	Bottom 16 bits of Z increment repeated four times
               0111	Top 16 bits of Z increment repeated four times
               1100	Bottom 16 bits of I step repeated four times
               1101	Top 16 bits of I step repeated four times
               1110	Bottom 16 bits of Z step repeated four times
               1111	Top 16 bits of Z step repeated four times

               Bit 0 =   dwrite  . gourd . atick[1]
               + dzwrite . gourz . atick[1]
               + dwrite  . srcshade
               + istepadd
               + zstepadd
               + init_if + init_ii + init_zf + init_zi
               Bit 1 =   dzwrite . gourz . (atick[0] + atick[1])
               + zstepadd
               + zstepfadd
               Bit 2 =   dwrite  . gourd . (atick[0] + atick[1])
               + dzwrite . gourz . (atick[0] + atick[1])
               + dwrite  . srcshade
               + istepadd + istepfadd + zstepadd + zstepfadd
               Bit 3 =   istepadd + istepfadd + zstepadd + zstepfadd
               */
            daddbsel = ((dwrite && gourd) || (dzwrite && gourz) || (dwrite && srcshade)
                  || istepadd || zstepadd || init_if || init_ii || init_zf || init_zi ? 0x01 : 0x00);
            daddbsel |= ((dzwrite && gourz) || zstepadd || zstepfadd ? 0x02 : 0x00);
            daddbsel |= ((dwrite && gourd) || (dzwrite && gourz) || (dwrite && srcshade)
                  || istepadd || istepfadd || zstepadd || zstepfadd ? 0x04 : 0x00);
            daddbsel |= (istepadd && istepfadd && zstepadd && zstepfadd ? 0x08 : 0x00);
            /* Data adder mode control
               000	16-bit normal add
               001	16-bit saturating add with carry
               010	8-bit saturating add with carry, carry into top byte is
               inhibited (YCrCb)
               011	8-bit saturating add with carry, carry into top byte and
               between top nybbles is inhibited (CRY)
               100	16-bit normal add with carry
               101	16-bit saturating add
               110	8-bit saturating add, carry into top byte is inhibited
               111	8-bit saturating add, carry into top byte and between top
               nybbles is inhibited

               The first five are used for Gouraud calculations, the latter three
               for adding source and destination data

               Bit 0 =   dzwrite . gourz . atick[1]
               + dwrite  . gourd . atick[1] . /topnen . /topben . /ext_int
               + dwrite  . gourd . atick[1] .  topnen .  topben . /ext_int
               + zstepadd
               + istepadd . /topnen . /topben . /ext_int
               + istepadd .  topnen .  topben . /ext_int
               + /gourd . /gourz . /topnen . /topben
               + /gourd . /gourz .  topnen .  topben
               + shadeadd . /topnen . /topben
               + shadeadd .  topnen .  topben
               + init_ii . /topnen . /topben . /ext_int
               + init_ii .  topnen .  topben . /ext_int
               + init_zi

               Bit 1 =   dwrite . gourd . atick[1] . /topben . /ext_int
               + istepadd . /topben . /ext_int
               + /gourd . /gourz .  /topben
               + shadeadd .  /topben
               + init_ii .  /topben . /ext_int

               Bit 2 =   /gourd . /gourz
               + shadeadd
               + dwrite  . gourd . atick[1] . ext_int
               + istepadd . ext_int
               + init_ii . ext_int
               */
            daddmode = ((dzwrite && gourz) || (dwrite && gourd && !topnen && !topben && !ext_int)
                  || (dwrite && gourd && topnen && topben && !ext_int) || zstepadd
                  || (istepadd && !topnen && !topben && !ext_int)
                  || (istepadd && topnen && topben && !ext_int) || (!gourd && !gourz && !topnen && !topben)
                  || (!gourd && !gourz && topnen && topben) || (shadeadd && !topnen && !topben)
                  || (shadeadd && topnen && topben) || (init_ii && !topnen && !topben && !ext_int)
                  || (init_ii && topnen && topben && !ext_int) || init_zi ? 0x01 : 0x00);
            daddmode |= ((dwrite && gourd && !topben && !ext_int) || (istepadd && !topben && !ext_int)
                  || (!gourd && !gourz && !topben) || (shadeadd && !topben)
                  || (init_ii && !topben && !ext_int) ? 0x02 : 0x00);
            daddmode |= ((!gourd && !gourz) || shadeadd || (dwrite && gourd && ext_int)
                  || (istepadd && ext_int) || (init_ii && ext_int) ? 0x04 : 0x00);

            patfadd = (dwrite && gourd) || (istepfadd && !datinit) || init_if;
            patdadd = (dwrite && gourd) || (istepadd && !datinit) || init_ii;
            srcz1add = (dzwrite && gourz) || (zstepadd && !datinit) || init_zi;
            srcz2add = (dzwrite && gourz) || zstepfadd || init_zf;
            srcshadd = srcdreadd && srcshade;
            daddq_sel = patfadd || patdadd || srcz1add || srcz2add || srcshadd;
            /* Select write data
               This has to be controlled from stage 1 of the pipe-line, delayed
               by one tick, as the write occurs in the cycle after the ack.

               00	pattern data
               01	lfu data
               10	adder output
               11	source zed

               Bit 0 =  /patdsel . /adddsel
               + dzwrite1d
               Bit 1 =   adddsel
               + dzwrite1d
               */

            data_sel = ((!patdsel && !adddsel) || dzwrite ? 0x01 : 0x00)
               | (adddsel || dzwrite ? 0x02 : 0x00);

            ADDRGEN(&address, &pixAddr, gena2i, zaddr,
                  a1_x, a1_y, a1_base, a1_pitch, a1_pixsize, a1_width, a1_zoffset,
                  a2_x, a2_y, a2_base, a2_pitch, a2_pixsize, a2_width, a2_zoffset);

            //Here's my guess as to how the addresses get truncated to phrase boundaries in phrase mode...
            if (!justify)
               address &= 0xFFFFF8;

            /* Generate source alignment shift
               -------------------------------
               The source alignment shift for data move is the difference between
               the source and destination X pointers, multiplied by the pixel
               size.  Only the low six bits of the pointers are of interest, as
               pixel sizes are always a power of 2 and window rows are always
               phrase aligned.

               When not in phrase mode, the top 3 bits of the shift value are
               set to zero (2/26).

               Source shifting is also used to extract bits for bit-to-byte
               expansion in phrase mode.  This involves only the bottom three
               bits of the shift value, and is based on the offset within the
               phrase of the destination X pointer, in pixels.

               Source shifting is disabled when srcen is not set.
               */

            dstxp = (dsta2 ? a2_x : a1_x) & 0x3F;
            srcxp = (dsta2 ? a1_x : a2_x) & 0x3F;
            shftv = ((dstxp - srcxp) << pixsize) & 0x3F;
            /* The phrase mode alignment count is given by the phrase offset
               of the first pixel, for bit to byte expansion */
            pobb = 0;

            if (pixsize == 3)
               pobb = dstxp & 0x07;
            else if (pixsize == 4)
               pobb = dstxp & 0x03;
            else if (pixsize == 5)
               pobb = dstxp & 0x01;

            pobbsel = phrase_mode && bcompen;
            loshd   = (pobbsel ? pobb : shftv) & 0x07;
            shfti   = (srcen || pobbsel ? (sshftld ? loshd : srcshift & 0x07) : 0);
            /* Enable for high bits is srcen . phrase_mode */
            shfti |= (srcen && phrase_mode ? (sshftld ? shftv & 0x38 : srcshift & 0x38) : 0);
            srcshift = shfti;

            if (sreadx)
            {
               //uint32_t srcAddr, pixAddr;
               //ADDRGEN(srcAddr, pixAddr, gena2i, zaddr,
               //	a1_x, a1_y, a1_base, a1_pitch, a1_pixsize, a1_width, a1_zoffset,
               //	a2_x, a2_y, a2_base, a2_pitch, a2_pixsize, a2_width, a2_zoffset);
               srcd2 = srcd1;
               srcd1 = ((uint64_t)JaguarReadLong(address + 0, BLITTER) << 32)
                  | (uint64_t)JaguarReadLong(address + 4, BLITTER);
               //Kludge to take pixel size into account...
               //Hmm. If we're not in phrase mode, this is most likely NOT going to be used...
               //Actually, it would be--because of BCOMPEN expansion, for example...
               if (!phrase_mode)
               {
                  if (bcompen)
                     srcd1 >>= 56;
                  else
                  {
                     if (pixsize == 5)
                        srcd1 >>= 32;
                     else if (pixsize == 4)
                        srcd1 >>= 48;
                     else
                        srcd1 >>= 56;
                  }
               }//*/
            }

            if (szreadx)
            {
               srcz2 = srcz1;
               srcz1 = ((uint64_t)JaguarReadLong(address, BLITTER) << 32) | (uint64_t)JaguarReadLong(address + 4, BLITTER);
            }

            if (sread)
            {
               srcd2 = srcd1;
               srcd1 = ((uint64_t)JaguarReadLong(address, BLITTER) << 32) | (uint64_t)JaguarReadLong(address + 4, BLITTER);
               //Kludge to take pixel size into account...
               if (!phrase_mode)
               {
                  if (bcompen)
                     srcd1 >>= 56;
                  else
                  {
                     if (pixsize == 5)
                        srcd1 >>= 32;
                     else if (pixsize == 4)
                        srcd1 >>= 48;
                     else
                        srcd1 >>= 56;
                  }
               }
            }

            if (szread)
            {
               srcz2 = srcz1;
               srcz1 = ((uint64_t)JaguarReadLong(address, BLITTER) << 32) | (uint64_t)JaguarReadLong(address + 4, BLITTER);
               //Kludge to take pixel size into account... I believe that it only has to take 16BPP mode into account. Not sure tho.
               if (!phrase_mode && pixsize == 4)
                  srcz1 >>= 48;

            }

            if (dread)
            {
               dstd = ((uint64_t)JaguarReadLong(address, BLITTER) << 32) | (uint64_t)JaguarReadLong(address + 4, BLITTER);
               //Kludge to take pixel size into account...
               if (!phrase_mode)
               {
                  if (pixsize == 5)
                     dstd >>= 32;
                  else if (pixsize == 4)
                     dstd >>= 48;
                  else
                     dstd >>= 56;
               }
            }

            if (dzread)
            {
               // Is Z always 64 bit read? Or sometimes 16 bit (dependent on phrase_mode)?
               dstz = ((uint64_t)JaguarReadLong(address, BLITTER) << 32) | (uint64_t)JaguarReadLong(address + 4, BLITTER);
               //Kludge to take pixel size into account... I believe that it only has to take 16BPP mode into account. Not sure tho.
               if (!phrase_mode && pixsize == 4)
                  dstz >>= 48;

            }

            // These vars should probably go further up in the code... !!! FIX !!!
            // We can't preassign these unless they're static...
            //NOTE: SRCSHADE requires GOURZ to be set to work properly--another Jaguar I bug
            if (dwrite)
            {
               //Counter is done on the dwrite state...! (We'll do it first, since it affects dstart/dend calculations.)
               //Here's the voodoo for figuring the correct amount of pixels in phrase mode (or not):
               int8_t inct = -((dsta2 ? a2_x : a1_x) & 0x07);	// From INNER_CNT
               uint8_t inc = 0;
               uint16_t oldicount;
               uint8_t dstart = 0;

               inc = (!phrase_mode || (phrase_mode && (inct & 0x01)) ? 0x01 : 0x00);
               inc |= (phrase_mode && (((pixsize == 3 || pixsize == 4) && (inct & 0x02)) || (pixsize == 5 && !(inct & 0x01))) ? 0x02 : 0x00);
               inc |= (phrase_mode && ((pixsize == 3 && (inct & 0x04)) || (pixsize == 4 && !(inct & 0x03))) ? 0x04 : 0x00);
               inc |= (phrase_mode && pixsize == 3 && !(inct & 0x07) ? 0x08 : 0x00);

               oldicount = icount;	// Save icount to detect underflow...
               icount -= inc;

               if (icount == 0 || ((icount & 0x8000) && !(oldicount & 0x8000)))
                  inner0 = true;
               // X/Y stepping is also done here, I think...No. It's done when a1_add or a2_add is asserted...

               //*********************************************************************************
               //Start & end write mask computations...
               //*********************************************************************************


               if (phrase_mode)
               {
                  if (pixsize == 3)
                     dstart = (dstxp & 0x07) << 3;
                  else if (pixsize == 4)
                     dstart = (dstxp & 0x03) << 4;
                  else if (pixsize == 5)
                     dstart = (dstxp & 0x01) << 5;
               }
               else
                  dstart    = pixAddr & 0x07;

               //This is the other Jaguar I bug... Normally, should ALWAYS select a1_x here.
               dstxwr = (dsta2 ? a2_x : a1_x) & 0x7FFE;
               pseq = dstxwr ^ (a1_win_x & 0x7FFE);
               pseq = (pixsize == 5 ? pseq : pseq & 0x7FFC);
               pseq = ((pixsize & 0x06) == 4 ? pseq : pseq & 0x7FF8);
               penden = clip_a1 && (pseq == 0);
               window_mask = 0;

               if (penden)
               {
                  if (pixsize == 3)
                     window_mask = (a1_win_x & 0x07) << 3;
                  else if (pixsize == 4)
                     window_mask = (a1_win_x & 0x03) << 4;
                  else if (pixsize == 5)
                     window_mask = (a1_win_x & 0x01) << 5;
               }
               else
                  window_mask    = 0;

               /* The mask to be used if within one phrase of the end of the inner
                  loop, similarly */

               if (inner0)
               {
                  if (pixsize == 3)
                     inner_mask = (icount & 0x07) << 3;
                  else if (pixsize == 4)
                     inner_mask = (icount & 0x03) << 4;
                  else if (pixsize == 5)
                     inner_mask = (icount & 0x01) << 5;
               }
               else
                  inner_mask    = 0;

               /* The actual mask used should be the 
                  lesser of the window masks and
                  the inner mask, where is all cases 000 means 1000. */
               window_mask = (window_mask == 0 ? 0x40 : window_mask);
               inner_mask  = (inner_mask == 0 ? 0x40 : inner_mask);

               emask       = (window_mask > inner_mask ? inner_mask : window_mask);
               /* The mask to be used for the pixel size, to which must be added
                  the bit offset */
               pma = pixAddr + (1 << pixsize);
               /* Select the mask */
               dend = (phrase_mode ? emask : pma);

               /* The cycle width in phrase mode is normally one phrase.  However,
                  at the start and end it may be narrower.  The start and end masks
                  are used to generate this.  The width is given by:

                  8 - start mask - (8 - end mask)
                  =	end mask - start mask

                  This is only used for writes in phrase mode.
                  Start and end from the address level of the pipeline are used.
                  */

               //More testing... This is almost certainly wrong, but how else does this work???
               //Seems to kinda work... But still, this doesn't seem to make any sense!
               if (phrase_mode && !dsten)
                  dstd = ((uint64_t)JaguarReadLong(address, BLITTER) << 32) | (uint64_t)JaguarReadLong(address + 4, BLITTER);

               //Testing only... for now...
               //This is wrong because the write data is a combination of srcd and dstd--either run
               //thru the LFU or in PATDSEL or ADDDSEL mode. [DONE now, thru DATA module]
               // Precedence is ADDDSEL > PATDSEL > LFU.
               //Also, doesn't take into account the start & end masks, or the phrase width...
               //Now it does!

               // srcd2 = xxxx xxxx 0123 4567, srcd = 8901 2345 xxxx xxxx, srcshift = $20 (32)
               srcd = (srcd2 << (64 - srcshift)) | (srcd1 >> srcshift);
               //bleh, ugly ugly ugly
               if (srcshift == 0)
                  srcd = srcd1;

               //NOTE: This only works with pixel sizes less than 8BPP...
               //DOUBLE NOTE: Still need to do regression testing to ensure that this doesn't break other stuff... !!! CHECK !!!
               if (!phrase_mode && srcshift != 0)
                  srcd = ((srcd2 & 0xFF) << (8 - srcshift)) | ((srcd1 & 0xFF) >> srcshift);

               //Z DATA() stuff done here... And it has to be done before any Z shifting...
               //Note that we need to have phrase mode start/end support here... (Not since we moved it from dzwrite...!)
               /*
                  Here are a couple of Cybermorph blits with Z:
                  $00113078	// DSTEN DSTENZ DSTWRZ CLIP_A1 GOURD GOURZ PATDSEL ZMODE=4
                  $09900F39	// SRCEN DSTEN DSTENZ DSTWRZ UPDA1 UPDA1F UPDA2 DSTA2 ZMODE=4 LFUFUNC=C DCOMPEN

                  We're having the same phrase mode overwrite problem we had with the pixels... !!! FIX !!!
                  Odd. It's equating 0 with 0... Even though ZMODE is $04 (less than)!
                  */
               if (gourz)
               {
                  uint16_t addq[4];
                  uint8_t initcin[4] = { 0, 0, 0, 0 };
                  ADDARRAY(addq, 7/*daddasel*/, 6/*daddbsel*/, 0/*daddmode*/, 0, 0, initcin, 0, 0, 0, 0, 0, srcz1, srcz2, zinc, 0);
                  srcz2 = ((uint64_t)addq[3] << 48) | ((uint64_t)addq[2] << 32) | ((uint64_t)addq[1] << 16) | (uint64_t)addq[0];
                  ADDARRAY(addq, 6/*daddasel*/, 7/*daddbsel*/, 1/*daddmode*/, 0, 0, initcin, 0, 0, 0, 0, 0, srcz1, srcz2, zinc, 0);
                  srcz1 = ((uint64_t)addq[3] << 48) | ((uint64_t)addq[2] << 32) | ((uint64_t)addq[1] << 16) | (uint64_t)addq[0];

               }

               zSrcShift = srcshift & 0x30;
               srcz = (srcz2 << (64 - zSrcShift)) | (srcz1 >> zSrcShift);
               //bleh, ugly ugly ugly
               if (zSrcShift == 0)
                  srcz = srcz1;


               //When in SRCSHADE mode, it adds the IINC to the read source (from LFU???)
               //According to following line, it gets LFU mode. But does it feed the source into the LFU
               //after the add?
               //Dest write address/pix address: 0014E83E/0 [dstart=0 dend=10 pwidth=8 srcshift=0][daas=4 dabs=5 dam=7 ds=1 daq=F] [0000000000006505] (icount=003F, inc=1)
               //Let's try this:
               if (srcshade)
               {
                  //NOTE: This is basically doubling the work done by DATA--since this is what
                  //      ADDARRAY is loaded with when srschshade is enabled... !!! FIX !!!
                  //      Also note that it doesn't work properly unless GOURZ is set--there's the clue!
                  uint16_t addq[4];
                  uint8_t initcin[4] = { 0, 0, 0, 0 };
                  ADDARRAY(addq, 4/*daddasel*/, 5/*daddbsel*/, 7/*daddmode*/, dstd, iinc, initcin, 0, 0, 0, patd, srcd, 0, 0, 0, 0);
                  srcd = ((uint64_t)addq[3] << 48) | ((uint64_t)addq[2] << 32) | ((uint64_t)addq[1] << 16) | (uint64_t)addq[0];
               }
               //Seems to work... Not 100% sure tho.
               //end try this

               //Temporary kludge, to see if the fractional pattern does anything...
               //This works, BTW
               //But it seems to mess up in Cybermorph... the shading should be smooth but it isn't...
               //Seems the carry out is lost again... !!! FIX !!! [DONE--see below]
               if (patfadd)
               {
                  uint16_t addq[4];
                  uint8_t initcin[4] = { 0, 0, 0, 0 };
                  ADDARRAY(addq, 4/*daddasel*/, 4/*daddbsel*/, 0/*daddmode*/, dstd, iinc, initcin, 0, 0, 0, patd, srcd, 0, 0, 0, 0);
                  srcd1 = ((uint64_t)addq[3] << 48) | ((uint64_t)addq[2] << 32) | ((uint64_t)addq[1] << 16) | (uint64_t)addq[0];
               }

               //Note that we still don't take atick[0] & [1] into account here, so this will skip half of the data needed... !!! FIX !!!
               //Not yet enumerated: dbinh, srcdread, srczread
               //Also, should do srcshift on the z value in phrase mode... !!! FIX !!! [DONE]
               //As well as add a srcz variable we can set external to this state... !!! FIX !!! [DONE]

               DATA(&wdata, &dcomp, &zcomp, &winhibit,
                     true, cmpdst, daddasel, daddbsel, daddmode, daddq_sel, data_sel, 0/*dbinh*/,
                     dend, dstart, dstd, iinc, lfufunc, &patd, patdadd,
                     phrase_mode, srcd, false/*srcdread*/, false/*srczread*/, srcz2add, zmode,
                     bcompen, bkgwren, dcompen, icount & 0x07, pixsize,
                     &srcz, dstz, zinc);

               /*
                  DEF ADDRCOMP (
                  a1_outside	// A1 pointer is outside window bounds
                  :OUT;
                  INT16/	a1_x
                  INT16/	a1_y
                  INT15/	a1_win_x
                  INT15/	a1_win_y
                  :IN);
                  BEGIN

               // The address is outside if negative, or if greater than or equal
               // to the window size

A1_xcomp	:= MAG_15 (a1xgr, a1xeq, a1xlt, a1_x{0..14}, a1_win_x{0..14});
A1_ycomp	:= MAG_15 (a1ygr, a1yeq, a1ylt, a1_y{0..14}, a1_win_y{0..14});
A1_outside	:= OR6 (a1_outside, a1_x{15}, a1xgr, a1xeq, a1_y{15}, a1ygr, a1yeq);
*/
               //NOTE: There seems to be an off-by-one bug here in the clip_a1 section... !!! FIX !!!
               //      Actually, seems to be related to phrase mode writes...
               //      Or is it? Could be related to non-15-bit compares as above?
               if (clip_a1 && ((a1_x & 0x8000) || (a1_y & 0x8000) || (a1_x >= a1_win_x) || (a1_y >= a1_win_y)))
                  winhibit = true;

               if (!winhibit)
               {
                  if (phrase_mode)
                  {
                     JaguarWriteLong(address + 0, wdata >> 32, BLITTER);
                     JaguarWriteLong(address + 4, wdata & 0xFFFFFFFF, BLITTER);
                  }
                  else
                  {
                     if (pixsize == 5)
                        JaguarWriteLong(address, wdata & 0xFFFFFFFF, BLITTER);
                     else if (pixsize == 4)
                        JaguarWriteWord(address, wdata & 0x0000FFFF, BLITTER);
                     else
                        JaguarWriteByte(address, wdata & 0x000000FF, BLITTER);
                  }
               }

            }

            if (dzwrite)
            {
               // OK, here's the big insight: When NOT in GOURZ mode, srcz1 & 2 function EXACTLY the same way that
               // srcd1 & 2 work--there's an implicit shift from srcz1 to srcz2 whenever srcz1 is read.
               // OTHERWISE, srcz1 is the integer for the computed Z and srcz2 is the fractional part.
               // Writes to srcz1 & 2 follow the same pattern as the other 64-bit registers--low 32 at the low address,
               // high 32 at the high address (little endian!).
               // NOTE: GOURZ is still not properly supported. Check patd/patf handling...
               //       Phrase mode start/end masks are not properly supported either...
               //This is not correct... !!! FIX !!!
               //Should be OK now... We'll see...
               //Nope. Having the same starstep write problems in phrase mode as we had with pixels... !!! FIX !!!
               //This is not causing the problem in Hover Strike... :-/
               //The problem was with the SREADX not shifting. Still problems with Z comparisons & other text in pregame screen...
               if (!winhibit)
               {
                  if (phrase_mode)
                  {
                     JaguarWriteLong(address + 0, srcz >> 32, BLITTER);
                     JaguarWriteLong(address + 4, srcz & 0xFFFFFFFF, BLITTER);
                  }
                  else
                  {
                     if (pixsize == 4)
                        JaguarWriteWord(address, srcz & 0x0000FFFF, BLITTER);
                  }
               }//*/
            }


            if (a1_add)
            {
               int16_t adda_x, adda_y, addb_x, addb_y, addq_x, addq_y;
               ADDAMUX(&adda_x, &adda_y, addasel, a1_step_x, a1_step_y, a1_stepf_x, a1_stepf_y, a2_step_x, a2_step_y,
                     a1_inc_x, a1_inc_y, a1_incf_x, a1_incf_y, adda_xconst, adda_yconst, addareg, suba_x, suba_y);
               ADDBMUX(&addb_x, &addb_y, addbsel, a1_x, a1_y, a2_x, a2_y, a1_frac_x, a1_frac_y);
               ADDRADD(&addq_x, &addq_y, a1fracldi, adda_x, adda_y, addb_x, addb_y, modx, suba_x, suba_y);

               //Now, write to what???
               //a2ptrld comes from a2ptrldi...
               //I believe it's addbsel that determines the writeback...
               // This is where atick[0] & [1] come in, in determining which part (fractional, integer)
               // gets written to...
               //a1_x = addq_x;
               //a1_y = addq_y;
               //Kludge, to get A1 channel increment working...
               if (a1addx == 3)
               {
                  a1_frac_x = addq_x, a1_frac_y = addq_y;

                  addasel = 2, addbsel = 0, a1fracldi = false;
                  ADDAMUX(&adda_x, &adda_y, addasel, a1_step_x, a1_step_y, a1_stepf_x, a1_stepf_y, a2_step_x, a2_step_y,
                        a1_inc_x, a1_inc_y, a1_incf_x, a1_incf_y, adda_xconst, adda_yconst, addareg, suba_x, suba_y);
                  ADDBMUX(&addb_x,&addb_y, addbsel, a1_x, a1_y, a2_x, a2_y, a1_frac_x, a1_frac_y);
                  ADDRADD(&addq_x, &addq_y, a1fracldi, adda_x, adda_y, addb_x, addb_y, modx, suba_x, suba_y);

                  a1_x = addq_x, a1_y = addq_y;
               }
               else
                  a1_x = addq_x, a1_y = addq_y;
            }

            if (a2_add)
            {
               int16_t adda_x, adda_y, addb_x, addb_y, addq_x, addq_y;
               ADDAMUX(&adda_x, &adda_y, addasel, a1_step_x, a1_step_y, a1_stepf_x, a1_stepf_y, a2_step_x, a2_step_y,
                     a1_inc_x, a1_inc_y, a1_incf_x, a1_incf_y, adda_xconst, adda_yconst, addareg, suba_x, suba_y);
               ADDBMUX(&addb_x, &addb_y, addbsel, a1_x, a1_y, a2_x, a2_y, a1_frac_x, a1_frac_y);
               ADDRADD(&addq_x, &addq_y, a1fracldi, adda_x, adda_y, addb_x, addb_y, modx, suba_x, suba_y);

               //Now, write to what???
               //a2ptrld comes from a2ptrldi...
               //I believe it's addbsel that determines the writeback...
               a2_x = addq_x;
               a2_y = addq_y;
            }
         }

         indone = true;
         // The outer counter is updated here as well on the clock cycle...

         /* the inner loop is started whenever another state is about to
            cause the inner state to go active */
         //Instart		:= ND7 (instart, innert[0], innert[2..7]);

         //Actually, it's done only when inner gets asserted without the 2nd line of conditions
         //(inner AND !indone)
         //fixed now...
         //Since we don't get here until the inner loop is finished (indone = true) we can get
         //away with doing it here...!
         ocount--;

         if (ocount == 0)
            outer0 = true;
      }

      if (a1fupdate)
      {
         uint32_t a1_frac_xt = (uint32_t)a1_frac_x + (uint32_t)a1_stepf_x;
         uint32_t a1_frac_yt = (uint32_t)a1_frac_y + (uint32_t)a1_stepf_y;
         a1FracCInX = a1_frac_xt >> 16;
         a1FracCInY = a1_frac_yt >> 16;
         a1_frac_x = (uint16_t)(a1_frac_xt & 0xFFFF);
         a1_frac_y = (uint16_t)(a1_frac_yt & 0xFFFF);
      }

      if (a1update)
      {
         a1_x += a1_step_x + a1FracCInX;
         a1_y += a1_step_y + a1FracCInY;
      }

      if (a2update)
      {
         a2_x += a2_step_x;
         a2_y += a2_step_y;
      }
   }

   // We never get here! !!! FIX !!!


   // Write values back to registers (in real blitter, these are continuously updated)
   SET16(blitter_ram, A1_PIXEL + 2, a1_x);
   SET16(blitter_ram, A1_PIXEL + 0, a1_y);
   SET16(blitter_ram, A1_FPIXEL + 2, a1_frac_x);
   SET16(blitter_ram, A1_FPIXEL + 0, a1_frac_y);
   SET16(blitter_ram, A2_PIXEL + 2, a2_x);
   SET16(blitter_ram, A2_PIXEL + 0, a2_y);

}

// Various pieces of the blitter puzzle are teased out here...

void ADDRGEN(uint32_t *address, uint32_t *pixa, bool gena2, bool zaddr,
	uint16_t a1_x, uint16_t a1_y, uint32_t a1_base, uint8_t a1_pitch, uint8_t a1_pixsize, uint8_t a1_width, uint8_t a1_zoffset,
	uint16_t a2_x, uint16_t a2_y, uint32_t a2_base, uint8_t a2_pitch, uint8_t a2_pixsize, uint8_t a2_width, uint8_t a2_zoffset)
{
	uint16_t x = (gena2 ? a2_x : a1_x) & 0xFFFF;	// Actually uses all 16 bits to generate address...!
	uint16_t y = (gena2 ? a2_y : a1_y) & 0x0FFF;
	uint8_t width = (gena2 ? a2_width : a1_width);
	uint8_t pixsize = (gena2 ? a2_pixsize : a1_pixsize);
	uint8_t pitch = (gena2 ? a2_pitch : a1_pitch);
	uint32_t base = (gena2 ? a2_base : a1_base) >> 3;//Only upper 21 bits are passed around the bus? Seems like it...
	uint8_t zoffset = (gena2 ? a2_zoffset : a1_zoffset);

	uint32_t ytm = ((uint32_t)y << 2) + ((width & 0x02) ? (uint32_t)y << 1 : 0) + ((width & 0x01) ? (uint32_t)y : 0);

	uint32_t ya = (ytm << (width >> 2)) >> 2;

	uint32_t pa = ya + x;
   uint8_t pt, za;
   uint32_t phradr, shup, addr;

	*pixa = pa << pixsize;

	pt = ((pitch & 0x01) && !(pitch & 0x02) ? 0x01 : 0x00)
		| (!(pitch & 0x01) && (pitch & 0x02) ? 0x02 : 0x00);
	phradr = (*pixa >> 6) << pt;
	shup = (pitch == 0x03 ? (*pixa >> 6) : 0);

	za = (zaddr ? zoffset : 0) & 0x03;
	addr = za + phradr + (shup << 1) + base;
	*address = ((*pixa & 0x38) >> 3) | ((addr & 0x1FFFFF) << 3);
	*pixa &= 0x07;
}

////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////
// Here's an important bit: The source data adder logic. Need to track down the inputs!!! //
////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////

void ADDARRAY(uint16_t * addq, uint8_t daddasel, uint8_t daddbsel, uint8_t daddmode,
	uint64_t dstd, uint32_t iinc, uint8_t initcin[], uint64_t initinc, uint16_t initpix,
	uint32_t istep, uint64_t patd, uint64_t srcd, uint64_t srcz1, uint64_t srcz2,
	uint32_t zinc, uint32_t zstep)
{
   unsigned i;
   uint16_t adda[4];
   uint16_t wordmux[8];
   uint16_t addb[4];
   uint16_t word;
   bool dbsel2, iincsel;
   uint32_t initpix2 = ((uint32_t)initpix << 16) | initpix;
   uint32_t addalo[8], addahi[8];
   uint8_t cinsel;
   static uint8_t co[4];//These are preserved between calls...
   uint8_t cin[4];
   bool eightbit;
   bool sat, hicinh;

   addalo[0] = dstd & 0xFFFFFFFF;
   addalo[1] = initpix2;
   addalo[2] = 0;
   addalo[3] = 0;
   addalo[4] = srcd & 0xFFFFFFFF;
   addalo[5] = patd & 0xFFFFFFFF;
   addalo[6] = srcz1 & 0xFFFFFFFF;
   addalo[7] = srcz2 & 0xFFFFFFFF;
   addahi[0] = dstd >> 32;
   addahi[1] = initpix2;
   addahi[2] = 0;
   addahi[3] = 0;
   addahi[4] = srcd >> 32;
   addahi[5] = patd >> 32;
   addahi[6] = srcz1 >> 32;
   addahi[7] = srcz2 >> 32;
   adda[0] = addalo[daddasel] & 0xFFFF;
   adda[1] = addalo[daddasel] >> 16;
   adda[2] = addahi[daddasel] & 0xFFFF;
   adda[3] = addahi[daddasel] >> 16;

   wordmux[0] = iinc & 0xFFFF;
   wordmux[1] = iinc >> 16;
   wordmux[2] = zinc & 0xFFFF;
   wordmux[3] = zinc >> 16;;
   wordmux[4] = istep & 0xFFFF;
   wordmux[5] = istep >> 16;;
   wordmux[6] = zstep & 0xFFFF;
   wordmux[7] = zstep >> 16;;
   word = wordmux[((daddbsel & 0x08) >> 1) | (daddbsel & 0x03)];
   dbsel2 = daddbsel & 0x04;
   iincsel = (daddbsel & 0x01) && !(daddbsel & 0x04);

   if (!dbsel2 && !iincsel)
      addb[0] = srcd & 0xFFFF,
         addb[1] = (srcd >> 16) & 0xFFFF,
         addb[2] = (srcd >> 32) & 0xFFFF,
         addb[3] = (srcd >> 48) & 0xFFFF;
   else if (dbsel2 && !iincsel)
      addb[0] = addb[1] = addb[2] = addb[3] = word;
   else if (!dbsel2 && iincsel)
      addb[0] = initinc & 0xFFFF,
         addb[1] = (initinc >> 16) & 0xFFFF,
         addb[2] = (initinc >> 32) & 0xFFFF,
         addb[3] = (initinc >> 48) & 0xFFFF;
   else
      addb[0] = addb[1] = addb[2] = addb[3] = 0;


   cinsel = (daddmode >= 1 && daddmode <= 4 ? 1 : 0);

   for(i = 0; i < 4; i++)
      cin[i] = initcin[i] | (co[i] & cinsel);

   eightbit = daddmode & 0x02;
   sat = daddmode & 0x03;
   hicinh = ((daddmode & 0x03) == 0x03);

   //Note that the carry out is saved between calls to this function...
    ADD16SAT(&addq[0], &co[0], adda[0], addb[0], cin[0], sat, eightbit, hicinh);
    ADD16SAT(&addq[1], &co[1], adda[1], addb[1], cin[1], sat, eightbit, hicinh);
    ADD16SAT(&addq[2], &co[2], adda[2], addb[2], cin[2], sat, eightbit, hicinh);
    ADD16SAT(&addq[3], &co[3], adda[3], addb[3], cin[3], sat, eightbit, hicinh);
}


void ADD16SAT(uint16_t *r, uint8_t *co, uint16_t a, const uint16_t b, const uint8_t cin, const bool sat, const bool eightbit, const bool hicinh)
{
   uint8_t carry[4];
   uint8_t btop, ctop;
   bool saturate, hisaturate;
   uint32_t qt   = (a & 0xFF) + (b & 0xFF) + cin;
   uint16_t q    = qt & 0x00FF;

   carry[0]      = ((qt & 0x0100) ? 1 : 0);
   carry[1]      = (carry[0] && !eightbit ? carry[0] : 0);
   qt            = (a & 0x0F00) + (b & 0x0F00) + (carry[1] << 8);
   carry[2]      = ((qt & 0x1000) ? 1 : 0);
   q            |= qt & 0x0F00;
   carry[3]      = (carry[2] && !hicinh ? carry[2] : 0);
   qt            = (a & 0xF000) + (b & 0xF000) + (carry[3] << 12);
   *co            = ((qt & 0x10000) ? 1 : 0);
   q            |= qt & 0xF000;

   if (eightbit)
   {
      btop  = (b & 0x0080) >> 7;
      ctop  = carry[0];
   }
   else
   {
      btop  = (b & 0x8000) >> 15;
      ctop  = *co;
   }

   saturate = sat && (btop ^ ctop);
   hisaturate = saturate && !eightbit;

   *r = (saturate ? (ctop ? 0x00FF : 0x0000) : q & 0x00FF);
   *r |= (hisaturate ? (ctop ? 0xFF00 : 0x0000) : q & 0xFF00);
}

void ADDAMUX(int16_t *adda_x, int16_t *adda_y, uint8_t addasel, int16_t a1_step_x, int16_t a1_step_y,
	int16_t a1_stepf_x, int16_t a1_stepf_y, int16_t a2_step_x, int16_t a2_step_y,
	int16_t a1_inc_x, int16_t a1_inc_y, int16_t a1_incf_x, int16_t a1_incf_y, uint8_t adda_xconst,
	bool adda_yconst, bool addareg, bool suba_x, bool suba_y)
{

   int16_t addar_x, addar_y, addac_x, addac_y, addas_x, addas_y;
	int16_t xterm[4], yterm[4];
	xterm[0] = a1_step_x, xterm[1] = a1_stepf_x, xterm[2] = a1_inc_x, xterm[3] = a1_incf_x;
	yterm[0] = a1_step_y, yterm[1] = a1_stepf_y, yterm[2] = a1_inc_y, yterm[3] = a1_incf_y;
   if (addasel & 0x04)
   {
      addar_x = a2_step_x;
      addar_y = a2_step_y;
   }
   else
   {
      addar_x = xterm[addasel & 0x03];
      addar_y = yterm[addasel & 0x03];
   }

   /* Generate a constant value - this is a power of 2 in the range
      0-64, or zero.  The control bits are adda_xconst[0..2], when they
      are all 1  the result is 0.
      Constants for Y can only be 0 or 1 */

	addac_x = (adda_xconst == 0x07 ? 0 : 1 << adda_xconst);
	addac_y = (adda_yconst ? 0x01 : 0);

   /* Select between constant value and register value */

   if (addareg)
   {
      addas_x = (addareg ? addar_x : addac_x);
      addas_y = (addareg ? addar_y : addac_y);
   }
   else
   {
      addas_x = (addareg ? addar_x : addac_x);
      addas_y = (addareg ? addar_y : addac_y);
   }

   /* Complement these values (complement flag gives adder carry in)*/

	*adda_x = addas_x ^ (suba_x ? 0xFFFF : 0x0000);
	*adda_y = addas_y ^ (suba_y ? 0xFFFF : 0x0000);
}


/**  ADDBMUX - Address adder input B selection  *******************

This module selects the register to be updated by the address
adder.  This can be one of three registers, the A1 and A2
pointers, or the A1 fractional part. It can also be zero, so that the step
registers load directly into the pointers.
*/

/*DEF ADDBMUX (
INT16/	addb_x
INT16/	addb_y
	:OUT;
	addbsel[0..1]
INT16/	a1_x
INT16/	a1_y
INT16/	a2_x
INT16/	a2_y
INT16/	a1_frac_x
INT16/	a1_frac_y
	:IN);
INT16/	zero16 :LOCAL;
BEGIN*/
void ADDBMUX(int16_t *addb_x, int16_t *addb_y, uint8_t addbsel, int16_t a1_x, int16_t a1_y,
	int16_t a2_x, int16_t a2_y, int16_t a1_frac_x, int16_t a1_frac_y)
{

/*Zero		:= TIE0 (zero);
Zero16		:= JOIN (zero16, zero, zero, zero, zero, zero, zero, zero,
			zero, zero, zero, zero, zero, zero, zero, zero, zero);
Addbselb[0-1]	:= BUF8 (addbselb[0-1], addbsel[0-1]);
Addb_x		:= MX4 (addb_x, a1_x, a2_x, a1_frac_x, zero16, addbselb[0..1]);
Addb_y		:= MX4 (addb_y, a1_y, a2_y, a1_frac_y, zero16, addbselb[0..1]);*/
////////////////////////////////////// C++ CODE //////////////////////////////////////
	int16_t xterm[4], yterm[4];
	xterm[0] = a1_x, xterm[1] = a2_x, xterm[2] = a1_frac_x, xterm[3] = 0;
	yterm[0] = a1_y, yterm[1] = a2_y, yterm[2] = a1_frac_y, yterm[3] = 0;
	*addb_x = xterm[addbsel & 0x03];
	*addb_y = yterm[addbsel & 0x03];
//////////////////////////////////////////////////////////////////////////////////////

//END;
}


/**  DATAMUX - Address local data bus selection  ******************

Select between the adder output and the input data bus
*/

/*DEF DATAMUX (
INT16/	data_x
INT16/	data_y
	:OUT;
INT32/	gpu_din
INT16/	addq_x
INT16/	addq_y
	addqsel
	:IN);

INT16/	gpu_lo, gpu_hi
:LOCAL;
BEGIN*/
void DATAMUX(int16_t *data_x, int16_t *data_y, uint32_t gpu_din, int16_t addq_x, int16_t addq_y, bool addqsel)
{
   if (addqsel)
   {
      *data_x = addq_x;
      *data_y = addq_y;
   }
   else
   {
      *data_x = (int16_t)(gpu_din & 0xFFFF);
      *data_y = (int16_t)(gpu_din >> 16);
   }
}


/******************************************************************
addradd
29/11/90

Blitter Address Adder
---------------------
The blitter address adder is a pair of sixteen bit adders, one
each for X and Y.  The multiplexing of the input terms is
performed elsewhere, but this adder can also perform modulo
arithmetic to align X-addresses onto phrase boundaries.

modx[0..2] take values
000	no mask
001	mask bit 0
010	mask bits 1-0
..
110  	mask bits 5-0

******************************************************************/

void ADDRADD(int16_t *addq_x, int16_t *addq_y, bool a1fracldi,
	uint16_t adda_x, uint16_t adda_y, uint16_t addb_x, uint16_t addb_y, uint8_t modx, bool suba_x, bool suba_y)
{

/* Perform the addition */

/*Adder_x		:= ADD16 (addqt_x[0..15], co_x, adda_x{0..15}, addb_x{0..15}, ci_x);
Adder_y		:= ADD16 (addq_y[0..15], co_y, adda_y{0..15}, addb_y{0..15}, ci_y);*/

/* latch carry and propagate if required */

/*Cxt0		:= AN2 (cxt[0], co_x, a1fracldi);
Cxt1		:= FD1Q (cxt[1], cxt[0], clk[0]);
Ci_x		:= EO (ci_x, cxt[1], suba_x);

yt0			:= AN2 (cyt[0], co_y, a1fracldi);
Cyt1		:= FD1Q (cyt[1], cyt[0], clk[0]);
Ci_y		:= EO (ci_y, cyt[1], suba_y);*/

////////////////////////////////////// C++ CODE //////////////////////////////////////
//I'm sure the following will generate a bunch of warnings, but will have to do for now.
	static uint16_t co_x = 0, co_y = 0;	// Carry out has to propogate between function calls...
	uint16_t ci_x = co_x ^ (suba_x ? 1 : 0);
	uint16_t ci_y = co_y ^ (suba_y ? 1 : 0);
	uint32_t addqt_x = adda_x + addb_x + ci_x;
	uint32_t addqt_y = adda_y + addb_y + ci_y;
	uint16_t mask[8] = { 0xFFFF, 0xFFFE, 0xFFFC, 0xFFF8, 0xFFF0, 0xFFE0, 0xFFC0, 0x0000 };
	co_x = ((addqt_x & 0x10000) && a1fracldi ? 1 : 0);
	co_y = ((addqt_y & 0x10000) && a1fracldi ? 1 : 0);
//////////////////////////////////////////////////////////////////////////////////////

/* Mask low bits of X to 0 if required */

/*Masksel		:= D38H (unused[0], masksel[0..4], maskbit[5], unused[1], modx[0..2]);

Maskbit[0-4]	:= OR2 (maskbit[0-4], masksel[0-4], maskbit[1-5]);

Mask[0-5]	:= MX2 (addq_x[0-5], addqt_x[0-5], zero, maskbit[0-5]);

Addq_x		:= JOIN (addq_x, addq_x[0..5], addqt_x[6..15]);
Addq_y		:= JOIN (addq_y, addq_y[0..15]);*/

////////////////////////////////////// C++ CODE //////////////////////////////////////
	*addq_x = addqt_x & mask[modx];
	*addq_y = addqt_y & 0xFFFF;
//////////////////////////////////////////////////////////////////////////////////////

//Unused[0-1]	:= DUMMY (unused[0-1]);

//END;
}


/*
DEF DATA (
		wdata[0..63]	// co-processor write data bus
		:BUS;
		dcomp[0..7]		// data byte equal flags
		srcd[0..7]		// bits to use for bit to byte expansion
		zcomp[0..3]		// output from Z comparators
		:OUT;
		a1_x[0..1]		// low two bits of A1 X pointer
		big_pix			// pixel organisation is big-endian
		blitter_active	// blitter is active
		clk				// co-processor clock
		cmpdst			// compare dest rather than source
		colorld			// load the pattern color fields
		daddasel[0..2]	// data adder input A selection
		daddbsel[0..3]	// data adder input B selection
		daddmode[0..2]	// data adder mode
		daddq_sel		// select adder output vs. GPU data
		data[0..63]		// co-processor read data bus
		data_ena		// enable write data
		data_sel[0..1]	// select data to write
		dbinh\[0..7]	// byte oriented changed data inhibits
		dend[0..5]		// end of changed write data zone
		dpipe[0..1]		// load computed data pipe-line latch
		dstart[0..5]	// start of changed write data zone
		dstdld[0..1]	// dest data load (two halves)
		dstzld[0..1]	// dest zed load (two halves)
		ext_int			// enable extended precision intensity calculations
INT32/	gpu_din			// GPU data bus
		iincld			// I increment load
		iincldx			// alternate I increment load
		init_if			// initialise I fraction phase
		init_ii			// initialise I integer phase
		init_zf			// initialise Z fraction phase
		intld[0..3]		// computed intensities load
		istepadd		// intensity step integer add
		istepfadd		// intensity step fraction add
		istepld			// I step load
		istepdld		// I step delta load
		lfu_func[0..3]	// LFU function code
		patdadd			// pattern data gouraud add
		patdld[0..1]	// pattern data load (two halves)
		pdsel[0..1]		// select pattern data type
		phrase_mode		// phrase write mode
		reload			// transfer contents of double buffers
		reset\			// system reset
		srcd1ld[0..1]	// source register 1 load (two halves)
		srcdread		// source data read load enable
		srczread		// source zed read load enable
		srcshift[0..5]	// source alignment shift
		srcz1ld[0..1]	// source zed 1 load (two halves)
		srcz2add		// zed fraction gouraud add
		srcz2ld[0..1]	// source zed 2 load (two halves)
		textrgb			// texture mapping in RGB mode
		txtd[0..63]		// data from the texture unit
		zedld[0..3]		// computed zeds load
		zincld			// Z increment load
		zmode[0..2]		// Z comparator mode
		zpipe[0..1]		// load computed zed pipe-line latch
		zstepadd		// zed step integer add
		zstepfadd		// zed step fraction add
		zstepld			// Z step load
		zstepdld		// Z step delta load
		:IN);
*/

void DATA(uint64_t *wdata, uint8_t *dcomp, uint8_t *zcomp, bool *nowrite,
	bool big_pix, bool cmpdst, uint8_t daddasel, uint8_t daddbsel, uint8_t daddmode, bool daddq_sel, uint8_t data_sel,
	uint8_t dbinh, uint8_t dend, uint8_t dstart, uint64_t dstd, uint32_t iinc, uint8_t lfu_func, uint64_t *patd, bool patdadd,
	bool phrase_mode, uint64_t srcd, bool srcdread, bool srczread, bool srcz2add, uint8_t zmode,
	bool bcompen, bool bkgwren, bool dcompen, uint8_t icount, uint8_t pixsize,
	uint64_t *srcz, uint64_t dstz, uint32_t zinc)
{
/*
  Stuff we absolutely *need* to have passed in/out:
IN:
  patdadd, dstd, srcd, patd, daddasel, daddbsel, daddmode, iinc, srcz1, srcz2, big_pix, phrase_mode, cmpdst
OUT:
  changed patd (wdata I guess...) (Nope. We pass it back directly now...)
*/

// Source data registers

/*Data_src	:= DATA_SRC (srcdlo, srcdhi, srcz[0..1], srczo[0..1], srczp[0..1], srcz1[0..1], srcz2[0..1], big_pix,
			clk, gpu_din, intld[0..3], local_data0, local_data1, srcd1ld[0..1], srcdread, srczread, srcshift[0..5],
			srcz1ld[0..1], srcz2add, srcz2ld[0..1], zedld[0..3], zpipe[0..1]);
Srcd[0-7]	:= JOIN (srcd[0-7], srcdlo{0-7});
Srcd[8-31]	:= JOIN (srcd[8-31], srcdlo{8-31});
Srcd[32-63]	:= JOIN (srcd[32-63], srcdhi{0-31});*/

// Destination data registers

/*Data_dst	:= DATA_DST (dstd[0..63], dstz[0..1], clk, dstdld[0..1], dstzld[0..1], load_data[0..1]);
Dstdlo		:= JOIN (dstdlo, dstd[0..31]);
Dstdhi		:= JOIN (dstdhi, dstd[32..63]);*/

// Pattern and Color data registers

// Looks like this is simply another register file for the pattern data registers. No adding or anything funky
// going on. Note that patd & patdv will output the same info.
// Patdldl/h (patdld[0..1]) can select the local_data bus to overwrite the current pattern data...
// Actually, it can be either patdld OR patdadd...!
/*Data_pat	:= DATA_PAT (colord[0..15], int0dp[8..10], int1dp[8..10], int2dp[8..10], int3dp[8..10], mixsel[0..2],
			patd[0..63], patdv[0..1], clk, colorld, dpipe[0], ext_int, gpu_din, intld[0..3], local_data0, local_data1,
			patdadd, patdld[0..1], reload, reset\);
Patdlo		:= JOIN (patdlo, patd[0..31]);
Patdhi		:= JOIN (patdhi, patd[32..63]);*/

// Multiplying data Mixer (NOT IN JAGUAR I)

/*Datamix		:= DATAMIX (patdo[0..1], clk, colord[0..15], dpipe[1], dstd[0..63], int0dp[8..10], int1dp[8..10],
			int2dp[8..10], int3dp[8..10], mixsel[0..2], patd[0..63], pdsel[0..1], srcd[0..63], textrgb, txtd[0..63]);*/

// Logic function unit

/*Lfu		:= LFU (lfu[0..1], srcdlo, srcdhi, dstdlo, dstdhi, lfu_func[0..3]);*/
////////////////////////////////////// C++ CODE //////////////////////////////////////
	uint64_t funcmask[2] = { 0, 0xFFFFFFFFFFFFFFFFLL };
	uint64_t func0 = funcmask[lfu_func & 0x01];
	uint64_t func1 = funcmask[(lfu_func >> 1) & 0x01];
	uint64_t func2 = funcmask[(lfu_func >> 2) & 0x01];
	uint64_t func3 = funcmask[(lfu_func >> 3) & 0x01];
	uint64_t lfu = (~srcd & ~dstd & func0) | (~srcd & dstd & func1) | (srcd & ~dstd & func2) | (srcd & dstd & func3);
   bool mir_bit, mir_byte;
   uint16_t masku;
   uint8_t e_coarse, e_fine;
   uint8_t s_coarse, s_fine;
   uint16_t maskt;
	uint8_t decl38e[2][8] = { { 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF },
		{ 0xFE, 0xFD, 0xFB, 0xF7, 0xEF, 0xDF, 0xBF, 0x7F } };
	uint8_t dech38[8] = { 0x01, 0x02, 0x04, 0x08, 0x10, 0x20, 0x40, 0x80 };
	uint8_t dech38el[2][8] = { { 0x01, 0x02, 0x04, 0x08, 0x10, 0x20, 0x40, 0x80 },
		{ 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 } };
   int en;
   uint64_t cmpd;
	uint8_t dbinht;
   uint16_t addq[4];
   uint8_t initcin[4] = { 0, 0, 0, 0 };
   uint16_t mask;
   uint64_t dmux[4];
   uint64_t ddat;
	uint64_t zwdata;
//////////////////////////////////////////////////////////////////////////////////////

// Increment and Step Registers

// Does it do anything without the step add lines? Check it!
// No. This is pretty much just a register file without the Jaguar II lines...
/*Inc_step	:= INC_STEP (iinc, istep[0..31], zinc, zstep[0..31], clk, ext_int, gpu_din, iincld, iincldx, istepadd,
			istepfadd, istepld, istepdld, reload, reset\, zincld, zstepadd, zstepfadd, zstepld, zstepdld);
Istep		:= JOIN (istep, istep[0..31]);
Zstep		:= JOIN (zstep, zstep[0..31]);*/

// Pixel data comparator

/*Datacomp	:= DATACOMP (dcomp[0..7], cmpdst, dstdlo, dstdhi, patdlo, patdhi, srcdlo, srcdhi);*/
////////////////////////////////////// C++ CODE //////////////////////////////////////
	*dcomp = 0;
	cmpd = *patd ^ (cmpdst ? dstd : srcd);

	if ((cmpd & 0x00000000000000FFLL) == 0)
		*dcomp |= 0x01u;
	if ((cmpd & 0x000000000000FF00LL) == 0)
		*dcomp |= 0x02u;
	if ((cmpd & 0x0000000000FF0000LL) == 0)
		*dcomp |= 0x04u;
	if ((cmpd & 0x00000000FF000000LL) == 0)
		*dcomp |= 0x08u;
	if ((cmpd & 0x000000FF00000000LL) == 0)
		*dcomp |= 0x10u;
	if ((cmpd & 0x0000FF0000000000LL) == 0)
		*dcomp |= 0x20u;
	if ((cmpd & 0x00FF000000000000LL) == 0)
		*dcomp |= 0x40u;
	if ((cmpd & 0xFF00000000000000LL) == 0)
		*dcomp |= 0x80u;
//////////////////////////////////////////////////////////////////////////////////////

// Zed comparator for Z-buffer operations

/*Zedcomp		:= ZEDCOMP (zcomp[0..3], srczp[0..1], dstz[0..1], zmode[0..2]);*/
////////////////////////////////////// C++ CODE //////////////////////////////////////
//srczp is srcz pipelined, also it goes through a source shift as well...
/*The shift is basically like so (each piece is 16 bits long):

	0         1         2         3         4          5         6
	srcz1lolo srcz1lohi srcz1hilo srcz1hihi srcrz2lolo srcz2lohi srcz2hilo

with srcshift bits 4 & 5 selecting the start position
*/
//So... basically what we have here is:
	*zcomp = 0;

	if ((((*srcz & 0x000000000000FFFFLL) < (dstz & 0x000000000000FFFFLL)) && (zmode & 0x01))
		|| (((*srcz & 0x000000000000FFFFLL) == (dstz & 0x000000000000FFFFLL)) && (zmode & 0x02))
		|| (((*srcz & 0x000000000000FFFFLL) > (dstz & 0x000000000000FFFFLL)) && (zmode & 0x04)))
		*zcomp |= 0x01u;

	if ((((*srcz & 0x00000000FFFF0000LL) < (dstz & 0x00000000FFFF0000LL)) && (zmode & 0x01))
		|| (((*srcz & 0x00000000FFFF0000LL) == (dstz & 0x00000000FFFF0000LL)) && (zmode & 0x02))
		|| (((*srcz & 0x00000000FFFF0000LL) > (dstz & 0x00000000FFFF0000LL)) && (zmode & 0x04)))
		*zcomp |= 0x02u;

	if ((((*srcz & 0x0000FFFF00000000LL) < (dstz & 0x0000FFFF00000000LL)) && (zmode & 0x01))
		|| (((*srcz & 0x0000FFFF00000000LL) == (dstz & 0x0000FFFF00000000LL)) && (zmode & 0x02))
		|| (((*srcz & 0x0000FFFF00000000LL) > (dstz & 0x0000FFFF00000000LL)) && (zmode & 0x04)))
		*zcomp |= 0x04u;

	if ((((*srcz & 0xFFFF000000000000LL) < (dstz & 0xFFFF000000000000LL)) && (zmode & 0x01))
		|| (((*srcz & 0xFFFF000000000000LL) == (dstz & 0xFFFF000000000000LL)) && (zmode & 0x02))
		|| (((*srcz & 0xFFFF000000000000LL) > (dstz & 0xFFFF000000000000LL)) && (zmode & 0x04)))
		*zcomp |= 0x08u;

//TEMP, TO TEST IF ZCOMP IS THE CULPRIT...
//Nope, this is NOT the problem...
//zcomp=0;
// We'll do the comparison/bit/byte inhibits here, since that's they way it happens
// in the real thing (dcomp goes out to COMP_CTRL and back into DATA through dbinh)...
	COMP_CTRL(&dbinht, nowrite,
		bcompen, true/*big_pix*/, bkgwren, *dcomp, dcompen, icount, pixsize, phrase_mode, srcd & 0xFF, *zcomp);
	dbinh = dbinht;

//////////////////////////////////////////////////////////////////////////////////////

// 22 Mar 94
// The data initializer - allows all four initial values to be computed from one (NOT IN JAGUAR I)

/*Datinit		:= DATINIT (initcin[0..3], initinc[0..63], initpix[0..15], a1_x[0..1], big_pix, clk, iinc, init_if, init_ii,
			init_zf, istep[0..31], zinc, zstep[0..31]);*/

// Adder array for Z and intensity increments

/*Addarray	:= ADDARRAY (addq[0..3], clk, daddasel[0..2], daddbsel[0..3], daddmode[0..2], dstdlo, dstdhi, iinc,
			initcin[0..3], initinc[0..63], initpix[0..15], istep, patdv[0..1], srcdlo, srcdhi, srcz1[0..1],
			srcz2[0..1], reset\, zinc, zstep);*/
/*void ADDARRAY(uint16_t * addq, uint8_t daddasel, uint8_t daddbsel, uint8_t daddmode,
	uint64_t dstd, uint32_t iinc, uint8_t initcin[], uint64_t initinc, uint16_t initpix,
	uint32_t istep, uint64_t patd, uint64_t srcd, uint64_t srcz1, uint64_t srcz2,
	uint32_t zinc, uint32_t zstep)*/
////////////////////////////////////// C++ CODE //////////////////////////////////////
	ADDARRAY(addq, daddasel, daddbsel, daddmode, dstd, iinc, initcin, 0, 0, 0, *patd, srcd, 0, 0, 0, 0);

	//This is normally done asynchronously above (thru local_data) when in patdadd mode...
//And now it's passed back to the caller to be persistent between calls...!
//But it's causing some serious fuck-ups in T2K now... !!! FIX !!! [DONE--???]
//Weird! It doesn't anymore...!
	if (patdadd)
		*patd = ((uint64_t)addq[3] << 48) | ((uint64_t)addq[2] << 32) | ((uint64_t)addq[1] << 16) | (uint64_t)addq[0];
//////////////////////////////////////////////////////////////////////////////////////

// Local data bus multiplexer

/*Local_mux	:= LOCAL_MUX (local_data[0..1], load_data[0..1],
	addq[0..3], gpu_din, data[0..63], blitter_active, daddq_sel);
Local_data0	:= JOIN (local_data0, local_data[0]);
Local_data1	:= JOIN (local_data1, local_data[1]);*/
////////////////////////////////////// C++ CODE //////////////////////////////////////
//////////////////////////////////////////////////////////////////////////////////////

// Data output multiplexer and tri-state drive

/*Data_mux	:= DATA_MUX (wdata[0..63], addq[0..3], big_pix, dstdlo, dstdhi, dstz[0..1], data_sel[0..1], data_ena,
			dstart[0..5], dend[0..5], dbinh\[0..7], lfu[0..1], patdo[0..1], phrase_mode, srczo[0..1]);*/
////////////////////////////////////// C++ CODE //////////////////////////////////////
// NOTE: patdo comes from DATAMIX and can be considered the same as patd for Jaguar I

//////////////////////////////////////////////////////////////////////////////////////
//}

/*DEF DATA_MUX (
		wdata[0..63]	// co-processor rwrite data bus
		:BUS;
INT16/	addq[0..3]
		big_pix			// Pixel organisation is big-endian
INT32/	dstdlo
INT32/	dstdhi
INT32/	dstzlo
INT32/	dstzhi
		data_sel[0..1]	// source of write data
		data_ena		// enable write data onto read/write bus
		dstart[0..5]	// start of changed write data
		dend[0..5]		// end of changed write data
		dbinh\[0..7]	// byte oriented changed data inhibits
INT32/	lfu[0..1]
INT32/	patd[0..1]
		phrase_mode		// phrase write mode
INT32/	srczlo
INT32/	srczhi
		:IN);*/

/*INT32/	addql[0..1], ddatlo, ddathi zero32
:LOCAL;
BEGIN

Phrase_mode\	:= INV1 (phrase_mode\, phrase_mode);
Zero		:= TIE0 (zero);
Zero32		:= JOIN (zero32, zero, zero, zero, zero, zero, zero, zero, zero, zero, zero, zero, zero, zero, zero, zero, zero, zero, zero, zero, zero, zero, zero, zero, zero, zero, zero, zero, zero, zero, zero, zero, zero);*/

/* Generate a changed data mask */

/*Edis		:= OR6 (edis\, dend[0..5]);
Ecoarse		:= DECL38E (e_coarse\[0..7], dend[3..5], edis\);
E_coarse[0]	:= INV1 (e_coarse[0], e_coarse\[0]);
Efine		:= DECL38E (unused[0], e_fine\[1..7], dend[0..2], e_coarse[0]);*/
////////////////////////////////////// C++ CODE //////////////////////////////////////

	en = ((dend & 0x3F) ? 1 : 0);
	e_coarse = decl38e[en][(dend & 0x38) >> 3];		// Actually, this is e_coarse inverted...
	e_fine = decl38e[(e_coarse & 0x01) ^ 0x01][dend & 0x07];
	e_fine &= 0xFE;
//////////////////////////////////////////////////////////////////////////////////////

/*Scoarse		:= DECH38 (s_coarse[0..7], dstart[3..5]);
Sfen\		:= INV1 (sfen\, s_coarse[0]);
Sfine		:= DECH38EL (s_fine[0..7], dstart[0..2], sfen\);*/
////////////////////////////////////// C++ CODE //////////////////////////////////////
	s_coarse = dech38[(dstart & 0x38) >> 3];
	s_fine = dech38el[(s_coarse & 0x01) ^ 0x01][dstart & 0x07];
//////////////////////////////////////////////////////////////////////////////////////

/*Maskt[0]	:= BUF1 (maskt[0], s_fine[0]);
Maskt[1-7]	:= OAN1P (maskt[1-7], maskt[0-6], s_fine[1-7], e_fine\[1-7]);*/
////////////////////////////////////// C++ CODE //////////////////////////////////////
	maskt = s_fine & 0x0001;
	maskt |= (((maskt & 0x0001) || (s_fine & 0x02u)) && (e_fine & 0x02u) ? 0x0002 : 0x0000);
	maskt |= (((maskt & 0x0002) || (s_fine & 0x04u)) && (e_fine & 0x04u) ? 0x0004 : 0x0000);
	maskt |= (((maskt & 0x0004) || (s_fine & 0x08u)) && (e_fine & 0x08u) ? 0x0008 : 0x0000);
	maskt |= (((maskt & 0x0008) || (s_fine & 0x10u)) && (e_fine & 0x10u) ? 0x0010 : 0x0000);
	maskt |= (((maskt & 0x0010) || (s_fine & 0x20u)) && (e_fine & 0x20u) ? 0x0020 : 0x0000);
	maskt |= (((maskt & 0x0020) || (s_fine & 0x40u)) && (e_fine & 0x40u) ? 0x0040 : 0x0000);
	maskt |= (((maskt & 0x0040) || (s_fine & 0x80u)) && (e_fine & 0x80u) ? 0x0080 : 0x0000);
//////////////////////////////////////////////////////////////////////////////////////

   /* Produce a look-ahead on the ripple carry */
	maskt |= (((s_coarse & e_coarse & 0x01u) || (s_coarse & 0x02u)) && (e_coarse & 0x02u) ? 0x0100 : 0x0000);
	maskt |= (((maskt & 0x0100) || (s_coarse & 0x04u)) && (e_coarse & 0x04u) ? 0x0200 : 0x0000);
	maskt |= (((maskt & 0x0200) || (s_coarse & 0x08u)) && (e_coarse & 0x08u) ? 0x0400 : 0x0000);
	maskt |= (((maskt & 0x0400) || (s_coarse & 0x10u)) && (e_coarse & 0x10u) ? 0x0800 : 0x0000);
	maskt |= (((maskt & 0x0800) || (s_coarse & 0x20u)) && (e_coarse & 0x20u) ? 0x1000 : 0x0000);
	maskt |= (((maskt & 0x1000) || (s_coarse & 0x40u)) && (e_coarse & 0x40u) ? 0x2000 : 0x0000);
	maskt |= (((maskt & 0x2000) || (s_coarse & 0x80u)) && (e_coarse & 0x80u) ? 0x4000 : 0x0000);

/* The bit terms are mirrored for big-endian pixels outside phrase
mode.  The byte terms are mirrored for big-endian pixels in phrase
mode.  */

/*Mirror_bit	:= AN2M (mir_bit, phrase_mode\, big_pix);
Mirror_byte	:= AN2H (mir_byte, phrase_mode, big_pix);

Masktb[14]	:= BUF1 (masktb[14], maskt[14]);
Masku[0]	:= MX4 (masku[0],  maskt[0],  maskt[7],  maskt[14],  zero, mir_bit, mir_byte);
Masku[1]	:= MX4 (masku[1],  maskt[1],  maskt[6],  maskt[14],  zero, mir_bit, mir_byte);
Masku[2]	:= MX4 (masku[2],  maskt[2],  maskt[5],  maskt[14],  zero, mir_bit, mir_byte);
Masku[3]	:= MX4 (masku[3],  maskt[3],  maskt[4],  masktb[14], zero, mir_bit, mir_byte);
Masku[4]	:= MX4 (masku[4],  maskt[4],  maskt[3],  masktb[14], zero, mir_bit, mir_byte);
Masku[5]	:= MX4 (masku[5],  maskt[5],  maskt[2],  masktb[14], zero, mir_bit, mir_byte);
Masku[6]	:= MX4 (masku[6],  maskt[6],  maskt[1],  masktb[14], zero, mir_bit, mir_byte);
Masku[7]	:= MX4 (masku[7],  maskt[7],  maskt[0],  masktb[14], zero, mir_bit, mir_byte);
Masku[8]	:= MX2 (masku[8],  maskt[8],  maskt[13], mir_byte);
Masku[9]	:= MX2 (masku[9],  maskt[9],  maskt[12], mir_byte);
Masku[10]	:= MX2 (masku[10], maskt[10], maskt[11], mir_byte);
Masku[11]	:= MX2 (masku[11], maskt[11], maskt[10], mir_byte);
Masku[12]	:= MX2 (masku[12], maskt[12], maskt[9],  mir_byte);
Masku[13]	:= MX2 (masku[13], maskt[13], maskt[8],  mir_byte);
Masku[14]	:= MX2 (masku[14], maskt[14], maskt[0],  mir_byte);*/
////////////////////////////////////// C++ CODE //////////////////////////////////////

	mir_bit  = true/*big_pix*/ && !phrase_mode;
	mir_byte = true/*big_pix*/ && phrase_mode;
	masku    = maskt;

	if (mir_bit)
	{
		masku &= 0xFF00;
		masku |= (maskt >> 7) & 0x0001;
		masku |= (maskt >> 5) & 0x0002;
		masku |= (maskt >> 3) & 0x0004;
		masku |= (maskt >> 1) & 0x0008;
		masku |= (maskt << 1) & 0x0010;
		masku |= (maskt << 3) & 0x0020;
		masku |= (maskt << 5) & 0x0040;
		masku |= (maskt << 7) & 0x0080;
	}

	if (mir_byte)
	{
		masku = 0;
		masku |= (maskt >> 14) & 0x0001;
		masku |= (maskt >> 13) & 0x0002;
		masku |= (maskt >> 12) & 0x0004;
		masku |= (maskt >> 11) & 0x0008;
		masku |= (maskt >> 10) & 0x0010;
		masku |= (maskt >> 9)  & 0x0020;
		masku |= (maskt >> 8)  & 0x0040;
		masku |= (maskt >> 7)  & 0x0080;

		masku |= (maskt >> 5) & 0x0100;
		masku |= (maskt >> 3) & 0x0200;
		masku |= (maskt >> 1) & 0x0400;
		masku |= (maskt << 1) & 0x0800;
		masku |= (maskt << 3) & 0x1000;
		masku |= (maskt << 5) & 0x2000;
		masku |= (maskt << 7) & 0x4000;
	}
//////////////////////////////////////////////////////////////////////////////////////

/* The maskt terms define the area for changed data, but the byte
inhibit terms can override these */

/*Mask[0-7]	:= AN2 (mask[0-7], masku[0-7], dbinh\[0]);
Mask[8-14]	:= AN2H (mask[8-14], masku[8-14], dbinh\[1-7]);*/
////////////////////////////////////// C++ CODE //////////////////////////////////////
	mask = masku & (!(dbinh & 0x01) ? 0xFFFF : 0xFF00);
	mask &= ~(((uint16_t)dbinh & 0x00FE) << 7);
//////////////////////////////////////////////////////////////////////////////////////

/*Addql[0]	:= JOIN (addql[0], addq[0..1]);
Addql[1]	:= JOIN (addql[1], addq[2..3]);

Dsel0b[0-1]	:= BUF8 (dsel0b[0-1], data_sel[0]);
Dsel1b[0-1]	:= BUF8 (dsel1b[0-1], data_sel[1]);
Ddatlo		:= MX4 (ddatlo, patd[0], lfu[0], addql[0], zero32, dsel0b[0], dsel1b[0]);
Ddathi		:= MX4 (ddathi, patd[1], lfu[1], addql[1], zero32, dsel0b[1], dsel1b[1]);*/
////////////////////////////////////// C++ CODE //////////////////////////////////////
	dmux[0] = *patd;
	dmux[1] = lfu;
	dmux[2] = ((uint64_t)addq[3] << 48) | ((uint64_t)addq[2] << 32) | ((uint64_t)addq[1] << 16) | (uint64_t)addq[0];
	dmux[3] = 0;
	ddat = dmux[data_sel];
//////////////////////////////////////////////////////////////////////////////////////

/*Zed_sel		:= AN2 (zed_sel, data_sel[0..1]);
Zed_selb[0-1]	:= BUF8 (zed_selb[0-1], zed_sel);

Dat[0-7]	:= MX4 (dat[0-7],   dstdlo{0-7},   ddatlo{0-7},   dstzlo{0-7},   srczlo{0-7},   mask[0-7], zed_selb[0]);
Dat[8-15]	:= MX4 (dat[8-15],  dstdlo{8-15},  ddatlo{8-15},  dstzlo{8-15},  srczlo{8-15},  mask[8],   zed_selb[0]);
Dat[16-23]	:= MX4 (dat[16-23], dstdlo{16-23}, ddatlo{16-23}, dstzlo{16-23}, srczlo{16-23}, mask[9],   zed_selb[0]);
Dat[24-31]	:= MX4 (dat[24-31], dstdlo{24-31}, ddatlo{24-31}, dstzlo{24-31}, srczlo{24-31}, mask[10],  zed_selb[0]);
Dat[32-39]	:= MX4 (dat[32-39], dstdhi{0-7},   ddathi{0-7},   dstzhi{0-7},   srczhi{0-7},   mask[11],  zed_selb[1]);
Dat[40-47]	:= MX4 (dat[40-47], dstdhi{8-15},  ddathi{8-15},  dstzhi{8-15},  srczhi{8-15},  mask[12],  zed_selb[1]);
Dat[48-55]	:= MX4 (dat[48-55], dstdhi{16-23}, ddathi{16-23}, dstzhi{16-23}, srczhi{16-23}, mask[13],  zed_selb[1]);
Dat[56-63]	:= MX4 (dat[56-63], dstdhi{24-31}, ddathi{24-31}, dstzhi{24-31}, srczhi{24-31}, mask[14],  zed_selb[1]);*/
////////////////////////////////////// C++ CODE //////////////////////////////////////
	*wdata = ((ddat & mask) | (dstd & ~mask)) & 0x00000000000000FFLL;
	*wdata |= ((mask & 0x0100) ? ddat : dstd) & 0x000000000000FF00LL;
	*wdata |= ((mask & 0x0200) ? ddat : dstd) & 0x0000000000FF0000LL;
	*wdata |= ((mask & 0x0400) ? ddat : dstd) & 0x00000000FF000000LL;
	*wdata |= ((mask & 0x0800) ? ddat : dstd) & 0x000000FF00000000LL;
	*wdata |= ((mask & 0x1000) ? ddat : dstd) & 0x0000FF0000000000LL;
	*wdata |= ((mask & 0x2000) ? ddat : dstd) & 0x00FF000000000000LL;
	*wdata |= ((mask & 0x4000) ? ddat : dstd) & 0xFF00000000000000LL;

//This is a crappy way of handling this, but it should work for now...
	zwdata = ((*srcz & mask) | (dstz & ~mask)) & 0x00000000000000FFLL;
	zwdata |= ((mask & 0x0100) ? *srcz : dstz) & 0x000000000000FF00LL;
	zwdata |= ((mask & 0x0200) ? *srcz : dstz) & 0x0000000000FF0000LL;
	zwdata |= ((mask & 0x0400) ? *srcz : dstz) & 0x00000000FF000000LL;
	zwdata |= ((mask & 0x0800) ? *srcz : dstz) & 0x000000FF00000000LL;
	zwdata |= ((mask & 0x1000) ? *srcz : dstz) & 0x0000FF0000000000LL;
	zwdata |= ((mask & 0x2000) ? *srcz : dstz) & 0x00FF000000000000LL;
	zwdata |= ((mask & 0x4000) ? *srcz : dstz) & 0xFF00000000000000LL;
	*srcz = zwdata;
//////////////////////////////////////////////////////////////////////////////////////

/*Data_enab[0-1]	:= BUF8 (data_enab[0-1], data_ena);
Datadrv[0-31]	:= TS (wdata[0-31],  dat[0-31],  data_enab[0]);
Datadrv[32-63]	:= TS (wdata[32-63], dat[32-63], data_enab[1]);

Unused[0]	:= DUMMY (unused[0]);

END;*/
}


/**  COMP_CTRL - Comparator output control logic  *****************

This block is responsible for taking the comparator outputs and
using them as appropriate to inhibit writes.  Two methods are
supported for inhibiting write data:

-	suppression of the inner loop controlled write operation
-	a set of eight byte inhibit lines to write back dest data

The first technique is used in pixel oriented modes, the second in
phrase mode, but the phrase mode form is only applicable to eight
and sixteen bit pixel modes.

Writes can be suppressed by data being equal, by the Z comparator
conditions being met, or by the bit to pixel expansion scheme.

Pipe-lining issues: the data derived comparator outputs are stable
until the next data read, well after the affected write from this
operation.  However, the inner counter bits can count immediately
before the ack for the last write.  Therefore, it is necessary to
delay bcompbit select terms by one inner loop pipe-line stage,
when generating the select for the data control - the output is
delayed one further tick to give it write data timing (2/34).

There is also a problem with computed data - the new values are
calculated before the write associated with the old value has been
performed.  The is taken care of within the zed comparator by
pipe-lining the comparator inputs where appropriate.
*/

void COMP_CTRL(uint8_t *dbinh, bool *nowrite,
	bool bcompen, bool big_pix, bool bkgwren, uint8_t dcomp, bool dcompen, uint8_t icount,
	uint8_t pixsize, bool phrase_mode, uint8_t srcd, uint8_t zcomp)
{
   //BEGIN

   /*Bkgwren\	:= INV1 (bkgwren\, bkgwren);
     Phrase_mode\	:= INV1 (phrase_mode\, phrase_mode);
     Pixsize\[0-2]	:= INV2 (pixsize\[0-2], pixsize[0-2]);*/

   /* The bit comparator bits are derived from the source data, which
      will have been suitably aligned for phrase mode.  The contents of
      the inner counter are used to select which bit to use.

      When not in phrase mode the inner count value is used to select
      one bit.  It is assumed that the count has already occurred, so,
      7 selects bit 0, etc.  In big-endian pixel mode, this turns round,
      so that a count of 7 selects bit 7.

      In phrase mode, the eight bits are used directly, and this mode is
      only applicable to 8-bit pixel mode (2/34) */

   /*Bcompselt[0-2]	:= EO (bcompselt[0-2], icount[0-2], big_pix);
Bcompbit	:= MX8 (bcompbit, srcd[7], srcd[6], srcd[5],
srcd[4], srcd[3], srcd[2], srcd[1], srcd[0], bcompselt[0..2]);
Bcompbit\	:= INV1 (bcompbit\, bcompbit);*/
   ////////////////////////////////////// C++ CODE //////////////////////////////////////
   uint8_t bcompselt = (big_pix ? ~icount : icount) & 0x07;
   uint8_t bitmask[8] = { 0x80, 0x40, 0x20, 0x10, 0x08, 0x04, 0x02, 0x01 };
   bool bcompbit = srcd & bitmask[bcompselt];
   bool winhibit, di0t0_1, di0t4, di1t2, di2t0_1, di2t4, di3t2;
   bool di4t0_1, di4t4, di5t2;
   bool di6t0_1, di6t4;
   bool di7t2;

   //////////////////////////////////////////////////////////////////////////////////////

   /* pipe-line the count */
   /*Bcompsel[0-2]	:= FDSYNC (bcompsel[0-2], bcompselt[0-2], step_inner, clk);
Bcompbt		:= MX8 (bcompbitpt, srcd[7], srcd[6], srcd[5],
srcd[4], srcd[3], srcd[2], srcd[1], srcd[0], bcompsel[0..2]);
Bcompbitp	:= FD1Q (bcompbitp, bcompbitpt, clk);
Bcompbitp\	:= INV1 (bcompbitp\, bcompbitp);*/

   /* For pixel mode, generate the write inhibit signal for all modes
      on bit inhibit, for 8 and 16 bit modes on comparator inhibit, and
      for 16 bit mode on Z inhibit

      Nowrite = bcompen . /bcompbit . /phrase_mode
      + dcompen . dcomp[0] . /phrase_mode . pixsize = 011
      + dcompen . dcomp[0..1] . /phrase_mode . pixsize = 100
      + zcomp[0] . /phrase_mode . pixsize = 100
      */

   /*Nowt0		:= NAN3 (nowt[0], bcompen, bcompbit\, phrase_mode\);
Nowt1		:= ND6  (nowt[1], dcompen, dcomp[0], phrase_mode\, pixsize\[2], pixsize[0..1]);
Nowt2		:= ND7  (nowt[2], dcompen, dcomp[0..1], phrase_mode\, pixsize[2], pixsize\[0..1]);
Nowt3		:= NAN5 (nowt[3], zcomp[0], phrase_mode\, pixsize[2], pixsize\[0..1]);
Nowt4		:= NAN4 (nowt[4], nowt[0..3]);
Nowrite		:= AN2  (nowrite, nowt[4], bkgwren\);*/
   ////////////////////////////////////// C++ CODE //////////////////////////////////////
   *nowrite = ((bcompen && !bcompbit && !phrase_mode)
         || (dcompen && (dcomp & 0x01) && !phrase_mode && (pixsize == 3))
         || (dcompen && ((dcomp & 0x03) == 0x03) && !phrase_mode && (pixsize == 4))
         || ((zcomp & 0x01) && !phrase_mode && (pixsize == 4)))
      && !bkgwren;
   //////////////////////////////////////////////////////////////////////////////////////

   /*Winht		:= NAN3 (winht, bcompen, bcompbitp\, phrase_mode\);
Winhibit	:= NAN4 (winhibit, winht, nowt[1..3]);*/
   ////////////////////////////////////// C++ CODE //////////////////////////////////////
   //This is the same as above, but with bcompbit delayed one tick and called 'winhibit'
   //Small difference: Besides the pipeline effect, it's also not using !bkgwren...
   //	bool winhibit = (bcompen && !
   winhibit = (bcompen && !bcompbit && !phrase_mode)
      || (dcompen && (dcomp & 0x01) && !phrase_mode && (pixsize == 3))
      || (dcompen && ((dcomp & 0x03) == 0x03) && !phrase_mode && (pixsize == 4))
      || ((zcomp & 0x01) && !phrase_mode && (pixsize == 4));
   //////////////////////////////////////////////////////////////////////////////////////

   /* For phrase mode, generate the byte inhibit signals for eight bit
      mode 011, or sixteen bit mode 100
      dbinh\[0] =  pixsize[2] . zcomp[0]
      +  pixsize[2] . dcomp[0] . dcomp[1] . dcompen
      + /pixsize[2] . dcomp[0] . dcompen
      + /srcd[0] . bcompen

      Inhibits 0-3 are also used when not in phrase mode to write back
      destination data.
      */

   /*Srcd\[0-7]	:= INV1 (srcd\[0-7], srcd[0-7]);

Di0t0		:= NAN2H (di0t[0], pixsize[2], zcomp[0]);
Di0t1		:= NAN4H (di0t[1], pixsize[2], dcomp[0..1], dcompen);
Di0t2		:= NAN2 (di0t[2], srcd\[0], bcompen);
Di0t3		:= NAN3 (di0t[3], pixsize\[2], dcomp[0], dcompen);
Di0t4		:= NAN4 (di0t[4], di0t[0..3]);
Dbinh[0]	:= ANR1P (dbinh\[0], di0t[4], phrase_mode, winhibit);*/
   ////////////////////////////////////// C++ CODE //////////////////////////////////////
   *dbinh = 0;
   di0t0_1 = ((pixsize & 0x04) && (zcomp & 0x01))
      || ((pixsize & 0x04) && (dcomp & 0x01) && (dcomp & 0x02) && dcompen);
   di0t4 = di0t0_1
      || (!(srcd & 0x01) && bcompen)
      || (!(pixsize & 0x04) && (dcomp & 0x01) && dcompen);
   *dbinh |= (!((di0t4 && phrase_mode) || winhibit) ? 0x01 : 0x00);
   //////////////////////////////////////////////////////////////////////////////////////

   /*Di1t0		:= NAN3 (di1t[0], pixsize\[2], dcomp[1], dcompen);
Di1t1		:= NAN2 (di1t[1], srcd\[1], bcompen);
Di1t2		:= NAN4 (di1t[2], di0t[0..1], di1t[0..1]);
Dbinh[1]	:= ANR1 (dbinh\[1], di1t[2], phrase_mode, winhibit);*/
   ////////////////////////////////////// C++ CODE //////////////////////////////////////
   di1t2 = di0t0_1
      || (!(srcd & 0x02) && bcompen)
      || (!(pixsize & 0x04) && (dcomp & 0x02) && dcompen);
   *dbinh |= (!((di1t2 && phrase_mode) || winhibit) ? 0x02 : 0x00);
   //////////////////////////////////////////////////////////////////////////////////////

   /*Di2t0		:= NAN2H (di2t[0], pixsize[2], zcomp[1]);
Di2t1		:= NAN4H (di2t[1], pixsize[2], dcomp[2..3], dcompen);
Di2t2		:= NAN2 (di2t[2], srcd\[2], bcompen);
Di2t3		:= NAN3 (di2t[3], pixsize\[2], dcomp[2], dcompen);
Di2t4		:= NAN4 (di2t[4], di2t[0..3]);
Dbinh[2]	:= ANR1 (dbinh\[2], di2t[4], phrase_mode, winhibit);*/
   ////////////////////////////////////// C++ CODE //////////////////////////////////////
   //[bcompen=F dcompen=T phrase_mode=T bkgwren=F][nw=F wi=F]
   //[di0t0_1=F di0t4=F][di1t2=F][di2t0_1=T di2t4=T][di3t2=T][di4t0_1=F di2t4=F][di5t2=F][di6t0_1=F di6t4=F][di7t2=F]
   //[dcomp=$00 dbinh=$0C][7804780400007804] (icount=0005, inc=4)
   di2t0_1 = ((pixsize & 0x04) && (zcomp & 0x02))
      || ((pixsize & 0x04) && (dcomp & 0x04) && (dcomp & 0x08) && dcompen);
   di2t4 = di2t0_1
      || (!(srcd & 0x04) && bcompen)
      || (!(pixsize & 0x04) && (dcomp & 0x04) && dcompen);
   *dbinh |= (!((di2t4 && phrase_mode) || winhibit) ? 0x04 : 0x00);
   //////////////////////////////////////////////////////////////////////////////////////

   /*Di3t0		:= NAN3 (di3t[0], pixsize\[2], dcomp[3], dcompen);
Di3t1		:= NAN2 (di3t[1], srcd\[3], bcompen);
Di3t2		:= NAN4 (di3t[2], di2t[0..1], di3t[0..1]);
Dbinh[3]	:= ANR1 (dbinh\[3], di3t[2], phrase_mode, winhibit);*/
   ////////////////////////////////////// C++ CODE //////////////////////////////////////
   di3t2 = di2t0_1
      || (!(srcd & 0x08) && bcompen)
      || (!(pixsize & 0x04) && (dcomp & 0x08) && dcompen);
   *dbinh |= (!((di3t2 && phrase_mode) || winhibit) ? 0x08 : 0x00);
   //////////////////////////////////////////////////////////////////////////////////////

   /*Di4t0		:= NAN2H (di4t[0], pixsize[2], zcomp[2]);
Di4t1		:= NAN4H (di4t[1], pixsize[2], dcomp[4..5], dcompen);
Di4t2		:= NAN2 (di4t[2], srcd\[4], bcompen);
Di4t3		:= NAN3 (di4t[3], pixsize\[2], dcomp[4], dcompen);
Di4t4		:= NAN4 (di4t[4], di4t[0..3]);
Dbinh[4]	:= NAN2 (dbinh\[4], di4t[4], phrase_mode);*/
   ////////////////////////////////////// C++ CODE //////////////////////////////////////
   di4t0_1 = ((pixsize & 0x04u) && (zcomp & 0x04u))
      || ((pixsize & 0x04u) && (dcomp & 0x10u) && (dcomp & 0x20u) && dcompen);
   di4t4 = di4t0_1
      || (!(srcd & 0x10u) && bcompen)
      || (!(pixsize & 0x04u) && (dcomp & 0x10u) && dcompen);
   *dbinh |= (!(di4t4 && phrase_mode) ? 0x10u : 0x00u);
   //////////////////////////////////////////////////////////////////////////////////////

   /*Di5t0		:= NAN3 (di5t[0], pixsize\[2], dcomp[5], dcompen);
Di5t1		:= NAN2 (di5t[1], srcd\[5], bcompen);
Di5t2		:= NAN4 (di5t[2], di4t[0..1], di5t[0..1]);
Dbinh[5]	:= NAN2 (dbinh\[5], di5t[2], phrase_mode);*/
   ////////////////////////////////////// C++ CODE //////////////////////////////////////
   di5t2 = di4t0_1
      || (!(srcd & 0x20) && bcompen)
      || (!(pixsize & 0x04) && (dcomp & 0x20) && dcompen);
   *dbinh |= (!(di5t2 && phrase_mode) ? 0x20 : 0x00);
   //////////////////////////////////////////////////////////////////////////////////////

   /*Di6t0		:= NAN2H (di6t[0], pixsize[2], zcomp[3]);
Di6t1		:= NAN4H (di6t[1], pixsize[2], dcomp[6..7], dcompen);
Di6t2		:= NAN2 (di6t[2], srcd\[6], bcompen);
Di6t3		:= NAN3 (di6t[3], pixsize\[2], dcomp[6], dcompen);
Di6t4		:= NAN4 (di6t[4], di6t[0..3]);
Dbinh[6]	:= NAN2 (dbinh\[6], di6t[4], phrase_mode);*/
   ////////////////////////////////////// C++ CODE //////////////////////////////////////
   di6t0_1 = ((pixsize & 0x04) && (zcomp & 0x08))
      || ((pixsize & 0x04) && (dcomp & 0x40) && (dcomp & 0x80) && dcompen);
   di6t4 = di6t0_1
      || (!(srcd & 0x40) && bcompen)
      || (!(pixsize & 0x04) && (dcomp & 0x40) && dcompen);
   *dbinh |= (!(di6t4 && phrase_mode) ? 0x40 : 0x00);
   //////////////////////////////////////////////////////////////////////////////////////

   /*Di7t0		:= NAN3 (di7t[0], pixsize\[2], dcomp[7], dcompen);
Di7t1		:= NAN2 (di7t[1], srcd\[7], bcompen);
Di7t2		:= NAN4 (di7t[2], di6t[0..1], di7t[0..1]);
Dbinh[7]	:= NAN2 (dbinh\[7], di7t[2], phrase_mode);*/
   ////////////////////////////////////// C++ CODE //////////////////////////////////////
   di7t2 = di6t0_1
      || (!(srcd & 0x80) && bcompen)
      || (!(pixsize & 0x04) && (dcomp & 0x80) && dcompen);
   *dbinh |= (!(di7t2 && phrase_mode) ? 0x80 : 0x00);
   //////////////////////////////////////////////////////////////////////////////////////

   //END;
   //kludge
   *dbinh = ~*dbinh;
}

#endif
