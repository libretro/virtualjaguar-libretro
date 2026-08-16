/*
 * src/jerry/inputdev.c -- per-port non-pad input device model.
 *
 * See inputdev.h for the row-blindness argument, the port scope and the
 * emission rule.  Bit assignments below come from
 * docs/jaguar-mouse-adapter-mapping.md section 4d and are NOT guesses.
 */

#include <string.h>

#include "inputdev.h"
#include "quadrature.h"
#include "joystick.h"
#include "state.h"

/* Wiring table.  Values are $F14000 bit positions for PORT 2, all active
 * low (0 = asserted).  Written as a literal table so the three cases
 * cannot drift apart.
 *
 *   case 1  ST adapter + ST mouse, and the PS/2 adapter (vendor-documented
 *           as "select Atari ST mouse")     X = 13/12   Y = 14/15
 *   case 2  dedicated Amiga adapter + Amiga X = 13/12   Y = 15/14
 *   case 3  Amiga mouse in an ST-wired
 *           adapter -- what an in-game
 *           "Atari / Amiga" selector picks  X = 13/15   Y = 12/14
 *
 * Do NOT "fix" the case-1 Y-phase assignment from Domin's twomice2.s
 * variable names: that source labels bit 15 YA and bit 14 YB, the reverse
 * of the wiring diagram, and its own decode table absorbs the difference
 * (mapping doc section 8 C).  The table below follows the physical wiring,
 * which two independent sources agree on.
 *
 * UNVERIFIED -- case 2's Y sign.  The dedicated Amiga adapter lands the
 * Amiga's V (Y phase A) on bit 15 and VQ (Y phase B) on bit 14, i.e. the
 * A/B pair is swapped relative to case 1, which inverts Y.  Domin
 * nonetheless drives both with one software mode, so either ST and Amiga
 * mice differ in encoder orientation and the swap cancels it, or the
 * wiki's (YA)/(YB) labels on the Amiga column are editorial rather than
 * measured, or he simply tolerated an inverted Y.  No document settles it
 * and NO FIXTURE IN THIS REPO CAN: case-2 hardware is rare and no
 * available title is known to target it.  The table implements the wiring
 * literally, as recorded, rather than guessing a sign.  Do not "correct"
 * it without a source. */
typedef struct
{
   uint8_t xa, xb, ya, yb;
} MouseWiring;

static const MouseWiring mouse_wiring[3] = {
   /* INPUTDEV_MOUSE_ST            */ { 13, 12, 14, 15 },
   /* INPUTDEV_MOUSE_AMIGA_ADAPTER */ { 13, 12, 15, 14 },
   /* INPUTDEV_MOUSE_AMIGA_ON_ST   */ { 13, 15, 12, 14 }
};

typedef struct
{
   InputDevType type;
   quad_axis    x;
   quad_axis    y;
   int32_t      scale_q8;
   uint8_t      btn_left;
   uint8_t      btn_right;
   uint8_t      rotary_id;   /* option-derived; see InputDevSetRotaryID */
} InputDevPort;

static InputDevPort inputdev_ports[2];
static uint32_t     inputdev_attach_mask;
static uint8_t      inputdev_armed;

/* joypadNButtons[], indexed by port.  A rotary is a matrix device and
 * reports through these slots; the mouse never touches them. */
static uint8_t *const inputdev_pad[2] = { joypad0Buttons, joypad1Buttons };

static int inputdev_is_mouse(InputDevType t)
{
   return (t == INPUTDEV_MOUSE_ST
           || t == INPUTDEV_MOUSE_AMIGA_ADAPTER
           || t == INPUTDEV_MOUSE_AMIGA_ON_ST) ? 1 : 0;
}

/* Dynamic (machine-visible) state only: encoders, button latches and the
 * armed flag.  The device type and the sensitivity scale are owned by the
 * core options and survive a machine reset, exactly as a physical mouse
 * stays plugged in across a console reset. */
