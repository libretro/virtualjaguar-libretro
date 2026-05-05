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
	bool hardwareTypeNTSC;						// Set to false for PAL
	bool useJaguarBIOS;
	bool useFastBlitter;
	bool useBusContention;						// Model blitter/OP bus cycle stealing
};

// Exported variables

extern struct VJSettings vjs;

#ifdef __cplusplus
}
#endif

#endif	// __SETTINGS_H__
