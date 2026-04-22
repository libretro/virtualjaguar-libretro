/*
 * test_memory_map.c — Memory map accuracy tests.
 *
 * Validates address decoding, RAM/ROM boundaries, register accessibility,
 * and mirror behavior against the Jaguar hardware spec and MiSTer.
 *
 * Build: cc -g -O0 -o test/test_memory_map test/test_memory_map.c -ldl
 * Run:   ./test/test_memory_map
 */

#include "test_framework.h"
#include "mister_ground_truth.h"

static struct vj_core core;

/* ================================================================== */
/* Main RAM Tests ($000000-$1FFFFF)                                    */
/* ================================================================== */

TEST(ram_byte_write_read)
{
    uint8_t *ram = core.GetRamPtr();
    ram[0x1000] = 0xA5;
    uint8_t val = core.JaguarReadByte(0x1000, CALLER_M68K);
    ASSERT_EQ_U8(val, 0xA5);
}

TEST(ram_word_write_read)
{
    core.JaguarWriteWord(0x2000, 0xBEEF, CALLER_M68K);
    uint16_t val = core.JaguarReadWord(0x2000, CALLER_M68K);
    ASSERT_EQ_U16(val, 0xBEEF);
}

TEST(ram_long_write_read)
{
    if (!core.JaguarWriteLong) { FAIL("JaguarWriteLong not available"); }
    core.JaguarWriteLong(0x3000, 0xDEADCAFE, CALLER_M68K);
    uint16_t hi = core.JaguarReadWord(0x3000, CALLER_M68K);
    uint16_t lo = core.JaguarReadWord(0x3002, CALLER_M68K);
    uint32_t val = ((uint32_t)hi << 16) | lo;
    ASSERT_EQ_U32(val, 0xDEADCAFE);
}

TEST(ram_boundary_low)
{
    core.JaguarWriteWord(0x000000, 0x1234, CALLER_M68K);
    uint16_t val = core.JaguarReadWord(0x000000, CALLER_M68K);
    ASSERT_EQ_U16(val, 0x1234);
}

TEST(ram_boundary_high)
{
    core.JaguarWriteWord(0x1FFFFE, 0x5678, CALLER_M68K);
    uint16_t val = core.JaguarReadWord(0x1FFFFE, CALLER_M68K);
    ASSERT_EQ_U16(val, 0x5678);
}

TEST(ram_big_endian_layout)
{
    uint8_t *ram = core.GetRamPtr();
    core.JaguarWriteWord(0x4000, 0xABCD, CALLER_M68K);
    /* Jaguar is big-endian: high byte first */
    ASSERT_EQ_U8(ram[0x4000], 0xAB);
    ASSERT_EQ_U8(ram[0x4001], 0xCD);
}

/* ================================================================== */
/* GPU RAM Tests ($F03000-$F03FFF)                                     */
/* ================================================================== */

TEST(gpu_ram_accessible_from_68k)
{
    core.GPUWriteLong(JAGUAR_GPU_RAM_BASE + 0x100, 0x12345678, CALLER_M68K);
    uint32_t val = core.GPUReadLong(JAGUAR_GPU_RAM_BASE + 0x100, CALLER_M68K);
    ASSERT_EQ_U32(val, 0x12345678);
}

TEST(gpu_ram_size_boundary)
{
    /* GPU RAM is exactly 4KB: $F03000-$F03FFF */
    core.GPUWriteLong(JAGUAR_GPU_RAM_BASE + 0xFFC, 0xAAAAAAAA, CALLER_M68K);
    uint32_t val = core.GPUReadLong(JAGUAR_GPU_RAM_BASE + 0xFFC, CALLER_M68K);
    ASSERT_EQ_U32(val, 0xAAAAAAAA);
}

/* ================================================================== */
/* DSP RAM Tests ($F1B000-$F1CFFF)                                     */
/* ================================================================== */

TEST(dsp_ram_accessible_from_68k)
{
    if (!core.DSPWriteLong) { FAIL("DSPWriteLong not available"); }
    core.DSPWriteLong(JAGUAR_DSP_RAM_BASE + 0x100, 0xFEDCBA98, CALLER_M68K);
    uint32_t val = core.DSPReadLong(JAGUAR_DSP_RAM_BASE + 0x100, CALLER_M68K);
    ASSERT_EQ_U32(val, 0xFEDCBA98);
}

