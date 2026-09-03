/*
 * Blitter SIMD ops -- out-of-line scalar vtable.
 *
 * The implementations themselves live in blitter_simd_scalar.h so that
 * blitter.c can inline them (see blitter_simd.h).  This file only
 * provides the function-pointer table, which test/test_blitter_simd.c
 * uses to exercise the very same code the core runs.
 */

#include "blitter_simd_arch.h"

/* Compile this file at all?  Always yes for Makefile.common and
 * ndk-build, which select exactly one arch file and pass a matching
 * -DBLITTER_SIMD_SCALAR -- BLITTER_SIMD_BUILD_* is unconditionally true
 * without BLITTER_SIMD_AUTODETECT, so the guard can never blank out
 * the file they chose.  Under autodetect (the SwiftPM build, which
 * hands every arch file to the compiler at once) exactly one of the
 * three survives, and it is the same one blitter_simd.h selects for
 * inlining -- both read blitter_simd_arch.h. */
#if defined(BLITTER_SIMD_BUILD_SCALAR)

/* Select our own arch before blitter_simd.h picks one.  This file IS
 * the scalar implementation, so it must get the scalar inline set even when
 * compiled standalone without the Makefile's -DBLITTER_SIMD_SCALAR --
 * as .github/workflows/c-cpp.yml does when it builds
 * test_blitter_simd by hand.  Without this the header chain pulled in
 * the scalar set as well and the two collided. */
#ifndef BLITTER_SIMD_SCALAR
#define BLITTER_SIMD_SCALAR 1
#endif

#include "blitter_simd.h"
#include "blitter_simd_scalar.h"

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

#endif /* BLITTER_SIMD_BUILD_SCALAR */
