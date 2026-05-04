/* test_audio_long_capture.c -- run a core for N seconds and emit a
 * per-second audio fingerprint so two builds can be compared.
 *
 * Usage:
 *   ./test/test_audio_long_capture <core.dylib> <rom.jag> <seconds> [bios=0|1]
 *
 * Output (one line per second):
 *   sec=NN samples=NN nonsilent=NN peak=NN rms=NN nonzero_runs=NN
 *
 * Final summary:
 *   TOTAL nonsilent=NN peak_global=NN sustained_secs=NN
 *
 * "Sustained" = a 1-second window where >50% of samples are non-silent
 * (proxy for "music playing", since SFX bursts < 50%).  Comparing two
 * builds: if v_old has sustained=12 and v_new has sustained=0, music
 * regressed.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdarg.h>
#include <math.h>
#include <dlfcn.h>
#include "../libretro-common/include/libretro.h"

#define SAMPLE_RATE        48000
#define SAMPLES_PER_FRAME  800     /* 48000 / 60 */
#define FRAMES_PER_SEC     60
#define SILENCE_THRESHOLD  256     /* |sample| > this is "non-silent" */

static int g_seconds = 60;
static int g_use_bios = 0;
static const char *g_rom = NULL;
static const char *g_wav_out = NULL;
static FILE *g_wav_fp = NULL;
static uint64_t g_wav_samples = 0;

typedef struct {
   uint64_t total_samples;
   uint64_t nonsilent_samples;
   int      peak_global;
   int      sustained_seconds;
   /* per-second */
   uint32_t per_sec_nonsilent[600];
   int32_t  per_sec_peak[600];
   double   per_sec_rms[600];
} stats_t;

static stats_t S;
static int current_sec = 0;
static uint32_t cur_sec_nonsilent = 0;
static int32_t  cur_sec_peak = 0;
static double   cur_sec_rms_acc = 0.0;
static uint32_t cur_sec_count = 0;
static int frames_in_sec = 0;

/* ---- libretro callback stubs ---- */

static retro_environment_t          env_cb;
static retro_video_refresh_t        video_cb;
static retro_audio_sample_t         sample_cb;
static retro_audio_sample_batch_t   batch_cb;
static retro_input_poll_t           poll_cb;
static retro_input_state_t          state_cb;

static void env_log(enum retro_log_level level, const char *fmt, ...)
{
   (void)level;
   (void)fmt;
}

static struct retro_log_callback log_cb = { env_log };
static const char *system_dir = "test/roms/private";
static struct retro_variable last_variable;

static bool environment(unsigned cmd, void *data)
{
   switch (cmd) {
      case RETRO_ENVIRONMENT_GET_LOG_INTERFACE:
         *(struct retro_log_callback *)data = log_cb;
         return true;
      case RETRO_ENVIRONMENT_GET_SYSTEM_DIRECTORY:
         *(const char **)data = system_dir;
         return true;
      case RETRO_ENVIRONMENT_GET_VARIABLE: {
         struct retro_variable *v = (struct retro_variable *)data;
         if (!v || !v->key) return false;
         if (strcmp(v->key, "virtualjaguar_bios") == 0) {
            v->value = g_use_bios ? "enabled" : "disabled";
            return true;
         }
         if (strcmp(v->key, "virtualjaguar_usefastblitter") == 0) {
            v->value = "disabled";
            return true;
         }
         v->value = NULL;
         return false;
      }
      case RETRO_ENVIRONMENT_SET_PIXEL_FORMAT:
      case RETRO_ENVIRONMENT_SET_INPUT_DESCRIPTORS:
      case RETRO_ENVIRONMENT_SET_PERFORMANCE_LEVEL:
      case RETRO_ENVIRONMENT_SET_MEMORY_MAPS:
      case RETRO_ENVIRONMENT_SET_VARIABLES:
      case RETRO_ENVIRONMENT_SET_GEOMETRY:
      case RETRO_ENVIRONMENT_SET_SUPPORT_ACHIEVEMENTS:
      case RETRO_ENVIRONMENT_SET_CORE_OPTIONS_V2:
      case RETRO_ENVIRONMENT_SET_CONTROLLER_INFO:
         return true;
      case RETRO_ENVIRONMENT_GET_VARIABLE_UPDATE: {
         bool *u = (bool *)data;
         if (u) *u = false;
         return true;
      }
      case RETRO_ENVIRONMENT_GET_CAN_DUPE: {
         bool *c = (bool *)data;
         if (c) *c = true;
         return true;
      }
      default:
         return false;
   }
   (void)last_variable;
}

