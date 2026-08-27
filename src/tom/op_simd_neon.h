#ifndef OP_SIMD_NEON_H
#define OP_SIMD_NEON_H

/*
 * Object Processor 16bpp phrase store — ARM NEON implementation
 *
 * Included by op.c. Capability comes from blitter_simd_arch.h
 * (BLITTER_SIMD_HAVE_NEON) so this stays on the same rpi2+/AArch64
 * set as the blitter kernels and stays scalar on rpi0/rpi1.
 *
 * Intrinsics are the ARMv7 Advanced-SIMD subset (vld1/vst1/vbsl/vceq)
 * so the file remains portable to 32-bit rpi2/rpi3. Do not add
 * AArch64-only ops here (vaddv*, vqtbl1q*, vzip1*, vrbit*, f64).
 */

#include "blitter_simd_arch.h"

#if defined(BLITTER_SIMD_HAVE_NEON)

#include <stdint.h>
#include <boolean.h>
#include <arm_neon.h>

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
 * pixels[63:56], [55:48], …, [7:0]. The explicit extract is host-
 * endian independent (vcreate_u64 + vst1 would reverse on LE).
 *
 * flagTRANS: skip a u16 lane when both bytes are zero (same test as
 * (bitsLo | bitsHi) == 0), keeping the existing LBUF bytes via vbsl.
 * Opaque: straight 8-byte store. Caller has already gated RMW,
 * REFLECT, shadow-FB hooks, and LBUF bounds.
 */
static OP_SIMD_INLINE void op_store_phrase_16bpp_neon(
      uint8_t *lbuf, uint64_t pixels, bool flagTRANS)
{
   uint8_t bytes[8];
   uint8x8_t src;
   uint8x8_t cur;
   uint8x8_t mask;
   uint16x4_t pix;
   uint16x4_t zero;
   uint16x4_t tmask;

   bytes[0] = (uint8_t)(pixels >> 56);
   bytes[1] = (uint8_t)(pixels >> 48);
   bytes[2] = (uint8_t)(pixels >> 40);
   bytes[3] = (uint8_t)(pixels >> 32);
   bytes[4] = (uint8_t)(pixels >> 24);
   bytes[5] = (uint8_t)(pixels >> 16);
   bytes[6] = (uint8_t)(pixels >> 8);
   bytes[7] = (uint8_t)(pixels);

   src = vld1_u8(bytes);
   if (!flagTRANS)
   {
      vst1_u8(lbuf, src);
      return;
   }

   pix = vreinterpret_u16_u8(src);
   zero = vdup_n_u16(0);
   tmask = vceq_u16(pix, zero);
   mask = vreinterpret_u8_u16(tmask);
   cur = vld1_u8(lbuf);
   src = vbsl_u8(mask, cur, src);
   vst1_u8(lbuf, src);
}

#endif /* BLITTER_SIMD_HAVE_NEON */

#endif /* OP_SIMD_NEON_H */
