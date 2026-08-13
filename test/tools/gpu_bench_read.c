/* test/tools/gpu_bench_read.c -- run one of the acid GPU timing ROMs
 * (tests/timing/gpu_*_loop_rate.jag, render_bound_loop_rate.jag) under
 * the full core-option surface and print the result block the ROM
 * publishes at $80000: passes, window constant, cycles/iteration.
 * The acid runner can't set core options; this harness tool can
 * (--option virtualjaguar_gpu_pipeline_timing=enabled).
 */
#include <stdio.h>
#include <stdint.h>
#include "../harness/harness.h"

static uint32_t rd32(const uint8_t *m, uint32_t a)
{
    return ((uint32_t)m[a] << 24) | ((uint32_t)m[a + 1] << 16)
         | ((uint32_t)m[a + 2] << 8) | (uint32_t)m[a + 3];
}

int main(int argc, char **argv)
{
    harness_config cfg = HARNESS_CONFIG_DEFAULT;
    uint8_t *ram;
    cfg.frames = 400;
    if (!harness_init_from_args(&cfg, argc, argv))
        return 1;
    if (!harness_load_rom(&cfg))
        return 1;
    harness_run(&cfg);
    {
        uint8_t **ramp = (uint8_t **)harness_dlsym(&cfg, "jaguarMainRAM");
        ram = ramp ? *ramp : NULL;
    }
    if (!ram)
    {
        fprintf(stderr, "jaguarMainRAM not exported\n");
        return 1;
    }
    printf("RESULT passes=%u window=%u cyc_per_iter=%u\n",
           rd32(ram, 0x80000), rd32(ram, 0x80004), rd32(ram, 0x80008));
    harness_shutdown(&cfg);
    return 0;
}
