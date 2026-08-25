//
// Jaguar joystick handler
//

#ifndef __JOYSTICK_H__
#define __JOYSTICK_H__

#include <stdint.h>

#include <boolean.h>

#ifdef __cplusplus
extern "C" {
#endif

enum
{
   BUTTON_FIRST = 0,
   BUTTON_U = 0,
   BUTTON_D = 1,
   BUTTON_L = 2,
   BUTTON_R = 3,

   BUTTON_s = 4,
   BUTTON_7 = 5,
   BUTTON_4 = 6,
   BUTTON_1 = 7,
   BUTTON_0 = 8,
   BUTTON_8 = 9,
   BUTTON_5 = 10,
   BUTTON_2 = 11,
   BUTTON_d = 12,
   BUTTON_9 = 13,
   BUTTON_6 = 14,
   BUTTON_3 = 15,

   BUTTON_A = 16,
   BUTTON_B = 17,
   BUTTON_C = 18,
   BUTTON_OPTION = 19,
   BUTTON_PAUSE = 20,
   BUTTON_LAST = 20
};

/* Team Tap (4-Player Adaptor, #513).
 *
 * TR10 "4-Player Adaptor (Team Tap)": the adaptor demultiplexes the four
 * row-select outputs into sixteen, and rewrites the row codes for its
 * sockets 1-3 into socket-0 codes so "those controllers will only see
 * socket 0 row codes".  A pad behind the tap is therefore an ORDINARY
 * pad that never knows it is there -- the whole feature is a console-side
 * decode change, with no new device semantics.
 *
 * So each port's button array is four sockets' worth of the SAME 21-slot
 * matrix, and SOCKET 0 KEEPS SLOTS [0..20].  That is what makes the
 * no-tap path bit-identical: every existing socket-0 reference
 * (libretro.c's update_input(), the $F14002 row/button table below,
 * test/tools/joymatrix_identity.c) indexes exactly the bytes it always
 * did, and the decode never reaches above index 20 unless a tap is
 * selected. */
#define JOYPAD_SOCKET_SLOTS  21
#define JOYPAD_SOCKETS        4
#define JOYPAD_BUTTON_SLOTS  (JOYPAD_SOCKETS * JOYPAD_SOCKET_SLOTS)

void JoystickInit(void);
void JoystickReset(void);
void JoystickDone(void);
//void JoystickWriteByte(uint32_t, uint8_t);
void JoystickWriteWord(uint32_t, uint16_t);
//uint8_t JoystickReadByte(uint32_t);
uint16_t JoystickReadWord(uint32_t);
void JoystickExec(void);

/* Attach / detach a Team Tap on `port` (0 = port 1, 1 = port 2).
 *
 * Not serialized, for the reason inputdev.h gives for the device type: it
 * is a fact about the user's configuration, not about the emulated
 * machine, and restoring it from a state would let a stale state fight
 * the current session.  The pads BEHIND it are serialized -- see
 * JoystickTeamTapStateSave() in state.h. */
void JoystickSetTeamTap(int port, bool attached);
bool JoystickGetTeamTap(int port);

/* Both are JOYPAD_BUTTON_SLOTS wide; slots [0..20] are socket 0. */
extern uint8_t joypad0Buttons[];
extern uint8_t joypad1Buttons[];
extern bool audioEnabled;
extern bool joysticksEnabled;

#ifdef __cplusplus
}
#endif

#endif	// __JOYSTICK_H__

