/*
 * src/jerry/axistune.h -- per-axis input tuning: dead zone, offset,
 * response exponent (#439).
 *
 * Pure and host-testable, like quadrature.h next door: no dependency on
 * any emulator header, no Jaguar types, integer arithmetic only.
 *
 * ONE LAYER, DERIVED FROM THE TWO SHIPPED CONSUMERS
 * =================================================
 * #439 is deliberately last in #428 so the layer is shaped by real
 * consumers rather than guessed.  The two that exist are:
 *
 *   the ST/Amiga mouse (#429)  two RELATIVE axes, host units per poll
 *   the Tempest rotary (#436)  one RELATIVE axis, host units per poll
 *
 * Both reach the machine through exactly one call --
 * QuadFeed(&q, delta, scale_q8) from InputDevFeed() -- so the tuning has
 * exactly one insertion point, and #437's analog/driving axes will arrive
 * at the same one.  There are no per-device copies of this arithmetic and
 * there must not be.
 *
 * BOTH CONSUMERS ARE RELATIVE, AND THAT DECIDES THE SEMANTICS
 * ==========================================================
 * DEAD ZONE IS A GATE, NOT A RE-BASE.  On an absolute axis a dead zone is
 * conventionally re-based (the dead-zone edge is remapped to zero) so the
 * response has no step at the boundary.  On a RELATIVE axis there is no
 * centre to re-base toward: the quantity is a speed, and a dead zone is a
 * noise gate on that speed.  Re-basing would subtract a constant from
 * every sample, systematically shortening slow motion -- on a device the
 * game integrates into a position, that is pointer drift versus the hand,
 * which is worse than the boundary step it would remove.  So the gate is
 * gate-only: below the threshold the sample is dropped, above it the
 * sample passes at full magnitude.
 *
 * THE ABSOLUTE-AXIS ANSWER (#437), now that the consumer exists
 * =============================================================
 * The analog / driving controller (TR10 bank-switching, inputdev.c) is an
 * ABSOLUTE consumer: the quantity is a stick POSITION, not a speed, and
 * the answer deferred above is now decided the other way around --
 * AxisTuneApplyAbs() RE-BASES its dead zone.  The drift argument that
 * forbade re-basing on a relative axis does not exist here: nothing
 * integrates a position, so subtracting the dead-zone edge cannot walk a
 * pointer away from the hand.  What a GATE would cost on a position is a
 * step: as the stick crosses the edge the output would jump from centre
 * straight to the edge value, which a game turns into a steering snap.
 * So the surviving magnitude is remapped (mag - dz) * range / (range -
 * dz): continuous at the edge, and full deflection still reads full
 * scale.
 *
 * THE IDENTITY RULE DIFFERS TOO, deliberately.  On the relative path
 * raw == 0 means "no sample this poll" and must stay 0 under every tune
 * (the #474 lesson, spelled out in AxisTuneApply).  On an absolute axis
 * raw == 0 IS a sample -- the stick resting at centre -- so the rule
 * becomes: dead zone and exponent are magnitude-symmetric and must FIX
 * the centre (raw 0 -> out 0 for every dz / e), and OFFSET ALONE may move
 * it, because cancelling a mis-centred rest position is offset's entire
 * job.  There is consequently no raw == 0 early-out in the absolute
 * path: with a non-zero offset, a centred stick must read -offset.
 *
 * The exponent's reference is the caller's `range` (full deflection),
 * not AXISTUNE_REF: an absolute axis has a natural full scale and the
 * curve's fixed point belongs there, so full deflection is never
 * attenuated and e > 1 buys finer control near centre.  The curve comes
 * back folded into the returned position rather than as a gain -- there
 * is no QuadFeed carry downstream to keep a fraction alive, and a
 * position quantised to the device's own 8-bit ADC step loses nothing.
 *
 * OFFSET IS A PER-SAMPLE BIAS, subtracted before the gate.  A real mouse
 * reports exactly zero at rest and is unaffected by any offset.  The case
 * this exists for is a frontend that routes something else to the port's
 * relative axis -- an analog stick mapped to RETRO_DEVICE_MOUSE is the
 * common one -- where a mis-centred source reports a small constant delta
 * forever, i.e. pointer drift with the hand off the controls.  Subtracting
 * the measured bias cancels it exactly.  It runs BEFORE the gate so a
 * cancelled bias does not eat the gate's budget.
 *
 * THE RESPONSE EXPONENT AND WHY ITS REFERENCE IS 64
 * ================================================
 * A power curve needs a reference magnitude: `out = REF * (in/REF)^e` is
 * the only form with a fixed point, and at in == REF the output is REF for
 * every exponent, so REF is "the speed the curve does not change".  Below
 * it, e > 1 attenuates (finer control); above it, the input passes through
 * unchanged.
 *
 * REF is NOT a taste constant invented here.  It is QUAD_MAX_BACKLOG,
 * quadrature.h's own saturation point: that header already establishes 64
 * Gray states as the most the relative path will ever carry from a single
 * feed, because a poll-rate-clocked encoder drains one state per poll and
 * everything past 64 is clamped away.  64 host units per poll at unity
 * sensitivity is therefore the top of the range this path can express, and
 * anchoring the curve anywhere else would be the guess.  AXISTUNE_REF is
 * asserted equal to QUAD_MAX_BACKLOG in test/test_axistune.c so the two
 * cannot drift apart.
 *
 * Consequence worth stating plainly, because a user will hit it: an
 * exponent above 1.0 makes ordinary movement SLOWER, since ordinary
 * movement is well below 64 units per poll.  That is what an expo curve
 * is; the paired sensitivity option is how you get the top speed back.
 * The two are different controls and the option text says so.
 *
 * THE CURVE COMES OUT AS A GAIN, NOT AS A DELTA
 * =============================================
 * AxisTuneApply() returns the gated delta in raw units and reports the
 * curve separately as a Q8 gain, instead of returning a curved delta.
 * That is not a stylistic choice -- it is the only form that keeps small
 * movement alive.  A curved delta has to be an integer, and at e = 2.0 a
 * 4-unit sample curves to 4 * 4/64 = 0.25 units, which truncates to zero:
 * the whole low end of the curve would quantise away, which on a mouse
 * reads as a dead device.  Handed back as a gain, the curve multiplies
 * into the scale QuadFeed() already applies, and QuadFeed's Q8 `frac`
 * carry -- which exists for exactly this reason, see quadrature.h --
 * accumulates the fraction across polls instead of discarding it.
 *
 * The gain is floored at 1 rather than 0, so the DEAD ZONE IS THE ONLY
 * THING THAT EVER DROPS A SAMPLE: even the bottom of the steepest curve
 * leaves a surviving sample moving, just very slowly.  See the comment at
 * the floor in axistune.c for why that matters at high exponents.
 *
 * DEFAULTS ARE THE IDENTITY, AND THAT IS ENFORCED STRUCTURALLY
 * ===========================================================
 * A tune whose three fields are all "off" makes AxisTuneApply() return its
 * argument and a gain of 256, by an early return placed before any
 * arithmetic.  A user who never opens the menu therefore gets the
 * pre-#439 path byte for byte; test/test_axistune.c proves it by sweeping
 * the entire int16 input range.
 *
 * A ZERO-FILLED axis_tune IS THAT IDENTITY.  This matters because
 * InputDevShutdown() memsets its port array (iOS cannot dlclose a core,
 * so every static has to return to its load-time value), and a zeroed
 * exponent field would otherwise mean e = 0.0 -- a degenerate curve on
 * every analog axis after a reload, visible on exactly one platform.
 * Hence exponent_q8 <= 0 is *defined* as linear here, the same defensive
 * shape as inputdev.c's existing `if (p->scale_q8 <= 0)` guard, rather
 * than left to callers to re-establish.
 */

