/*
 * bus_arbiter.h — Centralized bus arbitration model.
 *
 * Models the Jaguar's single 64-bit bus shared by all masters.
 * Based on the MiSTer FPGA implementation (arb.v / gateway.v / mem.v).
 *
 * Rather than cycle-accurate arbitration, this uses an accounting model:
 * each bus master reports its external memory accesses; the arbiter
 * computes how many cycles each master lost to higher-priority traffic.
 */

#ifndef BUS_ARBITER_H
#define BUS_ARBITER_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Bus master IDs, ordered by JTRM priority (0 = highest).
 *
 * JTRM priority table:
 *   1. Refresh
 *   2. DSP DMA
 *   3. GPU DMA
 *   4. Blitter (high priority, BUSHI)
 *   5. Object Processor
 *   6. DSP (normal)
 *   7. CPU interrupt acknowledge
 *   8. GPU (normal)
 *   9. Blitter (normal)
 *  10. CPU (68000)
 */
enum BusMaster {
    BM_REFRESH = 0,
    BM_DSP_DMA,
    BM_GPU_DMA,
    BM_BLITTER_HI,
    BM_OP,
    BM_DSP,
    BM_CPU_IACK,
    BM_GPU,
    BM_BLITTER,
    BM_CPU,
    BM_COUNT
};

struct BusArbiter {
    /* Accumulated bus cycles consumed per master this timeslice.
     * Units: system clocks (26.59 MHz). */
    uint32_t bus_cycles[BM_COUNT];

    /* DRAM timing derived from MEMCON1 DRAMSPEED field.
     * Page hit = base access time; page miss adds RAS precharge. */
    uint8_t dram_base_clocks;
    uint8_t dram_miss_penalty;

    /* OP active display tracking (set during HDB-HDE on visible lines) */
    uint8_t op_active;

    /* Feature toggle (from core option) */
    uint8_t enabled;
};

extern struct BusArbiter busArbiter;

void bus_arbiter_init(void);
void bus_arbiter_reset(void);

/* Called when MEMCON1 is written to recompute DRAM timing. */
void bus_arbiter_update_memcon(uint16_t memcon1);

/* Record that `master` consumed `sys_clocks` of bus bandwidth. */
void bus_arbiter_charge(int master, uint32_t sys_clocks);

/* Compute how many system clocks `master` lost to higher-priority
 * masters this timeslice.  Result is in system clocks. */
uint32_t bus_arbiter_penalty(int master);

/* Reset per-timeslice accumulators. Call at the start of each
 * event timeslice in JaguarExecuteNew(). */
void bus_arbiter_begin_timeslice(void);

/* Set OP active display state (called from TOM halfline callback). */
void bus_arbiter_set_op_active(int active);

/* Return DRAM access cost in system clocks for a given address.
 * Local GPU/DSP RAM returns 0 (no bus transaction).
 * ROM returns ROM speed from MEMCON1. */
uint32_t bus_arbiter_dram_cost(uint32_t addr);

/* Convenience: charge a DRAM access for `master` at `addr`.
 * Returns the cost charged (0 if address is local RAM). */
uint32_t bus_arbiter_charge_access(int master, uint32_t addr);

#ifdef __cplusplus
}
#endif

#endif /* BUS_ARBITER_H */
