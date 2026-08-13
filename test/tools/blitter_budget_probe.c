/* test/tools/blitter_budget_probe.c -- how much REAL time the blitter work
 * we perform each frame would take on hardware, as a fraction of one video
 * field.
 *
 * Why this exists
 * ---------------
 * Several Jaguar titles do not tick their game loop on VBL; they tick once
 * per completed render.  Jaguar Doom's MiniLoop (d_main.c) is the reference
 * case:
 *
 *     exit = ticker ();                             // menu/game logic tick
 *     vblsinframe = lasttics;                       // VBLs the last frame took
 *     while (!I_RefreshCompleted ()) ;              // wait on the renderer
 *     drawer ();
 *     while (DSPRead(&dspfinished) != 0xdef6) ;     // wait on the DSP
 *
 * So input auto-repeat, firing cadence and movement speed in those titles are
 * all a direct function of how fast our emulated renderer completes relative
 * to VBL.  Our blitter is not cycle-accurate (it completes a blit in zero
 * emulated time), so if a frame's blit would really take longer than one
 * field, hardware ticks that loop at half our rate -- and the game plays fast.
 *
 * Answering that needed a hardware or BigPEmu reference, which we cannot
 * instrument.  This probe removes the need for one: the JTRM gives the DRAM
 * access cost directly, so we can price the blitter traffic we ALREADY
 * perform and compare it against the field budget, entirely in-tree.
 *
 * Cost model (JTRM / MEMCON1, same source as src/core/bus_arbiter.h):
 *   page-mode (page hit) DRAM cycle .. 2 system clocks, fixed
 *   row change (page miss) .......... + 3 clocks at the Jaguar default
 *                                     DRAMSPEED 0b11 (MEMCON1 0x1861)
 * The blitter moves 64-bit phrases, so each counted phrase read/write is one
 * DRAM access.  We report a floor (every access a page hit, 2 clocks) and a
 * mixed estimate (--miss-rate, default 0.10) -- the floor alone is enough to
 * prove "this cannot fit in one field", which is the question that matters.
 *
 * One NTSC field = 26.590906 MHz / 60.0544 Hz = 442,780 system clocks.
 *
 * Reading the output: `field%` over 100 means the blit alone cannot finish
 * within one field, so hardware needs >= 2 VBLs for that frame while we
 * deliver it in one -- the loop above then ticks twice as fast as hardware.
 *
 * Requires a BENCH_PROFILE=1 core (perf counters) with test exports:
 *   make clean && make BENCH_PROFILE=1 TEST_EXPORTS=1
 * (`make` does not track CFLAGS changes, so `make clean` is load-bearing --
 * a plain rebuild leaves the old objects and the counters stay absent.)
 *
 * ALSO REQUIRES THE ACCURATE BLITTER.  PERF_INC(blitter_calls) exists only
 * in blitter_generic(); blitter_blit() -- the default "fast" blitter -- has
 * no counters, so a run that leaves the blitter on fast reports 0 blits
 * with no way to tell "title issues none" from "counters can't see them".
 * Always pass:
 *   --option virtualjaguar_usefastblitter=disabled
 * The tool tracks whether that option was passed explicitly: a zero-blits
 * result under the accurate blitter is reported as a legitimate 0-traffic
 * measurement (exit 0); a zero-blits result without it is reported as
 * genuinely ambiguous and exits 2, since the counters may simply be blind
 * to real traffic in that configuration.
 * Build:
 *   cc -O2 -Wall -std=c99 -I. -I./libretro-common/include \
 *      -o test/tools/blitter_budget_probe test/tools/blitter_budget_probe.c \
 *      test/harness/harness.c -ldl -lm
 * Run:
 *   VJ_EXPECT_BUILD=$(./scripts/build-id.sh) ./test/tools/blitter_budget_probe \
 *      ./virtualjaguar_libretro.dylib "rom.jag" --frames 600 [--window 60] \
 *      [--miss-rate 0.10] [--press F:BTN[:HOLD]] [--csv]
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../harness/harness.h"

#define SYSCLK_NTSC     26590906.0
#define FIELD_HZ_NTSC   60.0544
#define DRAM_PAGE_CYCLE 2.0   /* page hit, fixed (JTRM) */
#define DRAM_ROW_MISS   3.0   /* extra clocks, DRAMSPEED 0b11 default */

