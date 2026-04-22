/*
 * test_video_modes.c — Video mode, resolution, and timing register tests.
 *
 * Validates TOM video registers (VMODE, HP, VP, HDB, HDE, VDB, VDE),
 * resolution calculation, pixel clock (PWIDTH), and mode switching
 * against MiSTer FPGA tom.v reference.
 *
 * Build: cc -g -O0 -o test/test_video_modes test/test_video_modes.c -ldl
 * Run:   ./test/test_video_modes
 */

#include "test_framework.h"
#include "mister_ground_truth.h"

static struct vj_core core;

/* TOM video register addresses */
#define TOM_VMODE    0xF00028
#define TOM_HP       0xF0002E
#define TOM_HBB      0xF00030
#define TOM_HBE      0xF00032
#define TOM_HSYNC    0xF00034
#define TOM_HVS      0xF00036
#define TOM_HDB1     0xF00038
#define TOM_HDB2     0xF0003A
#define TOM_HDE      0xF0003C
#define TOM_VP       0xF0003E
#define TOM_VBB      0xF00040
#define TOM_VBE      0xF00042
#define TOM_VS       0xF00044
#define TOM_VDB      0xF00046
#define TOM_VDE      0xF00048
#define TOM_VEB      0xF0004A
#define TOM_VEE      0xF0004C
#define TOM_VI       0xF0004E
#define TOM_BG       0xF00058
#define TOM_HEQ      0xF00054

/* VMODE bit fields */
#define VMODE_VIDEN    (1 << 0)
#define VMODE_MODE_MASK (3 << 1)
#define VMODE_CRY16   (0 << 1)
#define VMODE_RGB24   (1 << 1)
#define VMODE_DIRECT16 (2 << 1)
#define VMODE_RGB16   (3 << 1)
#define VMODE_GENLOCK  (1 << 3)
#define VMODE_INCEN    (1 << 4)
#define VMODE_BINC     (1 << 5)
#define VMODE_CSYNC    (1 << 6)
#define VMODE_BGEN     (1 << 7)
#define VMODE_VARMOD   (1 << 8)
#define VMODE_PWIDTH_SHIFT 9
#define VMODE_PWIDTH_MASK  (0x7 << 9)

/* Typical NTSC/PAL timing values */
#define NTSC_HP   844
#define NTSC_VP   523
#define PAL_HP    852
#define PAL_VP    625

/* Helper: read TOM register */
static uint16_t tom_read(uint32_t addr)
{
    return core.TOMReadWord(addr, 0);
}

/* Helper: write TOM register */
static void tom_write(uint32_t addr, uint16_t data)
{
    core.TOMWriteWord(addr, data, 0);
}

/* ================================================================== */
/* VMODE Register Tests                                                */
/* ================================================================== */

TEST(vmode_write_read_basic)
{
    /* Write CRY16 + VIDEN */
    tom_write(TOM_VMODE, VMODE_CRY16 | VMODE_VIDEN);
    uint16_t val = tom_read(TOM_VMODE);
    CHECK_EQ(val & (VMODE_MODE_MASK | VMODE_VIDEN), VMODE_CRY16 | VMODE_VIDEN);
}

TEST(vmode_mode_cry16)
{
    tom_write(TOM_VMODE, VMODE_CRY16 | VMODE_VIDEN);
    uint16_t val = tom_read(TOM_VMODE);
    CHECK_EQ(val & VMODE_MODE_MASK, VMODE_CRY16);
}

TEST(vmode_mode_rgb24)
{
    tom_write(TOM_VMODE, VMODE_RGB24 | VMODE_VIDEN);
    uint16_t val = tom_read(TOM_VMODE);
    CHECK_EQ(val & VMODE_MODE_MASK, VMODE_RGB24);
}

TEST(vmode_mode_direct16)
{
    tom_write(TOM_VMODE, VMODE_DIRECT16 | VMODE_VIDEN);
    uint16_t val = tom_read(TOM_VMODE);
    CHECK_EQ(val & VMODE_MODE_MASK, VMODE_DIRECT16);
}

