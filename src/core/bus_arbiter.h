/*
 * bus_arbiter.h — GPU and 68K self-cost access timing (DRAM latency).
 *
 * Extracted from the full bus-arbiter experiment (PR #169) after device
 * testing showed its cross-processor half was harmful: deducting
 * GPU/blitter bus traffic from the 68K's cycle budget slowed innocent
 * 68K-paced work (real-BIOS boot logo animation, BIOS load) and made
 * pacing bursty (penalty applied in per-timeslice chunks).  What
 * survives here is the principled half: a GPU LOAD/STORE that leaves
 * local GPU RAM pays a DRAM access cost in system clocks, derived from
 * MEMCON1, charged to the GPU's OWN cycle budget in gpu.c; the 68K's
 * bus cycles pay their own DRAM cost via bus_arbiter_m68k_access().
 * Neither half can slow any other processor: costs are only ever
 * charged to the owning master's own cycle budget.  The 68K's
 * consumed time IS visible to time-based cross-processor sync points
 * (GPUSyncToM68K in gpu.c, keyed off m68k_cycles_run()) — that
 * coupling reads elapsed 68K time, not raw instruction count, so
 * DRAM wait-states folded into remainingCycles are exactly what it
 * should see.  That is the intended semantics, not a leak.
 *
 * The file keeps its name so the remaining #169 work (OP bus stealing,
 * blitter busy-time, 68K arbitration done right) can grow back around
 * it.
 *
 * MEMCON1 default on Jaguar: 0x1861
 *   Bits 3-4 (ROMSPEED): 0b00 = 10 system clocks per ROM access
 *     (JTRM: 0=10, 1=8, 2=6, 3=5; bit 7 FASTROM overrides to 2, test-only)
 *   Bits 5-6 (DRAMSPEED): 0b11 -> row-change (page miss) overhead of
 *     3 system clocks (precharge 2 + RAS-to-CAS 1, from the JTRM
 *     MEMCON1 table).  Page-mode (page hit) cycle time is always 2
 *     clocks, fixed, regardless of DRAMSPEED.  Full row-miss overhead
 *     table indexed by DRAMSPEED: { 7, 7, 5, 3 }.
 *   This model uses the row-miss cost as an average DRAM access cost
 *   (2 + row-miss) since GPU/68K access patterns are not tracked for
 *   page-hit/page-miss state -- see the DRAM cost model in
 *   bus_arbiter.c.
 */
#ifndef BUS_ARBITER_H
#define BUS_ARBITER_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Bus masters.  Locality is per-master: an address that is free for
 * one master (its own local RAM) is a bus transaction for another. */
enum BusMaster {
    BM_GPU = 0,
    BM_CPU,
    BM_COUNT
};

struct BusArbiter {
    /* DRAM row-change (page miss) overhead in system clocks, derived
     * from MEMCON1 DRAMSPEED (bits 5-6).  Page-mode (page hit) cycle
     * time is a fixed 2 clocks and is not stored here (DRAM_PAGE_CYCLE
     * in bus_arbiter.c).  NOT serialized: recomputed from MEMCON1 via
     * bus_arbiter_update_memcon() on savestate load. */
    uint8_t dram_row_miss;

    /* Cartridge ROM cycle time in system clocks, derived from MEMCON1
     * ROMSPEED (bits 3-4), overridden by FASTROM (bit 7).  NOT
     * serialized: recomputed from MEMCON1 via bus_arbiter_update_memcon()
     * on savestate load. */
    uint8_t rom_clocks;

    /* Cost of ONE refresh cycle in system clocks, derived from MEMCON1
     * DRAMSPEED (the "refresh" column of the JTRM MEMCON1 table).  NOT
     * serialized: recomputed via bus_arbiter_update_memcon(). */
    uint8_t dram_refresh_clks;

    /* MEMCON2 REFRATE (bits 8-11): refresh requests occur at
     * CLK / (64 * (REFRATE+1)); 0 = refresh disabled (JTRM).  NOT
     * serialized: recomputed via bus_arbiter_update_memcon2(). */
    uint8_t refrate;

    /* Feature toggle (from core option). */
    uint8_t enabled;

    /* Calibration multiplier applied to charged cycles.  NOT a user
     * option: settable only via the VJ_DRAM_SCALE env var while the
     * correct physical cost (OP occupancy, load latency, master
     * handoff) is being pinned down against measured game pace. */
    uint8_t contention_scale;

    /* 68K self-cost carry: system clocks not yet converted to a whole
     * 68K cycle (68K runs at system/2, so 0 or 1).  Savestate field. */
    uint32_t m68k_sysclk_carry;

    /* Elapsed system clocks not yet folded into a refresh request
     * (remainder modulo 64*(REFRATE+1)).  Savestate field. */
    uint32_t refresh_clk_carry;

    /* OP bus occupancy accumulated during the current halfline's
     * object-list processing, in system clocks.  Fed by
     * bus_arbiter_op_charge() from the OP's phrase traffic, drained
     * once per halfline by bus_arbiter_op_take().  Savestate field. */
    uint32_t op_clk_accum;

    /* OP-fetch + refresh occupancy (system clocks) waiting to be
     * deducted from the 68K's next execution slice(s) — the 68K is
     * the lowest-priority bus master (JTRM: refresh pri 2, OP pri 6,
     * CPU pri 11).  Savestate field. */
    uint32_t m68k_pending_stall;
};

extern struct BusArbiter busArbiter;

void bus_arbiter_init(void);

/* Called when MEMCON1 is written to recompute DRAM timing. */
void bus_arbiter_update_memcon(uint16_t memcon1);

/* Called when MEMCON2 is written to recompute the refresh rate. */
void bus_arbiter_update_memcon2(uint16_t memcon2);

/* Accumulate OP bus occupancy (system clocks) for the current halfline. */
void bus_arbiter_op_charge(uint32_t sysclks);

/* Drain the OP occupancy accumulator (scaled by contention_scale). */
uint32_t bus_arbiter_op_take(void);

/* Refresh clocks stolen during `elapsed_sysclks` of wall time, per
 * MEMCON2 REFRATE, carrying the sub-period remainder.  Scaled by
 * contention_scale. */
uint32_t bus_arbiter_refresh_clocks(uint32_t elapsed_sysclks);

/* Cost (in system clocks, scaled) the master pays for one access to
 * `addr`.  0 for local RAM. */
uint32_t bus_arbiter_charge_access(int master, uint32_t addr);

/* 68K self-cost: whole 68K cycles to charge for `naccesses` 16-bit
 * bus cycles at `addr` (a longword access passes 2).  Converts system
 * clocks to 68K cycles (system/2), carrying the odd clock in
 * busArbiter.m68k_sysclk_carry.  Does not check busArbiter.enabled —
 * call sites gate, same as the GPU half. */
uint32_t bus_arbiter_m68k_access(uint32_t addr, uint32_t naccesses);

#ifdef __cplusplus
}
#endif

#endif /* BUS_ARBITER_H */
