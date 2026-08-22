/*
 * perf_iface.h -- RETRO_ENVIRONMENT_GET_PERF_INTERFACE plumbing (issue #510).
 *
 * Answers "where did the milliseconds go", on the device that is actually
 * slow.  That is the gap: every profiling path this project ships is
 * host-side (docs/profiling.md -- Instruments, perf, flame graphs; `make
 * benchmark` is explicitly same-host-only), and none of it reaches a locked
 * down tvOS box running RetroArch.  The frontend already solves this: it
 * hands the core perf_register/perf_start/perf_stop/perf_log and renders the
 * results in its own performance-counter UI.
 *
 * Distinct from src/core/perf_counters.h, which is not being replaced.  That
 * one counts EVENTS ("the blitter was called 131,628 times") under a
 * non-shipping BENCH_PROFILE build.  This one measures TIME, in shipping
 * builds.  Both are useful; they are not the same tool.
 *
 * ---------------------------------------------------------------------
 * COST WHEN THE FRONTEND DECLINES
 *
 * One load-and-branch on `vjPerfActive`, never taken, at each probe site --
 * the same short-circuit-at-the-first-instruction shape crash_detect.c uses
 * for its off mode.  No indirect call, no counter touched.
 *
 * A truly branch-free off state would need a compile-time switch, which
 * would put the instrumentation outside shipping builds and defeat the whole
 * point.  Every probe here brackets a SLICE (an event-loop step, a halfline,
 * one blit), never an interpreter inner loop, so a perfectly-predicted
 * never-taken branch at that granularity is not measurable.
 *
 * NOTE that "declines" is narrower than "counters are off".  RetroArch
 * ACCEPTS env 28 unconditionally and gates on its own `perfcnt_enable`
 * setting further in: registration is never gated, only start/stop and
 * perf_log are (runloop.c).  So during ordinary RetroArch play vjPerfActive
 * is 1, the branch above is taken, and each probe makes a real indirect call
 * that returns immediately -- on the order of 2,000 per frame.  Still
 * negligible at slice granularity, but it is not the zero described above,
 * and it is why a capture with the setting off yields all-zero counters
 * rather than an error.  docs/profiling.md spells out that trap.
 *
 * ---------------------------------------------------------------------
 * READING THE NUMBERS -- these counters deliberately OVERLAP
 *
 * The Jaguar's processors do not run in disjoint phases, so the totals do
 * NOT sum to frame time and any attempt to read them as a pie chart is
 * wrong.  Two real nestings:
 *
 *   1. A 68K access into GPU/DSP local RAM runs RISC cycles inline via
 *      GPUSyncToM68K()/DSPSyncToM68K() (src/core/jaguar.c, the JaguarRead/
 *      Write dispatch).  So `m68k_slice` contains RISC time.
 *   2. The Object Processor runs the GPU inline from inside a halfline
 *      callback (src/tom/op.c).  So `op_halfline` contains GPU time.
 *
 * `gpu_sync` and `dsp_sync` exist to make nesting (1) measurable rather than
 * merely disclosed -- without them `m68k_slice` is uninterpretable on a
 * GPU-heavy title, which is exactly the workload this is meant to diagnose.
 * The arithmetic that decomposes it:
 *
 *      real 68K work    ~=  m68k_slice - gpu_sync - dsp_sync
 *      slice-driven GPU ~=  gpu_exec   - gpu_sync
 *
 * `gpu_exec` and `dsp_exec` are whole-truth totals: every path that executes
 * RISC cycles goes through GPUExec()/DSPExec(), so they need no correction.
 *
 * ---------------------------------------------------------------------
 * PROBE PLACEMENT RULE -- read before adding or moving one
 *
 * VJP_ENTER goes AFTER a function's top-of-function guards, VJP_LEAVE at the
 * single exit.  Every instrumented function was checked to have no `return`
 * between the two.  This is not only for correctness (an early return
 * between them leaks the depth counter and the slot then measures nothing
 * for the rest of the session) -- it is also more accurate, because the
 * guards are the "nothing to do" cases: a halted GPU, an odd halfline with
 * no OP pass.  Counting those would dilute the average with no-ops.
 *
 * If you add an early return inside an instrumented region, you must add a
 * VJP_LEAVE on that path.  VJPerfLeakedSlots() exists to catch the mistake;
 * test/tools/perf_iface_witness.c asserts it stays zero.
 */

#ifndef VJ_PERF_IFACE_H
#define VJ_PERF_IFACE_H

#include "libretro.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Probe slots.  Keep in sync with vjperf_idents[] in perf_iface.c. */
enum
{
   VJP_M68K = 0,     /* 68K slice, inclusive of nested RISC -- see above */
   VJP_GPU,          /* all GPU execution, every path                    */
   VJP_DSP,          /* all DSP execution, every path                    */
   VJP_GPU_SYNC,     /* the part of VJP_GPU pulled in by a 68K access    */
   VJP_DSP_SYNC,     /* the part of VJP_DSP pulled in by a 68K access    */
   VJP_OP,           /* Object Processor halfline render                 */
   VJP_BLITTER,      /* one blit, either engine                          */
   VJP_DAC,          /* audio mix for one frame                          */
   VJP_COUNT
};

/*
 * Non-zero once the frontend has handed us a usable interface AND at least
 * one counter registered.  Read directly by the macros below; do not write
 * it from outside perf_iface.c.
 */
extern int vjPerfActive;

/* Ask the frontend for the interface and register the counters.  Safe to
 * call when the frontend declines -- vjPerfActive simply stays 0. */
void VJPerfInit(retro_environment_t env_cb);

/* Dump totals to the frontend log (if it offered perf_log) and go inert. */
void VJPerfDeinit(void);

/* Out-of-line so the fast path is a single predictable branch. */
void VJPerfEnter(int slot);
void VJPerfLeave(int slot);

/*
 * Diagnostics, for tests rather than for play.
 *   VJPerfRegisteredCount -- how many counters the frontend accepted.  It may
 *                            accept fewer than we offer; it has a cap.
 *   VJPerfLeakedSlots     -- slots whose nesting depth is non-zero at a point
 *                            where nothing should be in flight.  Any non-zero
 *                            value means a probe region gained an early
 *                            return without a matching VJP_LEAVE.
 */
int VJPerfRegisteredCount(void);
int VJPerfLeakedSlots(void);

#define VJP_ENTER(slot) do { if (vjPerfActive) VJPerfEnter(slot); } while (0)
#define VJP_LEAVE(slot) do { if (vjPerfActive) VJPerfLeave(slot); } while (0)

#ifdef __cplusplus
}
#endif

#endif /* VJ_PERF_IFACE_H */
