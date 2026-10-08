/* test_m68k_prefetch.c -- the 68000 prefetch queue as self-modifying code
 * sees it (issue #811).
 *
 * The 68000 holds the two instruction words after the one it is executing.
 * A write to a word already in the queue does not change what runs next:
 * the stale copy executes, and only a refetch (after a branch, or the next
 * time round a loop) sees the new value.  Xenowings' anti-tamper check
 * depends on this: `ori.l #$04000400,(a1)` turns the next two
 * `addq.l #1,d6` into `addq.l #3,d6`, and the check only passes if the old
 * pair runs.
 *
 * Which words are already queued when the write lands depends on the
 * instruction's bus-cycle order: read-modify-write forms prefetch before
 * writing (both next words are queued), MOVE writes before its last
 * prefetch (only the next word is queued).  The cases below pin both
 * classes, the branch flush, the second pass through patched code, a data
 * read of a patched word, and a savestate taken with words queued.
 *
 * Every program is assembled into main RAM; no ROM is needed.
 *
 * Build: cc -o test/test_m68k_prefetch test/test_m68k_prefetch.c -ldl
 * Usage: ./test/test_m68k_prefetch   (needs a TEST_EXPORTS=1 core build)
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

#define OP_ADDQ1_D6   0x5286u   /* addq.l #1,d6 */
#define OP_ADDQ3_D6   0x5686u   /* addq.l #3,d6 = OP_ADDQ1_D6 | $0400 */
#define OP_NOP        0x4E71u
#define OP_BRA_SELF   0x60FEu   /* bra.s * */

#define CODE_BASE 0x4000u
#define STACK_TOP 0x8000u

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
static size_t (*p_retro_serialize_size)(void);
static bool (*p_retro_serialize)(void *, size_t);
static bool (*p_retro_unserialize)(const void *, size_t);
static int (*p_m68k_execute)(int);
static void (*p_m68k_set_reg)(int, unsigned int);
static unsigned int (*p_m68k_get_reg)(void *, int);
static uint8_t **p_jaguarMainRAM;

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
   if (level < RETRO_LOG_WARN)
      return;
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
   {
      struct retro_variable *var = (struct retro_variable *)data;
      if (var->key && strcmp(var->key, "virtualjaguar_bios") == 0)
      {
         var->value = "disabled";
         return true;
      }
      var->value = NULL;
      return false;
   }
   default:
      return false;
   }
}

static int passes = 0, fails = 0;

static void check(int ok, const char *what, uint32_t got, uint32_t want)
{
   if (ok)
   {
      printf("  PASS: %s ($%08X)\n", what, (unsigned)got);
      passes++;
   }
   else
   {
      printf("  FAIL: %s: got $%08X, want $%08X\n", what,
             (unsigned)got, (unsigned)want);
      fails++;
   }
}

/* ---- a tiny assembler into main RAM ---- */
static uint32_t asm_pc;

static void w16(uint32_t addr, uint32_t v)
{
   uint8_t *ram = *p_jaguarMainRAM;
   ram[addr]     = (uint8_t)(v >> 8);
   ram[addr + 1] = (uint8_t)v;
}

static uint32_t r32(uint32_t addr)
{
   uint8_t *ram = *p_jaguarMainRAM;
   return ((uint32_t)ram[addr] << 24) | ((uint32_t)ram[addr + 1] << 16)
        | ((uint32_t)ram[addr + 2] << 8) | ram[addr + 3];
}

static void e16(uint32_t v) { w16(asm_pc, v); asm_pc += 2; }
static void e32(uint32_t v) { e16(v >> 16); e16(v & 0xFFFF); }

static void moveq(int imm, int dn)  { e16(0x7000u | (dn << 9) | (imm & 0xFF)); }
static void lea_abs(uint32_t a, int an) { e16(0x41F9u | (an << 9)); e32(a); }
static void move_w_imm(uint32_t imm, int dn) { e16(0x303Cu | (dn << 9)); e16(imm); }
static void move_w_d_ind(int dn, int an) { e16(0x3080u | (an << 9) | dn); }
static void move_l_ind_d(int an, int dn) { e16(0x2010u | (dn << 9) | an); }
static void ori_l_ind(uint32_t imm, int an) { e16(0x0090u | an); e32(imm); }
static void ori_w_ind(uint32_t imm, int an) { e16(0x0050u | an); e16(imm); }
static void bra_s_to(uint32_t target)
{
   e16(0x6000u | ((target - (asm_pc + 2)) & 0xFF));
}
static void dbra_to(int dn, uint32_t target)
{
   e16(0x51C8u | dn);
   e16((target - asm_pc) & 0xFFFF);
}

