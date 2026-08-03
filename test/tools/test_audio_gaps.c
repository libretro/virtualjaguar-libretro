/* test/tools/test_audio_gaps.c -- detect audio-effect "stutter" (RMS
 * collapses) during Doom's attract demo, comparing virtualjaguar_dram_timing
 * on vs off (user report 2026-08-02: demo sfx stutter with the option
 * enabled, in-game audio fine, no stutter with the option off).
 *
 * Method: watch gametic ($04080C, same field used by test_doom_ticrate.c)
 * for the first attract-demo start (0 -> nonzero transition), reset the
 * harness's audio stats at that instant (harness_reset_audio) so
 * cfg.audio.frames[] captures a clean demo-only window (up to
 * HARNESS_MAX_AUDIO_FRAMES=1200 audio-batch callbacks), then scan the
 * captured per-callback RMS series for collapses: a frame's combined
 * L/R RMS magnitude drops under 10% of the trailing ~1s (60-callback)
 * median while that median is well above silence (>100) -- i.e. audio
 * was clearly playing and then briefly vanished.
 *
 * Build:
 *   cc -O2 -Wall -std=c99 -I./libretro-common/include \
 *      -o test/tools/test_audio_gaps test/tools/test_audio_gaps.c \
 *      test/harness/harness.c -ldl -lm
 * Run (needs a TEST_EXPORTS=1 core for jaguarMainRAM):
 *   VJ_EXPECT_BUILD=$(./scripts/build-id.sh) ./test/tools/test_audio_gaps \
 *      ./virtualjaguar_libretro.dylib \
 *      "test/roms/private/ROMS/Doom - Evil Unleashed (1994).jag" \
 *      --frames 2200 --quiet \
 *      [--option virtualjaguar_dram_timing=enabled]
 */
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <math.h>
#include "../harness/harness.h"

#define GAMETIC_ADDR         0x04080CU
#define TRAIL_WINDOW_FRAMES  60u      /* ~1s at 59.94 audio-batch callbacks/s */
#define GAP_RATIO            0.10
#define GAP_MEDIAN_FLOOR     100.0
#define WORST_N              10

static uint8_t *mainram;
static harness_config *g_cfg;
static int demo_reset_done = 0;
static unsigned demo_start_frame = 0;
static uint32_t prev_tic = 0xFFFFFFFFU;

typedef struct { unsigned frame; double median; double rms; } gap_t;

static uint32_t read_tic(void)
{
    return ((uint32_t)mainram[GAMETIC_ADDR]     << 24)
         | ((uint32_t)mainram[GAMETIC_ADDR + 1] << 16)
         | ((uint32_t)mainram[GAMETIC_ADDR + 2] <<  8)
         |  (uint32_t)mainram[GAMETIC_ADDR + 3];
}

static bool on_frame(void *ud, unsigned frame)
{
    uint32_t tic;
    (void)ud;
    if (!mainram) return true;
    tic = read_tic();
    /* The 0->nonzero transition alone is NOT a reliable demo-start marker:
     * a transient garbage value (e.g. tic=717 for exactly one frame) shows
     * up at $04080C around frame 8, before the game state is initialized,
     * then reverts to 0 (confirmed by direct per-frame dump on this ROM --
     * frame=8 tic=717, frame=9 tic=0, real demo start not until frame=355
     * tic=1). The genuine attract-demo start is the first true 0->1 edge:
     * gametic increments by exactly 1 from a real zero baseline and keeps
     * climbing every ~2 frames thereafter. */
    if (!demo_reset_done && prev_tic == 0 && tic == 1) {
        demo_reset_done = 1;
        demo_start_frame = frame;
        harness_reset_audio(g_cfg);
    }
    prev_tic = tic;
    return true;
}

static double frame_rms(const harness_audio_frame *af)
{
    double l = af->rms_l, r = af->rms_r;
    return sqrt(l * l + r * r);
}

static int cmp_double(const void *a, const void *b)
{
    double da = *(const double *)a, db = *(const double *)b;
    return (da > db) - (da < db);
}

