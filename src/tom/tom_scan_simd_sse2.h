/*
 * TOM scanline converters — x86 SSE2 implementation.
 *
 * Guarded by BLITTER_SIMD_HAVE_SSE2 from blitter_simd_arch.h (capability,
 * not a new BUILD_AXES flag).  Port of tom_scan_simd_neon.h; the
 * semantics comments there are the authority.  Kernels use only SSE2
 * (emmintrin.h) — no SSSE3 pshufb, no SSE4 — because the libretro
 * buildbot x86 baseline is SSE2.  Included from tom.c; keep this file
 * C89 (vars at top of each block).
 *
 * SSE2 implies x86, which is always little-endian, so the u32-lane
 * tricks below (24bpp field extraction from a raw dword load) can
 * assume byte 0 of memory is bits 7:0 of the lane.
 */

#ifndef TOM_SCAN_SIMD_SSE2_H
#define TOM_SCAN_SIMD_SSE2_H

#include "blitter_simd_arch.h"

#if defined(BLITTER_SIMD_HAVE_SSE2)

#include <emmintrin.h>
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

/* Byte-swap each u16 lane: the vrev16q_u8 of SSE2. */
#define TOM_SCAN_SSE2_BSWAP16(v) \
   _mm_or_si128(_mm_slli_epi16((v), 8), _mm_srli_epi16((v), 8))

/* 8 BE u16 pixels -> XRGB8888 via (color >> 1), matching
 * tom_render_16bpp_direct_scanline.  Returns pixels written (multiple of 8). */
static INLINE unsigned tom_scan_sse2_16bpp_direct(
   const uint8_t * VJ_RESTRICT src,
   uint32_t * VJ_RESTRICT dst,
   unsigned n)
{
   unsigned i;
   unsigned done;
   __m128i raw;
   __m128i pix;
   __m128i zero;

   done = n & ~7u;
   zero = _mm_setzero_si128();
   for (i = 0; i < done; i += 8)
   {
      raw = _mm_loadu_si128((const __m128i *)(src + (i * 2)));
      pix = _mm_srli_epi16(TOM_SCAN_SSE2_BSWAP16(raw), 1);
      _mm_storeu_si128((__m128i *)(dst + i), _mm_unpacklo_epi16(pix, zero));
      _mm_storeu_si128((__m128i *)(dst + i + 4), _mm_unpackhi_epi16(pix, zero));
   }
   return done;
}

/* 4 Jaguar 24bpp pixels (G,R,pad,B) per step -> XRGB8888
 * 0xFF000000 | (r << 16) | (g << 8) | b.  Returns pixels written (multiple of 4).
 *
 * No vld4 deinterleave here: on little-endian x86 a raw dword load puts
 * G in bits 7:0, R in 15:8, pad in 23:16, B in 31:24, so per lane
 *    out = 0xFF000000 | ((x & 0xFFFF) << 8) | (x >> 24)
 * moves G to 15:8, R to 23:16 and B to 7:0 in three ops. */
static INLINE unsigned tom_scan_sse2_24bpp(
   const uint8_t * VJ_RESTRICT src,
   uint32_t * VJ_RESTRICT dst,
   unsigned n)
{
   unsigned i;
   unsigned done;
   __m128i x;
   __m128i out;
   __m128i alpha;
   __m128i mask_gr;

   done = n & ~3u;
   alpha = _mm_set1_epi32((int)0xFF000000u);
   mask_gr = _mm_set1_epi32(0x0000FFFF);
   for (i = 0; i < done; i += 4)
   {
      x = _mm_loadu_si128((const __m128i *)(src + (i * 4)));
      out = _mm_or_si128(alpha,
            _mm_or_si128(_mm_slli_epi32(_mm_and_si128(x, mask_gr), 8),
                         _mm_srli_epi32(x, 24)));
      _mm_storeu_si128((__m128i *)(dst + i), out);
   }
   return done;
}

/* 8 Jaguar RGB16 pixels (RBG 556: RRRRR BBBBB GGGGGG) -> XRGB8888, bit-
 * identical to RGB16ToRGB32[] filled by TOMFillLookupTables:
 *   0xFF000000 | ((c & 0xF800) << 8) | ((c & 0x003F) << 10) | ((c & 0x07C0) >> 3)
 * Returns pixels written (multiple of 8). */
