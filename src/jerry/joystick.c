//
// Joystick handler
//
// by cal2
// GCC/SDL port by Niels Wagenaar (Linux/WIN32) and Caz (BeOS)
// Extensive rewrite by James Hammons
// (C) 2013 Underground Software
//
// JLH = James Hammons <jlhamm@acm.org>
//
// Who  When        What
// ---  ----------  -------------------------------------------------------------
// JLH  01/16/2010  Created this log ;-)
//

#include <string.h>			// For memset()
#include "joystick.h"
#include "inputdev.h"
#include "settings.h"

// Global vars

/* Row-code decode: low/high nibble of the JOYSTICK register's row-select
 * byte -> (socket, row).  File-scope so the write path can decode the row
 * for InputDevRowSelect() (#437) with the same tables the read path uses.
 *
 * SOURCE: TR10 p.18, "4-Player Adaptor (Team Tap)" (#513), read as a
 * rendered page image rather than through pdftotext's column order,
 * corroborated against the Atari original (04 - Technical Reference,
 * (C) 1995, p.17), which carries the identical table.  For a PORT 1
 * row-select nibble:
 *
 *   $0 $1 $2 $3  socket 1, rows 0 1 2 3    $7 $B $D $E  socket 0, rows 3 2 1 0
 *   $4 $5 $6 $8  socket 2, rows 0 1 2 3    $9 $A $C $F  socket 3, rows 0 1 2 3
 *
 * The socket-0 codes are scattered rather than contiguous because they
 * are the pre-existing single-controller codes; the other twelve were
 * fitted around them.
 *
 * Port 2's codes are "a mirror image of the codes for port 1 (the bit
 * order is reversed)", so teamTapSocket1[n] == teamTapSocket0[bitrev4(n)].
 * Written out rather than computed, matching the tables these replace.
 *
 * THESE REPLACE joypad0Offset[16] / joypad1Offset[16], which returned a
 * socket-0 base offset (row * 4) or 0xFF for the other twelve codes.
 * Their four non-0xFF entries sat at exactly the four socket-0 nibbles
 * above, at exactly the same rows -- port 1 $E $D $B $7 -> rows 0 1 2 3,
 * port 2 $7 $B $D $E -> rows 0 1 2 3.  The shipped code and the manual
 * agreed without either having been derived from the other, which is the
 * cross-check on the socket-0 column; the twelve 0xFF sentinels were
 * precisely the twelve non-socket-0 codes.
 *
 * Independent confirmation of the two entries that carry the detection
 * probe: the Joypad-TeamTap Tester (Domin, 2000) writes $81FA -- port-1
 * nibble $A -- then tests JOYBUTS bit 0, and $815F -- port-2 nibble $5 --
 * then tests bit 2.  Both tables put those two codes at socket 3, row 1,
 * which is where TR10's appendix puts the detect diode (D21, at the C1
 * matrix position, fitted only to socket 3; C1 is on row 1). */
/*                                          $0 $1 $2 $3 $4 $5 $6 $7 $8 $9 $A $B $C $D $E $F */
static const uint8_t teamTapSocket0[16] = {  1, 1, 1, 1, 2, 2, 2, 0, 2, 3, 3, 0, 3, 0, 0, 3 };
static const uint8_t teamTapRow0[16]    = {  0, 1, 2, 3, 0, 1, 2, 3, 3, 0, 1, 2, 2, 1, 0, 3 };
static const uint8_t teamTapSocket1[16] = {  1, 2, 2, 3, 1, 3, 2, 0, 1, 3, 2, 0, 1, 0, 0, 3 };
static const uint8_t teamTapRow1[16]    = {  0, 3, 0, 2, 2, 1, 2, 0, 1, 0, 1, 1, 3, 2, 3, 3 };

/* Is a Team Tap plugged into this port?  Config, not machine state -- see
 * joystick.h. */
