#ifndef __BLITTER_INTERNAL_H__
#define __BLITTER_INTERNAL_H__

#include <stddef.h>
#include <stdint.h>

extern uint8_t blitter_ram[0x100];

void blitter_blit(uint32_t cmd);
void BlitterMidsummer2(void);
void BlitterRunComparison(void);

/* Per-access trace facility (instrumented build only).
 * Always defined to keep call sites trivial; phase==0 makes the call a no-op. */
extern int blit_cmp_trace_phase; /* 0=off, 1=fast, 2=accurate */
void BlitterCompareTraceWrite(uint32_t addr, uint32_t value, int bits);
void BlitterCompareTraceRead(uint32_t addr, uint32_t value, int bits);

#endif
