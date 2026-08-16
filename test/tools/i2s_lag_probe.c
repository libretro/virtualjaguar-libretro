/* test/tools/i2s_lag_probe.c -- issue #393: watch the I2S resample
 * cursor pair for steady lag drift and gross-drift resyncs.
 *
 * Hypothesis under test: consumption advances only on output-sample
 * callbacks (799/frame NTSC, 960/frame PAL) while capture tracks the
 * full emulated frame (799.275 / 961.54 word-strobe periods), so the
 * read cursor's lag behind the write cursor grows a fraction of a
 * sample per frame until DSPSampleCallback's resync snaps it forward,
 * discarding ~(I2S_RESYNC_LAG - I2S_TARGET_LAG) ring samples -- an
 * audible skip.  Predicted period: ~36 s NTSC SCLK=19, ~8 s PAL.
 *
 * Prints one line per --window frames: frame, lag (ring samples),
 * resync count, per-frame ring writes, plus a delta-lag/frame slope.
 *
 * Build:
 *   cc -O2 -Wall -std=c99 -I./libretro-common/include \
 *      -o test/tools/i2s_lag_probe test/tools/i2s_lag_probe.c \
 *      test/harness/harness.c -ldl -lm
 * Run (needs a TEST_EXPORTS=1 core):
 *   VJ_EXPECT_BUILD=$(./scripts/build-id.sh) ./test/tools/i2s_lag_probe \
 *      ./virtualjaguar_libretro.dylib "rom.jag" --frames 3600 [--window 60]
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../harness/harness.h"

typedef double   (*lag_fn)(void);
typedef uint32_t (*u32_fn)(void);

typedef struct {
    lag_fn   get_lag;
    u32_fn   get_resync;
    u32_fn   get_writes;
    unsigned window;
    double   last_lag;
    double   peak_lag;
    unsigned last_frame;
    unsigned last_resync;
} probe_state;

static probe_state st;

static bool on_frame(void *userdata, unsigned frame)
{
    probe_state *p = (probe_state *)userdata;
    double lag = p->get_lag();
    if (lag > p->peak_lag)
        p->peak_lag = lag;
    if ((frame + 1) % p->window == 0) {
        unsigned rs = p->get_resync();
        unsigned wr = p->get_writes ? p->get_writes() : 0;
        double slope = (frame + 1 > p->last_frame)
            ? (lag - p->last_lag) / (double)(frame + 1 - p->last_frame) : 0.0;
        printf("%6u  lag=%8.2f  resyncs=%3u  writes/f=%5u  dlag/f=%+.4f%s\n",
               frame + 1, lag, rs, wr, slope,
               rs != p->last_resync ? "  <-- RESYNC" : "");
        p->last_lag = lag; p->last_frame = frame + 1; p->last_resync = rs;
    }
    return true;
}

int main(int argc, char **argv)
{
    harness_config cfg = HARNESS_CONFIG_DEFAULT;
    unsigned window = 60;
    double max_lag = -1.0;      /* --max-lag N: fail if peak lag ever exceeds N */
    int max_resyncs = -1;       /* --max-resyncs N: fail if resyncs exceed N */
    int failed = 0;
    int i;

    for (i = 1; i < argc - 1; i++) {
        if (!strcmp(argv[i], "--window"))
            window = (unsigned)atoi(argv[i + 1]);
        else if (!strcmp(argv[i], "--max-lag"))
            max_lag = atof(argv[i + 1]);
        else if (!strcmp(argv[i], "--max-resyncs"))
            max_resyncs = atoi(argv[i + 1]);
    }

    if (!harness_init_from_args(&cfg, argc, argv))
        return 1;
    if (!harness_load_rom(&cfg)) {
        harness_shutdown(&cfg);
        return 1;
    }

    st.get_lag    = (lag_fn)harness_dlsym(&cfg, "DACGetI2SLag");
    st.get_resync = (u32_fn)harness_dlsym(&cfg, "DACGetI2SResyncCount");
    st.get_writes = (u32_fn)harness_dlsym(&cfg, "DACGetI2SWriteCount");
    st.window = window ? window : 60;
    if (!st.get_lag || !st.get_resync) {
        fprintf(stderr, "core lacks DACGetI2SLag/DACGetI2SResyncCount "
                        "(need TEST_EXPORTS=1 build with #393 probes)\n");
        harness_shutdown(&cfg);
        return 1;
    }

    cfg.frame_callback = on_frame;
    cfg.frame_callback_data = &st;

    printf("# window=%u frames=%u rom=%s\n", st.window, cfg.frames,
           cfg.rom_path ? cfg.rom_path : "?");
    harness_run(&cfg);

    if (max_lag >= 0.0 && st.peak_lag > max_lag) {
        printf("FAIL i2s_lag: peak lag %.2f exceeds --max-lag %.2f "
               "(read cursor drifting; #393 skip class)\n", st.peak_lag, max_lag);
        failed = 1;
    }
    if (max_resyncs >= 0 && (int)st.get_resync() > max_resyncs) {
        printf("FAIL i2s_lag: %u gross-drift resyncs (max %d) -- each "
               "discards ~254 ring samples, an audible skip (#393)\n",
               st.get_resync(), max_resyncs);
        failed = 1;
    }
    if ((max_lag >= 0.0 || max_resyncs >= 0) && !failed)
        printf("PASS i2s_lag: peak lag %.2f, resyncs %u\n",
               st.peak_lag, st.get_resync());

    harness_shutdown(&cfg);
    return failed;
}
