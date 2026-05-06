/*
 * bus_arbiter.c — Centralized bus arbitration model.
 *
 * See bus_arbiter.h for design rationale.
 *
 * MEMCON1 default on Jaguar: 0x1861
 *   Bits 5-6 (DRAMSPEED): 0b11 = 5 system clocks per DRAM access
 *   Page miss adds ~4 clocks for RAS precharge + row activation
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
    busArbiter.enabled = 1;
}

void bus_arbiter_reset(void)
{
    int i;
    for (i = 0; i < BM_COUNT; i++)
        busArbiter.bus_cycles[i] = 0;
    busArbiter.op_active = 0;
}

void bus_arbiter_update_memcon(uint16_t memcon1)
{
    uint8_t dramspeed;
    dramspeed = (memcon1 >> 5) & 0x03;
    busArbiter.dram_base_clocks = dramspeed_table[dramspeed];
}

void bus_arbiter_charge(int master, uint32_t sys_clocks)
{
    if (master >= 0 && master < BM_COUNT)
        busArbiter.bus_cycles[master] += sys_clocks;
}

uint32_t bus_arbiter_penalty(int master)
{
    uint32_t higher_total;
    int i;

    if (!busArbiter.enabled)
        return 0;

    higher_total = 0;
    for (i = 0; i < master && i < BM_COUNT; i++)
        higher_total += busArbiter.bus_cycles[i];

    return higher_total;
}

void bus_arbiter_begin_timeslice(void)
{
    int i;
    for (i = 0; i < BM_COUNT; i++)
        busArbiter.bus_cycles[i] = 0;
}

void bus_arbiter_set_op_active(int active)
{
    busArbiter.op_active = (uint8_t)(active != 0);
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
    cost = bus_arbiter_dram_cost(addr);
    if (cost > 0)
        bus_arbiter_charge(master, cost);
    return cost;
}
