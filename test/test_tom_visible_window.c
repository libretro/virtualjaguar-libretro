/*
 * Unit test for TOM visible-window helper functions.
 *
 * Tests TOMGetTopVisible(), TOMGetBottomVisible(), TOMGetLeftVisibleHC(),
 * and TOMGetRightVisibleHC() which derive the visible display window from
 * VDB/VDE/HDB1/HDE registers.
 *
 * These are static functions in tom.c, so we provide stubs for all
 * dependencies and define the constants/macros needed, then paste the
 * functions under test directly.
 *
 * Build:
 *   $(CC) -O2 -Wall -std=c99 -I. -Isrc -Isrc/core -Isrc/tom \
 *         -Ilibretro-common/include -o test/test_tom_visible_window \
 *         test/test_tom_visible_window.c
 */
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>

/* Provide the settings struct (mock) */
#include <boolean.h>

struct VJSettings
{
   bool hardwareTypeNTSC;
   bool useJaguarBIOS;
   bool useFastBlitter;
};

struct VJSettings vjs = { 0 };

/* Mock tomRam8 — 16K like real TOM */
uint8_t tomRam8[0x4000];

/* GET16/SET16 macros (from vjag_memory.h) */
#define GET16(r, a)   ((r[(a)] << 8) | r[(a)+1])
#define SET16(r, a, v) do { r[(a)] = (uint8_t)(((v) & 0xFF00) >> 8); r[(a)+1] = (uint8_t)((v) & 0xFF); } while(0)

/* Register offsets (from tom.c) */
#define VDB   0x46
#define VDE   0x48
#define VP    0x3E
#define HDB1  0x38
#define HDE   0x3C

/* Screen constants (from tom.h) */
#define VIRTUAL_SCREEN_WIDTH  326

/* Visible window defaults (from tom.c) */
#define DEFAULT_LEFT_VISIBLE_HC       (208 - 16 - (1 * 4))
#define DEFAULT_RIGHT_VISIBLE_HC      (DEFAULT_LEFT_VISIBLE_HC + (VIRTUAL_SCREEN_WIDTH * 4))
#define DEFAULT_TOP_VISIBLE_VC        31
#define DEFAULT_BOTTOM_VISIBLE_VC     511

#define DEFAULT_LEFT_VISIBLE_HC_PAL   (208 - 16 - (-3 * 4))
#define DEFAULT_RIGHT_VISIBLE_HC_PAL  (DEFAULT_LEFT_VISIBLE_HC_PAL + (VIRTUAL_SCREEN_WIDTH * 4))
#define DEFAULT_TOP_VISIBLE_VC_PAL    67
#define DEFAULT_BOTTOM_VISIBLE_VC_PAL 579

/* Mode-aware fallback selectors */
#define FALLBACK_TOP_VC     (vjs.hardwareTypeNTSC ? DEFAULT_TOP_VISIBLE_VC : DEFAULT_TOP_VISIBLE_VC_PAL)
#define FALLBACK_BOTTOM_VC  (vjs.hardwareTypeNTSC ? DEFAULT_BOTTOM_VISIBLE_VC : DEFAULT_BOTTOM_VISIBLE_VC_PAL)
#define FALLBACK_LEFT_HC    (vjs.hardwareTypeNTSC ? DEFAULT_LEFT_VISIBLE_HC : DEFAULT_LEFT_VISIBLE_HC_PAL)
#define FALLBACK_RIGHT_HC   (vjs.hardwareTypeNTSC ? DEFAULT_RIGHT_VISIBLE_HC : DEFAULT_RIGHT_VISIBLE_HC_PAL)

/* Maximum framebuffer extents (from tom.c) */
#define MAX_VISIBLE_HEIGHT  512

/* === Paste the static functions under test === */

static uint16_t TOMGetTopVisible(void)
{
   uint16_t vdb = GET16(tomRam8, VDB);
   uint16_t vp = GET16(tomRam8, VP);

   if (vdb == 0 || vdb == 38)
      return FALLBACK_TOP_VC;

   if (vp == 0 || vdb > vp)
      return FALLBACK_TOP_VC;

   return vdb;
}

