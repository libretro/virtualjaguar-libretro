//
// settings.h: Header file
//

#ifndef __SETTINGS_H__
#define __SETTINGS_H__

#include <stdint.h>
#include <stdlib.h>								// for MAX_PATH on MinGW/Darwin
#include <limits.h>

#include <boolean.h>

#ifndef MAX_PATH
#define MAX_PATH		4096
#endif

#ifdef __cplusplus
extern "C" {
#endif

// Settings struct

struct VJSettings
{
	int32_t joyport;								// Joystick port
	bool hardwareTypeNTSC;						// Set to false for PAL
	bool useJaguarBIOS;
	bool GPUEnabled;
	bool DSPEnabled;
	bool usePipelinedDSP;
	bool hardwareTypeAlpine;
	bool audioEnabled;
	uint32_t frameSkip;
	uint32_t biosType;
	bool useFastBlitter;

	// Paths

	char jagBootPath[MAX_PATH];
	char CDBootPath[MAX_PATH];
	char EEPROMPath[MAX_PATH];
	char alpineROMPath[MAX_PATH];
};

// Render types

enum { RT_NORMAL = 0, RT_TV = 1 };

// BIOS types

enum { BT_K_SERIES, BT_M_SERIES, BT_STUBULATOR_1, BT_STUBULATOR_2 };

// Exported variables

extern struct VJSettings vjs;

#ifdef __cplusplus
}
#endif

#endif	// __SETTINGS_H__
