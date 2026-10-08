/*
 * test_crash_detect_inframe.c -- CrashDetectBlitIsAbsurd() table (issue #740).
 *
 * The blitter runs synchronously inside one register write, so a garbage
 * B_COUNT hangs retro_run where no per-frame check can see it.  The
 * inframe_hang signature fires at blit dispatch when the pixel count is
 * past 2^24, counted the way the accurate engine executes it (outer 0 =
 * 65536 lines per the JTRM; inner 0 = one step).  This pins the threshold,
 * the two values measured on Music Demo (ScatoLOGIC) in BIOS mode, and the
 * B_COUNT=0 boot blit three commercial Williams/Telegames carts issue.
 *
 * Second table: CrashDetectBlitNeverEnds() (issue #800), the blits whose
 * pixel count is small but whose inner counter never decrements -- phrase
 * mode below 8bpp, per INNER.NET's Inc0-Inc3 decode.  DEMO1B's blit
 * (B_COUNT=$000000DC, 14.4M "pixels") sits under the 2^24 threshold, so
 * the pixel rule alone never named it.
 *
 * Build: make TEST_EXPORTS=1 test/test_crash_detect_inframe
 * Run:   ./test/test_crash_detect_inframe ./virtualjaguar_libretro.dylib
 */

#include <stdio.h>
#include <stdint.h>
#include <dlfcn.h>

typedef int (*absurd_fn)(uint32_t);
typedef int (*never_fn)(uint32_t, uint32_t, uint32_t);
typedef int (*approx_fn)(uint32_t);

/* Third table: BlitterNeverEndsApprox() -- whether the accurate engine's
 * one-wrap-period result for a never-ending blit is exact (repeating the
 * step is idempotent on memory) or a defined one-pass approximation. */
struct approx_row {
   const char *name;
   uint32_t b_cmd;
   int want;
};

static const struct approx_row approx_rows[] = {
   /* Measured hangs, LFU clear and no accumulators: exact. */
   { "DEMO1B $00002208 (DSTEN, GOURZ w/o DSTWRZ)",   0x00002208u, 0 },
   { "Chroma-Luma $00002704 (SRCENX, LFU clear)",    0x00002704u, 0 },
   { "Native Demo $00002718 (DSTEN/DSTENZ, LFU 0)",  0x00002718u, 0 },
   { "pattern fill, LFU S only, no source read",     0x01810000u, 0 },
   /* Not provably idempotent: approximation. */
   { "Music Demo $2710826D (BCOMPEN, ZMODE, S&D)",   0x2710826Du, 1 },
   { "JagMania $FFFFFFFB (everything)",              0xFFFFFFFBu, 1 },
   { "DSTEN + LFU NOT D (toggles every pass)",       0x00A00008u, 1 },
   { "SRCEN + LFU S (source steps separately)",      0x01800001u, 1 },
   { "GOURD",                                        0x00001000u, 1 },
   { "GOURZ + DSTWRZ",                               0x00002020u, 1 },
};

/* A1_FLAGS / A2_FLAGS: pixel size in bits 3-5, X add control in bits
 * 16-17 (00 = phrase mode, 01 = add pixel size). */
#define FLAGS_PHRASE(psz)  ((uint32_t)(psz) << 3)
#define FLAGS_PIXEL(psz)   (((uint32_t)(psz) << 3) | 0x00010000u)

struct never_row {
   const char *name;
   uint32_t b_count;
   uint32_t dst_flags;
   uint32_t dst_x;
   int want;
};

static const struct never_row never_rows[] = {
   /* Measured: DEMO1B [a1] HLE, all address registers zero (A1_FLAGS=0 =
    * phrase mode, 1bpp), outer 0, inner $DC. */
   { "DEMO1B [a1] $000000DC, 1bpp phrase, x=0",  0x000000DCu, FLAGS_PHRASE(0), 0, 1 },
   /* Music Demo BIOS (#794): A1_FLAGS=$00200000 -> phrase, 1bpp, x=0. */
   { "Music Demo $2710826D, flags $00200000",    0x2710826Du, 0x00200000u,     0, 1 },
   { "2bpp phrase, x even",                      0x00010010u, FLAGS_PHRASE(1), 4, 1 },
   { "4bpp phrase, x odd, inner 2",              0x00010002u, FLAGS_PHRASE(2), 5, 1 },
   /* The 8/16/32 bpp decodes always decrement (Inc1-Inc3). */
   { "same at 8bpp phrase",                      0x000000DCu, FLAGS_PHRASE(3), 0, 0 },
   { "same at 16bpp phrase",                     0x000000DCu, FLAGS_PHRASE(4), 0, 0 },
   { "same at 32bpp phrase",                     0x000000DCu, FLAGS_PHRASE(5), 0, 0 },
   /* Pixel mode always decrements by one. */
   { "1bpp pixel mode",                          0x000000DCu, FLAGS_PIXEL(0),  0, 0 },
   /* The first write can still end the line. */
   { "1bpp phrase, inner 0 (count==0, one write)", 0x00010000u, FLAGS_PHRASE(0), 0, 0 },
   { "1bpp phrase, inner 1, x odd",              0x00010001u, FLAGS_PHRASE(0), 1, 0 },
   { "1bpp phrase, inner 1, x even",             0x00010001u, FLAGS_PHRASE(0), 2, 1 },
};

