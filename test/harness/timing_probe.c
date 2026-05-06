/*
 * test/harness/timing_probe.c — Per-frame timing diagnostic probe.
 */

#include "timing_probe.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

static const char *counter_names[TIMING_NUM_COUNTERS] = {
    "timing_halfline_callbacks",
    "timing_vblank_irqs",
    "timing_jerry_irqs",
    "timing_gpu_irqs_to_68k",
    "timing_m68k_cycles",
    "timing_risc_cycles",
    "blitter_calls"
};

static const char *counter_labels[TIMING_NUM_COUNTERS] = {
    "Halflines",
    "VBlank IRQs",
    "JERRY IRQs",
    "GPU->68K IRQs",
    "M68K cycles",
    "RISC cycles",
    "Blitter calls"
};

/* NTSC: 524 halflines/frame, PAL: 625 halflines/frame */
#define NTSC_HALFLINES  524
#define PAL_HALFLINES   625

/* Halfline period in microseconds */
#define NTSC_HALFLINE_US  31.777777777
#define PAL_HALFLINE_US   32.0

/* Expected M68K cycles per frame (halflines * halfline_us / m68k_cycle_us) */
#define M68K_CYCLE_US     0.07521368396
#define NTSC_M68K_PER_FRAME  ((unsigned long long)(NTSC_HALFLINES * NTSC_HALFLINE_US / M68K_CYCLE_US))
#define PAL_M68K_PER_FRAME   ((unsigned long long)(PAL_HALFLINES  * PAL_HALFLINE_US  / M68K_CYCLE_US))

/* RISC cycles per frame */
#define RISC_CYCLE_US     0.03760684198
#define NTSC_RISC_PER_FRAME  ((unsigned long long)(NTSC_HALFLINES * NTSC_HALFLINE_US / RISC_CYCLE_US))
#define PAL_RISC_PER_FRAME   ((unsigned long long)(PAL_HALFLINES  * PAL_HALFLINE_US  / RISC_CYCLE_US))

/* ---------------------------------------------------------------- */

static uint64_t wall_now(void)
{
#ifdef __APPLE__
    return mach_absolute_time();
#else
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ULL + (uint64_t)ts.tv_nsec;
#endif
}

static double wall_elapsed_us(const timing_probe *tp, uint64_t start, uint64_t end)
{
#ifdef __APPLE__
    uint64_t elapsed = end - start;
    return (double)elapsed * (double)tp->timebase.numer /
           (double)tp->timebase.denom / 1000.0;
#else
    (void)tp;
    return (double)(end - start) / 1000.0;
#endif
}

/* ---------------------------------------------------------------- */

bool timing_probe_init(timing_probe *tp, harness_config *cfg)
{
    int i;
    unsigned long long *(*pfind)(const char *);

    memset(tp, 0, sizeof(*tp));
    tp->cfg = cfg;

#ifdef __APPLE__
    mach_timebase_info(&tp->timebase);
#endif

    pfind = (unsigned long long *(*)(const char *))
            harness_dlsym(cfg, "perf_counters_find");
    if (!pfind) {
        fprintf(stderr, "timing_probe: perf_counters_find not found "
                "(build core with BENCH_PROFILE=1 TEST_EXPORTS=1)\n");
        return false;
    }

    for (i = 0; i < TIMING_NUM_COUNTERS; i++) {
        tp->cstate.ptrs[i] = pfind(counter_names[i]);
        tp->cstate.prev[i] = 0;
    }

    /* Halflines counter is critical */
    if (!tp->cstate.ptrs[TC_HALFLINES]) {
        fprintf(stderr, "timing_probe: timing_halfline_callbacks counter "
                "not found\n");
        return false;
    }

    /* Detect PAL from core option */
    tp->is_pal = 0;
    for (i = 0; i < (int)cfg->num_options; i++) {
        if (strcmp(cfg->options[i].key, "virtualjaguar_pal") == 0 &&
            strcmp(cfg->options[i].value, "enabled") == 0) {
            tp->is_pal = 1;
            break;
        }
    }

    tp->frame_capacity = TIMING_MAX_FRAMES;
    tp->frames = (timing_snapshot *)calloc(tp->frame_capacity,
                                           sizeof(timing_snapshot));
    if (!tp->frames) {
        fprintf(stderr, "timing_probe: allocation failed\n");
        return false;
    }

    return true;
}

static void snapshot_counters(timing_probe *tp)
{
    int i;
    for (i = 0; i < TIMING_NUM_COUNTERS; i++) {
        if (tp->cstate.ptrs[i])
            tp->cstate.prev[i] = *tp->cstate.ptrs[i];
        else
            tp->cstate.prev[i] = 0;
    }
}

