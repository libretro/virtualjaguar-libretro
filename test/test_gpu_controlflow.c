/*
 * test_gpu_controlflow.c — GPU/DSP control flow instruction tests.
 *
 * Validates JR, JUMP, JR cc, JUMP cc, and delayed-slot behavior
 * against MiSTer FPGA execon.v/prefetch.v reference.
 *
 * Build: cc -g -O0 -o test/test_gpu_controlflow test/test_gpu_controlflow.c -ldl
 * Run:   ./test/test_gpu_controlflow
 */

#include "test_framework.h"
#include "mister_ground_truth.h"

static struct vj_core core;

/* Condition codes for JR/JUMP (bits 4:0 of dst field / IMM_2):
 * 0 = always (no flags checked), builds from branch_condition_table.
 * Bit 0: require Z clear (NE)
 * Bit 1: require Z set (EQ)
 * Bit 2: require C/N clear (depending on bit 4)
 * Bit 3: require C/N set (depending on bit 4)
 */
#define CC_ALWAYS  0x00  /* unconditional — no conditions to fail */
#define CC_NE      0x01  /* != (fails if Z set → requires Z clear) */
#define CC_EQ      0x02  /* == (fails if Z clear → requires Z set) */
#define CC_CC      0x04  /* carry clear */
#define CC_CS      0x08  /* carry set */
#define CC_PL      0x14  /* positive (N clear) */
#define CC_MI      0x18  /* negative (N set) */

/* JR encoding: opcode 53, src=offset (signed 5-bit, in words), dst=condition
 * JUMP encoding: opcode 52, src=target register, dst=condition
 * Both: condition in IMM_2 (dst field), operand in IMM_1 (src field) */

/* Helper: write a program, run with larger budget for control flow, read R0 */
static uint32_t run_and_read_r0(uint32_t pc_start)
{
    core.GPUWriteLong(GPU_PC_REG, pc_start, 0);
    core.GPUWriteLong(GPU_CTRL_REG, 1, 0);
    core.GPUExec(500);
    core.GPUWriteLong(GPU_CTRL_REG, 0, 0);
    return core.gpu_reg_bank_0[0];
}

/* ================================================================== */
/* JR (Jump Relative) Tests                                            */
/* ================================================================== */

TEST(gpu_jr_unconditional_forward)
{
    core.GPUInit();
    gpu_fill_nops(&core, JAGUAR_GPU_RAM_BASE, JAGUAR_GPU_RAM_BASE + JAGUAR_GPU_RAM_SIZE);
    uint32_t pc = JAGUAR_GPU_RAM_BASE + 0x200;

    /* MOVEQ #1, R0 */
    core.GPUWriteWord(pc, gpu_encode(GPU_OP_MOVEQ, 1, 0), 0); pc += 2;
    /* JR +3 (skip 3 words forward from next PC, so skip the MOVEQ #5) */
    /* JR: opcode=53, src=offset (signed 5-bit), dst=condition (0=always) */
    core.GPUWriteWord(pc, gpu_encode(GPU_OP_JR, 3, 0), 0); pc += 2;
    /* Delay slot: NOP */
    core.GPUWriteWord(pc, GPU_NOP, 0); pc += 2;
    /* This should be skipped: MOVEQ #5, R0 */
    core.GPUWriteWord(pc, gpu_encode(GPU_OP_MOVEQ, 5, 0), 0); pc += 2;
    /* MOVEQ #9, R0 — also skipped */
    core.GPUWriteWord(pc, gpu_encode(GPU_OP_MOVEQ, 9, 0), 0); pc += 2;
    /* Landing: MOVEQ #2, R0 (this is where JR +3 should land) */
    core.GPUWriteWord(pc, gpu_encode(GPU_OP_MOVEQ, 2, 0), 0); pc += 2;
    gpu_fill_nops(&core, pc, pc + 16);

    uint32_t result = run_and_read_r0(JAGUAR_GPU_RAM_BASE + 0x200);
    /* R0 should be 2 (landed at the MOVEQ #2), not 5 or 9 */
    ASSERT_EQ_U32(result, 2);
}