static void inputdev_reset_dynamic(void)
{
   unsigned p;

   for (p = 0; p < 2; p++)
   {
      QuadReset(&inputdev_ports[p].x);
      QuadReset(&inputdev_ports[p].y);
      inputdev_ports[p].btn_left  = 0;
      inputdev_ports[p].btn_right = 0;
   }

   inputdev_armed = 0;
}

void InputDevInit(void)
{
   unsigned p;

   for (p = 0; p < 2; p++)
      if (inputdev_ports[p].scale_q8 <= 0)
         inputdev_ports[p].scale_q8 = 256;

   inputdev_reset_dynamic();
}

void InputDevReset(void)
{
   inputdev_reset_dynamic();
}

void InputDevShutdown(void)
{
   /* iOS cannot dlclose a core, so every static has to go back to its
    * load-time value here -- including the option-derived ones. */
   memset(inputdev_ports, 0, sizeof(inputdev_ports));
   inputdev_ports[0].scale_q8 = 256;
   inputdev_ports[1].scale_q8 = 256;
   inputdev_attach_mask       = 0;
   inputdev_armed             = 0;
}

void InputDevSetType(int port, InputDevType type)
{
   if (port < 0 || port > 1)
      return;

   /* Mouse is port 2 only (see inputdev.h); the rotary is valid on both.
    * Anything else is a plain pad, i.e. not attached. */
   if (inputdev_is_mouse(type))
   {
      if (port != 1)
         type = INPUTDEV_PAD;
   }
   else if (type != INPUTDEV_ROTARY)
      type = INPUTDEV_PAD;

   if (inputdev_ports[port].type != type)
   {
      inputdev_ports[port].type = type;
      QuadReset(&inputdev_ports[port].x);
      QuadReset(&inputdev_ports[port].y);
      inputdev_ports[port].btn_left  = 0;
      inputdev_ports[port].btn_right = 0;
      /* The arm flag is part of the dynamic state too: without this a
       * mouse -> pad -> mouse round trip leaves a stale arm behind, worth
       * one unrequested advance on the next read.  Cleared only on a real
       * type change -- apply_port_device() calls this on every
       * check_variables(), and an unconditional clear there would eat a
       * legitimate arm in the common path. */
      inputdev_armed = 0;
   }

   if (type == INPUTDEV_PAD)
      inputdev_attach_mask &= ~(1u << port);
   else
      inputdev_attach_mask |= (1u << port);
}

InputDevType InputDevGetType(int port)
{
   if (port < 0 || port > 1)
      return INPUTDEV_PAD;
   return inputdev_ports[port].type;
}

int InputDevAnyAttached(void)
{
   return inputdev_attach_mask ? 1 : 0;
}

void InputDevSetScale(int port, int32_t scale_q8)
{
   if (port < 0 || port > 1)
      return;
   if (scale_q8 < 1)
      scale_q8 = 1;
   if (scale_q8 > 4096)
      scale_q8 = 4096;
   inputdev_ports[port].scale_q8 = scale_q8;
}

void InputDevSetRotaryID(int port, int reports_rotary)
{
   if (port < 0 || port > 1)
      return;
   inputdev_ports[port].rotary_id = reports_rotary ? 1 : 0;
}

