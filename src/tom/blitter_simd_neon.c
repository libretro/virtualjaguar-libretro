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

const blitter_simd_ops_t blitter_simd_ops = {
   neon_lfu,
   neon_dcomp,
   neon_zcomp,
   neon_byte_merge
};
