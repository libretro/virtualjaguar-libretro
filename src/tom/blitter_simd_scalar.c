/*
 * Blitter SIMD ops — portable scalar reference implementation
 *
 * These are extracted verbatim from blitter.c DATA() and serve as:
 *   1. The fallback for platforms without SIMD
 *   2. The reference for bit-exactness testing of SIMD variants
 */

#include "blitter_simd.h"

/* Logic Function Unit
 *
 * Implements a 4-input truth table over each bit position of two
 * 64-bit words (srcd, dstd). lfu_func selects one of 16 boolean
 * functions (AND, OR, XOR, etc.).
 */
static uint64_t scalar_lfu(uint64_t srcd, uint64_t dstd, uint8_t lfu_func)
{
   uint64_t funcmask[2] = { 0, 0xFFFFFFFFFFFFFFFFULL };
   uint64_t func0 = funcmask[lfu_func & 0x01];
   uint64_t func1 = funcmask[(lfu_func >> 1) & 0x01];
   uint64_t func2 = funcmask[(lfu_func >> 2) & 0x01];
   uint64_t func3 = funcmask[(lfu_func >> 3) & 0x01];

   return (~srcd & ~dstd & func0)
        | (~srcd &  dstd & func1)
        | ( srcd & ~dstd & func2)
        | ( srcd &  dstd & func3);
}

/* Data Comparator
 *
 * XORs patd with either dstd or srcd (selected by cmpdst), then
 * checks each of the 8 bytes for equality (zero). Returns an 8-bit
 * mask with bit N set when byte N matches.
 */
static uint8_t scalar_dcomp(uint64_t patd, uint64_t srcd, uint64_t dstd, bool cmpdst)
{
   uint8_t result = 0;
   uint64_t cmpd = patd ^ (cmpdst ? dstd : srcd);

   if ((cmpd & 0x00000000000000FFULL) == 0) result |= 0x01;
   if ((cmpd & 0x000000000000FF00ULL) == 0) result |= 0x02;
   if ((cmpd & 0x0000000000FF0000ULL) == 0) result |= 0x04;
   if ((cmpd & 0x00000000FF000000ULL) == 0) result |= 0x08;
   if ((cmpd & 0x000000FF00000000ULL) == 0) result |= 0x10;
   if ((cmpd & 0x0000FF0000000000ULL) == 0) result |= 0x20;
   if ((cmpd & 0x00FF000000000000ULL) == 0) result |= 0x40;
   if ((cmpd & 0xFF00000000000000ULL) == 0) result |= 0x80;

   return result;
}

/* Z-buffer Comparator
 *
 * Compares 4 packed 16-bit Z values. For each of the 4 lanes,
 * sets the output bit if the comparison selected by zmode is true:
 *   bit 0 (0x01): less-than
 *   bit 1 (0x02): equal
 *   bit 2 (0x04): greater-than
 */
static uint8_t scalar_zcomp(uint64_t srcz, uint64_t dstz, uint8_t zmode)
{
   uint8_t result = 0;
   int i;

   for (i = 0; i < 4; i++)
   {
      uint16_t s = (uint16_t)(srcz >> (i * 16));
      uint16_t d = (uint16_t)(dstz >> (i * 16));

      if (((s < d) && (zmode & 0x01))
       || ((s == d) && (zmode & 0x02))
       || ((s > d) && (zmode & 0x04)))
         result |= (1u << i);
   }

   return result;
}

/* Byte Mask Merge
 *
 * Selects bytes from src or dst based on a 16-bit mask:
 *   - Byte 0: per-bit blend (src where mask bit is 1, dst where 0)
 *   - Bytes 1-7: whole-byte select via mask bits 8-14
 *
 * Used for both pixel data (ddat/dstd -> wdata) and Z-buffer data
 * (srcz/dstz -> zwdata).
 */
static uint64_t scalar_byte_merge(uint64_t src, uint64_t dst, uint16_t mask)
{
   uint64_t result;

   /* Byte 0: per-bit mask (low 8 bits of mask) */
   result = ((src & mask) | (dst & ~(uint64_t)mask)) & 0x00000000000000FFULL;

   /* Bytes 1-7: whole-byte select via mask bits 8-14 */
   result |= ((mask & 0x0100) ? src : dst) & 0x000000000000FF00ULL;
   result |= ((mask & 0x0200) ? src : dst) & 0x0000000000FF0000ULL;
   result |= ((mask & 0x0400) ? src : dst) & 0x00000000FF000000ULL;
   result |= ((mask & 0x0800) ? src : dst) & 0x000000FF00000000ULL;
   result |= ((mask & 0x1000) ? src : dst) & 0x0000FF0000000000ULL;
   result |= ((mask & 0x2000) ? src : dst) & 0x00FF000000000000ULL;
   result |= ((mask & 0x4000) ? src : dst) & 0xFF00000000000000ULL;

   return result;
}

/* ADD16SAT x4 — scalar reference
 *
 * Implements 4 parallel 16-bit saturating adds using the Jaguar's
 * segmented carry chain: low byte, mid nibble (bits 11:8), high
 * nibble (bits 15:12), with conditional carry propagation.
 */
static void scalar_add16sat_x4(uint16_t *addq, uint8_t *co,
                                const uint16_t *adda, const uint16_t *addb,
                                const uint8_t *cin,
                                bool sat, bool eightbit, bool hicinh)
{
   int i;
   for (i = 0; i < 4; i++)
   {
      uint16_t a = adda[i];
      uint16_t b = addb[i];
      uint8_t carry0, carry1, carry2, carry3;
      uint8_t btop, ctop;
      bool saturate, hisaturate;
      uint32_t qt;
      uint16_t q;

      qt      = (a & 0xFF) + (b & 0xFF) + cin[i];
      q       = qt & 0x00FF;
      carry0  = ((qt & 0x0100) ? 1 : 0);
      carry1  = (carry0 && !eightbit ? carry0 : 0);
      qt      = (a & 0x0F00) + (b & 0x0F00) + (carry1 << 8);
      carry2  = ((qt & 0x1000) ? 1 : 0);
      q      |= qt & 0x0F00;
      carry3  = (carry2 && !hicinh ? carry2 : 0);
      qt      = (a & 0xF000) + (b & 0xF000) + (carry3 << 12);
      co[i]   = ((qt & 0x10000) ? 1 : 0);
      q      |= qt & 0xF000;

      if (eightbit)
      {
         btop = (b & 0x0080) >> 7;
         ctop = carry0;
      }
      else
      {
         btop = (b & 0x8000) >> 15;
         ctop = co[i];
      }

      saturate   = sat && (btop ^ ctop);
      hisaturate = saturate && !eightbit;

      addq[i]  = (saturate ? (ctop ? 0x00FF : 0x0000) : q & 0x00FF);
      addq[i] |= (hisaturate ? (ctop ? 0xFF00 : 0x0000) : q & 0xFF00);
   }
}

const blitter_simd_ops_t blitter_simd_ops = {
   scalar_lfu,
   scalar_dcomp,
   scalar_zcomp,
   scalar_byte_merge,
   scalar_add16sat_x4
};
