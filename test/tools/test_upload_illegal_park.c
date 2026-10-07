/*
 * test/tools/test_upload_illegal_park.c -- uploaded-executable ILLEGAL park
 * (issue #800).
 *
 * Headerless BJL/Alpine-style demos (raw binaries, .abs) end their 68K main
 * with ILLEGAL -- "drop into the debugger" -- and leave the GPU/OP running
 * the show.  These images carry no vector table, and HLE used to install
 * none for them, so the trap jumped through a zero vector 4 to $0, ran the
 * vector table as code and walked the stack down through TOM.  There the
 * 68K's own exception frames landed on the blitter's B_COUNT/B_CMD mirror
 * and started 1bpp phrase-mode blits that real hardware never finishes, so
 * the ACCURATE blitter hung retro_run forever (DEMO1 (bin), DEMO1B,
 * Ladybug Demo).  JaguarReset now parks vector 4 at a BRA.S * loop at
 * $1000, as file.c always did for Alpine/JagServer images.
 *
 * Synthetic case (no ROM needed; always runs): writes a 48-byte raw binary
 * that the loader infers to $802000 (the common BJL startup idiom plus two
 * absolute JSRs), sets SP, calls a subroutine and executes ILLEGAL.  Asserts
 * the 68K is parked at $1000 after the run, and again after retro_reset()
 * (whose RAM clear used to wipe a load-time-only guard).
 *
 * ROM case (--rom PATH): runs PATH for --frames frames under the accurate
 * blitter and asserts the run completes with the 68K parked at $1000.  The
 * DEMO1B regression: before the fix this never returned.
 *
 * Both cases force the ACCURATE blitter (the harness default is FAST, which
 * shrugs the garbage blits off and hides the hang) and arm a wall-clock
 * alarm, so a regression fails the suite instead of hanging it.
 *
 * Build:  cc -O2 -Wall -std=c99 -I. -I./src -I./libretro-common/include \
 *           -o test/tools/test_upload_illegal_park \
 *           test/tools/test_upload_illegal_park.c test/harness/harness.c -ldl -lm
 * Run:    test_upload_illegal_park <core> [--rom PATH] [--frames N]
 */

#define _DEFAULT_SOURCE 1

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "../harness/harness.h"

#define PARK_PC            0x00001000u
#define M68K_REG_PC_INDEX  16          /* m68kinterface.h: D0-D7, A0-A7, PC */
#define WALL_CLOCK_SECS    300u

typedef unsigned int (*m68k_get_reg_fn)(void *, int);
typedef void (*retro_reset_fn)(void);

/* $802000: move.l #$00070007,$F0210C   ; G_END big-endian (BJL idiom)
 * $80200A: movea.l #$200000,a7
 * $802010: jsr $802020
 * $802016: jsr $802020
 * $80201C: illegal
 * $80201E: nop
 * $802020: rts                          ; padded with NOPs to 48 bytes */
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

static int run_case(const char *core, const char *rom, const char *frames,
                    int check_reset, const char *label)
{
   /* Writable copies: harness_init_from_args may split K=V in place. */
   static char a_prog[] = "test_upload_illegal_park";
   static char a_frames_flag[] = "--frames";
   static char a_option_flag[] = "--option";
   static char a_option[] = "virtualjaguar_usefastblitter=disabled";
   static char a_quiet[] = "--quiet";
   char *argv[10];
   int argc = 0;
   harness_config cfg = HARNESS_CONFIG_DEFAULT;
   m68k_get_reg_fn get_reg;
   retro_reset_fn do_reset;
   unsigned pc;
   int fails = 0;

   argv[argc++] = a_prog;
   argv[argc++] = (char *)core;
   argv[argc++] = (char *)rom;
   argv[argc++] = a_frames_flag;
   argv[argc++] = (char *)frames;
   argv[argc++] = a_option_flag;
   argv[argc++] = a_option;
   argv[argc++] = a_quiet;
   argv[argc] = NULL;

   if (!harness_init_from_args(&cfg, argc, argv))
   {
      fprintf(stderr, "FAIL [%s]: harness init\n", label);
      return 1;
   }
   if (!harness_load_rom(&cfg))
   {
      fprintf(stderr, "FAIL [%s]: could not load %s\n", label, rom);
      harness_shutdown(&cfg);
      return 1;
   }
   get_reg = (m68k_get_reg_fn)harness_dlsym(&cfg, "m68k_get_reg");
   do_reset = (retro_reset_fn)harness_dlsym(&cfg, "retro_reset");
   if (!get_reg || !do_reset)
   {
      fprintf(stderr, "FAIL [%s]: m68k_get_reg/retro_reset not exported "
              "(build the core with TEST_EXPORTS=1)\n", label);
      harness_shutdown(&cfg);
      return 1;
   }

   harness_run(&cfg);
   pc = get_reg(NULL, M68K_REG_PC_INDEX) & 0xFFFFFFu;
   if (pc != PARK_PC)
   {
      fprintf(stderr, "FAIL [%s]: 68K PC $%06X after %u frames, want the "
              "ILLEGAL park at $%06X\n", label, pc, cfg.current_frame, PARK_PC);
      fails++;
   }

   if (check_reset)
   {
      do_reset();
      harness_run(&cfg);
      pc = get_reg(NULL, M68K_REG_PC_INDEX) & 0xFFFFFFu;
      if (pc != PARK_PC)
      {
         fprintf(stderr, "FAIL [%s]: after retro_reset 68K PC $%06X, want "
                 "$%06X (reset wiped the vector-4 park)\n", label, pc, PARK_PC);
         fails++;
      }
   }

   harness_shutdown(&cfg);
   if (!fails)
      printf("PASS [%s]: 68K parked at $%06X%s\n", label, PARK_PC,
             check_reset ? " (and again after retro_reset)" : "");
   return fails;
}

int main(int argc, char **argv)
{
   const char *core;
   const char *rom = NULL;
   const char *frames = "600";
   char path[] = "/tmp/vj_upload_park_XXXXXX.jag";
   int fd, i, fails = 0;

   if (argc < 2)
   {
      fprintf(stderr, "usage: %s <core> [--rom PATH] [--frames N]\n", argv[0]);
      return 2;
   }
   core = argv[1];
   for (i = 2; i < argc; i++)
   {
      if (!strcmp(argv[i], "--rom") && i + 1 < argc)
         rom = argv[++i];
      else if (!strcmp(argv[i], "--frames") && i + 1 < argc)
         frames = argv[++i];
      else
      {
         fprintf(stderr, "unknown argument: %s\n", argv[i]);
         return 2;
      }
   }

   /* A regression is a hang inside one retro_run; SIGALRM's default
    * action kills the process, which the suite reports as a failure. */
   alarm(WALL_CLOCK_SECS);

   if (rom)
      return run_case(core, rom, frames, 0, rom) ? 1 : 0;

   fd = mkstemps(path, 4);
   if (fd < 0)
   {
      perror("mkstemps");
      return 1;
   }
   if (write(fd, synth_image, sizeof(synth_image)) != (ssize_t)sizeof(synth_image))
   {
      perror("write");
      close(fd);
      unlink(path);
      return 1;
   }
   close(fd);

   fails = run_case(core, path, "60", 1, "synthetic raw binary @ $802000");
   unlink(path);
   return fails ? 1 : 0;
}
