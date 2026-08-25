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
 * WHAT THE TEAM TAP ADDS (#513)
 * =============================
 * The Team Tap resolves the twelve row codes the tables above answered
 * with 0xFF, so it is exactly the class of change this file exists to
 * bound.  Four things are asserted about it, and the first is the point
 * of the whole file:
 *
 *   - the three digests above must come out UNCHANGED with no tap
 *     selected, which is the acceptance condition #513 inherits from
 *     #446.  run_sweep() and BUTTON_SLOTS are therefore left exactly as
 *     they were -- widening them to cover sockets 1-3 would move the
 *     constants and destroy the very proof that is wanted.  The tap
 *     sweep is a SEPARATE function with its own constant.
 *   - detaching a tap must restore that identity (assertion 5), so a
 *     session that toggled one is not left permanently perturbed.
 *   - the detection probe must answer BOTH ways, tested the way the
 *     Joypad-TeamTap Tester (Domin, 2000) tests it: write $81FA and read
 *     JOYBUTS bit 0 for port 1, write $815F and read bit 2 for port 2.
 *   - the pads behind the adapter must actually be READABLE, or a tap
 *     that decoded to nothing at all would pass everything above.  Same
 *     gap the fourth mouse assertion below exists to close.
 *
 * THREE ASSERTIONS
 * ================
 *   full digest  -- everything above.  "Nothing changed at all."
 *   port-1 digest -- the same sweep folding only port 1's own bits
 *                   ($F14000 bits 8-11, $F14002 bits 0-1).  A port-2
 *                   device must not perturb port 1; measured here on a
 *                   pad-only build so a later PR cannot derive its own
 *                   baseline from a build that already carries the
 *                   perturbation.
 *   port-1 digest with a MOUSE attached to port 2 -- the same constant.
 *                   Added once the device layer existed (#429): a
 *                   row-blind port-2 overlay drives $F14000 bits 12-15
 *                   and $F14002 bits 2-3 in every row, and this asserts
 *                   that none of that reaches port 1's own bits.  Skipped
 *                   with a printed note if the InputDev* test-ABI symbols
 *                   are absent, so this file still runs on a build that
 *                   predates the device layer.
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
/* For InputDevType: dlsym'd pointers must carry the real signature, or
 * UBSan -fsanitize=function aborts the suite on a type mismatch. */
#include "../../src/jerry/inputdev.h"
/* For JOYPAD_* and the BUTTON_* slot enum, and the same signature rule
 * again for JoystickSetTeamTap's `bool` parameter (#513). */
#include "../../src/jerry/joystick.h"
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

/* Third sweep: the same domain with an ST mouse attached to port 2.  This
 * one is NOT a develop measurement -- it is measured on the device layer
 * and is the only assertion that pins the mouse's own bits.
 *
 * Why it has to exist: every check in mouse_decode_test compares Gray
 * indices modulo 4, so inverting the active-low polarity on the direction
 * lines (inputdev.c, `if (!ax)` -> `if (ax)`) shifts the index by a
 * constant +2 mod 4 and every one of them still passes -- test_directions,
 * test_rate_ceiling, test_poll_shapes, test_case_discrimination, and even
 * `phase_seen != 0x0F`, which reads $D under the inversion.  This digest
 * folds the raw register words, so a constant phase offset moves it. */
#define EXPECT_DIGEST_FULL_WITH_MOUSE 0x5E1858EAu

/* Fifth sweep (#513): the same register domain with a Team Tap on BOTH
 * ports and all four sockets of each port seeded.  Like the mouse digest
 * this is measured on the feature, not on develop -- develop cannot
 * produce it.  It is what stops a tap that decodes to the wrong socket,
 * the wrong row, or nothing at all from passing: every other tap
 * assertion below tests one bit or one button, and a table with two
 * entries transposed would satisfy them all. */
#define EXPECT_DIGEST_TEAMTAP 0x480880C4u

/* Detection probe, verbatim from the Joypad-TeamTap Tester (Domin, 2000)
 * at file offsets 0x272 / 0x2F2: socket 3 row 1 on the port under test,
 * the other port's nibble parked at $F.  TR10: bit CLEAR means an adaptor
 * is present. */
#define TAP_PROBE_P1 0x81FAu     /* port-1 nibble $A, port 2 parked */
#define TAP_PROBE_P2 0x815Fu     /* port-2 nibble $5, port 1 parked */
#define TAP_DETECT_BIT_P1 0x0001u  /* JOYBUTS B0 */
#define TAP_DETECT_BIT_P2 0x0004u  /* JOYBUTS B2 */

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

/* ---- the sweep ------------------------------------------------------- *
 * Factored out so it can be run a second time with a port-2 device
 * attached.  The RNG is re-seeded here, so both runs see the identical
 * button vectors in the identical order -- that is what makes the two
 * port-1 digests directly comparable. */

static uint16_t (*p_JoystickReadWord)(uint32_t);
static void     (*p_JoystickWriteWord)(uint32_t, uint16_t);
static uint8_t  *p_joypad0Buttons;
static uint8_t  *p_joypad1Buttons;
static struct VJSettings *p_vjs;

static void run_sweep(uint32_t *out_full, uint32_t *out_port1,
                      unsigned long *out_reads)
{
   uint32_t digest_full  = FNV1A_OFFSET;
   uint32_t digest_port1 = FNV1A_OFFSET;
   unsigned long reads   = 0;
   int ntsc_idx;
   unsigned hi_idx, lo, vec, slot, off;

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

   *out_full  = digest_full;
   *out_port1 = digest_port1;
   *out_reads = reads;
}

/* ---- the Team Tap sweep (#513) --------------------------------------
 *
 * A SEPARATE function, deliberately.  run_sweep() above and its three
 * constants are the identity proof; the moment its loop bounds change,
 * those constants move and the proof is gone.  This one seeds all
 * JOYPAD_BUTTON_SLOTS (84) slots per port instead of the socket-0 21,
 * and is only ever run with taps attached.
 *
 * Seeding sockets 1-3 is the load-bearing part.  Without it a tap that
 * resolved every non-socket-0 code to an all-released socket would fold
 * exactly the same bytes as one that resolved them correctly. */
static void run_teamtap_sweep(uint32_t *out_full, unsigned long *out_reads)
{
   uint32_t digest     = FNV1A_OFFSET;
   unsigned long reads = 0;
   int ntsc_idx;
   unsigned hi_idx, lo, vec, slot, off;

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
               for (slot = 0; slot < JOYPAD_BUTTON_SLOTS; slot++)
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
                  fold_word(&digest, p_JoystickReadWord(off));
                  reads++;
               }
            }
         }
      }
   }

   *out_full  = digest;
   *out_reads = reads;
}

