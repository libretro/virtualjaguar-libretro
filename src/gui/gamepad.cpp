//
// gamepad.cpp - Host joystick handling (using SDL)
//
// by James Hammons
// (C) 2013 Underground Software
//
// JLH = James Hammons <jlhamm@acm.org>
//
// Who  When        What
// ---  ----------  -------------------------------------------------------------
// JLH  01/05/2013  Created this file
//

#include "gamepad.h"
#include "log.h"


// Class member initialization
/*static*/ int Gamepad::numJoysticks = 0;
/*static*/ SDL_Joystick * Gamepad::pad[8];
/*static*/ int Gamepad::numButtons[8];
/*static*/ int Gamepad::numHats[8];
/*static*/ bool Gamepad::button[8][256];
/*static*/ uint8_t Gamepad::hat[8][32];


Gamepad::Gamepad(void)//: numJoysticks(0)
{
	AllocateJoysticks();
}


Gamepad::~Gamepad(void)
{
	DeallocateJoysticks();
}


void Gamepad::AllocateJoysticks(void)
{
//	DeallocateJoysticks();
	numJoysticks = SDL_NumJoysticks();

	// Sanity check
	if (numJoysticks > 8)
		numJoysticks = 8;

	for(int i=0; i<numJoysticks; i++)
	{
		pad[i] = SDL_JoystickOpen(i);
		numButtons[i] = numHats[i] = 0;

		if (pad[i])
		{
			numButtons[i] = SDL_JoystickNumButtons(pad[i]);
			numHats[i] = SDL_JoystickNumHats(pad[i]);
		}
	}

	WriteLog("Gamepad: Found %u joystick%s.\n", numJoysticks, (numJoysticks == 1 ? "" : "s"));
}


void Gamepad::DeallocateJoysticks(void)
{
	for(int i=0; i<numJoysticks; i++)
		SDL_JoystickClose(pad[i]);
}


bool Gamepad::GetState(int joystickID, int buttonID)
{
	uint8_t hatMask[8] = { 1, 2, 4, 8, 16, 32, 64, 128 };

	if (buttonID & JOY_BUTTON)
	{
		// Handle SDL button
		int buttonNum = (buttonID & JOY_BUTTON_MASK);
		return button[joystickID][buttonNum];
	}
	else if (buttonID & JOY_HAT)
	{
		// Handle SDL hats
		int hatNumber = (buttonID & JOY_HATNUM_MASK) >> 3;
		uint8_t hatDirection = hatMask[buttonID & JOY_HATBUT_MASK];
		return (hat[joystickID][hatNumber] & hatDirection ? true : false);
	}

	// Default == failure
	return false;
}


int Gamepad::CheckButtonPressed(void)
{
	// This translates the hat direction to a mask index.
	int hatNum[16] = { -1, 0, 1, -1, 2, -1, -1, -1,
		3, -1, -1, -1, -1, -1, -1, -1 };

	// Return single button ID being pressed (if any)
	for(int i=0; i<numJoysticks; i++)
	{
		for(int j=0; j<numButtons[i]; j++)
		{
			if (button[i][j])
				return (JOY_BUTTON | j);
		}

		for(int j=0; j<numHats[i]; j++)
		{
			if (hat[i][j])
				return (JOY_HAT | hatNum[hat[i][j]]);
		}
	}

	return -1;
}


int Gamepad::GetButtonID(void)
{
	// Return single button ID being pressed (if any)
	return -1;
}


int Gamepad::GetJoystickID(void)
{
	// Return joystick ID of button being pressed (if any)
	return -1;
}


void Gamepad::Update(void)
{
//	SDL_PollEvent(&event);
	SDL_JoystickUpdate();

	for(int i=0; i<numJoysticks; i++)
	{
		for(int j=0; j<numButtons[i]; j++)
			button[i][j] = SDL_JoystickGetButton(pad[i], j);

		for(int j=0; j<numHats[i]; j++)
			hat[i][j] = SDL_JoystickGetHat(pad[i], j);
	}
}


#if 0
// Need to test this. It may be that the only time joysticks are detected is
// when the program is first run. That would suck.
// Well, it turns out that SDL doesn't do hot plugging. :-(
void Gamepad::CheckConsistency(void)
{
	int currentNumJoysticks = SDL_NumJoysticks();

	// Check to see if the # of joysticks reported by SDL changed
	if (currentNumJoysticks == numJoysticks)
		return;

	// Either one or more joysticks were plugged in, or removed. Fix up our
	// internal states to reflect this.

	
}
#endif

