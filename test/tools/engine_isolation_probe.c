/* test/tools/engine_isolation_probe.c -- measures per-engine opcode-count
 * deltas over a post-warmup window, to answer #536's actual open question:
 * does any existing ROM give attributable (single-engine) Ir, or does every
 * candidate mix GPU + DSP + blitter the way jagniccc deliberately does?
 *
 * Reads gpu_exec_opcode_count / dsp_exec_opcode_count (both cumulative
 * uint32_t globals exported by the wide TEST_EXPORTS ABI; see
 * src/tom/gpu.c:669 and src/jerry/dsp.c:317) once at --warmup frames and
 * again at --frames frames, and reports the delta over that window. A
 * genuinely single-engine ROM shows one of the two deltas at exactly 0.
 *
 * Not part of `make test` -- build by hand:
 *   cc -O2 -Wall -std=c99 -I. -I./libretro-common/include \
 *      -o /tmp/eiprobe test/tools/engine_isolation_probe.c \
 *      test/harness/harness.c -ldl -lm
 *
 * Usage:
 *   VJ_EXPECT_BUILD=$(./scripts/build-id.sh) /tmp/eiprobe <core> <rom> \
 *      [--warmup N] [--frames N]
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../harness/harness.h"

int main(int argc, char **argv)
{
    harness_config cfg = HARNESS_CONFIG_DEFAULT;
    unsigned warmup = 300;
    unsigned target = 900;
    uint32_t *gpu_count;
    uint32_t *dsp_count;
    uint32_t gpu_at_warmup, dsp_at_warmup;
    uint32_t gpu_at_target, dsp_at_target;
    unsigned i;
    int ai;

    if (!harness_init_from_args(&cfg, argc, argv)) return 1;
    if (!cfg.rom_path) {
        fprintf(stderr, "usage: engine_isolation_probe [core] <rom> "
                "[--warmup N] [--frames N]\n");
        return 1;
    }
    for (ai = 1; ai < argc; ai++) {
        if (strcmp(argv[ai], "--warmup") == 0 && ai + 1 < argc)
            warmup = (unsigned)atoi(argv[++ai]);
        else if (strcmp(argv[ai], "--frames") == 0 && ai + 1 < argc)
            target = (unsigned)atoi(argv[++ai]);
    }

    if (!harness_load_rom(&cfg)) return 1;

    gpu_count = (uint32_t *)harness_dlsym(&cfg, "gpu_exec_opcode_count");
    dsp_count = (uint32_t *)harness_dlsym(&cfg, "dsp_exec_opcode_count");
    if (!gpu_count || !dsp_count) {
        fprintf(stderr, "engine_isolation_probe: dlsym failed for "
                "gpu_exec_opcode_count/dsp_exec_opcode_count -- did you "
                "build with TEST_EXPORTS=1?\n");
        harness_shutdown(&cfg);
        return 2;
    }

    for (i = 0; i < warmup; i++) harness_step(&cfg);
    gpu_at_warmup = *gpu_count;
    dsp_at_warmup = *dsp_count;

    for (; i < target; i++) harness_step(&cfg);
    gpu_at_target = *gpu_count;
    dsp_at_target = *dsp_count;

    printf("rom=%s warmup=%u target=%u "
           "gpu_delta=%u dsp_delta=%u "
           "gpu_at_warmup=%u dsp_at_warmup=%u "
           "gpu_at_target=%u dsp_at_target=%u\n",
           cfg.rom_path, warmup, target,
           (unsigned)(gpu_at_target - gpu_at_warmup),
           (unsigned)(dsp_at_target - dsp_at_warmup),
           (unsigned)gpu_at_warmup, (unsigned)dsp_at_warmup,
           (unsigned)gpu_at_target, (unsigned)dsp_at_target);

    harness_shutdown(&cfg);
    return 0;
}
