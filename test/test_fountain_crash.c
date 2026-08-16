/*
 * test_fountain_crash.c -- BIOS-boot regression for issue #469.
 *
 * Two arms:
 *
 *   1. Dummy 128K cart + real BIOS (always).  Asserts vectors 2-255
 *      are parked at $E005DC after JaguarReset.  Catches a revert of
 *      the park on CI with no ROM on disk.
 *   2. Fountain vj.j64 live run (optional).  The unfixed crash
 *      presents 1024-wide frames (above retro_get_system_av_info
 *      max_width 652) and aborts RetroArch with no log.  This arm
 *      checks presented width and that GPU PC stays in local RAM for
 *      the first 30 frames.  It does NOT assert the intro completes;
 *      a later gpu_runaway (~frame 91) is a separate accuracy issue.
 *
 * Live ROM is not vendored.  Default path /tmp/fountain_vj.j64.
 * Missing default -> skip arm 2 (exit 0 if arm 1 passed).  An
 * explicit path that is unreadable is FAIL, not skip.
 *
 * Build:  make TEST_EXPORTS=1 test/test_fountain_crash
 * Run:    ./test/test_fountain_crash ./virtualjaguar_libretro.dylib \
 *            --bios --frames 180 --option virtualjaguar_pal=enabled
 */

#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "harness/harness.h"

#define FOUNTAIN_ROM_DEFAULT "/tmp/fountain_vj.j64"
#define DUMMY_CART_PATH      "/tmp/vj_dummy_cart_469.j64"
#define DUMMY_CART_SIZE      131072u
#define BIOS_ROM_PARK_PC     0x00E005DCu
#define MAX_PRESENT_WIDTH    652u
#define GPU_RAM_LO           0x00F03000u
#define GPU_RAM_HI           0x00F04000u
#define EARLY_GPU_FRAMES     30u

static uint32_t be32(const uint8_t *p)
{
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16)
         | ((uint32_t)p[2] << 8)  | (uint32_t)p[3];
}

static int write_dummy_cart(const char *path)
{
    uint8_t *buf;
    FILE *f;
    size_t nw;

    buf = (uint8_t *)calloc(1, DUMMY_CART_SIZE);
    if (!buf)
        return 0;
    buf[0x400] = 0x04;
    buf[0x401] = 0x04;
    buf[0x402] = 0x04;
    buf[0x403] = 0x04;
    f = fopen(path, "wb");
    if (!f) {
        free(buf);
        return 0;
    }
    nw = fwrite(buf, 1, DUMMY_CART_SIZE, f);
    fclose(f);
    free(buf);
    return nw == DUMMY_CART_SIZE;
}

static int check_parked_vectors(harness_config *cfg)
{
    uint8_t *(*get_ram)(void);
    uint8_t *ram;
    unsigned v;
    unsigned bad;

    get_ram = (uint8_t *(*)(void))harness_dlsym(cfg, "GetRamPtr");
    if (!get_ram) {
        fprintf(stderr, "FAIL: GetRamPtr not exported\n");
        return 0;
    }
    ram = get_ram();
    if (!ram) {
        fprintf(stderr, "FAIL: GetRamPtr returned NULL\n");
        return 0;
    }

    bad = 0;
    for (v = 2; v <= 255; v++) {
        uint32_t got = be32(ram + v * 4);

        if (got != BIOS_ROM_PARK_PC) {
            if (bad < 3)
                fprintf(stderr, "FAIL: vector %u at $%02X = $%08X want $%08X\n",
                        v, v * 4, got, BIOS_ROM_PARK_PC);
            bad++;
        }
    }
    if (bad) {
        fprintf(stderr, "FAIL: %u vectors not parked\n", bad);
        return 0;
    }
    return 1;
}

int main(int argc, char **argv)
{
    harness_config cfg = HARNESS_CONFIG_DEFAULT;
    harness_result results[2];
    unsigned nres;
    const char *live_rom;
    int live_requested;
    int dummy_ok;
    int live_ok;
    uint32_t (*gpu_pc)(void);
    int *gw;
    unsigned f;
    unsigned max_w;
    unsigned early_ok;
    unsigned width_ok;
    char dummy_detail[80];
    char live_detail[160];

    cfg.frames = 180;
    cfg.use_bios = 1;

    if (!harness_init_from_args(&cfg, argc, argv))
        return 1;

    live_rom = cfg.rom_path;
    live_requested = (live_rom != NULL);

    cfg.use_bios = 1;
    nres = 0;
    dummy_ok = 0;
    live_ok = 1;

    if (!write_dummy_cart(DUMMY_CART_PATH)) {
        fprintf(stderr, "FAIL: cannot write dummy cart %s\n", DUMMY_CART_PATH);
        harness_shutdown(&cfg);
        return 1;
    }
    cfg.rom_path = DUMMY_CART_PATH;
    if (!harness_load_rom(&cfg)) {
        harness_shutdown(&cfg);
        return 1;
    }
    dummy_ok = check_parked_vectors(&cfg);
    snprintf(dummy_detail, sizeof(dummy_detail),
             "vectors 2-255 parked at $%08X", BIOS_ROM_PARK_PC);
    results[nres].status = dummy_ok ? "PASS" : "FAIL";
    results[nres].name   = "bios_cart_vector_park";
    results[nres].detail = dummy_detail;
    nres++;
    harness_shutdown(&cfg);

    if (!dummy_ok)
        return 1;

    if (live_requested && access(live_rom, R_OK) != 0) {
        fprintf(stderr, "FAIL: Fountain ROM not readable: %s\n", live_rom);
        return 1;
    }
    if (!live_requested) {
        snprintf(live_detail, sizeof(live_detail),
                 "no live ROM (pass %s to run host-abort arm)",
                 FOUNTAIN_ROM_DEFAULT);
        results[nres].status = "SKIP";
        results[nres].name   = "fountain_bios_crash";
        results[nres].detail = live_detail;
        nres++;
        harness_report(&cfg, results, nres);
        return 0;
    }
    if (!harness_init_from_args(&cfg, argc, argv))
        return 1;
    cfg.use_bios = 1;
    cfg.rom_path = live_rom;
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

    live_ok = (cfg.video.total_frames_rendered > 0) && width_ok && early_ok;
    snprintf(live_detail, sizeof(live_detail),
             "max_width=%u early_gpu_ram=%s frames=%u (host abort, not intro-complete)",
             max_w, early_ok ? "yes" : "no",
             cfg.video.total_frames_rendered);
    results[nres].status = live_ok ? "PASS" : "FAIL";
    results[nres].name   = "fountain_bios_crash";
    results[nres].detail = live_detail;
    printf("  %s\n", live_detail);
    nres++;
    harness_report(&cfg, results, nres);
    harness_shutdown(&cfg);
    return live_ok ? 0 : 1;
}
