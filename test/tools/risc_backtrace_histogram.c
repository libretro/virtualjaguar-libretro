/* test/tools/risc_backtrace_histogram.c -- WHERE do the GPU and DSP
 * interpreters actually spend their instructions?
 *
 * Companion to test/tools/risc_pc_histogram.c, which samples ONE PC per
 * frame (the parked frame-end PC) and therefore measures "where is the
 * processor when retro_run returns".  This tool instead drains the
 * vjtrace GPU/DSP PC-history rings (vjtrace_backtrace(), up to 1024 PCs
 * per processor per frame -- see src/core/vjtrace.h) after every frame,
 * so the histogram covers in-frame execution, not just the final slice.
 * A `jr`-dominated interpreter profile resolves here into the exact
 * spin-loop PCs, each with its loop body disassembled so you can read
 * what the loop is polling.
 *
 * Overlay safety: 3D engines (e.g. Missile Command 3D) swap GPU kernels
 * in and out of local RAM, so code read at exit can be a DIFFERENT
 * program than the one that was hot.  The disassembly context is
 * therefore snapshotted from RISC RAM the first time a PC enters the
 * histogram, and the dump marks a PC whose resident code has since
 * changed.  (If two overlays run hot code at the SAME address, their
 * hits merge into one bucket -- an aliasing this tool reports but
 * cannot split.)
 *
 * Polled-address identification: after every frame the tool also
 * records where each processor is PARKED (gpu_pc / dsp_pc) together
 * with a first-seen snapshot of the 32 current-bank registers, so a
 * spin loop like `load (r0),r2 ; cmp r1,r2 ; jr N,-3` can be resolved
 * to the concrete address in r0.  Registers are read between slices,
 * so the snapshot is architecturally consistent with the parked PC.
 *
 * Sampling model / caveats:
 *   - The ring keeps the LAST 1024 PCs, so each frame contributes a
 *     1024-instruction window biased toward end-of-frame slices.  With
 *     ~400-450k RISC instructions per busy frame that is a ~0.2%
 *     sample; spin loops (thousands of iterations per slice) dominate
 *     any window, which is exactly the signal wanted.
 *   - To avoid re-counting stale history when a processor is halted or
 *     slow, only min(opcode_count_delta, ring_fill) NEWEST entries are
 *     consumed per sample (gpu_exec_opcode_count / dsp_exec_opcode_count
 *     deltas).  Delay-slot instructions advance the opcode counters
 *     without pushing ring entries, so the delta can exceed the pushed
 *     count -- the min() makes that safe (never stale data; at worst
 *     the whole ring is consumed).
 *   - Arming vjtrace force-disables the DSP idle-loop fast-forward
 *     (vjs.riscIdleSkip gate in DSPExec), so this tool always measures
 *     the FULLY INTERPRETED instruction stream -- which matches the
 *     shipping default (the option ships disabled), but NOT a config
 *     where the user enabled it.
 *
 * Needs a TEST_EXPORTS=1 core (vjtrace_arm/vjtrace_backtrace are only
 * exported there); exits loudly otherwise.
 *
 * Build:
 *   make TEST_EXPORTS=1
 *   cc -O2 -Wall -std=c99 -I./libretro-common/include \
 *      -o test/tools/risc_backtrace_histogram \
 *      test/tools/risc_backtrace_histogram.c test/harness/harness.c -ldl -lm
 *
 * Run:
 *   VJ_EXPECT_BUILD=$(./scripts/build-id.sh) \
 *   test/tools/risc_backtrace_histogram ./virtualjaguar_libretro.dylib \
 *      rom.jag --frames 2000 --warmup 900 [--every N] [--top N] [--no-disasm]
 */
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "../harness/harness.h"

/* who values for vjtrace_backtrace(), mirroring the enum in
 * src/core/vjag_memory.h: { UNKNOWN, JAGUAR, DSP, GPU, ... }. */
#define WHO_DSP 2
#define WHO_GPU 3

