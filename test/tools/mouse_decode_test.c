/*
 * test/tools/mouse_decode_test.c -- ST/Amiga mouse end-to-end decode test.
 *
 * WHAT THIS PROVES
 * ================
 * Synthetic host deltas go in through InputDevFeed(); a quadrature decoder
 * written here -- independently of src/jerry/quadrature.c, from the truth
 * table in docs/jaguar-mouse-adapter-mapping.md section 5 -- reads them
 * back out of $F14000 / $F14002 exactly as a 68K driver would, and the
 * decoded direction and distance are asserted.  Both directions on both
 * axes, all three wiring cases, three poll shapes.
 *
 * The decoder here is deliberately NOT a call into the encoder: it walks
 * the register bits, converts (A,B) levels to a Gray index and takes the
 * modulo-4 difference between consecutive samples, discarding two-state
 * jumps the way Domin's 16-entry table discards its "undetermined"
 * entries.  If the emulated device ever emits a two-state jump, DROPPED
 * goes non-zero and the run fails -- which is the property the whole
 * emission rule exists to guarantee.
 *
 * POLL SHAPES (the emission rule's regression)
 * ============================================
 *   A  write $817F, then ONE read per poll        (Domin's own loop)
 *   B  write $817F once at init, then read-only   (the deferred case)
 *   C  write $817F, then TWO reads per poll,
 *      decoding on the second                     (the common driver shape)
 *
 * Shape C is why the rule is "arm on write, advance on the next read"
 * rather than "advance once per read".  Under the naive rule shape C
 * advances the phase twice between decode steps, i.e. emits exactly the
 * two-state jump a decoder throws away -- roughly half the motion lost,
 * as jitter.  Under the real rule shape C decodes identically to shape A
 * with DROPPED == 0.  Both are asserted below.
 *
 * Shape B is expected to decode ZERO motion: a driver that never rewrites
 * the row select never arms the clock.  No such driver is documented; the
 * assertion is here as the detector that would justify adding the
 * stall-breaker, so that "frozen" is a recorded, tested property rather
 * than a surprise.
 *
 * ROW-BLINDNESS
 * =============
 * The adapter does not connect Jaguar pins 1-4, so it never sees the row
 * select and asserts identically in all four rows.  Every read below is
 * repeated across row codes $817F / $81BF / $81DF / $81EF and the mouse
 * bits are asserted equal in all four -- for the direction lines and for
 * both buttons.  That is the check that fails if anyone later "fixes" the
 * phantom-keypad or the LMB-reads-as-A-B-C-Option behaviour.
 *
 * USAGE
 *   ./test/tools/mouse_decode_test ./virtualjaguar_libretro.dylib [rom]
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#include "../harness/harness.h"

/* Mirrors src/jerry/inputdev.h; the tool must not include emulator
 * headers it would then be testing against itself.  An enum rather than
 * four #defines because the TYPE is load-bearing as well as the values:
 * InputDevSetType's second parameter is this enum, and UBSan's
 * -fsanitize=function compares a dlsym'd pointer's declared signature
 * against the callee's, so `int` there aborts the whole suite.  Values are
 * still restated by hand, so a renumbering in the header cannot silently
 * propagate into this test. */
typedef enum {
   DEV_PAD                 = 0,
   DEV_MOUSE_ST            = 1,
   DEV_MOUSE_AMIGA_ADAPTER = 2,
   DEV_MOUSE_AMIGA_ON_ST   = 3
} InputDevType;

#define BTN_LEFT   0x01
#define BTN_RIGHT  0x02

/* Mirrors INPUTDEV_STATE_SIZE in src/jerry/inputdev.h, restated here
 * on purpose: 2 ports x (2 axes x (int32 backlog + int32 carry +
 * uint8 phase) + 2 button latches) + 1 armed byte. */
#define INPUTDEV_STATE_SIZE_EXPECTED 41

/* Port-2 socket-0 row codes: rows 0, 1, 2, 3 (mapping doc section 2). */
static const uint16_t row_words[4] = { 0x817F, 0x81BF, 0x81DF, 0x81EF };

/* $F14000 bit pairs per wiring case (mapping doc section 4d). */
typedef struct { int xa, xb, ya, yb; } wiring;
static const wiring case_wiring[4] = {
   /* DEV_PAD, unused                 */ {  0,  0,  0,  0 },
   /* DEV_MOUSE_ST                    */ { 13, 12, 14, 15 },
   /* DEV_MOUSE_AMIGA_ADAPTER         */ { 13, 12, 15, 14 },
   /* DEV_MOUSE_AMIGA_ON_ST           */ { 13, 15, 12, 14 }
};

