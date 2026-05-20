//
// Jaguar blitter implementation
//

#ifndef __BLITTER_H__
#define __BLITTER_H__

#include "vjag_memory.h"

#ifdef __cplusplus
extern "C" {
#endif

void BlitterInit(void);
void BlitterReset(void);
void BlitterDone(void);

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
