/*
 * src/jerry/axistune.c -- per-axis input tuning (#439).
 *
 * See axistune.h for the semantics, for why the dead zone is a gate rather
 * than a re-base, for where AXISTUNE_REF comes from, and for why the
 * response curve is reported as a gain instead of folded into the delta.
 *
 * WHY THIS IS INTEGER-ONLY AND WHY THAT IS NOT OPTIONAL
 * ====================================================
 * The response curve is a fractional power, which is a natural fit for
 * powf().  It is not used, and libm is not linked into this path, because
 * the guardrail tests for the input devices are host-independent FNV
 * digests over a swept input domain (test/tools/joymatrix_identity.c says
 * so in its own header).  A float pow would make those digests depend on
 * the host's libm rounding, so the same core would digest differently on
 * x86-64, arm64 and i686 and the constants would stop meaning anything.
 *
 * The curve is therefore evaluated in Q16 fixed point with the classic
 * identity x^(1/2^k) = k successive square roots, which is exact integer
 * arithmetic and bit-identical everywhere.
 */

#include "axistune.h"

/* Q16 multiply of two values in [0, 1.0].
 *
 * Both operands are at most 65536 (1.0), so the product is at most 2^32 --
 * exactly one past what a uint32 holds.  That single case is a multiply by
 * 1.0, which needs no arithmetic at all, so the guards below are not
 * micro-optimisation: they are what keeps the product in range. */
static uint32_t axistune_mul_q16(uint32_t a, uint32_t b)
{
   if (a == 65536u)
      return b;
   if (b == 65536u)
      return a;
   /* Both are now <= 65535, so a * b <= 4294836225 and the rounding
    * addend cannot carry past uint32. */
   return (uint32_t)(((a * b) + 32768u) >> 16);
}

/* Integer square root of a uint32, bit-by-bit (no float, no libm). */
static uint32_t axistune_isqrt(uint32_t n)
{
   uint32_t rem  = 0;
   uint32_t root = 0;
   int      i;

   for (i = 0; i < 16; i++)
   {
      root <<= 1;
      rem   = (rem << 2) | (n >> 30);
      n   <<= 2;

      if (root < rem)
      {
         root++;
         rem -= root;
         root++;
      }
   }

   return root >> 1;
}

/* Q16 square root of a value in [0, 1.0].  sqrt(x) in Q16 is
 * isqrt(x << 16); the shift is in range precisely because x < 1.0 here,
 * and x == 1.0 is answered without shifting. */
static uint32_t axistune_sqrt_q16(uint32_t x_q16)
{
   if (x_q16 >= 65536u)
      return 65536u;
   return axistune_isqrt(x_q16 << 16);
}

/* base^exp in Q16, for base in [0, 1.0] and exp a non-negative Q8.
 *
 * Integer part by repeated multiply; fractional part by the successive-root
 * identity, one root per Q8 fraction bit.  Eight roots is the whole Q8
 * fraction, so this terminates in a fixed 8 iterations regardless of the
 * exponent. */
static uint32_t axistune_pow_q16(uint32_t base_q16, uint32_t exp_q8)
{
   uint32_t result = 65536u;
   uint32_t root   = base_q16;
   uint32_t ip     = exp_q8 >> 8;
   uint32_t fp     = exp_q8 & 0xFFu;
   uint32_t bit;

   if (base_q16 == 0u)
      return 0u;

   /* base <= 1.0, so each multiply only shrinks the result; eight is far
    * past the point where it has already underflowed to zero, and the
    * option ladder never exceeds 3. */
   if (ip > 8u)
      ip = 8u;

   while (ip > 0u)
   {
      result = axistune_mul_q16(result, base_q16);
      ip--;
   }

   for (bit = 0x80u; bit != 0u; bit >>= 1)
   {
      root = axistune_sqrt_q16(root);
      if ((fp & bit) != 0u)
         result = axistune_mul_q16(result, root);
   }

   return result;
}

void AxisTuneReset(axis_tune *t)
{
   if (!t)
      return;

   t->deadzone    = 0;
   t->offset      = 0;
   t->exponent_q8 = 256;   /* linear */
}