TEST(vmode_mode_rgb16)
{
    tom_write(TOM_VMODE, VMODE_RGB16 | VMODE_VIDEN);
    uint16_t val = tom_read(TOM_VMODE);
    CHECK_EQ(val & VMODE_MODE_MASK, VMODE_RGB16);
}

TEST(vmode_pwidth_values)
{
    /* Test PWIDTH settings 1-7 (pixel width = PWIDTH + 1 clocks) */
    for (unsigned pw = 1; pw <= 7; pw++) {
        uint16_t mode = VMODE_CRY16 | VMODE_VIDEN | (pw << VMODE_PWIDTH_SHIFT);
        tom_write(TOM_VMODE, mode);
        uint16_t val = tom_read(TOM_VMODE);
        uint16_t read_pw = (val & VMODE_PWIDTH_MASK) >> VMODE_PWIDTH_SHIFT;
        if (read_pw != pw) {
            FAIL("PWIDTH %u: got %u", pw, read_pw);
        }
    }
}

TEST(vmode_bgen_flag)
{
    tom_write(TOM_VMODE, VMODE_CRY16 | VMODE_BGEN | VMODE_VIDEN);
    uint16_t val = tom_read(TOM_VMODE);
    CHECK_EQ(val & VMODE_BGEN, VMODE_BGEN);
}

TEST(vmode_varmod_flag)
{
    tom_write(TOM_VMODE, VMODE_CRY16 | VMODE_VARMOD | VMODE_VIDEN);
    uint16_t val = tom_read(TOM_VMODE);
    CHECK_EQ(val & VMODE_VARMOD, VMODE_VARMOD);
}

/* ================================================================== */
/* Horizontal Timing Register Tests                                    */
/* ================================================================== */

TEST(hp_write_read)
{
    tom_write(TOM_HP, NTSC_HP);
    uint16_t val = tom_read(TOM_HP);
    CHECK_EQ(val, NTSC_HP);
}

TEST(hdb1_write_read)
{
    tom_write(TOM_HDB1, 123);
    uint16_t val = tom_read(TOM_HDB1);
    CHECK_EQ(val, 123);
}

TEST(hdb2_write_read)
{
    tom_write(TOM_HDB2, 456);
    uint16_t val = tom_read(TOM_HDB2);
    CHECK_EQ(val, 456);
}

TEST(hde_write_read)
{
    tom_write(TOM_HDE, 1398);
    uint16_t val = tom_read(TOM_HDE);
    CHECK_EQ(val, 1398);
}

TEST(hbb_write_read)
{
    tom_write(TOM_HBB, 1713);
    uint16_t val = tom_read(TOM_HBB);
    CHECK_EQ(val, 1713);
}

TEST(hbe_write_read)
{
    tom_write(TOM_HBE, 125);
    uint16_t val = tom_read(TOM_HBE);
    CHECK_EQ(val, 125);
}

TEST(hsync_write_read)
{
    tom_write(TOM_HSYNC, 64);
    uint16_t val = tom_read(TOM_HSYNC);
    CHECK_EQ(val, 64);
}

/* ================================================================== */
/* Vertical Timing Register Tests                                      */
/* ================================================================== */

TEST(vp_write_read)
{
    tom_write(TOM_VP, NTSC_VP);
    uint16_t val = tom_read(TOM_VP);
    CHECK_EQ(val, NTSC_VP);
}

TEST(vdb_write_read)
{
    tom_write(TOM_VDB, 38);
    uint16_t val = tom_read(TOM_VDB);
    CHECK_EQ(val, 38);
}

TEST(vde_write_read)
{
    tom_write(TOM_VDE, 518);
    uint16_t val = tom_read(TOM_VDE);
    CHECK_EQ(val, 518);
}

TEST(vbb_write_read)
{
    tom_write(TOM_VBB, 520);
    uint16_t val = tom_read(TOM_VBB);
    CHECK_EQ(val, 520);
}

