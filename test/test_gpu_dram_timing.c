/* test_gpu_dram_timing.c — unit test for the GPU DRAM-timing charge
 * (the GPU self-stall extracted from the full bus arbiter, PR #169).
 *
 * The model: a GPU LOAD/STORE that leaves local GPU RAM pays a DRAM
 * access cost in system clocks (base + page-miss from MEMCON1),
 * multiplied by the configurable scale.  Local GPU RAM costs nothing.
 * Disabled costs nothing anywhere.  There is NO cross-processor
 * penalty: the 68K/blitter halves of the arbiter were measured to slow
 * innocent 68K-paced work (real-BIOS boot logo animation) and are
 * deliberately not part of this.
 *
 * Links bus_arbiter.c directly (like test_jlink does jlink.c).
 */
#include <stdio.h>
#include <stdint.h>
#include "../src/core/bus_arbiter.h"

static int fails = 0;

static void check(int cond, const char *what)
{
    printf("%s %s\n", cond ? "PASS" : "FAIL", what);
    if (!cond) fails++;
}

int main(void)
{
    uint32_t ext1, scaled;

    bus_arbiter_init();
    check(busArbiter.enabled == 0, "init: disabled by default");
    bus_arbiter_update_memcon(0x1861);   /* power-on default */
    check(busArbiter.dram_base_clocks == 5,
          "MEMCON1 0x1861 -> DRAMSPEED 0b11 -> 5 clocks");

    busArbiter.enabled = 1;
    busArbiter.contention_scale = 1;

    /* Local GPU/DSP RAM are on internal buses: no DRAM transaction. */
    check(bus_arbiter_charge_access(BM_GPU, 0x00F03000) == 0,
          "GPU local RAM costs 0");
    check(bus_arbiter_charge_access(BM_GPU, 0x00F1B000) == 0,
          "DSP local RAM costs 0");

    /* Main DRAM costs base + page-miss average (5 + 4 at defaults). */
    ext1 = bus_arbiter_charge_access(BM_GPU, 0x00080000);
    check(ext1 == 9, "main DRAM access costs base+miss (9 clocks)");

    /* Cart ROM and I/O cost less than DRAM-with-miss but not zero. */
    check(bus_arbiter_charge_access(BM_GPU, 0x00800000) == 5,
          "cart ROM access costs base clocks");
    check(bus_arbiter_charge_access(BM_GPU, 0x00F00050) == 2,
          "TOM/JERRY register access costs 2 clocks");

    /* Calibration scale multiplies the charge. */
    busArbiter.contention_scale = 8;
    scaled = bus_arbiter_charge_access(BM_GPU, 0x00080000);
    check(scaled == ext1 * 8, "scale 8x multiplies the DRAM cost by 8");

    /* Slower DRAMSPEED settings track MEMCON1. */
    bus_arbiter_update_memcon(0x0000);   /* DRAMSPEED 0b00 = 2 clocks */
    busArbiter.contention_scale = 1;
    check(bus_arbiter_charge_access(BM_GPU, 0x00080000) == 6,
          "DRAMSPEED 0b00 -> 2+4 clocks");

    printf("%s: %d failure(s)\n", fails ? "FAIL" : "PASS", fails);
    return fails ? 1 : 0;
}
