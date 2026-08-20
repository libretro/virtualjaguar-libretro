/*
 * test/tools/procontroller_decode_test.c -- register-level test for the
 * Pro Controller's five extra buttons (#514).
 *
 * WHAT THIS PROVES
 * =================
 * Unlike the mouse (#429), rotary (#436), analog/driving (#437) and
 * paddle (#505) devices, the Pro Controller is NOT a new electrical
 * device and src/jerry/joystick.c was not touched to add it -- the spike
 * (docs/teamtap-procontroller-spike.md section 9, sourced from Atari's own
 * SDK header and developer newsletter, not the TR10 manual which never
 * mentions the device) found that its five extra buttons alias onto five
 * existing standard-matrix keypad slots:
 *
 *   Z  (Fire)            -> keypad 7  -> BUTTON_7 (row 1, column J9/J13)
 *   Y  (Fire)             -> keypad 8  -> BUTTON_8 (row 2, column J9/J13)
 *   X  (Fire)             -> keypad 9  -> BUTTON_9 (row 3, column J9/J13)
 *   Left shoulder         -> keypad 4  -> BUTTON_4 (row 1, column J10/J14)
 *   Right shoulder        -> keypad 6  -> BUTTON_6 (row 3, column J10/J14)
 *
 * This suite exercises the SHIPPING joystick.c decode directly -- the
 * same JoystickReadWord()/JoystickWriteWord() joymatrix_identity.c pins --
 * and asserts, for each of the five slots on each port, that pressing it:
 *
 *   1. clears EXACTLY the predicted $F14000 bit when the matching row is
 *      selected (active low, TR10 "Reading a zero means the appropriate
 *      button is depressed");
 *   2. moves NOTHING ELSE in $F14000 on that same read -- proven
 *      differentially (idle-baseline XOR pressed-reading must equal
 *      exactly the one predicted bit), so this does not need to guess at
 *      the row-echo bits JoystickReadWord's msk2[] tables also fold in;
 *   3. moves NOTHING AT ALL when a row OTHER than the predicted one is
 *      selected -- the aliasing is a single matrix intersection, not a
 *      device that leaks into every row;
 *   4. leaves $F14002 (A/B/C/Option/Pause + the NTSC bit) completely
 *      untouched -- these five slots are not in that register's mask[]
 *      table at all, and this is the check that would catch a transcription
 *      slipping one of them onto the wrong offset.
 *
 * This is a register-level guardrail, not game-level evidence.  No
 * detection method for the Pro Controller was ever published (spike
 * section 9.6), so no title can be shown to specifically require these
 * bindings; what this file pins is that the emulator's existing matrix
 * decode already produces the aliasing Atari's own SDK documents, exactly
 * as the spike concluded from reading joystick.c by hand.  The
 * libretro-layer preset that lets a user route RetroPad X/L1/R1/L2/R2 at
 * these five slots (libretro.c, "virtualjaguar_p1_device"/"_p2_device" =
 * "pad_pro") is a separate, thin remap this file does not need to touch --
 * see docs/input-devices-user-guide.md.
 *
 * USAGE
 *   ./test/tools/procontroller_decode_test ./virtualjaguar_libretro.dylib [rom]
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>

#include "../harness/harness.h"

/* Mirrors the joypadNButtons[] slot enum in src/jerry/joystick.h (same
 * hand-restatement convention as rotary_decode_test.c and
 * paddle_decode_test.c, so a renumbering there cannot silently propagate
 * here). */
#define SLOT_7  5    /* row 1, column J9/J13 -- Pro Controller Z */
#define SLOT_4  6    /* row 1, column J10/J14 -- Pro Controller Left shoulder */
#define SLOT_8  9    /* row 2, column J9/J13 -- Pro Controller Y */
#define SLOT_9  13   /* row 3, column J9/J13 -- Pro Controller X */
#define SLOT_6  14   /* row 3, column J10/J14 -- Pro Controller Right shoulder */
#define BUTTON_SLOTS 21

/* Socket-0 row-select words, output enable (bit 15) and audio enable
 * (bit 8) both set, matching the literal values a real driver (and
 * rotary_decode_test.c / joymatrix_identity.c) uses.  Low byte is
 * (port2 nibble << 4) | port1 nibble; 0xF parks the idle port on a code
 * that is not a socket-0 row, so it contributes nothing to the read. */
static const uint16_t row_word_p1[4] = { 0x81FE, 0x81FD, 0x81FB, 0x81F7 };
static const uint16_t row_word_p2[4] = { 0x817F, 0x81BF, 0x81DF, 0x81EF };

typedef struct
{
   const char *name;
   int         slot;   /* index into joypadNButtons[] */
   int         row;    /* 0-3, the row this slot lives in */
   int         col;    /* 0-3, the column within J8..J11 / J12..J15 */
} pro_slot;

static const pro_slot slots[5] = {
   { "Z (keypad 7)",             SLOT_7, 1, 1 },
   { "Y (keypad 8)",             SLOT_8, 2, 1 },
   { "X (keypad 9)",             SLOT_9, 3, 1 },
   { "Left shoulder (keypad 4)", SLOT_4, 1, 2 },
   { "Right shoulder (keypad 6)",SLOT_6, 3, 2 },
};

static uint16_t (*p_JoystickReadWord)(uint32_t);
static void     (*p_JoystickWriteWord)(uint32_t, uint16_t);
static uint8_t  *p_joypadButtons[2];

/* Runs one (port, slot) combination across all four rows.  Returns 0 on
 * success; on failure prints the mismatch and returns 1 -- kept as a
 * return code rather than aborting so a single bad slot does not hide
 * failures in the other four. */
