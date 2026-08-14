/*
 * src/jerry/inputdev.h -- per-port non-pad input device model.
 *
 * THE LOAD-BEARING FACT: THE MOUSE ADAPTER IS ROW-BLIND
 * ====================================================
 * docs/jaguar-mouse-adapter-mapping.md section 7.  Neither the ST nor the
 * Amiga adapter connects Jaguar connector pins 1-4 -- and those are exactly
 * J4..J7, the port-2 row-select OUTPUTS.  The adapter cannot see the row
 * select at all.  All six port-2 input lines (J12, J13, J14, J15, B2, B3)
 * therefore carry the mouse's state continuously and IDENTICALLY in rows
 * 0, 1, 2 and 3.
 *
 * A Jaguar pad is a matrix.  A mouse adapter is not.  So the mouse cannot
 * be expressed as joypadNButtons[] slots: doing that would return the
 * mouse in row 0 and nothing in rows 1-3, where real hardware returns the
 * same bits.  The device instead drives the raw $F14000 / $F14002 bits
 * through a row-independent overlay applied after the matrix decode.
 *
 * Three consequences we deliberately reproduce rather than "fix":
 *   1. In rows 1-3 the direction bits read as keypad presses (*, 7, 4, 1 /
 *      2, 5, 8, 0 / 3, 6, 9, #).  A title doing a full four-row scan while
 *      the mouse moves sees phantom digits.  That is hardware.
 *   2. LMB reads as A and B and C and Option, because $F14002 bit 3
 *      decodes per-row in joystick.c.
 *   3. RMB holds $F14002 bit 2 low in rows 1-3, which is the C1/C2/C3
 *      controller-type field, so a title re-probing controller type while
 *      the right button is held misidentifies the device.
 *
 * PORT SCOPE
 * ==========
 * The mouse is a PORT 2 device only.  The adapter is vendor-documented as
 * such, every title known to read one reads port 2, and a port-1 mouse
 * would be a configuration no software supports.  InputDevSetType()
 * refuses a mouse on port 0.
 *
 * `port` here is 0 for Jaguar port 1 and 1 for Jaguar port 2, matching
 * joypad0Buttons / joypad1Buttons.
 */

#ifndef __INPUTDEV_H__
#define __INPUTDEV_H__

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Wiring cases are docs/jaguar-mouse-adapter-mapping.md section 4d,
 * verbatim.  Values are persisted only in core-option strings, never in
 * the savestate, so they may be renumbered. */
typedef enum
{
   INPUTDEV_PAD = 0,             /* standard Jaguar joypad -- today's behaviour */
   INPUTDEV_MOUSE_ST,            /* case 1: ST adapter + ST mouse (also PS/2)   */
   INPUTDEV_MOUSE_AMIGA_ADAPTER, /* case 2: dedicated Amiga adapter + Amiga     */
   INPUTDEV_MOUSE_AMIGA_ON_ST,   /* case 3: Amiga mouse in an ST-wired adapter  */
   INPUTDEV_ROTARY               /* TR10 "Tempest" rotary -- RESERVED, see below */
} InputDevType;

/* INPUTDEV_ROTARY is declared but NOT implemented here.  It belongs to
 * issue #436, which lands as its own PR on top of this one; the constant
 * is reserved now only so that PR is a pure addition rather than a
 * renumbering.  Nothing in this build can select it: no core-option value
 * maps to it, and InputDevSetType() treats it as "not attached". */

void InputDevInit(void);
void InputDevReset(void);     /* JERRYReset path: dynamic state only        */
void InputDevShutdown(void);  /* retro_deinit: clear every static (iOS)     */

/* Selection.  port is 0 (Jaguar port 1) or 1 (Jaguar port 2). */
void         InputDevSetType(int port, InputDevType type);
InputDevType InputDevGetType(int port);
int          InputDevAnyAttached(void);  /* 0 => bit-identical to pad-only */

/* Per-frame host feed, called from update_input() in libretro.c.  dx/dy
 * are libretro relative units; buttons is a bitmask of INPUTDEV_BTN_*. */
#define INPUTDEV_BTN_LEFT   0x01
#define INPUTDEV_BTN_RIGHT  0x02
void InputDevFeed(int port, int32_t dx, int32_t dy, uint32_t buttons);

/* Sensitivity, Q8 (256 == 1.0), from the core options. */
void InputDevSetScale(int port, int32_t scale_q8);

/* ---- bus-side hooks, called only from joystick.c ------------------- *
 *
 * THE EMISSION RULE: arm on write, advance at most one state on the next
 * offset-0 read.
 *
 * The naive "advance once per offset-0 read" has a latent motion-loss
 * bug.  A driver that writes the row select once and then reads twice per
 * poll (one read for directions, one for buttons -- a very common shape)
 * would advance the phase twice between decode steps, producing exactly
 * the two-state jump a quadrature decoder discards.  That reads as
 * jitter, which is worse than lag.
 *
 * Write-then-read (every documented driver) yields exactly one advance
 * per poll.  Extra reads without an intervening write return the same
 * phase, which the decoder reads as "no movement" -- harmless -- instead
 * of a discarded diagonal.
 *
 * Deliberately deferred: a driver that writes the row select once at init
 * and then polls read-only forever would see a frozen device.  No such
 * driver is documented.  The mitigation, if a real title needs it, is a
 * stall-breaker that advances anyway after N consecutive unarmed reads;
 * it is not implemented speculatively. */
void     InputDevArm(void);                        /* $F14000 write          */
void     InputDevClock(void);                      /* $F14000 read, offset 0 */
uint16_t InputDevOverlayF14000(uint16_t data);
/* row0/row1 are the decoded socket-0 row (0-3) for each port, or 0xFF
 * when that port's nibble is not a socket-0 code.  The mouse ignores them
 * -- it is row-blind -- but the rotary's C2/C3 controller-type reporting
 * (#436) needs them, so the signature is final and joystick.c will not
 * have to change again. */
uint16_t InputDevOverlayF14002(uint16_t data, uint8_t row0, uint8_t row1);

/* ---- savestate (v12 trailing chunk) -------------------------------- *
 * The quadrature phase IS what the game reads at $F14000, and the
 * backlog/carry determine every future read, so all three are
 * machine-visible and must survive a rollback (issue #400's lesson).
 * The device type and the sensitivity scale are deliberately NOT saved:
 * both are option-derived and constant for a session, and restoring them
 * from a state would let a stale state fight the user's current option. */
#define INPUTDEV_STATE_SIZE 41
size_t InputDevStateSave(uint8_t *buf);
size_t InputDevStateLoad(const uint8_t *buf);

#ifdef __cplusplus
}
#endif

#endif /* __INPUTDEV_H__ */
