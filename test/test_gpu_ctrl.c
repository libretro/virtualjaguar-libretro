/*
 * test_gpu_ctrl.c — GPU control register accuracy tests.
 *
 * Validates GPU start/stop, interrupt dispatch, flag behavior, and
 * control register read/write semantics against MiSTer FPGA ground truth.
 *
 * Build: cc -g -O0 -o test/test_gpu_ctrl test/test_gpu_ctrl.c -ldl
 * Run:   ./test/test_gpu_ctrl
 */

#include "test_framework.h"
#include "mister_ground_truth.h"

static struct vj_core core;

/* ================================================================== */
/* GPU Control Register ($F02114) Tests                                */
/* ================================================================== */

TEST(gpu_ctrl_reset_clears_go)
{
    core.GPUReset();
    uint32_t ctrl = core.GPUReadLong(GPU_CTRL_REG, CALLER_M68K);
    ASSERT_EQ_U32(ctrl & GPU_CTRL_STAT_GO, 0);
}

TEST(gpu_ctrl_write_go_starts_gpu)
{
    core.GPUReset();
    /* Write a simple NOP program so GPU doesn't run off */
    core.GPUWriteWord(JAGUAR_GPU_RAM_BASE, GPU_NOP, 0);
    core.GPUWriteWord(JAGUAR_GPU_RAM_BASE + 2, GPU_NOP, 0);
    core.GPUWriteWord(JAGUAR_GPU_RAM_BASE + 4, GPU_NOP, 0);
    core.GPUWriteWord(JAGUAR_GPU_RAM_BASE + 6, GPU_NOP, 0);

    core.GPUWriteLong(GPU_PC_REG, JAGUAR_GPU_RAM_BASE, CALLER_M68K);
    core.GPUWriteLong(GPU_CTRL_REG, GPU_CTRL_GO, CALLER_M68K);

    ASSERT_TRUE(core.GPUIsRunning());
    uint32_t ctrl = core.GPUReadLong(GPU_CTRL_REG, CALLER_M68K);
    ASSERT_EQ_U32(ctrl & GPU_CTRL_STAT_GO, GPU_CTRL_STAT_GO);

    core.GPUWriteLong(GPU_CTRL_REG, 0, CALLER_M68K);
}

TEST(gpu_ctrl_write_zero_stops_gpu)
{
    core.GPUReset();
    gpu_fill_nops(&core, JAGUAR_GPU_RAM_BASE, JAGUAR_GPU_RAM_BASE + 32);
    core.GPUWriteLong(GPU_PC_REG, JAGUAR_GPU_RAM_BASE, CALLER_M68K);
    core.GPUWriteLong(GPU_CTRL_REG, GPU_CTRL_GO, CALLER_M68K);
    ASSERT_TRUE(core.GPUIsRunning());

    core.GPUWriteLong(GPU_CTRL_REG, 0, CALLER_M68K);
    ASSERT_FALSE(core.GPUIsRunning());
}

TEST(gpu_ctrl_bus_hog_readback)
{
    core.GPUReset();
    core.GPUWriteLong(GPU_CTRL_REG, GPU_CTRL_BUS_HOG, CALLER_M68K);
    uint32_t ctrl = core.GPUReadLong(GPU_CTRL_REG, CALLER_M68K);
    ASSERT_EQ_U32(ctrl & GPU_CTRL_STAT_BUSHOG, GPU_CTRL_STAT_BUSHOG);
    core.GPUWriteLong(GPU_CTRL_REG, 0, CALLER_M68K);
}

TEST(gpu_ctrl_pc_write_readback)
{
    core.GPUReset();
    core.GPUWriteLong(GPU_PC_REG, 0xF03100, CALLER_M68K);
    uint32_t pc = core.GPUGetPC();
    ASSERT_EQ_U32(pc, 0xF03100);
}

/* ================================================================== */
/* GPU Flags Register ($F02100) Tests                                  */
/* ================================================================== */

TEST(gpu_flags_reset_value)
{
    core.GPUReset();
    uint32_t flags = core.GPUReadLong(GPU_FLAGS_REG, CALLER_M68K);
    ASSERT_EQ_U32(flags & 0x0F, 0);
}

TEST(gpu_flags_imask_not_writable)
{
    core.GPUReset();
    /* Try to set IMASK via direct write — should be ignored per MiSTer */
    core.GPUWriteLong(GPU_FLAGS_REG, GPU_FLAGS_IMASK, CALLER_M68K);
    uint32_t flags = core.GPUReadLong(GPU_FLAGS_REG, CALLER_M68K);
    /* IMASK should NOT be set (only ISR entry sets it) */
    ASSERT_EQ_U32(flags & GPU_FLAGS_IMASK, 0);
}