/* Run the tester ROM's detection probe on one port and report whether the
 * detect bit came back SET (1 = "no adaptor", per TR10). */
static int tap_probe_bit_set(int port)
{
   p_JoystickWriteWord(0, (uint16_t)(port == 0 ? TAP_PROBE_P1
                                               : TAP_PROBE_P2));
   return (p_JoystickReadWord(2)
           & (uint16_t)(port == 0 ? TAP_DETECT_BIT_P1 : TAP_DETECT_BIT_P2))
          ? 1 : 0;
}

/* Is a press in port 1's tap socket 2, row 0, visible at $F14000?
 *
 * Socket 2 row 0 is row code $4 on port 1 (the other nibble is parked at
 * $F, which addresses socket 3 -- unreadable unless a tap is attached).
 * Row 0's first line is BUTTON_U, which the read path drives on
 * $F14000 bit 8, active low. */
static int tap_socket2_up_visible(void)
{
   unsigned slot = 2u * JOYPAD_SOCKET_SLOTS + (unsigned)BUTTON_U;
   uint16_t got;

   memset(p_joypad0Buttons, 0, JOYPAD_BUTTON_SLOTS);
   memset(p_joypad1Buttons, 0, JOYPAD_BUTTON_SLOTS);
   p_joypad0Buttons[slot] = 0xFF;

   p_JoystickWriteWord(0, 0x80F4u);
   got = p_JoystickReadWord(0);

   memset(p_joypad0Buttons, 0, JOYPAD_BUTTON_SLOTS);
   return (got & 0x0100u) ? 0 : 1;
}