int main(int argc, char **argv)
{
    harness_config cfg = HARNESS_CONFIG_DEFAULT;
    harness_result res;
    gap_t worst[WORST_N];
    unsigned worst_count = 0;
    unsigned gap_count = 0;
    unsigned i;

    cfg.frames = 2200;
    cfg.frame_callback = on_frame;
    g_cfg = &cfg;
    if (!harness_init_from_args(&cfg, argc, argv)) return 1;
    if (!harness_load_rom(&cfg))                   return 1;

    /* jaguarMainRAM is a POINTER variable (into jagMemSpace) -- dlsym gives
     * the address of the pointer itself, so it must be dereferenced once
     * (see cd_wedge_probe.c / test_doom_ticrate.c for the same idiom). */
    {
        uint8_t **ramp = (uint8_t **)harness_dlsym(&cfg, "jaguarMainRAM");
        mainram = (ramp != NULL) ? *ramp : NULL;
    }
    if (!mainram) {
        fprintf(stderr, "jaguarMainRAM not exported -- build the core "
                        "with TEST_EXPORTS=1\n");
        return 1;
    }

    harness_run(&cfg);

    /* Post-hoc scan of the (demo-window) captured audio callbacks for RMS
     * collapses. */
    for (i = 0; i < cfg.audio.frame_count; i++) {
        double window[TRAIL_WINDOW_FRAMES];
        unsigned wcount = 0;
        unsigned j = i;
        double median, cur;

        for (;;) {
            if (j == 0) {
                if (wcount < TRAIL_WINDOW_FRAMES)
                    window[wcount++] = frame_rms(&cfg.audio.frames[j]);
                break;
            }
            j--;
            window[wcount++] = frame_rms(&cfg.audio.frames[j]);
            if (wcount >= TRAIL_WINDOW_FRAMES)
                break;
        }
        if (wcount < 10)
            continue; /* not enough trailing history yet */

        {
            double tmp[TRAIL_WINDOW_FRAMES];
            memcpy(tmp, window, sizeof(double) * wcount);
            qsort(tmp, wcount, sizeof(double), cmp_double);
            median = (wcount % 2) ? tmp[wcount / 2]
                     : (tmp[wcount / 2 - 1] + tmp[wcount / 2]) / 2.0;
        }
        cur = frame_rms(&cfg.audio.frames[i]);
        if (median > GAP_MEDIAN_FLOOR && cur < median * GAP_RATIO) {
            gap_count++;
            if (worst_count < WORST_N) {
                worst[worst_count].frame  = cfg.audio.frames[i].frame;
                worst[worst_count].median = median;
                worst[worst_count].rms    = cur;
                worst_count++;
            } else {
                unsigned mi = 0, k;
                double best_sev = 1e18;
                for (k = 0; k < WORST_N; k++) {
                    double sev = worst[k].median - worst[k].rms;
                    if (sev < best_sev) { best_sev = sev; mi = k; }
                }
                if ((median - cur) > best_sev) {
                    worst[mi].frame  = cfg.audio.frames[i].frame;
                    worst[mi].median = median;
                    worst[mi].rms    = cur;
                }
            }
        }
    }

    if (!cfg.quiet) {
        for (i = 0; i < worst_count; i++)
            printf("gap frame=%u median=%.1f rms=%.1f\n",
                   worst[i].frame, worst[i].median, worst[i].rms);
    }

    printf("RESULT demo_start_frame=%u audio_frames_captured=%u "
           "gap_count=%u dropout_count=%u silent_after_onset=%u "
           "total_batch_calls=%u\n",
           demo_start_frame, cfg.audio.frame_count, gap_count,
           cfg.audio.dropout_count, cfg.audio.silent_after_onset,
           cfg.audio.total_batch_calls);

    /* gap_count above is dominated by ordinary decay tails (a sound rings
     * out and the trailing-median stays high for ~1s afterward while the
     * signal itself is legitimately fading to silence) -- NOT stutter.
     * A true stutter/interrupt is a gap that RECOVERS: RMS collapses for
     * a short run and then bounces back up near its pre-gap level, rather
     * than staying low. Re-scan for that stricter signature. */
    {
        unsigned interrupt_count = 0;
        unsigned min_samples = 0xFFFFFFFFU, max_samples = 0;
        double sum_samples = 0.0, sumsq_samples = 0.0;
        unsigned zero_sample_frames = 0;

        for (i = 0; i < cfg.audio.frame_count; i++) {
            double pre_rms, cur_rms;
            unsigned k, recovered = 0;
            cur_rms = frame_rms(&cfg.audio.frames[i]);
            if (i == 0) continue;
            pre_rms = frame_rms(&cfg.audio.frames[i - 1]);
            if (pre_rms < GAP_MEDIAN_FLOOR || cur_rms >= pre_rms * GAP_RATIO)
                continue; /* not a collapse relative to the immediately
                             preceding frame */
            for (k = i + 1; k < cfg.audio.frame_count && k <= i + 5; k++) {
                if (frame_rms(&cfg.audio.frames[k]) >= pre_rms * 0.5) {
                    recovered = 1;
                    break;
                }
            }
            if (recovered)
                interrupt_count++;
        }

        for (i = 0; i < cfg.audio.frame_count; i++) {
            unsigned s = (unsigned)cfg.audio.frames[i].samples;
            if (s < min_samples) min_samples = s;
            if (s > max_samples) max_samples = s;
            if (s == 0) zero_sample_frames++;
            sum_samples += s;
            sumsq_samples += (double)s * s;
        }
        {
            double n = (double)cfg.audio.frame_count;
            double mean = (n > 0) ? sum_samples / n : 0.0;
            double var = (n > 0) ? (sumsq_samples / n - mean * mean) : 0.0;
            double stddev = (var > 0) ? sqrt(var) : 0.0;
            printf("RESULT2 interrupt_count=%u min_samples=%u max_samples=%u "
                   "zero_sample_frames=%u mean_samples=%.1f stddev_samples=%.1f\n",
                   interrupt_count, min_samples, max_samples,
                   zero_sample_frames, mean, stddev);
        }
    }

    res.status = "INFO";
    res.name   = "audio_gaps";
    res.detail = "RMS-collapse gap count reported above (see RESULT line)";
    harness_report(&cfg, &res, 1);
    harness_shutdown(&cfg);
    return 0;
}
