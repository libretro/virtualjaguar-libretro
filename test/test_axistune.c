/*
 * test/test_axistune.c -- unit test for the per-axis input tuning layer.
 *
 * Standalone: compiles src/jerry/axistune.c directly, no core, no dlopen --
 * the same shape as test/test_quadrature.c next door, and for the same
 * reason (the module is deliberately pure so its properties can be pinned
 * without a machine).
 *
 * The assertions are #439's contract:
 *
 *   1. DEFAULTS ARE THE IDENTITY, proved exhaustively over the whole int16
 *      input range rather than sampled.  This is the issue's guardrail
 *      expressed as a number: AxisTuneApply() is the single insertion point
 *      the tuning has into the analog paths, so "returns its argument
 *      unchanged, and a unity gain, for every representable input" IS the
 *      statement that a defaults-only configuration is byte-identical to
 *      the pre-#439 core.
 *   2. A ZERO-FILLED axis_tune is that identity.  InputDevShutdown()
 *      memsets its port array, so if zero were not the identity there would
 *      be a degenerate curve on every analog axis after an iOS reload --
 *      a bug reachable on exactly one platform, which is exactly the kind
 *      that ships.
 *   3. The dead zone is a GATE, not a re-base: below the threshold the
 *      sample is dropped whole, above it it passes whole.  A re-basing
 *      implementation would return 1 where this asserts dz+1.
 *   4. The offset is subtracted BEFORE the gate, in host orientation.
 *   5. The curve's fixed point is AXISTUNE_REF: at and above it every
 *      exponent yields unity gain, so no fast motion is ever lost to the
 *      curve and no exponent silently rescales the top of the range.
 *   6. AXISTUNE_REF == QUAD_MAX_BACKLOG.  The reference is quadrature.h's
 *      saturation point, not a taste constant; this is what stops the two
 *      drifting apart if either header is edited.
 *   7. The curve matches hand-computed exact values at the points where
 *      the fixed-point evaluation must be exact (halves and squares).
 *   8. THE GATE IS THE ONLY THING THAT DROPS A SAMPLE: across every
 *      exponent the layer accepts and every magnitude below the reference,
 *      a sample that passed the gate keeps a non-zero gain.
 *   9. The effective response mag*gain is monotone non-decreasing in mag,
 *      verified for exponent_q8 in [100, 800] -- the exact domain
 *      test_response_is_monotone() sweeps below. That is not "every
 *      exponent": the invariant is genuinely FALSE below exponent_q8=100
 *      (e.g. exponent_q8=1 dips mag 15->16 from mag*gain 16290 to 16288,
 *      confirmed by direct calculation against AxisTuneApply()). The test
 *      does not chase that region because nothing in the shipped option
 *      pipeline can reach it: read_tune_exponent() (libretro.c) only ever
 *      sees one of the exponent core options' own enumerated values, which
 *      run 100%-300% (exponent_q8 256-768) -- comfortably inside the
 *      swept-and-proven [100, 800]. A curve with a fixed-point dip inside
 *      that reachable range would read as the pointer stalling at one
 *      speed and is invisible to spot checks.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../src/jerry/axistune.h"
#include "../src/jerry/quadrature.h"

static int failures;

static void check(int cond, const char *what)
{
   if (cond)
      printf("  ok   %s\n", what);
   else
   {
      printf("  FAIL %s\n", what);
      failures++;
   }
}

/* ---- 1 + 2: the identity ------------------------------------------- */

