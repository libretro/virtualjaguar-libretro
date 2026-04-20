/*
 * test_dsp_instructions.c — Unit tests for Jaguar DSP RISC instruction execution.
 *
 * The DSP uses the same RISC ISA as the GPU but has 8KB of work RAM
 * at $F1B000 and control registers at $F1A100.
 *
 * Build:
 *   make -j4 DEBUG=1 && cc -O0 -g -o test/test_dsp_instructions \
 *       test/test_dsp_instructions.c -ldl
 *
 * Run:
 *   DYLD_LIBRARY_PATH=. test/test_dsp_instructions
 */

#include "test_framework.h"

static struct vj_core C;
#define DSP_RAM  0xF1B000

static void dsp_test_setup(void)
{
    C.DSPReset();
    /* Fill 8KB DSP RAM with NOPs */
    for (uint32_t i = 0; i < 0x2000; i += 2)
        C.DSPWriteWord(DSP_RAM + i, GPU_NOP, 0);
}

static void dsp_run_program(uint32_t pc_addr)
{
    C.DSPWriteLong(DSP_PC_REG, pc_addr, 0);
    C.DSPWriteLong(DSP_CTRL_REG, 1, 0);  /* DSPGO */
    C.DSPExec(200);
    C.DSPWriteLong(DSP_CTRL_REG, 0, 0);  /* stop */
}

static void dsp_exec_one(uint16_t instr, uint32_t r_src, uint8_t src_reg,
                          uint32_t r_dst, uint8_t dst_reg)
{
    dsp_test_setup();
    C.dsp_reg_bank_0[src_reg] = r_src;
    C.dsp_reg_bank_0[dst_reg] = r_dst;
    C.DSPWriteWord(DSP_RAM, instr, 0);
    /* Fill a few NOPs after the instruction */
    for (int i = 1; i <= 15; i++)
        C.DSPWriteWord(DSP_RAM + i * 2, GPU_NOP, 0);
    dsp_run_program(DSP_RAM);
}

static uint32_t dsp_read_flags(void)
{
    return C.DSPReadLong(DSP_FLAGS_REG, 0);
}

/* ------------------------------------------------------------------ */
/* Arithmetic                                                          */
/* ------------------------------------------------------------------ */

TEST(dsp_add_basic)
{
    dsp_exec_one(gpu_encode(GPU_OP_ADD, 1, 2), 10, 1, 32, 2);
    ASSERT_EQ_U32(C.dsp_reg_bank_0[2], 42);
}

TEST(dsp_add_flags)
{
    dsp_exec_one(gpu_encode(GPU_OP_ADD, 1, 2), 0, 1, 0, 2);
    ASSERT_TRUE(dsp_read_flags() & GPU_FLAG_ZERO);
}

TEST(dsp_sub_basic)
{
    dsp_exec_one(gpu_encode(GPU_OP_SUB, 1, 2), 10, 1, 42, 2);
    ASSERT_EQ_U32(C.dsp_reg_bank_0[2], 32);
}

TEST(dsp_addq_basic)
{
    dsp_exec_one(gpu_encode(GPU_OP_ADDQ, 5, 2), 0, 0, 10, 2);
    ASSERT_EQ_U32(C.dsp_reg_bank_0[2], 15);
}

TEST(dsp_neg_basic)
{
    dsp_exec_one(gpu_encode(GPU_OP_NEG, 0, 2), 0, 0, 42, 2);
    ASSERT_EQ_U32(C.dsp_reg_bank_0[2], (uint32_t)-42);
}

TEST(dsp_mult_basic)
{
    dsp_exec_one(gpu_encode(GPU_OP_MULT, 1, 2), 6, 1, 7, 2);
    ASSERT_EQ_U32(C.dsp_reg_bank_0[2], 42);
}

TEST(dsp_imult_signed)
{
    dsp_exec_one(gpu_encode(GPU_OP_IMULT, 1, 2), (uint32_t)-3, 1, 14, 2);
    ASSERT_EQ_U32(C.dsp_reg_bank_0[2], (uint32_t)-42);
}

TEST(dsp_div_basic)
{
    dsp_exec_one(gpu_encode(GPU_OP_DIV, 1, 2), 7, 1, 42, 2);
    ASSERT_EQ_U32(C.dsp_reg_bank_0[2], 6);
}

/* ------------------------------------------------------------------ */
/* Logic                                                               */
/* ------------------------------------------------------------------ */

TEST(dsp_and_basic)
{
    dsp_exec_one(gpu_encode(GPU_OP_AND, 1, 2), 0xFF00, 1, 0xFFFF, 2);
    ASSERT_EQ_U32(C.dsp_reg_bank_0[2], 0xFF00);
}

TEST(dsp_or_basic)
{
    dsp_exec_one(gpu_encode(GPU_OP_OR, 1, 2), 0x0F, 1, 0xF0, 2);
    ASSERT_EQ_U32(C.dsp_reg_bank_0[2], 0xFF);
}

