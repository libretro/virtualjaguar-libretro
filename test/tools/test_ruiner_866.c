/*
 * test/tools/test_ruiner_866.c -- Ruiner Pinball must not run its GPU out of
 * GPU local RAM (issue #866).
 *
 * Ruiner Pinball's GPU kernel copies a decompressor into GPU RAM at $F03A08,
 * then patches four `movei` immediates with long load/store pairs at
 * word-aligned (not long-aligned) addresses.  GPU local RAM drops A1:A0 for
 * every long access, but `store (r15+rn)` did not, so $F03A3A/$F03A56 got the
 * wrong value, `jump (r8)` at $F03A4A went to $F09850 and the GPU ran into
 * DSP RAM (escape at about frame 56).
 *
 * Runs the cart (HLE boot, or --bios for the real boot ROM) and fails if the
 * GPU PC is ever sampled outside GPU local RAM ($F03000-$F03FFF) at the end
 * of a frame (PC 0 = halted GPU is ignored).  The ROM must be the good dump (CRC $5B6BB205).  The Makefile
 * skips (recorded in the skip ledger) when the ROM is absent.
 *
 * Usage:  test_ruiner_866 <core> --rom PATH [--frames N] [--bios]
 */

#define _DEFAULT_SOURCE 1

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <unistd.h>
#include "../harness/harness.h"

#define RUINER_GOOD_CRC  0x5B6BB205u
#define WALL_CLOCK_SECS  900u

static uint32_t file_crc32(const char *path, int *ok)
{
   FILE *f = fopen(path, "rb");
   uint32_t crc = 0xFFFFFFFFu;
   unsigned char buf[65536];
   size_t n, i;
   int b;

   *ok = 0;
   if (!f)
      return 0;
   while ((n = fread(buf, 1, sizeof(buf), f)) > 0)
      for (i = 0; i < n; i++)
      {
         crc ^= buf[i];
         for (b = 0; b < 8; b++)
            crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1u)));
      }
   fclose(f);
   *ok = 1;
   return crc ^ 0xFFFFFFFFu;
}

int main(int argc, char **argv)
{
   static char a_prog[] = "test_ruiner_866";
   static char a_quiet[] = "--quiet";
   static char a_bios[] = "--bios";
   const char *core, *rom = NULL;
   int i, nargc = 0, crc_ok, bios = 0;
   unsigned frames = 600, f;
   char *nargv[8];
   harness_config cfg = HARNESS_CONFIG_DEFAULT;
   uint32_t (*get_pc)(void);
   uint32_t pc = 0, crc;

   if (argc < 2)
   {
      fprintf(stderr, "usage: %s <core> --rom PATH [--frames N] [--bios]\n", argv[0]);
      return 2;
   }
   core = argv[1];
   for (i = 2; i < argc; i++)
   {
      if (!strcmp(argv[i], "--rom") && i + 1 < argc)
         rom = argv[++i];
      else if (!strcmp(argv[i], "--frames") && i + 1 < argc)
         frames = (unsigned)atoi(argv[++i]);
      else if (!strcmp(argv[i], "--bios"))
         bios = 1;
      else
      {
         fprintf(stderr, "unknown argument: %s\n", argv[i]);
         return 2;
      }
   }
   if (!rom)
   {
      fprintf(stderr, "--rom PATH is required\n");
      return 2;
   }
   crc = file_crc32(rom, &crc_ok);
   if (!crc_ok || crc != RUINER_GOOD_CRC)
   {
      fprintf(stderr, "FAIL: %s is not the good Ruiner Pinball dump "
              "(CRC $%08X, want $%08X)\n", rom, (unsigned)crc, RUINER_GOOD_CRC);
      return 1;
   }

   alarm(WALL_CLOCK_SECS);

   nargv[nargc++] = a_prog;
   nargv[nargc++] = (char *)core;
   nargv[nargc++] = (char *)rom;
   nargv[nargc++] = a_quiet;
   if (bios)
      nargv[nargc++] = a_bios;
   nargv[nargc] = NULL;

   if (!harness_init_from_args(&cfg, nargc, nargv) || !harness_load_rom(&cfg))
   {
      fprintf(stderr, "FAIL: could not load %s\n", rom);
      return 1;
   }
   get_pc = (uint32_t (*)(void))harness_dlsym(&cfg, "GPUGetPC");
   if (!get_pc)
   {
      fprintf(stderr, "FAIL: GPUGetPC not exported (build with TEST_EXPORTS=1)\n");
      harness_shutdown(&cfg);
      return 1;
   }

   for (f = 0; f < frames; f++)
   {
      harness_step(&cfg);
      pc = get_pc();
      /* PC 0 is a halted/reset GPU (the BIOS clears it between jobs), not
       * a runaway; anything else outside local RAM is an escape. */
      if (pc != 0 && (pc < 0xF03000u || pc >= 0xF04000u))
      {
         fprintf(stderr, "FAIL [%s]: GPU PC left local RAM at frame %u "
                 "(pc=$%08X) -- a movei immediate was patched at the wrong "
                 "address (#866)\n", bios ? "BIOS" : "HLE", f + 1, (unsigned)pc);
         harness_shutdown(&cfg);
         return 1;
      }
   }

   harness_shutdown(&cfg);
   printf("PASS [%s]: GPU stayed in local RAM for %u frames (last PC $%06X)\n",
          bios ? "BIOS" : "HLE", frames, (unsigned)pc);
   return 0;
}
