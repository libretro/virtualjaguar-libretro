/* test_video_dim_log.c — log every video resolution change.
 *
 * Diagnostic for issue #138 (Pitfall crash).  RetroArch log shows
 * framebuffer churn (320/326/652) for Pitfall — the user reports a
 * black-screen "crash" after ~45 sec.  Dimension changes are
 * expensive and may indicate a runtime visible-window problem
 * introduced by v2.3.0's PR #164 (derive visible window from
 * VDB/VDE/HDB/HDE).  This tool runs N frames and prints a row each
 * time width/height changes, so we can see at what frame and how
 * often the geometry oscillates.
 *
 * Build: cc -O2 -Wall -std=c99 -I.. -I../src -I../libretro-common/include \
 *           -o test_video_dim_log test_video_dim_log.c -ldl
 * Usage: ./test_video_dim_log <core> <rom> [--frames N] [--bios]
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <dlfcn.h>
#include "../../libretro-common/include/libretro.h"

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

static unsigned current_frame = 0;
static unsigned last_w = 0, last_h = 0;
static unsigned change_count = 0;
static unsigned long long total_pixels = 0;
static unsigned video_calls = 0;
static unsigned black_streak = 0;
static unsigned longest_black_streak = 0;
static int first_black_streak_frame = -1;

static void video_refresh(const void *data, unsigned w, unsigned h, size_t pitch)
{
   const uint32_t *p;
   uint32_t i, n;
   uint32_t nonblack = 0;

   video_calls++;
   if (w != last_w || h != last_h)
   {
      printf("  [frame %5u] dim: %ux%u -> %ux%u (change #%u)\n",
             current_frame, last_w, last_h, w, h, ++change_count);
      last_w = w; last_h = h;
   }
   if (!data) return;

   p = (const uint32_t *)data;
   n = w * h;
   if (n > 320*240) n = 320*240; /* cap loop work */
   for (i = 0; i < n; i++)
   {
      uint32_t v = p[i] & 0x00FFFFFF;
      if (v != 0) { nonblack++; if (nonblack > 4) break; }
   }
   total_pixels += w * h;

   if (nonblack <= 4)
   {
      if (black_streak == 0 && first_black_streak_frame < 0)
         first_black_streak_frame = (int)current_frame;
      black_streak++;
      if (black_streak > longest_black_streak)
         longest_black_streak = black_streak;
   }
   else
   {
      if (black_streak >= 30)
         printf("  [frame %5u] black streak ended after %u frames (started ~%u)\n",
                current_frame, black_streak, current_frame - black_streak);
      black_streak = 0;
   }
}

static void audio_sample(int16_t l, int16_t r) { (void)l; (void)r; }
static size_t audio_batch(const int16_t *d, size_t f) { (void)d; return f; }
static void input_poll(void) {}
static int16_t input_state(unsigned a, unsigned b, unsigned c, unsigned d)
{ (void)a; (void)b; (void)c; (void)d; return 0; }

static int use_bios = 0;
static struct retro_log_callback log_cb = { 0 };
static void log_printf(enum retro_log_level lv, const char *fmt, ...)
{ (void)lv; (void)fmt; }

