/*
 * test_gpu_instructions.c — Unit tests for Jaguar GPU RISC instruction execution.
 *
 * Tests GPU instructions against the Jaguar Technical Reference spec:
 *   docs/atari-jaguar-1999/04 - Technical Reference.md
 *
 * Approach: load the core dylib, call GPUReset(), write small programs
 * to GPU RAM ($F03000), set registers via exported gpu_reg_bank_0[],
 * execute, and verify register/flag state.
 *
 * Build:
 *   make -j4 DEBUG=1 && cc -O0 -g -o test/test_gpu_instructions \
 *       test/test_gpu_instructions.c -ldl
 *
 * Run:
 *   DYLD_LIBRARY_PATH=. test/test_gpu_instructions
 */

#include "test_framework.h"

static struct vj_core C;
#define GPU_RAM  0xF03000

/* Reset GPU and clear ALL GPU RAM with NOPs */
static void gpu_test_setup(void)
{
    C.GPUReset();
    for (uint32_t i = 0; i < 0x1000; i += 2)
        C.GPUWriteWord(GPU_RAM + i, GPU_NOP, 0);
}

/* Write a one-instruction program + NOPs, set regs, run, return */
static void gpu_exec_one(uint16_t instr, uint32_t r_src, uint8_t src_reg,
                          uint32_t r_dst, uint8_t dst_reg)
{
    gpu_test_setup();
    C.gpu_reg_bank_0[src_reg] = r_src;
    C.gpu_reg_bank_0[dst_reg] = r_dst;
    C.GPUWriteWord(GPU_RAM, instr, 0);
    gpu_fill_nops(&C, GPU_RAM + 2, GPU_RAM + 32);
    gpu_run_program(&C, GPU_RAM);
}

/* ------------------------------------------------------------------ */
/* Arithmetic tests                                                    */
/* ------------------------------------------------------------------ */

TEST(add_basic)
{
    /* ADD R1, R2: R2 = R1 + R2 */
    gpu_exec_one(gpu_encode(GPU_OP_ADD, 1, 2), 10, 1, 20, 2);
    ASSERT_EQ_U32(C.gpu_reg_bank_0[2], 30);
}

TEST(add_zero_flag)
{
    gpu_exec_one(gpu_encode(GPU_OP_ADD, 1, 2), 0, 1, 0, 2);
    ASSERT_EQ_U32(C.gpu_reg_bank_0[2], 0);
    ASSERT_TRUE(gpu_read_flags(&C) & GPU_FLAG_ZERO);
}

TEST(add_carry_flag)
{
    gpu_exec_one(gpu_encode(GPU_OP_ADD, 1, 2), 1, 1, 0xFFFFFFFF, 2);
    ASSERT_EQ_U32(C.gpu_reg_bank_0[2], 0);
    ASSERT_TRUE(gpu_read_flags(&C) & GPU_FLAG_CARRY);
    ASSERT_TRUE(gpu_read_flags(&C) & GPU_FLAG_ZERO);
}

TEST(add_negative_flag)
{
    gpu_exec_one(gpu_encode(GPU_OP_ADD, 1, 2), 1, 1, 0x7FFFFFFF, 2);
    ASSERT_EQ_U32(C.gpu_reg_bank_0[2], 0x80000000);
    ASSERT_TRUE(gpu_read_flags(&C) & GPU_FLAG_NEGA);
}

TEST(addq_basic)
{
    /* ADDQ #5, R2: R2 = R2 + 5 (src field 1-32, 0 encodes 32) */
    gpu_exec_one(gpu_encode(GPU_OP_ADDQ, 5, 2), 0, 1, 100, 2);
    ASSERT_EQ_U32(C.gpu_reg_bank_0[2], 105);
}

TEST(addq_32)
{
    /* ADDQ with src=0 means add 32 */
    gpu_exec_one(gpu_encode(GPU_OP_ADDQ, 0, 2), 0, 1, 0, 2);
    ASSERT_EQ_U32(C.gpu_reg_bank_0[2], 32);
}

