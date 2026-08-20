/*
 * test/tools/analog_decode_test.c -- TR10 bank-switching analog / driving
 * controller register-level test (#437).
 *
 * SYNTHETIC VERIFICATION, CLEARLY LABELLED AS SUCH
 * ================================================
 * No released Jaguar title reads this protocol -- Atari never shipped the
 * controller (research recorded in inputdev.h and
 * docs/input-devices-user-guide.md).  There is therefore no game to boot
 * against, and THIS SUITE IS THE DEVICE'S VERIFICATION: synthetic host
 * stick state goes in through InputDevFeedAnalog(), and a driver written
 * here -- independently, from the Jaguar Technical Reference V10 tables
 * ("Analogue Joystick and 'Driving' Controllers", "Identifying Controller
 * Types", "Reading Bank Switching Controllers") -- scans the banks out of
 * $F14000 / $F14002 exactly as TR10 tells a 68K game to, and asserts the
 * decoded values.
 *
 * WHAT IS PINNED
 *   - Bank 0 layout: X low/high nibbles in rows 0/1, Y in rows 2/3, on
 *     the port's four J lines; buttons A-D active low on the B column.
 *   - Bank 1 layout: hat switches in row 0, all-ones elsewhere.
 *   - Bank cycling on the row-3 -> row-0 transition of the OWN socket,
 *     including TR10's interleaving rule: reads of the other socket
 *     between banks must not desynchronise the device.
 *   - Controller identification: C2 C3 = 0 1 ("Bank Switching") with the
 *     port-dependent C-row mapping, the row-0 bank-0 flag, and the
 *     last-bank B-column 1111 ("Analogue Joystick or Driving
 *     Controller").
 *   - Value mapping: centre = 128, +X (right) high, +Y (stick FORWARD,
 *     i.e. libretro -Y) high.
 *   - ENGAGEMENT: a selected-but-never-fed device is bit-identical to a
 *     pad -- the liveness guardrail's core half.
 *   - Tuning through the shared axistune layer, absolute semantics:
 *     dead zone RE-BASES (edge reads centred, full deflection preserved),
 *     offset moves the rest position, exponent anchors at full
 *     deflection.
 *   - Savestate: the v12 chunk grew to 51 bytes, and a mid-scan rollback
 *     replays the remaining banks identically.
 *
 * USAGE
 *   ./test/tools/analog_decode_test ./virtualjaguar_libretro.dylib [rom]
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>

#include "../harness/harness.h"

/* Mirrors src/jerry/inputdev.h -- restated by hand on purpose, so a
 * renumbering there cannot silently propagate here (same reasoning as
 * mouse_decode_test.c). */
typedef enum {
   DEV_PAD                 = 0,
   DEV_MOUSE_ST            = 1,
   DEV_MOUSE_AMIGA_ADAPTER = 2,
   DEV_MOUSE_AMIGA_ON_ST   = 3,
   DEV_ROTARY              = 4,
   DEV_ANALOG              = 5,
   DEV_DRIVING             = 6
} InputDevType;

#define SW_A      0x01
#define SW_B      0x02
#define SW_C      0x04
#define SW_D      0x08
#define SW_UP     0x10
#define SW_DOWN   0x20
#define SW_LEFT   0x40
#define SW_RIGHT  0x80

/* Mirrors INPUTDEV_STATE_SIZE: the pre-#437 41 bytes plus 2 ports x
 * (bank + last row + ADC X + ADC Y + a two-byte switch mask + the 6D
 * controller's six DOF bytes, #538). */
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

/* ---- core entry points ---------------------------------------------- */

static uint16_t (*p_ReadWord)(uint32_t);
static void     (*p_WriteWord)(uint32_t, uint16_t);
static void     (*p_SetType)(int, InputDevType);
static void     (*p_SetTune)(int, int, int32_t, int32_t, int32_t);
static void     (*p_Reset)(void);
static void     (*p_FeedAnalog)(int, int32_t, int32_t, uint32_t);
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

/* One row: write the select, read JOYSTICK and JOYBUTS (TR10's own table
 * shape: "this example assumes you are always reading both registers"). */
static void scan_row(int port, int row, uint16_t *joy, uint16_t *but)
{
   p_WriteWord(0, row_word(port, row));
   *joy = p_ReadWord(0);
   *but = p_ReadWord(2);
}

/* One full bank in row order 0..3, as TR10 requires. */
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

/* The full TR10 recipe: every scan flips the bank on its own row-3 ->
 * row-0 transition, so a driver must read ALL banks into a table and
 * find bank 0 by the row-0 flag bit afterwards.  Returns the index (0 or
 * 4) of bank 0's rows within the 8-row table. */
