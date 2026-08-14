/*
 * test/tools/joymatrix_identity.c -- $F14000 / $F14002 identity guardrail.
 *
 * WHAT THIS IS FOR
 * ================
 * The input-devices track (#428) adds an ST/Amiga mouse (#429) and a
 * Tempest rotary (#436) to the joystick read path.  Both issues name the
 * same acceptance condition: with no such device selected, $F14000 and
 * $F14002 must behave EXACTLY as they do today.
 *
 * This tool is that condition expressed as a number.  It sweeps the whole
 * input domain of JoystickReadWord()/JoystickWriteWord() and folds every
 * result into an FNV-1a digest, which is asserted against a constant
 * measured on develop.  Any perturbation of the joystick path -- a changed
 * mask, a bit asserted in the wrong row, an overlay that fires when it
 * should not -- moves the digest.
 *
 * It is committed BEFORE any device code exists, so later PRs are measured
 * against a number that predates them.
 *
 * WHAT IS SWEPT
 * =============
 * These are exactly the inputs JoystickReadWord() reads.  Nothing else
 * reaches it, which is what makes one hard-coded constant valid on every
 * host:
 *
 *   joystick_ram[1]        all 256 values -- both row-select nibbles, so
 *                          every (port-1 row, port-2 row) pair including
 *                          the 0xFF "not a socket-0 code" cases
 *   write-word bit 15      output enable.  It gates the early return in
 *                          JoystickReadWord, i.e. the one structural risk
 *                          the device layer runs (see the design spec's
 *                          "joysticksEnabled is deliberately NOT touched")
 *   write-word bit 8       audio enable, swept alongside it
 *   joypad0Buttons[0..20]  64 seeded pseudo-random vectors, ALL 21 slots
 *   joypad1Buttons[0..20]  (indices 0-15 are reached as offsetN + i,
 *                          16-20 as BUTTON_A..BUTTON_PAUSE at $F14002)
 *   vjs.hardwareTypeNTSC   both ways -- $F14002 bakes the NTSC bit into
 *                          its return value, so a one-sided sweep would
 *                          make the digest host-config-dependent
 *
 * and for each combination it reads register offsets 0, 1, 2 and 3.
 * Offsets 1 and 3 hit the fallthrough `return 0xFFFF` today; folding them
 * pins that too.
 *
 * A button slot is filled from a random BIT (pressed / released), not a
 * random byte: a uniform byte would be non-zero 255 times in 256 and the
 * sweep would essentially never exercise "released".  When pressed, the
 * value is a varying non-zero, so a change from truthiness to `== 1` also
 * trips the digest.
 *
 * TWO ASSERTIONS
 * ==============
 *   full digest  -- everything above.  "Nothing changed at all."
 *   port-1 digest -- the same sweep folding only port 1's own bits
 *                   ($F14000 bits 8-11, $F14002 bits 0-1).  A port-2
 *                   device must not perturb port 1; measured here on a
 *                   pad-only build so a later PR cannot derive its own
 *                   baseline from a build that already carries the
 *                   perturbation.
 *
 * The digest is a pure function of the sweep, so it is reproducible on any
 * host: reads are folded byte-by-byte in a fixed order (endian-independent)
 * and the PRNG is explicit fixed-width unsigned arithmetic, never rand().
 * The loop order below is part of the digest's definition -- changing it
 * changes the constants.
 *
 * USAGE
 *   ./test/tools/joymatrix_identity ./virtualjaguar_libretro.dylib [rom]
 *   --print   report the measured digests and exit 0 without asserting
 *             (use when a change to the sweep itself needs re-baselining;
 *             never for verification)
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#include "../harness/harness.h"
#include "settings.h"

/* ---- sweep parameters (part of the digest's definition) ------------- */

#define SWEEP_ROW_BYTES   256      /* joystick_ram[1], all values         */
#define SWEEP_VECTORS      64      /* button vectors per write word       */
#define SWEEP_SEED  0x1D872B41u    /* xorshift32 seed                     */
#define BUTTON_SLOTS       21      /* BUTTON_U .. BUTTON_PAUSE            */