void AxisTuneSet(axis_tune *t, int32_t deadzone, int32_t offset,
                 int32_t exponent_q8)
{
   if (!t)
      return;

   /* A dead zone at or above the curve's reference would gate the entire
    * range the curve describes, so AXISTUNE_REF is the useful ceiling. */
   if (deadzone < 0)
      deadzone = 0;
   if (deadzone > AXISTUNE_REF)
      deadzone = AXISTUNE_REF;

   /* An offset larger than the reference is a source so badly mis-centred
    * that it is saturating the path on its own. */
   if (offset < -AXISTUNE_REF)
      offset = -AXISTUNE_REF;
   if (offset > AXISTUNE_REF)
      offset = AXISTUNE_REF;

   /* <= 0 is linear by definition (axistune.h, zero-fill identity), and
    * the top of the ladder is bounded so axistune_pow_q16's integer part
    * stays inside its own clamp. */
   if (exponent_q8 <= 0)
      exponent_q8 = 256;
   if (exponent_q8 > 2048)
      exponent_q8 = 2048;

   t->deadzone    = deadzone;
   t->offset      = offset;
   t->exponent_q8 = exponent_q8;
}

int AxisTuneIsIdentity(const axis_tune *t)
{
   if (!t)
      return 1;

   return (t->deadzone <= 0
           && t->offset == 0
           && (t->exponent_q8 == 256 || t->exponent_q8 <= 0)) ? 1 : 0;
}

int32_t AxisTuneApply(const axis_tune *t, int32_t raw, int32_t *gain_q8)
{
   int32_t  v, mag, gain;
   uint32_t n_q16, p_q16;

   if (gain_q8)
      *gain_q8 = 256;

   /* The identity early-out, before any arithmetic: this is what makes a
    * defaults-only configuration byte-identical to the pre-#439 path. */
   if (AxisTuneIsIdentity(t))
      return raw;

   v = raw - t->offset;

   if (v == 0)
      return 0;

   mag = (v < 0) ? -v : v;

   /* Gate, not re-base -- see axistune.h.  The sample is dropped whole or
    * passed whole. */
   if (mag <= t->deadzone)
      return 0;

   /* At or above the reference the curve is the identity by construction
    * (out = REF * (mag/REF)^e is REF at mag == REF for every e), and the
    * range above it passes through unchanged rather than saturating, so
    * no fast motion is ever lost to the curve. */
   if (t->exponent_q8 == 256 || mag >= AXISTUNE_REF)
      return v;

   /* mag < AXISTUNE_REF here, so the Q16 normalisation cannot reach 1.0
    * and the shift below cannot overflow. */
   n_q16 = ((uint32_t)mag << 16) / (uint32_t)AXISTUNE_REF;
   p_q16 = axistune_pow_q16(n_q16, (uint32_t)t->exponent_q8);

   /* out = REF * (mag/REF)^e, carried in Q8 so the gain division keeps a
    * fractional result for the small magnitudes that matter most.  The
    * product peaks at 64 * 65536, well inside int32. */
   gain = (int32_t)(((uint32_t)AXISTUNE_REF * p_q16) >> 8) / mag;

   /* THE DEAD ZONE IS THE ONLY THING ALLOWED TO DROP A SAMPLE.
    *
    * A Q8 gain cannot represent the bottom of a steep curve: at e = 3.0 a
    * 2-unit sample wants a gain of 0.25/256, which truncates to zero and
    * the sample vanishes -- so a user who picked a high exponent would see
    * slow movement not merely attenuated but completely inert, which reads
    * as a broken device rather than as a curve.  Flooring at 1 keeps such
    * a sample alive at 1/256 of a state per poll, where QuadFeed's `frac`
    * carry accumulates it instead of discarding it.  It overstates the
    * curve only in the region where the curve is asking for very nearly
    * nothing, and it keeps the two controls' responsibilities disjoint:
    * the gate decides what is dropped, the curve only decides how fast. */
   if (gain < 1)
      gain = 1;
   if (gain > AXISTUNE_MAX_GAIN)
      gain = AXISTUNE_MAX_GAIN;

   if (gain_q8)
      *gain_q8 = gain;

   return v;
}