static int scan_banks(int port, uint16_t joy[8], uint16_t but[8])
{
   scan_bank(port, &joy[0], &but[0]);
   scan_bank(port, &joy[4], &but[4]);
   return ((but[0] >> (port ? 2 : 0)) & 1u) == 0 ? 0 : 4;
}

static int nib(uint16_t joy, int port)
{
   return (joy >> j_base[port]) & 0x0F;
}

/* Decode an 8-bit value from a bank-0 scan: low nibble in `lo_row`, high
 * nibble in `lo_row + 1`. */
static int decode8(int port, const uint16_t joy[4], int lo_row)
{
   return nib(joy[lo_row], port) | (nib(joy[lo_row + 1], port) << 4);
}

static void attach(int port, InputDevType type)
{
   p_SetType(port, type);
   p_SetTune(port, 0, 0, 0, 256);
   p_SetTune(port, 1, 0, 0, 256);
   p_Reset();
   memset(p_joypad[port], 0x00, 21);
}

/* Feed and thereby ENGAGE (libretro.c only feeds once the stick has
 * proven live; at this layer the feed IS the engagement). */
static void feed(int port, int32_t x, int32_t y, uint32_t sw)
{
   p_FeedAnalog(port, x, y, sw);
}

/* ---- individual checks ----------------------------------------------- */

/* The liveness guardrail's core half: selected but never fed reads
 * bit-identical to a pad, controller-type probe included. */
static void test_inert_until_fed(int port)
{
   uint16_t joy_pad[4], but_pad[4], joy_an[4], but_an[4];
   int r, same;

   printf("%s: inert until engaged (liveness guardrail)\n",
          port_name[port]);

   attach(port, DEV_PAD);
   scan_bank(port, joy_pad, but_pad);

   attach(port, DEV_ANALOG);
   scan_bank(port, joy_an, but_an);

   same = 1;
   for (r = 0; r < 4; r++)
      if (joy_an[r] != joy_pad[r] || but_an[r] != but_pad[r])
         same = 0;

   report(same,
          "a selected-but-never-fed analog device reads bit-identical "
          "to a pad in every row of both registers");

   attach(port, DEV_PAD);
}

/* Bank 0: the X/Y nibbles, the buttons, the C column and the bank flag. */
static void test_bank0_layout(int port)
{
   uint16_t joy[4], but[4];
   int x, y;

   printf("%s: bank 0 layout (TR10 table)\n", port_name[port]);

   attach(port, DEV_ANALOG);
   /* +X full right, stick full FORWARD (libretro -Y). */
   feed(port, 32767, -32768, SW_A | SW_C);
   scan_bank(port, joy, but);

   x = decode8(port, joy, 0);
   y = decode8(port, joy, 2);

   sprintf(detail, "full right reads X = 255 (got %d)", x);
   report(x == 255, detail);
   sprintf(detail, "full forward reads Y = 255 (got %d) -- TR10 +Y is "
           "forward, libretro -Y", y);
   report(y == 255, detail);

   /* Buttons A-D active low on the B column, one per row. */
   report(bitv(but[0], b_bit[port]) == 0, "A held: row 0 B bit low");
   report(bitv(but[1], b_bit[port]) == 1, "B released: row 1 B bit high");
   report(bitv(but[2], b_bit[port]) == 0, "C held: row 2 B bit low");
   report(bitv(but[3], b_bit[port]) == 1, "D released: row 3 B bit high");

   /* Bank-0 flag: C column low in row 0. */
   report(bitv(but[0], c_bit[port]) == 0,
          "row 0 C column reads 0 (the bank-0 flag)");

   /* C1/C2/C3: C1 = 1 in row 1 on both ports; C2 = 0 in row 2 (port 1) /
    * row 3 (port 2); C3 = 1 in the other. */
   report(bitv(but[1], c_bit[port]) == 1, "row 1 reads C1 = 1");
   if (port == 0)
   {
      report(bitv(but[2], c_bit[port]) == 0, "row 2 reads C2 = 0 (port 1)");
      report(bitv(but[3], c_bit[port]) == 1, "row 3 reads C3 = 1 (port 1)");
   }
   else
   {
      report(bitv(but[2], c_bit[port]) == 1, "row 2 reads C3 = 1 (port 2)");
      report(bitv(but[3], c_bit[port]) == 0, "row 3 reads C2 = 0 (port 2)");
   }

   attach(port, DEV_PAD);
}

