/* test/tools/test_doom_pace_gate.c -- timing-campaign S0 Doom pace gate
 * (docs/timing-campaign-plan.md S0 / 5.1; issues #408 #401 #434).
 *
 * Runs the Jaguar DOOM attract demo headless and measures, over the
 * demo steady state (default frames 600..1500):
 *
 *   - mean fields/flip and the gap HISTOGRAM (fields between presented
 *     image changes -- the distribution is the sensitive instrument, 4.0
 *     silicon vs a delta function at 2 here), and per-300-field flip
 *     counts as a liveness floor;
 *   - GPU BUSY-TIME per flip: the core's TEST_EXPORTS gpu_busy_* census
 *     (src/tom/gpu.c) splits every GPU cycle into "spin" (PC inside one of
 *     Doom's three spin loops: idle mailbox poll, display-list adopt
 *     handshake, the r_phase9 2-field gate) and "work".  Below the 2-field
 *     floor present rate cannot move at all, so busy-time is the only
 *     instrument that sees a timing model working (#401, plan 2.1.3).
 *
 * The silicon bar is 4.0 fields/flip (15.01 flips/s, #434, band
 * [3.5,4.5]).  We present ~2.00 today, so this is a TRACKING gate, not a
 * target gate: the "expected" values live in the baseline file
 * (test/tools/timing_gate_baseline.txt) beside the silicon target.
 *
 *   PASS  no regression; if the numbers moved toward the target the gate
 *         says so (MOVED-TOWARD-TARGET) and asks for a re-baseline.
 *   FAIL  mean fields/flip fell below baseline - eps (moved AWAY from the
 *         silicon bar), or overshot the band top, or more 1-field gaps
 *         than baseline + tolerance (mass below the game's own 2-field
 *         floor), or busy-time/flip fell below baseline - tol, or any
 *         300-field window breached the liveness floor.
 *   exit 77  ROM absent (SKIP -- never a silent pass).
 *
 * Last stdout line starting "DOOM_PACE_RESULT " is machine-readable.
 *
 * Needs a TEST_EXPORTS=1 core (gpu_busy_* is compiled only under
 * VJ_TRACE).  Run with VJ_EXPECT_BUILD=$(./scripts/build-id.sh).
 *
 * Usage:
 *   test_doom_pace_gate <core> <rom> [--baseline FILE] [--win-lo F]
 *       [--win-hi F] [--csv FILE] [--measure-only] [--print-baseline]
 *       [--no-arm (overhead timing only)] [harness options: --option K=V ...]
 *
 * Build:
 *   cc -O2 -Wall -std=c99 -I. -I./libretro-common/include \
 *      -o test/tools/test_doom_pace_gate test/tools/test_doom_pace_gate.c \
 *      test/harness/harness.c -ldl -lm
 */
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <stddef.h>
#include "../harness/harness.h"

#define MAX_FIELDS   20000
#define NWIN         3
#define HIST_BUCKETS 8          /* gap 1..7, 8+ */

/* Doom (Evil Unleashed 1994) GPU spin loops, from docs/doom-render-cost-census.md
 * section 1 (disassembled at runtime).  [lo, hi) with hi = last
 * instruction start + 2. */
static const char  *win_name[NWIN] = { "idle_mailbox", "dl_adopt", "r_phase9_gate" };
static const uint32_t win_lo[NWIN] = { 0xF0308Au, 0xF03672u, 0xF0362Cu };
static const uint32_t win_hi[NWIN] = { 0xF030A0u, 0xF0369Cu, 0xF0365Au };

/* ---- baseline file -------------------------------------------------- */
typedef struct
{
    double silicon_target, band_lo, band_hi;
    double mean, mean_eps;
    double hist1, hist1_tol;
    double busy_mean, busy_tol_rel;
    double live_floor;
    int have_mean, have_hist1, have_busy;
} baseline_t;

static void baseline_defaults(baseline_t *b)
{
    memset(b, 0, sizeof(*b));
    b->silicon_target = 4.0;
    b->band_lo = 3.5;
    b->band_hi = 4.5;
    b->mean_eps = 0.05;
    b->hist1_tol = 5;
    b->busy_tol_rel = 0.05;
    b->live_floor = 30;
}

