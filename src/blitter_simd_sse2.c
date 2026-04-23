/*
 * Blitter SIMD ops — x86/x64 SSE2 implementation
 *
 * SSE2 is baseline for all x86_64 processors and available on x86
 * since Pentium 4 (2001). No runtime feature detection needed.
 *
 * Operations work on 64-bit values loaded into 128-bit SSE registers
 * (upper 64 bits unused or zeroed).
 */

#include "blitter_simd.h"
#include <emmintrin.h>  /* SSE2 */
#include <string.h>     /* memcpy for type-punning extract */

/* _mm_set_epi64x doesn't exist in MSVC 2010 and earlier.
 * Build from two 32-bit halves instead (pure SSE2). */
#if defined(_MSC_VER) && _MSC_VER < 1700
#define SSE2_SET64(hi, lo) \
   _mm_set_epi32((int)((uint64_t)(hi) >> 32), (int)(hi), \
                 (int)((uint64_t)(lo) >> 32), (int)(lo))
#else
#define SSE2_SET64(hi, lo) _mm_set_epi64x((int64_t)(hi), (int64_t)(lo))
#endif

static uint64_t sse2_extract_u64(__m128i v)
{
   uint64_t r;
   memcpy(&r, &v, sizeof(r));
   return r;
}

/* Logic Function Unit — SSE2
 *
 * The truth table has 4 terms, each gated by one bit of lfu_func.
 * We broadcast each gate to all 64 bits, then AND/OR in registers.
 */
static uint64_t sse2_lfu(uint64_t srcd, uint64_t dstd, uint8_t lfu_func)
{
   /* Build 64-bit masks from each lfu_func bit */
   uint64_t func0 = (lfu_func & 0x01) ? 0xFFFFFFFFFFFFFFFFULL : 0;
   uint64_t func1 = (lfu_func & 0x02) ? 0xFFFFFFFFFFFFFFFFULL : 0;
   uint64_t func2 = (lfu_func & 0x04) ? 0xFFFFFFFFFFFFFFFFULL : 0;
   uint64_t func3 = (lfu_func & 0x08) ? 0xFFFFFFFFFFFFFFFFULL : 0;

   __m128i vs   = SSE2_SET64(0, srcd);
   __m128i vd   = SSE2_SET64(0, dstd);
   __m128i vns  = _mm_andnot_si128(vs, _mm_set1_epi32(-1)); /* ~srcd */
   __m128i vnd  = _mm_andnot_si128(vd, _mm_set1_epi32(-1)); /* ~dstd */

   __m128i vf0  = SSE2_SET64(0, func0);
   __m128i vf1  = SSE2_SET64(0, func1);
   __m128i vf2  = SSE2_SET64(0, func2);
   __m128i vf3  = SSE2_SET64(0, func3);

   /* (~s & ~d & f0) | (~s & d & f1) | (s & ~d & f2) | (s & d & f3) */
   __m128i t0 = _mm_and_si128(_mm_and_si128(vns, vnd), vf0);
   __m128i t1 = _mm_and_si128(_mm_and_si128(vns, vd),  vf1);
   __m128i t2 = _mm_and_si128(_mm_and_si128(vs,  vnd), vf2);
   __m128i t3 = _mm_and_si128(_mm_and_si128(vs,  vd),  vf3);

   __m128i result = _mm_or_si128(_mm_or_si128(t0, t1), _mm_or_si128(t2, t3));
   return sse2_extract_u64(result);
}

/* Data Comparator — SSE2
 *
 * XOR the two 64-bit values, then check each byte for zero.
 * _mm_cmpeq_epi8 + _mm_movemask_epi8 gives us all 8 byte-equality
 * bits in one shot.
 */
static uint8_t sse2_dcomp(uint64_t patd, uint64_t srcd, uint64_t dstd, bool cmpdst)
{
   uint64_t other = cmpdst ? dstd : srcd;
   __m128i vp = SSE2_SET64(0, patd);
   __m128i vo = SSE2_SET64(0, other);
   __m128i vxor = _mm_xor_si128(vp, vo);

   /* Compare each byte against zero */
   __m128i vzero = _mm_setzero_si128();
   __m128i vcmp = _mm_cmpeq_epi8(vxor, vzero);

   /* movemask gives us 16 bits (one per byte of 128-bit reg).
    * We only care about the low 8 bytes. */
   return (uint8_t)(_mm_movemask_epi8(vcmp) & 0xFF);
}

