/*
 * test_irq.c — Unit tests for Jaguar interrupt handling and dispatch.
 *
 * Tests IRQ enable/latch/pending/clear for TOM, JERRY, GPU, and DSP
 * against the Technical Reference spec:
 *   docs/atari-jaguar-1999/04 - Technical Reference.md
 *
 * Build:
 *   make -j4 DEBUG=1 && cc -O0 -g -o test/test_irq test/test_irq.c -ldl
 *
 * Run:
 *   DYLD_LIBRARY_PATH=. test/test_irq
 */

#include "test_framework.h"

static struct vj_core C;

/* IRQ enums (from tom.h — not accessible via dlsym) */
enum { IRQ_VIDEO = 0, IRQ_GPU, IRQ_OPFLAG, IRQ_TIMER, IRQ_DSP };

/* GPU IRQ sources (from gpu.h) */
enum { GPUIRQ_CPU = 0, GPUIRQ_DSP, GPUIRQ_TIMER, GPUIRQ_OBJECT, GPUIRQ_BLITTER };

/* ------------------------------------------------------------------ */
/* TOM IRQ registers ($F000E0/$F000E2)                                 */
/*                                                                     */
/* $F000E0 (INT1): write 1 to enable, 0 to disable each IRQ           */
/*   bits 0-4: VIDEO, GPU, OPFLAG, TIMER, DSP                         */
/* $F000E2 (INT2): write to clear/latch pending interrupts             */
/* ------------------------------------------------------------------ */

/* TOM register addresses from Technical Reference */
#define TOM_INT1   0xF000E0  /* interrupt control (enable/disable) */
#define TOM_INT2   0xF000E2  /* interrupt clear/latch */

TEST(tom_irq_default_disabled)
{
    C.TOMReset();
    ASSERT_FALSE(C.TOMIRQEnabled(IRQ_VIDEO));
    ASSERT_FALSE(C.TOMIRQEnabled(IRQ_GPU));
    ASSERT_FALSE(C.TOMIRQEnabled(IRQ_OPFLAG));
    ASSERT_FALSE(C.TOMIRQEnabled(IRQ_TIMER));
    ASSERT_FALSE(C.TOMIRQEnabled(IRQ_DSP));
}

TEST(tom_irq_enable_video)
{
    C.TOMReset();
    C.TOMWriteWord(TOM_INT1, 0x0001, 0);  /* enable VIDEO IRQ */
    ASSERT_TRUE(C.TOMIRQEnabled(IRQ_VIDEO));
    ASSERT_FALSE(C.TOMIRQEnabled(IRQ_GPU));
}

TEST(tom_irq_enable_multiple)
{
    C.TOMReset();
    C.TOMWriteWord(TOM_INT1, 0x0015, 0);  /* enable VIDEO | OPFLAG | DSP */
    ASSERT_TRUE(C.TOMIRQEnabled(IRQ_VIDEO));
    ASSERT_FALSE(C.TOMIRQEnabled(IRQ_GPU));
    ASSERT_TRUE(C.TOMIRQEnabled(IRQ_OPFLAG));
    ASSERT_FALSE(C.TOMIRQEnabled(IRQ_TIMER));
    ASSERT_TRUE(C.TOMIRQEnabled(IRQ_DSP));
}

TEST(tom_irq_latch_set)
{
    C.TOMReset();
    C.TOMSetIRQLatch(IRQ_VIDEO, 1);
    uint16_t ctrl = C.TOMIRQControlReg();
    ASSERT_TRUE(ctrl & 0x0001);  /* VIDEO latch bit should be set */
}

TEST(tom_irq_latch_clear)
{
    C.TOMReset();
    C.TOMSetIRQLatch(IRQ_VIDEO, 1);
    C.TOMSetIRQLatch(IRQ_VIDEO, 0);
    uint16_t ctrl = C.TOMIRQControlReg();
    ASSERT_FALSE(ctrl & 0x0001);
}

TEST(tom_pending_video_int)
{
    C.TOMReset();
    C.TOMWriteWord(TOM_INT1, 0x0001, 0);  /* enable VIDEO */
    C.TOMSetPendingVideoInt();
    /* The interrupt latch should be set */
    uint16_t ctrl = C.TOMIRQControlReg();
    ASSERT_TRUE(ctrl & 0x0001);
}

TEST(tom_pending_gpu_int)
{
    C.TOMReset();
    C.TOMWriteWord(TOM_INT1, 0x0002, 0);  /* enable GPU */
    C.TOMSetPendingGPUInt();
    uint16_t ctrl = C.TOMIRQControlReg();
    ASSERT_TRUE(ctrl & 0x0002);
}

TEST(tom_pending_timer_int)
{
    C.TOMReset();
    C.TOMWriteWord(TOM_INT1, 0x0008, 0);  /* enable TIMER */
    C.TOMSetPendingTimerInt();
    uint16_t ctrl = C.TOMIRQControlReg();
    ASSERT_TRUE(ctrl & 0x0008);
}

TEST(tom_pending_jerry_int)
{
    C.TOMReset();
    C.TOMSetPendingJERRYInt();
    /* JERRY interrupt goes through TOM's DSP channel */
    uint16_t ctrl = C.TOMIRQControlReg();
    ASSERT_TRUE(ctrl & 0x0010);  /* DSP bit */
}

/* ------------------------------------------------------------------ */
/* JERRY IRQ tests                                                     */
/*                                                                     */
/* JERRY has its own interrupt enable register at $F10020:              */
/*   bit 0: EXTERNAL, bit 1: DSP, bit 2: TIMER1, bit 3: TIMER2        */
/*   bit 4: ASI (serial), bit 5: SSI (I2S)                             */
/* ------------------------------------------------------------------ */