int main(int argc, char **argv)
{
   harness_config cfg = HARNESS_CONFIG_DEFAULT;
   harness_result results[12];
   unsigned num_results = 0;
   int print_only = 0;
   int i;
   int ok_full, ok_port1, ok_mouse_port1, ok_mouse_full;
   int ok_tap_detect, ok_tap_isolated, ok_tap_readable;
   int ok_tap_digest, ok_tap_restore;

   void (*p_InputDevSetType)(int, InputDevType);
   void (*p_InputDevReset)(void);
   void (*p_InputDevFeed)(int, int32_t, int32_t, uint32_t);
   void (*p_JoystickSetTeamTap)(int, bool);

   uint32_t digest_full  = FNV1A_OFFSET;
   uint32_t digest_port1 = FNV1A_OFFSET;
   unsigned long reads   = 0;

   uint32_t mouse_full   = FNV1A_OFFSET;
   uint32_t mouse_port1  = FNV1A_OFFSET;
   unsigned long mouse_reads = 0;

   uint32_t tap_full     = FNV1A_OFFSET;
   unsigned long tap_reads = 0;
   uint32_t restore_full  = FNV1A_OFFSET;
   uint32_t restore_port1 = FNV1A_OFFSET;
   unsigned long restore_reads = 0;
   int p1_off = 1, p1_on = 0, p2_off = 1, p2_on = 0;
   int p2_while_p1_on = 0, sock_off = 1, sock_on = 0;

   char detail_full[192];
   char detail_port1[192];
   char detail_mouse[192];
   char detail_mouse_full[192];
   char detail_tap_detect[192];
   char detail_tap_isolated[192];
   char detail_tap_readable[192];
   char detail_tap_digest[192];
   char detail_tap_restore[192];

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

   /* The device-layer symbols are resolved BEFORE the pad-only sweep so a
    * build that cannot run the mouse assertion fails loudly here rather
    * than half-running.  A skip is exit 77 (never a silent 0): the live
    * trigger for this branch is an exports-test.list / link-test.T
    * divergence, which is a build bug in this repo, not an environment. */
   p_InputDevSetType = (void (*)(int, InputDevType))
                          harness_dlsym(&cfg, "InputDevSetType");
   p_InputDevReset   = (void (*)(void))harness_dlsym(&cfg, "InputDevReset");
   p_InputDevFeed    = (void (*)(int, int32_t, int32_t, uint32_t))
                          harness_dlsym(&cfg, "InputDevFeed");
   p_JoystickSetTeamTap = (void (*)(int, bool))
                          harness_dlsym(&cfg, "JoystickSetTeamTap");

   if (!p_JoystickSetTeamTap)
   {
      fprintf(stderr,
              "joymatrix_identity: JoystickSetTeamTap is not in the test "
              "ABI, so the Team Tap assertions cannot run.  Since #513 it "
              "is covered by the Joystick* wildcard in exports-test.list / "
              "link-test.T.  Exiting 77 (skip), NOT 0.\n");
      harness_shutdown(&cfg);
      return 77;
   }

   if (!p_InputDevSetType || !p_InputDevReset || !p_InputDevFeed)
   {
      fprintf(stderr,
              "joymatrix_identity: InputDevSetType/Reset/Feed are not in the "
              "test ABI, so the port-2-mouse assertions cannot run.  Since "
              "#429 these are part of the wide ABI -- check exports-test.list "
              "and link-test.T.  Exiting 77 (skip), NOT 0.\n");
      harness_shutdown(&cfg);
      return 77;
   }

   run_sweep(&digest_full, &digest_port1, &reads);

   /* Second sweep, identical domain, with an ST mouse on port 2.
    *
    * The feed is clamped to QUAD_MAX_BACKLOG (64) by QuadFeed, and the
    * encoder is clocked at most once per read whose port-2 row select is
    * asserted, so it drains over the first 64 qualifying polls of the
    * ~131,072 in the sweep and then holds a fixed phase for the rest.
    * That is deliberate and sufficient: the polarity class this digest
    * exists to catch shifts the held phase by a constant, which moves the
    * folded bits on every one of those reads.  (Re-feeding inside
    * run_sweep() would put mouse-specific code in the function that also
    * produces digests 1 and 2 -- the one place this file must not
    * perturb.)  Both buttons are held for the whole sweep. */
   p_InputDevSetType(1, INPUTDEV_MOUSE_ST);
   p_InputDevReset();
   p_InputDevFeed(1, 4000, -4000, 0x03);

   run_sweep(&mouse_full, &mouse_port1, &mouse_reads);

   p_InputDevSetType(1, INPUTDEV_PAD);
   p_InputDevReset();

   /* ---- Team Tap (#513) ---------------------------------------------
    *
    * The detection probe first, with NO tap, so the "no adaptor" answer
    * is measured on the same build that has to produce the "adaptor
    * present" one -- a test that only ever checked the present case
    * would pass on a core that cleared the bit unconditionally. */
   p1_off = tap_probe_bit_set(0);
   p2_off = tap_probe_bit_set(1);
   sock_off = tap_socket2_up_visible();

   p_JoystickSetTeamTap(0, true);
   p1_on          = tap_probe_bit_set(0);
   /* Port 2's own probe, with a tap on port 1 ONLY: the two adapters are
    * independent, so this must still report "no adaptor". */
   p2_while_p1_on = tap_probe_bit_set(1);
   sock_on        = tap_socket2_up_visible();

   p_JoystickSetTeamTap(1, true);
   p2_on = tap_probe_bit_set(1);

   /* Fifth sweep: both taps attached, all four sockets seeded. */
   run_teamtap_sweep(&tap_full, &tap_reads);

   /* And detach, then re-run the ORIGINAL sweep: identity has to come
    * back, or a session that ever selected a tap stays perturbed. */
   p_JoystickSetTeamTap(0, false);
   p_JoystickSetTeamTap(1, false);
   memset(p_joypad0Buttons, 0, JOYPAD_BUTTON_SLOTS);
   memset(p_joypad1Buttons, 0, JOYPAD_BUTTON_SLOTS);

   run_sweep(&restore_full, &restore_port1, &restore_reads);

   ok_full        = (digest_full  == EXPECT_DIGEST_FULL);
   ok_port1       = (digest_port1 == EXPECT_DIGEST_PORT1);
   ok_mouse_port1 = (mouse_port1  == EXPECT_DIGEST_PORT1);
   ok_mouse_full  = (mouse_full   == EXPECT_DIGEST_FULL_WITH_MOUSE);
   ok_tap_detect  = (p1_off == 1 && p1_on == 0 && p2_off == 1 && p2_on == 0);
   ok_tap_isolated = (p2_while_p1_on == 1);
   ok_tap_readable = (sock_off == 0 && sock_on == 1);
   ok_tap_digest   = (tap_full == EXPECT_DIGEST_TEAMTAP);
   ok_tap_restore  = (restore_full  == EXPECT_DIGEST_FULL
                      && restore_port1 == EXPECT_DIGEST_PORT1);

   if (print_only)
   {
      printf("joymatrix_identity: sweep = %d ntsc x %d hi-bytes x %d row bytes"
             " x %d vectors x 4 offsets = %lu reads (seed 0x%08X)\n",
             2, 4, SWEEP_ROW_BYTES, SWEEP_VECTORS, reads,
             (unsigned)SWEEP_SEED);
      printf("joymatrix_identity: EXPECT_DIGEST_FULL            = 0x%08Xu\n",
             digest_full);
      printf("joymatrix_identity: EXPECT_DIGEST_PORT1           = 0x%08Xu\n",
             digest_port1);
      printf("joymatrix_identity: EXPECT_DIGEST_FULL_WITH_MOUSE = 0x%08Xu\n",
             mouse_full);
      printf("joymatrix_identity: (port-1 bits with the mouse    = 0x%08X, "
             "must equal EXPECT_DIGEST_PORT1)\n", mouse_port1);
      printf("joymatrix_identity: EXPECT_DIGEST_TEAMTAP         = 0x%08Xu"
             "  (%lu reads, all %d slots/port seeded)\n",
             tap_full, tap_reads, (int)JOYPAD_BUTTON_SLOTS);
      printf("joymatrix_identity: (identity after detaching     = 0x%08X / "
             "0x%08X, must equal FULL / PORT1)\n",
             restore_full, restore_port1);
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

   /* Third assertion: a port-2 mouse must not perturb port 1's own bits.
    * The mouse's overlay drives $F14000 bits 12-15 and $F14002 bits 2-3
    * in EVERY row (it is row-blind), which is precisely the shape of
    * change that could leak sideways if a mask were written wrong. */
   sprintf(detail_mouse,
           "port-1 bits with an ST mouse on port 2: 0x%08X (expected 0x%08X)",
           mouse_port1, (unsigned)EXPECT_DIGEST_PORT1);

   results[num_results].status = ok_mouse_port1 ? "PASS" : "FAIL";
   results[num_results].name   = "joymatrix_identity_port1_with_mouse";
   results[num_results].detail = detail_mouse;
   num_results++;

   /* Fourth assertion: the mouse's OWN bits.  Without this the third one
    * only proves the mouse did not break port 1 -- a mouse that emitted
    * nothing at all, or emitted the right waveform with inverted
    * polarity, would pass it. */
   sprintf(detail_mouse_full,
           "full digest with an ST mouse on port 2: 0x%08X (expected 0x%08X); "
           "%lu reads",
           mouse_full, (unsigned)EXPECT_DIGEST_FULL_WITH_MOUSE, mouse_reads);

   results[num_results].status = ok_mouse_full ? "PASS" : "FAIL";
   results[num_results].name   = "joymatrix_identity_full_with_mouse";
   results[num_results].detail = detail_mouse_full;
   num_results++;

   /* ---- Team Tap assertions (#513) ---------------------------------- */

   sprintf(detail_tap_detect,
           "socket 3 row 1: port 1 B0 %d->%d, port 2 B2 %d->%d "
           "(TR10: 1 = no adaptor, 0 = adaptor present)",
           p1_off, p1_on, p2_off, p2_on);
   results[num_results].status = ok_tap_detect ? "PASS" : "FAIL";
   results[num_results].name   = "teamtap_detect_probe";
   results[num_results].detail = detail_tap_detect;
   num_results++;

   sprintf(detail_tap_isolated,
           "port 2's detect bit with a tap on port 1 only: %d (expected 1)",
           p2_while_p1_on);
   results[num_results].status = ok_tap_isolated ? "PASS" : "FAIL";
   results[num_results].name   = "teamtap_detect_per_port";
   results[num_results].detail = detail_tap_isolated;
   num_results++;

   sprintf(detail_tap_readable,
           "port 1 socket 2 row 0 Up at $F14000 bit 8: visible=%d without a "
           "tap (expected 0), visible=%d with one (expected 1)",
           sock_off, sock_on);
   results[num_results].status = ok_tap_readable ? "PASS" : "FAIL";
   results[num_results].name   = "teamtap_socket_readable";
   results[num_results].detail = detail_tap_readable;
   num_results++;

   sprintf(detail_tap_digest,
           "full digest with a Team Tap on both ports: 0x%08X "
           "(expected 0x%08X); %lu reads, all %d slots/port seeded",
           tap_full, (unsigned)EXPECT_DIGEST_TEAMTAP, tap_reads,
           (int)JOYPAD_BUTTON_SLOTS);
   results[num_results].status = ok_tap_digest ? "PASS" : "FAIL";
   results[num_results].name   = "teamtap_full_digest";
   results[num_results].detail = detail_tap_digest;
   num_results++;

   /* THE ACCEPTANCE CONDITION, restated as a second measurement: the two
    * develop constants have to come back after the taps are detached. */
   sprintf(detail_tap_restore,
           "after detaching both taps: 0x%08X / 0x%08X "
           "(expected 0x%08X / 0x%08X)",
           restore_full, restore_port1,
           (unsigned)EXPECT_DIGEST_FULL, (unsigned)EXPECT_DIGEST_PORT1);
   results[num_results].status = ok_tap_restore ? "PASS" : "FAIL";
   results[num_results].name   = "teamtap_detach_restores_identity";
   results[num_results].detail = detail_tap_restore;
   num_results++;

   harness_report(&cfg, results, num_results);

   if (!ok_mouse_port1 || !ok_mouse_full)
      fprintf(stderr,
              "joymatrix_identity: the port-2 MOUSE overlay changed.  A "
              "moved full-with-mouse digest with digests 1 and 2 intact "
              "means the device layer changed, not the joystick path -- "
              "check the wiring table and the active-low polarity in "
              "src/jerry/inputdev.c before re-baselining anything.\n");

   if (!ok_full || !ok_port1)
      fprintf(stderr,
              "joymatrix_identity: $F14000/$F14002 behaviour CHANGED.  This "
              "test exists to catch exactly that.  If the change is "
              "deliberate, say so and re-baseline with --print in the same "
              "commit; do not silently update the constants.\n");

   if (!ok_tap_detect || !ok_tap_isolated || !ok_tap_readable
       || !ok_tap_digest)
      fprintf(stderr,
              "joymatrix_identity: the TEAM TAP decode changed (#513).  The "
              "row-code/socket table lives in src/jerry/joystick.c and comes "
              "from TR10 p.18; the detect bit is socket 3 row 1 only.  Fix "
              "the decode, do not re-baseline the digest.\n");

   harness_shutdown(&cfg);
   /* Every assertion gates the exit status.  (An earlier revision folded
    * the mouse result into ok_port1 instead; a FAIL row that does not reach
    * the exit code is the skip-as-pass class wearing a different hat.) */
   return (ok_full && ok_port1 && ok_mouse_port1 && ok_mouse_full
           && ok_tap_detect && ok_tap_isolated && ok_tap_readable
           && ok_tap_digest && ok_tap_restore) ? 0 : 1;
}
