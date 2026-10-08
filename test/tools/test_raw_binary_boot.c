/*
 * test/tools/test_raw_binary_boot.c -- a headerless raw binary loads at its
 * link base and runs its own code with video (issue #818).
 *
 * Chroma-Luma Color Pick (bin) and JagMania (Jul 8) are raw 68K binaries
 * linked at $5000.  The loader's containment scorer put them at $4000 (a
 * $4000 window also contains their targets), so their first JSR landed in
 * font data, the 68K ran away into ILLEGAL and parked at $1000 (#800's
 * guard), and the screen stayed black.  file.c now breaks the tie on
 * entry-point consistency.
 *
 * For --rom PATH this asserts, under the shipped ACCURATE blitter:
 *   - the loader chose --expect-base as the run address;
 *   - after --frames frames the 68K PC is inside the loaded image (game
 *     code), not at the $1000 ILLEGAL park;
 *   - at least half the frames are lit (>1% non-black pixels).
 * --bios is passed through.  RAM-loaded images never run the boot ROM
 * (jaguar.c only takes the BIOS path for a cartridge), but BIOS mode still
 * changes reset state -- main and DSP RAM are PRNG-filled instead of
 * zeroed -- so the suite checks both modes.
 *
 * ROM-gated: the Makefile records a SKIP when the ROM is absent.
 *
 * Build:  cc -O2 -Wall -std=c99 -I. -I./src -I./libretro-common/include \
 *           -o test/tools/test_raw_binary_boot \
 *           test/tools/test_raw_binary_boot.c test/harness/harness.c -ldl -lm
 * Run:    test_raw_binary_boot <core> --rom PATH --expect-base ADDR
 *                              [--frames N] [--bios]
 */

#define _DEFAULT_SOURCE 1

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "../harness/harness.h"

#define PARK_PC            0x00001000u
#define M68K_REG_PC_INDEX  16          /* m68kinterface.h: D0-D7, A0-A7, PC */
/* Hang backstop only; the assertions are frame-based.  Generous because the
 * suite runs on loaded hosts. */
#define WALL_CLOCK_SECS    1200u

typedef unsigned int (*m68k_get_reg_fn)(void *, int);

typedef struct {
   unsigned frames;
   unsigned lit_frames;
} video_state;

static void video_cb(void *userdata, const void *data, unsigned width,
                     unsigned height, size_t pitch)
{
   video_state *st = (video_state *)userdata;
   const uint8_t *rows = (const uint8_t *)data;
   unsigned x, y, lit = 0, total = 0;

   if (!data || !width || !height)
      return;
   /* Every 4th pixel of every 4th row, as cart_boot_probe samples. */
   for (y = 0; y < height; y += 4)
   {
      const uint32_t *px = (const uint32_t *)(rows + y * pitch);
      for (x = 0; x < width; x += 4)
      {
         total++;
         if (px[x] & 0x00FFFFFFu)
            lit++;
      }
   }
   st->frames++;
   if (total && lit * 100u > total)
      st->lit_frames++;
}

