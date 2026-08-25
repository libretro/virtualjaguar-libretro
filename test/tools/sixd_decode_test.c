/*
 * test/tools/sixd_decode_test.c -- TR10 6D controller register-level test
 * (#538).
 *
 * SYNTHETIC VERIFICATION, CLEARLY LABELLED AS SUCH
 * ================================================
 * NO SOFTWARE ANYWHERE DRIVES THIS CONTROLLER.  Atari never shipped it;
 * #538 frames the work as completionism.  There is therefore no game to
 * boot against, and THIS SUITE IS THE DEVICE'S ENTIRE VERIFICATION:
 * synthetic host state goes in through InputDevFeed6D(), and a driver
 * written here -- independently, from the Jaguar Technical Reference V10
 * tables on pages 15, 16, 21, 22 and 27-28 -- scans the three banks out
 * of $F14000 / $F14002 exactly as TR10 tells a 68K game to, and asserts
 * the decoded values.  Passing this proves the emulated device matches
 * the manual.  It does NOT prove any program is happy with it.
 *
 * WHAT IS PINNED
 *   - The twelve-cell J-line map (TR10 p.22), driven with SIX PAIRWISE
 *     DISTINCT nibble patterns so that any transposition of two cells
 *     fails.  Feeding a uniform value would make a bank-0 row-1/row-2
 *     swap invisible, which is the single likeliest transcription bug.
 *   - Three banks cycling 0 -> 1 -> 2 -> 0 on the row-3 -> row-0
 *     transition of the OWN socket, including TR10's interleaving rule
 *     (reads of the other socket between banks must not desynchronise
 *     the device).
 *   - The B column in all three banks: A/B/C/D ascending in bank 0,
 *     Rezero/G/F/E DESCENDING in bank 1 (as printed), and bank 2's
 *     1,1,1,0 type identifier read rows 3..0 -- "6D Controller" in
 *     TR10 p.16's last-bank table.  That last zero is a line held low
 *     with no switch behind it, which #437's all-ones identifier never
 *     needed and is therefore easy to omit.
 *   - Identification: C2 C3 = 0 1 ("Bank Switching") with the
 *     PORT-DEPENDENT C-row mapping from TR10 p.15 (C2 is row 2 on port 1
 *     but row 3 on port 2), and the row-0 bank-0 flag.
 *   - TR10 p.23's sign conventions as we resolved them: +X leftward,
 *     +Y up, +Z toward the user -- all three opposite the host -- and
 *     the three torques passed through unnegated.  The X assertion is
 *     the checkable half of a documented CONTRADICTION on that page (its
 *     figure draws +X to the right; its prose says right-to-left).
 *   - ENGAGEMENT: a selected-but-never-fed device is bit-identical to a
 *     pad, the liveness guardrail's core half.
 *   - Tuning through the shared axistune layer, with the documented
 *     split of six DOF over two tuning slots.
 *   - Savestate: the v12 chunk grew to 65 bytes, and a mid-scan rollback
 *     replays the remaining banks identically.
 *
 * USAGE
 *   ./test/tools/sixd_decode_test ./virtualjaguar_libretro.dylib [rom]
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>

#include "../harness/harness.h"

/* Mirrors src/jerry/inputdev.h -- restated by hand on purpose, so a
 * renumbering there cannot silently propagate here (same reasoning as
 * mouse_decode_test.c and analog_decode_test.c). */
typedef enum {
   DEV_PAD                 = 0,
   DEV_MOUSE_ST            = 1,
   DEV_MOUSE_AMIGA_ADAPTER = 2,
   DEV_MOUSE_AMIGA_ON_ST   = 3,
   DEV_ROTARY              = 4,
   DEV_ANALOG              = 5,
   DEV_DRIVING             = 6,
   DEV_LIGHTGUN            = 7,
   DEV_PADDLE              = 8,
   DEV_6D                  = 9
} InputDevType;

#define SW_A       0x0001
#define SW_B       0x0002
#define SW_C       0x0004
#define SW_D       0x0008
#define SW_E       0x0100
#define SW_F       0x0200
#define SW_G       0x0400
#define SW_REZERO  0x0800

