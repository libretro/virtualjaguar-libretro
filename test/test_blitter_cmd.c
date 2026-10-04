/* test_blitter_cmd.c -- B_CMD decode + blit output regression test.
 *
 * WHY THIS TEST EXISTS
 * ====================
 * The ten field-decode assignments at the top of blitter_blit()
 *
 *     colour_index = 0;
 *     src   = cmd & 0x07;          dst   = (cmd >> 3)  & 0x07;
 *     misc  = (cmd >> 6)  & 0x03;  a1ctl = (cmd >> 8)  & 0x07;
 *     mode  = (cmd >> 11) & 0x07;  ity   = (cmd >> 14) & 0x0F;
 *     zop   = (cmd >> 18) & 0x07;  op    = (cmd >> 21) & 0x0F;
 *     ctrl  = (cmd >> 25) & 0x3F;
 *
 * were once deleted by accident and the entire 56k-assertion suite still
 * passed.  Understanding *why* it passed determines what this test has to
 * check, so it is worth stating plainly:
 *
 *   - Nine of them (src/dst/misc/a1ctl/mode/ity/zop/op/ctrl) are dead
 *     stores as far as rendering goes.  The blit's behaviour comes from
 *     the SRCEN, DSTEN, LFU_x, GOURD and PATDSEL macros, which read the `cmd`
 *     *parameter* directly rather than these decoded copies.  Their only
 *     consumer is BlitterStateSave/BlitterStateLoad.  Deleting them
 *     therefore corrupts save states and nothing else -- invisible to any
 *     amount of destination-memory checking.
 *
 *   - Only `colour_index = 0` affects pixels, and only across a sequence
 *     of Gouraud phrase-mode blits: colour_index advances (mod 4) inside
 *     the inner loop and, without the reset, leaks into the next blit and
 *     selects the wrong gd_c[]/gd_i[] entry.
 *
 * So this test guards the decode from two directions:
 *
 *   1. test_cmd_decode_reaches_savestate() -- runs one blit whose nine
 *      fields decode to a distinctive byte signature, serializes, and
 *      searches the blob for that signature.  This is what catches the
 *      nine dead stores; gutted, they read static-zero and the signature
 *      is absent.
 *
 *   2. test_gouraud_colour_index_reset() -- two Gouraud phrase-mode blits
 *      where the first leaves colour_index != 0.  This catches the
 *      colour_index line.
 *
 * The remaining vectors (pixel copy, phrase copy, LFU ops, pitch>1,
 * Gouraud) are plain output verification.  They do not trip on the decode
 * deletion -- nothing that reads destination memory can -- but the blit
 * path had zero output coverage before this file, which is the deeper
 * reason the regression survived.  They are worth having on their own
 * merits.
 *
 * BOTH BLITTERS
 * =============
 * The output vectors run twice, against blitter_blit() (the fast blitter)
 * and BlitterMidsummer2() (the accurate one, and the default: the
 * virtualjaguar_usefastblitter core option ships "disabled").  The two are
 * separate implementations and each gets its own row of golden bytes.
 *
 * They agree on every vector here except Gouraud, where the four
 * PHRASEINT seeds come out in opposite order within the phrase --
 * 0x11,0x44,0x77,0xAA under the fast blitter and 0xAA,0x77,0x44,0x11
 * under the accurate one.  That divergence is recorded, not judged; see
 * test/tools/test_blitter_compare for the tooling that explores this
 * class of difference.
 *
 * The two decode checks stay fast-only.  Midsummer2 reads blitter_ram
 * directly and never assigns colour_index or the nine decoded fields, so
 * it has no decode to guard -- the accurate vectors would NOT catch a
 * gutted field decode.
 *
 * EXPECTED BYTES ARE CHARACTERIZATION VALUES.  They were captured from a
 * known-good build via --record and assert "no change", not "matches real
 * Jaguar hardware".  If a deliberate blitter fix moves them, re-record and
 * say so in the commit.
 *
 * Registers are driven through the real MMIO path (JaguarWriteLong into
 * TOM's $F022xx window), so the TOM dispatch, the BlitterWriteByte
 * longword swap and blitter_blit() itself are all in the loop.  Writing
 * the low word of B_CMD ($F02238 + 2) dispatches the blit synchronously,
 * so no frames are run: nothing else in the machine can touch RAM and the
 * result is deterministic.
 *
 * Build: cc -O2 -Wall -std=c99 $(INCFLAGS) -o test/test_blitter_cmd \
 *          test/test_blitter_cmd.c test/harness/harness.c -ldl -lm
 * Usage: ./test/test_blitter_cmd [core.dylib] [--record]
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>

#include "harness/harness.h"
#include "settings.h"

/* Which blit implementation blitter_mmio.c dispatches B_CMD to.
 *
 * The output vectors run under both.  The two decode checks are fast-path
 * only: BlitterMidsummer2() reads blitter_ram directly and never assigns
 * colour_index or the nine decoded fields, so it has no decode to guard.
 * The accurate vectors are therefore pure output regression -- they would
 * NOT catch a gutted field decode.  Their value is that Midsummer2 is the
 * blitter shipped by default (virtualjaguar_usefastblitter defaults to
 * "disabled") and had no output coverage at all. */
enum { BLIT_FAST = 0, BLIT_ACCURATE = 1, BLIT_MODES = 2 };

static const char *blit_mode_name[BLIT_MODES] = { "fast", "accurate" };

/* ================================================================
 * Hardware addresses
 * ================================================================ */

#define BLIT_BASE   0xF02200u

/* Blitter register offsets (mirror of src/tom/blitter.c) */
#define A1_BASE          0x00
#define A1_FLAGS         0x04
#define A1_CLIP          0x08
#define A1_PIXEL         0x0C
#define A1_STEP          0x10
#define A1_FSTEP         0x14
#define A1_FPIXEL        0x18
#define A1_INC           0x1C
#define A1_FINC          0x20
#define A2_BASE          0x24
#define A2_FLAGS         0x28
#define A2_MASK          0x2C
#define A2_PIXEL         0x30
#define A2_STEP          0x34
#define B_CMD            0x38
#define PIXLINECOUNTER   0x3C
#define B_DSTZ           0x50
#define B_SRCZINT        0x58
#define B_SRCZFRAC       0x60
#define B_PATD           0x68
#define B_IINC           0x70
#define B_ZINC           0x74
#define B_PHRASEINT0     0x7C
#define B_PHRASEINT1     0x80
#define B_PHRASEINT2     0x84
#define B_PHRASEINT3     0x88

