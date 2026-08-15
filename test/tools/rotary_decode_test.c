/*
 * test/tools/rotary_decode_test.c -- Tempest rotary end-to-end decode test.
 *
 * WHAT THIS PROVES
 * ================
 * Synthetic host rotation goes in through InputDevFeed(); a quadrature
 * decoder written here -- independently of src/jerry/quadrature.c, from
 * the phase sequences in the Jaguar Technical Reference V10 section
 * "Rotary 'Tempest' Controller" -- reads it back out of $F14000 exactly as
 * a 68K driver would, and the decoded direction and distance are asserted.
 * Both directions, both ports.
 *
 * THE CLAIM THAT DISTINGUISHES THE ROTARY FROM THE MOUSE
 * =====================================================
 * The mouse adapter is ROW-BLIND: it does not connect Jaguar pins 1-4, so
 * it asserts the same lines in all four rows and mouse_decode_test asserts
 * exactly that.  The rotary is the opposite shape -- TR10 calls it "a
 * modified standard controller ... read just like a standard controller
 * using Socket 0 row codes", and the encoder's common wire goes to the ROW
 * STROBE (port pin 4 = J0), which is driven low by row 0 and no other row.
 *
 * So the rotary must be visible in row 0 and INVISIBLE in rows 1-3, where
 * the port must keep behaving as an ordinary pad.  test_row_scope() below
 * asserts both halves.  If someone ever "simplifies" the rotary onto the
 * mouse's row-independent overlay, that test fails.
 *
 * THE ROW-GATED EMISSION RULE (the regression for the real defect)
 * ===============================================================
 * Measured on Tempest 2000 (vjtrace --watch 0xF14000:4:rw, 400 frames):
 * the poll loop at $80FD2E..$80FDEE writes EIGHT row codes per frame --
 * port 1 rows 0-3 then port 2 rows 0-3 -- each followed immediately by a
 * move.l.  That is eight armed offset-0 reads per frame but exactly ONE
 * row-0 sample per port.
 *
 * A rotary clocked "one state per armed read" would therefore drain up to
 * eight states between the samples a driver actually decodes, making the
 * decoded result (states drained) mod 4: two decodes as "undetermined",
 * THREE DECODES AS -1 (a sign inversion), four and eight decode as zero.
 * test_t2k_scan_shape() replays that exact eight-write scan and asserts
 * one clean count per scan.
 *
 * Poll shape: Tempest 2000 is shape A/C (row select rewritten before every
 * read), NOT the read-only shape B, so the design spec's stall-breaker
 * branch is not taken.  test_poll_shapes() pins that: shape B is frozen by
 * design and is the detector that would justify adding one.
 *
 * USAGE
 *   ./test/tools/rotary_decode_test ./virtualjaguar_libretro.dylib [rom]
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>

#include "../harness/harness.h"

/* Mirrors src/jerry/inputdev.h; the tool must not include emulator
 * headers it would then be testing against itself.  An enum rather than
 * #defines because the TYPE is load-bearing as well as the values:
 * InputDevSetType's second parameter and InputDevGetType's return value
 * are this enum, and UBSan's -fsanitize=function compares a dlsym'd
 * pointer's declared signature against the callee's, so `int` there aborts
 * the whole suite.  Values are still restated by hand, so a renumbering in
 * the header cannot silently propagate into this test. */
typedef enum {
   DEV_PAD                 = 0,
   DEV_MOUSE_ST            = 1,
   DEV_MOUSE_AMIGA_ADAPTER = 2,
   DEV_MOUSE_AMIGA_ON_ST   = 3,
   DEV_ROTARY              = 4
} InputDevType;

/* Mirrors the joypadNButtons[] slot enum in src/jerry/joystick.h. */
#define SLOT_U       0
#define SLOT_D       1
#define SLOT_L       2
#define SLOT_R       3
#define SLOT_1       7   /* row 1, column 3 -- a keypad digit */
#define SLOT_PAUSE  20