static const char *case_name[4] = {
   "pad", "mouse_st", "mouse_amiga_adapter", "mouse_amiga (case 3)"
};

/* ---- core entry points ---------------------------------------------- */

static uint16_t (*p_ReadWord)(uint32_t);
static void     (*p_WriteWord)(uint32_t, uint16_t);
static void     (*p_SetType)(int, InputDevType);
static void     (*p_SetScale)(int, int32_t);
static void     (*p_Reset)(void);
static void     (*p_Feed)(int, int32_t, int32_t, uint32_t);
static int      (*p_GetType)(int);
static int      (*p_AnyAttached)(void);
static size_t   (*p_StateSave)(uint8_t *);

static size_t (*p_serialize_size)(void);
static int    (*p_serialize)(void *, size_t);
static int    (*p_unserialize)(const void *, size_t);

/* ---- reporting ------------------------------------------------------- */

static int failures;
static char detail[256];

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

/* (A,B) logic levels -> Gray index.  0=(0,0) 1=(1,0) 2=(1,1) 3=(0,1). */
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
   int32_t  net_x, net_y;
   unsigned dropped;
   unsigned polls;
} decode_result;

/* One step of the decoder for one axis.  Returns the signed movement, and
 * bumps *dropped on an undetermined (two-state) transition. */
static int32_t decode_step(int *prev, int idx, unsigned *dropped)
{
   int d;

   if (*prev < 0)
   {
      *prev = idx;
      return 0;
   }

   d = (idx - *prev) & 3;
   *prev = idx;

   if (d == 0) return 0;
   if (d == 1) return 1;
   if (d == 3) return -1;

   (*dropped)++;   /* d == 2: Domin's "undetermined" entry, discarded */
   return 0;
}

/*
 * Run `polls` poll iterations, feeding `feed_fn`-supplied deltas.
 * shape: 'A' | 'B' | 'C'.  `decode_case` selects which wiring's bit pairs
 * the decoder uses -- normally the same as the attached device, but the
 * axis-independence check deliberately mismatches them.
 */
static void run_polls(decode_result *r, char shape, int decode_case,
                      unsigned polls, int32_t dx_per_poll,
                      int32_t dy_per_poll, uint32_t buttons)
{
   const wiring *w = &case_wiring[decode_case];
   int prev_x = -1, prev_y = -1;
   unsigned i, row;

   uint16_t prime;

   (void)row;
   memset(r, 0, sizeof(*r));

   /* Prime the decoder with a reference sample, exactly as a real driver
    * does on its first pass: a quadrature decoder cannot report movement
    * until it has something to compare against.  Safe to read here
    * because the backlog is empty on entry, so this cannot advance the
    * encoder.  For shape B this IS the one row-select write the shape is
    * defined by -- it never writes again. */
   p_WriteWord(0, row_words[0]);
   prime  = p_ReadWord(0);
   prev_x = gray_index(level_of(prime, w->xa), level_of(prime, w->xb));
   prev_y = gray_index(level_of(prime, w->ya), level_of(prime, w->yb));

   for (i = 0; i < polls; i++)
   {
      uint16_t d0;
      int ax, bx, ay, by;

      if (dx_per_poll || dy_per_poll || buttons)
         p_Feed(1, dx_per_poll, dy_per_poll, buttons);

      if (shape != 'B')
         p_WriteWord(0, row_words[0]);

      d0 = p_ReadWord(0);
      if (shape == 'C')
         d0 = p_ReadWord(0);          /* second read is the decoded one */

      ax = level_of(d0, w->xa);
      bx = level_of(d0, w->xb);
      ay = level_of(d0, w->ya);
      by = level_of(d0, w->yb);

      r->net_x += decode_step(&prev_x, gray_index(ax, bx), &r->dropped);
      r->net_y += decode_step(&prev_y, gray_index(ay, by), &r->dropped);
      r->polls++;
   }
}

/*
 * Row-blindness sweep.  Run only once the backlog has drained, so the
 * four row reads cannot themselves advance the encoder and the phase is
 * genuinely constant across the sweep -- otherwise this would be testing
 * the emission clock, not the adapter's wiring.
 *
 * Returns 1 if all four rows agree on $F14000 bits 12-15; fills
 * btn_l/btn_r with the per-row LMB/RMB assertion.
 */
