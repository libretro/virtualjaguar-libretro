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
} InputDevPort;

static InputDevPort inputdev_ports[2];
static uint32_t     inputdev_attach_mask;
static uint8_t      inputdev_armed;

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

   /* Mouse is port 2 only (see inputdev.h), and the rotary is reserved
    * for #436 and not implemented in this build.  Anything else is a
    * plain pad, i.e. not attached. */
   if (type != INPUTDEV_PAD && !(inputdev_is_mouse(type) && port == 1))
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

void InputDevFeed(int port, int32_t dx, int32_t dy, uint32_t buttons)
{
   InputDevPort *p;

   if (port < 0 || port > 1)
      return;

   p = &inputdev_ports[port];
   if (!inputdev_is_mouse(p->type))
      return;

   if (p->scale_q8 <= 0)
      p->scale_q8 = 256;

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

void InputDevClock(void)
{
   unsigned p;

   if (!inputdev_attach_mask)
      return;
   if (!inputdev_armed)
      return;

   inputdev_armed = 0;

   for (p = 0; p < 2; p++)
   {
      if (!inputdev_is_mouse(inputdev_ports[p].type))
         continue;
      QuadAdvance(&inputdev_ports[p].x);
      QuadAdvance(&inputdev_ports[p].y);
   }
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

   /* The mouse is row-blind, so it ignores both rows; they are here for
    * the rotary's C2/C3 controller-type reporting (#436). */
   (void)row0;
   (void)row1;

   if (!inputdev_attach_mask)
      return data;

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
