/* test_dsp_irq.c -- DSP interrupt dispatch and execution test.
 * Loads DSP code into DSP RAM, triggers interrupts, and verifies behavior.
 * Build: cc -o test/test_dsp_irq test/test_dsp_irq.c -ldl
 * Usage: ./test/test_dsp_irq [rom_for_dsp_code.jag] */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dlfcn.h>
#include <stdarg.h>
#include <stdbool.h>
#include "../libretro-common/include/libretro.h"

/* --- libretro function pointers --- */
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

/* --- emulator internals via dlsym --- */
static uint8_t **p_jaguarMainRAM;
static uint32_t *p_dsp_control;
/* dsp_flags is static in dsp.c, not accessible via dlsym */
static uint32_t *p_dsp_pc;
static uint32_t *p_dsp_reg_bank_0;
static uint32_t *p_dsp_reg_bank_1;
static uint8_t *(*p_DSPGetRAM)(void);

/* --- stubs --- */
static void video_refresh(const void *d, unsigned w, unsigned h, size_t p)
{ (void)d; (void)w; (void)h; (void)p; }
static void audio_sample(int16_t l, int16_t r) { (void)l; (void)r; }
static size_t audio_batch(const int16_t *d, size_t f) { (void)d; return f; }
static void input_poll(void) {}
static int16_t input_state(unsigned p, unsigned d, unsigned i, unsigned id)
{ (void)p; (void)d; (void)i; (void)id; return 0; }