static int sweep_rows(int *btn_l, int *btn_r)
{
   uint16_t ref = 0;
   unsigned row;
   int      agree = 1;

   for (row = 0; row < 4; row++)
   {
      uint16_t v, d2;

      p_WriteWord(0, row_words[row]);
      v  = p_ReadWord(0);
      d2 = p_ReadWord(2);

      if (row == 0)
         ref = (uint16_t)(v & 0xF000u);
      else if ((uint16_t)(v & 0xF000u) != ref)
         agree = 0;

      btn_l[row] = ((d2 >> 3) & 1u) ? 0 : 1;
      btn_r[row] = ((d2 >> 2) & 1u) ? 0 : 1;
   }

   return agree;
}

/* ---- individual checks ----------------------------------------------- */

static void attach(int type)
{
   p_SetType(1, type);
   p_SetScale(1, 256);
   p_Reset();
}

static void test_directions(int type)
{
   decode_result r;
   int c = type;

   printf("%s: direction and distance\n", case_name[type]);

   /* +X: 1 unit per poll for 40 polls drains exactly, so NET == 40. */
   attach(type);
   run_polls(&r, 'A', c, 40, 1, 0, 0);
   sprintf(detail, "+X 40 units over 40 polls -> NET_X=%d NET_Y=%d dropped=%u",
           (int)r.net_x, (int)r.net_y, r.dropped);
   report(r.net_x == 40 && r.net_y == 0 && r.dropped == 0, detail);

   attach(type);
   run_polls(&r, 'A', c, 40, -1, 0, 0);
   sprintf(detail, "-X 40 units over 40 polls -> NET_X=%d NET_Y=%d dropped=%u",
           (int)r.net_x, (int)r.net_y, r.dropped);
   report(r.net_x == -40 && r.net_y == 0 && r.dropped == 0, detail);

   attach(type);
   run_polls(&r, 'A', c, 40, 0, 1, 0);
   sprintf(detail, "+Y 40 units over 40 polls -> NET_X=%d NET_Y=%d dropped=%u",
           (int)r.net_x, (int)r.net_y, r.dropped);
   report(r.net_y == 40 && r.net_x == 0 && r.dropped == 0, detail);

   attach(type);
   run_polls(&r, 'A', c, 40, 0, -1, 0);
   sprintf(detail, "-Y 40 units over 40 polls -> NET_X=%d NET_Y=%d dropped=%u",
           (int)r.net_x, (int)r.net_y, r.dropped);
   report(r.net_y == -40 && r.net_x == 0 && r.dropped == 0, detail);

   /* Diagonal: both axes advance independently and concurrently -- they
    * are four parallel wires, not a time-multiplexed stream. */
   attach(type);
   run_polls(&r, 'A', c, 40, 1, -1, 0);
   sprintf(detail, "diagonal 40 polls -> NET_X=%d NET_Y=%d dropped=%u",
           (int)r.net_x, (int)r.net_y, r.dropped);
   report(r.net_x == 40 && r.net_y == -40 && r.dropped == 0, detail);
}

static void test_rate_ceiling(int type)
{
   decode_result r;

   printf("%s: rate ceiling (lag, never overshoot, never a sign flip)\n",
          case_name[type]);

   /* 4 units per poll is four times what one poll can carry.  The result
    * must be exactly one state per poll -- lag -- and never a jump. */
   attach(type);
   run_polls(&r, 'A', type, 30, 4, 0, 0);
   sprintf(detail, "4 units/poll x 30 polls -> NET_X=%d (expect 30), dropped=%u",
           (int)r.net_x, r.dropped);
   report(r.net_x == 30 && r.dropped == 0, detail);

   attach(type);
   run_polls(&r, 'A', type, 30, -4, 0, 0);
   sprintf(detail, "-4 units/poll x 30 polls -> NET_X=%d (expect -30), dropped=%u",
           (int)r.net_x, r.dropped);
   report(r.net_x == -30 && r.dropped == 0, detail);
}