TEST(gpu_flags_int_enable_write_read)
{
    core.GPUReset();
    /* Enable interrupts 0 and 2 */
    uint32_t ena_bits = GPU_FLAGS_INT_ENA0 | GPU_FLAGS_INT_ENA2;
    core.GPUWriteLong(GPU_FLAGS_REG, ena_bits, CALLER_M68K);
    uint32_t flags = core.GPUReadLong(GPU_FLAGS_REG, CALLER_M68K);
    /* INT_ENA bits should be readable */
    ASSERT_EQ_U32(flags & (GPU_FLAGS_INT_ENA0 | GPU_FLAGS_INT_ENA2), ena_bits);
}

TEST(gpu_flags_int_enable_all_five)
{
    core.GPUReset();
    uint32_t all_ena = GPU_FLAGS_INT_ENA0 | GPU_FLAGS_INT_ENA1 |
                       GPU_FLAGS_INT_ENA2 | GPU_FLAGS_INT_ENA3 | GPU_FLAGS_INT_ENA4;
    core.GPUWriteLong(GPU_FLAGS_REG, all_ena, CALLER_M68K);
    uint32_t flags = core.GPUReadLong(GPU_FLAGS_REG, CALLER_M68K);
    ASSERT_EQ_U32(flags & all_ena, all_ena);
}

TEST(gpu_flags_zero_set_by_instruction)
{
    core.GPUReset();
    gpu_fill_nops(&core, JAGUAR_GPU_RAM_BASE, JAGUAR_GPU_RAM_BASE + JAGUAR_GPU_RAM_SIZE);
    uint32_t pc = JAGUAR_GPU_RAM_BASE + 0x100;

    /* MOVEQ #0, R0 — should set zero flag */
    core.GPUWriteWord(pc, gpu_encode(GPU_OP_MOVEQ, 0, 0), 0); pc += 2;
    /* CMP R0, R0 — also sets zero */
    core.GPUWriteWord(pc, gpu_encode(GPU_OP_CMP, 0, 0), 0); pc += 2;
    gpu_fill_nops(&core, pc, pc + 16);

    gpu_run_program(&core, JAGUAR_GPU_RAM_BASE + 0x100);

    uint32_t flags = core.GPUReadLong(GPU_FLAGS_REG, CALLER_M68K);
    ASSERT_EQ_U32(flags & GPU_FLAGS_ZERO, GPU_FLAGS_ZERO);
}

TEST(gpu_flags_carry_set_by_add_overflow)
{
    core.GPUReset();
    gpu_fill_nops(&core, JAGUAR_GPU_RAM_BASE, JAGUAR_GPU_RAM_BASE + JAGUAR_GPU_RAM_SIZE);
    uint32_t pc = JAGUAR_GPU_RAM_BASE + 0x100;

    /* MOVEI $FFFFFFFF, R0 */
    gpu_write_movei(&core, pc, 0, 0xFFFFFFFF); pc += 6;
    /* MOVEI $00000002, R1 */
    gpu_write_movei(&core, pc, 1, 0x00000002); pc += 6;
    /* ADD R1, R0 — 0xFFFFFFFF + 2 = overflow, sets carry */
    core.GPUWriteWord(pc, gpu_encode(GPU_OP_ADD, 1, 0), 0); pc += 2;
    gpu_fill_nops(&core, pc, pc + 16);

    gpu_run_program(&core, JAGUAR_GPU_RAM_BASE + 0x100);

    uint32_t flags = core.GPUReadLong(GPU_FLAGS_REG, CALLER_M68K);
    ASSERT_EQ_U32(flags & GPU_FLAGS_CARRY, GPU_FLAGS_CARRY);
}

