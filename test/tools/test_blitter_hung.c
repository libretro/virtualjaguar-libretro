/*
 * test/tools/test_blitter_hung.c -- a blit hardware never finishes hangs the
 * emulated blitter, never the host (issues #800, #794).
 *
 * Phrase mode below 8bpp: INNER.NET's inner-counter decrement is dstxp[0],
 * and phrase-aligned X keeps it at zero, so the blit never returns to idle
 * on hardware.  Policy: the accurate engine runs the stuck step for one
 * destination wrap period (exact when the step is idempotent on memory),
 * then the blitter stays "hung" until reset: B_CMD reads busy (IDLE bit 0
 * clear) and further starts are ignored.  The flag is saved in the "BLH1"
 * savestate chunk.
 *
 * ROM-free: boots a 48-byte synthetic raw binary that parks its 68K, then
 * programs the blitter directly through the exported register path:
 *   A1_BASE = $100000, A1_FLAGS = 0 (phrase mode, 1bpp, contiguous),
 *   A1_PIXEL = 0, B_COUNT = $000100DC (1 line, inner 220), B_CMD = 0
 *   (LFU clear: every write is zeros -- the idempotent class).
 * Asserts (accurate engine):
 *   - the blit returns (a wall-clock alarm turns a host hang into a FAIL);
 *   - memory: exactly one wrap period, $100000..$101FFF (65536 px at 1bpp
 *     = 1024 phrases), is zero and the next phrase is untouched;
 *   - B_CMD status: IDLE clear, stuck inner count $DC in bits 16-31;
 *   - a second B_CMD start while hung writes nothing;
 *   - serialize -> retro_reset clears the flag (status idle) ->
 *     unserialize restores it.
 * With --fast, the same hung reporting under the fast engine (its memory
 * effect is not asserted).
 *
 * ROM mode (--rom PATH [--bios]): runs PATH for 600 frames under the
 * accurate blitter and asserts every frame completes AND the blitter ended
 * hung -- i.e. the title really issued a never-ending blit and the host
 * survived it.  Reproducers: Music Demo (ScatoLOGIC) --bios (#794, frame
 * 158) and Native Demo (bin) HLE (frame 170); both used to hang retro_run
 * forever.
 *
 * Build:  cc -O2 -Wall -std=c99 -I. -I./src -I./libretro-common/include \
 *           -o test/tools/test_blitter_hung test/tools/test_blitter_hung.c \
 *           test/harness/harness.c -ldl -lm
 * Run:    test_blitter_hung <core> [--fast]
 *         test_blitter_hung <core> --rom PATH [--bios]
 */

#define _DEFAULT_SOURCE 1

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <unistd.h>
#include "../harness/harness.h"

#define WALL_CLOCK_SECS 120u

#define BLIT_REGS   0x00F02200u
#define R_A1_BASE   0x00u
#define R_A1_FLAGS  0x04u
#define R_A1_PIXEL  0x0Cu
#define R_B_CMD     0x38u
#define R_B_COUNT   0x3Cu

#define DST_BASE    0x00100000u
#define WRAP_BYTES  0x2000u          /* 65536 px at 1bpp */
#define SECOND_BASE 0x00180000u

typedef void     (*blit_wl_fn)(uint32_t, uint32_t, uint32_t);
typedef uint32_t (*blit_rl_fn)(uint32_t, uint32_t);
typedef int      (*is_hung_fn)(void);
typedef void     (*reset_fn)(void);
typedef size_t   (*ser_size_fn)(void);
typedef bool     (*ser_fn)(void *, size_t);
typedef bool     (*unser_fn)(const void *, size_t);

/* Same synthetic image as test_upload_illegal_park: BJL startup idiom,
 * SP = $200000, two JSRs, ILLEGAL -> 68K parks at $1000. */
static const unsigned char synth_image[] = {
   0x23, 0xFC, 0x00, 0x07, 0x00, 0x07, 0x00, 0xF0, 0x21, 0x0C,
   0x2E, 0x7C, 0x00, 0x20, 0x00, 0x00,
   0x4E, 0xB9, 0x00, 0x80, 0x20, 0x20,
   0x4E, 0xB9, 0x00, 0x80, 0x20, 0x20,
   0x4A, 0xFC,
   0x4E, 0x71,
   0x4E, 0x75,
   0x4E, 0x71, 0x4E, 0x71, 0x4E, 0x71, 0x4E, 0x71, 0x4E, 0x71,
   0x4E, 0x71, 0x4E, 0x71
};

static int fails = 0;

#define CHECK(cond, ...) do { \
   if (!(cond)) { fprintf(stderr, "FAIL: " __VA_ARGS__); fprintf(stderr, "\n"); fails++; } \
} while (0)