/* Socket-0 row codes.  Bit 15 enables the row-select outputs, bit 8 is
 * the audio-enable bit both real drivers set; the low byte is
 * (port2 nibble << 4) | port1 nibble, and 0xF means "that port is not a
 * socket-0 code", i.e. not scanned.  These are the literal values
 * Tempest 2000's own poll loop writes. */
static const uint16_t row_word_p1[4] = { 0x81FE, 0x81FD, 0x81FB, 0x81F7 };
static const uint16_t row_word_p2[4] = { 0x817F, 0x81BF, 0x81DF, 0x81EF };

/* $F14000 phase bits per port (TR10's row-0 matrix).  Phase 0 is the "A"
 * line and Phase 1 the "B" line. */
typedef struct { int a, b; } phase_bits;
static const phase_bits port_bits[2] = {
   /* port 1: J10 / J11 */ { 10, 11 },
   /* port 2: J14 / J15 */ { 14, 15 }
};

/* $F14002 controller-type bit per port, and the row in which C3 appears.
 * TR10 "Identifying Controller Types": C2 C3 = 1 0 is "Tempest Rotary".
 * The C-column rows differ by port -- port 1 reads C3/C2/C1 in rows
 * 3/2/1, port 2 reads C2/C3/C1 in rows 3/2/1. */
static const int   ctype_bit[2] = { 0, 2 };
static const int   c3_row[2]    = { 3, 2 };
static const int   c2_row[2]    = { 2, 3 };

static const char *port_name[2] = { "port 1", "port 2" };

/* ---- core entry points ---------------------------------------------- */

static uint16_t (*p_ReadWord)(uint32_t);
static void     (*p_WriteWord)(uint32_t, uint16_t);
static void     (*p_SetType)(int, InputDevType);
static void     (*p_SetScale)(int, int32_t);
static void     (*p_SetRotaryID)(int, int);
static void     (*p_Reset)(void);
static void     (*p_Feed)(int, int32_t, int32_t, uint32_t);
static InputDevType (*p_GetType)(int);
static int      (*p_AnyAttached)(void);

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

/* ---- decoder (independent of src/jerry/quadrature.c) ----------------- */

/* (A,B) logic levels -> Gray index.  0=(0,0) 1=(1,0) 2=(1,1) 3=(0,1).
 * TR10's anticlockwise sequence walks this table forwards. */
static int gray_index(int a, int b)
{
   if (!a && !b) return 0;
   if ( a && !b) return 1;
   if ( a &&  b) return 2;
   return 3;
}

static int level_of(uint16_t data, int bit)
{
   return (int)((data >> bit) & 1u);
}

typedef struct
{
   int32_t  net;        /* +ve = anticlockwise (increasing index, A leads B) */
   unsigned dropped;    /* two-state jumps: Domin's "undetermined" entry     */
   unsigned polls;
} decode_result;

static int32_t decode_step(int *prev, int idx, unsigned *dropped)
{
   int d;

   if (*prev < 0)
   {
      *prev = idx;
      return 0;
   }

   d     = (idx - *prev) & 3;
   *prev = idx;

   if (d == 0) return 0;
   if (d == 1) return 1;
   if (d == 3) return -1;

   (*dropped)++;
   return 0;
}

static uint16_t row_word(int port, int row)
{
   return port ? row_word_p2[row] : row_word_p1[row];
}

static int sample_phase(int port, uint16_t data)
{
   return gray_index(level_of(data, port_bits[port].a),
                     level_of(data, port_bits[port].b));
}

static void attach(int port, InputDevType type)
{
   p_SetType(port, type);
   p_SetScale(port, 256);
   p_SetRotaryID(port, 0);
   p_Reset();
   memset(p_joypad[port], 0x00, 21);
}

/*
 * Poll `polls` times against row 0 of `port`, feeding `d_per_poll` units
 * of rotation before each.  shape: 'A' one read per poll after a write,
 * 'B' write once then read-only, 'C' write then TWO reads per poll.
 */
