/*
 * test/tools/dsp_idle_probe_falsify.c — falsification test for the DSP
 * idle-loop fast-forward's admission rule (issue #569).
 *
 * The probe (src/jerry/dsp.c) is only hooked on a TAKEN backward `jr`.
 * That means an arrival at the loop head says nothing about how the
 * PREVIOUS arrival got there: a not-taken `jr`, a `jump`, or plain
 * fall-through all reach the head without the probe ever seeing them.
 * So verifying that the *decoded* body is pure is not the same as
 * verifying that the *executed* path was — and a compound period with
 * the same net register/flag effect, containing an undecoded `store`,
 * would otherwise satisfy every other admission condition.  Eliding
 * copies of such a store (I2S LTXD/RTXD, a blitter command, a latch
 * clear) is silent, hardware-visible divergence.
 *
 * This test hand-assembles two DSP programs into DSP local RAM and
 * drives DSPExec() directly, white-box:
 *
 *   A. a genuinely idle loop  ->  the probe MUST fire.
 *      addqt #1,r0 ; jr always,-2 ; (ds) nop
 *      Guards against the test passing simply because the feature is
 *      dead (which is exactly how a compile-time VJ_TRACE gate and a
 *      probe-reset bug each silently disabled it during development).
 *
 *   B. the compound-period exploit  ->  the probe MUST NOT fire.
 *      head:  subq #1,r1
 *      jr:    jr NE,head
 *             nop                  <- delay slot, decoded
 *             store r3,(r4)        <- NEVER DECODED
 *             movei #2,r1          <- NEVER DECODED
 *             jump always,(r5)     <- NEVER DECODED, r5 = head
 *             nop
 *      r1 cycles 2 -> 1 (jr taken, arrival) -> 0 (jr NOT taken, the
 *      undecoded tail runs and resets r1 to 2) -> 1 (taken, arrival).
 *      Every arrival sees r1 == 1, so all 64 register deltas are zero,
 *      the flags match, cost and opcode cost are uniform, the decoded
 *      portion is whitelisted, and the nonzero-delta rule is vacuous.
 *      Only the executed-path check (opcost == idleBodyCount + 1)
 *      rejects it.
 *
 *   C. a loop with an exit branch (#952, Cybermorph's DSP wait loop)
 *      ->  the probe MUST fire, and the loop must still leave once the
 *      polled register changes.
 *      head:  btst #0,r22 ; jr NE,out ; nop ; btst #1,r22 ; jr EQ,head
 *             nop                  <- delay slot
 *
 *   D. the exit-branch exploit  ->  the probe MUST NOT fire.
 *      head:  subq #1,r1 ; jr EQ,out ; nop x5 ; jr always,head ; nop
 *      out:   store r3,(r4) ; movei #1,r1 ; jump (r6) ; nop
 *      r6 = the closing jr.  The exit is taken every time, the detour
 *      runs an undecoded store and re-enters through a `jump` (never
 *      probed), and the route costs 9 opcodes, exactly the 7 decoded
 *      + delay slot + 1 of a straight pass.  Registers and flags match
 *      at every arrival, so only the exit-branch check (one taken jr per
 *      iteration) rejects it.
 *
 * Build: cc -O2 -Wall -std=c99 $(INCFLAGS) -o test/tools/dsp_idle_probe_falsify \
 *          test/tools/dsp_idle_probe_falsify.c test/harness/harness.c -ldl -lm
 * Needs the wide test ABI (make TEST_EXPORTS=1).  Uses test/roms/yarc.j64,
 * which is in-tree, so this never skips.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#include "../harness/harness.h"

/* RISC opcode indices (src/jerry/dsp.c dsp_opcode[]) */
#define OP_ADDQT   3
#define OP_SUBQ    6
#define OP_BTST   13
#define OP_STORE  47
#define OP_JUMP   52
#define OP_JR     53
#define OP_NOP    57
#define OP_MOVEI  38

/* Branch condition codes: 0 = always, 1 = NE (fails when Z is set). */
#define CC_ALWAYS  0
#define CC_NE      1
#define CC_EQ      2

#define DSP_RAM_BASE 0x00F1B000u

#define PROG_A_OFF   0x0100          /* $F1B100 */
#define PROG_B_OFF   0x0200          /* $F1B200 */
#define PROG_C_OFF   0x0300          /* $F1B300 */
#define PROG_C_OUT   0x0320          /* $F1B320 -- C's exit target */
#define SCRATCH_OFF  0x0400          /* $F1B400 -- the exploit's store target */
#define PROG_D_OFF   0x0500          /* $F1B500 */

static uint8_t  *dsp_ram;
static uint32_t *p_dsp_pc;
static uint32_t *p_dsp_control;
static uint32_t *p_bank0;
static uint32_t *p_bank1;
static uint32_t *p_fires;
static uint32_t *p_rejects;
static uint32_t *p_opcount;
static void    (*p_dspexec)(int32_t);

