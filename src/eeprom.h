//
// EEPROM.H: Header file
//

#ifndef __EEPROM_H__
#define __EEPROM_H__

#include "types.h"

void EepromInit(void);
void EepromReset(void);
void EepromDone(void);
void EepromUpdate(void);

uint8 EepromReadByte(uint32 offset);
uint16 EepromReadWord(uint32 offset);
void EepromWriteByte(uint32 offset, uint8 data);
void EepromWriteWord(uint32 offset, uint16 data);

#endif	// __EEPROM_H__
