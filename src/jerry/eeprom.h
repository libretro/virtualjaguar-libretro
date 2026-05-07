//
// EEPROM.H: Header file
//

#ifndef __EEPROM_H__
#define __EEPROM_H__

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

extern uint16_t eeprom_ram[64];
extern void (*eeprom_dirty_cb)(void);

void EepromInit(void);
void EepromReset(void);
void EepromDone(void);

uint8_t EepromReadByte(uint32_t offset);
uint16_t EepromReadWord(uint32_t offset);
void EepromWriteByte(uint32_t offset, uint8_t data);
void EepromWriteWord(uint32_t offset, uint16_t data);

#ifdef __cplusplus
}
#endif

#endif	// __EEPROM_H__
