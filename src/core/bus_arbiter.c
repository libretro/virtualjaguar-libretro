/*
 * bus_arbiter.c — GPU external-memory access timing.
 *
 * See bus_arbiter.h for scope and history.
 *
 * MiSTer reference: rtl/Rework/arb.v, mem.v, gateway.v
 */

#include "bus_arbiter.h"
#include <string.h>

struct BusArbiter busArbiter;

/* DRAMSPEED field (MEMCON1 bits 5-6) -> row-change (page miss) overhead,
 * in system clocks (precharge + RAS-to-CAS from the JTRM MEMCON1 table):
 *   DRAMSPEED  precharge  RAS-to-CAS  refresh  row-miss overhead
 *       0          4          3          5           7
 *       1          4          3          4           7
 *       2          3          2          4           5
 *       3          2          1          3           3
 * Page-mode (page hit) cycle time is always 2 clocks, fixed -- that is
 * DRAM_PAGE_CYCLE below, independent of DRAMSPEED. */
static const uint8_t dram_row_miss_table[4] = { 7, 7, 5, 3 };

/* Fixed page-mode (page hit) cycle time, per JTRM: "The page mode cycle
 * time is always two clock cycles." */
#define DRAM_PAGE_CYCLE 2

/* ROMSPEED field (MEMCON1 bits 3-4) -> ROM cycle time in system clocks,
 * per JTRM: 0=10, 1=8, 2=6, 3=5.  FASTROM (bit 7) overrides to 2 clocks
 * ("for test purposes only"). */
static const uint8_t rom_speed_table[4] = { 10, 8, 6, 5 };
#define ROM_FASTROM_CLOCKS 2

/* DRAMSPEED field -> cost of one refresh cycle in system clocks (the
 * "refresh" column of the JTRM MEMCON1 table, transcribed above). */
static const uint8_t dram_refresh_table[4] = { 5, 4, 4, 3 };

/* 68K access to GPU/DSP local RAM (an I/O-bus transaction for the 68K,
 * free for the owning RISC's internal bus) and to TOM/JERRY registers.
 * Estimate only: no JTRM figure was found for 68K/RISC-local-RAM access
 * cost.  IOSPEED (MEMCON1 bits 11-12) governs EXTERNAL peripherals, a
 * different bus, so it does not source this number. */
#define IO_BUS_CLOCKS_ESTIMATE 2

/* One MC68000 bus cycle with no wait states, expressed in system clocks:
 * four CPU clocks (S0-S7), and the Jaguar clocks the 68K at half the
 * system clock.  See bus_arbiter_m68k_access() for why the 68K is charged
 * only the excess of an access over this. */
#define M68K_BUS_CYCLE_SYSCLKS 8

void bus_arbiter_init(void)
{
    memset(&busArbiter, 0, sizeof(busArbiter));
    /* Match reset MEMCON1 = 0x1861: DRAMSPEED=3 -> row_miss 3,
     * ROMSPEED=0, FASTROM=0 -> rom_clocks 10. */
    busArbiter.dram_row_miss = 3;
    busArbiter.rom_clocks = 10;
    busArbiter.dram_refresh_clks = 3;
    /* Match TOM reset MEMCON2 = 0x35CC: REFRATE = 5. */
    busArbiter.refrate = 5;
    /* Default OFF: timing modeling is experimental and must be a
     * zero-behavior-change opt-in.  check_variables() enables it when
     * the virtualjaguar_dram_timing core option is set. */
    busArbiter.enabled = 0;
    busArbiter.contention_scale = 1;
}

void bus_arbiter_update_memcon(uint16_t memcon1)
{
    uint8_t dramspeed;
    uint8_t romspeed;
    dramspeed = (memcon1 >> 5) & 0x03;
    romspeed = (memcon1 >> 3) & 0x03;
    busArbiter.dram_row_miss = dram_row_miss_table[dramspeed];
    busArbiter.rom_clocks = (memcon1 & 0x80) ? ROM_FASTROM_CLOCKS
                                              : rom_speed_table[romspeed];
    busArbiter.dram_refresh_clks = dram_refresh_table[dramspeed];
}