#define BT_CAP    1024          /* VJT_PCHIST_CAP */
#define NBUCKET   32768u        /* per-processor open-addressing table */
#define CTX_BEFORE 8            /* disasm context: instrs before hot PC */
#define CTX_AFTER  5            /* ... and after (covers jr + delay slot) */
#define CTX_WORDS (CTX_BEFORE + 1 + CTX_AFTER)
#define NPARK     32            /* distinct parked PCs tracked per proc */

typedef void (*backtrace_fn)(int who, uint32_t *out, int maxn, int *count);
typedef uint16_t (*read_word_fn)(uint32_t addr, uint32_t who);
typedef uint32_t (*get_reg_fn)(int reg);

static backtrace_fn  g_backtrace;
static read_word_fn  g_rd;
static uint32_t     *g_gpu_cnt, *g_dsp_cnt;
static uint32_t     *g_gpu_pc,  *g_dsp_pc;
static get_reg_fn    g_gpu_reg, g_dsp_reg;

typedef struct
{
    uint32_t pc[NBUCKET];
    uint64_t n[NBUCKET];
    uint16_t code[NBUCKET][CTX_WORDS];  /* first-sighting snapshot */
    uint64_t total;
    uint32_t last_cnt;          /* opcode counter at previous sample */
} pc_hist;

/* Frame-end parked-PC table: count per distinct parked PC, plus the
 * current-bank register file captured the first time it was seen. */
typedef struct
{
    uint32_t pc[NPARK];
    uint64_t n[NPARK];
    uint32_t regs[NPARK][32];
    unsigned used;
    uint64_t overflow;          /* parks at PCs beyond the NPARK table */
} park_tab;

static pc_hist  hist_gpu, hist_dsp;
static park_tab park_gpu, park_dsp;

static unsigned opt_warmup = 0;
static unsigned opt_every  = 1;
static unsigned opt_top    = 20;
static int      opt_disasm = 1;

static uint32_t ctx_base(uint32_t pc)
{
    return (pc >= 2u * CTX_BEFORE) ? pc - 2u * CTX_BEFORE : 0;
}

static void bump(pc_hist *h, uint32_t pc)
{
    uint32_t start = (pc * 2654435761u) % NBUCKET, i;
    for (i = 0; i < NBUCKET; i++)
    {
        uint32_t k = (start + i) % NBUCKET;
        if (h->n[k] && h->pc[k] != pc)
            continue;
        if (!h->n[k] && g_rd)
        {
            /* New bucket: snapshot the surrounding code NOW, while the
             * overlay that is executing this PC is resident. */
            uint32_t base = ctx_base(pc), w;
            for (w = 0; w < CTX_WORDS; w++)
                h->code[k][w] = g_rd(base + w * 2, 0);
        }
        h->pc[k] = pc;
        h->n[k]++;
        h->total++;
        return;
    }
}

static void park_note(park_tab *t, uint32_t pc, get_reg_fn getreg)
{
    unsigned i;
    for (i = 0; i < t->used; i++)
    {
        if (t->pc[i] == pc)
        {
            t->n[i]++;
            return;
        }
    }
    if (t->used < NPARK)
    {
        int r;
        t->pc[t->used] = pc;
        t->n[t->used]  = 1;
        if (getreg)
            for (r = 0; r < 32; r++)
                t->regs[t->used][r] = getreg(r);
        t->used++;
    }
    else
        t->overflow++;
}

/* Drain one processor's backtrace into its histogram, consuming only
 * the entries new since the previous sample (see header comment). */
static void sample_proc(pc_hist *h, int who, uint32_t *cnt_sym)
{
    uint32_t buf[BT_CAP];
    int got = 0, take, i;
    uint32_t delta;

    g_backtrace(who, buf, BT_CAP, &got);
    if (got <= 0)
        return;
    if (cnt_sym)
    {
        uint32_t cur = *cnt_sym;
        delta = cur - h->last_cnt;      /* wraparound-safe */
        h->last_cnt = cur;
    }
    else
        delta = (uint32_t)got;
    take = (delta < (uint32_t)got) ? (int)delta : got;
    /* buf[] is oldest-first; the newest `take` entries are at the end. */
    for (i = got - take; i < got; i++)
        bump(h, buf[i]);
}

