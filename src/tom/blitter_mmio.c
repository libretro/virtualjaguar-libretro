#include "blitter.h"
#include "blitter_internal.h"

#include <string.h>
#include "settings.h"

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
   else if (((offset >= SRCDATA + 0) && (offset <= SRCDATA + 3))
         || ((offset >= DSTDATA + 0) && (offset <= DSTDATA + 3))
         || ((offset >= DSTZ + 0) && (offset <= DSTZ + 3))
         || ((offset >= SRCZINT + 0) && (offset <= SRCZINT + 3))
         || ((offset >= SRCZFRAC + 0) && (offset <= SRCZFRAC + 3))
         || ((offset >= PATTERNDATA + 0) && (offset <= PATTERNDATA + 3)))
   {
      /* 64-bit register longword swap: the Jaguar's F-bus maps the first
       * 32-bit write (lower address) to the LOW longword of the internal
       * 64-bit register, and the second write (higher address) to the HIGH
       * longword.  GET64 reads blitter_ram in big-endian byte order
       * (offset+0 = MSB), so we swap the two halves on write to match.
       * The PHRASEINT/PHRASEZ path above already has its own per-byte
       * mapping and is unaffected.  The Gouraud init reads (gd_c[]/gd_i[]
       * in blitter.c) are designed for this swapped layout. */
      blitter_ram[offset + 4] = data;
   }
   else if (((offset >= SRCDATA + 4) && (offset <= SRCDATA + 7))
         || ((offset >= DSTDATA + 4) && (offset <= DSTDATA + 7))
         || ((offset >= DSTZ + 4) && (offset <= DSTZ + 7))
         || ((offset >= SRCZINT + 4) && (offset <= SRCZINT + 7))
         || ((offset >= SRCZFRAC + 4) && (offset <= SRCZFRAC + 7))
         || ((offset >= PATTERNDATA + 4) && (offset <= PATTERNDATA + 7)))
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
   {
      if (BlitterCompareIsEnabled())
         BlitterRunComparison();
      else if (vjs.useFastBlitter)
         blitter_blit(GET32(blitter_ram, COMMAND));
      else
         BlitterMidsummer2();
   }
}


void BlitterWriteLong(uint32_t offset, uint32_t data, uint32_t who)
{
   BlitterWriteWord(offset + 0, data >> 16, who);
   BlitterWriteWord(offset + 2, data & 0xFFFF, who);
}
