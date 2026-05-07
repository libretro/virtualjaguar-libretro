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

/* ADD16SAT x4 — SSE2
 *
 * Processes all four 16-bit lanes in parallel using SSE2 vectors.
 * The Jaguar's segmented carry chain (low byte / mid nibble / high nibble)
 * is replicated across all 4 lanes simultaneously.
 *
 * Uses the low 64 bits of 128-bit SSE registers (4 x 16-bit lanes).
 */
static void sse2_add16sat_x4(uint16_t *addq, uint8_t *co,
                              const uint16_t *adda, const uint16_t *addb,
                              const uint8_t *cin,
                              bool sat, bool eightbit, bool hicinh)
{
   /* Load operands — only low 4 lanes used (64 bits) */
   __m128i va = _mm_loadl_epi64((const __m128i *)adda);
   __m128i vb = _mm_loadl_epi64((const __m128i *)addb);

   /* Expand cin[0..3] from uint8_t to 16-bit lanes */
   __m128i vcin;
   __m128i mask_lo, mask_mid, mask_hi, mask_0100, mask_1000;
   __m128i va_lo, vb_lo, sum1, q_lo, carry0, carry1;
   __m128i va_mid, vb_mid, sum2, q_mid, carry2, carry3;
   __m128i va_hi, vb_hi, sum3, q_hi;
   __m128i q;
   uint32_t s0, s1, s2, s3;
   uint16_t c3_0, c3_1, c3_2, c3_3;
   uint16_t carry3_buf[8];

   {
      uint16_t cin16[8] = { 0 };
      cin16[0] = cin[0]; cin16[1] = cin[1]; cin16[2] = cin[2]; cin16[3] = cin[3];
      vcin = _mm_loadu_si128((const __m128i *)cin16);
   }

   mask_lo   = _mm_set1_epi16(0x00FF);
   mask_mid  = _mm_set1_epi16(0x0F00);
   mask_hi   = _mm_set1_epi16((short)0xF000);
   mask_0100 = _mm_set1_epi16(0x0100);
   mask_1000 = _mm_set1_epi16(0x1000);

   /* Stage 1: low byte add */
   va_lo = _mm_and_si128(va, mask_lo);
   vb_lo = _mm_and_si128(vb, mask_lo);
   sum1  = _mm_add_epi16(_mm_add_epi16(va_lo, vb_lo), vcin);
   q_lo  = _mm_and_si128(sum1, mask_lo);

   /* carry0 = (sum1 >> 8) & 1 */
   carry0 = _mm_srli_epi16(_mm_and_si128(sum1, mask_0100), 8);

   /* carry1 = eightbit ? 0 : carry0 */
   carry1 = eightbit ? _mm_setzero_si128() : carry0;

   /* Stage 2: mid nibble add */
   va_mid = _mm_and_si128(va, mask_mid);
   vb_mid = _mm_and_si128(vb, mask_mid);
   sum2   = _mm_add_epi16(_mm_add_epi16(va_mid, vb_mid), _mm_slli_epi16(carry1, 8));
   q_mid  = _mm_and_si128(sum2, mask_mid);

   /* carry2 = (sum2 >> 12) & 1 */
   carry2 = _mm_srli_epi16(_mm_and_si128(sum2, mask_1000), 12);

   /* carry3 = hicinh ? 0 : carry2 */
   carry3 = hicinh ? _mm_setzero_si128() : carry2;

   /* Stage 3: high nibble add */
   va_hi = _mm_and_si128(va, mask_hi);
   vb_hi = _mm_and_si128(vb, mask_hi);
   sum3  = _mm_add_epi16(_mm_add_epi16(va_hi, vb_hi), _mm_slli_epi16(carry3, 12));
   q_hi  = _mm_and_si128(sum3, mask_hi);

   /* co[i]: detect carry out of bit 15.  16-bit lanes lose bit 16,
    * so widen to 32-bit to capture the overflow. */
   _mm_storeu_si128((__m128i *)carry3_buf, carry3);
   c3_0 = carry3_buf[0]; c3_1 = carry3_buf[1];
   c3_2 = carry3_buf[2]; c3_3 = carry3_buf[3];

   s0 = (uint32_t)(adda[0] & 0xF000) + (addb[0] & 0xF000) + ((uint32_t)c3_0 << 12);
   s1 = (uint32_t)(adda[1] & 0xF000) + (addb[1] & 0xF000) + ((uint32_t)c3_1 << 12);
   s2 = (uint32_t)(adda[2] & 0xF000) + (addb[2] & 0xF000) + ((uint32_t)c3_2 << 12);
   s3 = (uint32_t)(adda[3] & 0xF000) + (addb[3] & 0xF000) + ((uint32_t)c3_3 << 12);
   co[0] = (s0 & 0x10000) ? 1 : 0;
   co[1] = (s1 & 0x10000) ? 1 : 0;
   co[2] = (s2 & 0x10000) ? 1 : 0;
   co[3] = (s3 & 0x10000) ? 1 : 0;

   /* Combine low + mid + high */
   q = _mm_or_si128(_mm_or_si128(q_lo, q_mid), q_hi);

   /* Saturation logic */
   if (sat)
   {
      __m128i btop, ctop, do_saturate, do_hi_saturate;
      __m128i sat_lo_fill, sat_hi_fill;
      __m128i sat_mask, hi_sat_mask;
      __m128i result_lo, result_hi;
      uint16_t co16[8] = { 0 };
      __m128i vco;

      co16[0] = co[0]; co16[1] = co[1]; co16[2] = co[2]; co16[3] = co[3];
      vco = _mm_loadu_si128((const __m128i *)co16);

      if (eightbit)
      {
         btop = _mm_srli_epi16(_mm_and_si128(vb, _mm_set1_epi16(0x0080)), 7);
         ctop = carry0;
      }
      else
      {
         btop = _mm_srli_epi16(vb, 15);
         ctop = vco;
      }

      /* do_saturate = btop ^ ctop (non-zero lanes saturate) */
      do_saturate = _mm_xor_si128(btop, ctop);

      /* do_hi_saturate = eightbit ? 0 : do_saturate */
      do_hi_saturate = eightbit ? _mm_setzero_si128() : do_saturate;

      /* Build saturated fill: ctop ? 0x00FF/0xFF00 : 0x0000 */
      /* ctop is 0 or 1 in each lane. Expand to mask: cmpeq with 1 gives 0xFFFF where ctop=1 */
      {
         __m128i vone = _mm_set1_epi16(1);
         __m128i ctop_mask = _mm_cmpeq_epi16(ctop, vone);
         sat_lo_fill = _mm_and_si128(ctop_mask, mask_lo);   /* 0x00FF where ctop=1 */
         sat_hi_fill = _mm_and_si128(ctop_mask, _mm_set1_epi16((short)0xFF00));

         /* Expand do_saturate/do_hi_saturate to masks */
         sat_mask    = _mm_cmpeq_epi16(do_saturate, vone);
         hi_sat_mask = _mm_cmpeq_epi16(do_hi_saturate, vone);
      }

      /* Select: if saturating use fill, else use q */
      result_lo = _mm_or_si128(
         _mm_and_si128(sat_mask, sat_lo_fill),
         _mm_andnot_si128(sat_mask, _mm_and_si128(q, mask_lo))
      );
      result_hi = _mm_or_si128(
         _mm_and_si128(hi_sat_mask, sat_hi_fill),
         _mm_andnot_si128(hi_sat_mask, _mm_and_si128(q, _mm_set1_epi16((short)0xFF00)))
      );

      q = _mm_or_si128(result_lo, result_hi);
   }

   /* Store result — only low 4 lanes (64 bits) */
   _mm_storel_epi64((__m128i *)addq, q);
}

const blitter_simd_ops_t blitter_simd_ops = {
   sse2_lfu,
   sse2_dcomp,
   sse2_zcomp,
   sse2_byte_merge,
   sse2_add16sat_x4
};