void InputDevFeed(int port, int32_t dx, int32_t dy, uint32_t buttons)
{
   InputDevPort *p;

   if (port < 0 || port > 1)
      return;

   p = &inputdev_ports[port];

   if (p->scale_q8 <= 0)
      p->scale_q8 = 256;

   if (p->type == INPUTDEV_ROTARY)
   {
      /* One axis, not two: the rotary is a single wheel.  dy is ignored,
       * and so are the mouse buttons -- a rotary's A/B/C/Option/Pause and
       * keypad are real matrix switches and come from the retropad.
       *
       * DIRECTION.  TR10 gives the phase sequences as pin levels:
       *   anticlockwise  J10 0 1 1 0 ...   clockwise  J10 0 0 1 1 ...
       *                  J11 0 0 1 1 ...              J11 0 1 1 0 ...
       * i.e. anticlockwise = (A,B) walking 00,10,11,01 = INCREASING Gray
       * index = A leads B, and clockwise = decreasing.
       *
       * Mapping host +X to clockwise is a HOST-SIDE UX CONVENTION, not a
       * hardware fact: pushing a spinner to the right turns the knob
       * clockwise.  Hence the negation.  There is deliberately no invert
       * option -- an unsourced sign knob is how a real wiring bug gets
       * papered over instead of found. */
      QuadFeed(&p->x, -dx, p->scale_q8);
      (void)dy;
      (void)buttons;
      return;
   }

   if (!inputdev_is_mouse(p->type))
      return;

   /* One libretro unit = one Gray-code state at scale 1.0.  Domin's
    * decoder increments its position by one per state transition, so one
    * host "pixel" of motion maps to one decoded count.  Positive dx is
    * right and positive dy is down in libretro, and A-leads-B (increasing
    * phase) is right/down on the wire, so the mapping is direct. */
   QuadFeed(&p->x, dx, p->scale_q8);
   QuadFeed(&p->y, dy, p->scale_q8);

   p->btn_left  = (buttons & INPUTDEV_BTN_LEFT)  ? 1 : 0;
   p->btn_right = (buttons & INPUTDEV_BTN_RIGHT) ? 1 : 0;
}

void InputDevArm(void)
{
   if (!inputdev_attach_mask)
      return;
   inputdev_armed = 1;
}

/* Write a rotary port's current phase into its pad slots.
 *
 * POLARITY IS INVERTED RELATIVE TO THE MOUSE OVERLAY, which is the single
 * easiest way to ship a backwards spinner.  TR10 gives phase sequences as
 * PIN LEVELS, and "reading a zero means the appropriate button is
 * depressed"; joystick.c does `data &= (joypadNButtons[slot] ? mask :
 * 0xFFFF)`, so a NON-ZERO array entry pulls the bit low.  Therefore
 * pin level 1 -> slot 0x00, pin level 0 -> slot 0xFF.  The mouse overlay
 * drives raw $F14000 bits where "0 = asserted" applies with no inversion.
 * Two conventions in one feature, which is exactly why QuadLevels()
 * returns logic levels and each sink applies its own polarity here.
 *
 * Phase 0 -> BUTTON_L, Phase 1 -> BUTTON_R (TR10's row-0 matrix: J10/J14
 * and J11/J15, the same slot indices on both ports).  BUTTON_U / BUTTON_D
 * are forced clear: the controller physically has no up/down, which is
 * why TR10 tells rotary games to use A = Up and C = Down for menus.
 *
 * The encoder's rest phase is (1,1) -- QUAD_REST_PHASE, chosen by #429 --
 * so an idle rotary writes 0x00 into all four slots and is bit-identical
 * to an idle pad. */
static void inputdev_publish_rotary(unsigned port)
{
   uint8_t *pad = inputdev_pad[port];
   int a, b;

   QuadLevels(&inputdev_ports[port].x, &a, &b);

   pad[BUTTON_U] = 0x00;
   pad[BUTTON_D] = 0x00;
   pad[BUTTON_L] = a ? 0x00 : 0xFF;
   pad[BUTTON_R] = b ? 0x00 : 0xFF;
}