static bool teamTapAttached[2] = { false, false };

static uint8_t joystick_ram[4];
uint8_t joypad0Buttons[JOYPAD_BUTTON_SLOTS];
uint8_t joypad1Buttons[JOYPAD_BUTTON_SLOTS];
bool audioEnabled     = false;
bool joysticksEnabled = false;

void JoystickSetTeamTap(int port, bool attached)
{
	if (port < 0 || port > 1)
		return;

	/* Releasing the adapter releases the pads that were behind it: the
	 * sockets stop being readable the instant it is unplugged, and a
	 * latched press left in the array would otherwise be replayed the
	 * next time one is attached. */
	if (teamTapAttached[port] && !attached)
	{
		uint8_t *buttons = (port == 0 ? joypad0Buttons : joypad1Buttons);
		memset(buttons + JOYPAD_SOCKET_SLOTS, 0,
		       JOYPAD_BUTTON_SLOTS - JOYPAD_SOCKET_SLOTS);
	}

	teamTapAttached[port] = attached;
}

bool JoystickGetTeamTap(int port)
{
	if (port < 0 || port > 1)
		return false;

	return teamTapAttached[port];
}

/* Decode one port's row-select nibble into (socket, row).
 *
 * WITHOUT a Team Tap the port carries a single controller wired as socket
 * 0 and every other code is idle -- byte for byte what the twelve 0xFF
 * sentinels in the replaced tables meant.  (TR10 warns that real hardware
 * "may return an 'echo' of the standard Joypad controller at socket 0"
 * there.  This core has always returned idle instead; "may" reads as an
 * analogue artifact rather than a specified behaviour, so do not "fix"
 * that into an echo without hardware evidence.)
 *
 * WITH one, all sixteen codes resolve.  Both outputs are 0xFF when the
 * code addresses nothing. */
static void JoystickDecodeRow(int port, uint8_t nibble,
                              uint8_t *socket, uint8_t *row)
{
	const uint8_t *sockTab = (port == 0 ? teamTapSocket0 : teamTapSocket1);
	const uint8_t *rowTab  = (port == 0 ? teamTapRow0    : teamTapRow1);
	uint8_t s;

	nibble &= 0x0F;
	s       = sockTab[nibble];

	if (s != 0 && !teamTapAttached[port])
	{
		*socket = 0xFF;
		*row    = 0xFF;
		return;
	}

	*socket = s;
	*row    = rowTab[nibble];
}

void JoystickInit(void)
{
	JoystickReset();
}


void JoystickExec(void)
{
}


void JoystickReset(void)
{
	memset(joystick_ram, 0x00, 4);
	memset(joypad0Buttons, 0, JOYPAD_BUTTON_SLOTS);
	memset(joypad1Buttons, 0, JOYPAD_BUTTON_SLOTS);
}


void JoystickDone(void)
{
}


