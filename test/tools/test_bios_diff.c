/* BIOS/HLE snapshot probe.
 *
 * This is a diagnostic tool, not a CI pass/fail test. It boots the same ROM
 * with the real BIOS and HLE BIOS paths, captures selected hardware state, and
 * prints differences so HLE contract gaps can be promoted into focused tests.
 *
 * Build:
 *   cc -O2 -Wall -I. -Isrc -Isrc/core -Isrc/m68000 -Ilibretro-common/include \
 *      -o test/tools/test_bios_diff test/tools/test_bios_diff.c
 *
 * Usage:
 *   test/tools/test_bios_diff ./virtualjaguar_libretro.dylib game.j64 [frames]
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdbool.h>
#include <dlfcn.h>

#include "../../libretro-common/include/libretro.h"
#include "../../src/m68000/m68kinterface.h"

#define WHO_M68K 6

struct CoreSymbols
{
   void *handle;
   void (*retro_set_environment)(retro_environment_t);
   void (*retro_set_video_refresh)(retro_video_refresh_t);
   void (*retro_set_audio_sample)(retro_audio_sample_t);
   void (*retro_set_audio_sample_batch)(retro_audio_sample_batch_t);
   void (*retro_set_input_poll)(retro_input_poll_t);
   void (*retro_set_input_state)(retro_input_state_t);
   void (*retro_init)(void);
   void (*retro_deinit)(void);
   bool (*retro_load_game)(const struct retro_game_info *);
   void (*retro_run)(void);
   void (*retro_unload_game)(void);
   uint32_t (*GPUReadLong)(uint32_t, uint32_t);
   uint16_t (*JERRYReadWord)(uint32_t, uint32_t);
   unsigned int (*m68k_get_reg)(void *, m68k_register_t);
   uint8_t *tomRam8;
   uint8_t **jaguarMainRAM;
};

struct BiosSnapshot
{
   const char *label;
   uint32_t m68k_pc;
   uint32_t ram_sp;
   uint32_t ram_reset_vector;
   uint32_t ram_interrupt_vector;
   uint32_t ram_exception_stub;
   uint32_t ram_bios_workspace_804;
   uint32_t ram_op_stop_hi;
   uint32_t ram_op_stop_lo;
   uint16_t tom_memcon1;
   uint16_t tom_memcon2;
   uint16_t tom_olp_lo;
   uint16_t tom_olp_hi;
   uint16_t tom_vmode;
   uint16_t tom_int1;
   uint16_t tom_bg_hi;
   uint16_t tom_bg_lo;
   uint32_t gpu_auth;
   uint32_t gpu_flags;
   uint32_t gpu_end;
   uint32_t gpu_pc;
   uint32_t gpu_ctrl;
   uint16_t jerry_pit0;
   uint16_t jerry_pit1;
   uint16_t jerry_clk2;
   uint16_t jerry_clk3;
   uint16_t jerry_int;
   uint16_t jerry_sclk;
   uint16_t jerry_smode;
};

static const char *env_bios_value;

static void log_printf(enum retro_log_level level, const char *fmt, ...)
{
   va_list ap;

   if (level < RETRO_LOG_WARN)
      return;

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
      case RETRO_ENVIRONMENT_SET_MEMORY_MAPS:
      case RETRO_ENVIRONMENT_SET_SUPPORT_ACHIEVEMENTS:
      case RETRO_ENVIRONMENT_SET_CORE_OPTIONS_V2:
      case RETRO_ENVIRONMENT_SET_CORE_OPTIONS_UPDATE_DISPLAY_CALLBACK:
      case RETRO_ENVIRONMENT_SET_INPUT_DESCRIPTORS:
      case RETRO_ENVIRONMENT_SET_SERIALIZATION_QUIRKS:
         return true;
      case RETRO_ENVIRONMENT_GET_CORE_OPTIONS_VERSION:
      {
         unsigned *version = (unsigned *)data;
         *version = 2;
         return true;
      }
      case RETRO_ENVIRONMENT_GET_VARIABLE:
      {
         struct retro_variable *var = (struct retro_variable *)data;
         if (strcmp(var->key, "virtualjaguar_bios") == 0)
         {
            var->value = env_bios_value;
            return true;
         }
         if (strcmp(var->key, "virtualjaguar_pal") == 0)
         {
            var->value = "disabled";
            return true;
         }
         if (strcmp(var->key, "virtualjaguar_usefastblitter") == 0)
         {
            var->value = "disabled";
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
      case RETRO_ENVIRONMENT_GET_SYSTEM_DIRECTORY:
         *(const char **)data = "test/roms/private";
         return true;
      case RETRO_ENVIRONMENT_GET_SAVE_DIRECTORY:
         *(const char **)data = "/tmp";
         return true;
      default:
         return false;
   }
}

static void video_refresh(const void *data, unsigned width, unsigned height, size_t pitch)
{
   (void)data;
   (void)width;
   (void)height;
   (void)pitch;
}

static void audio_sample(int16_t left, int16_t right)
{
   (void)left;
   (void)right;
}

static size_t audio_sample_batch(const int16_t *data, size_t frames)
{
   (void)data;
   return frames;
}

static void input_poll(void)
{
}

static int16_t input_state(unsigned port, unsigned device, unsigned index, unsigned id)
{
   (void)port;
   (void)device;
   (void)index;
   (void)id;
   return 0;
}

static void *read_file(const char *path, size_t *out_size)
{
   FILE *f;
   long size;
   void *buf;

   f = fopen(path, "rb");
   if (!f)
      return NULL;

   fseek(f, 0, SEEK_END);
   size = ftell(f);
   fseek(f, 0, SEEK_SET);

   buf = malloc((size_t)size);
   if (!buf)
   {
      fclose(f);
      return NULL;
   }

   if (fread(buf, 1, (size_t)size, f) != (size_t)size)
   {
      free(buf);
      fclose(f);
      return NULL;
   }

   fclose(f);
   *out_size = (size_t)size;
   return buf;
}

static uint16_t get16(const uint8_t *p, uint32_t offset)
{
   return ((uint16_t)p[offset] << 8) | (uint16_t)p[offset + 1];
}

static uint32_t get32(const uint8_t *p, uint32_t offset)
{
   return ((uint32_t)p[offset] << 24)
      | ((uint32_t)p[offset + 1] << 16)
      | ((uint32_t)p[offset + 2] << 8)
      | (uint32_t)p[offset + 3];
}

#define LOAD_SYM(core, sym) do { \
   (core)->sym = dlsym((core)->handle, #sym); \
   if (!(core)->sym) { fprintf(stderr, "Missing symbol: %s\n", #sym); return false; } \
} while (0)

static bool load_core(const char *core_path, struct CoreSymbols *core)
{
   memset(core, 0, sizeof(*core));

   core->handle = dlopen(core_path, RTLD_NOW);
   if (!core->handle)
   {
      fprintf(stderr, "dlopen failed: %s\n", dlerror());
      return false;
   }

   LOAD_SYM(core, retro_set_environment);
   LOAD_SYM(core, retro_set_video_refresh);
   LOAD_SYM(core, retro_set_audio_sample);
   LOAD_SYM(core, retro_set_audio_sample_batch);
   LOAD_SYM(core, retro_set_input_poll);
   LOAD_SYM(core, retro_set_input_state);
   LOAD_SYM(core, retro_init);
   LOAD_SYM(core, retro_deinit);
   LOAD_SYM(core, retro_load_game);
   LOAD_SYM(core, retro_run);
   LOAD_SYM(core, retro_unload_game);
   LOAD_SYM(core, GPUReadLong);
   LOAD_SYM(core, JERRYReadWord);
   LOAD_SYM(core, m68k_get_reg);

   core->tomRam8 = dlsym(core->handle, "tomRam8");
   core->jaguarMainRAM = dlsym(core->handle, "jaguarMainRAM");
   if (!core->tomRam8 || !core->jaguarMainRAM)
   {
      fprintf(stderr, "Missing internal memory symbols\n");
      return false;
   }

   return true;
}

static void unload_core(struct CoreSymbols *core)
{
   if (core->handle)
      dlclose(core->handle);
   memset(core, 0, sizeof(*core));
}

static void capture_snapshot(struct CoreSymbols *core, const char *label,
      struct BiosSnapshot *snapshot)
{
   uint8_t *ram;

   memset(snapshot, 0, sizeof(*snapshot));
   snapshot->label = label;
   ram = *core->jaguarMainRAM;

   snapshot->m68k_pc = core->m68k_get_reg(NULL, M68K_REG_PC);
   snapshot->ram_sp = get32(ram, 0x0000);
   snapshot->ram_reset_vector = get32(ram, 0x0004);
   snapshot->ram_interrupt_vector = get32(ram, 0x0100);
   snapshot->ram_exception_stub = get32(ram, 0x0400);
   snapshot->ram_bios_workspace_804 = get32(ram, 0x0804);
   snapshot->ram_op_stop_hi = get32(ram, 0x1000);
   snapshot->ram_op_stop_lo = get32(ram, 0x1004);

   snapshot->tom_memcon1 = get16(core->tomRam8, 0x00);
   snapshot->tom_memcon2 = get16(core->tomRam8, 0x02);
   snapshot->tom_olp_lo = get16(core->tomRam8, 0x20);
   snapshot->tom_olp_hi = get16(core->tomRam8, 0x22);
   snapshot->tom_vmode = get16(core->tomRam8, 0x28);
   snapshot->tom_int1 = get16(core->tomRam8, 0xE0);
   snapshot->tom_bg_hi = get16(core->tomRam8, 0x58);
   snapshot->tom_bg_lo = get16(core->tomRam8, 0x5A);

   snapshot->gpu_auth = core->GPUReadLong(0xF03000, WHO_M68K);
   snapshot->gpu_flags = core->GPUReadLong(0xF02100, WHO_M68K);
   snapshot->gpu_end = core->GPUReadLong(0xF0210C, WHO_M68K);
   snapshot->gpu_pc = core->GPUReadLong(0xF02110, WHO_M68K);
   snapshot->gpu_ctrl = core->GPUReadLong(0xF02114, WHO_M68K);

   snapshot->jerry_pit0 = core->JERRYReadWord(0xF10000, WHO_M68K);
   snapshot->jerry_pit1 = core->JERRYReadWord(0xF10002, WHO_M68K);
   snapshot->jerry_clk2 = core->JERRYReadWord(0xF10012, WHO_M68K);
   snapshot->jerry_clk3 = core->JERRYReadWord(0xF10014, WHO_M68K);
   snapshot->jerry_int = core->JERRYReadWord(0xF10020, WHO_M68K);
   snapshot->jerry_sclk = core->JERRYReadWord(0xF1A152, WHO_M68K);
   snapshot->jerry_smode = core->JERRYReadWord(0xF1A156, WHO_M68K);
}

static bool run_mode(const char *core_path, const char *rom_path, const char *label,
      const char *bios_value, const void *rom_data, size_t rom_size, int frames,
      struct BiosSnapshot *snapshot)
{
   struct CoreSymbols core;
   struct retro_game_info game;
   int i;
   bool ok;

   env_bios_value = bios_value;
   if (!load_core(core_path, &core))
      return false;

   core.retro_set_environment(environment_cb);
   core.retro_set_video_refresh(video_refresh);
   core.retro_set_audio_sample(audio_sample);
   core.retro_set_audio_sample_batch(audio_sample_batch);
   core.retro_set_input_poll(input_poll);
   core.retro_set_input_state(input_state);
   core.retro_init();

   memset(&game, 0, sizeof(game));
   game.path = rom_path;
   game.data = rom_data;
   game.size = rom_size;

   ok = core.retro_load_game(&game);
   if (ok)
   {
      for (i = 0; i < frames; i++)
         core.retro_run();
      capture_snapshot(&core, label, snapshot);
      core.retro_unload_game();
   }
   else
      fprintf(stderr, "retro_load_game failed for %s\n", label);

   core.retro_deinit();
   unload_core(&core);
   return ok;
}

static void print_snapshot(const struct BiosSnapshot *s)
{
   printf("\n[%s]\n", s->label);
   printf("m68k_pc=%06X\n", s->m68k_pc);
   printf("ram.sp=%08X ram.reset=%08X ram.vector64=%08X ram.0400=%08X ram.0804=%08X\n",
         s->ram_sp, s->ram_reset_vector, s->ram_interrupt_vector,
         s->ram_exception_stub, s->ram_bios_workspace_804);
   printf("ram.op_stop=%08X:%08X\n", s->ram_op_stop_hi, s->ram_op_stop_lo);
   printf("tom.memcon1=%04X tom.memcon2=%04X tom.olp=%04X%04X tom.vmode=%04X tom.int1=%04X tom.bg=%04X%04X\n",
         s->tom_memcon1, s->tom_memcon2, s->tom_olp_hi, s->tom_olp_lo,
         s->tom_vmode, s->tom_int1, s->tom_bg_hi, s->tom_bg_lo);
   printf("gpu.auth=%08X gpu.flags=%08X gpu.end=%08X gpu.pc=%08X gpu.ctrl=%08X\n",
         s->gpu_auth, s->gpu_flags, s->gpu_end, s->gpu_pc, s->gpu_ctrl);
   printf("jerry.pit=%04X:%04X jerry.clk2=%04X jerry.clk3=%04X jerry.int=%04X jerry.sclk=%04X jerry.smode=%04X\n",
         s->jerry_pit0, s->jerry_pit1, s->jerry_clk2, s->jerry_clk3,
         s->jerry_int, s->jerry_sclk, s->jerry_smode);
}

#define DIFF_FIELD(a, b, field, fmt) do { \
   if ((a)->field != (b)->field) \
      printf("diff %-24s real=" fmt " hle=" fmt "\n", #field, (a)->field, (b)->field); \
} while (0)

static void print_diffs(const struct BiosSnapshot *real, const struct BiosSnapshot *hle)
{
   printf("\n[diffs]\n");
   DIFF_FIELD(real, hle, ram_sp, "%08X");
   DIFF_FIELD(real, hle, ram_reset_vector, "%08X");
   DIFF_FIELD(real, hle, ram_interrupt_vector, "%08X");
   DIFF_FIELD(real, hle, ram_exception_stub, "%08X");
   DIFF_FIELD(real, hle, ram_bios_workspace_804, "%08X");
   DIFF_FIELD(real, hle, ram_op_stop_hi, "%08X");
   DIFF_FIELD(real, hle, ram_op_stop_lo, "%08X");
   DIFF_FIELD(real, hle, tom_memcon1, "%04X");
   DIFF_FIELD(real, hle, tom_memcon2, "%04X");
   DIFF_FIELD(real, hle, tom_olp_lo, "%04X");
   DIFF_FIELD(real, hle, tom_olp_hi, "%04X");
   DIFF_FIELD(real, hle, tom_vmode, "%04X");
   DIFF_FIELD(real, hle, tom_int1, "%04X");
   DIFF_FIELD(real, hle, tom_bg_hi, "%04X");
   DIFF_FIELD(real, hle, tom_bg_lo, "%04X");
   DIFF_FIELD(real, hle, gpu_auth, "%08X");
   DIFF_FIELD(real, hle, gpu_flags, "%08X");
   DIFF_FIELD(real, hle, gpu_end, "%08X");
   DIFF_FIELD(real, hle, gpu_pc, "%08X");
   DIFF_FIELD(real, hle, gpu_ctrl, "%08X");
   DIFF_FIELD(real, hle, jerry_pit0, "%04X");
   DIFF_FIELD(real, hle, jerry_pit1, "%04X");
   DIFF_FIELD(real, hle, jerry_clk2, "%04X");
   DIFF_FIELD(real, hle, jerry_clk3, "%04X");
   DIFF_FIELD(real, hle, jerry_int, "%04X");
   DIFF_FIELD(real, hle, jerry_sclk, "%04X");
   DIFF_FIELD(real, hle, jerry_smode, "%04X");
}

int main(int argc, char **argv)
{
   const char *core_path;
   const char *rom_path;
   void *rom_data;
   size_t rom_size;
   int frames;
   struct BiosSnapshot real_snapshot;
   struct BiosSnapshot hle_snapshot;

   if (argc < 3)
   {
      fprintf(stderr, "Usage: %s <core.dylib> <rom_file> [frames]\n", argv[0]);
      return 1;
   }

   core_path = argv[1];
   rom_path = argv[2];
   frames = (argc >= 4) ? atoi(argv[3]) : 0;

   rom_data = read_file(rom_path, &rom_size);
   if (!rom_data)
   {
      fprintf(stderr, "Cannot read ROM: %s\n", rom_path);
      return 1;
   }

   if (!run_mode(core_path, rom_path, "real", "enabled", rom_data, rom_size, frames, &real_snapshot)
         || !run_mode(core_path, rom_path, "hle", "disabled", rom_data, rom_size, frames, &hle_snapshot))
   {
      free(rom_data);
      return 1;
   }

   printf("frames=%d\n", frames);
   print_snapshot(&real_snapshot);
   print_snapshot(&hle_snapshot);
   print_diffs(&real_snapshot, &hle_snapshot);

   free(rom_data);
   return 0;
}