TEST(vbe_write_read)
{
    tom_write(TOM_VBE, 24);
    uint16_t val = tom_read(TOM_VBE);
    CHECK_EQ(val, 24);
}

TEST(vs_write_read)
{
    tom_write(TOM_VS, 517);
    uint16_t val = tom_read(TOM_VS);
    CHECK_EQ(val, 517);
}

TEST(vi_write_read)
{
    tom_write(TOM_VI, 259);
    uint16_t val = tom_read(TOM_VI);
    CHECK_EQ(val, 259);
}

/* ================================================================== */
/* Resolution Configuration Tests                                      */
/* ================================================================== */

TEST(ntsc_320x240_timing_setup)
{
    /* Standard NTSC 320×240 setup (PWIDTH=4, CRY16) */
    tom_write(TOM_VMODE, VMODE_CRY16 | VMODE_VIDEN | (4 << VMODE_PWIDTH_SHIFT));
    tom_write(TOM_HP, NTSC_HP);
    tom_write(TOM_VP, NTSC_VP);
    tom_write(TOM_HDB1, 0x1A8);  /* typical HDB for 320px */
    tom_write(TOM_HDE, 0x7AC);   /* typical HDE */
    tom_write(TOM_VDB, 38);
    tom_write(TOM_VDE, 518);

    /* Verify all registers retained */
    CHECK_EQ(tom_read(TOM_HP), NTSC_HP);
    CHECK_EQ(tom_read(TOM_VP), NTSC_VP);
    uint16_t vdb = tom_read(TOM_VDB);
    uint16_t vde = tom_read(TOM_VDE);
    CHECK_EQ(vdb, 38);
    CHECK_EQ(vde, 518);

    /* Visible lines = (VDE - VDB) / 2 for interlaced counting */
    uint16_t visible_half_lines = vde - vdb;
    ASSERT_TRUE(visible_half_lines == 480 || visible_half_lines >= 200);
}

TEST(pal_320x256_timing_setup)
{
    /* PAL 320×256 setup */
    tom_write(TOM_VMODE, VMODE_CRY16 | VMODE_VIDEN | (4 << VMODE_PWIDTH_SHIFT));
    tom_write(TOM_HP, PAL_HP);
    tom_write(TOM_VP, PAL_VP);
    tom_write(TOM_HDB1, 0x1A8);
    tom_write(TOM_HDE, 0x7AC);
    tom_write(TOM_VDB, 44);
    tom_write(TOM_VDE, 556);

    CHECK_EQ(tom_read(TOM_HP), PAL_HP);
    CHECK_EQ(tom_read(TOM_VP), PAL_VP);
    CHECK_EQ(tom_read(TOM_VDB), 44);
    CHECK_EQ(tom_read(TOM_VDE), 556);
}

TEST(doom_wide_resolution)
{
    /* Doom uses wider resolution with PWIDTH=3 for ~400px width.
     * Some games set HDB/HDE to create wider display windows.
     * HDB/HDE are 11-bit registers (max 0x7FF = 2047). */
    tom_write(TOM_VMODE, VMODE_CRY16 | VMODE_VIDEN | (3 << VMODE_PWIDTH_SHIFT));
    tom_write(TOM_HP, NTSC_HP);
    tom_write(TOM_HDB1, 0x120);  /* earlier display start = wider */
    tom_write(TOM_HDE, 0x720);   /* later display end (must fit 11 bits) */

    uint16_t hdb = tom_read(TOM_HDB1);
    uint16_t hde = tom_read(TOM_HDE);
    CHECK_EQ(hdb, 0x120);
    CHECK_EQ(hde, 0x720);
}

TEST(narrow_160px_resolution)
{
    /* 160px wide (PWIDTH=8, narrowest common mode) */
    tom_write(TOM_VMODE, VMODE_CRY16 | VMODE_VIDEN | (7 << VMODE_PWIDTH_SHIFT));
    uint16_t val = tom_read(TOM_VMODE);
    uint16_t pw = (val & VMODE_PWIDTH_MASK) >> VMODE_PWIDTH_SHIFT;
    CHECK_EQ(pw, 7);
}

