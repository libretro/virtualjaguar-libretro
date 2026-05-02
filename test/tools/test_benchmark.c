/* Headless benchmark tool: loads core via dlopen, runs N frames, and
   reports wall-clock timing (total, FPS, ms/frame).

   Usage: test_benchmark <core.dylib> <rom_file> [num_frames]
          [--blitter fast|accurate] [--warmup N] [--load-srm file]
*/
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <dlfcn.h>
#include <stdint.h>
#include <stdbool.h>

#ifdef __APPLE__
#include <mach/mach_time.h>
#else
#include <time.h>
#endif

#include "../../libretro-common/include/libretro.h"

/* Function pointers loaded from the core */
static void (*pretro_set_environment)(retro_environment_t);
static void (*pretro_set_video_refresh)(retro_video_refresh_t);
static void (*pretro_set_audio_sample)(retro_audio_sample_t);
static void (*pretro_set_audio_sample_batch)(retro_audio_sample_batch_t);
static void (*pretro_set_input_poll)(retro_input_poll_t);
static void (*pretro_set_input_state)(retro_input_state_t);
static void (*pretro_init)(void);
static void (*pretro_deinit)(void);
static bool (*pretro_load_game)(const struct retro_game_info *);
static void (*pretro_run)(void);
static void (*pretro_unload_game)(void);
static void *(*pretro_get_memory_data)(unsigned);
static size_t (*pretro_get_memory_size)(unsigned);
static size_t (*pretro_serialize_size)(void);
static bool (*pretro_unserialize)(const void *, size_t);
/* Optional: only present when the core was built with BENCH_PROFILE=1. */
static void (*pperf_counters_dump)(FILE *);

/* Options state */
static int bios_option_set = 0;
static const char *blitter_value = "enabled"; /* default: fast blitter */

/* High-resolution timer helpers */
#ifdef __APPLE__
static mach_timebase_info_data_t timebase_info;

static uint64_t timer_now(void)
{
   return mach_absolute_time();
}

static double timer_elapsed_sec(uint64_t start, uint64_t end)
{
   uint64_t elapsed = end - start;
   if (timebase_info.denom == 0)
      mach_timebase_info(&timebase_info);
   /* Convert to nanoseconds, then seconds */
   return (double)elapsed * (double)timebase_info.numer / (double)timebase_info.denom / 1e9;
}
#else
static uint64_t timer_now(void)
{
   struct timespec ts;
   clock_gettime(CLOCK_MONOTONIC, &ts);
   return (uint64_t)ts.tv_sec * 1000000000ULL + (uint64_t)ts.tv_nsec;
}

static double timer_elapsed_sec(uint64_t start, uint64_t end)
{
   return (double)(end - start) / 1e9;
}
#endif

static void log_printf(enum retro_log_level level, const char *fmt, ...)
{
   va_list ap;
   const char *lvl_str = "???";
   switch (level) {
      case RETRO_LOG_DEBUG: lvl_str = "DBG"; break;
      case RETRO_LOG_INFO:  lvl_str = "INF"; break;
      case RETRO_LOG_WARN:  lvl_str = "WRN"; break;
      case RETRO_LOG_ERROR: lvl_str = "ERR"; break;
      default: break;
   }
   fprintf(stderr, "[%s] ", lvl_str);
   va_start(ap, fmt);
   vfprintf(stderr, fmt, ap);
   va_end(ap);
}

static bool environment_cb(unsigned cmd, void *data)
{
   switch (cmd)
   {
      case RETRO_ENVIRONMENT_GET_LOG_INTERFACE:
      {
         struct retro_log_callback *cb = (struct retro_log_callback *)data;
         cb->log = log_printf;
         return true;
      }
      case RETRO_ENVIRONMENT_SET_PIXEL_FORMAT:
         return true;
      case RETRO_ENVIRONMENT_GET_VARIABLE:
      {
         struct retro_variable *var = (struct retro_variable *)data;
         if (strcmp(var->key, "virtualjaguar_bios") == 0)
         {
            var->value = "enabled";
            bios_option_set = 1;
            return true;
         }
         if (strcmp(var->key, "virtualjaguar_pal") == 0)
         {
            var->value = "disabled";
            return true;
         }
         if (strcmp(var->key, "virtualjaguar_usefastblitter") == 0)
         {
            var->value = blitter_value;
            return true;
         }
         var->value = NULL;
         return false;
      }
      case RETRO_ENVIRONMENT_GET_VARIABLE_UPDATE:
      {
         bool *updated = (bool *)data;
         *updated = false;
         return true;
      }
      case RETRO_ENVIRONMENT_SET_MEMORY_MAPS:
      case RETRO_ENVIRONMENT_SET_SUPPORT_ACHIEVEMENTS:
      case RETRO_ENVIRONMENT_SET_CORE_OPTIONS_V2:
      case RETRO_ENVIRONMENT_SET_CORE_OPTIONS_UPDATE_DISPLAY_CALLBACK:
         return true;
      case RETRO_ENVIRONMENT_GET_CORE_OPTIONS_VERSION:
      {
         unsigned *version = (unsigned *)data;
         *version = 2;
         return true;
      }
      case RETRO_ENVIRONMENT_SET_INPUT_DESCRIPTORS:
         return true;
      case RETRO_ENVIRONMENT_GET_INPUT_BITMASKS:
         return false;
      case RETRO_ENVIRONMENT_GET_SYSTEM_DIRECTORY:
         *(const char **)data = "test/roms/private";
         return true;
      case RETRO_ENVIRONMENT_GET_SAVE_DIRECTORY:
         *(const char **)data = "/tmp";
         return true;
      default:
         return false;
   }
}

