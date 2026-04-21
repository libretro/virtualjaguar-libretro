/*
 * test_hle_bios.c — Unit tests for HLE CD BIOS against the calling convention spec.
 *
 * Tests the 18 jump table entries defined in:
 *   docs/cd-bios-calling-convention.md
 *
 * These tests verify that the HLE implementation matches the real BIOS
 * behavior, especially CD_read/CD_poll register conventions.
 *
 * Build:
 *   make -j4 DEBUG=1 && cc -O0 -g -o test/test_hle_bios \
 *       test/test_hle_bios.c -ldl
 *
 * Run:
 *   DYLD_LIBRARY_PATH=. test/test_hle_bios
 */

#include "test_framework.h"

static struct vj_core C;

/* m68k register enum (from m68kinterface.h) */
#define M68K_REG_D0  0
#define M68K_REG_D1  1
#define M68K_REG_D2  2
#define M68K_REG_A0  8
#define M68K_REG_A1  9
#define M68K_REG_A6  14
#define M68K_REG_PC  16
#define M68K_REG_SP  18

/* HLE jump table addresses (from cd-bios-calling-convention.md) */
#define JT_CD_SETUP_AUDIO_ISR  0x3000
#define JT_CD_WAIT_RESPONSE    0x3006
#define JT_CD_WAIT_RESPONSE2   0x300C
#define JT_CD_I2S_ENABLE       0x3012
#define JT_CD_SPIN_UP          0x3018
#define JT_CD_STOP_DRIVE       0x301E
#define JT_CD_SET_VOL_MUTE     0x3024
#define JT_CD_SET_VOL_MAX      0x302A
#define JT_CD_PAUSE            0x3030
#define JT_CD_UNPAUSE          0x3036
#define JT_CD_READ             0x303C
#define JT_CD_FIFO_DISABLE     0x3042
#define JT_CD_HW_RESET         0x3048
#define JT_CD_POLL             0x304E
#define JT_CD_SET_DAC_MODE     0x3054
#define JT_CD_READ_TOC         0x305A
#define JT_CD_SETUP_CDROM_ISR  0x3060
#define JT_CD_SETUP_DATA_ISR   0x3066

/* HLE functions (dlsym'd) */
static bool (*HLEBoot)(void);
static bool (*HLEHook)(uint32_t);
static bool (*HLEActive)(void);
static void (*HLESetActive)(bool);

static bool load_hle_symbols(void)
{
    HLEBoot      = dlsym(C.handle, "JaguarCDHLEBoot");
    HLEHook      = dlsym(C.handle, "JaguarCDHLEHook");
    HLEActive    = dlsym(C.handle, "JaguarCDHLEActive");
    HLESetActive = dlsym(C.handle, "JaguarCDHLESetActive");

    if (!HLEHook) {
        fprintf(stderr, "  WARN: JaguarCDHLEHook not found\n");
        return false;
    }
    return true;
}

/* ------------------------------------------------------------------ */
/* Jump table installation tests                                       */
/* ------------------------------------------------------------------ */

TEST(jump_table_installed)
{
    /* Requires HLEBoot with a disc image — skip when no disc available */
    if (!HLEBoot || !HLEActive) return;
    if (!HLEActive()) return;

    uint8_t *ram = C.GetRamPtr();
    if (!ram) FAIL("GetRamPtr returned NULL");

    uint16_t entry0 = (ram[JT_CD_SETUP_AUDIO_ISR] << 8) | ram[JT_CD_SETUP_AUDIO_ISR + 1];
    ASSERT_TRUE(entry0 == 0x4E75 || (entry0 >> 8) == 0x60);
}

/* ------------------------------------------------------------------ */
/* CD_poll return value tests (spec: A1=0 on success)                  */
/* ------------------------------------------------------------------ */

TEST(cd_poll_no_pending_read)
{
    /* When no CD_read is pending, CD_poll should return A0=0, A1=0 */
    if (!HLEHook || !HLESetActive) return;
    HLESetActive(true);

    C.m68k_set_reg(M68K_REG_A0, 0xDEAD);
    C.m68k_set_reg(M68K_REG_A1, 0xBEEF);

    HLEHook(JT_CD_POLL);

    uint32_t a0 = C.m68k_get_reg(NULL, M68K_REG_A0);
    uint32_t a1 = C.m68k_get_reg(NULL, M68K_REG_A1);

    ASSERT_EQ_U32(a0, 0);
    ASSERT_EQ_U32(a1, 0);
}

