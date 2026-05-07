/* Headless screenshot tool: loads core, loads save state, runs N frames,
   dumps framebuffer as PPM.

   Usage: test_screenshot <core.dylib> <rom_file> <state_file> [num_frames]
          [--bios real|hle] [--blitter fast|accurate]
          [--press-a START-END] [--press-b START-END] [--out file.ppm]
*/
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <dlfcn.h>
#include <stdint.h>
#include <stdbool.h>

#include "../../libretro-common/include/libretro.h"
#include "../../src/m68000/m68kinterface.h"

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
static bool (*pretro_unserialize)(const void *, size_t);
static void *(*pretro_get_memory_data)(unsigned);
static size_t (*pretro_get_memory_size)(unsigned);
static uint8_t *pjoypad0Buttons;
static bool *pjoysticksEnabled;
static unsigned int (*pm68k_get_reg)(void *, m68k_register_t);
static uint8_t **pjaguarMainRAM;

static const char *bios_value = "enabled";   /* default: real BIOS */
static const char *blitter_value = "disabled"; /* default: accurate */
static int bios_option_set = 0;
static int current_frame = 0;
static unsigned press_button_id = RETRO_DEVICE_ID_JOYPAD_B;
static int press_button_start = -1;
static int press_button_end = -1;

/* Captured frame */
static uint32_t *last_frame = NULL;
static unsigned last_width = 0, last_height = 0;
static size_t last_pitch = 0;

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
            var->value = bios_value;
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
      case RETRO_ENVIRONMENT_SET_INPUT_DESCRIPTORS:
         return true;
      case RETRO_ENVIRONMENT_GET_CORE_OPTIONS_VERSION:
      {
         unsigned *version = (unsigned *)data;
         *version = 2;
         return true;
      }
      case RETRO_ENVIRONMENT_GET_INPUT_BITMASKS:
         return false;
      case RETRO_ENVIRONMENT_GET_SYSTEM_DIRECTORY:
         *(const char **)data = "test/roms/private";
         return true;
      case RETRO_ENVIRONMENT_GET_SAVE_DIRECTORY:
         *(const char **)data = "/tmp";
         return true;
      case RETRO_ENVIRONMENT_SET_SERIALIZATION_QUIRKS:
         return true;
      default:
         return false;
   }
}

static void video_refresh(const void *data, unsigned width, unsigned height, size_t pitch)
{
   unsigned y;
   if (!data || !width || !height)
      return;

   if (!last_frame || last_width != width || last_height != height)
   {
      free(last_frame);
      last_frame = malloc(width * height * 4);
   }
   last_width = width;
   last_height = height;
   last_pitch = pitch;

   for (y = 0; y < height; y++)
      memcpy(last_frame + y * width, (const uint8_t *)data + y * pitch, width * 4);
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
   (void)port;
   (void)index;
   if (device == RETRO_DEVICE_JOYPAD
         && id == press_button_id
         && current_frame >= press_button_start
         && current_frame <= press_button_end)
      return 1;
   return 0;
}

static int save_ppm(const char *path, const uint32_t *fb, unsigned w, unsigned h)
{
   unsigned x, y;
   FILE *f = fopen(path, "wb");
   if (!f)
      return -1;

   fprintf(f, "P6\n%u %u\n255\n", w, h);
   for (y = 0; y < h; y++)
   {
      for (x = 0; x < w; x++)
      {
         uint32_t px = fb[y * w + x];
         uint8_t rgb[3];
         rgb[0] = (px >> 16) & 0xFF;
         rgb[1] = (px >> 8) & 0xFF;
         rgb[2] = px & 0xFF;
         fwrite(rgb, 1, 3, f);
      }
   }
   fclose(f);
   return 0;
}

static void *read_file(const char *path, size_t *out_size)
{
   FILE *f;
   long sz;
   void *buf;

   f = fopen(path, "rb");
   if (!f) return NULL;
   fseek(f, 0, SEEK_END);
   sz = ftell(f);
   fseek(f, 0, SEEK_SET);
   buf = malloc(sz);
   if (fread(buf, 1, sz, f) != (size_t)sz)
   {
      free(buf);
      fclose(f);
      return NULL;
   }
   fclose(f);
   *out_size = (size_t)sz;
   return buf;
}

