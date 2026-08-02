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

    /* bus_arbiter_m68k_access charges only WAIT STATES -- the excess of
     * an access over M68K_BUS_CYCLE_SYSCLKS (8), which is the four CPU
     * clocks an MC68000 bus cycle already takes and which the datasheet
     * instruction timings already include.  It then converts system
     * clocks to 68K cycles (the 68K runs at system/2), carrying an odd
     * clock.  Charging the absolute access cost instead double-counted
     * that baseline. */

    /* Main DRAM at MEMCON1 0x1861 costs 5 sysclks -- FASTER than the
     * CPU's own bus cycle, so the 68K never waits on it. */
    busArbiter.m68k_sysclk_carry = 0;
    check(bus_arbiter_m68k_access(0x00080000, 1) == 0,
          "DRAM (5 sysclks) is faster than a 68K bus cycle: no stall");
    check(busArbiter.m68k_sysclk_carry == 0,
          "no carry from a free access");
    check(bus_arbiter_m68k_access(0x00080000, 2) == 0,
          "longword to DRAM still free");

    /* I/O bus (2 sysclks) likewise costs the 68K nothing. */
    check(bus_arbiter_m68k_access(0x00F00050, 1) == 0,
          "I/O (2 sysclks) does not stall the 68K");

    /* Cart ROM at ROMSPEED=0 costs 10 sysclks: 2 over the baseline,
     * i.e. 1 68K cycle.  This is the only thing that stalls the CPU at
     * the reset MEMCON1 -- and it is why 68K code executing from cart
     * ROM runs measurably slower than its datasheet cycle counts. */
    check(bus_arbiter_m68k_access(0x00800000, 1) == 1,
          "cart ROM (10 sysclks) stalls 1 68K cycle per access");
    check(busArbiter.m68k_sysclk_carry == 0,
          "even wait leaves no carry");
    check(bus_arbiter_m68k_access(0x00800000, 2) == 2,
          "longword from cart ROM stalls 2 68K cycles");

    /* Odd waits carry.  DRAMSPEED=0 gives row-miss 7, so DRAM costs
     * 2 + 7 = 9 sysclks -- 1 over the baseline, an odd number. */
    bus_arbiter_update_memcon((uint16_t)(0x1861 & ~0x0060));
    busArbiter.m68k_sysclk_carry = 0;
    check(bus_arbiter_m68k_access(0x00080000, 1) == 0,
          "slow DRAM: 1 sysclk of wait is less than one 68K cycle");
    check(busArbiter.m68k_sysclk_carry == 1,
          "odd system clock carried");
    check(bus_arbiter_m68k_access(0x00080000, 1) == 1,
          "carry + 1 sysclk completes a 68K cycle");
    check(busArbiter.m68k_sysclk_carry == 0,
          "carry drained");
    bus_arbiter_update_memcon(0x1861);

    /* --- DRAM refresh model (MEMCON2 REFRATE) --------------------- */
    bus_arbiter_update_memcon(0x1861);   /* DRAMSPEED=3 */
    bus_arbiter_update_memcon2(0x35CC);  /* TOM reset value: REFRATE=5 */
    check(busArbiter.refrate == 5, "MEMCON2 0x35CC -> REFRATE 5");
    check(busArbiter.dram_refresh_clks == 3,
          "DRAMSPEED 3 -> refresh cycle costs 3 clocks");
    busArbiter.refresh_clk_carry = 0;
    /* One NTSC halfline = 845 system clocks.  REFRATE=5 -> request
     * period 64*6 = 384 clocks -> 2 refreshes owed, remainder 77,
     * cost 2 * 3 = 6 clocks. */
    check(bus_arbiter_refresh_clocks(845) == 6,
          "845-clock halfline steals 2 refresh cycles (6 clocks)");
    check(busArbiter.refresh_clk_carry == 77,
          "refresh remainder carries across halflines");
    check(bus_arbiter_refresh_clocks(845) == 6,
          "second halfline: (77+845)/384 = 2 again");
    check(busArbiter.refresh_clk_carry == 154, "carry accumulates");
    bus_arbiter_update_memcon2(0x0000);  /* REFRATE=0 */
    check(bus_arbiter_refresh_clocks(10000) == 0,
          "REFRATE 0 disables refresh entirely");
    bus_arbiter_update_memcon2(0x35CC);
    /* Slower DRAM costs more per refresh: DRAMSPEED=0 -> 5 clocks. */
    bus_arbiter_update_memcon(0x1861 & ~0x60);
    check(busArbiter.dram_refresh_clks == 5,
          "DRAMSPEED 0 -> refresh cycle costs 5 clocks");
    bus_arbiter_update_memcon(0x1861);

    /* --- OP occupancy accumulator --------------------------------- */
    busArbiter.contention_scale = 1;
    check(bus_arbiter_op_take() == 0, "OP accumulator starts empty");
    bus_arbiter_op_charge(80);   /* e.g. 40 phrases page-mode */
    bus_arbiter_op_charge(6);    /* row-miss surcharge */
    check(bus_arbiter_op_take() == 86, "OP accumulator sums charges");
    check(bus_arbiter_op_take() == 0, "OP accumulator drains on take");
    /* Calibration scale (dev-only env) multiplies on drain. */
    busArbiter.contention_scale = 4;
    bus_arbiter_op_charge(10);
    check(bus_arbiter_op_take() == 40, "contention_scale applies to OP clocks");
    busArbiter.contention_scale = 1;

    printf("%s: %d failure(s)\n", fails ? "FAIL" : "PASS", fails);
    return fails ? 1 : 0;
}
