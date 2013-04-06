#ifndef __JAGUAR_H__
#define __JAGUAR_H__

#include "types.h"
#include "memory.h"							// For "UNKNOWN" enum

void JaguarSetScreenBuffer(uint32 * buffer);
void JaguarSetScreenPitch(uint32 pitch);
void JaguarInit(void);
void JaguarReset(void);
void JaguarDone(void);

uint8 JaguarReadByte(uint32 offset, uint32 who = UNKNOWN);
uint16 JaguarReadWord(uint32 offset, uint32 who = UNKNOWN);
uint32 JaguarReadLong(uint32 offset, uint32 who = UNKNOWN);
void JaguarWriteByte(uint32 offset, uint8 data, uint32 who = UNKNOWN);
void JaguarWriteWord(uint32 offset, uint16 data, uint32 who = UNKNOWN);
void JaguarWriteLong(uint32 offset, uint32 data, uint32 who = UNKNOWN);

bool JaguarInterruptHandlerIsValid(uint32 i);
void JaguarDasm(uint32 offset, uint32 qt);

void JaguarExecuteNew(void);

// Exports from JAGUAR.CPP

extern int32 jaguarCPUInExec;
extern uint32 jaguarMainROMCRC32, jaguarROMSize, jaguarRunAddress;
extern char * jaguarEepromsPath;
extern bool jaguarCartInserted;

// Various clock rates

#define M68K_CLOCK_RATE_PAL		13296950
#define M68K_CLOCK_RATE_NTSC	13295453
#define RISC_CLOCK_RATE_PAL		26593900
#define RISC_CLOCK_RATE_NTSC	26590906

// Stuff for IRQ handling

#define ASSERT_LINE		1
#define CLEAR_LINE		0

//Temp debug stuff (will go away soon, so don't depend on these)

void DumpMainMemory(void);
uint8 * GetRamPtr(void);

#endif	// __JAGUAR_H__