uint16_t JoystickReadWord(uint32_t offset)
{
	offset &= 0x03;

	if (offset == 0)
	{
		uint8_t sock0, row0, sock1, row1;
		uint16_t data = 0xFFFF;

		if (!joysticksEnabled)
			return 0xFFFF;

		// Joystick data returns active low for buttons pressed, high for non-
		// pressed.
		JoystickDecodeRow(0, joystick_ram[1], &sock0, &row0);
		JoystickDecodeRow(1, (uint8_t)(joystick_ram[1] >> 4), &sock1, &row1);

		// Non-pad devices self-clock against the game's own polling: at
		// most one quadrature state per armed read (inputdev.h).  The
		// decoded row per port is passed rather than gated on here: a
		// mouse still advances only when ITS port's row select is
		// asserted (any of rows 0-3 -- the adapter is row-blind, and
		// without that a title polling port 1 rapidly would advance the
		// port-2 encoder between the port-2 poller's own samples, the
		// lost motion the one-state ceiling exists to prevent, mapping
		// doc section 5), while a rotary is a matrix device whose phases
		// exist in row 0 only and so samples on a row-0 read of its own
		// port.  Both gates live in inputdev.c because a port-1 rotary
		// has to advance on reads a port-2 test would reject.  No-op
		// when nothing but pads are attached.  The two lookups above are
		// pure table reads, hoisted (not added) so they can be passed
		// here.
		//
		// A TEAM TAP SOCKET OTHER THAN 0 PASSES 0xFF (#513).  Non-pad
		// devices in tap sockets are a deliberate non-goal -- TR10
		// forbids the useful half of the combination ("only a controller
		// read will be possible ... software control of advanced
		// features like rumble motors, force feedback and
		// analogue/digital mode will not be possible") and the device
		// layer is per-PORT, not per-socket.  Passing 0xFF for sockets
		// 1-3 keeps every InputDev* call seeing exactly what it saw
		// before this feature existed: those twelve row codes used to
		// decode to 0xFF unconditionally.
		InputDevClock(sock0 == 0 ? row0 : 0xFF,
		              sock1 == 0 ? row1 : 0xFF);

		if (sock0 != 0xFF)
		{
			unsigned i;
			uint16_t mask[4] = { 0xFEFF, 0xFDFF, 0xFBFF, 0xF7FF };
			uint16_t msk2[4] = { 0xFFFF, 0xFFFD, 0xFFFB, 0xFFF7 };
			uint8_t base = (uint8_t)(sock0 * JOYPAD_SOCKET_SLOTS + row0 * 4);

			for(i = 0; i < 4; i++)
				data &= (joypad0Buttons[base + i] ? mask[i] : 0xFFFF);

			data &= msk2[row0];
		}

		if (sock1 != 0xFF)
		{
			unsigned i;
			uint16_t mask[4] = { 0xEFFF, 0xDFFF, 0xBFFF, 0x7FFF };
			uint16_t msk2[4] = { 0xFF7F, 0xFFBF, 0xFFDF, 0xFFEF };
			uint8_t base = (uint8_t)(sock1 * JOYPAD_SOCKET_SLOTS + row1 * 4);

			for(i = 0; i < 4; i++)
				data &= (joypad1Buttons[base + i] ? mask[i] : 0xFFFF);

			data &= msk2[row1];
		}

		// Overlay for a non-pad device (inputdev.h).  The mouse is
		// row-independent and ignores the rows; the analog controller
		// (#437) drives a different slice of X/Y per row and needs them.
		return InputDevOverlayF14000(data,
		              sock0 == 0 ? row0 : 0xFF,
		              sock1 == 0 ? row1 : 0xFF);
	}
	else if (offset == 2)
	{
		uint8_t sock0, row0, sock1, row1;
		// Hardware ID returns NTSC/PAL identification bit here
		// N.B.: On real H/W, bit 7 is *always* zero...!
		uint16_t data = 0xFF6F | (vjs.hardwareTypeNTSC ? 0x10 : 0x00);
		const uint8_t mask[4][2] = { { BUTTON_A, BUTTON_PAUSE }, { BUTTON_B, 0xFF }, { BUTTON_C, 0xFF }, { BUTTON_OPTION, 0xFF } };

		if (!joysticksEnabled)
			return data;

		// Joystick data returns active low for buttons pressed, high for non-
		// pressed.
		JoystickDecodeRow(0, joystick_ram[1], &sock0, &row0);
		JoystickDecodeRow(1, (uint8_t)(joystick_ram[1] >> 4), &sock1, &row1);

		if (sock0 != 0xFF)
		{
			uint8_t base = (uint8_t)(sock0 * JOYPAD_SOCKET_SLOTS);

			data &= (joypad0Buttons[base + mask[row0][0]] ? 0xFFFD : 0xFFFF);
			if (mask[row0][1] != 0xFF)
				data &= (joypad0Buttons[base + mask[row0][1]] ? 0xFFFE : 0xFFFF);

			// TEAM TAP DETECTION (#513).  TR10: "To detect the presence
			// of a 4-player adaptor, program should inquire the status
			// of Row 1 of controller socket #3.  If a 4-Player adaptor
			// is present, the B0/B2 bit will be clear (0).  Otherwise it
			// will be set (1)."  The mechanism is not adapter logic: it
			// is diode D21 at the C1 matrix position, fitted only to
			// socket 3's harness, and C1 sits on row 1.
			//
			// sock0 can only be 3 with a tap attached, so this needs no
			// teamTapAttached[] test of its own.  Nothing else touches
			// B0 in row 1 -- mask[1][1] is 0xFF -- so the bit otherwise
			// stays at the 1 it inherits from the 0xFF6F base, which is
			// why the NO-TAP answer was already correct with no code for
			// it at all.
			if (sock0 == 3 && row0 == 1)
				data &= 0xFFFE;
		}

		if (sock1 != 0xFF)
		{
			uint8_t base = (uint8_t)(sock1 * JOYPAD_SOCKET_SLOTS);

			data &= (joypad1Buttons[base + mask[row1][0]] ? 0xFFF7 : 0xFFFF);
			if (mask[row1][1] != 0xFF)
				data &= (joypad1Buttons[base + mask[row1][1]] ? 0xFFFB : 0xFFFF);

			/* Port 2's detect bit is B2 (bit 2), the mirror of port 1's
			 * B0.  Row 1 leaves it alone for the same reason. */
			if (sock1 == 3 && row1 == 1)
				data &= 0xFFFB;
		}

		// The overlay takes the decoded row (0-3), or 0xFF when that
		// port's nibble addresses no socket-0 row -- the same contract
		// as $F14000 above, tap sockets included.
		return InputDevOverlayF14002(data,
		              sock0 == 0 ? row0 : 0xFF,
		              sock1 == 0 ? row1 : 0xFF);
	}

	return 0xFFFF;
}


