/* test/tools/m68k_pc_histogram.c -- where is the 68K actually spending
 * a frame?
 *
 * Reads the halfline-rate PC sampler (m68kPCSample, BENCH_PROFILE core)
 * and prints the hottest PCs over a window.  A frame loop that is
 * waiting on something spends nearly all its samples in a handful of
 * spin-loop addresses, which names the wait: GPU completion, the DSP
 * handshake, a field-synchronised list adopt, or a tick gate.
 *
 * Build:
 *   cc -O2 -Wall -std=c99 -I./libretro-common/include \
 *      -o test/tools/m68k_pc_histogram test/tools/m68k_pc_histogram.c \
 *      test/harness/harness.c -ldl -lm
 */
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include "../harness/harness.h"

#define NBUCKET 4096u
#define RING    0x2000u

static uint32_t *sample;
static uint32_t *sidx;
static uint32_t last_idx;

static uint32_t hpc[NBUCKET];
static uint32_t hcount[NBUCKET];
static uint32_t total;

static void bump(uint32_t pc)
{
    uint32_t h = (pc * 2654435761u) % NBUCKET, i;
    for (i = 0; i < NBUCKET; i++)
    {
        uint32_t k = (h + i) % NBUCKET;
        if (hcount[k] && hpc[k] != pc)
            continue;
        hpc[k] = pc;
        hcount[k]++;
        total++;
        return;
    }
}

static int cmp(const void *a, const void *b)
{
    uint32_t ia = *(const uint32_t *)a, ib = *(const uint32_t *)b;
    return (hcount[ib] > hcount[ia]) - (hcount[ib] < hcount[ia]);
}

static bool on_frame(void *ud, unsigned frame)
{
    uint32_t cur = *sidx, n, i;
    (void)ud; (void)frame;
    n = cur - last_idx;
    if (n > RING) n = RING;
    for (i = 0; i < n; i++)
        bump(sample[(cur - n + i) & (RING - 1)]);
    last_idx = cur;
    return true;
}

int main(int argc, char **argv)
{
    harness_config cfg = HARNESS_CONFIG_DEFAULT;
    uint32_t order[NBUCKET];
    unsigned i, shown = 0, topn = 24;
    cfg.frames = 900;
    if (!harness_init_from_args(&cfg, argc, argv))
        return 1;
    cfg.frame_callback = on_frame;
    if (!harness_load_rom(&cfg))
        return 1;
    sample = (uint32_t *)harness_dlsym(&cfg, getenv("PC_GPU") ? "gpuPCSample" : "m68kPCSample");
    sidx   = (uint32_t *)harness_dlsym(&cfg, "m68kPCSampleIdx");
    if (!sample || !sidx)
    {
        fprintf(stderr, "sampler not exported (need BENCH_PROFILE core)\n");
        return 1;
    }
    harness_run(&cfg);
    for (i = 0; i < NBUCKET; i++) order[i] = i;
    qsort(order, NBUCKET, sizeof(order[0]), cmp);
    printf("total samples=%u\n", total);
    {
        /* Guard the parse: atoi() on PC_TOP=0, a negative value, or a
         * non-numeric value silently becomes 0 (prints no rows -- reads
         * exactly like "no samples were taken", a confusing zero this tool
         * can already produce for a real reason: a core built without
         * BENCH_PROFILE) or, cast to unsigned, an enormous topn (dumps every
         * bucket). strtoul() with endptr/errno validation rejects both
         * non-numeric input and out-of-range values instead of coercing them,
         * and the result is clamped to [1, NBUCKET] so a huge PC_TOP can't
         * walk past the bucket table either. */
        const char *e = getenv("PC_TOP");
        if (e && *e != '\0')
        {
            char *end = NULL;
            unsigned long v;
            /* strtoul() accepts a leading '-' and silently wraps it into a
             * huge unsigned value (the C standard's documented behaviour,
             * not a bug) -- reject that explicitly rather than letting the
             * clamp below turn "-1" into "NBUCKET" without a word. */
            if (e[0] == '-')
                fprintf(stderr,
                        "PC_TOP=%s ignored (must be an integer in [1, %u]); using default %u\n",
                        e, NBUCKET, topn);
            else
            {
                errno = 0;
                v = strtoul(e, &end, 10);
                if (end == e || *end != '\0' || errno == ERANGE || v == 0)
                    fprintf(stderr,
                            "PC_TOP=%s ignored (must be an integer in [1, %u]); using default %u\n",
                            e, NBUCKET, topn);
                else
                {
                    if (v > NBUCKET) v = NBUCKET;
                    topn = (unsigned)v;
                }
            }
        }
    }
    for (i = 0; i < NBUCKET && shown < topn; i++)
        if (hcount[order[i]])
        {
            printf("  $%06X  %7u  %5.1f%%\n", hpc[order[i]],
                   hcount[order[i]], 100.0 * hcount[order[i]] / total);
            shown++;
        }
    harness_shutdown(&cfg);
    return 0;
}