#define AX_X   0
#define AX_Y   1
#define AX_Z   2
#define AX_TX  3
#define AX_TY  4
#define AX_TZ  5
#define AX_N   6

/* Mirrors INPUTDEV_STATE_SIZE: the pre-#437 41 bytes, plus 2 ports x
 * (bank + last row + ADC X + ADC Y + two-byte switch mask + six DOF
 * bytes). */
#define INPUTDEV_STATE_SIZE_EXPECTED 65

/* Socket-0 row-select words, output enable set (bit 15).  Port 1 rows
 * live in the low nibble, port 2 rows in the high nibble; the unused
 * port's nibble is F (no row). */
static const uint16_t row_word_p1[4] = { 0x81FE, 0x81FD, 0x81FB, 0x81F7 };
static const uint16_t row_word_p2[4] = { 0x817F, 0x81BF, 0x81DF, 0x81EF };

/* $F14000 J-line base bit and $F14002 column bits per port. */
static const int j_base[2] = { 8, 12 };
static const int c_bit[2]  = { 0, 2 };
static const int b_bit[2]  = { 1, 3 };

static const char *port_name[2] = { "port 1", "port 2" };
static const char *axis_name[AX_N] = { "X", "Y", "Z", "TX", "TY", "TZ" };

/* TR10 p.22's map, restated independently of inputdev.c: for each bank
 * and row, which DOF and which nibble the four J lines carry.  0x08 set
 * selects the high nibble. */
#define HI 0x08
static const uint8_t tr10_map[3][4] = {
   /* Bank 0 */ { AX_X,      AX_Y,      AX_Z,      AX_X | HI },
   /* Bank 1 */ { AX_TX,     AX_TY,     AX_TZ,     AX_Y | HI },
   /* Bank 2 */ { AX_TX | HI, AX_TY | HI, AX_TZ | HI, AX_Z | HI }
};

/* TR10 p.22's B column, restated the same way.  0 means "always 1";
 * FORCE_LOW is bank 2 row 0's unconditional zero. */
#define FORCE_LOW 0xFFFFu
static const uint16_t tr10_bcol[3][4] = {
   /* Bank 0 */ { SW_A,      SW_B, SW_C, SW_D },
   /* Bank 1 */ { SW_REZERO, SW_G, SW_F, SW_E },
   /* Bank 2 */ { FORCE_LOW, 0,    0,    0    }
};

/* ---- core entry points ---------------------------------------------- */

static uint16_t (*p_ReadWord)(uint32_t);
static void     (*p_WriteWord)(uint32_t, uint16_t);
static void     (*p_SetType)(int, InputDevType);
static void     (*p_SetTune)(int, int, int32_t, int32_t, int32_t);
static void     (*p_Reset)(void);
static void     (*p_Feed6D)(int, const int32_t *, uint32_t);
static InputDevType (*p_GetType)(int);
static size_t   (*p_StateSave)(uint8_t *);

static uint8_t  *p_joypad[2];

static size_t (*p_serialize_size)(void);
static bool   (*p_serialize)(void *, size_t);
static bool   (*p_unserialize)(const void *, size_t);

/* ---- reporting ------------------------------------------------------- */

static int  failures;
static char detail[320];

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

/* ---- the TR10 driver ------------------------------------------------- */

static uint16_t row_word(int port, int row)
{
   return port ? row_word_p2[row] : row_word_p1[row];
}

/* One row: write the select, read JOYSTICK and JOYBUTS (TR10 p.27's own
 * table shape: "this example assumes you are always reading both
 * registers"). */
static void scan_row(int port, int row, uint16_t *joy, uint16_t *but)
{
   p_WriteWord(0, row_word(port, row));
   *joy = p_ReadWord(0);
   *but = p_ReadWord(2);
}

/* One full bank in row order 0..3, as TR10 p.27 requires. */
static void scan_bank(int port, uint16_t joy[4], uint16_t but[4])
{
   int r;
   for (r = 0; r < 4; r++)
      scan_row(port, r, &joy[r], &but[r]);
}

static int bitv(uint16_t v, int bit)
{
   return (int)((v >> bit) & 1u);
}