static void run_polls(decode_result *r, int port, char shape,
                      unsigned polls, int32_t d_per_poll)
{
   int      prev = -1;
   unsigned i;
   uint16_t w    = row_word(port, 0);

   memset(r, 0, sizeof(*r));

   /* Prime the decoder with a reference sample, as any real driver must:
    * a quadrature decoder cannot report movement until it has something
    * to compare against.  The backlog is empty here, so this read cannot
    * advance the encoder.  For shape B this IS the single row-select
    * write that defines the shape. */
   p_WriteWord(0, w);
   prev = sample_phase(port, p_ReadWord(0));

   for (i = 0; i < polls; i++)
   {
      uint16_t d0;

      if (d_per_poll)
         p_Feed(port, d_per_poll, 0, 0);

      if (shape != 'B')
         p_WriteWord(0, w);

      d0 = p_ReadWord(0);
      if (shape == 'C')
         d0 = p_ReadWord(0);     /* second read is the decoded one */

      r->net += decode_step(&prev, sample_phase(port, d0), &r->dropped);
      r->polls++;
   }
}

/* ---- individual checks ----------------------------------------------- */

static void test_directions(int port)
{
   decode_result r;

   printf("%s rotary: direction and distance\n", port_name[port]);

   /* TR10: anticlockwise = A leads B = increasing Gray index.  The host
    * mapping is +X = clockwise (a spinner pushed right turns clockwise),
    * so a positive feed must decode NEGATIVE here. */
   attach(port, DEV_ROTARY);
   run_polls(&r, port, 'A', 40, 1);
   sprintf(detail,
           "+40 host units over 40 polls decode as clockwise "
           "(net=%d, expect -40), dropped=%u", (int)r.net, r.dropped);
   report(r.net == -40 && r.dropped == 0, detail);

   attach(port, DEV_ROTARY);
   run_polls(&r, port, 'A', 40, -1);
   sprintf(detail,
           "-40 host units over 40 polls decode as anticlockwise "
           "(net=%d, expect +40), dropped=%u", (int)r.net, r.dropped);
   report(r.net == 40 && r.dropped == 0, detail);

   /* Rate ceiling: four units per poll is four times what one poll can
    * carry.  The result must be exactly one state per poll -- lag -- and
    * never a jump, an overshoot or a sign flip. */
   attach(port, DEV_ROTARY);
   run_polls(&r, port, 'A', 30, 4);
   sprintf(detail,
           "4 units/poll x 30 polls lags rather than jitters "
           "(net=%d, expect -30), dropped=%u", (int)r.net, r.dropped);
   report(r.net == -30 && r.dropped == 0, detail);
}

static void test_poll_shapes(int port)
{
   decode_result ra, rb, rc;

   printf("%s rotary: poll shapes (the emission rule)\n", port_name[port]);

   attach(port, DEV_ROTARY);
   run_polls(&ra, port, 'A', 40, 1);

   attach(port, DEV_ROTARY);
   run_polls(&rc, port, 'C', 40, 1);

   sprintf(detail,
           "shape C (write + TWO reads) decodes as shape A: A=%d C=%d, "
           "dropped A=%u C=%u",
           (int)ra.net, (int)rc.net, ra.dropped, rc.dropped);
   report(rc.net == ra.net && ra.dropped == 0 && rc.dropped == 0, detail);

   /* Shape B is the design spec's deferred case and the detector for
    * whether a stall-breaker is ever needed.  Tempest 2000 is NOT this
    * shape -- measured, see the file header -- so no stall-breaker is
    * implemented and "frozen" is a recorded, tested property. */
   attach(port, DEV_ROTARY);
   run_polls(&rb, port, 'B', 40, 1);
   sprintf(detail,
           "shape B (write once, then read-only) is frozen by design: "
           "net=%d (expect 0)", (int)rb.net);
   report(rb.net == 0, detail);
}

