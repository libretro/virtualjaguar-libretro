/*
 * test_option_visibility.c — content-type-dependent core option visibility.
 *
 * Covers the dynamic-options half of #251: CD-only options must be hidden
 * when a cartridge is loaded, and the cartridge-only BIOS option must be
 * hidden for CD content (there, CD Boot Mode drives the boot ROM and the
 * cartridge setting is ignored by ResolveBootConfig).  With no content
 * loaded every option stays visible, so nothing is unreachable while the
 * user configures ahead of loading.
 *
 * Build:
 *   cc -O2 -Wall -std=c99 -I. -I./libretro-common/include \
 *      -o test/tools/test_option_visibility \
 *      test/tools/test_option_visibility.c -ldl
 *
 * Usage: test_option_visibility <core> <cart.j64> [disc.cue]
 * The disc argument is optional; the CD half is skipped without it.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdbool.h>
#include <dlfcn.h>

#include <libretro.h>

#define MAX_REC 64

static struct { char key[64]; bool visible; } rec[MAX_REC];
static unsigned rec_count;
static int failures;
static int checks;

static void record(const char *key, bool visible)
{
   unsigned i;
   for (i = 0; i < rec_count; i++)
   {
      if (!strcmp(rec[i].key, key))
      {
         rec[i].visible = visible;
         return;
      }
   }
   if (rec_count < MAX_REC)
   {
      strncpy(rec[rec_count].key, key, sizeof(rec[0].key) - 1);
      rec[rec_count].key[sizeof(rec[0].key) - 1] = '\0';
      rec[rec_count].visible = visible;
      rec_count++;
   }
}

/* Options default to visible: the core only calls SET_CORE_OPTIONS_DISPLAY
 * when a state CHANGES, so "never mentioned" means visible. */
static bool visible_of(const char *key)
{
   unsigned i;
   for (i = 0; i < rec_count; i++)
      if (!strcmp(rec[i].key, key))
         return rec[i].visible;
   return true;
}

static void expect(const char *what, const char *key, bool want)
{
   bool got = visible_of(key);
   checks++;
   if (got == want)
   {
      printf("  ok   %-28s %s visible=%d\n", key, what, (int)got);
   }
   else
   {
      printf("  FAIL %-28s %s visible=%d, expected %d\n",
             key, what, (int)got, (int)want);
      failures++;
   }
}

static void log_cb(enum retro_log_level level, const char *fmt, ...)
{
   (void)level; (void)fmt;
}

static bool env_cb(unsigned cmd, void *data)
{
   switch (cmd)
   {
      case RETRO_ENVIRONMENT_GET_LOG_INTERFACE:
         ((struct retro_log_callback *)data)->log = log_cb;
         return true;

      case RETRO_ENVIRONMENT_GET_SYSTEM_DIRECTORY:
      case RETRO_ENVIRONMENT_GET_SAVE_DIRECTORY:
         *(const char **)data = ".";
         return true;

      case RETRO_ENVIRONMENT_SET_PIXEL_FORMAT:
      case RETRO_ENVIRONMENT_SET_MEMORY_MAPS:
      case RETRO_ENVIRONMENT_SET_SUPPORT_ACHIEVEMENTS:
      case RETRO_ENVIRONMENT_SET_CORE_OPTIONS_V2:
      case RETRO_ENVIRONMENT_SET_CORE_OPTIONS_UPDATE_DISPLAY_CALLBACK:
         return true;

      case RETRO_ENVIRONMENT_GET_CORE_OPTIONS_VERSION:
         *(unsigned *)data = 2;
         return true;

      case RETRO_ENVIRONMENT_SET_CORE_OPTIONS_DISPLAY:
      {
         const struct retro_core_option_display *d =
            (const struct retro_core_option_display *)data;
         if (d && d->key)
            record(d->key, d->visible);
         return true;
      }

      case RETRO_ENVIRONMENT_GET_VARIABLE:
      {
         struct retro_variable *v = (struct retro_variable *)data;
         v->value = NULL;
         return true;
      }
   }
   return false;
}

static void video_cb(const void *d, unsigned w, unsigned h, size_t p)
{ (void)d; (void)w; (void)h; (void)p; }
static void audio_cb(int16_t l, int16_t r) { (void)l; (void)r; }
static size_t audio_batch_cb(const int16_t *d, size_t f) { (void)d; return f; }
static void input_poll_cb(void) {}
static int16_t input_state_cb(unsigned a, unsigned b, unsigned c, unsigned d)
{ (void)a; (void)b; (void)c; (void)d; return 0; }

typedef void (*set_env_fn)(retro_environment_t);
typedef void (*set_video_fn)(retro_video_refresh_t);
typedef void (*set_audio_fn)(retro_audio_sample_t);
typedef void (*set_audio_batch_fn)(retro_audio_sample_batch_t);
typedef void (*set_ipoll_fn)(retro_input_poll_t);
typedef void (*set_istate_fn)(retro_input_state_t);
typedef void (*void_fn)(void);
typedef bool (*load_fn)(const struct retro_game_info *);