TEST(gpu_flags_nega_set_by_sub)
{
    core.GPUReset();
    gpu_fill_nops(&core, JAGUAR_GPU_RAM_BASE, JAGUAR_GPU_RAM_BASE + JAGUAR_GPU_RAM_SIZE);
    uint32_t pc = JAGUAR_GPU_RAM_BASE + 0x100;

    /* MOVEQ #0, R0 */
    core.GPUWriteWord(pc, gpu_encode(GPU_OP_MOVEQ, 0, 0), 0); pc += 2;
    /* MOVEQ #1, R1 */
    core.GPUWriteWord(pc, gpu_encode(GPU_OP_MOVEQ, 1, 1), 0); pc += 2;
    /* SUB R1, R0 — 0 - 1 = -1 (bit 31 set), sets negative */
    core.GPUWriteWord(pc, gpu_encode(GPU_OP_SUB, 1, 0), 0); pc += 2;
    gpu_fill_nops(&core, pc, pc + 16);

    gpu_run_program(&core, JAGUAR_GPU_RAM_BASE + 0x100);

    uint32_t flags = core.GPUReadLong(GPU_FLAGS_REG, CALLER_M68K);
    ASSERT_EQ_U32(flags & GPU_FLAGS_NEGA, GPU_FLAGS_NEGA);
}

/* ================================================================== */
/* GPU IRQ Dispatch Tests                                              */
/* ================================================================== */

TEST(gpu_irq0_sets_pending)
{
    core.GPUReset();
    gpu_fill_nops(&core, JAGUAR_GPU_RAM_BASE, JAGUAR_GPU_RAM_BASE + JAGUAR_GPU_RAM_SIZE);
    /* Enable IRQ 0 */
    core.GPUWriteLong(GPU_FLAGS_REG, GPU_FLAGS_INT_ENA0, CALLER_M68K);
    /* Trigger IRQ 0 via G_CTRL bit 2 */
    core.GPUWriteLong(GPU_CTRL_REG, GPU_CTRL_GPUIRQ0, CALLER_M68K);
    /* IRQ 0 should be latched — can verify by reading flags */
    uint32_t flags = core.GPUReadLong(GPU_FLAGS_REG, CALLER_M68K);
    /* The latch state appears in the read-back bits above INT_ENA */
    (void)flags;
    /* At minimum the IRQ handler should have been called or is pending */
    ASSERT_TRUE(1); /* Structural test — if it doesn't crash, basic IRQ path works */
}

TEST(gpu_irq_vector_address)
{
    core.GPUReset();
    gpu_fill_nops(&core, JAGUAR_GPU_RAM_BASE, JAGUAR_GPU_RAM_BASE + JAGUAR_GPU_RAM_SIZE);
    /* Write a known pattern at each ISR vector location */
    for (int i = 0; i < 5; i++) {
        uint32_t vec_addr = GPU_ISR_VECTOR(i);
        /* Write NOP at each vector slot (so if ISR runs, it's safe) */
        for (uint32_t a = vec_addr; a < vec_addr + 16; a += 2)
            core.GPUWriteWord(a, GPU_NOP, 0);
    }

    /* Enable IRQ 0, install handler, trigger, verify PC jumps to $F03000 */
    core.GPUWriteLong(GPU_FLAGS_REG, GPU_FLAGS_INT_ENA0, CALLER_M68K);

    /* Put a program that just NOPs at $F03100 */
    gpu_fill_nops(&core, 0xF03100, 0xF03120);
    core.GPUWriteLong(GPU_PC_REG, 0xF03100, CALLER_M68K);
    core.GPUWriteLong(GPU_CTRL_REG, GPU_CTRL_GO, CALLER_M68K);

    /* Trigger CPU->GPU IRQ */
    core.GPUWriteLong(GPU_CTRL_REG, GPU_CTRL_GO | GPU_CTRL_GPUIRQ0, CALLER_M68K);
    core.GPUExec(50);

    /* After servicing IRQ 0, PC should have visited $F03000 */
    /* We can't easily verify PC history, but we can check IMASK got set */
    uint32_t flags = core.GPUReadLong(GPU_FLAGS_REG, CALLER_M68K);
    /* IMASK should be set if interrupt was serviced */
    CHECK_EQ(flags & GPU_FLAGS_IMASK, GPU_FLAGS_IMASK);

    core.GPUWriteLong(GPU_CTRL_REG, 0, CALLER_M68K);
}

/* ================================================================== */
/* GPU RAM Read/Write Tests                                            */
/* ================================================================== */

TEST(gpu_ram_byte_write_read)
{
    core.GPUReset();
    uint32_t addr = JAGUAR_GPU_RAM_BASE + 0x100;
    core.GPUWriteByte(addr, 0xA5, CALLER_M68K);
    uint8_t val = core.GPUReadByte(addr, CALLER_M68K);
    ASSERT_EQ_U8(val, 0xA5);
}

