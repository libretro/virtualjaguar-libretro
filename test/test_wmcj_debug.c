/* test_wmcj_debug.c -- Trace WMCJ HLE boot to find where it gets stuck.
 * Build: cc -o test/test_wmcj_debug test/test_wmcj_debug.c -ldl -O0 -g
 * Usage: ./test/test_wmcj_debug <core.dylib> <wmcj.jag>
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dlfcn.h>
#include <stdbool.h>
#include <stdarg.h>
#include "../libretro-common/include/libretro.h"

static bool use_bios = false;
static const char *system_dir = "/tmp";

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

static unsigned (*p_m68k_get_reg)(void *, int);
static uint8_t **p_jaguarMainRAM;
static uint32_t *p_pcQueue;
static uint32_t *p_pcQPtr;
static uint32_t *p_a6Queue;
static uint32_t *p_d0Queue;

/* DSP registers via TOM/JERRY read functions */
static uint16_t (*p_JERRYReadWord)(uint32_t, uint32_t);
static uint8_t (*p_JERRYReadByte)(uint32_t, uint32_t);
static uint32_t (*p_GPUReadLong)(uint32_t, uint32_t);
static uint8_t *(*p_DSPGetRAM)(void);
static bool (*p_DSPIsRunning)(void);
static uint32_t (*p_DSPReadLong)(uint32_t, uint32_t);

#define M68K_REG_PC   16
#define M68K_REG_A6   14
#define M68K_REG_SP   15

