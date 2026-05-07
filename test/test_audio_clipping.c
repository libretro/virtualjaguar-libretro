/* test_audio_clipping.c -- Detect sustained audio clipping at boot.
 *
 * Some titles (Skyhammer, Iron Soldier 2) emit audio that pegs at
 * +/-32767 for long stretches — clearly broken, not artistic intent.
 * This test loads a ROM, runs N frames, and asserts that the captured
 * S16 stereo stream does not contain sustained saturation.
 *
 * Build: see Makefile target test/test_audio_clipping
 * Usage: ./test/test_audio_clipping <core> <rom> [--bios] [--frames N]
 *        ./test/test_audio_clipping <core> --self-test
 *
 * Exit:  0 PASS, 1 FAIL (clipping detected), 2 SKIP (ROM missing)
 *
 * If <rom> is omitted or missing, exits 2 (skip) so CI without private
 * ROMs reports a clean skip instead of a failure.
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

#define WINDOW_START_FRAME 60     /* skip first 1s — boot silence/whoosh */
#define DEFAULT_TOTAL_FRAMES 300  /* 5s at 60fps */
#define SATURATION_LEVEL 32760    /* |s| >= this counts as saturated */
#define SATURATION_DENSITY_PCT 5.0  /* >5% of samples saturated => clipping */
/* >= SATURATION_RUN_SAMPLES consecutive saturated stereo frames => clipping.
 * 500 frames at 48 kHz output = ~10.4 ms; allows the brief DSP engine startup
 * transient (~312 frames / 6.5 ms) without flagging it as a real clip run. */
#define SATURATION_RUN_SAMPLES 500
#define LOUDNESS_RMS_THRESHOLD 20000.0  /* sustained RMS above this is impossible for clean music */
#define LOUDNESS_FRAME_FRACTION 0.30    /* > this fraction of post-onset frames at hot RMS => clipping */

/* ---------- libretro symbols ---------- */
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

/* ---------- capture state ---------- */
static unsigned current_frame = 0;
static uint64_t total_samples = 0;            /* across both channels */
static uint64_t saturated_samples = 0;
static unsigned longest_saturated_run = 0;
static unsigned current_saturated_run = 0;
static unsigned hot_frames = 0;               /* per-frame RMS above threshold */
static int first_audio_frame = -1;
static double window_sum_sq = 0;
static uint64_t window_sample_count = 0;
static unsigned active_window_frames = 0;

/* ---------- core libretro callback shims ---------- */
static void video_refresh(const void *d, unsigned w, unsigned h, size_t p)
{ (void)d; (void)w; (void)h; (void)p; }

static void audio_sample(int16_t l, int16_t r) { (void)l; (void)r; }

static size_t audio_batch(const int16_t *data, size_t frames)
{
   size_t i;
   bool in_window = (current_frame >= WINDOW_START_FRAME);
   double frame_sum_sq_l = 0, frame_sum_sq_r = 0;
   int16_t peak_l = 0, peak_r = 0;

   for (i = 0; i < frames; i++)
   {
      int16_t l = data[i * 2];
      int16_t r = data[i * 2 + 1];
      int16_t abs_l = (l < 0) ? -l : l;
      int16_t abs_r = (r < 0) ? -r : r;
      bool sat_l = abs_l >= SATURATION_LEVEL;
      bool sat_r = abs_r >= SATURATION_LEVEL;

      if (abs_l > peak_l) peak_l = abs_l;
      if (abs_r > peak_r) peak_r = abs_r;

      if (in_window)
      {
         total_samples += 2;
         if (sat_l) saturated_samples++;
         if (sat_r) saturated_samples++;

         /* Track longest run on either channel — clipping is mono-channel-friendly */
         if (sat_l || sat_r)
         {
            current_saturated_run++;
            if (current_saturated_run > longest_saturated_run)
               longest_saturated_run = current_saturated_run;
         }
         else
         {
            current_saturated_run = 0;
         }

         frame_sum_sq_l += (double)l * l;
         frame_sum_sq_r += (double)r * r;
      }

      if (first_audio_frame < 0 && (abs_l > 32 || abs_r > 32))
         first_audio_frame = (int)current_frame;
   }

   if (in_window && frames > 0 && first_audio_frame >= 0
       && (int)current_frame >= first_audio_frame)
   {
      double rms_l = sqrt(frame_sum_sq_l / frames);
      double rms_r = sqrt(frame_sum_sq_r / frames);
      window_sum_sq += frame_sum_sq_l + frame_sum_sq_r;
      window_sample_count += frames * 2;
      active_window_frames++;
      if (rms_l > LOUDNESS_RMS_THRESHOLD || rms_r > LOUDNESS_RMS_THRESHOLD)
         hot_frames++;
   }

   return frames;
}