static void log_printf(enum retro_log_level level, const char *fmt, ...)
{
   va_list ap;
   if (level < RETRO_LOG_WARN) return;
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
      *(const char **)data = "test/roms/private";
      return true;
   case RETRO_ENVIRONMENT_GET_SAVE_DIRECTORY:
      *(const char **)data = "/tmp";
      return true;
   case RETRO_ENVIRONMENT_GET_VARIABLE: {
      struct retro_variable *var = (struct retro_variable *)data;
      if (var->key && strcmp(var->key, "virtualjaguar_bios") == 0) {
         var->value = "disabled";
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

/* DSP opcode names for disassembly */
static const char *dsp_op_names[64] = {
   "add",     "addc",    "addq",    "addqt",   "sub",     "subc",    "subq",    "subqt",
   "neg",     "and",     "or",      "xor",     "not",     "btst",    "bset",    "bclr",
   "mult",    "imult",   "imultn",  "resmac",  "imacn",   "div",     "abs",     "sh",
   "shlq",    "shrq",    "sha",     "sharq",   "ror",     "rorq",    "cmp",     "cmpq",
   "sat8",    "sat16",   "move",    "moveq",   "moveta",  "movefa",  "movei",   "loadb",
   "loadw",   "load",    "sat32s",  "load14i", "load15i", "storeb",  "storew",  "store",
   "mirror",  "stor14i", "stor15i", "move_pc", "jump",    "jr",      "mmult",   "mtoi",
   "normi",   "nop",     "load14r", "load15r", "stor14r", "stor15r", "illegal", "addqmod"
};

static void disasm_dsp(uint8_t *ram, uint32_t start, uint32_t end)
{
   uint32_t pc;
   for (pc = start; pc < end; ) {
      uint16_t op = (ram[pc - 0xF1B000] << 8) | ram[pc - 0xF1B000 + 1];
      unsigned idx = op >> 10;
      unsigned reg1 = (op >> 5) & 0x1F;
      unsigned reg2 = op & 0x1F;
      if (idx == 38) { /* movei */
         uint16_t lo = (ram[pc - 0xF1B000 + 2] << 8) | ram[pc - 0xF1B000 + 3];
         uint16_t hi = (ram[pc - 0xF1B000 + 4] << 8) | ram[pc - 0xF1B000 + 5];
         uint32_t imm = (uint32_t)lo | ((uint32_t)hi << 16);
         printf("  %06X: %04X %04X %04X  movei  #$%08X, R%d\n", pc, op, lo, hi, imm, reg2);
         pc += 6;
      } else if (idx == 35) { /* moveq */
         printf("  %06X: %04X            moveq  #%d, R%d\n", pc, op, reg1, reg2);
         pc += 2;
      } else if (idx == 52) { /* jump */
         printf("  %06X: %04X            jump   cc%d, (R%d)\n", pc, op, reg2, reg1);
         pc += 2;
      } else if (idx == 53) { /* jr */
         int offset = (reg1 & 0x10) ? (int)(0xFFFFFFF0 | reg1) : (int)reg1;
         printf("  %06X: %04X            jr     cc%d, PC%+d [$%06X]\n",
            pc, op, reg2, offset * 2, pc + 2 + offset * 2);
         pc += 2;
      } else {
         printf("  %06X: %04X            %-7s R%d, R%d\n", pc, op, dsp_op_names[idx], reg1, reg2);
         pc += 2;
      }
   }
}

static void dump_dsp_ram_hex(uint8_t *ram, uint32_t start, uint32_t len)
{
   uint32_t i;
   for (i = 0; i < len; i += 16) {
      uint32_t j;
      printf("  %06X: ", 0xF1B000 + start + i);
      for (j = 0; j < 16 && (start + i + j) < len; j++)
         printf("%02X ", ram[start + i + j]);
      printf("\n");
   }
}

#define PASS(msg) do { printf("  PASS: %s\n", msg); passes++; } while(0)
#define FAIL(msg, ...) do { printf("  FAIL: " msg "\n", ##__VA_ARGS__); fails++; } while(0)

int main(int argc, char *argv[])
{
   int passes = 0, fails = 0;
   void *handle;
   const char *rom_path = NULL;
   uint8_t *rom_data = NULL;
   size_t rom_size = 0;

   if (argc > 1)
      rom_path = argv[1];

   handle = dlopen("./virtualjaguar_libretro.dylib", RTLD_NOW);
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

   p_jaguarMainRAM = dlsym(handle, "jaguarMainRAM");
   p_dsp_control = dlsym(handle, "dsp_control");
   /* dsp_flags is static, not accessible via dlsym */
   p_dsp_pc = dlsym(handle, "dsp_pc");
   p_DSPGetRAM = dlsym(handle, "DSPGetRAM");
   p_dsp_reg_bank_0 = dlsym(handle, "dsp_reg_bank_0");
   p_dsp_reg_bank_1 = dlsym(handle, "dsp_reg_bank_1");

   if (!p_dsp_control || !p_DSPGetRAM || !p_dsp_reg_bank_0) {
      fprintf(stderr, "Missing DSP symbols (dsp_control=%p, DSPGetRAM=%p, dsp_reg_bank_0=%p)\n",
         (void*)p_dsp_control, (void*)p_DSPGetRAM, (void*)p_dsp_reg_bank_0);
      return 1;
   }

   p_retro_set_environment(environment);
   p_retro_set_video_refresh(video_refresh);
   p_retro_set_audio_sample(audio_sample);
   p_retro_set_audio_sample_batch(audio_batch);
   p_retro_set_input_poll(input_poll);
   p_retro_set_input_state(input_state);
   p_retro_init();

   /* --- Test 1: Load a ROM that uses DSP and trace DSP code --- */
   if (rom_path) {
      FILE *f;
      struct retro_game_info game = {0};
      uint8_t *dsp_ram;
      unsigned frame;

      printf("=== DSP IRQ Test with ROM: %s ===\n", rom_path);

      f = fopen(rom_path, "rb");
      if (!f) { fprintf(stderr, "Cannot open: %s\n", rom_path); return 1; }
      fseek(f, 0, SEEK_END);
      rom_size = ftell(f);
      fseek(f, 0, SEEK_SET);
      rom_data = malloc(rom_size);
      fread(rom_data, 1, rom_size, f);
      fclose(f);

      game.path = rom_path;
      game.data = rom_data;
      game.size = rom_size;

      if (!p_retro_load_game(&game)) {
         fprintf(stderr, "retro_load_game FAILED\n");
         p_retro_deinit();
         return 1;
      }

      /* Run frames and watch for DSP activation */
      printf("\nRunning frames, watching DSP...\n");
      for (frame = 0; frame < 60; frame++) {
         uint32_t ctrl_before = *p_dsp_control;
         p_retro_run();
         uint32_t ctrl_after = *p_dsp_control;

         /* Detect DSP start */
         if (!(ctrl_before & 0x01) && (ctrl_after & 0x01)) {
            printf("\n--- DSP started at frame %u ---\n", frame);
            printf("  ctrl=%08X flags=%08X pc=%08X\n",
               *p_dsp_control, 0u/*flags*/, *p_dsp_pc);
         }

         /* Detect DSP stop */
         if ((ctrl_before & 0x01) && !(ctrl_after & 0x01)) {
            printf("\n--- DSP stopped at frame %u ---\n", frame);
            printf("  ctrl=%08X flags=%08X pc=%08X\n",
               *p_dsp_control, 0u/*flags*/, *p_dsp_pc);

            dsp_ram = p_DSPGetRAM();
            if (dsp_ram) {
               printf("\nDSP Interrupt Vectors ($F1B000-$F1B05F):\n");
               disasm_dsp(dsp_ram, 0xF1B000, 0xF1B060);

               printf("\nDSP Shutdown code ($F1B780-$F1B794):\n");
               disasm_dsp(dsp_ram, 0xF1B780, 0xF1B794);

               printf("\nDSP Init/Main code ($F1B794-$F1B800):\n");
               disasm_dsp(dsp_ram, 0xF1B794, 0xF1B800);

               printf("\nDSP Handshake area ($F1B9D0-$F1BA00):\n");
               dump_dsp_ram_hex(dsp_ram, 0x9D0, 0x30);

               printf("\nDSP Register Banks:\n");
               printf("  Bank 0: R0=%08X R1=%08X R30=%08X R31=%08X\n",
                  p_dsp_reg_bank_0[0], p_dsp_reg_bank_0[1],
                  p_dsp_reg_bank_0[30], p_dsp_reg_bank_0[31]);
               if (p_dsp_reg_bank_1)
                  printf("  Bank 1: R0=%08X R1=%08X R30=%08X R31=%08X\n",
                     p_dsp_reg_bank_1[0], p_dsp_reg_bank_1[1],
                     p_dsp_reg_bank_1[30], p_dsp_reg_bank_1[31]);
            }
            break;
         }

         /* Print status every 10 frames */
         if (frame < 5 || frame % 10 == 0)
            printf("F%03u: ctrl=%08X flags=%08X pc=%08X run=%d\n",
               frame, *p_dsp_control, 0u/*flags*/, *p_dsp_pc,
               (*p_dsp_control & 1) ? 1 : 0);
      }

      /* Check: did DSP process the command? */
      if (p_DSPGetRAM) {
         uint8_t *dsp_ram_ptr = p_DSPGetRAM();
         uint32_t handshake;
         if (dsp_ram_ptr) {
            handshake = (dsp_ram_ptr[0x9D8] << 24) |
                              (dsp_ram_ptr[0x9D9] << 16) |
                              (dsp_ram_ptr[0x9DA] << 8) |
                              dsp_ram_ptr[0x9DB];
            printf("\nHandshake $F1B9D8 = %08X\n", handshake);
            if (handshake == 0)
               PASS("DSP cleared handshake flag");
            else
               FAIL("DSP did NOT clear handshake ($F1B9D8=%08X)", handshake);
         }
      }

      /* Also dump the full ISR code area for analysis */
      if (p_DSPGetRAM) {
         uint8_t *dr = p_DSPGetRAM();
         if (dr) {
            printf("\nFull DSP code ($F1B300-$F1B400) - ISR body:\n");
            disasm_dsp(dr, 0xF1B300, 0xF1B400);

            printf("\nFull DSP code ($F1B700-$F1B800) - shutdown/init area:\n");
            disasm_dsp(dr, 0xF1B700, 0xF1B800);
         }
      }

      p_retro_unload_game();
   }

   printf("\n=== Results: %d passed, %d failed ===\n", passes, fails);

   p_retro_deinit();
   dlclose(handle);
   if (rom_data) free(rom_data);
   return fails > 0 ? 1 : 0;
}