/* B_CMD bits */
#define C_SRCEN     0x00000001u
#define C_DSTEN     0x00000008u
#define C_DSTENZ    0x00000010u
#define C_DSTWRZ    0x00000020u
#define C_UPDA1F    0x00000100u
#define C_UPDA1     0x00000200u
#define C_UPDA2     0x00000400u
#define C_GOURD     0x00001000u
#define C_GOURZ     0x00002000u
#define C_PATDSEL   0x00010000u
#define C_ZMODE_GT  0x00100000u   /* ZMODE bit 2: inhibit if src Z > dst Z */
#define C_LFU_NAN   0x00200000u
#define C_LFU_NA    0x00400000u
#define C_LFU_AN    0x00800000u
#define C_LFU_A     0x01000000u

/* A1/A2_FLAGS field builders.
 *   pitch  : bits 0-1   (lookup {0,1,3,2} phrases)
 *   pixsize: bits 3-5   (log2 of bpp; 4 => 16bpp)
 *   width  : m bits 9-10, e bits 11-14; width = ((4|m) << e) >> 2
 *   xadd   : bits 16-17 (0=phrase, 1=pixel, 2=zero, 3=increment)
 */
#define FLAG_PITCH(p)    ((uint32_t)(p) & 0x03u)
#define FLAG_PIXSIZE(s)  (((uint32_t)(s) & 0x07u) << 3)
#define FLAG_WIDTH(m,e)  ((((uint32_t)(m) & 0x03u) << 9) | (((uint32_t)(e) & 0x0Fu) << 11))
#define FLAG_XADD(x)     (((uint32_t)(x) & 0x03u) << 16)
#define FLAG_ZOFFS(z)    (((uint32_t)(z) & 0x07u) << 6)

#define PIXSIZE_16BPP    4
#define PIXSIZE_32BPP    5
#define XADD_PHRASE      0
#define XADD_PIXEL       1

/* width 8 at 16bpp: ((4|0) << 3) >> 2 == 8 */
#define WIDTH8_M         0
#define WIDTH8_E         3

/* Main RAM working areas.  Well clear of anything the loaded stub ROM or
 * the boot path touches, and phrase (8-byte) aligned as the blitter
 * requires -- A1/A2_BASE ignore the bottom three address bits. */
#define SRC_ADDR    0x00100000u
#define DST_ADDR    0x00110000u
#define SCRATCH_ADDR 0x00120000u

/* Bytes of the destination compared per vector */
#define WINDOW      64

#define WHO_M68K    6

/* ================================================================
 * Core entry points
 * ================================================================ */

static harness_config cfg = HARNESS_CONFIG_DEFAULT;

static void     (*p_JaguarWriteLong)(uint32_t, uint32_t, uint32_t);
static uint8_t   *p_jaguarMainRAM;
static size_t   (*p_retro_serialize_size)(void);
static bool     (*p_retro_serialize)(void *, size_t);
static void     (*p_BlitterCompareEnable)(int);
static struct VJSettings *p_vjs;

static int failures  = 0;
static int checks    = 0;
static int recording = 0;
static int blit_mode = BLIT_FAST;

/* blitter_mmio.c re-reads vjs.useFastBlitter on every B_CMD write, and we
 * run zero frames so check_variables() never fires again -- flipping the
 * setting here is all it takes to retarget the dispatch. */
static void select_blitter(int mode)
{
   blit_mode = mode;
   p_vjs->useFastBlitter = (mode == BLIT_FAST);
}

/* ================================================================
 * Register / memory helpers
 * ================================================================ */

/* Write a blitter register through TOM's MMIO window. */
static void wreg(uint32_t offset, uint32_t value)
{
   p_JaguarWriteLong(BLIT_BASE + offset, value, WHO_M68K);
}

/* Writing B_CMD dispatches the blit synchronously (blitter_mmio.c fires
 * on the low word, offset 0x3A). */
static void fire(uint32_t cmd)
{
   wreg(B_CMD, cmd);
}

static void ram_pattern(uint32_t addr, size_t len, uint8_t seed, uint8_t step)
{
   size_t i;
   for (i = 0; i < len; i++)
      p_jaguarMainRAM[addr + i] = (uint8_t)(seed + (uint8_t)(i * step));
}

static void ram_clear(uint32_t addr, size_t len)
{
   memset(p_jaguarMainRAM + addr, 0, len);
}

/* Put the blitter register file into a known state so a vector only has
 * to set what it cares about, and so vectors can't leak into each other. */
static void blit_regs_reset(void)
{
   uint32_t off;
   for (off = 0; off < 0x9C; off += 4)
   {
      if (off == B_CMD)   /* never write B_CMD here -- it would blit */
         continue;
      wreg(off, 0);
   }
}

/* ================================================================
 * Result checking
 * ================================================================ */

/* `expected` is indexed by blitter mode: the fast and accurate blitters
 * are separate implementations and do not agree byte-for-byte (that
 * divergence is what test/tools/test_blitter_compare exists to explore),
 * so each gets its own golden row. */
static void check_window(const char *name, uint32_t addr,
                         const uint8_t expected[BLIT_MODES][WINDOW],
                         size_t len)
{
   const uint8_t *got = p_jaguarMainRAM + addr;
   size_t i;

   checks++;

   if (recording)
   {
      printf("/* %s [%s] */\n", name, blit_mode_name[blit_mode]);
      for (i = 0; i < len; i++)
      {
         printf("%s0x%02X,", (i % 8) == 0 ? "   " : " ", got[i]);
         if ((i % 8) == 7)
            printf("\n");
      }
      printf("\n");
      return;
   }

   for (i = 0; i < len; i++)
   {
      if (got[i] != expected[blit_mode][i])
      {
         fprintf(stderr,
            "FAIL: %s [%s]: destination byte %u = 0x%02X, expected 0x%02X\n",
            name, blit_mode_name[blit_mode], (unsigned)i, got[i],
            expected[blit_mode][i]);
         fprintf(stderr, "      (blit output changed; if intentional, re-record"
                         " with --record and update the golden bytes)\n");
         failures++;
         return;
      }
   }

   printf("PASS: %s [%s]\n", name, blit_mode_name[blit_mode]);
}

static void check_true(const char *name, int cond, const char *detail)
{
   checks++;
   if (recording)
      return;
   if (cond)
   {
      printf("PASS: %s\n", name);
   }
   else
   {
      fprintf(stderr, "FAIL: %s: %s\n", name, detail);
      failures++;
   }
}

/* ================================================================
 * Vector 1 -- 16bpp pixel-mode copy, A1 <- A2
 * ================================================================ */

