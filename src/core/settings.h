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
	bool blitterTiming;
	bool gpuPipelineTiming;

	int32_t joyport;
	bool hardwareTypeAlpine;
	uint32_t frameSkip;
	uint32_t biosType;
	uint32_t cdBiosType;
	uint32_t cdBootMode;
	/* CD read-speed multiplier for the HLE streamed CD_read path:
	 * 1/2/4/8 = that many times the 1x CD-ROM rate (hardware is 2x),
	 * CDSPEED_INSTANT (0) = whole transfer in one tick.  Latched per-read
	 * at arm time; BIOS-mode FIFO cadence is NOT affected (see
	 * src/cd/jagcd_hle.c). */
	uint32_t cdReadSpeed;

	/* Cycle-exact DSP idle-loop fast-forward (issue #569, perf audit P1).
	 * Skips provably redundant iterations of a wait loop inside one RISC
	 * slice; bit-exact by construction (see the safety theorem in
	 * src/jerry/dsp.c).  Added after the existing scalars, never before
	 * them: test/test_hle_bios.c redeclares VJSettings with only the
	 * first three fields and resolves it via dlsym. */
	bool riscIdleSkip;

	char jagBootPath[MAX_PATH];
	char CDBootPath[MAX_PATH];
	char alpineROMPath[MAX_PATH];
};

enum { BT_K_SERIES, BT_M_SERIES, BT_STUBULATOR_1, BT_STUBULATOR_2, BT_CUSTOM };
enum { CDBIOS_RETAIL, CDBIOS_DEV };
enum { CDBOOT_AUTO, CDBOOT_HLE, CDBOOT_BIOS };
enum { CDSPEED_INSTANT = 0, CDSPEED_1X = 1, CDSPEED_2X = 2,
       CDSPEED_4X = 4, CDSPEED_8X = 8 };

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
