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
 *   page-hit/page-miss state — see bus_arbiter_dram_cost().
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
};

extern struct BusArbiter busArbiter;

void bus_arbiter_init(void);

/* Called when MEMCON1 is written to recompute DRAM timing. */
void bus_arbiter_update_memcon(uint16_t memcon1);

/* Return DRAM access cost in system clocks for a given address.
 * Local GPU/DSP RAM returns 0 (no bus transaction). */
uint32_t bus_arbiter_dram_cost(uint32_t addr);

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
