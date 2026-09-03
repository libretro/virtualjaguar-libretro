//
// dsp.h
//

#ifndef __DSP_H__
#define __DSP_H__

#include <boolean.h>

#include "vjag_memory.h"

#ifdef __cplusplus
extern "C" {
#endif

#define DSP_CONTROL_RAM_BASE    0x00F1A100
#define DSP_WORK_RAM_BASE		0x00F1B000

void DSPInit(void);
void DSPReset(void);
void DSPExec(int32_t);
void DSPDone(void);
void DSPUpdateRegisterBanks(void);
void DSPSetIRQLine(int irqline, int state);
/* Slice bookkeeping for the 68K->DSP-local-RAM sync; mirror of
 * GPUBeginSlice / GPUSliceRemaining / GPUSyncToM68K (issue #456). */
void DSPBeginSlice(uint32_t riscCycles);
int32_t DSPSliceRemaining(void);
void DSPSyncToM68K(void);
uint8_t DSPReadByte(uint32_t offset, uint32_t who);
uint16_t DSPReadWord(uint32_t offset, uint32_t who);
uint32_t DSPReadLong(uint32_t offset, uint32_t who);
void DSPWriteByte(uint32_t offset, uint8_t data, uint32_t who);
void DSPWriteWord(uint32_t offset, uint16_t data, uint32_t who);
void DSPWriteLong(uint32_t offset, uint32_t data, uint32_t who);
void DSPReleaseTimeslice(void);
bool DSPIsRunning(void);
uint8_t *DSPGetRAM(void);
uint32_t DSPGetFlags(void);
void DSPSetFlags(uint32_t v);
/* GDB stub accessors (issue #652): the active register bank and PC,
 * read and write. See src/jerry/dsp.c for why the writes are raw pokes
 * rather than routed through the MMIO write path. */
uint32_t DSPGetPC(void);
void DSPSetPC(uint32_t pc);
uint32_t DSPGetControl(void);
uint32_t DSPGetReg(int n);
void DSPSetReg(int n, uint32_t v);

void DSPExecP(int32_t cycles);
void DSPExecP2(int32_t cycles);
void DSPExecComp(int32_t cycles);

// Exported vars

extern uint32_t dsp_reg_bank_0[], dsp_reg_bank_1[];

/* Idle-loop fast-forward diagnostics (issue #569).  Counted only on the
 * cold probe path; read by test/tools, never by the emulator itself. */
extern uint32_t dsp_idle_skip_fires;
extern uint32_t dsp_idle_skip_rejects;
extern uint32_t dsp_idle_skip_iters;
extern uint32_t dsp_idle_skip_opcodes;

// DSP interrupt numbers (in $F1A100, bits 4-8 & 16)

enum { DSPIRQ_CPU = 0, DSPIRQ_SSI, DSPIRQ_TIMER0, DSPIRQ_TIMER1, DSPIRQ_EXT0, DSPIRQ_EXT1 };

#ifdef __cplusplus
}
#endif

#endif	// __DSP_H__