static INLINE unsigned tom_scan_sse2_16bpp_rgb(
   const uint8_t * VJ_RESTRICT src,
   uint32_t * VJ_RESTRICT dst,
   unsigned n)
{
   unsigned i;
   unsigned done;
   __m128i raw;
   __m128i c;
   __m128i c32;
   __m128i r;
   __m128i g;
   __m128i b;
   __m128i out;
   __m128i zero;
   __m128i alpha;
   __m128i mask_r;
   __m128i mask_g;
   __m128i mask_b;

   done = n & ~7u;
   zero = _mm_setzero_si128();
   alpha = _mm_set1_epi32((int)0xFF000000u);
   mask_r = _mm_set1_epi32(0xF800);
   mask_g = _mm_set1_epi32(0x003F);
   mask_b = _mm_set1_epi32(0x07C0);
   for (i = 0; i < done; i += 8)
   {
      raw = _mm_loadu_si128((const __m128i *)(src + (i * 2)));
      c = TOM_SCAN_SSE2_BSWAP16(raw);

      c32 = _mm_unpacklo_epi16(c, zero);
      r = _mm_slli_epi32(_mm_and_si128(c32, mask_r), 8);
      g = _mm_slli_epi32(_mm_and_si128(c32, mask_g), 10);
      b = _mm_srli_epi32(_mm_and_si128(c32, mask_b), 3);
      out = _mm_or_si128(alpha, _mm_or_si128(r, _mm_or_si128(g, b)));
      _mm_storeu_si128((__m128i *)(dst + i), out);

      c32 = _mm_unpackhi_epi16(c, zero);
      r = _mm_slli_epi32(_mm_and_si128(c32, mask_r), 8);
      g = _mm_slli_epi32(_mm_and_si128(c32, mask_g), 10);
      b = _mm_srli_epi32(_mm_and_si128(c32, mask_b), 3);
      out = _mm_or_si128(alpha, _mm_or_si128(r, _mm_or_si128(g, b)));
      _mm_storeu_si128((__m128i *)(dst + i + 4), out);
   }
   return done;
}

/* Strided gather of every 2nd uint32: dst[i] = src[i * 2].
 * Matches tom_render_scanline_hires's seed of tomHiresScratch from the
 * existing Nx row.  _mm_shuffle_ps(a, b, 2,0,2,0) is the vld2q lane-0
 * gather (bit moves only — no FP arithmetic, so no denormal hazard).
 * Returns source pixels written (multiple of 4). */
static INLINE unsigned tom_scan_sse2_hires_gather_n2(
   const uint32_t * VJ_RESTRICT src,
   uint32_t * VJ_RESTRICT dst,
   unsigned n)
{
   unsigned i;
   unsigned done;
   __m128i a;
   __m128i b;
   __m128i even;

   done = n & ~3u;
   for (i = 0; i < done; i += 4)
   {
      a = _mm_loadu_si128((const __m128i *)(src + (i * 2)));
      b = _mm_loadu_si128((const __m128i *)(src + (i * 2) + 4));
      even = _mm_castps_si128(_mm_shuffle_ps(
            _mm_castsi128_ps(a), _mm_castsi128_ps(b),
            _MM_SHUFFLE(2, 0, 2, 0)));
      _mm_storeu_si128((__m128i *)(dst + i), even);
   }
   return done;
}

/* 2x box-replicate: dst[2*i] = dst[2*i+1] = src[i].
 * unpacklo/unpackhi_epi32(a, a) is the vzipq pair.  Returns source
 * pixels expanded (multiple of 4). */
static INLINE unsigned tom_scan_sse2_hires_expand_n2(
   const uint32_t * VJ_RESTRICT src,
   uint32_t * VJ_RESTRICT dst,
   unsigned n)
{
   unsigned i;
   unsigned done;
   __m128i a;

   done = n & ~3u;
   for (i = 0; i < done; i += 4)
   {
      a = _mm_loadu_si128((const __m128i *)(src + i));
      _mm_storeu_si128((__m128i *)(dst + (i * 2)), _mm_unpacklo_epi32(a, a));
      _mm_storeu_si128((__m128i *)(dst + (i * 2) + 4), _mm_unpackhi_epi32(a, a));
   }
   return done;
}

#endif /* BLITTER_SIMD_HAVE_SSE2 */

#endif /* TOM_SCAN_SIMD_SSE2_H */