static void test_poll_shapes(int type)
{
   decode_result ra, rb, rc;

   printf("%s: poll shapes (the emission rule)\n", case_name[type]);

   attach(type);
   run_polls(&ra, 'A', type, 40, 1, 0, 0);

   attach(type);
   run_polls(&rc, 'C', type, 40, 1, 0, 0);

   sprintf(detail,
           "shape C (write + TWO reads) decodes as shape A: A=%d C=%d, "
           "dropped A=%u C=%u",
           (int)ra.net_x, (int)rc.net_x, ra.dropped, rc.dropped);
   report(rc.net_x == ra.net_x && rc.dropped == 0 && ra.dropped == 0, detail);

   attach(type);
   run_polls(&rb, 'B', type, 40, 1, 0, 0);
   sprintf(detail,
           "shape B (write once, then read-only) is frozen by design: "
           "NET_X=%d (expect 0)", (int)rb.net_x);
   report(rb.net_x == 0, detail);
}

static void test_row_blindness(int type)
{
   decode_result r;
   int bl[4], br[4];
   int agree;
   int phase_seen;

   printf("%s: row-blindness\n", case_name[type]);

   /* Drive to a phase that is NOT all-ones on the mouse bits (so "the
    * rows agree" is not trivially true of an idle adapter), with the
    * backlog fully drained, then hold both buttons. */
   attach(type);
   run_polls(&r, 'A', type, 21, 1, 0, BTN_LEFT | BTN_RIGHT);

   agree = sweep_rows(bl, br);

   p_WriteWord(0, row_words[0]);
   phase_seen = (int)((p_ReadWord(0) >> 12) & 0x0Fu);

   sprintf(detail,
           "$F14000 bits 12-15 read identically in rows 0-3 (value $%X)",
           phase_seen);
   report(agree && phase_seen != 0x0F, detail);

   sprintf(detail, "LMB asserts $F14002 bit 3 in every row (%d%d%d%d)",
           bl[0], bl[1], bl[2], bl[3]);
   report(bl[0] && bl[1] && bl[2] && bl[3], detail);

   sprintf(detail, "RMB asserts $F14002 bit 2 in every row (%d%d%d%d)",
           br[0], br[1], br[2], br[3]);
   report(br[0] && br[1] && br[2] && br[3], detail);

   /* Released buttons must not assert. */
   attach(type);
   run_polls(&r, 'A', type, 10, 1, 0, 0);
   sweep_rows(bl, br);
   report(!bl[0] && !bl[1] && !bl[2] && !bl[3]
          && !br[0] && !br[1] && !br[2] && !br[3],
          "released buttons leave $F14002 bits 2 and 3 high in every row");
}

static void test_case_discrimination(void)
{
   decode_result r;

   printf("wiring cases are genuinely distinct\n");

   /* Case 3 (Amiga mouse in an ST-wired adapter) pairs X = {13,15} and
    * Y = {12,14}; case 1 pairs X = {13,12} and Y = {14,15}.  Each case-1
    * "pair" therefore straddles one line from each real axis, so pure X
    * motion decodes as an oscillation on both and nets out to nothing.
    * This is the discriminator the mapping doc's section 8 A names: a
    * user on the wrong option value sees a cursor that barely moves, not
    * a merely inverted one. */
   attach(DEV_MOUSE_AMIGA_ON_ST);
   run_polls(&r, 'A', DEV_MOUSE_ST, 40, 1, 0, 0);
   sprintf(detail,
           "case-3 wiring read with a case-1 decoder does not decode cleanly "
           "(NET_X=%d NET_Y=%d dropped=%u; the matching decoder gives 40/0/0)",
           (int)r.net_x, (int)r.net_y, r.dropped);
   report(!(r.net_x == 40 && r.net_y == 0 && r.dropped == 0), detail);

   /* Cases 1 and 2 share their lines and differ only in Y phase sense,
    * so a case-2 device read with a case-1 decoder must invert Y and
    * leave X alone.  UNVERIFIED per the mapping doc section 8 B: this
    * asserts the table as implemented, not a measured hardware fact. */
   attach(DEV_MOUSE_AMIGA_ADAPTER);
   run_polls(&r, 'A', DEV_MOUSE_ST, 40, 0, 1, 0);
   sprintf(detail,
           "case-2 wiring read with a case-1 decoder inverts Y only "
           "(NET_X=%d NET_Y=%d)", (int)r.net_x, (int)r.net_y);
   report(r.net_x == 0 && r.net_y == -40, detail);
}