static uint16_t TOMGetBottomVisible(void)
{
   uint16_t vdb = GET16(tomRam8, VDB);
   uint16_t vde = GET16(tomRam8, VDE);
   uint16_t vp = GET16(tomRam8, VP);
   uint16_t result;
   uint16_t top;
   uint16_t max_bottom;

   if (vde == 0)
      return FALLBACK_BOTTOM_VC;
   if (vde == 518 && vdb == 38)
      return FALLBACK_BOTTOM_VC;

   if (vp == 0 || vde > vp)
      return FALLBACK_BOTTOM_VC;

   result = vde;

   top = TOMGetTopVisible();
   max_bottom = top + (MAX_VISIBLE_HEIGHT * 2);
   if (result > max_bottom)
      result = max_bottom;

   if (result <= top)
      return FALLBACK_BOTTOM_VC;

   return result;
}

static uint32_t TOMGetLeftVisibleHC(void)
{
   return FALLBACK_LEFT_HC;
}

static uint32_t TOMGetRightVisibleHC(void)
{
   uint16_t hde = GET16(tomRam8, HDE);
   uint32_t left = TOMGetLeftVisibleHC();
   uint32_t max_right = left + (uint32_t)VIRTUAL_SCREEN_WIDTH * 4;

   if (hde == 0 || (uint32_t)hde <= left)
      return max_right;

   if ((uint32_t)hde < max_right)
      return (uint32_t)hde;
   return max_right;
}

/* === Test harness === */

static int failures = 0;
static int passes = 0;

#define CHECK(cond, fmt, ...) do { \
   if (!(cond)) { \
      fprintf(stderr, "FAIL: " fmt "\n", __VA_ARGS__); \
      failures++; \
   } else { \
      passes++; \
   } \
} while(0)

static void reset_regs(void)
{
   memset(tomRam8, 0, sizeof(tomRam8));
}

/* === TOMGetTopVisible tests === */

static void test_top_vdb_zero_ntsc(void)
{
   reset_regs();
   vjs.hardwareTypeNTSC = 1;
   /* VDB=0, VP doesn't matter */
   SET16(tomRam8, VDB, 0);
   SET16(tomRam8, VP, 523);
   CHECK(TOMGetTopVisible() == DEFAULT_TOP_VISIBLE_VC,
         "top(VDB=0,NTSC): got %u, expected %u",
         (unsigned)TOMGetTopVisible(), (unsigned)DEFAULT_TOP_VISIBLE_VC);
}

static void test_top_vdb_reset_default_ntsc(void)
{
   reset_regs();
   vjs.hardwareTypeNTSC = 1;
   SET16(tomRam8, VDB, 38);
   SET16(tomRam8, VP, 523);
   CHECK(TOMGetTopVisible() == DEFAULT_TOP_VISIBLE_VC,
         "top(VDB=38,NTSC): got %u, expected %u",
         (unsigned)TOMGetTopVisible(), (unsigned)DEFAULT_TOP_VISIBLE_VC);
}

static void test_top_vdb_reset_default_pal(void)
{
   reset_regs();
   vjs.hardwareTypeNTSC = 0;
   SET16(tomRam8, VDB, 38);
   SET16(tomRam8, VP, 625);
   CHECK(TOMGetTopVisible() == DEFAULT_TOP_VISIBLE_VC_PAL,
         "top(VDB=38,PAL): got %u, expected %u",
         (unsigned)TOMGetTopVisible(), (unsigned)DEFAULT_TOP_VISIBLE_VC_PAL);
}

static void test_top_vdb_programmed(void)
{
   reset_regs();
   vjs.hardwareTypeNTSC = 1;
   SET16(tomRam8, VDB, 44);
   SET16(tomRam8, VP, 523);
   CHECK(TOMGetTopVisible() == 44,
         "top(VDB=44,VP=523): got %u, expected 44",
         (unsigned)TOMGetTopVisible());
}

static void test_top_vdb_garbage_exceeds_vp(void)
{
   reset_regs();
   vjs.hardwareTypeNTSC = 1;
   SET16(tomRam8, VDB, 600);
   SET16(tomRam8, VP, 523);
   CHECK(TOMGetTopVisible() == DEFAULT_TOP_VISIBLE_VC,
         "top(VDB=600>VP=523): got %u, expected %u",
         (unsigned)TOMGetTopVisible(), (unsigned)DEFAULT_TOP_VISIBLE_VC);
}

static void test_top_vp_zero(void)
{
   reset_regs();
   vjs.hardwareTypeNTSC = 1;
   SET16(tomRam8, VDB, 44);
   SET16(tomRam8, VP, 0);
   CHECK(TOMGetTopVisible() == DEFAULT_TOP_VISIBLE_VC,
         "top(VDB=44,VP=0): got %u, expected %u",
         (unsigned)TOMGetTopVisible(), (unsigned)DEFAULT_TOP_VISIBLE_VC);
}

