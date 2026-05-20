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
#include "blitter_internal.h"
#include "blitter_simd.h"

#include <string.h>
#include "jaguar.h"
#include "perf_counters.h"
#include "state.h"

// Various conditional compilation goodies...

#define USE_ORIGINAL_BLITTER
#define USE_MIDSUMMER_BLITTER_MKII

/* Portable always-inline.  Spelled to include the inline keyword
 * itself (MSVC's __forceinline IS the inline keyword for that
 * compiler), so call sites use `static BLITTER_ALWAYS_INLINE void
 * foo(...)` without an extra INLINE/inline.  Used to force inlining
 * of the blitter helpers (ADD16SAT, ADDARRAY, COMP_CTRL, DATA) so
 * the compiler can specialise them per call site. */
#if defined(_MSC_VER)
#  define BLITTER_ALWAYS_INLINE __forceinline
#elif defined(__GNUC__) || defined(__clang__)
#  define BLITTER_ALWAYS_INLINE inline __attribute__((always_inline))
#else
#  define BLITTER_ALWAYS_INLINE inline
#endif

/* Fast-path RAM helpers for the blitter inner loop.
 * ~98% of blitter memory accesses target main RAM (0x000000-0x1FFFFF).
 * These helpers inline the RAM check and byte-swap, avoiding the full
 * address dispatch in JaguarReadWord/JaguarWriteLong etc. for the common case. */

static BLITTER_ALWAYS_INLINE uint8_t blitter_read_byte(uint32_t addr)
{
   if (addr < 0x200000)
      return jaguarMainRAM[addr & 0x1FFFFF];
   return JaguarReadByte(addr, BLITTER);
}

static BLITTER_ALWAYS_INLINE uint16_t blitter_read_word(uint32_t addr)
{
   uint32_t a;
   if (addr < 0x200000)
   {
      a = addr & 0x1FFFFF;
      return ((uint16_t)jaguarMainRAM[a] << 8) | jaguarMainRAM[(a + 1) & 0x1FFFFF];
   }
   return JaguarReadWord(addr, BLITTER);
}

static BLITTER_ALWAYS_INLINE uint32_t blitter_read_long(uint32_t addr)
{
   uint32_t a;
   if (addr < 0x200000)
   {
      a = addr & 0x1FFFFF;
      return ((uint32_t)jaguarMainRAM[a] << 24)
           | ((uint32_t)jaguarMainRAM[(a + 1) & 0x1FFFFF] << 16)
           | ((uint32_t)jaguarMainRAM[(a + 2) & 0x1FFFFF] << 8)
           | (uint32_t)jaguarMainRAM[(a + 3) & 0x1FFFFF];
   }
   return JaguarReadLong(addr, BLITTER);
}

static BLITTER_ALWAYS_INLINE void blitter_write_byte(uint32_t addr, uint8_t data)
{
   if (addr < 0x200000)
   {
      jaguarMainRAM[addr & 0x1FFFFF] = data;
      return;
   }
   JaguarWriteByte(addr, data, BLITTER);
}

static BLITTER_ALWAYS_INLINE void blitter_write_word(uint32_t addr, uint16_t data)
{
   uint32_t a;
   if (addr < 0x200000)
   {
      a = addr & 0x1FFFFF;
      jaguarMainRAM[a]                   = (uint8_t)(data >> 8);
      jaguarMainRAM[(a + 1) & 0x1FFFFF] = (uint8_t)(data & 0xFF);
      return;
   }
   JaguarWriteWord(addr, data, BLITTER);
}

static BLITTER_ALWAYS_INLINE void blitter_write_long(uint32_t addr, uint32_t data)
{
   uint32_t a;
   if (addr < 0x200000)
   {
      a = addr & 0x1FFFFF;
      jaguarMainRAM[a]                   = (uint8_t)((data >> 24) & 0xFF);
      jaguarMainRAM[(a + 1) & 0x1FFFFF] = (uint8_t)((data >> 16) & 0xFF);
      jaguarMainRAM[(a + 2) & 0x1FFFFF] = (uint8_t)((data >> 8)  & 0xFF);
      jaguarMainRAM[(a + 3) & 0x1FFFFF] = (uint8_t)(data         & 0xFF);
      return;
   }
   JaguarWriteLong(addr, data, BLITTER);
}

// Local global variables

// Blitter register RAM (most of it is hidden from the user)

uint8_t blitter_ram[0x100];

// Other crapola

void BlitterMidsummer(uint32_t cmd);
void BlitterMidsummer2(void);

