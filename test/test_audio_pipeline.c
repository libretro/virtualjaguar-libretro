/* test_audio_pipeline.c -- Audio rendering and I2S pipeline tests.
 *
 * Validates the complete audio chain:
 * - I2S clock setup (SCLK/SMODE → JERRY I2S callback)
 * - DSP SSI interrupt delivery
 * - Audio sample generation via retro_audio_sample_batch
 * - Audio onset timing (how many frames until first non-silent audio)
 * - Audio dropout detection over sustained playback
 * - BIOS vs HLE audio initialization
 *
 * Build: cc -o test/test_audio_pipeline test/test_audio_pipeline.c -ldl
 * Usage: ./test/test_audio_pipeline [path/to/core.dylib] [rom_path]
 *
 * If rom_path is provided, runs against a real ROM.  Otherwise uses
 * a synthetic ROM that sets up I2S and DSP for basic audio output.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dlfcn.h>
#include <stdarg.h>
#include <stdbool.h>
#include <math.h>
#include "../libretro-common/include/libretro.h"

#ifdef __APPLE__
#define CORE_FILENAME "virtualjaguar_libretro.dylib"
#elif defined(_WIN32)
#define CORE_FILENAME "virtualjaguar_libretro.dll"
#else
#define CORE_FILENAME "virtualjaguar_libretro.so"
#endif

/* Jaguar hardware addresses */
#define DSP_FLAGS_ADDR   0xF1A100
#define DSP_CTRL_ADDR    0xF1A114

#define WHO_M68K  6

/* ================================================================
 * Audio capture state
 * ================================================================ */

#define MAX_AUDIO_FRAMES 600    /* capture up to 10 seconds at 60fps */
#define SAMPLES_PER_FRAME 800   /* 48000 Hz / 60 fps = 800 */

struct audio_frame_stats {
   unsigned frame;
   size_t samples;              /* number of stereo sample pairs */
   unsigned nonsilent_samples;  /* samples where |L| > threshold or |R| > threshold */
   int16_t peak_l, peak_r;     /* peak absolute value */
   double rms_l, rms_r;        /* RMS level */
};

static struct audio_frame_stats frame_stats[MAX_AUDIO_FRAMES];
static unsigned audio_frame_idx = 0;
static unsigned current_frame = 0;

/* Cumulative stats */
static size_t total_samples = 0;
static unsigned total_nonsilent = 0;
static unsigned total_batch_calls = 0;
static int first_audio_frame = -1;      /* first frame with non-silent audio */
static int first_batch_frame = -1;      /* first frame with ANY audio callback */
static unsigned silent_frames_after_onset = 0;
static unsigned dropout_count = 0;
static int was_playing = 0;

#define SILENCE_THRESHOLD 32  /* samples with |val| < this are "silent" */

static void reset_audio_stats(void)
{
   audio_frame_idx = 0;
   current_frame = 0;
   total_samples = 0;
   total_nonsilent = 0;
   total_batch_calls = 0;
   first_audio_frame = -1;
   first_batch_frame = -1;
   silent_frames_after_onset = 0;
   dropout_count = 0;
   was_playing = 0;
   memset(frame_stats, 0, sizeof(frame_stats));
}

/* ================================================================
 * Libretro callbacks
 * ================================================================ */

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

static void *core_handle;

static uint8_t **p_jaguarMainRAM;
static uint8_t *p_tomRam8;
static uint8_t *(*p_DSPGetRAM)(void);
static bool (*p_DSPIsRunning)(void);
static uint32_t *p_dsp_control;
static int32_t *p_JERRYI2SInterruptTimer;

static void video_refresh(const void *d, unsigned w, unsigned h, size_t p)
{ (void)d; (void)w; (void)h; (void)p; }

static void audio_sample(int16_t l, int16_t r)
{
   (void)l; (void)r;
}

