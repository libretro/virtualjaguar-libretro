/* test_gpu_irq.c — Verify GPU interrupt delivery in HLE CD boot.
 *
 * After the game initializes (loads GPU code, sets up display list),
 * the GPU enters a polling loop waiting for an interrupt (typically
 * CINT3/OP interrupt) to set a register via MOVETA. This test checks
 * each link in the interrupt chain:
 *   1. GPU ISR vector at $F03030 (CINT3) is NOT all-NOPs
 *   2. GPU interrupt mask includes CINT3
 *   3. OP is running (objectp_running == 1)
 *   4. GPU is running (GPUGO set)
 *   5. GPU interrupt latch gets set at some point
 *
 * Build: cc -o test/test_gpu_irq test/test_gpu_irq.c -ldl
 * Run:   VJ_CD_BOOT_MODE=hle ./test/test_gpu_irq "test/roms/private/Primal Rage (USA)/Primal Rage (USA).cue" 600
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dlfcn.h>
#include <stdarg.h>
#include <stdbool.h>
#include "../libretro-common/include/libretro.h"

/* libretro API */
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

/* GPU access */
static uint32_t (*p_GPUReadLong)(uint32_t offset, uint32_t who);
static uint16_t (*p_GPUReadWord)(uint32_t offset, uint32_t who);
static uint32_t (*p_GPUGetPC)(void);
static int (*p_GPUIsRunning)(void);
static void (*p_GPUDumpState)(const char *tag);

/* RAM + OP */
static uint8_t *(*p_GetRamPtr)(void);
static uint8_t *objectp_running_ptr;
static uint32_t *gpu_reg_bank_0;
static uint32_t *gpu_reg_bank_1;

#define M68K 0
#define GPU_WORK_RAM 0xF03000

static unsigned frame_count = 0;

static void video_refresh(const void *d, unsigned w, unsigned h, size_t p) { (void)d;(void)w;(void)h;(void)p; }
static void audio_sample(int16_t l, int16_t r) { (void)l;(void)r; }
static size_t audio_sample_batch(const int16_t *d, size_t f) { (void)d; return f; }
static void input_poll(void) {}
static int16_t input_state(unsigned a, unsigned b, unsigned c, unsigned d) { (void)a;(void)b;(void)c;(void)d; return 0; }

