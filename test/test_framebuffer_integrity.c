/*
 * test/test_framebuffer_integrity.c — Framebuffer integrity regression test.
 *
 * Catches two classes of bugs that slipped through the test suite:
 *   1. Alpha/X-channel corruption: AvP showed "red alpha noise" on the map
 *      screen because the high byte of XRGB8888 pixels contained random
 *      garbage instead of a consistent value.
 *   2. Screen position shifts: Battle Sphere had leftVisible drift causing
 *      the first N columns to be all-black border when they shouldn't be.
 *
 * Uses the shared harness to load the core, run 120 frames with yarc.j64
 * (public homebrew), then inspect the framebuffer via dlsym'd videoBuffer.
 *
 * Exit codes: 0 = pass, 1 = fail, 77 = skip (ROM not found)
 *
 * Build:  cc -O2 -Wall -std=c99 $(INCFLAGS) -o test/test_framebuffer_integrity \
 *           test/test_framebuffer_integrity.c test/harness/harness.c \
 *           $(if $(filter Linux,$(shell uname -s)),-ldl) -lm
 *
 * Usage:  ./test/test_framebuffer_integrity [core.dylib] [rom.j64]
 *         Default ROM: test/roms/yarc.j64
 */

#include "harness/harness.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

/* Suppress LeakSanitizer for the known-benign ROM buffer leak. */
#if defined(__SANITIZE_ADDRESS__)
#define FB_TEST_HAS_ASAN 1
#elif defined(__has_feature)
#if __has_feature(address_sanitizer)
#define FB_TEST_HAS_ASAN 1
#endif
#endif
#ifdef FB_TEST_HAS_ASAN
const char *__lsan_default_suppressions(void) {
    return "leak:harness_load_rom\n";
}
#endif

#define MAX_RESULTS 8
#define DEFAULT_ROM "test/roms/yarc.j64"
#define DEFAULT_FRAMES 120

/* Threshold: at least this fraction of pixels must be non-black
 * after running frames (ensures rendering is actually happening). */
#define MIN_NONBLACK_FRACTION 0.01

/* Maximum number of leading all-black columns before we flag a
 * screen position shift. If rendering is active, the first content
 * should appear within this many columns. */
#define MAX_LEADING_BLACK_COLS 10

/* Trailing side is more lenient: the framebuffer may be wider than
 * the active rendering area (e.g. 326px wide for 320px content). */
#define MAX_TRAILING_BLACK_COLS 16

/* Alpha consistency: what fraction of non-black pixels must share
 * the same X-channel value for the test to pass. */
#define ALPHA_CONSISTENCY_THRESHOLD 0.99

static int pass_count = 0;
static int fail_count = 0;

static void check(int cond, const char *name, const char *detail,
                  harness_result *results, unsigned *num)
{
    if (*num >= MAX_RESULTS) return;
    if (cond) {
        results[*num] = (harness_result){"PASS", name, detail};
        pass_count++;
    } else {
        results[*num] = (harness_result){"FAIL", name, detail};
        fail_count++;
    }
    (*num)++;
}

