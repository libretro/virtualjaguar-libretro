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
 *   E. Iron Soldier 2's DSP wait loop, word for word (#952)  ->  the
 *      probe MUST fire, and the loop must leave once the polled
 *      alternate-bank register moves:
 *      head:  nop ; addq #1,r19 ; movei #$F1B730,r14 ; movefa r28,r28
 *             sub r31,r28 ; jr NN,+2 ; sharq #2,r28 ; addq #31,r28
 *             moveq #0,r30 ; cmpq #8,r28 ; jr C,head ; nop
 *      Three relaxations at once: a 12-word body, an INTERNAL forward jr
 *      (taken here: 2 taken jrs, one opcode fewer), and a counter written
 *      by the flag-setting addq, whose flags `sub` overwrites unread.
 *      E2 is the same loop with the internal jr NOT taken (1 taken jr).
 *
 *   F. a counter whose flags ARE read  ->  MUST NOT fire.
 *      head:  addq #1,r19 ; jr NE,head ; nop      (r19 from $FFFF0000)
 *      The loop ends when r19 wraps to 0; extrapolating r19 would jump
 *      straight over the exit.  The wrap lies beyond this test's cycle
 *      budget, so the loop never reaches its exit park loop (which is
 *      idle and would fire itself): any fire is this loop's.
 *
 *   G. a counter whose carry is read  ->  MUST NOT fire.
 *      head:  moveq #0,r2 ; addq #1,r19 ; addc r0,r2 ; jr EQ,head ; nop
 *      r2 picks up addq's carry, so the loop ends when r19 wraps; every
 *      register addc reads has zero delta, so only the dead-flag rule
 *      (addc reads C) rejects it.
 *
 *   H. the internal-branch exploit  ->  MUST NOT fire.
 *      head:  moveq #0,r1 ; or r1,r1 ; jr always,+5 ; nop
 *             nop x4                      <- skipped, K = 4
 *      jr:    jr NE,head ; or r5,r5       <- (delay slot) r5 = 1
 *             jump (r6) ; store r3,(r4)   <- NEVER DECODED, r6 = jr
 *      The internal jr is always taken, so the closing jr arrives with
 *      Z set and falls through; its delay slot clears Z, the jump runs an
 *      undecoded store in ITS delay slot and lands back on the closing
 *      jr, now taken.  Two taken jrs and F = idleBodyCount + 1 opcodes:
 *      exactly the not-taken route's count, so only pairing the opcode
 *      count with the taken-jr count (2 taken means F - K) rejects it.
 *
 *   I. an exit jr and an internal jr in one body  ->  MUST NOT fire.
 *      head:  btst #0,r22 ; jr NE,out ; nop ; jr EQ,+2 ; nop ; nop
 *             jr always,head ; nop
 *      A taken exit plus a `jump` back would show the same taken-jr
 *      count as a taken internal skip, so the decoder refuses the mix.
 *
 *   J. F with a flag killer in the closing jr's delay slot  ->  MUST NOT
 *      fire.
 *      head:  addq #1,r19 ; jr NE,head ; cmpq #0,r0   (r19 from $FFFF0000)
 *      The jr reads addq's Z before its delay slot overwrites it, so the
 *      dead-flag scan must stop short of that slot.  At its own address:
 *      F's reject-memo entry would otherwise mask it.
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
#define OP_ADDC    1
#define OP_ADDQ    2
#define OP_ADDQT   3
#define OP_SUBQ    6
#define OP_BTST   13
#define OP_CMPQ   31
#define OP_STORE  47
#define OP_JUMP   52
#define OP_JR     53
#define OP_NOP    57
#define OP_MOVEI  38
#define OP_MOVEQ  35
#define OP_OR     10

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
#define PROG_E_OFF   0x0600          /* $F1B600 */
#define PROG_E_OUT   0x061C          /* $F1B61C -- E's exit park loop */
#define PROG_F_OFF   0x0700          /* $F1B700 */
#define PROG_J_OFF   0x0740          /* $F1B740 */
#define PROG_G_OFF   0x0780          /* $F1B780 */
#define PROG_H_OFF   0x0880          /* $F1B880 */
#define PROG_I_OFF   0x0800          /* $F1B800 */

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

