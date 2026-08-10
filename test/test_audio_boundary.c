/* test_audio_boundary.c -- Detect per-frame audio discontinuities.
 *
 * The failure mode this guards: a step in the sample stream at every
 * audio-batch boundary -- a 60 Hz impulse train heard as constant
 * crackle.  Shipped for months because the other two audio tests are
 * blind to it: it is not saturation (test_audio_clipping passed Atari
 * Karts at 0.000% saturated while it buzzed) and not silence
 * (test_audio_presence saw normal RMS).
 *
 * Metric: mean |s[i]-s[i-1]| over sample pairs adjacent to a batch
 * boundary vs. the mean over interior pairs.  A healthy stream is
 * statistically identical at the seam (ratio ~1); the 2026-08 bug
 * measured 20x on Atari Karts' attract mode with an absolute boundary
 * mean of 1601 against an interior of 80.
 *
 * FAIL requires both: ratio > RATIO_LIMIT and boundary mean >
 * ABS_FLOOR.  The floor keeps near-silent titles (interior ~0) from
 * failing on numerically meaningless ratios.
 *
 * Usage: ./test/test_audio_boundary <core> <rom> [--frames N] [--label TAG]
 *                                   [--ratio-limit R] [--quiet]
 * Exit:  0 PASS, 1 FAIL, 2 SKIP (ROM missing / no audio)
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <math.h>
#include <dlfcn.h>
#include "../libretro-common/include/libretro.h"

#define DEFAULT_FRAMES 1200
#define RATIO_LIMIT 4.0
#define ABS_FLOOR 150.0

static void *core_handle;
static void (*p_retro_init)(void);
static void (*p_retro_deinit)(void);
static void (*p_retro_set_environment)(retro_environment_t);
static void (*p_retro_set_video_refresh)(retro_video_refresh_t);
static void (*p_retro_set_audio_sample)(retro_audio_sample_t);
static void (*p_retro_set_audio_sample_batch)(retro_audio_sample_batch_t);
static void (*p_retro_set_input_poll)(retro_input_poll_t);
static void (*p_retro_set_input_state)(retro_input_state_t);
static bool (*p_retro_load_game)(const struct retro_game_info *);
static void (*p_retro_unload_game)(void);
static void (*p_retro_run)(void);

/* Accumulators: boundary-adjacent vs interior deltas, left channel.
 * "Boundary-adjacent" = the last delta inside a batch and the delta
 * across the seam into the next batch, so the metric is insensitive to
 * whether a bug lands the bad sample on the tail or the head. */
static double  boundary_sum = 0.0, interior_sum = 0.0;
static uint64_t boundary_cnt = 0, interior_cnt = 0;
static double  boundary_max = 0.0;
static int16_t prev_l = 0;
static int     have_prev = 0;
static uint64_t nonsilent = 0;

static size_t audio_batch(const int16_t *data, size_t frames)
{
   size_t i;
   for (i = 0; i < frames; i++)
   {
      int16_t l = data[i * 2];
      if (l > 32 || l < -32)
         nonsilent++;
      if (have_prev)
      {
         double d = fabs((double)l - (double)prev_l);
         /* First delta of a batch crosses the seam; last delta of a
          * batch is handled on the next call's first iteration only
          * for the seam itself, so also flag the final in-batch pair. */
         if (i == 0 || i == frames - 1)
         {
            boundary_sum += d;
            boundary_cnt++;
            if (d > boundary_max)
               boundary_max = d;
         }
         else
         {
            interior_sum += d;
            interior_cnt++;
         }
      }
      prev_l = l;
      have_prev = 1;
   }
   return frames;
}

static void video_refresh(const void *d, unsigned w, unsigned h, size_t p)
{ (void)d; (void)w; (void)h; (void)p; }
static void audio_sample(int16_t l, int16_t r) { (void)l; (void)r; }
static void input_poll(void) {}
static int16_t input_state(unsigned p, unsigned d, unsigned i, unsigned id)
{ (void)p; (void)d; (void)i; (void)id; return 0; }

static int log_quiet = 0;
static void log_printf(enum retro_log_level level, const char *fmt, ...)
{
   va_list ap;
   if (log_quiet || level < RETRO_LOG_WARN) return;
   va_start(ap, fmt);
   vfprintf(stderr, fmt, ap);
   va_end(ap);
}
static struct retro_log_callback log_cb = { log_printf };

static bool environment(unsigned cmd, void *data)
{
   switch (cmd)
   {
   case RETRO_ENVIRONMENT_GET_LOG_INTERFACE:
      *(struct retro_log_callback *)data = log_cb;
      return true;
   case RETRO_ENVIRONMENT_SET_PIXEL_FORMAT:
   case RETRO_ENVIRONMENT_SET_VARIABLES:
   case RETRO_ENVIRONMENT_SET_CORE_OPTIONS_V2:
   case RETRO_ENVIRONMENT_GET_VARIABLE_UPDATE:
   case RETRO_ENVIRONMENT_SET_MEMORY_MAPS:
   case RETRO_ENVIRONMENT_SET_INPUT_DESCRIPTORS:
   case RETRO_ENVIRONMENT_SET_SERIALIZATION_QUIRKS:
   case RETRO_ENVIRONMENT_SET_GEOMETRY:
   case RETRO_ENVIRONMENT_GET_CORE_OPTIONS_VERSION:
   case RETRO_ENVIRONMENT_GET_PREFERRED_HW_RENDER:
      return true;
   case RETRO_ENVIRONMENT_GET_SYSTEM_DIRECTORY:
   case RETRO_ENVIRONMENT_GET_SAVE_DIRECTORY:
      *(const char **)data = "/tmp";
      return true;
   case RETRO_ENVIRONMENT_GET_VARIABLE:
   {
      struct retro_variable *var = (struct retro_variable *)data;
      var->value = NULL;
      return false;
   }
   default:
      return false;
   }
}

