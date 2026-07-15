//
// CDROM.H
//

#ifndef __CDROM_H__
#define __CDROM_H__

#include <boolean.h>

#include "vjag_memory.h"

#ifdef __cplusplus
extern "C" {
#endif

void CDROMInit(void);
void CDROMReset(void);
void CDROMDone(void);

void BUTCHExec(uint32_t cycles);

uint8_t CDROMReadByte(uint32_t offset, uint32_t who);
uint16_t CDROMReadWord(uint32_t offset, uint32_t who);
void CDROMWriteByte(uint32_t offset, uint8_t data, uint32_t who);
void CDROMWriteWord(uint32_t offset, uint16_t data, uint32_t who);

bool ButchIsReadyToSend(void);
bool CDROMHasData(void);  // True when sector buffer has valid data
bool CDROMIsBiosOverride(void);
uint8_t CDROMReadFifoByte(uint32_t who);
uint16_t GetWordFromButchSSI(uint32_t offset, uint32_t who);
void SetSSIWordsXmittedFromButch(void);
void CDROMDiagSummary(void);

/* Diagnostic accessor for harnesses. Reads the same diag_* counters that
 * CDROMDiagSummary prints, so test harnesses can compose their own
 * per-disc lines without parsing log output. Any pointer may be NULL.
 * Pure read-only — no side effects, safe to call from any context. */
void CDROMDiagGetCounters(uint32_t *butchExec,
                          uint32_t *fifoIRQs,
                          uint32_t *dsaIRQs,
                          uint32_t *fifoReads,
                          uint32_t *seeks,
                          uint32_t *globalDisabled,
                          uint32_t *hleBytes);

/* --- CD trace ring (Task 4 instrumentation, see cdrom.c) ---
 * Diagnostic only; never touches savestates. */

/* Enable/disable the trace ring. Called from libretro.c's check_variables()
 * with the `virtualjaguar_cd_trace` core option state; internally OR'd
 * with the VJ_CD_TRACE=1 env override so headless harnesses work even if
 * they never poll core options. */
void CDTraceSetEnabled(int enabled);

/* Dump the ring to the RetroArch log (LOG_INF, "[CD-TRACE]" prefix).
 * Called on-demand (e.g. by crash_detect.c's cd_seek_wedge watchdog) or
 * by test harnesses that want the DSA conversation leading up to a stall. */
void CDTraceDump(void);

/* Push a trace event for the HLE CD_read path (src/cd/jagcd_hle.c) -- the
 * only call site outside cdrom.c, since jagcd_hle.c performs a synchronous
 * seek+transfer with no separate BUTCH-driven seek/FIFO state machine. */
void CDTraceHLERead(uint32_t lba, uint16_t byteCountTrunc);

/* Read-only accessor for crash_detect.c's cd_seek_wedge watchdog. Any
 * pointer may be NULL. seekStarts/seekDones count genuine (non-redundant)
 * $12xx seeks and their $0100 completions; fifoDrains counts completed
 * 16-word FIFO drain cycles. */
void CDROMDiagGetSeekWedgeState(uint32_t *seekStarts, uint32_t *seekDones,
                                uint32_t *fifoDrains);

/* First (boot-relevant) $12xx seek target, post-redirect, as an absolute
 * disc LBA.  Returns 0xFFFFFFFF if no seek has been issued since reset.
 * Consumed by test/test_cd_fifo_stream.c to locate the sync mark the GPU
 * CD ISR must find in the FIFO stream. Diagnostic only, never serialized. */
uint32_t CDROMDiagGetFirstSeekBlock(void);

#ifdef __cplusplus
}
#endif

#endif	// __CDROM_H__