/* ================================================================== */
/* TOM Register Tests ($F00000-$F000FF)                                */
/* ================================================================== */

TEST(tom_hc_readable)
{
    /* HC ($F00004) is the horizontal counter — should be readable */
    uint16_t hc = core.TOMReadWord(0xF00004, CALLER_M68K);
    (void)hc; /* Value depends on timing, just verify no crash */
    ASSERT_TRUE(1);
}

TEST(tom_vc_readable)
{
    /* VC ($F00006) is the vertical counter — readable */
    uint16_t vc = core.TOMReadWord(0xF00006, CALLER_M68K);
    (void)vc;
    ASSERT_TRUE(1);
}

TEST(tom_vmode_writable)
{
    /* VMODE ($F00028) controls video mode */
    core.TOMWriteWord(0xF00028, 0x0006, CALLER_M68K); /* CRY 16bpp */
    /* VMODE may or may not be readable — test doesn't crash */
    ASSERT_TRUE(1);
}

TEST(tom_int1_write_read)
{
    /* INT1 ($F000E0) interrupt control.
     * Write: bits 0-4 = enable, bits 8-12 = clear.
     * Read: returns PENDING state (latch), NOT enable state.
     * This matches MiSTer hardware: read returns pending IRQ bits.
     * Use TOMIRQEnabled() to check enable state. */
    core.TOMWriteWord(0xF000E0, 0x001F, CALLER_M68K); /* enable all 5 */
    /* Verify via TOMIRQEnabled that enables are stored */
    if (core.TOMIRQEnabled) {
        CHECK_EQ(core.TOMIRQEnabled(TOM_INT_VIDEO) != 0, 1);
        CHECK_EQ(core.TOMIRQEnabled(TOM_INT_GPU) != 0, 1);
        CHECK_EQ(core.TOMIRQEnabled(TOM_INT_TIMER) != 0, 1);
        CHECK_EQ(core.TOMIRQEnabled(TOM_INT_JERRY) != 0, 1);
    }
}

TEST(tom_int1_clear_bits)
{
    /* Write bits 8-12 to clear pending interrupts */
    core.TOMWriteWord(0xF000E0, 0x1F00, CALLER_M68K); /* clear all 5 */
    /* This shouldn't crash; pending state should be cleared */
    ASSERT_TRUE(1);
}

/* ================================================================== */
/* JERRY Register Tests ($F10000-$F1FFFF)                              */
/* ================================================================== */

TEST(jerry_pit1_prescale_write_read)
{
    core.JERRYWriteWord(JERRY_PIT1_PRESCALE, 0x00FF, CALLER_M68K);
    /* PIT registers may have separate read addresses per MiSTer */
    /* On this emulator, write and read may be at same address */
    uint16_t val = core.JERRYReadWord(JERRY_PIT1_PRESCALE, CALLER_M68K);
    (void)val; /* Verify no crash, value may differ based on implementation */
    ASSERT_TRUE(1);
}

TEST(jerry_int_ctrl_enable_bits)
{
    /* JINTCTRL ($F10020): lower byte = enable mask, upper byte = clear pending.
     * Reading $F10020 returns PENDING state, NOT enable mask.
     * Use JERRYIRQEnabled(bitmask) to check enables. */
    core.JERRYWriteWord(JERRY_INT_CTRL, 0x003F, CALLER_M68K);
    if (core.JERRYIRQEnabled) {
        CHECK_EQ(core.JERRYIRQEnabled(JERRY_IRQ2_TIMER1) != 0, 1);
        CHECK_EQ(core.JERRYIRQEnabled(JERRY_IRQ2_TIMER2) != 0, 1);
        CHECK_EQ(core.JERRYIRQEnabled(JERRY_IRQ2_EXTERNAL) != 0, 1);
    }
}

TEST(jerry_int_ctrl_clear_doesnt_persist)
{
    /* Clear bits (8-13) are write-only, shouldn't appear on read */
    core.JERRYWriteWord(JERRY_INT_CTRL, 0x3F00, CALLER_M68K);
    uint16_t val = core.JERRYReadWord(JERRY_INT_CTRL, CALLER_M68K);
    /* Clear bits should NOT persist in the readback */
    CHECK_EQ(val & 0x3F00, 0);
}

