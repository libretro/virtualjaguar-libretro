/*
 * test/tools/test_dsp_audio_diag.c — DSP audio diagnostic tool.
 *
 * Diagnoses "no audio" bugs by monitoring DSP state frame-by-frame:
 *   - PC escape detection (PC leaves $F1B000–$F1CFFF work RAM)
 *   - Register bank initialization health
 *   - LTXD non-zero ratio (are samples actually being written?)
 *   - I2S dispatch vs audio output correlation
 *   - Auto-dumps DSP state on failure for post-mortem
 *
 * Build: make dsp-diag
 * Usage: ./test/tools/test_dsp_audio_diag [core.dylib] <rom.jag> [options]
 *
 * Options (in addition to standard harness flags):
 *   --frames N           Number of frames to run (default: 300)
 *   --bios               Use BIOS boot instead of HLE
 *   --json               Machine-parseable output
 *   --dump-on-escape     Dump DSP RAM + registers on PC escape
 *   --dump-interval N    Print DSP state every N frames
 *
 * Exit codes:
 *   0 = all checks pass (audio is healthy)
 *   1 = one or more failures detected
 *   2 = setup error (missing ROM, core won't load, etc.)
 */

#include "../harness/harness.h"
#include "../harness/dsp_probe.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_RESULTS 16

static dsp_probe probe;
static int dump_on_escape = 0;
static unsigned dump_interval = 0;

static bool frame_callback(void *userdata, unsigned frame)
{
    harness_config *cfg = (harness_config *)userdata;
    (void)cfg;

    if (!dsp_probe_per_frame(&probe)) {
        if (dump_on_escape) {
            printf("\n!!! DSP PC ESCAPE at frame %u: PC=$%06X !!!\n",
                   probe.snap.frame, probe.snap.pc);
            dsp_probe_dump_banks(&probe);
            dsp_probe_dump_ram(&probe, probe.snap.pc & 0xFFFFF0, 16);
            /* Also dump around the ISR vector region */
            printf("  ISR vector region ($F1B000):\n");
            dsp_probe_dump_ram(&probe, DSP_WORK_RAM_BASE, 32);
        }
        return false;
    }

    if (dump_interval && !cfg->json_output && (frame % dump_interval == 0)) {
        dsp_probe_print_snapshot(&probe, 0);
    }

    return true;
}

