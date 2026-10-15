/*
 * test/tools/gpu_idle_probe_falsify.c — falsification test for the GPU
 * idle-loop fast-forward's admission rule (issue #569, GPU port).
 * Mirror of test/tools/dsp_idle_probe_falsify.c; read that header for
 * the full argument.  The GPU-specific twist this test pins down:
 *
 * gpu_exec_opcode_count is incremented ONLY in GPUExec's main loop --
 * the delay slot inlined by gpu_opcode_jr/jump is NOT counted (unlike
 * the DSP's, which is).  The executed-path identity is therefore
 * opcost == idleBodyCount on the GPU, not idleBodyCount + 1, and an
 * off-by-one in either direction has a distinct failure mode:
 *   - requiring idleBodyCount + 1 would make the probe reject every
 *     genuine loop, silently disabling the feature (program A guards);
 *   - a laxer check would admit the compound-period exploit hiding an
 *     undecoded store behind a not-taken jr (program B guards).
 *
 *   A. a genuinely idle loop  ->  the probe MUST fire.
 *      addqt #1,r0 ; jr always,-2 ; (ds) nop
 *
 *   B. the compound-period exploit  ->  the probe MUST NOT fire.
 *      head:  subq #1,r1
 *      jr:    jr NE,head
 *             nop                  <- delay slot, decoded
 *             store r3,(r4)        <- NEVER DECODED
 *             movei #2,r1          <- NEVER DECODED
 *             jump always,(r5)     <- NEVER DECODED, r5 = head
 *             nop
 *      Every arrival sees r1 == 1, all 64 register deltas are zero,
 *      the flags match, cost and opcode cost are uniform, the decoded
 *      portion is whitelisted.  Only the executed-path check (opcost
 *      == idleBodyCount: here 7 counted opcodes vs the 2 a
 *      straight-line body charges) rejects it.
 *
 *   C. the late-load loop (issue #811)  ->  the probe MUST NOT fire.
 *      head:  load (r2),r3      ; external (main RAM) load, data = $F02114
 *             moveq #0,r3       ; TOM bug 13: the load data lands AFTER this
 *             load (r3),r4      ; model says EA 0 (plain RAM); silicon and
 *                               ; the interpreter read G_CTRL
 *             jr always,head ; (ds) nop
 *      gpu_idle_check_body walks the body in program order, so it proves
 *      r3 == 0 at the second load and admits the loop -- but the
 *      interpreter lets the load data win (gpu_load_wb_fixups), so the
 *      real EA is register space and the dataflow proof is about a
 *      different loop.  The probe rejects any loop in which a fix-up
 *      fired between its snapshots.  Program D is the control: the same
 *      loop with the load aimed at r5 (no overwrite, no fix-up) is a
 *      genuine idle loop and MUST still fire, so the guard is shown to
 *      key on the fix-up and not on external loads in general.
 *
 *   E-M. admission of local-RAM long stores (issue #952).  Each program
 *      is a wait loop whose body contains a store.  The fire/no-fire
 *      verdict is checked, and every program that fires is also run
 *      with the skip switched off and the two end states (both register
 *      banks, all 4K of GPU local RAM, PC, flags-visible opcode count)
 *      compared bit for bit -- the skip is only worth having if it is
 *      invisible.
 *        E. Doom's frame-wait loop verbatim            -> fires, exact
 *        F. same, value register preloaded and the loop's own load
 *           rewrites it (stored value changes 1 -> 0)   -> fires, exact
 *        G. store_r14_indexed / store_r15_ri forms     -> fire, exact
 *        H. the store hits the long the loop polls     -> must NOT fire
 *        I. the store lands inside the loop's own code -> must NOT fire
 *           (also with the bytes written back unchanged, which is a
 *           fixed point only the code-overlap rule rules out)
 *        J. store to a hardware register (G_FLAGS)     -> must NOT fire
 *        K. store to main DRAM                         -> must NOT fire
 *        L. a counter feeds the stored value           -> must NOT fire
 *        M. unaligned local-RAM address                -> must NOT fire
 *
 * Unlike the DSP test, gpu_control is static, so the GPU is started
 * through the G_CTRL MMIO write path (GPUWriteLong), and yarc's GPU
 * program actively uses interrupts -- GPUSetFlags(0) masks INT_ENA0-4
 * first so GPUExec's slice-entry GPUHandleIRQs cannot hijack the PC
 * into a vector mid-test.
 *
 * Build: cc -O2 -Wall -std=c99 $(INCFLAGS) -o test/tools/gpu_idle_probe_falsify \
 *          test/tools/gpu_idle_probe_falsify.c test/harness/harness.c -ldl -lm
 * Needs the wide test ABI (make TEST_EXPORTS=1).  Uses test/roms/yarc.j64,
 * which is in-tree, so this never skips.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#include "../harness/harness.h"
#include "src/core/settings.h"       /* struct VJSettings: riscIdleSkip */

