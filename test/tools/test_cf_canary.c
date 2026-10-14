/* test/tools/test_cf_canary.c -- timing-campaign S0 Checkered Flag overshoot
 * canary (docs/timing-campaign-plan.md 1.3 / 5.3; #408).
 *
 * Checkered Flag's frame limiter (ROM $8026EE) loads a field budget from
 * 68K RAM word $43B0 -- 0/1/2 = 60/30/20 Hz -- and waits for the video-ISR
 * field counter $432C (a word, +1 per field, cleared when the game frame
 * completes) to pass it; it then stores the elapsed field count in $432E
 * and bumps the 32-bit game-frame counter $41AC.  So one game frame takes
 * AT LEAST budget+1 fields, and exactly that long while the render work
 * fits inside the cap window.
 *
 * What S0 measured (and the plan's 1.3 had wrong): the budget is NOT a
 * load-dependent output.  The ROM writes $43B0 only from two mode-setup
 * sites ($802182 sets 2, $8094D4 sets 1; boot leaves 0), and a 900-field
 * race shows zero writes to it.  In a press-driven boot trajectory it reads
 * 0 at boot, 1 after the first menu press and 2 from the second press on
 * (race included).  The race fixture (below) reads 2 = 20 Hz on the stock
 * core.  "Budget 0 throughout a race" was an artefact of which menu state
 * the old scripted runs happened to be in.
 *
 * The real overcharge signal is therefore the SPAN: fields per game frame
 * against the cap (budget+1).  A timing model that charges the race more
 * than it can absorb makes frames overrun the cap window (span > cap) --
 * the game rate falls below 60/(budget+1).  Today every frame lands exactly
 * on the cap: the render fits with headroom.
 *
 * Fixture: a mid-race RetroArch savestate (the only way to hold a race
 * headlessly -- scripted presses reach one but cannot hold it), restored
 * with --load-state.  The state must be raw; the Makefile extracts rzip
 * states via scripts/rzip_extract.py.  No input is scripted: fixed-frame
 * input is invalid across timing changes.
 *
 *   FAIL  restore did not take / game never advanced; budget rose above
 *         the baseline (cap pushed more conservative); mean fields per
 *         game frame above baseline + tolerance (frames no longer fit the
 *         cap window -- overcharge).
 *   WARN  more overrun frames (span > cap) than the baseline count: plausible in
 *         heavy scenes on silicon but needs reporter/BigPEmu
 *         corroboration before being judged (plan 5.3); any budget change
 *         during the run.
 *   exit 77  ROM or state absent (SKIP).
 *
 * Last stdout line starting "CF_CANARY_RESULT " is machine-readable.
 *
 * Usage:
 *   test_cf_canary <core> <rom> --load-state <raw state> [--frames N]
 *       [--baseline FILE] [--measure-only] [--hold a|b|c|none]
 *       [harness options: --option K=V, --press F:BTN ...]
 *
 * Build:
 *   cc -O2 -Wall -std=c99 -I. -I./libretro-common/include \
 *      -o test/tools/test_cf_canary test/tools/test_cf_canary.c \
 *      test/harness/harness.c -ldl -lm
 */
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "../harness/harness.h"
#include "../../libretro-common/include/libretro.h"

#define MAX_FIELDS   20000
#define SPAN_BUCKET  8           /* spans 1..7, 8+ */
#define BUDGET_ADDR  0x43B0u
#define GAMECTR_ADDR 0x41ACu
#define ISRCTR_ADDR  0x432Cu

static uint8_t **ramp;
static unsigned nfields;
static unsigned bhist[4];          /* fields at budget 0,1,2,3+ */
static unsigned budget_first, budget_changes, budget_max;
static unsigned shist[SPAN_BUCKET + 1];
static unsigned nspans, overruns, max_span;
static double span_sum;
static unsigned last_gf_field;
static uint32_t gctr_first, gctr_prev, gctr_last;
static int have_prev;
static unsigned isr_first, isr_last, pic_changes;
static uint32_t last_pic;
static harness_config *gcfg;

static unsigned ram16(uint32_t a)
{
    const uint8_t *r = *ramp;
    return ((unsigned)r[a] << 8) | r[a + 1];
}

static uint32_t ram32(uint32_t a)
{
    return ((uint32_t)ram16(a) << 16) | ram16(a + 2);
}