TEST(dsp_xor_basic)
{
    dsp_exec_one(gpu_encode(GPU_OP_XOR, 1, 2), 0xFF, 1, 0x0F, 2);
    ASSERT_EQ_U32(C.dsp_reg_bank_0[2], 0xF0);
}

TEST(dsp_not_basic)
{
    dsp_exec_one(gpu_encode(GPU_OP_NOT, 0, 2), 0, 0, 0, 2);
    ASSERT_EQ_U32(C.dsp_reg_bank_0[2], 0xFFFFFFFF);
}

/* ------------------------------------------------------------------ */
/* Shift / Rotate                                                      */
/* ------------------------------------------------------------------ */

TEST(dsp_shlq_basic)
{
    /* SHLQ #n, Rn: shift left by (32-n) */
    dsp_exec_one(gpu_encode(GPU_OP_SHLQ, 31, 2), 0, 0, 1, 2);
    ASSERT_EQ_U32(C.dsp_reg_bank_0[2], 2);  /* 1 << 1 */
}

TEST(dsp_shrq_basic)
{
    /* SHRQ #n, Rn: shift right by n (0 means 32) */
    dsp_exec_one(gpu_encode(GPU_OP_SHRQ, 4, 2), 0, 0, 0xFF, 2);
    ASSERT_EQ_U32(C.dsp_reg_bank_0[2], 0x0F);  /* 0xFF >> 4 */
}

TEST(dsp_sharq_sign)
{
    dsp_exec_one(gpu_encode(GPU_OP_SHARQ, 31, 2), 0, 0, 0x80000000, 2);
    ASSERT_EQ_U32(C.dsp_reg_bank_0[2], 0xFFFFFFFF);
}

TEST(dsp_sh_right)
{
    /* SH Rm, Rn: positive Rm = shift RIGHT */
    dsp_exec_one(gpu_encode(GPU_OP_SH, 1, 2), 4, 1, 0xFF, 2);
    ASSERT_EQ_U32(C.dsp_reg_bank_0[2], 0x0F);
}

TEST(dsp_sh_left)
{
    /* SH Rm, Rn: negative Rm = shift LEFT */
    dsp_exec_one(gpu_encode(GPU_OP_SH, 1, 2), (uint32_t)-4, 1, 1, 2);
    ASSERT_EQ_U32(C.dsp_reg_bank_0[2], 0x10);
}

TEST(dsp_ror_basic)
{
    dsp_exec_one(gpu_encode(GPU_OP_ROR, 1, 2), 4, 1, 0xFF, 2);
    ASSERT_EQ_U32(C.dsp_reg_bank_0[2], 0xF000000F);
}

/* ------------------------------------------------------------------ */
/* Compare                                                             */
/* ------------------------------------------------------------------ */

TEST(dsp_cmp_equal)
{
    dsp_exec_one(gpu_encode(GPU_OP_CMP, 1, 2), 42, 1, 42, 2);
    ASSERT_TRUE(dsp_read_flags() & GPU_FLAG_ZERO);
    ASSERT_EQ_U32(C.dsp_reg_bank_0[2], 42);
}

TEST(dsp_cmp_less)
{
    dsp_exec_one(gpu_encode(GPU_OP_CMP, 1, 2), 100, 1, 42, 2);
    ASSERT_TRUE(dsp_read_flags() & GPU_FLAG_NEGA);
}

/* ------------------------------------------------------------------ */
/* Move / MOVEI                                                        */
/* ------------------------------------------------------------------ */

TEST(dsp_move_basic)
{
    dsp_exec_one(gpu_encode(GPU_OP_MOVE, 1, 2), 0xDEADBEEF, 1, 0, 2);
    ASSERT_EQ_U32(C.dsp_reg_bank_0[2], 0xDEADBEEF);
}

TEST(dsp_moveq_basic)
{
    dsp_exec_one(gpu_encode(GPU_OP_MOVEQ, 17, 2), 0, 0, 0, 2);
    ASSERT_EQ_U32(C.dsp_reg_bank_0[2], 17);
}

TEST(dsp_movei_basic)
{
    dsp_test_setup();
    /* Write MOVEI #$CAFEBABE, R2 */
    uint16_t movei_op = gpu_encode(GPU_OP_MOVEI, 0, 2);
    C.DSPWriteWord(DSP_RAM, movei_op, 0);
    C.DSPWriteWord(DSP_RAM + 2, 0xBABE, 0);  /* low 16 bits */
    C.DSPWriteWord(DSP_RAM + 4, 0xCAFE, 0);  /* high 16 bits */
    dsp_run_program(DSP_RAM);
    ASSERT_EQ_U32(C.dsp_reg_bank_0[2], 0xCAFEBABE);
}

/* ------------------------------------------------------------------ */
/* Load / Store (DSP RAM)                                              */
/* ------------------------------------------------------------------ */

