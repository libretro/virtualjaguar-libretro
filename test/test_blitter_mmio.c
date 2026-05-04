/*
 * Unit test for blitter 64-bit register MMIO byte ordering.
 *
 * Verifies that the longword swap in BlitterWriteByte correctly maps
 * the Jaguar's F-bus layout (lower address = low longword) to the
 * internal blitter_ram layout expected by GET64 and the Gouraud
 * byte-level reads.
 *
 * Build:
 *   cc -O2 -Wall -std=c99 -I. -Isrc -Isrc/core -Isrc/tom \
 *      -D__LIBRETRO__ -DINLINE="inline" \
 *      -o test/test_blitter_mmio test/test_blitter_mmio.c \
 *      src/tom/blitter_mmio.c src/tom/blitter_compare.c
 */
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>

#include "vjag_memory.h"

/* Provide blitter_ram storage (normally in blitter.c) */
uint8_t blitter_ram[0x100];

void BlitterReset(void);
void BlitterWriteByte(uint32_t offset, uint8_t data, uint32_t who);

/* Stubs for dependencies we don't need */
struct VJSettings { int useFastBlitter; };
struct VJSettings vjs = { 0 };
int BlitterCompareIsEnabled(void) { return 0; }
void BlitterRunComparison(void) {}
void blitter_blit(uint32_t cmd) { (void)cmd; }
void BlitterMidsummer2(void) {}

static int failures = 0;

#define CHECK(cond, fmt, ...) do { \
   if (!(cond)) { \
      fprintf(stderr, "FAIL: " fmt "\n", ##__VA_ARGS__); \
      failures++; \
   } \
} while(0)

/* Register offsets (same as blitter_mmio.c) */
#define SRCDATA      ((uint32_t)0x40)
#define DSTDATA      ((uint32_t)0x48)
#define DSTZ         ((uint32_t)0x50)
#define SRCZINT      ((uint32_t)0x58)
#define SRCZFRAC     ((uint32_t)0x60)
#define PATTERNDATA  ((uint32_t)0x68)
#define PHRASEINT0   ((uint32_t)0x7C)
#define PHRASEINT1   ((uint32_t)0x80)
#define PHRASEINT2   ((uint32_t)0x84)
#define PHRASEINT3   ((uint32_t)0x88)

static void write_long(uint32_t offset, uint32_t val)
{
   BlitterWriteByte(offset + 0, (val >> 24) & 0xFF, 0);
   BlitterWriteByte(offset + 1, (val >> 16) & 0xFF, 0);
   BlitterWriteByte(offset + 2, (val >> 8)  & 0xFF, 0);
   BlitterWriteByte(offset + 3, (val)       & 0xFF, 0);
}

static void test_swap_one_register(const char *name, uint32_t reg)
{
   uint64_t got;

   BlitterReset();

   /* Simulate GPU writing two 32-bit longwords to a 64-bit register.
    * On hardware, the low F-bus address (reg+0) carries the LOW longword
    * and the high address (reg+4) carries the HIGH longword.
    * After the swap, GET64 should return (high_data << 32) | low_data. */
   write_long(reg + 0, 0x11223344);  /* low longword to low address */
   write_long(reg + 4, 0xAABBCCDD);  /* high longword to high address */

   got = GET64(blitter_ram, reg);

   CHECK(got == 0xAABBCCDD11223344ULL,
         "%s: GET64 = 0x%016llX, expected 0xAABBCCDD11223344",
         name, (unsigned long long)got);
}

static void test_phraseint_unaffected(void)
{
   BlitterReset();

   /* PHRASEINT writes bypass the swap and go directly to blitter_ram.
    * Pixel 0 intensity → PATTERNDATA byte 7
    * Pixel 3 intensity → PATTERNDATA byte 1 */
   BlitterWriteByte(PHRASEINT0 + 1, 0xAA, 0);  /* pixel 0 → PATTERNDATA+7 */
   BlitterWriteByte(PHRASEINT3 + 1, 0xBB, 0);  /* pixel 3 → PATTERNDATA+1 */

   CHECK(blitter_ram[PATTERNDATA + 7] == 0xAA,
         "PHRASEINT0 pixel 0 intensity: got 0x%02X at PATTERNDATA+7, expected 0xAA",
         blitter_ram[PATTERNDATA + 7]);
   CHECK(blitter_ram[PATTERNDATA + 1] == 0xBB,
         "PHRASEINT3 pixel 3 intensity: got 0x%02X at PATTERNDATA+1, expected 0xBB",
         blitter_ram[PATTERNDATA + 1]);
}