static int run_rom(const char *core, const char *rom, int bios)
{
   static char a_prog[] = "test_blitter_hung";
   static char a_frames_flag[] = "--frames";
   static char a_frames[] = "600";
   static char a_option_flag[] = "--option";
   static char a_acc[] = "virtualjaguar_usefastblitter=disabled";
   static char a_bios[] = "--bios";
   static char a_quiet[] = "--quiet";
   char *hargv[12];
   int hargc = 0;
   harness_config cfg = HARNESS_CONFIG_DEFAULT;
   is_hung_fn is_hung;
   unsigned frames;
   int hung;

   hargv[hargc++] = a_prog;
   hargv[hargc++] = (char *)core;
   hargv[hargc++] = (char *)rom;
   hargv[hargc++] = a_frames_flag;
   hargv[hargc++] = a_frames;
   hargv[hargc++] = a_option_flag;
   hargv[hargc++] = a_acc;
   if (bios)
      hargv[hargc++] = a_bios;
   hargv[hargc++] = a_quiet;
   hargv[hargc] = NULL;

   if (!harness_init_from_args(&cfg, hargc, hargv) || !harness_load_rom(&cfg))
   {
      fprintf(stderr, "FAIL: could not load %s\n", rom);
      return 1;
   }
   is_hung = (is_hung_fn)harness_dlsym(&cfg, "BlitterIsHung");
   if (!is_hung)
   {
      fprintf(stderr, "FAIL: BlitterIsHung not exported (TEST_EXPORTS=1)\n");
      harness_shutdown(&cfg);
      return 1;
   }
   harness_run(&cfg);
   frames = cfg.current_frame;
   hung = is_hung();
   harness_shutdown(&cfg);

   CHECK(frames >= 600, "%s: only %u of 600 frames", rom, frames);
   CHECK(hung, "%s: no never-ending blit was issued (blitter not hung) -- "
         "the reproducer no longer exercises the path", rom);
   if (fails)
      return 1;
   printf("PASS test_blitter_hung (%s%s): 600 frames, blitter hung, host not\n",
          rom, bios ? ", BIOS" : "");
   return 0;
}


