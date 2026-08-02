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

/* DRAMSPEED field (MEMCON1 bits 5-6) -> base DRAM clocks per access.
 * JTRM: 00=2, 01=3, 10=4, 11=5 system clocks. */
static const uint8_t dramspeed_table[4] = { 2, 3, 4, 5 };

/* RAS precharge penalty for page miss (row activation + precharge). */
#define PAGE_MISS_PENALTY 4

void bus_arbiter_init(void)
{
    memset(&busArbiter, 0, sizeof(busArbiter));
    busArbiter.dram_base_clocks = 5;
    busArbiter.dram_miss_penalty = PAGE_MISS_PENALTY;
    /* Default OFF: timing modeling is experimental and must be a
     * zero-behavior-change opt-in.  check_variables() enables it when
     * the virtualjaguar_gpu_dram_timing core option is set. */
    busArbiter.enabled = 0;
    busArbiter.contention_scale = 1;
}

void bus_arbiter_update_memcon(uint16_t memcon1)
{
    uint8_t dramspeed;
    dramspeed = (memcon1 >> 5) & 0x03;
    busArbiter.dram_base_clocks = dramspeed_table[dramspeed];
}

uint32_t bus_arbiter_dram_cost(uint32_t addr)
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
        return busArbiter.dram_base_clocks + busArbiter.dram_miss_penalty;

    /* Cartridge ROM (0x800000-0xDFFFFF): similar cost to DRAM.
     * ROMSPEED from MEMCON1 bits 3-4 controls this, but games rarely
     * access ROM from GPU at runtime. Use DRAM base cost. */
    if (addr >= 0x800000 && addr < 0xE00000)
        return busArbiter.dram_base_clocks;

    /* TOM/JERRY registers: ~2 system clocks (I/O bus). */
    if (addr >= 0xF00000)
        return 2;

    /* Default for other addresses */
    return busArbiter.dram_base_clocks;
}

uint32_t bus_arbiter_charge_access(int master, uint32_t addr)
{
    uint32_t cost;
    (void)master;
    cost = bus_arbiter_dram_cost(addr);
    if (cost == 0)
        return 0;
    if (busArbiter.contention_scale > 1)
        cost *= busArbiter.contention_scale;
    return cost;
}
