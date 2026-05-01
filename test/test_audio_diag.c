/* Minimal headless test: load core with BIOS enabled, run frames, check audio logs */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <dlfcn.h>
#include <stdint.h>
#include <stdbool.h>

#include "../libretro-common/include/libretro.h"

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

static int total_audio_frames = 0;
static int nonzero_audio_frames = 0;
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
         if (strcmp(var->key, "virtualjaguar_usefastblitter") == 0)
         {
            var->value = "enabled";
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
   size_t i;
   int has_nonzero = 0;
   total_audio_frames++;
   for (i = 0; i < frames * 2; i++)
   {
      if (data[i] != 0)
      {
         has_nonzero = 1;
         break;
      }
   }
   if (has_nonzero)
      nonzero_audio_frames++;
   return frames;
}

static void input_poll(void) {}
static int16_t input_state(unsigned port, unsigned device, unsigned index, unsigned id)
{
   (void)port; (void)device; (void)index; (void)id;
   return 0;
}

int main(int argc, char **argv)
{
   void *handle;
   const char *core_path;
   const char *rom_path;
   struct retro_game_info info;
   FILE *f;
   long fsize;
   int i;
   int num_frames = 300;

   if (argc < 3)
   {
      fprintf(stderr, "Usage: %s <core.dylib> <rom_file> [num_frames]\n", argv[0]);
      return 1;
   }

   core_path = argv[1];
   rom_path = argv[2];
   if (argc > 3) num_frames = atoi(argv[3]);

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
   fread((void*)info.data, 1, fsize, f);
   fclose(f);

   /* Load core */
   handle = dlopen(core_path, RTLD_LAZY);
   if (!handle) { fprintf(stderr, "dlopen failed: %s\n", dlerror()); return 1; }

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

   fprintf(stderr, "--- Running %d frames ---\n", num_frames);
   for (i = 0; i < num_frames; i++)
   {
      pretro_run();
   }

   fprintf(stderr, "\n=== AUDIO SUMMARY ===\n");
   fprintf(stderr, "Total audio_batch calls: %d\n", total_audio_frames);
   fprintf(stderr, "Non-zero audio frames:   %d\n", nonzero_audio_frames);
   fprintf(stderr, "Result: %s\n",
           nonzero_audio_frames > 0 ? "AUDIO PRESENT" : "ALL SILENT");

   pretro_unload_game();
   pretro_deinit();
   free((void*)info.data);
   dlclose(handle);

   return nonzero_audio_frames > 0 ? 0 : 1;
}
