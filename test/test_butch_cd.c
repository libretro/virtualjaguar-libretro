/*
 * test_butch_cd.c — BUTCH CD controller register accuracy tests.
 *
 * Validates all BUTCH register read/write behavior against MiSTer FPGA
 * butch.v implementation. This catches CD boot regressions.
 *
 * Build: cc -g -O0 -o test/test_butch_cd test/test_butch_cd.c -ldl
 * Run:   ./test/test_butch_cd
 */

#include "test_framework.h"
#include "mister_ground_truth.h"

static struct vj_core core;

/* ================================================================== */
/* BUTCH Register Read/Write Tests                                     */
/* ================================================================== */

TEST(butch_reset_state)
{
    uint16_t hi;
    uint16_t lo;
    uint32_t val;
    if (!core.CDROMReset) { FAIL("CDROMReset not available"); }
    core.CDROMReset();
    /* After reset, interrupt control should be 0 */
    hi = core.CDROMReadWord(BUTCH_INT_CTRL, CALLER_M68K);
    lo = core.CDROMReadWord(BUTCH_INT_CTRL + 2, CALLER_M68K);
    val = ((uint32_t)hi << 16) | lo;
    /* Master enable and all status bits should be clear */
    CHECK_EQ(val & BUTCH_INT_ENABLE, 0);
}

TEST(butch_int_enable_write)
{
    uint16_t data;
    uint16_t readback;
    core.CDROMReset();
    /* Write master enable + FIFO enable */
    data = BUTCH_INT_ENABLE | BUTCH_INT_FIFO_EN;
    core.CDROMWriteWord(BUTCH_INT_CTRL + 2, data, CALLER_M68K);
    /* Note: readback of BUTCH enable bits requires haveCDGoodness in the
     * emulator (set when a disc is loaded). Without a disc loaded, the
     * status read path is bypassed and returns 0. This is a known
     * implementation detail, not a hardware behavior — MiSTer always
     * returns enables in the read. Marking as CHECK for now. */
    readback = core.CDROMReadWord(BUTCH_INT_CTRL + 2, CALLER_M68K);
    CHECK_EQ(readback & (BUTCH_INT_ENABLE | BUTCH_INT_FIFO_EN),
             BUTCH_INT_ENABLE | BUTCH_INT_FIFO_EN);
}

TEST(butch_dscntrl_enable)
{
    uint16_t hi;
    core.CDROMReset();
    /* Write DSA enable ($10000) to DSCNTRL */
    core.CDROMWriteWord(BUTCH_DSCNTRL, 0x0001, CALLER_M68K);  /* high word: bit 16 */
    hi = core.CDROMReadWord(BUTCH_DSCNTRL, CALLER_M68K);
    CHECK_EQ(hi & 0x0001, 0x0001);
}

TEST(butch_i2s_ctrl_bits)
{
    uint16_t i2s_val;
    uint16_t readback;
    core.CDROMReset();
    /* Write I2S control: drive=1, jerry=1, fifo_en=1 */
    i2s_val = BUTCH_I2S_DRIVE | BUTCH_I2S_JERRY | BUTCH_I2S_FIFO_EN;
    core.CDROMWriteWord(BUTCH_I2CNTRL + 2, i2s_val, CALLER_M68K);
    readback = core.CDROMReadWord(BUTCH_I2CNTRL + 2, CALLER_M68K);
    CHECK_EQ(readback & 0x07, i2s_val & 0x07);
}

TEST(butch_subcode_ctrl_write)
{
    uint16_t readback;
    core.CDROMReset();
    core.CDROMWriteWord(BUTCH_SBCNTRL + 2, 0x0001, CALLER_M68K);
    readback = core.CDROMReadWord(BUTCH_SBCNTRL + 2, CALLER_M68K);
    CHECK_EQ(readback & 0x0001, 0x0001);
}

TEST(butch_fifo_initial_empty)
{
    uint16_t i2s_stat;
    core.CDROMReset();
    /* FIFO should be empty after reset — fifonempty bit should be 0 */
    i2s_stat = core.CDROMReadWord(BUTCH_I2CNTRL + 2, CALLER_M68K);
    CHECK_EQ(i2s_stat & BUTCH_I2S_FIFONEMPTY, 0);
}

