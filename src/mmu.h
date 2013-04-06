//
// mmu.h
//
// Jaguar Memory Manager Unit
//
// by James L. Hammons
//

#ifndef __MMU_H__
#define __MMU_H__

//#include "types.h"
#include "memory.h"

void MMUWrite8(uint32 address, uint8 data, uint32 who = UNKNOWN);
void MMUWrite16(uint32 address, uint16 data, uint32 who = UNKNOWN);
void MMUWrite32(uint32 address, uint32 data, uint32 who = UNKNOWN);
void MMUWrite64(uint32 address, uint64 data, uint32 who = UNKNOWN);
uint8 MMURead8(uint32 address, uint32 who = UNKNOWN);
uint16 MMURead16(uint32 address, uint32 who = UNKNOWN);
uint32 MMURead32(uint32 address, uint32 who = UNKNOWN);
uint64 MMURead64(uint32 address, uint32 who = UNKNOWN);

#endif	// __MMU_H__