TEST(addqt_no_flags)
{
    /* ADDQT: same as ADDQ but does NOT affect flags */
    gpu_test_setup();
    /* First set zero flag by doing an ADD that results in 0 */
    C.gpu_reg_bank_0[1] = 0;
    C.gpu_reg_bank_0[2] = 0;
    C.GPUWriteWord(GPU_RAM + 0, gpu_encode(GPU_OP_ADD, 1, 2), 0);
    /* Then ADDQT #1, R3 — should NOT clear the zero flag */
    C.gpu_reg_bank_0[3] = 0;
    C.GPUWriteWord(GPU_RAM + 2, gpu_encode(GPU_OP_ADDQT, 1, 3), 0);
    gpu_fill_nops(&C, GPU_RAM + 4, GPU_RAM + 32);
    gpu_run_program(&C, GPU_RAM);

    ASSERT_EQ_U32(C.gpu_reg_bank_0[3], 1);
    ASSERT_TRUE(gpu_read_flags(&C) & GPU_FLAG_ZERO);
}

TEST(sub_basic)
{
    gpu_exec_one(gpu_encode(GPU_OP_SUB, 1, 2), 10, 1, 30, 2);
    ASSERT_EQ_U32(C.gpu_reg_bank_0[2], 20);
}

TEST(sub_negative)
{
    gpu_exec_one(gpu_encode(GPU_OP_SUB, 1, 2), 30, 1, 10, 2);
    /* 10 - 30 = -20 = 0xFFFFFFEC */
    ASSERT_EQ_U32(C.gpu_reg_bank_0[2], 0xFFFFFFEC);
    ASSERT_TRUE(gpu_read_flags(&C) & GPU_FLAG_NEGA);
    ASSERT_TRUE(gpu_read_flags(&C) & GPU_FLAG_CARRY);
}

TEST(sub_zero)
{
    gpu_exec_one(gpu_encode(GPU_OP_SUB, 1, 2), 42, 1, 42, 2);
    ASSERT_EQ_U32(C.gpu_reg_bank_0[2], 0);
    ASSERT_TRUE(gpu_read_flags(&C) & GPU_FLAG_ZERO);
}

TEST(subq_basic)
{
    gpu_exec_one(gpu_encode(GPU_OP_SUBQ, 5, 2), 0, 1, 100, 2);
    ASSERT_EQ_U32(C.gpu_reg_bank_0[2], 95);
}

TEST(neg_basic)
{
    gpu_exec_one(gpu_encode(GPU_OP_NEG, 0, 2), 0, 0, 42, 2);
    ASSERT_EQ_U32(C.gpu_reg_bank_0[2], (uint32_t)-42);
    ASSERT_TRUE(gpu_read_flags(&C) & GPU_FLAG_NEGA);
}

TEST(neg_zero)
{
    gpu_exec_one(gpu_encode(GPU_OP_NEG, 0, 2), 0, 0, 0, 2);
    ASSERT_EQ_U32(C.gpu_reg_bank_0[2], 0);
    ASSERT_TRUE(gpu_read_flags(&C) & GPU_FLAG_ZERO);
}

TEST(abs_positive)
{
    gpu_exec_one(gpu_encode(GPU_OP_ABS, 0, 2), 0, 0, 42, 2);
    ASSERT_EQ_U32(C.gpu_reg_bank_0[2], 42);
}

TEST(abs_negative)
{
    gpu_exec_one(gpu_encode(GPU_OP_ABS, 0, 2), 0, 0, (uint32_t)-42, 2);
    ASSERT_EQ_U32(C.gpu_reg_bank_0[2], 42);
}

/* ------------------------------------------------------------------ */
/* Multiply / Divide                                                   */
/* ------------------------------------------------------------------ */

TEST(mult_basic)
{
    /* MULT: unsigned 16-bit × 16-bit → 32-bit result in Rn */
    gpu_exec_one(gpu_encode(GPU_OP_MULT, 1, 2), 100, 1, 200, 2);
    ASSERT_EQ_U32(C.gpu_reg_bank_0[2], 20000);
}

TEST(mult_zero)
{
    gpu_exec_one(gpu_encode(GPU_OP_MULT, 1, 2), 0, 1, 12345, 2);
    ASSERT_EQ_U32(C.gpu_reg_bank_0[2], 0);
    ASSERT_TRUE(gpu_read_flags(&C) & GPU_FLAG_ZERO);
}

