#ifndef __JAGUAR_H__
#define __JAGUAR_H__

#include <stdint.h>

#include <boolean.h>

#include "vjag_memory.h"							// For "UNKNOWN" enum

#ifdef __cplusplus
extern "C" {
#endif

void JaguarSetScreenBuffer(uint32_t * buffer);
void JaguarSetScreenPitch(uint32_t pitch);
void JaguarInit(void);
void JaguarReset(void);
void JaguarDone(void);

uint8_t JaguarReadByte(uint32_t offset, uint32_t who);
uint16_t JaguarReadWord(uint32_t offset, uint32_t who);
uint32_t JaguarReadLong(uint32_t offset, uint32_t who);
void JaguarWriteByte(uint32_t offset, uint8_t data, uint32_t who);
void JaguarWriteWord(uint32_t offset, uint16_t data, uint32_t who);
void JaguarWriteLong(uint32_t offset, uint32_t data, uint32_t who);

bool JaguarInterruptHandlerIsValid(uint32_t i);

void JaguarExecuteNew(void);

// Exports from JAGUAR.CPP

extern int32_t jaguarCPUInExec;
extern char * jaguarEepromsPath;
extern bool jaguarCartInserted;
extern bool bpmActive;
extern uint32_t bpmAddress1;

#ifdef __cplusplus
extern "C" {
#endif
extern uint32_t jaguarMainROMCRC32, jaguarROMSize, jaguarRunAddress;
#ifdef __cplusplus
}
#endif

// Various clock rates

#define M68K_CLOCK_RATE_PAL		13296950
#define M68K_CLOCK_RATE_NTSC	13295453
#define RISC_CLOCK_RATE_PAL		26593900
#define RISC_CLOCK_RATE_NTSC	26590906

// Stuff for IRQ handling

#define ASSERT_LINE		1
#define CLEAR_LINE		0

//Temp debug stuff (will go away soon, so don't depend on these)
uint8_t * GetRamPtr(void);

#ifdef __cplusplus
}
#endif

#endif	// __JAGUAR_H__
