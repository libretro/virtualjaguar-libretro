/*
 * bus_arbiter.h — GPU external-memory access timing (DRAM latency).
 *
 * Extracted from the full bus-arbiter experiment (PR #169) after device
 * testing showed its cross-processor half was harmful: deducting
 * GPU/blitter bus traffic from the 68K's cycle budget slowed innocent
 * 68K-paced work (real-BIOS boot logo animation, BIOS load) and made
 * pacing bursty (penalty applied in per-timeslice chunks).  What
 * survives here is the principled half: a GPU LOAD/STORE that leaves
 * local GPU RAM pays a DRAM access cost in system clocks, derived from
 * MEMCON1, charged to the GPU's OWN cycle budget in gpu.c.  It cannot
 * slow any other processor.
 *
 * The file keeps its name so the remaining #169 work (OP bus stealing,
 * blitter busy-time, 68K arbitration done right) can grow back around
 * it.
 *
 * MEMCON1 default on Jaguar: 0x1861
 *   Bits 5-6 (DRAMSPEED): 0b11 = 5 system clocks per DRAM access
 *   Page miss adds ~4 clocks for RAS precharge + row activation
 */
#ifndef BUS_ARBITER_H
#define BUS_ARBITER_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Bus masters (subset — only the GPU charges today; the enum survives
 * so future arbitration work keeps stable identities). */
enum BusMaster {
    BM_GPU = 0,
    BM_COUNT
};

struct BusArbiter {
    /* DRAM timing derived from MEMCON1 DRAMSPEED field.
     * Page hit = base access time; page miss adds RAS precharge. */
    uint8_t dram_base_clocks;
    uint8_t dram_miss_penalty;

    /* Feature toggle (from core option). */
    uint8_t enabled;

    /* Calibration multiplier applied to charged cycles.  NOT a user
     * option: settable only via the VJ_GPU_DRAM_SCALE env var while the
     * correct physical cost (OP occupancy, load latency, master
     * handoff) is being pinned down against measured game pace. */
    uint8_t contention_scale;
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

#ifdef __cplusplus
}
#endif

#endif /* BUS_ARBITER_H */
