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
#include "axistune.h"
#include "joystick.h"
#include "paddle.h"
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
   axis_tune    tune_x;      /* option-derived; see InputDevSetTune (#439) */
   axis_tune    tune_y;
   uint8_t      btn_left;
   uint8_t      btn_right;
   uint8_t      rotary_id;   /* option-derived; see InputDevSetRotaryID */
   /* Analog / driving controller (#437) -- TR10 bank-switching device.
    * an_bank / an_row are the microcontroller's bank counter and the row
    * code it last saw on its socket; an_x / an_y are the latched 8-bit
    * ADC values (128 = centre); an_sw is the INPUTDEV_SW_* mask.  All
    * five are machine-visible and serialized.  an_engaged is the
    * host-routing liveness latch (inputdev.h, ENGAGEMENT) and is NOT. */
   uint8_t      an_bank;
   uint8_t      an_row;      /* last row decoded, 0-3; 0xFF = none yet */
   uint8_t      an_x;
   uint8_t      an_y;
   uint8_t      an_sw;
   uint8_t      an_engaged;
   int32_t      gun_col;     /* light gun aim, NATIVE framebuffer pixels */
   int32_t      gun_row;
   uint8_t      gun_offscreen;
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

/* Analog stick and driving controller are one wire protocol (inputdev.h);
 * the two types differ only in how libretro.c sources host values. */
static int inputdev_is_analog(InputDevType t)
{
   return (t == INPUTDEV_ANALOG || t == INPUTDEV_DRIVING) ? 1 : 0;
}

/* Dynamic (machine-visible) state only: encoders, button latches and the
 * armed flag.  The device type, the sensitivity scale, and the per-axis
 * tuning (tune_x/tune_y, #439 -- dead zone, offset, response exponent) are
 * all owned by the core options and survive a machine reset, exactly as a
 * physical mouse stays plugged in across a console reset. Deliberate, and
 * load-bearing: see test/tools/tuning_identity.c's sweep-order comment. */
static void inputdev_reset_dynamic(void)
{
   unsigned p;

   for (p = 0; p < 2; p++)
   {
      QuadReset(&inputdev_ports[p].x);
      QuadReset(&inputdev_ports[p].y);
      inputdev_ports[p].btn_left  = 0;
      inputdev_ports[p].btn_right = 0;
      /* Analog device (#437): the microcontroller powers up in bank 0
       * with nothing latched.  Engagement clears too -- the port has to
       * prove live again after a reset, exactly as after a type change. */
      inputdev_ports[p].an_bank    = 0;
      inputdev_ports[p].an_row     = 0xFF;
      inputdev_ports[p].an_x       = 128;
      inputdev_ports[p].an_y       = 128;
      inputdev_ports[p].an_sw      = 0;
      inputdev_ports[p].an_engaged = 0;
      inputdev_ports[p].gun_offscreen = 1;
   }

   inputdev_armed = 0;
}

void InputDevInit(void)
{
   unsigned p;

   for (p = 0; p < 2; p++)
   {
      if (inputdev_ports[p].scale_q8 <= 0)
         inputdev_ports[p].scale_q8 = 256;

      /* A zero-filled axis_tune already reads as the identity (see
       * axistune.h), so this is belt-and-braces rather than load-bearing
       * -- but it makes the default explicit at exactly the place the
       * scale's default is, instead of leaving it to a reader to work out
       * that exponent_q8 == 0 means linear. */
      if (inputdev_ports[p].tune_x.exponent_q8 <= 0)
         AxisTuneReset(&inputdev_ports[p].tune_x);
      if (inputdev_ports[p].tune_y.exponent_q8 <= 0)
         AxisTuneReset(&inputdev_ports[p].tune_y);
   }

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
   AxisTuneReset(&inputdev_ports[0].tune_x);
   AxisTuneReset(&inputdev_ports[0].tune_y);
   AxisTuneReset(&inputdev_ports[1].tune_x);
   AxisTuneReset(&inputdev_ports[1].tune_y);
   inputdev_ports[0].gun_offscreen = 1;
   inputdev_ports[1].gun_offscreen = 1;
   inputdev_attach_mask       = 0;
   inputdev_armed             = 0;
}