static int nib(uint16_t joy, int port)
{
   return (joy >> j_base[port]) & 0x0F;
}

/* TR10 p.28's full recipe for a three-bank device: scan three banks into
 * a twelve-row table, then find bank 0 by the row-0 flag bit.  Returns
 * the table index (0, 4 or 8) at which bank 0's rows start. */
static int scan_banks(int port, uint16_t joy[12], uint16_t but[12])
{
   int b;

   for (b = 0; b < 3; b++)
      scan_bank(port, &joy[4 * b], &but[4 * b]);

   for (b = 0; b < 3; b++)
      if (bitv(but[4 * b], c_bit[port]) == 0)
         return 4 * b;

   return -1;
}

/* Reassemble all six DOF bytes from a bank-ordered twelve-row table. */
static void decode_axes(int port, const uint16_t joy[12], int bank0,
                        int out[AX_N])
{
   int b, r;

   for (r = 0; r < AX_N; r++)
      out[r] = 0;

   for (b = 0; b < 3; b++)
      for (r = 0; r < 4; r++)
      {
         uint8_t cell = tr10_map[b][r];
         int     v    = nib(joy[(bank0 + 4 * b) % 12 + r], port);

         if (cell & HI)
            out[cell & 0x07] |= v << 4;
         else
            out[cell & 0x07] |= v;
      }
}

static void attach(int port, InputDevType type)
{
   p_SetType(port, type);
   p_SetTune(port, 0, 0, 0, 256);
   p_SetTune(port, 1, 0, 0, 256);
   p_Reset();
   memset(p_joypad[port], 0x00, 21);
}

/* Feed and thereby ENGAGE (libretro.c only feeds once an axis has proven
 * live; at this layer the feed IS the engagement). */
static void feed(int port, const int32_t axes[AX_N], uint32_t sw)
{
   p_Feed6D(port, axes, sw);
}

static void axes_centre(int32_t a[AX_N])
{
   int i;
   for (i = 0; i < AX_N; i++)
      a[i] = 0;
}

/* ---- individual checks ----------------------------------------------- */

/* The liveness guardrail's core half: selected but never fed reads
 * bit-identical to a pad, controller-type probe included. */
static void test_inert_until_fed(int port)
{
   uint16_t joy_pad[4], but_pad[4], joy_6d[4], but_6d[4];
   int r, same;

   printf("%s: inert until engaged (liveness guardrail)\n",
          port_name[port]);

   attach(port, DEV_PAD);
   scan_bank(port, joy_pad, but_pad);

   attach(port, DEV_6D);
   scan_bank(port, joy_6d, but_6d);

   same = 1;
   for (r = 0; r < 4; r++)
      if (joy_6d[r] != joy_pad[r] || but_6d[r] != but_pad[r])
         same = 0;

   report(same,
          "a selected-but-never-fed 6D device reads bit-identical to a "
          "pad in every row of both registers");

   attach(port, DEV_PAD);
}

/* The twelve-cell J map, driven with six pairwise-distinct nibble pairs
 * so that no transposition of two cells can pass.
 *
 * The target bytes are chosen in the DEVICE's domain and the host feed is
 * derived from them, rather than the other way round, so this test states
 * the table and not the arithmetic. */