struct row {
   const char *name;
   uint32_t b_count;
   int want;
};

static const struct row rows[] = {
   /* Measured: the wedged GPU's garbage, 10000 x 33389 = 334M pixels
    * (accurate blitter ran it for hours), then 65535 x 65535 (fast). */
   { "Music Demo garbage $2710826D",          0x2710826Du, 1 },
   { "Music Demo garbage $FFFFFFFF",          0xFFFFFFFFu, 1 },
   /* Outer 0 runs 65536 lines (JTRM and the accurate engine agree); inner 0
    * stops after one step (<= one phrase, 64 px) in the accurate engine. */
   { "Williams/Telegames boot blit B_COUNT=0",  0x00000000u, 0 },
   { "inner 0 (one phrase) x 65535 lines",      0xFFFF0000u, 0 },
   { "outer 0 (65536 lines) x 512",             0x00000200u, 1 },
   { "outer 0 (65536 lines) x 256 = 2^24",      0x00000100u, 0 },
   /* Boundary: exactly 2^24 is allowed, one more line is not. */
   { "4096 x 4096 = 2^24 exactly",              0x10001000u, 0 },
   { "4097 x 4096",                             0x10011000u, 1 },
   /* Ordinary blits. */
   { "full-screen 320 x 240",                 0x00F00140u, 0 },
   { "one pixel",                             0x00010001u, 0 },
   { "inner 0, 1 line",                       0x00010000u, 0 },
   { "16 lines x 65535",                      0x0010FFFFu, 0 },
};

int main(int argc, char **argv)
{
#ifdef __APPLE__
   const char *core = argc > 1 ? argv[1] : "./virtualjaguar_libretro.dylib";
#else
   const char *core = argc > 1 ? argv[1] : "./virtualjaguar_libretro.so";
#endif
   void *handle;
   absurd_fn fn;
   never_fn never;
   approx_fn approx;
   unsigned i, fails = 0;

   handle = dlopen(core, RTLD_LAZY);
   if (!handle)
   {
      fprintf(stderr, "FAIL: dlopen(%s): %s\n", core, dlerror());
      return 1;
   }
   fn = (absurd_fn)dlsym(handle, "CrashDetectBlitIsAbsurd");
   if (!fn)
   {
      fprintf(stderr, "FAIL: CrashDetectBlitIsAbsurd not exported (build with TEST_EXPORTS=1)\n");
      return 1;
   }

   for (i = 0; i < sizeof(rows) / sizeof(rows[0]); i++)
   {
      const struct row *r = &rows[i];
      int got = fn(r->b_count);
      if (got != r->want)
      {
         fprintf(stderr, "FAIL: %s (B_COUNT=$%08X): got %d want %d\n",
                 r->name, (unsigned)r->b_count, got, r->want);
         fails++;
      }
   }

   never = (never_fn)dlsym(handle, "CrashDetectBlitNeverEnds");
   if (!never)
   {
      fprintf(stderr, "FAIL: CrashDetectBlitNeverEnds not exported (build with TEST_EXPORTS=1)\n");
      return 1;
   }
   for (i = 0; i < sizeof(never_rows) / sizeof(never_rows[0]); i++)
   {
      const struct never_row *r = &never_rows[i];
      int got = never(r->b_count, r->dst_flags, r->dst_x);
      if (got != r->want)
      {
         fprintf(stderr, "FAIL: %s (B_COUNT=$%08X flags=$%08X x=%u): never_ends got %d want %d\n",
                 r->name, (unsigned)r->b_count, (unsigned)r->dst_flags,
                 (unsigned)r->dst_x, got, r->want);
         fails++;
      }
   }

   approx = (approx_fn)dlsym(handle, "BlitterNeverEndsApprox");
   if (!approx)
   {
      fprintf(stderr, "FAIL: BlitterNeverEndsApprox not exported (build with TEST_EXPORTS=1)\n");
      return 1;
   }
   for (i = 0; i < sizeof(approx_rows) / sizeof(approx_rows[0]); i++)
   {
      const struct approx_row *r = &approx_rows[i];
      int got = approx(r->b_cmd);
      if (got != r->want)
      {
         fprintf(stderr, "FAIL: %s (B_CMD=$%08X): approx got %d want %d\n",
                 r->name, (unsigned)r->b_cmd, got, r->want);
         fails++;
      }
   }

   if (fails)
   {
      fprintf(stderr, "test_crash_detect_inframe: %u failure(s)\n", fails);
      return 1;
   }
   printf("test_crash_detect_inframe: %u cases PASS\n",
          (unsigned)(sizeof(rows) / sizeof(rows[0])
                     + sizeof(never_rows) / sizeof(never_rows[0])
                     + sizeof(approx_rows) / sizeof(approx_rows[0])));
   return 0;
}
