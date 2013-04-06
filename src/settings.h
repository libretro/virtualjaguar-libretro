//
// SETTINGS.H: Header file
//

#ifndef __SETTINGS_H__
#define __SETTINGS_H__

// MAX_PATH isn't defined in stdlib.h on *nix, so we do it here...
//#ifdef __GCCUNIX__
#include <limits.h>
#define MAX_PATH		_POSIX_PATH_MAX
//#else
#include <stdlib.h>								// for MAX_PATH on MinGW/Darwin
//#endif
#include "types.h"

// Settings struct

struct VJSettings
{
	bool useJoystick;
	int32 joyport;								// Joystick port
	bool hardwareTypeNTSC;						// Set to false for PAL
	bool useJaguarBIOS;
	bool GPUEnabled;
	bool DSPEnabled;
	bool usePipelinedDSP;
	bool fullscreen;
	bool useOpenGL;
	uint32 glFilter;
	bool hardwareTypeAlpine;
	bool audioEnabled;
	uint32 frameSkip;
	uint32 renderType;
	bool allowWritesToROM;

	// Keybindings in order of U, D, L, R, C, B, A, Op, Pa, 0-9, #, *

	uint32 p1KeyBindings[21];
	uint32 p2KeyBindings[21];

	// Paths

	char ROMPath[MAX_PATH];
	char jagBootPath[MAX_PATH];
	char CDBootPath[MAX_PATH];
	char EEPROMPath[MAX_PATH];
	char alpineROMPath[MAX_PATH];
	char absROMPath[MAX_PATH];
};

// Render types

enum { RT_NORMAL = 0, RT_TV = 1 };

// Exported functions

//void LoadVJSettings(void);
//void SaveVJSettings(void);

// Exported variables

extern VJSettings vjs;

#endif	// __SETTINGS_H__
