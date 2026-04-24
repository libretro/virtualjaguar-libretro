/* test_rom_boot.c -- Minimal cart ROM boot tracer.
 * Build: cc -o test/test_rom_boot test/test_rom_boot.c -ldl
 * Usage: ./test/test_rom_boot <rom.jag> [num_frames] */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dlfcn.h>
#include <stdarg.h>
#include <stdbool.h>
#include "../libretro-common/include/libretro.h"

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

static unsigned int (*p_m68k_get_reg)(void *, int);

static unsigned frame_count = 0;
static unsigned nonblack_count = 0;

static void video_refresh(const void *data, unsigned w, unsigned h, size_t pitch)
{
   if (!data) return;
   const uint32_t *px = (const uint32_t *)data;
   unsigned total = w * h;
   unsigned nb = 0;
   for (unsigned i = 0; i < total; i += 37)
      if ((px[i] & 0x00FFFFFF) > 0x010101) nb++;
   nonblack_count = nb;
}

static void audio_sample(int16_t l, int16_t r) { (void)l; (void)r; }
static size_t audio_batch(const int16_t *d, size_t f) { (void)d; return f; }
static void input_poll(void) {}
static int16_t input_state(unsigned p, unsigned d, unsigned i, unsigned id)
{ (void)p; (void)d; (void)i; (void)id; return 0; }

static void log_printf(enum retro_log_level level, const char *fmt, ...)
{
   if (level < RETRO_LOG_WARN) return;
   va_list ap;
   const char *ls[] = {"DBG", "INF", "WRN", "ERR"};
   printf("[%s] ", ls[level < 4 ? level : 3]);
   va_start(ap, fmt);
   vprintf(fmt, ap);
   va_end(ap);
}

static struct retro_log_callback log_cb = { log_printf };

static bool environment(unsigned cmd, void *data)
{
   switch (cmd) {
   case RETRO_ENVIRONMENT_GET_LOG_INTERFACE:
      *(struct retro_log_callback *)data = log_cb;
      return true;
   case RETRO_ENVIRONMENT_SET_PIXEL_FORMAT:
      return true;
   case RETRO_ENVIRONMENT_GET_SYSTEM_DIRECTORY:
      *(const char **)data = "test/roms/private";
      return true;
   case RETRO_ENVIRONMENT_GET_SAVE_DIRECTORY:
      *(const char **)data = "/tmp";
      return true;
   case RETRO_ENVIRONMENT_SET_VARIABLES:
   case RETRO_ENVIRONMENT_SET_CORE_OPTIONS_V2:
      return true;
   case RETRO_ENVIRONMENT_GET_VARIABLE: {
      struct retro_variable *var = (struct retro_variable *)data;
      if (var->key && strcmp(var->key, "virtualjaguar_bios") == 0) {
         const char *env = getenv("VJ_USE_BIOS");
         var->value = (env && strcmp(env, "1") == 0) ? "enabled" : "disabled";
         return true;
      }
      if (var->key && strcmp(var->key, "virtualjaguar_usefastblitter") == 0) {
         var->value = "enabled";
         return true;
      }
      var->value = NULL;
      return false;
   }
   case RETRO_ENVIRONMENT_GET_VARIABLE_UPDATE:
      *(bool *)data = false;
      return true;
   case RETRO_ENVIRONMENT_SET_MEMORY_MAPS:
   case RETRO_ENVIRONMENT_SET_INPUT_DESCRIPTORS:
   case RETRO_ENVIRONMENT_SET_SERIALIZATION_QUIRKS:
   case RETRO_ENVIRONMENT_SET_GEOMETRY:
   case RETRO_ENVIRONMENT_GET_CORE_OPTIONS_VERSION:
   case RETRO_ENVIRONMENT_GET_PREFERRED_HW_RENDER:
      return true;
   default:
      return false;
   }
}