static void video_refresh(const void *data, unsigned width, unsigned height, size_t pitch)
{
   (void)data; (void)width; (void)height; (void)pitch;
}
static void input_poll(void) {}
static int16_t input_state(unsigned port, unsigned device, unsigned index, unsigned id)
{
   (void)port; (void)device; (void)index; (void)id;
   return 0;
}
static void audio_sample_one(int16_t l, int16_t r) { (void)l; (void)r; }

static void account_sample(int16_t l, int16_t r)
{
   int absl = l < 0 ? -l : l;
   int absr = r < 0 ? -r : r;
   int peak = absl > absr ? absl : absr;
   S.total_samples++;
   if (peak > SILENCE_THRESHOLD) {
      S.nonsilent_samples++;
      cur_sec_nonsilent++;
   }
   if (peak > S.peak_global)
      S.peak_global = peak;
   if (peak > cur_sec_peak)
      cur_sec_peak = peak;
   cur_sec_rms_acc += (double)peak * (double)peak;
   cur_sec_count++;
}

static size_t audio_sample_batch(const int16_t *data, size_t frames)
{
   size_t i;
   for (i = 0; i < frames; i++) {
      account_sample(data[i * 2 + 0], data[i * 2 + 1]);
   }
   if (g_wav_fp) {
      fwrite(data, sizeof(int16_t) * 2, frames, g_wav_fp);
      g_wav_samples += frames;
   }
   return frames;
}

/* Write a 44-byte canonical WAV header for stereo s16 at 48kHz.
 * Call after we know the final sample count. */
static void wav_write_header(FILE *f, uint64_t total_stereo_samples)
{
   uint32_t data_bytes = (uint32_t)(total_stereo_samples * 4);
   uint32_t fmt_chunk_size = 16;
   uint32_t riff_size = 4 + (8 + fmt_chunk_size) + (8 + data_bytes);
   uint16_t audio_format = 1;
   uint16_t num_channels = 2;
   uint32_t sample_rate = 48000;
   uint16_t bits_per_sample = 16;
   uint16_t block_align = num_channels * bits_per_sample / 8;
   uint32_t byte_rate = sample_rate * block_align;
   fseek(f, 0, SEEK_SET);
   fwrite("RIFF", 1, 4, f);
   fwrite(&riff_size, 4, 1, f);
   fwrite("WAVE", 1, 4, f);
   fwrite("fmt ", 1, 4, f);
   fwrite(&fmt_chunk_size, 4, 1, f);
   fwrite(&audio_format, 2, 1, f);
   fwrite(&num_channels, 2, 1, f);
   fwrite(&sample_rate, 4, 1, f);
   fwrite(&byte_rate, 4, 1, f);
   fwrite(&block_align, 2, 1, f);
   fwrite(&bits_per_sample, 2, 1, f);
   fwrite("data", 1, 4, f);
   fwrite(&data_bytes, 4, 1, f);
}

static void emit_second(void)
{
   double rms = 0.0;
   uint32_t pct;
   if (cur_sec_count > 0)
      rms = sqrt(cur_sec_rms_acc / (double)cur_sec_count);
   pct = (cur_sec_nonsilent * 100) / (cur_sec_count == 0 ? 1 : cur_sec_count);
   S.per_sec_nonsilent[current_sec] = cur_sec_nonsilent;
   S.per_sec_peak[current_sec]      = cur_sec_peak;
   S.per_sec_rms[current_sec]       = rms;
   if (pct >= 50) S.sustained_seconds++;
   printf("sec=%2d  samples=%5u  nonsilent=%5u (%3u%%)  peak=%6d  rms=%7.1f\n",
         current_sec, cur_sec_count, cur_sec_nonsilent, pct,
         cur_sec_peak, rms);
   fflush(stdout);
   current_sec++;
   cur_sec_nonsilent = 0;
   cur_sec_peak = 0;
   cur_sec_rms_acc = 0.0;
   cur_sec_count = 0;
}