static const uint8_t expect_pixel_copy[BLIT_MODES][WINDOW] = {
{  /* fast */
   0x11, 0x18, 0x1F, 0x26, 0x2D, 0x34, 0x3B, 0x42,
   0x49, 0x50, 0x57, 0x5E, 0x65, 0x6C, 0x73, 0x7A,
   0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
   0x08, 0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F,
   0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17,
   0x18, 0x19, 0x1A, 0x1B, 0x1C, 0x1D, 0x1E, 0x1F,
   0x20, 0x21, 0x22, 0x23, 0x24, 0x25, 0x26, 0x27,
   0x28, 0x29, 0x2A, 0x2B, 0x2C, 0x2D, 0x2E, 0x2F,
},
{  /* accurate */
   0x11, 0x18, 0x1F, 0x26, 0x2D, 0x34, 0x3B, 0x42,
   0x49, 0x50, 0x57, 0x5E, 0x65, 0x6C, 0x73, 0x7A,
   0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
   0x08, 0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F,
   0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17,
   0x18, 0x19, 0x1A, 0x1B, 0x1C, 0x1D, 0x1E, 0x1F,
   0x20, 0x21, 0x22, 0x23, 0x24, 0x25, 0x26, 0x27,
   0x28, 0x29, 0x2A, 0x2B, 0x2C, 0x2D, 0x2E, 0x2F,
},
};

static void setup_16bpp_copy(uint32_t xadd)
{
   blit_regs_reset();

   ram_pattern(SRC_ADDR, WINDOW, 0x11, 0x07);
   ram_pattern(DST_ADDR, WINDOW, 0xF0, 0x01);

   wreg(A1_BASE,  DST_ADDR);
   wreg(A1_FLAGS, FLAG_PIXSIZE(PIXSIZE_16BPP) |
                  FLAG_WIDTH(WIDTH8_M, WIDTH8_E) |
                  FLAG_XADD(xadd));
   wreg(A2_BASE,  SRC_ADDR);
   wreg(A2_FLAGS, FLAG_PIXSIZE(PIXSIZE_16BPP) |
                  FLAG_WIDTH(WIDTH8_M, WIDTH8_E) |
                  FLAG_XADD(xadd));
   wreg(PIXLINECOUNTER, (1u << 16) | 8u);   /* 1 line of 8 pixels */
}

static void test_pixel_copy(void)
{
   setup_16bpp_copy(XADD_PIXEL);
   /* LFU_AN|LFU_A == "destination becomes source" */
   fire(C_SRCEN | C_UPDA1 | C_UPDA2 | C_LFU_AN | C_LFU_A);
   check_window("pixel_copy", DST_ADDR, expect_pixel_copy, WINDOW);
}

/* ================================================================
 * Vector 2 -- phrase-mode copy (XADD=phrase on both address generators)
 * ================================================================ */

static const uint8_t expect_phrase_copy[BLIT_MODES][WINDOW] = {
{  /* fast */
   0x11, 0x18, 0x1F, 0x26, 0x2D, 0x34, 0x3B, 0x42,
   0x49, 0x50, 0x57, 0x5E, 0x65, 0x6C, 0x73, 0x7A,
   0x81, 0x88, 0x8F, 0x96, 0x9D, 0xA4, 0xAB, 0xB2,
   0xB9, 0xC0, 0xC7, 0xCE, 0xD5, 0xDC, 0xE3, 0xEA,
   0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17,
   0x18, 0x19, 0x1A, 0x1B, 0x1C, 0x1D, 0x1E, 0x1F,
   0x20, 0x21, 0x22, 0x23, 0x24, 0x25, 0x26, 0x27,
   0x28, 0x29, 0x2A, 0x2B, 0x2C, 0x2D, 0x2E, 0x2F,
},
{  /* accurate */
   0x11, 0x18, 0x1F, 0x26, 0x2D, 0x34, 0x3B, 0x42,
   0x49, 0x50, 0x57, 0x5E, 0x65, 0x6C, 0x73, 0x7A,
   0x81, 0x88, 0x8F, 0x96, 0x9D, 0xA4, 0xAB, 0xB2,
   0xB9, 0xC0, 0xC7, 0xCE, 0xD5, 0xDC, 0xE3, 0xEA,
   0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17,
   0x18, 0x19, 0x1A, 0x1B, 0x1C, 0x1D, 0x1E, 0x1F,
   0x20, 0x21, 0x22, 0x23, 0x24, 0x25, 0x26, 0x27,
   0x28, 0x29, 0x2A, 0x2B, 0x2C, 0x2D, 0x2E, 0x2F,
},
};

static void test_phrase_copy(void)
{
   setup_16bpp_copy(XADD_PHRASE);
   wreg(PIXLINECOUNTER, (1u << 16) | 16u);  /* 4 phrases at 16bpp */
   fire(C_SRCEN | C_UPDA1 | C_UPDA2 | C_LFU_AN | C_LFU_A);
   check_window("phrase_copy", DST_ADDR, expect_phrase_copy, WINDOW);
}

/* ================================================================
 * Vector 3 -- logic function unit
 *
 * Same source and destination for every case; only the LFU bits move, so
 * the destination bytes isolate the logic op.  LFU truth table indexes on
 * (source, destination): NAN=!s&!d, NA=!s&d, AN=s&!d, A=s&d.
 * ================================================================ */

