/* test_cd_boot.c -- Minimal test harness for CD boot diagnostics.
 * Build: make -j4 && cc -o test/test_cd_boot test/test_cd_boot.c -L. -lvirtualjaguar_libretro -Wl,-rpath,.
 * Actually, just link against the dylib directly:
 *   cc -o test/test_cd_boot test/test_cd_boot.c -ldl
 * Or use the simpler approach: include retro API and call it. */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dlfcn.h>
#include <stdarg.h>
#include <stdbool.h>
#include "../libretro-common/include/libretro.h"

/* Function pointers for the libretro API */
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
static void (*p_retro_get_system_info)(struct retro_system_info *);
static void (*p_retro_get_system_av_info)(struct retro_system_av_info *);

static unsigned frame_count = 0;
static uint32_t last_frame_hash = 0;
static unsigned width_seen = 0, height_seen = 0;
static bool got_video = false;

static void video_refresh(const void *data, unsigned width, unsigned height, size_t pitch)
{
   if (!data) return;
   got_video = true;
   width_seen = width;
   height_seen = height;

   /* Simple hash of video buffer to detect changes */
   const uint32_t *pixels = (const uint32_t *)data;
   uint32_t hash = 0;
   unsigned total = width * height;
   for (unsigned i = 0; i < total; i += 97)  /* sample every 97th pixel */
      hash = hash * 31 + pixels[i];

   if (hash != last_frame_hash)
   {
      /* Check if frame is all black (or near-black) */
      unsigned nonblack = 0;
      for (unsigned i = 0; i < total; i += 37)
      {
         uint32_t p = pixels[i] & 0x00FFFFFF;
         if (p > 0x010101)
            nonblack++;
      }
      printf("  Frame %u: %ux%u, hash=0x%08X, nonblack_samples=%u/%u\n",
             frame_count, width, height, hash, nonblack, total / 37);
      last_frame_hash = hash;
   }
}

static void audio_sample(int16_t left, int16_t right) { (void)left; (void)right; }
static size_t audio_sample_batch(const int16_t *data, size_t frames) { (void)data; return frames; }
static void input_poll(void) {}
static int16_t input_state(unsigned port, unsigned device, unsigned index, unsigned id)
{
   (void)port; (void)device; (void)index; (void)id;
   return 0;
}

static void log_printf(enum retro_log_level level, const char *fmt, ...)
{
   va_list ap;
   const char *lvl_str[] = {"DEBUG", "INFO", "WARN", "ERROR"};
   printf("[%s] ", lvl_str[level < 4 ? level : 3]);
   va_start(ap, fmt);
   vprintf(fmt, ap);
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
      return true;
   case RETRO_ENVIRONMENT_GET_SYSTEM_DIRECTORY:
      /* Look for BIOS files in test/roms/private or current dir */
      *(const char **)data = "test/roms/private";
      return true;
   case RETRO_ENVIRONMENT_GET_SAVE_DIRECTORY:
      *(const char **)data = ".";
      return true;
   case RETRO_ENVIRONMENT_SET_VARIABLES:
   case RETRO_ENVIRONMENT_SET_CORE_OPTIONS_V2:
      return true;
   case RETRO_ENVIRONMENT_GET_VARIABLE:
   {
      struct retro_variable *var = (struct retro_variable *)data;
      /* Force CD BIOS on */
      if (var->key && strcmp(var->key, "virtualjaguar_bios") == 0)
      {
         var->value = "enabled";
         return true;
      }
      if (var->key && strcmp(var->key, "virtualjaguar_usefastblitter") == 0)
      {
         var->value = "enabled";
         return true;
      }
      var->value = NULL;
      return false;
   }
   case RETRO_ENVIRONMENT_GET_VARIABLE_UPDATE:
      *(bool *)data = false;
      return true;
   default:
      return false;
   }
}

int main(int argc, char *argv[])
{
   if (argc < 2)
   {
      fprintf(stderr, "Usage: %s <path-to-cue-or-chd> [num_frames]\n", argv[0]);
      return 1;
   }

   const char *image_path = argv[1];
   unsigned num_frames = argc > 2 ? atoi(argv[2]) : 300;

   /* Load the core */
   void *handle = dlopen("./virtualjaguar_libretro.dylib", RTLD_NOW);
   if (!handle)
   {
      fprintf(stderr, "Failed to load core: %s\n", dlerror());
      return 1;
   }

#define LOAD_SYM(sym) do { \
   p_##sym = dlsym(handle, #sym); \
   if (!p_##sym) { fprintf(stderr, "Missing symbol: %s\n", #sym); return 1; } \
} while(0)

   LOAD_SYM(retro_init);
   LOAD_SYM(retro_deinit);
   LOAD_SYM(retro_set_environment);
   LOAD_SYM(retro_set_video_refresh);
   LOAD_SYM(retro_set_audio_sample);
   LOAD_SYM(retro_set_audio_sample_batch);
   LOAD_SYM(retro_set_input_poll);
   LOAD_SYM(retro_set_input_state);
   LOAD_SYM(retro_load_game);
   LOAD_SYM(retro_unload_game);
   LOAD_SYM(retro_run);
   LOAD_SYM(retro_get_system_info);
   LOAD_SYM(retro_get_system_av_info);

   p_retro_set_environment(environment);
   p_retro_set_video_refresh(video_refresh);
   p_retro_set_audio_sample(audio_sample);
   p_retro_set_audio_sample_batch(audio_sample_batch);
   p_retro_set_input_poll(input_poll);
   p_retro_set_input_state(input_state);

   p_retro_init();

   struct retro_game_info game = {0};
   game.path = image_path;

   printf("Loading CD image: %s\n", image_path);
   if (!p_retro_load_game(&game))
   {
      fprintf(stderr, "retro_load_game failed!\n");
      p_retro_deinit();
      dlclose(handle);
      return 1;
   }

   printf("Game loaded successfully. Running %u frames...\n", num_frames);

   /* Check initial RAM state */
   /* Access jaguarMainRAM to read vectors */
   uint8_t *(*get_ram)(void) = dlsym(handle, "GetRamPtr");
   if (get_ram)
   {
      uint8_t *ram = get_ram();
      uint32_t sp = (ram[0]<<24) | (ram[1]<<16) | (ram[2]<<8) | ram[3];
      uint32_t pc = (ram[4]<<24) | (ram[5]<<16) | (ram[6]<<8) | ram[7];
      printf("Initial vectors: SP=0x%08X, PC=0x%08X\n", sp, pc);
   }

   for (frame_count = 0; frame_count < num_frames; frame_count++)
   {
      p_retro_run();

      /* Print status at key frames */
      if (frame_count == 0 || frame_count == 10 || frame_count == 30 ||
          frame_count == 60 || frame_count == 120 || frame_count == 299)
      {
         if (!got_video)
            printf("  Frame %u: no video output\n", frame_count);
      }
   }

   printf("\nDone. Total frames: %u\n", num_frames);

   p_retro_unload_game();
   p_retro_deinit();
   dlclose(handle);
   return 0;
}
