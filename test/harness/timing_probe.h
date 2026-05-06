/*
 * test/harness/timing_probe.h — Per-frame timing diagnostic probe.
 *
 * ======================================================================
 * USAGE
 * ======================================================================
 *
 *   #include "harness/harness.h"
 *   #include "harness/timing_probe.h"
 *
 *   timing_probe tp;
 *   if (!timing_probe_init(&tp, &cfg)) return 1;
 *
 *   // Use as frame callback:
 *   cfg.frame_callback = timing_probe_frame_cb;
 *   cfg.frame_callback_data = &tp;
 *   harness_run(&cfg);
 *
 *   timing_probe_finish(&tp);
 *   timing_probe_print_summary(&tp, 0);
 *
 * ======================================================================
 * WHAT THIS MEASURES
 * ======================================================================
 *
 *   Per frame (via perf_counters deltas):
 *   - Halfline callbacks fired (expected: 524 NTSC, 625 PAL)
 *   - VBlank IRQs delivered (expected: 1)
 *   - JERRY IRQs delivered
 *   - GPU-to-68K IRQs delivered
 *   - M68K cycles executed (requested by event system)
 *   - RISC cycles executed (requested by event system)
 *   - Blitter calls
 *
 *   Per frame (measured externally):
 *   - Wall-clock frame duration (microseconds)
 *
 *   Derived:
 *   - Emulated time per frame (halflines * halfline period)
 *   - Speed ratio (emulated / wall = 1.0 means real-time)
 *   - Anomaly detection (frames deviating >10% from median)
 *
 * ======================================================================
 */

#ifndef TIMING_PROBE_H
#define TIMING_PROBE_H

#include "harness.h"

#ifdef __APPLE__
#include <mach/mach_time.h>
#else
#include <time.h>
#endif

#define TIMING_MAX_FRAMES    16384
#define TIMING_NUM_COUNTERS  9

/* Counter indices */
#define TC_HALFLINES    0
#define TC_VBLANK       1
#define TC_JERRY        2
#define TC_GPU_IRQ      3
#define TC_M68K_CYC     4
#define TC_RISC_CYC     5
#define TC_BLITTER      6
#define TC_GPU_ACTIVE   7
#define TC_GPU_OFFERED  8

/* Per-frame snapshot of counter deltas + wall time */
typedef struct {
    unsigned frame;
    unsigned long long counters[TIMING_NUM_COUNTERS];
    double   wall_us;
    double   emu_us;
    double   speed_ratio;
} timing_snapshot;

/* Summary statistics for one counter across all frames */
typedef struct {
    const char *name;
    double mean;
    double stddev;
    unsigned long long min_val;
    unsigned long long max_val;
    double expected;
} timing_counter_stats;

/* Overall summary */
typedef struct {
    unsigned total_frames;
    timing_counter_stats counters[TIMING_NUM_COUNTERS];
    double mean_wall_us;
    double stddev_wall_us;
    double mean_speed_ratio;
    unsigned anomaly_count;
    int is_pal;
} timing_summary;

/* Resolved perf_counters_find pointers */
typedef struct {
    unsigned long long *ptrs[TIMING_NUM_COUNTERS];
    unsigned long long prev[TIMING_NUM_COUNTERS];
} timing_counter_state;

typedef struct {
    harness_config     *cfg;
    timing_counter_state cstate;
    timing_snapshot    *frames;
    unsigned            frame_count;
    unsigned            frame_capacity;
    timing_summary      summary;
    uint64_t            last_wall_tick;
    int                 is_pal;
#ifdef __APPLE__
    mach_timebase_info_data_t timebase;
#endif
} timing_probe;

/* Initialize — resolves perf counter symbols.
 * Core must be built with BENCH_PROFILE=1 + TEST_EXPORTS=1.
 * Returns false if critical counters missing. */
bool timing_probe_init(timing_probe *tp, harness_config *cfg);

/* Per-frame callback (matches harness_frame_cb signature).
 * Pass timing_probe* as userdata. */
bool timing_probe_frame_cb(void *userdata, unsigned frame);

/* Finalize — compute summary statistics after all frames. */
void timing_probe_finish(timing_probe *tp);

/* Print summary in human or JSON format. */
void timing_probe_print_summary(const timing_probe *tp, int json);

/* Print per-frame CSV to stdout. */
void timing_probe_print_csv(const timing_probe *tp);

/* Clean up allocated memory. */
void timing_probe_destroy(timing_probe *tp);

#endif /* TIMING_PROBE_H */
