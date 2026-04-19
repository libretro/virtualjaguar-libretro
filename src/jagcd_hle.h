#ifndef __JAGCD_HLE_H__
#define __JAGCD_HLE_H__

#include <stdint.h>
#include <boolean.h>

#ifdef __cplusplus
extern "C" {
#endif

/* HLE (High-Level Emulation) CD BIOS replacement.
 *
 * When no real CD BIOS ROM is available, the HLE path handles the entire
 * CD boot sequence in C: extracts the boot stub from the disc image,
 * sets up the BIOS jump table and TOC, and intercepts BIOS CD_read calls
 * to DMA sectors directly into Jaguar RAM. */

/* Set up the HLE CD environment after JaguarReset().
 * Extracts boot stub, populates TOC, installs jump table stubs,
 * and configures 68K entry point at $080000.
 * Returns true if HLE boot was set up successfully. */
bool JaguarCDHLEBoot(void);

/* Called from M68KInstructionHook for every instruction.
 * Intercepts BIOS jump table calls (CD_read, etc.) and handles
 * them entirely in C.
 * Returns true if the PC was handled (caller should skip other hooks). */
bool JaguarCDHLEHook(uint32_t pc);

/* Called from gpu.c when the GPU data phase starts (boot stub's
 * GPU program that would read CD data via BUTCH).  Instead of letting
 * the broken BUTCH path run, reads sectors directly into Jaguar RAM.
 * Returns true if the data was transferred (caller should stop GPU). */
bool JaguarCDHLEGPUDataPhase(void);

/* True if HLE mode is active (set by JaguarCDHLEBoot on success). */
bool JaguarCDHLEActive(void);

#ifdef __cplusplus
}
#endif

#endif /* __JAGCD_HLE_H__ */
