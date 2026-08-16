/*
 * test_fountain_crash.c -- BIOS-boot regression for issue #469.
 *
 * Fountain (Outline 2026, 128K vj.j64) is a jagcrypt GPU-only intro.
 * Before the fix, real-BIOS boot left 68K exception vectors as PRNG
 * fill; the 68K double-faulted, smashed the GPU kernel, and the GPU
 * ran TOM MMIO as code.  The core then presented 1024-wide frames
 * (above retro_get_system_av_info max_width 652), which aborts
 * RetroArch with no log.
 *
 * ROM is not vendored.  Default path is /tmp/fountain_vj.j64; exit 77
 * if it is missing (GNU skip).  `make test` records that skip in the
 * ledger so it cannot read as a pass.
 *
 * Build:  cc -O2 -Wall -std=c99 -I. -I./test -I./libretro-common/include \
 *            -o test/test_fountain_crash test/test_fountain_crash.c \
 *            test/harness/harness.c -ldl -lm
 * Run:    ./test/test_fountain_crash ./virtualjaguar_libretro.dylib \
 *            /tmp/fountain_vj.j64 --bios --frames 180
 */

#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>

#include "harness/harness.h"

#define FOUNTAIN_ROM_DEFAULT "/tmp/fountain_vj.j64"
#define MAX_PRESENT_WIDTH    652u
#define GPU_RAM_LO           0x00F03000u
#define GPU_RAM_HI           0x00F04000u
#define EARLY_GPU_FRAMES     30u

int main(int argc, char **argv)
{
    harness_config cfg = HARNESS_CONFIG_DEFAULT;
    harness_result res;
    uint32_t (*gpu_pc)(void);
    int *gw;
    unsigned f;
    unsigned max_w;
    unsigned early_ok;
    unsigned width_ok;
    int ok;
    char detail[160];

    cfg.frames = 180;
    cfg.use_bios = 1;

    if (!harness_init_from_args(&cfg, argc, argv))
        return 1;

    if (!cfg.rom_path) {
        if (access(FOUNTAIN_ROM_DEFAULT, R_OK) != 0) {
            fprintf(stderr,
                    "SKIP: Fountain ROM not at %s (download vj.j64 from "
                    "JaguarDemos Fountain/; do not vendor it)\n",
                    FOUNTAIN_ROM_DEFAULT);
            return 77;
        }
        cfg.rom_path = FOUNTAIN_ROM_DEFAULT;
    } else if (access(cfg.rom_path, R_OK) != 0) {
        fprintf(stderr, "SKIP: Fountain ROM not readable: %s\n", cfg.rom_path);
        return 77;
    }

    cfg.use_bios = 1;

    if (!harness_load_rom(&cfg))
        return 1;

    gpu_pc = (uint32_t (*)(void))harness_dlsym(&cfg, "GPUGetPC");
    gw = (int *)harness_dlsym(&cfg, "game_width");
    if (!gpu_pc || !gw) {
        fprintf(stderr, "FAIL: GPUGetPC/game_width not exported\n");
        harness_shutdown(&cfg);
        return 1;
    }

    max_w = 0;
    early_ok = 1;
    width_ok = 1;
    for (f = 0; f < cfg.frames; f++) {
        unsigned w;
        uint32_t pc;
        harness_step(&cfg);
        w = (unsigned)*gw;
        pc = gpu_pc();
        if (w > max_w)
            max_w = w;
        if (w > MAX_PRESENT_WIDTH)
            width_ok = 0;
        if (f < EARLY_GPU_FRAMES
            && (pc < GPU_RAM_LO || pc >= GPU_RAM_HI))
            early_ok = 0;
    }

    ok = (cfg.video.total_frames_rendered > 0) && width_ok && early_ok;
    snprintf(detail, sizeof(detail),
             "max_width=%u early_gpu_ram=%s frames=%u",
             max_w, early_ok ? "yes" : "no",
             cfg.video.total_frames_rendered);

    res.status = ok ? "PASS" : "FAIL";
    res.name   = "fountain_bios_crash";
    res.detail = detail;
    printf("  %s\n", detail);
    harness_report(&cfg, &res, 1);
    harness_shutdown(&cfg);
    return ok ? 0 : 1;
}
