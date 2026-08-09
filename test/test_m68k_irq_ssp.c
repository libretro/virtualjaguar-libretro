/* test_m68k_irq_ssp.c -- 68000 interrupt entry must use the SUPERVISOR stack.
 *
 * The 68000 has two A7 registers selected by the S bit.  On exception entry it
 * copies the SR, sets S -- which makes A7 the supervisor stack pointer -- and
 * only then pushes the PC/SR frame (M68000 Programmer's Reference Manual,
 * "Exception Processing Sequence").  m68ki_init_exception() used to set
 * regs.s = 1 without swapping A7, so an interrupt taken in USER mode stacked
 * its frame on the user stack and the handler's RTE restored A7 from a stale
 * regs.usp while clobbering regs.isp with the user value.
 *
 * No commercial Jaguar title in the local test set ever leaves supervisor
 * mode, so nothing else in the suite exercises this path -- hence this
 * targeted test.
 *
 * Build: cc -o test/test_m68k_irq_ssp test/test_m68k_irq_ssp.c -ldl
 * Usage: ./test/test_m68k_irq_ssp   (needs a TEST_EXPORTS=1 core build)
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dlfcn.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include "../libretro-common/include/libretro.h"

#ifdef __APPLE__
#define CORE_FILENAME "virtualjaguar_libretro.dylib"
#elif defined(_WIN32)
#define CORE_FILENAME "virtualjaguar_libretro.dll"
#else
#define CORE_FILENAME "virtualjaguar_libretro.so"
#endif

/* 68K register IDs (must match m68kinterface.h enum) */
enum {
   M68K_REG_D0 = 0, M68K_REG_D1, M68K_REG_D2, M68K_REG_D3,
   M68K_REG_D4, M68K_REG_D5, M68K_REG_D6, M68K_REG_D7,
   M68K_REG_A0, M68K_REG_A1, M68K_REG_A2, M68K_REG_A3,
   M68K_REG_A4, M68K_REG_A5, M68K_REG_A6, M68K_REG_A7,
   M68K_REG_PC, M68K_REG_SR, M68K_REG_SP, M68K_REG_USP
};

/* Memory layout in Jaguar main RAM.  The two stacks are a page apart so a
   frame pushed on the wrong one is unambiguous from its address alone. */
#define VECTOR_64       0x000100   /* irq_ack_handler() returns 64 for level 2 */
#define RESULT_A7       0x003000   /* A7 as seen inside the handler */
#define RESULT_SR       0x003004   /* SR as seen inside the handler */
#define CODE_BASE       0x004000
#define HANDLER_BASE    0x005000
#define USER_STACK_TOP  0x008000
#define SUPER_STACK_TOP 0x009000
#define SENTINEL        0xDEADBEEF

static void (*p_retro_init)(void);
static void (*p_retro_deinit)(void);
static void (*p_retro_set_environment)(retro_environment_t);
static void (*p_retro_set_video_refresh)(retro_video_refresh_t);
static void (*p_retro_set_audio_sample)(retro_audio_sample_t);
static void (*p_retro_set_audio_sample_batch)(retro_audio_sample_batch_t);
static void (*p_retro_set_input_poll)(retro_input_poll_t);
static void (*p_retro_set_input_state)(retro_input_state_t);
static bool (*p_retro_load_game)(const struct retro_game_info *);
static int (*p_m68k_execute)(int);
static void (*p_m68k_set_reg)(int, unsigned int);
static unsigned int (*p_m68k_get_reg)(void *, int);
static void (*p_m68k_set_irq2)(unsigned int);
static uint8_t **p_jaguarMainRAM;

static void video_refresh(const void *d, unsigned w, unsigned h, size_t p)
{ (void)d;(void)w;(void)h;(void)p; }
static void audio_sample(int16_t l, int16_t r) { (void)l;(void)r; }
static size_t audio_batch(const int16_t *d, size_t f) { (void)d; return f; }
static void input_poll(void) {}
static int16_t input_state(unsigned p, unsigned d, unsigned i, unsigned id)
{ (void)p;(void)d;(void)i;(void)id; return 0; }

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
   struct retro_variable *var;

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
   case RETRO_ENVIRONMENT_GET_SAVE_DIRECTORY:
      *(const char **)data = "/tmp";
      return true;
   case RETRO_ENVIRONMENT_GET_VARIABLE:
      var = (struct retro_variable *)data;
      if (var->key && strcmp(var->key, "virtualjaguar_bios") == 0)
         { var->value = "disabled"; return true; }
      if (var->key && strcmp(var->key, "virtualjaguar_crash_detect") == 0)
         { var->value = "disabled"; return true; }
      var->value = NULL;
      return false;
   default:
      return false;
   }
}

