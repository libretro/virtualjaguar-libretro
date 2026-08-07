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
 * CYCLE-DOMAIN CONTRACT (issue #318).  Every charge in this model is a
 * memory-system latency, and memory-system latencies are WALL TIME:
 * they come from DRAM/ROM silicon, which does not speed up when a core
 * option overclocks a processor.  Instruction costs (gpu_opcode_cycles,
 * the UAE 68K's published timings) are CORE TIME and scale with their
 * processor's clock.  Concretely, per charge:
 *
 *   68K per-access wait states  computed in sysclks here, converted to
 *     the 68K's SCALED cycle domain by bus_arbiter_m68k_access() (the
 *     caller passes its clock-scale percent) so the wall time of a wait
 *     state is invariant under the m68k_clock_scale enhancement.
 *   GPU per-access self-cost    returned in sysclks by
 *     bus_arbiter_charge_access(); gpu.c converts to the GPU's scaled
 *     cycle domain at the point it deducts from the slice budget.
 *   OP fetch / row-change / DRAM refresh  charged to the 68K as
 *     m68k_pending_stall and drained by M68KExecuteWithStalls() BEFORE
 *     the 68K clock scale is applied — already wall time.
 *   DSP external accesses       currently pay NOTHING (no charge hook
 *     in dsp.c).  Known asymmetry; the pipeline-hazard work (#313) is
 *     expected to add the DSP-side hook and must place its charges in
 *     this same wall-time domain.
 *
 * One deliberate simplification: the 68K wait-state THRESHOLD
 * (M68K_BUS_CYCLE_SYSCLKS, the stock 8-sysclk bus cycle an access must
 * exceed before it costs anything) stays defined against the STOCK bus
 * cycle even when the 68K is overclocked.  A hypothetical overclocked
 * 68000 would have a shorter bus cycle and see more wait states; the
 * clock scales are enhancement levers documented as "bug reports only
 * valid at 1x", so the model keeps the stock threshold and only makes
 * the charged excess wall-time invariant.
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

    /* 68K self-cost carry: sub-cycle remainder from converting wall
     * sysclks into scaled 68K cycles.  Units are sysclk-hundredths
     * (sysclks x scale_pct), remainder modulo 200 — at stock scale
     * (100) this is exactly the old odd-sysclk carry with values
     * 0/100 instead of 0/1.  Savestate field; an old state's 0/1
     * value loads as a sub-cycle epsilon, which is harmless. */
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
 * bus cycles at `addr` (a longword access passes 2).  Converts wall
 * system clocks into the 68K's cycle domain at `scale_pct` (the
 * m68k_clock_scale percent, 100 = stock): cycles = sysclks x
 * scale_pct / 200, sub-cycle remainder carried in
 * busArbiter.m68k_sysclk_carry.  At 100 this is exactly the old
 * system/2 conversion.  Wall time of a wait state is thus invariant
 * under the clock scale — see the cycle-domain contract above.  Does
 * not check busArbiter.enabled — call sites gate, same as the GPU
 * half. */
uint32_t bus_arbiter_m68k_access(uint32_t addr, uint32_t naccesses,
                                 uint32_t scale_pct);

#ifdef __cplusplus
}
#endif

#endif /* BUS_ARBITER_H */
