/* test_audio_presence.c -- Detect missing or stuck audio (silence / DC).
 *
 * Counterpart to test_audio_clipping.c.  That test detects loud-broken
 * audio (saturation/clipping).  This test detects the OTHER failure
 * mode: a "fix" that silences the game instead of fixing it, or a
 * regression where the DSP audio engine never starts emitting samples
 * at all.  Closing PR #170 was the prompt for this — the clipping
 * test passed on PR #170 because Iron Soldier 2 went from
 * "RMS=28587 (loud broken)" to "RMS=521 (effectively silent)".
 *
 * The test runs N frames and asserts:
 *   - first_audio_frame is reached (some non-trivial sample seen)
 *   - window RMS lies inside [floor, ceiling] for the chosen ROM
 *   - longest run of near-zero stereo frames < max_zero_run_pct of window
 *
 * Tunable per-ROM via --rms-floor / --rms-ceiling / --max-zero-run-pct.
 *
 * Build: see Makefile target test/test_audio_presence
 * Usage: ./test/test_audio_presence <core> <rom> [opts]
 *
 * Exit:  0 PASS, 1 FAIL, 2 SKIP (ROM missing)
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
#define ONSET_THRESHOLD 32        /* |s| > this => audio onset */
#define ZERO_THRESHOLD 32         /* both channels |s| <= this => "near-zero" frame */

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
static int first_audio_frame = -1;
static double window_sum_sq = 0;
static uint64_t window_sample_count = 0;       /* counts mono samples */
static unsigned longest_zero_run = 0;
static unsigned current_zero_run = 0;
static unsigned active_window_frames = 0;

/* ---------- core libretro callback shims ---------- */
static void video_refresh(const void *d, unsigned w, unsigned h, size_t p)
{ (void)d; (void)w; (void)h; (void)p; }

static void audio_sample(int16_t l, int16_t r) { (void)l; (void)r; }

