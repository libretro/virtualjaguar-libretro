#ifndef __JAGUAR_H__
#define __JAGUAR_H__

#include <stdint.h>

#include <boolean.h>

#include "vjag_memory.h"							// For "UNKNOWN" enum

#ifdef __cplusplus
extern "C" {
#endif

void JaguarSetScreenBuffer(uint32_t * buffer);
void JaguarSetScreenPitch(uint32_t pitch);
void JaguarInit(void);
void JaguarReset(void);
void JaguarApplyHLEBIOSState(void);
void JaguarDone(void);
void JaguarSeedPRNG(uint32_t seed);
uint32_t JaguarRand(void);

uint8_t JaguarReadByte(uint32_t offset, uint32_t who);
uint16_t JaguarReadWord(uint32_t offset, uint32_t who);
uint32_t JaguarReadLong(uint32_t offset, uint32_t who);
void JaguarWriteByte(uint32_t offset, uint8_t data, uint32_t who);
void JaguarWriteWord(uint32_t offset, uint16_t data, uint32_t who);
void JaguarWriteLong(uint32_t offset, uint32_t data, uint32_t who);

bool JaguarInterruptHandlerIsValid(uint32_t i);

void JaguarExecuteNew(void);

// Exports from JAGUAR.CPP

extern int32_t jaguarCPUInExec;
extern char * jaguarEepromsPath;
extern bool jaguarCartInserted;
extern bool jaguarMemTrackInserted;
extern bool bpmActive;
extern uint32_t bpmAddress1;
/* Fills the 16 D0-D7/A0-A7 traceback rings in M68KInstructionHook().
 * Default off -- see the comment on its definition in jaguar.c (#540).
 * Exported in the test ABI only; set it before the first retro_run(). */
extern bool startM68KTracing;

#ifdef __cplusplus
extern "C" {
#endif
extern uint32_t jaguarMainROMCRC32, jaguarROMSize, jaguarRunAddress;
extern uint32_t jaguarLoadedRAMStart, jaguarLoadedRAMEnd;
#ifdef __cplusplus
}
#endif

// Various clock rates

#define M68K_CLOCK_RATE_PAL		13296950
#define M68K_CLOCK_RATE_NTSC	13295453
#define RISC_CLOCK_RATE_PAL		26593900
#define RISC_CLOCK_RATE_NTSC	26590906

#define SYSTEM_CLOCK_RATE		(vjs.hardwareTypeNTSC ? RISC_CLOCK_RATE_NTSC : RISC_CLOCK_RATE_PAL)
#define M68K_CLOCK_RATE			(vjs.hardwareTypeNTSC ? M68K_CLOCK_RATE_NTSC : M68K_CLOCK_RATE_PAL)

/* Video field geometry -- ONE definition, so the rate we advertise to the
 * frontend can never drift from the field the core actually paces.
 *
 * JTRM Revision 8 p.15 (VP -- Vertical Period, $F0003E): "This eleven bit
 * register determines the number of half lines per field.  The number is
 * one more than the value written into this register.  If the number of
 * half lines is odd then the display is interlaced."
 * JTRM Revision 10 p.8 ("Video Timings"): Vertical Lines (non interlaced)
 * = 524 NTSC / 624 PAL (the interlaced variants are 525 / 625).
 *
 * Jaguar titles run non-interlaced, so VP+1 is the EVEN member: 524 / 624
 * -- which is exactly what TOMReset programs (VP = 523 / 623).  525 would
 * put TOM into interlace, so a non-interlaced NTSC field is 60.05 Hz, NOT
 * the 59.94 Hz of 262.5-line interlaced video (that arithmetic is the same
 * one that puts a 262-line NES/SNES field at ~60.1 Hz).
 *
 * The halfline periods are (HP+1)/master clock, with the HP values
 * TOMReset writes (844 NTSC / 850 PAL): 845/26.590906 MHz = 31.77778 us
 * and 851/26.593900 MHz = 31.99972 us, matching the Rev 10 line periods
 * of 63.5555 us / 64.0 us.
 *
 * These are the RESET DEFAULTS.  VP is game-programmable and
 * HalflineCallback follows the live register, so the emulated field
 * always tracks whatever a title programs; these are what the machine
 * runs unless a title says otherwise, and are therefore what gets
 * advertised (chasing a live VP would mean firing SET_SYSTEM_AV_INFO
 * mid-session, which tears down the frontend's audio pipeline). */
#define VJ_HALFLINES_PER_FIELD_NTSC	524u
#define VJ_HALFLINES_PER_FIELD_PAL	624u
#define VJ_HALFLINE_US_NTSC		31.777777777	/* (HP+1)/26.590906 MHz */
#define VJ_HALFLINE_US_PAL		32.0		/* (HP+1)/26.593900 MHz */

/* 31.777777777 (NTSC) / 32.0 (PAL), per vjs.hardwareTypeNTSC. */
double JaguarGetHalflinePeriodUs(void);
/* 524 (NTSC) / 624 (PAL): the reset-default field length. */
uint32_t JaguarGetDefaultFieldHalflines(void);
/* 1e6 / (halflines * halfline_us): 60.05445 Hz NTSC / 50.08013 Hz PAL. */
double JaguarGetFieldRateHz(void);

/* Clock-scale enhancement levers (issue #314), in percent of the stock
 * rate (100 = stock).  Config, not state: set from the core options in
 * libretro.c::check_variables(), never serialized.  Applied ONLY where
 * execution budgets are handed out (JaguarExecuteNew() timeslices and
 * the 68K->GPU 2:1 coupling in GPUSyncToM68K()) -- bus costs and all
 * event scheduling (video, PIT, UART, I2S) stay on the real, unscaled
 * sysclock.  For the per-access charges that are deducted from a
 * SCALED budget (bus_arbiter_m68k_access() wait states, the GPU bus
 * stall in gpu.c) wall-time invariance is enforced by converting the
 * sysclk charge into the consumer's scaled cycle domain -- see the
 * cycle-domain contract in bus_arbiter.h (issue #318).  At 100 the
 * integer arithmetic (c * 100 / 100) is an exact identity, so 1x is
 * bit-identical to the unscaled build. */
extern uint32_t m68kClockScalePct;
void M68KClockScaleReset(void);
/* Drop any pending 16-bit-port low-word latch (GPU/DSP local RAM writes);
 * called on reset and on savestate load so post-load execution cannot
 * depend on pre-load history. */
void M68KResetRiscWordLatch(void);
extern uint32_t riscClockScalePct;

#define SCALE_M68K_CYCLES(c)	((uint32_t)(((uint64_t)(c) * m68kClockScalePct) / 100u))
#define SCALE_RISC_CYCLES(c)	((uint32_t)(((uint64_t)(c) * riscClockScalePct) / 100u))

// Stuff for IRQ handling

#define ASSERT_LINE		1
#define CLEAR_LINE		0

//Temp debug stuff (will go away soon, so don't depend on these)
uint8_t * GetRamPtr(void);

#ifdef __cplusplus
}
#endif

#endif	// __JAGUAR_H__
