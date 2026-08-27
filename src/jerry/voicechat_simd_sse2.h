#ifndef VOICECHAT_SIMD_SSE2_H
#define VOICECHAT_SIMD_SSE2_H

/*
 * VoiceChatMixInto SSE2 saturating mix — 4 interleaved stereo pairs.
 *
 * Guarded by BLITTER_SIMD_HAVE_SSE2 (src/tom/blitter_simd_arch.h) at the
 * include site in voicechat.c, exactly like the NEON header.  SSE2 only
 * (emmintrin.h) — the buildbot x86 baseline.  C89-lint skips this header;
 * voicechat.c stays C89.
 *
 * mixed must already fit in int16.  _mm_adds_epi16 saturates each lane
 * to [-32768, 32767], matching the scalar (int)add + clamp path when
 * both addends are int16.  If mixed were truncated from a wider sum,
 * the results would diverge (e.g. -32768 + 40000 = 7232 scalar vs
 * sat-add of a truncated mixed).
 */

#include <stdint.h>
#include <emmintrin.h>

static void voicechat_mix_sat4(int16_t *stereo, int16_t mixed)
{
   __m128i vm;
   __m128i vbuf;
   __m128i vout;

   vm = _mm_set1_epi16(mixed);
   vbuf = _mm_loadu_si128((const __m128i *)stereo);
   vout = _mm_adds_epi16(vbuf, vm);
   _mm_storeu_si128((__m128i *)stereo, vout);
}

#endif /* VOICECHAT_SIMD_SSE2_H */