static bool on_frame(void *ud, unsigned frame)
{
    unsigned b = ram16(BUDGET_ADDR);
    uint32_t c = ram32(GAMECTR_ADDR);
    (void)ud;
    if (frame == 1)
    {
        isr_first = ram16(ISRCTR_ADDR);
        budget_first = b;
    }
    isr_last = ram16(ISRCTR_ADDR);
    if (gcfg->last_fb_hash != last_pic)
        pic_changes++;
    last_pic = gcfg->last_fb_hash;

    bhist[b > 3 ? 3 : b]++;
    if (b != budget_first)
        budget_changes++;
    if (b > budget_max)
        budget_max = b;

    if (!have_prev)
    {
        gctr_first = c;
        have_prev = 1;
    }
    else if (c != gctr_prev)
    {
        /* A game frame completed on this field.  The first boundary only
         * anchors the next span. */
        if (last_gf_field)
        {
            unsigned span = frame - last_gf_field;
            shist[span > SPAN_BUCKET ? SPAN_BUCKET : span]++;
            nspans++;
            span_sum += (double)span;
            if (span > max_span)
                max_span = span;
            if (span > b + 1)
                overruns++;
        }
        last_gf_field = frame;
    }
    gctr_prev = c;
    gctr_last = c;
    nfields = frame;
    return frame < MAX_FIELDS;
}

/* key=value baseline file, same format as the Doom gate's. */
static int load_baseline(const char *path, double *budget, double *span,
                         double *tol, double *overruns, int *have)
{
    char line[256];
    FILE *f = fopen(path, "r");
    if (!f)
        return 0;
    while (fgets(line, sizeof(line), f))
    {
        char *eq;
        if (line[0] == '#' || line[0] == '\n')
            continue;
        eq = strchr(line, '=');
        if (!eq)
            continue;
        *eq = 0;
        if (!strcmp(line, "cf_baseline_budget")) { *budget = atof(eq + 1); have[0] = 1; }
        else if (!strcmp(line, "cf_baseline_fields_per_game_frame")) { *span = atof(eq + 1); have[1] = 1; }
        else if (!strcmp(line, "cf_span_tolerance")) { *tol = atof(eq + 1); }
        else if (!strcmp(line, "cf_baseline_overruns")) { *overruns = atof(eq + 1); }
    }
    fclose(f);
    return 1;
}