/*
 * THE EMISSION CLOCK IS GATED ON PORT 2'S ROW SELECT (mapping doc §5).
 *
 * §5 requires at most one Gray state per $F14000 read taken with port 2's
 * row select asserted.  Ungated, a title polling PORT 1 rapidly advances
 * the port-2 encoder between the port-2 poller's own samples -- which does
 * not merely lag, it loses motion outright, because the decoder discards
 * the resulting two-state jumps.
 *
 * This test fails without the gate: with it, 200 port-1 polls leave the
 * phase untouched and the queued motion intact; without it, those polls
 * drain the 40-state backlog and the following port-2 run decodes nothing.
 */
static void test_port1_poll_does_not_clock(void)
{
   /* Port-1-only row select.  Low nibble 7 = port 1's socket-0 row 0;
    * high nibble F is NOT a socket-0 code, so port 2 is unselected. */
   const uint16_t p1_row_word = 0x81F7;
   decode_result r;
   uint16_t ref;
   unsigned i;
   int held = 1;

   printf("emission clock is gated on port 2's row select\n");

   attach(DEV_MOUSE_ST);
   p_Feed(1, 40, 0, 0);

   /* Sample the mouse bits without clocking: a read with no intervening
    * write is unarmed, so it cannot advance the encoder.  The first read
    * consumes the arm left by the write; the second is the sample. */
   p_WriteWord(0, row_words[0]);
   (void)p_ReadWord(0);
   ref = (uint16_t)(p_ReadWord(0) & 0xF000u);

   for (i = 0; i < 200; i++)
   {
      p_WriteWord(0, p1_row_word);
      if ((uint16_t)(p_ReadWord(0) & 0xF000u) != ref)
         held = 0;
   }

   sprintf(detail,
           "200 port-1 row polls do not advance the port-2 encoder "
           "($F14000 bits 12-15 held at $%X)",
           (unsigned)((ref >> 12) & 0x0Fu));
   report(held, detail);

   /* The queued motion is still there afterwards.  run_polls primes with
    * one port-2 write+read (one state) and then polls 30 times, so a
    * backlog that survived the port-1 traffic decodes exactly 30. */
   run_polls(&r, 'A', DEV_MOUSE_ST, 30, 0, 0, 0);
   sprintf(detail,
           "the motion queued before them is not lost (NET_X=%d expect 30, "
           "dropped=%u)", (int)r.net_x, r.dropped);
   report(r.net_x == 30 && r.dropped == 0, detail);
}

static void test_pad_is_inert(void)
{
   decode_result r;
   int bl[4], br[4];
   unsigned i;

   printf("no device selected -> the overlay is inert\n");

   p_SetType(1, DEV_PAD);
   p_Reset();

   report(p_GetType(1) == DEV_PAD && p_AnyAttached() == 0,
          "port 2 reports pad and nothing is attached");

   /* Feeding a detached port must do nothing at all. */
   for (i = 0; i < 20; i++)
      p_Feed(1, 100, 100, BTN_LEFT | BTN_RIGHT);

   run_polls(&r, 'A', DEV_MOUSE_ST, 20, 0, 0, 0);
   sweep_rows(bl, br);
   sprintf(detail, "no motion or buttons reach $F14000/$F14002 "
           "(NET_X=%d NET_Y=%d LMB=%d RMB=%d)",
           (int)r.net_x, (int)r.net_y, bl[0], br[0]);
   report(r.net_x == 0 && r.net_y == 0 && !bl[0] && !br[0], detail);
}

/*
 * THE FRONTEND MUST NOT BE ABLE TO SILENTLY DETACH THE MOUSE.
 *
 * retro_set_controller_port_device used to force INPUTDEV_PAD on any
 * JOYPAD/NONE assignment and defer to "the next check_variables()".
 * Nothing schedules one, and RetroArch issues a per-port controller init
 * right after load -- so the mouse the core option had just attached was
 * detached for the entire session.  That is the regression that shipped;
 * this pins it.
 *
 * The core option is set AFTER the load deliberately: the harness's
 * environment callback serves cfg.options live, so this measures what
 * retro_set_controller_port_device resolves at call time, which is the
 * thing under test.
 */
