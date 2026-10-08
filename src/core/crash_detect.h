/* crash_detect.h -- Lightweight runtime watchdog for Jaguar emulation
 * anomalies that look like a "crash" to the user (GPU/DSP PC escape,
 * GPU/DSP wedge, video stall).  Hooks once per frame; cost is a few
 * comparisons + a tiny rolling framebuffer hash, so it's enabled by
 * default in production builds.
 *
 * On detect, fires LOG_WRN/LOG_ERR via vj_log_cb so the signature
 * shows up in any RetroArch log without extra plumbing.  Does not
 * abort -- the core keeps running so users get the full event trace
 * of their session, not just the first trip.
 *
 * Tuning lives at the top of crash_detect.c.
 */

#ifndef CRASH_DETECT_H
#define CRASH_DETECT_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Watchdog modes. Set via core option `virtualjaguar_crash_detect`. */
#define CRASH_DETECT_OFF      0
#define CRASH_DETECT_ON       1   /* default -- log on anomaly only */
#define CRASH_DETECT_VERBOSE  2   /* also log periodic heartbeat snapshots */

/* Lifecycle */
void CrashDetectInit(void);
void CrashDetectReset(void);   /* retro_load_game and retro_reset */
void CrashDetectSetMode(int mode);

/* Record G_PC at every GPU GO so gpu_runaway can tell a program the GPU
 * was started at from a jump into a data buffer (issue #461). */
void CrashDetectNoteGPUGo(uint32_t pc);

/* cd_seek_wedge per-frame predicate (issue #741), split out so a unit test
 * can pin it without a disc.  Returns 1 when this frame counts toward the
 * wedge window: a seek has been issued, the FIFO drain counter did not move,
 * a RISC processor is running, and the drive still owes data -- a seek is
 * outstanding, or the game still has a FIFO transfer open: BUTCH master AND
 * FIFO interrupts armed (butch_int = BUTCH low byte) AND I2S FIFO data
 * enabled (i2s_ctrl = I2CNTRL low byte, bit 2).  A game ends a transfer by
 * clearing either one; that idle drive is not a wedge. */
int CrashDetectCDSeekWedgeFrame(uint32_t seek_starts, uint32_t seek_dones,
                                uint32_t fifo_drains, uint32_t last_fifo_drains,
                                int processor_running, uint8_t butch_int,
                                uint8_t i2s_ctrl);

/* Watchdog state for tests (issue #799).  A harness's own pass criteria
 * (PC in RAM, not looping) are all satisfied by a 68K that keeps running
 * its CD service loop while the transfer is dead, so they cannot see a
 * cd_seek_wedge; the watchdog can.  Fires = how many times the wedge
 * crossed its threshold since CrashDetectReset() (sticky, not subject to
 * the log throttle, 0 while the watchdog is disabled); Streak = frames the
 * wedge predicate has held in a row right now. */
unsigned CrashDetectCDSeekWedgeFires(void);
unsigned CrashDetectCDSeekWedgeStreak(void);

/* In-frame hang signature (issue #740).  The blitter runs synchronously
 * inside one register write, so a garbage B_COUNT freezes the host inside
 * retro_run and the per-frame checks never get a turn.  Called at blit
 * dispatch, before either engine runs: logs `inframe_hang` once per
 * LOG_REPEAT window when the blit is absurdly large, then lets it run --
 * log only, no behaviour change.  b_count = B_COUNT ($F0223C); dst_flags
 * and dst_x feed CrashDetectBlitNeverEnds. */
void CrashDetectNoteBlit(uint32_t b_count, uint32_t b_cmd, uint32_t a1_base,
                         uint32_t dst_flags, uint32_t dst_x);

/* Whether a B_COUNT value's pixel count (inner * outer, as the accurate
 * engine executes it) crosses the inframe_hang threshold.  Split out for
 * the unit test. */
int CrashDetectBlitIsAbsurd(uint32_t b_count);

/* Whether the blit's first inner loop can never end (issue #800): phrase
 * mode below 8bpp, where the netlist's inner-counter decrement is
 * dstxp[0] and phrase-aligned X keeps it at zero.  dst_flags = A1_FLAGS
 * (A2_FLAGS when B_CMD.DSTA2), dst_x = that pointer's X.  Split out for the
 * unit test. */
int CrashDetectBlitNeverEnds(uint32_t b_count, uint32_t dst_flags, uint32_t dst_x);

/* Per-frame hook -- call once at the END of JaguarExecuteNew so all
 * subsystems have been advanced.  fb may be NULL if no framebuffer
 * is available this frame; the stall detector skips that frame. */
void CrashDetectFrameTick(const uint32_t *fb, unsigned w, unsigned h);

#ifdef __cplusplus
}
#endif

#endif /* CRASH_DETECT_H */