static void test_gouraud_byte_layout(void)
{
   /* Verify the Gouraud init reads (gd_c[]/gd_i[] in blitter.c)
    * see the correct per-pixel data after PHRASEINT writes.
    *
    * The Gouraud code reads:
    *   gd_c[0] = blitter_ram[PATTERNDATA + 6]  (pixel 0 color)
    *   gd_i[0] = blitter_ram[PATTERNDATA + 7]  (pixel 0 intensity)
    *   gd_c[3] = blitter_ram[PATTERNDATA + 0]  (pixel 3 color)
    *   gd_i[3] = blitter_ram[PATTERNDATA + 1]  (pixel 3 intensity)
    */
   BlitterReset();

   /* Write distinct values for each pixel via PHRASEINT */
   BlitterWriteByte(PHRASEINT0 + 1, 0x10, 0);  /* pixel 0 intensity → byte 7 */
   BlitterWriteByte(PHRASEINT1 + 1, 0x20, 0);  /* pixel 1 intensity → byte 5 */
   BlitterWriteByte(PHRASEINT2 + 1, 0x30, 0);  /* pixel 2 intensity → byte 3 */
   BlitterWriteByte(PHRASEINT3 + 1, 0x40, 0);  /* pixel 3 intensity → byte 1 */

   /* Verify Gouraud read positions */
   CHECK(blitter_ram[PATTERNDATA + 7] == 0x10,
         "Gouraud pixel 0 intensity at byte 7: got 0x%02X, expected 0x10",
         blitter_ram[PATTERNDATA + 7]);
   CHECK(blitter_ram[PATTERNDATA + 5] == 0x20,
         "Gouraud pixel 1 intensity at byte 5: got 0x%02X, expected 0x20",
         blitter_ram[PATTERNDATA + 5]);
   CHECK(blitter_ram[PATTERNDATA + 3] == 0x30,
         "Gouraud pixel 2 intensity at byte 3: got 0x%02X, expected 0x30",
         blitter_ram[PATTERNDATA + 3]);
   CHECK(blitter_ram[PATTERNDATA + 1] == 0x40,
         "Gouraud pixel 3 intensity at byte 1: got 0x%02X, expected 0x40",
         blitter_ram[PATTERNDATA + 1]);
}

static void test_direct_write_does_not_corrupt_phraseint(void)
{
   /* A direct write to PATTERNDATA+0..3 should go to blitter_ram+4..7
    * (via swap), NOT to bytes 0..3 which hold pixel 2-3 PHRASEINT data. */
   BlitterReset();

   /* First set pixel 3 intensity via PHRASEINT */
   BlitterWriteByte(PHRASEINT3 + 1, 0xFF, 0);  /* → PATTERNDATA byte 1 */
   CHECK(blitter_ram[PATTERNDATA + 1] == 0xFF,
         "pre-check: pixel 3 intensity at byte 1");

   /* Now do a direct 32-bit write to PATTERNDATA+0 (low address).
    * With the swap this goes to bytes 4-7, not bytes 0-3.
    * Pixel 3's intensity at byte 1 must be preserved. */
   write_long(PATTERNDATA + 0, 0xDEADBEEF);

   CHECK(blitter_ram[PATTERNDATA + 1] == 0xFF,
         "direct write to PATTERNDATA+0..3 must not clobber byte 1: got 0x%02X",
         blitter_ram[PATTERNDATA + 1]);

   /* The written data should land at bytes 4-7 */
   CHECK(blitter_ram[PATTERNDATA + 4] == 0xDE,
         "PATTERNDATA+0 write → byte 4: got 0x%02X, expected 0xDE",
         blitter_ram[PATTERNDATA + 4]);
   CHECK(blitter_ram[PATTERNDATA + 7] == 0xEF,
         "PATTERNDATA+3 write → byte 7: got 0x%02X, expected 0xEF",
         blitter_ram[PATTERNDATA + 7]);
}

static void test_asymmetric_halves(void)
{
   /* This is the Battle Sphere regression case: writing different values
    * to the two halves of a 64-bit register must produce the correct
    * GET64 result.  Without the swap, the halves get transposed. */
   uint64_t got;

   BlitterReset();

   /* Write pattern: low half = all zeros, high half = white pixels */
   write_long(PATTERNDATA + 0, 0x00000000);  /* low address = low data */
   write_long(PATTERNDATA + 4, 0xFFFFFFFF);  /* high address = high data */

   got = GET64(blitter_ram, PATTERNDATA);

   CHECK(got == 0xFFFFFFFF00000000ULL,
         "asymmetric PATTERNDATA: GET64 = 0x%016llX, expected 0xFFFFFFFF00000000",
         (unsigned long long)got);

   /* Reverse pattern */
   BlitterReset();
   write_long(PATTERNDATA + 0, 0xFFFFFFFF);
   write_long(PATTERNDATA + 4, 0x00000000);

   got = GET64(blitter_ram, PATTERNDATA);
   CHECK(got == 0x00000000FFFFFFFFULL,
         "asymmetric PATTERNDATA reversed: GET64 = 0x%016llX, expected 0x00000000FFFFFFFF",
         (unsigned long long)got);
}

int main(void)
{
   printf("test_blitter_mmio: 64-bit register byte ordering\n");

   test_swap_one_register("SRCDATA",     SRCDATA);
   test_swap_one_register("DSTDATA",     DSTDATA);
   test_swap_one_register("DSTZ",        DSTZ);
   test_swap_one_register("SRCZINT",     SRCZINT);
   test_swap_one_register("SRCZFRAC",    SRCZFRAC);
   test_swap_one_register("PATTERNDATA", PATTERNDATA);

   test_phraseint_unaffected();
   test_gouraud_byte_layout();
   test_direct_write_does_not_corrupt_phraseint();
   test_asymmetric_halves();

   if (failures == 0)
      printf("All tests passed.\n");
   else
      printf("%d test(s) FAILED.\n", failures);

   return failures ? 1 : 0;
}
