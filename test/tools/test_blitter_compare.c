/* Blitter comparison test: loads core, enables comparison mode, runs frames,
   reports which blit operations produce different results between the fast
   (old) blitter and the accurate (Midsummer2) blitter.

   Usage: test_blitter_compare <core.dylib> <rom_file> [num_frames]
*/
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <dlfcn.h>
#include <stdint.h>
#include <stdbool.h>

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
static size_t (*pretro_serialize_size)(void);
static bool (*pretro_serialize)(void *, size_t);
static bool (*pretro_unserialize)(const void *, size_t);
static void *(*pretro_get_memory_data)(unsigned);
static size_t (*pretro_get_memory_size)(unsigned);

/* Blitter comparison API */
static void (*pBlitterCompareEnable)(int);
static int (*pBlitterCompareIsEnabled)(void);
static void (*pBlitterCompareGetStats)(uint32_t *, uint32_t *, uint32_t *);
static void (*pBlitterCompareDumpCmdStats)(void);

static int bios_option_set = 0;

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
         /* Do NOT set usefastblitter - comparison mode handles both */
         if (strcmp(var->key, "virtualjaguar_usefastblitter") == 0)
         {
            var->value = "disabled";
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

static int current_frame = 0;

/* Scripted input: frame range, device, id, value */
struct input_event {
   int start_frame;
   int end_frame;
   unsigned device;
   unsigned id;
   int16_t value;
};

#define MAX_INPUT_EVENTS 32
static struct input_event input_script[MAX_INPUT_EVENTS];
static int input_script_count = 0;

static void add_input(int start, int end, unsigned device, unsigned id, int16_t val)
{
   if (input_script_count < MAX_INPUT_EVENTS)
   {
      input_script[input_script_count].start_frame = start;
      input_script[input_script_count].end_frame = end;
      input_script[input_script_count].device = device;
      input_script[input_script_count].id = id;
      input_script[input_script_count].value = val;
      input_script_count++;
   }
}

static void input_poll(void) {}
static int16_t input_state(unsigned port, unsigned device, unsigned index, unsigned id)
{
   int i;
   (void)index;
   if (port != 0)
      return 0;
   for (i = 0; i < input_script_count; i++)
   {
      if (current_frame >= input_script[i].start_frame &&
          current_frame <= input_script[i].end_frame &&
          device == input_script[i].device &&
          id == input_script[i].id)
         return input_script[i].value;
   }
   return 0;
}

int main(int argc, char **argv)
{
   void *handle;
   const char *core_path;
   const char *rom_path;
   const char *state_load_path = NULL;
   const char *state_save_path = NULL;
   const char *srm_load_path = NULL;
   struct retro_game_info info;
   FILE *f;
   long fsize;
   int i;
   int num_frames = 60;
   int warmup_frames = 0;
   uint32_t total, diffs, skipped;

   if (argc < 3)
   {
      fprintf(stderr, "Usage: %s <core.dylib> <rom_file> [num_frames] "
              "[--load-state file] [--save-state file] [--load-srm file] [--warmup N]\n", argv[0]);
      return 1;
   }

   core_path = argv[1];
   rom_path = argv[2];
   for (i = 3; i < argc; i++)
   {
      if (strcmp(argv[i], "--load-state") == 0 && i + 1 < argc)
         state_load_path = argv[++i];
      else if (strcmp(argv[i], "--save-state") == 0 && i + 1 < argc)
         state_save_path = argv[++i];
      else if (strcmp(argv[i], "--load-srm") == 0 && i + 1 < argc)
         srm_load_path = argv[++i];
      else if (strcmp(argv[i], "--warmup") == 0 && i + 1 < argc)
         warmup_frames = atoi(argv[++i]);
      else
         num_frames = atoi(argv[i]);
   }

   /* Load ROM */
   f = fopen(rom_path, "rb");
   if (!f) { fprintf(stderr, "Cannot open ROM: %s\n", rom_path); return 1; }
   fseek(f, 0, SEEK_END);
   fsize = ftell(f);
   fseek(f, 0, SEEK_SET);
   info.data = malloc(fsize);
   info.size = fsize;
   info.path = rom_path;
   info.meta = NULL;
   if (fread((void*)info.data, 1, fsize, f) != (size_t)fsize)
   {
      fprintf(stderr, "Failed to read ROM\n");
      fclose(f);
      return 1;
   }
   fclose(f);

   /* Load core */
   handle = dlopen(core_path, RTLD_LAZY);
   if (!handle) { fprintf(stderr, "dlopen failed: %s\n", dlerror()); return 1; }

#define LOAD_SYM(sym) do { \
   p##sym = dlsym(handle, #sym); \
   if (!p##sym) { fprintf(stderr, "Missing symbol: " #sym "\n"); return 1; } \
} while(0)

#define LOAD_SYM_OPT(sym) do { \
   p##sym = dlsym(handle, #sym); \
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
   LOAD_SYM(retro_serialize_size);
   LOAD_SYM(retro_serialize);
   LOAD_SYM(retro_unserialize);
   LOAD_SYM(retro_get_memory_data);
   LOAD_SYM(retro_get_memory_size);

   /* Load blitter comparison symbols */
   LOAD_SYM_OPT(BlitterCompareEnable);
   LOAD_SYM_OPT(BlitterCompareIsEnabled);
   LOAD_SYM_OPT(BlitterCompareGetStats);
   LOAD_SYM_OPT(BlitterCompareDumpCmdStats);

   if (!pBlitterCompareEnable || !pBlitterCompareGetStats)
   {
      fprintf(stderr, "Core does not export BlitterCompare symbols. Rebuild core.\n");
      free((void*)info.data);
      dlclose(handle);
      return 1;
   }

   pretro_set_environment(environment_cb);
   pretro_set_video_refresh(video_refresh);
   pretro_set_audio_sample(audio_sample);
   pretro_set_audio_sample_batch(audio_sample_batch);
   pretro_set_input_poll(input_poll);
   pretro_set_input_state(input_state);

   pretro_init();

   fprintf(stderr, "--- Loading game (BIOS %s) ---\n",
           bios_option_set ? "ENABLED" : "disabled");

   if (!pretro_load_game(&info))
   {
      fprintf(stderr, "retro_load_game failed!\n");
      free((void*)info.data);
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

   /* Load save state if provided */
   if (state_load_path)
   {
      size_t sz;
      void *state_buf;
      FILE *sf = fopen(state_load_path, "rb");
      if (!sf)
      {
         fprintf(stderr, "Cannot open state file: %s\n", state_load_path);
         pretro_unload_game();
         pretro_deinit();
         free((void*)info.data);
         dlclose(handle);
         return 1;
      }
      fseek(sf, 0, SEEK_END);
      sz = (size_t)ftell(sf);
      fseek(sf, 0, SEEK_SET);
      state_buf = malloc(sz);
      if (fread(state_buf, 1, sz, sf) == sz)
      {
         /* RetroArch wraps savestates with a 16-byte "RASTATE\x01MEM "
          * header.  Strip it so the core's retro_unserialize sees just
          * its own VJSS payload. */
         void *unser_buf = state_buf;
         size_t unser_sz = sz;
         if (sz > 16 && memcmp(state_buf, "RASTATE", 7) == 0)
         {
            unser_buf = (uint8_t *)state_buf + 16;
            unser_sz  = sz - 16;
         }
         if (pretro_unserialize(unser_buf, unser_sz))
            fprintf(stderr, "--- Loaded save state from %s (%zu bytes) ---\n", state_load_path, unser_sz);
         else
            fprintf(stderr, "WARNING: retro_unserialize failed for %s\n", state_load_path);
      }
      fclose(sf);
      free(state_buf);
   }

   /* Set up warmup input: press Start at frame 30 to get past title screen */
   if (warmup_frames > 0 && !state_load_path)
   {
      add_input(30, 32, RETRO_DEVICE_JOYPAD, RETRO_DEVICE_ID_JOYPAD_START, 1);
   }

   /* Run warmup frames without comparison (to reach desired game state) */
   if (warmup_frames > 0)
   {
      fprintf(stderr, "--- Running %d warmup frames ---\n", warmup_frames);
      for (i = 0; i < warmup_frames; i++)
      {
         current_frame = i;
         pretro_run();
      }
      input_script_count = 0;
   }

   /* Save state if requested (useful for creating a state at the right moment) */
   if (state_save_path)
   {
      size_t sz = pretro_serialize_size();
      void *state_buf = malloc(sz);
      if (pretro_serialize(state_buf, sz))
      {
         FILE *sf = fopen(state_save_path, "wb");
         if (sf)
         {
            fwrite(state_buf, 1, sz, sf);
            fclose(sf);
            fprintf(stderr, "--- Saved state to %s (%zu bytes) ---\n", state_save_path, sz);
         }
      }
      free(state_buf);
   }

   /* Set up default input script for AVP:
      - Frame 0-2: Press keyboard 8 to toggle map overlay
      (If loading a state, the game should already be in-level) */
   if (!state_load_path)
   {
      add_input(60, 62, RETRO_DEVICE_JOYPAD, RETRO_DEVICE_ID_JOYPAD_START, 1);
      add_input(180, 182, RETRO_DEVICE_KEYBOARD, RETROK_8, 1);
   }
   else
   {
      add_input(5, 7, RETRO_DEVICE_KEYBOARD, RETROK_8, 1);
   }

   /* Enable blitter comparison mode */
   pBlitterCompareEnable(1);
   fprintf(stderr, "--- Blitter comparison enabled ---\n");
   fprintf(stderr, "--- Running %d frames ---\n", num_frames);

   for (i = 0; i < num_frames; i++)
   {
      current_frame = i;
      pretro_run();

      /* Print periodic stats */
      if ((i + 1) % 30 == 0)
      {
         pBlitterCompareGetStats(&total, &diffs, &skipped);
         fprintf(stderr, "[Frame %d] blits=%u diffs=%u skipped=%u\n",
                 i + 1, (unsigned)total, (unsigned)diffs, (unsigned)skipped);
      }
   }

   /* Final stats */
   pBlitterCompareGetStats(&total, &diffs, &skipped);
   if (pBlitterCompareDumpCmdStats)
      pBlitterCompareDumpCmdStats();
   fprintf(stderr, "\n=== BLITTER COMPARISON SUMMARY ===\n");
   fprintf(stderr, "Total blits compared:  %u\n", (unsigned)total);
   fprintf(stderr, "Blits with differences: %u\n", (unsigned)diffs);
   fprintf(stderr, "Blits skipped:         %u\n", (unsigned)skipped);
   if (total > 0)
      fprintf(stderr, "Difference rate:       %.2f%%\n",
              100.0 * diffs / total);
   fprintf(stderr, "Result: %s\n",
           diffs > 0 ? "DIFFERENCES FOUND" : "IDENTICAL");

   pBlitterCompareEnable(0);
   pretro_unload_game();
   pretro_deinit();
   free((void*)info.data);
   dlclose(handle);

   return diffs > 0 ? 1 : 0;
}
