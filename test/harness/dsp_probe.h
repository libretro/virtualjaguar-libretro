/*
 * test/harness/dsp_probe.h — DSP state inspection probe.
 *
 * ======================================================================
 * USAGE
 * ======================================================================
 *
 *   #include "harness/harness.h"
 *   #include "harness/dsp_probe.h"
 *
 *   dsp_probe probe;
 *   if (!dsp_probe_init(&probe, &cfg)) return 1;
 *
 *   // After running frames:
 *   dsp_probe_snapshot(&probe);
 *   if (dsp_probe_pc_escaped(&probe))
 *       printf("DSP PC escaped work RAM: 0x%06X\n", probe.snap.pc);
 *
 * ======================================================================
 * WHAT THIS PROBES
 * ======================================================================
 *
 *   - DSP PC (program counter)
 *   - DSP FLAGS register (IMASK, REGPAGE, interrupt enables)
 *   - DSP CONTROL register (running, interrupt latches)
 *   - Register banks 0 and 1 (32 x uint32_t each)
 *   - DSP RAM (8 KB at $F1B000)
 *   - LTXD register value (audio output sample)
 *   - I2S interrupt timer state
 *
 * ======================================================================
 */

#ifndef DSP_PROBE_H
#define DSP_PROBE_H

#include "harness.h"

#define DSP_WORK_RAM_BASE  0x00F1B000
#define DSP_WORK_RAM_END   0x00F1CFFF
#define DSP_RAM_SIZE       8192
#define DSP_NUM_REGS       32

/* Snapshot of DSP state at one point in time */
typedef struct {
    unsigned  frame;
    uint32_t  pc;
    uint32_t  flags;
    uint32_t  control;
    uint32_t  bank0[DSP_NUM_REGS];
    uint32_t  bank1[DSP_NUM_REGS];
    int       running;
    int16_t   ltxd_value;
    int32_t   i2s_timer;
} dsp_snapshot;

/* Counters accumulated across frames */
typedef struct {
    unsigned  ltxd_nonzero_writes;
    unsigned  ltxd_total_samples;
    unsigned  i2s_dispatches;
    unsigned  pc_escape_count;
    uint32_t  first_escape_pc;
    unsigned  first_escape_frame;
} dsp_counters;

/* Resolved symbol pointers */
typedef struct {
    uint32_t *pc;
    uint32_t *control;
    uint32_t *bank0;
    uint32_t *bank1;
    uint8_t *(*get_ram)(void);
    bool    (*is_running)(void);
    uint32_t (*get_flags)(void);
    uint16_t **ltxd;   /* points to the `uint16_t *ltxd` global */
    int32_t  *i2s_timer;
} dsp_symbols;

typedef struct {
    harness_config *cfg;
    dsp_symbols     sym;
    dsp_snapshot    snap;
    dsp_counters    counters;
    /* History ring buffer (last N snapshots for dump-on-failure) */
    dsp_snapshot    history[64];
    unsigned        history_idx;
    unsigned        history_count;
} dsp_probe;

/* Initialize probe — resolves all DSP symbols from loaded core.
 * Returns false if critical symbols missing. */
bool dsp_probe_init(dsp_probe *p, harness_config *cfg);

/* Take a snapshot of current DSP state. */
void dsp_probe_snapshot(dsp_probe *p);

/* Check if DSP PC is outside work RAM. */
bool dsp_probe_pc_escaped(const dsp_probe *p);

/* Count non-zero registers in a bank. */
unsigned dsp_probe_bank_nonzero(const uint32_t bank[DSP_NUM_REGS]);

/* Get LTXD non-zero ratio (0.0–1.0). */
double dsp_probe_ltxd_ratio(const dsp_probe *p);

/* Print a disassembly-style dump of DSP RAM around an address. */
void dsp_probe_dump_ram(const dsp_probe *p, uint32_t addr, unsigned words);

/* Print register bank state. */
void dsp_probe_dump_banks(const dsp_probe *p);

/* Print snapshot in human or JSON format. */
void dsp_probe_print_snapshot(const dsp_probe *p, int json);

/* Convenience: call from a per-frame callback to accumulate counters
 * and detect escapes. Returns false if a fatal condition (PC escape). */
bool dsp_probe_per_frame(dsp_probe *p);

#endif /* DSP_PROBE_H */