static void video_refresh(const void *data, unsigned width, unsigned height, size_t pitch)
{
   (void)data; (void)width; (void)height; (void)pitch;
}

static void audio_sample(int16_t left, int16_t right)
{
   (void)left; (void)right;
}

static size_t audio_sample_batch(const int16_t *data, size_t frames)
{
   (void)data;
   return frames;
}

static void input_poll(void) {}
static int16_t input_state(unsigned port, unsigned device, unsigned index, unsigned id)
{
   (void)port; (void)device; (void)index; (void)id;
   return 0;
}

static void print_usage(const char *progname)
{
   fprintf(stderr,
      "Usage: %s <core.dylib> <rom_file> [num_frames]\n"
      "       [--blitter fast|accurate] [--warmup N] [--load-srm file]\n"
      "       [--load-state file]\n"
      "\n"
      "Options:\n"
      "  num_frames           Number of frames to benchmark (default: 300)\n"
      "  --blitter fast       Use fast blitter (default)\n"
      "  --blitter accurate   Use accurate (Midsummer2) blitter\n"
      "  --warmup N           Run N warmup frames before timing\n"
      "  --load-srm file      Load EEPROM save data from file\n"
      "  --load-state file    Load a save state into the core after retro_load_game.\n"
      "                       Accepts raw retro_serialize() payloads or RetroArch\n"
      "                       RASTATE container files (the MEM chunk is extracted).\n",
      progname);
}

int main(int argc, char **argv)
{
   void *handle;
   const char *core_path;
   const char *rom_path;
   const char *srm_load_path = NULL;
   const char *state_load_path = NULL;
   struct retro_game_info info;
   FILE *f;
   long fsize;
   int i;
   int num_frames = 300;
   int warmup_frames = 0;
   uint64_t t_start, t_end;
   double elapsed, fps, ms_per_frame;

   if (argc < 3)
   {
      print_usage(argv[0]);
      return 1;
   }

   core_path = argv[1];
   rom_path = argv[2];

   /* Parse optional arguments */
   for (i = 3; i < argc; i++)
   {
      if (strcmp(argv[i], "--blitter") == 0 && i + 1 < argc)
      {
         const char *mode = argv[++i];
         if (strcmp(mode, "fast") == 0)
            blitter_value = "enabled";
         else if (strcmp(mode, "accurate") == 0)
            blitter_value = "disabled";
         else
         {
            fprintf(stderr, "Unknown blitter mode: %s (use 'fast' or 'accurate')\n", mode);
            return 1;
         }
      }
      else if (strcmp(argv[i], "--warmup") == 0 && i + 1 < argc)
         warmup_frames = atoi(argv[++i]);
      else if (strcmp(argv[i], "--load-srm") == 0 && i + 1 < argc)
         srm_load_path = argv[++i];
      else if (strcmp(argv[i], "--load-state") == 0 && i + 1 < argc)
         state_load_path = argv[++i];
      else if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0)
      {
         print_usage(argv[0]);
         return 0;
      }
      else
         num_frames = atoi(argv[i]);
   }

#ifdef __APPLE__
   /* Initialize timebase for mach_absolute_time conversion */
   mach_timebase_info(&timebase_info);