/* RISC opcode indices (src/tom/gpu.c gpu_dispatch[]) */
#define OP_ADDQT   3
#define OP_SUBQ    6
#define OP_STORE  47
#define OP_JUMP   52
#define OP_JR     53
#define OP_NOP    57
#define OP_MOVEI  38
#define OP_MOVEQ  35
#define OP_LOAD   41
#define OP_OR     10
#define OP_STORE_R14_INDEXED 49
#define OP_STORE_R15_RI      61
#define CC_Z       2

/* Branch condition codes: 0 = always, 1 = NE (fails when Z is set). */
#define CC_ALWAYS  0
#define CC_NE      1

#define GPU_RAM_BASE 0x00F03000u
#define G_CTRL       0x00F02114u

/* High in the 4K local RAM, clear of the ISR vectors ($F03010-$F0305F)
 * and of yarc's own spin code around $F03192. */
#define PROG_A_OFF   0x0E00          /* $F03E00 */
#define PROG_B_OFF   0x0E80          /* $F03E80 */
#define SCRATCH_OFF  0x0F80          /* $F03F80 -- the exploit's store target */
#define PROG_C_OFF   0x0F00          /* $F03F00 */
#define MAINRAM_PTR  0x00100000u     /* main-RAM long holding G_CTRL's address */
#define G_CTRL_ADDR  0x00F02114u

static uint8_t  *gpu_ram;
static uint32_t *p_bank0;
static uint32_t *p_bank1;
static uint32_t *p_fires;
static uint32_t *p_rejects;
static uint32_t *p_opcount;
static uint64_t *p_wbfix;
static uint8_t **p_mainram_slot;     /* jaguarMainRAM is a POINTER */
static uint32_t (*p_readlong)(uint32_t, uint32_t);
static void    (*p_gpuexec)(int32_t);
static void    (*p_setpc)(uint32_t);
static void    (*p_setflags)(uint32_t);
static void    (*p_writelong)(uint32_t, uint32_t, uint32_t);

static uint16_t enc(uint32_t idx, uint32_t p1, uint32_t p2)
{
    return (uint16_t)((idx << 10) | ((p1 & 0x1F) << 5) | (p2 & 0x1F));
}

static void put16(uint32_t off, uint16_t w)
{
    gpu_ram[off]     = (uint8_t)(w >> 8);
    gpu_ram[off + 1] = (uint8_t)(w & 0xFF);
}

/* Write the same value into both banks: which one is current depends on
 * G_FLAGS REGPAGE/IMASK, which is not exported.  Writing both makes the
 * setup correct either way. */
static void setreg(unsigned n, uint32_t v)
{
    p_bank0[n] = v;
    p_bank1[n] = v;
}

static void build_prog_a(void)
{
    /* addqt #1,r0 ; jr always,-2 ; (ds) nop
     * jr offset -2 -> target = (jr+2) + (-2*2) = jr - 2 = head. */
    put16(PROG_A_OFF + 0, enc(OP_ADDQT, 1, 0));
    put16(PROG_A_OFF + 2, enc(OP_JR, 0x1E /* -2 */, CC_ALWAYS));
    put16(PROG_A_OFF + 4, enc(OP_NOP, 0, 0));
}