static void test_nibble_map(int port)
{
   /* X=0x12 Y=0x34 Z=0x56 TX=0x78 TY=0x9A TZ=0xBC: all twelve nibbles
    * distinct except where the same digit appears in different cells,
    * and no two CELLS share a (value, position) pair. */
   static const int want[AX_N] = { 0x12, 0x34, 0x56, 0x78, 0x9A, 0xBC };
   int32_t  axes[AX_N];
   uint16_t joy[12], but[12];
   int      got[AX_N];
   int      bank0, i, ok;

   printf("%s: J-line nibble map (TR10 p.22, twelve cells)\n",
          port_name[port]);

   attach(port, DEV_6D);

   /* inputdev_analog_byte maps host v16 -> 128 + clamp(v16/258), and the
    * three translations are negated on the way (TR10 p.23).  Invert that
    * to get a host value that lands on each target byte exactly. */
   for (i = 0; i < AX_N; i++)
   {
      int32_t d = (int32_t)want[i] - 128;
      if (i == AX_X || i == AX_Y || i == AX_Z)
         d = -d;
      axes[i] = d * 258;
   }

   feed(port, axes, 0);

   bank0 = scan_banks(port, joy, but);
   sprintf(detail, "bank 0 located by the row-0 flag bit (index %d)",
           bank0);
   report(bank0 >= 0, detail);
   if (bank0 < 0)
   {
      attach(port, DEV_PAD);
      return;
   }

   decode_axes(port, joy, bank0, got);

   ok = 1;
   for (i = 0; i < AX_N; i++)
      if (got[i] != want[i])
      {
         ok = 0;
         sprintf(detail, "%s reads 0x%02X, expected 0x%02X",
                 axis_name[i], got[i], want[i]);
         report(0, detail);
      }

   if (ok)
      report(1,
             "all six DOF reassemble from their twelve nibble cells "
             "(X=12 Y=34 Z=56 TX=78 TY=9A TZ=BC)");

   attach(port, DEV_PAD);
}

/* TR10 p.23's sign conventions, and the checkable half of its own
 * contradiction about X. */
static void test_sign_conventions(int port)
{
   int32_t  axes[AX_N];
   uint16_t joy[12], but[12];
   int      got[AX_N];
   int      bank0;

   printf("%s: TR10 p.23 sign conventions\n", port_name[port]);

   attach(port, DEV_6D);
   axes_centre(axes);
   axes[AX_X]  =  32767;   /* host: stick full RIGHT */
   axes[AX_Y]  =  32767;   /* host: stick full DOWN  */
   axes[AX_Z]  =  32767;   /* host: pushed fully AWAY */
   axes[AX_TX] =  32767;
   axes[AX_TY] =  32767;
   axes[AX_TZ] =  32767;
   feed(port, axes, 0);

   bank0 = scan_banks(port, joy, but);
   if (bank0 < 0)
   {
      report(0, "bank 0 located");
      attach(port, DEV_PAD);
      return;
   }
   decode_axes(port, joy, bank0, got);

   sprintf(detail,
           "host full RIGHT reads X = %d (low) -- TR10 p.23 prose: "
           "\"X is positive right to left\".  ITS FIGURE DISAGREES; we "
           "follow the prose, and this is the one assertion to flip if "
           "hardware ever settles it",
           got[AX_X]);
   report(got[AX_X] == 1, detail);

   sprintf(detail,
           "host full DOWN reads Y = %d (low) -- TR10 \"Y is positive "
           "UP\"", got[AX_Y]);
   report(got[AX_Y] == 1, detail);

   sprintf(detail,
           "host pushed AWAY reads Z = %d (low) -- TR10 \"Z is positive "
           "coming BACK (towards the user)\"", got[AX_Z]);
   report(got[AX_Z] == 1, detail);

   sprintf(detail,
           "torques pass through unnegated: TX=%d TY=%d TZ=%d -- a NAMED "
           "GUESS, TR10's \"counter-clockwise facing the positive "
           "direction\" is not resolvable without hardware",
           got[AX_TX], got[AX_TY], got[AX_TZ]);
   report(got[AX_TX] == 255 && got[AX_TY] == 255 && got[AX_TZ] == 255,
          detail);

   /* Centre reads 128 on every DOF, and that is what an idle stick and a
    * frontend with no analog routed both produce. */
   axes_centre(axes);
   feed(port, axes, 0);
   bank0 = scan_banks(port, joy, but);
   decode_axes(port, joy, bank0, got);
   sprintf(detail,
           "centre reads 128 on all six DOF (got %d %d %d %d %d %d)",
           got[0], got[1], got[2], got[3], got[4], got[5]);
   report(got[0] == 128 && got[1] == 128 && got[2] == 128
          && got[3] == 128 && got[4] == 128 && got[5] == 128, detail);

   attach(port, DEV_PAD);
}

/* The B column across all three banks, including bank 1's descending
 * Rezero/G/F/E and bank 2's 1,1,1,0 type identifier. */
