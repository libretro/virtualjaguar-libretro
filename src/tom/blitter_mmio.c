#include "blitter.h"
#include "blitter_internal.h"

#include <string.h>
#include "bus_arbiter.h"
#include "settings.h"
#include "vjag_memory.h"

#define A1_FLAGS        ((uint32_t)0x04)
#define A1_PIXEL        ((uint32_t)0x0C)
#define COMMAND         ((uint32_t)0x38)
#define SRCDATA         ((uint32_t)0x40)
#define DSTDATA         ((uint32_t)0x48)
#define DSTZ            ((uint32_t)0x50)
#define SRCZINT         ((uint32_t)0x58)
#define SRCZFRAC        ((uint32_t)0x60)
#define PATTERNDATA     ((uint32_t)0x68)
#define INTENSITYINC    ((uint32_t)0x70)
#define PHRASEINT0      ((uint32_t)0x7C)
#define PHRASEINT1      ((uint32_t)0x80)
#define PHRASEINT2      ((uint32_t)0x84)
#define PHRASEINT3      ((uint32_t)0x88)
#define PHRASEZ0        ((uint32_t)0x8C)
#define PHRASEZ1        ((uint32_t)0x90)
#define PHRASEZ2        ((uint32_t)0x94)
#define PHRASEZ3        ((uint32_t)0x98)

/* How long this blit would keep the bus on real hardware, in system
 * clocks (issue #399/#401: the emulated blitter completes every blit in
 * zero emulated time, so titles that pace a render-bound loop on blit
 * completion -- Jaguar Doom's menu, whose M_Drawer never calls
 * I_Update() and so has no VBL gate at all -- iterate faster than
 * hardware, and their auto-repeat thresholds land inside a normal
 * button tap).
 *
 * The blitter is the top-priority bus master, so while a blit runs the
 * 68K -- which has no cache and fetches every instruction from
 * DRAM/ROM -- is effectively frozen.  Rather than model per-access bus
 * arbitration, the 68K that kicks a blit is charged the blit's whole
 * bus time through the existing pending-stall channel (drained
 * gradually by M68KExecuteWithStalls, remainder carried across slices;
 * the field is already serialized).  GPU-kicked blits are deliberately
 * NOT charged: GPU code executes from local RAM and is not frozen the
 * same way, and charging it correctly needs the GPU self-cost path.
 *
 * Cost model, from the JTRM DRAM timing used by bus_arbiter.h: one
 * page-mode DRAM cycle is a fixed 2 system clocks.  Each inner-loop
 * unit performs one memory op per enabled port: destination write
 * always, plus source read (SRCEN), source Z read (SRCENZ),
 * destination read (DSTEN), destination Z read (DSTENZ), Z write
 * (DSTWRZ).  In phrase mode (A1 XADD = phrase) a unit moves 64 bits,
 * so the pixel count collapses by 64/bpp.  Row-miss overhead and bus
 * arbitration slack are real but second-order for linear sweeps and
 * are deliberately left out: this is a documented FLOOR, so any error
 * keeps us on the too-fast side rather than inventing slowness. */
static uint32_t BlitDurationSysclks(void)
{
   uint32_t count, cmd, a1flags;
   uint32_t ops;
   uint64_t units, clks;

   count   = GET32(blitter_ram, 0x3C);          /* B_COUNT */
   cmd     = GET32(blitter_ram, COMMAND);
   a1flags = GET32(blitter_ram, A1_FLAGS);

   units = (uint64_t)(count & 0xFFFF) * (count >> 16);

   ops = 1;                                     /* dst write */
   if (cmd & 0x0001) ops++;                     /* SRCEN */
   if (cmd & 0x0002) ops++;                     /* SRCENZ */
   if (cmd & 0x0008) ops++;                     /* DSTEN */
   if (cmd & 0x0010) ops++;                     /* DSTENZ */
   if (cmd & 0x0020) ops++;                     /* DSTWRZ */

   if (((a1flags >> 16) & 3) == 0)
   {
      /* Phrase mode: one memory op per 64-bit phrase. */
      uint32_t bpp = 1u << ((a1flags >> 3) & 7);
      if (bpp > 32)
         bpp = 32;
      units = (units * bpp + 63) / 64;
   }

   clks = units * ops * 2u;

   /* Garbage counts (uninitialised B_COUNT) must not freeze the 68K
    * for seconds: cap at one field's worth of bus time. */
   if (clks > 442780u)
      clks = 442780u;
   return (uint32_t)clks;
}


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

   (void)who;

   /* Real hardware returns $00000805, as documented in the JTRM. */
   if (offset == (COMMAND + 0))
      return 0x00;
   if (offset == (COMMAND + 1))
      return 0x00;
   if (offset == (COMMAND + 2))
      return 0x08;
   if (offset == (COMMAND + 3))
      return 0x05;	/* always idle/never stopped (collision detection ignored!) */

   /* Jaguar I bug: A1_PIXEL is mirrored when A1_FLAGS is read. */
   if (offset >= A1_FLAGS && offset <= (A1_FLAGS + 3))
      return blitter_ram[offset + 0x08];

   /* Jaguar I bug: A2_PIXEL is mirrored when A2_MASK is read. */
   if (offset >= 0x2C && offset <= 0x2F)
      return blitter_ram[offset + 0x04];

   return blitter_ram[offset];
}


uint16_t BlitterReadWord(uint32_t offset, uint32_t who/*=UNKNOWN*/)
{
   return ((uint16_t)BlitterReadByte(offset, who) << 8) | (uint16_t)BlitterReadByte(offset + 1, who);
}