int main(int argc, char **argv)
{
   static char a_prog[] = "test_raw_binary_boot";
   static char a_frames_flag[] = "--frames";
   static char a_option_flag[] = "--option";
   static char a_option[] = "virtualjaguar_usefastblitter=disabled";
   static char a_bios[] = "--bios";
   static char a_quiet[] = "--quiet";
   char *hargv[12];
   int hargc = 0;
   const char *core, *rom = NULL, *frames = "300";
   unsigned long expect_base = 0;
   int bios = 0, have_base = 0, fails = 0, i;
   harness_config cfg = HARNESS_CONFIG_DEFAULT;
   video_state vs;
   m68k_get_reg_fn get_reg;
   uint32_t *p_run, *p_size;
   uint32_t base, size;
   unsigned pc;
   const char *label;

   if (argc < 2)
   {
      fprintf(stderr, "usage: %s <core> --rom PATH --expect-base ADDR "
              "[--frames N] [--bios]\n", argv[0]);
      return 2;
   }
   core = argv[1];
   for (i = 2; i < argc; i++)
   {
      if (!strcmp(argv[i], "--rom") && i + 1 < argc)
         rom = argv[++i];
      else if (!strcmp(argv[i], "--expect-base") && i + 1 < argc)
      {
         expect_base = strtoul(argv[++i], NULL, 0);
         have_base = 1;
      }
      else if (!strcmp(argv[i], "--frames") && i + 1 < argc)
         frames = argv[++i];
      else if (!strcmp(argv[i], "--bios"))
         bios = 1;
      else
      {
         fprintf(stderr, "unknown argument: %s\n", argv[i]);
         return 2;
      }
   }
   if (!rom || !have_base)
   {
      fprintf(stderr, "--rom and --expect-base are required\n");
      return 2;
   }
   label = bios ? "bios" : "hle";

   alarm(WALL_CLOCK_SECS);

   hargv[hargc++] = a_prog;
   hargv[hargc++] = (char *)core;
   hargv[hargc++] = (char *)rom;
   hargv[hargc++] = a_frames_flag;
   hargv[hargc++] = (char *)frames;
   hargv[hargc++] = a_option_flag;
   hargv[hargc++] = a_option;
   if (bios)
      hargv[hargc++] = a_bios;
   hargv[hargc++] = a_quiet;
   hargv[hargc] = NULL;

   memset(&vs, 0, sizeof(vs));
   if (!harness_init_from_args(&cfg, hargc, hargv))
   {
      fprintf(stderr, "FAIL [%s] %s: harness init\n", label, rom);
      return 1;
   }
   cfg.video_callback = video_cb;
   cfg.video_callback_data = &vs;
   if (!harness_load_rom(&cfg))
   {
      fprintf(stderr, "FAIL [%s] %s: could not load\n", label, rom);
      harness_shutdown(&cfg);
      return 1;
   }

   get_reg = (m68k_get_reg_fn)harness_dlsym(&cfg, "m68k_get_reg");
   p_run = (uint32_t *)harness_dlsym(&cfg, "jaguarRunAddress");
   p_size = (uint32_t *)harness_dlsym(&cfg, "jaguarROMSize");
   if (!get_reg || !p_run || !p_size)
   {
      fprintf(stderr, "FAIL [%s]: m68k_get_reg/jaguarRunAddress/jaguarROMSize "
              "not exported (build the core with TEST_EXPORTS=1)\n", label);
      harness_shutdown(&cfg);
      return 1;
   }
   base = *p_run;
   size = *p_size;
   if (base != (uint32_t)expect_base)
   {
      fprintf(stderr, "FAIL [%s] %s: loaded at $%06X, want $%06lX\n",
              label, rom, (unsigned)base, expect_base);
      fails++;
   }

   harness_run(&cfg);
   pc = get_reg(NULL, M68K_REG_PC_INDEX) & 0xFFFFFFu;

   if (pc == PARK_PC || pc < base || pc >= base + size)
   {
      fprintf(stderr, "FAIL [%s] %s: 68K PC $%06X after %u frames is outside "
              "the image [$%06X, $%06X)%s\n", label, rom, pc, vs.frames,
              (unsigned)base, (unsigned)(base + size),
              pc == PARK_PC ? " -- parked on ILLEGAL (runaway)" : "");
      fails++;
   }
   if (vs.frames == 0 || vs.lit_frames * 2 < vs.frames)
   {
      fprintf(stderr, "FAIL [%s] %s: %u of %u frames lit, want at least half\n",
              label, rom, vs.lit_frames, vs.frames);
      fails++;
   }

   harness_shutdown(&cfg);
   if (!fails)
      printf("PASS [%s] %s: runs at $%06X, PC $%06X, %u/%u frames lit\n",
             label, rom, (unsigned)base, pc, vs.lit_frames, vs.frames);
   return fails ? 1 : 0;
}