typedef unsigned long long *(*find_fn)(const char *);

typedef struct {
    unsigned long long *reads, *writes, *calls, *inner;
    unsigned long long  p_reads, p_writes, p_calls, p_inner;
    double   field_clocks, miss_rate;
    unsigned window, csv;
    /* accumulators over the window */
    unsigned long long w_reads, w_writes, w_calls, w_inner;
    double   peak_field_pct;
    unsigned peak_frame, over100;
    /* set when --option virtualjaguar_usefastblitter=disabled was passed
     * explicitly, i.e. the accurate blitter (and its perf counters) was
     * live for the whole run -- see the zero-blits branch in main(). */
    unsigned accurate_requested;
} budget_state;

static budget_state st;

static unsigned long long take(unsigned long long *p, unsigned long long *prev)
{
    unsigned long long now = p ? *p : 0, d = now - *prev;
    *prev = now;
    return d;
}

static bool on_frame(void *ud, unsigned frame)
{
    budget_state *b = (budget_state *)ud;
    unsigned long long r = take(b->reads,  &b->p_reads);
    unsigned long long w = take(b->writes, &b->p_writes);
    unsigned long long c = take(b->calls,  &b->p_calls);
    unsigned long long i = take(b->inner,  &b->p_inner);
    double access = (double)(r + w);
    double clocks = access * (DRAM_PAGE_CYCLE + b->miss_rate * DRAM_ROW_MISS);
    double pct = 100.0 * clocks / b->field_clocks;

    b->w_reads += r; b->w_writes += w; b->w_calls += c; b->w_inner += i;
    if (pct > b->peak_field_pct) { b->peak_field_pct = pct; b->peak_frame = frame; }
    if (pct > 100.0) b->over100++;

    if (b->csv)
        printf("%u,%llu,%llu,%llu,%llu,%.1f,%.2f\n",
               frame, c, i, r, w, clocks, pct);
    else if (b->window && (frame + 1) % b->window == 0) {
        double aw = (double)(b->w_reads + b->w_writes) / (double)b->window;
        double cw = aw * (DRAM_PAGE_CYCLE + b->miss_rate * DRAM_ROW_MISS);
        printf("f%-6u blits/f=%-6.1f px/f=%-9.0f phrases/f=%-9.0f "
               "modeled=%8.0f clk  field=%6.1f%%\n",
               frame + 1,
               (double)b->w_calls / (double)b->window,
               (double)b->w_inner / (double)b->window,
               aw, cw, 100.0 * cw / b->field_clocks);
        b->w_reads = b->w_writes = b->w_calls = b->w_inner = 0;
    }
    return true;
}

