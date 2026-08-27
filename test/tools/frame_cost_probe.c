/* test/tools/frame_cost_probe.c -- what did the expensive frames DO?
 *
 * Times every retro_run() with the monotonic clock and pairs each frame
 * with per-frame work counts: GPU/DSP interpreted-instruction deltas
 * (gpu_exec_opcode_count / dsp_exec_opcode_count), the DSP idle-skip
 * savings (dsp_idle_skip_opcodes: opcodes NOT interpreted), and the
 * vjtrace event-type counters (IRQ asserts/dispatches, GPU_GO, OP list
 * starts/objects/branches, blitter commands -- vjtrace_counters, the
 * same totals trace_probe's --field-csv drains).  Purpose: attribute
 * frame-time spikes (a 110 ms frame on a title whose p50 is 3 ms) to
 * WHAT that frame did more of -- blitter storm, GPU scene rebuild, OP
 * list explosion, IRQ pileup.
 *
 * Output: a CSV (one row per measured frame) to --csv FILE or stdout,
 * plus a --top N report (stderr) ranking the N most expensive frames
 * with their counts next to the median frame's.
 *
 * Timing caveat (docs/agent/testing.md "Measurement hazards"): wall
 * clock needs a quiet host; use the count columns for cross-run
 * comparison and the ms column only for same-run spike attribution.
 * Fixed-frame scripted input is invalid for timing runs -- drive
 * attract mode or a --load-state instead.
 *
 * Needs a TEST_EXPORTS=1 core (opcode counters + vjtrace are only
 * exported there); exits loudly otherwise.
 *
 * Build:
 *   make TEST_EXPORTS=1
 *   cc -O2 -Wall -std=c99 -I. -I./libretro-common/include \
 *      -o test/tools/frame_cost_probe \
 *      test/tools/frame_cost_probe.c test/harness/harness.c -ldl -lm
 *
 * Run:
 *   VJ_EXPECT_BUILD=$(./scripts/build-id.sh) \
 *   test/tools/frame_cost_probe ./virtualjaguar_libretro.dylib rom.jag \
 *      --frames 2000 --warmup 900 --csv /tmp/cost.csv --top 12
 */
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "../harness/harness.h"
#include "src/core/vjtrace.h"    /* event enum + vjtrace_counters_t (outside
                                  * the VJ_TRACE guard by design) */

/* One numeric field per column so the median/top report can iterate. */
typedef enum { F_GPU, F_DSP, F_SKIP, F_IRQA, F_IRQD, F_GGO, F_OPL, F_OPO,
               F_OPB, F_BLT, F__N } field_id;

typedef struct
{
    double   ms;
    uint64_t v[F__N];
} frame_row;

static const char *csv_header =
    "frame,ms,gpu_ops,dsp_ops,dsp_skip_ops,irq_assert,irq_dispatch,"
    "gpu_go,op_list,op_obj,op_branch,blit_cmd";

static int cmp_double(const void *a, const void *b)
{
    double da = *(const double *)a, db = *(const double *)b;
    return (da > db) - (da < db);
}

static int cmp_u64(const void *a, const void *b)
{
    uint64_t da = *(const uint64_t *)a, db = *(const uint64_t *)b;
    return (da > db) - (da < db);
}

static void print_row(FILE *out, const char *label, double ms,
                      const uint64_t *v)
{
    int f;
    fprintf(out, "%8s %9.2f", label, ms);
    for (f = 0; f < F__N; f++)
        fprintf(out, " %9llu", (unsigned long long)v[f]);
    fprintf(out, "\n");
}

