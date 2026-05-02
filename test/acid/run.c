/*
 * test/acid/run.c - acid-test harness.
 *
 * Loads a libretro core via dlopen, loads a synthetic .jag test ROM,
 * runs it for a fixed number of frames, then reads the four-word
 * "acid signature" out of main RAM at offset 0x100 and prints
 * PASS / FAIL / NOT-RUN-YET.
 *
 * Usage: run <core.dylib> <test.jag> [num_frames]
 *   num_frames defaults to 600 (10 seconds of emulated time at 60 Hz).
 *
 * Exit codes:
 *   0  PASS
 *   1  FAIL or NOT-RUN-YET
 *   2  harness error (couldn't load core/ROM, etc.)
 *
 * The signature convention is documented in test/acid/include/acid_test.s
 * and test/acid/README.md.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdarg.h>
#include <dlfcn.h>

#include "libretro.h"

/* Acid signature offsets and magic, mirrored from acid_test.s. */
#define ACID_BASE         0x100
#define ACID_RESULT       (ACID_BASE + 0)
#define ACID_DETAIL       (ACID_BASE + 4)
#define ACID_OBSERVED     (ACID_BASE + 8)
#define ACID_EXPECTED     (ACID_BASE + 12)
#define ACID_PASS_MAGIC   0x12345678u
#define ACID_FAIL_MAGIC   0xDEADBEEFu

#define DEFAULT_FRAMES    600

/* Function pointers loaded from the core. */
static void   (*pretro_set_environment)(retro_environment_t);
static void   (*pretro_set_video_refresh)(retro_video_refresh_t);
static void   (*pretro_set_audio_sample)(retro_audio_sample_t);
static void   (*pretro_set_audio_sample_batch)(retro_audio_sample_batch_t);
static void   (*pretro_set_input_poll)(retro_input_poll_t);
static void   (*pretro_set_input_state)(retro_input_state_t);
static void   (*pretro_init)(void);
static void   (*pretro_deinit)(void);
static bool   (*pretro_load_game)(const struct retro_game_info *);
static void   (*pretro_run)(void);
static void   (*pretro_unload_game)(void);
static void  *(*pretro_get_memory_data)(unsigned);
static size_t (*pretro_get_memory_size)(unsigned);

/* libretro callback stubs. */
static void log_printf(enum retro_log_level lvl, const char *fmt, ...)
{
   va_list ap; (void)lvl;
   va_start(ap, fmt);
   vfprintf(stderr, fmt, ap);
   va_end(ap);
}

static bool environment_cb(unsigned cmd, void *data)
{
   switch (cmd)
   {
      case RETRO_ENVIRONMENT_GET_LOG_INTERFACE:
         ((struct retro_log_callback *)data)->log = log_printf;
         return true;
      case RETRO_ENVIRONMENT_SET_PIXEL_FORMAT:
         return true;
      case RETRO_ENVIRONMENT_GET_VARIABLE:
      {
         struct retro_variable *var = (struct retro_variable *)data;
         /* Acid tests don't depend on these, but the core polls
          * them.  Return sane defaults. */
         if (strcmp(var->key, "virtualjaguar_bios") == 0)
            { var->value = "enabled";  return true; }
         if (strcmp(var->key, "virtualjaguar_pal") == 0)
            { var->value = "disabled"; return true; }
         if (strcmp(var->key, "virtualjaguar_usefastblitter") == 0)
            { var->value = "disabled"; return true; } /* accurate by default */
         var->value = NULL;
         return false;
      }
      case RETRO_ENVIRONMENT_GET_VARIABLE_UPDATE:
         *(bool *)data = false; return true;
      case RETRO_ENVIRONMENT_SET_MEMORY_MAPS:
      case RETRO_ENVIRONMENT_SET_SUPPORT_ACHIEVEMENTS:
      case RETRO_ENVIRONMENT_SET_CORE_OPTIONS_V2:
      case RETRO_ENVIRONMENT_SET_CORE_OPTIONS_UPDATE_DISPLAY_CALLBACK:
      case RETRO_ENVIRONMENT_SET_INPUT_DESCRIPTORS:
         return true;
      case RETRO_ENVIRONMENT_GET_CORE_OPTIONS_VERSION:
         *(unsigned *)data = 2; return true;
      case RETRO_ENVIRONMENT_GET_INPUT_BITMASKS:
         return false;
      case RETRO_ENVIRONMENT_GET_SYSTEM_DIRECTORY:
         *(const char **)data = "."; return true;
      case RETRO_ENVIRONMENT_GET_SAVE_DIRECTORY:
         *(const char **)data = "/tmp"; return true;
      default:
         return false;
   }
}

static void  video_refresh(const void *d, unsigned w, unsigned h, size_t p)
{ (void)d; (void)w; (void)h; (void)p; }
static void  audio_sample(int16_t l, int16_t r) { (void)l; (void)r; }
static size_t audio_sample_batch(const int16_t *d, size_t f) { (void)d; return f; }
static void  input_poll(void) { }
static int16_t input_state(unsigned a, unsigned b, unsigned c, unsigned d)
{ (void)a; (void)b; (void)c; (void)d; return 0; }

