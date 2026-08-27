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

/* RISC opcode indices (src/tom/gpu.c gpu_dispatch[]) */
#define OP_ADDQT   3
#define OP_SUBQ    6
#define OP_STORE  47
#define OP_JUMP   52
#define OP_JR     53
#define OP_NOP    57
#define OP_MOVEI  38

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

static uint8_t  *gpu_ram;
static uint32_t *p_bank0;
static uint32_t *p_bank1;
static uint32_t *p_fires;
static uint32_t *p_rejects;
static uint32_t *p_opcount;
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
    harness_result res[3];
    unsigned nres = 0;
    uint32_t fires_a, fires_b, rej_a, rej_b, ops_a, ops_b;
    static char detail_a[192], detail_b[192], detail_c[192];
    int ok_a, ok_b;

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
    p_gpuexec   = (void (*)(int32_t))harness_dlsym(&cfg, "GPUExec");
    p_setpc     = (void (*)(uint32_t))harness_dlsym(&cfg, "GPUSetPC");
    p_setflags  = (void (*)(uint32_t))harness_dlsym(&cfg, "GPUSetFlags");
    p_writelong = (void (*)(uint32_t, uint32_t, uint32_t))
                  harness_dlsym(&cfg, "GPUWriteLong");

    if (!gpu_ram || !p_bank0 || !p_bank1 || !p_fires || !p_rejects
        || !p_opcount || !p_gpuexec || !p_setpc || !p_setflags
        || !p_writelong) {
        fprintf(stderr, "gpu_idle_probe_falsify: missing test ABI symbols "
                        "(build with TEST_EXPORTS=1)\n");
        return 1;
    }

    build_prog_a();
    build_prog_b();

    fires_a = run_prog(PROG_A_OFF, 20000, &rej_a, &ops_a);
    fires_b = run_prog(PROG_B_OFF, 20000, &rej_b, &ops_b);

    ok_a = (fires_a > 0);
    ok_b = (fires_b == 0);

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

    harness_report(&cfg, res, nres);
    harness_shutdown(&cfg);
    return (ok_a && ok_b) ? 0 : 1;
}