void JoystickWriteWord(uint32_t offset, uint16_t data)
{
	offset &= 0x03;
	joystick_ram[offset + 0] = (data >> 8) & 0xFF;
	joystick_ram[offset + 1] = (data & 0xFF);

	if (offset == 0)
	{
		uint8_t sock0, row0, sock1, row1;

		audioEnabled     = ((data & 0x0100) ? true : false);
		joysticksEnabled = ((data & 0x8000) ? true : false);
		// Arm the non-pad emission clock (inputdev.h).
		InputDevArm();

		// Row-select change notification: the analog controller's bank
		// clock (#437) -- TR10 switches banks on the row-3 -> row-0
		// transition of the device's own socket, and the row select is
		// this latched write, not a read.  With bit 15 clear the JOY
		// outputs are disabled (tri-state, pulled up), so the device
		// sees no row code at all.
		if (joysticksEnabled)
		{
			JoystickDecodeRow(0, joystick_ram[1], &sock0, &row0);
			JoystickDecodeRow(1, (uint8_t)(joystick_ram[1] >> 4),
			                  &sock1, &row1);
		}
		else
		{
			sock0 = sock1 = 0xFF;
			row0  = row1  = 0xFF;
		}

		/* Tap sockets 1-3 pass 0xFF, exactly as the read path does --
		 * see the InputDevClock() call there for why. */
		InputDevRowSelect(sock0 == 0 ? row0 : 0xFF,
		                  sock1 == 0 ? row1 : 0xFF);
	}
}

#include "state.h"