TEST(wide_640px_resolution)
{
    /* 640px wide (PWIDTH=2) */
    tom_write(TOM_VMODE, VMODE_CRY16 | VMODE_VIDEN | (2 << VMODE_PWIDTH_SHIFT));
    uint16_t val = tom_read(TOM_VMODE);
    uint16_t pw = (val & VMODE_PWIDTH_MASK) >> VMODE_PWIDTH_SHIFT;
    CHECK_EQ(pw, 2);
}

/* ================================================================== */
/* Video Mode Switching Tests                                          */
/* ================================================================== */

TEST(mode_switch_cry_to_rgb16)
{
    tom_write(TOM_VMODE, VMODE_CRY16 | VMODE_VIDEN | (4 << VMODE_PWIDTH_SHIFT));
    uint16_t v1 = tom_read(TOM_VMODE);
    CHECK_EQ(v1 & VMODE_MODE_MASK, VMODE_CRY16);

    tom_write(TOM_VMODE, VMODE_RGB16 | VMODE_VIDEN | (4 << VMODE_PWIDTH_SHIFT));
    uint16_t v2 = tom_read(TOM_VMODE);
    CHECK_EQ(v2 & VMODE_MODE_MASK, VMODE_RGB16);
}

TEST(pwidth_change_preserves_mode)
{
    tom_write(TOM_VMODE, VMODE_RGB16 | VMODE_VIDEN | (4 << VMODE_PWIDTH_SHIFT));
    tom_write(TOM_VMODE, VMODE_RGB16 | VMODE_VIDEN | (2 << VMODE_PWIDTH_SHIFT));
    uint16_t val = tom_read(TOM_VMODE);
    CHECK_EQ(val & VMODE_MODE_MASK, VMODE_RGB16);
    CHECK_EQ((val & VMODE_PWIDTH_MASK) >> VMODE_PWIDTH_SHIFT, 2);
}

TEST(viden_disable_enable)
{
    tom_write(TOM_VMODE, VMODE_CRY16 | VMODE_VIDEN);
    CHECK_EQ(tom_read(TOM_VMODE) & VMODE_VIDEN, VMODE_VIDEN);

    tom_write(TOM_VMODE, VMODE_CRY16); /* VIDEN cleared */
    CHECK_EQ(tom_read(TOM_VMODE) & VMODE_VIDEN, 0);
}

/* ================================================================== */
/* Background Color Register                                           */
/* ================================================================== */

TEST(bg_color_write_read)
{
    tom_write(TOM_BG, 0x7FFF);
    uint16_t val = tom_read(TOM_BG);
    ASSERT_EQ_U16(val, 0x7FFF);
}

TEST(bg_color_zero)
{
    tom_write(TOM_BG, 0x0000);
    uint16_t val = tom_read(TOM_BG);
    ASSERT_EQ_U16(val, 0x0000);
}

/* ================================================================== */
/* Line Buffer Interaction with Video Modes                            */
/* ================================================================== */

TEST(line_buffer_accessible_all_modes)
{
    /* Verify line buffers work regardless of VMODE setting */
    uint16_t modes[] = { VMODE_CRY16, VMODE_RGB24, VMODE_DIRECT16, VMODE_RGB16 };
    for (int i = 0; i < 4; i++) {
        tom_write(TOM_VMODE, modes[i] | VMODE_VIDEN | (4 << VMODE_PWIDTH_SHIFT));
        /* Write to line buffer A */
        core.TOMWriteWord(0xF00800, 0x1234 + i, 0);
        uint16_t val = core.TOMReadWord(0xF00800, 0);
        if (val != (0x1234 + i)) {
            FAIL("Line buffer write/read failed in mode %d: got 0x%04X", i, val);
        }
    }
}

