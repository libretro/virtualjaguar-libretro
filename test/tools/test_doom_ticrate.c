/* test/tools/test_doom_ticrate.c — measure Jaguar Doom's game-logic
 * (gametic) advance rate with virtualjaguar_dram_timing on vs off.
 *
 * gametic lives at $04080C in main RAM (32-bit big-endian); $047DA4
 * also counts but is a decoy, not the scheduler tick (established
 * during PR #260 calibration).  Baseline on develop, option off:
 * ~29.97 tics/s in the auto-demo (gametic advances 1 per 2 frames).
 *
 * Build:
 *   cc -O2 -Wall -std=c99 -I./libretro-common/include \
 *      -o test/tools/test_doom_ticrate test/tools/test_doom_ticrate.c \
 *      test/harness/harness.c -ldl -lm
 * Run (needs a TEST_EXPORTS=1 core for jaguarMainRAM):
 *   ./test/tools/test_doom_ticrate ./virtualjaguar_libretro.dylib \
 *      "test/roms/private/ROMS/Doom - Evil Unleashed (1994).jag" \
 *      --frames 5400 --quiet \
 *      [--option virtualjaguar_dram_timing=enabled]
 */
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include "../harness/harness.h"

#define GAMETIC_ADDR  0x04080CU
#define WINDOW_FRAMES 300U          /* 5 s reporting windows */
#define MAX_WINDOWS   64U
/* Doom's attract demo is a 750-tic loop (established during PR #260
 * calibration): the counter resets to 0 partway through some windows,
 * which makes a naive (tic_end - tic_start) go negative and, since the
 * arithmetic here is unsigned, wrap to a huge value instead. 40 tics/s
 * is comfortably above Doom's 30 Hz logic cap, so anything above it in
 * a window is unambiguously a reset artifact, not a real rate. */
#define CLEAN_RATE_CEILING 40.0

static uint8_t *mainram;
static uint32_t first_tic, last_tic;
static unsigned first_frame_seen, last_frame_seen;
static uint32_t win_start_tic;
static unsigned win_start_frame;
static double window_rates[MAX_WINDOWS];
static unsigned window_count;

static int cmp_double(const void *a, const void *b)
{
    double da = *(const double *)a, db = *(const double *)b;
    return (da > db) - (da < db);
}

static uint32_t read_tic(void)
{
    return ((uint32_t)mainram[GAMETIC_ADDR]     << 24)
         | ((uint32_t)mainram[GAMETIC_ADDR + 1] << 16)
         | ((uint32_t)mainram[GAMETIC_ADDR + 2] <<  8)
         |  (uint32_t)mainram[GAMETIC_ADDR + 3];
}

static bool on_frame(void *ud, unsigned frame)
{
    uint32_t tic = read_tic();
    (void)ud;
    if (first_frame_seen == 0 && tic != 0 && tic < 0x10000000) {
        first_frame_seen = frame;
        first_tic = win_start_tic = tic;
        win_start_frame = frame;
    }
    if (first_frame_seen && frame - win_start_frame >= WINDOW_FRAMES) {
        double win_rate = (double)(tic - win_start_tic) * 59.94
                   / (double)(frame - win_start_frame);
        printf("window f%-5u-%-5u  %6.2f tics/s\n", win_start_frame, frame,
               win_rate);
        if (window_count < MAX_WINDOWS)
            window_rates[window_count++] = win_rate;
        win_start_tic = tic;
        win_start_frame = frame;
    }
    last_tic = tic;
    last_frame_seen = frame;
    return true;
}

int main(int argc, char **argv)
{
    harness_config cfg = HARNESS_CONFIG_DEFAULT;
    harness_result res;
    double rate = 0.0;
    cfg.frames = 5400;
    cfg.frame_callback = on_frame;
    if (!harness_init_from_args(&cfg, argc, argv)) return 1;
    if (!harness_load_rom(&cfg))                   return 1;
    /* jaguarMainRAM is `uint8_t *jaguarMainRAM` (a pointer variable into
     * jagMemSpace), not an array -- dlsym gives the address of the pointer
     * variable itself, so it must be dereferenced once (see cd_wedge_probe.c,
     * test_op_gpu_object.c, and most other harness-based tools for the same
     * pattern). Brief's original code cast the dlsym result directly to
     * uint8_t*, which silently read the pointer's own bytes as "RAM" and
     * always saw gametic==0. */
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
    if (first_frame_seen && last_frame_seen > first_frame_seen)
        rate = (double)(last_tic - first_tic) * 59.94
             / (double)(last_frame_seen - first_frame_seen);
    printf("RESULT gametic=%u..%u frames=%u..%u rate=%.2f tics/s\n",
           first_tic, last_tic, first_frame_seen, last_frame_seen, rate);
    /* The raw RESULT line above is corrupted by the demo-loop wraparound
     * (see CLEAN_RATE_CEILING comment) whenever the sampled span crosses a
     * reset. MEDIAN below drops wrap-artifact windows and is the number to
     * calibrate against. */
    {
        double clean[MAX_WINDOWS];
        unsigned i, clean_count = 0;
        double median = 0.0;
        for (i = 0; i < window_count; i++) {
            if (window_rates[i] > 0.0 && window_rates[i] <= CLEAN_RATE_CEILING)
                clean[clean_count++] = window_rates[i];
        }
        if (clean_count) {
            qsort(clean, clean_count, sizeof(double), cmp_double);
            median = (clean_count % 2)
                ? clean[clean_count / 2]
                : (clean[clean_count / 2 - 1] + clean[clean_count / 2]) / 2.0;
        }
        printf("MEDIAN clean_windows=%u/%u median=%.2f tics/s\n",
               clean_count, window_count, median);
        /* PASS/FAIL and the exit code follow the cleaned measurement,
         * not the wrap-corruptible raw span: a run that captured no
         * clean window measured nothing. */
        rate = median;
    }
    res.status = (rate > 0.0) ? "PASS" : "FAIL";
    res.name   = "doom_ticrate";
    res.detail = "gametic advance rate measured (median of clean windows)";
    harness_report(&cfg, &res, 1);
    harness_shutdown(&cfg);
    return (rate > 0.0) ? 0 : 1;
}
