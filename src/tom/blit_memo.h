/*
 * BLIT_MEMO.H
 *
 * Titledb-gated blitter memoization (issue #411 route 3, prototype).
 *
 * Some titles re-render an identical scene every engine cycle while the
 * player is idle (measured on Alien vs Predator: one bit-identical
 * 1,446-blit stream per 5-field cycle, producing a framebuffer the
 * machine already has).  This module memoizes blits: when a launch's
 * full pre-launch register file matches a recorded entry AND every
 * main-RAM page that blit read or wrote is untouched since the entry
 * was recorded, the destination already contains exactly the bytes the
 * blit would produce, so the pixel work is skipped and only the
 * register side effects (the recorded post-launch register file) are
 * replayed.  This is memoization, not a heuristic: the skip condition
 * makes the output bit-identical by construction.
 *
 * Stream structure is handled by chaining: each entry remembers its
 * successor, so consecutive identical blit streams (which interleave
 * writes on shared pages and would defeat naive per-blit page
 * validity) skip as a prefix-matched chain.  A skipped prefix is sound
 * on its own: each skipped blit's output is already in RAM regardless
 * of whether the rest of the stream matches.
 *
 * Soundness boundaries (why this must stay per-title gated):
 *  - A blit that reads or writes anything outside main RAM (GPU/TOM/
 *    JERRY space, mutable cart) is never skipped -- it re-executes
 *    live on every match ("exec-through").
 *  - Intermediate states are not reproduced: while a chain is being
 *    skipped, RAM holds the stream's FINAL bytes, not the byte-exact
 *    intermediates a live run would show mid-stream.  A game whose
 *    68K/GPU reads the destination buffer mid-stream could observe
 *    the difference.  No tracked title does; the `verify` mode exists
 *    to falsify exactly this class before tagging a title.
 *  - CD content is not covered: the CD HLE writes main RAM without
 *    passing the write hooks, so the libretro layer refuses to enable
 *    the memo for CD content.
 *
 * Timing is unaffected: the bus-occupancy model (BlitDurationSysclks)
 * is computed analytically from the pre-launch register file at the
 * launch site, before this module runs.
 */

#ifndef __BLIT_MEMO_H__
#define __BLIT_MEMO_H__

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define BLIT_MEMO_OFF    0
#define BLIT_MEMO_ON     1
/* verify: never skips; on every would-be skip it executes the blit
 * live and checks that the destination pages and the post-launch
 * register file come out identical to the memo.  The falsifier to run
 * before tagging a new title in the DB. */
#define BLIT_MEMO_VERIFY 2

/* Hot-path gates: call sites test these before calling the hooks so
 * the disabled mode costs one predictable branch. */
extern int blitMemoMode;
extern int blitMemoRecording;

/* Mode control (libretro option layer). Changing mode flushes. */
void BlitMemoSetMode(int mode);

/* Flush all entries + page generations.  Call on savestate load,
 * machine reset, and anything that rewrites RAM behind the hooks. */
void BlitMemoFlush(void);

/* Engine identity changed (fast <-> accurate): recorded post-states
 * belong to one engine; flush when it flips. */
void BlitMemoNotifyEngine(int useFast);

/* Once per emulated frame (retro_run): advances the shadow-restamp
 * dedupe epoch. */
void BlitMemoFrame(void);

/* Free the pool and reset every static (iOS never dlcloses). */
void BlitMemoShutdown(void);

/* The launch site (blitter_mmio.c, B_CMD dispatch).  Returns 1 if the
 * blit was handled (skipped, verified, or executed under recording);
 * 0 when the memo is off and the caller should dispatch as before. */
int BlitMemoLaunch(void);

/* Write-path hook: page-generation touch for main-RAM writes, plus
 * footprint + write-log capture / untracked-write classification while
 * recording.  `data` is the value being written (logged for replay).
 * Call guarded:  if (blitMemoMode) BlitMemoWriteHook(addr, len, data); */
void BlitMemoWriteHook(uint32_t addr, uint32_t len, uint32_t data);

/* Read-path hook: footprint capture / untracked-read classification.
 * Only meaningful while recording; call guarded:
 *   if (blitMemoRecording) BlitMemoNoteRead(addr, len); */
void BlitMemoNoteRead(uint32_t addr, uint32_t len);

/* Stats (test ABI; cumulative since load). */
extern uint32_t blitMemoHits;         /* blits skipped */
extern uint32_t blitMemoMisses;       /* no matching entry; recorded */
extern uint32_t blitMemoDirty;        /* entry matched, pages dirty */
extern uint32_t blitMemoExecThrough;  /* matched but untracked I/O */
extern uint32_t blitMemoVerifyFails;  /* verify-mode divergences */

#ifdef __cplusplus
}
#endif

#endif /* __BLIT_MEMO_H__ */