static uint16_t enc(uint32_t idx, uint32_t p1, uint32_t p2)
{
    return (uint16_t)((idx << 10) | ((p1 & 0x1F) << 5) | (p2 & 0x1F));
}

static void put16(uint32_t off, uint16_t w)
{
    dsp_ram[off]     = (uint8_t)(w >> 8);
    dsp_ram[off + 1] = (uint8_t)(w & 0xFF);
}

/* Write the same value into both banks: which one is current depends on
 * D_FLAGS REGPAGE/IMASK, which is not exported.  Writing both makes the
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
    setreg(4, DSP_RAM_BASE + SCRATCH_OFF);         /* store destination        */
    setreg(5, DSP_RAM_BASE + PROG_B_OFF);          /* jump target = head       */
}

static void build_prog_c(void)
{
    /* jr offsets: target = (jr + 2) + off * 2. */
    put16(PROG_C_OFF +  0, enc(OP_BTST, 0, 22));              /* btst #0,r22    */
    put16(PROG_C_OFF +  2, enc(OP_JR, 14, CC_NE));            /* jr ne,out      */
    put16(PROG_C_OFF +  4, enc(OP_NOP, 0, 0));
    put16(PROG_C_OFF +  6, enc(OP_BTST, 1, 22));              /* btst #1,r22    */
    put16(PROG_C_OFF +  8, enc(OP_JR, 0x1B /* -5 */, CC_EQ)); /* jr eq,head     */
    put16(PROG_C_OFF + 10, enc(OP_NOP, 0, 0));                /* (delay slot)   */
    /* out: park in an idle loop of its own so the PC stays readable. */
    put16(PROG_C_OUT +  0, enc(OP_ADDQT, 1, 7));              /* addqt #1,r7    */
    put16(PROG_C_OUT +  2, enc(OP_JR, 0x1E /* -2 */, CC_ALWAYS));
    put16(PROG_C_OUT +  4, enc(OP_NOP, 0, 0));

    setreg(22, 0);                                 /* nothing to do yet        */
}

static void build_prog_d(void)
{
    unsigned i;

    put16(PROG_D_OFF +  0, enc(OP_SUBQ, 1, 1));               /* subq #1,r1     */
    put16(PROG_D_OFF +  2, enc(OP_JR, 7, CC_EQ));             /* jr eq,out      */
    for (i = 4; i <= 12; i += 2)
        put16(PROG_D_OFF + i, enc(OP_NOP, 0, 0));
    put16(PROG_D_OFF + 14, enc(OP_JR, 0x18 /* -8 */, CC_ALWAYS)); /* jr head */
    put16(PROG_D_OFF + 16, enc(OP_NOP, 0, 0));                /* (delay slot)   */
    /* out = head + 18 */
    put16(PROG_D_OFF + 18, enc(OP_STORE, 4, 3));              /* store r3,(r4)  */
    put16(PROG_D_OFF + 20, enc(OP_MOVEI, 0, 1));              /* movei #1,r1    */
    put16(PROG_D_OFF + 22, 0x0001);                           /*   LSW          */
    put16(PROG_D_OFF + 24, 0x0000);                           /*   MSW          */
    put16(PROG_D_OFF + 26, enc(OP_JUMP, 6, CC_ALWAYS));       /* jump (r6)      */
    put16(PROG_D_OFF + 28, enc(OP_NOP, 0, 0));                /* (delay slot)   */

    setreg(1, 1);
    setreg(3, 0xDEADBEEFu);
    setreg(4, DSP_RAM_BASE + SCRATCH_OFF);
    setreg(6, DSP_RAM_BASE + PROG_D_OFF + 14);     /* jump target = closing jr */
}

/* Run one hand-assembled program from `off` for `cycles`, returning how
 * many times the fast-forward fired. */
static uint32_t run_prog(uint32_t off, int32_t cycles,
                         uint32_t *rejects_out, uint32_t *opcodes_out)
{
    uint32_t f0 = *p_fires, r0 = *p_rejects, o0 = *p_opcount;

    *p_dsp_pc      = DSP_RAM_BASE + off;
    *p_dsp_control |= 0x01;                        /* DSPGO */
    p_dspexec(cycles);

    if (rejects_out)
        *rejects_out = *p_rejects - r0;
    if (opcodes_out)
        *opcodes_out = *p_opcount - o0;
    return *p_fires - f0;
}