#define JERRY_INT_CTRL  0xF10020

TEST(jerry_irq_default)
{
    C.JERRYReset();
    ASSERT_FALSE(C.JERRYIRQEnabled(0x01));  /* EXTERNAL */
    ASSERT_FALSE(C.JERRYIRQEnabled(0x04));  /* TIMER1 */
    ASSERT_FALSE(C.JERRYIRQEnabled(0x20));  /* SSI */
}

TEST(jerry_irq_enable)
{
    C.JERRYReset();
    C.JERRYWriteWord(JERRY_INT_CTRL, 0x24, 0);  /* enable TIMER1 | SSI */
    ASSERT_TRUE(C.JERRYIRQEnabled(0x04));
    ASSERT_TRUE(C.JERRYIRQEnabled(0x20));
    ASSERT_FALSE(C.JERRYIRQEnabled(0x01));
}

/* ------------------------------------------------------------------ */
/* GPU IRQ tests                                                       */
/*                                                                     */
/* GPU has 5 interrupt sources: CPU, DSP, TIMER, OBJECT, BLITTER       */
/* Enabled via G_FLAGS register ($F02100) bits 4-8 (INT_ENA0-4)        */
/* Cleared via G_FLAGS bits 9-13 (INT_CLR0-4) - write 1 to clear      */
/* ------------------------------------------------------------------ */

TEST(gpu_irq_cpu)
{
    C.GPUReset();
    /* Enable CPU interrupt (bit 4 of G_FLAGS = INT_ENA0) */
    uint32_t flags = C.GPUReadLong(GPU_FLAGS_REG, 0);
    flags |= (1 << 4);  /* INT_ENA0 */
    C.GPUWriteLong(GPU_FLAGS_REG, flags, 0);

    /* Trigger CPU interrupt */
    C.GPUSetIRQLine(GPUIRQ_CPU, 1);
    /* HandleIRQs would vector to $F03000 (ISR slot 0) */
}

TEST(gpu_irq_clear)
{
    C.GPUReset();
    /* Enable and trigger TIMER interrupt */
    C.GPUWriteLong(GPU_FLAGS_REG, (1 << 6), 0);  /* INT_ENA2 = TIMER */
    C.GPUSetIRQLine(GPUIRQ_TIMER, 1);

    /* Clear it by writing INT_CLR2 (bit 11) */
    C.GPUWriteLong(GPU_FLAGS_REG, (1 << 11), 0);
    /* After clear, the pending bit should be gone */
}

TEST(gpu_irq_mask)
{
    C.GPUReset();
    /* Per JTRM, writing 1 to IMASK has no effect — only IRQ logic can set it */
    C.GPUWriteLong(GPU_FLAGS_REG, GPU_FLAG_IMASK, 0);
    uint32_t flags = C.GPUReadLong(GPU_FLAGS_REG, 0);
    ASSERT_FALSE(flags & GPU_FLAG_IMASK);
}

/* ------------------------------------------------------------------ */
/* Memory-mapped register access tests                                 */
/* ------------------------------------------------------------------ */

TEST(tom_vmode_default)
{
    C.TOMReset();
    /* After reset, video mode register at $F00028 should be accessible */
    uint16_t vmode = C.TOMReadWord(0xF00028, 0);
    /* Just verify we can read it without crashing */
    (void)vmode;
}

TEST(jerry_timer_prescaler)
{
    C.JERRYReset();
    /* PIT1 prescaler: write at $F10000, read back at $F10036 */
    C.JERRYWriteWord(0xF10000, 0x1234, 0);
    uint16_t val = C.JERRYReadWord(0xF10036, 0);
    ASSERT_EQ_U16(val, 0x1234);
}

TEST(butch_int_ctrl_default)
{
    if (!C.CDROMReset) { return; }
    C.CDROMReset();
    /* BUTCH interrupt control at $DFFF00 — should be 0 after reset */
    uint16_t val = C.CDROMReadWord(0xDFFF00, 0);
    ASSERT_EQ_U16(val, 0);
}

TEST(butch_status_register)
{
    if (!C.CDROMReset) { return; }
    C.CDROMReset();
    /* BUTCH status at $DFFF02 */
    uint16_t status = C.CDROMReadWord(0xDFFF02, 0);
    /* Bit 12 (SBFULL) and bit 13 (DSARDY) have defined meanings */
    (void)status;
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

    TEST_INIT("IRQ Handling & Dispatch");

    /* TOM */
    RUN_TEST(tom_irq_default_disabled);
    RUN_TEST(tom_irq_enable_video);
    RUN_TEST(tom_irq_enable_multiple);
    RUN_TEST(tom_irq_latch_set);
    RUN_TEST(tom_irq_latch_clear);
    RUN_TEST(tom_pending_video_int);
    RUN_TEST(tom_pending_gpu_int);
    RUN_TEST(tom_pending_timer_int);
    RUN_TEST(tom_pending_jerry_int);

    /* JERRY */
    RUN_TEST(jerry_irq_default);
    RUN_TEST(jerry_irq_enable);

    /* GPU IRQs */
    RUN_TEST(gpu_irq_cpu);
    RUN_TEST(gpu_irq_clear);
    RUN_TEST(gpu_irq_mask);

    /* Register access */
    RUN_TEST(tom_vmode_default);
    RUN_TEST(jerry_timer_prescaler);

    if (C.CDROMReset)
    {
        RUN_TEST(butch_int_ctrl_default);
        RUN_TEST(butch_status_register);
    }
    else
    {
        SKIP_TEST(butch_int_ctrl_default, "CDROMReset not found");
        SKIP_TEST(butch_status_register, "CDROMReset not found");
    }

    int result = TEST_REPORT();
    vj_core_unload(&C);
    return result;
}