static void test_button_column(int port)
{
   static const uint16_t all_sw = SW_A | SW_B | SW_C | SW_D
                                | SW_E | SW_F | SW_G | SW_REZERO;
   int32_t  axes[AX_N];
   uint16_t joy[12], but[12];
   int      bank0, b, r, ok;

   printf("%s: B column across three banks (TR10 p.22, p.16)\n",
          port_name[port]);

   attach(port, DEV_6D);
   axes_centre(axes);

   /* Nothing pressed: only bank 2 row 0's unconditional zero is low. */
   feed(port, axes, 0);
   bank0 = scan_banks(port, joy, but);
   if (bank0 < 0)
   {
      report(0, "bank 0 located");
      attach(port, DEV_PAD);
      return;
   }

   ok = 1;
   for (b = 0; b < 3; b++)
      for (r = 0; r < 4; r++)
      {
         int want = (tr10_bcol[b][r] == FORCE_LOW) ? 0 : 1;
         int got  = bitv(but[(bank0 + 4 * b) % 12 + r], b_bit[port]);
         if (got != want)
         {
            ok = 0;
            sprintf(detail,
                    "no buttons: bank %d row %d B column = %d, expected "
                    "%d", b, r, got, want);
            report(0, detail);
         }
      }
   if (ok)
      report(1,
             "with nothing pressed, the only low B-column cell is bank 2 "
             "row 0 -- the last-bank identifier 1,1,1,0 = \"6D "
             "Controller\" (TR10 p.16)");

   /* Every button held: every switch-backed cell goes low, and bank 2's
    * three identifier ones STAY high (no switch is wired there). */
   feed(port, axes, all_sw);
   bank0 = scan_banks(port, joy, but);
   ok = 1;
   for (b = 0; b < 3; b++)
      for (r = 0; r < 4; r++)
      {
         int want = tr10_bcol[b][r] ? 0 : 1;
         int got  = bitv(but[(bank0 + 4 * b) % 12 + r], b_bit[port]);
         if (got != want)
         {
            ok = 0;
            sprintf(detail,
                    "all buttons: bank %d row %d B column = %d, expected "
                    "%d", b, r, got, want);
            report(0, detail);
         }
      }
   if (ok)
      report(1,
             "with A-G and Rezero all held, every switch-backed cell "
             "reads low and bank 2's three identifier ones stay high");

   /* One button at a time, to pin the ORDER -- bank 0 ascends A,B,C,D
    * and bank 1 DESCENDS Rezero,G,F,E, which is what TR10 prints and the
    * first thing a tester with silicon should check. */
   ok = 1;
   for (b = 0; b < 2; b++)
      for (r = 0; r < 4; r++)
      {
         int bb, rr, only = 1;

         feed(port, axes, tr10_bcol[b][r]);
         bank0 = scan_banks(port, joy, but);

         for (bb = 0; bb < 3; bb++)
            for (rr = 0; rr < 4; rr++)
            {
               int want = (bb == b && rr == r)
                        || (tr10_bcol[bb][rr] == FORCE_LOW) ? 0 : 1;
               if (bitv(but[(bank0 + 4 * bb) % 12 + rr], b_bit[port])
                     != want)
                  only = 0;
            }

         if (!only)
         {
            ok = 0;
            sprintf(detail,
                    "the switch at bank %d row %d does not land there "
                    "alone", b, r);
            report(0, detail);
         }
      }
   if (ok)
      report(1,
             "each of the eight switches lands in exactly its own cell: "
             "bank 0 = A,B,C,D ascending and bank 1 = Rezero,G,F,E "
             "DESCENDING, as printed");

   attach(port, DEV_PAD);
}

/* Three banks, cycling on the row-3 -> row-0 transition of the OWN
 * socket, plus the C-column identification field. */
