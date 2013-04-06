//
// Jaguar blitter implementation
//

#ifndef __BLITTER_H__
#define __BLITTER_H__

//#include "types.h"
#include "memory.h"

void BlitterInit(void);
void BlitterReset(void);
void BlitterDone(void);

uint8 BlitterReadByte(uint32, uint32 who = UNKNOWN);
uint16 BlitterReadWord(uint32, uint32 who = UNKNOWN);
uint32 BlitterReadLong(uint32, uint32 who = UNKNOWN);
void BlitterWriteByte(uint32, uint8, uint32 who = UNKNOWN);
void BlitterWriteWord(uint32, uint16, uint32 who = UNKNOWN);
void BlitterWriteLong(uint32, uint32, uint32 who = UNKNOWN);

uint32 blitter_reg_read(uint32 offset);
void blitter_reg_write(uint32 offset, uint32 data);

extern uint8 blitter_working;

//For testing only...
void LogBlit(void);

#endif	// __BLITTER_H__