/*
 * THE DISCRIMINATOR: the rotary is a matrix device, not a row-blind one.
 */
static void test_row_scope(int port)
{
   decode_result r;
   uint16_t      v[4];
   int           row, phase_rows, pad_rows;

   printf("%s rotary: visible in row 0 ONLY (not row-blind)\n",
          port_name[port]);

   /* Drive to a phase in which at least one phase line is asserted, so
    * "the rotary is invisible in rows 1-3" is not trivially true of an
    * idle encoder.  Then read every row without feeding, so the phase is
    * constant across the sweep. */
   attach(port, DEV_ROTARY);
   run_polls(&r, port, 'A', 21, 1);

   for (row = 0; row < 4; row++)
   {
      p_WriteWord(0, row_word(port, row));
      v[row] = p_ReadWord(0);
   }

   phase_rows = 0;
   pad_rows   = 0;
   for (row = 0; row < 4; row++)
   {
      int asserted = (!level_of(v[row], port_bits[port].a)
                      || !level_of(v[row], port_bits[port].b));
      if (row == 0)
         phase_rows = asserted;
      else if (asserted)
         pad_rows++;
   }

   sprintf(detail,
           "phase lines assert in row 0 ($%04X) and in NO other row "
           "(rows 1-3: $%04X $%04X $%04X)",
           v[0], v[1], v[2], v[3]);
   report(phase_rows && pad_rows == 0, detail);

   /* And rows 1-3 still behave as an ordinary pad: a keypad digit poked
    * into the matrix must read back.  This is the half that fails if the
    * rotary is ever moved onto the mouse's row-blind overlay, which would
    * bury the keypad under the phase lines. */
   p_joypad[port][SLOT_1] = 0xFF;
   p_WriteWord(0, row_word(port, 1));
   v[1] = p_ReadWord(0);
   p_joypad[port][SLOT_1] = 0x00;

   sprintf(detail,
           "rows 1-3 still decode as a standard pad (keypad '1' pulls "
           "bit %d low in row 1: $%04X)", port_bits[port].b, v[1]);
   report(!level_of(v[1], port_bits[port].b), detail);

   /* Up and Down do not exist on a rotary.  TR10 notes this is why rotary
    * games are told to use A = Up and C = Down for menus. */
   p_joypad[port][SLOT_U] = 0xFF;
   p_joypad[port][SLOT_D] = 0xFF;
   attach(port, DEV_ROTARY);
   run_polls(&r, port, 'A', 5, 1);
   report(p_joypad[port][SLOT_U] == 0x00 && p_joypad[port][SLOT_D] == 0x00,
          "Up and Down never assert on a rotary port");
}

/*
 * Replay Tempest 2000's measured scan: eight row-select writes per frame
 * (port 1 rows 0-3, then port 2 rows 0-3), each followed by a move.l --
 * one offset-0 read and one offset-2 read.  Exactly ONE row-0 sample per
 * port per scan.
 *
 * This is the regression for the row-gated advance.  Clocked "one state
 * per armed read" the encoder drains up to 8 states between the samples
 * the driver decodes, so the decoded value is (drained mod 4): 2 is
 * dropped, 3 inverts the sign, 4 and 8 read as no movement at all.
 */