static void test_bank_cycle_and_id(int port)
{
   int32_t  axes[AX_N];
   uint16_t joy[16], but[16];
   int      i, seen[3], ok;

   printf("%s: three-bank cycle and identification (TR10 p.16, p.27)\n",
          port_name[port]);

   attach(port, DEV_6D);
   axes_centre(axes);
   feed(port, axes, 0);

   /* Four consecutive bank scans: the flag bit must show 0 exactly once
    * per three, i.e. the cycle length is three, not two. */
   for (i = 0; i < 4; i++)
      scan_bank(port, &joy[4 * i], &but[4 * i]);

   seen[0] = seen[1] = seen[2] = 0;
   for (i = 0; i < 3; i++)
      if (bitv(but[4 * i], c_bit[port]) == 0)
         seen[i] = 1;

   sprintf(detail,
           "the bank-0 flag appears exactly once in three consecutive "
           "scans (%d %d %d) -- a two-bank cycle would show it twice",
           seen[0], seen[1], seen[2]);
   report(seen[0] + seen[1] + seen[2] == 1, detail);

   report(bitv(but[0], c_bit[port]) == bitv(but[12], c_bit[port]),
          "the fourth scan returns to the first scan's bank -- the cycle "
          "is 0, 1, 2, 0 (TR10 p.22)");

   /* C column, rows 1-3: C1 = 1, C2 = 0, C3 = 1 -> C2 C3 = 0 1 =
    * "Bank Switching".  The C-row assignment is PORT-DEPENDENT: TR10
    * p.15's port-headed tables give port 1 rows 1/2/3 = C1/C2/C3 and
    * port 2 rows 1/2/3 = C1/C3/C2, so the single zero moves. */
   ok = 1;
   for (i = 1; i < 4; i++)
   {
      int want = (i == (port == 0 ? 2 : 3)) ? 0 : 1;
      if (bitv(but[i], c_bit[port]) != want)
      {
         ok = 0;
         sprintf(detail, "C column row %d = %d, expected %d", i,
                 bitv(but[i], c_bit[port]), want);
         report(0, detail);
      }
   }
   if (ok)
   {
      sprintf(detail,
              "C2 C3 = 0 1 (\"Bank Switching\") with C2 in row %d on %s",
              port == 0 ? 2 : 3, port_name[port]);
      report(1, detail);
   }

   attach(port, DEV_PAD);
}

/* TR10 p.27: "Controllers are expected to ignore any requests for rows
 * on other controllers.  Such requests must not cause the controller to
 * lose synchronisation or perform any bank switching." */
static void test_interleave_immunity(int port)
{
   const int other = port ^ 1;
   int32_t  axes[AX_N];
   uint16_t joy_a[12], but_a[12], joy_b[12], but_b[12];
   uint16_t junk_j, junk_b;
   int      bank0_a, bank0_b, b, r, same;

   printf("%s: interleaved reads of the other socket (TR10 p.27)\n",
          port_name[port]);

   attach(port, DEV_6D);
   axes_centre(axes);
   axes[AX_X] = 20000;
   feed(port, axes, SW_B);

   bank0_a = scan_banks(port, joy_a, but_a);

   /* The same three-bank scan again, but with a full four-row scan of
    * the OTHER socket wedged between each of our rows. */
   for (b = 0; b < 3; b++)
      for (r = 0; r < 4; r++)
      {
         int o;
         for (o = 0; o < 4; o++)
            scan_row(other, o, &junk_j, &junk_b);
         scan_row(port, r, &joy_b[4 * b + r], &but_b[4 * b + r]);
      }

   bank0_b = -1;
   for (b = 0; b < 3; b++)
      if (bitv(but_b[4 * b], c_bit[port]) == 0)
         bank0_b = 4 * b;

   same = (bank0_a >= 0 && bank0_b >= 0);
   if (same)
      for (b = 0; b < 12; b++)
      {
         int ia = (bank0_a + b) % 12;
         int ib = (bank0_b + b) % 12;
         if (joy_a[ia] != joy_b[ib] || but_a[ia] != but_b[ib])
            same = 0;
      }

   report(same,
          "a four-row scan of the other socket between every row leaves "
          "this device's three banks word-identical -- no lost "
          "synchronisation, no spurious bank switch");

   attach(port, DEV_PAD);
}