static int passes = 0, fails = 0;
#define PASS(msg, ...) do { printf("  PASS: " msg "\n", ##__VA_ARGS__); passes++; } while(0)
#define FAIL(msg, ...) do { printf("  FAIL: " msg "\n", ##__VA_ARGS__); fails++; } while(0)

static void w16(uint32_t addr, uint16_t v)
{
   uint8_t *ram = *p_jaguarMainRAM;
   ram[addr]     = (uint8_t)((v >> 8) & 0xFF);
   ram[addr + 1] = (uint8_t)(v & 0xFF);
}

static void w32(uint32_t addr, uint32_t v)
{
   uint8_t *ram = *p_jaguarMainRAM;
   ram[addr]     = (uint8_t)((v >> 24) & 0xFF);
   ram[addr + 1] = (uint8_t)((v >> 16) & 0xFF);
   ram[addr + 2] = (uint8_t)((v >> 8) & 0xFF);
   ram[addr + 3] = (uint8_t)(v & 0xFF);
}

static uint32_t r32(uint32_t addr)
{
   uint8_t *ram = *p_jaguarMainRAM;
   return ((uint32_t)ram[addr] << 24) | ((uint32_t)ram[addr + 1] << 16)
        | ((uint32_t)ram[addr + 2] << 8) | (uint32_t)ram[addr + 3];
}

/* Build the program.  Supervisor prologue drops to user mode, the user body
   pushes a sentinel on the user stack, then spins. */
static void build_program(void)
{
   uint32_t a = CODE_BASE;
   uint32_t h = HANDLER_BASE;

   /* MOVE.L #USER_STACK_TOP, A0 */
   w16(a, 0x207C); w16(a + 2, USER_STACK_TOP >> 16);
   w16(a + 4, USER_STACK_TOP & 0xFFFF); a += 6;
   /* MOVE.L A0, USP  -- seed the user stack pointer while still privileged */
   w16(a, 0x4E60); a += 2;
   /* ANDI.W #$D8FF, SR -- clear S (bit 13) and the interrupt mask, so the
      level-2 IRQ below is enabled and taken in USER mode.  MakeFromSR()
      performs the 1->0 swap, leaving A7 = USER_STACK_TOP. */
   w16(a, 0x027C); w16(a + 2, 0xD8FF); a += 4;
   /* MOVE.L #SENTINEL, D0 */
   w16(a, 0x203C); w16(a + 2, SENTINEL >> 16);
   w16(a + 4, SENTINEL & 0xFFFF); a += 6;
   /* MOVE.L D0, -(A7) -- a real user-stack push the frame must not disturb */
   w16(a, 0x2F00); a += 2;
   /* BRA.S * -- park here; A7 stays at USER_STACK_TOP-4 */
   w16(a, 0x60FE);

   /* Handler: record A7 and SR, then return. */
   /* MOVE.L A7, D1 (src mode 001 = address register direct, reg 7) */
   w16(h, 0x220F); h += 2;
   /* MOVE.L D1, RESULT_A7 (absolute long) */
   w16(h, 0x23C1); w16(h + 2, RESULT_A7 >> 16);
   w16(h + 4, RESULT_A7 & 0xFFFF); h += 6;
   /* MOVE.W SR, D2 ; MOVE.L D2, RESULT_SR */
   w16(h, 0x40C2); h += 2;
   w16(h, 0x23C2); w16(h + 2, RESULT_SR >> 16);
   w16(h + 4, RESULT_SR & 0xFFFF); h += 6;
   /* RTE */
   w16(h, 0x4E73);

   w32(VECTOR_64, HANDLER_BASE);
   w32(RESULT_A7, 0);
   w32(RESULT_SR, 0);
}