static int check_slot(int port, const pro_slot *s, char *detail, size_t detail_size)
{
   const uint16_t *row_word = (port == 0) ? row_word_p1 : row_word_p2;
   uint16_t predicted_bit = (uint16_t)(1u << (8 + (port == 1 ? 4 : 0) + s->col));
   int row;

   for (row = 0; row < 4; row++)
   {
      uint16_t baseline, pressed, diff;
      uint16_t f14002_baseline, f14002_pressed;

      memset(p_joypadButtons[port], 0, BUTTON_SLOTS);
      p_JoystickWriteWord(0, row_word[row]);

      baseline         = p_JoystickReadWord(0);
      f14002_baseline  = p_JoystickReadWord(2);

      p_joypadButtons[port][s->slot] = 0xff;   /* press */

      pressed          = p_JoystickReadWord(0);
      f14002_pressed   = p_JoystickReadWord(2);

      p_joypadButtons[port][s->slot] = 0;      /* release, for the next row */

      diff = (uint16_t)(baseline ^ pressed);

      if (row == s->row)
      {
         if (diff != predicted_bit)
         {
            snprintf(detail, detail_size,
                     "port %d %s: row %d expected diff 0x%04X (bit %d), "
                     "got 0x%04X (baseline 0x%04X, pressed 0x%04X)",
                     port + 1, s->name, row, predicted_bit,
                     8 + (port == 1 ? 4 : 0) + s->col, diff, baseline, pressed);
            return 1;
         }
         if (pressed & predicted_bit)
         {
            snprintf(detail, detail_size,
                     "port %d %s: row %d -- predicted bit is SET (should be "
                     "active-low CLEAR) when pressed: 0x%04X",
                     port + 1, s->name, row, pressed);
            return 1;
         }
      }
      else if (diff != 0)
      {
         snprintf(detail, detail_size,
                  "port %d %s: pressed on row %d (its own row is %d) but "
                  "$F14000 moved anyway -- diff 0x%04X (baseline 0x%04X, "
                  "pressed 0x%04X)",
                  port + 1, s->name, row, s->row, diff, baseline, pressed);
         return 1;
      }

      if (f14002_baseline != f14002_pressed)
      {
         snprintf(detail, detail_size,
                  "port %d %s: row %d -- $F14002 moved (0x%04X -> 0x%04X); "
                  "these five slots are not in that register's mask table",
                  port + 1, s->name, row, f14002_baseline, f14002_pressed);
         return 1;
      }
   }

   snprintf(detail, detail_size,
            "port %d %s: predicted bit %d, all 4 rows clean",
            port + 1, s->name, 8 + (port == 1 ? 4 : 0) + s->col);
   return 0;
}

int main(int argc, char **argv)
{
   harness_config cfg = HARNESS_CONFIG_DEFAULT;
   harness_result results[10];
   /* One persistent buffer per result slot -- a single buffer reused
    * across loop iterations would leave every harness_result.detail
    * pointer aliasing the same (reused) stack address by the time
    * harness_report() prints them all at the end. */
   char details[10][192];
   unsigned num_results = 0;
   int port, i;
   int all_ok = 1;

   cfg.frames = 0;
   if (!harness_init_from_args(&cfg, argc, argv))
      return 1;

   if (!cfg.rom_path)
      cfg.rom_path = "test/roms/yarc.j64";
   if (!harness_load_rom(&cfg))
      return 1;

   p_JoystickReadWord  = (uint16_t (*)(uint32_t))
                            harness_dlsym(&cfg, "JoystickReadWord");
   p_JoystickWriteWord = (void (*)(uint32_t, uint16_t))
                            harness_dlsym(&cfg, "JoystickWriteWord");
   p_joypadButtons[0]  = (uint8_t *)harness_dlsym(&cfg, "joypad0Buttons");
   p_joypadButtons[1]  = (uint8_t *)harness_dlsym(&cfg, "joypad1Buttons");

   if (!p_JoystickReadWord || !p_JoystickWriteWord ||
       !p_joypadButtons[0] || !p_joypadButtons[1])
   {
      fprintf(stderr,
              "procontroller_decode_test: missing test-ABI symbols.  The "
              "wide test ABI must export Joystick* / joypad0Buttons / "
              "joypad1Buttons.  Build with TEST_EXPORTS=1.\n");
      harness_shutdown(&cfg);
      return 1;
   }

   for (port = 0; port < 2; port++)
   {
      for (i = 0; i < 5; i++)
      {
         int rc = check_slot(port, &slots[i], details[num_results],
                              sizeof(details[num_results]));

         results[num_results].status = rc ? "FAIL" : "PASS";
         results[num_results].name   = slots[i].name;
         results[num_results].detail = details[num_results];
         num_results++;

         if (rc)
            all_ok = 0;
      }
   }

   /* Restore idle state before shutdown, matching the other decode
    * tests' hygiene. */
   memset(p_joypadButtons[0], 0, BUTTON_SLOTS);
   memset(p_joypadButtons[1], 0, BUTTON_SLOTS);
   p_JoystickWriteWord(0, 0x81FF);

   harness_report(&cfg, results, num_results);

   if (!all_ok)
      fprintf(stderr,
              "procontroller_decode_test: the Pro Controller aliasing "
              "(docs/teamtap-procontroller-spike.md section 9.4/9.5) no "
              "longer matches src/jerry/joystick.c's matrix decode.  If "
              "joystick.c changed deliberately, this file's slot table "
              "needs re-deriving from the new row/column layout, not "
              "silently patched to match.\n");

   harness_shutdown(&cfg);
   return all_ok ? 0 : 1;
}