TEST(imult_signed)
{
    /* IMULT: signed 16-bit × 16-bit → signed 32-bit */
    gpu_exec_one(gpu_encode(GPU_OP_IMULT, 1, 2),
                 (uint32_t)-3 & 0xFFFF, 1, 7, 2);
    /* (-3) * 7 = -21 */
    ASSERT_EQ_U32(C.gpu_reg_bank_0[2], (uint32_t)-21);
    ASSERT_TRUE(gpu_read_flags(&C) & GPU_FLAG_NEGA);
}

TEST(div_basic)
{
    /* DIV: RN.L / RM.L → quotient in RN, remainder in ??? */
    gpu_exec_one(gpu_encode(GPU_OP_DIV, 1, 2), 7, 1, 100, 2);
    ASSERT_EQ_U32(C.gpu_reg_bank_0[2], 14);  /* 100 / 7 = 14 */
}

TEST(div_by_zero)
{
    /* Division by zero — implementation-defined but should not crash */
    gpu_exec_one(gpu_encode(GPU_OP_DIV, 1, 2), 0, 1, 100, 2);
    /* Just verify GPU still runs (didn't hang) */
    ASSERT_FALSE(C.GPUIsRunning());
}

/* ------------------------------------------------------------------ */
/* Logic tests                                                         */
/* ------------------------------------------------------------------ */

TEST(and_basic)
{
    gpu_exec_one(gpu_encode(GPU_OP_AND, 1, 2), 0xFF00FF00, 1, 0xFFFF0000, 2);
    ASSERT_EQ_U32(C.gpu_reg_bank_0[2], 0xFF000000);
}

TEST(or_basic)
{
    gpu_exec_one(gpu_encode(GPU_OP_OR, 1, 2), 0x00FF00FF, 1, 0xFF00FF00, 2);
    ASSERT_EQ_U32(C.gpu_reg_bank_0[2], 0xFFFFFFFF);
}

TEST(xor_basic)
{
    gpu_exec_one(gpu_encode(GPU_OP_XOR, 1, 2), 0xAAAAAAAA, 1, 0xFFFFFFFF, 2);
    ASSERT_EQ_U32(C.gpu_reg_bank_0[2], 0x55555555);
}

TEST(not_basic)
{
    gpu_exec_one(gpu_encode(GPU_OP_NOT, 0, 2), 0, 0, 0xFF00FF00, 2);
    ASSERT_EQ_U32(C.gpu_reg_bank_0[2], 0x00FF00FF);
}

TEST(btst_set)
{
    /* BTST #n, Rn: test bit n of Rn, set ZERO if bit is clear */
    gpu_exec_one(gpu_encode(GPU_OP_BTST, 0, 2), 0, 0, 0x00000001, 2);
    ASSERT_FALSE(gpu_read_flags(&C) & GPU_FLAG_ZERO);  /* bit 0 is set */
}

TEST(btst_clear)
{
    gpu_exec_one(gpu_encode(GPU_OP_BTST, 0, 2), 0, 0, 0x00000002, 2);
    ASSERT_TRUE(gpu_read_flags(&C) & GPU_FLAG_ZERO);  /* bit 0 is clear */
}

TEST(bset_basic)
{
    gpu_exec_one(gpu_encode(GPU_OP_BSET, 7, 2), 0, 0, 0, 2);
    ASSERT_EQ_U32(C.gpu_reg_bank_0[2], 0x80);
}

TEST(bclr_basic)
{
    gpu_exec_one(gpu_encode(GPU_OP_BCLR, 0, 2), 0, 0, 0xFF, 2);
    ASSERT_EQ_U32(C.gpu_reg_bank_0[2], 0xFE);
}

/* ------------------------------------------------------------------ */
/* Shift / Rotate                                                      */
/* ------------------------------------------------------------------ */

TEST(shlq_basic)
{
    /* SHLQ #n, Rn: shift left by (32-n) */
    gpu_exec_one(gpu_encode(GPU_OP_SHLQ, 31, 2), 0, 0, 1, 2);
    ASSERT_EQ_U32(C.gpu_reg_bank_0[2], 2);  /* 32-31 = shift left 1 */
}

TEST(shlq_large)
{
    gpu_exec_one(gpu_encode(GPU_OP_SHLQ, 16, 2), 0, 0, 1, 2);
    ASSERT_EQ_U32(C.gpu_reg_bank_0[2], 0x00010000);  /* shift left 16 */
}