static load_fn  p_load;
static void_fn  p_unload;

static bool load_content(const char *path)
{
   struct retro_game_info gi;
   uint8_t *buf = NULL;
   long     len = 0;
   bool     ok;
   FILE    *f;

   memset(&gi, 0, sizeof(gi));
   gi.path = path;

   /* Cartridges are handed over as data; disc images are opened by path. */
   f = fopen(path, "rb");
   if (f)
   {
      fseek(f, 0, SEEK_END);
      len = ftell(f);
      fseek(f, 0, SEEK_SET);
      if (len > 0 && len < 32 * 1024 * 1024 && !strstr(path, ".cue"))
      {
         buf = (uint8_t *)malloc((size_t)len);
         if (buf && fread(buf, 1, (size_t)len, f) == (size_t)len)
         {
            gi.data = buf;
            gi.size = (size_t)len;
         }
      }
      fclose(f);
   }

   rec_count = 0;              /* fresh recording per load */
   ok = p_load(&gi);
   free(buf);
   return ok;
}

int main(int argc, char **argv)
{
   void *lib;
   const char *core = (argc > 1) ? argv[1] : "./virtualjaguar_libretro.dylib";
   const char *cart = (argc > 2) ? argv[2] : NULL;
   const char *disc = (argc > 3) ? argv[3] : NULL;

   /* The Makefile always passes a disc argument; it expands to an empty
    * string when the private CD tree is absent (as on CI).  Treat that as
    * "no disc" rather than handing retro_load_game an empty path. */
   if (cart && !*cart)
      cart = NULL;
   if (disc && !*disc)
      disc = NULL;

   printf("=== Core Option Visibility ===\n");

   if (!cart)
   {
      printf("  SKIP: no cartridge ROM supplied\n");
      return 0;
   }

   lib = dlopen(core, RTLD_NOW);
   if (!lib)
   {
      printf("  FAIL: dlopen %s: %s\n", core, dlerror());
      return 1;
   }

   ((set_env_fn)dlsym(lib, "retro_set_environment"))(env_cb);
   ((set_video_fn)dlsym(lib, "retro_set_video_refresh"))(video_cb);
   ((set_audio_fn)dlsym(lib, "retro_set_audio_sample"))(audio_cb);
   ((set_audio_batch_fn)dlsym(lib, "retro_set_audio_sample_batch"))(audio_batch_cb);
   ((set_ipoll_fn)dlsym(lib, "retro_set_input_poll"))(input_poll_cb);
   ((set_istate_fn)dlsym(lib, "retro_set_input_state"))(input_state_cb);
   ((void_fn)dlsym(lib, "retro_init"))();

   p_load   = (load_fn)dlsym(lib, "retro_load_game");
   p_unload = (void_fn)dlsym(lib, "retro_unload_game");

   /* --- cartridge: CD options hidden, cartridge BIOS option shown --- */
   if (!load_content(cart))
   {
      printf("  FAIL: could not load cartridge %s\n", cart);
      dlclose(lib);
      return 1;
   }
   printf("[cartridge] %s\n", cart);
   expect("(cart)", "virtualjaguar_cd_boot_mode",  false);
   expect("(cart)", "virtualjaguar_cd_bios_type",  false);
   expect("(cart)", "virtualjaguar_cd_read_speed", false);
   expect("(cart)", "virtualjaguar_cd_trace",      false);
   expect("(cart)", "virtualjaguar_bios",          true);
   expect("(cart)", "virtualjaguar_bios_type",     true);
   p_unload();

   /* --- CD: CD options shown, cartridge BIOS option hidden --- */
   if (disc)
   {
      if (!load_content(disc))
      {
         /* Not a visibility regression — some images in the private tree
          * legitimately fail to load (see #230).  Disc compatibility is
          * covered by the CD boot suites; skip rather than fail here. */
         printf("[disc] SKIP: %s did not load\n", disc);
      }
      else
      {
         printf("[disc] %s\n", disc);
         expect("(cd)", "virtualjaguar_cd_boot_mode",  true);
         expect("(cd)", "virtualjaguar_cd_bios_type",  true);
         expect("(cd)", "virtualjaguar_cd_read_speed", true);
         expect("(cd)", "virtualjaguar_cd_trace",      true);
         expect("(cd)", "virtualjaguar_bios",          false);
         expect("(cd)", "virtualjaguar_bios_type",     false);
         p_unload();
      }
   }
   else
   {
      printf("[disc] SKIP: no disc image supplied\n");
   }

   ((void_fn)dlsym(lib, "retro_deinit"))();
   dlclose(lib);

   printf("--- Core Option Visibility: %d checks, %d failed ---\n",
          checks, failures);
   return failures ? 1 : 0;
}