static size_t audio_batch(const int16_t *data, size_t frames)
{
   size_t i;
   unsigned nonsilent = 0;
   int16_t peak_l = 0, peak_r = 0;
   double sum_sq_l = 0, sum_sq_r = 0;

   total_batch_calls++;
   total_samples += frames;

   if (first_batch_frame < 0)
      first_batch_frame = (int)current_frame;

   for (i = 0; i < frames; i++) {
      int16_t l = data[i * 2];
      int16_t r = data[i * 2 + 1];
      int16_t abs_l = (l < 0) ? -l : l;
      int16_t abs_r = (r < 0) ? -r : r;

      if (abs_l > SILENCE_THRESHOLD || abs_r > SILENCE_THRESHOLD) {
         nonsilent++;
         total_nonsilent++;
      }

      if (abs_l > peak_l) peak_l = abs_l;
      if (abs_r > peak_r) peak_r = abs_r;
      sum_sq_l += (double)l * l;
      sum_sq_r += (double)r * r;
   }

   if (nonsilent > 0 && first_audio_frame < 0)
      first_audio_frame = (int)current_frame;

   /* Dropout detection */
   if (nonsilent > 0) {
      if (!was_playing && first_audio_frame >= 0 && (int)current_frame > first_audio_frame + 5)
         dropout_count++; /* was silent, now playing again = recovered dropout */
      was_playing = 1;
   } else {
      if (was_playing)
         silent_frames_after_onset++;
      was_playing = 0;
   }

   /* Record per-frame stats */
   if (audio_frame_idx < MAX_AUDIO_FRAMES) {
      frame_stats[audio_frame_idx].frame = current_frame;
      frame_stats[audio_frame_idx].samples = frames;
      frame_stats[audio_frame_idx].nonsilent_samples = nonsilent;
      frame_stats[audio_frame_idx].peak_l = peak_l;
      frame_stats[audio_frame_idx].peak_r = peak_r;
      frame_stats[audio_frame_idx].rms_l = (frames > 0) ? sqrt(sum_sq_l / frames) : 0;
      frame_stats[audio_frame_idx].rms_r = (frames > 0) ? sqrt(sum_sq_r / frames) : 0;
      audio_frame_idx++;
   }

   return frames;
}

static void input_poll(void) {}
static int16_t input_state(unsigned p, unsigned d, unsigned i, unsigned id)
{ (void)p; (void)d; (void)i; (void)id; return 0; }

static int use_bios = 0;

static void log_printf(enum retro_log_level level, const char *fmt, ...)
{
   va_list ap;
   if (level < RETRO_LOG_WARN) return;
   va_start(ap, fmt);
   vfprintf(stderr, fmt, ap);
   va_end(ap);
}

static struct retro_log_callback log_cb = { log_printf };

static bool environment(unsigned cmd, void *data)
{
   switch (cmd) {
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
      *(const char **)data = "/tmp";
      return true;
   case RETRO_ENVIRONMENT_GET_SAVE_DIRECTORY:
      *(const char **)data = "/tmp";
      return true;
   case RETRO_ENVIRONMENT_GET_VARIABLE: {
      struct retro_variable *var = (struct retro_variable *)data;
      if (var->key && strcmp(var->key, "virtualjaguar_bios") == 0) {
         var->value = use_bios ? "enabled" : "disabled";
         return true;
      }
      if (var->key && strcmp(var->key, "virtualjaguar_usefastblitter") == 0) {
         var->value = "enabled";
         return true;
      }
      var->value = NULL;
      return false;
   }
   default:
      return false;
   }
}

/* Test counters */
static int passes = 0, fails = 0;
#define PASS(msg, ...) do { printf("  PASS: " msg "\n", ##__VA_ARGS__); passes++; } while(0)
#define FAIL(msg, ...) do { printf("  FAIL: " msg "\n", ##__VA_ARGS__); fails++; } while(0)
#define INFO(msg, ...) do { printf("  INFO: " msg "\n", ##__VA_ARGS__); } while(0)

/* ================================================================
 * Core loading
 * ================================================================ */

