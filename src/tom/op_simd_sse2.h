#ifndef OP_SIMD_SSE2_H
#define OP_SIMD_SSE2_H

/*
 * Object Processor 16bpp / 24bpp phrase store — x86 SSE2 implementation
 *
 * Included by op.c. Capability comes from blitter_simd_arch.h
 * (BLITTER_SIMD_HAVE_SSE2): baseline on x86-64, __SSE2__-gated on i386,
 * /arch:SSE2-gated on 32-bit MSVC — same policy as the blitter kernels,
 * so a plain i386 build stays scalar instead of taking intrinsics its
 * target cannot execute.
 *
 * SSE2 only (emmintrin.h). Do not add SSSE3/SSE4 ops here (pshufb,
 * blendv, ptest) — the libretro buildbot x86 baseline is SSE2.
 *
 * Port of op_simd_neon.h; the semantics comments there are the
 * authority. The 8-byte phrase lives in the low half of an __m128i
 * (movq load/store); the high half is ignored.
 */

#include "blitter_simd_arch.h"

#if defined(BLITTER_SIMD_HAVE_SSE2)

#include <stdint.h>
#include <boolean.h>
#include <emmintrin.h>

#if defined(_MSC_VER)
#  define OP_SIMD_INLINE __forceinline
#elif defined(__GNUC__) || defined(__clang__)
#  define OP_SIMD_INLINE inline __attribute__((always_inline))
#else
#  define OP_SIMD_INLINE INLINE
#endif

/*
 * Store one 16bpp phrase (4 pixels / 8 bytes) into LBUF.
 *
 * Byte order matches OPProcessFixedBitmap's scalar walk: bitsHi then
 * bitsLo per pixel through a uint8_t* with lbufDelta == +2, which is
 * pixels[63:56], [55:48], …, [7:0]. The explicit byte extract is host-
 * endian independent, exactly as in the NEON port.
 *
 * flagTRANS: skip a u16 lane when both bytes are zero (same test as
 * (bitsLo | bitsHi) == 0) — _mm_cmpeq_epi16 against zero is lane-order
 * agnostic, so the swapped byte order inside the lane doesn't matter.
 * Keep the existing LBUF bytes via and/andnot/or select (the SSE2
 * spelling of vbsl). Opaque: straight 8-byte store. Caller has already
 * gated RMW, REFLECT, shadow-FB hooks, and LBUF bounds.
 */
static OP_SIMD_INLINE void op_store_phrase_16bpp_sse2(
      uint8_t *lbuf, uint64_t pixels, bool flagTRANS)
{
   uint8_t bytes[8];
   __m128i src;
   __m128i cur;
   __m128i mask;
   __m128i zero;

   bytes[0] = (uint8_t)(pixels >> 56);
   bytes[1] = (uint8_t)(pixels >> 48);
   bytes[2] = (uint8_t)(pixels >> 40);
   bytes[3] = (uint8_t)(pixels >> 32);
   bytes[4] = (uint8_t)(pixels >> 24);
   bytes[5] = (uint8_t)(pixels >> 16);
   bytes[6] = (uint8_t)(pixels >> 8);
   bytes[7] = (uint8_t)(pixels);

   src = _mm_loadl_epi64((const __m128i *)bytes);
   if (!flagTRANS)
   {
      _mm_storel_epi64((__m128i *)lbuf, src);
      return;
   }

   zero = _mm_setzero_si128();
   mask = _mm_cmpeq_epi16(src, zero);
   cur = _mm_loadl_epi64((const __m128i *)lbuf);
   src = _mm_or_si128(_mm_and_si128(mask, cur),
                      _mm_andnot_si128(mask, src));
   _mm_storel_epi64((__m128i *)lbuf, src);
}

/*
 * Store one 24bpp phrase (2 pixels / 8 bytes) into LBUF.
 *
 * Byte order matches OPProcessFixedBitmap's scalar walk: bits3..bits0
 * per pixel through a uint8_t* with lbufDelta == +4. The 24bpp loop
 * never applies RMW or shadow-FB hooks (RMW is 16bpp CRY-only); the
 * caller still gates REFLECT, partial firstPix/i, and LBUF bounds.
 *
 * flagTRANS: skip a u32 lane when all four bytes are zero (same test
 * as (bits3 | bits2 | bits1 | bits0) == 0). Opaque: straight 8-byte
 * store.
 */
static OP_SIMD_INLINE void op_store_phrase_24bpp_sse2(
      uint8_t *lbuf, uint64_t pixels, bool flagTRANS)
{
   uint8_t bytes[8];
   __m128i src;
   __m128i cur;
   __m128i mask;
   __m128i zero;

   bytes[0] = (uint8_t)(pixels >> 56);
   bytes[1] = (uint8_t)(pixels >> 48);
   bytes[2] = (uint8_t)(pixels >> 40);
   bytes[3] = (uint8_t)(pixels >> 32);
   bytes[4] = (uint8_t)(pixels >> 24);
   bytes[5] = (uint8_t)(pixels >> 16);
   bytes[6] = (uint8_t)(pixels >> 8);
   bytes[7] = (uint8_t)(pixels);

   src = _mm_loadl_epi64((const __m128i *)bytes);
   if (!flagTRANS)
   {
      _mm_storel_epi64((__m128i *)lbuf, src);
      return;
   }

   zero = _mm_setzero_si128();
   mask = _mm_cmpeq_epi32(src, zero);
   cur = _mm_loadl_epi64((const __m128i *)lbuf);
   src = _mm_or_si128(_mm_and_si128(mask, cur),
                      _mm_andnot_si128(mask, src));
   _mm_storel_epi64((__m128i *)lbuf, src);
}

#endif /* BLITTER_SIMD_HAVE_SSE2 */

#endif /* OP_SIMD_SSE2_H */
