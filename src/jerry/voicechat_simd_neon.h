#ifndef VOICECHAT_SIMD_NEON_H
#define VOICECHAT_SIMD_NEON_H

/*
 * VoiceChatMixInto NEON saturating mix — 4 interleaved stereo pairs.
 *
 * Guarded by BLITTER_SIMD_HAVE_NEON (src/tom/blitter_simd_arch.h).
 * Intrinsics are ARMv7-portable (vdupq_n_s16 / vld1q_s16 / vqaddq_s16 /
 * vst1q_s16).  C89-lint skips this header; voicechat.c stays C89.
 *
 * mixed must already fit in int16.  vqaddq_s16 saturates each lane to
 * [-32768, 32767], matching the scalar (int)add + clamp path when both
 * addends are int16.  If mixed were truncated from a wider sum, the
 * results would diverge (e.g. -32768 + 40000 = 7232 scalar vs sat-add
 * of a truncated mixed).
 */

#include <stdint.h>
#include <arm_neon.h>

static void voicechat_mix_sat4(int16_t *stereo, int16_t mixed)
{
   int16x8_t vm;
   int16x8_t vbuf;
   int16x8_t vout;

   vm = vdupq_n_s16(mixed);
   vbuf = vld1q_s16(stereo);
   vout = vqaddq_s16(vbuf, vm);
   vst1q_s16(stereo, vout);
}

#endif /* VOICECHAT_SIMD_NEON_H */