void InputDevClock(uint8_t row0, uint8_t row1)
{
   uint8_t  row[2];
   unsigned p;
   int      armed, consumed;

   if (!inputdev_attach_mask)
      return;

   row[0]   = row0;
   row[1]   = row1;
   armed    = inputdev_armed ? 1 : 0;
   consumed = 0;

   for (p = 0; p < 2; p++)
   {
      InputDevType t = inputdev_ports[p].type;

      if (inputdev_is_mouse(t))
      {
         /* Row-blind: every read of ITS PORT is a sample, whatever the
          * row, so any armed read with that port addressed advances.
          * Both axes, independently -- they are four parallel wires, not
          * a time-multiplexed stream. */
         if (armed && row[p] != 0xFF)
         {
            QuadAdvance(&inputdev_ports[p].x);
            QuadAdvance(&inputdev_ports[p].y);
            consumed = 1;
         }
      }
      else if (t == INPUTDEV_ROTARY)
      {
         /* Row-gated: a rotary's phases exist in row 0 only, so the only
          * read that samples it is a row-0 read of its own port.  See the
          * measured Tempest 2000 scan in inputdev.h -- advancing on all
          * eight of its per-frame armed reads would decode as garbage,
          * including a sign inversion at three states. */
         if (armed && row[p] == 0)
         {
            QuadAdvance(&inputdev_ports[p].x);
            consumed = 1;
         }

         /* Published on EVERY call, not only when it advanced:
          * update_input() zeroes a rotary port's four direction slots
          * each frame, so without an unconditional publish a rotary would
          * read as "both phases high" for every read between the frame
          * boundary and the next armed row-0 advance. */
         inputdev_publish_rotary(p);
      }
   }

   /* The arm is spent only by a read some attached device could decode
    * from (inputdev.h, ARM CONSUMPTION).
    *
    * THIS GUARDS NOTHING REACHABLE TODAY, and the comment here used to
    * claim otherwise.  The reasoning that motivated it -- a port-1-only
    * read eating the arm a port-2 mouse was owed -- cannot happen on the
    * bus: the row select lives in joystick_ram[1], the only writer is
    * JoystickWriteWord at offset 0, and that path arms unconditionally.
    * So the arm is always set at the first read after any row change,
    * and a later read at unchanged rows re-runs this same gate to the
    * same answer.  Measured, not argued: 2.4M random write/read
    * sequences across six device combinations digest bit-identically
    * with and without the `if`.
    *
    * It stays because it is the honest statement of the contract -- an
    * arm is a permission for ONE decodable sample, not for one bus read
    * -- and the next device that samples on some other condition (a
    * lightgun latch, a second row-blind peripheral) makes the difference
    * real.  Pinned by test_arm_is_spent_only_by_a_decodable_read in
    * mouse_decode_test.c, which reaches it by calling InputDevArm() and
    * InputDevClock() directly, because no aligned bus sequence can. */
   if (consumed)
      inputdev_armed = 0;
}

uint16_t InputDevOverlayF14000(uint16_t data)
{
   const MouseWiring *w;
   InputDevPort *p;
   int ax, bx, ay, by;

   if (!inputdev_attach_mask)
      return data;

   p = &inputdev_ports[1];
   if (!inputdev_is_mouse(p->type))
      return data;

   w = &mouse_wiring[(int)p->type - (int)INPUTDEV_MOUSE_ST];

   QuadLevels(&p->x, &ax, &bx);
   QuadLevels(&p->y, &ay, &by);

   /* Active low: a logic 0 on the wire pulls the register bit low.  No
    * row test -- the adapter never sees the row select, so it asserts
    * identically in every row (including the "not a socket-0 code" case,
    * where the pad matrix contributes nothing at all). */
   if (!ax)
      data &= (uint16_t)~(1u << w->xa);
   if (!bx)
      data &= (uint16_t)~(1u << w->xb);
   if (!ay)
      data &= (uint16_t)~(1u << w->ya);
   if (!by)
      data &= (uint16_t)~(1u << w->yb);

   return data;
}