TEST(shrq_basic)
{
    /* SHRQ #n, Rn: shift right by n (0 encodes 32) */
    gpu_exec_one(gpu_encode(GPU_OP_SHRQ, 31, 2), 0, 0, 0x80000000, 2);
    ASSERT_EQ_U32(C.gpu_reg_bank_0[2], 0x00000001);  /* 0x80000000 >> 31 */
}

TEST(sharq_sign_extend)
{
    /* SHARQ #n: arithmetic shift right by n — sign-extends */
    gpu_exec_one(gpu_encode(GPU_OP_SHARQ, 31, 2), 0, 0, 0x80000000, 2);
    ASSERT_EQ_U32(C.gpu_reg_bank_0[2], 0xFFFFFFFF);  /* sign-extended >> 31 */
}

TEST(sh_right_positive)
{
    /* SH Rm, Rn: positive Rm = shift RIGHT, negative Rm = shift LEFT */
    gpu_exec_one(gpu_encode(GPU_OP_SH, 1, 2), 4, 1, 0xFF, 2);
    ASSERT_EQ_U32(C.gpu_reg_bank_0[2], 0x0F);  /* 0xFF >> 4 */
}

TEST(sh_left_negative)
{
    /* Negative Rm = shift left by |Rm| */
    gpu_exec_one(gpu_encode(GPU_OP_SH, 1, 2), (uint32_t)-4, 1, 1, 2);
    ASSERT_EQ_U32(C.gpu_reg_bank_0[2], 0x10);  /* 1 << 4 */
}

TEST(ror_basic)
{
    gpu_exec_one(gpu_encode(GPU_OP_ROR, 1, 2), 4, 1, 0xFF, 2);
    ASSERT_EQ_U32(C.gpu_reg_bank_0[2], 0xF000000F);  /* rotate right 4 */
}

TEST(rorq_basic)
{
    /* RORQ #n, Rn: rotate right by n (src field direct, 0 means 32) */
    gpu_exec_one(gpu_encode(GPU_OP_RORQ, 31, 2), 0, 0, 0xFF, 2);
    /* 0xFF rotated right by 31 = rotated left by 1 = 0x1FE */
    ASSERT_EQ_U32(C.gpu_reg_bank_0[2], 0x1FE);
}

/* ------------------------------------------------------------------ */
/* Compare                                                             */
/* ------------------------------------------------------------------ */

TEST(cmp_equal)
{
    gpu_exec_one(gpu_encode(GPU_OP_CMP, 1, 2), 42, 1, 42, 2);
    ASSERT_TRUE(gpu_read_flags(&C) & GPU_FLAG_ZERO);
    ASSERT_EQ_U32(C.gpu_reg_bank_0[2], 42);  /* CMP doesn't modify Rn */
}

TEST(cmp_less)
{
    gpu_exec_one(gpu_encode(GPU_OP_CMP, 1, 2), 100, 1, 42, 2);
    ASSERT_TRUE(gpu_read_flags(&C) & GPU_FLAG_NEGA);
    ASSERT_TRUE(gpu_read_flags(&C) & GPU_FLAG_CARRY);
}

TEST(cmp_greater)
{
    gpu_exec_one(gpu_encode(GPU_OP_CMP, 1, 2), 10, 1, 42, 2);
    ASSERT_FALSE(gpu_read_flags(&C) & GPU_FLAG_ZERO);
    ASSERT_FALSE(gpu_read_flags(&C) & GPU_FLAG_NEGA);
}

TEST(cmpq_equal)
{
    /* CMPQ #imm, Rn: compare immediate (0-31) with Rn */
    gpu_exec_one(gpu_encode(GPU_OP_CMPQ, 5, 2), 0, 0, 5, 2);
    ASSERT_TRUE(gpu_read_flags(&C) & GPU_FLAG_ZERO);
}

/* ------------------------------------------------------------------ */
/* Move / MOVEI                                                        */
/* ------------------------------------------------------------------ */

TEST(move_basic)
{
    gpu_exec_one(gpu_encode(GPU_OP_MOVE, 1, 2), 0xDEADBEEF, 1, 0, 2);
    ASSERT_EQ_U32(C.gpu_reg_bank_0[2], 0xDEADBEEF);
}