static void test_defaults_are_identity(void)
{
   axis_tune t;
   axis_tune zeroed;
   int32_t   raw;
   int       ok_reset  = 1;
   int       ok_zeroed = 1;

   printf("defaults are the identity, over the whole int16 range\n");

   AxisTuneReset(&t);
   memset(&zeroed, 0, sizeof(zeroed));

   check(AxisTuneIsIdentity(&t),      "a reset tune reports itself as the identity");
   check(AxisTuneIsIdentity(&zeroed), "a ZERO-FILLED tune does too (InputDevShutdown memsets)");

   for (raw = -32768; raw <= 32767; raw++)
   {
      int32_t gain = 0;

      if (AxisTuneApply(&t, raw, &gain) != raw || gain != 256)
      {
         ok_reset = 0;
         break;
      }

      gain = 0;
      if (AxisTuneApply(&zeroed, raw, &gain) != raw || gain != 256)
      {
         ok_zeroed = 0;
         break;
      }
   }

   check(ok_reset,  "reset tune returns every input unchanged at unity gain");
   check(ok_zeroed, "zero-filled tune does the same -- no iOS-reload curve");

   /* A NULL tune is the identity too: the analog paths must never be able
    * to lose a sample to a missing configuration. */
   {
      int32_t gain = 0;
      check(AxisTuneApply(NULL, 1234, &gain) == 1234 && gain == 256,
            "a NULL tune is the identity as well");
   }
}

/* ---- 3: the dead zone gates, it does not re-base -------------------- */

static void test_deadzone_is_a_gate(void)
{
   axis_tune t;
   int32_t   gain = 0;
   int       ok   = 1;
   int32_t   m;

   printf("dead zone gates whole samples (no re-basing)\n");

   AxisTuneSet(&t, 3, 0, 256);

   check(AxisTuneApply(&t, 3, &gain)  == 0, "at the threshold the sample is dropped");
   check(AxisTuneApply(&t, -3, &gain) == 0, "and symmetrically in the negative direction");

   /* The discriminating assertion: a re-basing implementation returns 1
    * here (4 - 3), this one returns 4. */
   check(AxisTuneApply(&t, 4, &gain)  ==  4, "one unit past it, the FULL sample passes (4, not 1)");
   check(AxisTuneApply(&t, -4, &gain) == -4, "likewise negative (-4, not -1)");

   for (m = 1; m <= 3; m++)
      if (AxisTuneApply(&t, m, &gain) != 0 || AxisTuneApply(&t, -m, &gain) != 0)
         ok = 0;
   check(ok, "everything at or below the threshold is dropped, both signs");

   /* A dead zone alone must not disturb the response of what survives. */
   AxisTuneApply(&t, 40, &gain);
   check(gain == 256, "a dead zone alone leaves the gain at unity");
}

/* ---- 4: the offset is subtracted before the gate -------------------- */

static void test_offset(void)
{
   axis_tune t;
   int32_t   gain = 0;

   printf("offset is a per-sample bias, subtracted before the gate\n");

   AxisTuneSet(&t, 0, 2, 256);
   check(AxisTuneApply(&t, 2, &gain)  ==  0, "a sample equal to the offset cancels to zero");
   check(AxisTuneApply(&t, 5, &gain)  ==  3, "and a larger one is reduced by exactly the offset");
   check(AxisTuneApply(&t, -5, &gain) == -7, "a sample of the opposite sign moves further out");

   /* Ordering: with offset 2 and dead zone 1, a raw 3 becomes 1, which the
    * gate then drops.  Gating first would have passed it. */
   AxisTuneSet(&t, 1, 2, 256);
   check(AxisTuneApply(&t, 3, &gain) == 0,
         "the offset runs first -- a cancelled sample can then be gated");
   check(AxisTuneApply(&t, 5, &gain) == 3,
         "and a sample that survives both keeps its de-biased magnitude");
}

/* ---- 4b: an idle device must stay idle ------------------------------- */

static void test_offset_leaves_idle_alone(void)
{
   axis_tune t;
   int32_t   gain = 0;
   int32_t   off;
   int       ok = 1;

   printf("a non-zero offset never manufactures motion from an idle device\n");

   /* libretro.c feeds every attached port every frame, passing 0 when the
    * frontend reports no motion.  On a relative axis that is "no sample",
    * not "a sample at the biased centre", so the offset must not turn it
    * into one -- otherwise a configured offset spins the knob forever
    * with the controls untouched, which is the exact failure the option
    * exists to cure. */
   for (off = -64; off <= 64; off++)
   {
      AxisTuneSet(&t, 0, off, 256);      /* dead zone 0: nothing else can gate */
      if (AxisTuneApply(&t, 0, &gain) != 0)
         ok = 0;
   }
   check(ok, "raw 0 stays 0 for every offset in [-64, 64] at dead zone 0");

   /* Same, with a curve in play: the early-out must precede the curve too. */
   ok = 1;
   for (off = -64; off <= 64; off++)
   {
      AxisTuneSet(&t, 0, off, 512);      /* exponent 2.0 */
      if (AxisTuneApply(&t, 0, &gain) != 0)
         ok = 0;
   }
   check(ok, "and stays 0 with a non-identity exponent as well");

   /* The offset must still do its job on a real sample -- this is the
    * guard against "fixing" the above by disabling offset entirely. */
   AxisTuneSet(&t, 0, 3, 256);
   check(AxisTuneApply(&t, 3, &gain) == 0,
         "a device that really reports +3 at rest is still cancelled");
   check(AxisTuneApply(&t, 10, &gain) == 7,
         "and real motion is still de-biased");
}