/* Centre and hard-left mappings. */
static void test_value_mapping(int port)
{
   uint16_t joy[8], but[8];
   int base, x, y;

   printf("%s: value mapping (two-bank scans, TR10 recipe)\n",
          port_name[port]);

   attach(port, DEV_ANALOG);
   feed(port, 0, 0, 0);
   base = scan_banks(port, joy, but);
   x = decode8(port, &joy[base], 0);
   y = decode8(port, &joy[base], 2);
   sprintf(detail, "centred stick reads X = Y = 128 (got %d / %d)", x, y);
   report(x == 128 && y == 128, detail);

   feed(port, -32768, 32767, 0);
   base = scan_banks(port, joy, but);
   x = decode8(port, &joy[base], 0);
   y = decode8(port, &joy[base], 2);
   sprintf(detail,
           "full left / full back reads X = Y = 1 (got %d / %d) -- the "
           "symmetric 1..255 range, TR10 forbids assuming the endpoints",
           x, y);
   report(x == 1 && y == 1, detail);

   attach(port, DEV_PAD);
}

/* Bank cycling, the bank-1 layout, and the type identification a TR10
 * driver derives from a full multi-bank scan. */
static void test_bank_cycle_and_id(int port)
{
   uint16_t joy[4], but[4];
   int r, ok;

   printf("%s: bank cycling and controller identification\n",
          port_name[port]);

   attach(port, DEV_ANALOG);
   feed(port, 0, 0, SW_UP | SW_LEFT);

   /* First full scan: this is bank 0 (flag low in row 0). */
   scan_bank(port, joy, but);
   report(bitv(but[0], c_bit[port]) == 0, "first scan is bank 0");

   /* The row-3 -> row-0 transition of the NEXT scan switches banks. */
   scan_bank(port, joy, but);
   report(bitv(but[0], c_bit[port]) == 1,
          "second scan reads the bank-1 flag (row-3 -> row-0 switched)");

   /* Bank 1 row 0: hat switches on the J lines, active low. */
   report(bitv(joy[0], j_base[port] + 0) == 0
          && bitv(joy[0], j_base[port] + 1) == 1
          && bitv(joy[0], j_base[port] + 2) == 0
          && bitv(joy[0], j_base[port] + 3) == 1,
          "bank 1 row 0 reads the hat: Up and Left low, Down and Right high");

   /* Bank 1 rows 1-3: J lines all high; B column 1 in every row -- the
    * last-bank 1111 that identifies "Analogue Joystick or Driving
    * Controller" among the bank-switching types. */
   ok = 1;
   for (r = 1; r < 4; r++)
      if (nib(joy[r], port) != 0x0F)
         ok = 0;
   report(ok, "bank 1 rows 1-3 read all 1s on the J lines");

   ok = 1;
   for (r = 0; r < 4; r++)
      if (bitv(but[r], b_bit[port]) != 1)
         ok = 0;
   report(ok, "bank 1 B column reads 1111: the analog/driving type ID");

   /* Third scan cycles back to bank 0. */
   scan_bank(port, joy, but);
   report(bitv(but[0], c_bit[port]) == 0, "third scan is bank 0 again");

   attach(port, DEV_PAD);
}

/* TR10: "It is acceptable to read a bank from one controller, followed by
 * a bank or multiple banks from other controllers, and then come back" --
 * other-socket rows must neither switch banks nor desynchronise. */
static void test_interleave_immunity(int port)
{
   uint16_t joy[4], but[4], j, b;
   int      other = port ^ 1;
   int      r;

   printf("%s: other-socket interleave immunity\n", port_name[port]);

   attach(port, DEV_ANALOG);
   attach(other, DEV_PAD);
   feed(port, 0, 0, 0);

   /* Scan bank 0 of our port, but wedge a full scan of the OTHER port
    * between our row 3 and our next row 0. */
   scan_bank(port, joy, but);
   for (r = 0; r < 4; r++)
      scan_row(other, r, &j, &b);

   /* Our next row 0 must still see the 3 -> 0 transition: bank 1. */
   scan_row(port, 0, &j, &b);
   report(bitv(b, c_bit[port]) == 1,
          "a full other-socket scan between row 3 and row 0 does not "
          "eat the bank switch");

   /* And the interleaved reads themselves must not have advanced the
    * cycle: rows 1-3 then 0 again -> back to bank 0. */
   scan_row(port, 1, &j, &b);
   scan_row(port, 2, &j, &b);
   scan_row(port, 3, &j, &b);
   scan_row(port, 0, &j, &b);
   report(bitv(b, c_bit[port]) == 0,
          "exactly one switch per own-socket 3 -> 0 transition");

   attach(port, DEV_PAD);
}

