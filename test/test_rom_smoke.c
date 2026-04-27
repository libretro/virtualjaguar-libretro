/* test_rom_smoke.c -- Batch ROM smoke tester.
 *
 * Loads each ROM, runs N frames, captures boot indicators:
 *   - Did retro_load_game succeed?
 *   - After N frames: PC location, crash/hang, video output
 *   - Prints CSV-style report for easy diffing across builds
 *
 * Build: cc -o test/test_rom_smoke test/test_rom_smoke.c -ldl -O0 -g
 * Usage: ./test/test_rom_smoke <core.dylib> <rom1.jag> [rom2.jag ...]
 *    or: ./test/test_rom_smoke <core.dylib> --dir <rom_directory>
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <dlfcn.h>
#include <stdarg.h>
#include <stdbool.h>
#include <dirent.h>
#include <signal.h>
#include <setjmp.h>
#include "../libretro-common/include/libretro.h"

/* ================================================================
 * Configurable parameters
 * ================================================================ */

#define DEFAULT_FRAMES     60
#define MAX_ROMS          512

/* ================================================================
 * Libretro function pointers
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
static void (*p_retro_reset)(void);
static void *(*p_retro_get_memory_data)(unsigned);
static size_t (*p_retro_get_memory_size)(unsigned);
static size_t (*p_retro_serialize_size)(void);
static bool (*p_retro_serialize)(void *, size_t);
static bool (*p_retro_unserialize)(const void *, size_t);

static void *core_handle;

/* Emulator internals via dlsym */
static uint8_t **p_jaguarMainRAM;
static unsigned (*p_m68k_get_reg)(void *, int);

/* m68k register IDs (from m68kinterface.h) */
#define M68K_REG_PC   16
#define M68K_REG_SR   17

/* ================================================================
 * Video capture — count non-black pixels
 * ================================================================ */

static unsigned frame_width, frame_height;
static unsigned long total_nonblack_pixels;
static unsigned frames_with_video;

static void video_refresh(const void *data, unsigned w, unsigned h, size_t pitch)
{
   if (!data || w == 0 || h == 0)
      return;

   frame_width = w;
   frame_height = h;

   /* Count non-black pixels (XRGB8888) */
   {
      unsigned y;
      unsigned long count = 0;
      const uint8_t *row = (const uint8_t *)data;
      for (y = 0; y < h; y++) {
         const uint32_t *pix = (const uint32_t *)row;
         unsigned x;
         for (x = 0; x < w; x++) {
            if ((pix[x] & 0x00FFFFFF) != 0)
               count++;
         }
         row += pitch;
      }
      total_nonblack_pixels += count;
      if (count > 0)
         frames_with_video++;
   }
}

/* Per-frame video tracking for regression analysis */
static unsigned current_run_frame;
static bool verbose_mode = false;

static void audio_sample(int16_t l, int16_t r) { (void)l; (void)r; }
static size_t audio_batch(const int16_t *d, size_t f) { (void)d; return f; }
static void input_poll(void) {}

/* Input injection: press a button during a range of frames */
#define MAX_INPUT_EVENTS 16
static struct {
   unsigned start_frame;
   unsigned end_frame;
   unsigned retro_btn;
} input_events[MAX_INPUT_EVENTS];
static int input_event_count = 0;

static int16_t input_state(unsigned p, unsigned d, unsigned idx, unsigned id)
{
   int i;
   (void)idx;
   if (p != 0 || d != RETRO_DEVICE_JOYPAD)
      return 0;
   for (i = 0; i < input_event_count; i++) {
      if (id == input_events[i].retro_btn
            && current_run_frame >= input_events[i].start_frame
            && current_run_frame <= input_events[i].end_frame)
         return 1;
   }
   return 0;
}