TEST(gpu_jr_conditional_taken)
{
    core.GPUInit();
    gpu_fill_nops(&core, JAGUAR_GPU_RAM_BASE, JAGUAR_GPU_RAM_BASE + JAGUAR_GPU_RAM_SIZE);
    uint32_t pc = JAGUAR_GPU_RAM_BASE + 0x200;

    /* MOVEQ #0, R0 — sets zero flag */
    core.GPUWriteWord(pc, gpu_encode(GPU_OP_MOVEQ, 0, 0), 0); pc += 2;
    /* CMP R0, R0 — explicitly set zero flag */
    core.GPUWriteWord(pc, gpu_encode(GPU_OP_CMP, 0, 0), 0); pc += 2;
    /* JR EQ, +2 (taken because Z is set) */
    core.GPUWriteWord(pc, gpu_encode(GPU_OP_JR, 2, CC_EQ), 0); pc += 2;
    /* Delay slot: NOP */
    core.GPUWriteWord(pc, GPU_NOP, 0); pc += 2;
    /* Skipped: MOVEQ #7, R0 */
    core.GPUWriteWord(pc, gpu_encode(GPU_OP_MOVEQ, 7, 0), 0); pc += 2;
    /* Landing: MOVEQ #3, R0 */
    core.GPUWriteWord(pc, gpu_encode(GPU_OP_MOVEQ, 3, 0), 0); pc += 2;
    gpu_fill_nops(&core, pc, pc + 16);

    uint32_t result = run_and_read_r0(JAGUAR_GPU_RAM_BASE + 0x200);
    ASSERT_EQ_U32(result, 3);
}

TEST(gpu_jr_conditional_not_taken)
{
    core.GPUInit();
    gpu_fill_nops(&core, JAGUAR_GPU_RAM_BASE, JAGUAR_GPU_RAM_BASE + JAGUAR_GPU_RAM_SIZE);
    uint32_t pc = JAGUAR_GPU_RAM_BASE + 0x200;

    /* MOVEQ #1, R0 — doesn't set zero flag */
    core.GPUWriteWord(pc, gpu_encode(GPU_OP_MOVEQ, 1, 0), 0); pc += 2;
    /* MOVEQ #1, R1 */
    core.GPUWriteWord(pc, gpu_encode(GPU_OP_MOVEQ, 1, 1), 0); pc += 2;
    /* CMP R0, R1 — sets zero (1 == 1) then... actually we want NE */
    /* Let's use MOVEQ #2, R1 so CMP gives NE */
    core.GPUWriteWord(pc - 2, gpu_encode(GPU_OP_MOVEQ, 2, 1), 0);
    /* CMP R0, R1 — R1-R0 = 2-1 = 1, not zero, not negative */
    core.GPUWriteWord(pc, gpu_encode(GPU_OP_CMP, 0, 1), 0); pc += 2;
    /* JR EQ, +2 — NOT taken because Z is clear */
    core.GPUWriteWord(pc, gpu_encode(GPU_OP_JR, 2, CC_EQ), 0); pc += 2;
    /* Delay slot: NOP */
    core.GPUWriteWord(pc, GPU_NOP, 0); pc += 2;
    /* Fall-through: MOVEQ #4, R0 */
    core.GPUWriteWord(pc, gpu_encode(GPU_OP_MOVEQ, 4, 0), 0); pc += 2;
    gpu_fill_nops(&core, pc, pc + 16);

    uint32_t result = run_and_read_r0(JAGUAR_GPU_RAM_BASE + 0x200);
    /* Should be 4 (branch not taken, executes fall-through) */
    ASSERT_EQ_U32(result, 4);
}

TEST(gpu_jr_delay_slot_executes)
{
    core.GPUInit();
    gpu_fill_nops(&core, JAGUAR_GPU_RAM_BASE, JAGUAR_GPU_RAM_BASE + JAGUAR_GPU_RAM_SIZE);
    uint32_t pc = JAGUAR_GPU_RAM_BASE + 0x200;

    /* MOVEQ #0, R0 */
    core.GPUWriteWord(pc, gpu_encode(GPU_OP_MOVEQ, 0, 0), 0); pc += 2;
    /* JR +2 (unconditional) */
    core.GPUWriteWord(pc, gpu_encode(GPU_OP_JR, 2, 0), 0); pc += 2;
    /* Delay slot: MOVEQ #10, R1 — MUST execute */
    core.GPUWriteWord(pc, gpu_encode(GPU_OP_MOVEQ, 10, 1), 0); pc += 2;
    /* Skipped */
    core.GPUWriteWord(pc, gpu_encode(GPU_OP_MOVEQ, 99, 0), 0); pc += 2;
    /* Landing: MOVE R1, R0 */
    core.GPUWriteWord(pc, gpu_encode(GPU_OP_MOVE, 1, 0), 0); pc += 2;
    gpu_fill_nops(&core, pc, pc + 16);

    uint32_t result = run_and_read_r0(JAGUAR_GPU_RAM_BASE + 0x200);
    /* R0 = R1 = 10 (delay slot set R1, landing copied to R0) */
    ASSERT_EQ_U32(result, 10);
}

/* ================================================================== */
/* JUMP (Jump Absolute) Tests                                          */
/* ================================================================== */