/* High byte of the write word: bit 15 (output enable) and bit 8 (audio),
 * both set and clear. */
static const uint8_t sweep_hi_bytes[4] = { 0x00, 0x01, 0x80, 0x81 };

/* Expected digests, measured on develop at 1aa9c43 (the merge-base of
 * this branch) with the joystick path untouched.  If you change the sweep
 * above, re-measure with --print and update BOTH of these in the same
 * commit; if you did NOT change the sweep, a mismatch is a real change in
 * $F14000/$F14002 behaviour and must be explained, not re-baselined. */
#define EXPECT_DIGEST_FULL    0xC24DDCDEu
#define EXPECT_DIGEST_PORT1   0xD0CD02E2u

/* ---- FNV-1a ---------------------------------------------------------- */

#define FNV1A_OFFSET 2166136261u
#define FNV1A_PRIME  16777619u

static void fold_byte(uint32_t *h, uint8_t b)
{
   *h ^= (uint32_t)b;
   *h *= FNV1A_PRIME;
}

/* Fixed byte order -> the digest does not depend on host endianness. */
static void fold_word(uint32_t *h, uint16_t w)
{
   fold_byte(h, (uint8_t)((w >> 8) & 0xFF));
   fold_byte(h, (uint8_t)(w & 0xFF));
}

/* ---- xorshift32 (explicit, reproducible; never rand()) --------------- */

static uint32_t rng_state;

static uint32_t rng_next(void)
{
   uint32_t x = rng_state;
   x ^= x << 13;
   x ^= x >> 17;
   x ^= x << 5;
   rng_state = x;
   return x;
}

