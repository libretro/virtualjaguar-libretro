/* dump_pc.c — Focused diagnostic: dump code around the stuck PC after transition.
 * Build: cc -g -O0 -o test/dump_pc test/dump_pc.c -ldl
 * Run:   VJ_CD_BOOT_MODE=hle VJ_HLE_MODE=1 ./test/dump_pc "path/to.cue" 460
 */
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

static void video_refresh(const void *d, unsigned w, unsigned h, size_t p) { (void)d;(void)w;(void)h;(void)p; }
static void audio_sample(int16_t l, int16_t r) { (void)l;(void)r; }
static size_t audio_sample_batch(const int16_t *d, size_t f) { (void)d; return f; }
static void input_poll(void) {}
static int16_t input_state(unsigned a, unsigned b, unsigned c, unsigned d) { (void)a;(void)b;(void)c;(void)d; return 0; }

static void log_printf(enum retro_log_level level, const char *fmt, ...) {
   (void)level; va_list ap; va_start(ap, fmt); vfprintf(stderr, fmt, ap); va_end(ap);
}
static struct retro_log_callback log_cb = { log_printf };

static bool environment(unsigned cmd, void *data) {
   switch (cmd) {
   case RETRO_ENVIRONMENT_GET_LOG_INTERFACE: *(struct retro_log_callback *)data = log_cb; return true;
   case RETRO_ENVIRONMENT_SET_PIXEL_FORMAT: return true;
   case RETRO_ENVIRONMENT_GET_SYSTEM_DIRECTORY:
      *(const char **)data = (getenv("VJ_HLE_MODE") && strcmp(getenv("VJ_HLE_MODE"), "1") == 0) ? "/nonexistent" : "test/roms/private";
      return true;
   case RETRO_ENVIRONMENT_GET_SAVE_DIRECTORY: *(const char **)data = "."; return true;
   case RETRO_ENVIRONMENT_SET_VARIABLES: case RETRO_ENVIRONMENT_SET_CORE_OPTIONS_V2: return true;
   case RETRO_ENVIRONMENT_GET_VARIABLE: {
      struct retro_variable *var = (struct retro_variable *)data;
      if (var->key && strcmp(var->key, "virtualjaguar_bios") == 0) { var->value = "enabled"; return true; }
      if (var->key && strcmp(var->key, "virtualjaguar_usefastblitter") == 0) { var->value = "enabled"; return true; }
      if (var->key && strcmp(var->key, "virtualjaguar_cd_bios_type") == 0) { var->value = "retail"; return true; }
      if (var->key && strcmp(var->key, "virtualjaguar_cd_boot_mode") == 0) {
         const char *env = getenv("VJ_CD_BOOT_MODE");
         var->value = (env ? env : "hle"); return true;
      }
      var->value = NULL; return false;
   }
   case RETRO_ENVIRONMENT_GET_VARIABLE_UPDATE: *(bool *)data = false; return true;
   default: return false;
   }
}

