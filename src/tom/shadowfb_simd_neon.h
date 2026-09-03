/*
 * Hi-res shadow-framebuffer Stage-1 kernels — ARM NEON implementation.
 *
 * Guarded by BLITTER_SIMD_HAVE_NEON from blitter_simd_arch.h (capability,
 * not a new BUILD_AXES flag).  Kernels use only the ARMv7 Advanced-SIMD
 * subset so rpi2 / rpi3-32-bit stay portable: vget_low_* / vget_high_* +
 * the struct-returning vzip_u16 rather than any A64-only form (no vzip1 /
 * vqtbl1q / vaddv).  Included from shadowfb.c; keep this file C89 (vars
 * at top of each block).
 *
 * Every kernel is a pure store rewrite of the scalar loop it replaces:
 * identical bytes end up at identical addresses, so byte-identity of the
 * presented frame is structural, not incidental.  The {value16, frac16}
 * fill pattern is built as uint16 LANES (vzip of two dups) and stored
 * with vst1*_u16, which writes elements in increasing address order on
 * any host endianness -- exactly the two uint16_t members of a
 * shadowfb_sub, with no byte-layout assumption.
 */

#ifndef SHADOWFB_SIMD_NEON_H
#define SHADOWFB_SIMD_NEON_H

#include "blitter_simd_arch.h"

#if defined(BLITTER_SIMD_HAVE_NEON)

#include <arm_neon.h>
#include <stdint.h>

#ifndef VJ_RESTRICT
#if defined(__GNUC__) || defined(__clang__) || defined(_MSC_VER)
#  define VJ_RESTRICT __restrict
#else
#  define VJ_RESTRICT
#endif
#endif

#ifndef INLINE
#define INLINE
#endif

/* [value16, frac16, value16, frac16] as uint16 lanes -- two consecutive
 * shadowfb_sub entries' worth of pattern per 64-bit half. */
static INLINE uint16x8_t shadowfb_neon_sub_pattern(uint16_t value16,
                                                   uint16_t frac16)
{
   uint16x4x2_t z;
   z = vzip_u16(vdup_n_u16(value16), vdup_n_u16(frac16));
   return vcombine_u16(z.val[0], z.val[0]);
}

/* Fill `nn` shadowfb_sub entries with {value16, frac16} (the Stage-1 box
 * replication store in ShadowHiresStoreCry).  Returns entries written
 * (multiple of 4; nn=4 at N=2, so the whole block in one store). */
static INLINE uint32_t shadowfb_neon_sub_fill(
   shadowfb_sub * VJ_RESTRICT dst,
   uint16_t value16,
   uint16_t frac16,
   uint32_t nn)
{
   uint32_t i;
   uint32_t done;
   uint16x8_t q;

   q = shadowfb_neon_sub_pattern(value16, frac16);
   done = nn & ~3u;
   for (i = 0; i < done; i += 4)
      vst1q_u16((uint16_t *)(dst + i), q);
   return done;
}

/* ShadowHiresLineFromRAM sub-plane body at N=2: scatter one 2x2 block
 * (sub-row-major: blk[0..1] -> line sub-row 0, blk[2..3] -> sub-row 1)
 * into the two line-buffer rows, or box-replicate {value16, 0} on a
 * resolve miss (blk == NULL).  One 16-byte load + two 8-byte stores
 * replaces the scalar sy/sx double loop with its per-subpixel blk test. */
static INLINE void shadowfb_neon_line_sub_n2(
   shadowfb_sub * VJ_RESTRICT row0,
   shadowfb_sub * VJ_RESTRICT row1,
   const shadowfb_sub * VJ_RESTRICT blk,
   uint16_t value16)
{
   uint16x8_t q;

   if (blk)
      q = vld1q_u16((const uint16_t *)blk);
   else
      q = shadowfb_neon_sub_pattern(value16, 0);
   vst1_u16((uint16_t *)row0, vget_low_u16(q));
   vst1_u16((uint16_t *)row1, vget_high_u16(q));
}

/* ShadowHiresLineFromRAM replacement-plane body at N=2: same 2x2
 * scatter for the pack-art plane (real uint32 entries), zero-filling
 * when the word carries no replacement block (rblk == NULL) -- exactly
 * the scalar `rblk ? rblk[sy * n + sx] : 0`. */
static INLINE void shadowfb_neon_line_repl_n2(
   uint32_t * VJ_RESTRICT row0,
   uint32_t * VJ_RESTRICT row1,
   const uint32_t * VJ_RESTRICT rblk)
{
   uint32x4_t q;

   if (rblk)
      q = vld1q_u32(rblk);
   else
      q = vdupq_n_u32(0);
   vst1_u32(row0, vget_low_u32(q));
   vst1_u32(row1, vget_high_u32(q));
}

#endif /* BLITTER_SIMD_HAVE_NEON */

#endif /* SHADOWFB_SIMD_NEON_H */