int main(int argc, char **argv)
{
   harness_config cfg = HARNESS_CONFIG_DEFAULT;
   harness_result results[2];
   unsigned num_results = 0;
   int print_only = 0;
   int i;
   int ok_full, ok_port1;

   uint16_t (*p_JoystickReadWord)(uint32_t);
   void     (*p_JoystickWriteWord)(uint32_t, uint16_t);
   uint8_t  *p_joypad0Buttons;
   uint8_t  *p_joypad1Buttons;
   struct VJSettings *p_vjs;

   uint32_t digest_full  = FNV1A_OFFSET;
   uint32_t digest_port1 = FNV1A_OFFSET;
   unsigned long reads   = 0;

   int ntsc_idx;
   unsigned hi_idx, lo, vec, slot, off;
   char detail_full[192];
   char detail_port1[192];

   for (i = 1; i < argc; i++)
   {
      if (strcmp(argv[i], "--print") == 0)
         print_only = 1;
   }

   cfg.frames = 0;
   if (!harness_init_from_args(&cfg, argc, argv))
      return 1;

   /* A ROM is loaded purely so the core is properly initialised (and so
    * harness_shutdown's retro_deinit is balanced).  No frame is ever run,
    * so update_input() never touches joypadNButtons -- the sweep owns
    * every input this test reads. */
   if (!cfg.rom_path)
      cfg.rom_path = "test/roms/yarc.j64";
   if (!harness_load_rom(&cfg))
      return 1;

   p_JoystickReadWord  = (uint16_t (*)(uint32_t))
                            harness_dlsym(&cfg, "JoystickReadWord");
   p_JoystickWriteWord = (void (*)(uint32_t, uint16_t))
                            harness_dlsym(&cfg, "JoystickWriteWord");
   p_joypad0Buttons    = (uint8_t *)harness_dlsym(&cfg, "joypad0Buttons");
   p_joypad1Buttons    = (uint8_t *)harness_dlsym(&cfg, "joypad1Buttons");
   p_vjs               = (struct VJSettings *)harness_dlsym(&cfg, "vjs");

   if (!p_JoystickReadWord || !p_JoystickWriteWord ||
       !p_joypad0Buttons || !p_joypad1Buttons || !p_vjs)
   {
      fprintf(stderr,
              "joymatrix_identity: missing test-ABI symbols.  The wide test "
              "ABI must export Joystick* / joypad0Buttons / joypad1Buttons "
              "(exports-test.list and link-test.T).  Build with "
              "TEST_EXPORTS=1.\n");
      harness_shutdown(&cfg);
      return 1;
   }

   rng_state = SWEEP_SEED;

   for (ntsc_idx = 0; ntsc_idx < 2; ntsc_idx++)
   {
      p_vjs->hardwareTypeNTSC = (ntsc_idx == 0) ? true : false;

      for (hi_idx = 0; hi_idx < 4; hi_idx++)
      {
         for (lo = 0; lo < SWEEP_ROW_BYTES; lo++)
         {
            uint16_t word = (uint16_t)(((uint16_t)sweep_hi_bytes[hi_idx] << 8)
                                       | (uint16_t)lo);

            for (vec = 0; vec < SWEEP_VECTORS; vec++)
            {
               for (slot = 0; slot < BUTTON_SLOTS; slot++)
               {
                  uint32_t r0 = rng_next();
                  uint32_t r1 = rng_next();
                  p_joypad0Buttons[slot] = (r0 & 1u)
                     ? (uint8_t)(1u + ((r0 >> 1) & 0x7Fu)) : (uint8_t)0;
                  p_joypad1Buttons[slot] = (r1 & 1u)
                     ? (uint8_t)(1u + ((r1 >> 1) & 0x7Fu)) : (uint8_t)0;
               }

               p_JoystickWriteWord(0, word);

               for (off = 0; off < 4; off++)
               {
                  uint16_t got = p_JoystickReadWord(off);
                  fold_word(&digest_full, got);
                  reads++;

                  /* Port-1 sub-digest: only port 1's own bits.
                   * $F14000 bits 8-11 (J8..J11), $F14002 bits 0-1 (B0/B1). */
                  if (off == 0)
                     fold_word(&digest_port1, (uint16_t)(got & 0x0F00u));
                  else if (off == 2)
                     fold_word(&digest_port1, (uint16_t)(got & 0x0003u));
               }
            }
         }
      }
   }

   ok_full  = (digest_full  == EXPECT_DIGEST_FULL);
   ok_port1 = (digest_port1 == EXPECT_DIGEST_PORT1);

   if (print_only)
   {
      printf("joymatrix_identity: sweep = %d ntsc x %d hi-bytes x %d row bytes"
             " x %d vectors x 4 offsets = %lu reads (seed 0x%08X)\n",
             2, 4, SWEEP_ROW_BYTES, SWEEP_VECTORS, reads,
             (unsigned)SWEEP_SEED);
      printf("joymatrix_identity: EXPECT_DIGEST_FULL  = 0x%08Xu\n",
             digest_full);
      printf("joymatrix_identity: EXPECT_DIGEST_PORT1 = 0x%08Xu\n",
             digest_port1);
      harness_shutdown(&cfg);
      return 0;
   }

   sprintf(detail_full,
           "digest 0x%08X (expected 0x%08X); %lu reads, seed 0x%08X, "
           "%d row bytes x %d vectors",
           digest_full, (unsigned)EXPECT_DIGEST_FULL, reads,
           (unsigned)SWEEP_SEED, SWEEP_ROW_BYTES, SWEEP_VECTORS);
   sprintf(detail_port1,
           "port-1 bits digest 0x%08X (expected 0x%08X); "
           "$F14000 bits 8-11 + $F14002 bits 0-1",
           digest_port1, (unsigned)EXPECT_DIGEST_PORT1);

   results[num_results].status = ok_full ? "PASS" : "FAIL";
   results[num_results].name   = "joymatrix_identity_full";
   results[num_results].detail = detail_full;
   num_results++;

   results[num_results].status = ok_port1 ? "PASS" : "FAIL";
   results[num_results].name   = "joymatrix_identity_port1";
   results[num_results].detail = detail_port1;
   num_results++;

   harness_report(&cfg, results, num_results);

   if (!ok_full || !ok_port1)
      fprintf(stderr,
              "joymatrix_identity: $F14000/$F14002 behaviour CHANGED.  This "
              "test exists to catch exactly that.  If the change is "
              "deliberate, say so and re-baseline with --print in the same "
              "commit; do not silently update the constants.\n");

   harness_shutdown(&cfg);
   return (ok_full && ok_port1) ? 0 : 1;
}
