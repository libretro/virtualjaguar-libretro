/*
 * TOM scanline converters — ARM NEON implementation.
 *
 * Guarded by BLITTER_SIMD_HAVE_NEON from blitter_simd_arch.h (capability,
 * not a new BUILD_AXES flag).  Kernels use only the ARMv7 Advanced-SIMD
 * subset so rpi2 / rpi3-32-bit stay portable: vget_high_* + vmovl_* rather
 * than vmovl_high_*, no vaddv / vqtbl1q / vzip1.  Included from tom.c;
 * keep this file C89 (vars at top of each block).
 */

#ifndef TOM_SCAN_SIMD_NEON_H
#define TOM_SCAN_SIMD_NEON_H

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

/* 8 BE u16 pixels -> XRGB8888 via (color >> 1), matching
 * tom_render_16bpp_direct_scanline.  Returns pixels written (multiple of 8). */
static INLINE unsigned tom_scan_neon_16bpp_direct(
   const uint8_t * VJ_RESTRICT src,
   uint32_t * VJ_RESTRICT dst,
   unsigned n)
{
   unsigned i;
   unsigned done;
   uint8x16_t raw;
   uint16x8_t pix;
   uint32x4_t lo;
   uint32x4_t hi;

   done = n & ~7u;
   for (i = 0; i < done; i += 8)
   {
      raw = vld1q_u8(src + (i * 2));
      pix = vshrq_n_u16(vreinterpretq_u16_u8(vrev16q_u8(raw)), 1);
      lo = vmovl_u16(vget_low_u16(pix));
      hi = vmovl_u16(vget_high_u16(pix));
      vst1q_u32(dst + i, lo);
      vst1q_u32(dst + i + 4, hi);
   }
   return done;
}

/* 8 Jaguar 24bpp pixels (G,R,pad,B) -> XRGB8888
 * 0xFF000000 | (r << 16) | (g << 8) | b.  Returns pixels written (multiple of 8). */
static INLINE unsigned tom_scan_neon_24bpp(
   const uint8_t * VJ_RESTRICT src,
   uint32_t * VJ_RESTRICT dst,
   unsigned n)
{
   unsigned i;
   unsigned done;
   uint8x8x4_t in;
   uint16x8_t r16;
   uint16x8_t g16;
   uint16x8_t b16;
   uint32x4_t r;
   uint32x4_t g;
   uint32x4_t b;
   uint32x4_t out;
   uint32x4_t alpha;

   done = n & ~7u;
   alpha = vdupq_n_u32(0xFF000000u);
   for (i = 0; i < done; i += 8)
   {
      in = vld4_u8(src + (i * 4));
      /* in.val[0]=G, [1]=R, [2]=pad, [3]=B */
      g16 = vmovl_u8(in.val[0]);
      r16 = vmovl_u8(in.val[1]);
      b16 = vmovl_u8(in.val[3]);

      r = vshlq_n_u32(vmovl_u16(vget_low_u16(r16)), 16);
      g = vshlq_n_u32(vmovl_u16(vget_low_u16(g16)), 8);
      b = vmovl_u16(vget_low_u16(b16));
      out = vorrq_u32(alpha, vorrq_u32(r, vorrq_u32(g, b)));
      vst1q_u32(dst + i, out);

      r = vshlq_n_u32(vmovl_u16(vget_high_u16(r16)), 16);
      g = vshlq_n_u32(vmovl_u16(vget_high_u16(g16)), 8);
      b = vmovl_u16(vget_high_u16(b16));
      out = vorrq_u32(alpha, vorrq_u32(r, vorrq_u32(g, b)));
      vst1q_u32(dst + i + 4, out);
   }
   return done;
}

/* 8 Jaguar RGB16 pixels (RBG 556: RRRRR BBBBB GGGGGG) -> XRGB8888, bit-
 * identical to RGB16ToRGB32[] filled by TOMFillLookupTables:
 *   0xFF000000 | ((c & 0xF800) << 8) | ((c & 0x003F) << 10) | ((c & 0x07C0) >> 3)
 * Returns pixels written (multiple of 8). */
static INLINE unsigned tom_scan_neon_16bpp_rgb(
   const uint8_t * VJ_RESTRICT src,
   uint32_t * VJ_RESTRICT dst,
   unsigned n)
{
   unsigned i;
   unsigned done;
   uint8x16_t raw;
   uint16x8_t c;
   uint32x4_t c32;
   uint32x4_t r;
   uint32x4_t g;
   uint32x4_t b;
   uint32x4_t out;
   uint32x4_t alpha;
   uint32x4_t mask_r;
   uint32x4_t mask_g;
   uint32x4_t mask_b;

   done = n & ~7u;
   alpha = vdupq_n_u32(0xFF000000u);
   mask_r = vdupq_n_u32(0xF800u);
   mask_g = vdupq_n_u32(0x003Fu);
   mask_b = vdupq_n_u32(0x07C0u);
   for (i = 0; i < done; i += 8)
   {
      raw = vld1q_u8(src + (i * 2));
      c = vreinterpretq_u16_u8(vrev16q_u8(raw));

      c32 = vmovl_u16(vget_low_u16(c));
      r = vshlq_n_u32(vandq_u32(c32, mask_r), 8);
      g = vshlq_n_u32(vandq_u32(c32, mask_g), 10);
      b = vshrq_n_u32(vandq_u32(c32, mask_b), 3);
      out = vorrq_u32(alpha, vorrq_u32(r, vorrq_u32(g, b)));
      vst1q_u32(dst + i, out);

      c32 = vmovl_u16(vget_high_u16(c));
      r = vshlq_n_u32(vandq_u32(c32, mask_r), 8);
      g = vshlq_n_u32(vandq_u32(c32, mask_g), 10);
      b = vshrq_n_u32(vandq_u32(c32, mask_b), 3);
      out = vorrq_u32(alpha, vorrq_u32(r, vorrq_u32(g, b)));
      vst1q_u32(dst + i + 4, out);
   }
   return done;
}

/* Strided gather of every 2nd uint32: dst[i] = src[i * 2].
 * Matches tom_render_scanline_hires's seed of tomHiresScratch from the
 * existing Nx row.  Returns source pixels written (multiple of 4). */
static INLINE unsigned tom_scan_neon_hires_gather_n2(
   const uint32_t * VJ_RESTRICT src,
   uint32_t * VJ_RESTRICT dst,
   unsigned n)
{
   unsigned i;
   unsigned done;
   uint32x4x2_t t;

   done = n & ~3u;
   for (i = 0; i < done; i += 4)
   {
      t = vld2q_u32(src + (i * 2));
      vst1q_u32(dst + i, t.val[0]);
   }
   return done;
}

/* 2x box-replicate: dst[2*i] = dst[2*i+1] = src[i].
 * ARMv7 vzipq (struct form), not A64 vzip1/vzip2.  Returns source
 * pixels expanded (multiple of 4). */
static INLINE unsigned tom_scan_neon_hires_expand_n2(
   const uint32_t * VJ_RESTRICT src,
   uint32_t * VJ_RESTRICT dst,
   unsigned n)
{
   unsigned i;
   unsigned done;
   uint32x4_t a;
   uint32x4x2_t z;

   done = n & ~3u;
   for (i = 0; i < done; i += 4)
   {
      a = vld1q_u32(src + i);
      z = vzipq_u32(a, a);
      vst1q_u32(dst + (i * 2), z.val[0]);
      vst1q_u32(dst + (i * 2) + 4, z.val[1]);
   }
   return done;
}

#endif /* BLITTER_SIMD_HAVE_NEON */

#endif /* TOM_SCAN_SIMD_NEON_H */