int main(int argc, char **argv)
{
   void *handle;
   uint8_t *dummy_rom;
   struct retro_game_info game;
   uint32_t user_a7, handler_a7, handler_sr, post_a7, sentinel;

   (void)argc; (void)argv;

   printf("=== 68K interrupt entry uses the supervisor stack ===\n");

   handle = dlopen("./" CORE_FILENAME, RTLD_NOW);
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
   LOAD(m68k_execute);
   LOAD(m68k_set_reg);
   LOAD(m68k_get_reg);
   LOAD(m68k_set_irq2);
#undef LOAD

   p_jaguarMainRAM = (uint8_t **)dlsym(handle, "jaguarMainRAM");
   if (!p_jaguarMainRAM || !*p_jaguarMainRAM) {
      fprintf(stderr, "Missing jaguarMainRAM\n");
      return 1;
   }

   p_retro_set_environment(environment);
   p_retro_set_video_refresh(video_refresh);
   p_retro_set_audio_sample(audio_sample);
   p_retro_set_audio_sample_batch(audio_batch);
   p_retro_set_input_poll(input_poll);
   p_retro_set_input_state(input_state);
   p_retro_init();

   dummy_rom = (uint8_t *)calloc(1, 131072);
   if (!dummy_rom) { fprintf(stderr, "oom\n"); return 1; }
   dummy_rom[0x404] = 0x00; dummy_rom[0x405] = 0x80;
   dummy_rom[0x406] = 0x20; dummy_rom[0x407] = 0x00;
   dummy_rom[0x2000] = 0x60; dummy_rom[0x2001] = 0xFE;

   memset(&game, 0, sizeof(game));
   game.path = "dummy.jag";
   game.data = dummy_rom;
   game.size = 131072;

   if (!p_retro_load_game(&game)) {
      fprintf(stderr, "retro_load_game failed\n");
      p_retro_deinit(); free(dummy_rom);
      return 1;
   }

   build_program();

   /* Enter supervisor mode with a known supervisor stack, then run the
      prologue + user body.  Order matters: set SR first (so the S bit is
      already 1), then A7, so no swap fires behind our back. */
   p_m68k_set_reg(M68K_REG_SR, 0x2700);
   p_m68k_set_reg(M68K_REG_SP, SUPER_STACK_TOP);
   p_m68k_set_reg(M68K_REG_PC, CODE_BASE);
   p_m68k_execute(200);

   user_a7 = p_m68k_get_reg(NULL, M68K_REG_A7);
   if ((p_m68k_get_reg(NULL, M68K_REG_SR) & 0x2000) == 0)
      PASS("prologue reached user mode (SR=$%04X)",
           p_m68k_get_reg(NULL, M68K_REG_SR));
   else
      FAIL("prologue still supervisor (SR=$%04X) -- rest of test is void",
           p_m68k_get_reg(NULL, M68K_REG_SR));

   if (user_a7 == USER_STACK_TOP - 4)
      PASS("user stack in use before the IRQ (A7=$%06X)", user_a7);
   else
      FAIL("expected user A7=$%06X, got $%06X", USER_STACK_TOP - 4, user_a7);

   /* Take a level-2 interrupt right here, in user mode. */
   p_m68k_set_irq2(2);
   p_m68k_execute(200);

   handler_a7 = r32(RESULT_A7);
   handler_sr = r32(RESULT_SR);
   post_a7    = p_m68k_get_reg(NULL, M68K_REG_A7);
   sentinel   = r32(USER_STACK_TOP - 4);

   if (handler_a7 == 0)
      FAIL("handler never ran -- no A7 recorded");
   else if (handler_a7 > USER_STACK_TOP && handler_a7 <= SUPER_STACK_TOP)
      PASS("frame stacked on the supervisor stack (handler A7=$%06X)",
           handler_a7);
   else
      FAIL("frame stacked on the WRONG stack: handler A7=$%06X, expected "
           "$%06X..$%06X", handler_a7, USER_STACK_TOP + 1, SUPER_STACK_TOP);

   if ((handler_sr & 0x2000) != 0)
      PASS("handler runs in supervisor mode (SR=$%04X)", handler_sr & 0xFFFF);
   else
      FAIL("handler SR=$%04X has S clear", handler_sr & 0xFFFF);

   if (sentinel == SENTINEL)
      PASS("user stack contents intact across the interrupt");
   else
      FAIL("user stack clobbered: $%08X at $%06X, expected $%08X",
           sentinel, USER_STACK_TOP - 4, SENTINEL);

   if (post_a7 == USER_STACK_TOP - 4)
      PASS("RTE restored the user stack pointer (A7=$%06X)", post_a7);
   else
      FAIL("RTE left A7=$%06X, expected $%06X (stale regs.usp)",
           post_a7, USER_STACK_TOP - 4);

   printf("\n=== Results: %d passed, %d failed ===\n", passes, fails);

   free(dummy_rom);
   return fails ? 1 : 0;
}