PERF_COUNTER(blitter_calls);
PERF_COUNTER(blitter_outer);
PERF_COUNTER(blitter_inner);
PERF_COUNTER(blitter_inner_io);
PERF_COUNTER(blitter_inner_idle);
PERF_COUNTER(blitter_phrase_reads);
PERF_COUNTER(blitter_phrase_writes);

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
               if (BCOMPEN)
               {
                  uint32_t pixShift = (~bitPos) & (bppSrc - 1);
                  srcdata = (srcdata >> pixShift) & 0x01;
                  bitPos++;
               }

               if (!CMPDST)
               {
                  if (srcdata == 0)
                     inhibit = 1;
               }
               else
               {
                  if (dstdata == READ_RDATA(PATTERNDATA, a2, REG(A2_FLAGS), a2_phrase_mode))
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
               WRITE_PIXEL(a2, REG(A2_FLAGS), writedata);

               if (DSTWRZ)
                  WRITE_ZDATA(a2, REG(A2_FLAGS), srczdata);
            }
         }

         // Update x and y (inner loop)
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

      if (a1_phrase_mode)			// v2
      {
         uint32_t pixelSize;
         // Bump the pointer to the next phrase boundary.
         uint32_t size = 64 / a1_psize;

         // Align source to destination phrase position.
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
         // Bump the pointer to the next phrase boundary.
         uint32_t size = 64 / a2_psize;

         // Align source to destination phrase position.
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

   // Pixel size derived from the phrase control bits.
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

// Here's attempt #2--taken from the Oberon chip specs!

#ifdef USE_MIDSUMMER_BLITTER_MKII

static void ADDRGEN(uint32_t *, uint32_t *, bool, bool,
	uint16_t, uint16_t, uint32_t, uint8_t, uint8_t, uint8_t, uint8_t,
	uint16_t, uint16_t, uint32_t, uint8_t, uint8_t, uint8_t, uint8_t,
	uint32_t, uint32_t);
static uint32_t addrgen_ya(uint16_t y, uint8_t width)
{
	uint32_t y12 = (uint32_t)(y & 0x0FFF);
	uint32_t ytm = (y12 << 2)
		+ ((width & 0x02) ? y12 << 1 : 0)
		+ ((width & 0x01) ? y12 : 0);
	return (ytm << (width >> 2)) >> 2;
}
/* ADD16SAT / ADDARRAY are defined inline below so the compiler can
 * specialise per call-site (most callers pass compile-time constants
 * for daddasel/daddbsel/daddmode and the sat/eightbit/hicinh flags).
 * Profile data on AvP gameplay shows ADDARRAY as the single largest
 * leaf in the entire emulator, called millions of times per frame. */
static void ADDAMUX(int16_t *adda_x, int16_t *adda_y, uint8_t addasel, int16_t a1_step_x, int16_t a1_step_y,
	int16_t a1_stepf_x, int16_t a1_stepf_y, int16_t a2_step_x, int16_t a2_step_y,
	int16_t a1_inc_x, int16_t a1_inc_y, int16_t a1_incf_x, int16_t a1_incf_y, uint8_t adda_xconst,
	bool adda_yconst, bool addareg, bool suba_x, bool suba_y);
static void ADDBMUX(int16_t *addb_x, int16_t *addb_y, uint8_t addbsel, int16_t a1_x, int16_t a1_y,
	int16_t a2_x, int16_t a2_y, int16_t a1_frac_x, int16_t a1_frac_y);
static void DATAMUX(int16_t *data_x, int16_t *data_y, uint32_t gpu_din, int16_t addq_x, int16_t addq_y, bool addqsel);
static void ADDRADD(int16_t *addq_x, int16_t *addq_y, bool a1fracldi,
	uint16_t adda_x, uint16_t adda_y, uint16_t addb_x, uint16_t addb_y, uint8_t modx, bool suba_x, bool suba_y);
/* DATA + COMP_CTRL are defined inline below (above BlitterMidsummer2)
 * so the compiler can specialise them per call.  Both are called
 * exclusively from the BlitterMidsummer2 inner loop. */


/* AvP-gameplay hot path: ADDARRAY at 1910 samples, ADD16SAT inlined
 * inside it.  Inlined here so the compiler can specialise the 4
 * call sites in BlitterMidsummer2 (compile-time daddasel/daddbsel/
 * daddmode -> dead switch arms eliminated) and the call inside DATA
 * (where the args are loop-invariant for the duration of a blit). */
static BLITTER_ALWAYS_INLINE
void ADD16SAT(uint16_t *r, uint8_t *co, uint16_t a, uint16_t b,
              uint8_t cin, bool sat, bool eightbit, bool hicinh)
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

static BLITTER_ALWAYS_INLINE
void ADDARRAY(uint16_t *addq, uint8_t daddasel, uint8_t daddbsel,
              uint8_t daddmode, uint64_t dstd, uint32_t iinc,
              uint8_t initcin[], uint64_t initinc, uint16_t initpix,
              uint32_t istep, uint64_t patd, uint64_t srcd,
              uint64_t srcz1, uint64_t srcz2, uint32_t zinc,
              uint32_t zstep)
{
   unsigned i;
   uint16_t adda[4];
   uint16_t addb[4];
   uint64_t adda_val;
   uint32_t initpix2;
   uint16_t word;
   uint8_t cinsel;
   static uint8_t co[4]; /* preserved between calls (hardware artifact) */
   uint8_t cin[4];
   bool eightbit;
   bool sat, hicinh;
   uint8_t bsel_idx;

   initpix2 = ((uint32_t)initpix << 16) | initpix;

   switch (daddasel)
   {
      case 0:  adda_val = dstd; break;
      case 1:  adda_val = ((uint64_t)initpix2 << 32) | initpix2; break;
      case 2:
      case 3:  adda_val = 0; break;
      case 4:  adda_val = srcd; break;
      case 5:  adda_val = patd; break;
      case 6:  adda_val = srcz1; break;
      default: adda_val = srcz2; break;
   }
   adda[0] = (uint16_t)adda_val;
   adda[1] = (uint16_t)(adda_val >> 16);
   adda[2] = (uint16_t)(adda_val >> 32);
   adda[3] = (uint16_t)(adda_val >> 48);

   if (!(daddbsel & 0x04))
   {
      if (daddbsel & 0x01)
      {
         addb[0] = (uint16_t)initinc;
         addb[1] = (uint16_t)(initinc >> 16);
         addb[2] = (uint16_t)(initinc >> 32);
         addb[3] = (uint16_t)(initinc >> 48);
      }
      else
      {
         addb[0] = (uint16_t)srcd;
         addb[1] = (uint16_t)(srcd >> 16);
         addb[2] = (uint16_t)(srcd >> 32);
         addb[3] = (uint16_t)(srcd >> 48);
      }
   }
   else
   {
      bsel_idx = ((daddbsel & 0x08) >> 1) | (daddbsel & 0x03);
      switch (bsel_idx)
      {
         case 0: word = iinc & 0xFFFF; break;
         case 1: word = iinc >> 16; break;
         case 2: word = zinc & 0xFFFF; break;
         case 3: word = zinc >> 16; break;
         case 4: word = istep & 0xFFFF; break;
         case 5: word = istep >> 16; break;
         case 6: word = zstep & 0xFFFF; break;
         default: word = zstep >> 16; break;
      }
      addb[0] = addb[1] = addb[2] = addb[3] = word;
   }

   cinsel = ((daddmode & 0x03) && !(daddmode & 0x04) ? 1 : 0);

   for (i = 0; i < 4; i++)
      cin[i] = initcin[i] | (co[i] & cinsel);

   eightbit = daddmode & 0x02;
   sat = daddmode & 0x03;
   hicinh = ((daddmode & 0x03) == 0x03);

   blitter_simd_ops.add16sat_x4(addq, co, adda, addb, cin, sat, eightbit, hicinh);
}

static BLITTER_ALWAYS_INLINE
void COMP_CTRL(uint8_t *dbinh, bool *nowrite,
	bool bcompen, bool big_pix, bool bkgwren, uint8_t dcomp, bool dcompen, uint8_t icount,
	uint8_t pixsize, bool phrase_mode, uint8_t srcd, uint8_t zcomp)
{
   /*
    * Branchless byte-parallel rewrite of the per-bit dbinh computation.
    * The eight dbinh bits follow a structured pattern (see ASIC net
    * comments below): four byte-pairs share a common 16-bit z+dcomp
    * term (t0_1), each bit adds bcomp and single-dcomp terms, then
    * the result is gated by phrase_mode / winhibit.
    *
    * Verified bit-exact against the original gate-level C for >500M
    * input combinations covering all pixsize/dcomp/srcd/zcomp values.
    *
    * ASIC gate references (preserved for hardware traceability):
    *   Bcompselt[0-2] := EO (bcompselt[0-2], icount[0-2], big_pix);
    *   Bcompbit       := MX8 (bcompbit, srcd[7..0], bcompselt[0..2]);
    *   Nowt[0..4], Nowrite -- pixel-mode write inhibit
    *   Winht, Winhibit     -- pipelined write inhibit
    *   Di0t[0..4]/Dbinh[0] := ANR1P -- byte inhibit with winhibit
    *   Di1t[0..2]/Dbinh[1] := ANR1
    *   ...through Di7t[0..2]/Dbinh[7] := NAN2
    */

   uint8_t bcompselt = (big_pix ? ~icount : icount) & 0x07;
   bool bcompbit = (srcd >> (7 - bcompselt)) & 0x01;
   bool winhibit;
   uint8_t pix16;         /* non-zero when pixsize bit 2 is set (16bpp) */
   uint8_t zspread;       /* zcomp[0..3] spread: each bit covers a byte-pair */
   uint8_t dcomp_pair;    /* dcomp adjacent-pair AND for 16bpp mode */
   uint8_t t0_1_spread;   /* shared 16bpp z+dcomp term, spread to byte pairs */
   uint8_t bcomp_term;    /* bit comparator: ~srcd where bcompen */
   uint8_t sdcomp_term;   /* single-byte dcomp: dcomp where !pix16 & dcompen */
   uint8_t inhibit_all;   /* combined per-byte inhibit before mode gating */
   uint8_t gated;          /* after phrase_mode / winhibit gating */

   /* nowrite and winhibit (pixel-mode write inhibit)
    *
    * Z-comparator lane in pixel-mode 16bpp: the fast blitter reads
    * source/dest Z via `REG(SRCZINT|DSTZ) & 0xFFFF`, which is bytes 2-3
    * of the 8-byte register (low 16 of the high 32 half).  In the
    * GET64-shift lane convention here, that is lane 2 -- so
    * `zcomp & 0x04` matches fast.  Previously accurate used `zcomp & 0x01`
    * (lane 0 = bytes 6-7), which produced visibly wrong z-inhibit
    * decisions in BSG sprite blits (cmd=09900F71 / 09800F41 families:
    * pixel-mode 16bpp DCOMPEN sprites with constant source Z).
    *
    * This is a match-fast pragmatic fix; the JTRM-pure behaviour would
    * select the lane based on the destination pixel's position within a
    * phrase, which neither path currently does.  See #189 for the full
    * divergence writeup. */
   winhibit = (bcompen && !bcompbit && !phrase_mode)
      || (dcompen && (dcomp & 0x01) && !phrase_mode && (pixsize == 3))
      || (dcompen && ((dcomp & 0x03) == 0x03) && !phrase_mode && (pixsize == 4))
      || ((zcomp & 0x04) && !phrase_mode && (pixsize == 4));
   *nowrite = winhibit && !bkgwren;

   /* 16-bit pixel mode flag */
   pix16 = pixsize & 0x04;

   /* Spread zcomp[0..3] to byte pairs: zcomp bit N -> dbinh bits 2N, 2N+1 */
   zspread = (uint8_t)(
      ((zcomp & 0x01) ? 0x03 : 0x00) |
      ((zcomp & 0x02) ? 0x0C : 0x00) |
      ((zcomp & 0x04) ? 0x30 : 0x00) |
      ((zcomp & 0x08) ? 0xC0 : 0x00));

   /* dcomp adjacent-pair AND: for 16bpp, both bytes in a 16-bit pixel
    * must match.  Spread result to both bits of each pair. */
   dcomp_pair = (uint8_t)(
      (((dcomp & 0x01) && (dcomp & 0x02)) ? 0x03 : 0x00) |
      (((dcomp & 0x04) && (dcomp & 0x08)) ? 0x0C : 0x00) |
      (((dcomp & 0x10) && (dcomp & 0x20)) ? 0x30 : 0x00) |
      (((dcomp & 0x40) && (dcomp & 0x80)) ? 0xC0 : 0x00));

   /* t0_1: shared 16bpp term = (pix16 & zspread) | (pix16 & dcomp_pair & dcompen) */
   t0_1_spread = 0;
   if (pix16)
      t0_1_spread = zspread | (dcompen ? dcomp_pair : 0);

   /* Bit comparator: inhibit where source bit is 0 and bcompen active */
   bcomp_term = bcompen ? (uint8_t)(~srcd) : 0;

   /* Single-byte dcomp: for 8bpp (!pix16), each dcomp bit inhibits directly */
   sdcomp_term = (!pix16 && dcompen) ? dcomp : 0;

   /* Combined per-byte inhibit */
   inhibit_all = t0_1_spread | bcomp_term | sdcomp_term;

   /* Gate by phrase_mode and winhibit:
    * Bits 0-3 (ANR1P): inhibit = (term & phrase_mode) | winhibit
    * Bits 4-7 (NAN2):  inhibit = term & phrase_mode
    * Output is active-high inhibit (matching the ~dbinh inversion). */
   if (phrase_mode)
   {
      gated = inhibit_all;
      if (winhibit)
         gated |= 0x0F;
   }
   else
   {
      gated = 0;
      if (winhibit)
         gated |= 0x0F;
   }

   *dbinh = gated;
}

static BLITTER_ALWAYS_INLINE
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
	uint64_t lfu = blitter_simd_ops.lfu(srcd, dstd, lfu_func);
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
	uint8_t dbinht;
   uint16_t addq[4];
   uint8_t initcin[4] = { 0, 0, 0, 0 };
   uint16_t mask;
   uint64_t dmux[4];
   uint64_t ddat;
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
	*dcomp = blitter_simd_ops.dcomp(*patd, srcd, dstd, cmpdst);

	/* Source-pixel transparency for DCOMPEN+!CMPDST.
	 *
	 * The fast blitter's DCOMPEN+!CMPDST path inhibits writes whenever
	 * the source pixel is zero (blitter.c:497-500, "if (srcdata == 0)
	 * inhibit = 1").  JTRM calls DCOMPEN "pixel-level transparency",
	 * which matches that behaviour -- the transparent colour is
	 * hardcoded to zero, regardless of PATD.
	 *
	 * The gate-level rewrite of dcomp above (scalar_dcomp & SIMD
	 * variants) only checks byte-equality against PATD, which is the
	 * documented DATACOMP module behaviour but is missing the pixel
	 * transparency check the fast path performs.  For sprite blits
	 * with non-zero PATD and zero source pixels (BSG cmd=0x49802609
	 * family, ~120 blits / 3.76% residual divergence on develop),
	 * accurate writes zeros over the existing destination while fast
	 * preserves it via the source-zero inhibit.
	 *
	 * OR the per-byte "source byte == 0" mask into dcomp so the
	 * existing dcomp_pair (16bpp pair AND) and pixel-mode winhibit
	 * paths in COMP_CTRL fire when the source pixel is fully zero.
	 * Augment-only (no replacement) so cases where the original
	 * byte-equality already matched (PATD-based pattern stamping)
	 * continue to inhibit identically. */
	/* Gated to 8bpp (pixsize==3) and 16bpp (pixsize==4) only.  In those
	 * modes the per-byte zero mask matches fast's per-pixel zero check:
	 * 8bpp = one byte per pixel (1:1); 16bpp is reconciled by the
	 * dcomp_pair adjacent-byte AND inside COMP_CTRL.
	 *
	 * For 32bpp (pixsize==5) COMP_CTRL has no 4-byte AND, so OR'ing the
	 * per-byte mask here would inhibit a 32-bit pixel whenever any one
	 * of its bytes is zero, which differs from fast's "full 32-bit
	 * srcdata == 0" check.  No failing testcase currently exercises
	 * 32bpp DCOMPEN; gating out keeps semantics conservative and
	 * matches fast for the modes BSG and other failing titles use. */
	if (dcompen && !cmpdst && (pixsize == 3 || pixsize == 4))
	{
		/* Per-byte "byte is 0" mask.  Compact loop over the 8 bytes of
		 * srcd.  (A SWAR byte-zero bit-trick would be tempting but
		 * `(s - 0x01...01) & ~s & 0x80...80` fails on cross-byte
		 * borrow -- e.g. s=0x0001_0000 spuriously flags byte 2 as zero
		 * because the borrow chain propagates the wrong way.  The loop
		 * is honest and compiles to predictable code on every target.) */
		uint64_t s = srcd;
		uint8_t zero_mask = 0;
		unsigned i;
		for (i = 0; i < 8; i++)
			if (((s >> (i * 8)) & 0xFFu) == 0)
				zero_mask |= (uint8_t)(1u << i);
		*dcomp |= zero_mask;
	}
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
	*zcomp = blitter_simd_ops.zcomp(*srcz, dstz, zmode);

//TEMP, TO TEST IF ZCOMP IS THE CULPRIT...
//Nope, this is NOT the problem...
//zcomp=0;
// We'll do the comparison/bit/byte inhibits here, since that's they way it happens
// in the real thing (dcomp goes out to COMP_CTRL and back into DATA through dbinh)...
	{
	uint8_t bcomp_bits;
	if (bcompen && phrase_mode)
	{
		bcomp_bits = (srcd >> 56) & 0xFF;
	}
	else
		bcomp_bits = srcd & 0xFF;

	COMP_CTRL(&dbinht, nowrite,
		bcompen, true/*big_pix*/, bkgwren, *dcomp, dcompen, icount, pixsize, phrase_mode, bcomp_bits, *zcomp);
	}
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
	{
	uint64_t patd_pre = *patd;
	ADDARRAY(addq, daddasel, daddbsel, daddmode, dstd, iinc, initcin, 0, 0, 0, *patd, srcd, 0, 0, 0, 0);

	if (patdadd)
		*patd = ((uint64_t)addq[3] << 48) | ((uint64_t)addq[2] << 32) | ((uint64_t)addq[1] << 16) | (uint64_t)addq[0];
//////////////////////////////////////////////////////////////////////////////////////

// Local data bus multiplexer
// In hardware, the write data mux reads patd BEFORE the register update.
// patd_pre captures the pre-increment value for the data output mux.

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
	/* Parallel prefix (Kogge-Stone) replaces the 15-step serial
	 * OAN1P ripple carry with O(log n) branchless shift-and-combine steps.
	 *
	 * The carry chain is: maskt[n] = (maskt[n-1] | s[n]) & e[n]
	 * which is a generate-propagate network:
	 *   generate  g[n] = s[n] & e[n]   (bit starts a new run)
	 *   propagate p[n] = e[n]           (bit allows carry through)
	 *   carry     c[n] = g[n] | (p[n] & c[n-1])
	 * Bit 0 is special: g[0] = s_fine[0], p[0] = 1 (no gate).
	 *
	 * Verified bit-exact for all 4096 dstart/dend combinations. */
	{
		uint16_t fg, fp, cg, cp;

		/* Fine section (bits 0-7) */
		fg = (uint16_t)(s_fine & e_fine) | (uint16_t)(s_fine & 0x01u);
		fp = (uint16_t)e_fine | 0x01u;

		fg  |= fp & (fg << 1);
		fp  &= (fp << 1);
		fg  |= fp & (fg << 2);
		fp  &= (fp << 2);
		fg  |= fp & (fg << 4);
		maskt = fg & 0x00FFu;

		/* Coarse section (bits 8-14): same pattern,
		 * seed = s_coarse & e_coarse, propagate = e_coarse */
		cg = (uint16_t)(s_coarse & e_coarse);
		cp = (uint16_t)e_coarse;

		cg  |= cp & (cg << 1);
		cp  &= (cp << 1);
		cg  |= cp & (cg << 2);
		cp  &= (cp << 2);
		cg  |= cp & (cg << 4);
		maskt |= (cg & 0x00FEu) << 7;
	}

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
		/* MX4 input 2: masku[7:0] = {8{maskt[14]}} (broadcast bit 14) */
		masku = (maskt & 0x4000) ? 0x00FF : 0x0000;
		/* MX2: reverse bits 8-13, maskt[0] at position 14 */
		masku |= (maskt >> 5) & 0x0100;
		masku |= (maskt >> 3) & 0x0200;
		masku |= (maskt >> 1) & 0x0400;
		masku |= (maskt << 1) & 0x0800;
		masku |= (maskt << 3) & 0x1000;
		masku |= (maskt << 5) & 0x2000;
		masku |= (maskt & 0x0001) << 14;
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
	dmux[0] = patd_pre;
	dmux[1] = lfu;
	dmux[2] = ((uint64_t)addq[3] << 48) | ((uint64_t)addq[2] << 32) | ((uint64_t)addq[1] << 16) | (uint64_t)addq[0];
	dmux[3] = 0;
	ddat = dmux[data_sel];
	}
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
	*wdata = blitter_simd_ops.byte_merge(ddat, dstd, mask);
	*srcz = blitter_simd_ops.byte_merge(*srcz, dstz, mask);
//////////////////////////////////////////////////////////////////////////////////////

/*Data_enab[0-1]	:= BUF8 (data_enab[0-1], data_ena);
Datadrv[0-31]	:= TS (wdata[0-31],  dat[0-31],  data_enab[0]);
Datadrv[32-63]	:= TS (wdata[32-63], dat[32-63], data_enab[1]);

Unused[0]	:= DUMMY (unused[0]);

END;*/
}

