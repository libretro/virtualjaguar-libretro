#ifndef __BLITTER_INTERNAL_H__
#define __BLITTER_INTERNAL_H__

#include <stddef.h>
#include <stdint.h>

extern uint8_t blitter_ram[0x100];

void blitter_blit(uint32_t cmd);
void BlitterMidsummer2(void);
void BlitterRunComparison(void);

#endif
