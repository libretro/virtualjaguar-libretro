//
// GPU.H: Header file
//

#ifndef __GPU_H__
#define __GPU_H__

//#include "types.h"
#include "memory.h"

#define GPU_CONTROL_RAM_BASE    0x00F02100
#define GPU_WORK_RAM_BASE		0x00F03000

void GPUInit(void);
void GPUReset(void);
void GPUExec(int32);
void GPUDone(void);
void GPUUpdateRegisterBanks(void);
void GPUHandleIRQs(void);
void GPUSetIRQLine(int irqline, int state);

uint8 GPUReadByte(uint32 offset, uint32 who = UNKNOWN);
uint16 GPUReadWord(uint32 offset, uint32 who = UNKNOWN);
uint32 GPUReadLong(uint32 offset, uint32 who = UNKNOWN);
void GPUWriteByte(uint32 offset, uint8 data, uint32 who = UNKNOWN);
void GPUWriteWord(uint32 offset, uint16 data, uint32 who = UNKNOWN);
void GPUWriteLong(uint32 offset, uint32 data, uint32 who = UNKNOWN);

uint32 GPUGetPC(void);
void GPUReleaseTimeslice(void);
void GPUResetStats(void);
uint32 GPUReadPC(void);

// GPU interrupt numbers (from $F00100, bits 4-8)

enum { GPUIRQ_CPU = 0, GPUIRQ_DSP, GPUIRQ_TIMER, GPUIRQ_OBJECT, GPUIRQ_BLITTER };

#endif	// __GPU_H__