static bool load_core(const char *path)
{
   core_handle = dlopen(path, RTLD_NOW);
   if (!core_handle)
   {
      fprintf(stderr, "dlopen(%s): %s\n", path, dlerror());
      return false;
   }
#define LOAD(sym) do { p_##sym = dlsym(core_handle, #sym); \
   if (!p_##sym) { fprintf(stderr, "Missing symbol: %s\n", #sym); return false; } } while (0)
   LOAD(retro_init);
   LOAD(retro_deinit);
   LOAD(retro_set_environment);
   LOAD(retro_set_video_refresh);
   LOAD(retro_set_audio_sample);
   LOAD(retro_set_audio_sample_batch);
   LOAD(retro_set_input_poll);
   LOAD(retro_set_input_state);
   LOAD(retro_load_game);
   LOAD(retro_unload_game);
   LOAD(retro_run);
#undef LOAD
   return true;
}

int main(int argc, char **argv)
{
   const char *core_path = NULL, *rom_path = NULL, *label = NULL;
   unsigned total_frames = DEFAULT_FRAMES;
   double ratio_limit = RATIO_LIMIT;
   uint8_t *rom_data;
   size_t rom_size;
   FILE *f;
   struct retro_game_info game;
   double interior_mean, boundary_mean, ratio;
   unsigned i;

   for (i = 1; i < (unsigned)argc; i++)
   {
      if (!strcmp(argv[i], "--frames") && i + 1 < (unsigned)argc)
         total_frames = (unsigned)atoi(argv[++i]);
      else if (!strcmp(argv[i], "--label") && i + 1 < (unsigned)argc)
         label = argv[++i];
      else if (!strcmp(argv[i], "--ratio-limit") && i + 1 < (unsigned)argc)
         ratio_limit = atof(argv[++i]);
      else if (!strcmp(argv[i], "--quiet"))
         log_quiet = 1;
      else if (argv[i][0] != '-' && !core_path)
         core_path = argv[i];
      else if (argv[i][0] != '-' && !rom_path)
         rom_path = argv[i];
   }
   if (!core_path || !rom_path)
   {
      fprintf(stderr, "Usage: %s <core> <rom> [--frames N] [--label TAG] "
              "[--ratio-limit R] [--quiet]\n", argv[0]);
      return 2;
   }
   if (!label)
      label = rom_path;

   printf("\n=== Audio boundary check: %s ===\n", label);

   f = fopen(rom_path, "rb");
   if (!f)
   {
      printf("  SKIP: ROM not found at %s\n", rom_path);
      return 2;
   }
   fseek(f, 0, SEEK_END);
   rom_size = (size_t)ftell(f);
   fseek(f, 0, SEEK_SET);
   rom_data = (uint8_t *)malloc(rom_size);
   if (!rom_data || fread(rom_data, 1, rom_size, f) != rom_size)
   {
      fclose(f);
      free(rom_data);
      printf("  SKIP: could not read ROM\n");
      return 2;
   }
   fclose(f);

   if (!load_core(core_path))
   {
      free(rom_data);
      return 1;
   }

   p_retro_set_environment(environment);
   p_retro_init();
   p_retro_set_video_refresh(video_refresh);
   p_retro_set_audio_sample(audio_sample);
   p_retro_set_audio_sample_batch(audio_batch);
   p_retro_set_input_poll(input_poll);
   p_retro_set_input_state(input_state);

   memset(&game, 0, sizeof(game));
   game.path = rom_path;
   game.data = rom_data;
   game.size = rom_size;
   if (!p_retro_load_game(&game))
   {
      printf("  FAIL: retro_load_game rejected ROM\n");
      free(rom_data);
      return 1;
   }

   for (i = 0; i < total_frames; i++)
      p_retro_run();

   p_retro_unload_game();
   p_retro_deinit();
   dlclose(core_handle);
   free(rom_data);

   if (interior_cnt < 10000 || nonsilent < 4800)
   {
      printf("  SKIP: not enough audio to judge (%llu interior deltas, "
             "%llu non-silent samples)\n",
             (unsigned long long)interior_cnt,
             (unsigned long long)nonsilent);
      return 2;
   }

   interior_mean = interior_sum / (double)interior_cnt;
   boundary_mean = boundary_sum / (double)boundary_cnt;
   ratio = (interior_mean > 0.0) ? boundary_mean / interior_mean : 0.0;

   printf("  Frames: %u\n", total_frames);
   printf("  Interior mean delta:  %.1f (%llu pairs)\n",
          interior_mean, (unsigned long long)interior_cnt);
   printf("  Boundary mean delta:  %.1f (%llu pairs, max %.0f)\n",
          boundary_mean, (unsigned long long)boundary_cnt, boundary_max);
   printf("  Ratio: %.2fx (limit %.1fx, abs floor %.0f)\n",
          ratio, ratio_limit, ABS_FLOOR);

   if (ratio > ratio_limit && boundary_mean > ABS_FLOOR)
   {
      printf("  FAIL: batch boundaries are discontinuous -- a per-frame "
             "step (60 Hz crackle).  See src/jerry/dac.c word-strobe "
             "capture + continuous ring cursors.\n");
      return 1;
   }
   printf("  PASS: batch seams statistically match the interior\n");
   return 0;
}
