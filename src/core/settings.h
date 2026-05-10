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

struct CDBootStrategy;

struct VJSettings
{
	/* Original three fields kept first so the test harness in
	 * test/test_hle_bios.c (which redeclares VJSettings with just these
	 * three) sees the same layout via dlsym. */
	bool hardwareTypeNTSC;
	bool useJaguarBIOS;
	bool useFastBlitter;

	int32_t joyport;
	bool hardwareTypeAlpine;
	uint32_t frameSkip;
	uint32_t biosType;
	uint32_t cdBiosType;
	uint32_t cdBootMode;

	char jagBootPath[MAX_PATH];
	char CDBootPath[MAX_PATH];
	char alpineROMPath[MAX_PATH];
};

enum { BT_K_SERIES, BT_M_SERIES, BT_STUBULATOR_1, BT_STUBULATOR_2 };
enum { CDBIOS_RETAIL, CDBIOS_DEV };
enum { CDBOOT_AUTO, CDBOOT_HLE, CDBOOT_BIOS };

struct BootConfig
{
	bool isCDGame;
	bool showBootROM;
	bool cdBiosAvailable;
	const struct CDBootStrategy *strategy;
};

void ResolveBootConfig(struct BootConfig *cfg,
                       bool isCDGame, bool cdBiosFileLoaded,
                       uint32_t cdBootMode, bool userWantsBIOS);

extern struct BootConfig bootConfig;

// Exported variables

extern struct VJSettings vjs;

#ifdef __cplusplus
}
#endif

#endif	// __SETTINGS_H__