#endif

   /* Load ROM */
   f = fopen(rom_path, "rb");
   if (!f)
   {
      fprintf(stderr, "Cannot open ROM: %s\n", rom_path);
      return 1;
   }
   fseek(f, 0, SEEK_END);
   fsize = ftell(f);
   fseek(f, 0, SEEK_SET);
   info.data = malloc(fsize);
   info.size = fsize;
   info.path = rom_path;
   info.meta = NULL;
   if (fread((void *)info.data, 1, fsize, f) != (size_t)fsize)
   {
      fprintf(stderr, "Failed to read ROM\n");
      fclose(f);
      return 1;
   }
   fclose(f);

   /* Load core */
   handle = dlopen(core_path, RTLD_LAZY);
   if (!handle)
   {
      fprintf(stderr, "dlopen failed: %s\n", dlerror());
      free((void *)info.data);
      return 1;
   }

#define LOAD_SYM(sym) do { \
   p##sym = dlsym(handle, #sym); \
   if (!p##sym) { fprintf(stderr, "Missing symbol: " #sym "\n"); return 1; } \
} while(0)

   LOAD_SYM(retro_set_environment);
   LOAD_SYM(retro_set_video_refresh);
   LOAD_SYM(retro_set_audio_sample);
   LOAD_SYM(retro_set_audio_sample_batch);
   LOAD_SYM(retro_set_input_poll);
   LOAD_SYM(retro_set_input_state);
   LOAD_SYM(retro_init);
   LOAD_SYM(retro_deinit);
   LOAD_SYM(retro_load_game);
   LOAD_SYM(retro_run);
   LOAD_SYM(retro_unload_game);
   LOAD_SYM(retro_get_memory_data);
   LOAD_SYM(retro_get_memory_size);
   LOAD_SYM(retro_serialize_size);
   LOAD_SYM(retro_unserialize);

   /* Optional perf-counter dump; absent unless built with BENCH_PROFILE=1. */
   pperf_counters_dump = dlsym(handle, "perf_counters_dump");

   pretro_set_environment(environment_cb);
   pretro_set_video_refresh(video_refresh);
   pretro_set_audio_sample(audio_sample);
   pretro_set_audio_sample_batch(audio_sample_batch);
   pretro_set_input_poll(input_poll);
   pretro_set_input_state(input_state);

   pretro_init();

   fprintf(stderr, "--- Benchmark configuration ---\n");
   fprintf(stderr, "  Core:    %s\n", core_path);
   fprintf(stderr, "  ROM:     %s\n", rom_path);
   fprintf(stderr, "  Blitter: %s\n",
           strcmp(blitter_value, "enabled") == 0 ? "fast" : "accurate");
   fprintf(stderr, "  BIOS:    %s\n", bios_option_set ? "enabled" : "enabled (pending)");
   fprintf(stderr, "  Warmup:  %d frames\n", warmup_frames);
   fprintf(stderr, "  Measure: %d frames\n", num_frames);
   fprintf(stderr, "---\n");

   if (!pretro_load_game(&info))
   {
      fprintf(stderr, "retro_load_game failed!\n");
      free((void *)info.data);
      dlclose(handle);
      return 1;
   }

   /* Load SRM (EEPROM save) if provided */
   if (srm_load_path)
   {
      void *save_data = pretro_get_memory_data(0);
      size_t save_size = pretro_get_memory_size(0);
      if (save_data && save_size > 0)
      {
         FILE *sf = fopen(srm_load_path, "rb");
         if (sf)
         {
            size_t srm_size;
            fseek(sf, 0, SEEK_END);
            srm_size = (size_t)ftell(sf);
            fseek(sf, 0, SEEK_SET);
            if (srm_size <= save_size)
            {
               if (fread(save_data, 1, srm_size, sf) == srm_size)
                  fprintf(stderr, "--- Loaded SRM from %s (%zu bytes into %zu) ---\n",
                          srm_load_path, srm_size, save_size);
            }
            fclose(sf);
         }
         else
            fprintf(stderr, "WARNING: Cannot open SRM file: %s\n", srm_load_path);
      }
      else
         fprintf(stderr, "WARNING: Core reports no SAVE_RAM area\n");
   }

   /* Load save state if provided.  Accepts both raw retro_serialize()
    * payloads and RetroArch RASTATE container files (extracts the
    * MEM chunk).  See https://github.com/libretro/RetroArch on the
    * RASTATE format. */
   if (state_load_path)
   {
      FILE *stf = NULL;
      long st_total = 0;
      uint8_t *st_buf = NULL;
      const uint8_t *payload = NULL;
      size_t payload_size = 0;
      size_t expected;
      const char *state_err = NULL;

      stf = fopen(state_load_path, "rb");
      if (!stf)
      {
         state_err = "cannot open state file";
         goto state_fail;
      }
      if (fseek(stf, 0, SEEK_END) != 0)
      {
         state_err = "fseek to end failed";
         goto state_fail;
      }
      st_total = ftell(stf);
      if (st_total <= 0)
      {
         state_err = "ftell failed or empty state file";
         goto state_fail;
      }
      if (fseek(stf, 0, SEEK_SET) != 0)
      {
         state_err = "fseek to start failed";
         goto state_fail;
      }
      st_buf = (uint8_t *)malloc((size_t)st_total);
      if (!st_buf)
      {
         state_err = "malloc failed for state buffer";
         goto state_fail;
      }
      if (fread(st_buf, 1, (size_t)st_total, stf) != (size_t)st_total)
      {
         state_err = "short read on state file";
         goto state_fail;
      }
      fclose(stf);
      stf = NULL;
      payload = st_buf;
      payload_size = (size_t)st_total;
      /* RASTATE container? "RASTATE" + 1 version byte, then chunks. */
      if (st_total >= 16 && memcmp(st_buf, "RASTATE", 7) == 0)
      {
         const uint8_t *p = st_buf + 8;       /* past magic+version */
         const uint8_t *end = st_buf + st_total;
         int found = 0;
         /* Each chunk: 4-byte type + 4-byte LE size + payload. */
         while (p + 8 <= end)
         {
            uint32_t chunk_size = (uint32_t)p[4] | ((uint32_t)p[5] << 8)
                               | ((uint32_t)p[6] << 16) | ((uint32_t)p[7] << 24);
            /* Bounds-check the declared chunk size against the buffer. */
            if (chunk_size > (uint32_t)(end - (p + 8)))
            {
               state_err = "RASTATE chunk size overruns buffer";
               goto state_fail;
            }
            if (memcmp(p, "MEM ", 4) == 0)
            {
               payload = p + 8;
               payload_size = chunk_size;
               found = 1;
               break;
            }
            p += 8 + chunk_size;
         }
         if (!found)
         {
            state_err = "no MEM chunk in RASTATE file";
            goto state_fail;
         }
         fprintf(stderr, "--- RASTATE: extracted MEM chunk (%zu bytes) ---\n", payload_size);
      }
      expected = pretro_serialize_size();
      fprintf(stderr, "--- State payload: %zu bytes (core expects %zu) ---\n",
              payload_size, expected);
      if (!pretro_unserialize(payload, payload_size))
      {
         state_err = "retro_unserialize failed";
         goto state_fail;
      }
      fprintf(stderr, "--- State loaded from %s ---\n", state_load_path);
      free(st_buf);
      st_buf = NULL;

      if (0)
      {
state_fail:
         fprintf(stderr, "ERROR: %s: %s\n",
                 state_err ? state_err : "state load error",
                 state_load_path);
         if (stf) fclose(stf);
         if (st_buf) free(st_buf);
         pretro_unload_game();
         pretro_deinit();
         free((void *)info.data);
         dlclose(handle);
         return 1;
      }
   }

   /* Run warmup frames (not timed) */
   if (warmup_frames > 0)
   {
      fprintf(stderr, "--- Running %d warmup frames ---\n", warmup_frames);
      for (i = 0; i < warmup_frames; i++)
         pretro_run();
      fprintf(stderr, "--- Warmup complete ---\n");
   }

   /* Timed run */
   fprintf(stderr, "--- Benchmarking %d frames ---\n", num_frames);
   t_start = timer_now();

   for (i = 0; i < num_frames; i++)
      pretro_run();

   t_end = timer_now();

   elapsed = timer_elapsed_sec(t_start, t_end);
   fps = (double)num_frames / elapsed;
   ms_per_frame = (elapsed * 1000.0) / (double)num_frames;

   /* Print results */
   printf("\n=== BENCHMARK RESULTS ===\n");
   printf("Blitter mode:    %s\n",
          strcmp(blitter_value, "enabled") == 0 ? "fast" : "accurate");
   printf("Frames measured: %d\n", num_frames);
   printf("Warmup frames:   %d\n", warmup_frames);
   printf("Total time:      %.3f s\n", elapsed);
   printf("Frames/sec:      %.2f\n", fps);
   printf("Time/frame:      %.3f ms\n", ms_per_frame);
   printf("=========================\n");

   if (pperf_counters_dump)
      pperf_counters_dump(stderr);

   pretro_unload_game();
   pretro_deinit();
   free((void *)info.data);
   dlclose(handle);

   return 0;
}
