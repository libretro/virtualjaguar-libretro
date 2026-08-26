/*
 * SIMD-accelerated blitter operations for Virtual Jaguar
 *
 * Provides architecture-specific implementations of the blitter's
 * hottest data-path operations. Only one implementation is active per
 * build; the arch is selected in Makefile.common, which compiles the
 * matching blitter_simd_<arch>.c *and* passes -DBLITTER_SIMD_<ARCH>.
 *
 * The implementations live in blitter_simd_<arch>.h as static inline
 * functions so BlitterMidsummer2 can inline them.  That matters: these
 * are a handful of SIMD instructions each, called up to six times per
 * inner-loop iteration, and Tempest 2000 runs ~99k inner iterations per
 * frame.  Reaching them through the blitter_simd_ops function-pointer
 * table (a different translation unit, and no LTO on any desktop
 * target) cost ~21% of accurate-blitter runtime in indirect-call and
 * argument-marshalling overhead, and it also defeated the per-call-site
 * specialisation that ADDARRAY's BLITTER_ALWAYS_INLINE exists to enable
 * -- sat/eightbit/hicinh are compile-time constants at those sites.
 *
 * blitter_simd_ops still exists, and each arch .c file builds it out of
 * thin wrappers around the very same inline functions, so
 * test/test_blitter_simd.c validates exactly the code the core runs.
 */

#ifndef BLITTER_SIMD_H
#define BLITTER_SIMD_H

#include <stdint.h>
#include <boolean.h>

/* Portable always-inline, spelled to include the inline keyword itself
 * (MSVC's __forceinline IS the inline keyword for that compiler). */
#if defined(_MSC_VER)
#  define BLITTER_SIMD_INLINE __forceinline
#elif defined(__GNUC__) || defined(__clang__)
#  define BLITTER_SIMD_INLINE inline __attribute__((always_inline))
#else
#  define BLITTER_SIMD_INLINE inline
#endif

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

   /* ADD16SAT x4: four parallel 16-bit saturating adds with the Jaguar's
    * segmented carry chain (low byte, mid nibble, high nibble).
    *
    * Each lane computes:
    *   q[7:0]  = a[7:0]  + b[7:0]  + cin[i]
    *   q[11:8] = a[11:8] + b[11:8] + carry_from_byte (if !eightbit)
    *   q[15:12]= a[15:12]+ b[15:12]+ carry_from_nib  (if !hicinh)
    *
    * When sat is true, the result saturates to 0x00/0xFF (eightbit) or
    * 0x0000/0xFFFF (16-bit) on overflow.
    *
    * addq[0..3]: output 16-bit results.
    * co[0..3]:   output carry-out bits (preserved between calls in HW).
    * adda[0..3], addb[0..3]: input operands.
    * cin[0..3]:  carry-in per lane.
    * sat, eightbit, hicinh: mode flags (uniform across all 4 lanes). */
   void (*add16sat_x4)(uint16_t *addq, uint8_t *co,
                       const uint16_t *adda, const uint16_t *addb,
                       const uint8_t *cin,
                       bool sat, bool eightbit, bool hicinh);
} blitter_simd_ops_t;

extern const blitter_simd_ops_t blitter_simd_ops;

/* Pull in the selected implementation as static inline functions:
 *   blitter_simd_lfu / _dcomp / _zcomp / _byte_merge / _add16sat_x4
 *
 * Makefile.common defines exactly one of these to match the .c file it
 * added to SOURCES_C.  Builds that don't go through it (ndk-build, and
 * every MSVC target) get scalar, which is what those already selected.
 * A mismatch between the -D and the compiled .c is a hard compile error
 * rather than a silent slow path -- see blitter_simd_<arch>.c.
 *
 * BLITTER_SIMD_AUTODETECT is the opt-in for build systems that compile
 * the arch .c files by guard rather than by selection, and so cannot
 * pass a matching -D.  The Swift Package Manager manifest used by the
 * Provenance frontend is the case this exists for: it hands every
 * blitter_simd_<arch>.c to the compiler at once and lets each file's
 * own top-of-file arch guard blank out the ones that don't apply.
 * Without a -D those builds fell through to the scalar branch below and
 * inlined the scalar ops into BlitterMidsummer2 on hardware that has
 * NEON -- the vtable was NEON, but the vtable is only used by
 * test/test_blitter_simd.c, so nothing caught it.  That is a silent
 * slow path of exactly the kind the paragraph above says cannot happen,
 * and it is why the detection is spelled here, where the real target
 * compiler evaluates it, rather than in the manifest, which is compiled
 * for the host and would get the x86_64 simulator slice wrong.
 *
 * Deliberately opt-in: leaving it undefined keeps ndk-build and every
 * MSVC target on the scalar path they select today, byte for byte. */
#if !defined(BLITTER_SIMD_NEON) && !defined(BLITTER_SIMD_SSE2) \
 && !defined(BLITTER_SIMD_SCALAR) && defined(BLITTER_SIMD_AUTODETECT)
#  if defined(__ARM_NEON) || defined(__ARM_NEON__) || defined(__aarch64__)
#    define BLITTER_SIMD_NEON 1
#  elif defined(__x86_64__) || defined(__i386__) \
     || defined(_M_X64) || defined(_M_IX86)
#    define BLITTER_SIMD_SSE2 1
#  endif
#endif

#if defined(BLITTER_SIMD_NEON)
#  include "blitter_simd_neon.h"
#elif defined(BLITTER_SIMD_SSE2)
#  include "blitter_simd_sse2.h"
#else
#  include "blitter_simd_scalar.h"
#endif

#endif /* BLITTER_SIMD_H */
