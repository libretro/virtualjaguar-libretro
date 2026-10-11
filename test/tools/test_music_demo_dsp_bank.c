/*
 * test/tools/test_music_demo_dsp_bank.c -- the instruction behind a
 * DSP-issued D_FLAGS store must still use the PRE-store register bank
 * (issue #853).
 *
 * Music Demo (2002) (ScatoLOGIC) enters its DSP program at $F1B020 with
 *
 *     store  ...,(D_FLAGS)     ; REGPAGE=1 + I2S interrupt enable
 *     movei  #$F1B00C,r31      ; the ISR's stack pointer, meant for bank 0
 *     ...
 *     jr     T,-1              ; idle at $F1B034
 *
 * and its I2S handler does `load (r31),r28` in bank 0.  When the core
 * switched banks inside the store, the movei landed in bank 1, bank-0 r31
 * stayed 0, the first interrupt pushed to $FFFFFFFC and the handler
 * returned to garbage: dsp_pc_escape at frame 5 under the real BIOS, and
 * an equivalent runaway in HLE that the crash watchdog's 24-bit PC mask
 * hid.  The audio is generated inside the ISR, which is why the bug was
 * inaudible -- so this test checks the DSP itself, not the audio.
 *
 * Assertion (needs the private ROM; the Makefile skips by name if absent):
 * the DSP PC never leaves DSP local RAM ($F1B000-$F1CFFF, NOT masked to 24
 * bits) while the DSP is running, sampled every frame for --frames frames.
 * Before the fix it left at frame 5 in both modes ($FFFFFFEF under the
 * BIOS, $C1903000 in HLE).  A bank-0 r31 check was tried and dropped: the
 * runaway code happens to leave $F1B00C there too, so it discriminates
 * nothing.
 *
 * Run once per boot mode (HLE, and --bios with a BIOS in the system dir).
 *
 * Build: cc -O2 -Wall -std=c99 $(INCFLAGS) -o test/tools/test_music_demo_dsp_bank \
 *          test/tools/test_music_demo_dsp_bank.c test/harness/harness.c \
 *          test/harness/dsp_probe.c -ldl -lm
 * Needs the wide test ABI (make TEST_EXPORTS=1).
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#include "../harness/harness.h"
#include "../harness/dsp_probe.h"

static dsp_probe probe;

static bool frame_callback(void *userdata, unsigned frame)
{
    (void)userdata;
    (void)frame;
    /* Returns false (stops the run) on the first escape. */
    return dsp_probe_per_frame(&probe);
}

int main(int argc, char **argv)
{
    harness_config cfg = HARNESS_CONFIG_DEFAULT;
    harness_result results[1];
    char detail[1][160];
    unsigned num_results = 0;
    int exit_code = 0;

    cfg.frames = 1200;
    cfg.frame_callback = frame_callback;
    cfg.frame_callback_data = &cfg;

    if (!harness_init_from_args(&cfg, argc, argv))
        return 2;
    if (!cfg.rom_path) {
        fprintf(stderr, "Usage: test_music_demo_dsp_bank [core] <Music Demo rom> "
                        "[--bios] [--frames N]\n");
        harness_shutdown(&cfg);
        return 2;
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

    harness_run(&cfg);
    dsp_probe_snapshot(&probe);

    if (probe.counters.pc_escape_count == 0) {
        snprintf(detail[0], sizeof(detail[0]),
                 "%s: DSP PC stayed in local RAM for %u frames",
                 cfg.use_bios ? "BIOS" : "HLE", cfg.current_frame);
        results[num_results++] = (harness_result){"PASS", "dsp_pc_local", detail[0]};
    } else {
        snprintf(detail[0], sizeof(detail[0]),
                 "%s: DSP PC left local RAM at frame %u (PC=$%08X)",
                 cfg.use_bios ? "BIOS" : "HLE",
                 probe.counters.first_escape_frame,
                 probe.counters.first_escape_pc);
        results[num_results++] = (harness_result){"FAIL", "dsp_pc_local", detail[0]};
        exit_code = 1;
    }

    harness_report(&cfg, results, num_results);
    harness_shutdown(&cfg);
    return exit_code;
}