/* Z-buffer Comparator — SSE2
 *
 * 4 packed 16-bit comparisons. SSE2 has _mm_cmpeq_epi16 and
 * _mm_cmpgt_epi16 (signed), but Z values are unsigned. We handle
 * this by biasing both operands by -32768 before signed compare.
 */
static uint8_t sse2_zcomp(uint64_t srcz, uint64_t dstz, uint8_t zmode)
{
   uint8_t result = 0;
   uint8_t packed = 0;

   __m128i vs = SSE2_SET64(0, srcz);
   __m128i vd = SSE2_SET64(0, dstz);

   /* Bias for unsigned comparison via signed instructions */
   __m128i bias = _mm_set1_epi16((short)0x8000);
   __m128i vs_biased = _mm_sub_epi16(vs, bias);
   __m128i vd_biased = _mm_sub_epi16(vd, bias);

   /* EQ: src == dst */
   if (zmode & 0x02)
   {
      __m128i veq = _mm_cmpeq_epi16(vs, vd);
      result |= (uint8_t)(_mm_movemask_epi8(veq) & 0xFF);
   }

   /* LT: src < dst (signed compare after bias = unsigned compare) */
   if (zmode & 0x01)
   {
      __m128i vlt = _mm_cmplt_epi16(vs_biased, vd_biased);
      result |= (uint8_t)(_mm_movemask_epi8(vlt) & 0xFF);
   }

   /* GT: src > dst */
   if (zmode & 0x04)
   {
      __m128i vgt = _mm_cmpgt_epi16(vs_biased, vd_biased);
      result |= (uint8_t)(_mm_movemask_epi8(vgt) & 0xFF);
   }

   /* movemask gives 2 bits per 16-bit lane (one per byte).
    * Convert to 1 bit per lane: lanes at positions 0,2,4,6 */
   if (result & 0x03) packed |= 0x01;  /* lane 0: bytes 0-1 */
   if (result & 0x0C) packed |= 0x02;  /* lane 1: bytes 2-3 */
   if (result & 0x30) packed |= 0x04;  /* lane 2: bytes 4-5 */
   if (result & 0xC0) packed |= 0x08;  /* lane 3: bytes 6-7 */

   return packed;
}

/* Byte Mask Merge — SSE2
 *
 * Byte 0 is per-bit blended, bytes 1-7 are whole-byte selected.
 * We build a byte-level mask from the 16-bit input, then use
 * AND/ANDNOT/OR to blend.
 */
static uint64_t sse2_byte_merge(uint64_t src, uint64_t dst, uint16_t mask)
{
   /* Build an 8-byte selection mask directly via bit arithmetic.
    * Byte 0 = low 8 bits of mask (per-bit blend).
    * Bytes 1-7 = 0xFF or 0x00 from mask bits 8-14 (whole-byte select).
    * We expand each bit to a full 0xFF byte using sign-extension. */
   uint64_t sel64 = (uint64_t)(mask & 0xFF);  /* byte 0: per-bit */
   __m128i vmask, vsrc, vdst, r;

   sel64 |= (uint64_t)((uint8_t)(-(int8_t)((mask >> 8)  & 1))) << 8;
   sel64 |= (uint64_t)((uint8_t)(-(int8_t)((mask >> 9)  & 1))) << 16;
   sel64 |= (uint64_t)((uint8_t)(-(int8_t)((mask >> 10) & 1))) << 24;
   sel64 |= (uint64_t)((uint8_t)(-(int8_t)((mask >> 11) & 1))) << 32;
   sel64 |= (uint64_t)((uint8_t)(-(int8_t)((mask >> 12) & 1))) << 40;
   sel64 |= (uint64_t)((uint8_t)(-(int8_t)((mask >> 13) & 1))) << 48;
   sel64 |= (uint64_t)((uint8_t)(-(int8_t)((mask >> 14) & 1))) << 56;

   vmask = SSE2_SET64(0, sel64);
   vsrc  = SSE2_SET64(0, src);
   vdst  = SSE2_SET64(0, dst);

   /* result = (src & mask) | (dst & ~mask) */
   r = _mm_or_si128(
      _mm_and_si128(vsrc, vmask),
      _mm_andnot_si128(vmask, vdst)
   );

   return sse2_extract_u64(r);
}

const blitter_simd_ops_t blitter_simd_ops = {
   sse2_lfu,
   sse2_dcomp,
   sse2_zcomp,
   sse2_byte_merge
};
