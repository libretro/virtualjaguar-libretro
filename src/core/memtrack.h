#ifndef _VIRTUALJAGUAR_MEMTRACK_H
#define _VIRTUALJAGUAR_MEMTRACK_H

/*
 * memtrack.h: Header file
 */

#include <stdint.h>

#include <boolean.h>

#define MT_MEM_SIZE 0x20000

/* NVRAM data window.  The save area is NOT in the $8xxxxx ROM window -- it
 * sits at $900000, which is why an implementation that maps it over the cart
 * ROM never works alongside a CD BIOS. */
#define MT_DATA_BASE 0x900000
#define MT_DATA_END  (MT_DATA_BASE + MT_MEM_SIZE)   /* $920000 */

/* Flash command/ID window (inside the ROM window, override-only). */
#define MT_CMD_UNLOCK1 0x815554        /* $800000 + 4 * $5555 */
#define MT_CMD_UNLOCK2 0x80AAA8        /* $800000 + 4 * $2AAA */
#define MT_ID_MANUF    0x800000
#define MT_ID_DEVICE   0x800004

extern uint8_t mtMem[MT_MEM_SIZE];

/* Called after any NVRAM write so the libretro layer can keep its
 * RETRO_MEMORY_SAVE_RAM mirror current (mirrors eeprom_dirty_cb). */
extern void (*mt_dirty_cb)(void);

void MTInit(void);
void MTReset(void);
void MTDone(void);

/* Does the Memory Track answer for this address at all?  Everything it does
 * not claim must fall through to the normal cart ROM / CD BIOS. */
bool MTClaimsRead(uint32_t addr);
bool MTClaimsWrite(uint32_t addr);

uint8_t  MTReadByte(uint32_t addr);
void     MTWriteByte(uint32_t addr, uint8_t data);
uint16_t MTReadWord(uint32_t addr);
uint32_t MTReadLong(uint32_t addr);
void MTWriteWord(uint32_t addr, uint16_t data);
void MTWriteLong(uint32_t addr, uint32_t data);

#endif
