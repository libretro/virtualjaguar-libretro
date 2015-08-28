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
uint16_t GetWordFromButchSSI(uint32_t offset, uint32_t who);
void SetSSIWordsXmittedFromButch(void);

#ifdef __cplusplus
}
#endif

#endif	// __CDROM_H__