int main(int argc, char **argv)
{
    harness_config cfg = HARNESS_CONFIG_DEFAULT;
    harness_result res[6];
    unsigned nres = 0;
    uint32_t fires_a, fires_b, rej_a, rej_b, ops_a, ops_b;
    uint32_t fires_c, fires_d, rej_c, rej_d, ops_c, ops_d, pc_c;
    static char detail_a[192], detail_b[192], detail_c[192];
    static char detail_d[192], detail_e[192], detail_f[192];
    int ok_a, ok_b, ok_c, ok_d, ok_e;

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

    dsp_ram       = (uint8_t *)((uint8_t *(*)(void))harness_dlsym(&cfg, "DSPGetRAM"))();
    p_dsp_pc      = (uint32_t *)harness_dlsym(&cfg, "dsp_pc");
    p_dsp_control = (uint32_t *)harness_dlsym(&cfg, "dsp_control");
    p_bank0       = (uint32_t *)harness_dlsym(&cfg, "dsp_reg_bank_0");
    p_bank1       = (uint32_t *)harness_dlsym(&cfg, "dsp_reg_bank_1");
    p_fires       = (uint32_t *)harness_dlsym(&cfg, "dsp_idle_skip_fires");
    p_rejects     = (uint32_t *)harness_dlsym(&cfg, "dsp_idle_skip_rejects");
    p_opcount     = (uint32_t *)harness_dlsym(&cfg, "dsp_exec_opcode_count");
    p_dspexec     = (void (*)(int32_t))harness_dlsym(&cfg, "DSPExec");

    if (!dsp_ram || !p_dsp_pc || !p_dsp_control || !p_bank0 || !p_bank1
        || !p_fires || !p_rejects || !p_opcount || !p_dspexec) {
        fprintf(stderr, "dsp_idle_probe_falsify: missing test ABI symbols "
                        "(build with TEST_EXPORTS=1)\n");
        return 1;
    }

    /* Build each program just before it runs: B and D both use r1. */
    build_prog_a();
    fires_a = run_prog(PROG_A_OFF, 20000, &rej_a, &ops_a);
    build_prog_b();
    fires_b = run_prog(PROG_B_OFF, 20000, &rej_b, &ops_b);
    build_prog_c();
    fires_c = run_prog(PROG_C_OFF, 20000, &rej_c, &ops_c);
    /* The polled bit arrives: C must leave the loop it was skipping. */
    setreg(22, 1);
    p_dspexec(200);
    pc_c = *p_dsp_pc;
    build_prog_d();
    fires_d = run_prog(PROG_D_OFF, 20000, &rej_d, &ops_d);

    ok_a = (fires_a > 0);
    ok_b = (fires_b == 0);
    ok_c = (fires_c > 0);
    ok_d = (pc_c >= DSP_RAM_BASE + PROG_C_OUT
            && pc_c <= DSP_RAM_BASE + PROG_C_OUT + 4);
    ok_e = (fires_d == 0);

    snprintf(detail_a, sizeof detail_a,
             "genuinely idle loop (addqt/jr/nop) fired %u time(s), "
             "%u reject(s), %u opcodes charged", fires_a, rej_a, ops_a);
    res[nres].status = ok_a ? "PASS" : "FAIL";
    res[nres].name   = "idle_loop_still_fires";
    res[nres].detail = detail_a;
    nres++;

    snprintf(detail_b, sizeof detail_b,
             "compound period hiding an undecoded store fired %u time(s) "
             "(must be 0), %u reject(s), %u opcodes charged",
             fires_b, rej_b, ops_b);
    res[nres].status = ok_b ? "PASS" : "FAIL";
    res[nres].name   = "undecoded_store_path_rejected";
    res[nres].detail = detail_b;
    nres++;

    snprintf(detail_c, sizeof detail_c,
             "exploit arrival-to-arrival costs 10 opcodes vs the "
             "idleBodyCount+1 = 3 a straight-line body would charge");
    res[nres].status = "INFO";
    res[nres].name   = "executed_path_invariant";
    res[nres].detail = detail_c;
    nres++;

    snprintf(detail_d, sizeof detail_d,
             "loop with an exit jr (btst/jr ne/nop/btst/jr eq) fired %u "
             "time(s), %u reject(s), %u opcodes charged",
             fires_c, rej_c, ops_c);
    res[nres].status = ok_c ? "PASS" : "FAIL";
    res[nres].name   = "exit_branch_loop_fires";
    res[nres].detail = detail_d;
    nres++;

    snprintf(detail_e, sizeof detail_e,
             "after r22 bit 0 set, DSP PC $%06X (must be in $%06X..$%06X)",
             pc_c, DSP_RAM_BASE + PROG_C_OUT, DSP_RAM_BASE + PROG_C_OUT + 4);
    res[nres].status = ok_d ? "PASS" : "FAIL";
    res[nres].name   = "exit_branch_still_exits";
    res[nres].detail = detail_e;
    nres++;

    snprintf(detail_f, sizeof detail_f,
             "taken exit + undecoded store + jump back, same opcode count, "
             "fired %u time(s) (must be 0), %u reject(s), %u opcodes charged",
             fires_d, rej_d, ops_d);
    res[nres].status = ok_e ? "PASS" : "FAIL";
    res[nres].name   = "taken_exit_detour_rejected";
    res[nres].detail = detail_f;
    nres++;

    harness_report(&cfg, res, nres);
    harness_shutdown(&cfg);
    return (ok_a && ok_b && ok_c && ok_d && ok_e) ? 0 : 1;
}