/* Park loop for a program's exit path, so the PC stays readable. */
static void put_park(uint32_t off)
{
    put16(off + 0, enc(OP_ADDQT, 1, 7));                     /* addqt #1,r7    */
    put16(off + 2, enc(OP_JR, 0x1E /* -2 */, CC_ALWAYS));
    put16(off + 4, enc(OP_NOP, 0, 0));
}

static void build_prog_e(uint32_t alt_r28)
{
    /* The words of Iron Soldier 2 (World) at $F1B0A6-$F1B0C0. */
    static const uint16_t is2[] = {
        0xE400,                         /* nop                    */
        0x0833,                         /* addq   #1,r19          */
        0x980E, 0xB730, 0x00F1,         /* movei  #$00F1B730,r14  */
        0x979C,                         /* movefa r28,r28         */
        0x13FC,                         /* sub    r31,r28         */
        0xD454,                         /* jr     NN,+2           */
        0x6C5C,                         /* sharq  #2,r28          */
        0x0BFC,                         /* addq   #31,r28         */
        0x8C1E,                         /* moveq  #0,r30          */
        0x7D1C,                         /* cmpq   #8,r28          */
        0xD668,                         /* jr     C,-13 (head)    */
        0xE400                          /* nop    (delay slot)    */
    };
    unsigned i;

    for (i = 0; i < sizeof is2 / sizeof is2[0]; i++)
        put16(PROG_E_OFF + 2 * i, is2[i]);
    put_park(PROG_E_OUT);

    setreg(19, 0);
    setreg(31, 0x40);
    /* movefa reads the ALTERNATE bank's r28, the one the IS2 interrupt
     * handler maintains; setreg writes both, and the loop only ever
     * overwrites the current bank's copy. */
    setreg(28, alt_r28);
}

/* F at `off` with `slot` in the closing jr's delay slot (J: cmpq). */
static void build_prog_f(uint32_t off, uint16_t slot)
{
    put16(off + 0, enc(OP_ADDQ, 1, 19));                     /* addq #1,r19    */
    put16(off + 2, enc(OP_JR, 0x1E /* -2 */, CC_NE));        /* jr ne,head     */
    put16(off + 4, slot);
    put_park(off + 6);

    setreg(19, 0xFFFF0000u);
}

static void build_prog_g(void)
{
    put16(PROG_G_OFF + 0, enc(OP_MOVEQ, 0, 2));              /* moveq #0,r2    */
    put16(PROG_G_OFF + 2, enc(OP_ADDQ, 1, 19));              /* addq #1,r19    */
    put16(PROG_G_OFF + 4, enc(OP_ADDC, 0, 2));               /* addc r0,r2     */
    put16(PROG_G_OFF + 6, enc(OP_JR, 0x1C /* -4 */, CC_EQ)); /* jr eq,head     */
    put16(PROG_G_OFF + 8, enc(OP_NOP, 0, 0));
    put_park(PROG_G_OFF + 10);

    setreg(0, 0);
    setreg(19, 0xFFFF0000u);
}

static void build_prog_h(void)
{
    unsigned i;

    put16(PROG_H_OFF +  0, enc(OP_MOVEQ, 0, 1));             /* moveq #0,r1    */
    put16(PROG_H_OFF +  2, enc(OP_OR, 1, 1));                /* or r1,r1: Z=1  */
    put16(PROG_H_OFF +  4, enc(OP_JR, 5, CC_ALWAYS));        /* jr +5 (internal) */
    for (i = 6; i <= 14; i += 2)                              /* delay slot + 4 */
        put16(PROG_H_OFF + i, enc(OP_NOP, 0, 0));
    put16(PROG_H_OFF + 16, enc(OP_JR, 0x17 /* -9 */, CC_NE)); /* jr ne,head  */
    put16(PROG_H_OFF + 18, enc(OP_OR, 5, 5));                /* (ds) or r5,r5  */
    put16(PROG_H_OFF + 20, enc(OP_JUMP, 6, CC_ALWAYS));      /* jump (r6)      */
    put16(PROG_H_OFF + 22, enc(OP_STORE, 4, 3));             /* (ds) store     */

    setreg(3, 0xDEADBEEFu);
    setreg(4, DSP_RAM_BASE + SCRATCH_OFF);
    setreg(5, 1);
    setreg(6, DSP_RAM_BASE + PROG_H_OFF + 16);     /* jump target = closing jr */
}

