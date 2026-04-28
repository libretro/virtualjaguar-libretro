/*
 * SIMD-accelerated blitter operations for Virtual Jaguar
 *
 * Provides architecture-specific implementations of the blitter's
 * hottest data-path operations. Only one implementation file is
 * compiled per build (selected in Makefile.common).
 *
 * Each arch file defines:
 *   const blitter_simd_ops_t blitter_simd_ops = { ... };
 */

#ifndef BLITTER_SIMD_H
#define BLITTER_SIMD_H

#include <stdint.h>
#include <boolean.h>

typedef struct
{
   /* Logic Function Unit: 64-bit truth table over srcd/dstd.
    * lfu_func is a 4-bit selector (0-15). */
   uint64_t (*lfu)(uint64_t srcd, uint64_t dstd, uint8_t lfu_func);

   /* Data Comparator: per-byte equality of patd vs (cmpdst ? dstd : srcd).
    * Returns 8-bit mask, one bit per byte. */
   uint8_t  (*dcomp)(uint64_t patd, uint64_t srcd, uint64_t dstd, bool cmpdst);

   /* Z-buffer Comparator: 4 independent 16-bit comparisons.
    * zmode bits: 0=LT, 1=EQ, 2=GT. Returns 4-bit mask. */
   uint8_t  (*zcomp)(uint64_t srcz, uint64_t dstz, uint8_t zmode);

   /* Byte Mask Merge: select bytes from src or dst based on 16-bit mask.
    * Bits 0-7 control byte 0 (per-bit blend within the lowest byte).
    * Bits 8-14 control bytes 1-7 (whole-byte select, one bit each).
    * Used for both pixel data (ddat/dstd) and Z data (srcz/dstz). */
   uint64_t (*byte_merge)(uint64_t src, uint64_t dst, uint16_t mask);
} blitter_simd_ops_t;

extern const blitter_simd_ops_t blitter_simd_ops;

#endif /* BLITTER_SIMD_H */
