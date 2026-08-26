/*
 * Blitter SIMD capability detection.
 *
 * Answers one question: which SIMD instruction sets does *this*
 * compilation actually have?  Split out of blitter_simd.h rather than
 * spelled inline there because two different places need the answer and
 * they must never disagree:
 *
 *   1. blitter_simd.h's BLITTER_SIMD_AUTODETECT block, which picks the
 *      inline implementation blitter.c gets.
 *   2. Each blitter_simd_<arch>.c's top-of-file guard, which decides
 *      whether that file contributes the blitter_simd_ops vtable.
 *
 * If those two ever answered differently the build would either define
 * blitter_simd_ops twice or not at all, so they read the same macros
 * from here.  The .c files cannot get the answer from blitter_simd.h
 * itself: they have to select their implementation *before* including
 * it (blitter_simd.h dispatches on BLITTER_SIMD_<ARCH>, and including
 * it first would pull in the scalar inline set and collide with the
 * arch one).
 *
 * These test CAPABILITY, not architecture.  SSE2 is baseline on x86-64
 * but optional on 32-bit x86 (GCC/Clang need -msse2 or an -march that
 * implies it; MSVC needs /arch:SSE2), and NEON is optional on ARMv7-A.
 * Makefile.common can *add* -msse2 when it selects the SSE2 source --
 * see its "required on i686 / gcc -m32" rule -- but a build that selects
 * by guard has no such lever and must take the target as it finds it.
 * Asking "is the feature on?" rather than "what arch is this?" is what
 * keeps a plain i386 build off the SSE2 path instead of handing
 * blitter_simd_sse2.c intrinsics its target cannot compile.
 */

#ifndef BLITTER_SIMD_ARCH_H
#define BLITTER_SIMD_ARCH_H

/* NEON.  __ARM_NEON is the ACLE spelling every GCC/Clang emits when NEON
 * codegen is enabled; _M_ARM64 is MSVC's, where NEON is mandatory. */
#if defined(__ARM_NEON) || defined(__ARM_NEON__) || defined(__aarch64__) \
 || defined(_M_ARM64) || defined(_M_ARM64EC)
#  define BLITTER_SIMD_HAVE_NEON 1
#endif

/* SSE2.  The 64-bit spellings need no further test; the 32-bit ones do. */
#if defined(__x86_64__) || defined(_M_X64) \
 || (defined(__i386__) && defined(__SSE2__)) \
 || (defined(_M_IX86) && defined(_M_IX86_FP) && _M_IX86_FP == 2)
#  define BLITTER_SIMD_HAVE_SSE2 1
#endif

/* Whether a blitter_simd_<arch>.c should compile its body at all.
 *
 * Without BLITTER_SIMD_AUTODETECT these are all true, so Makefile.common
 * and ndk-build -- which compile exactly ONE arch file and pass a
 * matching -DBLITTER_SIMD_<ARCH> -- are completely unaffected: the guard
 * can never blank out the one file they chose.
 *
 * With it, a build that hands every arch file to the compiler at once
 * (the Provenance SwiftPM manifest) gets exactly one non-empty file, and
 * it is the same one blitter_simd.h selects for inlining.  Scalar is the
 * fall-through, so such a build always links rather than failing to find
 * blitter_simd_ops on a target with neither NEON nor SSE2.
 */
#if !defined(BLITTER_SIMD_AUTODETECT) || defined(BLITTER_SIMD_HAVE_NEON)
#  define BLITTER_SIMD_BUILD_NEON 1
#endif

#if !defined(BLITTER_SIMD_AUTODETECT) || defined(BLITTER_SIMD_HAVE_SSE2)
#  define BLITTER_SIMD_BUILD_SSE2 1
#endif

#if !defined(BLITTER_SIMD_AUTODETECT) \
 || (!defined(BLITTER_SIMD_HAVE_NEON) && !defined(BLITTER_SIMD_HAVE_SSE2))
#  define BLITTER_SIMD_BUILD_SCALAR 1
#endif

#endif /* BLITTER_SIMD_ARCH_H */