TEST(moveq_basic)
{
    /* MOVEQ #imm, Rn: load 0-31 into Rn */
    gpu_exec_one(gpu_encode(GPU_OP_MOVEQ, 17, 2), 0, 0, 0, 2);
    ASSERT_EQ_U32(C.gpu_reg_bank_0[2], 17);
}

TEST(movei_basic)
{
    gpu_test_setup();
    gpu_write_movei(&C, GPU_RAM, 2, 0xCAFEBEEF);
    gpu_fill_nops(&C, GPU_RAM + 6, GPU_RAM + 32);
    gpu_run_program(&C, GPU_RAM);
    ASSERT_EQ_U32(C.gpu_reg_bank_0[2], 0xCAFEBEEF);
}

/* ------------------------------------------------------------------ */
/* Load / Store (GPU RAM access)                                       */
/* ------------------------------------------------------------------ */

TEST(store_load_long)
{
    gpu_test_setup();
    uint32_t prog = GPU_RAM + 0x80;  /* program at +0x80 */
    uint32_t data = GPU_RAM + 0xF00; /* data area far from program to avoid GPU executing it */

    /* MOVEI #data_addr, R3 */
    gpu_write_movei(&C, prog, 3, data);
    prog += 6;
    /* R1 = 0xDEADBEEF */
    C.gpu_reg_bank_0[1] = 0xDEADBEEF;
    /* STORE R1, (R3): Rm=R3 (addr) in src, Rn=R1 (data) in dst */
    C.GPUWriteWord(prog, gpu_encode(GPU_OP_STORE, 3, 1), 0);
    prog += 2;
    /* Clear R2 */
    C.gpu_reg_bank_0[2] = 0;
    /* LOAD (R3), R2 */
    C.GPUWriteWord(prog, gpu_encode(GPU_OP_LOAD, 3, 2), 0);
    prog += 2;
    gpu_fill_nops(&C, prog, prog + 16);
    gpu_run_program(&C, GPU_RAM + 0x80);

    ASSERT_EQ_U32(C.gpu_reg_bank_0[2], 0xDEADBEEF);
}

TEST(store_load_word)
{
    gpu_test_setup();
    uint32_t prog = GPU_RAM + 0x80;
    uint32_t data = GPU_RAM + 0xF00;

    gpu_write_movei(&C, prog, 3, data);
    prog += 6;
    C.gpu_reg_bank_0[1] = 0xBEEF;
    C.GPUWriteWord(prog, gpu_encode(GPU_OP_STOREW, 3, 1), 0);
    prog += 2;
    C.gpu_reg_bank_0[2] = 0;
    C.GPUWriteWord(prog, gpu_encode(GPU_OP_LOADW, 3, 2), 0);
    prog += 2;
    gpu_fill_nops(&C, prog, prog + 16);
    gpu_run_program(&C, GPU_RAM + 0x80);

    ASSERT_EQ_U32(C.gpu_reg_bank_0[2], 0xBEEF);
}

TEST(store_load_byte)
{
    gpu_test_setup();
    uint32_t prog = GPU_RAM + 0x80;
    uint32_t data = GPU_RAM + 0xF00;

    gpu_write_movei(&C, prog, 3, data);
    prog += 6;
    C.gpu_reg_bank_0[1] = 0x42;
    C.GPUWriteWord(prog, gpu_encode(GPU_OP_STOREB, 3, 1), 0);
    prog += 2;
    C.gpu_reg_bank_0[2] = 0;
    C.GPUWriteWord(prog, gpu_encode(GPU_OP_LOADB, 3, 2), 0);
    prog += 2;
    gpu_fill_nops(&C, prog, prog + 16);
    gpu_run_program(&C, GPU_RAM + 0x80);

    ASSERT_EQ_U32(C.gpu_reg_bank_0[2], 0x42);
}

/* ------------------------------------------------------------------ */
/* Saturation                                                          */
/* ------------------------------------------------------------------ */

TEST(sat8_clamp)
{
    gpu_exec_one(gpu_encode(GPU_OP_SAT8, 0, 2), 0, 0, 0x1FF, 2);
    ASSERT_EQ_U32(C.gpu_reg_bank_0[2], 0xFF);
}

TEST(sat8_no_clamp)
{
    gpu_exec_one(gpu_encode(GPU_OP_SAT8, 0, 2), 0, 0, 200, 2);
    ASSERT_EQ_U32(C.gpu_reg_bank_0[2], 200);
}