#ifdef BLITTER_TRACE
#include <mach/mach_time.h>
#include <stdio.h>
static double bm2_trace_threshold_ms = 0.3; /* dump any blit slower than this */
static uint64_t bm2_trace_t0;
#endif

void BlitterMidsummer2(void)
{
   uint32_t cmd = (PERF_INC(blitter_calls), GET32(blitter_ram, COMMAND));
#ifdef BLITTER_TRACE
   bm2_trace_t0 = mach_absolute_time();
#endif


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
      PERF_INC(blitter_outer);
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
         bool idle_inner = true, sreadx = false, szreadx = false, sread = false,
              szread = false, dread = false, dzread = false, dwrite = false, dzwrite = false;
         bool inner0 = false;
         bool idle_inneri, sreadxi, szreadxi, sreadi, szreadi, dreadi, dzreadi, dwritei, dzwritei;
         //other stuff
         uint8_t srcshift = 0;
         uint16_t icount = GET16(blitter_ram, PIXLINECOUNTER + 2);
         bool srca_addi, dsta_addi, gensrc, gendst, gena2i, zaddr, fontread, justify, a1_add, a2_add;
         bool adda_yconst, addareg, suba_x, suba_y, a1fracldi, shadeadd;
         uint8_t addasel, a1_xconst, a2_xconst, adda_xconst, addbsel, maska1, maska2, modx, daddasel;
         uint8_t daddbsel, daddmode;
         bool patfadd, patdadd, srcz2add, daddq_sel;
         uint8_t data_sel;
         uint32_t address, pixAddr;
         uint8_t dstxp;
         uint64_t srcz = 0;
         bool winhibit;
         uint32_t a1_ya_cached, a2_ya_cached;
         /* CLIP_A1 must check the a1 position THAT WAS USED for the
          * sread that loaded srcd, not the post-step a1.  The state
          * machine steps a1 (via srca_addi) during the same cycle as
          * sread but BEFORE the dwrite that consumes srcd, so by the
          * time we evaluate the clip check at dwrite, a1 is one step
          * ahead.  Cache a1_x/a1_y at sread time and use the cached
          * values for the clip test below.  Matches fast, which checks
          * CLIP_A1 against pre-step a1.  Fixes BSG cmd=0x09800F41
          * family per-pixel divergence at row-edge positions where the
          * fractional source X drifts across 0 mid-row. */
         int16_t a1_x_at_sread = a1_x;
         int16_t a1_y_at_sread = a1_y;

         indone = false;

         /* Precompute y*width row offsets (invariant when y unchanged) */
         a1_ya_cached = addrgen_ya((uint16_t)a1_y, a1_width);
         a2_ya_cached = addrgen_ya((uint16_t)a2_y, a2_width);

         /* Precompute address constants (invariant during inner loop) */
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

         /* Precompute srcshift — loaded on first inner cycle (sshftld),
            then held constant for all subsequent cycles. */
         {
            uint8_t dstxp0, srcxp0, shftv0, pobb0, loshd0;
            bool pobbsel0;

            dstxp0 = (dsta2 ? a2_x : a1_x) & 0x3F;
            srcxp0 = (dsta2 ? a1_x : a2_x) & 0x3F;
            shftv0 = ((dstxp0 - srcxp0) << pixsize) & 0x3F;
            pobb0 = 0;
            if (pixsize == 3)
               pobb0 = dstxp0 & 0x07;
            else if (pixsize == 4)
               pobb0 = dstxp0 & 0x03;
            else if (pixsize == 5)
               pobb0 = dstxp0 & 0x01;

            pobbsel0 = phrase_mode && bcompen;
            loshd0 = (pobbsel0 ? pobb0 : shftv0) & 0x07;
            srcshift = (srcen || pobbsel0 ? loshd0 : 0);
            srcshift |= (srcen && phrase_mode ? shftv0 & 0x38 : 0);
         }

         /*=================================================================
          * COLLAPSED INNER LOOP — PATTERN FILL
          *
          * Conditions: PATDSEL set, SRCEN/SRCENX/DSTEN/DSTENZ/DSTWRZ off,
          * no GOURD/GOURZ/SRCSHADE (pure pattern fill only).
          *
          * The hardware state machine takes 2 cycles per pixel for this
          * config (idle_inner -> dwrite).  This collapsed path does exactly
          * the same work in 1 iteration: ADDRGEN, dstart/dend masks, DATA
          * (patd passthrough), write, address step, icount decrement.
          *
          * The existing state machine is the fallback for all other configs.
          *=================================================================*/
         if (patdsel && !srcen && !srcenx && !dsten && !dstenz && !dstwrz
               && !gourd && !gourz && !srcshade && !adddsel
               && !bcompen && !dcompen && !a2update && zmode == 0)
         {
            /* Collapsed-path local variables (C89: all at top of block) */
            bool pf_a1_add, pf_a2_add;
            bool pf_gena2i;
            bool pf_justify;
            uint8_t pf_addasel, pf_adda_xconst, pf_addbsel, pf_modx;
            bool pf_adda_yconst, pf_addareg, pf_suba_x, pf_suba_y, pf_a1fracldi;
            uint8_t pf_maska1, pf_maska2;

            /* For pattern fill dwrite: dsta_addi=true, srca_addi=false.
               Destination pointer (A1 if !dsta2, A2 if dsta2) steps. */
            pf_a1_add = !dsta2;
            pf_a2_add = dsta2;
            pf_gena2i = dsta2;

            /* justify = !(!fontread && phrase_mode).
               fontread = (sread||sreadx) && bcompen; eligibility excludes
               bcompen/srcen/srcenx so fontread is always false here. */
            pf_justify = !phrase_mode;

            /* Precompute address-adder decode (invariant for entire inner loop).
               These mirror the decode at lines 2131-2212 for the dwrite state. */
            pf_addasel = (pf_a1_add && a1addx == 3 ? 0x03 : 0x00);
            pf_addasel |= (a2update ? 0x04 : 0x00);
            pf_adda_xconst = (pf_a2_add ? a2_xconst : a1_xconst);
            pf_adda_yconst = (pf_a2_add ? a2addy : a1addy);
            pf_addareg = ((pf_a1_add && a1addx == 3) || (pf_a2_add && a2addx == 3)
                  ? true : false);
            pf_suba_x = ((pf_a1_add && a1xsign && a1addx == 1)
                  || (pf_a2_add && a2xsign && a2addx == 1) ? true : false);
            pf_suba_y = ((pf_a1_add && a1addy && a1ysign)
                  || (pf_a2_add && a2addy && a2ysign) ? true : false);
            pf_addbsel = (pf_a2_add ? 0x01 : 0x00);
            pf_addbsel |= (pf_a1_add && a1addx == 3 ? 0x02 : 0x00);
            pf_maska1 = (pf_a1_add && a1addx == 0 ? 6 - a1_pixsize : 0);
            pf_maska2 = (pf_a2_add && a2addx == 0 ? 6 - a2_pixsize : 0);
            pf_modx = (pf_a2_add ? pf_maska2 : pf_maska1);
            pf_a1fracldi = (pf_a1_add && a1addx == 3);

            while (true)
            {
               /* Per-pixel locals (C89: all at top) */
               uint8_t pf_inc, pf_dstart, pf_ppp;
               uint16_t pf_oldicount, pf_dstxwr, pf_pseq;
               bool pf_penden;
               uint8_t pf_window_mask, pf_inner_mask, pf_emask, pf_dend;
               uint8_t pf_pma;
               uint64_t pf_wdata;
               uint8_t pf_dcomp, pf_zcomp;
               bool pf_winhibit;
               uint64_t pf_dstd_local;

               PERF_INC(blitter_inner);

               /* ---- ADDRGEN for destination ---- */
               ADDRGEN(&address, &pixAddr, pf_gena2i, false/*zaddr*/,
                     a1_x, a1_y, a1_base, a1_pitch, a1_pixsize, a1_width, a1_zoffset,
                     a2_x, a2_y, a2_base, a2_pitch, a2_pixsize, a2_width, a2_zoffset,
                     a1_ya_cached, a2_ya_cached);

               /* Phrase-align address: matches state machine's `if (!justify)`.
                  For patfill (fontread=false), !justify == phrase_mode. */
               if (!pf_justify)
                  address &= 0xFFFFF8;

               dstxp = (dsta2 ? a2_x : a1_x) & 0x3F;

               /* ---- icount decrement (same as dwrite state) ---- */
               pf_ppp = 64 >> pixsize;
               if (phrase_mode)
                  pf_inc = pf_ppp - ((dsta2 ? a2_x : a1_x) & (pf_ppp - 1));
               else
                  pf_inc = 1;

               pf_oldicount = icount;
               icount -= pf_inc;

               if (icount == 0 || ((icount & 0x8000) && !(pf_oldicount & 0x8000)))
                  inner0 = true;

               /* ---- dstart/dend mask computation ---- */
               if (phrase_mode)
                  pf_dstart = (dstxp & (pf_ppp - 1)) << pixsize;
               else
                  pf_dstart = pixAddr & 0x07;

               pf_dstxwr = (dsta2 ? a2_x : a1_x) & 0x7FFE;
               pf_pseq = pf_dstxwr ^ (a1_win_x & 0x7FFE);
               pf_pseq = (pixsize == 5 ? pf_pseq : pf_pseq & 0x7FFC);
               pf_pseq = ((pixsize & 0x06) == 4 ? pf_pseq : pf_pseq & 0x7FF8);
               pf_penden = clip_a1 && (pf_pseq == 0);

               if (pf_penden)
                  pf_window_mask = (a1_win_x & (pf_ppp - 1)) << pixsize;
               else
                  pf_window_mask = 0;

               if (inner0)
                  pf_inner_mask = (icount & (pf_ppp - 1)) << pixsize;
               else
                  pf_inner_mask = 0;

               pf_window_mask = (pf_window_mask == 0 ? 0x40 : pf_window_mask);
               pf_inner_mask = (pf_inner_mask == 0 ? 0x40 : pf_inner_mask);
               pf_emask = (pf_window_mask > pf_inner_mask ? pf_inner_mask : pf_window_mask);
               pf_pma = pixAddr + (1 << pixsize);
               pf_dend = (phrase_mode ? pf_emask : pf_pma);

               /* Implicit dest read for phrase-mode byte merging.
                * When bkgwren is set, use the DSTDATA register value (dstd) as the
                * background — matching the state machine, where !dsten skips dread
                * and dstd retains its register-init value. */
               if (bkgwren)
                  pf_dstd_local = dstd;
               else if (phrase_mode)
                  pf_dstd_local = ((uint64_t)blitter_read_long(address) << 32)
                     | (uint64_t)blitter_read_long(address + 4);
               else if (pixsize < 3)
                  pf_dstd_local = (uint64_t)blitter_read_byte(address);
               else
                  pf_dstd_local = 0;

               /* ---- DATA: patd passthrough with masking ---- */
               /* For pure patfill (!gourd,!gourz,!srcshade): daddasel=0,
                  daddbsel=0, patdadd=false, daddq_sel=false, data_sel=0.
                  daddmode: bit0=1 (!gourd&&!gourz), bit1=!topben,
                  bit2=1 (!gourd&&!gourz).  patdadd=false so ADDARRAY
                  result is discarded — daddmode value is functionally
                  irrelevant, but we compute it identically. */
               DATA(&pf_wdata, &pf_dcomp, &pf_zcomp, &pf_winhibit,
                     true/*big_pix*/, cmpdst,
                     0/*daddasel*/, 0/*daddbsel*/,
                     (uint8_t)(0x05 | (topben ? 0x00 : 0x02))/*daddmode*/,
                     false/*daddq_sel*/,
                     0/*data_sel*/, 0/*dbinh*/, pf_dend, pf_dstart, pf_dstd_local,
                     iinc, lfufunc, &patd, false/*patdadd*/,
                     phrase_mode, 0/*srcd*/, false/*srcdread*/, false/*srczread*/,
                     false/*srcz2add*/, zmode,
                     bcompen, bkgwren, dcompen, icount & 0x07, pixsize,
                     &srcz, dstz, zinc);

               /* ---- Window clipping (CLIP_A1) ---- */
               if (clip_a1 && ((a1_x & 0x8000) || (a1_y & 0x8000)
                     || (a1_x >= a1_win_x) || (a1_y >= a1_win_y)))
                  pf_winhibit = true;

               /* ---- Write pixel/phrase ---- */
               PERF_INC(blitter_phrase_writes);
               if (!pf_winhibit || bkgwren)
               {
                  if (phrase_mode)
                  {
                     blitter_write_long(address + 0, pf_wdata >> 32);
                     blitter_write_long(address + 4, pf_wdata & 0xFFFFFFFF);
                  }
                  else
                  {
                     if (pixsize == 5)
                        blitter_write_long(address, pf_wdata & 0xFFFFFFFF);
                     else if (pixsize == 4)
                        blitter_write_word(address, pf_wdata & 0x0000FFFF);
                     else
                        blitter_write_byte(address, pf_wdata & 0x000000FF);
                  }
               }

               /* ---- Address stepping (same ADDAMUX/ADDBMUX/ADDRADD chain) ---- */
               if (pf_a1_add)
               {
                  int16_t adda_x, adda_y, addb_x, addb_y, addq_x, addq_y;
                  ADDAMUX(&adda_x, &adda_y, pf_addasel,
                        a1_step_x, a1_step_y, a1_stepf_x, a1_stepf_y,
                        a2_step_x, a2_step_y, a1_inc_x, a1_inc_y,
                        a1_incf_x, a1_incf_y, pf_adda_xconst,
                        pf_adda_yconst, pf_addareg, pf_suba_x, pf_suba_y);
                  ADDBMUX(&addb_x, &addb_y, pf_addbsel,
                        a1_x, a1_y, a2_x, a2_y, a1_frac_x, a1_frac_y);
                  ADDRADD(&addq_x, &addq_y, pf_a1fracldi,
                        adda_x, adda_y, addb_x, addb_y,
                        pf_modx, pf_suba_x, pf_suba_y);

                  if (a1addx == 3)
                  {
                     a1_frac_x = addq_x;
                     a1_frac_y = addq_y;
                     ADDAMUX(&adda_x, &adda_y, 2/*addasel*/,
                           a1_step_x, a1_step_y, a1_stepf_x, a1_stepf_y,
                           a2_step_x, a2_step_y, a1_inc_x, a1_inc_y,
                           a1_incf_x, a1_incf_y, pf_adda_xconst,
                           pf_adda_yconst, pf_addareg, pf_suba_x, pf_suba_y);
                     ADDBMUX(&addb_x, &addb_y, 0/*addbsel*/,
                           a1_x, a1_y, a2_x, a2_y, a1_frac_x, a1_frac_y);
                     ADDRADD(&addq_x, &addq_y, false/*a1fracldi*/,
                           adda_x, adda_y, addb_x, addb_y,
                           pf_modx, pf_suba_x, pf_suba_y);
                     a1_x = addq_x;
                     if (addq_y != a1_y)
                     {
                        a1_y = addq_y;
                        a1_ya_cached = addrgen_ya((uint16_t)a1_y, a1_width);
                     }
                  }
                  else
                  {
                     a1_x = addq_x;
                     if (addq_y != a1_y)
                     {
                        a1_y = addq_y;
                        a1_ya_cached = addrgen_ya((uint16_t)a1_y, a1_width);
                     }
                  }
               }

               if (pf_a2_add)
               {
                  int16_t adda_x, adda_y, addb_x, addb_y, addq_x, addq_y;
                  ADDAMUX(&adda_x, &adda_y, pf_addasel,
                        a1_step_x, a1_step_y, a1_stepf_x, a1_stepf_y,
                        a2_step_x, a2_step_y, a1_inc_x, a1_inc_y,
                        a1_incf_x, a1_incf_y, pf_adda_xconst,
                        pf_adda_yconst, pf_addareg, pf_suba_x, pf_suba_y);
                  ADDBMUX(&addb_x, &addb_y, pf_addbsel,
                        a1_x, a1_y, a2_x, a2_y, a1_frac_x, a1_frac_y);
                  ADDRADD(&addq_x, &addq_y, pf_a1fracldi,
                        adda_x, adda_y, addb_x, addb_y,
                        pf_modx, pf_suba_x, pf_suba_y);
                  a2_x = addq_x;
                  if (addq_y != a2_y)
                  {
                     a2_y = addq_y;
                     a2_ya_cached = addrgen_ya((uint16_t)a2_y, a2_width);
                  }
               }

               /* ---- Check if inner loop is done ---- */
               if (inner0)
                  break;
            }

            /* Skip the state machine — collapsed path handled everything */
            goto patfill_inner_done;
         }

         /*=================================================================
          * FAST PATH: Collapsed inner loop for simple copy blits.
          *
          * Eligibility: SRCEN set, no SRCENX/SRCENZ/DSTEN/DSTENZ/DSTWRZ,
          * no BCOMPEN/DCOMPEN/GOURD/GOURZ/SRCSHADE/ADDDSEL/PATDSEL.
          * This covers the idle_inner -> sread -> dwrite state chain,
          * collapsing 3 state-machine iterations per pixel into 1.
          *
          * The collapsed loop performs identical work to the state machine:
          *   1. ADDRGEN for source, JaguarReadLong (sread)
          *   2. Source shift/alignment (srcd1/srcd2 pipeline)
          *   3. ADDRGEN for destination, compute dstart/dend masks
          *   4. DATA function (LFU + byte merge)
          *   5. CLIP_A1 window check
          *   6. Write pixel/phrase
          *   7. Step both A1 and A2 addresses
          *   8. Decrement icount, check inner0
          *=================================================================*/
         if (srcen && !srcenx && !srcenz && !dsten && !dstenz && !dstwrz
               && !bcompen && !dcompen && !gourd && !gourz && !srcshade
               && !adddsel && !patdsel && zmode == 0
               && a1addx != 3 && a2addx != 3)
         {
            /* Pre-decode values that are invariant across the inner loop.
             * For a simple copy with no fractional increment:
             *   - source pointer is A2 (if !dsta2) or A1 (if dsta2)
             *   - dest pointer is A1 (if !dsta2) or A2 (if dsta2)
             *   - data_sel = 1 (LFU, since !patdsel && !adddsel)
             *   - daddasel/daddbsel/daddmode are constant (no gourd/srcshade)
             *   - patfadd = false, patdadd = false
             */
            uint8_t fc_ppp = 64 >> pixsize;
            /* Source stepping decode: when stepping the source ptr,
             * a1_add/a2_add depend on dsta2, and the ADDAMUX/ADDBMUX
             * selects depend on which channel is being stepped.
             *
             * For the source step (!dsta2 => a2_add, dsta2 => a1_add):
             *   addasel = 0, addbsel = (!dsta2 ? 1 : 0)
             *   adda_xconst = source channel's xconst
             *   modx = source channel's mask
             *   addareg/a1fracldi = false (no XADDINC for simple copy)
             *   suba_x/suba_y from source channel's sign bits
             *
             * For the dest step (!dsta2 => a1_add, dsta2 => a2_add):
             *   addasel = 0, addbsel = (!dsta2 ? 0 : 1)
             *   adda_xconst = dest channel's xconst
             *   modx = dest channel's mask
             *   suba_x/suba_y from dest channel's sign bits
             */

            /* Source channel stepping parameters */
            uint8_t fc_src_xconst = (dsta2 ? a1_xconst : a2_xconst);
            uint8_t fc_src_addbsel = (dsta2 ? 0x00 : 0x01);
            uint8_t fc_src_modx = 0;
            bool fc_src_suba_x, fc_src_suba_y;
            /* Dest channel stepping parameters */
            uint8_t fc_dst_xconst = (dsta2 ? a2_xconst : a1_xconst);
            uint8_t fc_dst_addbsel = (dsta2 ? 0x01 : 0x00);
            uint8_t fc_dst_modx = 0;
            bool fc_dst_suba_x, fc_dst_suba_y;
            bool fc_inner0 = false;

            if (dsta2)
            {
               /* Source is A1, dest is A2 */
               fc_src_suba_x = (a1xsign && a1addx == 1);
               fc_src_suba_y = (a1addy && a1ysign);
               fc_dst_suba_x = (a2xsign && a2addx == 1);
               fc_dst_suba_y = (a2addy && a2ysign);
               if (a1addx == 0)
                  fc_src_modx = 6 - a1_pixsize;
               if (a2addx == 0)
                  fc_dst_modx = 6 - a2_pixsize;
            }
            else
            {
               /* Source is A2, dest is A1 */
               fc_src_suba_x = (a2xsign && a2addx == 1);
               fc_src_suba_y = (a2addy && a2ysign);
               fc_dst_suba_x = (a1xsign && a1addx == 1);
               fc_dst_suba_y = (a1addy && a1ysign);
               if (a2addx == 0)
                  fc_src_modx = 6 - a2_pixsize;
               if (a1addx == 0)
                  fc_dst_modx = 6 - a1_pixsize;
            }

            while (!fc_inner0)
            {
               /* --- sread: read source data --- */
               uint32_t fc_src_addr;
               uint32_t fc_dst_addr, fc_dst_pixa;
               uint64_t fc_srcd;
               uint8_t fc_dstxp, fc_dstart;
               uint8_t fc_inc;
               uint16_t fc_oldicount;
               uint16_t fc_dstxwr, fc_pseq;
               bool fc_penden;
               uint8_t fc_window_mask, fc_inner_mask, fc_emask, fc_pma, fc_dend;
               uint64_t fc_wdata;
               uint8_t fc_dcomp_val, fc_zcomp_val;
               bool fc_winhibit;
               int16_t fc_adda_x, fc_adda_y, fc_addb_x, fc_addb_y, fc_addq_x, fc_addq_y;

               PERF_INC(blitter_inner);

               /* Generate source address (gena2i = !dsta2 for source read) */
               ADDRGEN(&fc_src_addr, &fc_dst_pixa, !dsta2, false/*zaddr*/,
                     a1_x, a1_y, a1_base, a1_pitch, a1_pixsize, a1_width, a1_zoffset,
                     a2_x, a2_y, a2_base, a2_pitch, a2_pixsize, a2_width, a2_zoffset,
                     a1_ya_cached, a2_ya_cached);
               if (phrase_mode)
                  fc_src_addr &= 0xFFFFF8;

               /* Source data pipeline: srcd2 = previous, srcd1 = new read */
               srcd2 = srcd1;
               srcd1 = ((uint64_t)blitter_read_long(fc_src_addr) << 32)
                  | (uint64_t)blitter_read_long(fc_src_addr + 4);
               PERF_INC(blitter_phrase_reads);

               /* Pixel mode: shift source to correct position */
               if (!phrase_mode)
               {
                  if (pixsize == 5)
                     srcd1 >>= 32;
                  else if (pixsize == 4)
                     srcd1 >>= 48;
                  else
                     srcd1 >>= 56;
               }

               /* --- dwrite: compute and write destination --- */

               /* Counter update (done first as in state machine) */
               if (phrase_mode)
                  fc_inc = fc_ppp - ((dsta2 ? a2_x : a1_x) & (fc_ppp - 1));
               else
                  fc_inc = 1;

               fc_oldicount = icount;
               icount -= fc_inc;

               if (icount == 0 || ((icount & 0x8000) && !(fc_oldicount & 0x8000)))
                  fc_inner0 = true;

               /* Generate destination address (gena2i = dsta2 for dest write) */
               ADDRGEN(&fc_dst_addr, &fc_dst_pixa, dsta2, false/*zaddr*/,
                     a1_x, a1_y, a1_base, a1_pitch, a1_pixsize, a1_width, a1_zoffset,
                     a2_x, a2_y, a2_base, a2_pitch, a2_pixsize, a2_width, a2_zoffset,
                     a1_ya_cached, a2_ya_cached);
               if (phrase_mode)
                  fc_dst_addr &= 0xFFFFF8;

               fc_dstxp = (dsta2 ? a2_x : a1_x) & 0x3F;

               /* Start/end mask computation */
               fc_dstart = 0;
               if (phrase_mode)
                  fc_dstart = (fc_dstxp & (fc_ppp - 1)) << pixsize;
               else
                  fc_dstart = fc_dst_pixa & 0x07;

               fc_dstxwr = (dsta2 ? a2_x : a1_x) & 0x7FFE;
               fc_pseq = fc_dstxwr ^ (a1_win_x & 0x7FFE);
               fc_pseq = (pixsize == 5 ? fc_pseq : fc_pseq & 0x7FFC);
               fc_pseq = ((pixsize & 0x06) == 4 ? fc_pseq : fc_pseq & 0x7FF8);
               fc_penden = clip_a1 && (fc_pseq == 0);
               fc_window_mask = 0;

               if (fc_penden)
                  fc_window_mask = (a1_win_x & (fc_ppp - 1)) << pixsize;
               else
                  fc_window_mask = 0;

               fc_inner_mask = 0;
               if (fc_inner0)
                  fc_inner_mask = (icount & (fc_ppp - 1)) << pixsize;

               fc_window_mask = (fc_window_mask == 0 ? 0x40 : fc_window_mask);
               fc_inner_mask  = (fc_inner_mask == 0 ? 0x40 : fc_inner_mask);
               fc_emask       = (fc_window_mask > fc_inner_mask ? fc_inner_mask : fc_window_mask);
               fc_pma = fc_dst_pixa + (1 << pixsize);
               fc_dend = (phrase_mode ? fc_emask : fc_pma);

               /* Implicit dest read for byte merging (phrase mode or sub-byte pixels) */
               if (phrase_mode && !bkgwren)
                  dstd = ((uint64_t)blitter_read_long(fc_dst_addr) << 32)
                     | (uint64_t)blitter_read_long(fc_dst_addr + 4);
               else if (!phrase_mode && pixsize < 3 && !bkgwren)
                  dstd = (uint64_t)blitter_read_byte(fc_dst_addr);

               /* Source shift/alignment */
               /* Guard against UB: shifting a 64-bit value by 64 is undefined in C. */
               if (srcshift == 0)
                  fc_srcd = srcd1;
               else
                  fc_srcd = (srcd2 << (64 - srcshift)) | (srcd1 >> srcshift);
               if (!phrase_mode && srcshift != 0)
                  fc_srcd = ((srcd2 & 0xFF) << (8 - srcshift)) | ((srcd1 & 0xFF) >> srcshift);

               /* DATA: LFU + masking + byte merge.
                * For simple copy: data_sel=1 (LFU), no gourd, no patdadd,
                * no srcshade, no bcompen/dcompen. */
               {
                  uint64_t fc_srcz_dummy = 0;
                  DATA(&fc_wdata, &fc_dcomp_val, &fc_zcomp_val, &fc_winhibit,
                        true/*big_pix*/, cmpdst,
                        0/*daddasel*/, 0/*daddbsel*/,
                        (uint8_t)(0x05 | (topben ? 0x00 : 0x02))/*daddmode*/,
                        false/*daddq_sel*/, 1/*data_sel=LFU*/, 0/*dbinh*/,
                        fc_dend, fc_dstart, dstd, iinc, lfufunc, &patd,
                        false/*patdadd*/,
                        phrase_mode, fc_srcd, false/*srcdread*/, false/*srczread*/,
                        false/*srcz2add*/, zmode,
                        false/*bcompen*/, bkgwren, false/*dcompen*/,
                        icount & 0x07, pixsize,
                        &fc_srcz_dummy, dstz, zinc);
                  (void)fc_dcomp_val;
                  (void)fc_zcomp_val;
               }

               /* Window clipping */
               if (clip_a1 && ((a1_x & 0x8000) || (a1_y & 0x8000)
                     || (a1_x >= a1_win_x) || (a1_y >= a1_win_y)))
                  fc_winhibit = true;

               /* Write */
               PERF_INC(blitter_phrase_writes);
               if (!fc_winhibit || bkgwren)
               {
                  if (phrase_mode)
                  {
                     blitter_write_long(fc_dst_addr + 0, fc_wdata >> 32);
                     blitter_write_long(fc_dst_addr + 4, fc_wdata & 0xFFFFFFFF);
                  }
                  else
                  {
                     if (pixsize == 5)
                        blitter_write_long(fc_dst_addr, fc_wdata & 0xFFFFFFFF);
                     else if (pixsize == 4)
                        blitter_write_word(fc_dst_addr, fc_wdata & 0x0000FFFF);
                     else
                        blitter_write_byte(fc_dst_addr, fc_wdata & 0x000000FF);
                  }
               }

               /* --- Step source pointer --- */
               ADDAMUX(&fc_adda_x, &fc_adda_y, 0/*addasel*/,
                     a1_step_x, a1_step_y, a1_stepf_x, a1_stepf_y,
                     a2_step_x, a2_step_y,
                     a1_inc_x, a1_inc_y, a1_incf_x, a1_incf_y,
                     fc_src_xconst, (dsta2 ? a1addy : a2addy)/*adda_yconst*/,
                     false/*addareg*/, fc_src_suba_x, fc_src_suba_y);
               ADDBMUX(&fc_addb_x, &fc_addb_y, fc_src_addbsel,
                     a1_x, a1_y, a2_x, a2_y, a1_frac_x, a1_frac_y);
               ADDRADD(&fc_addq_x, &fc_addq_y, false/*a1fracldi*/,
                     fc_adda_x, fc_adda_y, fc_addb_x, fc_addb_y,
                     fc_src_modx, fc_src_suba_x, fc_src_suba_y);

               if (dsta2)
               {
                  a1_x = fc_addq_x;
                  if (fc_addq_y != a1_y)
                  {
                     a1_y = fc_addq_y;
                     a1_ya_cached = addrgen_ya((uint16_t)a1_y, a1_width);
                  }
               }
               else
               {
                  a2_x = fc_addq_x;
                  if (fc_addq_y != a2_y)
                  {
                     a2_y = fc_addq_y;
                     a2_ya_cached = addrgen_ya((uint16_t)a2_y, a2_width);
                  }
               }

               /* --- Step destination pointer --- */
               ADDAMUX(&fc_adda_x, &fc_adda_y, 0/*addasel*/,
                     a1_step_x, a1_step_y, a1_stepf_x, a1_stepf_y,
                     a2_step_x, a2_step_y,
                     a1_inc_x, a1_inc_y, a1_incf_x, a1_incf_y,
                     fc_dst_xconst, (dsta2 ? a2addy : a1addy)/*adda_yconst*/,
                     false/*addareg*/, fc_dst_suba_x, fc_dst_suba_y);
               ADDBMUX(&fc_addb_x, &fc_addb_y, fc_dst_addbsel,
                     a1_x, a1_y, a2_x, a2_y, a1_frac_x, a1_frac_y);
               ADDRADD(&fc_addq_x, &fc_addq_y, false/*a1fracldi*/,
                     fc_adda_x, fc_adda_y, fc_addb_x, fc_addb_y,
                     fc_dst_modx, fc_dst_suba_x, fc_dst_suba_y);

               if (dsta2)
               {
                  a2_x = fc_addq_x;
                  if (fc_addq_y != a2_y)
                  {
                     a2_y = fc_addq_y;
                     a2_ya_cached = addrgen_ya((uint16_t)a2_y, a2_width);
                  }
               }
               else
               {
                  a1_x = fc_addq_x;
                  if (fc_addq_y != a1_y)
                  {
                     a1_y = fc_addq_y;
                     a1_ya_cached = addrgen_ya((uint16_t)a1_y, a1_width);
                  }
               }
            } /* end collapsed copy loop */

            /* Mark inner loop as done and skip the state machine */
            goto fc_inner_done;
         }

         while (true)
         {
#ifdef BENCH_PROFILE
            int blitter_did_io = 0;
#endif
            /* PERF_INC embedded via comma operator to keep C89 decl
             * order valid (no statements before declarations).  */
            uint16_t dstxwr = (PERF_INC(blitter_inner), 0), pseq;
            bool penden;
            uint8_t window_mask;
            uint8_t inner_mask = 0;
            uint8_t emask, pma, dend;
            uint64_t srcd;
            uint8_t zSrcShift;
            uint64_t wdata;
            uint8_t dcomp, zcomp;

            //NOTE: sshftld probably is only asserted at the beginning of the inner loop. !!! FIX !!!
            /* State machine: step is always true (no bus contention in
               Jaguar I), textext/txtread never assert. Both eliminated. */

            if ((dzwrite && inner0)
                  || (dwrite && !dstwrz && inner0))
            {
               idle_inneri = true;
               break;
            }
            else
               idle_inneri = false;

            sreadxi = (idle_inner && srcenx);
            szreadxi = (sreadx && srcenz);

            sreadi = (szreadx
                  || (sreadx && !srcenz && srcen)
                  || (idle_inner && !srcenx && srcen)
                  || (dzwrite && !inner0 && srcen)
                  || (dwrite && !dstwrz && !inner0 && srcen));

            szreadi = (sread && srcenz);

            dreadi = ((szread && dsten)
                  || (sread && !srcenz && dsten)
                  || (sreadx && !srcenz && !srcen && dsten)
                  || (idle_inner && !srcenx && !srcen && dsten)
                  || (dzwrite && !inner0 && !srcen && dsten)
                  || (dwrite && !dstwrz && !inner0 && !srcen && dsten));

            dzreadi = ((dread && dstenz)
                  || (szread && !dsten && dstenz)
                  || (sread && !srcenz && !dsten && dstenz)
                  || (sreadx && !srcenz && !srcen && !dsten && dstenz)
                  || (idle_inner && !srcenx && !srcen && !dsten && dstenz)
                  || (dzwrite && !inner0 && !srcen && !dsten && dstenz)
                  || (dwrite && !dstwrz && !inner0 && !srcen && !dsten && dstenz));

            dwritei = (dzread
                  || (dread && !dstenz)
                  || (szread && !dsten && !dstenz)
                  || (sread && !srcenz && !dsten && !dstenz)
                  || (sreadx && !srcenz && !srcen && !dsten && !dstenz)
                  || (idle_inner && !srcenx && !srcen && !dsten && !dstenz)
                  || (dzwrite && !inner0 && !srcen && !dsten && !dstenz)
                  || (dwrite && !dstwrz && !inner0 && !srcen && !dsten && !dstenz));

            dzwritei = (dwrite && dstwrz);

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

            ADDRGEN(&address, &pixAddr, gena2i, zaddr,
                  a1_x, a1_y, a1_base, a1_pitch, a1_pixsize, a1_width, a1_zoffset,
                  a2_x, a2_y, a2_base, a2_pitch, a2_pixsize, a2_width, a2_zoffset,
                  a1_ya_cached, a2_ya_cached);

            //Here's my guess as to how the addresses get truncated to phrase boundaries in phrase mode...
            if (!justify)
               address &= 0xFFFFF8;

            /* dstxp needed for dstart computation in dwrite */
            dstxp = (dsta2 ? a2_x : a1_x) & 0x3F;

            if (sreadx)
            {
               PERF_INC(blitter_phrase_reads);
#ifdef BENCH_PROFILE
               blitter_did_io = 1;
#endif
               /* Snapshot pre-step a1 for the CLIP_A1 check at dwrite
                * time -- see a1_x_at_sread declaration. */
               a1_x_at_sread = a1_x;
               a1_y_at_sread = a1_y;
               //uint32_t srcAddr, pixAddr;
               //ADDRGEN(srcAddr, pixAddr, gena2i, zaddr,
               //	a1_x, a1_y, a1_base, a1_pitch, a1_pixsize, a1_width, a1_zoffset,
               //	a2_x, a2_y, a2_base, a2_pitch, a2_pixsize, a2_width, a2_zoffset);
               srcd2 = srcd1;
               srcd1 = ((uint64_t)blitter_read_long(address + 0) << 32)
                  | (uint64_t)blitter_read_long(address + 4);
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
               srcz1 = ((uint64_t)blitter_read_long(address) << 32) | (uint64_t)blitter_read_long(address + 4);
            }

            if (sread)
            {
               PERF_INC(blitter_phrase_reads);
#ifdef BENCH_PROFILE
               blitter_did_io = 1;
#endif
               /* Snapshot pre-step a1 for the CLIP_A1 check at dwrite
                * time -- see a1_x_at_sread declaration. */
               a1_x_at_sread = a1_x;
               a1_y_at_sread = a1_y;
               srcd2 = srcd1;
               srcd1 = ((uint64_t)blitter_read_long(address) << 32) | (uint64_t)blitter_read_long(address + 4);
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
               PERF_INC(blitter_phrase_reads);
#ifdef BENCH_PROFILE
               blitter_did_io = 1;
#endif
               srcz2 = srcz1;
               srcz1 = ((uint64_t)blitter_read_long(address) << 32) | (uint64_t)blitter_read_long(address + 4);
               //Kludge to take pixel size into account... I believe that it only has to take 16BPP mode into account. Not sure tho.
               if (!phrase_mode && pixsize == 4)
                  srcz1 >>= 48;

            }

            if (dread)
            {
               PERF_INC(blitter_phrase_reads);
#ifdef BENCH_PROFILE
               blitter_did_io = 1;
#endif
               dstd = ((uint64_t)blitter_read_long(address) << 32) | (uint64_t)blitter_read_long(address + 4);
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
               dstz = ((uint64_t)blitter_read_long(address) << 32) | (uint64_t)blitter_read_long(address + 4);
               //Kludge to take pixel size into account... I believe that it only has to take 16BPP mode into account. Not sure tho.
               if (!phrase_mode && pixsize == 4)
                  dstz >>= 48;

            }

            // These vars should probably go further up in the code... !!! FIX !!!
            // We can't preassign these unless they're static...
            //NOTE: SRCSHADE requires GOURZ to be set to work properly--another Jaguar I bug
            if (dwrite)
            {
               int8_t inct;
               uint8_t inc = 0;
               uint16_t oldicount;
               uint8_t dstart = 0;
               uint8_t ppp;
#ifdef BENCH_PROFILE
               blitter_did_io = 1;
#endif

               PERF_INC(blitter_phrase_writes);
               ppp = 64 >> pixsize;
               inct = -((dsta2 ? a2_x : a1_x) & 0x07);
               inc = (!phrase_mode || (phrase_mode && (inct & 0x01)) ? 0x01 : 0x00);
               inc |= (phrase_mode && (((pixsize == 3 || pixsize == 4) && (inct & 0x02)) || (pixsize == 5 && !(inct & 0x01))) ? 0x02 : 0x00);
               inc |= (phrase_mode && ((pixsize == 3 && (inct & 0x04)) || (pixsize == 4 && !(inct & 0x03))) ? 0x04 : 0x00);
               inc |= (phrase_mode && pixsize == 3 && !(inct & 0x07) ? 0x08 : 0x00);

               oldicount = icount;
               icount -= inc;

               if (icount == 0 || ((icount & 0x8000) && !(oldicount & 0x8000)))
                  inner0 = true;
               // X/Y stepping is also done here, I think...No. It's done when a1_add or a2_add is asserted...

               //*********************************************************************************
               //Start & end write mask computations...
               //*********************************************************************************


               if (phrase_mode)
                  dstart = (dstxp & (ppp - 1)) << pixsize;
               else
                  dstart = pixAddr & 0x07;

               //This is the other Jaguar I bug... Normally, should ALWAYS select a1_x here.
               dstxwr = (dsta2 ? a2_x : a1_x) & 0x7FFE;
               pseq = dstxwr ^ (a1_win_x & 0x7FFE);
               pseq = (pixsize == 5 ? pseq : pseq & 0x7FFC);
               pseq = ((pixsize & 0x06) == 4 ? pseq : pseq & 0x7FF8);
               penden = clip_a1 && (pseq == 0);
               window_mask = 0;

               if (penden)
                  window_mask = (a1_win_x & (ppp - 1)) << pixsize;
               else
                  window_mask = 0;

               /* The mask to be used if within one phrase of the end of the inner
                  loop, similarly */

               if (inner0)
                  inner_mask = (icount & (ppp - 1)) << pixsize;
               else
                  inner_mask = 0;

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

               //Phrase mode needs destination data for start/end mask byte merging,
               //but NOT when bkgwren is set (hardware uses DSTDATA register value).
               //Pixel mode at pixsize < 3 (1bpp/2bpp/4bpp) writes a single byte
               //via JaguarWriteByte below, but the byte holds multiple pixels --
               //byte_merge must see the existing dest byte in the low 8 bits of
               //dstd or the unmodified pixel slots in that byte get zeroed
               //(matches WRITE_PIXEL_1/2/4 RMW in the fast blitter).
               /* Phrase writes merge via byte_merge(mask): inhibited bytes
                * come from dstd.  When bkgwren is set, hardware uses the
                * DSTDATA register value as background — reading memory
                * would inject stale pixels as noise (AvP red-artifact bug).
                * When bkgwren is NOT set, always read the framebuffer so
                * byte_merge has the live pixels for uninhibited positions
                * (Battle Sphere cockpit reticle fix, commit 54ca486). */
               if (phrase_mode && !bkgwren)
                  dstd = ((uint64_t)blitter_read_long(address) << 32) | (uint64_t)blitter_read_long(address + 4);
               else if (!phrase_mode && pixsize < 3 && !dsten && !bkgwren)
                  dstd = (uint64_t)blitter_read_byte(address);

               // Write data combines srcd and dstd through ADDDSEL, PATDSEL, or LFU.
               // Precedence is ADDDSEL > PATDSEL > LFU.

               // srcd2 = xxxx xxxx 0123 4567, srcd = 8901 2345 xxxx xxxx, srcshift = $20 (32)
               /* Guard against UB: shifting a 64-bit value by 64 is undefined in C. */
               if (srcshift == 0)
                  srcd = srcd1;
               else
                  srcd = (srcd2 << (64 - srcshift)) | (srcd1 >> srcshift);

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
               /* Guard against UB: shifting a 64-bit value by 64 is undefined in C. */
               if (zSrcShift == 0)
                  srcz = srcz1;
               else
                  srcz = (srcz2 << (64 - zSrcShift)) | (srcz1 >> zSrcShift);


               //When in SRCSHADE mode, it adds the IINC to the read source (from LFU???)
               //According to following line, it gets LFU mode. But does it feed the source into the LFU
               //after the add?
               //Dest write address/pix address: 0014E83E/0 [dstart=0 dend=10 pwidth=8 srcshift=0][daas=4 dabs=5 dam=7 ds=1 daq=F] [0000000000006505] (icount=003F, inc=1)
               //Let's try this:
               if (srcshade)
               {
                  uint16_t addq[4];
                  uint8_t initcin[4] = { 0, 0, 0, 0 };
                  uint32_t iinc_masked = iinc & 0x00FFFFFF;
                  ADDARRAY(addq, 4/*daddasel*/, 5/*daddbsel*/, 7/*daddmode*/, dstd, iinc_masked, initcin, 0, 0, 0, patd, srcd, 0, 0, 0, 0);
                  srcd = ((uint64_t)addq[3] << 48) | ((uint64_t)addq[2] << 32) | ((uint64_t)addq[1] << 16) | (uint64_t)addq[0];
               }

               /* DCONTROL: compute data adder signals.  Moved here from
                  the per-iteration scope since they are only consumed
                  during dwrite (dwrite=true, dzwrite=false here). */
               shadeadd = srcshade;
               daddasel = (gourd ? 0x01 : 0x00);
               daddasel |= ((gourd || gourz || srcshade) ? 0x04 : 0x00);
               daddbsel = (gourd || srcshade ? 0x01 : 0x00);
               daddbsel |= (gourd || srcshade ? 0x04 : 0x00);
               /* daddmode bit 0: NAND tree (dcontrol.v:130-146) makes
                  bit 0 always 1 when dwrite&&gourd, !gourd&&!gourz,
                  or shadeadd. */
               daddmode = (gourd || (!gourd && !gourz) || shadeadd ? 0x01 : 0x00);
               daddmode |= ((gourd && !topben && !ext_int)
                     || (!gourd && !gourz && !topben) || (shadeadd && !topben) ? 0x02 : 0x00);
               daddmode |= ((!gourd && !gourz) || shadeadd || (gourd && ext_int) ? 0x04 : 0x00);
               patfadd = gourd;
               patdadd = gourd;
               srcz2add = false;
               daddq_sel = gourd;
               data_sel = ((!patdsel && !adddsel) ? 0x01 : 0x00)
                  | (adddsel ? 0x02 : 0x00);

               if (patfadd)
               {
                  uint16_t addq[4];
                  uint8_t initcin[4] = { 0, 0, 0, 0 };
                  ADDARRAY(addq, 4/*daddasel*/, 4/*daddbsel*/, 0/*daddmode*/, dstd, iinc, initcin, 0, 0, 0, patd, srcd, 0, 0, 0, 0);
                  srcd1 = ((uint64_t)addq[3] << 48) | ((uint64_t)addq[2] << 32) | ((uint64_t)addq[1] << 16) | (uint64_t)addq[0];
               }

               /* atick[0]/[1] two-phase pipeline: fractional intensity/Z update
                  runs in the patfadd/srcz2add block above (Phase 0), integer
                  update runs via DATA→patdadd below (Phase 1).  The dbinh
                  param below is overwritten inside DATA by COMP_CTRL. */

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
               /* Check CLIP_A1 against the pre-step a1 position (the one
                * used for the sread that loaded srcd), not the post-step
                * a1.  See a1_x_at_sread declaration above for the
                * rationale.  This matches fast's CLIP_A1 timing.
                *
                * When srcen/srcenx are both clear, no sread/sreadx ever
                * fires to refresh the cache, but a1 can still step each
                * iteration via dsta_addi (when !dsta2).  Refresh from
                * live a1_x/a1_y at the top of dwrite -- the step for
                * this cycle hasn't fired yet, so live a1 == pre-step. */
               if (!srcen && !srcenx)
               {
                  a1_x_at_sread = a1_x;
                  a1_y_at_sread = a1_y;
               }
               if (clip_a1 && ((a1_x_at_sread & 0x8000) || (a1_y_at_sread & 0x8000)
                            || (a1_x_at_sread >= (int16_t)a1_win_x)
                            || (a1_y_at_sread >= (int16_t)a1_win_y)))
                  winhibit = true;


               if (!winhibit || bkgwren)
               {
                  if (phrase_mode)
                  {
                     blitter_write_long(address + 0, wdata >> 32);
                     blitter_write_long(address + 4, wdata & 0xFFFFFFFF);
                  }
                  else
                  {
                     if (pixsize == 5)
                        blitter_write_long(address, wdata & 0xFFFFFFFF);
                     else if (pixsize == 4)
                        blitter_write_word(address, wdata & 0x0000FFFF);
                     else
                        blitter_write_byte(address, wdata & 0x000000FF);
                  }
               }

            }

            if (dzwrite)
            {
               PERF_INC(blitter_phrase_writes);
#ifdef BENCH_PROFILE
               blitter_did_io = 1;
#endif
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
                     blitter_write_long(address + 0, srcz >> 32);
                     blitter_write_long(address + 4, srcz & 0xFFFFFFFF);
                  }
                  else
                  {
                     if (pixsize == 4)
                        blitter_write_word(address, srcz & 0x0000FFFF);
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

                  a1_x = addq_x;
                  if (addq_y != a1_y)
                  {
                     a1_y = addq_y;
                     a1_ya_cached = addrgen_ya((uint16_t)a1_y, a1_width);
                  }
               }
               else
               {
                  a1_x = addq_x;
                  if (addq_y != a1_y)
                  {
                     a1_y = addq_y;
                     a1_ya_cached = addrgen_ya((uint16_t)a1_y, a1_width);
                  }
               }
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
               if (addq_y != a2_y)
               {
                  a2_y = addq_y;
                  a2_ya_cached = addrgen_ya((uint16_t)a2_y, a2_width);
               }
            }
#ifdef BENCH_PROFILE
            if (blitter_did_io) PERF_INC(blitter_inner_io);
            else                PERF_INC(blitter_inner_idle);
#endif
         }