static bool on_frame(void *ud, unsigned frame)
{
    (void)ud;
    if (frame <= opt_warmup)
    {
        /* Keep the "new since last sample" cursors current through the
         * warmup so the first counted sample isn't credited with the
         * whole warmup's opcode delta. */
        if (g_gpu_cnt) hist_gpu.last_cnt = *g_gpu_cnt;
        if (g_dsp_cnt) hist_dsp.last_cnt = *g_dsp_cnt;
        return true;
    }
    if ((frame - opt_warmup) % opt_every)
        return true;
    sample_proc(&hist_gpu, WHO_GPU, g_gpu_cnt);
    sample_proc(&hist_dsp, WHO_DSP, g_dsp_cnt);
    if (g_gpu_pc)
        park_note(&park_gpu, *g_gpu_pc, g_gpu_reg);
    if (g_dsp_pc)
        park_note(&park_dsp, *g_dsp_pc, g_dsp_reg);
    return true;
}

/* ---- RISC disassembler (pattern from test/tools/gpu_disasm_dump.c) ---- */

static const char *mn_gpu[64] = {
"add","addc","addq","addqt","sub","subc","subq","subqt",
"neg","and","or","xor","not","btst","bset","bclr",
"mult","imult","imultn","resmac","imacn","div","abs","sh",
"shlq","shrq","sha","sharq","ror","rorq","cmp","cmpq",
"sat8","sat16","move","moveq","moveta","movefa","movei","loadb",
"loadw","load","loadp","load(r14+n)","load(r15+n)","storeb","storew","store",
"storep","store(r14+n)","store(r15+n)","move pc","jump","jr","mmult","mtoi",
"normi","nop","load(r14+rn)","load(r15+rn)","store(r14+rn)","store(r15+rn)","sat24","pack"
};
/* DSP slots that differ from the GPU (src/jerry/dsp.c dispatch table):
 * 32 subqmod, 33 sat16s, 42 sat32s, 48 mirror, 62 illegal, 63 addqmod. */
static const char *mn_dsp[64] = {
"add","addc","addq","addqt","sub","subc","subq","subqt",
"neg","and","or","xor","not","btst","bset","bclr",
"mult","imult","imultn","resmac","imacn","div","abs","sh",
"shlq","shrq","sha","sharq","ror","rorq","cmp","cmpq",
"subqmod","sat16s","move","moveq","moveta","movefa","movei","loadb",
"loadw","load","sat32s","load(r14+n)","load(r15+n)","storeb","storew","store",
"mirror","store(r14+n)","store(r15+n)","move pc","jump","jr","mmult","mtoi",
"normi","nop","load(r14+rn)","load(r15+rn)","store(r14+rn)","store(r15+rn)","illegal","addqmod"
};

/* Real jr/jump condition-code decode (mirrors build_branch_condition_table
 * in src/tom/gpu.c): bit 4 selects N instead of C for the tests in bits
 * 2-3 -- do NOT mask with &7 (the issue #406 mislabeling bug). */
static void print_cc(unsigned j)
{
    int need_and = 0;
    if (j == 0) { printf("T"); return; }
    if (j & 1) { printf("NZ"); need_and = 1; }
    if (j & 2) { printf("%sZ", need_and ? " " : ""); need_and = 1; }
    if (j & 4) { printf("%s%s", need_and ? " " : "", (j & 0x10) ? "NN" : "NC"); need_and = 1; }
    if (j & 8) { printf("%s%s", need_and ? " " : "", (j & 0x10) ? "N" : "C"); need_and = 1; }
    if (!need_and) printf("cc%u", j);
}

/* Disassemble the snapshotted context window around hot_pc.  The words
 * came from the first-sighting snapshot, so a `movei` immediate that
 * extends past the window end is simply not decoded (shown raw). */