int main(int argc, char *argv[]) {
   if (argc < 2) { fprintf(stderr, "Usage: %s <cue> [frames]\n", argv[0]); return 1; }
   unsigned num_frames = argc > 2 ? (unsigned)atoi(argv[2]) : 460;

   void *handle = dlopen("./virtualjaguar_libretro.dylib", RTLD_NOW);
   if (!handle) { fprintf(stderr, "dlopen: %s\n", dlerror()); return 1; }

#define LOAD(sym) do { p_##sym = dlsym(handle, #sym); if (!p_##sym) { fprintf(stderr, "Missing: %s\n", #sym); return 1; } } while(0)
   LOAD(retro_init); LOAD(retro_deinit); LOAD(retro_set_environment);
   LOAD(retro_set_video_refresh); LOAD(retro_set_audio_sample);
   LOAD(retro_set_audio_sample_batch); LOAD(retro_set_input_poll);
   LOAD(retro_set_input_state); LOAD(retro_load_game); LOAD(retro_unload_game); LOAD(retro_run);
   p_m68k_get_reg = dlsym(handle, "m68k_get_reg");
   uint8_t *(*get_ram)(void) = dlsym(handle, "GetRamPtr");

   p_retro_set_environment(environment);
   p_retro_set_video_refresh(video_refresh);
   p_retro_set_audio_sample(audio_sample);
   p_retro_set_audio_sample_batch(audio_sample_batch);
   p_retro_set_input_poll(input_poll);
   p_retro_set_input_state(input_state);
   p_retro_init();

   struct retro_game_info game = {0};
   game.path = argv[1];
   if (!p_retro_load_game(&game)) { fprintf(stderr, "Load failed\n"); return 1; }

   uint32_t prev_pc = 0;
   for (unsigned f = 0; f < num_frames; f++) {
      p_retro_run();
      if (p_m68k_get_reg) {
         uint32_t pc = p_m68k_get_reg(NULL, 16);
         uint32_t sp = p_m68k_get_reg(NULL, 15);
         if (pc != prev_pc && f >= 400) {
            printf("Frame %u: PC=$%06X SP=$%06X\n", f, pc, sp);
            prev_pc = pc;
         }
      }
   }

   if (!get_ram || !p_m68k_get_reg) { printf("Missing symbols\n"); goto done; }

   uint8_t *ram = get_ram();
   uint32_t pc = p_m68k_get_reg(NULL, 16);
   uint32_t sp = p_m68k_get_reg(NULL, 15);
   printf("\n=== Final: PC=$%06X SP=$%06X ===\n", pc, sp);

   /* Dump code around stuck PC */
   uint32_t base = (pc > 0x40) ? pc - 0x40 : 0;
   printf("\nCode at $%06X-$%06X:\n", base, pc + 0x60);
   for (uint32_t a = base; a < pc + 0x60 && a < 0x200000; a += 2)
      printf("  $%06X: %02X%02X%s\n", a, ram[a], ram[a+1], (a == pc) ? "  <-- PC" : "");

   /* Dump stack */
   printf("\nStack at SP=$%06X:\n", sp);
   for (uint32_t a = sp; a < sp + 0x40 && a + 3 < 0x200000; a += 4) {
      uint32_t v = (ram[a]<<24)|(ram[a+1]<<16)|(ram[a+2]<<8)|ram[a+3];
      printf("  $%06X: $%08X\n", a, v);
   }

   /* Dump all 68K registers */
   printf("\n68K regs:\n");
   for (int r = 0; r <= 7; r++)
      printf("  D%d=$%08X A%d=$%08X\n", r, p_m68k_get_reg(NULL, r), r, p_m68k_get_reg(NULL, 8+r));
   printf("  PC=$%08X SR=$%04X\n", p_m68k_get_reg(NULL, 16), p_m68k_get_reg(NULL, 17) & 0xFFFF);

   /* Look for what the code at PC is polling */
   /* Common pattern: TST.L <addr> / BEQ.S back_to_tst */
   printf("\nChecking if stuck PC is polling a memory location...\n");
   uint16_t opcode = (ram[pc] << 8) | ram[pc+1];
   printf("  Opcode at PC: $%04X\n", opcode);
   if (opcode == 0x4AB9) { /* TST.L <abs32> */
      uint32_t addr = (ram[pc+2]<<24)|(ram[pc+3]<<16)|(ram[pc+4]<<8)|ram[pc+5];
      uint32_t val = 0;
      if (addr < 0x200000)
         val = (ram[addr]<<24)|(ram[addr+1]<<16)|(ram[addr+2]<<8)|ram[addr+3];
      printf("  TST.L $%08X = $%08X\n", addr, val);
   } else if (opcode == 0x4A39) { /* TST.B <abs32> */
      uint32_t addr = (ram[pc+2]<<24)|(ram[pc+3]<<16)|(ram[pc+4]<<8)|ram[pc+5];
      printf("  TST.B $%08X = $%02X\n", addr, (addr < 0x200000) ? ram[addr] : 0xFF);
   } else if ((opcode & 0xFFF0) == 0x4A90) { /* TST.L (An) */
      int reg = opcode & 7;
      uint32_t addr = p_m68k_get_reg(NULL, 8 + reg);
      uint32_t val = 0;
      if (addr < 0x200000)
         val = (ram[addr]<<24)|(ram[addr+1]<<16)|(ram[addr+2]<<8)|ram[addr+3];
      printf("  TST.L (A%d) => TST.L ($%08X) = $%08X\n", reg, addr, val);
   } else if ((opcode & 0xFF00) == 0x0C00 || (opcode & 0xFF00) == 0x0C80) {
      printf("  CMP instruction\n");
   }

   /* Dump the VBlank/interrupt vectors in case the game re-installed them */
   printf("\nException vectors at stuck point:\n");
   for (unsigned v = 0; v < 4; v++) {
      uint32_t val = (ram[v*4]<<24)|(ram[v*4+1]<<16)|(ram[v*4+2]<<8)|ram[v*4+3];
      printf("  Vec %u ($%03X) = $%08X\n", v, v*4, val);
   }
   for (unsigned v = 24; v <= 31; v++) {
      uint32_t val = (ram[v*4]<<24)|(ram[v*4+1]<<16)|(ram[v*4+2]<<8)|ram[v*4+3];
      printf("  Vec %u ($%03X) = $%08X\n", v, v*4, val);
   }
   for (unsigned v = 64; v <= 71; v++) {
      uint32_t val = (ram[v*4]<<24)|(ram[v*4+1]<<16)|(ram[v*4+2]<<8)|ram[v*4+3];
      printf("  Vec %u ($%03X) = $%08X\n", v, v*4, val);
   }

done:
   p_retro_unload_game();
   p_retro_deinit();
   dlclose(handle);
   return 0;
}