int main(int argc, char **argv)
{
   static char a_prog[] = "test_blitter_hung";
   static char a_frames_flag[] = "--frames";
   static char a_frames[] = "10";
   static char a_option_flag[] = "--option";
   static char a_acc[] = "virtualjaguar_usefastblitter=disabled";
   static char a_fast[] = "virtualjaguar_usefastblitter=enabled";
   static char a_quiet[] = "--quiet";
   char path[] = "/tmp/vj_blitter_hung_XXXXXX.jag";
   char *hargv[10];
   int hargc = 0, fd, fast = 0;
   harness_config cfg = HARNESS_CONFIG_DEFAULT;
   blit_wl_fn wl;
   blit_rl_fn rl;
   is_hung_fn is_hung;
   reset_fn do_reset;
   ser_size_fn ser_size;
   ser_fn ser;
   unser_fn unser;
   uint8_t **ram_pp;
   uint8_t *ram;
   uint32_t status, i, nonzero = 0;
   size_t sz;
   void *blob;

   if (argc < 2)
   {
      fprintf(stderr, "usage: %s <core> [--fast]\n", argv[0]);
      return 2;
   }
   if (argc > 2 && !strcmp(argv[2], "--fast"))
      fast = 1;

   alarm(WALL_CLOCK_SECS);

   if (argc > 3 && !strcmp(argv[2], "--rom"))
      return run_rom(argv[1], argv[3], argc > 4 && !strcmp(argv[4], "--bios"));

   fd = mkstemps(path, 4);
   if (fd < 0 || write(fd, synth_image, sizeof(synth_image)) != (ssize_t)sizeof(synth_image))
   {
      perror("synthetic image");
      return 1;
   }
   close(fd);

   hargv[hargc++] = a_prog;
   hargv[hargc++] = argv[1];
   hargv[hargc++] = path;
   hargv[hargc++] = a_frames_flag;
   hargv[hargc++] = a_frames;
   hargv[hargc++] = a_option_flag;
   hargv[hargc++] = fast ? a_fast : a_acc;
   hargv[hargc++] = a_quiet;
   hargv[hargc] = NULL;

   if (!harness_init_from_args(&cfg, hargc, hargv) || !harness_load_rom(&cfg))
   {
      fprintf(stderr, "FAIL: could not boot the synthetic image\n");
      unlink(path);
      return 1;
   }
   unlink(path);

   wl = (blit_wl_fn)harness_dlsym(&cfg, "BlitterWriteLong");
   rl = (blit_rl_fn)harness_dlsym(&cfg, "BlitterReadLong");
   is_hung = (is_hung_fn)harness_dlsym(&cfg, "BlitterIsHung");
   do_reset = (reset_fn)harness_dlsym(&cfg, "retro_reset");
   ser_size = (ser_size_fn)harness_dlsym(&cfg, "retro_serialize_size");
   ser = (ser_fn)harness_dlsym(&cfg, "retro_serialize");
   unser = (unser_fn)harness_dlsym(&cfg, "retro_unserialize");
   ram_pp = (uint8_t **)harness_dlsym(&cfg, "jaguarMainRAM");
   if (!wl || !rl || !is_hung || !do_reset || !ser_size || !ser || !unser || !ram_pp)
   {
      fprintf(stderr, "FAIL: symbols missing (build the core with TEST_EXPORTS=1)\n");
      harness_shutdown(&cfg);
      return 1;
   }

   harness_run(&cfg);    /* 68K parks; nothing else touches the blitter */
   ram = *ram_pp;

   CHECK(!is_hung(), "blitter hung before any blit");
   status = rl(BLIT_REGS + R_B_CMD, 0);
   CHECK(status & 1u, "status $%08X: IDLE clear before any blit", (unsigned)status);

   memset(ram + DST_BASE, 0xFF, WRAP_BYTES + 16);
   memset(ram + SECOND_BASE, 0xFF, 64);

   wl(BLIT_REGS + R_A1_BASE, DST_BASE, 0);
   wl(BLIT_REGS + R_A1_FLAGS, 0, 0);
   wl(BLIT_REGS + R_A1_PIXEL, 0, 0);
   wl(BLIT_REGS + R_B_COUNT, 0x000100DCu, 0);
   wl(BLIT_REGS + R_B_CMD, 0, 0);        /* starts the blit; must return */

   CHECK(is_hung(), "never-ending blit did not leave the blitter hung");
   status = rl(BLIT_REGS + R_B_CMD, 0);
   CHECK(!(status & 1u), "status $%08X: IDLE set while hung", (unsigned)status);
   CHECK((status >> 16) == 0xDCu, "status $%08X: inner count %u, want 220",
         (unsigned)status, (unsigned)(status >> 16));

   if (!fast)
   {
      for (i = 0; i < WRAP_BYTES; i++)
         if (ram[DST_BASE + i] != 0)
            nonzero++;
      CHECK(nonzero == 0, "%u of %u bytes in the wrap period not cleared",
            (unsigned)nonzero, (unsigned)WRAP_BYTES);
      for (i = WRAP_BYTES; i < WRAP_BYTES + 16; i++)
         CHECK(ram[DST_BASE + i] == 0xFF,
               "byte $%06X past the wrap period was written",
               (unsigned)(DST_BASE + i));
   }

   /* A start while hung is ignored (OUTER.NET: go only leaves idle). */
   wl(BLIT_REGS + R_A1_BASE, SECOND_BASE, 0);
   wl(BLIT_REGS + R_A1_FLAGS, 0x00010018u, 0);    /* 8bpp pixel mode */
   wl(BLIT_REGS + R_B_COUNT, 0x00010010u, 0);
   wl(BLIT_REGS + R_B_CMD, 0, 0);
   for (i = 0; i < 64; i++)
      CHECK(ram[SECOND_BASE + i] == 0xFF, "start while hung wrote $%06X",
            (unsigned)(SECOND_BASE + i));

   /* Savestate round trip, with reset in between. */
   sz = ser_size();
   blob = malloc(sz);
   CHECK(blob && ser(blob, sz), "retro_serialize failed");
   do_reset();
   CHECK(!is_hung(), "retro_reset did not clear the hung flag");
   status = rl(BLIT_REGS + R_B_CMD, 0);
   CHECK(status & 1u, "status $%08X after reset: IDLE clear", (unsigned)status);
   if (blob)
   {
      CHECK(unser(blob, sz), "retro_unserialize failed");
      CHECK(is_hung(), "hung flag not restored from the savestate");
      status = rl(BLIT_REGS + R_B_CMD, 0);
      CHECK(!(status & 1u) && (status >> 16) == 0xDCu,
            "status $%08X after unserialize", (unsigned)status);
      free(blob);
   }

   harness_shutdown(&cfg);
   if (fails)
   {
      fprintf(stderr, "test_blitter_hung (%s): %d failure(s)\n",
              fast ? "fast" : "accurate", fails);
      return 1;
   }
   printf("PASS test_blitter_hung (%s engine): never-ending blit returned, "
          "status busy, start ignored, reset clears, savestate restores\n",
          fast ? "fast" : "accurate");
   return 0;
}