static void build_prog_b(void)
{
    put16(PROG_B_OFF +  0, enc(OP_SUBQ, 1, 1));               /* subq #1,r1     */
    put16(PROG_B_OFF +  2, enc(OP_JR, 0x1E /* -2 */, CC_NE)); /* jr ne,head     */
    put16(PROG_B_OFF +  4, enc(OP_NOP, 0, 0));                /* (delay slot)   */
    put16(PROG_B_OFF +  6, enc(OP_STORE, 4, 3));              /* store r3,(r4)  */
    put16(PROG_B_OFF +  8, enc(OP_MOVEI, 0, 1));              /* movei #2,r1    */
    put16(PROG_B_OFF + 10, 0x0002);                           /*   LSW          */
    put16(PROG_B_OFF + 12, 0x0000);                           /*   MSW          */
    put16(PROG_B_OFF + 14, enc(OP_JUMP, 5, CC_ALWAYS));       /* jump (r5)      */
    put16(PROG_B_OFF + 16, enc(OP_NOP, 0, 0));                /* (delay slot)   */

    setreg(1, 2);                                  /* loop counter             */
    setreg(3, 0xDEADBEEFu);                        /* value the store writes   */
    setreg(4, GPU_RAM_BASE + SCRATCH_OFF);         /* store destination        */
    setreg(5, GPU_RAM_BASE + PROG_B_OFF);          /* jump target = head       */
}

static void build_prog_c(uint32_t load_dst)
{
    /* load (r2),rD ; moveq #0,r3 ; load (r3),r4 ; jr always,head ; nop
     * jr offset -4 -> target = (jr+2) + (-4*2) = jr - 6 = head.
     * C: rD = r3 (the overwritten register) -- the fix-up arms.
     * D: rD = r5 -- nothing overwrites the load target, no fix-up. */
    put16(PROG_C_OFF + 0, enc(OP_LOAD, 2, load_dst));
    put16(PROG_C_OFF + 2, enc(OP_MOVEQ, 0, 3));
    put16(PROG_C_OFF + 4, enc(OP_LOAD, 3, 4));
    put16(PROG_C_OFF + 6, enc(OP_JR, 0x1C /* -4 */, CC_ALWAYS));
    put16(PROG_C_OFF + 8, enc(OP_NOP, 0, 0));

    setreg(2, MAINRAM_PTR);                        /* load address (RAM)       */
    setreg(3, 0);
    setreg(4, 0);
    setreg(5, 0);
    /* Big-endian long in main RAM: the value the late load delivers. */
    (*p_mainram_slot)[MAINRAM_PTR + 0] = (uint8_t)(G_CTRL_ADDR >> 24);
    (*p_mainram_slot)[MAINRAM_PTR + 1] = (uint8_t)(G_CTRL_ADDR >> 16);
    (*p_mainram_slot)[MAINRAM_PTR + 2] = (uint8_t)(G_CTRL_ADDR >> 8);
    (*p_mainram_slot)[MAINRAM_PTR + 3] = (uint8_t)(G_CTRL_ADDR);
}

static uint32_t run_prog(uint32_t off, int32_t cycles,
                         uint32_t *rejects_out, uint32_t *opcodes_out);

/* ---- store-admission programs (E-M) ------------------------------- */

#define PROG_S_OFF   0x0C00          /* $F03C00: stays clear of vectors   */
#define FLAG_OFF     0x0F90          /* $F03F90: the long the loop stores */
#define POLL_OFF     0x0F94          /* $F03F94: the long the loop polls  */

#define JR_Z_BACK(words) enc(OP_JR, (uint32_t)(32 - (words)) & 0x1F, CC_Z)

/* movei #imm,rN  (3 words) */
static uint32_t emit_movei(uint32_t at, uint32_t rn, uint32_t imm)
{
    put16(at,     enc(OP_MOVEI, 0, rn));
    put16(at + 2, (uint16_t)(imm & 0xFFFF));
    put16(at + 4, (uint16_t)(imm >> 16));
    return at + 6;
}

/* Which variant of the store loop to assemble. */
typedef enum {
    ST_DOOM,        /* Doom's loop verbatim                              */
    ST_VALUE_LOADED,/* r1 preloaded to 1, rewritten by the loop's load   */
    ST_R14_INDEXED, /* store_r14_indexed form                            */
    ST_R15_RI,      /* store_r15_ri form                                 */
    ST_ALIAS_POLL,  /* store aimed at the polled long                    */
    ST_IN_CODE,     /* store aimed at the loop's own code                */
    ST_CODE_SAME,   /* same, writing back the bytes already there        */
    ST_REGISTER,    /* store aimed at G_FLAGS                            */
    ST_DRAM,        /* store aimed at main DRAM                          */
    ST_COUNTER,     /* stored value is a running counter                 */
    ST_UNALIGNED    /* store aimed at flag+1                             */
} st_kind;

