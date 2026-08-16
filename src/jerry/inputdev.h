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
 * THE ROTARY IS THE OPPOSITE SHAPE: IT IS A MATRIX DEVICE
 * =======================================================
 * Jaguar Technical Reference V10, section "Rotary 'Tempest' Controller":
 * "all existing rotary controllers are modified standard controllers,
 * consequently they should be read just like a standard controller using
 * Socket 0 row codes".  Rows 1-3 are an ordinary pad; only row 0 changes,
 * where Up/Down disappear and Left/Right become Phase 0 / Phase 1.
 *
 * Independently corroborated by the wiring: the encoder's common wire goes
 * to joystick port pin 4 -- the ROW STROBE -- and not to ground.  Port-1
 * pin 4 is J0, and row 0 is the only row code that drives J0 low
 * (J3 J2 J1 J0 = 1110), so the phases appear in row 0 and only row 0.
 *
 * So the rotary reuses joypadNButtons[] unchanged and must NOT borrow the
 * mouse's row-blind overlay.  Two peripherals, one encoder, two entirely
 * different assertion mechanisms.
 *
 * PORT SCOPE
 * ==========
 * The mouse is a PORT 2 device only.  The adapter is vendor-documented as
 * such, every title known to read one reads port 2, and a port-1 mouse
 * would be a configuration no software supports.  InputDevSetType()
 * refuses a mouse on port 0.
 *
 * The rotary is offered on BOTH ports: TR10 documents the rotary matrix
 * for both, and Tempest 2000's hidden CONTROLLER TYPE menu carries
 * independent P1 and P2 toggles.
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
   INPUTDEV_ROTARY               /* TR10 "Tempest" rotary (#436)                */
} InputDevType;

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

/* Whether a rotary on this port identifies itself to software as a rotary
 * (diode D23 fitted -- TR10 supplemental).  TR10 "Identifying Controller
 * Types": C2 C3 = 1 0 means "Tempest Rotary", 1 1 means "Standard Jaguar
 * Joypad (or nothing connected)", so a rotary that reports itself must
 * read C3 = 0.
 *
 * The C2/C3 rows DIFFER BY PORT (from TR10's port-headed tables; the
 * "Standard Jaguar Controller Matrix" table reads "C1" for both Row 2 and
 * Row 1, which is an OCR slip):
 *
 *            Row 3   Row 2   Row 1
 *   port 1   C3      C2      C1     ($F14002 bit 0)
 *   port 2   C2      C3      C1     ($F14002 bit 2)
 *
 * so "C3 = 0" is bit 0 in row 3 on port 1, and bit 2 in row 2 on port 2.
 *
 * Default OFF, and deliberately not load-bearing: TR10's supplemental
 * section says most real rotaries shipped WITHOUT diode D23 and therefore
 * identify as a standard joypad, and Tempest 2000 does not consult these
 * bits at all -- it uses its own hidden CONTROLLER TYPE menu. */
void InputDevSetRotaryID(int port, int reports_rotary);