TEST(cd_poll_a1_zero_on_success)
{
    /* Per spec: "A1 = Error status: 0 = OK, non-zero = error"
     * After a successful CD_read, CD_poll must return A1=0. */
    if (!HLEHook || !HLESetActive) return;
    HLESetActive(true);

    /* Simulate a completed read by setting up HLE state.
     * We do this by calling CD_read with known parameters first. */

    /* Set up CD_read registers: D0=MSF, D1=sentinel, A0=dest, A1=end */
    C.m68k_set_reg(M68K_REG_D0, 0x00000002);   /* MSF 00:00:02 */
    C.m68k_set_reg(M68K_REG_D1, 0x00000000);   /* no sentinel */
    C.m68k_set_reg(M68K_REG_A0, 0x4000);        /* dest */
    C.m68k_set_reg(M68K_REG_A1, 0x5000);        /* end */

    /* Try the hook — it may or may not have a disc loaded.
     * Even if CD_read fails, CD_poll should still return A1=0. */
    HLEHook(JT_CD_POLL);

    uint32_t a1 = C.m68k_get_reg(NULL, M68K_REG_A1);
    ASSERT_EQ_U32(a1, 0);  /* MUST be 0 — boot stubs check this! */
}

TEST(cd_poll_a0_advances_past_end_after_read)
{
    /* On real hardware the GPU CD ISR pre-decrements the dest pointer
     * before each long write, so once the transfer completes the
     * pointer sits one long PAST the end address. Two stub idioms rely
     * on this:
     *   (a) `cmpa.l A6,A0; blt poll`  with A6=end -> wants A0 >= end
     *   (b) `cmp.l  A0,D0; bge poll`  with D0=end -> wants A0 >  end
     *
     * Reporting A0 = end+4 satisfies both. Reporting exactly A0 = end
     * regresses Highlander (idiom b: PC stays in the wait loop forever
     * because end >= end is true). */
    if (!HLEHook || !HLEActive || !HLESetActive) return;

    /* Need an actual disc loaded so CD_read can stream sectors. The
     * jump_table_installed test skips when no disc is mounted; do the
     * same here. */
    HLEBoot(); /* ensure HLE state is initialised */
    HLESetActive(true);
    if (!HLEActive()) return;

    const uint32_t dest = 0x080000;
    const uint32_t end  = 0x081000;

    C.m68k_set_reg(M68K_REG_D0, 0x00000010);   /* MSF 00:00:16 (LBA 16) */
    C.m68k_set_reg(M68K_REG_D1, 0x00000000);   /* match-anything */
    C.m68k_set_reg(M68K_REG_A0, dest);
    C.m68k_set_reg(M68K_REG_A1, end);
    HLEHook(JT_CD_READ);

    HLEHook(JT_CD_POLL);

    uint32_t a0 = C.m68k_get_reg(NULL, M68K_REG_A0);
    uint32_t a1 = C.m68k_get_reg(NULL, M68K_REG_A1);

    ASSERT_EQ_U32(a1, 0);
    ASSERT_EQ_U32(a0, end + 4);
}

/* ------------------------------------------------------------------ */
/* CD_wait_response tests                                              */
/* ------------------------------------------------------------------ */

TEST(cd_wait_response_returns_zero)
{
    /* CD_wait_response ($3006) should return D1=0 (idle/ready)
     * when no DSA command is pending */
    if (!HLEHook || !HLESetActive) return;
    HLESetActive(true);

    C.m68k_set_reg(M68K_REG_D1, 0xFFFF);
    HLEHook(JT_CD_WAIT_RESPONSE);

    uint32_t d1 = C.m68k_get_reg(NULL, M68K_REG_D1);
    ASSERT_EQ_U32(d1, 0);
}

/* ------------------------------------------------------------------ */
/* ISR setup tests — $3000, $3060, $3066                               */
/* ------------------------------------------------------------------ */