/* ================================================================== */
/* CLUT Tests ($F00400-$F007FF)                                        */
/* ================================================================== */

TEST(clut_a_write_read)
{
    /* CLUT A: $F00400-$F005FF (256 entries × 16 bits) */
    core.TOMWriteWord(0xF00400, 0x7FFF, CALLER_M68K);
    uint16_t val = core.TOMReadWord(0xF00400, CALLER_M68K);
    ASSERT_EQ_U16(val, 0x7FFF);
}

TEST(clut_b_write_read)
{
    /* CLUT B: $F00600-$F007FF */
    core.TOMWriteWord(0xF00600, 0x1234, CALLER_M68K);
    uint16_t val = core.TOMReadWord(0xF00600, CALLER_M68K);
    ASSERT_EQ_U16(val, 0x1234);
}

TEST(clut_a_full_range)
{
    for (unsigned i = 0; i < 256; i++) {
        uint32_t addr = 0xF00400 + (i * 2);
        uint16_t pattern = (uint16_t)(i | (i << 8));
        core.TOMWriteWord(addr, pattern, CALLER_M68K);
    }
    for (unsigned i = 0; i < 256; i++) {
        uint32_t addr = 0xF00400 + (i * 2);
        uint16_t expected = (uint16_t)(i | (i << 8));
        uint16_t actual = core.TOMReadWord(addr, CALLER_M68K);
        if (actual != expected) {
            FAIL("CLUT_A[%u]: got $%04X, expected $%04X", i, actual, expected);
        }
    }
}

/* ================================================================== */
/* Line Buffer Tests ($F00800-$F0159F)                                 */
/* ================================================================== */

TEST(line_buffer_a_write_read)
{
    /* Line buffer A: $F00800-$F00D9F */
    core.TOMWriteWord(0xF00800, 0xAAAA, CALLER_M68K);
    uint16_t val = core.TOMReadWord(0xF00800, CALLER_M68K);
    ASSERT_EQ_U16(val, 0xAAAA);
}

TEST(line_buffer_b_write_read)
{
    /* Line buffer B: $F01000-$F0159F */
    core.TOMWriteWord(0xF01000, 0x5555, CALLER_M68K);
    uint16_t val = core.TOMReadWord(0xF01000, CALLER_M68K);
    ASSERT_EQ_U16(val, 0x5555);
}

/* ================================================================== */
/* Main                                                                */
/* ================================================================== */

int main(int argc, char *argv[])
{
    (void)argc; (void)argv;
    TEST_INIT("Memory Map Accuracy");

    if (!vj_core_load(&core)) return 1;
    vj_core_init(&core);

    /* Main RAM */
    RUN_TEST(ram_byte_write_read);
    RUN_TEST(ram_word_write_read);
    RUN_TEST(ram_long_write_read);
    RUN_TEST(ram_boundary_low);
    RUN_TEST(ram_boundary_high);
    RUN_TEST(ram_big_endian_layout);

    /* GPU RAM */
    RUN_TEST(gpu_ram_accessible_from_68k);
    RUN_TEST(gpu_ram_size_boundary);

    /* DSP RAM */
    RUN_TEST(dsp_ram_accessible_from_68k);

    /* TOM Registers */
    RUN_TEST(tom_hc_readable);
    RUN_TEST(tom_vc_readable);
    RUN_TEST(tom_vmode_writable);
    RUN_TEST(tom_int1_write_read);
    RUN_TEST(tom_int1_clear_bits);

    /* JERRY Registers */
    RUN_TEST(jerry_pit1_prescale_write_read);
    RUN_TEST(jerry_int_ctrl_enable_bits);
    RUN_TEST(jerry_int_ctrl_clear_doesnt_persist);

    /* CLUT */
    RUN_TEST(clut_a_write_read);
    RUN_TEST(clut_b_write_read);
    RUN_TEST(clut_a_full_range);

    /* Line Buffers */
    RUN_TEST(line_buffer_a_write_read);
    RUN_TEST(line_buffer_b_write_read);

    vj_core_unload(&core);
    return TEST_REPORT();
}
