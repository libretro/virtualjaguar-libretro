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

#ifdef __cplusplus
}
#endif

#endif	// __CDROM_H__