/* A 6D device on one port must not perturb the other port's bits. */
static void test_port_isolation(void)
{
   int32_t  axes[AX_N];
   uint16_t joy_ref[4], but_ref[4], joy_now[4], but_now[4];
   int      r, same;

   printf("port isolation\n");

   attach(0, DEV_PAD);
   attach(1, DEV_PAD);
   scan_bank(1, joy_ref, but_ref);

   attach(0, DEV_6D);
   axes_centre(axes);
   axes[AX_TZ] = -30000;
   feed(0, axes, SW_G);
   scan_bank(1, joy_now, but_now);

   same = 1;
   for (r = 0; r < 4; r++)
   {
      /* Port 2's own bits only: J12-J15 and B2/B3. */
      if ((joy_now[r] & 0xF000u) != (joy_ref[r] & 0xF000u)
          || (but_now[r] & 0x000Cu) != (but_ref[r] & 0x000Cu))
         same = 0;
   }

   report(same,
          "an engaged 6D controller on port 1 leaves every port-2 bit "
          "untouched");

   attach(0, DEV_PAD);
   attach(1, DEV_PAD);
}

/* The shared axistune layer (#439), absolute semantics, with the
 * documented two-slots-for-six-DOF split: host-horizontal-derived DOF
 * (X, TX) take tune_x and everything else takes tune_y. */
static void test_tuning(int port)
{
   int32_t  axes[AX_N];
   uint16_t joy[12], but[12];
   int      got[AX_N], bank0;

   printf("%s: shared per-axis tuning (#439)\n", port_name[port]);

   attach(port, DEV_6D);

   /* A dead zone on the X slot must move X and TX and leave the other
    * four alone. */
   p_SetTune(port, 0, 40, 0, 256);
   axes_centre(axes);
   axes[AX_X]  = 40 * 258;   /* exactly at the dead-zone edge */
   axes[AX_TX] = 40 * 258;
   axes[AX_Y]  = 40 * 258;
   feed(port, axes, 0);

   bank0 = scan_banks(port, joy, but);
   if (bank0 < 0)
   {
      report(0, "bank 0 located");
      attach(port, DEV_PAD);
      return;
   }
   decode_axes(port, joy, bank0, got);

   sprintf(detail,
           "a dead zone on the X tuning slot rebases X (%d) and TX (%d) "
           "to centre", got[AX_X], got[AX_TX]);
   report(got[AX_X] == 128 && got[AX_TX] == 128, detail);

   sprintf(detail,
           "the untouched Y slot leaves Y deflected (%d)", got[AX_Y]);
   report(got[AX_Y] != 128, detail);

   p_SetTune(port, 0, 0, 0, 256);
   attach(port, DEV_PAD);
}

/* The v12 chunk grew, and a mid-scan rollback replays identically. */
static void test_savestate(void)
{
   uint8_t  chunk[256];
   uint8_t *snap;
   size_t   n, sz;
   int32_t  axes[AX_N];
   uint16_t joy_c[4], but_c[4], joy_r[4], but_r[4];
   int      r, same;

   printf("savestate\n");

   attach(0, DEV_6D);
   axes_centre(axes);
   axes[AX_TY] = 25000;
   feed(0, axes, SW_E);

   memset(chunk, 0, sizeof(chunk));
   n = p_StateSave(chunk);
   sprintf(detail,
           "InputDevStateSave writes %u bytes, INPUTDEV_STATE_SIZE = %d "
           "(a mismatch means the trailing chunk moved without this "
           "constant)",
           (unsigned)n, INPUTDEV_STATE_SIZE_EXPECTED);
   report(n == (size_t)INPUTDEV_STATE_SIZE_EXPECTED, detail);

   if (!p_serialize_size || !p_serialize || !p_unserialize)
   {
      report(0, "retro_serialize symbols available");
      attach(0, DEV_PAD);
      return;
   }

   /* Scan one bank, snapshot mid-cycle, scan the second bank, roll back,
    * and scan again: the replay must be word-identical, which it can
    * only be if the bank counter and the six latches survived. */
   scan_bank(0, joy_c, but_c);

   sz   = p_serialize_size();
   snap = (uint8_t *)malloc(sz);
   if (!snap || !p_serialize(snap, sz))
   {
      report(0, "retro_serialize succeeded");
      free(snap);
      attach(0, DEV_PAD);
      return;
   }

   scan_bank(0, joy_c, but_c);

   if (!p_unserialize(snap, sz))
   {
      report(0, "retro_unserialize succeeded");
      free(snap);
      attach(0, DEV_PAD);
      return;
   }

   scan_bank(0, joy_r, but_r);

   same = 1;
   for (r = 0; r < 4; r++)
      if (joy_r[r] != joy_c[r] || but_r[r] != but_c[r])
         same = 0;

   report(same,
          "a rolled-back scan replays the next bank word-identically -- "
          "the three-bank counter and all six DOF latches survived");

   free(snap);
   attach(0, DEV_PAD);
}

