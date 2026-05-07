/*
 * Blitter SIMD ops — ARM NEON implementation
 *
 * NEON is mandatory on AArch64 and available on most ARMv7-A with
 * VFPv3. The Makefile selects this file when targeting ARM64 or
 * when HAVE_NEON=1.
 */

#include "blitter_simd.h"
#include <arm_neon.h>

/* Logic Function Unit — NEON
 *
 * Same truth-table approach as scalar, but using 64-bit NEON
 * integer operations (uint64x1_t).
 */
static uint64_t neon_lfu(uint64_t srcd, uint64_t dstd, uint8_t lfu_func)
{
   uint64x1_t vs  = vcreate_u64(srcd);
   uint64x1_t vd  = vcreate_u64(dstd);
   /* NEON vmvn only works up to 32-bit, so we use EOR with all-ones */
   uint64x1_t ones = vcreate_u64(0xFFFFFFFFFFFFFFFFULL);
   uint64x1_t vns = veor_u64(vs, ones);  /* ~srcd */
   uint64x1_t vnd = veor_u64(vd, ones);  /* ~dstd */

   uint64x1_t vf0 = vcreate_u64((lfu_func & 0x01) ? 0xFFFFFFFFFFFFFFFFULL : 0);
   uint64x1_t vf1 = vcreate_u64((lfu_func & 0x02) ? 0xFFFFFFFFFFFFFFFFULL : 0);
   uint64x1_t vf2 = vcreate_u64((lfu_func & 0x04) ? 0xFFFFFFFFFFFFFFFFULL : 0);
   uint64x1_t vf3 = vcreate_u64((lfu_func & 0x08) ? 0xFFFFFFFFFFFFFFFFULL : 0);

   /* (~s & ~d & f0) | (~s & d & f1) | (s & ~d & f2) | (s & d & f3) */
   uint64x1_t t0 = vand_u64(vand_u64(vns, vnd), vf0);
   uint64x1_t t1 = vand_u64(vand_u64(vns, vd),  vf1);
   uint64x1_t t2 = vand_u64(vand_u64(vs,  vnd), vf2);
   uint64x1_t t3 = vand_u64(vand_u64(vs,  vd),  vf3);

   uint64x1_t result = vorr_u64(vorr_u64(t0, t1), vorr_u64(t2, t3));
   return vget_lane_u64(result, 0);
}

/* Data Comparator — NEON
 *
 * XOR + per-byte zero check. NEON vceq gives 0xFF per matching byte,
 * then we extract one bit per byte.
 */
static uint8_t neon_dcomp(uint64_t patd, uint64_t srcd, uint64_t dstd, bool cmpdst)
{
   uint64_t other = cmpdst ? dstd : srcd;
   uint8x8_t vp = vreinterpret_u8_u64(vcreate_u64(patd));
   uint8x8_t vo = vreinterpret_u8_u64(vcreate_u64(other));

   /* XOR then compare each byte to zero */
   uint8x8_t vxor = veor_u8(vp, vo);
   uint8x8_t vzero = vdup_n_u8(0);
   uint8x8_t vcmp = vceq_u8(vxor, vzero);  /* 0xFF where equal, 0 otherwise */

   /* Convert compare lanes from 0xFF/0x00 to 1/0, then pack into
    * the result byte via lane extraction — avoids weights load
    * and vpadd chain. */
   uint8x8_t vbits = vshr_n_u8(vcmp, 7);

   return (uint8_t)(
      (vget_lane_u8(vbits, 0) << 0) |
      (vget_lane_u8(vbits, 1) << 1) |
      (vget_lane_u8(vbits, 2) << 2) |
      (vget_lane_u8(vbits, 3) << 3) |
      (vget_lane_u8(vbits, 4) << 4) |
      (vget_lane_u8(vbits, 5) << 5) |
      (vget_lane_u8(vbits, 6) << 6) |
      (vget_lane_u8(vbits, 7) << 7));
}

/* Z-buffer Comparator — NEON
 *
 * 4 packed unsigned 16-bit comparisons. NEON has native unsigned
 * compare instructions (vclt_u16, vceq_u16, vcgt_u16).
 */
static uint8_t neon_zcomp(uint64_t srcz, uint64_t dstz, uint8_t zmode)
{
   uint16x4_t vs = vreinterpret_u16_u64(vcreate_u64(srcz));
   uint16x4_t vd = vreinterpret_u16_u64(vcreate_u64(dstz));
   uint16x4_t vresult = vdup_n_u16(0);

   if (zmode & 0x01)  /* LT */
      vresult = vorr_u16(vresult, vclt_u16(vs, vd));
   if (zmode & 0x02)  /* EQ */
      vresult = vorr_u16(vresult, vceq_u16(vs, vd));
   if (zmode & 0x04)  /* GT */
      vresult = vorr_u16(vresult, vcgt_u16(vs, vd));

   /* Extract one bit per 16-bit lane.
    * vresult lanes are 0xFFFF or 0x0000. Narrow and extract. */
   /* Shift each lane: lane0 >> 0, lane1 >> 1, lane2 >> 2, lane3 >> 3
    * But easier: just read each lane */
   uint8_t result = 0;
   if (vget_lane_u16(vresult, 0)) result |= 0x01;
   if (vget_lane_u16(vresult, 1)) result |= 0x02;
   if (vget_lane_u16(vresult, 2)) result |= 0x04;
   if (vget_lane_u16(vresult, 3)) result |= 0x08;

   return result;
}