/* Assemble the loop and its registers.  Common shape:
 *     [movei #addr,r7 ; moveq #1,r1]   <- Doom/variants set these in-loop
 *     store r1,(r7)
 *     movei #POLL,r2 ; load (r2),r1 ; or r1,r1
 *     jr Z,head ; nop
 * The poll long holds 0, so Z stays set and the loop never exits. */
static void build_prog_store(st_kind k)
{
    uint32_t at = PROG_S_OFF, head = PROG_S_OFF, jr, words;
    uint32_t code_same_off = 0;
    uint32_t addr = GPU_RAM_BASE + FLAG_OFF;

    if (k == ST_ALIAS_POLL)  addr = GPU_RAM_BASE + POLL_OFF;
    if (k == ST_IN_CODE)     addr = GPU_RAM_BASE + PROG_S_OFF + 4;
    if (k == ST_REGISTER)    addr = 0x00F02100u;
    if (k == ST_DRAM)        addr = 0x00100100u;
    if (k == ST_UNALIGNED)   addr = GPU_RAM_BASE + FLAG_OFF + 1;

    setreg(2, 0);
    setreg(1, 1);
    gpu_ram[POLL_OFF + 0] = gpu_ram[POLL_OFF + 1] = 0;
    gpu_ram[POLL_OFF + 2] = gpu_ram[POLL_OFF + 3] = 0;
    gpu_ram[FLAG_OFF + 0] = gpu_ram[FLAG_OFF + 1] = 0xAA;
    gpu_ram[FLAG_OFF + 2] = gpu_ram[FLAG_OFF + 3] = 0xAA;

    switch (k) {
    case ST_VALUE_LOADED:
        setreg(7, addr);                          /* r1 = 1 preloaded     */
        put16(at, enc(OP_STORE, 7, 1));  at += 2;
        break;
    case ST_R14_INDEXED:
        setreg(14, addr - 8);                     /* + (2 << 2) = addr    */
        put16(at, enc(OP_STORE_R14_INDEXED, 2, 1)); at += 2;
        break;
    case ST_R15_RI:
        setreg(15, addr - 16);
        setreg(8, 16);
        put16(at, enc(OP_STORE_R15_RI, 8, 1)); at += 2;
        break;
    case ST_COUNTER:
        setreg(3, 0);
        put16(at, enc(OP_ADDQT, 1, 3)); at += 2;  /* addqt #1,r3          */
        setreg(7, addr);
        put16(at, enc(OP_STORE, 7, 3));  at += 2; /* store r3,(r7)        */
        break;
    default:
        at = emit_movei(at, 7, addr);
        /* The aliasing case stores 0 into a poll long that already holds
         * 0, so the loop keeps spinning (a 1 would end the wait and the
         * program would run off into empty RAM) and only the alias rule
         * stands between it and the skip. */
        put16(at, enc(OP_MOVEQ, k == ST_ALIAS_POLL ? 0 : 1, 1));  at += 2;
        if (k == ST_CODE_SAME) {
            /* store r5,(r6): r6 = this very long (the store opcode and
             * the movei opcode behind it), r5 = the bytes already there,
             * so the write changes nothing and only the code-overlap rule
             * keeps the loop out. */
            code_same_off = at;
            put16(at, enc(OP_STORE, 6, 5));
        } else
            put16(at, enc(OP_STORE, 7, 1));
        at += 2;
        break;
    }
    at = emit_movei(at, 2, GPU_RAM_BASE + POLL_OFF);
    put16(at, enc(OP_LOAD, 2, 1));       at += 2;
    put16(at, enc(OP_OR, 1, 1));         at += 2;
    jr = at;
    words = (jr + 2 - head) / 2;                 /* back to head, in words */
    put16(jr,     JR_Z_BACK(words));
    put16(jr + 2, enc(OP_NOP, 0, 0));
    if (k == ST_CODE_SAME) {
        setreg(6, GPU_RAM_BASE + code_same_off);
        setreg(5, ((uint32_t)gpu_ram[code_same_off] << 24)
                | ((uint32_t)gpu_ram[code_same_off + 1] << 16)
                | ((uint32_t)gpu_ram[code_same_off + 2] << 8)
                |  (uint32_t)gpu_ram[code_same_off + 3]);
    }
}