static const uint8_t expect_lfu_and[BLIT_MODES][WINDOW] = {
{  /* fast */
   0x10, 0x10, 0x12, 0x22, 0x24, 0x34, 0x32, 0x42,
   0x48, 0x50, 0x52, 0x5A, 0x64, 0x6C, 0x72, 0x7A,
   0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
   0x08, 0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F,
   0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17,
   0x18, 0x19, 0x1A, 0x1B, 0x1C, 0x1D, 0x1E, 0x1F,
   0x20, 0x21, 0x22, 0x23, 0x24, 0x25, 0x26, 0x27,
   0x28, 0x29, 0x2A, 0x2B, 0x2C, 0x2D, 0x2E, 0x2F,
},
{  /* accurate */
   0x10, 0x10, 0x12, 0x22, 0x24, 0x34, 0x32, 0x42,
   0x48, 0x50, 0x52, 0x5A, 0x64, 0x6C, 0x72, 0x7A,
   0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
   0x08, 0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F,
   0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17,
   0x18, 0x19, 0x1A, 0x1B, 0x1C, 0x1D, 0x1E, 0x1F,
   0x20, 0x21, 0x22, 0x23, 0x24, 0x25, 0x26, 0x27,
   0x28, 0x29, 0x2A, 0x2B, 0x2C, 0x2D, 0x2E, 0x2F,
},
};
static const uint8_t expect_lfu_or[BLIT_MODES][WINDOW] = {
{  /* fast */
   0xF1, 0xF9, 0xFF, 0xF7, 0xFD, 0xF5, 0xFF, 0xF7,
   0xF9, 0xF9, 0xFF, 0xFF, 0xFD, 0xFD, 0xFF, 0xFF,
   0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
   0x08, 0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F,
   0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17,
   0x18, 0x19, 0x1A, 0x1B, 0x1C, 0x1D, 0x1E, 0x1F,
   0x20, 0x21, 0x22, 0x23, 0x24, 0x25, 0x26, 0x27,
   0x28, 0x29, 0x2A, 0x2B, 0x2C, 0x2D, 0x2E, 0x2F,
},
{  /* accurate */
   0xF1, 0xF9, 0xFF, 0xF7, 0xFD, 0xF5, 0xFF, 0xF7,
   0xF9, 0xF9, 0xFF, 0xFF, 0xFD, 0xFD, 0xFF, 0xFF,
   0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
   0x08, 0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F,
   0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17,
   0x18, 0x19, 0x1A, 0x1B, 0x1C, 0x1D, 0x1E, 0x1F,
   0x20, 0x21, 0x22, 0x23, 0x24, 0x25, 0x26, 0x27,
   0x28, 0x29, 0x2A, 0x2B, 0x2C, 0x2D, 0x2E, 0x2F,
},
};
static const uint8_t expect_lfu_xor[BLIT_MODES][WINDOW] = {
{  /* fast */
   0xE1, 0xE9, 0xED, 0xD5, 0xD9, 0xC1, 0xCD, 0xB5,
   0xB1, 0xA9, 0xAD, 0xA5, 0x99, 0x91, 0x8D, 0x85,
   0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
   0x08, 0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F,
   0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17,
   0x18, 0x19, 0x1A, 0x1B, 0x1C, 0x1D, 0x1E, 0x1F,
   0x20, 0x21, 0x22, 0x23, 0x24, 0x25, 0x26, 0x27,
   0x28, 0x29, 0x2A, 0x2B, 0x2C, 0x2D, 0x2E, 0x2F,
},
{  /* accurate */
   0xE1, 0xE9, 0xED, 0xD5, 0xD9, 0xC1, 0xCD, 0xB5,
   0xB1, 0xA9, 0xAD, 0xA5, 0x99, 0x91, 0x8D, 0x85,
   0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
   0x08, 0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F,
   0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17,
   0x18, 0x19, 0x1A, 0x1B, 0x1C, 0x1D, 0x1E, 0x1F,
   0x20, 0x21, 0x22, 0x23, 0x24, 0x25, 0x26, 0x27,
   0x28, 0x29, 0x2A, 0x2B, 0x2C, 0x2D, 0x2E, 0x2F,
},
};
static const uint8_t expect_lfu_clear[BLIT_MODES][WINDOW] = {
{  /* fast */
   0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
   0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
   0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
   0x08, 0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F,
   0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17,
   0x18, 0x19, 0x1A, 0x1B, 0x1C, 0x1D, 0x1E, 0x1F,
   0x20, 0x21, 0x22, 0x23, 0x24, 0x25, 0x26, 0x27,
   0x28, 0x29, 0x2A, 0x2B, 0x2C, 0x2D, 0x2E, 0x2F,
},
{  /* accurate */
   0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
   0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
   0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
   0x08, 0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F,
   0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17,
   0x18, 0x19, 0x1A, 0x1B, 0x1C, 0x1D, 0x1E, 0x1F,
   0x20, 0x21, 0x22, 0x23, 0x24, 0x25, 0x26, 0x27,
   0x28, 0x29, 0x2A, 0x2B, 0x2C, 0x2D, 0x2E, 0x2F,
},
};

static void run_lfu(const char *name, uint32_t lfu,
                    const uint8_t expected[BLIT_MODES][WINDOW])
{
   setup_16bpp_copy(XADD_PIXEL);
   fire(C_SRCEN | C_DSTEN | C_UPDA1 | C_UPDA2 | lfu);
   check_window(name, DST_ADDR, expected, WINDOW);
}

static void test_lfu_ops(void)
{
   run_lfu("lfu_and",   C_LFU_A,                          expect_lfu_and);
   run_lfu("lfu_or",    C_LFU_NA | C_LFU_AN | C_LFU_A,    expect_lfu_or);
   run_lfu("lfu_xor",   C_LFU_NA | C_LFU_AN,              expect_lfu_xor);
   run_lfu("lfu_clear", 0,                                expect_lfu_clear);
}

/* ================================================================
 * Vector 4 -- pitch > 1
 *
 * A1_FLAGS pitch field 2 maps through pitchValue[] to 3, i.e. one phrase
 * written then three skipped.  This is the addressing mode that mangled
 * the Battle Morph boot stub, so it is worth pinning down.
 * ================================================================ */

static const uint8_t expect_pitch3[BLIT_MODES][WINDOW] = {
{  /* fast */
   0x21, 0x26, 0x2B, 0x30, 0x35, 0x3A, 0x3F, 0x44,
   0x98, 0x9B, 0x9E, 0xA1, 0xA4, 0xA7, 0xAA, 0xAD,
   0xB0, 0xB3, 0xB6, 0xB9, 0xBC, 0xBF, 0xC2, 0xC5,
   0xC8, 0xCB, 0xCE, 0xD1, 0xD4, 0xD7, 0xDA, 0xDD,
   0x49, 0x4E, 0x53, 0x58, 0x5D, 0x62, 0x67, 0x6C,
   0xF8, 0xFB, 0xFE, 0x01, 0x04, 0x07, 0x0A, 0x0D,
   0x10, 0x13, 0x16, 0x19, 0x1C, 0x1F, 0x22, 0x25,
   0x28, 0x2B, 0x2E, 0x31, 0x34, 0x37, 0x3A, 0x3D,
},
{  /* accurate */
   0x21, 0x26, 0x2B, 0x30, 0x35, 0x3A, 0x3F, 0x44,
   0x98, 0x9B, 0x9E, 0xA1, 0xA4, 0xA7, 0xAA, 0xAD,
   0xB0, 0xB3, 0xB6, 0xB9, 0xBC, 0xBF, 0xC2, 0xC5,
   0xC8, 0xCB, 0xCE, 0xD1, 0xD4, 0xD7, 0xDA, 0xDD,
   0x49, 0x4E, 0x53, 0x58, 0x5D, 0x62, 0x67, 0x6C,
   0xF8, 0xFB, 0xFE, 0x01, 0x04, 0x07, 0x0A, 0x0D,
   0x10, 0x13, 0x16, 0x19, 0x1C, 0x1F, 0x22, 0x25,
   0x28, 0x2B, 0x2E, 0x31, 0x34, 0x37, 0x3A, 0x3D,
},
};

