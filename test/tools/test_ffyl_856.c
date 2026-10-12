/*
 * test/tools/test_ffyl_856.c -- BIOS-mode power-on RAM regression (issue #856).
 *
 * Fight For Your Life (1996) sizes a byte fill from RAM $F9A90, which nothing
 * ever writes.  BIOS mode used to fill power-on RAM with a per-word PRNG; the
 * value there overran the fill into the GPU parameter block at $FA260, the
 * GPU job ran away and stored a garbage pointer at $1D8870, and the 68K's copy
 * loop took an address error into the boot ROM's exception landing pad
 * ($E005DC) at about frame 1032.  Power-on RAM is now zero-filled in both boot
 * modes.
 *
 * Runs the cart with the real boot ROM and fails unless (a) the boot ROM
 * hands off to the cart (the 68K is seen in cart space $800000-$DFFFFF) and
 * (b) after that the 68K is never sampled on $E005DC.  The ROM must be the
 * good dump (CRC $B14C4753): the [a1] bad dump is an authentic BIOS reject
 * that never hands off, so a loose filename match would test nothing.  The
 * Makefile skips (recorded in the skip ledger) when the ROM is absent.
 *
 * Usage:  test_ffyl_856 <core> --rom PATH [--frames N]
 */

#define _DEFAULT_SOURCE 1

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <unistd.h>
#include "../harness/harness.h"

#define FFYL_GOOD_CRC      0xB14C4753u
#define BIOS_TRAP_PARK     0x00E005DCu
#define M68K_REG_PC_INDEX  16          /* m68kinterface.h: D0-D7, A0-A7, PC */
#define WALL_CLOCK_SECS    1800u

typedef unsigned int (*m68k_get_reg_fn)(void *, int);

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
   static char a_prog[] = "test_ffyl_856";
   static char a_quiet[] = "--quiet";
   static char a_bios[] = "--bios";
   const char *core, *rom = NULL;
   int i, nargc = 0, crc_ok;
   unsigned frames = 1400, f, handoff = 0;
   char *nargv[8];
   harness_config cfg = HARNESS_CONFIG_DEFAULT;
   m68k_get_reg_fn get_reg;
   unsigned pc = 0;
   uint32_t crc;

   if (argc < 2)
   {
      fprintf(stderr, "usage: %s <core> --rom PATH [--frames N]\n", argv[0]);
      return 2;
   }
   core = argv[1];
   for (i = 2; i < argc; i++)
   {
      if (!strcmp(argv[i], "--rom") && i + 1 < argc)
         rom = argv[++i];
      else if (!strcmp(argv[i], "--frames") && i + 1 < argc)
         frames = (unsigned)atoi(argv[++i]);
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
   if (!crc_ok || crc != FFYL_GOOD_CRC)
   {
      fprintf(stderr, "FAIL: %s is not the good Fight For Your Life dump "
              "(CRC $%08X, want $%08X)\n", rom, (unsigned)crc, FFYL_GOOD_CRC);
      return 1;
   }

   alarm(WALL_CLOCK_SECS);

   nargv[nargc++] = a_prog;
   nargv[nargc++] = (char *)core;
   nargv[nargc++] = (char *)rom;
   nargv[nargc++] = a_quiet;
   nargv[nargc++] = a_bios;
   nargv[nargc] = NULL;

   if (!harness_init_from_args(&cfg, nargc, nargv) || !harness_load_rom(&cfg))
   {
      fprintf(stderr, "FAIL: could not load %s\n", rom);
      return 1;
   }
   get_reg = (m68k_get_reg_fn)harness_dlsym(&cfg, "m68k_get_reg");
   if (!get_reg)
   {
      fprintf(stderr, "FAIL: m68k_get_reg not exported (build with TEST_EXPORTS=1)\n");
      harness_shutdown(&cfg);
      return 1;
   }

   for (f = 0; f < frames; f++)
   {
      harness_step(&cfg);
      pc = get_reg(NULL, M68K_REG_PC_INDEX) & 0xFFFFFFu;
      if (!handoff && pc >= 0x800000u && pc < 0xE00000u)
         handoff = f + 1;
      if (handoff && pc == BIOS_TRAP_PARK)
      {
         fprintf(stderr, "FAIL: 68K parked on the boot ROM trap pad $%06X on "
                 "frame %u (handoff at frame %u) -- the $F9A90 uninitialised "
                 "read sized a fill that wrecked the GPU job (#856)\n",
                 pc, f + 1, handoff);
         harness_shutdown(&cfg);
         return 1;
      }
   }

   harness_shutdown(&cfg);
   if (!handoff)
   {
      fprintf(stderr, "FAIL: boot ROM never handed off to the cart in %u "
              "frames (last PC $%06X)\n", frames, pc);
      return 1;
   }
   printf("PASS [BIOS]: handoff at frame %u, %u frames without a trap "
          "(last PC $%06X)\n", handoff, frames, pc);
   return 0;
}
