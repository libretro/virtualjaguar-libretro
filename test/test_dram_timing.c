/* test_dram_timing.c — unit test for the symmetric DRAM self-cost timing
 * (the GPU self-stall and 68K self-cost extracted from the full bus arbiter, PR #169).
 *
 * The model: each master (GPU, 68K) pays DRAM access cost for its own
 * memory accesses — a LOAD/STORE that leaves local RAM pays system
 * clocks (base + page-miss from MEMCON1), multiplied by scale.  Local
 * RISC RAM is free only for the owning processor (internal bus); the
 * 68K accessing RISC local RAM incurs an I/O-bus transaction (~2
 * clocks).  The 68K-side cost is carried across calls to handle the
 * 68K's half-system-clock rate.  Disabled costs nothing anywhere.
 * There is NO cross-processor penalty: charging the 68K for GPU/blitter
 * bus traffic was measured to slow innocent 68K-paced work (real-BIOS
 * boot logo animation) and is deliberately not implemented.
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
    check(busArbiter.dram_row_miss == 3 && busArbiter.rom_clocks == 10,
          "MEMCON1 0x1861 -> DRAMSPEED 0b11 row_miss=3, ROMSPEED 0b00 rom_clocks=10");

    /* FASTROM (bit 7) overrides ROMSPEED to 2 clocks. Check it, then
     * restore power-on timing so it doesn't affect what follows. */
    bus_arbiter_update_memcon(0x1861 | 0x80);
    check(busArbiter.rom_clocks == 2, "FASTROM overrides rom_clocks to 2");
    bus_arbiter_update_memcon(0x1861);

    busArbiter.enabled = 1;
    busArbiter.contention_scale = 1;

    /* Local GPU/DSP RAM are on internal buses: no DRAM transaction. */
    check(bus_arbiter_charge_access(BM_GPU, 0x00F03000) == 0,
          "GPU local RAM costs 0");
    check(bus_arbiter_charge_access(BM_GPU, 0x00F1B000) == 0,
          "DSP local RAM costs 0");

    /* Main DRAM costs page cycle + row-miss overhead (2 + 3 at defaults,
     * per JTRM MEMCON1 DRAMSPEED=0b11 -> precharge 2, RAS-to-CAS 1). */
    ext1 = bus_arbiter_charge_access(BM_GPU, 0x00080000);
    check(ext1 == 5, "main DRAM access costs page+row-miss (5 clocks)");

    /* Cart ROM is ROMSPEED-derived (bits 3-4 = 0 at reset -> 10 clocks). */
    check(bus_arbiter_charge_access(BM_GPU, 0x00800000) == 10,
          "cart ROM access costs ROMSPEED clocks (10 at reset)");
    check(bus_arbiter_charge_access(BM_GPU, 0x00E00000) == 10,
          "bootstrap ROM bank uses ROMSPEED timing");
    check(bus_arbiter_charge_access(BM_GPU, 0x00F00050) == 2,
          "TOM/JERRY register access costs 2 clocks");

    /* Calibration scale multiplies the charge. */
    busArbiter.contention_scale = 8;
    scaled = bus_arbiter_charge_access(BM_GPU, 0x00080000);
    check(scaled == ext1 * 8, "scale 8x multiplies the DRAM cost by 8");

    /* Slower DRAMSPEED settings track MEMCON1. */
    bus_arbiter_update_memcon(0x0000);   /* DRAMSPEED 0b00 -> row-miss 7 */
    busArbiter.contention_scale = 1;
    check(bus_arbiter_charge_access(BM_GPU, 0x00080000) == 9,
          "DRAMSPEED 0b00 -> 2+7 clocks");

    /* ---- 68K half (symmetric self-cost) ---- */

    /* Restore power-on timing for the 68K checks. */
    bus_arbiter_update_memcon(0x1861);
    busArbiter.contention_scale = 1;

    /* GPU/DSP local RAM is free only for the owning RISC.  For the 68K
     * it is modeled as an I/O-bus transaction: IO_BUS_CLOCKS_ESTIMATE
     * (2, unsourced -- no JTRM figure found for 68K access to RISC
     * local RAM; this is a model assertion, not a hardware fact). */
    check(bus_arbiter_charge_access(BM_CPU, 0x00F03000) == 2,
          "68K access to GPU local RAM charged IO_BUS_CLOCKS_ESTIMATE (2, unsourced)");
    check(bus_arbiter_charge_access(BM_CPU, 0x00F1B000) == 2,
          "68K access to DSP local RAM charged IO_BUS_CLOCKS_ESTIMATE (2, unsourced)");
    check(bus_arbiter_charge_access(BM_GPU, 0x00F03000) == 0,
          "GPU access to GPU local RAM still free");

    /* bus_arbiter_m68k_access converts system clocks to 68K cycles
     * (68K runs at system/2) with a carry for the odd clock.
     * DRAM cost at 0x1861 = 5 sysclks: 2 cycles carry 1, then 3. */
    busArbiter.m68k_sysclk_carry = 0;
    check(bus_arbiter_m68k_access(0x00080000, 1) == 2,
          "5 sysclks -> 2 68K cycles, carry 1");
    check(busArbiter.m68k_sysclk_carry == 1,
          "odd system clock carried");
    check(bus_arbiter_m68k_access(0x00080000, 1) == 3,
          "carry+5 sysclks -> 3 68K cycles, carry 0");
    check(busArbiter.m68k_sysclk_carry == 0,
          "carry drained");

    /* A longword is two 16-bit bus cycles. */
    check(bus_arbiter_m68k_access(0x00080000, 2) == 5,
          "2 accesses = 10 sysclks -> 5 68K cycles");

    /* I/O access: 2 sysclks -> 1 cycle, no carry. */
    check(bus_arbiter_m68k_access(0x00F00050, 1) == 1,
          "I/O 2 sysclks -> 1 68K cycle");
    check(busArbiter.m68k_sysclk_carry == 0,
          "no carry from even cost");

    printf("%s: %d failure(s)\n", fails ? "FAIL" : "PASS", fails);
    return fails ? 1 : 0;
}