typedef struct {
    uint32_t bank0[32], bank1[32];
    uint8_t  ram[0x1000];
    uint32_t pc, opcount, fires, rejects;
} gpu_snap;

static uint32_t (*p_getpc)(void);

static void take_snap(gpu_snap *g, uint32_t fires, uint32_t rejects,
                      uint32_t ops)
{
    memcpy(g->bank0, p_bank0, sizeof g->bank0);
    memcpy(g->bank1, p_bank1, sizeof g->bank1);
    memcpy(g->ram, gpu_ram, sizeof g->ram);
    g->pc      = p_getpc();
    g->opcount = ops;
    g->fires   = fires;
    g->rejects = rejects;
}

/* Run program `k` with the skip on and (if it fired) again with it off.
 * Returns the number of fires; *exact is set to 1 when the two end
 * states are bit-identical (or when there was nothing to compare). */
static uint32_t run_store_prog(st_kind k, struct VJSettings *vjs_p,
                               int *exact, uint32_t *rej_out, int *fired_mem)
{
    gpu_snap on, off;
    uint32_t fires, rej, ops;

    build_prog_store(k);
    fires = run_prog(PROG_S_OFF, 20000, &rej, &ops);
    take_snap(&on, fires, rej, ops);
    *rej_out = rej;
    *exact = 1;
    if (fired_mem)
        *fired_mem = ((uint32_t)gpu_ram[FLAG_OFF] << 24
                      | (uint32_t)gpu_ram[FLAG_OFF + 1] << 16
                      | (uint32_t)gpu_ram[FLAG_OFF + 2] << 8
                      | (uint32_t)gpu_ram[FLAG_OFF + 3]);

    if (fires > 0) {
        vjs_p->riscIdleSkip = false;
        build_prog_store(k);
        run_prog(PROG_S_OFF, 20000, NULL, &ops);
        vjs_p->riscIdleSkip = true;
        take_snap(&off, 0, 0, ops);
        /* Opcode counts are compared too: the skip advances
         * gpu_exec_opcode_count by exactly what the interpreter would. */
        *exact = (memcmp(on.bank0, off.bank0, sizeof on.bank0) == 0
                  && memcmp(on.bank1, off.bank1, sizeof on.bank1) == 0
                  && memcmp(on.ram, off.ram, sizeof on.ram) == 0
                  && on.pc == off.pc && on.opcount == off.opcount);
    }
    return fires;
}

/* Run one hand-assembled program from `off` for `cycles`, returning how
 * many times the fast-forward fired. */
static uint32_t run_prog(uint32_t off, int32_t cycles,
                         uint32_t *rejects_out, uint32_t *opcodes_out)
{
    uint32_t f0 = *p_fires, r0 = *p_rejects, o0 = *p_opcount;

    /* Mask every GPU interrupt enable (raw poke -- no MMIO decode side
     * effects) so the slice-entry GPUHandleIRQs cannot vector away. */
    p_setflags(0);
    p_setpc(GPU_RAM_BASE + off);
    p_writelong(G_CTRL, 0x01, 0);                  /* GPUGO */
    p_gpuexec(cycles);

    if (rejects_out)
        *rejects_out = *p_rejects - r0;
    if (opcodes_out)
        *opcodes_out = *p_opcount - o0;
    return *p_fires - f0;
}