int main(int argc, char **argv)
{
   void *handle;
   const char *core_path, *rom_path, *state_path;
   const char *out_path = "/tmp/jaguar_screenshot.ppm";
   struct retro_game_info info;
   size_t fsize, state_size;
   void *rom_data, *state_data;
   int i, num_frames = 3;

   if (argc < 4)
   {
      fprintf(stderr, "Usage: %s <core.dylib> <rom_file> <state_file> [num_frames]\n"
              "       [--bios real|hle] [--blitter fast|accurate]\n"
              "       [--press-a START-END] [--press-b START-END] [--out file.ppm]\n", argv[0]);
      return 1;
   }

   core_path  = argv[1];
   rom_path   = argv[2];
   state_path = argv[3];

   for (i = 4; i < argc; i++)
   {
      if (strcmp(argv[i], "--blitter") == 0 && i + 1 < argc)
      {
         const char *mode = argv[++i];
         if (strcmp(mode, "fast") == 0)
            blitter_value = "enabled";
         else if (strcmp(mode, "accurate") == 0)
            blitter_value = "disabled";
      }
      else if (strcmp(argv[i], "--bios") == 0 && i + 1 < argc)
      {
         const char *mode = argv[++i];
         if (strcmp(mode, "real") == 0)
            bios_value = "enabled";
         else if (strcmp(mode, "hle") == 0)
            bios_value = "disabled";
         else
         {
            fprintf(stderr, "Invalid --bios mode: %s\n", mode);
            return 1;
         }
      }
      else if (strcmp(argv[i], "--out") == 0 && i + 1 < argc)
         out_path = argv[++i];
      else if (strcmp(argv[i], "--press-b") == 0 && i + 1 < argc)
      {
         i++;
         press_button_id = RETRO_DEVICE_ID_JOYPAD_B;
         if (sscanf(argv[i], "%d-%d", &press_button_start, &press_button_end) != 2)
         {
            fprintf(stderr, "Invalid --press-b range: %s\n", argv[i]);
            return 1;
         }
      }
      else if (strcmp(argv[i], "--press-a") == 0 && i + 1 < argc)
      {
         i++;
         press_button_id = RETRO_DEVICE_ID_JOYPAD_A;
         if (sscanf(argv[i], "%d-%d", &press_button_start, &press_button_end) != 2)
         {
            fprintf(stderr, "Invalid --press-a range: %s\n", argv[i]);
            return 1;
         }
      }
      else
         num_frames = atoi(argv[i]);
   }

   /* Load ROM */
   rom_data = read_file(rom_path, &fsize);
   if (!rom_data) { fprintf(stderr, "Cannot read ROM: %s\n", rom_path); return 1; }
   info.data = rom_data;
   info.size = fsize;
   info.path = rom_path;
   info.meta = NULL;

   /* Load save state data (optional: pass empty string "" or "-" to skip) */
   if (state_path && state_path[0] && strcmp(state_path, "-") != 0)
   {
      state_data = read_file(state_path, &state_size);
      if (!state_data) { fprintf(stderr, "Cannot read state: %s\n", state_path); return 1; }

      /* Strip RetroArch "RASTATE" wrapper if present (16-byte header) */
      if (state_size > 16 && memcmp(state_data, "RASTATE", 7) == 0)
      {
         fprintf(stderr, "Detected RetroArch state wrapper, stripping 16-byte header\n");
         memmove(state_data, (uint8_t *)state_data + 16, state_size - 16);
         state_size -= 16;
      }
   }
   else
   {
      state_data = NULL;
      state_size = 0;
   }

   /* Load core */
   handle = dlopen(core_path, RTLD_LAZY);
   if (!handle) { fprintf(stderr, "dlopen: %s\n", dlerror()); return 1; }

#define LOAD_SYM(sym) do { \
   p##sym = dlsym(handle, #sym); \
   if (!p##sym) { fprintf(stderr, "Missing: " #sym "\n"); return 1; } \
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
   LOAD_SYM(retro_unserialize);
   LOAD_SYM(retro_get_memory_data);
   LOAD_SYM(retro_get_memory_size);

   pjoypad0Buttons = dlsym(handle, "joypad0Buttons");
   pjoysticksEnabled = dlsym(handle, "joysticksEnabled");
   pm68k_get_reg = dlsym(handle, "m68k_get_reg");
   pjaguarMainRAM = dlsym(handle, "jaguarMainRAM");

   pretro_set_environment(environment_cb);
   pretro_set_video_refresh(video_refresh);
   pretro_set_audio_sample(audio_sample);
   pretro_set_audio_sample_batch(audio_sample_batch);
   pretro_set_input_poll(input_poll);
   pretro_set_input_state(input_state);

   pretro_init();

   fprintf(stderr, "BIOS: %s\n", strcmp(bios_value, "enabled") == 0 ? "real" : "hle");
   fprintf(stderr, "Blitter: %s\n", strcmp(blitter_value, "enabled") == 0 ? "fast" : "accurate");

   if (!pretro_load_game(&info))
   {
      fprintf(stderr, "retro_load_game failed!\n");
      return 1;
   }

   /* Load save state (skip when --no-state) */
   if (state_data)
   {
      size_t expected = pretro_serialize_size();
      fprintf(stderr, "State file: %zu bytes, core expects: %zu\n", state_size, expected);
      if (state_size > expected)
         state_size = expected;

      if (!pretro_unserialize(state_data, state_size))
      {
         fprintf(stderr, "retro_unserialize failed!\n");
         return 1;
      }
      fprintf(stderr, "Save state loaded OK\n");
   }
   else
   {
      fprintf(stderr, "Cold boot (no state)\n");
   }

   /* Run frames to render */
   fprintf(stderr, "Running %d frames...\n", num_frames);
   for (i = 0; i < num_frames; i++)
   {
      current_frame = i;
      pretro_run();
      if (press_button_start >= 0 && pjoypad0Buttons && pjoysticksEnabled
            && (i < 5 || (i % 30) == 0))
      {
         uint32_t pc = pm68k_get_reg ? pm68k_get_reg(NULL, M68K_REG_PC) : 0;
         uint8_t ram4450 = (pjaguarMainRAM && *pjaguarMainRAM) ? (*pjaguarMainRAM)[0x4450] : 0;
         fprintf(stderr, "[input] frame=%d enabled=%u A=%02X B=%02X\n",
               i, (unsigned)*pjoysticksEnabled,
               (unsigned)pjoypad0Buttons[16], (unsigned)pjoypad0Buttons[17]);
         fprintf(stderr, "[state] frame=%d pc=%06X ram4450=%02X\n",
               i, pc, (unsigned)ram4450);
      }
   }


   /* Save screenshot */
   if (last_frame && last_width && last_height)
   {
      int px, py;
      fprintf(stderr, "Captured %ux%u frame\n", last_width, last_height);
      if (save_ppm(out_path, last_frame, last_width, last_height) == 0)
         fprintf(stderr, "Screenshot saved: %s\n", out_path);

      /* Dump pixel samples from center area (where red noise appears) */
      fprintf(stderr, "--- Pixel samples (center) ---\n");
      for (py = 100; py <= 140; py += 10)
      {
         fprintf(stderr, "y=%d: ", py);
         for (px = 100; px <= 200; px += 20)
         {
            if ((unsigned)px < last_width && (unsigned)py < last_height)
            {
               uint32_t p = last_frame[py * last_width + px];
               fprintf(stderr, "(%d,%d)=%06X ", px, py, p & 0xFFFFFF);
            }
         }
         fprintf(stderr, "\n");
      }
   }
   else
      fprintf(stderr, "No frame captured!\n");

   pretro_unload_game();
   pretro_deinit();
   free(rom_data);
   free(state_data);
   free(last_frame);
   dlclose(handle);
   return 0;
}