static size_t audio_batch(const int16_t *data, size_t frames)
{
   size_t i;
   bool in_window = (current_frame >= WINDOW_START_FRAME);
   double frame_sum_sq = 0;

   for (i = 0; i < frames; i++)
   {
      int16_t l = data[i * 2];
      int16_t r = data[i * 2 + 1];
      /* Promote to int32_t so that negating INT16_MIN doesn't overflow
       * (-(-32768) is undefined as a 16-bit op).  Caught by Copilot
       * review on PR #182. */
      int32_t abs_l = (l < 0) ? -(int32_t)l : (int32_t)l;
      int32_t abs_r = (r < 0) ? -(int32_t)r : (int32_t)r;
      bool near_zero = (abs_l <= ZERO_THRESHOLD) && (abs_r <= ZERO_THRESHOLD);

      if (first_audio_frame < 0 && (abs_l > ONSET_THRESHOLD || abs_r > ONSET_THRESHOLD))
         first_audio_frame = (int)current_frame;

      if (in_window)
      {
         frame_sum_sq += (double)l * l + (double)r * r;

         if (near_zero)
         {
            current_zero_run++;
            if (current_zero_run > longest_zero_run)
               longest_zero_run = current_zero_run;
         }
         else
         {
            current_zero_run = 0;
         }
      }
   }

   if (in_window && frames > 0)
   {
      window_sum_sq += frame_sum_sq;
      window_sample_count += frames * 2;
      active_window_frames++;
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
   if (!p_##sym) { \
      fprintf(stderr, "Missing symbol: %s\n", #sym); \
      dlclose(core_handle); core_handle = NULL; \
      return false; \
   } \
} while (0)
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

/* ---------- ROM load ---------- */
static unsigned char *rom_buf = NULL;
static size_t rom_size = 0;

static int load_rom(const char *path)
{
   FILE *fp;
   long size;

   fp = fopen(path, "rb");
   if (!fp) return -1;
   fseek(fp, 0, SEEK_END);
   size = ftell(fp);
   fseek(fp, 0, SEEK_SET);
   if (size <= 0) { fclose(fp); return -1; }

   rom_buf = (unsigned char *)malloc((size_t)size);
   if (!rom_buf) { fclose(fp); return -1; }
   if (fread(rom_buf, 1, (size_t)size, fp) != (size_t)size)
   {
      free(rom_buf); rom_buf = NULL; fclose(fp); return -1;
   }
   fclose(fp);
   rom_size = (size_t)size;
   return 0;
}

int main(int argc, char **argv)
{
   const char *core_path = NULL;
   const char *rom_path = NULL;
   const char *label = NULL;
   unsigned total_frames = DEFAULT_TOTAL_FRAMES;
   double rms_floor = 100.0;
   double rms_ceiling = 25000.0;
   double max_zero_run_pct = 50.0;
   int i;
   struct retro_game_info game;
   double window_rms;
   unsigned zero_run_pct = 0;
   int fail_count = 0;

   for (i = 1; i < argc; i++)
   {
      const char *a = argv[i];
      if (a[0] != '-')
      {
         if (!core_path)      core_path = a;
         else if (!rom_path)  rom_path = a;
         continue;
      }
      if (!strcmp(a, "--bios"))       use_bios = 1;
      else if (!strcmp(a, "--quiet")) log_quiet = 1;
      else if (!strcmp(a, "--frames") && i + 1 < argc) total_frames = (unsigned)atoi(argv[++i]);
      else if (!strcmp(a, "--rms-floor") && i + 1 < argc) rms_floor = atof(argv[++i]);
      else if (!strcmp(a, "--rms-ceiling") && i + 1 < argc) rms_ceiling = atof(argv[++i]);
      else if (!strcmp(a, "--max-zero-run-pct") && i + 1 < argc) max_zero_run_pct = atof(argv[++i]);
      else if (!strcmp(a, "--label") && i + 1 < argc) label = argv[++i];
      else
      {
         fprintf(stderr, "Unknown arg: %s\n", a);
         return 2;
      }
   }

   if (!core_path)
   {
      fprintf(stderr, "Usage: %s <core> <rom> [--bios] [--frames N]\n"
                      "  [--rms-floor F] [--rms-ceiling C] [--max-zero-run-pct P]\n"
                      "  [--label NAME] [--quiet]\n", argv[0]);
      return 2;
   }
   if (!rom_path)
   {
      fprintf(stderr, "SKIP: no ROM path given\n");
      return 2;
   }

   /* `--frames` must leave a non-empty measurement window past the
    * boot-skip prefix; otherwise the report's `total_frames -
    * WINDOW_START_FRAME` underflows when printed via %u.
    * Caught by Copilot review on PR #182. */
   if (total_frames <= WINDOW_START_FRAME)
   {
      fprintf(stderr,
              "ERROR: --frames %u must exceed the %u-frame boot skip "
              "(WINDOW_START_FRAME).  Pick a value > %u.\n",
              total_frames, WINDOW_START_FRAME, WINDOW_START_FRAME);
      return 2;
   }

   if (load_rom(rom_path) < 0)
   {
      fprintf(stderr, "SKIP: ROM not found or unreadable: %s\n", rom_path);
      return 2;
   }

   if (!load_core(core_path))
   {
      free(rom_buf);
      return 1;
   }

   init_core();

   memset(&game, 0, sizeof(game));
   game.path = rom_path;
   game.data = rom_buf;
   game.size = rom_size;

   if (!p_retro_load_game(&game))
   {
      fprintf(stderr, "FAIL: retro_load_game failed\n");
      p_retro_deinit();
      dlclose(core_handle); core_handle = NULL;
      free(rom_buf);
      return 1;
   }

   for (current_frame = 0; current_frame < total_frames; current_frame++)
      p_retro_run();

   p_retro_unload_game();
   p_retro_deinit();
   dlclose(core_handle); core_handle = NULL;
   free(rom_buf);

   /* ---------- Report ---------- */
   if (window_sample_count > 0)
      window_rms = sqrt(window_sum_sq / (double)window_sample_count);
   else
      window_rms = 0.0;

   if (active_window_frames > 0)
   {
      double frames_in_run = (double)longest_zero_run /
         ((double)window_sample_count / (double)active_window_frames / 2.0);
      zero_run_pct = (unsigned)(frames_in_run / (double)active_window_frames * 100.0);
   }

   if (!log_quiet)
   {
      printf("\n=== Presence check: %s ===\n", label ? label : rom_path);
      printf("  ROM:    %s\n", rom_path);
      printf("  Frames: %u, Window: [%u, %u)\n",
             total_frames, WINDOW_START_FRAME, total_frames);
      printf("  BIOS:   %s\n", use_bios ? "enabled" : "disabled (HLE)");
      printf("  First audio at frame: %d\n", first_audio_frame);
      printf("  Active window frames: %u / %u\n",
             active_window_frames, total_frames - WINDOW_START_FRAME);
      printf("  Window RMS:           %.1f  [floor %.1f, ceiling %.1f]\n",
             window_rms, rms_floor, rms_ceiling);
      printf("  Longest zero run:     %u stereo frames (~%u%% of window)\n",
             longest_zero_run, zero_run_pct);
   }

   if (first_audio_frame < 0)
   {
      printf("  FAIL: no audio onset detected (game silent throughout %u frames)\n",
             total_frames);
      fail_count++;
   }
   if (window_rms < rms_floor)
   {
      printf("  FAIL: window RMS %.1f below floor %.1f (audio missing or muted)\n",
             window_rms, rms_floor);
      fail_count++;
   }
   if (window_rms > rms_ceiling)
   {
      printf("  FAIL: window RMS %.1f above ceiling %.1f (audio possibly broken/saturated)\n",
             window_rms, rms_ceiling);
      fail_count++;
   }
   if ((double)zero_run_pct > max_zero_run_pct)
   {
      printf("  FAIL: longest zero run is ~%u%% of window (>%.0f%%, audio stuck or dropped out)\n",
             zero_run_pct, max_zero_run_pct);
      fail_count++;
   }

   if (fail_count == 0)
   {
      printf("  PASS: audio is present and within expected envelope\n");
      return 0;
   }
   return 1;
}