/* The driving skin is the same wire device. */
static void test_driving_same_wire(int port)
{
   uint16_t joy_a[4], but_a[4], joy_d[4], but_d[4];
   int r, same;

   printf("%s: driving controller is the same wire protocol\n",
          port_name[port]);

   attach(port, DEV_ANALOG);
   feed(port, 12345, -6789, SW_B | SW_UP);
   scan_bank(port, joy_a, but_a);

   attach(port, DEV_DRIVING);
   feed(port, 12345, -6789, SW_B | SW_UP);
   scan_bank(port, joy_d, but_d);

   same = 1;
   for (r = 0; r < 4; r++)
      if (joy_a[r] != joy_d[r] || but_a[r] != but_d[r])
         same = 0;
   report(same,
          "identical feeds read identically as 'analog' and 'driving'");

   attach(port, DEV_PAD);
}

/* Tuning integration through the shared axistune layer, absolute
 * semantics, asserted at the register level. */
static int read_x(int port)
{
   uint16_t joy[8], but[8];
   int base = scan_banks(port, joy, but);
   return decode8(port, &joy[base], 0);
}

static void test_tuning(int port)
{
   int x;

   printf("%s: tuning integration (absolute semantics)\n",
          port_name[port]);

   attach(port, DEV_ANALOG);

   /* Dead zone 16 ADC counts.  A deflection inside it reads centred. */
   p_SetTune(port, 0, 16, 0, 256);
   feed(port, 10 * 258, 0, 0);            /* ~10 counts */
   x = read_x(port);
   sprintf(detail, "10 counts inside a 16-count dead zone reads centred "
           "(got %d, expect 128)", x);
   report(x == 128, detail);

   /* RE-BASE, not gate: just past the edge is a small value, not a step
    * to 17 counts. */
   feed(port, 18 * 258, 0, 0);            /* ~18 counts */
   x = read_x(port);
   sprintf(detail, "18 counts re-bases to a small deflection "
           "(got %d, expect 129..131, NOT ~146)", x);
   report(x >= 129 && x <= 131, detail);

   /* Full deflection is preserved by the re-base. */
   feed(port, 32767, 0, 0);
   x = read_x(port);
   sprintf(detail, "full deflection still reads 255 through the dead zone "
           "(got %d)", x);
   report(x == 255, detail);

   /* Offset moves the rest position -- the absolute path's raw==0 rule:
    * 0 is a sample, and offset is the one field allowed to move it. */
   p_SetTune(port, 0, 0, 8, 256);
   feed(port, 0, 0, 0);
   x = read_x(port);
   sprintf(detail, "a +8-count offset moves a centred stick to 120 "
           "(got %d)", x);
   report(x == 120, detail);

   /* Exponent 2.0 anchors at full deflection: half stick curves toward
    * centre, full stick is untouched. */
   p_SetTune(port, 0, 0, 0, 512);
   feed(port, 16384, 0, 0);               /* ~half deflection */
   x = read_x(port);
   /* 16384/258 = 63 counts; 127 * (63/127)^2 = 31.25 -> ~31, so the byte
    * reads ~159 instead of the linear ~191. */
   sprintf(detail, "half deflection at e = 2.0 curves toward centre "
           "(got %d, expect ~159)", x);
   report(x >= 157 && x <= 161, detail);

   feed(port, 32767, 0, 0);
   x = read_x(port);
   sprintf(detail, "full deflection at e = 2.0 still reads 255 (got %d)",
           x);
   report(x == 255, detail);

   p_SetTune(port, 0, 0, 0, 256);
   attach(port, DEV_PAD);
}

/* Cross-port isolation: an engaged analog device on one port leaves the
 * other port's pad matrix untouched. */
static void test_port_isolation(void)
{
   uint16_t j, b;

   printf("cross-port isolation\n");

   attach(0, DEV_ANALOG);
   attach(1, DEV_PAD);
   feed(0, 32767, 0, 0);

   /* Press port-2 pad Up (slot 0 of joypad1Buttons). */
   p_joypad[1][0] = 0xFF;
   scan_row(1, 0, &j, &b);
   report(bitv(j, 12) == 0,
          "port-2 pad Up still reads through with an engaged analog "
          "device on port 1");
   p_joypad[1][0] = 0x00;

   attach(0, DEV_PAD);
}

