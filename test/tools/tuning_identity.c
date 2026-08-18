/*
 * test/tools/tuning_identity.c -- #439's guardrail, measured end to end.
 *
 * WHAT THIS PROVES
 * ================
 * Per-axis tuning (#439) inserts arithmetic into the path every analog
 * device's motion takes to the $F14000 matrix.  The issue names two things
 * that must stay true afterwards, and this file is both of them expressed
 * as digests over a swept input domain:
 *
 *   1. DEFAULTS CHANGE NOTHING.  A user who never opens the menu must get
 *      the pre-#439 core.  Asserted by running the identical sweep twice --
 *      once with the tuning API never called at all, once with it called
 *      using the option defaults -- and requiring the two digests to be
 *      equal.
 *
 *   2. THE DIGITAL PAD IS NOT TOUCHED.  Tuning applies only to analog
 *      device paths.  Asserted by sweeping with both ports as plain pads,
 *      driving all 21 button slots on both, with an aggressive non-default
 *      tuning configured -- and requiring that digest to equal the same
 *      sweep with tuning at defaults.
 *
 * WHY IT SELF-BASELINES INSTEAD OF ASSERTING A CONSTANT
 * ====================================================
 * joymatrix_identity.c holds hard-coded digests measured on develop, which
 * is the right instrument for "nothing about the joystick path moved".  It
 * is the wrong instrument for this question, because the constant would
 * have to be re-measured on a build that already contains the change it is
 * supposed to be judging.  Comparing two runs of the SAME binary against
 * each other has no such circularity: the only difference between them is
 * whether the tuning API was called.
 *
 * THE NEGATIVE CONTROL IS NOT OPTIONAL
 * ====================================
 * A self-baselining equality test passes trivially if the layer is not
 * wired up at all -- delete the AxisTuneApply() call from InputDevFeed()
 * and every assertion above still holds.  So a third digest is taken with
 * a non-default tuning on the ANALOG ports and required to DIFFER.  That
 * is what makes the equalities mean "defaults are inert" rather than "the
 * feature does nothing".
 *
 * USAGE
 *   ./test/tools/tuning_identity ./virtualjaguar_libretro.dylib [rom]
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#include "../harness/harness.h"

/* Mirrors InputDevType in src/jerry/inputdev.h.  dlsym'd pointers must
 * carry the real signature or UBSan -fsanitize=function aborts on a type
 * mismatch, so the enum is restated rather than passed as int. */
typedef enum
{
   DEV_PAD                 = 0,
   DEV_MOUSE_ST            = 1,
   DEV_MOUSE_AMIGA_ADAPTER = 2,
   DEV_MOUSE_AMIGA_ON_ST   = 3,
   DEV_ROTARY              = 4
} InputDevType;

/* Mirrors INPUTDEV_AXIS_* in src/jerry/inputdev.h. */
#define AXIS_X 0
#define AXIS_Y 1

#define BUTTON_SLOTS 21

/* Port-1 and port-2 socket-0 row codes, output enable set.  Sweeping both
 * ports' rows matters: the mouse is row-blind and the rotary is row-0-only,
 * so a single-row sweep would miss one of the two devices entirely. */
static const uint16_t row_words[8] = {
   0x81FE, 0x81FD, 0x81FB, 0x81F7,   /* port 1 rows 0-3 */
   0x817F, 0x81BF, 0x81DF, 0x81EF    /* port 2 rows 0-3 */
};

#define FNV1A_OFFSET 0x811C9DC5u
#define FNV1A_PRIME  0x01000193u

#define SWEEP_SEED  0x5DEECE66u
#define SWEEP_STEPS 4096

static uint32_t rng_state;

static uint32_t rng_next(void)
{
   rng_state = (rng_state * 1103515245u) + 12345u;
   return rng_state;
}

static void fold_word(uint32_t *d, uint16_t v)
{
   *d = (*d ^ (uint32_t)(v & 0xFFu))       * FNV1A_PRIME;
   *d = (*d ^ (uint32_t)((v >> 8) & 0xFFu)) * FNV1A_PRIME;
}

/* ---- core entry points ---------------------------------------------- */

static uint16_t (*p_ReadWord)(uint32_t);
static void     (*p_WriteWord)(uint32_t, uint16_t);
static void     (*p_SetType)(int, InputDevType);
static void     (*p_SetScale)(int, int32_t);
static void     (*p_SetTune)(int, int, int32_t, int32_t, int32_t);
static void     (*p_Reset)(void);
static void     (*p_Feed)(int, int32_t, int32_t, uint32_t);
static uint8_t  *p_joypad0Buttons;
static uint8_t  *p_joypad1Buttons;