static int baseline_load(baseline_t *b, const char *path)
{
    char line[256];
    FILE *f = fopen(path, "r");
    if (!f)
        return 0;
    while (fgets(line, sizeof(line), f))
    {
        char *eq;
        double v;
        if (line[0] == '#' || line[0] == '\n')
            continue;
        eq = strchr(line, '=');
        if (!eq)
            continue;
        *eq = 0;
        v = atof(eq + 1);
        if (!strcmp(line, "doom_silicon_fields_per_flip")) b->silicon_target = v;
        else if (!strcmp(line, "doom_band_lo")) b->band_lo = v;
        else if (!strcmp(line, "doom_band_hi")) b->band_hi = v;
        else if (!strcmp(line, "doom_baseline_mean_fields_per_flip")) { b->mean = v; b->have_mean = 1; }
        else if (!strcmp(line, "doom_mean_eps")) b->mean_eps = v;
        else if (!strcmp(line, "doom_baseline_gap1_count")) { b->hist1 = v; b->have_hist1 = 1; }
        else if (!strcmp(line, "doom_gap1_tolerance")) b->hist1_tol = v;
        else if (!strcmp(line, "doom_baseline_busy_fields_per_flip")) { b->busy_mean = v; b->have_busy = 1; }
        else if (!strcmp(line, "doom_busy_tolerance_rel")) b->busy_tol_rel = v;
        else if (!strcmp(line, "doom_liveness_floor_flips_per_300")) b->live_floor = v;
    }
    fclose(f);
    return 1;
}

/* ---- capture -------------------------------------------------------- */
static uint64_t last_hash;
static int      cur_flip;
static unsigned nfields;
static uint8_t  flip_at[MAX_FIELDS + 2];
static uint64_t work_at[MAX_FIELDS + 2], total_at[MAX_FIELDS + 2];
static uint64_t winc_at[MAX_FIELDS + 2][NWIN];

static uint64_t *p_work, *p_total, *p_win;

/* Same FNV subsample as present_rate_probe.c so numbers stay comparable
 * with every earlier fields/flip figure on record. */
static void on_video(void *ud, const void *data, unsigned w, unsigned h,
                     size_t pitch)
{
    const uint8_t *p = (const uint8_t *)data;
    uint64_t hv = 1469598103934665603ull;
    unsigned y, x;
    (void)ud;
    if (!p)
        return;
    for (y = 0; y < h; y += 8)
    {
        const uint8_t *row = p + y * pitch;
        for (x = 0; x < w * 4; x += 16)
        {
            hv ^= row[x];
            hv *= 1099511628211ull;
        }
    }
    if (hv != last_hash)
    {
        last_hash = hv;
        cur_flip = 1;
    }
}

static bool on_frame(void *ud, unsigned frame)
{
    unsigned i;
    (void)ud;
    if (frame > MAX_FIELDS)
        return false;
    flip_at[frame]  = (uint8_t)cur_flip;
    work_at[frame]  = *p_work;
    total_at[frame] = *p_total;
    for (i = 0; i < NWIN; i++)
        winc_at[frame][i] = p_win[i];
    cur_flip = 0;
    nfields = frame;
    return true;
}

static int cmp_dbl(const void *a, const void *b)
{
    double x = *(const double *)a, y = *(const double *)b;
    return (x > y) - (x < y);
}

