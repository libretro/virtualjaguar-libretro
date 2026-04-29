/* heap_search.c — Find all references to heap base $001FB750 in game RAM.
 * Build: cc -g -O0 -o test/heap_search test/heap_search.c -ldl
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

static void vid(const void *d, unsigned w, unsigned h, size_t p) { (void)d;(void)w;(void)h;(void)p; }
static void aud(int16_t l, int16_t r) { (void)l;(void)r; }
static size_t audb(const int16_t *d, size_t f) { (void)d; return f; }
static void ipoll(void) {}
static int16_t istate(unsigned a, unsigned b, unsigned c, unsigned d) { (void)a;(void)b;(void)c;(void)d; return 0; }
static void logp(enum retro_log_level l, const char *fmt, ...) { va_list ap; (void)l; va_start(ap, fmt); vfprintf(stderr, fmt, ap); va_end(ap); }
static struct retro_log_callback log_cb = { logp };

static bool env(unsigned cmd, void *data) {
   switch (cmd) {
   case RETRO_ENVIRONMENT_GET_LOG_INTERFACE: *(struct retro_log_callback *)data = log_cb; return true;
   case RETRO_ENVIRONMENT_SET_PIXEL_FORMAT: return true;
   case RETRO_ENVIRONMENT_GET_SYSTEM_DIRECTORY: *(const char **)data = "/nonexistent"; return true;
   case RETRO_ENVIRONMENT_GET_SAVE_DIRECTORY: *(const char **)data = "."; return true;
   case RETRO_ENVIRONMENT_SET_VARIABLES: case RETRO_ENVIRONMENT_SET_CORE_OPTIONS_V2: return true;
   case RETRO_ENVIRONMENT_GET_VARIABLE: {
      struct retro_variable *var = (struct retro_variable *)data;
      if (var->key && strcmp(var->key, "virtualjaguar_bios") == 0) { var->value = "enabled"; return true; }
      if (var->key && strcmp(var->key, "virtualjaguar_usefastblitter") == 0) { var->value = "enabled"; return true; }
      if (var->key && strcmp(var->key, "virtualjaguar_cd_bios_type") == 0) { var->value = "retail"; return true; }
      if (var->key && strcmp(var->key, "virtualjaguar_cd_boot_mode") == 0) { var->value = "hle"; return true; }
      var->value = NULL; return false;
   }
   case RETRO_ENVIRONMENT_GET_VARIABLE_UPDATE: *(bool *)data = false; return true;
   default: return false;
   }
}

int main(int argc, char *argv[]) {
   void *handle;
   uint8_t *(*get_ram)(void);
   struct retro_game_info game = {0};
   uint8_t *ram;
   unsigned int (*p_m68k_get_reg)(void *, int);
   unsigned f;
   uint32_t a;

   if (argc < 2) { fprintf(stderr, "Usage: %s <cue>\n", argv[0]); return 1; }

   handle = dlopen("./virtualjaguar_libretro.dylib", RTLD_NOW);
   if (!handle) { fprintf(stderr, "dlopen: %s\n", dlerror()); return 1; }
#define L(s) do { p_##s = dlsym(handle, #s); if (!p_##s) { fprintf(stderr, "Missing: %s\n", #s); return 1; } } while(0)
   L(retro_init); L(retro_deinit); L(retro_set_environment);
   L(retro_set_video_refresh); L(retro_set_audio_sample);
   L(retro_set_audio_sample_batch); L(retro_set_input_poll);
   L(retro_set_input_state); L(retro_load_game); L(retro_unload_game); L(retro_run);
   get_ram = dlsym(handle, "GetRamPtr");

   p_retro_set_environment(env);
   p_retro_set_video_refresh(vid);
   p_retro_set_audio_sample(aud);
   p_retro_set_audio_sample_batch(audb);
   p_retro_set_input_poll(ipoll);
   p_retro_set_input_state(istate);
   p_retro_init();

   game.path = argv[1];
   if (!p_retro_load_game(&game)) { fprintf(stderr, "Load failed\n"); return 1; }

   ram = get_ram();
   p_m68k_get_reg = dlsym(handle, "m68k_get_reg");

   /* Dump heap at multiple points */
   for (f = 0; f < 420; f++) {
      p_retro_run();
      if (f == 5 || f == 100 || f == 405 || f == 410 || f == 415 || f == 418) {
         uint32_t heap_ptr = (ram[0x1FB750]<<24)|(ram[0x1FB751]<<16)|(ram[0x1FB752]<<8)|ram[0x1FB753];
         uint32_t pc = p_m68k_get_reg ? p_m68k_get_reg(NULL, 16) : 0;
         printf("Frame %3u: PC=$%06X heap_base=$%08X", f, pc, heap_ptr);
         if (heap_ptr > 0 && heap_ptr < 0x200000) {
            uint32_t next, size, node, total_free;
            int count;
            next = (ram[heap_ptr]<<24)|(ram[heap_ptr+1]<<16)|(ram[heap_ptr+2]<<8)|ram[heap_ptr+3];
            size = (ram[heap_ptr+4]<<24)|(ram[heap_ptr+5]<<16)|(ram[heap_ptr+6]<<8)|ram[heap_ptr+7];
            printf(" → block at $%06X: next=$%08X size=$%08X", heap_ptr, next, size);
            /* Walk the list */
            node = heap_ptr;
            count = 0;
            total_free = 0;
            while (node && node < 0x200000 && count < 20) {
               uint32_t n = (ram[node]<<24)|(ram[node+1]<<16)|(ram[node+2]<<8)|ram[node+3];
               uint32_t s = (ram[node+4]<<24)|(ram[node+5]<<16)|(ram[node+6]<<8)|ram[node+7];
               total_free += s;
               count++;
               node = n;
            }
            printf(" (free_blocks=%d, total_free=$%X)", count, total_free);
         }
         printf("\n");
      }
   }

   /* Search for $001FB750 (big-endian: 00 1F B7 50) in all of RAM */
   printf("=== Searching for heap base $001FB750 in RAM ===\n");
   for (a = 0; a < 0x1FFFFF; a++) {
      if (ram[a] == 0x00 && ram[a+1] == 0x1F && ram[a+2] == 0xB7 && ram[a+3] == 0x50) {
         int i;
         printf("  $%06X: %02X%02X%02X%02X (context: -8:", a, ram[a], ram[a+1], ram[a+2], ram[a+3]);
         for (i = -8; i < 12; i += 2)
            printf(" %02X%02X", ram[a+i], ram[a+i+1]);
         printf(")\n");
      }
   }

   /* Dump the heap area itself */
   printf("\n=== Heap at $001FB750-$001FB7A0 ===\n");
   for (a = 0x1FB750; a < 0x1FB7A0; a += 16) {
      unsigned b;
      printf("  $%06X:", a);
      for (b = 0; b < 16; b += 2)
         printf(" %02X%02X", ram[a+b], ram[a+b+1]);
      printf("\n");
   }

   /* Also check if there's a heap_init function by searching for other
    * memory management functions near $0396A4 */
   printf("\n=== Functions near allocator $039690-$039800 ===\n");
   for (a = 0x39690; a < 0x39800; a += 2)
      printf("  $%06X: %02X%02X\n", a, ram[a], ram[a+1]);

   /* Check what the game's init code does — look at $01C250 area */
   printf("\n=== Init entry code $01C240-$01C2A0 ===\n");
   for (a = 0x1C240; a < 0x1C2A0; a += 2)
      printf("  $%06X: %02X%02X\n", a, ram[a], ram[a+1]);

   /* Check free/init functions that reference $1FB750 */
   /* Search for the pattern 41F9 001F B750 (LEA $1FB750, A0) */
   printf("\n=== LEA $1FB750,An instructions in RAM ===\n");
   for (a = 0x4000; a < 0x1F0000; a += 2) {
      uint16_t op = (ram[a] << 8) | ram[a+1];
      if ((op & 0xF1FF) == 0x41F9) {  /* LEA <abs32>, An */
         uint32_t addr = (ram[a+2]<<24)|(ram[a+3]<<16)|(ram[a+4]<<8)|ram[a+5];
         if (addr == 0x001FB750) {
            int i;
            printf("  $%06X: %04X %08X  (LEA $1FB750, A%d)\n", a, op, addr, (op >> 9) & 7);
            printf("    context: ");
            for (i = -4; i < 16; i += 2)
               printf("%02X%02X ", ram[a+i], ram[a+i+1]);
            printf("\n");
         }
      }
   }

   /* Also check $001FD030-$001FD040 — common BIOS variable area */
   printf("\n=== BIOS var area $001FD030-$001FD040 ===\n");
   for (a = 0x1FD030; a < 0x1FD040; a += 4) {
      uint32_t v = (ram[a]<<24)|(ram[a+1]<<16)|(ram[a+2]<<8)|ram[a+3];
      printf("  $%06X: $%08X\n", a, v);
   }

   p_retro_unload_game();
   p_retro_deinit();
   dlclose(handle);
   return 0;
}