static void prep(void)
{
   uint32_t a;
   int i;

   for (a = CODE_BASE; a < CODE_BASE + 0x200; a += 2)
      w16(a, OP_NOP);
   for (i = M68K_REG_D0; i <= M68K_REG_A6; i++)
      p_m68k_set_reg(i, 0);
   asm_pc = CODE_BASE;
}

static void run(int cycles)
{
   p_m68k_set_reg(M68K_REG_SR, 0x2700);
   p_m68k_set_reg(M68K_REG_SP, STACK_TOP);
   p_m68k_set_reg(M68K_REG_PC, CODE_BASE);
   p_m68k_execute(cycles);
}

static uint32_t D(int n) { return p_m68k_get_reg(NULL, M68K_REG_D0 + n); }

/* ============================================================ */

/* The Xenowings sequence, run twice.  Pass 1: the queue already holds both
 * `addq #1`, so d6 += 2.  Pass 2: memory now holds `addq #3` and the queue
 * fetched it before the (idempotent) ORI, so d6 += 6.  Total 8; a core with
 * no queue gets 12. */
static void test_ori_l_patches_queued_pair(void)
{
   uint32_t loop, t;

   printf("\n--- ori.l #$04000400,(a1) over the next two words ---\n");
   prep();
   moveq(0, 6);
   moveq(1, 7);                  /* dbra: two passes */
   t = CODE_BASE + 2 + 2 + 6 + 6;
   lea_abs(t, 1);
   loop = asm_pc;
   ori_l_ind(0x04000400u, 1);
   e16(OP_ADDQ1_D6);             /* t   */
   e16(OP_ADDQ1_D6);             /* t+2 */
   dbra_to(7, loop);
   move_l_ind_d(1, 2);           /* data read of the patched words */
   e16(OP_BRA_SELF);
   run(600);

   check(D(6) == 8, "stale pair on pass 1, patched pair on pass 2: d6 == 2 + 6",
         D(6), 8);
   check(D(2) == 0x56865686u, "data read of the patched words sees the new value",
         D(2), 0x56865686u);
   check(r32(t) == 0x56865686u, "memory holds the patched pair", r32(t), 0x56865686u);
}

/* MOVE writes before its final prefetch: the word at next_pc is queued
 * (stale), the word at next_pc+2 is not (fresh). */
static void test_move_w_next_word_is_stale(void)
{
   uint32_t t;

   printf("\n--- move.w Dn,(An) onto next_pc ---\n");
   prep();
   moveq(0, 6);
   move_w_imm(OP_ADDQ3_D6, 0);
   t = CODE_BASE + 2 + 4 + 6 + 2;
   lea_abs(t, 0);
   move_w_d_ind(0, 0);
   e16(OP_ADDQ1_D6);             /* t: queued before the write */
   e16(OP_BRA_SELF);
   run(300);
   check(D(6) == 1, "the queued `addq #1` runs", D(6), 1);
}

static void test_move_w_word_after_is_fresh(void)
{
   uint32_t t2;

   printf("\n--- move.w Dn,(An) onto next_pc+2 ---\n");
   prep();
   moveq(0, 6);
   move_w_imm(OP_ADDQ3_D6, 0);
   t2 = CODE_BASE + 2 + 4 + 6 + 2 + 2;
   lea_abs(t2, 0);
   move_w_d_ind(0, 0);
   e16(OP_NOP);                  /* next_pc */
   e16(OP_ADDQ1_D6);             /* next_pc+2: fetched after the write */
   e16(OP_BRA_SELF);
   run(300);
   check(D(6) == 3, "MOVE's last prefetch follows the write: `addq #3` runs",
         D(6), 3);
}

/* A read-modify-write instruction prefetches before writing, so next_pc+2
 * is already queued. */
static void test_ori_w_word_after_is_stale(void)
{
   uint32_t t2;

   printf("\n--- ori.w #$0400,(a1) onto next_pc+2 ---\n");
   prep();
   moveq(0, 6);
   t2 = CODE_BASE + 2 + 6 + 4 + 2;
   lea_abs(t2, 1);
   ori_w_ind(0x0400u, 1);
   e16(OP_NOP);                  /* next_pc */
   e16(OP_ADDQ1_D6);             /* next_pc+2: queued before the write */
   e16(OP_BRA_SELF);
   run(300);
   check(D(6) == 1, "the queued `addq #1` runs", D(6), 1);
}

/* A taken branch refills the queue, so a patched word that was queued runs
 * in its new form when control comes back to it. */