static uint32_t bus_arbiter_dram_cost(uint32_t addr)
{
    /* GPU local RAM: 0xF03000-0xF03FFF — no bus transaction */
    if (addr >= 0xF03000 && addr <= 0xF03FFF)
        return 0;

    /* DSP local RAM: 0xF1B000-0xF1CFFF — no bus transaction */
    if (addr >= 0xF1B000 && addr <= 0xF1CFFF)
        return 0;

    /* Main DRAM (0x000000-0x1FFFFF): full DRAM access cost.
     * Use page-miss cost as the average — sequential access patterns
     * would get page hits, but without tracking row state, the miss
     * cost is a reasonable average for scattered GPU access patterns. */
    if (addr < 0x200000)
        return DRAM_PAGE_CYCLE + busArbiter.dram_row_miss;

    /* Cartridge ROM (0x800000-0xDFFFFF): ROMSPEED-derived cost from
     * MEMCON1 bits 3-4 (FASTROM override on bit 7).  ROM is the
     * hottest path here — 68K instruction fetches run out of it. */
    if (addr >= 0x800000 && addr < 0xE00000)
        return busArbiter.rom_clocks;

    /* Bootstrap ROM bank ($E00000-$E3FFFF): same ROMSPEED-derived
     * timing as the cartridge bank (JTRM: both ROM banks share width
     * and speed). */
    if (addr >= 0xE00000 && addr <= 0xE3FFFF)
        return busArbiter.rom_clocks;

    /* TOM/JERRY registers: I/O bus. */
    if (addr >= 0xF00000)
        return IO_BUS_CLOCKS_ESTIMATE;

    /* Default for other addresses (0x200000-0x7FFFFF, 0xE40000-0xEFFFFF):
     * treat as DRAM-equivalent, same approximation as main DRAM above. */
    return DRAM_PAGE_CYCLE + busArbiter.dram_row_miss;
}

uint32_t bus_arbiter_charge_access(int master, uint32_t addr)
{
    uint32_t cost;
    cost = bus_arbiter_dram_cost(addr);
    if (cost == 0)
    {
        /* GPU/DSP local RAM: internal bus for the owning RISC (free),
         * but an I/O-bus transaction for the 68K. */
        if (master != BM_CPU)
            return 0;
        cost = IO_BUS_CLOCKS_ESTIMATE;
    }
    if (busArbiter.contention_scale > 1)
        cost *= busArbiter.contention_scale;
    return cost;
}

uint32_t bus_arbiter_m68k_access(uint32_t addr, uint32_t naccesses)
{
    uint32_t per_access, sysclks, cycles;

    per_access = bus_arbiter_dram_cost(addr);
    if (per_access == 0)
        per_access = IO_BUS_CLOCKS_ESTIMATE;   /* GPU/DSP local RAM: I/O bus for the 68K */

    /* Charge only the WAIT STATES, not the whole access.
     *
     * An MC68000 bus cycle is four CPU clocks (states S0-S7) with no wait
     * states, and the published instruction timings the UAE core returns
     * already include the fetches an instruction performs.  The Jaguar
     * clocks the 68K at half the system clock, so those four CPU clocks
     * are already 8 system clocks of budget.  Charging the absolute
     * access time on top of the datasheet count bills that baseline
     * twice: it made a `subq.l/bne.s` pair out of cart ROM cost ~28
     * cycles instead of 18+2, i.e. 1.49x rather than the correct 1.11x.
     *
     * Consequence worth knowing: at the reset MEMCON1 ($1861) cart ROM is
     * 10 system clocks, so 68K code fetched from ROM pays 2 clocks (one
     * CPU cycle) per access -- while DRAM (2 + row-miss = 5) and the I/O
     * bus (2) are both FASTER than the CPU's own bus cycle and cost it
     * nothing at all.  The 68K is simply not a master that DRAM latency
     * can stall; only slow ROM stalls it.  The GPU half in gpu.c is
     * unaffected -- a RISC LOAD/STORE has no comparable built-in bus
     * cycle to subtract, so it still pays the full access cost. */
    if (per_access <= M68K_BUS_CYCLE_SYSCLKS)
        return 0;
    per_access -= M68K_BUS_CYCLE_SYSCLKS;

    if (busArbiter.contention_scale > 1)
        per_access *= busArbiter.contention_scale;

    sysclks = per_access * naccesses;
    if (sysclks == 0)
        return 0;
    busArbiter.m68k_sysclk_carry += sysclks;
    cycles = busArbiter.m68k_sysclk_carry >> 1;
    busArbiter.m68k_sysclk_carry &= 1;
    return cycles;
}

void bus_arbiter_update_memcon2(uint16_t memcon2)
{
    busArbiter.refrate = (memcon2 >> 8) & 0x0F;
}

void bus_arbiter_op_charge(uint32_t sysclks)
{
    busArbiter.op_clk_accum += sysclks;
}

uint32_t bus_arbiter_op_take(void)
{
    uint32_t clks;
    clks = busArbiter.op_clk_accum;
    busArbiter.op_clk_accum = 0;
    if (busArbiter.contention_scale > 1)
        clks *= busArbiter.contention_scale;
    return clks;
}

uint32_t bus_arbiter_refresh_clocks(uint32_t elapsed_sysclks)
{
    uint32_t period, nrefresh, clks;
    if (busArbiter.refrate == 0)
        return 0;
    period = 64u * (uint32_t)(busArbiter.refrate + 1);
    busArbiter.refresh_clk_carry += elapsed_sysclks;
    nrefresh = busArbiter.refresh_clk_carry / period;
    busArbiter.refresh_clk_carry -= nrefresh * period;
    clks = nrefresh * busArbiter.dram_refresh_clks;
    if (busArbiter.contention_scale > 1)
        clks *= busArbiter.contention_scale;
    return clks;
}
