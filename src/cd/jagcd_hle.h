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

/* Streamed CD_read transfer: CD_read arms a transfer that this tick
 * advances at the real double-speed drive rate (352,800 B/s).  Called
 * once per halfline from HalflineCallback when CD content is loaded.
 * Instant delivery is NOT correct: games poll from code inside the
 * destination range and rely on the drive's streaming latency to jump
 * away before their own code is overwritten. */
void JaguarCDHLEStreamTick(void);

/* True while a streamed CD_read transfer is in flight. */
bool JaguarCDHLEStreamActive(void);

/* Force HLE active state (for unit testing without a disc image). */
void JaguarCDHLESetActive(bool active);

#ifdef __cplusplus
}
#endif

#endif /* __JAGCD_HLE_H__ */
