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

/* JaguarCDHLEHook's own internal gate, exposed as a plain bool for
 * jaguar.c's M68KInstructionHook outer guard -- a direct mirror of the
 * "if (!hle_active) return false;" check at the top of JaguarCDHLEHook,
 * not the more conservative JaguarCDHLEActive() compound (which also
 * requires bootConfig.strategy == &cd_boot_strategy_hle). All writers
 * live in jagcd_hle.c. */
extern bool hle_active;

/* Streamed CD_read transfer: CD_read arms a transfer that this tick
 * advances at the real double-speed drive rate (352,800 B/s).  Called
 * once per halfline from HalflineCallback when CD content is loaded.
 * Instant delivery is NOT correct: games poll from code inside the
 * destination range and rely on the drive's streaming latency to jump
 * away before their own code is overwritten. */
void JaguarCDHLEStreamTick(void);

/* True while a streamed CD_read transfer is in flight. */
bool JaguarCDHLEStreamActive(void);

/* Test/probe accessors for the most recently armed CD_read stream:
 * destination base, wire byte count (long-rounded), and a monotonically
 * increasing arm counter (bumps once per accepted CD_read, including
 * the instant-completion fallback).  Lets harnesses attribute RAM
 * mutations to a CD stream targeting a watched region vs the game's
 * own writes (test_cd_hle_idempotent). */
uint32_t JaguarCDHLEStreamDest(void);
uint32_t JaguarCDHLEStreamBytes(void);
uint32_t JaguarCDHLEStreamArmCount(void);

/* Force HLE active state (for unit testing without a disc image). */
void JaguarCDHLESetActive(bool active);

#ifdef __cplusplus
}
#endif

#endif /* __JAGCD_HLE_H__ */