/* ---- bus-side hooks, called only from joystick.c ------------------- *
 *
 * THE EMISSION RULE: arm on write, advance at most one state on the next
 * offset-0 read THAT THE DEVICE'S OWN POLLER COULD HAVE DECODED FROM.
 *
 * For a MOUSE that means a read taken with its port's row select asserted
 * -- any of rows 0-3, not row 0 specifically, because the adapter is
 * row-blind and every row is a poll of it.  Without that gate a title that
 * polls port 1 rapidly would advance the port-2 encoder between the port-2
 * poller's own samples, which is precisely the lost motion the one-state
 * ceiling exists to prevent.
 *
 * For a ROTARY it means a row-0 read of its own port -- see "WHY THE
 * ROTARY ALSO NEEDS THE ROW" below.
 *
 * The gate lives HERE, not at the call site.  It used to be joystick.c's
 * `offset1 != 0xFF` test, which was equivalent while the mouse was the
 * only device and it was port-2-only; a port-1 rotary has to advance on
 * reads that test rejects, so joystick.c now calls unconditionally and
 * passes the decoded rows.
 *
 * ARM CONSUMPTION FOLLOWS THE SAME GATE.  The arm flag is cleared only
 * when some attached device actually sampled -- not on every offset-0
 * read.  Clearing unconditionally would let a port-1-only read eat the arm
 * that a port-2 mouse's next read was owed, changing mouse advance timing
 * (which test/tools/joymatrix_identity.c's mouse digest measures).
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
 * driver is documented, and Tempest 2000 is NOT one -- measured, see
 * below.  The mitigation, if a real title ever needs it, is a
 * stall-breaker that advances anyway after N consecutive unarmed reads;
 * it is not implemented speculatively.
 *
 * WHY THE ROTARY ALSO NEEDS THE ROW (#436)
 * ========================================
 * "One state per armed read" is the right rate limit for the mouse
 * because the row-blind adapter is sampled by every read of its port.  It
 * is the WRONG limit for the rotary, because a rotary's phases exist in
 * row 0 only: the sample a rotary driver actually decodes is one row-0
 * read, and a four-row scan issues three more armed reads that decode
 * nothing.
 *
 * Measured on Tempest 2000 (vjtrace --watch 0xF14000:4:rw, 400 frames):
 * the poll loop at $80FD2E..$80FDEE writes eight row codes per frame --
 * $81FE/$81FD/$81FB/$81F7 (port 1 rows 0-3) and $817F/$81BF/$81DF/$81EF
 * (port 2 rows 0-3) -- each immediately followed by a move.l, i.e. eight
 * armed offset-0 reads per frame but exactly ONE row-0 sample per port.
 *
 * Draining eight states between consecutive row-0 samples makes the
 * decoded result (states drained) mod 4, so the device works only at
 * exactly one count per frame: two decodes as "undetermined", THREE
 * DECODES AS -1 (a sign inversion), four and eight decode as zero.  That
 * is precisely the jitter the emission rule exists to prevent, and no
 * sensitivity setting can compensate for it.
 *
 * So InputDevClock() takes the decoded socket-0 row for each port (0-3,
 * or 0xFF when that port's nibble is not a socket-0 code) and a rotary
 * advances only on an armed row-0 read of its own port.  The mouse uses
 * the same arguments only to tell "my port was addressed" from "it was
 * not", exactly as its old call-site gate did.
 *
 * The row is NOT derivable inside this file: joystick_ram is private to
 * joystick.c, and the row readback bits in the assembled $F14000 word
 * cannot distinguish "port 1 row 0" from "port 1 not addressed" (that
 * port's row-0 mask is 0xFFFF, i.e. it clears nothing). */
void     InputDevArm(void);                        /* $F14000 write          */
void     InputDevClock(uint8_t row0, uint8_t row1);/* $F14000 read, offset 0 */
uint16_t InputDevOverlayF14000(uint16_t data);
/* row0/row1 are the decoded socket-0 row (0-3) for each port, or 0xFF
 * when that port's nibble is not a socket-0 code.  The mouse ignores them
 * -- it is row-blind -- but the rotary's C2/C3 controller-type reporting
 * (#436) needs them. */
uint16_t InputDevOverlayF14002(uint16_t data, uint8_t row0, uint8_t row1);

/* ---- savestate (v12 trailing chunk) -------------------------------- *
 * The quadrature phase IS what the game reads at $F14000, and the
 * backlog/carry determine every future read, so all three are
 * machine-visible and must survive a rollback (issue #400's lesson).
 * The device type and the sensitivity scale are deliberately NOT saved:
 * both are option-derived and constant for a session, and restoring them
 * from a state would let a stale state fight the user's current option.
 *
 * The rotary (#436) adds NOTHING here and the chunk does not grow.  It
 * uses the port's existing X axis, so its backlog/carry/phase are already
 * in this chunk; its published phase lives in joypadNButtons[], which
 * JoystickStateSave() already serializes; and the controller-type ID flag
 * is option-derived like the scale.  Hence no STATE_VERSION bump beyond
 * the v12 this chunk already introduced. */
#define INPUTDEV_STATE_SIZE 41
size_t InputDevStateSave(uint8_t *buf);
size_t InputDevStateLoad(const uint8_t *buf);

#ifdef __cplusplus
}
#endif

#endif /* __INPUTDEV_H__ */