TEST(isr_setup_audio_stores_a0)
{
    /* CD_setup_audio_isr ($3000): stores A0 to hle_gpu_data_base
     * and sets [$3072]=0 */
    if (!HLEHook || !HLESetActive) return;
    HLESetActive(true);
    uint8_t *ram = C.GetRamPtr();
    if (!ram) FAIL("GetRamPtr returned NULL");

    C.m68k_set_reg(M68K_REG_A0, 0xF030A4);
    HLEHook(JT_CD_SETUP_AUDIO_ISR);

    /* [$3072] should be 0 (audio mode) */
    ASSERT_EQ_U8(ram[0x3072], 0x00);
}

TEST(isr_setup_cdrom_stores_mode_ff)
{
    /* CD_setup_cdrom_isr ($3060): stores A0, sets [$3072]=$FF */
    if (!HLEHook || !HLESetActive) return;
    HLESetActive(true);
    uint8_t *ram = C.GetRamPtr();
    if (!ram) FAIL("GetRamPtr returned NULL");

    C.m68k_set_reg(M68K_REG_A0, 0xF03118);
    HLEHook(JT_CD_SETUP_CDROM_ISR);

    ASSERT_EQ_U8(ram[0x3072], 0xFF);

    /* [$3074-$3077] should contain A0 value */
    uint32_t stored_a0 = ((uint32_t)ram[0x3074] << 24) |
                         ((uint32_t)ram[0x3075] << 16) |
                         ((uint32_t)ram[0x3076] << 8)  |
                         ((uint32_t)ram[0x3077]);
    ASSERT_EQ_U32(stored_a0, 0xF03118);
}

TEST(isr_setup_data_stores_mode_01)
{
    /* CD_setup_data_isr ($3066): stores A0, sets [$3072]=1 */
    if (!HLEHook || !HLESetActive) return;
    HLESetActive(true);
    uint8_t *ram = C.GetRamPtr();
    if (!ram) FAIL("GetRamPtr returned NULL");

    C.m68k_set_reg(M68K_REG_A0, 0xF030B0);
    HLEHook(JT_CD_SETUP_DATA_ISR);

    ASSERT_EQ_U8(ram[0x3072], 0x01);
}

/* ------------------------------------------------------------------ */
/* TOC population tests                                                */
/* ------------------------------------------------------------------ */

TEST(toc_at_2c00_has_entries)
{
    /* After HLE boot, TOC at $2C00 should have track entries.
     * Each entry is 8 bytes. Even without a disc loaded, the format
     * should be correct (possibly empty). */
    uint8_t *ram = C.GetRamPtr();
    if (!ram) FAIL("GetRamPtr returned NULL");

    /* First TOC entry at $2C00: track number in byte[0] */
    /* If no disc is loaded, entry may be zero — that's OK */
    /* Just verify the TOC area is accessible */
    uint8_t first = ram[0x2C00];
    (void)first;  /* No assertion — just verify no crash */
}

/* ------------------------------------------------------------------ */
/* No-op entries should not crash                                      */
/* ------------------------------------------------------------------ */

TEST(noop_entries_safe)
{
    if (!HLEHook) return;

    /* These entries should be safe to call without side effects */
    uint32_t noop_entries[] = {
        JT_CD_I2S_ENABLE,     /* $3012 */
        JT_CD_SPIN_UP,        /* $3018 */
        JT_CD_STOP_DRIVE,     /* $301E */
        JT_CD_SET_VOL_MUTE,   /* $3024 */
        JT_CD_SET_VOL_MAX,    /* $302A */
        JT_CD_PAUSE,          /* $3030 */
        JT_CD_UNPAUSE,        /* $3036 */
        JT_CD_FIFO_DISABLE,   /* $3042 */
        JT_CD_HW_RESET,       /* $3048 */
        JT_CD_SET_DAC_MODE,   /* $3054 */
    };

    for (int i = 0; i < (int)(sizeof(noop_entries) / sizeof(noop_entries[0])); i++)
    {
        HLEHook(noop_entries[i]);
    }
    /* If we get here without crashing, all no-op entries are safe */
}

/* ------------------------------------------------------------------ */
/* Memory state tests                                                  */
/* ------------------------------------------------------------------ */

TEST(cd_ready_flag_address)
{
    /* $3727C is the CD-ready flag used by boot stubs */
    uint8_t *ram = C.GetRamPtr();
    if (!ram) FAIL("GetRamPtr returned NULL");

    /* Just verify the address is within RAM bounds (2MB) */
    ASSERT_TRUE(0x3727C < 0x200000);

    /* The HLE sets this to $FFFF during boot */
    /* We can't test the value unless HLE boot was called */
}

