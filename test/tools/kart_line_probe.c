/* test/tools/kart_line_probe.c -- issue #1085: Atari Karts AI karts must
 * follow the racing line.
 *
 * The game keeps six karts at $4528 + n*$18 (X in the high word, Y in the
 * low word of one long) and each kart's point on the racing line at
 * $4628 + n*$10 (same packing).  Its GPU kart code seeds every kart's
 * steering from the low bits of TOM HC ($F00004).  While HC came from a
 * per-read counter that restarted every half-line, each kart read the
 * same value every field, steered with a fixed bias, and left the track:
 * mean kart-to-line distance over an attract-demo race measured
 * 1550-4046 (62-100% of samples more than 800 away).  With HC derived
 * from elapsed time it measures 35-60.
 *
 * Runs the attract demo with no input, finds each race (kart 0 moving,
 * split on gaps of more than 120 fields), and fails if a race of at least
 * 300 fields has a mean kart-to-line distance above --max-mean, or if no
 * race was seen at all (a demo that never starts must not pass).
 *
 * Build:
 *   cc -O2 -Wall -std=c99 -I./libretro-common/include \
 *      -o test/tools/kart_line_probe test/tools/kart_line_probe.c \
 *      test/harness/harness.c -ldl -lm
 * Run (needs a TEST_EXPORTS=1 core):
 *   ./test/tools/kart_line_probe ./virtualjaguar_libretro.dylib \
 *      "Atari Karts (1995).jag" [--frames 4200] [--max-mean 400]
 */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../harness/harness.h"

#define KARTS      6
#define MIN_RACE   300
#define RACE_GAP   120

typedef void *(*mem_fn)(unsigned);

static mem_fn   p_mem;
static double   g_max_mean = 400.0;
static uint32_t g_prev_x0;
static int      g_have_prev;
static unsigned g_last_move;
static unsigned g_race_start;
static unsigned g_race_fields;
static double   g_race_sum;
static unsigned g_races;
static int      g_failed;

static uint32_t rd32(const uint8_t *ram, uint32_t a)
{
   return ((uint32_t)ram[a] << 24) | ((uint32_t)ram[a + 1] << 16)
        | ((uint32_t)ram[a + 2] << 8) | (uint32_t)ram[a + 3];
}

static void end_race(void)
{
   double mean;
   if (g_race_fields < MIN_RACE)
      return;
   mean = g_race_sum / ((double)g_race_fields * KARTS);
   g_races++;
   printf("race fields %u-%u: mean kart-to-line distance %.0f (max %.0f)\n",
          g_race_start, g_race_start + g_race_fields - 1, mean, g_max_mean);
   if (mean > g_max_mean)
      g_failed = 1;
}

static bool on_frame(void *userdata, unsigned frame)
{
   const uint8_t *ram = (const uint8_t *)p_mem(2);
   uint32_t x0 = rd32(ram, 0x4528) >> 16;
   int k;
   (void)userdata;

   if (g_have_prev && x0 != g_prev_x0)
   {
      if (!g_race_fields || frame - g_last_move > RACE_GAP)
      {
         end_race();
         g_race_start  = frame;
         g_race_fields = 0;
         g_race_sum    = 0.0;
      }
      g_last_move = frame;
      for (k = 0; k < KARTS; k++)
      {
         uint32_t p = rd32(ram, 0x4528 + 0x18 * k);
         uint32_t l = rd32(ram, 0x4628 + 0x10 * k);
         double dx = (double)(p >> 16) - (double)(l >> 16);
         double dy = (double)(p & 0xFFFF) - (double)(l & 0xFFFF);
         g_race_sum += sqrt(dx * dx + dy * dy);
      }
      g_race_fields++;
   }
   g_prev_x0   = x0;
   g_have_prev = 1;
   return true;
}

int main(int argc, char **argv)
{
   harness_config cfg;
   int i;

   memset(&cfg, 0, sizeof(cfg));
   for (i = 1; i < argc; i++)
   {
      if (!strcmp(argv[i], "--max-mean") && i + 1 < argc)
         g_max_mean = atof(argv[++i]);
   }

   cfg.frames         = 4200;
   cfg.quiet          = 1;
   cfg.frame_callback = on_frame;
   if (!harness_init_from_args(&cfg, argc, argv))
      return 1;
   if (!harness_load_rom(&cfg))
      return 1;

   p_mem = (mem_fn)harness_dlsym(&cfg, "retro_get_memory_data");
   if (!p_mem || !p_mem(2))
   {
      fprintf(stderr, "kart_line_probe: no system RAM from the core\n");
      return 1;
   }

   harness_run(&cfg);
   end_race();
   harness_shutdown(&cfg);

   if (!g_races)
   {
      printf("FAIL: no attract-demo race of %u+ fields in %u fields\n",
             MIN_RACE, cfg.frames);
      return 1;
   }
   printf("%s: %u race(s) checked\n", g_failed ? "FAIL" : "PASS", g_races);
   return g_failed ? 1 : 0;
}