static int failures;

static void report(int cond, const char *what)
{
   if (cond)
      printf("  ok   %s\n", what);
   else
   {
      printf("  FAIL %s\n", what);
      failures++;
   }
}

/* ---- the sweep ------------------------------------------------------- *
 *
 * TUNE MODE:
 *   0  never call InputDevSetTune at all -- the pre-#439 configuration
 *   1  call it with the option defaults (0 dead zone, 0 offset, linear)
 *   2  call it with an aggressive non-default tuning
 *
 * The RNG is re-seeded per sweep, so every mode sees the identical motion
 * stream and the identical button vectors in the identical order.  That is
 * what makes the digests directly comparable; without it the comparison
 * would be measuring the RNG. */
static uint32_t run_sweep(int p0_type, int p1_type, int tune_mode)
{
   uint32_t digest = FNV1A_OFFSET;
   unsigned step, slot, i;
   int      port;

   p_Reset();
   p_SetType(0, (InputDevType)p0_type);
   p_SetType(1, (InputDevType)p1_type);
   p_SetScale(0, 256);
   p_SetScale(1, 256);

   for (port = 0; port < 2; port++)
   {
      if (tune_mode == 1)
      {
         p_SetTune(port, AXIS_X, 0, 0, 256);
         p_SetTune(port, AXIS_Y, 0, 0, 256);
      }
      else if (tune_mode == 2)
      {
         /* Deliberately aggressive: a dead zone that swallows the smaller
          * deltas the stream generates, a non-zero bias, and a steep
          * curve.  All three have to be inert on a pad. */
         p_SetTune(port, AXIS_X, 3, 2, 512);
         p_SetTune(port, AXIS_Y, 2, -1, 384);
      }
   }

   rng_state = SWEEP_SEED;

   for (step = 0; step < SWEEP_STEPS; step++)
   {
      /* Fill every pad slot on both ports from the stream.  A slot is
       * filled from a random BIT, not a random byte: a uniform byte would
       * be non-zero 255 times in 256 and "released" would never be swept.
       * Same construction as joymatrix_identity.c, for the same reason. */
      for (slot = 0; slot < BUTTON_SLOTS; slot++)
      {
         uint32_t r0 = rng_next();
         uint32_t r1 = rng_next();

         p_joypad0Buttons[slot] = (r0 & 1u)
            ? (uint8_t)(1u + ((r0 >> 1) & 0x7Fu)) : (uint8_t)0;
         p_joypad1Buttons[slot] = (r1 & 1u)
            ? (uint8_t)(1u + ((r1 >> 1) & 0x7Fu)) : (uint8_t)0;
      }

      /* Host motion for both ports.  The range straddles the dead zones
       * and the curve's reference (64) in both directions, so no tuning
       * knob is swept only in the region where it happens to be inert. */
      for (port = 0; port < 2; port++)
      {
         uint32_t r  = rng_next();
         int32_t  dx = (int32_t)(r % 161u) - 80;
         int32_t  dy = (int32_t)((r >> 8) % 161u) - 80;

         p_Feed(port, dx, dy, (r >> 16) & 3u);
      }

      /* One full eight-row scan per step, folding both register offsets.
       * The write arms the device layer and the read clocks it, which is
       * the sequence a real driver's poll produces. */
      for (i = 0; i < 8; i++)
      {
         p_WriteWord(0, row_words[i]);
         fold_word(&digest, p_ReadWord(0));
         fold_word(&digest, p_ReadWord(2));
      }
   }

   return digest;
}

