/*
 * test/tools/test_flipout_851.c -- cart mirroring regression (issue #851).
 *
 * Flip Out (a 2 MiB cart) executes "cmpa.l $A02000.l,a2 / blt / illegal" at
 * $11EEAA.  A 2 MiB cart repeats at $A00000 on hardware, so the read returns
 * the cart's own word at $2000 and the branch is taken.  The core used to
 * return 0 there, fell into the ILLEGAL at $11EEB4, and in HLE then looped
 * ILLEGAL / RTE (vector 4 is the $404 RTE stub) for the rest of the run, with
 * the crash watchdog silent because the loop never escapes.
 *
 * Steps the game one frame at a time and fails if the 68K PC is ever sampled
 * on the ILLEGAL ($11EEB4), on the HLE vector-4 stub ($404 -- nothing else
 * in this game executes there), or, with --bios, on the boot ROM's exception
 * landing pad ($E005DC; the BIOS run traps near frame 825).  Needs the private ROM; the Makefile skips
 * (recorded in the skip ledger) when it is absent.
 *
 * Usage:  test_flipout_851 <core> --rom PATH [--frames N] [--bios]
 *
 * Build:  cc -O2 -Wall -std=c99 -I. -I./src -I./libretro-common/include \
 *           -o test/tools/test_flipout_851 test/tools/test_flipout_851.c \
 *           test/harness/harness.c -ldl -lm
 */

#define _DEFAULT_SOURCE 1

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "../harness/harness.h"

#define ILLEGAL_PC         0x0011EEB4u
#define HLE_VEC4_STUB      0x00000404u
#define BIOS_TRAP_PARK     0x00E005DCu   /* boot ROM: every exception vector lands here */
#define M68K_REG_PC_INDEX  16          /* m68kinterface.h: D0-D7, A0-A7, PC */
#define WALL_CLOCK_SECS    1800u

typedef unsigned int (*m68k_get_reg_fn)(void *, int);

int main(int argc, char **argv)
{
   static char a_prog[] = "test_flipout_851";
   static char a_quiet[] = "--quiet";
   static char a_bios[] = "--bios";
   const char *core, *rom = NULL;
   int bios = 0, i, nargc = 0;
   unsigned frames = 1000, f;
   char *nargv[8];
   harness_config cfg = HARNESS_CONFIG_DEFAULT;
   m68k_get_reg_fn get_reg;
   unsigned pc = 0;

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
      if (pc == ILLEGAL_PC || (!bios && pc == HLE_VEC4_STUB)
          || (bios && pc == BIOS_TRAP_PARK))
      {
         fprintf(stderr, "FAIL [%s]: 68K sampled at $%06X on frame %u -- the "
                 "cmpa.l $A02000 read hit an unmirrored cart window and "
                 "trapped (#851)\n", bios ? "BIOS" : "HLE", pc, f + 1);
         harness_shutdown(&cfg);
         return 1;
      }
   }

   harness_shutdown(&cfg);
   printf("PASS [%s]: %u frames, 68K never parked on the $11EEB4 ILLEGAL (last PC $%06X)\n",
          bios ? "BIOS" : "HLE", frames, pc);
   return 0;
}