static void input_poll(void) {}
static int16_t input_state(unsigned p, unsigned d, unsigned i, unsigned id)
{ (void)p; (void)d; (void)i; (void)id; return 0; }

static int use_bios = 0;
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
      if (var->key && strcmp(var->key, "virtualjaguar_bios") == 0)
      {
         var->value = use_bios ? "enabled" : "disabled";
         return true;
      }
      var->value = NULL;
      return false;
   }
   default:
      return false;
   }
}

/* ---------- core load ---------- */
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

static void init_core(void)
{
   p_retro_set_environment(environment);
   p_retro_init();
   p_retro_set_video_refresh(video_refresh);
   p_retro_set_audio_sample(audio_sample);
   p_retro_set_audio_sample_batch(audio_batch);
   p_retro_set_input_poll(input_poll);
   p_retro_set_input_state(input_state);
}

/* ---------- main test ---------- */
static uint8_t *read_rom_file(const char *path, size_t *size_out)
{
   FILE *f = fopen(path, "rb");
   uint8_t *buf;
   size_t sz;
   if (!f) return NULL;
   fseek(f, 0, SEEK_END);
   sz = ftell(f);
   fseek(f, 0, SEEK_SET);
   buf = malloc(sz);
   if (!buf) { fclose(f); return NULL; }
   if (fread(buf, 1, sz, f) != sz) { free(buf); fclose(f); return NULL; }
   fclose(f);
   *size_out = sz;
   return buf;
}

static void reset_capture(void)
{
   current_frame = 0;
   total_samples = 0;
   saturated_samples = 0;
   longest_saturated_run = 0;
   current_saturated_run = 0;
   hot_frames = 0;
   first_audio_frame = -1;
   window_sum_sq = 0;
   window_sample_count = 0;
   active_window_frames = 0;
}

static int run_clipping_test(const char *core_path, const char *rom_path,
                             unsigned total_frames, const char *label,
                             int expect_clipping)
{
   uint8_t *rom_data;
   size_t rom_size;
   struct retro_game_info game;
   double saturation_density_pct;
   double window_rms;
   double hot_frame_pct;
   unsigned i;
   int verdict = 0;

   printf("\n=== Clipping check: %s ===\n", label);
   printf("  ROM:    %s\n", rom_path);
   printf("  Frames: %u, Window: [%u, %u)\n",
          total_frames, WINDOW_START_FRAME, total_frames);
   printf("  BIOS:   %s\n", use_bios ? "enabled" : "disabled (HLE)");

   rom_data = read_rom_file(rom_path, &rom_size);
   if (!rom_data)
   {
      printf("  SKIP: ROM not found at %s\n", rom_path);
      return 2;
   }

   if (!load_core(core_path))
   {
      free(rom_data);
      return 1;
   }

   init_core();

   memset(&game, 0, sizeof(game));
   game.path = rom_path;
   game.data = rom_data;
   game.size = rom_size;

   if (!p_retro_load_game(&game))
   {
      printf("  FAIL: retro_load_game rejected ROM\n");
      free(rom_data);
      dlclose(core_handle);
      return 1;
   }

   reset_capture();
   for (i = 0; i < total_frames; i++)
   {
      current_frame = i;
      p_retro_run();
   }

   p_retro_unload_game();
   p_retro_deinit();
   dlclose(core_handle);
   free(rom_data);

   if (total_samples == 0)
   {
      printf("  SKIP: no audio samples in window\n");
      return 2;
   }

   saturation_density_pct = 100.0 * (double)saturated_samples / (double)total_samples;
   window_rms = (window_sample_count > 0)
      ? sqrt(window_sum_sq / (double)window_sample_count) : 0.0;
   hot_frame_pct = (active_window_frames > 0)
      ? 100.0 * hot_frames / active_window_frames : 0.0;

   printf("  First audio at frame: %d\n", first_audio_frame);
   printf("  Samples in window:    %llu\n", (unsigned long long)total_samples);
   printf("  Saturated (|s|>=%d):  %llu (%.3f%%)\n",
          SATURATION_LEVEL, (unsigned long long)saturated_samples,
          saturation_density_pct);
   printf("  Longest saturation run: %u samples\n", longest_saturated_run);
   printf("  Window RMS (combined): %.1f\n", window_rms);
   printf("  Hot frames (RMS>%.0f): %u/%u (%.1f%%)\n",
          LOUDNESS_RMS_THRESHOLD, hot_frames, active_window_frames, hot_frame_pct);

   if (saturation_density_pct > SATURATION_DENSITY_PCT)
   {
      printf("  FAIL: saturation density %.2f%% exceeds %.1f%%\n",
             saturation_density_pct, SATURATION_DENSITY_PCT);
      verdict = 1;
   }
   if (longest_saturated_run >= SATURATION_RUN_SAMPLES)
   {
      printf("  FAIL: %u-sample saturation run exceeds %d\n",
             longest_saturated_run, SATURATION_RUN_SAMPLES);
      verdict = 1;
   }
   if (hot_frame_pct > 100.0 * LOUDNESS_FRAME_FRACTION)
   {
      printf("  FAIL: %.1f%% of frames at sustained RMS > %.0f (>%.0f%%)\n",
             hot_frame_pct, LOUDNESS_RMS_THRESHOLD,
             100.0 * LOUDNESS_FRAME_FRACTION);
      verdict = 1;
   }

   if (verdict == 0)
      printf("  PASS: no clipping signature in window\n");

   if (expect_clipping)
   {
      /* Inverted: this ROM is a known-broken regression watcher. Pass
       * means the clipping is still there as documented; an unexpected
       * "clean" result means someone fixed the bug — celebrate by
       * making the test fail so the manifest gets updated. */
      if (verdict == 1)
      {
         printf("  EXPECTED-FAIL: clipping confirmed (known issue)\n");
         return 0;
      }
      printf("  UNEXPECTED-PASS: clipping is gone — remove --expect-clipping\n");
      return 1;
   }

   return verdict;
}