static void test_branch_flushes_queue(void)
{
   uint32_t t, x, end;

   printf("\n--- patch, then a taken branch away and back ---\n");
   prep();
   moveq(0, 6);
   t   = CODE_BASE + 2 + 6 + 4 + 2;
   x   = t + 4;
   end = x + 2;
   lea_abs(t, 1);
   ori_w_ind(0x0400u, 1);
   bra_s_to(x);                  /* next_pc: branch away */
   e16(OP_ADDQ1_D6);             /* t = next_pc+2, queued at the write */
   bra_s_to(end);
   bra_s_to(t);                  /* x: come back */
   e16(OP_BRA_SELF);             /* end */
   run(400);
   check(D(6) == 3, "after the branch the patched `addq #3` runs", D(6), 3);
}

/* A savestate taken while the queue holds stale words must carry them:
 * loading it and running on gives the same result as not saving. */
static void test_savestate_carries_queue(void)
{
   uint32_t t;
   size_t sz;
   void *st;
   uint32_t first, second;

   printf("\n--- savestate with patched words queued ---\n");
   prep();
   t = CODE_BASE + 6;
   ori_l_ind(0x04000400u, 1);    /* CODE_BASE */
   e16(OP_ADDQ1_D6);             /* t */
   e16(OP_ADDQ1_D6);
   e16(OP_BRA_SELF);
   p_m68k_set_reg(M68K_REG_A1, t);
   p_m68k_set_reg(M68K_REG_SR, 0x2700);
   p_m68k_set_reg(M68K_REG_SP, STACK_TOP);
   p_m68k_set_reg(M68K_REG_PC, CODE_BASE);
   p_m68k_execute(1);            /* exactly the ORI */

   sz = p_retro_serialize_size();
   st = malloc(sz);
   if (!st || !p_retro_serialize(st, sz))
   {
      check(0, "retro_serialize", 0, 1);
      free(st);
      return;
   }
   p_m68k_execute(200);
   first = D(6);
   if (!p_retro_unserialize(st, sz))
   {
      check(0, "retro_unserialize", 0, 1);
      free(st);
      return;
   }
   p_m68k_execute(200);
   second = D(6);
   free(st);

   check(first == 2, "uninterrupted run: stale pair, d6 == 2", first, 2);
   check(second == 2, "after save + load: still the stale pair, d6 == 2",
         second, 2);
}

/* ============================================================ */

int main(int argc, char *argv[])
{
   void *handle;
   uint8_t *dummy_rom;
   struct retro_game_info game;
   (void)argc; (void)argv;

   printf("=== 68000 prefetch queue (#811) ===\n");

   handle = dlopen("./" CORE_FILENAME, RTLD_NOW);
   if (!handle)
   {
      fprintf(stderr, "dlopen: %s\n", dlerror());
      return 1;
   }

#define LOAD(sym) do { \
   *(void **)(&p_##sym) = dlsym(handle, #sym); \
   if (!p_##sym) { fprintf(stderr, "Missing: %s\n", #sym); return 1; } \
} while (0)

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
   LOAD(retro_serialize_size);
   LOAD(retro_serialize);
   LOAD(retro_unserialize);
   LOAD(m68k_execute);
   LOAD(m68k_set_reg);
   LOAD(m68k_get_reg);

   p_jaguarMainRAM = (uint8_t **)dlsym(handle, "jaguarMainRAM");
   if (!p_jaguarMainRAM || !*p_jaguarMainRAM)
   {
      fprintf(stderr, "Missing jaguarMainRAM (needs a TEST_EXPORTS=1 build)\n");
      return 1;
   }

   p_retro_set_environment(environment);
   p_retro_set_video_refresh(video_refresh);
   p_retro_set_audio_sample(audio_sample);
   p_retro_set_audio_sample_batch(audio_batch);
   p_retro_set_input_poll(input_poll);
   p_retro_set_input_state(input_state);
   p_retro_init();

   /* Minimal cart: entry point $802000 holding `bra.s *`. */
   dummy_rom = (uint8_t *)calloc(1, 131072);
   dummy_rom[0x404] = 0x00; dummy_rom[0x405] = 0x80;
   dummy_rom[0x406] = 0x20; dummy_rom[0x407] = 0x00;
   dummy_rom[0x2000] = 0x60; dummy_rom[0x2001] = 0xFE;

   memset(&game, 0, sizeof(game));
   game.path = "dummy.jag";
   game.data = dummy_rom;
   game.size = 131072;
   if (!p_retro_load_game(&game))
   {
      fprintf(stderr, "retro_load_game failed\n");
      p_retro_deinit();
      free(dummy_rom);
      return 1;
   }

   test_ori_l_patches_queued_pair();
   test_move_w_next_word_is_stale();
   test_move_w_word_after_is_fresh();
   test_ori_w_word_after_is_stale();
   test_branch_flushes_queue();
   test_savestate_carries_queue();

   printf("\n=== Results: %d passed, %d failed ===\n", passes, fails);

   p_retro_unload_game();
   p_retro_deinit();
   dlclose(handle);
   free(dummy_rom);
   return fails > 0 ? 1 : 0;
}
