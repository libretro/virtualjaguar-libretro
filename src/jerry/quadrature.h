/*
 * src/jerry/quadrature.h -- 2-bit Gray-code quadrature encoder.
 *
 * Pure and host-testable: no dependency on any emulator header, no Jaguar
 * types.  Both the ST/Amiga mouse adapter (#429) and the Tempest rotary
 * (#436) hang a two-phase optical encoder off the same port input lines,
 * and both emit the SAME Gray code -- docs/jaguar-mouse-adapter-mapping.md
 * section 5 derives it from Domin's driver, and the Jaguar Technical
 * Reference V10 specifies it bit-for-bit for the rotary.  One encoder
 * serves both.
 *
 * Phase table (index -> logic levels on the A and B lines):
 *
 *     index | A | B
 *     ------+---+---
 *       0   | 0 | 0
 *       1   | 1 | 0
 *       2   | 1 | 1
 *       3   | 0 | 1
 *
 * Positive direction = index increasing = A leads B.
 * Negative direction = index decreasing = B leads A.
 *
 * For the mouse (mapping doc section 5, from Domin's dirtab_horizontal /
 * dirtab_vertical): A leads B = right on X, down on Y.
 *
 * WHY ONE STATE PER ADVANCE IS THE WHOLE RATE POLICY
 * ==================================================
 * The binding constraint is the game's decoder, not the physical device.
 * A transition of TWO Gray states between consecutive polls indexes the
 * "undetermined" entry of Domin's 16-entry table and is silently
 * discarded -- direction is unrecoverable from it.  So emitting faster
 * than the title polls does not merely lag, it loses motion, and near the
 * threshold it loses motion non-uniformly, which reads as jitter.
 * QuadAdvance() therefore moves at most one state, ever.  Excess motion
 * queues in `backlog` and drains at whatever rate the title polls at,
 * which is correct for every poll rate without having to know it.
 */

#ifndef __QUADRATURE_H__
#define __QUADRATURE_H__

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Backlog ceiling, in Gray-code states.  A poll-rate-clocked encoder can
 * only drain one state per poll (see above), so an unbounded accumulator
 * turns a fast flick into seconds of pointer drift after the hand has
 * stopped.  64 states is ~53 ms of lag at Domin's 1200 Hz poll rate and
 * ~1 s at a VBL-only 60 Hz poll rate -- past which the device is already
 * unusable, so clamping there loses nothing a user wants. */
#define QUAD_MAX_BACKLOG 64

typedef struct
{
   int32_t backlog;   /* pending Gray states, signed; +ve = positive dir */
   int32_t frac;      /* Q8 sensitivity remainder, carried between feeds */
   uint8_t phase;     /* current Gray index 0..3 (table above)           */
} quad_axis;

void QuadReset(quad_axis *q);

/* Add host-relative motion.  `delta` is in raw libretro device units;
 * `scale_q8` is a Q8 multiplier (256 == 1.0) from the sensitivity option.
 * The Q8 remainder is carried so a <1.0 scale decimates smoothly instead
 * of quantising every small movement to zero. */
void QuadFeed(quad_axis *q, int32_t delta, int32_t scale_q8);

/* Advance at most ONE Gray-code state toward draining the backlog.
 * Returns 1 if the phase changed. */
int QuadAdvance(quad_axis *q);

/* Current phase as two levels.  a/b are 0 or 1 -- LOGIC LEVELS, not
 * active-low bit values.  Callers apply the polarity their sink needs:
 * the mouse overlay drives raw $F14000 bits where 0 = asserted, while a
 * joypadNButtons[] slot is non-zero to pull its bit low.  Two conventions
 * in one feature, which is why this returns levels and not bits. */
void QuadLevels(const quad_axis *q, int *a, int *b);

#ifdef __cplusplus
}
#endif

#endif /* __QUADRATURE_H__ */