/* Savestate: chunk size, and a mid-cycle rollback replays identically. */
static void test_savestate(void)
{
   uint8_t  chunk[256];
   uint8_t *snap;
   size_t   sz, n;
   uint16_t joy_c[4], but_c[4], joy_r[4], but_r[4];
   int      r, same;

   printf("savestate\n");

   n = p_StateSave(chunk);
   sprintf(detail, "InputDevStateSave wrote %u bytes (expect %d: the v12 "
           "chunk grew by 5 per port for #437)",
           (unsigned)n, INPUTDEV_STATE_SIZE_EXPECTED);
   report(n == (size_t)INPUTDEV_STATE_SIZE_EXPECTED, detail);

   if (!p_serialize || !p_unserialize || !p_serialize_size)
   {
      report(0, "retro_serialize/retro_unserialize resolvable");
      return;
   }

   attach(0, DEV_ANALOG);
   feed(0, 23456, -12345, SW_D);

   /* Scan bank 0 fully, so the device sits at row 3 of bank 0: the very
    * next row-0 write must switch to bank 1. */
   scan_bank(0, joy_c, but_c);

   sz   = p_serialize_size();
   snap = (uint8_t *)malloc(sz);
   if (!snap)
   {
      report(0, "snapshot allocation");
      return;
   }
   report(p_serialize(snap, sz) ? 1 : 0, "retro_serialize mid-cycle");

   /* Continue: the next scan is bank 1. */
   scan_bank(0, joy_c, but_c);

   /* Roll back and replay the same scan. */
   report(p_unserialize(snap, sz) ? 1 : 0, "retro_unserialize accepted");
   scan_bank(0, joy_r, but_r);

   same = 1;
   for (r = 0; r < 4; r++)
      if (joy_r[r] != joy_c[r] || but_r[r] != but_c[r])
         same = 0;
   report(same && bitv(but_r[0], c_bit[0]) == 1,
          "the rolled-back scan replays bank 1 word-identically -- "
          "bank counter and latches survived");

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
   p_FeedAnalog = (void (*)(int, int32_t, int32_t, uint32_t))
                     harness_dlsym(&cfg, "InputDevFeedAnalog");
   p_GetType    = (InputDevType (*)(int))harness_dlsym(&cfg, "InputDevGetType");
   p_StateSave  = (size_t (*)(uint8_t *))harness_dlsym(&cfg, "InputDevStateSave");

   p_joypad[0] = (uint8_t *)harness_dlsym(&cfg, "joypad0Buttons");
   p_joypad[1] = (uint8_t *)harness_dlsym(&cfg, "joypad1Buttons");

   p_serialize_size = (size_t (*)(void))harness_dlsym(&cfg, "retro_serialize_size");
   p_serialize      = (bool (*)(void *, size_t))harness_dlsym(&cfg, "retro_serialize");
   p_unserialize    = (bool (*)(const void *, size_t))
                         harness_dlsym(&cfg, "retro_unserialize");

   if (!p_ReadWord || !p_WriteWord || !p_SetType || !p_SetTune || !p_Reset
       || !p_FeedAnalog || !p_GetType || !p_StateSave
       || !p_joypad[0] || !p_joypad[1])
   {
      fprintf(stderr,
              "analog_decode_test: missing test-ABI symbols.  The wide "
              "test ABI must export Joystick*, InputDev* and "
              "joypadNButtons (exports-test.list and link-test.T).  "
              "Build with TEST_EXPORTS=1.\n");
      harness_shutdown(&cfg);
      return 1;
   }

   /* Both ports and both skins are valid (TR10 restricts neither). */
   for (port = 0; port < 2; port++)
   {
      sprintf(detail, "InputDevSetType accepts analog on %s",
              port_name[port]);
      p_SetType(port, DEV_ANALOG);
      report(p_GetType(port) == DEV_ANALOG, detail);
      sprintf(detail, "InputDevSetType accepts driving on %s",
              port_name[port]);
      p_SetType(port, DEV_DRIVING);
      report(p_GetType(port) == DEV_DRIVING, detail);
      p_SetType(port, DEV_PAD);
   }

   for (port = 0; port < 2; port++)
   {
      test_inert_until_fed(port);
      test_bank0_layout(port);
      test_value_mapping(port);
      test_bank_cycle_and_id(port);
      test_interleave_immunity(port);
      test_driving_same_wire(port);
      test_tuning(port);
   }

   test_port_isolation();
   test_savestate();

   sprintf(summary, "%d failure(s)", failures);
   result.status = failures ? "FAIL" : "PASS";
   result.name   = "analog_decode";
   result.detail = summary;
   harness_report(&cfg, &result, 1);

   harness_shutdown(&cfg);
   return failures ? 1 : 0;
}