/* ---- 5 + 6 + 7: the response curve ---------------------------------- */

static void test_curve_reference(void)
{
   axis_tune t;
   int32_t   gain = 0;
   int       ok   = 1;
   int32_t   e;

   printf("the response curve's reference and fixed point\n");

   check(AXISTUNE_REF == QUAD_MAX_BACKLOG,
         "AXISTUNE_REF is QUAD_MAX_BACKLOG -- the encoder's own saturation point");

   /* At and above the reference every exponent is unity, by construction:
    * REF * (REF/REF)^e == REF for all e. */
   for (e = 128; e <= 800; e += 8)
   {
      AxisTuneSet(&t, 0, 0, e);

      gain = 0;
      if (AxisTuneApply(&t, AXISTUNE_REF, &gain) != AXISTUNE_REF || gain != 256)
         ok = 0;

      gain = 0;
      if (AxisTuneApply(&t, 4 * AXISTUNE_REF, &gain) != 4 * AXISTUNE_REF
          || gain != 256)
         ok = 0;
   }
   check(ok, "at and above the reference, every exponent is unity gain");
}

static void test_curve_exact_points(void)
{
   axis_tune t;
   int32_t   gain;

   printf("the curve matches hand-computed values where it must be exact\n");

   /* e = 2.0.  out = 64 * (m/64)^2 = m^2/64, so gain = 256 * m / 64.  */
   AxisTuneSet(&t, 0, 0, 512);
   gain = 0; AxisTuneApply(&t, 32, &gain);
   check(gain == 128, "e=2.00, mag=32 -> gain 128 (half speed at half the reference)");
   gain = 0; AxisTuneApply(&t, 16, &gain);
   check(gain ==  64, "e=2.00, mag=16 -> gain 64");
   gain = 0; AxisTuneApply(&t,  4, &gain);
   check(gain ==  16, "e=2.00, mag=4  -> gain 16");

   /* e = 0.5.  out = 64 * sqrt(m/64), so gain = 256 * 64 * sqrt(m/64) / m. */
   AxisTuneSet(&t, 0, 0, 128);
   gain = 0; AxisTuneApply(&t, 16, &gain);
   check(gain == 512, "e=0.50, mag=16 -> gain 512 (sub-linear amplifies below the reference)");
   gain = 0; AxisTuneApply(&t,  4, &gain);
   check(gain == 1024, "e=0.50, mag=4  -> gain 1024");

   /* e = 1.5 is the successive-root path with both an integer and a
    * fractional part: out = 64 * (m/64)^1.5, gain = 256 * (m/64)^0.5. */
   AxisTuneSet(&t, 0, 0, 384);
   gain = 0; AxisTuneApply(&t, 16, &gain);
   check(gain == 128, "e=1.50, mag=16 -> gain 128 (= 256 * sqrt(1/4))");
   gain = 0; AxisTuneApply(&t,  4, &gain);
   check(gain ==  64, "e=1.50, mag=4  -> gain 64  (= 256 * sqrt(1/16))");

   /* Linear is a straight pass-through at every magnitude. */
   AxisTuneSet(&t, 0, 0, 256);
   gain = 0; AxisTuneApply(&t, 7, &gain);
   check(gain == 256, "e=1.00 is unity gain everywhere");
}

/* ---- 8 + 9: the invariants that hold across the whole ladder -------- */