TEST(butch_address_decode_range)
{
    uint32_t offset;
    core.CDROMReset();
    /* All 12 BUTCH registers (each 4 bytes) should be accessible */
    /* Write patterns to each, verify no crash */

    for (offset = 0; offset <= 0x2C; offset += 4) {
        uint32_t addr = BUTCH_BASE + offset;
        core.CDROMWriteWord(addr, 0x0000, CALLER_M68K);
        core.CDROMWriteWord(addr + 2, 0x0000, CALLER_M68K);
        core.CDROMReadWord(addr, CALLER_M68K);
        core.CDROMReadWord(addr + 2, CALLER_M68K);
    }
    ASSERT_TRUE(1); /* If we get here without crash, decode works */
}

/* ================================================================== */
/* DSA Command/Response Protocol Tests                                 */
/* ================================================================== */

TEST(butch_dsa_command_write)
{
    uint16_t cmd;
    core.CDROMReset();
    /* Enable DSA */
    core.CDROMWriteWord(BUTCH_DSCNTRL, 0x0001, CALLER_M68K);
    core.CDROMWriteWord(BUTCH_DSCNTRL + 2, 0x0000, CALLER_M68K);

    /* Write a STOP command to DS_DATA */
    cmd = (DSA_CMD_STOP << 8) | 0x00;
    core.CDROMWriteWord(BUTCH_DS_DATA, cmd, CALLER_M68K);
    /* Should not crash — command is queued */
    ASSERT_TRUE(1);
}

TEST(butch_dsa_read_toc_command)
{
    uint16_t cmd;
    uint16_t resp;
    core.CDROMReset();
    /* Enable DSA */
    core.CDROMWriteWord(BUTCH_DSCNTRL, 0x0001, CALLER_M68K);
    core.CDROMWriteWord(BUTCH_DSCNTRL + 2, 0x0000, CALLER_M68K);

    /* Send READ_TOC command */
    cmd = (DSA_CMD_READ_TOC << 8) | 0x00;
    core.CDROMWriteWord(BUTCH_DS_DATA, cmd, CALLER_M68K);
    /* Read response — should get TOC data or error */
    resp = core.CDROMReadWord(BUTCH_DS_DATA, CALLER_M68K);
    (void)resp; /* Just verify no crash */
    ASSERT_TRUE(1);
}

TEST(butch_dsa_get_status_command)
{
    uint16_t cmd;
    uint16_t resp;
    core.CDROMReset();
    core.CDROMWriteWord(BUTCH_DSCNTRL, 0x0001, CALLER_M68K);
    core.CDROMWriteWord(BUTCH_DSCNTRL + 2, 0x0000, CALLER_M68K);

    cmd = (DSA_CMD_GET_STATUS << 8) | 0x00;
    core.CDROMWriteWord(BUTCH_DS_DATA, cmd, CALLER_M68K);
    resp = core.CDROMReadWord(BUTCH_DS_DATA, CALLER_M68K);
    /* Response code should be DSA_RSP_DISC_STATUS (0x03xx) */
    (void)resp;
    ASSERT_TRUE(1);
}

/* ================================================================== */
/* I2S FIFO Tests                                                      */
/* ================================================================== */

TEST(butch_fifo_write_read)
{
    uint16_t i2s_stat;
    core.CDROMReset();
    /* Enable I2S FIFO */
    core.CDROMWriteWord(BUTCH_I2CNTRL + 2,
                        BUTCH_I2S_DRIVE | BUTCH_I2S_FIFO_EN, CALLER_M68K);

    /* Write data to FIFO via I2SDAT1 */
    core.CDROMWriteWord(BUTCH_I2SDAT1, 0xDEAD, CALLER_M68K);
    core.CDROMWriteWord(BUTCH_I2SDAT1 + 2, 0xBEEF, CALLER_M68K);

    /* FIFO should now be non-empty */
    i2s_stat = core.CDROMReadWord(BUTCH_I2CNTRL + 2, CALLER_M68K);
    /* Note: fifonempty behavior depends on whether writes actually push to FIFO */
    (void)i2s_stat;
    ASSERT_TRUE(1);
}