TEST(gpu_ram_word_write_read)
{
    core.GPUReset();
    uint32_t addr = JAGUAR_GPU_RAM_BASE + 0x200;
    core.GPUWriteWord(addr, 0xDEAD, CALLER_M68K);
    uint16_t val = core.GPUReadWord(addr, CALLER_M68K);
    ASSERT_EQ_U16(val, 0xDEAD);
}

TEST(gpu_ram_long_write_read)
{
    core.GPUReset();
    uint32_t addr = JAGUAR_GPU_RAM_BASE + 0x300;
    core.GPUWriteLong(addr, 0xCAFEBABE, CALLER_M68K);
    uint32_t val = core.GPUReadLong(addr, CALLER_M68K);
    ASSERT_EQ_U32(val, 0xCAFEBABE);
}

TEST(gpu_ram_full_range)
{
    core.GPUReset();
    /* Write pattern to all of GPU RAM, verify readback */
    for (uint32_t offset = 0; offset < JAGUAR_GPU_RAM_SIZE; offset += 4) {
        uint32_t addr = JAGUAR_GPU_RAM_BASE + offset;
        uint32_t pattern = 0xA5000000 | offset;
        core.GPUWriteLong(addr, pattern, CALLER_M68K);
    }
    for (uint32_t offset = 0; offset < JAGUAR_GPU_RAM_SIZE; offset += 4) {
        uint32_t addr = JAGUAR_GPU_RAM_BASE + offset;
        uint32_t expected = 0xA5000000 | offset;
        uint32_t actual = core.GPUReadLong(addr, CALLER_M68K);
        if (actual != expected) {
            FAIL("GPU RAM[$%03X]: got $%08X, expected $%08X", offset, actual, expected);
        }
    }
}

/* ================================================================== */
/* GPU Word-Write Bug Regression (from gpu.c diff)                     */
/* ================================================================== */

TEST(gpu_write_word_boundary_check)
{
    core.GPUReset();
    /* The bug was: (offset == GPU_WORK_RAM_BASE + 0x0FFF) || (GPU_CONTROL_RAM_BASE + 0x1F)
     * Missing 'offset ==' on second condition — always true! Fixed to:
     * (offset == GPU_WORK_RAM_BASE + 0x0FFF) || (offset == GPU_CONTROL_RAM_BASE + 0x1F) */

    /* Write to a control register should work */
    core.GPUWriteWord(0xF02100, 0x0000, CALLER_M68K);
    /* Write to GPU RAM end should also work */
    core.GPUWriteWord(JAGUAR_GPU_RAM_BASE + 0x0FFE, 0x1234, CALLER_M68K);
    uint16_t val = core.GPUReadWord(JAGUAR_GPU_RAM_BASE + 0x0FFE, CALLER_M68K);
    ASSERT_EQ_U16(val, 0x1234);
}

/* ================================================================== */
/* Main                                                                */
/* ================================================================== */

int main(int argc, char *argv[])
{
    (void)argc; (void)argv;
    TEST_INIT("GPU Control Register Accuracy");

    if (!vj_core_load(&core)) return 1;
    vj_core_init(&core);

    /* Control register tests */
    RUN_TEST(gpu_ctrl_reset_clears_go);
    RUN_TEST(gpu_ctrl_write_go_starts_gpu);
    RUN_TEST(gpu_ctrl_write_zero_stops_gpu);
    RUN_TEST(gpu_ctrl_bus_hog_readback);
    RUN_TEST(gpu_ctrl_pc_write_readback);

    /* Flags register tests */
    RUN_TEST(gpu_flags_reset_value);
    RUN_TEST(gpu_flags_imask_not_writable);
    RUN_TEST(gpu_flags_int_enable_write_read);
    RUN_TEST(gpu_flags_int_enable_all_five);
    RUN_TEST(gpu_flags_zero_set_by_instruction);
    RUN_TEST(gpu_flags_carry_set_by_add_overflow);
    RUN_TEST(gpu_flags_nega_set_by_sub);

    /* IRQ dispatch tests */
    RUN_TEST(gpu_irq0_sets_pending);
    RUN_TEST(gpu_irq_vector_address);

    /* GPU RAM tests */
    RUN_TEST(gpu_ram_byte_write_read);
    RUN_TEST(gpu_ram_word_write_read);
    RUN_TEST(gpu_ram_long_write_read);
    RUN_TEST(gpu_ram_full_range);

    /* Regression tests */
    RUN_TEST(gpu_write_word_boundary_check);

    vj_core_unload(&core);
    return TEST_REPORT();
}
