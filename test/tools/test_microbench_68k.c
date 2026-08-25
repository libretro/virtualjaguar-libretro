/*
 * test/tools/test_microbench_68k.c -- assertion harness for the 68K-only
 * microbenchmark ROM (test/microbench/bench68k.j64, issue #536).
 *
 * Boots the ROM, watches the reserved sentinel block in main RAM once per
 * rendered frame, and asserts the 68K reached its fixed iteration count.
 *
 * Emits one machine-parseable line before the PASS/FAIL verdict:
 *
 *   MICROBENCH engine=68k done=<0|1> done_frame=<N|-1> start_frame=<N|-1> \
 *       count=<iterations> expect_count=<iterations> budget=<frames>
 *
 * `done_frame` is the point of the tool: a microbenchmark that merely
 * passes tells you nothing, but the frame it finished on is the number
 * #536 exists to produce, and it makes a marginal frame budget
 * self-diagnosing instead of a silent "sentinel never written".
 *
 * Needs the wide test ABI (make TEST_EXPORTS=1) for the jaguarMainRAM
 * export.
 *
 * Build:
 *   cc -O2 -Wall -std=c99 -I. -I./src -I./libretro-common/include \
 *      -o test/tools/test_microbench_68k test/tools/test_microbench_68k.c \
 *      test/harness/harness.c -ldl -lm
 *
 * Run:
 *   ./test/tools/test_microbench_68k ./virtualjaguar_libretro.dylib \
 *       test/microbench/bench68k.j64
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../harness/harness.h"

/* Sentinel convention -- see test/microbench/cartboot.inc, which is the
 * authority.  $010000-$01000F is reserved; benchmark working buffers live
 * at $020000 and above so they can never reach it. */
#define MB_SENT_START       0x00010000u
#define MB_SENT_DONE        0x00010004u
#define MB_SENT_COUNT       0x00010008u

#define MB_MAGIC_START      0xC0DE57A7u
#define MB_MAGIC_68K        0xC0DE0068u

/* bench68k.s: ITERATIONS .equ 1000000 */
#define MB_EXPECT_COUNT     1000000u

/* Measured completion frames (harness defaults unless noted):
 *      114  defaults (dram_timing / gpu_pipeline_timing both "disabled")
 *      114  gpu_pipeline_timing=enabled, and PAL
 *      128  dram_timing=enabled
 *      228  both enabled + VJ_DRAM_SCALE=8
 *      288  both enabled + VJ_DRAM_SCALE=12
 *      350  both enabled + VJ_DRAM_SCALE=16   (top of dram_scale_sweep.sh)
 * 600 clears the slowest of those with ~1.7x margin and still costs about
 * a second of wall time.  If the budget is ever marginal the tool says so
 * precisely -- done=0 with start_frame set means "booted, didn't finish",
 * not a silent mystery. */
#define MB_FRAME_BUDGET     600

typedef struct {
    uint8_t **ramp;         /* jaguarMainRAM is a POINTER variable into
                               jagMemSpace, not the array itself -- dlsym
                               hands back the address OF the pointer. */
    unsigned  frame;
    int       start_frame;
    int       done_frame;
    uint32_t  count;
} mb_state;

static uint32_t mb_read32(const uint8_t *ram, uint32_t addr)
{
    return ((uint32_t)ram[addr] << 24)
         | ((uint32_t)ram[addr + 1] << 16)
         | ((uint32_t)ram[addr + 2] << 8)
         |  (uint32_t)ram[addr + 3];
}

static void mb_frame_cb(void *userdata, const void *data,
                        unsigned width, unsigned height, size_t pitch)
{
    mb_state *st = (mb_state *)userdata;
    const uint8_t *ram;

    (void)data;
    (void)width;
    (void)height;
    (void)pitch;

    st->frame++;

    if (!st->ramp || !*st->ramp)
        return;
    ram = *st->ramp;

    if (st->start_frame < 0 && mb_read32(ram, MB_SENT_START) == MB_MAGIC_START)
        st->start_frame = (int)st->frame;

    if (st->done_frame < 0 && mb_read32(ram, MB_SENT_DONE) == MB_MAGIC_68K) {
        st->done_frame = (int)st->frame;
        st->count = mb_read32(ram, MB_SENT_COUNT);
    }
}

int main(int argc, char **argv)
{
    harness_config cfg = HARNESS_CONFIG_DEFAULT;
    mb_state st;
    harness_result res;
    int failed = 0;
    const char *detail = "68K loop completed with the expected iteration count";

    memset(&st, 0, sizeof(st));
    st.start_frame = -1;
    st.done_frame  = -1;

    cfg.frames = MB_FRAME_BUDGET;
    cfg.quiet  = 1;
    if (!harness_init_from_args(&cfg, argc, argv))
        return 2;
    cfg.video_callback      = mb_frame_cb;
    cfg.video_callback_data = &st;

    /* Resolve before the run: the callback fires per frame and must not
     * pay for a dlsym each time. */
    st.ramp = (uint8_t **)harness_dlsym(&cfg, "jaguarMainRAM");
    if (!st.ramp) {
        fprintf(stderr,
                "test_microbench_68k: core does not export jaguarMainRAM "
                "-- rebuild with `make TEST_EXPORTS=1`\n");
        harness_shutdown(&cfg);
        return 2;
    }

    if (!harness_load_rom(&cfg)) {
        fprintf(stderr, "test_microbench_68k: failed to load ROM \"%s\"\n",
                cfg.rom_path ? cfg.rom_path : "(none)");
        harness_shutdown(&cfg);
        return 3;
    }

    harness_run(&cfg);

    printf("MICROBENCH engine=68k done=%d done_frame=%d start_frame=%d "
           "count=%u expect_count=%u budget=%u\n",
           st.done_frame >= 0 ? 1 : 0, st.done_frame, st.start_frame,
           (unsigned)st.count, (unsigned)MB_EXPECT_COUNT,
           (unsigned)MB_FRAME_BUDGET);

    if (st.start_frame < 0) {
        detail = "ROM never reached its entry point (no START magic at $010000)";
        failed = 1;
    } else if (st.done_frame < 0) {
        detail = "ROM booted but the 68K loop never finished within the frame budget";
        failed = 1;
    } else if (st.count != MB_EXPECT_COUNT) {
        detail = "loop completed but the iteration count is wrong";
        failed = 1;
    }

    res.status = failed ? "FAIL" : "PASS";
    res.name   = "microbench_68k";
    res.detail = detail;
    harness_report(&cfg, &res, 1);
    harness_shutdown(&cfg);
    return failed ? 1 : 0;
}