static void disasm_window(uint32_t hot_pc, const uint16_t *code,
                          const char **mn, int overlaid)
{
    uint32_t base = ctx_base(hot_pc);
    int wi;
    if (overlaid)
        printf("      (RISC RAM at this address has been overlaid since "
               "sampling; showing sampled-time code)\n");
    for (wi = 0; wi < CTX_WORDS; wi++)
    {
        uint32_t a = base + (uint32_t)wi * 2;
        uint16_t w = code[wi];
        unsigned op = w >> 10, r1 = (w >> 5) & 0x1F, r2 = w & 0x1F;
        printf("      %c $%06X: %04X  %-14s", (a == hot_pc) ? '*' : ' ',
               a, w, mn[op]);
        if (op == 53)               /* jr */
        {
            int off = (r1 & 0x10) ? (int)r1 - 32 : (int)r1;
            printf(" ");
            print_cc(r2);
            printf(", %+d -> $%06X", off, (uint32_t)(a + 2 + off * 2));
        }
        else if (op == 52)          /* jump */
        {
            printf(" ");
            print_cc(r2);
            printf(", (r%u)", r1);
        }
        else if (op == 38 && wi + 2 < CTX_WORDS)   /* movei */
            printf(" #$%08X, r%u",
                   ((uint32_t)code[wi + 2] << 16) | code[wi + 1], r2);
        else if (op == 35)          /* moveq: literal 0-31 */
            printf(" #%u, r%u", r1, r2);
        else if (op == 31)          /* cmpq: signed -16..15 */
            printf(" #%d, r%u", (int)((r1 ^ 0x10) - 0x10), r2);
        else if (op == 2 || op == 6 || op == 24 || op == 25)
            printf(" #%u, r%u", r1 ? r1 : 32, r2);   /* quick: 0 means 32 */
        else
            printf(" r%u, r%u", r1, r2);
        printf("\n");
    }
}

static const pc_hist *g_sort_hist;
static int cmp_desc(const void *a, const void *b)
{
    uint32_t ia = *(const uint32_t *)a, ib = *(const uint32_t *)b;
    uint64_t na = g_sort_hist->n[ia], nb = g_sort_hist->n[ib];
    return (nb > na) - (nb < na);
}

static void dump_hist(const char *tag, pc_hist *h, const char **mn,
                      uint32_t local_base, uint32_t local_size)
{
    static uint32_t order[NBUCKET];
    unsigned i, shown = 0;
    printf("\n=== %s backtrace-ring PC histogram: %llu sampled instructions ===\n",
           tag, (unsigned long long)h->total);
    if (!h->total)
        return;
    g_sort_hist = h;
    for (i = 0; i < NBUCKET; i++)
        order[i] = i;
    qsort(order, NBUCKET, sizeof(order[0]), cmp_desc);
    for (i = 0; i < NBUCKET && shown < opt_top; i++)
    {
        uint32_t k = order[i];
        if (!h->n[k])
            break;
        printf("  #%-2u $%06X  %10llu  %5.2f%%%s\n", shown + 1, h->pc[k],
               (unsigned long long)h->n[k], 100.0 * (double)h->n[k] / (double)h->total,
               (h->pc[k] >= local_base && h->pc[k] < local_base + local_size)
                   ? "" : "  (outside local RAM)");
        if (opt_disasm && g_rd && shown < 8)
        {
            int overlaid = 0;
            uint32_t hot_wi = (h->pc[k] - ctx_base(h->pc[k])) / 2;
            if (g_rd(h->pc[k], 0) != h->code[k][hot_wi])
                overlaid = 1;
            disasm_window(h->pc[k], h->code[k], mn, overlaid);
        }
        shown++;
    }
}