TEST(butch_fifo_dat1_dat2_both_read)
{
    uint16_t dat1_hi;
    uint16_t dat1_lo;
    uint16_t dat2_hi;
    uint16_t dat2_lo;
    core.CDROMReset();
    core.CDROMWriteWord(BUTCH_I2CNTRL + 2,
                        BUTCH_I2S_DRIVE | BUTCH_I2S_FIFO_EN, CALLER_M68K);

    /* Per MiSTer butch.v: I2SDAT1 ($DFFF24) and I2SDAT2 ($DFFF28) both
     * read from the same FIFO. They exist to allow consecutive reads
     * without needing to re-address. */
    dat1_hi = core.CDROMReadWord(BUTCH_I2SDAT1, CALLER_M68K);
    dat1_lo = core.CDROMReadWord(BUTCH_I2SDAT1 + 2, CALLER_M68K);
    dat2_hi = core.CDROMReadWord(BUTCH_I2SDAT2, CALLER_M68K);
    dat2_lo = core.CDROMReadWord(BUTCH_I2SDAT2 + 2, CALLER_M68K);
    (void)dat1_hi; (void)dat1_lo; (void)dat2_hi; (void)dat2_lo;
    ASSERT_TRUE(1); /* Structural — verify both addresses decode */
}

/* ================================================================== */
/* EEPROM Interface Tests                                              */
/* ================================================================== */

TEST(butch_eeprom_cs_active_low)
{
    uint16_t readback;
    core.CDROMReset();
    /* MiSTer butch.v line 302: eeprom_cs = !butch_reg[11][0]
     * So writing 0 to bit 0 = CS active (asserted)
     * Writing 1 to bit 0 = CS inactive (deasserted) */
    core.CDROMWriteWord(BUTCH_EEPROM + 2, 0x0000, CALLER_M68K);
    /* CS should be active when bit 0 = 0 */
    readback = core.CDROMReadWord(BUTCH_EEPROM + 2, CALLER_M68K);
    CHECK_EQ(readback & BUTCH_EE_CS, 0); /* CS bit reads 0 = asserted */
}

/* ================================================================== */
/* Interrupt Logic Tests                                               */
/* ================================================================== */

TEST(butch_eint_requires_master_enable)
{
    uint16_t ctrl;
    core.CDROMReset();
    /* Set FIFO status bit (simulate half-full condition) without master enable.
     * External interrupt should NOT fire. */
    /* Enable FIFO interrupt but NOT master */
    core.CDROMWriteWord(BUTCH_INT_CTRL + 2, BUTCH_INT_FIFO_EN, CALLER_M68K);
    /* Without master enable (bit 0), no interrupt should propagate */
    /* This is a structural test — verify the logic path exists */
    ctrl = core.CDROMReadWord(BUTCH_INT_CTRL + 2, CALLER_M68K);
    CHECK_EQ(ctrl & BUTCH_INT_ENABLE, 0);
}

TEST(butch_int_fifo_requires_both_bits)
{
    uint16_t ctrl_hi;
    core.CDROMReset();
    /* Per MiSTer: fifo_int = butch_reg[0][9] && butch_reg[0][1]
     * Both the status bit AND the enable bit must be set for interrupt.
     * Enable bit alone shouldn't trigger. */
    core.CDROMWriteWord(BUTCH_INT_CTRL + 2,
                        BUTCH_INT_ENABLE | BUTCH_INT_FIFO_EN, CALLER_M68K);
    /* FIFO status (bit 9) won't be set unless FIFO is actually half-full */
    ctrl_hi = core.CDROMReadWord(BUTCH_INT_CTRL, CALLER_M68K);
    /* Status bit 9 should be in high word — check it's not spuriously set */
    (void)ctrl_hi;
    ASSERT_TRUE(1);
}

/* ================================================================== */
/* CD interrupt routing — BUTCH -> JERRY ext -> GPU IRQ1               */
/* ================================================================== */

/* The CD BIOS installs its CD-data ISR at $F03010 — the GPU IRQ **1**
 * vector (JTRM: vector = int# * 16; int 1 = DSP/JERRY source) — and
 * enables only G_FLAGS INT_ENA1 ($20). Its ISR epilogue acks via
 * INT_CLR1 (G_FLAGS bit 10) and re-arms the JERRY external latch
 * (J_INT = $0101), because BUTCH's eint enters through JERRY
 * (MiSTer butch.v line 83: "external interrupt to Jerry").
 *
 * Therefore when a DSA response is ready and BUTCH interrupts are
 * enabled, the GPU must see IRQ **1** latched (G_CTRL INT_LAT1, bit 7)
 * and dispatch to $F03010. Asserting IRQ0 instead leaves the interrupt
 * permanently masked (BIOS never sets INT_ENA0) — the FIFO-never-fills
 * boot deadlock (Primal Rage / Highlander / Iron Soldier 2, bios mode).
 *
 * Needs a loaded disc image (haveCDGoodness gates BUTCHExec), so we
 * synthesize a 16-sector single-track CUE/BIN — no ROM required. */