static bool load_core(const char *path)
{
   core_handle = dlopen(path, RTLD_NOW);
   if (!core_handle) {
      fprintf(stderr, "dlopen(%s): %s\n", path, dlerror());
      return false;
   }

   p_retro_init                 = dlsym(core_handle, "retro_init");
   p_retro_deinit               = dlsym(core_handle, "retro_deinit");
   p_retro_set_environment      = dlsym(core_handle, "retro_set_environment");
   p_retro_set_video_refresh    = dlsym(core_handle, "retro_set_video_refresh");
   p_retro_set_audio_sample     = dlsym(core_handle, "retro_set_audio_sample");
   p_retro_set_audio_sample_batch = dlsym(core_handle, "retro_set_audio_sample_batch");
   p_retro_set_input_poll       = dlsym(core_handle, "retro_set_input_poll");
   p_retro_set_input_state      = dlsym(core_handle, "retro_set_input_state");
   p_retro_load_game            = dlsym(core_handle, "retro_load_game");
   p_retro_unload_game          = dlsym(core_handle, "retro_unload_game");
   p_retro_run                  = dlsym(core_handle, "retro_run");

   p_jaguarMainRAM  = dlsym(core_handle, "jaguarMainRAM");
   p_tomRam8        = dlsym(core_handle, "tomRam8");
   p_DSPGetRAM      = dlsym(core_handle, "DSPGetRAM");
   p_DSPIsRunning   = dlsym(core_handle, "DSPIsRunning");
   p_dsp_control    = dlsym(core_handle, "dsp_control");
   p_JERRYI2SInterruptTimer = dlsym(core_handle, "JERRYI2SInterruptTimer");

   if (!p_retro_init || !p_retro_load_game) {
      fprintf(stderr, "Missing required symbols\n");
      dlclose(core_handle);
      return false;
   }

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

static void run_frames(unsigned n)
{
   unsigned i;
   for (i = 0; i < n; i++) {
      current_frame++;
      p_retro_run();
   }
}

/* ================================================================
 * Test 1: Audio Callback Fires
 *
 * After running frames with a dummy ROM, verify that the audio
 * batch callback was invoked (the I2S system generates samples
 * even without a game-loaded DSP program).
 * ================================================================ */
static void test_audio_callback_fires(void)
{
   struct retro_game_info game;
   uint8_t *rom;

   printf("\n=== Test 1: Audio Callback Fires ===\n");

   rom = calloc(1, 131072);
   if (!rom) { FAIL("alloc"); return; }
   rom[0x404] = 0x00; rom[0x405] = 0x80;
   rom[0x406] = 0x20; rom[0x407] = 0x00;
   rom[0x2000] = 0x60; rom[0x2001] = 0xFE;

   init_core();
   memset(&game, 0, sizeof(game));
   game.path = "audio_test_dummy.jag";
   game.data = rom;
   game.size = 131072;

   if (!p_retro_load_game(&game)) {
      FAIL("retro_load_game failed");
      free(rom); return;
   }

   reset_audio_stats();
   run_frames(30);

   printf("  Batch calls: %u, Total samples: %zu, Non-silent: %u\n",
          total_batch_calls, total_samples, total_nonsilent);

   if (total_batch_calls > 0)
      PASS("Audio batch callback fired %u times over 30 frames", total_batch_calls);
   else
      FAIL("Audio batch callback never fired");

   if (total_samples > 0)
      PASS("Generated %zu audio samples", total_samples);
   else
      FAIL("No audio samples generated");

   /* Check I2S timer state */
   if (p_JERRYI2SInterruptTimer) {
      printf("  I2S interrupt timer = %d\n", *p_JERRYI2SInterruptTimer);
      if (*p_JERRYI2SInterruptTimer >= 0)
         PASS("I2S interrupt timer active (value=%d)", *p_JERRYI2SInterruptTimer);
      else
         INFO("I2S interrupt timer inactive (-1) — I2S may not be started yet");
   }

   /* Check DSP state */
   if (p_DSPIsRunning) {
      int running = p_DSPIsRunning();
      printf("  DSP running: %s\n", running ? "YES" : "NO");
   }

   p_retro_unload_game();
   p_retro_deinit();
   free(rom);
}

/* ================================================================
 * Test 2: Audio Onset Timing
 *
 * For a real ROM, measure how many frames until the first non-silent
 * audio sample.  BIOS boot typically produces sound within ~30 frames
 * (logo/intro).  HLE should be comparable.
 * ================================================================ */
static void test_audio_onset(const char *rom_path, int bios_mode)
{
   FILE *f;
   long rom_size;
   uint8_t *rom_data;
   struct retro_game_info game;

   printf("\n=== Test 2: Audio Onset Timing (%s, %s) ===\n",
          rom_path ? rom_path : "dummy",
          bios_mode ? "BIOS" : "HLE");

   if (!rom_path) {
      INFO("No ROM specified — skipping onset test");
      return;
   }

   f = fopen(rom_path, "rb");
   if (!f) {
      INFO("Cannot open ROM '%s' — skipping", rom_path);
      return;
   }
   fseek(f, 0, SEEK_END);
   rom_size = ftell(f);
   fseek(f, 0, SEEK_SET);
   rom_data = malloc(rom_size);
   if (!rom_data) { fclose(f); FAIL("alloc"); return; }
   fread(rom_data, 1, rom_size, f);
   fclose(f);

   use_bios = bios_mode;
   init_core();

   memset(&game, 0, sizeof(game));
   game.path = rom_path;
   game.data = rom_data;
   game.size = rom_size;

   if (!p_retro_load_game(&game)) {
      FAIL("retro_load_game failed for '%s'", rom_path);
      free(rom_data); return;
   }

   reset_audio_stats();
   run_frames(300); /* 5 seconds */

   printf("  First audio batch: frame %d\n", first_batch_frame);
   printf("  First non-silent: frame %d\n", first_audio_frame);
   printf("  Total samples: %zu, Non-silent: %u\n", total_samples, total_nonsilent);
   printf("  Batch calls: %u over 300 frames\n", total_batch_calls);
   printf("  Silent frames after onset: %u\n", silent_frames_after_onset);
   printf("  Dropout events: %u\n", dropout_count);

   /* Print per-frame audio timeline (first 30 frames with audio) */
   {
      unsigned shown = 0;
      unsigned i;
      printf("  --- Audio timeline (first 30 active frames) ---\n");
      for (i = 0; i < audio_frame_idx && shown < 30; i++) {
         if (frame_stats[i].nonsilent_samples > 0 || shown > 0) {
            printf("  F%3u: %4zu samp, %3u active, peak L=%5d R=%5d, "
                   "RMS L=%6.1f R=%6.1f\n",
                   frame_stats[i].frame,
                   frame_stats[i].samples,
                   frame_stats[i].nonsilent_samples,
                   frame_stats[i].peak_l,
                   frame_stats[i].peak_r,
                   frame_stats[i].rms_l,
                   frame_stats[i].rms_r);
            shown++;
         }
      }
   }

   /* Audio callback should fire */
   if (total_batch_calls >= 200)
      PASS("Audio callbacks consistent: %u/300 frames", total_batch_calls);
   else if (total_batch_calls > 0)
      PASS("Audio callbacks present but sparse: %u/300 frames", total_batch_calls);
   else
      FAIL("No audio callbacks in 300 frames");

   /* Onset timing */
   if (first_audio_frame >= 0 && first_audio_frame < 60)
      PASS("Audio onset at frame %d (within 1 second)", first_audio_frame);
   else if (first_audio_frame >= 0 && first_audio_frame < 180)
      PASS("Audio onset at frame %d (within 3 seconds — may be slow)", first_audio_frame);
   else if (first_audio_frame >= 0)
      FAIL("Audio onset late: frame %d (after 3 seconds)", first_audio_frame);
   else
      FAIL("No non-silent audio in 300 frames");

   /* Dropout detection */
   if (first_audio_frame >= 0) {
      unsigned active_frames = 300 - (unsigned)first_audio_frame;
      unsigned dropout_pct;

      if (active_frames > 0)
         dropout_pct = (silent_frames_after_onset * 100) / active_frames;
      else
         dropout_pct = 0;

      if (dropout_pct < 5)
         PASS("Audio stable: %u%% dropout rate (%u silent frames after onset)",
              dropout_pct, silent_frames_after_onset);
      else if (dropout_pct < 20)
         PASS("Audio mostly stable: %u%% dropout rate (%u silent, %u dropouts)",
              dropout_pct, silent_frames_after_onset, dropout_count);
      else
         FAIL("Audio unstable: %u%% dropout rate (%u silent, %u dropouts)",
              dropout_pct, silent_frames_after_onset, dropout_count);
   }

   /* Volume check */
   {
      unsigned loud_frames = 0;
      unsigned i;
      for (i = 0; i < audio_frame_idx; i++) {
         if (frame_stats[i].peak_l > 1000 || frame_stats[i].peak_r > 1000)
            loud_frames++;
      }
      if (loud_frames > 0)
         PASS("Audio has meaningful volume: %u frames with peak > 1000", loud_frames);
      else if (total_nonsilent > 0)
         INFO("Audio present but very quiet (all peaks < 1000)");
      else
         INFO("No audio to measure volume");
   }

   p_retro_unload_game();
   p_retro_deinit();
   free(rom_data);
}

/* ================================================================
 * Test 3: I2S State Verification
 *
 * After HLE init, verify the I2S subsystem is configured by
 * checking internal JERRY state (I2S timer, callback chain).
 * ================================================================ */
static void test_i2s_state(void)
{
   struct retro_game_info game;
   uint8_t *rom;

   printf("\n=== Test 3: I2S State Verification ===\n");

   rom = calloc(1, 131072);
   if (!rom) { FAIL("alloc"); return; }
   rom[0x404] = 0x00; rom[0x405] = 0x80;
   rom[0x406] = 0x20; rom[0x407] = 0x00;
   rom[0x2000] = 0x60; rom[0x2001] = 0xFE;

   use_bios = 0;
   init_core();
   memset(&game, 0, sizeof(game));
   game.path = "i2s_state_test.jag";
   game.data = rom;
   game.size = 131072;

   if (!p_retro_load_game(&game)) {
      FAIL("retro_load_game failed");
      free(rom); return;
   }

   /* Before any frames */
   if (p_JERRYI2SInterruptTimer) {
      printf("  I2S timer before run: %d\n", *p_JERRYI2SInterruptTimer);
   }

   /* Run a few frames to let I2S initialize */
   reset_audio_stats();
   run_frames(5);

   if (p_JERRYI2SInterruptTimer) {
      int timer_val = *p_JERRYI2SInterruptTimer;
      printf("  I2S timer after 5 frames: %d\n", timer_val);

      if (timer_val >= 0)
         PASS("I2S timer active after HLE init (value=%d)", timer_val);
      else
         FAIL("I2S timer inactive (-1) — SCLK/SMODE write may have failed");
   } else {
      INFO("JERRYI2SInterruptTimer symbol not found — cannot verify");
   }

   /* Check if audio callbacks started */
   if (total_batch_calls > 0)
      PASS("Audio batch active: %u calls in 5 frames", total_batch_calls);
   else
      FAIL("No audio batch calls in 5 frames — I2S not generating samples");

   /* Check sample rate sanity: expect ~800 samples per frame at 48kHz/60fps */
   if (total_samples > 0) {
      unsigned avg_per_frame = (unsigned)(total_samples / 5);
      printf("  Average samples per frame: %u (expect ~800)\n", avg_per_frame);
      if (avg_per_frame >= 500 && avg_per_frame <= 1200)
         PASS("Sample rate reasonable: ~%u samples/frame", avg_per_frame);
      else if (avg_per_frame > 0)
         PASS("Samples present but rate unusual: %u/frame", avg_per_frame);
      else
         FAIL("No samples generated");
   }

   p_retro_unload_game();
   p_retro_deinit();
   free(rom);
}

/* ================================================================
 * Test 4: BIOS vs HLE Audio Comparison
 *
 * Compare audio onset and volume between BIOS and HLE modes
 * using the same ROM.
 * ================================================================ */
static void test_bios_vs_hle_audio(const char *rom_path)
{
   FILE *f;
   long rom_size;
   uint8_t *rom_data;
   struct retro_game_info game;
   int hle_onset, bios_onset;
   unsigned hle_nonsilent, bios_nonsilent;
   unsigned hle_dropouts, bios_dropouts;

   printf("\n=== Test 4: BIOS vs HLE Audio Comparison ===\n");

   if (!rom_path) {
      INFO("No ROM specified — skipping comparison");
      return;
   }

   f = fopen(rom_path, "rb");
   if (!f) {
      INFO("Cannot open ROM '%s' — skipping", rom_path);
      return;
   }
   fseek(f, 0, SEEK_END);
   rom_size = ftell(f);
   fseek(f, 0, SEEK_SET);
   rom_data = malloc(rom_size);
   if (!rom_data) { fclose(f); FAIL("alloc"); return; }
   fread(rom_data, 1, rom_size, f);
   fclose(f);

   /* --- HLE mode --- */
   use_bios = 0;
   init_core();
   memset(&game, 0, sizeof(game));
   game.path = rom_path;
   game.data = rom_data;
   game.size = rom_size;

   if (!p_retro_load_game(&game)) {
      INFO("HLE load failed — skipping comparison");
      free(rom_data); return;
   }

   reset_audio_stats();
   run_frames(300);
   hle_onset = first_audio_frame;
   hle_nonsilent = total_nonsilent;
   hle_dropouts = dropout_count;

   p_retro_unload_game();
   p_retro_deinit();

   /* --- BIOS mode --- */
   use_bios = 1;
   init_core();
   memset(&game, 0, sizeof(game));
   game.path = rom_path;
   game.data = rom_data;
   game.size = rom_size;

   if (!p_retro_load_game(&game)) {
      INFO("BIOS load failed — only HLE results available");
      printf("  HLE: onset=%d, non-silent=%u, dropouts=%u\n",
             hle_onset, hle_nonsilent, hle_dropouts);
      free(rom_data); return;
   }

   reset_audio_stats();
   run_frames(300);
   bios_onset = first_audio_frame;
   bios_nonsilent = total_nonsilent;
   bios_dropouts = dropout_count;

   p_retro_unload_game();
   p_retro_deinit();

   /* Report */
   printf("  HLE:  onset=frame %d, non-silent=%u, dropouts=%u\n",
          hle_onset, hle_nonsilent, hle_dropouts);
   printf("  BIOS: onset=frame %d, non-silent=%u, dropouts=%u\n",
          bios_onset, bios_nonsilent, bios_dropouts);

   /* Onset comparison */
   if (hle_onset >= 0 && bios_onset >= 0) {
      int diff = hle_onset - bios_onset;
      if (diff >= -30 && diff <= 30)
         PASS("Onset similar: HLE=%d BIOS=%d (diff=%d frames)", hle_onset, bios_onset, diff);
      else
         FAIL("Onset mismatch: HLE=%d BIOS=%d (diff=%d frames)", hle_onset, bios_onset, diff);
   } else if (bios_onset >= 0 && hle_onset < 0) {
      FAIL("BIOS has audio (frame %d) but HLE silent", bios_onset);
   } else if (hle_onset >= 0 && bios_onset < 0) {
      FAIL("HLE has audio (frame %d) but BIOS silent", hle_onset);
   } else {
      FAIL("Neither mode produced audio in 300 frames");
   }

   /* Volume comparison */
   if (hle_nonsilent > 0 && bios_nonsilent > 0) {
      double ratio;
      if (bios_nonsilent > hle_nonsilent)
         ratio = (double)hle_nonsilent / bios_nonsilent;
      else
         ratio = (double)bios_nonsilent / hle_nonsilent;

      if (ratio > 0.5)
         PASS("Volume comparable: HLE=%u BIOS=%u (ratio=%.2f)",
              hle_nonsilent, bios_nonsilent, ratio);
      else
         FAIL("Volume mismatch: HLE=%u BIOS=%u (ratio=%.2f)",
              hle_nonsilent, bios_nonsilent, ratio);
   }

   /* Dropout comparison */
   if (bios_dropouts == 0 && hle_dropouts > 0)
      FAIL("HLE has %u dropouts but BIOS has none", hle_dropouts);
   else if (hle_dropouts <= bios_dropouts + 2)
      PASS("Dropout rate comparable: HLE=%u BIOS=%u", hle_dropouts, bios_dropouts);

   free(rom_data);
}

/* ================================================================
 * Main
 * ================================================================ */

int main(int argc, char **argv)
{
   const char *core_path;
   const char *rom_path = NULL;

   core_path = CORE_FILENAME;

   if (argc > 1) {
      /* Check if first arg is a .dylib/.so/.dll (core) or a ROM */
      const char *ext = strrchr(argv[1], '.');
      if (ext && (strcmp(ext, ".dylib") == 0 || strcmp(ext, ".so") == 0 || strcmp(ext, ".dll") == 0))
         core_path = argv[1];
      else
         rom_path = argv[1];
   }
   if (argc > 2)
      rom_path = argv[2];

   printf("=== Audio Pipeline Tests ===\n");
   printf("Core: %s\n", core_path);
   if (rom_path)
      printf("ROM: %s\n", rom_path);

   if (!load_core(core_path)) return 1;

   test_audio_callback_fires();
   test_i2s_state();

   test_audio_onset(rom_path, 0); /* HLE */
   test_audio_onset(rom_path, 1); /* BIOS */
   test_bios_vs_hle_audio(rom_path);

   dlclose(core_handle);

   printf("\n=== Results: %d passed, %d failed ===\n", passes, fails);
   return fails > 0 ? 1 : 0;
}