int main(int argc, char **argv)
{
   harness_config cfg = HARNESS_CONFIG_DEFAULT;
   harness_result result;
   char     summary[128];
   uint32_t pad_untouched, pad_default, pad_tuned;
   uint32_t analog_untouched, analog_default, analog_tuned;

   cfg.frames = 0;
   if (!harness_init_from_args(&cfg, argc, argv))
      return 1;

   if (!cfg.rom_path)
      cfg.rom_path = "test/roms/yarc.j64";
   if (!harness_load_rom(&cfg))
      return 1;

   p_ReadWord  = (uint16_t (*)(uint32_t))harness_dlsym(&cfg, "JoystickReadWord");
   p_WriteWord = (void (*)(uint32_t, uint16_t))
                    harness_dlsym(&cfg, "JoystickWriteWord");
   p_SetType   = (void (*)(int, InputDevType))
                    harness_dlsym(&cfg, "InputDevSetType");
   p_SetScale  = (void (*)(int, int32_t))harness_dlsym(&cfg, "InputDevSetScale");
   p_SetTune   = (void (*)(int, int, int32_t, int32_t, int32_t))
                    harness_dlsym(&cfg, "InputDevSetTune");
   p_Reset     = (void (*)(void))harness_dlsym(&cfg, "InputDevReset");
   p_Feed      = (void (*)(int, int32_t, int32_t, uint32_t))
                    harness_dlsym(&cfg, "InputDevFeed");
   p_joypad0Buttons = (uint8_t *)harness_dlsym(&cfg, "joypad0Buttons");
   p_joypad1Buttons = (uint8_t *)harness_dlsym(&cfg, "joypad1Buttons");

   if (!p_ReadWord || !p_WriteWord || !p_SetType || !p_SetScale
       || !p_SetTune || !p_Reset || !p_Feed
       || !p_joypad0Buttons || !p_joypad1Buttons)
   {
      fprintf(stderr,
              "tuning_identity: missing test-ABI symbols.  The wide test "
              "ABI must export Joystick* / InputDev* / joypad0Buttons / "
              "joypad1Buttons.  Build with TEST_EXPORTS=1.\n");
      harness_shutdown(&cfg);
      return 1;
   }

   printf("=== Per-axis tuning identity (#439) ===\n\n");

   /* SWEEP ORDER IS LOAD-BEARING, and getting it wrong is a silent
    * inversion rather than an error.
    *
    * A tune is a static in the core: InputDevReset() clears the encoders
    * but deliberately NOT the option-derived tuning, exactly as it leaves
    * the sensitivity scale alone (src/jerry/inputdev.h).  So "mode 0" --
    * the pre-#439 configuration, where the API has never been called -- is
    * only observable BEFORE the first InputDevSetTune() call in the
    * process.  Both mode-0 sweeps therefore run first.
    *
    * Written the obvious way (all three modes per device configuration,
    * pads then analog) the analog mode-0 sweep inherits the aggressive
    * tuning left behind by the pad mode-2 sweep, and the test reports the
    * exact opposite of the truth: "defaults changed the digest" and "a
    * non-default tuning did not". */
   pad_untouched    = run_sweep(DEV_PAD, DEV_PAD, 0);
   analog_untouched = run_sweep(DEV_ROTARY, DEV_MOUSE_ST, 0);

   pad_default      = run_sweep(DEV_PAD, DEV_PAD, 1);
   analog_default   = run_sweep(DEV_ROTARY, DEV_MOUSE_ST, 1);

   pad_tuned        = run_sweep(DEV_PAD, DEV_PAD, 2);
   analog_tuned     = run_sweep(DEV_ROTARY, DEV_MOUSE_ST, 2);

   /* --- the digital pad must be bit-identical, tuned or not ---------- */
   printf("the standard digital pad is untouched by any tuning\n");
   printf("       pad digests: untouched 0x%08X  defaults 0x%08X  tuned 0x%08X\n",
          pad_untouched, pad_default, pad_tuned);

   report(pad_default == pad_untouched,
          "pad: setting the tuning API to its defaults changes nothing");
   report(pad_tuned == pad_untouched,
          "pad: an aggressive tuning changes nothing either -- the guardrail");

   /* --- analog ports: defaults inert, non-defaults live -------------- */
   printf("\nanalog paths: defaults are inert, non-defaults are not\n");
   printf("       analog digests: untouched 0x%08X  defaults 0x%08X  tuned 0x%08X\n",
          analog_untouched, analog_default, analog_tuned);

   report(analog_default == analog_untouched,
          "rotary on port 1 + mouse on port 2: defaults are byte-identical "
          "to never calling the API");

   /* The negative control.  Without this the equality above would still
    * hold on a build where the tuning layer was never wired in. */
   report(analog_tuned != analog_untouched,
          "and a non-default tuning DOES move the analog digest (the layer "
          "is actually connected)");

   /* An analog sweep that digests the same as a pad sweep would mean the
    * devices contributed nothing and every assertion above is vacuous. */
   report(analog_untouched != pad_untouched,
          "the analog sweep is not accidentally a pad sweep");

   sprintf(summary, "%d failure(s)", failures);
   result.status = failures ? "FAIL" : "PASS";
   result.name   = "tuning_identity";
   result.detail = summary;
   harness_report(&cfg, &result, 1);

   harness_shutdown(&cfg);
   return failures ? 1 : 0;
}