int main(int argc, char **argv)
{
    harness_config cfg = HARNESS_CONFIG_DEFAULT;
    const char *hold = "none";
    const char *baseline_path = "test/tools/timing_gate_baseline.txt";
    int a, fail = 0, measure_only = 0, have[2] = { 0, 0 };
    double bl_budget = 2.0, bl_span = 3.0, bl_tol = 0.25, bl_over = 0.0, mean_span;
    unsigned i;
    char *build;

    cfg.frames = 1200;
    cfg.quiet = 1;
    for (a = 1; a < argc; a++)
    {
        if (!strcmp(argv[a], "--hold") && a + 1 < argc) hold = argv[++a];
        else if (!strcmp(argv[a], "--baseline") && a + 1 < argc) baseline_path = argv[++a];
        else if (!strcmp(argv[a], "--measure-only")) measure_only = 1;
    }
    if (!harness_init_from_args(&cfg, argc, argv))
        return 2;
    if (!cfg.rom_path || !cfg.load_state_path)
    {
        fprintf(stderr, "usage: test_cf_canary <core> <rom> --load-state <raw state> "
                        "[--frames N] [--baseline FILE] [--measure-only]\n");
        return 2;
    }
    {
        FILE *f = fopen(cfg.rom_path, "rb");
        if (!f) { printf("SKIP: ROM not present (%s)\n", cfg.rom_path); return 77; }
        fclose(f);
        f = fopen(cfg.load_state_path, "rb");
        if (!f) { printf("SKIP: state not present (%s)\n", cfg.load_state_path); return 77; }
        fclose(f);
    }
    if (!load_baseline(baseline_path, &bl_budget, &bl_span, &bl_tol, &bl_over, have) && !measure_only)
    {
        fprintf(stderr, "cannot read baseline %s (use --measure-only)\n", baseline_path);
        return 2;
    }
    /* Stock settings: the shipped default is the accurate blitter, the
     * harness default the fast one.  Overridable with --option. */
    {
        int haveopt = 0;
        for (i = 0; i < cfg.num_options; i++)
            if (!strcmp(cfg.options[i].key, "virtualjaguar_usefastblitter"))
                haveopt = 1;
        if (!haveopt)
            harness_set_option(&cfg, "virtualjaguar_usefastblitter", "disabled");
    }
    if (!strcmp(hold, "a")) harness_press(&cfg, 0, RETRO_DEVICE_ID_JOYPAD_A, 1, cfg.frames);
    else if (!strcmp(hold, "b")) harness_press(&cfg, 0, RETRO_DEVICE_ID_JOYPAD_B, 1, cfg.frames);
    else if (!strcmp(hold, "c")) harness_press(&cfg, 0, RETRO_DEVICE_ID_JOYPAD_X, 1, cfg.frames);

    if (!harness_load_rom(&cfg))
        return 2;
    ramp = (uint8_t **)harness_dlsym(&cfg, "jaguarMainRAM");
    if (!ramp || !*ramp)
    {
        fprintf(stderr, "jaguarMainRAM not exported (build with TEST_EXPORTS=1)\n");
        harness_shutdown(&cfg);
        return 2;
    }
    gcfg = &cfg;
    cfg.want_fb_hash = 1;
    cfg.frame_callback = on_frame;
    harness_run(&cfg);
    harness_shutdown(&cfg);

    build = getenv("VJ_EXPECT_BUILD");
    mean_span = nspans ? span_sum / (double)nspans : 0.0;
    printf("Checkered Flag canary: %s\n  state=%s fields=%u hold=%s build=%s\n",
           cfg.rom_path, cfg.load_state_path, nfields, hold, build ? build : "(unset)");
    printf("  budget $%04X: start %u, max %u, %u change(s); fields at 0(60Hz)=%u 1(30Hz)=%u "
           "2(20Hz)=%u 3+=%u\n", BUDGET_ADDR, budget_first, budget_max, budget_changes,
           bhist[0], bhist[1], bhist[2], bhist[3]);
    printf("  game frames ($%04X): %u -> %u; %u spans, mean %.3f fields/game-frame "
           "(cap = budget+1), %u overrun(s), max span %u\n", GAMECTR_ADDR,
           (unsigned)gctr_first, (unsigned)gctr_last, nspans, mean_span, overruns, max_span);
    printf("  span histogram (fields: count):");
    for (i = 1; i <= SPAN_BUCKET; i++)
        printf(" %u%s:%u", i, i == SPAN_BUCKET ? "+" : "", shist[i]);
    printf("\n  video-ISR counter $%04X: %u -> %u; %u picture changes\n", ISRCTR_ADDR,
           isr_first, isr_last, pic_changes);
    printf("CF_CANARY_RESULT build=%s fields=%u budget_start=%u budget_max=%u budget_changes=%u "
           "spans=%u mean_span=%.4f overruns=%u max_span=%u",
           build ? build : "-", nfields, budget_first, budget_max, budget_changes, nspans,
           mean_span, overruns, max_span);
    for (i = 1; i <= SPAN_BUCKET; i++)
        printf(" span%u=%u", i, shist[i]);
    printf("\n");

    if (measure_only)
    {
        printf("MEASURE-ONLY (no gate applied)\n");
        return 0;
    }
    if (nfields < cfg.frames)
    {
        printf("FAIL: run stopped early at field %u of %u\n", nfields, cfg.frames);
        fail = 1;
    }
    if (nspans < 5 || pic_changes == 0)
    {
        printf("FAIL: game never advanced (%u game-frame spans, %u picture changes): "
               "state did not restore, or the race is paused\n", nspans, pic_changes);
        fail = 1;
    }
    if (have[0] && (double)budget_max > bl_budget)
    {
        printf("FAIL: frame-cap budget rose to %u (baseline %.0f) -- cap pushed more "
               "conservative (plan 5.3 overcharge)\n", budget_max, bl_budget);
        fail = 1;
    }
    if (have[1] && nspans >= 5 && mean_span > bl_span + bl_tol)
    {
        printf("FAIL: %.3f fields per game frame > baseline %.3f + %.2f -- frames no "
               "longer fit the cap window (overcharge)\n", mean_span, bl_span, bl_tol);
        fail = 1;
    }
    if (budget_changes > 0)
        printf("WARN: budget changed %u time(s) during the race -- not seen on develop; "
               "investigate\n", budget_changes);
    if ((double)overruns > bl_over)
        printf("WARN: %u game frame(s) overran the cap window (span > budget+1, max %u; baseline %.0f): "
               "plausible in heavy scenes on silicon, needs reporter/BigPEmu corroboration "
               "before judging (plan 5.3)\n", overruns, max_span, bl_over);
    if (fail)
    {
        printf("FAIL: Checkered Flag canary\n");
        return 1;
    }
    printf("PASS: Checkered Flag canary (budget %u, %.3f fields/game-frame, %u overruns)\n",
           budget_max, mean_span, overruns);
    return 0;
}
