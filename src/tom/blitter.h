//
// Jaguar blitter implementation
//

#ifndef __BLITTER_H__
#define __BLITTER_H__

#include <stddef.h>
#include "vjag_memory.h"

#ifdef __cplusplus
extern "C" {
#endif

void BlitterInit(void);
/* Blitter bus-time model (virtualjaguar_blitter_timing) */
void BlitterTimingTick(uint32_t sysclks);
uint32_t BlitterTimingGetBusy(void);
void BlitterTimingSetBusy(uint32_t clks);
void BlitterReset(void);
/* Zero the B_CMD decode statics that BlitterStateSave() serialises.
 * Called by BlitterReset()/BlitterDone(); see blitter.c for why (#479). */
void BlitterResetDecodeState(void);
void BlitterDone(void);

/* Blits that never end on hardware (issues #800, #794).
 *
 * BlitterNeverEnds: whether a blit's first inner loop can never finish.
 * Exact netlist condition, shared by the accurate engine, the dispatch
 * path and the crash watchdog so they cannot drift apart.  dst_flags =
 * A1_FLAGS, or A2_FLAGS when B_CMD.DSTA2; dst_x = that pointer's X.
 *
 * BlitterNeverEndsApprox: for such a blit, whether the one-wrap-period
 * memory effect the accurate engine produces is only an approximation
 * (the repeated step is not provably idempotent on memory).
 *
 * BlitterIsHung: the sticky "blitter hung" flag.  Set at dispatch when
 * a never-ending blit was started; B_CMD then reads busy and further
 * starts are ignored, until reset / power-on.  Serialised as the
 * trailing "BLH1" savestate chunk (v16, after CDX1). */
int BlitterNeverEnds(uint32_t b_count, uint32_t dst_flags, uint32_t dst_x);
int BlitterNeverEndsApprox(uint32_t b_cmd);
int BlitterIsHung(void);
size_t BlitterHungStateSize(void);
size_t BlitterHungStateSave(uint8_t *buf);
size_t BlitterHungStateLoad(const uint8_t *buf);
void BlitterHungStateReset(void);

uint8_t BlitterReadByte(uint32_t, uint32_t who);
uint16_t BlitterReadWord(uint32_t, uint32_t who);
uint32_t BlitterReadLong(uint32_t, uint32_t who);
void BlitterWriteByte(uint32_t, uint8_t, uint32_t who);
void BlitterWriteWord(uint32_t, uint16_t, uint32_t who);
void BlitterWriteLong(uint32_t, uint32_t, uint32_t who);

uint32_t blitter_reg_read(uint32_t offset);
void blitter_reg_write(uint32_t offset, uint32_t data);

void BlitterCompareEnable(int enable);
int BlitterCompareIsEnabled(void);
void BlitterCompareGetStats(uint32_t *total, uint32_t *diffs, uint32_t *skipped);
void BlitterCompareDumpCmdStats(void);

/* Filter scaffolding (test tooling).  Filters narrow which blits the
 * compare path actually diffs; non-matching blits are still executed
 * (using the non-compare default path) so game state advances normally.
 *
 *   SetFrame:        the test tool calls this once per frame so the
 *                    compare facility knows the current frame number.
 *                    Default: 0.
 *   SetFrameWindow:  inclusive [first,last] frame range.  Default
 *                    [0, UINT32_MAX] = always-on.
 *   SetCmdMask:      compare only when (cmd & mask) == value.
 *                    Default mask=0 = accept all.
 *   SetVerbose:      on diff, dump the full pre-blit register set and
 *                    a side-by-side byte-pair hexdump of the differing
 *                    destination region. */
void BlitterCompareSetFrame(uint32_t frame);
void BlitterCompareSetFrameWindow(uint32_t first, uint32_t last);
void BlitterCompareSetCmdMask(uint32_t mask, uint32_t value);
void BlitterCompareSetVerbose(int verbose);

#ifdef __cplusplus
}
#endif

#endif	// __BLITTER_H__