static void test_t2k_scan_shape(int port)
{
   int      prev = -1;
   unsigned scan, dropped = 0;
   int32_t  net = 0;
   int      row, half;

   printf("%s rotary: Tempest 2000's measured 8-write scan\n",
          port_name[port]);

   attach(port, DEV_ROTARY);

   /* Prime from a row-0 sample, as the driver's first pass does. */
   p_WriteWord(0, row_word(port, 0));
   prev = sample_phase(port, p_ReadWord(0));

   for (scan = 0; scan < 40; scan++)
   {
      p_Feed(port, 1, 0, 0);        /* one unit of rotation per frame */

      /* half 0 = port-1 rows 0-3, half 1 = port-2 rows 0-3, in the order
       * the measured loop issues them.  Each write is followed by a
       * move.l, i.e. one offset-0 read and one offset-2 read. */
      for (half = 0; half < 2; half++)
      {
         for (row = 0; row < 4; row++)
         {
            uint16_t d0;

            p_WriteWord(0, half ? row_word_p2[row] : row_word_p1[row]);
            d0 = p_ReadWord(0);
            p_ReadWord(2);

            /* The driver decodes the rotary from this sample, and only
             * from this one: its own port's row 0. */
            if (half == port && row == 0)
               net += decode_step(&prev, sample_phase(port, d0), &dropped);
         }
      }
   }

   sprintf(detail,
           "one clean count per scan across 40 scans (net=%d, expect -40), "
           "dropped=%u -- a per-read clock would decode garbage here",
           (int)net, dropped);
   report(net == -40 && dropped == 0, detail);
}

/*
 * Tempest 2000's rotary support is hidden behind an unlock that needs
 * PAUSE ON BOTH CONTROLLERS AT ONCE.  It is the first thing a user does
 * and the easiest thing this feature can break, so it is asserted here:
 * a rotary port must still be able to report Pause while the other port
 * is a pad also reporting Pause.
 */
static void test_pause_unlock(void)
{
   uint16_t d2;
   int      p1_pause, p2_pause;

   printf("Tempest 2000 unlock: Pause on BOTH controllers at once\n");

   attach(0, DEV_ROTARY);
   attach(1, DEV_PAD);

   p_joypad[0][SLOT_PAUSE] = 0xFF;
   p_joypad[1][SLOT_PAUSE] = 0xFF;

   /* Row 0 of both ports at once: port-1 nibble 0xE, port-2 nibble 0x7. */
   p_WriteWord(0, 0x817E);
   p_ReadWord(0);
   d2 = p_ReadWord(2);

   p1_pause = !level_of(d2, 0);   /* port 1 Pause -> $F14002 bit 0 */
   p2_pause = !level_of(d2, 2);   /* port 2 Pause -> $F14002 bit 2 */

   sprintf(detail,
           "rotary on port 1 + pad on port 2 both report Pause "
           "($F14002=$%04X, p1=%d p2=%d)", d2, p1_pause, p2_pause);
   report(p1_pause && p2_pause, detail);

   p_joypad[0][SLOT_PAUSE] = 0x00;
   p_joypad[1][SLOT_PAUSE] = 0x00;
   attach(0, DEV_PAD);
}

static void test_controller_id(int port)
{
   uint16_t d2_c3, d2_c2;
   int      bit = ctype_bit[port];

   printf("%s rotary: controller-type identification\n", port_name[port]);

   /* Default (no diode): C2 C3 = 1 1, i.e. "standard joypad", which is
    * what most rotary controllers ever built actually report. */
   attach(port, DEV_ROTARY);
   p_SetRotaryID(port, 0);

   p_WriteWord(0, row_word(port, c3_row[port]));
   p_ReadWord(0);
   d2_c3 = p_ReadWord(2);

   sprintf(detail,
           "with the diode absent (default) C3 reads 1 -- standard joypad "
           "($F14002=$%04X bit %d)", d2_c3, bit);
   report(level_of(d2_c3, bit) == 1, detail);

   /* With the diode fitted: C3 = 0, C2 = 1 -> "Tempest Rotary". */
   p_SetRotaryID(port, 1);

   p_WriteWord(0, row_word(port, c3_row[port]));
   p_ReadWord(0);
   d2_c3 = p_ReadWord(2);

   p_WriteWord(0, row_word(port, c2_row[port]));
   p_ReadWord(0);
   d2_c2 = p_ReadWord(2);

   sprintf(detail,
           "with the diode fitted C2 C3 = 1 0 in the right rows for this "
           "port (row %d bit %d = 0 -> $%04X; row %d bit %d = 1 -> $%04X)",
           c3_row[port], bit, d2_c3, c2_row[port], bit, d2_c2);
   report(level_of(d2_c3, bit) == 0 && level_of(d2_c2, bit) == 1, detail);

   p_SetRotaryID(port, 0);
}

