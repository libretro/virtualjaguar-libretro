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

#ifdef __cplusplus
}
#endif

#endif	// __BLITTER_H__
