#ifndef __JAGCD_HLE_H__
#define __JAGCD_HLE_H__

#include <stdint.h>
#include <boolean.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Set up the HLE CD environment after JaguarReset().
 * Extracts boot stub, populates TOC, installs jump table stubs,
 * and configures 68K entry point.
 * Returns true if HLE boot was set up successfully. */
bool JaguarCDHLEBoot(void);

/* Called from M68KInstructionHook for every instruction.
 * Intercepts BIOS jump table calls (CD_read, etc.) and handles
 * them entirely in C.
 * Returns true if the PC was handled (caller should skip other hooks). */
bool JaguarCDHLEHook(uint32_t pc);

/* Called from gpu.c when the GPU data phase starts. */
bool JaguarCDHLEGPUDataPhase(void);

/* True if HLE mode is active (set by JaguarCDHLEBoot on success). */
bool JaguarCDHLEActive(void);

/* Force HLE active state (for unit testing without a disc image). */
void JaguarCDHLESetActive(bool active);

#ifdef __cplusplus
}
#endif

#endif /* __JAGCD_HLE_H__ */