void InputDevSetType(int port, InputDevType type)
{
   if (port < 0 || port > 1)
      return;

   /* Mouse is port 2 only and the light gun is port 1 only (both are
    * hardware facts -- see inputdev.h); the rotary, the analog / driving
    * controller (#437 -- TR10 restricts neither socket) and the paddle
    * (#505 -- the ADC has a channel pair per socket) are valid on both.
    * Anything else is a plain pad, i.e. not attached. */
   if (inputdev_is_mouse(type))
   {
      if (port != 1)
         type = INPUTDEV_PAD;
   }
   else if (type == INPUTDEV_LIGHTGUN)
   {
      if (port != 0)
         type = INPUTDEV_PAD;
   }
   else if (type != INPUTDEV_ROTARY && type != INPUTDEV_PADDLE
            && !inputdev_is_analog(type))
      type = INPUTDEV_PAD;

   if (inputdev_ports[port].type != type)
   {
      inputdev_ports[port].type = type;
      QuadReset(&inputdev_ports[port].x);
      QuadReset(&inputdev_ports[port].y);
      inputdev_ports[port].btn_left  = 0;
      inputdev_ports[port].btn_right = 0;
      /* Analog dynamic state is per-plug too: a freshly attached
       * controller is in bank 0 with nothing latched, and has to earn
       * engagement again (inputdev.h). */
      inputdev_ports[port].an_bank    = 0;
      inputdev_ports[port].an_row     = 0xFF;
      inputdev_ports[port].an_x       = 128;
      inputdev_ports[port].an_y       = 128;
      inputdev_ports[port].an_sw      = 0;
      inputdev_ports[port].an_engaged = 0;
      /* An unplugged gun stops pulsing LP, which is exactly "off-screen":
       * LPH/LPV keep whatever they last latched. */
      inputdev_ports[port].gun_col       = 0;
      inputdev_ports[port].gun_row       = 0;
      inputdev_ports[port].gun_offscreen = 1;
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

   /* Plug / unplug this port's pot lines at the motherboard ADC (#505).
    * Unconditional rather than inside the type-change block above: the
    * converter owns its own no-op-on-unchanged test, and routing the
    * unplug through here is what makes paddle -> pad actually unfit the
    * ADC and put $F17C00 back to the not-fitted $FF. */
   PaddleSetAttached(port, type == INPUTDEV_PADDLE);
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

void InputDevSetTune(int port, int axis, int32_t deadzone, int32_t offset,
                     int32_t exponent_q8)
{
   if (port < 0 || port > 1)
      return;

   AxisTuneSet(axis == INPUTDEV_AXIS_Y ? &inputdev_ports[port].tune_y
                                       : &inputdev_ports[port].tune_x,
               deadzone, offset, exponent_q8);
}

void InputDevSetRotaryID(int port, int reports_rotary)
{
   if (port < 0 || port > 1)
      return;
   inputdev_ports[port].rotary_id = reports_rotary ? 1 : 0;
}

/* The single tuned feed path (#439).  Every analog source funnels through
 * here, so the tuning has exactly one implementation and cannot drift
 * between devices.
 *
 * `invert` negates the TUNED delta, not the raw one, and the order matters:
 * the dead zone and the curve are magnitude-symmetric and commute with a
 * sign flip, but the OFFSET DOES NOT.  An offset cancels a bias measured on
 * the host axis, so it has to be subtracted in host orientation, before any
 * device-specific inversion.  Do not "simplify" this by negating `raw` at
 * the call site.
 *
 * The curve arrives as a Q8 gain and multiplies into the port's Q8
 * sensitivity, which is what lets QuadFeed's existing fractional carry keep
 * small movement alive -- axistune.h explains why a curved delta could not.
 * The product is clamped to the same 1..4096 window InputDevSetScale
 * enforces, because QuadFeed's overflow argument is written against a 4096
 * ceiling and two multipliers in series would otherwise walk past it. */
static void inputdev_feed_axis(quad_axis *q, const axis_tune *t,
                               int32_t raw, int invert, int32_t scale_q8)
{
   int32_t gain_q8 = 256;
   int32_t d       = AxisTuneApply(t, raw, &gain_q8);
   int32_t eff;

   if (invert)
      d = -d;

   /* At defaults gain_q8 is exactly 256 and this is exactly scale_q8, so
    * the call below is bit-for-bit the pre-#439 one. */
   eff = (scale_q8 * gain_q8) / 256;
   if (eff < 1)
      eff = 1;
   if (eff > 4096)
      eff = 4096;

   QuadFeed(q, d, eff);
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
       * papered over instead of found.  The negation is applied to the
       * TUNED delta by inputdev_feed_axis -- see the note there on why the
       * offset must be subtracted in host orientation first. */
      inputdev_feed_axis(&p->x, &p->tune_x, dx, 1, p->scale_q8);
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
   inputdev_feed_axis(&p->x, &p->tune_x, dx, 0, p->scale_q8);
   inputdev_feed_axis(&p->y, &p->tune_y, dy, 0, p->scale_q8);

   p->btn_left  = (buttons & INPUTDEV_BTN_LEFT)  ? 1 : 0;
   p->btn_right = (buttons & INPUTDEV_BTN_RIGHT) ? 1 : 0;
}

/* Absolute-axis conversion for the analog device (#437): libretro int16
 * -> tuned 8-bit ADC byte.  The tuning runs in the device's own 8-bit
 * domain (range 127) so the shared dead-zone/offset option units mean
 * "ADC counts" here, comparable in magnitude to the relative path's
 * "host units per poll".  `invert` flips the TUNED value -- the offset
 * must be subtracted in host orientation first, exactly as
 * inputdev_feed_axis does on the relative path (#474). */
static uint8_t inputdev_analog_byte(const axis_tune *t, int32_t v16,
                                    int invert)
{
   int32_t v8;

   /* -32768..32767 -> -128..127, then symmetrized to -127..127 so the
    * tuned magnitude maps cleanly onto 128 +/- 127 (byte range 1..255;
    * TR10 forbids games from assuming the endpoints anyway). */
   v8 = v16 / 258;
   if (v8 >  127)  v8 =  127;
   if (v8 < -127)  v8 = -127;

   v8 = AxisTuneApplyAbs(t, v8, 127);

   if (invert)
      v8 = -v8;

   return (uint8_t)(128 + v8);
}

void InputDevFeedAnalog(int port, int32_t x, int32_t y, uint32_t switches)
{
   InputDevPort *p;

   if (port < 0 || port > 1)
      return;

   p = &inputdev_ports[port];

   if (!inputdev_is_analog(p->type))
      return;

   /* TR10: +X = right (matches the libretro convention, no inversion) and
    * +Y = forward / accelerator, which is libretro's -Y -- hence the
    * inversion, applied to the tuned value. */
   p->an_x       = inputdev_analog_byte(&p->tune_x, x, 0);
   p->an_y       = inputdev_analog_byte(&p->tune_y, y, 1);
   p->an_sw      = (uint8_t)(switches & 0xFF);
   p->an_engaged = 1;
}

/* Paddle host feed (#505).  See inputdev.h for why Y is NOT inverted here
 * and why this device drives no matrix overlay; see paddle.h for the
 * converter itself.
 *
 * The 8-bit conversion is inputdev_analog_byte(), shared verbatim with
 * #437: both devices present a stick position as an 8-bit ADC count with
 * 128 at centre, so they must agree on what a given dead zone, offset or
 * response exponent does -- one arithmetic, two consumers, which is the
 * rule axistune.h sets out. */
void InputDevFeedPaddle(int port, int32_t x, int32_t y)
{
   InputDevPort *p;

   if (port < 0 || port > 1)
      return;

   p = &inputdev_ports[port];

   if (p->type != INPUTDEV_PADDLE)
      return;

   PaddleFeed(port,
              inputdev_analog_byte(&p->tune_x, x, 0),
              inputdev_analog_byte(&p->tune_y, y, 0));
}

/* Light gun host feed (#438).  See the block comment in inputdev.h for why
 * the trigger lands on BUTTON_B and why the aim is fed every frame rather
 * than only when the trigger is pulled. */
void InputDevFeedLightgun(int port, int32_t col, int32_t row,
                          int offscreen, uint32_t buttons)
{
   InputDevPort *p;
   uint8_t      *pad;

   if (port < 0 || port > 1)
      return;

   p = &inputdev_ports[port];

   if (p->type != INPUTDEV_LIGHTGUN)
      return;

   p->gun_col       = col;
   p->gun_row       = row;
   p->gun_offscreen = offscreen ? 1 : 0;

   /* A gun-equipped controller is a MODIFIED STANDARD CONTROLLER (TR10's
    * own phrase for the rotary, and the only shape a Jaguar peripheral
    * with buttons can take), so its switches are ordinary matrix slots --
    * the rotary's mechanism, not the mouse's row-blind overlay.  Written
    * here rather than OR-ed into the retropad fill so that a frontend
    * routing both a pad and a gun to port 1 cannot double-drive a line:
    * update_input() runs this after every retropad path. */
   pad = inputdev_pad[port];

   if (buttons & INPUTDEV_GUN_TRIGGER)
      pad[BUTTON_B]      = 0xFF;
   if (buttons & INPUTDEV_GUN_AUX_A)
      pad[BUTTON_A]      = 0xFF;
   if (buttons & INPUTDEV_GUN_AUX_B)
      pad[BUTTON_C]      = 0xFF;
   if (buttons & INPUTDEV_GUN_START)
      pad[BUTTON_OPTION] = 0xFF;
   if (buttons & INPUTDEV_GUN_SELECT)
      pad[BUTTON_PAUSE]  = 0xFF;
}

int InputDevLightgunAim(int32_t *col, int32_t *row)
{
   const InputDevPort *p;

   if (!inputdev_attach_mask)
      return 0;

   /* Port 1 only -- InputDevSetType() guarantees no other port can hold
    * a gun, so there is nothing to search. */
   p = &inputdev_ports[0];

   if (p->type != INPUTDEV_LIGHTGUN || p->gun_offscreen)
      return 0;

   if (col)
      *col = p->gun_col;
   if (row)
      *row = p->gun_row;

   return 1;
}

void InputDevArm(void)
{
   if (!inputdev_attach_mask)
      return;
   inputdev_armed = 1;
}

void InputDevRowSelect(uint8_t row0, uint8_t row1)
{
   uint8_t  row[2];
   unsigned p;

   if (!inputdev_attach_mask)
      return;

   row[0] = row0;
   row[1] = row1;

   for (p = 0; p < 2; p++)
   {
      InputDevPort *dp = &inputdev_ports[p];

      if (!inputdev_is_analog(dp->type) || !dp->an_engaged)
         continue;

      /* TR10: "Bank switching is done automatically when the controller
       * sees a transition from row 3 to row 0 (of the same controller
       * socket)", and interleaved reads of OTHER sockets "must not cause
       * the controller to lose synchronisation or perform any bank
       * switching" -- TR10's own driver recipe reads a bank from one
       * controller, banks from others, then comes back.  While another
       * socket is addressed (or the output enable is clear), OUR lines
       * sit at the pulled-up no-row pattern; the microcontroller treats
       * that as idle and REMEMBERS the last row it decoded, so a
       * row-3 ... idle ... row-0 sequence still switches.  Hence a
       * non-row code is skipped, not recorded.  Only two banks exist on
       * this device, so the cycle is a toggle. */
      if (row[p] > 3)
         continue;

      if (dp->an_row == 3 && row[p] == 0)
         dp->an_bank ^= 1;

      dp->an_row = row[p];
   }
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

/* The nibble an engaged analog controller drives on its four J lines for
 * the current row and bank, as logic levels (bit i of the return = line
 * J+i).  TR10 Bank 0/Bank 1 tables, restated in inputdev.h. */
static uint8_t inputdev_analog_nibble(const InputDevPort *p, uint8_t row)
{
   if (p->an_bank == 0)
   {
      switch (row)
      {
         case 0:  return (uint8_t)(p->an_x & 0x0F);        /* X0..X3 */
         case 1:  return (uint8_t)((p->an_x >> 4) & 0x0F); /* X4..X7 */
         case 2:  return (uint8_t)(p->an_y & 0x0F);        /* Y0..Y3 */
         default: return (uint8_t)((p->an_y >> 4) & 0x0F); /* Y4..Y7 */
      }
   }

   /* Bank 1: rows 1-3 read all 1s; row 0 carries the hat / gear shift
    * switches, active low (pressed pulls the line to 0). */
   if (row != 0)
      return 0x0F;

   return (uint8_t)(0x0F
                    & ~((p->an_sw & INPUTDEV_SW_UP    ? 1u : 0u)
                       | (p->an_sw & INPUTDEV_SW_DOWN  ? 2u : 0u)
                       | (p->an_sw & INPUTDEV_SW_LEFT  ? 4u : 0u)
                       | (p->an_sw & INPUTDEV_SW_RIGHT ? 8u : 0u)));
}

uint16_t InputDevOverlayF14000(uint16_t data, uint8_t row0, uint8_t row1)
{
   const MouseWiring *w;
   InputDevPort *p;
   uint8_t  row[2];
   unsigned q;
   int ax, bx, ay, by;

   if (!inputdev_attach_mask)
      return data;

   row[0] = row0;
   row[1] = row1;

   /* Analog / driving controller (#437): drives its port's four J lines
    * ($F14000 bits 8-11 for port 1, 12-15 for port 2) whenever its own
    * row select is asserted.  The matrix contributes nothing there (an
    * engaged analog port's pad slots are suppressed by update_input, and
    * the bus default is all-high), so clearing the zero-level lines is a
    * complete drive, same AND-only shape as the mouse overlay below. */
   for (q = 0; q < 2; q++)
   {
      InputDevPort *ap = &inputdev_ports[q];
      uint8_t nib;

      if (!inputdev_is_analog(ap->type) || !ap->an_engaged)
         continue;
      if (row[q] > 3)
         continue;

      nib = inputdev_analog_nibble(ap, row[q]);
      data &= (uint16_t)~(((uint16_t)(~nib & 0x0F)) << (8 + 4 * q));
   }

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

   /* Analog / driving controller (#437): the B and C columns.
    *
    * Port 1's columns are B0 (bit 0, the C column) and B1 (bit 1, the
    * button column); port 2's are B2 (bit 2) and B3 (bit 3).
    *
    * C column: in row 0 it is the BANK FLAG -- "It will always be zero in
    * Bank 0, while all other banks will return 1" (TR10, Reading Bank
    * Switching Controllers).  In rows 1-3 it carries C1/C2/C3, whose
    * row assignment differs by port (the same port-headed TR10 tables the
    * rotary ID above uses): port 1 reads C1/C2/C3 in rows 1/2/3, port 2
    * reads C1/C3/C2 in rows 1/2/3.  The values are fixed -- C1 = 1,
    * C2 = 0, C3 = 1, i.e. C2 C3 = 0 1 = "Bank Switching" -- so the only
    * bit to clear is C2: row 2 on port 1, row 3 on port 2.
    *
    * B column: bank 0 carries buttons A/B/C/D in rows 0-3, active low;
    * bank 1 reads 1 in every row, which is the "1111 = Analogue Joystick
    * or Driving Controller" type identifier among the bank-switching
    * types.  All-high needs no clearing. */
   {
      unsigned q;
      uint8_t  arow[2];

      arow[0] = row0;
      arow[1] = row1;

      for (q = 0; q < 2; q++)
      {
         InputDevPort *ap    = &inputdev_ports[q];
         unsigned      c_bit = 0 + 2 * q;
         unsigned      b_bit = 1 + 2 * q;
         uint8_t       r     = arow[q];

         if (!inputdev_is_analog(ap->type) || !ap->an_engaged)
            continue;
         if (r > 3)
            continue;

         if (r == 0)
         {
            if (ap->an_bank == 0)
               data &= (uint16_t)~(1u << c_bit);
         }
         else if (r == (q == 0 ? 2u : 3u))
            data &= (uint16_t)~(1u << c_bit);   /* C2 = 0 */

         if (ap->an_bank == 0)
         {
            static const uint8_t sw_of_row[4] = {
               INPUTDEV_SW_A, INPUTDEV_SW_B, INPUTDEV_SW_C, INPUTDEV_SW_D
            };
            if (ap->an_sw & sw_of_row[r])
               data &= (uint16_t)~(1u << b_bit);
         }
      }
   }

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

   /* Analog controller (#437), appended so the pre-analog field order is
    * untouched.  Machine-visible only: bank, last row, latched ADC bytes
    * and switch mask; engagement is host-routing-derived and stays out
    * (inputdev.h). */
   for (p = 0; p < 2; p++)
   {
      STATE_SAVE_VAR(buf, inputdev_ports[p].an_bank);
      STATE_SAVE_VAR(buf, inputdev_ports[p].an_row);
      STATE_SAVE_VAR(buf, inputdev_ports[p].an_x);
      STATE_SAVE_VAR(buf, inputdev_ports[p].an_y);
      STATE_SAVE_VAR(buf, inputdev_ports[p].an_sw);
   }

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

   /* Analog controller (#437).  A state is untrusted input: the bank has
    * exactly two values and the row is 0-3 or the none-yet sentinel, so
    * anything else is coerced.  The ADC bytes and the switch mask are
    * full-range by construction.  A pre-extension v12 develop state
    * reads the zero-fill tail here (see inputdev.h) -- bank 0, row 0,
    * zeroed latches -- inert unless a device is selected and engaged. */
   for (p = 0; p < 2; p++)
   {
      STATE_LOAD_VAR(buf, inputdev_ports[p].an_bank);
      STATE_LOAD_VAR(buf, inputdev_ports[p].an_row);
      STATE_LOAD_VAR(buf, inputdev_ports[p].an_x);
      STATE_LOAD_VAR(buf, inputdev_ports[p].an_y);
      STATE_LOAD_VAR(buf, inputdev_ports[p].an_sw);
      inputdev_ports[p].an_bank &= 1;
      if (inputdev_ports[p].an_row > 3)
         inputdev_ports[p].an_row = 0xFF;
   }

   return (size_t)(buf - start);
}