uint16_t InputDevOverlayF14002(uint16_t data, uint8_t row0, uint8_t row1)
{
   InputDevPort *p;

   if (!inputdev_attach_mask)
      return data;

   /* Rotary controller-type identification (TR10 "Identifying Controller
    * Types"; see InputDevSetRotaryID).  C2 C3 = 1 0 is "Tempest Rotary",
    * and the C-column rows differ by port: C3 is $F14002 bit 0 in row 3
    * on port 1, and bit 2 in row 2 on port 2.  C2 is left high, which is
    * what joystick.c already returns.
    *
    * joystick.c drives only bit 1 (port 1) / bit 3 (port 2) in those
    * rows, so clearing the C bit here cannot collide with a button. */
   if (inputdev_ports[0].type == INPUTDEV_ROTARY
       && inputdev_ports[0].rotary_id && row0 == 3)
      data &= (uint16_t)~(1u << 0);

   if (inputdev_ports[1].type == INPUTDEV_ROTARY
       && inputdev_ports[1].rotary_id && row1 == 2)
      data &= (uint16_t)~(1u << 2);

   /* The mouse is row-blind, so its own contribution ignores both rows. */
   p = &inputdev_ports[1];
   if (!inputdev_is_mouse(p->type))
      return data;

   /* LMB -> B3 ($F14002 bit 3), RMB -> B2 (bit 2), in EVERY row.
    *
    * Both of these are faithful hardware quirks, not oversights:
    * joystick.c decodes bit 3 as A / B / C / Option depending on the row,
    * so a held LMB reads as all four; and bit 2 is the C1/C2/C3
    * controller-type field in rows 1-3, so a held RMB changes the
    * reported controller type.  Do not add a row test to "fix" either. */
   if (p->btn_left)
      data &= (uint16_t)~(1u << 3);
   if (p->btn_right)
      data &= (uint16_t)~(1u << 2);

   return data;
}

size_t InputDevStateSave(uint8_t *buf)
{
   uint8_t *start = buf;
   unsigned p;

   for (p = 0; p < 2; p++)
   {
      STATE_SAVE_VAR(buf, inputdev_ports[p].x.backlog);
      STATE_SAVE_VAR(buf, inputdev_ports[p].x.frac);
      STATE_SAVE_VAR(buf, inputdev_ports[p].x.phase);
      STATE_SAVE_VAR(buf, inputdev_ports[p].y.backlog);
      STATE_SAVE_VAR(buf, inputdev_ports[p].y.frac);
      STATE_SAVE_VAR(buf, inputdev_ports[p].y.phase);
      STATE_SAVE_VAR(buf, inputdev_ports[p].btn_left);
      STATE_SAVE_VAR(buf, inputdev_ports[p].btn_right);
   }

   STATE_SAVE_VAR(buf, inputdev_armed);

   return (size_t)(buf - start);
}

/* A savestate is untrusted input.  `phase` was already masked on load;
 * `backlog` and `frac` need the same treatment, because QuadFeed() clamps
 * only AFTER `q->backlog += states`, so a corrupt backlog near INT32_MAX
 * overflows before the clamp can see it.  The invariants are the ones
 * QuadFeed/QuadAdvance maintain: |backlog| <= QUAD_MAX_BACKLOG and
 * |frac| < 256 (it is a Q8 remainder). */
static void inputdev_sanitize_axis(quad_axis *q)
{
   q->phase &= 3;

   if (q->backlog >  QUAD_MAX_BACKLOG)
      q->backlog =  QUAD_MAX_BACKLOG;
   if (q->backlog < -QUAD_MAX_BACKLOG)
      q->backlog = -QUAD_MAX_BACKLOG;

   if (q->frac >  255)
      q->frac =  255;
   if (q->frac < -255)
      q->frac = -255;
}

size_t InputDevStateLoad(const uint8_t *buf)
{
   const uint8_t *start = buf;
   unsigned p;

   for (p = 0; p < 2; p++)
   {
      STATE_LOAD_VAR(buf, inputdev_ports[p].x.backlog);
      STATE_LOAD_VAR(buf, inputdev_ports[p].x.frac);
      STATE_LOAD_VAR(buf, inputdev_ports[p].x.phase);
      STATE_LOAD_VAR(buf, inputdev_ports[p].y.backlog);
      STATE_LOAD_VAR(buf, inputdev_ports[p].y.frac);
      STATE_LOAD_VAR(buf, inputdev_ports[p].y.phase);
      STATE_LOAD_VAR(buf, inputdev_ports[p].btn_left);
      STATE_LOAD_VAR(buf, inputdev_ports[p].btn_right);
      inputdev_sanitize_axis(&inputdev_ports[p].x);
      inputdev_sanitize_axis(&inputdev_ports[p].y);
   }

   STATE_LOAD_VAR(buf, inputdev_armed);

   return (size_t)(buf - start);
}