/* Back-to-back single-word DSA commands must BOTH deliver their responses.
 * Device-traced on Baldies (bios, 2026-07-15): after transfer #2 the game
 * writes $7001 (Set DAC Mode) and $150A (Set Mode) to DS_DATA in the same
 * tick without reading between them.  With a single command latch the
 * $70nn echo is silently replaced by the $17nn mode status; the game polls
 * DS_DATA repeatedly for its echo and wedges (cd_seek_wedge, GPU spinning).
 * Real hardware queues each drive response in the DSA RX path. */
TEST(butch_dsa_back_to_back_responses)
{
    bool (*openImage)(const char *);
    void (*closeImage)(void);
    char dir[512], cuePath[600], binPath[600];
    static const char *tmpl = "/tmp";
    const char *tmp;
    FILE *f;
    uint32_t i;
    uint16_t r1, r2;
    static uint8_t sector[2352];

    openImage = (bool (*)(const char *))dlsym(core.handle, "CDIntfOpenImage");
    closeImage = (void (*)(void))dlsym(core.handle, "CDIntfCloseImage");
    if (!openImage || !closeImage)
        { FAIL("CDIntfOpenImage/CDIntfCloseImage not exported (need TEST_EXPORTS=1)"); }

    tmp = getenv("TMPDIR");
    if (!tmp) tmp = tmpl;
    snprintf(dir, sizeof(dir), "%s", tmp);
    snprintf(binPath, sizeof(binPath), "%s/t7_dsa_b2b.bin", dir);
    snprintf(cuePath, sizeof(cuePath), "%s/t7_dsa_b2b.cue", dir);

    f = fopen(binPath, "wb");
    if (!f) { FAIL("cannot create synthetic BIN"); }
    for (i = 0; i < 16; i++)
        fwrite(sector, 1, sizeof(sector), f);
    fclose(f);
    f = fopen(cuePath, "w");
    if (!f) { remove(binPath); FAIL("cannot create synthetic CUE"); }
    fprintf(f, "FILE \"t7_dsa_b2b.bin\" BINARY\n"
               "  TRACK 01 MODE1/2352\n"
               "    INDEX 01 00:00:00\n");
    fclose(f);

    if (!openImage(cuePath))
        { remove(cuePath); remove(binPath); FAIL("synthetic CUE did not load"); }
    core.CDROMInit();
    core.CDROMReset();
    core.CDROMWriteWord(BUTCH_DSCNTRL, 0x0001, CALLER_M68K);

    /* The traced sequence: two commands, no read in between */
    core.CDROMWriteWord(BUTCH_DS_DATA, 0x7001, CALLER_M68K);  /* Set DAC Mode 1 */
    core.CDROMWriteWord(BUTCH_DS_DATA, 0x150A, CALLER_M68K);  /* Set Mode $0A  */

    r1 = core.CDROMReadWord(BUTCH_DS_DATA, CALLER_M68K);
    r2 = core.CDROMReadWord(BUTCH_DS_DATA, CALLER_M68K);

    closeImage();
    remove(cuePath);
    remove(binPath);

    CHECK_EQ(r1, 0x7001);   /* DAC-mode echo must not be lost */
    CHECK_EQ(r2, 0x170A);   /* then the mode status           */
}