uint32_t BlitterReadLong(uint32_t offset, uint32_t who/*=UNKNOWN*/)
{
   return (BlitterReadWord(offset, who) << 16) | BlitterReadWord(offset + 2, who);
}


void BlitterWriteByte(uint32_t offset, uint8_t data, uint32_t who/*=UNKNOWN*/)
{
   offset &= 0xFF;

   (void)who;

   /* INTENSITY writes also update their PATTERNDATA/SRCDATA mirrors. */
   if ((offset >= PHRASEINT0) && (offset <= (PHRASEZ3 + 3)))
   {
      switch (offset)
      {
      /* INTENSITY registers 0-3 */
      case PHRASEINT0 + 0: break;
      case PHRASEINT0 + 1: blitter_ram[PATTERNDATA + 7] = data; break;
      case PHRASEINT0 + 2: blitter_ram[SRCDATA + 6] = data; break;
      case PHRASEINT0 + 3: blitter_ram[SRCDATA + 7] = data; break;

      case PHRASEINT1 + 0: break;
      case PHRASEINT1 + 1: blitter_ram[PATTERNDATA + 5] = data; break;
      case PHRASEINT1 + 2: blitter_ram[SRCDATA + 4] = data; break;
      case PHRASEINT1 + 3: blitter_ram[SRCDATA + 5] = data; break;

      case PHRASEINT2 + 0: break;
      case PHRASEINT2 + 1: blitter_ram[PATTERNDATA + 3] = data; break;
      case PHRASEINT2 + 2: blitter_ram[SRCDATA + 2] = data; break;
      case PHRASEINT2 + 3: blitter_ram[SRCDATA + 3] = data; break;

      case PHRASEINT3 + 0: break;
      case PHRASEINT3 + 1: blitter_ram[PATTERNDATA + 1] = data; break;
      case PHRASEINT3 + 2: blitter_ram[SRCDATA + 0] = data; break;
      case PHRASEINT3 + 3: blitter_ram[SRCDATA + 1] = data; break;

      /* Z registers 0-3 */
      case PHRASEZ0 + 0: blitter_ram[SRCZINT + 6] = data; break;
      case PHRASEZ0 + 1: blitter_ram[SRCZINT + 7] = data; break;
      case PHRASEZ0 + 2: blitter_ram[SRCZFRAC + 6] = data; break;
      case PHRASEZ0 + 3: blitter_ram[SRCZFRAC + 7] = data; break;

      case PHRASEZ1 + 0: blitter_ram[SRCZINT + 4] = data; break;
      case PHRASEZ1 + 1: blitter_ram[SRCZINT + 5] = data; break;
      case PHRASEZ1 + 2: blitter_ram[SRCZFRAC + 4] = data; break;
      case PHRASEZ1 + 3: blitter_ram[SRCZFRAC + 5] = data; break;

      case PHRASEZ2 + 0: blitter_ram[SRCZINT + 2] = data; break;
      case PHRASEZ2 + 1: blitter_ram[SRCZINT + 3] = data; break;
      case PHRASEZ2 + 2: blitter_ram[SRCZFRAC + 2] = data; break;
      case PHRASEZ2 + 3: blitter_ram[SRCZFRAC + 3] = data; break;

      case PHRASEZ3 + 0: blitter_ram[SRCZINT + 0] = data; break;
      case PHRASEZ3 + 1: blitter_ram[SRCZINT + 1] = data; break;
      case PHRASEZ3 + 2: blitter_ram[SRCZFRAC + 0] = data; break;
      case PHRASEZ3 + 3: blitter_ram[SRCZFRAC + 1] = data; break;
      }
   }
   else if (offset >= SRCDATA && offset <= (PATTERNDATA + 7))
   {
      /* 64-bit register longword swap: SRCDATA..PATTERNDATA (0x40-0x6F)
       * are six contiguous 8-byte registers.  The Jaguar's F-bus maps the
       * low address to the LOW longword and the high address to the HIGH
       * longword.  GET64 reads blitter_ram in big-endian byte order
       * (offset+0 = MSB), so we XOR bit 2 to swap the two 4-byte halves.
       * The PHRASEINT/PHRASEZ path above has its own per-byte mapping.
       * The Gouraud init reads (gd_c[]/gd_i[] in blitter.c) are designed
       * for this swapped layout. */
      blitter_ram[offset ^ 4] = data;
   }
   else
      blitter_ram[offset] = data;
}


void BlitterWriteWord(uint32_t offset, uint16_t data, uint32_t who/*=UNKNOWN*/)
{
   BlitterWriteByte(offset + 0, data >> 8, who);
   BlitterWriteByte(offset + 1, data & 0xFF, who);

   if ((offset & 0xFF) == 0x3A)
   {
      /* Compute the bus time BEFORE dispatch: the engines update their
       * shadow registers as they run. */
      uint32_t busClks = 0;
      if (vjs.blitterTiming && who == M68K)
         busClks = BlitDurationSysclks();

      if (BlitterCompareIsEnabled())
         BlitterRunComparison();
      else if (vjs.useFastBlitter)
         blitter_blit(GET32(blitter_ram, COMMAND));
      else
         BlitterMidsummer2();

      /* The blit's results are complete (memory is final, as before);
       * only the TIME is owed.  See BlitDurationSysclks above. */
      busArbiter.m68k_pending_stall += busClks;
   }
}


void BlitterWriteLong(uint32_t offset, uint32_t data, uint32_t who)
{
   BlitterWriteWord(offset + 0, data >> 16, who);
   BlitterWriteWord(offset + 2, data & 0xFFFF, who);
}