TEST(gpu_jump_unconditional)
{
    core.GPUInit();
    gpu_fill_nops(&core, JAGUAR_GPU_RAM_BASE, JAGUAR_GPU_RAM_BASE + JAGUAR_GPU_RAM_SIZE);
    uint32_t pc = JAGUAR_GPU_RAM_BASE + 0x200;
    uint32_t target = JAGUAR_GPU_RAM_BASE + 0x300;

    /* MOVEI target, R2 */
    gpu_write_movei(&core, pc, 2, target); pc += 6;
    /* JUMP (R2) — unconditional */
    core.GPUWriteWord(pc, gpu_encode(GPU_OP_JUMP, 2, 0), 0); pc += 2;
    /* Delay slot: NOP */
    core.GPUWriteWord(pc, GPU_NOP, 0); pc += 2;
    /* Skipped: MOVEQ #15, R0 */
    core.GPUWriteWord(pc, gpu_encode(GPU_OP_MOVEQ, 15, 0), 0); pc += 2;

    /* At target: MOVEQ #6, R0 */
    core.GPUWriteWord(target, gpu_encode(GPU_OP_MOVEQ, 6, 0), 0);
    gpu_fill_nops(&core, target + 2, target + 16);

    uint32_t result = run_and_read_r0(JAGUAR_GPU_RAM_BASE + 0x200);
    ASSERT_EQ_U32(result, 6);
}

TEST(gpu_jump_conditional_ne)
{
    core.GPUInit();
    gpu_fill_nops(&core, JAGUAR_GPU_RAM_BASE, JAGUAR_GPU_RAM_BASE + JAGUAR_GPU_RAM_SIZE);
    uint32_t pc = JAGUAR_GPU_RAM_BASE + 0x200;
    uint32_t target = JAGUAR_GPU_RAM_BASE + 0x300;

    /* MOVEQ #1, R0 */
    core.GPUWriteWord(pc, gpu_encode(GPU_OP_MOVEQ, 1, 0), 0); pc += 2;
    /* MOVEQ #2, R1 */
    core.GPUWriteWord(pc, gpu_encode(GPU_OP_MOVEQ, 2, 1), 0); pc += 2;
    /* CMP R0, R1 — sets Z=0 (not equal), N=0 (positive result) */
    core.GPUWriteWord(pc, gpu_encode(GPU_OP_CMP, 0, 1), 0); pc += 2;
    /* MOVEI target, R2 */
    gpu_write_movei(&core, pc, 2, target); pc += 6;
    /* JUMP NE, (R2) — taken because Z=0 */
    core.GPUWriteWord(pc, gpu_encode(GPU_OP_JUMP, 2, CC_NE), 0); pc += 2;
    /* Delay slot: NOP */
    core.GPUWriteWord(pc, GPU_NOP, 0); pc += 2;
    /* Fall-through (skipped): MOVEQ #20, R0 */
    core.GPUWriteWord(pc, gpu_encode(GPU_OP_MOVEQ, 20, 0), 0); pc += 2;

    /* Target: MOVEQ #8, R0 */
    core.GPUWriteWord(target, gpu_encode(GPU_OP_MOVEQ, 8, 0), 0);
    gpu_fill_nops(&core, target + 2, target + 16);

    uint32_t result = run_and_read_r0(JAGUAR_GPU_RAM_BASE + 0x200);
    ASSERT_EQ_U32(result, 8);
}

/* ================================================================== */
/* MOVPC (Move PC) Test                                                */
/* ================================================================== */

TEST(gpu_movpc_captures_pc)
{
    core.GPUInit();
    gpu_fill_nops(&core, JAGUAR_GPU_RAM_BASE, JAGUAR_GPU_RAM_BASE + JAGUAR_GPU_RAM_SIZE);
    uint32_t pc = JAGUAR_GPU_RAM_BASE + 0x200;

    /* MOVPC R0 — in this emulator, stores address of MOVPC itself (gpu_pc - 2) */
    core.GPUWriteWord(pc, gpu_encode(GPU_OP_MOVPC, 0, 0), 0); pc += 2;
    gpu_fill_nops(&core, pc, pc + 16);

    uint32_t result = run_and_read_r0(JAGUAR_GPU_RAM_BASE + 0x200);
    ASSERT_EQ_U32(result, JAGUAR_GPU_RAM_BASE + 0x200);
}

/* ================================================================== */
/* STORE/LOAD basic test                                               */
/* ================================================================== */