int main(int argc, char **argv)
{
    harness_config cfg = HARNESS_CONFIG_DEFAULT;
    void (*arm)(void);
    vjtrace_counters_t *ctr;
    uint32_t *gpu_cnt, *dsp_cnt, *skip_cnt;
    frame_row *rows;
    FILE *csv = stdout;
    const char *csv_path = NULL;
    unsigned warmup = 0, top_n = 10, i;
    int a;

    cfg.frames = 2000;
    for (a = 1; a < argc; a++)
    {
        if (!strcmp(argv[a], "--warmup") && a + 1 < argc)
            warmup = (unsigned)strtoul(argv[++a], NULL, 10);
        else if (!strcmp(argv[a], "--csv") && a + 1 < argc)
            csv_path = argv[++a];
        else if (!strcmp(argv[a], "--top") && a + 1 < argc)
            top_n = (unsigned)strtoul(argv[++a], NULL, 10);
    }
    if (!harness_init_from_args(&cfg, argc, argv))
        return 1;
    if (!harness_load_rom(&cfg))
        return 1;

    arm      = (void (*)(void))harness_dlsym(&cfg, "vjtrace_arm");
    ctr      = (vjtrace_counters_t *)harness_dlsym(&cfg, "vjtrace_counters");
    gpu_cnt  = (uint32_t *)harness_dlsym(&cfg, "gpu_exec_opcode_count");
    dsp_cnt  = (uint32_t *)harness_dlsym(&cfg, "dsp_exec_opcode_count");
    skip_cnt = (uint32_t *)harness_dlsym(&cfg, "dsp_idle_skip_opcodes");
    if (!arm || !ctr || !gpu_cnt || !dsp_cnt)
    {
        fprintf(stderr, "frame_cost_probe: required symbols not exported -- "
                        "build the core with TEST_EXPORTS=1\n");
        return 2;
    }
    arm();   /* vjtrace counters only advance while armed */

    if (csv_path)
    {
        csv = fopen(csv_path, "w");
        if (!csv)
        {
            fprintf(stderr, "cannot open %s for writing\n", csv_path);
            return 1;
        }
    }

    rows = (frame_row *)calloc(cfg.frames, sizeof(frame_row));
    if (!rows)
    {
        fprintf(stderr, "calloc failed for %u rows\n", cfg.frames);
        return 1;
    }

    for (i = 0; i < warmup; i++)
        harness_step(&cfg);

    fprintf(csv, "%s\n", csv_header);
    {
        uint32_t g0 = *gpu_cnt, d0 = *dsp_cnt;
        uint32_t s0 = skip_cnt ? *skip_cnt : 0;
        vjtrace_counters_t c0 = *ctr;
        for (i = 0; i < cfg.frames; i++)
        {
            frame_row *r = &rows[i];
            uint64_t t0 = harness_time_now(), t1;
            int f;
            harness_step(&cfg);
            t1 = harness_time_now();
            r->ms        = harness_time_elapsed_sec(t0, t1) * 1000.0;
            r->v[F_GPU]  = *gpu_cnt - g0;               g0 = *gpu_cnt;
            r->v[F_DSP]  = *dsp_cnt - d0;               d0 = *dsp_cnt;
            r->v[F_SKIP] = skip_cnt ? (uint32_t)(*skip_cnt - s0) : 0;
            if (skip_cnt) s0 = *skip_cnt;
            r->v[F_IRQA] = ctr->ev[VJT_EV_IRQ_ASSERT]    - c0.ev[VJT_EV_IRQ_ASSERT];
            r->v[F_IRQD] = ctr->ev[VJT_EV_IRQ_DISPATCH]  - c0.ev[VJT_EV_IRQ_DISPATCH];
            r->v[F_GGO]  = ctr->ev[VJT_EV_GPU_GO]        - c0.ev[VJT_EV_GPU_GO];
            r->v[F_OPL]  = ctr->ev[VJT_EV_OP_LIST_START] - c0.ev[VJT_EV_OP_LIST_START];
            r->v[F_OPO]  = ctr->ev[VJT_EV_OP_OBJECT]     - c0.ev[VJT_EV_OP_OBJECT];
            r->v[F_OPB]  = ctr->ev[VJT_EV_OP_BRANCH]     - c0.ev[VJT_EV_OP_BRANCH];
            r->v[F_BLT]  = ctr->ev[VJT_EV_BLIT_CMD]      - c0.ev[VJT_EV_BLIT_CMD];
            c0 = *ctr;
            fprintf(csv, "%u,%.3f", warmup + i + 1, r->ms);
            for (f = 0; f < F__N; f++)
                fprintf(csv, ",%llu", (unsigned long long)r->v[f]);
            fprintf(csv, "\n");
        }
    }
    if (csv != stdout)
        fclose(csv);

    /* ---- summary + --top N report (stderr so the CSV can be stdout) ---- */
    {
        unsigned n = cfg.frames;
        double *ms_sorted = (double *)malloc(n * sizeof(double));
        unsigned *order   = (unsigned *)malloc(n * sizeof(unsigned));
        uint64_t *scratch = (uint64_t *)malloc(n * sizeof(uint64_t));
        if (n && ms_sorted && order && scratch)
        {
            uint64_t med_v[F__N];
            double p50, p99, max_ms;
            unsigned over = 0, j, k;
            int f;
            for (i = 0; i < n; i++)
                ms_sorted[i] = rows[i].ms;
            qsort(ms_sorted, n, sizeof(double), cmp_double);
            p50    = ms_sorted[n / 2];
            p99    = ms_sorted[(unsigned)((double)n * 0.99)];
            max_ms = ms_sorted[n - 1];
            for (i = 0; i < n; i++)
                if (rows[i].ms > 1000.0 / 60.0)
                    over++;
            fprintf(stderr, "frame_cost_probe: %u frames (warmup %u)  "
                            "p50=%.2fms p99=%.2fms max=%.2fms  "
                            "over-budget=%u (%.2f%%)\n",
                    n, warmup, p50, p99, max_ms, over, 100.0 * over / n);

            for (f = 0; f < F__N; f++)
            {
                for (i = 0; i < n; i++)
                    scratch[i] = rows[i].v[f];
                qsort(scratch, n, sizeof(uint64_t), cmp_u64);
                med_v[f] = scratch[n / 2];
            }

            /* Partial selection sort: top_n most expensive frames. */
            for (i = 0; i < n; i++)
                order[i] = i;
            if (top_n > n)
                top_n = n;
            for (j = 0; j < top_n; j++)
            {
                unsigned best = j;
                for (k = j + 1; k < n; k++)
                    if (rows[order[k]].ms > rows[order[best]].ms)
                        best = k;
                { unsigned t = order[j]; order[j] = order[best]; order[best] = t; }
            }

            fprintf(stderr, "\n--- top %u most expensive frames vs median ---\n",
                    top_n);
            fprintf(stderr, "%8s %9s %9s %9s %9s %9s %9s %9s %9s %9s %9s %9s\n",
                    "frame", "ms", "gpu_ops", "dsp_ops", "skip_ops", "irq_a",
                    "irq_d", "gpu_go", "op_list", "op_obj", "op_br", "blit");
            print_row(stderr, "median", p50, med_v);
            for (j = 0; j < top_n; j++)
            {
                char label[16];
                snprintf(label, sizeof(label), "%u", warmup + order[j] + 1);
                print_row(stderr, label, rows[order[j]].ms, rows[order[j]].v);
            }
        }
        free(ms_sorted);
        free(order);
        free(scratch);
    }

    free(rows);
    harness_shutdown(&cfg);
    return 0;
}
