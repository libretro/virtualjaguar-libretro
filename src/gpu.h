//
// GPU.H: Header file
//

#ifndef __GPU_H__
#define __GPU_H__

#include "vjag_memory.h"

#ifdef __cplusplus
extern "C" {
#endif

#define GPU_CONTROL_RAM_BASE    0x00F02100
#define GPU_WORK_RAM_BASE		0x00F03000

void GPUInit(void);
void GPUReset(void);
void GPUExec(int32_t);
void GPUUpdateRegisterBanks(void);
void GPUHandleIRQs(void);
void GPUSetIRQLine(int irqline, int state);

uint8_t GPUReadByte(uint32_t offset, uint32_t who);
uint16_t GPUReadWord(uint32_t offset, uint32_t who);
uint32_t GPUReadLong(uint32_t offset, uint32_t who);
void GPUWriteByte(uint32_t offset, uint8_t data, uint32_t who);
void GPUWriteWord(uint32_t offset, uint16_t data, uint32_t who);
void GPUWriteLong(uint32_t offset, uint32_t data, uint32_t who);

uint32_t GPUGetPC(void);
void GPUReleaseTimeslice(void);
void GPUResetStats(void);
uint32_t GPUReadPC(void);

// GPU interrupt numbers (from $F00100, bits 4-8)

enum { GPUIRQ_CPU = 0, GPUIRQ_DSP, GPUIRQ_TIMER, GPUIRQ_OBJECT, GPUIRQ_BLITTER };

// Exported vars

extern uint32_t gpu_reg_bank_0[], gpu_reg_bank_1[];

#ifdef __cplusplus
}
#endif

#endif	// __GPU_H__