static void test_pitch(void)
{
   blit_regs_reset();

   ram_pattern(SRC_ADDR, WINDOW, 0x21, 0x05);
   ram_pattern(DST_ADDR, WINDOW, 0x80, 0x03);

   wreg(A1_BASE,  DST_ADDR);
   wreg(A1_FLAGS, FLAG_PITCH(2) |                 /* pitchValue[2] == 3 */
                  FLAG_PIXSIZE(PIXSIZE_16BPP) |
                  FLAG_WIDTH(WIDTH8_M, WIDTH8_E) |
                  FLAG_XADD(XADD_PHRASE));
   wreg(A2_BASE,  SRC_ADDR);
   wreg(A2_FLAGS, FLAG_PIXSIZE(PIXSIZE_16BPP) |
                  FLAG_WIDTH(WIDTH8_M, WIDTH8_E) |
                  FLAG_XADD(XADD_PHRASE));
   wreg(PIXLINECOUNTER, (2u << 16) | 4u);   /* 2 lines of 4 pixels */

   fire(C_SRCEN | C_UPDA1 | C_UPDA2 | C_LFU_AN | C_LFU_A);
   check_window("pitch3", DST_ADDR, expect_pitch3, WINDOW);
}

/* ================================================================
 * Vector 6 -- Z compare reads destination Z from memory on DSTENZ alone
 *
 * JTRM v8 p.73: B_CMD bits 0-5 are independent memory-cycle enables, and
 * DSTENZ (bit 4) "Enables a destination Z read as part of inner loop
 * operation" -- it does not depend on DSTEN (bit 3).  Without DSTENZ the
 * comparator sees the DSTZ register (p.68).  The fast blitter had this
 * backwards when DSTEN was clear: DSTENZ read the DSTZ register, so every
 * Z test compared against the register instead of the Z buffer (#786,
 * 42Bastian's PolyEngine drew nothing).
 *
 * Layout: A1, 16bpp pixel mode, pitch 2 phrases, ZOFFS 1 -- a phrase of
 * four pixels followed by their phrase of Z.  Source Z (SRCZINT) is
 * $2000 in every lane; destination Z alternates $1000 / $3000.  ZMODE
 * "greater" inhibits where src > dst, so pixels 0 and 2 keep their old
 * $1111 and pixels 1 and 3 take the pattern $ABCD.  The DSTZ register
 * holds $FFFF, which would let all four through -- the old bug's output.
 * Hand-derived from the JTRM, not recorded: both blitters must match.
 * ================================================================ */

static const uint8_t expect_dstenz_no_dsten[BLIT_MODES][WINDOW] = {
{  /* fast */
   0x11, 0x11, 0xAB, 0xCD, 0x11, 0x11, 0xAB, 0xCD,
   0x10, 0x00, 0x30, 0x00, 0x10, 0x00, 0x30, 0x00,
   0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x11,
   0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x11,
   0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x11,
   0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x11,
   0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x11,
   0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x11,
},
{  /* accurate */
   0x11, 0x11, 0xAB, 0xCD, 0x11, 0x11, 0xAB, 0xCD,
   0x10, 0x00, 0x30, 0x00, 0x10, 0x00, 0x30, 0x00,
   0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x11,
   0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x11,
   0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x11,
   0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x11,
   0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x11,
   0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x11,
},
};

static void test_dstenz_without_dsten(void)
{
   static const uint8_t dst_z[8] = {
      0x10, 0x00, 0x30, 0x00, 0x10, 0x00, 0x30, 0x00 };

   blit_regs_reset();

   memset(p_jaguarMainRAM + DST_ADDR, 0x11, WINDOW);
   memcpy(p_jaguarMainRAM + DST_ADDR + 8, dst_z, sizeof(dst_z));

   wreg(A1_BASE,  DST_ADDR);
   wreg(A1_FLAGS, FLAG_PITCH(1) |                 /* 2 phrases: pixels, Z */
                  FLAG_PIXSIZE(PIXSIZE_16BPP) |
                  FLAG_ZOFFS(1) |
                  FLAG_WIDTH(WIDTH8_M, WIDTH8_E) |
                  FLAG_XADD(XADD_PIXEL));
   /* The fast blitter sizes the SRCZ register read by the SOURCE
    * channel's flags even with SRCEN clear, so A2 must say 16bpp too. */
   wreg(A2_FLAGS, FLAG_PIXSIZE(PIXSIZE_16BPP) |
                  FLAG_WIDTH(WIDTH8_M, WIDTH8_E) |
                  FLAG_XADD(XADD_PIXEL));
   wreg(B_SRCZINT,     0x20002000u);
   wreg(B_SRCZINT + 4, 0x20002000u);
   wreg(B_DSTZ,        0xFFFFFFFFu);
   wreg(B_DSTZ + 4,    0xFFFFFFFFu);
   wreg(B_PATD,        0xABCDABCDu);
   wreg(B_PATD + 4,    0xABCDABCDu);
   wreg(PIXLINECOUNTER, (1u << 16) | 4u);   /* 1 line of 4 pixels */

   fire(C_PATDSEL | C_DSTENZ | C_ZMODE_GT | C_UPDA1);
   check_window("dstenz_without_dsten", DST_ADDR, expect_dstenz_no_dsten,
                WINDOW);
}

/* ================================================================
 * Vector 7 -- 32bpp phrase copy with DSTWRZ clears the Z phrases
 *
 * The Z-buffer clear 42Bastian's PolyEngine runs every frame (#786): a
 * 32bpp phrase-mode copy of a background image, DSTWRZ set and SRCENZ
 * clear, so each Z phrase -- ZOFFS phrases above its pixel phrase -- gets
 * the SRCZ registers (all ones here).  Pitch 2 phrases, ZOFFS 1: pixel
 * phrases at +0 and +16 take the source, Z phrases at +8 and +24 become
 * $FF.  The fast blitter addressed Z with the 16bpp formula and wrote 16
 * bits, so the Z phrases stayed at their old $55.
 * ================================================================ */