#ifndef __AXISTUNE_H__
#define __AXISTUNE_H__

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Reference magnitude for the response curve, in raw host units per poll.
 * Equal to QUAD_MAX_BACKLOG -- see the header comment above for why that
 * is the defensible anchor and not a taste constant. */
#define AXISTUNE_REF 64

/* Ceiling on the reported gain.  Reached only by sub-linear exponents,
 * whose gain grows without bound as the magnitude approaches zero; 4096
 * (16x) is InputDevSetScale()'s own ceiling, and the product of the two
 * is clamped there as well so QuadFeed()'s overflow argument -- written
 * against a 4096 scale -- still holds. */
#define AXISTUNE_MAX_GAIN 4096

typedef struct
{
   int32_t deadzone;      /* raw units, >= 0; 0 == off                    */
   int32_t offset;        /* raw units, signed; 0 == off                  */
   int32_t exponent_q8;   /* Q8; 256 == linear; <= 0 also reads as linear */
} axis_tune;

/* Identity: dead zone off, offset off, linear response. */
void AxisTuneReset(axis_tune *t);

/* Store clamped values.  The inputs come from core-option strings, which
 * a user can hand-edit to anything, so the clamp lives here rather than
 * at each call site. */
void AxisTuneSet(axis_tune *t, int32_t deadzone, int32_t offset,
                 int32_t exponent_q8);

/* Non-zero when this tune cannot change any sample. */
int AxisTuneIsIdentity(const axis_tune *t);

/* Apply offset and dead zone to `raw`, returning the surviving delta in
 * raw units (0 when the sample is gated), and write the response curve's
 * Q8 gain for that magnitude to *gain_q8 (256 == unity).  `gain_q8` may
 * be NULL.  The caller multiplies the gain into its own Q8 sensitivity;
 * see "THE CURVE COMES OUT AS A GAIN" above for why it is not folded into
 * the returned delta. */
int32_t AxisTuneApply(const axis_tune *t, int32_t raw, int32_t *gain_q8);

/* Absolute-axis variant (#437) -- see "THE ABSOLUTE-AXIS ANSWER" above.
 * `raw` is a position in [-range, +range] with 0 at centre; the return is
 * the tuned position in the same domain, clamped.  Offset is subtracted
 * first (host orientation -- the caller applies any device-specific
 * inversion to the RESULT, as inputdev_feed_axis does on the relative
 * path); the dead zone re-bases; the exponent's fixed point is `range`.
 * dz >= range degenerates to "everything gated", never a divide by
 * zero. */
int32_t AxisTuneApplyAbs(const axis_tune *t, int32_t raw, int32_t range);

#ifdef __cplusplus
}
#endif

#endif /* __AXISTUNE_H__ */