int main(int argc, char **argv)
{
    harness_config cfg = HARNESS_CONFIG_DEFAULT;
    baseline_t bl;
    const char *baseline_path = "test/tools/timing_gate_baseline.txt";
    const char *csv_path = NULL;
    unsigned lo = 600, hi = 1500, a, i, f;
    int measure_only = 0, print_baseline = 0, fail = 0, moved_toward = 0, no_arm = 0;
    void (*arm)(int);
    void (*reset)(void);
    void (*setwin)(uint32_t, uint32_t, uint32_t);
    uint64_t t_lo, t_hi;
    unsigned nflips = 0, first_flip = 0, last_flip = 0, prev_flip = 0;
    unsigned hist[HIST_BUCKETS + 1];
    unsigned ngaps = 0, livewin[8], nlive = 0, min_live = 0xFFFFFFFFu;
    double mean, busy_mean = 0, busy_p50 = 0, busy_p90 = 0, busy_max = 0;
    double cyc_per_field, work_pct, spin_pct[NWIN];
    double *busy_v = NULL;
    unsigned nbusy = 0;
    char *build;
    FILE *csv = NULL;

    baseline_defaults(&bl);
    cfg.frames = 1500;
    cfg.quiet = 1;
    for (a = 1; (int)a < argc; a++)
    {
        if (!strcmp(argv[a], "--baseline") && (int)a + 1 < argc) baseline_path = argv[++a];
        else if (!strcmp(argv[a], "--win-lo") && (int)a + 1 < argc) lo = (unsigned)strtoul(argv[++a], NULL, 10);
        else if (!strcmp(argv[a], "--win-hi") && (int)a + 1 < argc) hi = (unsigned)strtoul(argv[++a], NULL, 10);
        else if (!strcmp(argv[a], "--csv") && (int)a + 1 < argc) csv_path = argv[++a];
        else if (!strcmp(argv[a], "--measure-only")) measure_only = 1;
        else if (!strcmp(argv[a], "--print-baseline")) print_baseline = 1;
        else if (!strcmp(argv[a], "--no-arm")) no_arm = 1;
    }
    if (!harness_init_from_args(&cfg, argc, argv))
        return 2;
    if (!cfg.rom_path)
    {
        fprintf(stderr, "usage: test_doom_pace_gate <core> <rom> [--baseline FILE] "
                        "[--win-lo F] [--win-hi F] [--csv FILE] [--measure-only]\n");
        return 2;
    }
    {
        FILE *rf = fopen(cfg.rom_path, "rb");
        if (!rf)
        {
            printf("SKIP: ROM not present (%s)\n", cfg.rom_path);
            return 77;
        }
        fclose(rf);
    }
    if (hi > MAX_FIELDS || lo >= hi || lo < 2)
    {
        fprintf(stderr, "bad window %u..%u\n", lo, hi);
        return 2;
    }
    if (cfg.frames < hi)
        cfg.frames = hi;
    if (!baseline_load(&bl, baseline_path) && !measure_only)
    {
        fprintf(stderr, "cannot read baseline %s (use --measure-only)\n", baseline_path);
        return 2;
    }
    /* Pin the config: the harness defaults to the FAST blitter, the shipped
     * core to ACCURATE.  This gate matches the census (fast); override with
     * --option virtualjaguar_usefastblitter=disabled.  Echoed below. */
    {
        int have = 0;
        for (i = 0; i < cfg.num_options; i++)
            if (!strcmp(cfg.options[i].key, "virtualjaguar_usefastblitter"))
                have = 1;
        if (!have)
            harness_set_option(&cfg, "virtualjaguar_usefastblitter", "enabled");
    }

    if (!harness_load_rom(&cfg))
        return 2;
    arm    = (void (*)(int))harness_dlsym(&cfg, "gpu_busy_arm");
    reset  = (void (*)(void))harness_dlsym(&cfg, "gpu_busy_reset");
    setwin = (void (*)(uint32_t, uint32_t, uint32_t))harness_dlsym(&cfg, "gpu_busy_set_window");
    p_work  = (uint64_t *)harness_dlsym(&cfg, "gpu_busy_work_cycles");
    p_total = (uint64_t *)harness_dlsym(&cfg, "gpu_busy_total_cycles");
    p_win   = (uint64_t *)harness_dlsym(&cfg, "gpu_busy_win_cycles");
    if (!arm || !reset || !setwin || !p_work || !p_total || !p_win)
    {
        fprintf(stderr, "test_doom_pace_gate: gpu_busy_* not exported -- "
                        "build the core with TEST_EXPORTS=1\n");
        harness_shutdown(&cfg);
        return 2;
    }
    for (i = 0; i < NWIN; i++)
        setwin(i, win_lo[i], win_hi[i]);
    reset();
    /* --no-arm: leave the census disarmed (counters stay zero) so the
     * same binary can be timed with and without the counter live. */
    arm(no_arm ? 0 : 1);

    cfg.frame_callback = on_frame;
    cfg.video_callback = on_video;
    harness_run(&cfg);
    harness_shutdown(&cfg);
    if (nfields < hi)
    {
        fprintf(stderr, "run stopped at field %u (< window end %u)\n", nfields, hi);
        return 2;
    }

    /* ---- analysis over (lo, hi] ------------------------------------ */
    memset(hist, 0, sizeof(hist));
    busy_v = (double *)calloc(hi - lo + 2, sizeof(double));
    t_lo = total_at[lo];
    t_hi = total_at[hi];
    cyc_per_field = (double)(t_hi - t_lo) / (double)(hi - lo);
    work_pct = (t_hi > t_lo) ? 100.0 * (double)(work_at[hi] - work_at[lo]) / (double)(t_hi - t_lo) : 0.0;
    for (i = 0; i < NWIN; i++)
        spin_pct[i] = (t_hi > t_lo)
            ? 100.0 * (double)(winc_at[hi][i] - winc_at[lo][i]) / (double)(t_hi - t_lo) : 0.0;

    for (f = lo + 1; f <= hi; f++)
    {
        if (!flip_at[f])
            continue;
        nflips++;
        if (prev_flip)
        {
            unsigned g = f - prev_flip;
            hist[g > HIST_BUCKETS ? HIST_BUCKETS : g]++;
            ngaps++;
            if (cyc_per_field > 0)
                busy_v[nbusy++] = (double)(work_at[f] - work_at[prev_flip]) / cyc_per_field;
        }
        else
            first_flip = f;
        prev_flip = f;
        last_flip = f;
    }
    mean = (ngaps > 0) ? (double)(last_flip - first_flip) / (double)ngaps : 0.0;
    if (nbusy)
    {
        double sum = 0;
        qsort(busy_v, nbusy, sizeof(double), cmp_dbl);
        for (i = 0; i < nbusy; i++)
            sum += busy_v[i];
        busy_mean = sum / nbusy;
        busy_p50 = busy_v[nbusy / 2];
        busy_p90 = busy_v[(unsigned)((double)nbusy * 0.9)];
        busy_max = busy_v[nbusy - 1];
    }
    /* Liveness: flips per 300-field window (plan: floor 30; healthy ~100-150). */
    for (f = lo; f + 300 <= hi && nlive < 8; f += 300)
    {
        unsigned c = 0, k;
        for (k = f + 1; k <= f + 300; k++)
            c += flip_at[k];
        livewin[nlive++] = c;
        if (c < min_live)
            min_live = c;
    }
    if (nlive == 0)
        min_live = nflips;

    if (csv_path)
    {
        csv = fopen(csv_path, "w");
        if (csv)
        {
            fprintf(csv, "field,flip,work_cyc,total_cyc,spin_w0,spin_w1,spin_w2\n");
            for (f = lo + 1; f <= hi; f++)
                fprintf(csv, "%u,%d,%llu,%llu,%llu,%llu,%llu\n", f, flip_at[f],
                        (unsigned long long)(work_at[f] - work_at[f - 1]),
                        (unsigned long long)(total_at[f] - total_at[f - 1]),
                        (unsigned long long)(winc_at[f][0] - winc_at[f - 1][0]),
                        (unsigned long long)(winc_at[f][1] - winc_at[f - 1][1]),
                        (unsigned long long)(winc_at[f][2] - winc_at[f - 1][2]));
            fclose(csv);
        }
    }

    /* ---- human report ---------------------------------------------- */
    build = getenv("VJ_EXPECT_BUILD");
    printf("Doom pace gate: %s\n", cfg.rom_path);
    printf("  window fields %u..%u  build=%s\n", lo, hi, build ? build : "(unset)");
    printf("  options:");
    for (i = 0; i < cfg.num_options; i++)
        printf(" %s=%s", cfg.options[i].key, cfg.options[i].value);
    printf("\n");
    printf("  flips=%u gaps=%u  mean fields/flip=%.3f  (silicon %.2f, band [%.1f,%.1f])\n",
           nflips, ngaps, mean, bl.silicon_target, bl.band_lo, bl.band_hi);
    printf("  gap histogram (fields: count):");
    for (i = 1; i <= HIST_BUCKETS; i++)
        printf(" %u%s:%u", i, i == HIST_BUCKETS ? "+" : "", hist[i]);
    printf("\n  per-300-field flips (liveness floor %.0f):", bl.live_floor);
    for (i = 0; i < nlive; i++)
        printf(" %u", livewin[i]);
    printf("\n  GPU: %.0f cycles/field  work=%.1f%%", cyc_per_field, work_pct);
    for (i = 0; i < NWIN; i++)
        printf("  %s=%.1f%%", win_name[i], spin_pct[i]);
    printf("\n  GPU busy-time per flip (fields): mean=%.3f p50=%.3f p90=%.3f max=%.3f\n",
           busy_mean, busy_p50, busy_p90, busy_max);

    /* machine-readable */
    printf("DOOM_PACE_RESULT build=%s win=%u-%u flips=%u mean_fpf=%.4f",
           build ? build : "-", lo, hi, nflips, mean);
    for (i = 1; i <= HIST_BUCKETS; i++)
        printf(" gap%u=%u", i, hist[i]);
    printf(" min_live300=%u busy_fpf_mean=%.4f busy_fpf_p50=%.4f busy_fpf_p90=%.4f"
           " busy_fpf_max=%.4f work_pct=%.2f cyc_per_field=%.0f silicon_fpf=%.2f\n",
           min_live, busy_mean, busy_p50, busy_p90, busy_max, work_pct,
           cyc_per_field, bl.silicon_target);

    if (print_baseline)
    {
        printf("# --- baseline lines for test/tools/timing_gate_baseline.txt ---\n");
        printf("doom_baseline_mean_fields_per_flip=%.3f\n", mean);
        printf("doom_baseline_gap1_count=%u\n", hist[1]);
        printf("doom_baseline_busy_fields_per_flip=%.3f\n", busy_mean);
    }
    if (measure_only)
    {
        printf("MEASURE-ONLY (no gate applied)\n");
        free(busy_v);
        return 0;
    }

    /* ---- gate ------------------------------------------------------ */
    if (nflips < 2 || ngaps == 0)
    {
        printf("FAIL: fewer than two flips in the window (demo not running?)\n");
        fail = 1;
    }
    if (nlive > 0 && (double)min_live < bl.live_floor)
    {
        printf("FAIL: liveness floor breached: min %u flips per 300 fields < %.0f\n",
               min_live, bl.live_floor);
        fail = 1;
    }
    if (bl.have_mean)
    {
        if (mean > bl.band_hi)
        {
            printf("FAIL: mean %.3f overshoots the silicon band top %.1f (overcharge)\n",
                   mean, bl.band_hi);
            fail = 1;
        }
        else if (mean < bl.mean - bl.mean_eps)
        {
            printf("FAIL: mean %.3f fell below baseline %.3f - %.2f (moved AWAY from the %.1f silicon bar)\n",
                   mean, bl.mean, bl.mean_eps, bl.silicon_target);
            fail = 1;
        }
        else if (mean > bl.mean + bl.mean_eps)
            moved_toward = 1;
    }
    if (bl.have_hist1 && (double)hist[1] > bl.hist1 + bl.hist1_tol)
    {
        printf("FAIL: %u one-field gaps > baseline %.0f + %.0f (mass below the game's 2-field floor)\n",
               hist[1], bl.hist1, bl.hist1_tol);
        fail = 1;
    }
    if (bl.have_busy)
    {
        if (busy_mean < bl.busy_mean * (1.0 - bl.busy_tol_rel))
        {
            printf("FAIL: GPU busy-time/flip %.3f below baseline %.3f - %.0f%% (GPU got cheaper: away from target)\n",
                   busy_mean, bl.busy_mean, 100.0 * bl.busy_tol_rel);
            fail = 1;
        }
        else if (busy_mean > bl.busy_mean * (1.0 + bl.busy_tol_rel))
            moved_toward = 1;
    }
    if (fail)
    {
        printf("FAIL: Doom pace gate\n");
        free(busy_v);
        return 1;
    }
    if (moved_toward)
        printf("MOVED-TOWARD-TARGET: mean/busy-time rose above the recorded baseline "
               "(%.3f / %.3f); re-baseline test/tools/timing_gate_baseline.txt\n",
               bl.mean, bl.busy_mean);
    printf("PASS: Doom pace gate (tracking; silicon target %.1f not yet reached, mean %.3f)\n",
           bl.silicon_target, mean);
    free(busy_v);
    return 0;
}