/* === TOMGetBottomVisible tests === */

static void test_bottom_vde_zero(void)
{
   reset_regs();
   vjs.hardwareTypeNTSC = 1;
   SET16(tomRam8, VDE, 0);
   SET16(tomRam8, VDB, 44);
   SET16(tomRam8, VP, 523);
   CHECK(TOMGetBottomVisible() == DEFAULT_BOTTOM_VISIBLE_VC,
         "bottom(VDE=0): got %u, expected %u",
         (unsigned)TOMGetBottomVisible(), (unsigned)DEFAULT_BOTTOM_VISIBLE_VC);
}

static void test_bottom_both_reset_ntsc(void)
{
   reset_regs();
   vjs.hardwareTypeNTSC = 1;
   SET16(tomRam8, VDE, 518);
   SET16(tomRam8, VDB, 38);
   SET16(tomRam8, VP, 523);
   CHECK(TOMGetBottomVisible() == DEFAULT_BOTTOM_VISIBLE_VC,
         "bottom(VDE=518,VDB=38,NTSC): got %u, expected %u",
         (unsigned)TOMGetBottomVisible(), (unsigned)DEFAULT_BOTTOM_VISIBLE_VC);
}

static void test_bottom_both_reset_pal(void)
{
   reset_regs();
   vjs.hardwareTypeNTSC = 0;
   SET16(tomRam8, VDE, 518);
   SET16(tomRam8, VDB, 38);
   SET16(tomRam8, VP, 625);
   CHECK(TOMGetBottomVisible() == DEFAULT_BOTTOM_VISIBLE_VC_PAL,
         "bottom(VDE=518,VDB=38,PAL): got %u, expected %u",
         (unsigned)TOMGetBottomVisible(), (unsigned)DEFAULT_BOTTOM_VISIBLE_VC_PAL);
}

static void test_bottom_programmed(void)
{
   reset_regs();
   vjs.hardwareTypeNTSC = 1;
   SET16(tomRam8, VDE, 500);
   SET16(tomRam8, VDB, 44);
   SET16(tomRam8, VP, 523);
   CHECK(TOMGetBottomVisible() == 500,
         "bottom(VDE=500,VDB=44,VP=523): got %u, expected 500",
         (unsigned)TOMGetBottomVisible());
}

static void test_bottom_vde_exceeds_vp(void)
{
   reset_regs();
   vjs.hardwareTypeNTSC = 1;
   SET16(tomRam8, VDE, 600);
   SET16(tomRam8, VDB, 44);
   SET16(tomRam8, VP, 523);
   CHECK(TOMGetBottomVisible() == DEFAULT_BOTTOM_VISIBLE_VC,
         "bottom(VDE=600>VP=523): got %u, expected %u",
         (unsigned)TOMGetBottomVisible(), (unsigned)DEFAULT_BOTTOM_VISIBLE_VC);
}

static void test_bottom_vde_le_top(void)
{
   /* VDE <= top should produce fallback (inverted window) */
   reset_regs();
   vjs.hardwareTypeNTSC = 1;
   SET16(tomRam8, VDB, 44);
   SET16(tomRam8, VDE, 44);  /* equal to VDB, which becomes top */
   SET16(tomRam8, VP, 523);
   CHECK(TOMGetBottomVisible() == DEFAULT_BOTTOM_VISIBLE_VC,
         "bottom(VDE<=top): got %u, expected %u",
         (unsigned)TOMGetBottomVisible(), (unsigned)DEFAULT_BOTTOM_VISIBLE_VC);
}

static void test_bottom_vp_zero(void)
{
   reset_regs();
   vjs.hardwareTypeNTSC = 1;
   SET16(tomRam8, VDE, 500);
   SET16(tomRam8, VDB, 44);
   SET16(tomRam8, VP, 0);
   CHECK(TOMGetBottomVisible() == DEFAULT_BOTTOM_VISIBLE_VC,
         "bottom(VP=0): got %u, expected %u",
         (unsigned)TOMGetBottomVisible(), (unsigned)DEFAULT_BOTTOM_VISIBLE_VC);
}

/* === TOMGetLeftVisibleHC tests === */

