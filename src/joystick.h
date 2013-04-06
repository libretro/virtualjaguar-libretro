//
// Jaguar joystick handler
//

#ifndef __JOYSTICK_H__
#define __JOYSTICK_H__

#include "types.h"

enum { BUTTON_FIRST = 0, BUTTON_U = 0,
BUTTON_D = 1,
BUTTON_L = 2,
BUTTON_R = 3,
BUTTON_A = 4,
BUTTON_B = 5,
BUTTON_C = 6,
BUTTON_PAUSE = 7,
BUTTON_OPTION = 8,
    
BUTTON_1 = 9,
BUTTON_2 = 10,
BUTTON_3 = 11,
BUTTON_4 = 12,
BUTTON_5 = 13,
BUTTON_6 = 14,
BUTTON_7 = 15,
BUTTON_8 = 16,
BUTTON_9 = 17,
BUTTON_0 = 18,
    
BUTTON_s = 19,
BUTTON_d = 20,

BUTTON_LAST = 20 };

void JoystickInit(void);
void JoystickReset(void);
void JoystickDone(void);
void JoystickWriteByte(uint32, uint8);
void JoystickWriteWord(uint32, uint16);
uint8 JoystickReadByte(uint32);
uint16 JoystickReadWord(uint32);
void JoystickExec(void);

extern bool keyBuffer[];
extern uint8 joypad_0_buttons[];
extern uint8 joypad_1_buttons[];
#endif