static void video_refresh(const void *d, unsigned w, unsigned h, size_t p)
{ (void)d; (void)w; (void)h; (void)p; }
static void audio_sample(int16_t l, int16_t r) { (void)l; (void)r; }
static size_t audio_batch(const int16_t *d, size_t f) { (void)d; return f; }
static void input_poll(void) {}
static int16_t input_state(unsigned p, unsigned d, unsigned i, unsigned id)
{ (void)p; (void)d; (void)i; (void)id; return 0; }
static void log_printf(enum retro_log_level lv, const char *fmt, ...) {
   va_list ap;
   (void)lv;
   va_start(ap, fmt);
   vfprintf(stderr, fmt, ap);
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

#define LOADSYM(h, name) do { \
   *(void **)&p_##name = dlsym(h, #name); \
   if (!p_##name) { fprintf(stderr, "dlsym(%s) failed: %s\n", #name, dlerror()); exit(1); } \
} while(0)

#define LOADSYM_OPT(h, name) (*(void **)&p_##name = dlsym(h, #name))

int main(int argc, char **argv)
{
   void *handle;
   struct retro_game_info info;
   FILE *fp;
   int frame;
   uint32_t pc, sp, a6, qptr;
   int i;

   {
      int a;
      for (a = 1; a < argc; a++) {
         if (strcmp(argv[a], "--bios") == 0) use_bios = true;
         else if (strcmp(argv[a], "--sysdir") == 0 && a+1 < argc) system_dir = argv[++a];
      }
   }
   if (argc < 3) {
      fprintf(stderr, "Usage: %s <core.dylib> [--bios] [--sysdir DIR] <rom.jag>\n", argv[0]);
      return 1;
   }

   handle = dlopen(argv[1], RTLD_NOW);
   if (!handle) { fprintf(stderr, "dlopen: %s\n", dlerror()); return 1; }

   LOADSYM(handle, retro_init);
   LOADSYM(handle, retro_deinit);
   LOADSYM(handle, retro_set_environment);
   LOADSYM(handle, retro_set_video_refresh);
   LOADSYM(handle, retro_set_audio_sample);
   LOADSYM(handle, retro_set_audio_sample_batch);
   LOADSYM(handle, retro_set_input_poll);
   LOADSYM(handle, retro_set_input_state);
   LOADSYM(handle, retro_load_game);
   LOADSYM(handle, retro_unload_game);
   LOADSYM(handle, retro_run);
   LOADSYM(handle, m68k_get_reg);

   LOADSYM_OPT(handle, jaguarMainRAM);
   LOADSYM_OPT(handle, pcQueue);
   LOADSYM_OPT(handle, pcQPtr);
   LOADSYM_OPT(handle, a6Queue);
   LOADSYM_OPT(handle, d0Queue);
   LOADSYM_OPT(handle, JERRYReadWord);
   LOADSYM_OPT(handle, JERRYReadByte);
   LOADSYM_OPT(handle, GPUReadLong);
   LOADSYM_OPT(handle, DSPGetRAM);
   LOADSYM_OPT(handle, DSPIsRunning);
   LOADSYM_OPT(handle, DSPReadLong);

   p_retro_set_environment(environment);
   p_retro_set_video_refresh(video_refresh);
   p_retro_set_audio_sample(audio_sample);
   p_retro_set_audio_sample_batch(audio_batch);
   p_retro_set_input_poll(input_poll);
   p_retro_set_input_state(input_state);
   p_retro_init();

   memset(&info, 0, sizeof(info));
   info.path = argv[argc - 1];
   fp = fopen(argv[argc - 1], "rb");
   if (!fp) { perror("fopen"); return 1; }
   fseek(fp, 0, SEEK_END);
   info.size = ftell(fp);
   fseek(fp, 0, SEEK_SET);
   info.data = malloc(info.size);
   fread((void *)info.data, 1, info.size, fp);
   fclose(fp);

   if (!p_retro_load_game(&info)) {
      fprintf(stderr, "retro_load_game failed\n");
      return 1;
   }

   printf("=== WMCJ HLE boot trace ===\n");
   for (frame = 0; frame < 120; frame++) {
      p_retro_run();
      pc = p_m68k_get_reg(NULL, M68K_REG_PC);
      sp = p_m68k_get_reg(NULL, M68K_REG_SP);
      a6 = p_m68k_get_reg(NULL, M68K_REG_A6);
      if (frame < 15 || frame % 10 == 0 || frame == 119) {
         bool dsp_run = p_DSPIsRunning ? p_DSPIsRunning() : false;
         uint32_t dsp_val = p_DSPReadLong ? p_DSPReadLong(0xF1B9D8, 0) : 0xDEAD;
         printf("Frame %3d: PC=$%08X  SP=$%08X  DSP=%s  $B9D8=$%08X\n",
                frame, pc, sp, dsp_run ? "RUN" : "off", dsp_val);
      }
   }

   /* Dump the last 32 entries from pcQueue */
   if (p_pcQueue && p_pcQPtr) {
      qptr = *p_pcQPtr;
      printf("\n=== Last 32 PCs from pcQueue (qptr=%u) ===\n", qptr);
      for (i = 31; i >= 0; i--) {
         uint32_t idx = (qptr - 1 - i) & 0x3FF;
         uint32_t qpc = p_pcQueue[idx];
         printf("  [-%2d] PC=$%08X", i, qpc);
         if (p_a6Queue)
            printf("  A6=$%08X", p_a6Queue[idx]);
         if (p_d0Queue)
            printf("  D0=$%08X", p_d0Queue[idx]);
         printf("\n");
      }
   }

   /* Dump RAM around where the game might be looping */
   if (p_jaguarMainRAM) {
      uint8_t *ram = *p_jaguarMainRAM;
      pc = p_m68k_get_reg(NULL, M68K_REG_PC);
      printf("\n=== RAM at PC=$%08X (16 bytes) ===\n", pc);
      if (pc < 0x200000) {
         for (i = 0; i < 16; i++)
            printf("%02X ", ram[pc + i]);
         printf("\n");
      }

      /* Dump vector 64 ($100) */
      printf("\n=== Vector 64 ($100) = $%02X%02X%02X%02X ===\n",
             ram[0x100], ram[0x101], ram[0x102], ram[0x103]);

      /* Dump first 8 bytes of RAM (SP + PC vectors) */
      printf("=== SP vector ($0) = $%02X%02X%02X%02X ===\n",
             ram[0], ram[1], ram[2], ram[3]);
      printf("=== PC vector ($4) = $%02X%02X%02X%02X ===\n",
             ram[4], ram[5], ram[6], ram[7]);
   }

   /* Dump DSP CTRL trace log */
   {
      struct { uint32_t data, who, old_ctrl, new_ctrl; } *log;
      int *log_count;
      log = dlsym(handle, "dsp_ctrl_log");
      log_count = dlsym(handle, "dsp_ctrl_log_count");
      if (log && log_count) {
         printf("\n=== DSP_CTRL write log (%d entries) ===\n", *log_count);
         for (i = 0; i < *log_count && i < 64; i++)
            printf("  [%2d] who=%u data=$%08X  old=$%08X → new=$%08X  DSPGO=%d→%d\n",
                   i, log[i].who, log[i].data, log[i].old_ctrl, log[i].new_ctrl,
                   (log[i].old_ctrl & 1), (log[i].new_ctrl & 1));
      }
   }

   /* Check DSP state */
   if (p_JERRYReadWord) {
      uint16_t dsp_ctrl_lo = p_JERRYReadWord(0xF1A114, 0);
      uint16_t dsp_ctrl_hi = p_JERRYReadWord(0xF1A116, 0);
      uint16_t dsp_flags = p_JERRYReadWord(0xF1A100, 0);
      printf("\n=== DSP State ===\n");
      printf("DSP_CTRL = $%04X%04X (bit0=running: %s)\n",
             dsp_ctrl_hi, dsp_ctrl_lo,
             (dsp_ctrl_lo & 1) ? "YES" : "NO");
      printf("DSP_FLAGS = $%04X\n", dsp_flags);
   }
   if (p_DSPIsRunning) {
      printf("DSPIsRunning() = %s\n", p_DSPIsRunning() ? "YES" : "NO");
   }
   if (p_DSPReadLong) {
      printf("DSP RAM $F1B9D8 via DSPReadLong = $%08X\n",
             p_DSPReadLong(0xF1B9D8, 0));
   }
   if (p_DSPGetRAM) {
      uint8_t *dsp_ram = p_DSPGetRAM();
      printf("\n=== DSP RAM at $F1B9D0 (32 bytes) ===\n");
      for (i = 0; i < 32; i++) {
         printf("%02X ", dsp_ram[0x9D0 + i]);
         if ((i & 15) == 15) printf("\n");
      }
      printf("\n=== DSP RAM at $F1B000 first 64 bytes ===\n");
      for (i = 0; i < 64; i++) {
         printf("%02X ", dsp_ram[i]);
         if ((i & 15) == 15) printf("\n");
      }
   }

   /* Dump RAM around the stuck loop */
   if (p_jaguarMainRAM) {
      uint8_t *ram = *p_jaguarMainRAM;
      printf("\n=== RAM at $15AC0 (32 bytes - stuck loop) ===\n");
      for (i = 0; i < 32; i++) {
         printf("%02X ", ram[0x15AC0 + i]);
         if ((i & 15) == 15) printf("\n");
      }

      printf("\n=== RAM at $14220 (32 bytes) ===\n");
      for (i = 0; i < 32; i++) {
         printf("%02X ", ram[0x14220 + i]);
         if ((i & 15) == 15) printf("\n");
      }

      /* Also dump some key game workspace areas */
      printf("\n=== RAM at $50A0 (32 bytes - game vars) ===\n");
      for (i = 0; i < 32; i++) {
         printf("%02X ", ram[0x50A0 + i]);
         if ((i & 15) == 15) printf("\n");
      }

      printf("\n=== Vector 64 ($100) = $%02X%02X%02X%02X ===\n",
             ram[0x100], ram[0x101], ram[0x102], ram[0x103]);
   }

   /* Dump ROM around the loop (info.data still valid) */
   {
      const uint8_t *rom = (const uint8_t *)info.data;
      printf("\n=== ROM at offset $26E0 (32 bytes around loop) ===\n");
      for (i = 0; i < 32; i++) {
         printf("%02X ", rom[0x26E0 + i]);
         if ((i & 15) == 15) printf("\n");
      }
      printf("\n=== ROM at offset $2130 (32 bytes around blitter poll) ===\n");
      for (i = 0; i < 32; i++) {
         printf("%02X ", rom[0x2130 + i]);
         if ((i & 15) == 15) printf("\n");
      }
   }

   p_retro_unload_game();
   p_retro_deinit();
   free((void *)info.data);
   dlclose(handle);
   return 0;
}