static void dump_park(const char *tag, const park_tab *t, uint64_t frames)
{
    unsigned i, j;
    if (!frames)
        return;
    printf("\n--- %s frame-end parked PCs (%llu sampled frames%s) ---\n",
           tag, (unsigned long long)frames,
           t->overflow ? ", table overflowed" : "");
    for (i = 0; i < t->used; i++)
    {
        /* Only worth printing when the processor parks there repeatedly. */
        if (t->n[i] * 50 < frames)
            continue;
        printf("  parked $%06X  %llu frames (%.1f%%), first-seen regs:\n",
               t->pc[i], (unsigned long long)t->n[i],
               100.0 * (double)t->n[i] / (double)frames);
        for (j = 0; j < 32; j++)
            printf("    r%-2u=$%08X%s", j, t->regs[i][j],
                   (j % 4 == 3) ? "\n" : "");
    }
}

int main(int argc, char **argv)
{
    harness_config cfg = HARNESS_CONFIG_DEFAULT;
    void (*arm)(void);
    uint64_t sampled_frames;
    int a;
    cfg.frames = 2000;
    for (a = 1; a < argc; a++)
    {
        if (!strcmp(argv[a], "--warmup") && a + 1 < argc)
            opt_warmup = (unsigned)strtoul(argv[++a], NULL, 10);
        else if (!strcmp(argv[a], "--every") && a + 1 < argc)
        {
            opt_every = (unsigned)strtoul(argv[++a], NULL, 10);
            if (!opt_every) opt_every = 1;
        }
        else if (!strcmp(argv[a], "--top") && a + 1 < argc)
        {
            opt_top = (unsigned)strtoul(argv[++a], NULL, 10);
            if (!opt_top) opt_top = 20;
        }
        else if (!strcmp(argv[a], "--no-disasm"))
            opt_disasm = 0;
    }
    if (!harness_init_from_args(&cfg, argc, argv))
        return 1;
    cfg.frame_callback = on_frame;
    if (!harness_load_rom(&cfg))
        return 1;

    arm         = (void (*)(void))harness_dlsym(&cfg, "vjtrace_arm");
    g_backtrace = (backtrace_fn)harness_dlsym(&cfg, "vjtrace_backtrace");
    g_rd        = (read_word_fn)harness_dlsym(&cfg, "JaguarReadWord");
    g_gpu_cnt   = (uint32_t *)harness_dlsym(&cfg, "gpu_exec_opcode_count");
    g_dsp_cnt   = (uint32_t *)harness_dlsym(&cfg, "dsp_exec_opcode_count");
    g_gpu_pc    = (uint32_t *)harness_dlsym(&cfg, "gpu_pc");
    g_dsp_pc    = (uint32_t *)harness_dlsym(&cfg, "dsp_pc");
    g_gpu_reg   = (get_reg_fn)harness_dlsym(&cfg, "GPUGetReg");
    g_dsp_reg   = (get_reg_fn)harness_dlsym(&cfg, "DSPGetReg");
    if (!arm || !g_backtrace)
    {
        fprintf(stderr, "vjtrace_arm/vjtrace_backtrace not exported -- "
                        "build the core with TEST_EXPORTS=1\n");
        return 2;
    }
    /* The GPU/DSP PC-history hooks are disarmed by default (see the
     * PERFORMANCE NOTE in src/core/vjtrace.h); nothing fills the rings
     * until this call. */
    arm();

    harness_run(&cfg);

    sampled_frames = (cfg.frames > opt_warmup)
                     ? (cfg.frames - opt_warmup + opt_every - 1) / opt_every
                     : 0;
    printf("risc_backtrace_histogram: %u frames run, warmup %u, sampled every %u\n",
           cfg.frames, opt_warmup, opt_every);
    if (g_gpu_cnt)
        printf("gpu_exec_opcode_count total=%u\n", *g_gpu_cnt);
    if (g_dsp_cnt)
        printf("dsp_exec_opcode_count total=%u\n", *g_dsp_cnt);
    dump_hist("GPU", &hist_gpu, mn_gpu, 0xF03000, 0x1000);
    dump_park("GPU", &park_gpu, sampled_frames);
    dump_hist("DSP", &hist_dsp, mn_dsp, 0xF1B000, 0x2000);
    dump_park("DSP", &park_dsp, sampled_frames);

    harness_shutdown(&cfg);
    return 0;
}