static void build_prog_i(void)
{
    put16(PROG_I_OFF +  0, enc(OP_BTST, 0, 22));             /* btst #0,r22    */
    put16(PROG_I_OFF +  2, enc(OP_JR, 10, CC_NE));           /* jr ne,out      */
    put16(PROG_I_OFF +  4, enc(OP_NOP, 0, 0));
    put16(PROG_I_OFF +  6, enc(OP_JR, 2, CC_EQ));            /* jr eq,+2 (internal) */
    put16(PROG_I_OFF +  8, enc(OP_NOP, 0, 0));               /* (delay slot)   */
    put16(PROG_I_OFF + 10, enc(OP_NOP, 0, 0));               /* skipped        */
    put16(PROG_I_OFF + 12, enc(OP_JR, 0x19 /* -7 */, CC_ALWAYS)); /* jr head */
    put16(PROG_I_OFF + 14, enc(OP_NOP, 0, 0));               /* (delay slot)   */
    put_park(PROG_I_OFF + 24);                                /* out            */

    setreg(22, 0);
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
    harness_result res[14];
    unsigned nres = 0;
    uint32_t fires_a, fires_b, rej_a, rej_b, ops_a, ops_b;
    uint32_t fires_c, fires_d, rej_c, rej_d, ops_c, ops_d, pc_c;
    uint32_t fires_e, fires_e2, fires_f, fires_g, fires_h, fires_i, pc_e;
    uint32_t fires_j, rej_e, rej_e2, rej_f, rej_g, rej_h, rej_i, rej_j;
    static char detail_a[192], detail_b[192], detail_c[192];
    static char detail_d[192], detail_e[192], detail_f[192];
    static char detail_g[192], detail_h[192], detail_i[192];
    static char detail_j[192], detail_k[192], detail_l[192];
    static char detail_m[192], detail_n[192];
    int ok_a, ok_b, ok_c, ok_d, ok_e;
    int ok_f, ok_g, ok_h, ok_i, ok_j, ok_k, ok_l, ok_m;

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
    /* E: alt r28 $50 -> r28 = $10 >= 0, internal jr taken, $10>>2 = 4 < 8. */
    build_prog_e(0x50);
    fires_e = run_prog(PROG_E_OFF, 20000, &rej_e, NULL);
    /* The interrupt handler's pointer moves: $60 -> $20>>2 = 8, not < 8. */
    setreg(28, 0x60);
    p_dspexec(200);
    pc_e = *p_dsp_pc;
    /* E2: alt r28 $40-100 -> r28 = -100 < 0, internal jr not taken,
     * -100>>2 + 31 = 6 < 8. */
    build_prog_e(0x40u - 100u);
    fires_e2 = run_prog(PROG_E_OFF, 20000, &rej_e2, NULL);
    build_prog_f(PROG_F_OFF, enc(OP_NOP, 0, 0));
    fires_f = run_prog(PROG_F_OFF, 20000, &rej_f, NULL);
    build_prog_f(PROG_J_OFF, enc(OP_CMPQ, 0, 0));           /* cmpq #0,r0     */
    fires_j = run_prog(PROG_J_OFF, 20000, &rej_j, NULL);
    build_prog_g();
    fires_g = run_prog(PROG_G_OFF, 20000, &rej_g, NULL);
    build_prog_h();
    fires_h = run_prog(PROG_H_OFF, 20000, &rej_h, NULL);
    build_prog_i();
    fires_i = run_prog(PROG_I_OFF, 20000, &rej_i, NULL);

    ok_a = (fires_a > 0);
    ok_b = (fires_b == 0);
    ok_c = (fires_c > 0);
    ok_d = (pc_c >= DSP_RAM_BASE + PROG_C_OUT
            && pc_c <= DSP_RAM_BASE + PROG_C_OUT + 4);
    ok_e = (fires_d == 0);
    ok_f = (fires_e > 0);
    ok_g = (pc_e >= DSP_RAM_BASE + PROG_E_OUT
            && pc_e <= DSP_RAM_BASE + PROG_E_OUT + 4);
    ok_h = (fires_e2 > 0);
    ok_i = (fires_f == 0);
    ok_j = (fires_g == 0);
    ok_k = (fires_i == 0);
    ok_l = (fires_h == 0);
    ok_m = (fires_j == 0);

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

    snprintf(detail_g, sizeof detail_g,
             "Iron Soldier 2 loop, internal jr taken, fired %u time(s), "
             "%u reject(s)", fires_e, rej_e);
    res[nres].status = ok_f ? "PASS" : "FAIL";
    res[nres].name   = "internal_branch_loop_fires";
    res[nres].detail = detail_g;
    nres++;

    snprintf(detail_h, sizeof detail_h,
             "after alt r28 moved, DSP PC $%06X (must be in $%06X..$%06X)",
             pc_e, DSP_RAM_BASE + PROG_E_OUT, DSP_RAM_BASE + PROG_E_OUT + 4);
    res[nres].status = ok_g ? "PASS" : "FAIL";
    res[nres].name   = "internal_branch_loop_still_exits";
    res[nres].detail = detail_h;
    nres++;

    snprintf(detail_i, sizeof detail_i,
             "Iron Soldier 2 loop, internal jr not taken, fired %u time(s), "
             "%u reject(s)", fires_e2, rej_e2);
    res[nres].status = ok_h ? "PASS" : "FAIL";
    res[nres].name   = "internal_branch_untaken_fires";
    res[nres].detail = detail_i;
    nres++;

    snprintf(detail_j, sizeof detail_j,
             "counter addq whose Z the closing jr reads fired %u time(s) "
             "(must be 0), %u reject(s)", fires_f, rej_f);
    res[nres].status = ok_i ? "PASS" : "FAIL";
    res[nres].name   = "live_flag_counter_rejected";
    res[nres].detail = detail_j;
    nres++;

    snprintf(detail_k, sizeof detail_k,
             "counter addq whose carry addc reads fired %u time(s) "
             "(must be 0), %u reject(s)", fires_g, rej_g);
    res[nres].status = ok_j ? "PASS" : "FAIL";
    res[nres].name   = "live_carry_counter_rejected";
    res[nres].detail = detail_k;
    nres++;

    snprintf(detail_n, sizeof detail_n,
             "counter addq whose Z the closing jr reads before its delay "
             "slot's cmpq fired %u time(s) (must be 0), %u reject(s)",
             fires_j, rej_j);
    res[nres].status = ok_m ? "PASS" : "FAIL";
    res[nres].name   = "delay_slot_flag_kill_rejected";
    res[nres].detail = detail_n;
    nres++;

    snprintf(detail_m, sizeof detail_m,
             "taken internal jr + fall-through + undecoded store + jump "
             "back, F opcodes with 2 taken jrs, fired %u time(s) (must be 0), "
             "%u reject(s)", fires_h, rej_h);
    res[nres].status = ok_l ? "PASS" : "FAIL";
    res[nres].name   = "internal_branch_detour_rejected";
    res[nres].detail = detail_m;
    nres++;

    snprintf(detail_l, sizeof detail_l,
             "body with an exit jr and an internal jr fired %u time(s) "
             "(must be 0), %u reject(s)", fires_i, rej_i);
    res[nres].status = ok_k ? "PASS" : "FAIL";
    res[nres].name   = "exit_plus_internal_rejected";
    res[nres].detail = detail_l;
    nres++;

    harness_report(&cfg, res, nres);
    harness_shutdown(&cfg);
    return (ok_a && ok_b && ok_c && ok_d && ok_e
            && ok_f && ok_g && ok_h && ok_i && ok_j && ok_k && ok_l && ok_m) ? 0 : 1;
}
