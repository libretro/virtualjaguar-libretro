//
// CDROM.H
//

#ifndef __CDROM_H__
#define __CDROM_H__

//#include "types.h"
#include "memory.h"

void CDROMInit(void);
void CDROMReset(void);
void CDROMDone(void);

void BUTCHExec(uint32 cycles);

uint8 CDROMReadByte(uint32 offset, uint32 who = UNKNOWN);
uint16 CDROMReadWord(uint32 offset, uint32 who = UNKNOWN);
void CDROMWriteByte(uint32 offset, uint8 data, uint32 who = UNKNOWN);
void CDROMWriteWord(uint32 offset, uint16 data, uint32 who = UNKNOWN);

bool ButchIsReadyToSend(void);
uint16 GetWordFromButchSSI(uint32 offset, uint32 who = UNKNOWN);
void SetSSIWordsXmittedFromButch(void);

#endif	// __CDROM_H__