int main(int argc, char **argv)
{
    harness_config cfg = HARNESS_CONFIG_DEFAULT;
    harness_result res[16];
    unsigned nres = 0;
    uint32_t fires_a, fires_b, rej_a, rej_b, ops_a, ops_b;
    uint32_t fires_c, fires_d, rej_c, rej_d;
    uint64_t wb_c, wb_d, wb0;
    static char detail_a[192], detail_b[192], detail_c[192];
    static char detail_e[256], detail_f[256];
    int ok_a, ok_b, ok_c, ok_d;
    struct VJSettings *vjs_p;
    static const struct {
        st_kind k; const char *name; int must_fire;
    } store_cases[] = {
        { ST_DOOM,         "gpu_idle_store_doom_loop_fires_exact",       1 },
        { ST_VALUE_LOADED, "gpu_idle_store_value_rewritten_fires_exact", 1 },
        { ST_R14_INDEXED,  "gpu_idle_store_r14_indexed_fires_exact",     1 },
        { ST_R15_RI,       "gpu_idle_store_r15_ri_fires_exact",          1 },
        { ST_ALIAS_POLL,   "gpu_idle_store_aliasing_poll_rejected",      0 },
        { ST_IN_CODE,      "gpu_idle_store_into_own_code_rejected",      0 },
        { ST_CODE_SAME,    "gpu_idle_store_same_bytes_into_code_rejected", 0 },
        { ST_DRAM,         "gpu_idle_store_to_dram_rejected",            0 },
        { ST_COUNTER,      "gpu_idle_store_of_counter_rejected",         0 },
        { ST_UNALIGNED,    "gpu_idle_store_unaligned_rejected",          0 },
        { ST_REGISTER,     "gpu_idle_store_to_register_rejected",        0 }
    };
    static char store_detail[11][256];
    int sc, all_store_ok = 1;

    cfg.frames = 2;
    cfg.quiet  = 1;
    if (!harness_init_from_args(&cfg, argc, argv))
        return 1;
    /* The feature is off by default; this test is about the admission
     * rule, so turn it on explicitly. */
    harness_set_option(&cfg, "virtualjaguar_risc_idle_skip", "enabled");
    if (!harness_load_rom(&cfg))
        return 1;
    harness_run(&cfg);                             /* let options apply */

    gpu_ram     = (uint8_t *)((uint8_t *(*)(void))harness_dlsym(&cfg, "GPUGetRAM"))();
    p_bank0     = (uint32_t *)harness_dlsym(&cfg, "gpu_reg_bank_0");
    p_bank1     = (uint32_t *)harness_dlsym(&cfg, "gpu_reg_bank_1");
    p_fires     = (uint32_t *)harness_dlsym(&cfg, "gpu_idle_skip_fires");
    p_rejects   = (uint32_t *)harness_dlsym(&cfg, "gpu_idle_skip_rejects");
    p_opcount   = (uint32_t *)harness_dlsym(&cfg, "gpu_exec_opcode_count");
    p_wbfix     = (uint64_t *)harness_dlsym(&cfg, "gpu_load_wb_fixups");
    p_mainram_slot = (uint8_t **)harness_dlsym(&cfg, "jaguarMainRAM");
    p_readlong  = (uint32_t (*)(uint32_t, uint32_t))
                  harness_dlsym(&cfg, "JaguarReadLong");
    p_gpuexec   = (void (*)(int32_t))harness_dlsym(&cfg, "GPUExec");
    p_setpc     = (void (*)(uint32_t))harness_dlsym(&cfg, "GPUSetPC");
    p_setflags  = (void (*)(uint32_t))harness_dlsym(&cfg, "GPUSetFlags");
    p_writelong = (void (*)(uint32_t, uint32_t, uint32_t))
                  harness_dlsym(&cfg, "GPUWriteLong");
    p_getpc     = (uint32_t (*)(void))harness_dlsym(&cfg, "GPUGetPC");
    vjs_p       = (struct VJSettings *)harness_dlsym(&cfg, "vjs");

    if (!gpu_ram || !p_bank0 || !p_bank1 || !p_fires || !p_rejects
        || !p_opcount || !p_gpuexec || !p_setpc || !p_setflags
        || !p_writelong || !p_wbfix || !p_mainram_slot || !*p_mainram_slot
        || !p_readlong || !p_getpc || !vjs_p) {
        fprintf(stderr, "gpu_idle_probe_falsify: missing test ABI symbols "
                        "(build with TEST_EXPORTS=1)\n");
        return 1;
    }

    build_prog_a();
    build_prog_b();

    fires_a = run_prog(PROG_A_OFF, 20000, &rej_a, &ops_a);
    fires_b = run_prog(PROG_B_OFF, 20000, &rej_b, &ops_b);

    /* C: late-load loop (must be rejected, and the fix-up must really
     * have fired -- otherwise "0 fires" would prove nothing).  D: the
     * no-overwrite control (must still fire, fix-up count unchanged). */
    build_prog_c(3);
    /* The late value is only meaningful if the core itself reads back
     * G_CTRL's address from the poked main-RAM long (gpu_load_wb_fixups
     * counts even when the late value equals the moveq's). */
    if (p_readlong(MAINRAM_PTR, 0) != G_CTRL_ADDR) {
        fprintf(stderr, "gpu_idle_probe_falsify: main-RAM poke not visible "
                        "to JaguarReadLong ($%08X)\n",
                p_readlong(MAINRAM_PTR, 0));
        return 1;
    }
    wb0     = *p_wbfix;
    fires_c = run_prog(PROG_C_OFF, 20000, &rej_c, NULL);
    wb_c    = *p_wbfix - wb0;
    build_prog_c(5);
    wb0     = *p_wbfix;
    fires_d = run_prog(PROG_C_OFF, 20000, &rej_d, NULL);
    wb_d    = *p_wbfix - wb0;

    ok_a = (fires_a > 0);
    ok_b = (fires_b == 0);
    ok_c = (fires_c == 0 && wb_c > 0);
    ok_d = (fires_d > 0 && wb_d == 0);

    snprintf(detail_a, sizeof detail_a,
             "genuinely idle loop (addqt/jr/nop) fired %u time(s), "
             "%u reject(s), %u opcodes charged", fires_a, rej_a, ops_a);
    res[nres].status = ok_a ? "PASS" : "FAIL";
    res[nres].name   = "gpu_idle_loop_still_fires";
    res[nres].detail = detail_a;
    nres++;

    snprintf(detail_b, sizeof detail_b,
             "compound period hiding an undecoded store fired %u time(s) "
             "(must be 0), %u reject(s), %u opcodes charged",
             fires_b, rej_b, ops_b);
    res[nres].status = ok_b ? "PASS" : "FAIL";
    res[nres].name   = "gpu_undecoded_store_path_rejected";
    res[nres].detail = detail_b;
    nres++;

    snprintf(detail_c, sizeof detail_c,
             "exploit arrival-to-arrival costs 7 counted opcodes vs the "
             "idleBodyCount = 2 a straight-line body charges (GPU delay "
             "slots are uncounted, unlike the DSP's)");
    res[nres].status = "INFO";
    res[nres].name   = "gpu_executed_path_invariant";
    res[nres].detail = detail_c;
    nres++;

    snprintf(detail_e, sizeof detail_e,
             "late-load loop (load (r2),r3 / moveq #0,r3 / load (r3),r4) "
             "fired %u time(s) (must be 0), %u reject(s), %llu late "
             "write-back(s) applied (must be > 0)",
             fires_c, rej_c, (unsigned long long)wb_c);
    res[nres].status = ok_c ? "PASS" : "FAIL";
    res[nres].name   = "gpu_late_load_loop_rejected";
    res[nres].detail = detail_e;
    nres++;

    snprintf(detail_f, sizeof detail_f,
             "same loop with the load aimed at r5 (no overwrite) fired %u "
             "time(s) (must be > 0), %u reject(s), %llu late write-back(s) "
             "(must be 0)", fires_d, rej_d, (unsigned long long)wb_d);
    res[nres].status = ok_d ? "PASS" : "FAIL";
    res[nres].name   = "gpu_external_load_loop_still_fires";
    res[nres].detail = detail_f;
    nres++;

    /* E-M: local-RAM long stores in wait loops (issue #952). */
    for (sc = 0; sc < (int)(sizeof store_cases / sizeof store_cases[0]); sc++) {
        int exact = 1, memval = 0, ok;
        uint32_t rej = 0, fires;

        fires = run_store_prog(store_cases[sc].k, vjs_p, &exact, &rej, &memval);
        ok = store_cases[sc].must_fire ? (fires > 0 && exact) : (fires == 0);
        /* The flag long must hold what the interpreter leaves there: the
         * stored 0 or 1, never the 0xAAAAAAAA the test seeded it with. */
        if (store_cases[sc].must_fire && (uint32_t)memval > 1u)
            ok = 0;
        snprintf(store_detail[sc], sizeof store_detail[sc],
                 "fired %u time(s) (%s), %u reject(s), skip-on vs skip-off "
                 "end state %s, flag long = %08X",
                 fires, store_cases[sc].must_fire ? "must be > 0" : "must be 0",
                 rej, exact ? "bit-identical" : "DIFFERS", (unsigned)memval);
        res[nres].status = ok ? "PASS" : "FAIL";
        res[nres].name   = store_cases[sc].name;
        res[nres].detail = store_detail[sc];
        nres++;
        if (!ok)
            all_store_ok = 0;
    }

    harness_report(&cfg, res, nres);
    harness_shutdown(&cfg);
    return (ok_a && ok_b && ok_c && ok_d && all_store_ok) ? 0 : 1;
}