bool timing_probe_frame_cb(void *userdata, unsigned frame)
{
    timing_probe *tp = (timing_probe *)userdata;
    timing_snapshot *snap;
    uint64_t now;
    double halfline_us;
    int i;

    now = wall_now();

    if (frame == 1) {
        /* First frame: just baseline the counters */
        snapshot_counters(tp);
        tp->last_wall_tick = now;
        return true;
    }

    if (tp->frame_count >= tp->frame_capacity)
        return true; /* silently stop recording */

    snap = &tp->frames[tp->frame_count];
    snap->frame = frame;
    snap->wall_us = wall_elapsed_us(tp, tp->last_wall_tick, now);

    for (i = 0; i < TIMING_NUM_COUNTERS; i++) {
        if (tp->cstate.ptrs[i]) {
            unsigned long long cur = *tp->cstate.ptrs[i];
            snap->counters[i] = cur - tp->cstate.prev[i];
            tp->cstate.prev[i] = cur;
        } else {
            snap->counters[i] = 0;
        }
    }

    halfline_us = tp->is_pal ? PAL_HALFLINE_US : NTSC_HALFLINE_US;
    snap->emu_us = (double)snap->counters[TC_HALFLINES] * halfline_us;
    snap->speed_ratio = (snap->wall_us > 0.0) ?
                        (snap->emu_us / snap->wall_us) : 0.0;

    tp->last_wall_tick = now;
    tp->frame_count++;

    return true;
}

void timing_probe_finish(timing_probe *tp)
{
    unsigned i, f;
    double expected_halflines, expected_m68k, expected_risc;
    timing_summary *s = &tp->summary;

    s->total_frames = tp->frame_count;
    s->is_pal = tp->is_pal;

    if (tp->frame_count == 0)
        return;

    expected_halflines = tp->is_pal ? PAL_HALFLINES : NTSC_HALFLINES;
    expected_m68k = tp->is_pal ? PAL_M68K_PER_FRAME : NTSC_M68K_PER_FRAME;
    expected_risc = tp->is_pal ? PAL_RISC_PER_FRAME : NTSC_RISC_PER_FRAME;

    /* Compute per-counter statistics */
    for (i = 0; i < TIMING_NUM_COUNTERS; i++) {
        double sum = 0.0, sum_sq = 0.0;
        unsigned long long cmin = (unsigned long long)-1, cmax = 0;

        s->counters[i].name = counter_labels[i];

        for (f = 0; f < tp->frame_count; f++) {
            unsigned long long v = tp->frames[f].counters[i];
            sum += (double)v;
            sum_sq += (double)v * (double)v;
            if (v < cmin) cmin = v;
            if (v > cmax) cmax = v;
        }

        s->counters[i].mean = sum / tp->frame_count;
        s->counters[i].stddev = sqrt(
            (sum_sq / tp->frame_count) -
            (s->counters[i].mean * s->counters[i].mean));
        s->counters[i].min_val = cmin;
        s->counters[i].max_val = cmax;
        s->counters[i].expected = -1.0;
    }

    s->counters[TC_HALFLINES].expected = expected_halflines;
    s->counters[TC_VBLANK].expected = 1.0;
    s->counters[TC_M68K_CYC].expected = expected_m68k;
    s->counters[TC_RISC_CYC].expected = expected_risc;

    /* Wall time + speed ratio stats */
    {
        double wsum = 0.0, wsum_sq = 0.0;
        double rsum = 0.0;
        for (f = 0; f < tp->frame_count; f++) {
            wsum += tp->frames[f].wall_us;
            wsum_sq += tp->frames[f].wall_us * tp->frames[f].wall_us;
            rsum += tp->frames[f].speed_ratio;
        }
        s->mean_wall_us = wsum / tp->frame_count;
        s->stddev_wall_us = sqrt(
            (wsum_sq / tp->frame_count) -
            (s->mean_wall_us * s->mean_wall_us));
        s->mean_speed_ratio = rsum / tp->frame_count;
    }

    /* Anomaly detection: frames where halflines deviate >10% from expected */
    s->anomaly_count = 0;
    for (f = 0; f < tp->frame_count; f++) {
        double hl = (double)tp->frames[f].counters[TC_HALFLINES];
        double ratio = hl / expected_halflines;
        if (ratio < 0.9 || ratio > 1.1)
            s->anomaly_count++;
    }
}