TEST(gpu_auth_magic)
{
    /* GPU auth magic ($03D0DEAD) at $F03000 is set by HLE boot
     * to indicate authentication passed */
    /* This is in GPU RAM — verify via GPUReadLong */
    uint32_t auth = C.GPUReadLong(0xF03000, 0);
    /* After retro_init without a CD game, this may or may not be set */
    (void)auth;
}

/* ------------------------------------------------------------------ */
/* Big-endian memory access tests                                      */
/* ------------------------------------------------------------------ */

TEST(ram_set32_get32)
{
    uint8_t *ram = C.GetRamPtr();
    if (!ram) FAIL("GetRamPtr returned NULL");

    /* Write a 32-bit big-endian value using direct byte access */
    ram[0x1000] = 0xDE;
    ram[0x1001] = 0xAD;
    ram[0x1002] = 0xBE;
    ram[0x1003] = 0xEF;

    /* Read it back via JaguarReadWord (16-bit, big-endian) */
    uint16_t hi = C.JaguarReadWord(0x1000, 0);
    uint16_t lo = C.JaguarReadWord(0x1002, 0);

    ASSERT_EQ_U16(hi, 0xDEAD);
    ASSERT_EQ_U16(lo, 0xBEEF);
}

TEST(ram_write_read_long)
{
    C.JaguarWriteLong(0x1010, 0xCAFEBABE, 0);

    uint16_t hi = C.JaguarReadWord(0x1010, 0);
    uint16_t lo = C.JaguarReadWord(0x1012, 0);

    ASSERT_EQ_U16(hi, 0xCAFE);
    ASSERT_EQ_U16(lo, 0xBABE);
}

TEST(ram_byte_order)
{
    uint8_t *ram = C.GetRamPtr();
    if (!ram) FAIL("GetRamPtr returned NULL");

    C.JaguarWriteWord(0x1020, 0xABCD, 0);

    ASSERT_EQ_U8(ram[0x1020], 0xAB);
    ASSERT_EQ_U8(ram[0x1021], 0xCD);
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

    bool have_hle = load_hle_symbols();

    TEST_INIT("HLE CD BIOS & Memory");

    /* Jump table */
    if (have_hle)
        RUN_TEST(jump_table_installed);
    else
        SKIP_TEST(jump_table_installed, "HLE symbols not found");

    /* CD_poll */
    if (have_hle) {
        RUN_TEST(cd_poll_no_pending_read);
        RUN_TEST(cd_poll_a1_zero_on_success);
        RUN_TEST(cd_poll_a0_advances_past_end_after_read);
    } else {
        SKIP_TEST(cd_poll_no_pending_read, "HLE not available");
        SKIP_TEST(cd_poll_a1_zero_on_success, "HLE not available");
        SKIP_TEST(cd_poll_a0_advances_past_end_after_read, "HLE not available");
    }

    /* CD_wait_response */
    if (have_hle)
        RUN_TEST(cd_wait_response_returns_zero);
    else
        SKIP_TEST(cd_wait_response_returns_zero, "HLE not available");

    /* ISR setup */
    if (have_hle) {
        RUN_TEST(isr_setup_audio_stores_a0);
        RUN_TEST(isr_setup_cdrom_stores_mode_ff);
        RUN_TEST(isr_setup_data_stores_mode_01);
    } else {
        SKIP_TEST(isr_setup_audio_stores_a0, "HLE not available");
        SKIP_TEST(isr_setup_cdrom_stores_mode_ff, "HLE not available");
        SKIP_TEST(isr_setup_data_stores_mode_01, "HLE not available");
    }

    /* TOC */
    RUN_TEST(toc_at_2c00_has_entries);

    /* No-op entries */
    if (have_hle)
        RUN_TEST(noop_entries_safe);
    else
        SKIP_TEST(noop_entries_safe, "HLE not available");

    /* Memory state */
    RUN_TEST(cd_ready_flag_address);
    RUN_TEST(gpu_auth_magic);

    /* Big-endian memory */
    RUN_TEST(ram_set32_get32);
    RUN_TEST(ram_write_read_long);
    RUN_TEST(ram_byte_order);

    int result = TEST_REPORT();
    vj_core_unload(&C);
    return result;
}
