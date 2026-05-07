/*
 * test_frame_timing — Per-frame timing diagnostic for Virtual Jaguar.
 *
 * Loads the libretro core and a ROM, runs N frames, and reports per-frame
 * counter deltas (halflines, VBlank IRQs, M68K cycles, etc.) plus wall-clock
 * timing. Designed to diagnose "game runs too fast/slow" bugs by comparing
 * emulated-time metrics against expected NTSC/PAL values.
 *
 * Usage:
 *   ./test_frame_timing [core.dylib] [rom.jag] [--frames N] [--csv] [--json]
 *                       [--bios] [--pal] [--option K=V]
 *
 * Build:
 *   make frame-timing FRAME_TIMING_ROM=path/to/rom.jag
 *
 * Requires core built with BENCH_PROFILE=1 TEST_EXPORTS=1.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../harness/harness.h"
#include "../harness/timing_probe.h"

int main(int argc, char **argv)
{
    harness_config cfg = HARNESS_CONFIG_DEFAULT;
    timing_probe tp;
    int csv_mode = 0;
    int pal_mode = 0;
    int i;

    cfg.frames = 600;
    cfg.quiet = 1;

    /* Pre-scan for our custom flags before harness parses */
    for (i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--csv") == 0) {
            csv_mode = 1;
            /* Remove from argv so harness doesn't choke */
            argv[i] = "--quiet";
        } else if (strcmp(argv[i], "--pal") == 0) {
            pal_mode = 1;
            argv[i] = "--option";
            /* Insert the option value — but we can't easily extend argv,
             * so just set it after harness init */
        }
    }

    if (!harness_init_from_args(&cfg, argc, argv)) {
        fprintf(stderr, "Usage: %s [core.dylib] [rom] [--frames N] "
                "[--csv] [--json] [--bios] [--pal]\n", argv[0]);
        return 1;
    }

    if (pal_mode)
        harness_set_option(&cfg, "virtualjaguar_pal", "enabled");

    if (!harness_load_rom(&cfg))
        return 1;

    if (!timing_probe_init(&tp, &cfg)) {
        harness_shutdown(&cfg);
        return 1;
    }

    tp.is_pal = pal_mode;
    cfg.frame_callback = timing_probe_frame_cb;
    cfg.frame_callback_data = &tp;

    if (!csv_mode && !cfg.json_output)
        fprintf(stderr, "Running %u frames...\n", cfg.frames);

    harness_run(&cfg);
    timing_probe_finish(&tp);

    if (csv_mode) {
        timing_probe_print_csv(&tp);
    } else {
        timing_probe_print_summary(&tp, cfg.json_output);
    }

    timing_probe_destroy(&tp);
    harness_shutdown(&cfg);

    return (tp.summary.anomaly_count > 0) ? 1 : 0;
}
