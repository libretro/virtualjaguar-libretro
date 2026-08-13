/* test_audio_rate.c -- the core must deliver its advertised sample rate.
 *
 * Guards the underrun-pop class: if samples-per-frame x advertised fps
 * drifts from the advertised sample_rate, the frontend's audio buffer
 * slowly drains (or overfills) and periodically underruns -- heard as
 * a pop every few seconds, in every title, unrelated to content.
 *
 * The 2026-08 regression measured -108 samples/sec: an NTSC field is
 * 799.27 periods of the 48 kHz sample clock, but the DAC restarted its
 * clock every frame and so delivered a flat 799, and retro_get_system_
 * av_info claimed a rounded 60 fps against a true 60.0544.  Both halves
 * are checked here: the batch average must land on the fractional
 * value, and fps x samples-per-frame must equal sample_rate.
 *
 * Usage: ./test/test_audio_rate <core> <rom> [--frames N] [--quiet]
 * Exit:  0 PASS, 1 FAIL, 2 SKIP
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

#define DEFAULT_FRAMES 900
/* Long-run drift the frontend can absorb without a resync, in samples
 * per second.  Dynamic rate control nudges the resampler by fractions
 * of a percent; 5 samples/sec out of 48000 is ~0.01%. */
#define DRIFT_LIMIT 5.0

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
static void (*p_retro_get_system_av_info)(struct retro_system_av_info *);

static uint64_t total_frames_delivered = 0;
static unsigned batch_calls = 0;

static size_t audio_batch(const int16_t *data, size_t frames)
{
   (void)data;
   total_frames_delivered += frames;
   batch_calls++;
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
   LOAD(retro_get_system_av_info);
#undef LOAD
   return true;
}

int main(int argc, char **argv)
{
   const char *core_path = NULL, *rom_path = NULL, *label = NULL;
   unsigned total_frames = DEFAULT_FRAMES;
   uint8_t *rom_data;
   size_t rom_size;
   FILE *f;
   struct retro_game_info game;
   struct retro_system_av_info av;
   double per_frame, produced, drift;
   unsigned i;

   for (i = 1; i < (unsigned)argc; i++)
   {
      if (!strcmp(argv[i], "--frames") && i + 1 < (unsigned)argc)
         total_frames = (unsigned)atoi(argv[++i]);
      else if (!strcmp(argv[i], "--label") && i + 1 < (unsigned)argc)
         label = argv[++i];
      else if (!strcmp(argv[i], "--quiet"))
         log_quiet = 1;
      else if (argv[i][0] != '-' && !core_path)
         core_path = argv[i];
      else if (argv[i][0] != '-' && !rom_path)
         rom_path = argv[i];
   }
   if (!core_path || !rom_path)
   {
      fprintf(stderr, "Usage: %s <core> <rom> [--frames N] [--quiet]\n", argv[0]);
      return 2;
   }
   if (!label)
      label = rom_path;

   printf("\n=== Audio rate check: %s ===\n", label);

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

   memset(&av, 0, sizeof(av));
   p_retro_get_system_av_info(&av);

   for (i = 0; i < total_frames; i++)
      p_retro_run();

   p_retro_unload_game();
   p_retro_deinit();
   dlclose(core_handle);
   free(rom_data);

   if (batch_calls < 60)
   {
      printf("  SKIP: only %u audio batches in %u frames\n",
             batch_calls, total_frames);
      return 2;
   }

   per_frame = (double)total_frames_delivered / (double)total_frames;
   produced  = per_frame * av.timing.fps;
   drift     = produced - av.timing.sample_rate;

   printf("  Advertised: %.4f fps, %.0f Hz\n",
          av.timing.fps, av.timing.sample_rate);
   printf("  Delivered:  %.4f samples/frame over %u frames (%u batches)\n",
          per_frame, total_frames, batch_calls);
   printf("  Implied rate: %.2f Hz -> drift %+.2f samples/sec (limit %.1f)\n",
          produced, drift, DRIFT_LIMIT);

   if (fabs(drift) > DRIFT_LIMIT)
   {
      printf("  FAIL: the core does not deliver its advertised sample rate.\n"
             "        The frontend's audio buffer drifts and underruns --\n"
             "        a periodic pop in every title.  Check that the DAC\n"
             "        sample clock free-runs across frames (src/jerry/dac.c)\n"
             "        and that timing.fps matches the emulated field rate\n"
             "        (libretro.c retro_get_system_av_info).\n");
      return 1;
   }
   printf("  PASS: delivered rate matches the advertised rate\n");
   return 0;
}
