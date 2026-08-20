/*
 * test/tools/teamtap_rom_probe.c -- ask a real ROM whether it sees the tap.
 *
 * WHAT THIS IS FOR
 * ================
 * test/tools/joymatrix_identity.c proves the Team Tap decode (#513) at the
 * register level: the right row codes reach the right sockets, the detect
 * bit answers both ways, and nothing moves with no tap selected.  What it
 * cannot prove is that a program written by someone who owned the hardware
 * agrees.
 *
 * The Joypad-TeamTap Tester (Matthias Domin, 2000, PD) is that program.  It
 * implements TR10's detection sequence verbatim -- at file offsets 0x272
 * and 0x2F2 --
 *
 *     33FC 81FA 00F14000    move.w  #$81FA,$F14000  ; port 1 socket 3 row 1
 *     3039 00F14002         move.w  $F14002,d0      ; read JOYBUTS
 *     0800 0000             btst    #0,d0           ; test B0
 *     6632                  bne.s   ...             ; set -> no adaptor
 *     0039 0001 000068AA    ori.b   #$01,$68AA      ; clear -> tap on port 1
 *
 * and the port-2 twin with #$815F / btst #2 / ori.b #$02.  So byte $68AA of
 * main RAM is THE ROM'S OWN VERDICT: bit 0 = "Team Tap on port 1", bit 1 =
 * "Team Tap on port 2".
 *
 * Reading that byte is a far stronger check than looking at the screen, and
 * it is the reason this file exists rather than a screenshot baseline: the
 * framebuffer would only tell us the tester drew something.
 *
 * WHY IT IS NOT IN `make test`
 * ============================
 * The ROM lives in the private corpus (test/roms/private, gitignored), so
 * CI has no copy.  Run it by hand; it exits 77 (skip, never a silent 0)
 * when the ROM is absent.
 *
 * USAGE
 *   make TEST_EXPORTS=1 test/tools/teamtap_rom_probe
 *   ./test/tools/teamtap_rom_probe ./virtualjaguar_libretro.dylib \
 *       "test/roms/private/ROMS/Public Domain/Joypad-TeamTap Tester by Matthias Domin (2000) (PD).jag" \
 *       --expect 0            # no tap selected
 *   ... --option virtualjaguar_p1_device=teamtap --expect 1
 *   ... --option virtualjaguar_p1_device=teamtap \
 *       --option virtualjaguar_p2_device=teamtap --expect 3
 *
 * Exit codes: 0 = matched --expect (or no --expect given), 1 = mismatch,
 * 2 = harness error, 77 = ROM absent.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#include "../harness/harness.h"

/* The tester's own flag byte -- see the disassembly above. */
#define TESTER_VERDICT_ADDR 0x68AA
#define VERDICT_TAP_PORT1   0x01
#define VERDICT_TAP_PORT2   0x02

int main(int argc, char **argv)
{
   harness_config cfg = HARNESS_CONFIG_DEFAULT;
   harness_result results[1];
   char detail[192];
   uint8_t *ram;
   uint8_t  verdict;
   int      expect = -1;
   int      ok;
   int      i;

   /* The detection probe runs during the tester's init, long before 300
    * fields; the default is generous rather than tuned. */
   cfg.frames = 300;

   for (i = 1; i < argc; i++)
   {
      if (strcmp(argv[i], "--expect") == 0 && i + 1 < argc)
         expect = atoi(argv[++i]);
   }

   if (!harness_init_from_args(&cfg, argc, argv))
      return 2;

   if (!cfg.rom_path)
   {
      fprintf(stderr,
              "teamtap_rom_probe: no ROM given.  This needs the "
              "Joypad-TeamTap Tester from the private corpus.\n");
      harness_shutdown(&cfg);
      return 77;
   }

   {
      FILE *f = fopen(cfg.rom_path, "rb");
      if (!f)
      {
         fprintf(stderr,
                 "==== SKIP (teamtap_rom_probe): ROM '%s' not found ====\n"
                 "     The Joypad-TeamTap Tester is in the private corpus,\n"
                 "     which CI does not have.  Exiting 77, NOT 0.\n",
                 cfg.rom_path);
         harness_shutdown(&cfg);
         return 77;
      }
      fclose(f);
   }

   if (!harness_load_rom(&cfg))
   {
      harness_shutdown(&cfg);
      return 2;
   }

   /* jaguarMainRAM is a POINTER into jagMemSpace, not an array
    * (src/core/vjag_memory.c), so the dlsym result has to be dereferenced.
    * Taking it as a uint8_t* directly reads the pointer's own bytes and
    * reports a plausible-looking 0x00 forever -- which is exactly how
    * this file first "proved" the tap was not detected while the tester
    * was printing "TeamTap Found!" on screen. */
   {
      uint8_t **pram = (uint8_t **)harness_dlsym(&cfg, "jaguarMainRAM");

      if (!pram || !*pram)
      {
         fprintf(stderr,
                 "teamtap_rom_probe: jaguarMainRAM is not in the test ABI.  "
                 "Build with TEST_EXPORTS=1.\n");
         harness_shutdown(&cfg);
         return 2;
      }
      ram = *pram;
   }

   harness_run(&cfg);

   verdict = ram[TESTER_VERDICT_ADDR];
   ok      = (expect < 0) || (verdict == (uint8_t)expect);

   sprintf(detail,
           "$%04X = 0x%02X -- tester reports tap on port 1: %s, port 2: %s%s",
           (unsigned)TESTER_VERDICT_ADDR, verdict,
           (verdict & VERDICT_TAP_PORT1) ? "YES" : "no",
           (verdict & VERDICT_TAP_PORT2) ? "YES" : "no",
           (expect < 0) ? "" : (ok ? " (matches --expect)"
                                   : " (DOES NOT match --expect)"));

   results[0].status = ok ? "PASS" : "FAIL";
   results[0].name   = "teamtap_rom_verdict";
   results[0].detail = detail;

   harness_report(&cfg, results, 1);
   harness_shutdown(&cfg);

   return ok ? 0 : 1;
}