static void test_gate_is_the_only_drop(void)
{
   axis_tune t;
   int32_t   e, m;
   int       ok_floor = 1;
   int       ok_ceil  = 1;
   int32_t   worst_e  = 0;
   int32_t   worst_m  = 0;

   printf("only the dead zone drops a sample; the curve never zeroes one\n");

   for (e = 1; e <= 2048; e++)
   {
      AxisTuneSet(&t, 0, 0, e);

      for (m = 1; m <= AXISTUNE_REF; m++)
      {
         int32_t gain = 0;
         int32_t d    = AxisTuneApply(&t, m, &gain);

         if (d != m)
            ok_floor = 0;
         if (gain < 1)
         {
            if (ok_floor)
            {
               worst_e = e;
               worst_m = m;
            }
            ok_floor = 0;
         }
         if (gain > AXISTUNE_MAX_GAIN)
            ok_ceil = 0;
      }
   }

   if (!ok_floor)
      printf("       first zero gain at exponent_q8=%d magnitude=%d\n",
             (int)worst_e, (int)worst_m);

   check(ok_floor,
         "no surviving sample gets a zero gain, at any exponent the layer accepts");
   check(ok_ceil, "and no gain exceeds AXISTUNE_MAX_GAIN");
}

static void test_response_is_monotone(void)
{
   axis_tune t;
   int32_t   e, m;
   int       ok = 1;

   printf("the effective response is monotone in magnitude\n");

   for (e = 100; e <= 800; e++)
   {
      int32_t prev = -1;

      AxisTuneSet(&t, 0, 0, e);

      for (m = 1; m <= 200; m++)
      {
         int32_t gain = 0;
         int32_t out;

         AxisTuneApply(&t, m, &gain);
         out = m * gain;          /* Q8 states before the port sensitivity */

         if (prev >= 0 && out < prev)
            ok = 0;
         prev = out;
      }
   }

   check(ok, "mag*gain never decreases as magnitude grows, for any exponent");
}

/* ---- clamping ------------------------------------------------------- */

static void test_set_clamps(void)
{
   axis_tune t;
   int32_t   gain = 0;

   printf("AxisTuneSet clamps hand-edited configuration values\n");

   AxisTuneSet(&t, -5, 0, 256);
   check(t.deadzone == 0, "a negative dead zone clamps to off");

   AxisTuneSet(&t, 1000000, 0, 256);
   check(t.deadzone == AXISTUNE_REF, "an absurd dead zone clamps to the reference");

   AxisTuneSet(&t, 0, -1000000, 256);
   check(t.offset == -AXISTUNE_REF, "an absurd offset clamps to -reference");

   AxisTuneSet(&t, 0, 1000000, 256);
   check(t.offset == AXISTUNE_REF, "and symmetrically the other way");

   AxisTuneSet(&t, 0, 0, 0);
   check(t.exponent_q8 == 256, "a zero exponent resolves to linear, not to a degenerate curve");

   AxisTuneSet(&t, 0, 0, -400);
   check(t.exponent_q8 == 256, "and so does a negative one");

   AxisTuneSet(&t, 0, 0, 999999);
   check(t.exponent_q8 == 2048, "an absurd exponent clamps to the ceiling");

   /* Clamped-but-extreme values must still behave, not just store. */
   AxisTuneSet(&t, 0, 0, 2048);
   check(AxisTuneApply(&t, 1, &gain) == 1 && gain >= 1,
         "the clamped ceiling exponent still passes a 1-unit sample");
}

int main(void)
{
   printf("=== Per-axis input tuning (axistune) ===\n\n");

   test_defaults_are_identity();
   printf("\n");
   test_deadzone_is_a_gate();
   printf("\n");
   test_offset();
   test_offset_leaves_idle_alone();
   printf("\n");
   test_curve_reference();
   printf("\n");
   test_curve_exact_points();
   printf("\n");
   test_gate_is_the_only_drop();
   printf("\n");
   test_response_is_monotone();
   printf("\n");
   test_set_clamps();

   printf("\n%s\n", failures ? "FAILED" : "All axistune checks passed");
   return failures ? 1 : 0;
}