int main(int argc, char **argv)
{
    harness_config cfg = HARNESS_CONFIG_DEFAULT;
    find_fn find;
    int i;

    st.window = 60; st.miss_rate = 0.10;
    for (i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--csv")) st.csv = 1;
        else if (!strcmp(argv[i], "--window") && i + 1 < argc)
            st.window = (unsigned)strtoul(argv[++i], NULL, 10);
        else if (!strcmp(argv[i], "--miss-rate") && i + 1 < argc)
            st.miss_rate = strtod(argv[++i], NULL);
    }

    if (!harness_init_from_args(&cfg, argc, argv)) return 1;
    {
        /* Was the accurate blitter explicitly requested?  If so, the perf
         * counters (accurate-path only) were live for the whole run, so a
         * zero-blits result below is a real measurement, not a config gap. */
        unsigned oi;
        for (oi = 0; oi < cfg.num_options; oi++)
            if (!strcmp(cfg.options[oi].key, "virtualjaguar_usefastblitter") &&
                !strcmp(cfg.options[oi].value, "disabled"))
                st.accurate_requested = 1;
    }
    if (!harness_load_rom(&cfg)) { harness_shutdown(&cfg); return 1; }

    find = (find_fn)harness_dlsym(&cfg, "perf_counters_find");
    if (!find) {
        fprintf(stderr, "core lacks perf_counters_find\n");
        harness_shutdown(&cfg); return 1;
    }
    st.reads  = find("blitter_phrase_reads");
    st.writes = find("blitter_phrase_writes");
    st.calls  = find("blitter_calls");
    st.inner  = find("blitter_inner");
    if (!st.reads || !st.writes) {
        fprintf(stderr, "blitter perf counters absent -- rebuild the core with "
                        "BENCH_PROFILE=1 TEST_EXPORTS=1\n");
        harness_shutdown(&cfg); return 1;
    }

    st.field_clocks = SYSCLK_NTSC / FIELD_HZ_NTSC;
    cfg.frame_callback = on_frame;
    cfg.frame_callback_data = &st;

    if (st.csv)
        printf("frame,blits,pixels,phrase_reads,phrase_writes,modeled_clocks,field_pct\n");
    else
        printf("# field budget = %.0f system clocks (%.4f MHz / %.4f Hz), "
               "miss-rate %.2f\n", st.field_clocks, SYSCLK_NTSC / 1e6,
               FIELD_HZ_NTSC, st.miss_rate);

    harness_run(&cfg);

    printf("\nPEAK: frame %u at %.1f%% of one field; %u/%u frames exceed 100%%\n",
           st.peak_frame, st.peak_field_pct, st.over100, cfg.frames);
    /* Zero blits is ambiguous under the fast blitter, not automatically
     * false: PERF_INC(blitter_calls) lives only in the accurate blitter
     * path (blitter_generic), not in blitter_blit(), so a fast-blitter run
     * silently reports 0 whether or not the title actually issues blits --
     * that gap is how "Doom performs zero blitter traffic" got into #401;
     * Doom in fact issues ~400 blits/field under the accurate blitter
     * (docs/doom-render-cost-census.md §4.1). But if the ACCURATE blitter
     * was explicitly selected (st.accurate_requested), the counters were
     * live for the whole run, so a zero here is a real measurement -- not
     * every title uses the blitter, and asserting the fast-blitter
     * explanation for that case would itself be a confident wrong
     * conclusion from ambiguous-looking data. */
    if (st.peak_field_pct <= 0.0)
    {
        if (st.accurate_requested)
        {
            printf("=> ZERO blitter traffic, with the ACCURATE blitter explicitly selected\n"
                   "   (--option virtualjaguar_usefastblitter=disabled), so the perf counters\n"
                   "   were live for the whole run.  This is a legitimate result: this title\n"
                   "   issues no blitter traffic in this window -- it is not a fast-blitter\n"
                   "   counter gap.\n");
            harness_shutdown(&cfg);
            return 0;
        }
        printf("=> NO blitter traffic counted, but the counters only exist in the ACCURATE\n"
               "   blitter path and this run did not explicitly select it, so this result is\n"
               "   AMBIGUOUS: it may mean the title truly issues no blits, or it may mean the\n"
               "   fast blitter is simply hiding real traffic from these counters. Re-run\n"
               "   with:\n"
               "     --option virtualjaguar_usefastblitter=disabled\n"
               "   to resolve which one it is before concluding either way.\n");
        harness_shutdown(&cfg);
        return 2;
    }
    printf("%s\n", st.peak_field_pct > 100.0
        ? "=> blitter work alone cannot fit in one field: hardware needs >=2 VBLs\n"
          "   for those frames while we deliver them in one, so render-bound game\n"
          "   loops (Doom-class) tick faster here than on hardware."
        : "=> blitter work fits within one field; render-bound loops are not\n"
          "   being accelerated by blitter cost alone.");
    harness_shutdown(&cfg);
    return 0;
}
