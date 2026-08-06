/*
 * Blitter SIMD ops -- out-of-line neon vtable.
 *
 * The implementations themselves live in blitter_simd_neon.h so that
 * blitter.c can inline them (see blitter_simd.h).  This file only
 * provides the function-pointer table, which test/test_blitter_simd.c
 * uses to exercise the very same code the core runs.
 */

/* Select our own arch before blitter_simd.h picks one.  This file IS
 * the neon implementation, so it must get the neon inline set even when
 * compiled standalone without the Makefile's -DBLITTER_SIMD_NEON --
 * as .github/workflows/c-cpp.yml does when it builds
 * test_blitter_simd by hand.  Without this the header chain pulled in
 * the scalar set as well and the two collided. */
#ifndef BLITTER_SIMD_NEON
#define BLITTER_SIMD_NEON 1
#endif

#include "blitter_simd.h"
#include "blitter_simd_neon.h"

static uint64_t ops_lfu(uint64_t srcd, uint64_t dstd, uint8_t lfu_func)
{
   return blitter_simd_lfu(srcd, dstd, lfu_func);
}

static uint8_t ops_dcomp(uint64_t patd, uint64_t srcd, uint64_t dstd, bool cmpdst)
{
   return blitter_simd_dcomp(patd, srcd, dstd, cmpdst);
}

static uint8_t ops_zcomp(uint64_t srcz, uint64_t dstz, uint8_t zmode)
{
   return blitter_simd_zcomp(srcz, dstz, zmode);
}

static uint64_t ops_byte_merge(uint64_t src, uint64_t dst, uint16_t mask)
{
   return blitter_simd_byte_merge(src, dst, mask);
}

static void ops_add16sat_x4(uint16_t *addq, uint8_t *co,
                            const uint16_t *adda, const uint16_t *addb,
                            const uint8_t *cin,
                            bool sat, bool eightbit, bool hicinh)
{
   blitter_simd_add16sat_x4(addq, co, adda, addb, cin, sat, eightbit, hicinh);
}

const blitter_simd_ops_t blitter_simd_ops = {
   ops_lfu,
   ops_dcomp,
   ops_zcomp,
   ops_byte_merge,
   ops_add16sat_x4
};