static void test_left_always_fallback_ntsc(void)
{
   reset_regs();
   vjs.hardwareTypeNTSC = 1;
   SET16(tomRam8, HDB1, 300);
   CHECK(TOMGetLeftVisibleHC() == (uint32_t)DEFAULT_LEFT_VISIBLE_HC,
         "left(NTSC): got %u, expected %u",
         (unsigned)TOMGetLeftVisibleHC(), (unsigned)DEFAULT_LEFT_VISIBLE_HC);
}

static void test_left_always_fallback_pal(void)
{
   reset_regs();
   vjs.hardwareTypeNTSC = 0;
   SET16(tomRam8, HDB1, 300);
   CHECK(TOMGetLeftVisibleHC() == (uint32_t)DEFAULT_LEFT_VISIBLE_HC_PAL,
         "left(PAL): got %u, expected %u",
         (unsigned)TOMGetLeftVisibleHC(), (unsigned)DEFAULT_LEFT_VISIBLE_HC_PAL);
}

/* === TOMGetRightVisibleHC tests === */

static void test_right_hde_zero(void)
{
   uint32_t left, max_right;
   reset_regs();
   vjs.hardwareTypeNTSC = 1;
   SET16(tomRam8, HDE, 0);
   left = (uint32_t)DEFAULT_LEFT_VISIBLE_HC;
   max_right = left + (uint32_t)VIRTUAL_SCREEN_WIDTH * 4;
   CHECK(TOMGetRightVisibleHC() == max_right,
         "right(HDE=0): got %u, expected %u",
         (unsigned)TOMGetRightVisibleHC(), (unsigned)max_right);
}

static void test_right_hde_valid(void)
{
   uint32_t left;
   reset_regs();
   vjs.hardwareTypeNTSC = 1;
   SET16(tomRam8, HDE, 1400);
   left = (uint32_t)DEFAULT_LEFT_VISIBLE_HC;
   /* 1400 should be > left (188) and < max_right (188 + 1304 = 1492) */
   CHECK(TOMGetRightVisibleHC() == 1400,
         "right(HDE=1400): got %u, expected 1400",
         (unsigned)TOMGetRightVisibleHC());
   (void)left;
}

static void test_right_hde_exceeds_max(void)
{
   uint32_t left, max_right;
   reset_regs();
   vjs.hardwareTypeNTSC = 1;
   SET16(tomRam8, HDE, 3000);
   left = (uint32_t)DEFAULT_LEFT_VISIBLE_HC;
   max_right = left + (uint32_t)VIRTUAL_SCREEN_WIDTH * 4;
   CHECK(TOMGetRightVisibleHC() == max_right,
         "right(HDE=3000>max): got %u, expected %u",
         (unsigned)TOMGetRightVisibleHC(), (unsigned)max_right);
}

static void test_right_hde_le_left(void)
{
   uint32_t left, max_right;
   reset_regs();
   vjs.hardwareTypeNTSC = 1;
   /* HDE=100, which is <= left (188) */
   SET16(tomRam8, HDE, 100);
   left = (uint32_t)DEFAULT_LEFT_VISIBLE_HC;
   max_right = left + (uint32_t)VIRTUAL_SCREEN_WIDTH * 4;
   CHECK(TOMGetRightVisibleHC() == max_right,
         "right(HDE<=left): got %u, expected %u",
         (unsigned)TOMGetRightVisibleHC(), (unsigned)max_right);
}

int main(void)
{
   printf("test_tom_visible_window: TOM visible-window helpers\n");

   /* TOMGetTopVisible */
   test_top_vdb_zero_ntsc();
   test_top_vdb_reset_default_ntsc();
   test_top_vdb_reset_default_pal();
   test_top_vdb_programmed();
   test_top_vdb_garbage_exceeds_vp();
   test_top_vp_zero();

   /* TOMGetBottomVisible */
   test_bottom_vde_zero();
   test_bottom_both_reset_ntsc();
   test_bottom_both_reset_pal();
   test_bottom_programmed();
   test_bottom_vde_exceeds_vp();
   test_bottom_vde_le_top();
   test_bottom_vp_zero();

   /* TOMGetLeftVisibleHC */
   test_left_always_fallback_ntsc();
   test_left_always_fallback_pal();

   /* TOMGetRightVisibleHC */
   test_right_hde_zero();
   test_right_hde_valid();
   test_right_hde_exceeds_max();
   test_right_hde_le_left();

   if (failures == 0)
      printf("All %d tests passed.\n", passes);
   else
      printf("%d test(s) FAILED (%d passed).\n", failures, passes);

   return failures ? 1 : 0;
}