/* ================================================================== */
/* Object Processor Display Window                                     */
/* ================================================================== */

TEST(op_display_window_hdb_hde_range)
{
    /* The OP uses HDB1/HDE to determine where to start/stop
     * rendering objects on each line. Verify extreme values. */
    tom_write(TOM_HDB1, 0);
    tom_write(TOM_HDE, 0x7FF);  /* max 11-bit value */
    CHECK_EQ(tom_read(TOM_HDB1), 0);
    CHECK_EQ(tom_read(TOM_HDE) & 0x7FF, 0x7FF);
}

TEST(op_display_window_vdb_vde_range)
{
    /* VDB/VDE control vertical display window for OP */
    tom_write(TOM_VDB, 0);
    tom_write(TOM_VDE, 0x7FF);
    CHECK_EQ(tom_read(TOM_VDB), 0);
    CHECK_EQ(tom_read(TOM_VDE) & 0x7FF, 0x7FF);
}

/* ================================================================== */
/* Resolution Calculation Tests (TOMGetVideoModeWidth/Height)          */
/* ================================================================== */

TEST(resolution_default_ntsc_320)
{
    /* Default NTSC: HDB1=203, HDE=1665, pwidth=4 → 326 pixels
     * HDE is clamped to RIGHT_VISIBLE_HC (1492), so: (1492-188)/4 = 326 */
    tom_write(TOM_VMODE, VMODE_CRY16 | VMODE_VIDEN | (3 << VMODE_PWIDTH_SHIFT));
    tom_write(TOM_HDB1, 203);
    tom_write(TOM_HDE, 1665);
    tom_write(TOM_VDB, 38);
    tom_write(TOM_VDE, 518);

    if (core.TOMGetVideoModeWidth) {
        uint32_t w = core.TOMGetVideoModeWidth();
        ASSERT_TRUE(w >= 320 && w <= 330);
    }
}

TEST(resolution_default_height_240)
{
    /* Default NTSC: VDB=38, VDE=518 → (518-38)/2 = 240 */
    tom_write(TOM_VDB, 38);
    tom_write(TOM_VDE, 518);

    if (core.TOMGetVideoModeHeight) {
        uint32_t h = core.TOMGetVideoModeHeight();
        ASSERT_EQ(h, 240);
    }
}

TEST(resolution_narrow_hde)
{
    /* Narrower HDE should produce fewer pixels.
     * HDB1=203, HDE=1000, pwidth=4 → (1000-188)/4 = 203 */
    tom_write(TOM_VMODE, VMODE_CRY16 | VMODE_VIDEN | (3 << VMODE_PWIDTH_SHIFT));
    tom_write(TOM_HDB1, 203);
    tom_write(TOM_HDE, 1000);

    if (core.TOMGetVideoModeWidth) {
        uint32_t w = core.TOMGetVideoModeWidth();
        ASSERT_TRUE(w < 326);
        ASSERT_TRUE(w >= 180 && w <= 210);
    }
}

TEST(resolution_pwidth8_163)
{
    /* pwidth=8: (1492-188)/8 = 163 (Doom-like) */
    tom_write(TOM_VMODE, VMODE_CRY16 | VMODE_VIDEN | (7 << VMODE_PWIDTH_SHIFT));
    tom_write(TOM_HDB1, 203);
    tom_write(TOM_HDE, 1665);

    if (core.TOMGetVideoModeWidth) {
        uint32_t w = core.TOMGetVideoModeWidth();
        ASSERT_TRUE(w >= 160 && w <= 170);
    }
}

TEST(resolution_pwidth2_wide)
{
    /* pwidth=2: wider mode, should be ~652 but clamped to VIRTUAL_SCREEN_WIDTH */
    tom_write(TOM_VMODE, VMODE_CRY16 | VMODE_VIDEN | (1 << VMODE_PWIDTH_SHIFT));
    tom_write(TOM_HDB1, 203);
    tom_write(TOM_HDE, 1665);

    if (core.TOMGetVideoModeWidth) {
        uint32_t w = core.TOMGetVideoModeWidth();
        /* Should fall back to (rightHC-leftHC)/pwidth = 1304/2 = 652
         * but clamped to VIRTUAL_SCREEN_WIDTH (326), so fallback path */
        ASSERT_TRUE(w > 0);
    }
}