static void test_frontend_pad_assignment_rereads_option(harness_config *cfg,
                                                        void (*p_SetPort)(unsigned, unsigned))
{
   /* Mirrors libretro.h; this file deliberately includes no emulator or
    * libretro headers (see the InputDevType comment above). */
   const unsigned DEV_NONE_ID   = 0;
   const unsigned DEV_JOYPAD_ID = 1;
   const unsigned DEV_MOUSE_ID  = 2;

   printf("frontend port-device assignment vs the core option\n");

   if (!p_SetPort)
   {
      report(0, "retro_set_controller_port_device resolvable");
      return;
   }

   harness_set_option(cfg, "virtualjaguar_p2_device", "mouse_st");

   /* virtualjaguar_p2_device = mouse_st is now in effect.  A frontend
    * assigning JOYPAD to port 2 -- exactly RetroArch's post-load per-port
    * init -- must leave the option's mouse attached. */
   p_SetPort(1, DEV_JOYPAD_ID);
   sprintf(detail,
           "RETRO_DEVICE_JOYPAD on port 2 re-reads the core option instead of "
           "forcing pad (type=%d, expect %d)", p_GetType(1), (int)DEV_MOUSE_ST);
   report(p_GetType(1) == DEV_MOUSE_ST, detail);

   p_SetPort(1, DEV_NONE_ID);
   sprintf(detail,
           "RETRO_DEVICE_NONE on port 2 likewise (type=%d, expect %d)",
           p_GetType(1), (int)DEV_MOUSE_ST);
   report(p_GetType(1) == DEV_MOUSE_ST, detail);

   /* An explicit frontend mouse still outranks the option, and port 1 is
    * never given a mouse whatever the frontend says. */
   p_SetPort(1, DEV_MOUSE_ID);
   report(p_GetType(1) == DEV_MOUSE_ST,
          "an explicit frontend mouse on port 2 is honoured");

   p_SetPort(0, DEV_MOUSE_ID);
   report(p_GetType(0) == DEV_PAD,
          "a frontend mouse on port 1 is still refused");

   p_SetPort(0, DEV_JOYPAD_ID);
   p_SetPort(1, DEV_JOYPAD_ID);
}

static void test_port1_rejects_mouse(void)
{
   printf("port scope\n");
   p_SetType(0, DEV_MOUSE_ST);
   report(p_GetType(0) == DEV_PAD,
          "InputDevSetType refuses a mouse on port 1 "
          "(vendor-documented port-2 device)");
   p_SetType(0, DEV_PAD);
}

static void test_savestate(void)
{
   uint8_t *snap;
   size_t   sz;
   decode_result cont, restored;
   int      ok;

   printf("savestate round-trip mid-motion\n");

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

   /* Drive the encoder into the middle of a Gray cycle with a non-empty
    * backlog and a non-zero Q8 carry, then save. */
   attach(DEV_MOUSE_ST);
   p_SetScale(1, 96);               /* 37.5%: guarantees a carry remainder */
   run_polls(&cont, 'A', DEV_MOUSE_ST, 7, 3, 2, BTN_LEFT);

   ok = p_serialize(snap, sz) ? 1 : 0;
   report(ok, "retro_serialize succeeded mid-motion");
   if (!ok)
   {
      free(snap);
      return;
    }

   /* Continue from here and record what the machine does next. */
   run_polls(&cont, 'A', DEV_MOUSE_ST, 25, 3, 2, 0);

   /* Now roll back and replay the identical input.  If the phase, the
    * backlog or the Q8 carry were outside the state blob, the replay
    * diverges -- issue #400's failure mode exactly. */
   ok = p_unserialize(snap, sz) ? 1 : 0;
   report(ok, "retro_unserialize accepted the v12 state");

   run_polls(&restored, 'A', DEV_MOUSE_ST, 25, 3, 2, 0);

   sprintf(detail,
           "post-restore replay matches (NET_X %d vs %d, NET_Y %d vs %d, "
           "dropped %u vs %u)",
           (int)cont.net_x, (int)restored.net_x,
           (int)cont.net_y, (int)restored.net_y,
           cont.dropped, restored.dropped);
   report(cont.net_x == restored.net_x && cont.net_y == restored.net_y
          && cont.dropped == restored.dropped && restored.dropped == 0,
          detail);

   /* An older (v11) state must still load.  v11 is what shipped in
    * v3.3.0, i.e. what real users' existing saves carry, and the v12
    * chunk is TRAILING precisely so those keep working: a v11 header
    * simply means "stop before the input-device chunk".  Patch the
    * header's version field down and re-load. */
   if (p_serialize(snap, sz))
   {
      uint32_t v11 = 11;

      memcpy(snap + 4, &v11, sizeof(v11));   /* header: magic, version, ... */
      report(p_unserialize(snap, sz) ? 1 : 0,
             "a v11 state (the v3.3.0 release layout) still loads");
   }
   else
      report(0, "re-serialize for the v11 downgrade check");

   free(snap);
   p_SetScale(1, 256);
}