static const uint8_t expect_z_clear_32bpp[BLIT_MODES][WINDOW] = {
{  /* fast */
   0x21, 0x24, 0x27, 0x2A, 0x2D, 0x30, 0x33, 0x36,
   0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
   0x39, 0x3C, 0x3F, 0x42, 0x45, 0x48, 0x4B, 0x4E,
   0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
   0x55, 0x55, 0x55, 0x55, 0x55, 0x55, 0x55, 0x55,
   0x55, 0x55, 0x55, 0x55, 0x55, 0x55, 0x55, 0x55,
   0x55, 0x55, 0x55, 0x55, 0x55, 0x55, 0x55, 0x55,
   0x55, 0x55, 0x55, 0x55, 0x55, 0x55, 0x55, 0x55,
},
{  /* accurate */
   0x21, 0x24, 0x27, 0x2A, 0x2D, 0x30, 0x33, 0x36,
   0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
   0x39, 0x3C, 0x3F, 0x42, 0x45, 0x48, 0x4B, 0x4E,
   0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
   0x55, 0x55, 0x55, 0x55, 0x55, 0x55, 0x55, 0x55,
   0x55, 0x55, 0x55, 0x55, 0x55, 0x55, 0x55, 0x55,
   0x55, 0x55, 0x55, 0x55, 0x55, 0x55, 0x55, 0x55,
   0x55, 0x55, 0x55, 0x55, 0x55, 0x55, 0x55, 0x55,
},
};

static void test_z_clear_32bpp(void)
{
   blit_regs_reset();

   ram_pattern(SRC_ADDR, WINDOW, 0x21, 0x03);
   memset(p_jaguarMainRAM + DST_ADDR, 0x55, WINDOW);

   wreg(A1_BASE,  DST_ADDR);
   wreg(A1_FLAGS, FLAG_PITCH(1) |                 /* 2 phrases: pixels, Z */
                  FLAG_PIXSIZE(PIXSIZE_32BPP) |
                  FLAG_ZOFFS(1) |
                  FLAG_WIDTH(WIDTH8_M, WIDTH8_E) |
                  FLAG_XADD(XADD_PHRASE));
   wreg(A2_BASE,  SRC_ADDR);
   wreg(A2_FLAGS, FLAG_PIXSIZE(PIXSIZE_32BPP) |
                  FLAG_WIDTH(WIDTH8_M, WIDTH8_E) |
                  FLAG_XADD(XADD_PHRASE));
   wreg(B_SRCZINT,     0xFFFFFFFFu);
   wreg(B_SRCZINT + 4, 0xFFFFFFFFu);
   wreg(PIXLINECOUNTER, (1u << 16) | 4u);   /* 1 line, 4 longs = 2 phrases */

   fire(C_SRCEN | C_DSTWRZ | C_UPDA1 | C_UPDA2 | C_LFU_AN | C_LFU_A);
   check_window("z_clear_32bpp", DST_ADDR, expect_z_clear_32bpp, WINDOW);
}

/* ================================================================
 * Vector 8 -- computed Z: each pixel gets its Z, THEN ZINC is added
 *
 * Blitter netlist (jag_sim INNER.NET, DCONTROL.NET, DATA.NET): in the
 * dzwrite state zpipe[0] latches the computed Z on atick[0], the integer
 * add (Srcz1add) lands on atick[1], and the Z write data is that latched
 * value -- so the first pixel is written with the seed, matching the
 * JTRM v8 p.81-82 example.  The accurate blitter used to add first and
 * wrote every Z one ZINC ahead (#789).
 *
 * 16bpp pixel mode, pitch 2 phrases, ZOFFS 1, GOURZ + DSTWRZ, no Z
 * compare.  All four Z lanes seeded $1000.0000, ZINC $0010.0000, so
 * the Z phrase must read 1000 1010 1020 1030.  Hand-derived, not
 * recorded: both blitters must match.
 * ================================================================ */

static const uint8_t expect_gourz_seed_first[BLIT_MODES][WINDOW] = {
{  /* fast */
   0xAB, 0xCD, 0xAB, 0xCD, 0xAB, 0xCD, 0xAB, 0xCD,
   0x10, 0x00, 0x10, 0x10, 0x10, 0x20, 0x10, 0x30,
   0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x11,
   0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x11,
   0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x11,
   0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x11,
   0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x11,
   0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x11,
},
{  /* accurate */
   0xAB, 0xCD, 0xAB, 0xCD, 0xAB, 0xCD, 0xAB, 0xCD,
   0x10, 0x00, 0x10, 0x10, 0x10, 0x20, 0x10, 0x30,
   0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x11,
   0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x11,
   0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x11,
   0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x11,
   0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x11,
   0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x11,
},
};

static void test_gourz_seed_first(void)
{
   blit_regs_reset();

   memset(p_jaguarMainRAM + DST_ADDR, 0x11, WINDOW);

   wreg(A1_BASE,  DST_ADDR);
   wreg(A1_FLAGS, FLAG_PITCH(1) |                 /* 2 phrases: pixels, Z */
                  FLAG_PIXSIZE(PIXSIZE_16BPP) |
                  FLAG_ZOFFS(1) |
                  FLAG_WIDTH(WIDTH8_M, WIDTH8_E) |
                  FLAG_XADD(XADD_PIXEL));
   wreg(A2_FLAGS, FLAG_PIXSIZE(PIXSIZE_16BPP) |
                  FLAG_WIDTH(WIDTH8_M, WIDTH8_E) |
                  FLAG_XADD(XADD_PIXEL));
   wreg(B_SRCZINT,      0x10001000u);
   wreg(B_SRCZINT + 4,  0x10001000u);
   wreg(B_SRCZFRAC,     0x00000000u);
   wreg(B_SRCZFRAC + 4, 0x00000000u);
   wreg(B_ZINC,         0x00100000u);
   wreg(B_PATD,         0xABCDABCDu);
   wreg(B_PATD + 4,     0xABCDABCDu);
   wreg(PIXLINECOUNTER, (1u << 16) | 4u);   /* 1 line of 4 pixels */

   fire(C_PATDSEL | C_GOURZ | C_DSTWRZ | C_UPDA1);
   check_window("gourz_seed_first", DST_ADDR, expect_gourz_seed_first,
                WINDOW);
}

/* ================================================================
 * Vector 5 -- Gouraud shading, and the colour_index reset
 *
 * colour_index advances (mod 4) once per pixel while GOURD and A1 phrase
 * mode are both active, and is reset to 0 at the top of blitter_blit().
 * A first blit of 6 pixels leaves it at 2; without the reset the second
 * blit starts reading gd_c[2]/gd_i[2] instead of gd_c[0]/gd_i[0] and
 * writes different pixels.  This is the one field-decode assignment with
 * an effect on rendering, so it gets a dedicated vector.
 * ================================================================ */