static void test_port_isolation(void)
{
   decode_result r;
   uint16_t      before, after;

   printf("a rotary on one port does not perturb the other\n");

   attach(0, DEV_PAD);
   attach(1, DEV_ROTARY);

   /* Port 1 is a plain pad with nothing pressed; its bits must read the
    * same before and after the port-2 rotary is driven. */
   p_WriteWord(0, row_word(0, 0));
   before = (uint16_t)(p_ReadWord(0) & 0x0F00u);

   run_polls(&r, 1, 'A', 25, 1);

   p_WriteWord(0, row_word(0, 0));
   after = (uint16_t)(p_ReadWord(0) & 0x0F00u);

   sprintf(detail,
           "port-1 direction bits unchanged by a port-2 rotary "
           "($%04X vs $%04X), while port 2 decoded net=%d",
           before, after, (int)r.net);
   report(before == after && before == 0x0F00u && r.net == -25, detail);

   attach(1, DEV_PAD);
}

static void test_pad_is_inert(void)
{
   decode_result r;
   unsigned      i;

   printf("no device selected -> nothing reaches the matrix\n");

   p_SetType(0, DEV_PAD);
   p_SetType(1, DEV_PAD);
   p_Reset();
   memset(p_joypad[0], 0x00, 21);
   memset(p_joypad[1], 0x00, 21);

   report(p_GetType(0) == DEV_PAD && p_GetType(1) == DEV_PAD
          && p_AnyAttached() == 0,
          "both ports report pad and nothing is attached");

   for (i = 0; i < 20; i++)
   {
      p_Feed(0, 100, 100, 3);
      p_Feed(1, 100, 100, 3);
   }

   run_polls(&r, 0, 'A', 20, 0);
   sprintf(detail, "no rotation reaches $F14000 (net=%d)", (int)r.net);
   report(r.net == 0, detail);
}

static void test_mouse_still_row_blind(void)
{
   decode_result r;
   uint16_t      v[4];
   int           row, rows_asserting = 0;

   printf("the mouse is still row-blind (the two shapes stay distinct)\n");

   attach(1, DEV_MOUSE_ST);
   run_polls(&r, 1, 'A', 21, 1);

   for (row = 0; row < 4; row++)
   {
      p_WriteWord(0, row_word(1, row));
      v[row] = p_ReadWord(0);
      if ((uint16_t)(v[row] & 0xF000u) != 0xF000u)
         rows_asserting++;
   }

   sprintf(detail,
           "a port-2 mouse asserts in ALL FOUR rows ($%04X $%04X $%04X "
           "$%04X) where the rotary asserts in one",
           v[0], v[1], v[2], v[3]);
   report(rows_asserting == 4, detail);

   attach(1, DEV_PAD);
}