int main(int argc, char **argv)
{
   harness_config cfg = HARNESS_CONFIG_DEFAULT;
   harness_result result;
   char           summary[128];
   int            port;

   cfg.frames = 0;
   if (!harness_init_from_args(&cfg, argc, argv))
      return 1;

   if (!cfg.rom_path)
      cfg.rom_path = "test/roms/yarc.j64";
   if (!harness_load_rom(&cfg))
      return 1;

   p_ReadWord   = (uint16_t (*)(uint32_t))harness_dlsym(&cfg, "JoystickReadWord");
   p_WriteWord  = (void (*)(uint32_t, uint16_t))harness_dlsym(&cfg, "JoystickWriteWord");
   p_SetType    = (void (*)(int, InputDevType))
                     harness_dlsym(&cfg, "InputDevSetType");
   p_SetTune    = (void (*)(int, int, int32_t, int32_t, int32_t))
                     harness_dlsym(&cfg, "InputDevSetTune");
   p_Reset      = (void (*)(void))harness_dlsym(&cfg, "InputDevReset");
   p_Feed6D     = (void (*)(int, const int32_t *, uint32_t))
                     harness_dlsym(&cfg, "InputDevFeed6D");
   p_GetType    = (InputDevType (*)(int))harness_dlsym(&cfg, "InputDevGetType");
   p_StateSave  = (size_t (*)(uint8_t *))harness_dlsym(&cfg, "InputDevStateSave");

   p_joypad[0] = (uint8_t *)harness_dlsym(&cfg, "joypad0Buttons");
   p_joypad[1] = (uint8_t *)harness_dlsym(&cfg, "joypad1Buttons");

   p_serialize_size = (size_t (*)(void))harness_dlsym(&cfg, "retro_serialize_size");
   p_serialize      = (bool (*)(void *, size_t))harness_dlsym(&cfg, "retro_serialize");
   p_unserialize    = (bool (*)(const void *, size_t))
                         harness_dlsym(&cfg, "retro_unserialize");

   if (!p_ReadWord || !p_WriteWord || !p_SetType || !p_SetTune || !p_Reset
       || !p_Feed6D || !p_GetType || !p_StateSave
       || !p_joypad[0] || !p_joypad[1])
   {
      fprintf(stderr,
              "sixd_decode_test: missing test-ABI symbols.  The wide "
              "test ABI must export Joystick*, InputDev* and "
              "joypadNButtons (exports-test.list and link-test.T).  "
              "Build with TEST_EXPORTS=1.\n");
      harness_shutdown(&cfg);
      return 1;
   }

   /* TR10's advanced-controller chapter restricts neither socket. */
   for (port = 0; port < 2; port++)
   {
      sprintf(detail, "InputDevSetType accepts a 6D controller on %s",
              port_name[port]);
      p_SetType(port, DEV_6D);
      report(p_GetType(port) == DEV_6D, detail);
      p_SetType(port, DEV_PAD);
   }

   for (port = 0; port < 2; port++)
   {
      test_inert_until_fed(port);
      test_nibble_map(port);
      test_sign_conventions(port);
      test_button_column(port);
      test_bank_cycle_and_id(port);
      test_interleave_immunity(port);
      test_tuning(port);
   }

   test_port_isolation();
   test_savestate();

   sprintf(summary, "%d failure(s)", failures);
   result.status = failures ? "FAIL" : "PASS";
   result.name   = "sixd_decode";
   result.detail = summary;
   harness_report(&cfg, &result, 1);

   harness_shutdown(&cfg);
   return failures ? 1 : 0;
}
