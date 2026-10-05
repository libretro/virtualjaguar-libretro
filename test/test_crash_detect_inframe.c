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
 * Build: make TEST_EXPORTS=1 test/test_crash_detect_inframe
 * Run:   ./test/test_crash_detect_inframe ./virtualjaguar_libretro.dylib
 */

#include <stdio.h>
#include <stdint.h>
#include <dlfcn.h>

typedef int (*absurd_fn)(uint32_t);

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

   if (fails)
   {
      fprintf(stderr, "test_crash_detect_inframe: %u failure(s)\n", fails);
      return 1;
   }
   printf("test_crash_detect_inframe: %u cases PASS\n", (unsigned)(sizeof(rows) / sizeof(rows[0])));
   return 0;
}