static void test_savestate(void)
{
   uint8_t      *snap;
   size_t        sz;
   decode_result cont, restored;
   int           ok;

   printf("savestate round-trip mid-rotation\n");

   if (!p_serialize || !p_unserialize || !p_serialize_size)
   {
      report(0, "retro_serialize/retro_unserialize resolvable");
      return;
   }

   sz   = p_serialize_size();
   snap = (uint8_t *)malloc(sz);
   if (!snap)
   {
      report(0, "allocate state buffer");
      return;
   }

   /* Stop in the middle of a Gray cycle with a non-empty backlog and a
    * non-zero Q8 carry, then save. */
   attach(0, DEV_ROTARY);
   p_SetScale(0, 96);              /* 37.5%: guarantees a carry remainder */
   run_polls(&cont, 0, 'A', 7, 3);

   ok = p_serialize(snap, sz) ? 1 : 0;
   report(ok, "retro_serialize succeeded mid-rotation");
   if (!ok)
   {
      free(snap);
      return;
   }

   run_polls(&cont, 0, 'A', 25, 3);

   ok = p_unserialize(snap, sz) ? 1 : 0;
   report(ok, "retro_unserialize accepted the state");

   run_polls(&restored, 0, 'A', 25, 3);

   sprintf(detail,
           "post-restore replay matches (net %d vs %d, dropped %u vs %u) "
           "-- phase, backlog and Q8 carry all survived the rollback",
           (int)cont.net, (int)restored.net, cont.dropped, restored.dropped);
   report(cont.net == restored.net && cont.dropped == restored.dropped
          && restored.dropped == 0, detail);

   free(snap);
   p_SetScale(0, 256);
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

   p_ReadWord    = (uint16_t (*)(uint32_t))harness_dlsym(&cfg, "JoystickReadWord");
   p_WriteWord   = (void (*)(uint32_t, uint16_t))harness_dlsym(&cfg, "JoystickWriteWord");
   p_SetType     = (void (*)(int, InputDevType))
                      harness_dlsym(&cfg, "InputDevSetType");
   p_SetScale    = (void (*)(int, int32_t))harness_dlsym(&cfg, "InputDevSetScale");
   p_SetRotaryID = (void (*)(int, int))harness_dlsym(&cfg, "InputDevSetRotaryID");
   p_Reset       = (void (*)(void))harness_dlsym(&cfg, "InputDevReset");
   p_Feed        = (void (*)(int, int32_t, int32_t, uint32_t))
                      harness_dlsym(&cfg, "InputDevFeed");
   p_GetType     = (InputDevType (*)(int))harness_dlsym(&cfg, "InputDevGetType");
   p_AnyAttached = (int (*)(void))harness_dlsym(&cfg, "InputDevAnyAttached");

   p_joypad[0] = (uint8_t *)harness_dlsym(&cfg, "joypad0Buttons");
   p_joypad[1] = (uint8_t *)harness_dlsym(&cfg, "joypad1Buttons");

   p_serialize_size = (size_t (*)(void))harness_dlsym(&cfg, "retro_serialize_size");
   p_serialize      = (bool (*)(void *, size_t))harness_dlsym(&cfg, "retro_serialize");
   p_unserialize    = (bool (*)(const void *, size_t))
                         harness_dlsym(&cfg, "retro_unserialize");

   if (!p_ReadWord || !p_WriteWord || !p_SetType || !p_SetScale
       || !p_SetRotaryID || !p_Reset || !p_Feed || !p_GetType
       || !p_AnyAttached || !p_joypad[0] || !p_joypad[1])
   {
      fprintf(stderr,
              "rotary_decode_test: missing test-ABI symbols.  The wide test "
              "ABI must export Joystick*, InputDev* and joypadNButtons "
              "(exports-test.list and link-test.T).  Build with "
              "TEST_EXPORTS=1.\n");
      harness_shutdown(&cfg);
      return 1;
   }

   /* The rotary is valid on BOTH ports, unlike the mouse. */
   for (port = 0; port < 2; port++)
   {
      sprintf(detail, "InputDevSetType accepts a rotary on %s",
              port_name[port]);
      p_SetType(port, DEV_ROTARY);
      report(p_GetType(port) == DEV_ROTARY, detail);
      p_SetType(port, DEV_PAD);
   }

   for (port = 0; port < 2; port++)
   {
      test_directions(port);
      test_poll_shapes(port);
      test_row_scope(port);
      test_t2k_scan_shape(port);
      test_controller_id(port);
      attach(port, DEV_PAD);
   }

   test_pause_unlock();
   test_port_isolation();
   test_mouse_still_row_blind();
   test_savestate();
   test_pad_is_inert();

   sprintf(summary, "%d failure(s)", failures);
   result.status = failures ? "FAIL" : "PASS";
   result.name   = "rotary_decode";
   result.detail = summary;
   harness_report(&cfg, &result, 1);

   harness_shutdown(&cfg);
   return failures ? 1 : 0;
}
