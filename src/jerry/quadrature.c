/*
 * src/jerry/quadrature.c -- 2-bit Gray-code quadrature encoder.
 *
 * See quadrature.h for the phase table, the direction convention and the
 * rationale for the one-state-per-advance rate policy.
 */

#include "quadrature.h"

/* Phase index -> A / B logic levels.  Kept as tables rather than bit
 * arithmetic so the sequence is readable against the manual's waveform. */
static const uint8_t quad_level_a[4] = { 0, 1, 1, 0 };
static const uint8_t quad_level_b[4] = { 0, 0, 1, 1 };

void QuadReset(quad_axis *q)
{
   if (!q)
      return;

   q->backlog = 0;
   q->frac    = 0;
   q->phase   = 0;
}

void QuadFeed(quad_axis *q, int32_t delta, int32_t scale_q8)
{
   int32_t acc;
   int32_t states;

   if (!q || delta == 0)
      return;

   /* Clamp the incoming delta so `delta * scale_q8` cannot overflow an
    * int32 even at the top of the sensitivity ladder.  The bound is the
    * int16_t range because that is what a libretro relative axis can
    * actually report; 32767 * 4096 (InputDevSetScale's ceiling) is
    * 1.34e8, comfortably inside int32.  A frontend reporting a
    * full-range delta in one frame is already far past anything
    * QUAD_MAX_BACKLOG will let through. */
   if (delta >  32767)
      delta =  32767;
   if (delta < -32767)
      delta = -32767;

   acc    = q->frac + (delta * scale_q8);
   /* Truncating division: the remainder keeps the sign of the dividend,
    * so the carry stays consistent in both directions (a -0.5-state feed
    * yields 0 states and a -128 carry, and the next one completes it). */
   states = acc / 256;
   q->frac = acc - (states * 256);

   q->backlog += states;

   if (q->backlog >  QUAD_MAX_BACKLOG)
      q->backlog =  QUAD_MAX_BACKLOG;
   if (q->backlog < -QUAD_MAX_BACKLOG)
      q->backlog = -QUAD_MAX_BACKLOG;
}

int QuadAdvance(quad_axis *q)
{
   if (!q || q->backlog == 0)
      return 0;

   if (q->backlog > 0)
   {
      q->phase = (uint8_t)((q->phase + 1) & 3);
      q->backlog--;
   }
   else
   {
      q->phase = (uint8_t)((q->phase + 3) & 3);
      q->backlog++;
   }

   return 1;
}

void QuadLevels(const quad_axis *q, int *a, int *b)
{
   uint8_t phase = q ? (uint8_t)(q->phase & 3) : (uint8_t)0;

   if (a)
      *a = (int)quad_level_a[phase];
   if (b)
      *b = (int)quad_level_b[phase];
}