int main(int argc, char **argv)
{
   void *handle;
   const char *core_path;
   FILE *f;
   long sz;
   void *rom_buf;
   struct retro_game_info game;
   int frame;

   /* Symbols */
   void  (*retro_set_environment)(retro_environment_t);
   void  (*retro_set_video_refresh)(retro_video_refresh_t);
   void  (*retro_set_audio_sample)(retro_audio_sample_t);
   void  (*retro_set_audio_sample_batch)(retro_audio_sample_batch_t);
   void  (*retro_set_input_poll)(retro_input_poll_t);
   void  (*retro_set_input_state)(retro_input_state_t);
   void  (*retro_init)(void);
   void  (*retro_deinit)(void);
   bool  (*retro_load_game)(const struct retro_game_info *);
   void  (*retro_unload_game)(void);
   void  (*retro_run)(void);
   void  (*retro_reset)(void);

   if (argc < 4) {
      fprintf(stderr, "Usage: %s <core.dylib> <rom> <seconds> [bios=0|1] [out.wav]\n",
            argv[0]);
      return 2;
   }
   core_path = argv[1];
   g_rom = argv[2];
   g_seconds = atoi(argv[3]);
   if (g_seconds < 1 || g_seconds > 600) g_seconds = 60;
   if (argc >= 5) g_use_bios = atoi(argv[4]) ? 1 : 0;
   if (argc >= 6) {
      g_wav_out = argv[5];
      g_wav_fp = fopen(g_wav_out, "wb");
      if (!g_wav_fp) {
         fprintf(stderr, "open WAV %s: failed\n", g_wav_out);
         return 2;
      }
      /* Reserve 44 bytes for header; we patch on close. */
      {
         char zero[44] = {0};
         fwrite(zero, 1, 44, g_wav_fp);
      }
   }

   handle = dlopen(core_path, RTLD_NOW | RTLD_LOCAL);
   if (!handle) {
      fprintf(stderr, "dlopen failed: %s\n", dlerror());
      return 2;
   }

#define LOAD(s) do { \
      *(void **)&s = dlsym(handle, #s); \
      if (!s) { fprintf(stderr, "dlsym " #s " failed\n"); return 2; } \
   } while (0)
   LOAD(retro_set_environment);
   LOAD(retro_set_video_refresh);
   LOAD(retro_set_audio_sample);
   LOAD(retro_set_audio_sample_batch);
   LOAD(retro_set_input_poll);
   LOAD(retro_set_input_state);
   LOAD(retro_init);
   LOAD(retro_deinit);
   LOAD(retro_load_game);
   LOAD(retro_unload_game);
   LOAD(retro_run);
   LOAD(retro_reset);
#undef LOAD

   env_cb    = environment;
   video_cb  = video_refresh;
   sample_cb = audio_sample_one;
   batch_cb  = audio_sample_batch;
   poll_cb   = input_poll;
   state_cb  = input_state;

   retro_set_environment(env_cb);
   retro_set_video_refresh(video_cb);
   retro_set_audio_sample(sample_cb);
   retro_set_audio_sample_batch(batch_cb);
   retro_set_input_poll(poll_cb);
   retro_set_input_state(state_cb);

   retro_init();

   f = fopen(g_rom, "rb");
   if (!f) { fprintf(stderr, "open ROM failed\n"); return 2; }
   fseek(f, 0, SEEK_END); sz = ftell(f); fseek(f, 0, SEEK_SET);
   rom_buf = malloc((size_t)sz);
   if (fread(rom_buf, 1, (size_t)sz, f) != (size_t)sz) {
      fprintf(stderr, "fread short\n"); return 2;
   }
   fclose(f);

   game.path = g_rom;
   game.data = rom_buf;
   game.size = (size_t)sz;
   game.meta = NULL;

   if (!retro_load_game(&game)) {
      fprintf(stderr, "retro_load_game failed\n");
      return 2;
   }

   memset(&S, 0, sizeof(S));
   printf("=== %s | bios=%d | %d sec | rom=%s ===\n",
         core_path, g_use_bios, g_seconds, g_rom);

   for (frame = 0; frame < g_seconds * FRAMES_PER_SEC; frame++) {
      retro_run();
      frames_in_sec++;
      if (frames_in_sec >= FRAMES_PER_SEC) {
         emit_second();
         frames_in_sec = 0;
      }
   }

   printf("--- TOTAL ---\n");
   printf("total_samples=%llu  nonsilent=%llu (%.1f%%)  peak_global=%d  "
         "sustained_secs=%d/%d\n",
         (unsigned long long)S.total_samples,
         (unsigned long long)S.nonsilent_samples,
         100.0 * (double)S.nonsilent_samples
            / (double)(S.total_samples == 0 ? 1 : S.total_samples),
         S.peak_global,
         S.sustained_seconds, g_seconds);

   if (g_wav_fp) {
      wav_write_header(g_wav_fp, g_wav_samples);
      fclose(g_wav_fp);
      printf("WAV: wrote %llu stereo frames to %s\n",
            (unsigned long long)g_wav_samples, g_wav_out);
   }

   retro_unload_game();
   retro_deinit();
   free(rom_buf);
   dlclose(handle);
   return 0;
}