TEST(gpu_store_load_basic)
{
    core.GPUInit();
    gpu_fill_nops(&core, JAGUAR_GPU_RAM_BASE, JAGUAR_GPU_RAM_BASE + JAGUAR_GPU_RAM_SIZE);
    uint32_t pc = JAGUAR_GPU_RAM_BASE + 0x200;
    uint32_t data_addr = JAGUAR_GPU_RAM_BASE + 0xF00;

    /* MOVEI data_addr, R2 */
    gpu_write_movei(&core, pc, 2, data_addr); pc += 6;
    /* MOVEQ #7, R1 */
    core.GPUWriteWord(pc, gpu_encode(GPU_OP_MOVEQ, 7, 1), 0); pc += 2;
    /* STORE R1, (R2) — store 7 to data_addr: RM=R2(addr), RN=R1(data) */
    core.GPUWriteWord(pc, gpu_encode(GPU_OP_STORE, 2, 1), 0); pc += 2;
    /* MOVEQ #0, R0 */
    core.GPUWriteWord(pc, gpu_encode(GPU_OP_MOVEQ, 0, 0), 0); pc += 2;
    /* LOAD (R2), R0 — load from data_addr into R0 */
    core.GPUWriteWord(pc, gpu_encode(GPU_OP_LOAD, 2, 0), 0); pc += 2;
    gpu_fill_nops(&core, pc, pc + 16);

    uint32_t result = run_and_read_r0(JAGUAR_GPU_RAM_BASE + 0x200);
    ASSERT_EQ_U32(result, 7);
}

/* ================================================================== */
/* STORE/LOAD (Subroutine Pattern) Test                                */
/* ================================================================== */

TEST(gpu_store_load_subroutine_pattern)
{
    core.GPUInit();
    gpu_fill_nops(&core, JAGUAR_GPU_RAM_BASE, JAGUAR_GPU_RAM_BASE + JAGUAR_GPU_RAM_SIZE);
    uint32_t pc = JAGUAR_GPU_RAM_BASE + 0x200;
    uint32_t sub_addr = JAGUAR_GPU_RAM_BASE + 0x300;

    /* Simple call/return: JUMP to subroutine, subroutine JUMPs back via R3 */

    /* MOVEQ #0, R0 */
    core.GPUWriteWord(pc, gpu_encode(GPU_OP_MOVEQ, 0, 0), 0); pc += 2;
    /* MOVEI sub_addr, R2 (call target) */
    gpu_write_movei(&core, pc, 2, sub_addr); pc += 6;
    /* Calculate return address: after MOVEI(6) + JUMP(2) + NOP(2) = 10 */
    uint32_t return_addr = pc + 10;
    /* MOVEI return_addr, R3 (link register) */
    gpu_write_movei(&core, pc, 3, return_addr); pc += 6;
    /* JUMP (R2) — call subroutine */
    core.GPUWriteWord(pc, gpu_encode(GPU_OP_JUMP, 2, 0), 0); pc += 2;
    /* Delay slot: NOP */
    core.GPUWriteWord(pc, GPU_NOP, 0); pc += 2;
    /* Return landing */
    gpu_fill_nops(&core, pc, pc + 16);

    /* Subroutine: MOVEQ #12, R0; JUMP (R3); NOP */
    uint32_t sp = sub_addr;
    core.GPUWriteWord(sp, gpu_encode(GPU_OP_MOVEQ, 12, 0), 0); sp += 2;
    core.GPUWriteWord(sp, gpu_encode(GPU_OP_JUMP, 3, 0), 0); sp += 2;
    core.GPUWriteWord(sp, GPU_NOP, 0); sp += 2;
    gpu_fill_nops(&core, sp, sp + 16);

    uint32_t result = run_and_read_r0(JAGUAR_GPU_RAM_BASE + 0x200);
    ASSERT_EQ_U32(result, 12);
}

/* ================================================================== */
/* Main                                                                */
/* ================================================================== */

int main(int argc, char *argv[])
{
    (void)argc; (void)argv;
    TEST_INIT("GPU Control Flow");

    if (!vj_core_load(&core)) return 1;
    vj_core_init(&core);

    /* JR tests */
    RUN_TEST(gpu_jr_unconditional_forward);
    RUN_TEST(gpu_jr_conditional_taken);
    RUN_TEST(gpu_jr_conditional_not_taken);
    RUN_TEST(gpu_jr_delay_slot_executes);

    /* JUMP tests */
    RUN_TEST(gpu_jump_unconditional);
    RUN_TEST(gpu_jump_conditional_ne);

    /* MOVPC */
    RUN_TEST(gpu_movpc_captures_pc);

    /* STORE/LOAD */
    RUN_TEST(gpu_store_load_basic);

    /* Subroutine pattern */
    RUN_TEST(gpu_store_load_subroutine_pattern);

    vj_core_unload(&core);
    return TEST_REPORT();
}
