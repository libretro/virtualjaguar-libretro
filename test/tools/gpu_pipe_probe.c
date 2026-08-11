/* test/tools/gpu_pipe_probe.c -- magnitude probe for the GPU pipeline
 * timing model (virtualjaguar_gpu_pipeline_timing).
 *
 * Reports, per 300-frame window: GPU opcodes executed, external
 * transfers issued through the modeled gateway, and stall cycles
 * charged -- so a calibration session can see whether the model is
 * firing at all and how large its charges are relative to the field
 * budget (442,780 sysclks NTSC).
 *
 * Build:
 *   cc -O2 -Wall -std=c99 -I./libretro-common/include \
 *      -o test/tools/gpu_pipe_probe test/tools/gpu_pipe_probe.c \
 *      test/harness/harness.c -ldl -lm
 * Run (TEST_EXPORTS core):
 *   ./test/tools/gpu_pipe_probe <core> <rom> --frames 1500 --quiet \
 *      --option virtualjaguar_gpu_pipeline_timing=enabled
 */
#include <stdio.h>
#include <stdint.h>
#include "../harness/harness.h"

static uint64_t *stall_total, *ext_total; static unsigned long long *src[5]; static uint64_t lastv[5]; static const char *srcname[5]={"video","gpu","object","timer","jerry"};
static uint32_t *opcode_count;
static uint64_t last_stall, last_ext, last_ops;

static bool on_frame(void *ud, unsigned frame)
{
    (void)ud;
    if (frame && (frame % 300) == 0)
    {
        uint64_t ds = *stall_total - last_stall;
        uint64_t de = *ext_total - last_ext;
        uint64_t dop = (uint32_t)(*opcode_count - (uint32_t)last_ops);
        printf("f%-5u ops/frame=%-8llu ext/frame=%-7llu stall/frame=%-8llu stall%%field=%.1f\n",
               frame,
               (unsigned long long)(dop / 300),
               (unsigned long long)(de / 300),
               (unsigned long long)(ds / 300),
               100.0 * ((double)ds / 300.0) / 442780.0);
        { int i; printf("      int1/frame(x100):"); for (i=0;i<5;i++) if (src[i]) { printf(" %s=%llu", srcname[i], (unsigned long long)((*src[i]-lastv[i])*100/300)); lastv[i]=*src[i]; } printf("\n"); }
        last_stall = *stall_total; last_ext = *ext_total; last_ops = *opcode_count;
    }
    return true;
}

int main(int argc, char **argv)
{
    harness_config cfg = HARNESS_CONFIG_DEFAULT;
    cfg.frames = 1500;
    if (!harness_init_from_args(&cfg, argc, argv))
        return 1;
    cfg.frame_callback = on_frame;
    if (!harness_load_rom(&cfg))
        return 1;
    stall_total  = (uint64_t *)harness_dlsym(&cfg, "gpu_pipe_stall_total");
    ext_total    = (uint64_t *)harness_dlsym(&cfg, "gpu_pipe_ext_total");
    opcode_count = (uint32_t *)harness_dlsym(&cfg, "gpu_exec_opcode_count");
    {
        unsigned long long *(*find)(const char *) =
            (unsigned long long *(*)(const char *))harness_dlsym(&cfg, "perf_counters_find");
        if (find) { src[0]=find("timing_int1_video"); src[1]=find("timing_int1_gpu"); src[2]=find("timing_int1_object"); src[3]=find("timing_int1_timer"); src[4]=find("timing_int1_jerry"); }
    }
    if (!stall_total || !ext_total || !opcode_count)
    {
        fprintf(stderr, "missing exports (need TEST_EXPORTS core)\n");
        return 1;
    }
    harness_run(&cfg);
    harness_shutdown(&cfg);
    return 0;
}