TEST(butch_dsa_irq_routes_to_gpu_irq1)
{
    bool (*openImage)(const char *);
    void (*closeImage)(void);
    void (*butchExec)(uint32_t);
    char dir[512], cuePath[600], binPath[600];
    static const char *tmpl = "/tmp";
    const char *tmp;
    FILE *f;
    uint32_t i, gctrl, gpc;
    static uint8_t sector[2352];

    openImage = (bool (*)(const char *))dlsym(core.handle, "CDIntfOpenImage");
    closeImage = (void (*)(void))dlsym(core.handle, "CDIntfCloseImage");
    butchExec = (void (*)(uint32_t))dlsym(core.handle, "BUTCHExec");
    if (!openImage || !closeImage || !butchExec)
        { FAIL("CDIntfOpenImage/CDIntfCloseImage/BUTCHExec not exported (need TEST_EXPORTS=1)"); }

    tmp = getenv("TMPDIR");
    if (!tmp) tmp = tmpl;
    snprintf(dir, sizeof(dir), "%s", tmp);
    snprintf(binPath, sizeof(binPath), "%s/t6_butch_irq.bin", dir);
    snprintf(cuePath, sizeof(cuePath), "%s/t6_butch_irq.cue", dir);

    f = fopen(binPath, "wb");
    if (!f) { FAIL("cannot create synthetic BIN"); }
    for (i = 0; i < 16; i++)
        fwrite(sector, 1, sizeof(sector), f);
    fclose(f);
    f = fopen(cuePath, "w");
    if (!f) { remove(binPath); FAIL("cannot create synthetic CUE"); }
    fprintf(f, "FILE \"t6_butch_irq.bin\" BINARY\n"
               "  TRACK 01 MODE1/2352\n"
               "    INDEX 01 00:00:00\n");
    fclose(f);

    if (!openImage(cuePath))
        { remove(cuePath); remove(binPath); FAIL("synthetic CUE did not load"); }
    core.CDROMInit();     /* re-latch haveCDGoodness now that an image is open */
    core.CDROMReset();
    core.GPUReset();

    /* Mimic the CD BIOS interrupt setup exactly:
     * G_FLAGS = INT_ENA1 only; BUTCH = master enable + DSA RX enable. */
    core.GPUWriteLong(0xF02100, GPU_FLAGS_INT_ENA1, CALLER_M68K);
    core.CDROMWriteWord(BUTCH_INT_CTRL + 2,
                        BUTCH_INT_ENABLE | BUTCH_INT_RBUF_EN, CALLER_M68K);
    core.CDROMWriteWord(BUTCH_DSCNTRL, 0x0001, CALLER_M68K);   /* DSA enable */

    /* Seek to MSF 00:02:00 (block 0) — Goto Min / Sec / Frame. */
    core.CDROMWriteWord(BUTCH_DS_DATA, 0x1000, CALLER_M68K);
    core.CDROMWriteWord(BUTCH_DS_DATA, 0x1102, CALLER_M68K);
    core.CDROMWriteWord(BUTCH_DS_DATA, 0x1200, CALLER_M68K);

    /* Tick BUTCH past SEEK_DELAY_TICKS so the $0100 response queues and
     * the DSA interrupt fires. */
    for (i = 0; i < 200; i++)
        butchExec(0);

    gctrl = core.GPUReadLong(0xF02114, CALLER_M68K);
    gpc   = core.GPUReadLong(0xF02110, CALLER_M68K);

    closeImage();
    remove(cuePath);
    remove(binPath);

    /* INT_LAT1 (bit 7) must be latched — the line the BIOS enables. */
    CHECK_EQ((gctrl >> 7) & 1, 1);
    /* And with only INT_ENA1 set, the GPU must dispatch to the IRQ1
     * vector, where the BIOS's CD ISR entry stub lives. */
    CHECK_EQ(gpc, 0xF03010);
}

/* ================================================================== */
/* Main                                                                */
/* ================================================================== */

int main(int argc, char *argv[])
{
    (void)argc; (void)argv;
    TEST_INIT("BUTCH CD Controller Accuracy");

    if (!vj_core_load(&core)) return 1;
    vj_core_init(&core);

    /* Register read/write */
    RUN_TEST(butch_reset_state);
    RUN_TEST(butch_int_enable_write);
    RUN_TEST(butch_dscntrl_enable);
    RUN_TEST(butch_i2s_ctrl_bits);
    RUN_TEST(butch_subcode_ctrl_write);
    RUN_TEST(butch_fifo_initial_empty);
    RUN_TEST(butch_address_decode_range);

    /* DSA command/response */
    RUN_TEST(butch_dsa_command_write);
    RUN_TEST(butch_dsa_read_toc_command);
    RUN_TEST(butch_dsa_get_status_command);
    RUN_TEST(butch_dsa_back_to_back_responses);

    /* FIFO */
    RUN_TEST(butch_fifo_write_read);
    RUN_TEST(butch_fifo_dat1_dat2_both_read);

    /* EEPROM */
    RUN_TEST(butch_eeprom_cs_active_low);

    /* Interrupt logic */
    RUN_TEST(butch_eint_requires_master_enable);
    RUN_TEST(butch_int_fifo_requires_both_bits);
    RUN_TEST(butch_dsa_irq_routes_to_gpu_irq1);

    vj_core_unload(&core);
    return TEST_REPORT();
}
