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

/* Per-frame hook -- call once at the END of JaguarExecuteNew so all
 * subsystems have been advanced.  fb may be NULL if no framebuffer
 * is available this frame; the stall detector skips that frame. */
void CrashDetectFrameTick(const uint32_t *fb, unsigned w, unsigned h);

#ifdef __cplusplus
}
#endif

#endif /* CRASH_DETECT_H */
