/*
 * test/test_quadrature.c -- unit test for the Gray-code quadrature encoder.
 *
 * Standalone: compiles src/jerry/quadrature.c directly, no core, no
 * dlopen.  Asserts the properties the mouse (#429) and the rotary (#436)
 * both depend on:
 *
 *   1. the Gray sequence, forwards and backwards, against the table in
 *      quadrature.h (which is docs/jaguar-mouse-adapter-mapping.md
 *      section 5 and the Technical Reference V10's rotary sequence -- the
 *      two agree bit-for-bit);
 *   2. QuadAdvance() moves AT MOST one state, ever.  This is the whole
 *      rate policy: a two-state jump indexes the "undetermined" entry of
 *      a real decoder's table and is silently discarded, so emitting one
 *      is worse than emitting nothing;
 *   3. the backlog clamp;
 *   4. the Q8 remainder carry, so a <1.0 sensitivity decimates smoothly
 *      instead of quantising every small movement to zero.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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

/* Reference phase table, written out independently of the implementation
 * so a change to either side has to be deliberate. */
static const int ref_a[4] = { 0, 1, 1, 0 };
static const int ref_b[4] = { 0, 0, 1, 1 };

static void test_sequence_forward(void)
{
   quad_axis q;
   int i;
   int ok = 1;

   printf("Gray sequence, positive direction (A leads B)\n");

   QuadReset(&q);
   QuadFeed(&q, 8, 256);

   for (i = 0; i < 8; i++)
   {
      int a, b;
      int expect = (QUAD_REST_PHASE + i + 1) & 3;

      if (!QuadAdvance(&q))
      {
         ok = 0;
         break;
      }
      QuadLevels(&q, &a, &b);
      if (a != ref_a[expect] || b != ref_b[expect])
         ok = 0;
   }

   check(ok, "8 advances walk the Gray table upward from the rest phase");
   check(q.backlog == 0, "backlog fully drained after 8 advances");
   check(QuadAdvance(&q) == 0, "an empty backlog does not advance");
}

static void test_sequence_reverse(void)
{
   quad_axis q;
   int i;
   int ok = 1;

   printf("Gray sequence, negative direction (B leads A)\n");

   QuadReset(&q);
   QuadFeed(&q, -8, 256);

   for (i = 0; i < 8; i++)
   {
      int a, b;
      int expect = (QUAD_REST_PHASE - (i + 1)) & 3;   /* downward */

      if (!QuadAdvance(&q))
      {
         ok = 0;
         break;
      }
      QuadLevels(&q, &a, &b);
      if (a != ref_a[expect] || b != ref_b[expect])
         ok = 0;
   }

   check(ok, "8 advances walk the Gray table downward from the rest phase");
   check(q.backlog == 0, "backlog fully drained after 8 advances");
}

static void test_one_state_per_advance(void)
{
   quad_axis q;
   int i;
   int ok = 1;

   printf("one state per advance (the rate policy)\n");

   /* Motion arriving faster than it can drain: 100 units fed per poll,
    * one poll per iteration.  Every advance must still move exactly one
    * state -- this is the case where a naive implementation would "catch
    * up" by jumping, which a real decoder discards outright. */
   QuadReset(&q);

   for (i = 0; i < 200; i++)
   {
      uint8_t before;
      int step;

      QuadFeed(&q, 100, 256);
      before = q.phase;
      QuadAdvance(&q);
      step = ((int)q.phase - (int)before + 4) & 3;
      if (step != 1)
         ok = 0;
   }

   check(ok, "200 saturated polls each move exactly 1 state (never a jump)");

   /* And when the backlog IS empty, an advance is a no-op rather than a
    * wrap: a decoder reads "no movement", which is harmless. */
   ok = 1;
   QuadReset(&q);
   QuadFeed(&q, 3, 256);
   for (i = 0; i < 10; i++)
   {
      uint8_t before = q.phase;
      int step;

      QuadAdvance(&q);
      step = ((int)q.phase - (int)before + 4) & 3;
      if (step > 1)
         ok = 0;
      if (i >= 3 && step != 0)
         ok = 0;
   }

   check(ok, "advances past an empty backlog hold the phase steady");
}

static void test_backlog_clamp(void)
{
   quad_axis q;

   printf("backlog clamp\n");

   QuadReset(&q);
   QuadFeed(&q, 100000, 256);
   check(q.backlog == QUAD_MAX_BACKLOG, "positive backlog clamps at +64");

   QuadReset(&q);
   QuadFeed(&q, -100000, 256);
   check(q.backlog == -QUAD_MAX_BACKLOG, "negative backlog clamps at -64");
}

static void test_q8_carry(void)
{
   quad_axis q;
   int i;
   int advanced = 0;

   printf("Q8 sensitivity carry\n");

   /* 25% sensitivity: four one-unit feeds must produce exactly one state,
    * not zero (which is what dropping the remainder would give). */
   QuadReset(&q);
   for (i = 0; i < 4; i++)
      QuadFeed(&q, 1, 64);
   check(q.backlog == 1, "4 x (1 unit @ 25%) accumulates exactly 1 state");

   QuadReset(&q);
   for (i = 0; i < 4; i++)
      QuadFeed(&q, -1, 64);
   check(q.backlog == -1, "4 x (-1 unit @ 25%) accumulates exactly -1 state");

   /* 200% doubles. */
   QuadReset(&q);
   QuadFeed(&q, 3, 512);
   check(q.backlog == 6, "3 units @ 200% is 6 states");

   /* A single sub-unit feed emits nothing yet but is not lost. */
   QuadReset(&q);
   QuadFeed(&q, 1, 64);
   check(q.backlog == 0, "1 unit @ 25% emits nothing yet");
   for (i = 0; i < 3; i++)
      QuadFeed(&q, 1, 64);
   while (QuadAdvance(&q))
      advanced++;
   check(advanced == 1, "...and the carried remainder completes the state");
}

static void test_reset(void)
{
   quad_axis q;

   printf("reset\n");

   QuadReset(&q);
   QuadFeed(&q, 7, 256);
   QuadAdvance(&q);
   QuadFeed(&q, 1, 64);
   QuadReset(&q);
   check(q.backlog == 0 && q.frac == 0 && q.phase == QUAD_REST_PHASE,
         "QuadReset zeroes backlog and carry and parks at the rest phase");

   /* The rest phase must be the (1,1) state: the mouse overlay is active
    * low, so any other parking position leaves an IDLE mouse asserting
    * direction lines at $F14000 -- see QUAD_REST_PHASE in quadrature.h. */
   {
      int a = -1, b = -1;
      QuadLevels(&q, &a, &b);
      check(a == 1 && b == 1,
            "the rest phase drives both lines high (idle asserts nothing)");
   }
}

int main(void)
{
   printf("=== quadrature encoder ===\n");

   test_sequence_forward();
   test_sequence_reverse();
   test_one_state_per_advance();
   test_backlog_clamp();
   test_q8_carry();
   test_reset();

   if (failures)
   {
      printf("FAILED: %d check(s)\n", failures);
      return 1;
   }

   printf("All quadrature checks passed\n");
   return 0;
}