static bool environment(unsigned cmd, void *data)
{
   switch (cmd)
   {
   case RETRO_ENVIRONMENT_GET_LOG_INTERFACE:
      log_cb.log = log_printf;
      *(struct retro_log_callback *)data = log_cb;
      return true;
   case RETRO_ENVIRONMENT_SET_PIXEL_FORMAT:
   case RETRO_ENVIRONMENT_SET_VARIABLES:
   case RETRO_ENVIRONMENT_SET_CORE_OPTIONS_V2:
   case RETRO_ENVIRONMENT_GET_VARIABLE_UPDATE:
   case RETRO_ENVIRONMENT_SET_MEMORY_MAPS:
   case RETRO_ENVIRONMENT_SET_INPUT_DESCRIPTORS:
   case RETRO_ENVIRONMENT_SET_SERIALIZATION_QUIRKS:
   case RETRO_ENVIRONMENT_GET_CORE_OPTIONS_VERSION:
   case RETRO_ENVIRONMENT_GET_PREFERRED_HW_RENDER:
      return true;
   case RETRO_ENVIRONMENT_SET_GEOMETRY:
   {
      const struct retro_game_geometry *g = (const struct retro_game_geometry *)data;
      printf("  [frame %5u] SET_GEOMETRY: %ux%u, aspect %.3f\n",
             current_frame, g->base_width, g->base_height, g->aspect_ratio);
      return true;
   }
   case RETRO_ENVIRONMENT_SET_SYSTEM_AV_INFO:
   {
      const struct retro_system_av_info *av = (const struct retro_system_av_info *)data;
      printf("  [frame %5u] SET_SYSTEM_AV_INFO: %ux%u, aspect %.3f, fps %.2f\n",
             current_frame, av->geometry.base_width, av->geometry.base_height,
             av->geometry.aspect_ratio, av->timing.fps);
      return true;
   }
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

static bool load_core(const char *path)
{
   core_handle = dlopen(path, RTLD_NOW);
   if (!core_handle) { fprintf(stderr, "dlopen: %s\n", dlerror()); return false; }
#define LOAD(s) do { \
   p_##s = dlsym(core_handle, #s); \
   if (!p_##s) { \
      fprintf(stderr, "missing %s\n", #s); \
      dlclose(core_handle); core_handle = NULL; \
      return false; \
   } \
} while (0)
   LOAD(retro_init); LOAD(retro_deinit);
   LOAD(retro_set_environment); LOAD(retro_set_video_refresh);
   LOAD(retro_set_audio_sample); LOAD(retro_set_audio_sample_batch);
   LOAD(retro_set_input_poll); LOAD(retro_set_input_state);
   LOAD(retro_load_game); LOAD(retro_unload_game); LOAD(retro_run);
#undef LOAD
   return true;
}

int main(int argc, char **argv)
{
   const char *core = NULL, *rom = NULL;
   unsigned frames = 600;
   FILE *fp;
   long sz;
   unsigned char *buf;
   struct retro_game_info game;
   int i;

   for (i = 1; i < argc; i++)
   {
      const char *a = argv[i];
      if (a[0] != '-') { if (!core) core = a; else if (!rom) rom = a; continue; }
      if (!strcmp(a, "--bios")) use_bios = 1;
      else if (!strcmp(a, "--frames") && i + 1 < argc) frames = (unsigned)atoi(argv[++i]);
   }
   if (!core || !rom) { fprintf(stderr, "Usage: %s <core> <rom> [--frames N] [--bios]\n", argv[0]); return 2; }

   fp = fopen(rom, "rb");
   if (!fp) { fprintf(stderr, "open %s: fail\n", rom); return 2; }
   if (fseek(fp, 0, SEEK_END) != 0) { fclose(fp); fprintf(stderr, "fseek: fail\n"); return 2; }
   sz = ftell(fp);
   if (sz <= 0) { fclose(fp); fprintf(stderr, "ftell: bad size %ld\n", sz); return 2; }
   if (fseek(fp, 0, SEEK_SET) != 0) { fclose(fp); fprintf(stderr, "fseek rewind: fail\n"); return 2; }
   buf = malloc((size_t)sz);
   if (!buf) { fclose(fp); fprintf(stderr, "malloc(%ld): fail\n", sz); return 2; }
   if (fread(buf, 1, (size_t)sz, fp) != (size_t)sz)
   { fclose(fp); free(buf); fprintf(stderr, "fread: short read\n"); return 2; }
   fclose(fp);

   if (!load_core(core)) { free(buf); return 1; }
   p_retro_set_environment(environment);
   p_retro_init();
   p_retro_set_video_refresh(video_refresh);
   p_retro_set_audio_sample(audio_sample);
   p_retro_set_audio_sample_batch(audio_batch);
   p_retro_set_input_poll(input_poll);
   p_retro_set_input_state(input_state);

   memset(&game, 0, sizeof(game));
   game.path = rom; game.data = buf; game.size = (size_t)sz;
   if (!p_retro_load_game(&game))
   {
      fprintf(stderr, "load_game failed\n");
      p_retro_deinit();
      dlclose(core_handle); core_handle = NULL;
      free(buf);
      return 1;
   }

   printf("=== Video dim log: %s, %u frames, BIOS=%s ===\n",
          rom, frames, use_bios ? "on" : "off");
   for (current_frame = 0; current_frame < frames; current_frame++)
      p_retro_run();

   printf("\n=== Summary ===\n");
   printf("  Total dim changes:      %u\n", change_count);
   printf("  Video callback calls:   %u\n", video_calls);
   printf("  Final dimensions:       %ux%u\n", last_w, last_h);
   printf("  Longest black streak:   %u frames", longest_black_streak);
   if (first_black_streak_frame >= 0)
      printf(" (first started ~frame %d)\n", first_black_streak_frame);
   else
      printf("\n");

   p_retro_unload_game();
   p_retro_deinit();
   dlclose(core_handle); core_handle = NULL;
   free(buf);
   return 0;
}
