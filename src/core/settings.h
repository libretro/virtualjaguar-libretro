//
// settings.h: Header file
//

#ifndef __SETTINGS_H__
#define __SETTINGS_H__

#include <stdint.h>

#include <boolean.h>

#ifdef __cplusplus
extern "C" {
#endif

// Settings struct

struct VJSettings
{
	int32_t joyport;								// Joystick port
	bool hardwareTypeNTSC;						// Set to false for PAL
	bool useJaguarBIOS;
	bool hardwareTypeAlpine;
	/* Legacy BIOS selector; currently always defaults to BT_K_SERIES. */
	uint32_t biosType;
	bool useFastBlitter;
};

// BIOS types

enum { BT_K_SERIES, BT_M_SERIES, BT_STUBULATOR_1, BT_STUBULATOR_2 };

// Exported variables

extern struct VJSettings vjs;

#ifdef __cplusplus
}
#endif

#endif	// __SETTINGS_H__