size_t JoystickStateSave(uint8_t *buf)
{
	uint8_t *start = buf;

	/* SOCKET 0 ONLY, and the length is JOYPAD_SOCKET_SLOTS rather than
	 * sizeof() ON PURPOSE.  This chunk sits in the MIDDLE of the state
	 * blob, and test/test_state_compat.c synthesizes its v1/v2/v3
	 * fixtures by locating module blocks inside a live blob and splicing
	 * -- so growing it here would invalidate every one of them and every
	 * released state along with them.  The Team Tap's sockets 1-3 are a
	 * TRAILING chunk instead: JoystickTeamTapStateSave() below. */
	STATE_SAVE_BUF(buf, joystick_ram, sizeof(joystick_ram));
	STATE_SAVE_BUF(buf, joypad0Buttons, JOYPAD_SOCKET_SLOTS);
	STATE_SAVE_BUF(buf, joypad1Buttons, JOYPAD_SOCKET_SLOTS);
	STATE_SAVE_VAR(buf, audioEnabled);
	STATE_SAVE_VAR(buf, joysticksEnabled);

	return (size_t)(buf - start);
}

size_t JoystickStateLoad(const uint8_t *buf)
{
	const uint8_t *start = buf;

	STATE_LOAD_BUF(buf, joystick_ram, sizeof(joystick_ram));
	STATE_LOAD_BUF(buf, joypad0Buttons, JOYPAD_SOCKET_SLOTS);
	STATE_LOAD_BUF(buf, joypad1Buttons, JOYPAD_SOCKET_SLOTS);
	STATE_LOAD_VAR(buf, audioEnabled);
	STATE_LOAD_VAR(buf, joysticksEnabled);

	return (size_t)(buf - start);
}

/* v13 trailing chunk: the pads in Team Tap sockets 1-3, both ports.
 *
 * These have to be in the blob.  What a pad behind the tap is holding is
 * machine-visible at $F14000/$F14002 the moment the title selects that
 * socket's row code, so a state restored without them replays different
 * input -- the same class of bug as #400 (hi-res epoch) and #479, both of
 * which were state living outside the blob.  Run-ahead and netplay are
 * exactly the consumers that notice.
 *
 * The ADAPTER'S OWN PRESENCE is deliberately NOT here: that is the user's
 * configuration, like the InputDevType, and a stale state must not be able
 * to attach or detach it (joystick.h).  A state saved with a tap and
 * loaded without one simply restores bytes the decode never reads. */
size_t JoystickTeamTapStateSave(uint8_t *buf)
{
	uint8_t *start = buf;

	STATE_SAVE_BUF(buf, joypad0Buttons + JOYPAD_SOCKET_SLOTS,
	               JOYPAD_BUTTON_SLOTS - JOYPAD_SOCKET_SLOTS);
	STATE_SAVE_BUF(buf, joypad1Buttons + JOYPAD_SOCKET_SLOTS,
	               JOYPAD_BUTTON_SLOTS - JOYPAD_SOCKET_SLOTS);

	return (size_t)(buf - start);
}

size_t JoystickTeamTapStateLoad(const uint8_t *buf)
{
	const uint8_t *start = buf;

	STATE_LOAD_BUF(buf, joypad0Buttons + JOYPAD_SOCKET_SLOTS,
	               JOYPAD_BUTTON_SLOTS - JOYPAD_SOCKET_SLOTS);
	STATE_LOAD_BUF(buf, joypad1Buttons + JOYPAD_SOCKET_SLOTS,
	               JOYPAD_BUTTON_SLOTS - JOYPAD_SOCKET_SLOTS);

	return (size_t)(buf - start);
}

/* Pre-v13 states carry no such chunk: sockets 1-3 released, which is
 * precisely what a pre-v13 core was. */
void JoystickTeamTapStateReset(void)
{
	memset(joypad0Buttons + JOYPAD_SOCKET_SLOTS, 0,
	       JOYPAD_BUTTON_SLOTS - JOYPAD_SOCKET_SLOTS);
	memset(joypad1Buttons + JOYPAD_SOCKET_SLOTS, 0,
	       JOYPAD_BUTTON_SLOTS - JOYPAD_SOCKET_SLOTS);
}