void timing_probe_print_summary(const timing_probe *tp, int json)
{
    unsigned i;
    const timing_summary *s = &tp->summary;

    if (json) {
        printf("{\"total_frames\":%u,\"mode\":\"%s\","
               "\"mean_wall_us\":%.1f,\"stddev_wall_us\":%.1f,"
               "\"mean_speed_ratio\":%.4f,\"anomaly_count\":%u,"
               "\"counters\":[",
               s->total_frames, s->is_pal ? "PAL" : "NTSC",
               s->mean_wall_us, s->stddev_wall_us,
               s->mean_speed_ratio, s->anomaly_count);
        for (i = 0; i < TIMING_NUM_COUNTERS; i++) {
            if (i > 0) printf(",");
            printf("{\"name\":\"%s\",\"mean\":%.1f,\"stddev\":%.1f,"
                   "\"min\":%llu,\"max\":%llu",
                   s->counters[i].name,
                   s->counters[i].mean, s->counters[i].stddev,
                   s->counters[i].min_val, s->counters[i].max_val);
            if (s->counters[i].expected >= 0.0)
                printf(",\"expected\":%.0f", s->counters[i].expected);
            printf("}");
        }
        printf("]}\n");
        return;
    }

    /* Human-readable output */
    printf("\n");
    printf("Frame Timing Diagnostic (%s, %u frames)\n",
           s->is_pal ? "PAL" : "NTSC", s->total_frames);
    printf("======================================================\n\n");

    printf("Per-frame counter averages:\n");
    for (i = 0; i < TIMING_NUM_COUNTERS; i++) {
        char status[16] = "";
        if (s->counters[i].expected >= 0.0) {
            double ratio = s->counters[i].mean / s->counters[i].expected;
            if (ratio > 1.05 || ratio < 0.95)
                snprintf(status, sizeof(status), "  %.2fx!", ratio);
            else
                snprintf(status, sizeof(status), "  OK");
        }
        printf("  %-16s %10.1f +/- %-8.1f  [%llu .. %llu]",
               s->counters[i].name,
               s->counters[i].mean, s->counters[i].stddev,
               s->counters[i].min_val, s->counters[i].max_val);
        if (s->counters[i].expected >= 0.0)
            printf("  (expected: %.0f)%s", s->counters[i].expected, status);
        printf("\n");
    }

    printf("\nFrame time:   %.2f ms +/- %.2f ms  (expected: %.2f ms)\n",
           s->mean_wall_us / 1000.0, s->stddev_wall_us / 1000.0,
           s->is_pal ? 20.0 : 16.67);
    printf("Speed ratio:  %.4fx  (1.0 = real-time)\n",
           s->mean_speed_ratio);

    printf("\nAnomalies: %u of %u frames (%.1f%%)\n",
           s->anomaly_count, s->total_frames,
           s->total_frames > 0 ?
           100.0 * s->anomaly_count / s->total_frames : 0.0);

    if (s->anomaly_count == 0) {
        printf("\nNo timing anomalies detected in the frame loop.\n"
               "If game appears too fast, the issue is likely in game\n"
               "logic response to timer registers, not in frame pacing.\n");
    } else if (s->counters[TC_HALFLINES].mean > 1.5 *
               s->counters[TC_HALFLINES].expected) {
        printf("\nHalfline count is ~%.1fx expected — frame boundary\n"
               "detection may be double-counting halflines.\n",
               s->counters[TC_HALFLINES].mean /
               s->counters[TC_HALFLINES].expected);
    } else {
        printf("\nSome frames deviate from expected timing.\n"
               "Use --csv for per-frame data to identify the pattern.\n");
    }
}

void timing_probe_print_csv(const timing_probe *tp)
{
    unsigned f, i;

    /* Header */
    printf("frame,wall_us,emu_us,speed_ratio");
    for (i = 0; i < TIMING_NUM_COUNTERS; i++)
        printf(",%s", counter_labels[i]);
    printf("\n");

    /* Data rows */
    for (f = 0; f < tp->frame_count; f++) {
        const timing_snapshot *snap = &tp->frames[f];
        printf("%u,%.1f,%.1f,%.4f",
               snap->frame, snap->wall_us, snap->emu_us, snap->speed_ratio);
        for (i = 0; i < TIMING_NUM_COUNTERS; i++)
            printf(",%llu", snap->counters[i]);
        printf("\n");
    }
}

void timing_probe_destroy(timing_probe *tp)
{
    if (tp->frames) {
        free(tp->frames);
        tp->frames = NULL;
    }
    tp->frame_count = 0;
}
