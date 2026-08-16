/* test/tools/blit_memo_verify.c -- blit-memoization soundness gate.
 *
 * Runs one ROM with `virtualjaguar_blit_memo=verify`, which never
 * skips: every blit the memo WOULD have skipped is executed live and
 * its write log + post-launch state compared against what the memo
 * would have replayed.  Any mismatch means a skip would have changed
 * emulation, i.e. that title must not carry the titledb tag.
 *
 * This is the gate to run before adding `virtualjaguar_blit_memo` to a
 * title's row in src/core/titledb.c, and the corpus sweep
 * (test/tools/blit_memo_sweep.sh) is just this tool over every
 * cartridge.
 *
 * Exit codes -- a zero-divergence result is only meaningful when the
 * checker actually ran, so "never verified" is NOT a pass:
 *   0  clean   : runs >= --min-runs and fails == 0
 *   1  DIVERGE : fails > 0                    (title is unsafe)
 *   3  thin    : fails == 0 but runs < --min-runs (no verdict; the
 *                title never repeated a blit stream in this window --
 *                run longer or script input that reaches gameplay)
 *   2  harness/ROM error
 *
 * Build:
 *   cc -O2 -Wall -std=c99 -I. -I./test/harness -I./libretro-common/include \
 *      -o test/tools/blit_memo_verify test/tools/blit_memo_verify.c \
 *      test/harness/harness.c -ldl -lm
 *
 * Usage:
 *   blit_memo_verify <core> <rom> [--frames N] [--min-runs N] [--json]
 *   ...plus every standard harness flag (--press, --option, --bios).
 */
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>

#include "harness.h"

typedef struct
{
   const char *name;
   uint32_t   *ptr;
} bm_counter;

int main(int argc, char **argv)
{
   harness_config cfg = HARNESS_CONFIG_DEFAULT;
   harness_result res;
   uint32_t runs = 0, fails = 0, misses = 0, dirty = 0, through = 0;
   unsigned long min_runs = 1000;
   int i, json = 0, rc;
   char detail[256];
   bm_counter counters[5];

   cfg.frames = 3000;

   /* Peel off our own flags before the harness parser sees argv. */
   for (i = 1; i < argc; i++)
   {
      if (!strcmp(argv[i], "--min-runs") && i + 1 < argc)
      {
         min_runs = strtoul(argv[i + 1], NULL, 0);
         memmove(&argv[i], &argv[i + 2], (argc - i - 2) * sizeof(char *));
         argc -= 2;
         i--;
      }
      else if (!strcmp(argv[i], "--json"))
         json = 1;   /* harness parses this too; just note it */
   }

   if (!harness_init_from_args(&cfg, argc, argv))
      return 2;
   if (!cfg.rom_path)
   {
      fprintf(stderr,
              "usage: blit_memo_verify <core> <rom> [--frames N] "
              "[--min-runs N] [--json] [harness flags]\n");
      return 2;
   }

   /* Force verify mode regardless of what the per-title DB says: a
    * title already tagged `enabled` must still be re-checkable. */
   harness_set_option(&cfg, "virtualjaguar_blit_memo", "verify");

   if (!harness_load_rom(&cfg))
      return 2;

   counters[0].name = "blitMemoVerifyRuns";  counters[0].ptr = &runs;
   counters[1].name = "blitMemoVerifyFails"; counters[1].ptr = &fails;
   counters[2].name = "blitMemoMisses";      counters[2].ptr = &misses;
   counters[3].name = "blitMemoDirty";       counters[3].ptr = &dirty;
   counters[4].name = "blitMemoExecThrough"; counters[4].ptr = &through;

   harness_run(&cfg);

   for (i = 0; i < 5; i++)
   {
      uint32_t *p = (uint32_t *)harness_dlsym(&cfg, counters[i].name);
      if (!p)
      {
         fprintf(stderr, "blit_memo_verify: core does not export %s "
                         "(need a TEST_EXPORTS=1 build)\n",
                 counters[i].name);
         harness_shutdown(&cfg);
         return 2;
      }
      *counters[i].ptr = *p;
   }

   if (fails > 0)
   {
      rc = 1;
      res.status = "FAIL";
   }
   else if (runs < min_runs)
   {
      rc = 3;
      res.status = "SKIP";
   }
   else
   {
      rc = 0;
      res.status = "PASS";
   }

   snprintf(detail, sizeof(detail),
            "verify runs=%u fails=%u (misses=%u dirty=%u exec_through=%u)%s",
            runs, fails, misses, dirty, through,
            rc == 3 ? " -- too few checks for a verdict" : "");

   res.name   = "blit_memo_verify";
   res.detail = detail;
   harness_report(&cfg, &res, 1);
   if (!json)
      printf("BMVERIFY runs=%u fails=%u misses=%u dirty=%u through=%u rc=%d\n",
             runs, fails, misses, dirty, through, rc);

   harness_shutdown(&cfg);
   return rc;
}