patfill_inner_done:
fc_inner_done:
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

   // Write values back to registers (in real blitter, these are continuously updated)
   SET16(blitter_ram, A1_PIXEL + 2, a1_x);
   SET16(blitter_ram, A1_PIXEL + 0, a1_y);
   SET16(blitter_ram, A1_FPIXEL + 2, a1_frac_x);
   SET16(blitter_ram, A1_FPIXEL + 0, a1_frac_y);
   SET16(blitter_ram, A2_PIXEL + 2, a2_x);
   SET16(blitter_ram, A2_PIXEL + 0, a2_y);

#ifdef BLITTER_TRACE
   {
      static mach_timebase_info_data_t tb;
      uint64_t t1 = mach_absolute_time();
      double ms;
      if (tb.denom == 0) mach_timebase_info(&tb);
      ms = (double)(t1 - bm2_trace_t0) * (double)tb.numer / (double)tb.denom / 1e6;
      if (ms >= bm2_trace_threshold_ms) {
         uint16_t pcount = GET16(blitter_ram, PIXLINECOUNTER + 2);
         uint16_t lcount = GET16(blitter_ram, PIXLINECOUNTER);
         uint8_t pixsize = (blitter_ram[A1_FLAGS + 3] & 0x38) >> 3;
         fprintf(stderr,
            "[BLITTER_TRACE] %.2f ms cmd=%08x pixsize=%u inner=%u outer=%u "
            "src(en=%d enx=%d enz=%d) dst(en=%d enz=%d wrz=%d) "
            "gourd=%d gourz=%d srcshade=%d bcompen=%d dcompen=%d\n",
            ms, cmd, pixsize, pcount, lcount,
            (int)srcen, (int)srcenx, (int)srcenz,
            (int)dsten, (int)dstenz, (int)dstwrz,
            (int)gourd, (int)gourz, (int)srcshade,
            (int)bcompen, (int)dcompen);
      }
   }
