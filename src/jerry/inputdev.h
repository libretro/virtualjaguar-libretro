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
 * THE ANALOG / DRIVING CONTROLLER IS A BANK-SWITCHING MATRIX DEVICE (#437)
 * ========================================================================
 * TR10 sections "Analogue Joystick and 'Driving' Controllers",
 * "Identifying Controller Types" and "Reading Bank Switching Controllers"
 * are the complete wire spec, and the first surprise is that there is no
 * console-side ADC in it AT ALL: "Early versions of the Jaguar included
 * an 8 bit ADC on the motherboard.  This has been deleted -- analogue
 * controllers now require their own ADC chip."  The controller carries a
 * microcontroller (Atari's prototype used a 68HC05P9) that digitises the
 * pots itself and answers the ordinary $F14000/$F14002 row scan with
 * data bits instead of switch closures.  So this device, like the rotary,
 * is row-select-driven matrix synthesis -- no new bus plumbing.
 *
 * The layout, per TR10's Bank 0 / Bank 1 tables (port 1 columns are
 * B0/B1/J8..J11, port 2 columns B2/B3/J12..J15; J8..J15 are $F14000 bits
 * 8..15, B0..B3 are $F14002 bits 0..3):
 *
 *   Bank 0   C column     B column   J+0  J+1  J+2  J+3
 *   Row 0    0 (bank id)  button A   X0   X1   X2   X3
 *   Row 1    C1 = 1       button B   X4   X5   X6   X7
 *   Row 2    (C table)    button C   Y0   Y1   Y2   Y3
 *   Row 3    (C table)    button D   Y4   Y5   Y6   Y7
 *
 *   Bank 1   C column     B column   J+0  J+1   J+2   J+3
 *   Row 0    1 (bank id)  1          Up   Down  Left  Right
 *   Rows 1-3 (C table)    1          1    1     1     1
 *
 * X/Y are the 8-bit ADC values presented as logic levels (NOT active-low
 * switches); the buttons and the hat ARE switches and read active low.
 * C2 C3 = 0 1 identifies "Bank Switching" (see the rotary's C-row table
 * below for which row carries which C bit on which port), and the B-column
 * bits of the last bank -- all 1s across rows 3..0 -- identify "Analogue
 * Joystick or Driving Controller" among the bank-switching types.
 *
 * THE BANK SWITCHES ON THE ROW SELECT, NOT ON READS: "Bank switching is
 * done automatically when the controller sees a transition from row 3 to
 * row 0 (of the same controller socket)."  The row select is a latched
 * output the 68K WRITES, so the device clocks its bank from
 * InputDevRowSelect() (called on every $F14000 write), not from
 * InputDevClock().  A nibble that is not a socket-0 row code (or a write
 * with the output enable bit clear) breaks the row-3 -> row-0 adjacency,
 * exactly as the physical lines leaving the row-code space would.
 *
 * TWO DELIBERATE SIMPLIFICATIONS, both supersets of the spec:
 *   1. TR10 requires ~25us of settling per row and ~300us per bank
 *      change, and tells games to delay before reading.  The emulated
 *      controller answers instantly; a compliant driver's delay loop just
 *      spins over valid data.
 *   2. Real controllers have arbitrary centres and ranges (TR10's example
 *      driving controller reads 160 centred / 245 hard right / 75 hard
 *      left, drifting with temperature) and games MUST calibrate.  The
 *      emulated device is ideal -- 128 centred, symmetric -- which every
 *      calibration routine handles trivially.
 *
 * NO RELEASED TITLE READS THIS PROTOCOL (research for #437: Atari never
 *  shipped the controller; the one known consumer of console-side analog
 * input, JANALOG.ABS, targets the deleted early-board ADC instead).  The
 * device exists for homebrew and for parity with BigPEmu's analog /
 * driving types, and its verification is therefore the synthetic
 * register-level suite in test/tools/analog_decode_test.c, clearly
 * labelled as such.
 *
 * ANALOG vs DRIVING: identical on the wire -- TR10 defines ONE protocol
 * and two interpretations (stick: X=roll +right, Y=pitch +forward;
 * driving: X=steering +right, Y=accelerator +/brake -).  The two
 * InputDevTypes differ only in how libretro.c sources host values.
 *
 * ENGAGEMENT (the liveness guardrail, core side).  A configured analog
 * device must not steal the port from a pad the frontend is actually
 * routing (same rule as the mouse's inputdev_live).  Until the first
 * InputDevFeedAnalog() call the device drives NOTHING -- overlays and
 * bank clock inert, the port bit-identical to a pad -- and libretro.c
 * only starts feeding once the stick has proven live.  Consequence worth
 * stating: a title that probes controller types once at boot sees a
 * standard pad unless the stick moves first; deflect the stick during
 * boot (or before the title's controller menu) to be identified.  Like
 * inputdev_live, engagement is host-routing-derived and NOT serialized.
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
   INPUTDEV_ROTARY,              /* TR10 "Tempest" rotary (#436)                */
   INPUTDEV_ANALOG,              /* TR10 bank-switching analogue stick (#437)   */
   INPUTDEV_DRIVING,             /* same wire protocol, driving skin (#437)     */
   INPUTDEV_LIGHTGUN             /* TR10 port-1 light gun (#438)                */
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

/* Analog / driving feed (#437).  x and y are ABSOLUTE positions in the
 * libretro int16 convention (-32768..32767, +x right, +y DOWN); the
 * device applies the per-axis tuning in host orientation and THEN flips Y
 * to TR10's +forward/+accelerator -- same offset-before-inversion order
 * the relative path uses, and for the same #474 reason.  `switches` is a
 * mask of INPUTDEV_SW_*; the hat doubles as the driving controller's
 * gear shift (Up/Down) and Spare 1/2 per TR10.  First call marks the
 * device ENGAGED (see the header comment). */
#define INPUTDEV_SW_A      0x01
#define INPUTDEV_SW_B      0x02
#define INPUTDEV_SW_C      0x04
#define INPUTDEV_SW_D      0x08
#define INPUTDEV_SW_UP     0x10
#define INPUTDEV_SW_DOWN   0x20
#define INPUTDEV_SW_LEFT   0x40
#define INPUTDEV_SW_RIGHT  0x80
void InputDevFeedAnalog(int port, int32_t x, int32_t y, uint32_t switches);

/* Sensitivity, Q8 (256 == 1.0), from the core options. */
void InputDevSetScale(int port, int32_t scale_q8);

/* Per-axis tuning -- dead zone, offset, response exponent (#439).
 *
 * ONE LAYER FOR EVERY ANALOG SOURCE.  Both shipped analog devices reach
 * the machine through InputDevFeed(), so the tuning is applied there and
 * nowhere else; #437's analog/driving axes will land on the same call.
 * The semantics, the reference magnitude for the exponent and the reason
 * the curve arrives as a gain rather than a modified delta are all in
 * axistune.h -- read that before changing anything here.
 *
 * `axis` is INPUTDEV_AXIS_X or INPUTDEV_AXIS_Y.  A rotary is a single
 * wheel and uses X only; its Y tune is stored but never consulted.
 *
 * THE PAD IS NOT TOUCHED BY ANY OF THIS.  A port whose type is
 * INPUTDEV_PAD never enters InputDevFeed's tuned paths at all -- the
 * standard digital pad's bits come from joypadNButtons[] via joystick.c
 * and this file contributes nothing to them.  Pinned by
 * test/tools/joymatrix_identity's unchanged digests and by
 * test/tools/tuning_identity.
 *
 * Values are clamped in AxisTuneSet(), because they originate in
 * core-option strings a user can hand-edit.  Defaults (0 / 0 / 256) are
 * the exact identity, so a user who never opens the menu gets the
 * pre-#439 behaviour byte for byte. */
#define INPUTDEV_AXIS_X 0
#define INPUTDEV_AXIS_Y 1
void InputDevSetTune(int port, int axis, int32_t deadzone, int32_t offset,
                     int32_t exponent_q8);

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

/* ---- light gun (#438) ---------------------------------------------- *
 *
 * PORT 1 ONLY, and that is silicon, not policy: TR10's pin-out gives port
 * 1 pin 6 as "Button input / Light Gun on Port 1" while port 2's pin 6 is
 * plain B2.  InputDevSetType() refuses a gun on port 2, the mirror of the
 * mouse's port-1 refusal.
 *
 * THE GUN IS NOT A MATRIX DEVICE AND NOT AN OVERLAY.  It contributes
 * nothing to $F14000/$F14002 by itself.  What it drives is TOM's LP pin,
 * whose rising edge latches LPH ($F00008) and LPV ($F0000A) -- registers
 * that live in tomRam8 and are synthesized in src/tom/tom.c, not here.
 * This file owns only the AIM POINT and the gun's buttons; tom.c owns the
 * geometry that turns an aim point into a pair of counter values.
 *
 * THE LATCH IS CONTINUOUS, NOT TRIGGER-GATED.  A real gun's photodiode
 * pulses LP every field the beam crosses the aim point, whether or not
 * the trigger is held -- there is no "LP was pulsed" status bit anywhere
 * in the register map, so software cannot detect a shot from LPH/LPV at
 * all.  Balloons (the one confirmed title) proves it: it reads LPH/LPV
 * unconditionally after every VBL and draws a live crosshair from them,
 * and reads the trigger from an ORDINARY PAD BUTTON.  A trigger-edge-only
 * latch would freeze that crosshair.
 *
 * WHICH pad button: Balloons polls all four rows into a bitmask and tests
 * bit 25, which decodes to row 1 / JOYBUTS bit 1 -- BUTTON_B (verified by
 * simulating joystick.c against the ROM's own scan loop at $4A42).  So
 * TRIGGER drives BUTTON_B.  The remaining libretro gun buttons are mapped
 * to the other switches a gun-equipped controller physically has, so a
 * title that wants a different one is still reachable.
 *
 * `col`/`row` are NATIVE framebuffer pixels -- the internal-resolution
 * factor must already be divided out by the caller (libretro.c), because
 * the enhancement path must not be able to move the aim point (#400). */
#define INPUTDEV_GUN_TRIGGER  0x01
#define INPUTDEV_GUN_AUX_A    0x02
#define INPUTDEV_GUN_AUX_B    0x04
#define INPUTDEV_GUN_START    0x08
#define INPUTDEV_GUN_SELECT   0x10

void InputDevFeedLightgun(int port, int32_t col, int32_t row,
                          int offscreen, uint32_t buttons);

/* Current aim point, for the LPH/LPV synthesis in src/tom/tom.c.  Returns
 * 0, and leaves the outputs untouched, when no gun is attached or the
 * shot is off-screen, which is the "photodiode sees no light, so LP never
 * pulses and the registers keep their last value" case. */
int InputDevLightgunAim(int32_t *col, int32_t *row);

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
/* Row-select change notification (#437), called from JoystickWriteWord on
 * every offset-0 write with the DECODED socket-0 row per port (0-3, or
 * 0xFF when that nibble is not a socket-0 code or the output enable bit
 * is clear).  This is the analog controller's bank clock -- TR10: banks
 * switch on the row-3 -> row-0 transition of the device's own socket --
 * and a no-op for every other device type. */
void     InputDevRowSelect(uint8_t row0, uint8_t row1);
void     InputDevClock(uint8_t row0, uint8_t row1);/* $F14000 read, offset 0 */
/* row0/row1: decoded socket-0 row per port, as in InputDevOverlayF14002.
 * The mouse ignores them (row-blind); the analog controller (#437) needs
 * them because its J-line nibble is a different slice of X/Y per row. */
uint16_t InputDevOverlayF14000(uint16_t data, uint8_t row0, uint8_t row1);
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
 * The per-axis tuning (#439) is NOT saved either, for the same reason as
 * the scale: it is option-derived and constant for a session, and it
 * changes only how host motion is turned into feeds, never any state the
 * machine can observe.  The chunk does not grow and STATE_VERSION does not
 * move.
 *
 * The rotary (#436) adds NOTHING here and the chunk does not grow.  It
 * uses the port's existing X axis, so its backlog/carry/phase are already
 * in this chunk; its published phase lives in joypadNButtons[], which
 * JoystickStateSave() already serializes; and the controller-type ID flag
 * is option-derived like the scale.  Hence no STATE_VERSION bump beyond
 * the v12 this chunk already introduced.
 *
 * The analog controller (#437) DOES grow the chunk, by 5 bytes per port:
 * bank, last observed row, the latched X/Y ADC bytes and the switch mask
 * are all machine-visible (the bank decides which table a read decodes
 * from, and TR10's own driver recipe -- "read all banks into a table,
 * find bank 0 by the flag bit" -- depends on it surviving a rollback).
 * v12 is still develop-only (v3.3.0 shipped v11), so per the
 * one-bump-per-release policy the v12 layout is extended IN PLACE rather
 * than minting a v13; a pre-extension develop state loads these ten
 * bytes from the zero-fill tail -- bank 0, row 0, zeroed latches --
 * which is inert unless an analog device is both selected and engaged,
 * and the first frame's feed overwrites the latches anyway.  Engagement
 * itself is host-routing-derived and deliberately NOT saved, same
 * argument as libretro.c's inputdev_live. */
#define INPUTDEV_STATE_SIZE 51
size_t InputDevStateSave(uint8_t *buf);
size_t InputDevStateLoad(const uint8_t *buf);

#ifdef __cplusplus
}
#endif

#endif /* __INPUTDEV_H__ */
