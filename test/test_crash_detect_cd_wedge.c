/*
 * test_crash_detect_cd_wedge.c -- CrashDetectCDSeekWedgeFrame() table
 * (issue #741).
 *
 * cd_seek_wedge used to count every frame with frozen FIFO drains.  That
 * fired on 15 finished transfers across 7 bios-mode titles that went on to
 * run (BUTCH low byte $02: the game cleared the master interrupt enable);
 * Philia ends its transfer by clearing I2CNTRL bit 2 instead.  The real
 * IMASK-stuck transfer wedge has both still on: BUTCH $03 (master + FIFO
 * half-full interrupts armed) and I2CNTRL bit 2 (FIFO data enabled).
 * This table pins both sides so the signature can't silently go blind (or
 * noisy) again; the disc-backed half lives in
 * test/tools/cd_seek_wedge_regress.sh.
 *
 * Build: make TEST_EXPORTS=1 test/test_crash_detect_cd_wedge
 * Run:   ./test/test_crash_detect_cd_wedge ./virtualjaguar_libretro.dylib
 */

#include <stdio.h>
#include <stdint.h>
#include <dlfcn.h>

typedef int (*wedge_frame_fn)(uint32_t, uint32_t, uint32_t, uint32_t, int, uint8_t, uint8_t);

struct row {
   const char *name;
   uint32_t starts, dones, drains, last_drains;
   int running;
   uint8_t butch;
   uint8_t i2s;
   int want;
};

static const struct row rows[] = {
   /* Measured false positives (bios mode, all alive afterwards).
    *                                                        starts dones drains last  run butch i2s  want */
   { "Myst intro pause: BUTCH $02, I2S on",                10, 10, 75081, 75081, 1, 0x02, 0x05, 0 },
   { "Primal Rage FMV done: BUTCH $02, I2S on",            2,  2,  17996, 17996, 1, 0x02, 0x07, 0 },
   { "Philia waiting for a button: BUTCH $03, I2S off",    8,  8,  115875,115875,1, 0x03, 0x01, 0 },
   { "transfer done, all BUTCH IRQs off",                  4,  4,  92617, 92617, 1, 0x00, 0x05, 0 },
   { "master on for DSA/subcode, FIFO IRQ off (CDDA)",     3,  3,  500,   500,   1, 0x01, 0x05, 0 },
   /* Measured true positives (7c98e16 re-broken as a control). */
   { "Primal Rage IMASK wedge: BUTCH $03, I2S on",         2,  2,  12774, 12774, 1, 0x03, 0x05, 1 },
   { "BrainDead 13 IMASK wedge: BUTCH $03, I2S on",        1,  1,  678,   678,   1, 0x03, 0x05, 1 },
   { "extra BUTCH enable bits don't mask it",              1,  1,  678,   678,   1, 0x0F, 0x05, 1 },
   /* Shape (a): seek response never arrived -- fires whatever the regs say. */
   { "seek outstanding, everything off",                   3,  2,  900,   900,   1, 0x00, 0x00, 1 },
   /* Never counts. */
   { "no seek yet",                                        0,  0,  0,     0,     1, 0x03, 0x05, 0 },
   { "drains moving",                                      2,  2,  1001,  1000,  1, 0x03, 0x05, 0 },
   { "no processor running",                               2,  2,  1000,  1000,  0, 0x03, 0x05, 0 },
};

int main(int argc, char **argv)
{
#ifdef __APPLE__
   const char *core = argc > 1 ? argv[1] : "./virtualjaguar_libretro.dylib";
#else
   const char *core = argc > 1 ? argv[1] : "./virtualjaguar_libretro.so";
#endif
   void *handle;
   wedge_frame_fn fn;
   unsigned i, fails = 0;

   handle = dlopen(core, RTLD_LAZY);
   if (!handle)
   {
      fprintf(stderr, "FAIL: dlopen(%s): %s\n", core, dlerror());
      return 1;
   }
   fn = (wedge_frame_fn)dlsym(handle, "CrashDetectCDSeekWedgeFrame");
   if (!fn)
   {
      fprintf(stderr, "FAIL: CrashDetectCDSeekWedgeFrame not exported (build with TEST_EXPORTS=1)\n");
      return 1;
   }

   for (i = 0; i < sizeof(rows) / sizeof(rows[0]); i++)
   {
      const struct row *r = &rows[i];
      int got = fn(r->starts, r->dones, r->drains, r->last_drains, r->running, r->butch, r->i2s);
      if (got != r->want)
      {
         fprintf(stderr, "FAIL: %s: got %d want %d\n", r->name, got, r->want);
         fails++;
      }
   }

   if (fails)
   {
      fprintf(stderr, "test_crash_detect_cd_wedge: %u failure(s)\n", fails);
      return 1;
   }
   printf("test_crash_detect_cd_wedge: %u cases PASS\n", (unsigned)(sizeof(rows) / sizeof(rows[0])));
   return 0;
}