/* Byte Mask Merge — NEON
 *
 * Build a byte-level mask, then use vbsl (bitwise select) to blend.
 * vbsl(mask, src, dst) = (src & mask) | (dst & ~mask)
 */
static uint64_t neon_byte_merge(uint64_t src, uint64_t dst, uint16_t mask)
{
   /* Build 8-byte selection mask entirely in registers.
    * Byte 0 uses the low 8 bits directly; bytes 1-7 are all-ones or
    * all-zero based on mask bits 8-14. */
   uint64_t sel =
      ((uint64_t)(mask & 0x00FF)) |
      ((uint64_t)((mask & 0x0100) ? 0xFF : 0x00) <<  8) |
      ((uint64_t)((mask & 0x0200) ? 0xFF : 0x00) << 16) |
      ((uint64_t)((mask & 0x0400) ? 0xFF : 0x00) << 24) |
      ((uint64_t)((mask & 0x0800) ? 0xFF : 0x00) << 32) |
      ((uint64_t)((mask & 0x1000) ? 0xFF : 0x00) << 40) |
      ((uint64_t)((mask & 0x2000) ? 0xFF : 0x00) << 48) |
      ((uint64_t)((mask & 0x4000) ? 0xFF : 0x00) << 56);

   uint8x8_t vmask = vreinterpret_u8_u64(vcreate_u64(sel));
   uint8x8_t vsrc  = vreinterpret_u8_u64(vcreate_u64(src));
   uint8x8_t vdst  = vreinterpret_u8_u64(vcreate_u64(dst));

   /* vbsl: for each bit, selects src where mask=1, dst where mask=0 */
   uint8x8_t result = vbsl_u8(vmask, vsrc, vdst);

   return vget_lane_u64(vreinterpret_u64_u8(result), 0);
}

/* ADD16SAT x4 — NEON
 *
 * Processes all four 16-bit lanes in parallel using NEON 16x4 vectors.
 * The Jaguar's segmented carry chain (low byte / mid nibble / high nibble)
 * is replicated across all 4 lanes simultaneously.
 *
 * Stage 1: low byte  (bits 7:0)   — add with per-lane cin
 * Stage 2: mid nibble (bits 11:8) — add with carry from stage 1 (if !eightbit)
 * Stage 3: high nibble (bits 15:12)— add with carry from stage 2 (if !hicinh)
 * Then: saturation logic based on overflow detection.
 */