int main(int argc, char **argv)
{
    harness_config cfg = HARNESS_CONFIG_DEFAULT;
    harness_result results[MAX_RESULTS];
    unsigned num_results = 0;
    int exit_code = 0;
    char detail_buf[8][256];
    int i, filt_argc;
    char *filt_argv[64];

    /* Separate our flags from harness flags.  Build a filtered argv
     * that only contains args the harness understands. */
    filt_argv[0] = argv[0];
    filt_argc = 1;
    for (i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--dump-on-escape") == 0) {
            dump_on_escape = 1;
        } else if (strcmp(argv[i], "--dump-interval") == 0 && i + 1 < argc) {
            dump_interval = (unsigned)atoi(argv[++i]);
        } else {
            if (filt_argc < 63) filt_argv[filt_argc++] = argv[i];
        }
    }
    filt_argv[filt_argc] = NULL;

    cfg.frames = 300;
    cfg.frame_callback = frame_callback;
    cfg.frame_callback_data = &cfg;

    if (!harness_init_from_args(&cfg, filt_argc, filt_argv)) return 2;

    if (!cfg.rom_path) {
        fprintf(stderr, "Usage: test_dsp_audio_diag [core.dylib] <rom.jag> [options]\n");
        harness_shutdown(&cfg);
        return 2;
    }

    if (!cfg.quiet && !cfg.json_output) {
        printf("=== DSP Audio Diagnostic ===\n");
        printf("Core: %s\n", cfg.core_path);
        printf("ROM:  %s\n", cfg.rom_path);
        printf("Mode: %s | Frames: %u\n",
               cfg.use_bios ? "BIOS" : "HLE", cfg.frames);
    }

    if (!harness_load_rom(&cfg)) {
        harness_shutdown(&cfg);
        return 2;
    }

    if (!dsp_probe_init(&probe, &cfg)) {
        fprintf(stderr, "Cannot initialize DSP probe (need TEST_EXPORTS=1 build)\n");
        harness_shutdown(&cfg);
        return 2;
    }

    /* Run the diagnostic */
    harness_run(&cfg);

    /* Take final snapshot */
    dsp_probe_snapshot(&probe);

    /* ================================================================
     * Assertions
     * ================================================================ */

    /* 1. DSP PC stays in work RAM */
    if (probe.counters.pc_escape_count == 0) {
        results[num_results++] = (harness_result){
            "PASS", "dsp_pc_bounded",
            "DSP PC stayed within work RAM for all frames"
        };
    } else {
        snprintf(detail_buf[0], sizeof(detail_buf[0]),
                 "DSP PC escaped %u times, first at frame %u (PC=$%06X)",
                 probe.counters.pc_escape_count,
                 probe.counters.first_escape_frame,
                 probe.counters.first_escape_pc);
        results[num_results++] = (harness_result){"FAIL", "dsp_pc_bounded", detail_buf[0]};
        exit_code = 1;
    }

    /* 2. LTXD non-zero ratio after frame 30 (audio health) */
    {
        double ratio = dsp_probe_ltxd_ratio(&probe);
        if (cfg.current_frame > 30 && ratio > 0.5) {
            snprintf(detail_buf[1], sizeof(detail_buf[1]),
                     "LTXD non-zero ratio %.1f%% (healthy)", ratio * 100);
            results[num_results++] = (harness_result){"PASS", "ltxd_health", detail_buf[1]};
        } else if (cfg.current_frame > 30 && ratio > 0.1) {
            snprintf(detail_buf[1], sizeof(detail_buf[1]),
                     "LTXD non-zero ratio %.1f%% (marginal)", ratio * 100);
            results[num_results++] = (harness_result){"PASS", "ltxd_health", detail_buf[1]};
        } else if (cfg.current_frame > 30) {
            snprintf(detail_buf[1], sizeof(detail_buf[1]),
                     "LTXD non-zero ratio %.1f%% (no audio output)", ratio * 100);
            results[num_results++] = (harness_result){"FAIL", "ltxd_health", detail_buf[1]};
            exit_code = 1;
        } else {
            results[num_results++] = (harness_result){
                "SKIP", "ltxd_health", "not enough frames to measure"
            };
        }
    }

    /* 3. Bank1 initialization (main program bank should have >10 regs set) */
    {
        unsigned b1_nz = dsp_probe_bank_nonzero(probe.snap.bank1);
        if (b1_nz >= 10) {
            snprintf(detail_buf[2], sizeof(detail_buf[2]),
                     "Bank1 has %u/32 non-zero registers (well-initialized)", b1_nz);
            results[num_results++] = (harness_result){"PASS", "bank1_init", detail_buf[2]};
        } else if (b1_nz >= 5) {
            snprintf(detail_buf[2], sizeof(detail_buf[2]),
                     "Bank1 has %u/32 non-zero registers (sparse)", b1_nz);
            results[num_results++] = (harness_result){"PASS", "bank1_init", detail_buf[2]};
        } else {
            snprintf(detail_buf[2], sizeof(detail_buf[2]),
                     "Bank1 has %u/32 non-zero registers (insufficient init)", b1_nz);
            results[num_results++] = (harness_result){"FAIL", "bank1_init", detail_buf[2]};
            exit_code = 1;
        }
    }

    /* 4. Audio callback fires */
    if (cfg.audio.total_batch_calls > 0) {
        snprintf(detail_buf[3], sizeof(detail_buf[3]),
                 "%u batch callbacks over %u frames",
                 cfg.audio.total_batch_calls, cfg.current_frame);
        results[num_results++] = (harness_result){"PASS", "audio_callbacks", detail_buf[3]};
    } else {
        results[num_results++] = (harness_result){
            "FAIL", "audio_callbacks", "no audio batch callbacks fired"
        };
        exit_code = 1;
    }

    /* 5. Non-silent audio detected */
    if (cfg.audio.total_nonsilent > 0) {
        snprintf(detail_buf[4], sizeof(detail_buf[4]),
                 "%u non-silent samples, onset at frame %d",
                 cfg.audio.total_nonsilent, cfg.audio.first_audio_frame);
        results[num_results++] = (harness_result){"PASS", "audio_nonsilent", detail_buf[4]};
    } else {
        results[num_results++] = (harness_result){
            "FAIL", "audio_nonsilent", "all audio samples are silent"
        };
        exit_code = 1;
    }

    /* 6. DSP is running */
    if (probe.snap.running) {
        results[num_results++] = (harness_result){"PASS", "dsp_running", "DSP is active"};
    } else {
        results[num_results++] = (harness_result){
            "FAIL", "dsp_running", "DSP halted before end of test"
        };
        exit_code = 1;
    }

    /* Report */
    harness_report(&cfg, results, num_results);

    /* Extra detail on failure */
    if (exit_code && !cfg.json_output && !cfg.quiet) {
        printf("\n--- DSP state at end of test ---\n");
        dsp_probe_print_snapshot(&probe, 0);
        dsp_probe_dump_banks(&probe);
        if (probe.counters.pc_escape_count > 0) {
            printf("\n--- DSP RAM at ISR vector region ---\n");
            dsp_probe_dump_ram(&probe, DSP_WORK_RAM_BASE, 32);
        }
    }

    harness_shutdown(&cfg);
    return exit_code;
}