static void log_printf(enum retro_log_level level, const char *fmt, ...)
{
   (void)level;
   va_list ap;
   va_start(ap, fmt);
   vfprintf(stderr, fmt, ap);
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
      *(const char **)data = "/nonexistent";  /* Force HLE by hiding BIOS */
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
      if (var->key && strcmp(var->key, "virtualjaguar_bios") == 0)
      { var->value = "enabled"; return true; }
      if (var->key && strcmp(var->key, "virtualjaguar_usefastblitter") == 0)
      { var->value = "enabled"; return true; }
      if (var->key && strcmp(var->key, "virtualjaguar_cd_bios_type") == 0)
      { var->value = "retail"; return true; }
      if (var->key && strcmp(var->key, "virtualjaguar_cd_boot_mode") == 0)
      {
         const char *env = getenv("VJ_CD_BOOT_MODE");
         var->value = (env ? env : "hle");
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

static int failures = 0;
#define CHECK(cond, ...) do { \
   if (!(cond)) { printf("FAIL: "); printf(__VA_ARGS__); printf("\n"); failures++; } \
   else { printf("PASS: "); printf(__VA_ARGS__); printf("\n"); } \
} while(0)

int main(int argc, char *argv[])
{
   if (argc < 2)
   {
      fprintf(stderr, "Usage: %s <path-to-cue> [num_frames]\n", argv[0]);
      return 1;
   }

   const char *image_path = argv[1];
   unsigned num_frames = argc > 2 ? (unsigned)atoi(argv[2]) : 600;

   void *handle = dlopen("./virtualjaguar_libretro.dylib", RTLD_NOW);
   if (!handle) { fprintf(stderr, "dlopen: %s\n", dlerror()); return 1; }

#define LOAD_SYM(sym) do { \
   p_##sym = dlsym(handle, #sym); \
   if (!p_##sym) { fprintf(stderr, "Missing: %s\n", #sym); return 1; } \
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
   LOAD_SYM(GPUReadLong);
   LOAD_SYM(GPUReadWord);
   LOAD_SYM(GPUGetPC);
   LOAD_SYM(GPUIsRunning);

   p_GPUDumpState = dlsym(handle, "GPUDumpState");
   p_GetRamPtr = dlsym(handle, "GetRamPtr");
   objectp_running_ptr = dlsym(handle, "objectp_running");
   gpu_reg_bank_0 = dlsym(handle, "gpu_reg_bank_0");
   gpu_reg_bank_1 = dlsym(handle, "gpu_reg_bank_1");

   p_retro_set_environment(environment);
   p_retro_set_video_refresh(video_refresh);
   p_retro_set_audio_sample(audio_sample);
   p_retro_set_audio_sample_batch(audio_sample_batch);
   p_retro_set_input_poll(input_poll);
   p_retro_set_input_state(input_state);

   p_retro_init();

   struct retro_game_info game = {0};
   game.path = image_path;

   printf("=== GPU IRQ Chain Test ===\n");
   printf("Loading: %s\n", image_path);
   if (!p_retro_load_game(&game))
   {
      fprintf(stderr, "retro_load_game failed!\n");
      p_retro_deinit();
      dlclose(handle);
      return 1;
   }

   printf("Running %u frames...\n\n", num_frames);
   for (frame_count = 0; frame_count < num_frames; frame_count++)
      p_retro_run();

   printf("=== Post-boot GPU state (frame %u) ===\n\n", num_frames);

   /* 1. Check GPU ISR vectors — each is 16 bytes at GPU_WORK_RAM + irq*16 */
   printf("--- GPU ISR Vectors ---\n");
   for (int irq = 0; irq < 5; irq++)
   {
      uint32_t addr = GPU_WORK_RAM + irq * 0x10;
      const char *names[] = {"CINT0/CPU", "CINT1/DSP", "CINT2/PIT", "CINT3/OP", "CINT4/BLT"};
      printf("  %s ($%06X): ", names[irq], addr);
      bool all_nop = true;
      for (int w = 0; w < 8; w++)
      {
         uint16_t word = p_GPUReadWord(addr + w * 2, M68K);
         printf("%04X ", word);
         if (word != 0xE400)  /* E400 = NOP */
            all_nop = false;
      }
      printf("%s\n", all_nop ? " <-- ALL NOPS!" : "");
   }

   /* CINT3 (OP interrupt) is what the game needs */
   {
      bool cint3_nop = true;
      for (int w = 0; w < 8; w++)
      {
         uint16_t word = p_GPUReadWord(GPU_WORK_RAM + 0x30 + w * 2, M68K);
         if (word != 0xE400) cint3_nop = false;
      }
      CHECK(!cint3_nop, "CINT3 (OP) ISR vector is NOT all-NOPs");
   }

   /* 2. Check GPU flags — interrupt mask bits */
   uint32_t g_flags = p_GPUReadLong(0xF02100, M68K);
   uint32_t int_mask = (g_flags >> 4) & 0x1F;
   printf("\n--- GPU Flags ($F02100) = $%08X ---\n", g_flags);
   printf("  Interrupt mask: $%02X (", int_mask);
   for (int i = 0; i < 5; i++)
      if (int_mask & (1 << i))
         printf("%s ", (const char*[]){"CPU","DSP","PIT","OP","BLT"}[i]);
   printf(")\n");
   CHECK(int_mask & 0x08, "CINT3 (OP) interrupt is enabled in GPU flags mask=$%02X", int_mask);

   /* 3. Check if GPU is running */
   int running = p_GPUIsRunning();
   uint32_t gpc = p_GPUGetPC();
   printf("\n--- GPU State ---\n");
   printf("  Running: %d, PC: $%06X\n", running, gpc);
   CHECK(running, "GPU is running (GPUGO set)");

   /* 4. Check Object Processor */
   printf("\n--- Object Processor ---\n");
   if (objectp_running_ptr)
   {
      printf("  objectp_running: %d\n", *objectp_running_ptr);
      CHECK(*objectp_running_ptr, "Object Processor is running");
   }
   else
      printf("  (objectp_running not exported)\n");

   /* 5. Dump the OP list pointer and check for GPU interrupt objects */
   if (p_GetRamPtr)
   {
      /* OLP is at TOM register $F00020-$F00023 (high) and $F00024-$F00027 (low) */
      /* But we can read it via TOM read. Actually, let's just read gpu ram for the list. */
      /* The OP list pointer (OLP) is at TOM $F00020 (high word) and $F00024 (low word) */
      uint16_t olp_hi = p_GPUReadWord(0xF00020, M68K);  /* Actually TOM, routed through */
      /* This won't work via GPUReadWord for TOM regs. Use a different approach. */
      /* Let's look at what the game set up by inspecting $F00020 from tomRam */
   }

   /* 6. Check GPU register banks */
   printf("\n--- GPU Register Banks ---\n");
   if (gpu_reg_bank_0 && gpu_reg_bank_1)
   {
      printf("  Bank 0 R0-R7: ");
      for (int i = 0; i < 8; i++) printf("$%08X ", gpu_reg_bank_0[i]);
      printf("\n  Bank 0 R24-R31: ");
      for (int i = 24; i < 32; i++) printf("$%08X ", gpu_reg_bank_0[i]);
      printf("\n  Bank 1 R0-R7: ");
      for (int i = 0; i < 8; i++) printf("$%08X ", gpu_reg_bank_1[i]);
      printf("\n  Bank 1 R24-R31: ");
      for (int i = 24; i < 32; i++) printf("$%08X ", gpu_reg_bank_1[i]);
      printf("\n");

      /* The game's GPU main code polls R1 (in active bank).
       * If REGPAGE=1, active bank is bank_1. R1 should eventually be non-zero
       * after a GPU ISR fires. */
      bool regpage = (g_flags & 0x4000) != 0;
      uint32_t *active_bank = regpage ? gpu_reg_bank_1 : gpu_reg_bank_0;
      printf("  Active bank: %d (REGPAGE=%d)\n", regpage ? 1 : 0, regpage);
      printf("  Active R1 (poll register): $%08X\n", active_bank[1]);
      CHECK(active_bank[1] != 0, "GPU active bank R1 is non-zero (set by ISR)");
   }

   /* 7. Dump GPU code at the main loop PC */
   printf("\n--- GPU Code at current PC=$%06X ---\n", gpc);
   for (uint32_t a = (gpc > 0x10 ? gpc - 0x10 : GPU_WORK_RAM); a < gpc + 0x20 && a < GPU_WORK_RAM + 0x1000; a += 2)
   {
      uint16_t w = p_GPUReadWord(a, M68K);
      printf("  $%06X: %04X%s\n", a, w, (a == gpc) ? "  <-- PC" : "");
   }

   /* 8. Dump GPU RAM mailbox area */
   printf("\n--- GPU RAM Mailbox ($F03E90-$F03EAF) ---\n");
   for (uint32_t a = 0xF03E90; a < 0xF03EB0; a += 4)
      printf("  $%06X: $%08X\n", a, p_GPUReadLong(a, M68K));

   /* Full GPU state dump */
   if (p_GPUDumpState)
      p_GPUDumpState("test-final");

   printf("\n=== Summary: %d failure(s) ===\n", failures);

   p_retro_unload_game();
   p_retro_deinit();
   dlclose(handle);
   return failures > 0 ? 1 : 0;
}