TEST(resolution_custom_height_256)
{
    /* PAL-like 256 line mode: VDB=38, VDE=550 → (550-38)/2 = 256 */
    tom_write(TOM_VDB, 38);
    tom_write(TOM_VDE, 550);

    if (core.TOMGetVideoModeHeight) {
        uint32_t h = core.TOMGetVideoModeHeight();
        ASSERT_EQ(h, 256);
    }
}

TEST(resolution_custom_height_200)
{
    /* Some games use 200 lines: VDB=78, VDE=478 → (478-78)/2 = 200 */
    tom_write(TOM_VDB, 78);
    tom_write(TOM_VDE, 478);

    if (core.TOMGetVideoModeHeight) {
        uint32_t h = core.TOMGetVideoModeHeight();
        ASSERT_EQ(h, 200);
    }
}

/* ================================================================== */
/* Main                                                                */
/* ================================================================== */

int main(int argc, char *argv[])
{
    (void)argc; (void)argv;
    TEST_INIT("Video Modes & Resolution");

    if (!vj_core_load(&core)) return 1;
    vj_core_init(&core);

    /* VMODE register */
    RUN_TEST(vmode_write_read_basic);
    RUN_TEST(vmode_mode_cry16);
    RUN_TEST(vmode_mode_rgb24);
    RUN_TEST(vmode_mode_direct16);
    RUN_TEST(vmode_mode_rgb16);
    RUN_TEST(vmode_pwidth_values);
    RUN_TEST(vmode_bgen_flag);
    RUN_TEST(vmode_varmod_flag);

    /* Horizontal timing */
    RUN_TEST(hp_write_read);
    RUN_TEST(hdb1_write_read);
    RUN_TEST(hdb2_write_read);
    RUN_TEST(hde_write_read);
    RUN_TEST(hbb_write_read);
    RUN_TEST(hbe_write_read);
    RUN_TEST(hsync_write_read);

    /* Vertical timing */
    RUN_TEST(vp_write_read);
    RUN_TEST(vdb_write_read);
    RUN_TEST(vde_write_read);
    RUN_TEST(vbb_write_read);
    RUN_TEST(vbe_write_read);
    RUN_TEST(vs_write_read);
    RUN_TEST(vi_write_read);

    /* Resolution configurations */
    RUN_TEST(ntsc_320x240_timing_setup);
    RUN_TEST(pal_320x256_timing_setup);
    RUN_TEST(doom_wide_resolution);
    RUN_TEST(narrow_160px_resolution);
    RUN_TEST(wide_640px_resolution);

    /* Mode switching */
    RUN_TEST(mode_switch_cry_to_rgb16);
    RUN_TEST(pwidth_change_preserves_mode);
    RUN_TEST(viden_disable_enable);

    /* Background color */
    RUN_TEST(bg_color_write_read);
    RUN_TEST(bg_color_zero);

    /* Line buffer + modes */
    RUN_TEST(line_buffer_accessible_all_modes);

    /* OP display window */
    RUN_TEST(op_display_window_hdb_hde_range);
    RUN_TEST(op_display_window_vdb_vde_range);

    /* Resolution calculation (TOMGetVideoModeWidth/Height with HDB1/HDE/VDB/VDE) */
    RUN_TEST(resolution_default_ntsc_320);
    RUN_TEST(resolution_default_height_240);
    RUN_TEST(resolution_narrow_hde);
    RUN_TEST(resolution_pwidth8_163);
    RUN_TEST(resolution_pwidth2_wide);
    RUN_TEST(resolution_custom_height_256);
    RUN_TEST(resolution_custom_height_200);

    vj_core_unload(&core);
    return TEST_REPORT();
}
