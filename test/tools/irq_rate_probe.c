/* test/tools/irq_rate_probe.c -- per-field accounting of interrupt
 * delivery vs. what the game's ISR actually counted.
 *
 * Doom's VI handler (init.s "Frame:") does exactly one
 * `addq.l #1,_ticcount` per execution, and I_Update gates the whole
 * MiniLoop on `while (ticcount - lastticcount < 3)`.  So on hardware
 * _ticcount advances exactly 1 per video field and no display can
 * happen faster than one per 3 fields.  If our _ticcount advances
 * faster than TOM asserts the video interrupt, the 68K ISR is running
 * more than once per assert and every ticcount-gated title runs fast
 * -- independent of any GPU/blitter timing model.
 *
 * Prints per-window (default 300 frames, --window N): INT1 asserts by
 * source, 68K interrupt dispatches (m68k_diag_interrupt_count), the raw
 * value + advance rate of any main-RAM address given with --watch ADDR
 * (repeatable, hex), the same for GPU-local RAM with --watch-gpu ADDR
 * (read via GPUReadLong), the GPU's own IRQ3/IRQ0 assert counters
 * (gpu_irq3_count/gpu_irq0_count -- object-processor and CD/BUTCH
 * sources), whether GPUIsRunning() was ever false at a frame boundary,
 * and the current Object List Pointer (OPGetListPointer).
 *
 * Build:
 *   cc -O2 -Wall -std=c99 -I./libretro-common/include \
 *      -o test/tools/irq_rate_probe test/tools/irq_rate_probe.c \
 *      test/harness/harness.c -ldl -lm
 */
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "../harness/harness.h"

#define MAXW 8
static unsigned WINDOW = 300u;

static uint8_t *ram;
static unsigned long long *src[5];
static uint64_t lastv[5];
static const char *srcname[5] = { "video", "gpu", "object", "timer", "jerry" };
static unsigned int (*irq_count)(void);
static unsigned last_irq;
static uint32_t watch[MAXW];
static uint32_t watch_last[MAXW];
static unsigned watch_n;
static uint32_t watchg[MAXW];
static uint32_t watchg_last[MAXW];
static unsigned watchg_n;
static uint32_t (*gpu_rdlong)(uint32_t, uint32_t);
static uint32_t *gpu_irq3_assert, *gpu_irq0_assert;
static uint32_t gpu_irq3_assert_last, gpu_irq0_assert_last;
static int (*gpu_is_running)(void);
static unsigned gpu_not_running_frames;
static uint32_t (*op_get_list_ptr)(void);
static unsigned window_no;

static uint32_t rd32(uint32_t a)
{
    return ((uint32_t)ram[a] << 24) | ((uint32_t)ram[a + 1] << 16)
         | ((uint32_t)ram[a + 2] << 8) | (uint32_t)ram[a + 3];
}

static bool on_frame(void *ud, unsigned frame)
{
    unsigned i;
    (void)ud;
    if (gpu_is_running && !gpu_is_running())
        gpu_not_running_frames++;
    if (!frame || (frame % WINDOW) != 0)
        return true;
    printf("w%-2u ", ++window_no);
    for (i = 0; i < 5; i++)
        if (src[i])
        {
            printf("%s=%.3f ", srcname[i],
                   (double)(*src[i] - lastv[i]) / WINDOW);
            lastv[i] = *src[i];
        }
    if (irq_count)
    {
        unsigned c = irq_count();
        printf("| 68k_irq_dispatch=%.3f ", (double)(c - last_irq) / WINDOW);
        last_irq = c;
    }
    for (i = 0; i < watch_n; i++)
    {
        uint32_t v = rd32(watch[i]);
        printf("| $%06X=%08X(d%.3f) ", watch[i], v,
               (double)(uint32_t)(v - watch_last[i]) / WINDOW);
        watch_last[i] = v;
    }
    for (i = 0; i < watchg_n; i++)
    {
        uint32_t v = gpu_rdlong ? gpu_rdlong(watchg[i], 0) : 0;
        printf("| gpu:$%06X=%08X(d%.3f) ", watchg[i], v,
               (double)(int32_t)(v - watchg_last[i]) / WINDOW);
        watchg_last[i] = v;
    }
    if (gpu_irq3_assert)
    {
        printf(" | irq3_assert=%.3f",
               (double)(*gpu_irq3_assert - gpu_irq3_assert_last) / WINDOW);
        gpu_irq3_assert_last = *gpu_irq3_assert;
    }
    if (gpu_irq0_assert)
    {
        printf(" irq0_assert=%.3f",
               (double)(*gpu_irq0_assert - gpu_irq0_assert_last) / WINDOW);
        gpu_irq0_assert_last = *gpu_irq0_assert;
    }
    if (gpu_is_running)
    {
        printf(" gpu_not_running_frames=%u", gpu_not_running_frames);
        gpu_not_running_frames = 0;
    }
    if (op_get_list_ptr)
        printf(" olp=$%06X", op_get_list_ptr());
    printf("\n");
    return true;
}

int main(int argc, char **argv)
{
    harness_config cfg = HARNESS_CONFIG_DEFAULT;
    int i;
    cfg.frames = 1200;
    for (i = 1; i < argc; i++)
        if (!strcmp(argv[i], "--watch") && i + 1 < argc && watch_n < MAXW)
            watch[watch_n++] = (uint32_t)strtoul(argv[++i], NULL, 16);
        else if (!strcmp(argv[i], "--watch-gpu") && i + 1 < argc && watchg_n < MAXW)
            watchg[watchg_n++] = (uint32_t)strtoul(argv[++i], NULL, 16);
        else if (!strcmp(argv[i], "--window") && i + 1 < argc)
            WINDOW = (unsigned)strtoul(argv[++i], NULL, 10);
    if (!harness_init_from_args(&cfg, argc, argv))
        return 1;
    cfg.frame_callback = on_frame;
    if (!harness_load_rom(&cfg))
        return 1;
    {
        uint8_t **ramp = (uint8_t **)harness_dlsym(&cfg, "jaguarMainRAM");
        unsigned long long *(*find)(const char *) =
            (unsigned long long *(*)(const char *))
            harness_dlsym(&cfg, "perf_counters_find");
        ram = ramp ? *ramp : NULL;
        irq_count = (unsigned int (*)(void))
            harness_dlsym(&cfg, "m68k_diag_interrupt_count");
        gpu_rdlong = (uint32_t (*)(uint32_t, uint32_t))
            harness_dlsym(&cfg, "GPUReadLong");
        gpu_irq3_assert = (uint32_t *)harness_dlsym(&cfg, "gpu_irq3_count");
        gpu_irq0_assert = (uint32_t *)harness_dlsym(&cfg, "gpu_irq0_count");
        gpu_is_running = (int (*)(void))harness_dlsym(&cfg, "GPUIsRunning");
        op_get_list_ptr = (uint32_t (*)(void))harness_dlsym(&cfg, "OPGetListPointer");
        if (find)
        {
            src[0] = find("timing_int1_video");
            src[1] = find("timing_int1_gpu");
            src[2] = find("timing_int1_object");
            src[3] = find("timing_int1_timer");
            src[4] = find("timing_int1_jerry");
        }
    }
    if (!ram)
    {
        fprintf(stderr, "jaguarMainRAM not exported (need TEST_EXPORTS)\n");
        return 1;
    }
    harness_run(&cfg);
    harness_shutdown(&cfg);
    return 0;
}