TEST(dsp_store_load_long)
{
    dsp_test_setup();
    uint32_t prog = DSP_RAM + 0x80;
    uint32_t data = DSP_RAM + 0x1F00;  /* data far from program */

    /* MOVEI #data_addr, R3 */
    uint16_t movei_op = gpu_encode(GPU_OP_MOVEI, 0, 3);
    C.DSPWriteWord(prog, movei_op, 0);
    C.DSPWriteWord(prog + 2, data & 0xFFFF, 0);
    C.DSPWriteWord(prog + 4, data >> 16, 0);
    prog += 6;

    C.dsp_reg_bank_0[1] = 0xDEADBEEF;
    /* STORE R1, (R3) — Rm=R3 (addr) in src, Rn=R1 (data) in dst */
    C.DSPWriteWord(prog, gpu_encode(GPU_OP_STORE, 3, 1), 0);
    prog += 2;

    C.dsp_reg_bank_0[2] = 0;
    /* LOAD (R3), R2 */
    C.DSPWriteWord(prog, gpu_encode(GPU_OP_LOAD, 3, 2), 0);
    prog += 2;

    dsp_run_program(DSP_RAM + 0x80);
    ASSERT_EQ_U32(C.dsp_reg_bank_0[2], 0xDEADBEEF);
}

/* ------------------------------------------------------------------ */
/* Saturation (DSP has signed variants, not GPU's unsigned SAT8/SAT16) */
/* ------------------------------------------------------------------ */

TEST(dsp_sat16s_clamp_pos)
{
    /* sat16s: clamp to signed 16-bit range (-32768..32767) */
    dsp_exec_one(gpu_encode(GPU_OP_SAT16, 0, 2), 0, 0, 0x1FFFF, 2);
    ASSERT_EQ_U32(C.dsp_reg_bank_0[2], 32767);  /* 0x7FFF */
}

TEST(dsp_sat16s_clamp_neg)
{
    dsp_exec_one(gpu_encode(GPU_OP_SAT16, 0, 2), 0, 0, (uint32_t)-50000, 2);
    ASSERT_EQ_U32(C.dsp_reg_bank_0[2], (uint32_t)-32768);  /* 0xFFFF8000 */
}

TEST(dsp_sat16s_passthrough)
{
    dsp_exec_one(gpu_encode(GPU_OP_SAT16, 0, 2), 0, 0, 1000, 2);
    ASSERT_EQ_U32(C.dsp_reg_bank_0[2], 1000);
}

/* ------------------------------------------------------------------ */
/* DSP-specific: IMASK via IRQ (same as GPU, verify DSP path)          */
/* ------------------------------------------------------------------ */

TEST(dsp_imask_not_writable)
{
    C.DSPReset();
    C.DSPWriteLong(DSP_FLAGS_REG, GPU_FLAG_IMASK, 0);
    uint32_t flags = C.DSPReadLong(DSP_FLAGS_REG, 0);
    ASSERT_FALSE(flags & GPU_FLAG_IMASK);
}

/* ------------------------------------------------------------------ */
/* Main                                                                */
/* ------------------------------------------------------------------ */

int main(int argc, char *argv[])
{
    (void)argc; (void)argv;

    if (!vj_core_load(&C))
    {
        fprintf(stderr, "Failed to load core\n");
        return 1;
    }
    if (!C.dsp_reg_bank_0)
    {
        fprintf(stderr, "dsp_reg_bank_0 not found in core\n");
        return 1;
    }
    vj_core_init(&C);

    TEST_INIT("DSP RISC Instructions");

    /* Arithmetic */
    RUN_TEST(dsp_add_basic);
    RUN_TEST(dsp_add_flags);
    RUN_TEST(dsp_sub_basic);
    RUN_TEST(dsp_addq_basic);
    RUN_TEST(dsp_neg_basic);
    RUN_TEST(dsp_mult_basic);
    RUN_TEST(dsp_imult_signed);
    RUN_TEST(dsp_div_basic);

    /* Logic */
    RUN_TEST(dsp_and_basic);
    RUN_TEST(dsp_or_basic);
    RUN_TEST(dsp_xor_basic);
    RUN_TEST(dsp_not_basic);

    /* Shift / Rotate */
    RUN_TEST(dsp_shlq_basic);
    RUN_TEST(dsp_shrq_basic);
    RUN_TEST(dsp_sharq_sign);
    RUN_TEST(dsp_sh_right);
    RUN_TEST(dsp_sh_left);
    RUN_TEST(dsp_ror_basic);

    /* Compare */
    RUN_TEST(dsp_cmp_equal);
    RUN_TEST(dsp_cmp_less);

    /* Move / MOVEI */
    RUN_TEST(dsp_move_basic);
    RUN_TEST(dsp_moveq_basic);
    RUN_TEST(dsp_movei_basic);

    /* Load / Store */
    RUN_TEST(dsp_store_load_long);

    /* Saturation (DSP signed variants) */
    RUN_TEST(dsp_sat16s_clamp_pos);
    RUN_TEST(dsp_sat16s_clamp_neg);
    RUN_TEST(dsp_sat16s_passthrough);

    /* DSP-specific */
    RUN_TEST(dsp_imask_not_writable);

    int result = TEST_REPORT();
    vj_core_unload(&C);
    return result;
}