static void test_chunk_size(void)
{
   uint8_t scratch[256];
   size_t  n;

   printf("state chunk\n");

   if (!p_StateSave)
   {
      report(0, "InputDevStateSave resolvable");
      return;
   }

   memset(scratch, 0, sizeof(scratch));
   n = p_StateSave(scratch);
   sprintf(detail,
           "InputDevStateSave wrote %u bytes; inputdev.h advertises "
           "INPUTDEV_STATE_SIZE = %d (a mismatch means the trailing chunk "
           "grew without its constant)",
           (unsigned)n, INPUTDEV_STATE_SIZE_EXPECTED);
   report(n == (size_t)INPUTDEV_STATE_SIZE_EXPECTED, detail);
}

int main(int argc, char **argv)
{
   harness_config cfg = HARNESS_CONFIG_DEFAULT;
   harness_result result;
   char summary[128];
   int t;

   cfg.frames = 0;
   if (!harness_init_from_args(&cfg, argc, argv))
      return 1;

   if (!cfg.rom_path)
      cfg.rom_path = "test/roms/yarc.j64";
   if (!harness_load_rom(&cfg))
      return 1;

   p_ReadWord    = (uint16_t (*)(uint32_t))harness_dlsym(&cfg, "JoystickReadWord");
   p_WriteWord   = (void (*)(uint32_t, uint16_t))harness_dlsym(&cfg, "JoystickWriteWord");
   p_SetType     = (void (*)(int, InputDevType))harness_dlsym(&cfg, "InputDevSetType");
   p_SetScale    = (void (*)(int, int32_t))harness_dlsym(&cfg, "InputDevSetScale");
   p_Reset       = (void (*)(void))harness_dlsym(&cfg, "InputDevReset");
   p_Feed        = (void (*)(int, int32_t, int32_t, uint32_t))
                      harness_dlsym(&cfg, "InputDevFeed");
   p_GetType     = (int (*)(int))harness_dlsym(&cfg, "InputDevGetType");
   p_AnyAttached = (int (*)(void))harness_dlsym(&cfg, "InputDevAnyAttached");
   p_StateSave   = (size_t (*)(uint8_t *))
                      harness_dlsym(&cfg, "InputDevStateSave");

   p_serialize_size = (size_t (*)(void))harness_dlsym(&cfg, "retro_serialize_size");
   p_serialize      = (int (*)(void *, size_t))harness_dlsym(&cfg, "retro_serialize");
   p_unserialize    = (int (*)(const void *, size_t))
                         harness_dlsym(&cfg, "retro_unserialize");

   if (!p_ReadWord || !p_WriteWord || !p_SetType || !p_SetScale
       || !p_Reset || !p_Feed || !p_GetType || !p_AnyAttached)
   {
      fprintf(stderr,
              "mouse_decode_test: missing test-ABI symbols.  The wide test "
              "ABI must export Joystick* and InputDev* (exports-test.list "
              "and link-test.T).  Build with TEST_EXPORTS=1.\n");
      harness_shutdown(&cfg);
      return 1;
   }

   for (t = DEV_MOUSE_ST; t <= DEV_MOUSE_AMIGA_ON_ST; t++)
   {
      test_directions(t);
      test_rate_ceiling(t);
      test_poll_shapes(t);
      test_row_blindness(t);
   }

   test_case_discrimination();
   test_port1_poll_does_not_clock();
   test_savestate();
   test_chunk_size();
   test_port1_rejects_mouse();
   test_pad_is_inert();
   /* Last: it leaves a core option set, which nothing after it should see. */
   test_frontend_pad_assignment_rereads_option(
      &cfg, (void (*)(unsigned, unsigned))
               harness_dlsym(&cfg, "retro_set_controller_port_device"));

   sprintf(summary, "%d failure(s)", failures);
   result.status = failures ? "FAIL" : "PASS";
   result.name   = "mouse_decode";
   result.detail = summary;
   harness_report(&cfg, &result, 1);

   harness_shutdown(&cfg);
   return failures ? 1 : 0;
}