TEST(sat16_clamp)
{
    gpu_exec_one(gpu_encode(GPU_OP_SAT16, 0, 2), 0, 0, 0x1FFFF, 2);
    ASSERT_EQ_U32(C.gpu_reg_bank_0[2], 0xFFFF);
}

TEST(sat16_negative)
{
    gpu_exec_one(gpu_encode(GPU_OP_SAT16, 0, 2), 0, 0, 0x80000000, 2);
    ASSERT_EQ_U32(C.gpu_reg_bank_0[2], 0);
}

/* ------------------------------------------------------------------ */
/* Register bank switching                                             */
/* ------------------------------------------------------------------ */

TEST(register_bank_switch)
{
    gpu_test_setup();
    /* Set distinct values in both banks */
    C.gpu_reg_bank_0[5] = 0xAAAAAAAA;
    C.gpu_reg_bank_1[5] = 0xBBBBBBBB;

    /* After reset, bank 0 is active; R5 should be 0xAAAAAAAA */
    /* Write program: MOVE R5, R6 (captures R5 into R6) */
    C.GPUWriteWord(GPU_RAM, gpu_encode(GPU_OP_MOVE, 5, 6), 0);
    gpu_fill_nops(&C, GPU_RAM + 2, GPU_RAM + 32);
    gpu_run_program(&C, GPU_RAM);

    ASSERT_EQ_U32(C.gpu_reg_bank_0[6], 0xAAAAAAAA);
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
    vj_core_init(&C);

    if (!C.gpu_reg_bank_0 || !C.GPUReset || !C.GPUExec)
    {
        fprintf(stderr, "Required GPU symbols not found\n");
        vj_core_unload(&C);
        return 1;
    }

    TEST_INIT("GPU RISC Instructions");

    /* Arithmetic */
    RUN_TEST(add_basic);
    RUN_TEST(add_zero_flag);
    RUN_TEST(add_carry_flag);
    RUN_TEST(add_negative_flag);
    RUN_TEST(addq_basic);
    RUN_TEST(addq_32);
    RUN_TEST(addqt_no_flags);
    RUN_TEST(sub_basic);
    RUN_TEST(sub_negative);
    RUN_TEST(sub_zero);
    RUN_TEST(subq_basic);
    RUN_TEST(neg_basic);
    RUN_TEST(neg_zero);
    RUN_TEST(abs_positive);
    RUN_TEST(abs_negative);

    /* Multiply / Divide */
    RUN_TEST(mult_basic);
    RUN_TEST(mult_zero);
    RUN_TEST(imult_signed);
    RUN_TEST(div_basic);
    RUN_TEST(div_by_zero);

    /* Logic */
    RUN_TEST(and_basic);
    RUN_TEST(or_basic);
    RUN_TEST(xor_basic);
    RUN_TEST(not_basic);
    RUN_TEST(btst_set);
    RUN_TEST(btst_clear);
    RUN_TEST(bset_basic);
    RUN_TEST(bclr_basic);

    /* Shift / Rotate */
    RUN_TEST(shlq_basic);
    RUN_TEST(shlq_large);
    RUN_TEST(shrq_basic);
    RUN_TEST(sharq_sign_extend);
    RUN_TEST(sh_right_positive);
    RUN_TEST(sh_left_negative);
    RUN_TEST(ror_basic);
    RUN_TEST(rorq_basic);

    /* Compare */
    RUN_TEST(cmp_equal);
    RUN_TEST(cmp_less);
    RUN_TEST(cmp_greater);
    RUN_TEST(cmpq_equal);

    /* Move */
    RUN_TEST(move_basic);
    RUN_TEST(moveq_basic);
    RUN_TEST(movei_basic);

    /* Load / Store */
    RUN_TEST(store_load_long);
    RUN_TEST(store_load_word);
    RUN_TEST(store_load_byte);

    /* Saturation */
    RUN_TEST(sat8_clamp);
    RUN_TEST(sat8_no_clamp);
    RUN_TEST(sat16_clamp);
    RUN_TEST(sat16_negative);

    /* Register banks */
    RUN_TEST(register_bank_switch);

    int result = TEST_REPORT();
    vj_core_unload(&C);
    return result;
}
