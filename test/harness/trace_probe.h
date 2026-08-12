/*
 * test/harness/trace_probe.h — shared vjtrace flight-recorder probe.
 *
 * ======================================================================
 * USAGE
 * ======================================================================
 *
 *   #include "harness/harness.h"
 *   #include "harness/trace_probe.h"
 *
 *   harness_config cfg = HARNESS_CONFIG_DEFAULT;
 *   if (!harness_init_from_args(&cfg, argc, argv)) return 1;
 *   if (!harness_load_rom(&cfg))                   return 1;
 *   cfg.frame_callback = my_own_hook;      // optional, set BEFORE attach
 *   if (!trace_probe_attach(&cfg))                 return 1;
 *   harness_run(&cfg);
 *   trace_probe_finish(&cfg);
 *   harness_shutdown(&cfg);
 *
 * trace_probe_attach() installs itself as cfg->frame_callback and CHAINS
 * to whatever callback was already there, so set your own hook first.  A
 * tool that installs its hook after attaching displaces the probe; in
 * that case call trace_probe_frame() from your own hook instead.
 *
 * Attach is a no-op success when none of the flight-recorder flags were
 * given, so a tool may call it unconditionally.
 *
 * ======================================================================
 * FLAGS (parsed by harness.c into cfg, acted on here)
 * ======================================================================
 *
 *   --trace-out FILE          Write the binary "VJTR" event ring at exit
 *   --field-csv FILE          One row per emulated frame (columns below)
 *   --watch A[:LEN][:r|w|rw]  Memory watch, repeatable, max 16.
 *                             A and LEN accept 0x-prefixed hex, a
 *                             $-prefixed hex, or plain decimal (strtoul
 *                             base 0, so a leading 0 means octal).
 *                             LEN defaults to 4, mode defaults to w.
 *                             The core range is [A, A+LEN-1] inclusive.
 *   --snap FRAME              Write "<prefix>_fNNNNNN.vjsn" after FRAME,
 *                             repeatable
 *   --snap-prefix BASE        Snapshot base name (default "vjt_snap")
 *   --mark FRAME:TAG          Emit a VJT_EV_MARK after FRAME.  TAG's
 *                             first four characters are packed into the
 *                             event's `value` big-endian
 *                             (value = t[0]<<24 | t[1]<<16 | t[2]<<8 |
 *                             t[3], missing characters 0); `addr` is the
 *                             TAG's full length in characters, so a
 *                             reader can tell a truncated tag from an
 *                             exact one.  who = DEBUG (host origin).
 *
 * FLAG-NAME COLLISIONS — read before adding attach to another tool.
 * test/tools/cd_wedge_probe.c already defines --snap / --snap-prefix
 * (RAM hexdumps, not VJSN state files) and test/tools/irq_rate_probe.c
 * already defines --watch (its own address list).  Both pre-parse argv
 * themselves and neither calls trace_probe_attach(), so today the two
 * meanings never collide: harness.c only records the raw strings and
 * nothing interprets them unless a tool attaches.  Adding
 * trace_probe_attach() to either tool would make one invocation do both
 * things at once — rename the flag on one side first.
 *
 * ======================================================================
 * PER-FIELD CSV
 * ======================================================================
 *
 *   frame,pad0,irq_video,irq_gpu,irq_obj,irq_timer,irq_jerry,
 *   irq_dispatch,gpu_go,gpu_stop,op_list_start,op_obj,op_gpu_obj,
 *   op_branch,blit_cmd,watch_rd,watch_wr,fb_hash
 *
 * - `frame` is the HARNESS frame counter (1-based, cfg->current_frame).
 *   The FIRST emulated frame is 1, not 0.
 *
 *   CORRELATING A CSV ROW WITH RING EVENTS — no correction needed, in
 *   either direction:
 *
 *       ring.frame == csv.frame      (machine AND host-injected events)
 *
 *   libretro.c ticks vjtrace_frame_tick(++counter) at the TOP of
 *   retro_run, so events the machine emits while frame N runs carry N,
 *   and the events this probe emits from the post-run frame hook —
 *   INPUT_EDGE, MARK, and the SNAPSHOT event from --snap — carry N too
 *   (the next tick has not happened yet).  Row N's counts are therefore
 *   exactly the ring events stamped N.  Frame 0 exists but holds only
 *   what was emitted during retro_init / retro_load_game, before the
 *   first retro_run; it has no CSV row.
 * - `pad0` is the joypad bitmask the harness injected on port 0 that
 *   frame (bit N = RETRO_DEVICE_ID_JOYPAD id N).  On a change the probe
 *   also emits VJT_EV_INPUT_EDGE (who = DEBUG, addr = pad index, value =
 *   bits) so the ring records host input alongside machine events.
 * - Every count column except the five irq_* splits is a per-frame delta
 *   of vjtrace_counters.ev[].  The irq_* splits come from draining the
 *   ring, because the counters carry one counter per event TYPE and the
 *   IRQ source lives in the event's addr field (0 video, 1 gpu, 2 obj,
 *   3 timer, 4 jerry).  Scoping the drain to those five columns is
 *   deliberate: if the ring evicts between frames only they degrade, and
 *   the probe reports the eviction at finish rather than silently
 *   undercounting everything.  Raise the ring size with VJ_TRACE_RING.
 * - `fb_hash` is an FNV-1a over the visible bytes of the framebuffer the
 *   core handed the video callback this frame (hex).  A duped frame
 *   (NULL data) repeats the previous hash, which is what was presented.
 *
 * ======================================================================
 */

#ifndef TRACE_PROBE_H
#define TRACE_PROBE_H

#include "harness.h"

/* Opaque; the single instance lives inside trace_probe.c. */
typedef struct trace_probe trace_probe;

/* Resolve the core's vjtrace exports and apply the flight-recorder
 * flags.  Call after harness_load_rom().
 *
 * Returns 1 on success, including the no-op case where no flag was
 * given.  Returns 0 if a requested output file cannot be opened.
 *
 * Does NOT return when a flag was given but the core does not export
 * the matching vjtrace symbol (a production build, i.e. one made
 * without TEST_EXPORTS=1): prints the missing symbol names and calls
 * exit(2), because silently producing an empty CSV or ring would be
 * indistinguishable from a real measurement of nothing. */
int trace_probe_attach(harness_config *cfg);

/* One field's worth of bookkeeping: drain the ring, write the CSV row,
 * fire any --snap / --mark due at this frame.  Installed as the
 * harness frame callback by attach (chaining to any prior callback); a
 * tool that owns the frame callback itself can call this directly. */
void trace_probe_frame(harness_config *cfg, int frame);

/* Write --trace-out, close the CSV, print a one-line summary.  Call
 * before harness_shutdown() — the ring lives in the core. */
void trace_probe_finish(harness_config *cfg);

#endif /* TRACE_PROBE_H */