static unsigned parse_button(const char *name)
{
   if (strcmp(name, "b") == 0 || strcmp(name, "B") == 0)
      return RETRO_DEVICE_ID_JOYPAD_B;
   if (strcmp(name, "a") == 0 || strcmp(name, "A") == 0)
      return RETRO_DEVICE_ID_JOYPAD_A;
   if (strcmp(name, "start") == 0)
      return RETRO_DEVICE_ID_JOYPAD_START;
   if (strcmp(name, "select") == 0)
      return RETRO_DEVICE_ID_JOYPAD_SELECT;
   if (strcmp(name, "up") == 0)
      return RETRO_DEVICE_ID_JOYPAD_UP;
   if (strcmp(name, "down") == 0)
      return RETRO_DEVICE_ID_JOYPAD_DOWN;
   if (strcmp(name, "left") == 0)
      return RETRO_DEVICE_ID_JOYPAD_LEFT;
   if (strcmp(name, "right") == 0)
      return RETRO_DEVICE_ID_JOYPAD_RIGHT;
   if (strcmp(name, "x") == 0 || strcmp(name, "X") == 0)
      return RETRO_DEVICE_ID_JOYPAD_X;
   if (strcmp(name, "y") == 0 || strcmp(name, "Y") == 0)
      return RETRO_DEVICE_ID_JOYPAD_Y;
   return RETRO_DEVICE_ID_JOYPAD_B;
}

static void log_printf(enum retro_log_level level, const char *fmt, ...)
{
   (void)level; (void)fmt;
}

static struct retro_log_callback log_cb = { log_printf };

static bool use_bios = false;
static const char *system_dir = "/tmp";

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
      *(const char **)data = system_dir;
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

/* ================================================================
 * Crash recovery via setjmp/longjmp + signal handler
 * ================================================================ */

static sigjmp_buf jump_buf;
static volatile sig_atomic_t in_test = 0;

static void crash_handler(int sig)
{
   if (in_test)
      siglongjmp(jump_buf, sig);
   /* Re-raise if not in test */
   signal(sig, SIG_DFL);
   raise(sig);
}

/* ================================================================
 * Core loading
 * ================================================================ */