int main(int argc, char **argv)
{
    harness_config cfg = HARNESS_CONFIG_DEFAULT;
    harness_result results[MAX_RESULTS];
    unsigned num_results = 0;
    uint32_t **fb_ptr;
    int *width_ptr;
    int *height_ptr;
    uint32_t *fb;
    int w, h;
    unsigned total_pixels, nonblack_count;
    unsigned i, x, y;
    double nonblack_frac;
    char detail_buf[256];

    /* Alpha analysis variables */
    unsigned alpha_00_count, alpha_ff_count, alpha_other_count;
    unsigned dominant_alpha_count;
    uint8_t dominant_alpha;
    double alpha_consistency;

    /* Screen position analysis variables */
    unsigned leading_black_cols;
    int col_has_content;

    cfg.frames = DEFAULT_FRAMES;

    if (!harness_init_from_args(&cfg, argc, argv)) {
        fprintf(stderr, "Failed to load core\n");
        return 2;
    }

    /* Default ROM if none provided */
    if (!cfg.rom_path)
        cfg.rom_path = DEFAULT_ROM;

    /* Check if ROM exists before attempting to load */
    {
        FILE *f = fopen(cfg.rom_path, "rb");
        if (!f) {
            fprintf(stderr, "SKIP: ROM '%s' not found\n", cfg.rom_path);
            harness_shutdown(&cfg);
            return 77;
        }
        fclose(f);
    }

    /* Resolve videoBuffer, game_width, game_height from the core */
    fb_ptr = (uint32_t **)harness_dlsym(&cfg, "videoBuffer");
    width_ptr = (int *)harness_dlsym(&cfg, "game_width");
    height_ptr = (int *)harness_dlsym(&cfg, "game_height");
    if (!fb_ptr || !width_ptr || !height_ptr) {
        fprintf(stderr, "Cannot resolve videoBuffer/game_width/game_height\n");
        harness_shutdown(&cfg);
        return 2;
    }

    if (!harness_load_rom(&cfg)) {
        fprintf(stderr, "Failed to load ROM '%s'\n", cfg.rom_path);
        harness_shutdown(&cfg);
        return 2;
    }

    /* Run frames to let the ROM render */
    harness_run(&cfg);

    /* Read the framebuffer state */
    fb = *fb_ptr;
    w = *width_ptr;
    h = *height_ptr;

    if (!fb || w <= 0 || h <= 0) {
        fprintf(stderr, "No framebuffer available (fb=%p, %dx%d)\n",
                (void *)fb, w, h);
        check(0, "framebuffer_available",
              "videoBuffer is NULL or dimensions are zero",
              results, &num_results);
        harness_report(&cfg, results, num_results);
        harness_shutdown(&cfg);
        return 1;
    }

    total_pixels = (unsigned)(w * h);

    /* ================================================================
     * Test 1: Basic sanity — rendering is happening
     * ================================================================ */

    nonblack_count = 0;
    for (i = 0; i < total_pixels; i++) {
        /* A pixel is "non-black" if any of the RGB channels are non-zero.
         * Mask off the X/alpha channel (high byte). */
        if (fb[i] & 0x00FFFFFF)
            nonblack_count++;
    }

    nonblack_frac = (double)nonblack_count / (double)total_pixels;
    snprintf(detail_buf, sizeof(detail_buf),
             "%.1f%% non-black (%u/%u pixels), need >%.0f%%",
             nonblack_frac * 100.0, nonblack_count, total_pixels,
             MIN_NONBLACK_FRACTION * 100.0);
    check(nonblack_frac > MIN_NONBLACK_FRACTION,
          "rendering_active", detail_buf, results, &num_results);

    /* If nothing rendered, the other tests are meaningless — skip them */
    if (nonblack_count == 0) {
        check(0, "alpha_integrity",
              "skipped: no rendered pixels to analyze",
              results, &num_results);
        check(0, "screen_position",
              "skipped: no rendered pixels to analyze",
              results, &num_results);
        harness_report(&cfg, results, num_results);
        harness_shutdown(&cfg);
        return fail_count > 0 ? 1 : 0;
    }

    /* ================================================================
     * Test 2: Alpha/X-channel integrity
     *
     * In XRGB8888, the high byte (X) should be a consistent value
     * across all non-black pixels. The core may output 0x00 or 0xFF
     * (both are valid), but it must NOT output random garbage.
     *
     * Count how many non-black pixels have X=0x00 vs X=0xFF vs other.
     * If a single value dominates (>99%), the alpha is consistent.
     * ================================================================ */

    alpha_00_count = 0;
    alpha_ff_count = 0;
    alpha_other_count = 0;

    for (i = 0; i < total_pixels; i++) {
        uint32_t pixel = fb[i];
        uint8_t alpha;

        /* Only inspect non-black pixels (black may legitimately have any X) */
        if (!(pixel & 0x00FFFFFF))
            continue;

        alpha = (uint8_t)(pixel >> 24);
        if (alpha == 0x00)
            alpha_00_count++;
        else if (alpha == 0xFF)
            alpha_ff_count++;
        else
            alpha_other_count++;
    }

    /* Determine the dominant alpha value */
    if (alpha_00_count >= alpha_ff_count) {
        dominant_alpha = 0x00;
        dominant_alpha_count = alpha_00_count;
    } else {
        dominant_alpha = 0xFF;
        dominant_alpha_count = alpha_ff_count;
    }

    alpha_consistency = (double)dominant_alpha_count / (double)nonblack_count;

    snprintf(detail_buf, sizeof(detail_buf),
             "X=0x%02X: %u, X=0x%02X: %u, other: %u (%.1f%% consistent, need >%.0f%%)",
             dominant_alpha, dominant_alpha_count,
             (dominant_alpha == 0x00) ? 0xFF : 0x00,
             (dominant_alpha == 0x00) ? alpha_ff_count : alpha_00_count,
             alpha_other_count,
             alpha_consistency * 100.0,
             ALPHA_CONSISTENCY_THRESHOLD * 100.0);
    check(alpha_consistency >= ALPHA_CONSISTENCY_THRESHOLD,
          "alpha_integrity", detail_buf, results, &num_results);

    /* Additional check: if there are ANY "other" alpha values (not 0x00
     * or 0xFF), that's very suspicious — flag it separately. */
    if (alpha_other_count > 0) {
        unsigned sample_idx = 0;
        uint32_t sample_pixel = 0;

        /* Find a sample pixel with weird alpha for diagnostics */
        for (i = 0; i < total_pixels && sample_idx == 0; i++) {
            uint32_t pixel = fb[i];
            uint8_t a;
            if (!(pixel & 0x00FFFFFF))
                continue;
            a = (uint8_t)(pixel >> 24);
            if (a != 0x00 && a != 0xFF) {
                sample_pixel = pixel;
                sample_idx = i;
            }
        }
        snprintf(detail_buf, sizeof(detail_buf),
                 "%u pixels with unexpected X values (sample: pixel[%u]=0x%08X)",
                 alpha_other_count, sample_idx, sample_pixel);
        check(alpha_other_count == 0, "alpha_no_garbage",
              detail_buf, results, &num_results);
    } else {
        check(1, "alpha_no_garbage",
              "no pixels with unexpected X-channel values",
              results, &num_results);
    }

    /* ================================================================
     * Test 3: Screen position — no excessive left-side black border
     *
     * If leftVisible shifted right unexpectedly, the first N columns
     * will be entirely black. For yarc.j64, content should start within
     * the first few columns.
     * ================================================================ */

    leading_black_cols = 0;
    for (x = 0; x < (unsigned)w && x < MAX_LEADING_BLACK_COLS + 5; x++) {
        col_has_content = 0;
        for (y = 0; y < (unsigned)h; y++) {
            if (fb[y * w + x] & 0x00FFFFFF) {
                col_has_content = 1;
                break;
            }
        }
        if (col_has_content)
            break;
        leading_black_cols++;
    }

    snprintf(detail_buf, sizeof(detail_buf),
             "%u leading black columns (max allowed: %d)",
             leading_black_cols, MAX_LEADING_BLACK_COLS);
    check(leading_black_cols <= MAX_LEADING_BLACK_COLS,
          "screen_position", detail_buf, results, &num_results);

    /* ================================================================
     * Test 4: No excessive right-side black border (symmetric check)
     *
     * If the screen width grew without rendering filling it, the right
     * side will be all-black. Check last few columns.
     * ================================================================ */

    {
        unsigned trailing_black_cols = 0;
        for (x = (unsigned)w; x > 0 && ((unsigned)w - x) < MAX_LEADING_BLACK_COLS + 5; ) {
            x--;
            col_has_content = 0;
            for (y = 0; y < (unsigned)h; y++) {
                if (fb[y * w + x] & 0x00FFFFFF) {
                    col_has_content = 1;
                    break;
                }
            }
            if (col_has_content)
                break;
            trailing_black_cols++;
        }

        snprintf(detail_buf, sizeof(detail_buf),
                 "%u trailing black columns (max allowed: %d)",
                 trailing_black_cols, MAX_TRAILING_BLACK_COLS);
        check(trailing_black_cols <= MAX_TRAILING_BLACK_COLS,
              "screen_position_right", detail_buf, results, &num_results);
    }

    /* ================================================================
     * Report
     * ================================================================ */

    if (!cfg.quiet) {
        printf("\n  Framebuffer: %dx%d, %u total pixels\n", w, h, total_pixels);
        printf("  Non-black: %u (%.1f%%)\n", nonblack_count, nonblack_frac * 100.0);
        printf("  Alpha: X=0x00: %u, X=0xFF: %u, other: %u\n",
               alpha_00_count, alpha_ff_count, alpha_other_count);
        printf("  Position: %u leading black cols, ", leading_black_cols);
        printf("rendering starts at column %u\n",
               leading_black_cols < (unsigned)w ? leading_black_cols : (unsigned)w);
    }

    harness_report(&cfg, results, num_results);
    harness_shutdown(&cfg);

    return fail_count > 0 ? 1 : 0;
}