#endif
}

// Various pieces of the blitter puzzle are teased out here...

static void ADDRGEN(uint32_t *address, uint32_t *pixa, bool gena2, bool zaddr,
	uint16_t a1_x, uint16_t a1_y, uint32_t a1_base, uint8_t a1_pitch, uint8_t a1_pixsize, uint8_t a1_width, uint8_t a1_zoffset,
	uint16_t a2_x, uint16_t a2_y, uint32_t a2_base, uint8_t a2_pitch, uint8_t a2_pixsize, uint8_t a2_width, uint8_t a2_zoffset,
	uint32_t a1_ya_pre, uint32_t a2_ya_pre)
{
	uint16_t x = (gena2 ? a2_x : a1_x) & 0xFFFF;	/* Actually uses all 16 bits to generate address...! */
	uint8_t pixsize = (gena2 ? a2_pixsize : a1_pixsize);
	uint8_t pitch = (gena2 ? a2_pitch : a1_pitch);
	uint32_t base = (gena2 ? a2_base : a1_base) >> 3;/*Only upper 21 bits are passed around the bus? Seems like it...*/
	uint8_t zoffset = (gena2 ? a2_zoffset : a1_zoffset);

	uint32_t ya = (gena2 ? a2_ya_pre : a1_ya_pre);

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


static void ADDAMUX(int16_t *adda_x, int16_t *adda_y, uint8_t addasel, int16_t a1_step_x, int16_t a1_step_y,
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
static void ADDBMUX(int16_t *addb_x, int16_t *addb_y, uint8_t addbsel, int16_t a1_x, int16_t a1_y,
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
static void DATAMUX(int16_t *data_x, int16_t *data_y, uint32_t gpu_din, int16_t addq_x, int16_t addq_y, bool addqsel)
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

static void ADDRADD(int16_t *addq_x, int16_t *addq_y, bool a1fracldi,
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


#endif


/* Save state serialization for Blitter */

size_t BlitterStateSave(uint8_t *buf)
{
   uint8_t *start = buf;

   STATE_SAVE_BUF(buf, blitter_ram, sizeof(blitter_ram));
   STATE_SAVE_VAR(buf, src);
   STATE_SAVE_VAR(buf, dst);
   STATE_SAVE_VAR(buf, misc);
   STATE_SAVE_VAR(buf, a1ctl);
   STATE_SAVE_VAR(buf, mode);
   STATE_SAVE_VAR(buf, ity);
   STATE_SAVE_VAR(buf, zop);
   STATE_SAVE_VAR(buf, op);
   STATE_SAVE_VAR(buf, ctrl);
   STATE_SAVE_VAR(buf, a1_addr);
   STATE_SAVE_VAR(buf, a2_addr);
   STATE_SAVE_VAR(buf, a1_zoffs);
   STATE_SAVE_VAR(buf, a2_zoffs);
   STATE_SAVE_VAR(buf, xadd_a1_control);
   STATE_SAVE_VAR(buf, xadd_a2_control);
   STATE_SAVE_VAR(buf, a1_pitch);
   STATE_SAVE_VAR(buf, a2_pitch);
   STATE_SAVE_VAR(buf, n_pixels);
   STATE_SAVE_VAR(buf, n_lines);
   STATE_SAVE_VAR(buf, a1_x);
   STATE_SAVE_VAR(buf, a1_y);
   STATE_SAVE_VAR(buf, a1_width);
   STATE_SAVE_VAR(buf, a2_x);
   STATE_SAVE_VAR(buf, a2_y);
   STATE_SAVE_VAR(buf, a2_width);
   STATE_SAVE_VAR(buf, a2_mask_x);
   STATE_SAVE_VAR(buf, a2_mask_y);
   STATE_SAVE_VAR(buf, a1_xadd);
   STATE_SAVE_VAR(buf, a1_yadd);
   STATE_SAVE_VAR(buf, a2_xadd);
   STATE_SAVE_VAR(buf, a2_yadd);
   STATE_SAVE_VAR(buf, a1_phrase_mode);
   STATE_SAVE_VAR(buf, a2_phrase_mode);
   STATE_SAVE_VAR(buf, a1_step_x);
   STATE_SAVE_VAR(buf, a1_step_y);
   STATE_SAVE_VAR(buf, a2_step_x);
   STATE_SAVE_VAR(buf, a2_step_y);
   STATE_SAVE_VAR(buf, outer_loop);
   STATE_SAVE_VAR(buf, inner_loop);
   STATE_SAVE_VAR(buf, a2_psize);
   STATE_SAVE_VAR(buf, a1_psize);
   STATE_SAVE_VAR(buf, gouraud_add);
   STATE_SAVE_BUF(buf, gd_i, sizeof(gd_i));
   STATE_SAVE_BUF(buf, gd_c, sizeof(gd_c));
   STATE_SAVE_VAR(buf, gd_ia);
   STATE_SAVE_VAR(buf, gd_ca);
   STATE_SAVE_VAR(buf, colour_index);
   STATE_SAVE_VAR(buf, zadd);
   STATE_SAVE_BUF(buf, z_i, sizeof(z_i));
   STATE_SAVE_VAR(buf, a1_clip_x);
   STATE_SAVE_VAR(buf, a1_clip_y);

   return (size_t)(buf - start);
}


size_t BlitterStateLoad(const uint8_t *buf)
{
   const uint8_t *start = buf;

   STATE_LOAD_BUF(buf, blitter_ram, sizeof(blitter_ram));
   STATE_LOAD_VAR(buf, src);
   STATE_LOAD_VAR(buf, dst);
   STATE_LOAD_VAR(buf, misc);
   STATE_LOAD_VAR(buf, a1ctl);
   STATE_LOAD_VAR(buf, mode);
   STATE_LOAD_VAR(buf, ity);
   STATE_LOAD_VAR(buf, zop);
   STATE_LOAD_VAR(buf, op);
   STATE_LOAD_VAR(buf, ctrl);
   STATE_LOAD_VAR(buf, a1_addr);
   STATE_LOAD_VAR(buf, a2_addr);
   STATE_LOAD_VAR(buf, a1_zoffs);
   STATE_LOAD_VAR(buf, a2_zoffs);
   STATE_LOAD_VAR(buf, xadd_a1_control);
   STATE_LOAD_VAR(buf, xadd_a2_control);
   STATE_LOAD_VAR(buf, a1_pitch);
   STATE_LOAD_VAR(buf, a2_pitch);
   STATE_LOAD_VAR(buf, n_pixels);
   STATE_LOAD_VAR(buf, n_lines);
   STATE_LOAD_VAR(buf, a1_x);
   STATE_LOAD_VAR(buf, a1_y);
   STATE_LOAD_VAR(buf, a1_width);
   STATE_LOAD_VAR(buf, a2_x);
   STATE_LOAD_VAR(buf, a2_y);
   STATE_LOAD_VAR(buf, a2_width);
   STATE_LOAD_VAR(buf, a2_mask_x);
   STATE_LOAD_VAR(buf, a2_mask_y);
   STATE_LOAD_VAR(buf, a1_xadd);
   STATE_LOAD_VAR(buf, a1_yadd);
   STATE_LOAD_VAR(buf, a2_xadd);
   STATE_LOAD_VAR(buf, a2_yadd);
   STATE_LOAD_VAR(buf, a1_phrase_mode);
   STATE_LOAD_VAR(buf, a2_phrase_mode);
   STATE_LOAD_VAR(buf, a1_step_x);
   STATE_LOAD_VAR(buf, a1_step_y);
   STATE_LOAD_VAR(buf, a2_step_x);
   STATE_LOAD_VAR(buf, a2_step_y);
   STATE_LOAD_VAR(buf, outer_loop);
   STATE_LOAD_VAR(buf, inner_loop);
   STATE_LOAD_VAR(buf, a2_psize);
   STATE_LOAD_VAR(buf, a1_psize);
   STATE_LOAD_VAR(buf, gouraud_add);
   STATE_LOAD_BUF(buf, gd_i, sizeof(gd_i));
   STATE_LOAD_BUF(buf, gd_c, sizeof(gd_c));
   STATE_LOAD_VAR(buf, gd_ia);
   STATE_LOAD_VAR(buf, gd_ca);
   STATE_LOAD_VAR(buf, colour_index);
   STATE_LOAD_VAR(buf, zadd);
   STATE_LOAD_BUF(buf, z_i, sizeof(z_i));
   STATE_LOAD_VAR(buf, a1_clip_x);
   STATE_LOAD_VAR(buf, a1_clip_y);

   return (size_t)(buf - start);
}