static int self_test(const char *core_path)
{
   /* Sanity: a synthetic 1MB ROM with a BRA self-loop should produce
    * no audio at all — not clipping. Confirms the test infrastructure
    * doesn't false-positive on silence.
    */
   uint8_t *rom = calloc(1, 1048576);
   struct retro_game_info game;
   double sat_pct;
   unsigned i;

   if (!rom) return 1;
   rom[0x404] = 0x00; rom[0x405] = 0x80;
   rom[0x406] = 0x20; rom[0x407] = 0x00;
   rom[0x2000] = 0x60; rom[0x2001] = 0xFE;

   printf("\n=== Self-test: silence on dummy ROM ===\n");
   if (!load_core(core_path)) { free(rom); return 1; }
   init_core();

   memset(&game, 0, sizeof(game));
   game.path = "dummy.j64";
   game.data = rom;
   game.size = 1048576;
   if (!p_retro_load_game(&game))
   {
      printf("  FAIL: retro_load_game rejected dummy\n");
      free(rom); dlclose(core_handle); return 1;
   }

   reset_capture();
   for (i = 0; i < DEFAULT_TOTAL_FRAMES; i++)
   {
      current_frame = i;
      p_retro_run();
   }
   p_retro_unload_game();
   p_retro_deinit();
   dlclose(core_handle);
   free(rom);

   sat_pct = (total_samples > 0)
      ? 100.0 * saturated_samples / total_samples : 0.0;
   printf("  Saturated: %.3f%%, Hot frames: %u, Longest run: %u\n",
          sat_pct, hot_frames, longest_saturated_run);
   if (sat_pct > 0.5 || longest_saturated_run >= SATURATION_RUN_SAMPLES
       || hot_frames > 0)
   {
      printf("  FAIL: dummy ROM tripped clipping heuristics — false positive\n");
      return 1;
   }
   printf("  PASS: dummy ROM stays silent\n");
   return 0;
}

static void usage(const char *prog)
{
   fprintf(stderr,
      "Usage: %s <core> <rom> [--bios] [--frames N] [--quiet]\n"
      "                       [--expect-clipping] [--label TAG]\n"
      "       %s <core> --self-test\n"
      "\n"
      "  --expect-clipping  Invert verdict: pass when clipping is detected\n"
      "                     (use for known-broken titles so a fix flips CI red).\n",
      prog, prog);
}

int main(int argc, char **argv)
{
   const char *core_path;
   const char *rom_path = NULL;
   const char *label = NULL;
   unsigned total_frames = DEFAULT_TOTAL_FRAMES;
   int self = 0;
   int expect_clipping = 0;
   int i;

   if (argc < 2) { usage(argv[0]); return 2; }
   core_path = argv[1];

   for (i = 2; i < argc; i++)
   {
      if (strcmp(argv[i], "--bios") == 0) use_bios = 1;
      else if (strcmp(argv[i], "--quiet") == 0) log_quiet = 1;
      else if (strcmp(argv[i], "--self-test") == 0) self = 1;
      else if (strcmp(argv[i], "--expect-clipping") == 0) expect_clipping = 1;
      else if (strcmp(argv[i], "--frames") == 0 && i + 1 < argc)
         total_frames = (unsigned)atoi(argv[++i]);
      else if (strcmp(argv[i], "--label") == 0 && i + 1 < argc)
         label = argv[++i];
      else if (argv[i][0] != '-' && !rom_path)
         rom_path = argv[i];
      else { usage(argv[0]); return 2; }
   }

   if (self) return self_test(core_path);
   if (!rom_path) { usage(argv[0]); return 2; }
   if (!label) label = rom_path;
   return run_clipping_test(core_path, rom_path, total_frames, label,
                            expect_clipping);
}