/* The four PHRASEINT seeds (0x11/0x44/0x77/0xAA) appear in colour_index
 * order across the first phrase, then again incremented by B_IINC across
 * the second -- direct evidence that colour_index is cycling per pixel. */
static const uint8_t expect_gourd_single[BLIT_MODES][WINDOW] = {
{  /* fast */
   0x00, 0x11, 0x00, 0x44, 0x00, 0x77, 0x00, 0xAA,
   0x00, 0x12, 0x00, 0x45, 0x00, 0x78, 0x00, 0xAB,
   0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
   0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
   0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
   0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
   0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
   0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
},
{  /* accurate */
   0x00, 0xAA, 0x00, 0x77, 0x00, 0x44, 0x00, 0x11,
   0x00, 0xAB, 0x00, 0x78, 0x00, 0x45, 0x00, 0x12,
   0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
   0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
   0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
   0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
   0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
   0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
},
};
/* Identical to the fast row above: the preceding 6-pixel blit must not
 * shift the starting colour_index.  Without the reset this starts at
 * 0x77.  Fast-path only, so the accurate row is never read -- it exists
 * only to satisfy check_window()'s per-mode indexing. */
static const uint8_t expect_gourd_second[BLIT_MODES][WINDOW] = {
{  /* fast */
   0x00, 0x11, 0x00, 0x44, 0x00, 0x77, 0x00, 0xAA,
   0x00, 0x12, 0x00, 0x45, 0x00, 0x78, 0x00, 0xAB,
   0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
   0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
   0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
   0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
   0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
   0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
},
{  /* accurate */
   0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
   0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
   0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
   0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
   0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
   0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
   0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
   0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
},
};

/* Per-pixel Gouraud seeds.  The PHRASEINT registers feed the four
 * gd_c[]/gd_i[] slots that colour_index selects between. */
static void setup_gouraud(uint32_t dst_addr, uint32_t pixels)
{
   blit_regs_reset();

   ram_clear(dst_addr, WINDOW);

   wreg(A1_BASE,  dst_addr);
   wreg(A1_FLAGS, FLAG_PIXSIZE(PIXSIZE_16BPP) |
                  FLAG_WIDTH(WIDTH8_M, WIDTH8_E) |
                  FLAG_XADD(XADD_PHRASE));   /* phrase mode: advances colour_index */
   wreg(A2_BASE,  SRC_ADDR);
   wreg(A2_FLAGS, FLAG_PIXSIZE(PIXSIZE_16BPP) |
                  FLAG_WIDTH(WIDTH8_M, WIDTH8_E) |
                  FLAG_XADD(XADD_PHRASE));

   /* Four clearly distinct colour/intensity seeds, one per slot, so a
    * wrong starting colour_index produces visibly different bytes. */
   wreg(B_PHRASEINT0, 0x00110000);
   wreg(B_PHRASEINT1, 0x00440000);
   wreg(B_PHRASEINT2, 0x00770000);
   wreg(B_PHRASEINT3, 0x00AA0000);
   wreg(B_IINC, 0x00010000);

   wreg(PIXLINECOUNTER, (1u << 16) | pixels);
}

#define GOURD_CMD  (C_GOURD | C_PATDSEL | C_UPDA1 | C_UPDA2)

/* Output vector: a Gouraud blit run on its own.  Runs under both
 * blitters. */
static void test_gouraud_output(void)
{
   setup_gouraud(DST_ADDR, 8);
   fire(GOURD_CMD);
   check_window("gourd_single", DST_ADDR, expect_gourd_single, WINDOW);
}

/* Decode guard -- fast blitter only.  BlitterMidsummer2() has no
 * colour_index to reset, so this concept does not apply to it. */
static void test_gouraud_colour_index_reset(void)
{
   /* The same blit as test_gouraud_output(), but preceded by a 6-pixel
    * Gouraud blit that leaves colour_index == 2.  With the reset intact
    * the second blit must produce exactly the baseline bytes. */
   setup_gouraud(SCRATCH_ADDR, 6);
   fire(GOURD_CMD);

   setup_gouraud(DST_ADDR, 8);
   fire(GOURD_CMD);
   check_window("gourd_second", DST_ADDR, expect_gourd_second, WINDOW);

   /* The guard itself: this must match the standalone baseline.  If
    * colour_index is not reset per blit, the second run starts mid-table
    * and this fails. */
   check_true("gourd_colour_index_reset",
              memcmp(p_jaguarMainRAM + DST_ADDR,
                     expect_gourd_single[BLIT_FAST], WINDOW) == 0,
              "a preceding Gouraud blit changed this blit's output -- "
              "colour_index is not being reset at the top of blitter_blit()");
}

/* ================================================================
 * Vector 6 -- the cmd field decode reaches the save state
 *
 * This is the check that catches deletion of the nine render-inert
 * decode assignments.  BlitterStateSave() writes src, dst, misc, a1ctl,
 * mode, ity, zop, op, ctrl as nine consecutive bytes; we pick a command
 * word whose fields decode to a distinctive sequence and look for it in
 * the serialized blob.
 * ================================================================ */

#define SIG_SRC    5
#define SIG_DST    3
#define SIG_MISC   1
#define SIG_A1CTL  6
#define SIG_MODE   2
#define SIG_ITY    9
#define SIG_ZOP    5
#define SIG_OP     11
#define SIG_CTRL   0x2A