static const char *result_label(uint32_t magic)
{
   if (magic == ACID_PASS_MAGIC) return "PASS";
   if (magic == ACID_FAIL_MAGIC) return "FAIL";
   if (magic == 0)               return "NOT-RUN-YET";
   return "UNKNOWN";
}

/* Big-endian 32-bit read; main RAM is byte-array, big-endian Jaguar. */
static uint32_t read_be32(const uint8_t *p)
{
   return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16)
        | ((uint32_t)p[2] << 8)  |  (uint32_t)p[3];
}

int main(int argc, char **argv)
{
   void *handle;
   const char *core_path, *rom_path;
   int num_frames = DEFAULT_FRAMES;
   FILE *f;
   long fsize;
   struct retro_game_info info;
   uint8_t *ram;
   size_t ram_size;
   uint32_t result, detail, observed, expected;
   int rc = 1;

   if (argc < 3)
   {
      fprintf(stderr, "Usage: %s <core.dylib|.so> <test.jag> [num_frames]\n",
              argv[0]);
      return 2;
   }
   core_path = argv[1];
   rom_path  = argv[2];
   if (argc >= 4) num_frames = atoi(argv[3]);
   if (num_frames <= 0) num_frames = DEFAULT_FRAMES;

   /* Slurp ROM. */
   f = fopen(rom_path, "rb");
   if (!f) { fprintf(stderr, "ERROR: cannot open %s\n", rom_path); return 2; }
   fseek(f, 0, SEEK_END);
   fsize = ftell(f);
   fseek(f, 0, SEEK_SET);
   if (fsize <= 0)
   {
      fprintf(stderr, "ERROR: ROM is empty or seek failed: %s\n", rom_path);
      fclose(f); return 2;
   }
   info.path = rom_path;
   info.size = (size_t)fsize;
   info.meta = NULL;
   info.data = malloc((size_t)fsize);
   if (!info.data)
   {
      fprintf(stderr, "ERROR: malloc failed for %ld byte ROM\n", fsize);
      fclose(f); return 2;
   }
   if (fread((void *)info.data, 1, (size_t)fsize, f) != (size_t)fsize)
   {
      fprintf(stderr, "ERROR: short read on %s\n", rom_path);
      free((void *)info.data); fclose(f); return 2;
   }
   fclose(f);

   /* Load core. */
   handle = dlopen(core_path, RTLD_LAZY);
   if (!handle)
   {
      fprintf(stderr, "ERROR: dlopen %s: %s\n", core_path, dlerror());
      free((void *)info.data); return 2;
   }

#define LOAD_SYM(s) do { \
      p##s = dlsym(handle, #s); \
      if (!p##s) { \
         fprintf(stderr, "ERROR: missing symbol %s in core\n", #s); \
         dlclose(handle); free((void *)info.data); return 2; \
      } \
   } while (0)
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
#undef LOAD_SYM

   pretro_set_environment(environment_cb);
   pretro_set_video_refresh(video_refresh);
   pretro_set_audio_sample(audio_sample);
   pretro_set_audio_sample_batch(audio_sample_batch);
   pretro_set_input_poll(input_poll);
   pretro_set_input_state(input_state);
   pretro_init();

   if (!pretro_load_game(&info))
   {
      fprintf(stderr, "ERROR: retro_load_game failed for %s\n", rom_path);
      pretro_deinit(); dlclose(handle); free((void *)info.data);
      return 2;
   }

   ram      = (uint8_t *)pretro_get_memory_data(RETRO_MEMORY_SYSTEM_RAM);
   ram_size = pretro_get_memory_size(RETRO_MEMORY_SYSTEM_RAM);
   if (!ram || ram_size < ACID_EXPECTED + 4)
   {
      fprintf(stderr, "ERROR: SYSTEM_RAM unavailable or too small (%zu)\n",
              ram_size);
      pretro_unload_game(); pretro_deinit();
      dlclose(handle); free((void *)info.data); return 2;
   }

   /* Seed the signature block to NOT-RUN-YET so a test that never
    * boots is distinguishable from one that ran but failed silently. */
   memset(ram + ACID_RESULT, 0, 16);

   {
      int i;
      for (i = 0; i < num_frames; i++)
         pretro_run();
   }

   result   = read_be32(ram + ACID_RESULT);
   detail   = read_be32(ram + ACID_DETAIL);
   observed = read_be32(ram + ACID_OBSERVED);
   expected = read_be32(ram + ACID_EXPECTED);

   printf("[%-11s] %s", result_label(result), rom_path);
   if (result == ACID_PASS_MAGIC)
   {
      printf("\n");
      rc = 0;
   }
   else if (result == ACID_FAIL_MAGIC)
   {
      printf(" detail=0x%08x observed=0x%08x expected=0x%08x\n",
             detail, observed, expected);
      rc = 1;
   }
   else
   {
      printf(" (signature=0x%08x -- test never wrote a result; "
             "boot stub or BIOS auth bypass may be broken)\n", result);
      rc = 1;
   }

   pretro_unload_game();
   pretro_deinit();
   free((void *)info.data);
   dlclose(handle);
   return rc;
}