static int load_core(const char *path)
{
   core_handle = dlopen(path, RTLD_NOW);
   if (!core_handle) {
      fprintf(stderr, "ERROR: dlopen(%s): %s\n", path, dlerror());
      return 0;
   }

#define LOAD_SYM(name) \
   p_##name = dlsym(core_handle, #name); \
   if (!p_##name) { fprintf(stderr, "ERROR: missing symbol: %s\n", #name); return 0; }

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
   LOAD_SYM(retro_reset);
   LOAD_SYM(retro_get_memory_data);
   LOAD_SYM(retro_get_memory_size);
   LOAD_SYM(retro_serialize_size);
   LOAD_SYM(retro_serialize);
   LOAD_SYM(retro_unserialize);
#undef LOAD_SYM

   /* Optional internal symbols */
   p_jaguarMainRAM = dlsym(core_handle, "jaguarMainRAM");
   p_m68k_get_reg = dlsym(core_handle, "m68k_get_reg");

   return 1;
}

/* ================================================================
 * ROM file I/O
 * ================================================================ */

static uint8_t *load_rom_file(const char *path, size_t *size_out)
{
   FILE *f;
   long sz;
   uint8_t *buf;

   f = fopen(path, "rb");
   if (!f)
      return NULL;

   fseek(f, 0, SEEK_END);
   sz = ftell(f);
   fseek(f, 0, SEEK_SET);

   if (sz <= 0) {
      fclose(f);
      return NULL;
   }

   buf = (uint8_t *)malloc(sz);
   if (!buf) {
      fclose(f);
      return NULL;
   }

   if (fread(buf, 1, sz, f) != (size_t)sz) {
      free(buf);
      fclose(f);
      return NULL;
   }
   fclose(f);
   *size_out = (size_t)sz;
   return buf;
}

/* Extract just the filename from a path */
static const char *basename_ptr(const char *path)
{
   const char *p = strrchr(path, '/');
   return p ? p + 1 : path;
}

/* ================================================================
 * Boot status classification
 * ================================================================ */

typedef enum {
   BOOT_OK,
   BOOT_LOAD_FAIL,
   BOOT_CRASH,
   BOOT_HUNG,
   BOOT_NO_VIDEO,
   BOOT_BLACK_SCREEN
} boot_status_t;

static const char *status_str(boot_status_t s)
{
   switch (s) {
   case BOOT_OK:          return "OK";
   case BOOT_LOAD_FAIL:   return "LOAD_FAIL";
   case BOOT_CRASH:       return "CRASH";
   case BOOT_HUNG:        return "HUNG";
   case BOOT_NO_VIDEO:    return "NO_VIDEO";
   case BOOT_BLACK_SCREEN: return "BLACK_SCREEN";
   }
   return "UNKNOWN";
}

/* ================================================================
 * Single ROM test
 * ================================================================ */

typedef struct {
   const char *rom_path;
   const char *rom_name;
   boot_status_t status;
   uint32_t final_pc;
   unsigned long nonblack_pixels;
   unsigned video_frames;
   unsigned res_w, res_h;
   int crashed_signal;
} rom_result_t;

static const char *srm_path = NULL;

static void load_srm(void)
{
   void *sram;
   size_t sram_size;
   FILE *f;
   long sz;

   if (!srm_path)
      return;

   sram = p_retro_get_memory_data(RETRO_MEMORY_SAVE_RAM);
   sram_size = p_retro_get_memory_size(RETRO_MEMORY_SAVE_RAM);
   if (!sram || sram_size == 0) {
      fprintf(stderr, "  WARN: Core has no SRAM region\n");
      return;
   }

   f = fopen(srm_path, "rb");
   if (!f) {
      fprintf(stderr, "  WARN: Cannot open SRM: %s\n", srm_path);
      return;
   }
   fseek(f, 0, SEEK_END);
   sz = ftell(f);
   fseek(f, 0, SEEK_SET);
   if (sz > 0) {
      size_t to_read = (size_t)sz < sram_size ? (size_t)sz : sram_size;
      fread(sram, 1, to_read, f);
      if (verbose_mode)
         fprintf(stderr, "  Loaded %zu bytes SRM (core SRAM=%zu)\n", to_read, sram_size);
   }
   fclose(f);
}

static void test_one_rom(const char *path, unsigned num_frames, rom_result_t *result)
{
   uint8_t *rom_data;
   size_t rom_size;
   struct retro_game_info game;
   unsigned i;
   int sig;
   unsigned blank_streak;
   unsigned blank_after_video;

   memset(result, 0, sizeof(*result));
   result->rom_path = path;
   result->rom_name = basename_ptr(path);

   rom_data = load_rom_file(path, &rom_size);
   if (!rom_data) {
      result->status = BOOT_LOAD_FAIL;
      return;
   }

   /* Reset video counters */
   frame_width = 0;
   frame_height = 0;
   total_nonblack_pixels = 0;
   frames_with_video = 0;
   current_run_frame = 0;

   /* Init core */
   p_retro_set_environment(environment);
   p_retro_set_video_refresh(video_refresh);
   p_retro_set_audio_sample(audio_sample);
   p_retro_set_audio_sample_batch(audio_batch);
   p_retro_set_input_poll(input_poll);
   p_retro_set_input_state(input_state);
   p_retro_init();

   /* Load ROM */
   memset(&game, 0, sizeof(game));
   game.path = path;
   game.data = rom_data;
   game.size = rom_size;

   if (!p_retro_load_game(&game)) {
      result->status = BOOT_LOAD_FAIL;
      p_retro_deinit();
      free(rom_data);
      return;
   }

   /* Load SRAM if provided */
   load_srm();

   /* Run frames with crash protection */
   in_test = 1;
   sig = sigsetjmp(jump_buf, 1);
   if (sig != 0) {
      result->status = BOOT_CRASH;
      result->crashed_signal = sig;
      in_test = 0;
      p_retro_unload_game();
      p_retro_deinit();
      free(rom_data);
      return;
   }

   blank_streak = 0;
   blank_after_video = 0;

   for (i = 0; i < num_frames; i++) {
      unsigned long before = total_nonblack_pixels;
      current_run_frame = i;
      p_retro_run();

      if (verbose_mode) {
         unsigned long frame_pixels = total_nonblack_pixels - before;
         uint32_t pc = 0;
         if (p_m68k_get_reg)
            pc = p_m68k_get_reg(NULL, M68K_REG_PC);
         fprintf(stderr, "  frame %4u: %7lu px  PC=0x%06X", i, frame_pixels, pc);
         if (input_event_count > 0) {
            int j;
            for (j = 0; j < input_event_count; j++) {
               if (i >= input_events[j].start_frame && i <= input_events[j].end_frame)
                  fprintf(stderr, "  [BTN %u]", input_events[j].retro_btn);
            }
         }
         fprintf(stderr, "\n");
      }

      /* Track video blackout after video was previously active */
      {
         unsigned long frame_pixels = total_nonblack_pixels - before;
         if (frame_pixels == 0 && frames_with_video > 10) {
            blank_streak++;
            if (blank_streak >= 30)
               blank_after_video = 1;
         } else {
            blank_streak = 0;
         }
      }
   }

   in_test = 0;

   /* Capture final state */
   if (p_m68k_get_reg)
      result->final_pc = p_m68k_get_reg(NULL, M68K_REG_PC);

   result->nonblack_pixels = total_nonblack_pixels;
   result->video_frames = frames_with_video;
   result->res_w = frame_width;
   result->res_h = frame_height;

   /* Classify result */
   if (total_nonblack_pixels > 100 && frames_with_video > 2)
      result->status = BOOT_OK;
   else if (frames_with_video == 0)
      result->status = BOOT_NO_VIDEO;
   else
      result->status = BOOT_BLACK_SCREEN;

   if (blank_after_video && result->status == BOOT_OK)
      result->status = BOOT_BLACK_SCREEN;

   /* Check for hung CPU */
   if (result->final_pc < 0x200 && result->status != BOOT_OK)
      result->status = BOOT_HUNG;

   p_retro_unload_game();
   p_retro_deinit();
   free(rom_data);
}

/* ================================================================
 * Directory scanning
 * ================================================================ */

static int collect_roms_from_dir(const char *dirpath, char **paths, int max)
{
   DIR *dir;
   struct dirent *ent;
   int count = 0;
   char fullpath[4096];

   dir = opendir(dirpath);
   if (!dir) {
      fprintf(stderr, "ERROR: cannot open directory: %s\n", dirpath);
      return 0;
   }

   while ((ent = readdir(dir)) != NULL && count < max) {
      const char *ext;
      size_t len = strlen(ent->d_name);
      if (len < 5)
         continue;
      ext = ent->d_name + len - 4;
      if (strcasecmp(ext, ".jag") != 0 &&
          strcasecmp(ext, ".j64") != 0 &&
          strcasecmp(ext, ".rom") != 0 &&
          strcasecmp(ext, ".bin") != 0 &&
          strcasecmp(ext, ".abs") != 0)
         continue;

      snprintf(fullpath, sizeof(fullpath), "%s/%s", dirpath, ent->d_name);
      paths[count] = strdup(fullpath);
      count++;
   }

   closedir(dir);
   return count;
}

/* ================================================================
 * Main
 * ================================================================ */

int main(int argc, char **argv)
{
   char *rom_paths[MAX_ROMS];
   int rom_count = 0;
   unsigned num_frames = DEFAULT_FRAMES;
   int i;
   int ok_count = 0, fail_count = 0, crash_count = 0;
   const char *core_path;

   if (argc < 3) {
      fprintf(stderr,
         "Usage: %s <core.dylib> [options] <rom.jag ...>\n"
         "       %s <core.dylib> [options] --dir <rom_directory>\n"
         "\nOptions:\n"
         "  --frames N          Run N frames (default %d)\n"
         "  --bios              Use real BIOS\n"
         "  --sysdir DIR        System directory for BIOS files\n"
         "  --srm PATH          Load SRAM data from file\n"
         "  --input F1-F2:BTN   Press BTN during frames F1-F2 (b,a,start,up,down,...)\n"
         "  --verbose           Per-frame video stats to stderr\n",
         argv[0], argv[0], DEFAULT_FRAMES);
      return 1;
   }

   core_path = argv[1];

   /* Parse arguments */
   for (i = 2; i < argc; i++) {
      if (strcmp(argv[i], "--frames") == 0 && i + 1 < argc) {
         num_frames = (unsigned)atoi(argv[++i]);
      } else if (strcmp(argv[i], "--bios") == 0) {
         use_bios = true;
      } else if (strcmp(argv[i], "--sysdir") == 0 && i + 1 < argc) {
         system_dir = argv[++i];
      } else if (strcmp(argv[i], "--srm") == 0 && i + 1 < argc) {
         srm_path = argv[++i];
      } else if (strcmp(argv[i], "--verbose") == 0) {
         verbose_mode = true;
      } else if (strcmp(argv[i], "--input") == 0 && i + 1 < argc) {
         unsigned f1, f2;
         char btn_name[32];
         i++;
         if (sscanf(argv[i], "%u-%u:%31s", &f1, &f2, btn_name) == 3) {
            if (input_event_count < MAX_INPUT_EVENTS) {
               input_events[input_event_count].start_frame = f1;
               input_events[input_event_count].end_frame = f2;
               input_events[input_event_count].retro_btn = parse_button(btn_name);
               input_event_count++;
            }
         } else {
            fprintf(stderr, "WARN: bad --input format: %s (expected F1-F2:BTN)\n", argv[i]);
         }
      } else if (strcmp(argv[i], "--dir") == 0 && i + 1 < argc) {
         rom_count = collect_roms_from_dir(argv[++i], rom_paths, MAX_ROMS);
      } else {
         if (rom_count < MAX_ROMS) {
            rom_paths[rom_count++] = strdup(argv[i]);
         }
      }
   }

   if (rom_count == 0) {
      fprintf(stderr, "ERROR: no ROM files specified\n");
      return 1;
   }

   /* Load core */
   if (!load_core(core_path))
      return 1;

   /* Install crash handlers */
   signal(SIGSEGV, crash_handler);
   signal(SIGBUS, crash_handler);
   signal(SIGFPE, crash_handler);
   signal(SIGABRT, crash_handler);

   /* Print CSV header */
   printf("%-50s  %-12s  PC          NonBlack   VidFrames  Resolution\n",
          "ROM", "Status");
   printf("%-50s  %-12s  ----------  ---------  ---------  ----------\n",
          "--------------------------------------------------",
          "------------");

   /* Test each ROM */
   for (i = 0; i < rom_count; i++) {
      rom_result_t result;
      test_one_rom(rom_paths[i], num_frames, &result);

      printf("%-50s  %-12s  0x%08X  %-9lu  %-9u  %ux%u",
             result.rom_name, status_str(result.status),
             result.final_pc, result.nonblack_pixels,
             result.video_frames, result.res_w, result.res_h);

      if (result.status == BOOT_CRASH)
         printf("  (signal %d)", result.crashed_signal);

      printf("\n");
      fflush(stdout);

      switch (result.status) {
      case BOOT_OK:
         ok_count++;
         break;
      case BOOT_CRASH:
         crash_count++;
         fail_count++;
         break;
      default:
         fail_count++;
         break;
      }
   }

   printf("\n=== Summary: %d ROMs tested, %d OK, %d failed (%d crashes) ===\n",
          rom_count, ok_count, fail_count, crash_count);

   /* Clean up */
   for (i = 0; i < rom_count; i++)
      free(rom_paths[i]);

   dlclose(core_handle);
   return fail_count > 0 ? 1 : 0;
}