int main(int argc, char *argv[])
{
   if (argc < 2) {
      fprintf(stderr, "Usage: %s <rom.jag> [num_frames]\n", argv[0]);
      return 1;
   }

   const char *rom_path = argv[1];
   unsigned num_frames = argc > 2 ? atoi(argv[2]) : 120;

   /* Read ROM into memory */
   FILE *f = fopen(rom_path, "rb");
   if (!f) { fprintf(stderr, "Cannot open: %s\n", rom_path); return 1; }
   fseek(f, 0, SEEK_END);
   size_t rom_size = ftell(f);
   fseek(f, 0, SEEK_SET);
   uint8_t *rom_data = malloc(rom_size);
   fread(rom_data, 1, rom_size, f);
   fclose(f);
   printf("ROM: %s (%zu bytes)\n", rom_path, rom_size);

   void *handle = dlopen("./virtualjaguar_libretro.dylib", RTLD_NOW);
   if (!handle) { fprintf(stderr, "dlopen: %s\n", dlerror()); return 1; }

#define LOAD(sym) do { \
   p_##sym = dlsym(handle, #sym); \
   if (!p_##sym) { fprintf(stderr, "Missing: %s\n", #sym); return 1; } \
} while(0)

   LOAD(retro_init);
   LOAD(retro_deinit);
   LOAD(retro_set_environment);
   LOAD(retro_set_video_refresh);
   LOAD(retro_set_audio_sample);
   LOAD(retro_set_audio_sample_batch);
   LOAD(retro_set_input_poll);
   LOAD(retro_set_input_state);
   LOAD(retro_load_game);
   LOAD(retro_unload_game);
   LOAD(retro_run);

   p_m68k_get_reg = dlsym(handle, "m68k_get_reg");

   /* Also get direct RAM pointer for inspection */
   uint8_t **p_jaguarMainRAM = dlsym(handle, "jaguarMainRAM");
   uint32_t *p_dsp_control = dlsym(handle, "dsp_control");
   uint32_t *p_dsp_pc = dlsym(handle, "dsp_pc");
   uint8_t *(*p_DSPGetRAM)(void) = dlsym(handle, "DSPGetRAM");
   uint32_t *p_dsp_reg_bank_0 = dlsym(handle, "dsp_reg_bank_0");

   p_retro_set_environment(environment);
   p_retro_set_video_refresh(video_refresh);
   p_retro_set_audio_sample(audio_sample);
   p_retro_set_audio_sample_batch(audio_batch);
   p_retro_set_input_poll(input_poll);
   p_retro_set_input_state(input_state);
   p_retro_init();

   struct retro_game_info game = {0};
   game.path = rom_path;
   game.data = rom_data;
   game.size = rom_size;

   if (!p_retro_load_game(&game)) {
      fprintf(stderr, "retro_load_game FAILED\n");
      p_retro_deinit();
      return 1;
   }

   printf("Loaded. Running %u frames...\n", num_frames);

   uint32_t prev_pc = 0xFFFFFFFF;
   unsigned stuck_count = 0;

   for (frame_count = 0; frame_count < num_frames; frame_count++) {
      p_retro_run();

      uint32_t pc = 0, sp = 0, sr = 0;
      uint32_t d0 = 0, d1 = 0, a0 = 0, a6 = 0;
      if (p_m68k_get_reg) {
         pc = p_m68k_get_reg(NULL, 16);
         sp = p_m68k_get_reg(NULL, 18);
         sr = p_m68k_get_reg(NULL, 17);
         d0 = p_m68k_get_reg(NULL, 0);
         d1 = p_m68k_get_reg(NULL, 1);
         a0 = p_m68k_get_reg(NULL, 8);
         a6 = p_m68k_get_reg(NULL, 14);
      }

      if (frame_count < 10 || frame_count % 10 == 0 || pc != prev_pc) {
         printf("F%03u: PC=%08X SP=%08X SR=%04X D0=%08X A0=%08X A6=%08X nb=%u",
                frame_count, pc, sp, sr & 0xFFFF, d0, a0, a6, nonblack_count);
         if (p_dsp_control)
            printf(" DSP:ctrl=%08X pc=%08X run=%d",
               *p_dsp_control, p_dsp_pc ? *p_dsp_pc : 0, (*p_dsp_control & 0x01) ? 1 : 0);
         printf("\n");
      }

      if (pc == prev_pc) {
         stuck_count++;
         if (stuck_count >= 500) {
            printf("*** CPU stuck at PC=%08X for %u frames ***\n", pc, stuck_count);
            /* Dump memory around PC */
            if (p_jaguarMainRAM && pc < 0x200000) {
               uint8_t *ram = *p_jaguarMainRAM;
               uint32_t dump_start = (pc >= 16) ? pc - 16 : 0;
               printf("  RAM[PC-16..PC+16]:\n");
               for (int row = 0; row < 4; row++) {
                  uint32_t addr = dump_start + row * 8;
                  printf("    %06X: ", addr);
                  for (int j = 0; j < 8; j++)
                     printf("%02X ", ram[addr + j]);
                  printf(" %s\n", addr == pc ? "<-- PC" : (addr + 8 > pc && addr <= pc) ? "<-- PC in row" : "");
               }
            }
            /* Check if in ROM space */
            if (pc >= 0x800000 && pc < 0xC00000) {
               unsigned off = pc - 0x800000;
               if (off + 16 <= rom_size) {
                  printf("  ROM[PC]: ");
                  for (int j = 0; j < 16; j++)
                     printf("%02X ", rom_data[off + j]);
                  printf("\n");
               }
            }
            /* Dump DSP state */
            if (p_dsp_control) {
               printf("  DSP ctrl=%08X pc=%08X run=%d\n",
                  *p_dsp_control, p_dsp_pc ? *p_dsp_pc : 0, (*p_dsp_control & 1) ? 1 : 0);
            }
            if (p_DSPGetRAM) {
               uint8_t *dsp_ram = p_DSPGetRAM();
               if (dsp_ram) {
                  /* Dump DSP interrupt vectors ($F1B000-$F1B05F) */
                  printf("  DSP vectors (F1B000-F1B05F):\n");
                  for (int row = 0; row < 6; row++) {
                     printf("    F1B%03X: ", row * 16);
                     for (int j = 0; j < 16; j++)
                        printf("%02X ", dsp_ram[row * 16 + j]);
                     printf("\n");
                  }
                  /* Dump DSP code around PC ($F1B780-$F1B7A0) */
                  printf("  DSP code (F1B780-F1B7BF):\n");
                  for (int row = 0; row < 4; row++) {
                     unsigned off = 0x780 + row * 16;
                     printf("    F1B%03X: ", off);
                     for (int j = 0; j < 16; j++)
                        printf("%02X ", dsp_ram[off + j]);
                     printf("\n");
                  }
                  /* Dump flag area ($F1B9D0-$F1B9FF) */
                  printf("  DSP flag area (F1B9D0-F1B9FF):\n");
                  for (int row = 0; row < 3; row++) {
                     unsigned off = 0x9D0 + row * 16;
                     printf("    F1B%03X: ", off);
                     for (int j = 0; j < 16; j++)
                        printf("%02X ", dsp_ram[off + j]);
                     printf("\n");
                  }
               }
            }
            if (p_dsp_reg_bank_0) {
               printf("  DSP regs: R0=%08X R1=%08X R30=%08X R31=%08X\n",
                  p_dsp_reg_bank_0[0], p_dsp_reg_bank_0[1],
                  p_dsp_reg_bank_0[30], p_dsp_reg_bank_0[31]);
            }
            break;
         }
      } else {
         stuck_count = 0;
      }
      prev_pc = pc;
   }

   printf("\nFinal state after %u frames:\n", frame_count);
   if (p_m68k_get_reg) {
      printf("  PC=%08X SP=%08X\n",
         p_m68k_get_reg(NULL, 16), p_m68k_get_reg(NULL, 18));
      printf("  D0-D3: %08X %08X %08X %08X\n",
         p_m68k_get_reg(NULL, 0), p_m68k_get_reg(NULL, 1),
         p_m68k_get_reg(NULL, 2), p_m68k_get_reg(NULL, 3));
      printf("  A0-A3: %08X %08X %08X %08X\n",
         p_m68k_get_reg(NULL, 8), p_m68k_get_reg(NULL, 9),
         p_m68k_get_reg(NULL, 10), p_m68k_get_reg(NULL, 11));
   }
   printf("  nonblack pixels: %u\n", nonblack_count);

   p_retro_unload_game();
   p_retro_deinit();
   dlclose(handle);
   free(rom_data);
   return 0;
}