static void neon_add16sat_x4(uint16_t *addq, uint8_t *co,
                              const uint16_t *adda, const uint16_t *addb,
                              const uint8_t *cin,
                              bool sat, bool eightbit, bool hicinh)
{
   /* Load operands into NEON 16x4 vectors */
   uint16x4_t va = vld1_u16(adda);
   uint16x4_t vb = vld1_u16(addb);

   /* Masks for isolating bit fields */
   uint16x4_t mask_lo  = vdup_n_u16(0x00FF);
   uint16x4_t mask_mid = vdup_n_u16(0x0F00);
   uint16x4_t mask_hi  = vdup_n_u16(0xF000);
   uint16x4_t vzero    = vdup_n_u16(0);
   uint16x4_t vone     = vdup_n_u16(1);

   /* Expand cin[0..3] from uint8_t to uint16x4_t */
   uint16_t cin16[4];
   uint16x4_t vcin;

   /* Stage 1 results */
   uint16x4_t va_lo, vb_lo, sum1, q_lo, carry0, carry1;

   /* Stage 2 results */
   uint16x4_t va_mid, vb_mid, carry_shifted1, sum2, q_mid, carry2, carry3;

   /* Stage 3 results */
   uint16x4_t va_hi, vb_hi, carry_shifted3, sum3, q_hi;

   /* Combine result */
   uint16x4_t q;

   /* Saturation */
   uint16x4_t btop, ctop, do_saturate;
   uint16x4_t sat_lo_fill, sat_hi_fill;
   uint16x4_t sat_lo_zero, sat_hi_zero;

   cin16[0] = cin[0]; cin16[1] = cin[1]; cin16[2] = cin[2]; cin16[3] = cin[3];
   vcin = vld1_u16(cin16);

   /* Stage 1: low byte add */
   va_lo = vand_u16(va, mask_lo);
   vb_lo = vand_u16(vb, mask_lo);
   sum1  = vadd_u16(vadd_u16(va_lo, vb_lo), vcin);
   q_lo  = vand_u16(sum1, mask_lo);

   /* carry0 = (sum1 & 0x100) ? 1 : 0 */
   carry0 = vshr_n_u16(vand_u16(sum1, vdup_n_u16(0x0100)), 8);

   /* carry1 = carry0 if !eightbit, else 0 */
   carry1 = eightbit ? vzero : carry0;

   /* Stage 2: mid nibble add */
   va_mid = vand_u16(va, mask_mid);
   vb_mid = vand_u16(vb, mask_mid);
   carry_shifted1 = vshl_n_u16(carry1, 8);
   sum2   = vadd_u16(vadd_u16(va_mid, vb_mid), carry_shifted1);
   q_mid  = vand_u16(sum2, mask_mid);

   /* carry2 = (sum2 & 0x1000) ? 1 : 0 */
   carry2 = vshr_n_u16(vand_u16(sum2, vdup_n_u16(0x1000)), 12);

   /* carry3 = carry2 if !hicinh, else 0 */
   carry3 = hicinh ? vzero : carry2;

   /* Stage 3: high nibble add */
   va_hi = vand_u16(va, mask_hi);
   vb_hi = vand_u16(vb, mask_hi);
   carry_shifted3 = vshl_n_u16(carry3, 12);
   sum3   = vadd_u16(vadd_u16(va_hi, vb_hi), carry_shifted3);
   q_hi   = vand_u16(sum3, mask_hi);

   /* co[i] = (sum3 & 0x10000) ? 1 : 0 — detect carry out of bit 15.
    * Since our lanes are only 16-bit, the carry is lost. Widen to
    * 32-bit for the overflow detection. */
   {
      /* Use 32-bit arithmetic to capture the carry */
      uint32_t s0 = (uint32_t)(adda[0] & 0xF000) + (addb[0] & 0xF000) + ((uint32_t)vget_lane_u16(carry3, 0) << 12);
      uint32_t s1 = (uint32_t)(adda[1] & 0xF000) + (addb[1] & 0xF000) + ((uint32_t)vget_lane_u16(carry3, 1) << 12);
      uint32_t s2 = (uint32_t)(adda[2] & 0xF000) + (addb[2] & 0xF000) + ((uint32_t)vget_lane_u16(carry3, 2) << 12);
      uint32_t s3 = (uint32_t)(adda[3] & 0xF000) + (addb[3] & 0xF000) + ((uint32_t)vget_lane_u16(carry3, 3) << 12);
      co[0] = (s0 & 0x10000) ? 1 : 0;
      co[1] = (s1 & 0x10000) ? 1 : 0;
      co[2] = (s2 & 0x10000) ? 1 : 0;
      co[3] = (s3 & 0x10000) ? 1 : 0;
   }

   /* Combine low + mid + high */
   q = vorr_u16(vorr_u16(q_lo, q_mid), q_hi);

   /* Saturation logic */
   if (sat)
   {
      uint16_t co16[4];
      uint16x4_t vco;
      uint16x4_t do_hi_saturate;
      uint16x4_t result_lo, result_hi;

      co16[0] = co[0]; co16[1] = co[1]; co16[2] = co[2]; co16[3] = co[3];
      vco = vld1_u16(co16);

      if (eightbit)
      {
         btop = vshr_n_u16(vand_u16(vb, vdup_n_u16(0x0080)), 7);
         ctop = carry0;
      }
      else
      {
         btop = vshr_n_u16(vb, 15);
         ctop = vco;
      }

      /* saturate = sat && (btop ^ ctop) — sat is already true here */
      do_saturate = veor_u16(btop, ctop); /* non-zero where we saturate */

      /* do_hi_saturate = do_saturate && !eightbit */
      do_hi_saturate = eightbit ? vzero : do_saturate;

      /* When saturating:
       *   low byte = ctop ? 0xFF : 0x00
       *   high byte = ctop ? 0xFF00 : 0x0000 (only if !eightbit)
       * When not saturating, keep q as-is. */

      /* Expand ctop/do_saturate from {0,1} to full 16-bit masks {0x0000,0xFFFF} */
      {
         uint16x4_t ctop_mask     = vceq_u16(ctop, vone);          /* 0xFFFF where ctop=1 */
         uint16x4_t sat_mask      = vceq_u16(do_saturate, vone);   /* 0xFFFF where saturating */
         uint16x4_t hi_sat_mask   = vceq_u16(do_hi_saturate, vone);

         /* Build saturated fill values: ctop_mask ? 0x00FF/0xFF00 : 0x0000 */
         sat_lo_fill  = vand_u16(ctop_mask, vdup_n_u16(0x00FF));
         sat_hi_fill  = vand_u16(ctop_mask, vdup_n_u16(0xFF00));

         /* Build non-saturated values */
         sat_lo_zero  = vand_u16(q, mask_lo);
         sat_hi_zero  = vand_u16(q, vdup_n_u16(0xFF00));

         result_lo = vbsl_u16(sat_mask, sat_lo_fill, sat_lo_zero);
         result_hi = vbsl_u16(hi_sat_mask, sat_hi_fill, sat_hi_zero);
      }

      q = vorr_u16(result_lo, result_hi);
   }

   vst1_u16(addq, q);
}

const blitter_simd_ops_t blitter_simd_ops = {
   neon_lfu,
   neon_dcomp,
   neon_zcomp,
   neon_byte_merge,
   neon_add16sat_x4
};