static void test_cmd_decode_reaches_savestate(void)
{
   uint32_t cmd;
   size_t   size, i;
   uint8_t *blob;
   uint8_t  sig[9];
   int      found = 0;

   cmd = ((uint32_t)SIG_SRC)          |
         ((uint32_t)SIG_DST   <<  3)  |
         ((uint32_t)SIG_MISC  <<  6)  |
         ((uint32_t)SIG_A1CTL <<  8)  |
         ((uint32_t)SIG_MODE  << 11)  |
         ((uint32_t)SIG_ITY   << 14)  |
         ((uint32_t)SIG_ZOP   << 18)  |
         ((uint32_t)SIG_OP    << 21)  |
         ((uint32_t)SIG_CTRL  << 25);

   sig[0] = SIG_SRC;  sig[1] = SIG_DST;   sig[2] = SIG_MISC;
   sig[3] = SIG_A1CTL; sig[4] = SIG_MODE; sig[5] = SIG_ITY;
   sig[6] = SIG_ZOP;  sig[7] = SIG_OP;    sig[8] = SIG_CTRL;

   /* A single pixel into scratch RAM: the decode at the top of
    * blitter_blit() runs regardless, and we do not check the pixels. */
   blit_regs_reset();
   ram_clear(SCRATCH_ADDR, WINDOW);
   wreg(A1_BASE,  SCRATCH_ADDR);
   wreg(A1_FLAGS, FLAG_PIXSIZE(PIXSIZE_16BPP) |
                  FLAG_WIDTH(WIDTH8_M, WIDTH8_E) |
                  FLAG_XADD(XADD_PIXEL));
   wreg(A2_BASE,  SRC_ADDR);
   wreg(A2_FLAGS, FLAG_PIXSIZE(PIXSIZE_16BPP) |
                  FLAG_WIDTH(WIDTH8_M, WIDTH8_E) |
                  FLAG_XADD(XADD_PIXEL));
   wreg(A1_CLIP,  0x0FFF0FFF);       /* CLIPA1 is set in the signature */
   wreg(PIXLINECOUNTER, (1u << 16) | 1u);
   fire(cmd);

   size = p_retro_serialize_size();
   if (size == 0)
   {
      check_true("cmd_decode_savestate", 0, "retro_serialize_size() == 0");
      return;
   }

   blob = malloc(size);
   if (!blob)
   {
      check_true("cmd_decode_savestate", 0, "out of memory");
      return;
   }

   if (!p_retro_serialize(blob, size))
   {
      free(blob);
      check_true("cmd_decode_savestate", 0, "retro_serialize() failed");
      return;
   }

   for (i = 0; i + sizeof(sig) <= size; i++)
   {
      if (memcmp(blob + i, sig, sizeof(sig)) == 0)
      {
         found = 1;
         break;
      }
   }
   free(blob);

   check_true("cmd_decode_savestate", found,
      "the decoded B_CMD fields (src/dst/misc/a1ctl/mode/ity/zop/op/ctrl) "
      "are not in the save state -- the field-decode assignments at the top "
      "of blitter_blit() are missing, so save states will restore a stale "
      "blitter command");
}

/* ================================================================
 * Synthetic ROM
 *
 * The core needs a game loaded before its RAM exists, but this test runs
 * zero frames, so the ROM only has to be loadable: an entry vector and a
 * branch-to-self.
 * ================================================================ */

#define ROM_SIZE   131072u
#define ROM_ENTRY  0x2000u

static char rom_path[512];

static bool write_stub_rom(void)
{
   static uint8_t rom[ROM_SIZE];
   const char *tmp = getenv("TMPDIR");
   FILE *f;

   memset(rom, 0, sizeof(rom));
   /* Cartridge entry point at $802000 */
   rom[0x404] = 0x00; rom[0x405] = 0x80;
   rom[0x406] = 0x20; rom[0x407] = 0x00;
   /* BRA.S * -- park the 68K if anything ever does run it */
   rom[ROM_ENTRY] = 0x60; rom[ROM_ENTRY + 1] = 0xFE;

   snprintf(rom_path, sizeof(rom_path), "%s%sblitter_cmd_stub.j64",
            (tmp && *tmp) ? tmp : "/tmp",
            (tmp && *tmp && tmp[strlen(tmp) - 1] == '/') ? "" : "/");

   f = fopen(rom_path, "wb");
   if (!f)
   {
      fprintf(stderr, "cannot create stub ROM at '%s'\n", rom_path);
      return false;
   }
   if (fwrite(rom, 1, sizeof(rom), f) != sizeof(rom))
   {
      fprintf(stderr, "short write on stub ROM '%s'\n", rom_path);
      fclose(f);
      return false;
   }
   fclose(f);
   return true;
}

/* ================================================================ */

int main(int argc, char **argv)
{
   uint8_t **ram_slot;
   int i;

   for (i = 1; i < argc; i++)
   {
      if (strcmp(argv[i], "--record") == 0)
         recording = 1;
   }

   printf("test_blitter_cmd: B_CMD decode + blit output\n");

   if (!write_stub_rom())
      return 1;

   cfg.frames = 0;
   cfg.quiet  = 1;
   if (!harness_init_from_args(&cfg, argc, argv))
      return 1;

   /* blitter_blit() is the fast blitter; the default option routes to
    * BlitterMidsummer2() instead. */
   harness_set_option(&cfg, "virtualjaguar_usefastblitter", "enabled");
   harness_set_option(&cfg, "virtualjaguar_crash_detect", "disabled");

   cfg.rom_path = rom_path;
   if (!harness_load_rom(&cfg))
      return 1;

   p_JaguarWriteLong      = harness_dlsym(&cfg, "JaguarWriteLong");
   p_retro_serialize_size = harness_dlsym(&cfg, "retro_serialize_size");
   p_retro_serialize      = harness_dlsym(&cfg, "retro_serialize");
   p_BlitterCompareEnable = harness_dlsym(&cfg, "BlitterCompareEnable");
   p_vjs                  = harness_dlsym(&cfg, "vjs");

   /* jaguarMainRAM is `uint8_t *`, so the symbol is the pointer variable
    * itself -- dereference to reach the 2 MB main RAM block. */
   ram_slot        = harness_dlsym(&cfg, "jaguarMainRAM");
   p_jaguarMainRAM = ram_slot ? *ram_slot : NULL;

   if (!p_JaguarWriteLong || !p_jaguarMainRAM || !p_vjs ||
       !p_retro_serialize_size || !p_retro_serialize ||
       !p_BlitterCompareEnable)
   {
      fprintf(stderr,
         "FAIL: required core symbols missing -- build with TEST_EXPORTS=1\n");
      harness_shutdown(&cfg);
      return 1;
   }

   /* blitter_mmio.c checks compare mode *before* the fast/accurate split;
    * with it on, both passes would run the comparison harness instead of
    * the implementation under test.  It is off by default -- assert it. */
   p_BlitterCompareEnable(0);

   /* Output vectors run under both blit implementations. */
   for (i = 0; i < BLIT_MODES; i++)
   {
      select_blitter(i);
      test_pixel_copy();
      test_phrase_copy();
      test_lfu_ops();
      test_pitch();
      test_gouraud_output();
      test_dstenz_without_dsten();
      test_z_clear_32bpp();
      test_gourz_seed_first();
   }

   /* Decode guards: fast path only (see the enum comment up top). */
   select_blitter(BLIT_FAST);
   test_gouraud_colour_index_reset();
   test_cmd_decode_reaches_savestate();

   harness_shutdown(&cfg);
   remove(rom_path);

   if (recording)
   {
      printf("/* recorded %d vectors */\n", checks);
      return 0;
   }

   if (failures == 0)
      printf("All %d blitter command checks passed.\n", checks);
   else
      printf("%d of %d blitter command checks FAILED.\n", failures, checks);

   return failures ? 1 : 0;
}
