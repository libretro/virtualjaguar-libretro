/* test_m68k_ops.c -- Motorola 68000 instruction set verification.
 * Writes 68K machine code to Jaguar main RAM, executes via m68k_execute,
 * and verifies register results.
 *
 * Build: cc -o test/test_m68k_ops test/test_m68k_ops.c -ldl
 * Usage: ./test/test_m68k_ops
 *
 * Tests: MOVEQ, MOVE.L, ADD, ADDI, ADDQ, SUB, SUBQ, NEG, AND, OR, EOR, NOT,
 *        CLR, SWAP, EXT, MULU, MULS, DIVU, DIVS, LSL, LSR, ASR, ROL, ROR,
 *        BTST, BSET, BCLR, CMP, TST, BRA, BEQ, BNE, BSR/RTS, NOP, LEA
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dlfcn.h>
#include <stdarg.h>
#include <stdbool.h>
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

/* ---- 68K instruction encoding macros (big-endian, Motorola format) ---- */

/* MOVEQ #imm8, Dn (sign-extended to 32 bits) */
#define M68K_MOVEQ(imm8, dn) ((uint16_t)(0x7000 | ((dn) << 9) | ((imm8) & 0xFF)))

/* ADD.L Dn, Dm: Dm = Dm + Dn */
#define M68K_ADD_L(src, dst) ((uint16_t)(0xD080 | ((dst) << 9) | (src)))

/* SUB.L Dn, Dm: Dm = Dm - Dn */
#define M68K_SUB_L(src, dst) ((uint16_t)(0x9080 | ((dst) << 9) | (src)))

/* AND.L Dn, Dm: Dm = Dm & Dn */
#define M68K_AND_L(src, dst) ((uint16_t)(0xC080 | ((dst) << 9) | (src)))

/* OR.L Dn, Dm: Dm = Dm | Dn */
#define M68K_OR_L(src, dst) ((uint16_t)(0x8080 | ((dst) << 9) | (src)))

/* EOR.L Dn, Dm: Dm = Dm ^ Dn (note: src in bits 11-9) */
#define M68K_EOR_L(src, dst) ((uint16_t)(0xB180 | ((src) << 9) | (dst)))

/* NOT.L Dn */
#define M68K_NOT_L(dn) ((uint16_t)(0x4680 | (dn)))

/* NEG.L Dn */
#define M68K_NEG_L(dn) ((uint16_t)(0x4480 | (dn)))

/* CLR.L Dn */
#define M68K_CLR_L(dn) ((uint16_t)(0x4280 | (dn)))

/* SWAP Dn (swap upper and lower 16 bits) */
#define M68K_SWAP(dn) ((uint16_t)(0x4840 | (dn)))

/* EXT.W Dn (sign-extend byte to word) */
#define M68K_EXT_W(dn) ((uint16_t)(0x4880 | (dn)))

/* EXT.L Dn (sign-extend word to long) */
#define M68K_EXT_L(dn) ((uint16_t)(0x48C0 | (dn)))

/* TST.L Dn */
#define M68K_TST_L(dn) ((uint16_t)(0x4A80 | (dn)))

/* ADDQ.L #n, Dn (n=1-8, 8 encoded as 0) */
#define M68K_ADDQ_L(n, dn) ((uint16_t)(0x5080 | (((n) & 7) << 9) | (dn)))

/* SUBQ.L #n, Dn (n=1-8) */
#define M68K_SUBQ_L(n, dn) ((uint16_t)(0x5180 | (((n) & 7) << 9) | (dn)))

/* MULU.W Dn, Dm: Dm.L = Dm.W(unsigned) * Dn.W(unsigned) */
#define M68K_MULU(src, dst) ((uint16_t)(0xC0C0 | ((dst) << 9) | (src)))

/* MULS.W Dn, Dm: Dm.L = Dm.W(signed) * Dn.W(signed) */
#define M68K_MULS(src, dst) ((uint16_t)(0xC1C0 | ((dst) << 9) | (src)))

/* DIVU.W Dn, Dm: Dm = Dm / Dn (quotient in low word, remainder in high) */
#define M68K_DIVU(src, dst) ((uint16_t)(0x80C0 | ((dst) << 9) | (src)))

/* DIVS.W Dn, Dm */
#define M68K_DIVS(src, dst) ((uint16_t)(0x81C0 | ((dst) << 9) | (src)))

/* CMP.L Dn, Dm: flags = Dm - Dn */
#define M68K_CMP_L(src, dst) ((uint16_t)(0xB080 | ((dst) << 9) | (src)))

/* LSL.L #n, Dn (n=1-8) */
#define M68K_LSL_L(n, dn) ((uint16_t)(0xE188 | (((n) & 7) << 9) | (dn)))

/* LSR.L #n, Dn */
#define M68K_LSR_L(n, dn) ((uint16_t)(0xE088 | (((n) & 7) << 9) | (dn)))

/* ASL.L #n, Dn */
#define M68K_ASL_L(n, dn) ((uint16_t)(0xE180 | (((n) & 7) << 9) | (dn)))

/* ASR.L #n, Dn */
#define M68K_ASR_L(n, dn) ((uint16_t)(0xE080 | (((n) & 7) << 9) | (dn)))

/* ROL.L #n, Dn */
#define M68K_ROL_L(n, dn) ((uint16_t)(0xE198 | (((n) & 7) << 9) | (dn)))

/* ROR.L #n, Dn */
#define M68K_ROR_L(n, dn) ((uint16_t)(0xE098 | (((n) & 7) << 9) | (dn)))

/* BRA.S offset (8-bit signed displacement from PC+2) */
#define M68K_BRA_S(off8) ((uint16_t)(0x6000 | ((off8) & 0xFF)))

/* BEQ.S offset */
#define M68K_BEQ_S(off8) ((uint16_t)(0x6700 | ((off8) & 0xFF)))

/* BNE.S offset */
#define M68K_BNE_S(off8) ((uint16_t)(0x6600 | ((off8) & 0xFF)))

/* BSR.S offset */
#define M68K_BSR_S(off8) ((uint16_t)(0x6100 | ((off8) & 0xFF)))

/* NOP */
#define M68K_NOP 0x4E71

/* RTS */
#define M68K_RTS 0x4E75

/* MOVE.L Dn, Dm */
#define M68K_MOVE_L_D_D(src, dst) ((uint16_t)(0x2000 | ((dst) << 9) | (src)))

/* MOVE.L (An), Dn */
#define M68K_MOVE_L_IND_D(an, dn) ((uint16_t)(0x2010 | ((dn) << 9) | (an)))

/* MOVE.L Dn, (An) */
#define M68K_MOVE_L_D_IND(dn, an) ((uint16_t)(0x2080 | ((an) << 9) | (dn)))

/* MOVEA.L Dn, An */
#define M68K_MOVEA_L_D_A(dn, an) ((uint16_t)(0x2040 | ((an) << 9) | (dn)))

/* BTST #bit, Dn (2 words: opcode + bit number) */
#define M68K_BTST_IMM(dn) ((uint16_t)(0x0800 | (dn)))

/* BSET #bit, Dn */
#define M68K_BSET_IMM(dn) ((uint16_t)(0x08C0 | (dn)))

/* BCLR #bit, Dn */
#define M68K_BCLR_IMM(dn) ((uint16_t)(0x0880 | (dn)))

/* LEA abs.L, An: 0100_AAA_111_111_001 + 32-bit address */
#define M68K_LEA_ABS_L(an) ((uint16_t)(0x41F9 | ((an) << 9)))

/* ---- libretro function pointers ---- */
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

/* 68K interface */
static int (*p_m68k_execute)(int);
static void (*p_m68k_set_reg)(int, unsigned int);
static unsigned int (*p_m68k_get_reg)(void *, int);
static uint8_t **p_jaguarMainRAM;

/* Stubs */
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
   case RETRO_ENVIRONMENT_GET_VARIABLE: {
      struct retro_variable *var = (struct retro_variable *)data;
      if (var->key && strcmp(var->key, "virtualjaguar_bios") == 0)
         { var->value = "disabled"; return true; }
      if (var->key && strcmp(var->key, "virtualjaguar_usefastblitter") == 0)
         { var->value = "enabled"; return true; }
      var->value = NULL;
      return false;
   }
   default: return false;
   }
}

static int passes = 0, fails = 0;
#define PASS(msg, ...) do { printf("  PASS: " msg "\n", ##__VA_ARGS__); passes++; } while(0)
#define FAIL(msg, ...) do { printf("  FAIL: " msg "\n", ##__VA_ARGS__); fails++; } while(0)

/* ---- RAM helpers (big-endian) ---- */
#define CODE_BASE 0x4000
#define STACK_TOP 0x8000

static void w16(uint32_t addr, uint16_t v) {
   uint8_t *ram = *p_jaguarMainRAM;
   ram[addr] = (v >> 8) & 0xFF;
   ram[addr + 1] = v & 0xFF;
}

static void w32(uint32_t addr, uint32_t v) {
   uint8_t *ram = *p_jaguarMainRAM;
   ram[addr] = (v >> 24) & 0xFF;
   ram[addr + 1] = (v >> 16) & 0xFF;
   ram[addr + 2] = (v >> 8) & 0xFF;
   ram[addr + 3] = v & 0xFF;
}

static uint32_t r32(uint32_t addr) {
   uint8_t *ram = *p_jaguarMainRAM;
   return ((uint32_t)ram[addr] << 24) | ((uint32_t)ram[addr+1] << 16) |
          ((uint32_t)ram[addr+2] << 8) | ram[addr+3];
}

/* Write MOVE.L #imm32, Dn at addr, returns next free address */
static uint32_t wmoveil(uint32_t addr, uint32_t imm, int dn) {
   w16(addr, 0x203C | (dn << 9));
   w16(addr + 2, (imm >> 16) & 0xFFFF);
   w16(addr + 4, imm & 0xFFFF);
   return addr + 6;
}

static void prep(void) {
   uint32_t i;
   for (i = CODE_BASE; i < CODE_BASE + 0x400; i += 2)
      w16(i, M68K_NOP);
   /* Clear data registers */
   for (i = M68K_REG_D0; i <= M68K_REG_D7; i++)
      p_m68k_set_reg(i, 0);
   for (i = M68K_REG_A0; i <= M68K_REG_A6; i++)
      p_m68k_set_reg(i, 0);
}

static void run(int cycles) {
   p_m68k_set_reg(M68K_REG_PC, CODE_BASE);
   p_m68k_set_reg(M68K_REG_SR, 0x2700);
   p_m68k_set_reg(M68K_REG_SP, STACK_TOP);
   p_m68k_execute(cycles);
}

static uint32_t D(int n) { return p_m68k_get_reg(NULL, M68K_REG_D0 + n); }
static uint32_t A(int n) { return p_m68k_get_reg(NULL, M68K_REG_A0 + n); }
static uint32_t PC(void) { return p_m68k_get_reg(NULL, M68K_REG_PC); }
static uint32_t SR(void) { return p_m68k_get_reg(NULL, M68K_REG_SR); }

/* ============================================================ */

static void test_moveq(void)
{
   printf("\n--- MOVEQ ---\n");
   prep();
   w16(CODE_BASE, M68K_MOVEQ(42, 0));
   w16(CODE_BASE + 2, M68K_MOVEQ(-1, 1));
   run(20);
   if (D(0) == 42) PASS("moveq #42, D0");
   else FAIL("moveq #42: D0=%u", D(0));
   if (D(1) == 0xFFFFFFFF) PASS("moveq #-1, D1 (sign-extended)");
   else FAIL("moveq #-1: D1=%08X", D(1));
}

static void test_move_l(void)
{
   printf("\n--- MOVE.L #imm ---\n");
   prep();
   wmoveil(CODE_BASE, 0xDEADBEEF, 0);
   run(20);
   if (D(0) == 0xDEADBEEF) PASS("move.l #$DEADBEEF, D0");
   else FAIL("move.l: D0=%08X", D(0));
}

static void test_move_d_d(void)
{
   printf("\n--- MOVE.L Dn, Dm ---\n");
   prep();
   w16(CODE_BASE, M68K_MOVEQ(77, 0));
   w16(CODE_BASE + 2, M68K_MOVE_L_D_D(0, 1));
   run(20);
   if (D(1) == 77) PASS("move.l D0, D1 = 77");
   else FAIL("move.l D0,D1: D1=%u", D(1));
}

static void test_add(void)
{
   printf("\n--- ADD.L ---\n");
   prep();
   w16(CODE_BASE, M68K_MOVEQ(10, 0));
   w16(CODE_BASE + 2, M68K_MOVEQ(20, 1));
   w16(CODE_BASE + 4, M68K_ADD_L(0, 1));
   run(30);
   if (D(1) == 30) PASS("10+20=30");
   else FAIL("10+20=%u", D(1));
}

static void test_addq(void)
{
   printf("\n--- ADDQ.L ---\n");
   prep();
   w16(CODE_BASE, M68K_MOVEQ(10, 0));
   w16(CODE_BASE + 2, M68K_ADDQ_L(5, 0));
   run(20);
   if (D(0) == 15) PASS("addq #5, D0(10) -> 15");
   else FAIL("addq -> %u", D(0));

   prep();
   w16(CODE_BASE, M68K_MOVEQ(0, 0));
   w16(CODE_BASE + 2, M68K_ADDQ_L(8, 0));
   run(20);
   if (D(0) == 8) PASS("addq #8, D0(0) -> 8");
   else FAIL("addq #8 -> %u", D(0));
}

static void test_sub(void)
{
   printf("\n--- SUB.L ---\n");
   prep();
   w16(CODE_BASE, M68K_MOVEQ(5, 0));
   w16(CODE_BASE + 2, M68K_MOVEQ(20, 1));
   w16(CODE_BASE + 4, M68K_SUB_L(0, 1));
   run(30);
   if (D(1) == 15) PASS("20-5=15");
   else FAIL("20-5=%u", D(1));
}

static void test_subq(void)
{
   printf("\n--- SUBQ.L ---\n");
   prep();
   w16(CODE_BASE, M68K_MOVEQ(20, 0));
   w16(CODE_BASE + 2, M68K_SUBQ_L(5, 0));
   run(20);
   if (D(0) == 15) PASS("subq #5, D0(20) -> 15");
   else FAIL("subq -> %u", D(0));
}

static void test_neg(void)
{
   printf("\n--- NEG.L ---\n");
   prep();
   w16(CODE_BASE, M68K_MOVEQ(42, 0));
   w16(CODE_BASE + 2, M68K_NEG_L(0));
   run(20);
   if (D(0) == (uint32_t)-42) PASS("neg 42 = -42");
   else FAIL("neg 42 = %08X", D(0));
}

static void test_and(void)
{
   uint32_t a;
   printf("\n--- AND.L ---\n");
   prep();
   a = CODE_BASE;
   a = wmoveil(a, 0xFF00FF00, 0);
   a = wmoveil(a, 0x0F0F0F0F, 1);
   w16(a, M68K_AND_L(0, 1));
   run(40);
   if (D(1) == 0x0F000F00) PASS("AND = 0x0F000F00");
   else FAIL("AND = %08X", D(1));
}

static void test_or(void)
{
   uint32_t a;
   printf("\n--- OR.L ---\n");
   prep();
   a = CODE_BASE;
   a = wmoveil(a, 0xF0F00000, 0);
   a = wmoveil(a, 0x0F0F0000, 1);
   w16(a, M68K_OR_L(0, 1));
   run(40);
   if (D(1) == 0xFFFF0000) PASS("OR = 0xFFFF0000");
   else FAIL("OR = %08X", D(1));
}

static void test_eor(void)
{
   uint32_t a;
   printf("\n--- EOR.L ---\n");
   prep();
   a = CODE_BASE;
   a = wmoveil(a, 0xFFFF0000, 0);
   a = wmoveil(a, 0xFF00FF00, 1);
   w16(a, M68K_EOR_L(0, 1));
   run(40);
   if (D(1) == 0x00FFFF00) PASS("EOR = 0x00FFFF00");
   else FAIL("EOR = %08X", D(1));
}

static void test_not(void)
{
   uint32_t a;
   printf("\n--- NOT.L ---\n");
   prep();
   a = CODE_BASE;
   a = wmoveil(a, 0xAAAA5555, 0);
   w16(a, M68K_NOT_L(0));
   run(20);
   if (D(0) == 0x5555AAAA) PASS("NOT = 0x5555AAAA");
   else FAIL("NOT = %08X", D(0));
}

static void test_clr(void)
{
   printf("\n--- CLR.L ---\n");
   prep();
   w16(CODE_BASE, M68K_MOVEQ(77, 0));
   w16(CODE_BASE + 2, M68K_CLR_L(0));
   run(20);
   if (D(0) == 0) PASS("clr.l D0 -> 0");
   else FAIL("clr: D0=%u", D(0));
}

static void test_swap(void)
{
   uint32_t a;
   printf("\n--- SWAP ---\n");
   prep();
   a = CODE_BASE;
   a = wmoveil(a, 0x12345678, 0);
   w16(a, M68K_SWAP(0));
   run(20);
   if (D(0) == 0x56781234) PASS("swap 0x12345678 -> 0x56781234");
   else FAIL("swap = %08X", D(0));
}

static void test_ext(void)
{
   uint32_t a;
   printf("\n--- EXT ---\n");
   prep();
   /* EXT.W: sign-extend byte to word. Load D1 with 0x00000080. */
   a = CODE_BASE;
   a = wmoveil(a, 0x00000080, 1);
   w16(a, M68K_EXT_W(1)); a += 2;
   /* EXT.L: sign-extend word to long. Load D2 with 0x0000FF80. */
   a = wmoveil(a, 0x0000FF80, 2);
   w16(a, M68K_EXT_L(2));
   run(40);
   if ((D(1) & 0xFFFF) == 0xFF80) PASS("ext.w 0x80 -> 0xFF80");
   else FAIL("ext.w: D1=%08X", D(1));
   if (D(2) == 0xFFFFFF80) PASS("ext.l 0xFF80 -> 0xFFFFFF80");
   else FAIL("ext.l: D2=%08X", D(2));
}

static void test_mulu(void)
{
   printf("\n--- MULU ---\n");
   prep();
   w16(CODE_BASE, M68K_MOVEQ(7, 0));
   w16(CODE_BASE + 2, M68K_MOVEQ(6, 1));
   w16(CODE_BASE + 4, M68K_MULU(0, 1));
   run(80);
   if (D(1) == 42) PASS("mulu 7*6=42");
   else FAIL("mulu 7*6=%u", D(1));
}

static void test_muls(void)
{
   printf("\n--- MULS ---\n");
   prep();
   w16(CODE_BASE, M68K_MOVEQ(-7, 0));
   w16(CODE_BASE + 2, M68K_MOVEQ(6, 1));
   w16(CODE_BASE + 4, M68K_MULS(0, 1));
   run(80);
   if (D(1) == (uint32_t)-42) PASS("muls -7*6=-42");
   else FAIL("muls -7*6=%d", (int32_t)D(1));
}

static void test_divu(void)
{
   printf("\n--- DIVU ---\n");
   prep();
   /* DIVU: D1(long) / D0(word) -> D1 low word = quotient, high word = remainder */
   w16(CODE_BASE, M68K_MOVEQ(7, 0));
   uint32_t a = wmoveil(CODE_BASE + 2, 100, 1);
   w16(a, M68K_DIVU(0, 1));
   run(160);
   if ((D(1) & 0xFFFF) == 14) PASS("divu 100/7 quotient=14");
   else FAIL("divu 100/7: D1=%08X (quotient=%u)", D(1), D(1) & 0xFFFF);
   if ((D(1) >> 16) == 2) PASS("divu 100/7 remainder=2");
   else FAIL("divu remainder=%u", D(1) >> 16);
}

static void test_divs(void)
{
   printf("\n--- DIVS ---\n");
   prep();
   w16(CODE_BASE, M68K_MOVEQ(7, 0));
   uint32_t a = wmoveil(CODE_BASE + 2, (uint32_t)-100, 1);
   w16(a, M68K_DIVS(0, 1));
   run(160);
   if ((int16_t)(D(1) & 0xFFFF) == -14) PASS("divs -100/7 quotient=-14");
   else FAIL("divs: D1=%08X (quotient=%d)", D(1), (int16_t)(D(1) & 0xFFFF));
}

static void test_lsl(void)
{
   printf("\n--- LSL.L ---\n");
   prep();
   w16(CODE_BASE, M68K_MOVEQ(1, 0));
   w16(CODE_BASE + 2, M68K_LSL_L(4, 0));
   run(20);
   if (D(0) == 16) PASS("lsl #4, 1 -> 16");
   else FAIL("lsl -> %u", D(0));
}

static void test_lsr(void)
{
   uint32_t a;
   printf("\n--- LSR.L ---\n");
   prep();
   a = CODE_BASE;
   a = wmoveil(a, 0x80, 0);
   w16(a, M68K_LSR_L(3, 0));
   run(20);
   if (D(0) == 0x10) PASS("lsr #3, 0x80 -> 0x10");
   else FAIL("lsr -> %08X", D(0));
}

static void test_asr(void)
{
   printf("\n--- ASR.L ---\n");
   prep();
   uint32_t a = wmoveil(CODE_BASE, (uint32_t)-16, 0);
   w16(a, M68K_ASR_L(2, 0));
   run(20);
   if (D(0) == (uint32_t)-4) PASS("asr #2, -16 -> -4");
   else FAIL("asr -> %08X", D(0));
}

static void test_rol(void)
{
   uint32_t a;
   printf("\n--- ROL.L ---\n");
   prep();
   a = wmoveil(CODE_BASE, 0x80000001, 0);
   w16(a, M68K_ROL_L(4, 0));
   run(20);
   if (D(0) == 0x00000018) PASS("rol #4, 0x80000001 -> 0x00000018");
   else FAIL("rol -> %08X", D(0));
}

static void test_ror(void)
{
   uint32_t a;
   printf("\n--- ROR.L ---\n");
   prep();
   a = wmoveil(CODE_BASE, 0x12345678, 0);
   w16(a, M68K_ROR_L(4, 0));
   run(20);
   if (D(0) == 0x81234567) PASS("ror #4, 0x12345678 -> 0x81234567");
   else FAIL("ror -> %08X", D(0));
}

static void test_btst(void)
{
   printf("\n--- BTST ---\n");
   prep();
   w16(CODE_BASE, M68K_MOVEQ(8, 0));     /* D0 = 8 (bit 3 set) */
   w16(CODE_BASE + 2, M68K_BTST_IMM(0)); /* btst #3, D0 */
   w16(CODE_BASE + 4, 3);                 /* bit number */
   run(30);
   /* Z flag should be clear (bit is set). Check SR bit 2 (Z). */
   if (!(SR() & 0x04)) PASS("btst #3, 8: Z=0 (bit set)");
   else FAIL("btst: SR=%04X (expected Z=0)", SR());
}

static void test_bset(void)
{
   printf("\n--- BSET ---\n");
   prep();
   w16(CODE_BASE, M68K_MOVEQ(0, 0));
   w16(CODE_BASE + 2, M68K_BSET_IMM(0)); /* bset #5, D0 */
   w16(CODE_BASE + 4, 5);
   run(30);
   if (D(0) == 32) PASS("bset #5, D0(0) -> 32");
   else FAIL("bset -> %u", D(0));
}

static void test_bclr(void)
{
   printf("\n--- BCLR ---\n");
   prep();
   w16(CODE_BASE, M68K_MOVEQ(0x7F, 0));  /* D0 = 0xFF (sign-extended? no, 0x7F) */
   w16(CODE_BASE + 2, M68K_BCLR_IMM(0)); /* bclr #3, D0 */
   w16(CODE_BASE + 4, 3);
   run(30);
   if (D(0) == 0x77) PASS("bclr #3, 0x7F -> 0x77");
   else FAIL("bclr -> %02X", D(0));
}

static void test_cmp(void)
{
   printf("\n--- CMP.L ---\n");
   prep();
   w16(CODE_BASE, M68K_MOVEQ(10, 0));
   w16(CODE_BASE + 2, M68K_MOVEQ(10, 1));
   w16(CODE_BASE + 4, M68K_CMP_L(0, 1));
   run(30);
   if (D(0) == 10 && D(1) == 10) PASS("cmp leaves operands unchanged");
   else FAIL("cmp: D0=%u D1=%u", D(0), D(1));
   if (SR() & 0x04) PASS("cmp equal: Z=1");
   else FAIL("cmp equal: SR=%04X (expected Z=1)", SR());
}

static void test_tst(void)
{
   printf("\n--- TST.L ---\n");
   prep();
   w16(CODE_BASE, M68K_MOVEQ(0, 0));
   w16(CODE_BASE + 2, M68K_TST_L(0));
   run(20);
   if (SR() & 0x04) PASS("tst 0: Z=1");
   else FAIL("tst 0: SR=%04X", SR());
}

static void test_bra(void)
{
   printf("\n--- BRA.S ---\n");
   prep();
   /* BRA.S +4 skips 2 instructions (each 2 bytes) */
   w16(CODE_BASE, M68K_BRA_S(4));
   w16(CODE_BASE + 2, M68K_MOVEQ(7, 0));  /* skipped */
   w16(CODE_BASE + 4, M68K_MOVEQ(7, 0));  /* skipped */
   w16(CODE_BASE + 6, M68K_MOVEQ(25, 0)); /* target */
   run(30);
   if (D(0) == 25) PASS("bra.s skipped to moveq #25");
   else FAIL("bra.s: D0=%u", D(0));
}

static void test_beq(void)
{
   printf("\n--- BEQ.S ---\n");
   prep();
   /* Set Z flag via cmp equal, then beq */
   w16(CODE_BASE, M68K_MOVEQ(0, 0));
   w16(CODE_BASE + 2, M68K_TST_L(0));      /* Z=1 */
   w16(CODE_BASE + 4, M68K_BEQ_S(2));       /* branch +2 */
   w16(CODE_BASE + 6, M68K_MOVEQ(7, 1));    /* skipped */
   w16(CODE_BASE + 8, M68K_MOVEQ(25, 1));   /* target */
   run(40);
   if (D(1) == 25) PASS("beq taken: D1=25");
   else FAIL("beq: D1=%u", D(1));
}

static void test_bne(void)
{
   printf("\n--- BNE.S ---\n");
   prep();
   w16(CODE_BASE, M68K_MOVEQ(5, 0));
   w16(CODE_BASE + 2, M68K_TST_L(0));      /* Z=0 (non-zero) */
   w16(CODE_BASE + 4, M68K_BNE_S(2));       /* branch +2 */
   w16(CODE_BASE + 6, M68K_MOVEQ(7, 1));    /* skipped */
   w16(CODE_BASE + 8, M68K_MOVEQ(25, 1));   /* target */
   run(40);
   if (D(1) == 25) PASS("bne taken: D1=25");
   else FAIL("bne: D1=%u", D(1));
}

static void test_bsr_rts(void)
{
   printf("\n--- BSR/RTS ---\n");
   prep();
   /* BSR to subroutine that sets D0=42 and returns */
   w16(CODE_BASE, M68K_BSR_S(4));            /* bsr.s +4 (to CODE_BASE+6) */
   w16(CODE_BASE + 2, M68K_MOVEQ(0, 1));     /* D1=0 marker (executed after return) */
   w16(CODE_BASE + 4, M68K_NOP);             /* padding */
   w16(CODE_BASE + 6, M68K_MOVEQ(42, 0));    /* subroutine: D0=42 */
   w16(CODE_BASE + 8, M68K_RTS);             /* return */
   run(60);
   if (D(0) == 42) PASS("bsr called subroutine: D0=42");
   else FAIL("bsr: D0=%u", D(0));
   if (D(1) == 0) PASS("rts returned to caller");
   else FAIL("rts: D1=%u (expected 0)", D(1));
}

static void test_lea(void)
{
   printf("\n--- LEA ---\n");
   prep();
   /* LEA $00012345, A0 */
   w16(CODE_BASE, M68K_LEA_ABS_L(0));
   w32(CODE_BASE + 2, 0x00012345);
   run(20);
   if (A(0) == 0x00012345) PASS("lea $00012345, A0");
   else FAIL("lea: A0=%08X", A(0));
}

static void test_movea(void)
{
   printf("\n--- MOVEA.L ---\n");
   prep();
   uint32_t a = wmoveil(CODE_BASE, 0x00100000, 0);
   w16(a, M68K_MOVEA_L_D_A(0, 0));
   run(20);
   if (A(0) == 0x00100000) PASS("movea.l D0, A0 = $100000");
   else FAIL("movea: A0=%08X", A(0));
}

static void test_load_store_indirect(void)
{
   uint32_t a;
   printf("\n--- MOVE.L (An) / MOVE.L Dn,(An) ---\n");
   prep();
   /* Store 0xCAFEBABE to address $2000, then load it back */
   a = CODE_BASE;
   a = wmoveil(a, 0x00002000, 0); /* D0 = target address */
   w16(a, M68K_MOVEA_L_D_A(0, 0)); a += 2; /* A0 = $2000 */
   a = wmoveil(a, 0xCAFEBABE, 1); /* D1 = value */
   w16(a, M68K_MOVE_L_D_IND(1, 0)); a += 2; /* (A0) = D1 */
   w16(a, M68K_CLR_L(1)); a += 2;            /* D1 = 0 */
   w16(a, M68K_MOVE_L_IND_D(0, 1)); a += 2;  /* D1 = (A0) */
   run(80);
   if (D(1) == 0xCAFEBABE) PASS("store+load indirect: 0xCAFEBABE");
   else FAIL("store+load: D1=%08X", D(1));
}

static void test_nop(void)
{
   printf("\n--- NOP ---\n");
   prep();
   w16(CODE_BASE, M68K_MOVEQ(5, 0));
   w16(CODE_BASE + 2, M68K_NOP);
   w16(CODE_BASE + 4, M68K_NOP);
   run(30);
   if (D(0) == 5) PASS("NOPs don't modify registers");
   else FAIL("NOP: D0=%u", D(0));
}

/* ============================================================ */

int main(int argc, char *argv[])
{
   void *handle;
   uint8_t *dummy_rom;
   struct retro_game_info game;
   (void)argc; (void)argv;

   printf("=== 68K Instruction Set Tests ===\n");

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
   LOAD(retro_unload_game);
   LOAD(m68k_execute);
   LOAD(m68k_set_reg);
   LOAD(m68k_get_reg);

   p_jaguarMainRAM = dlsym(handle, "jaguarMainRAM");
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

   dummy_rom = calloc(1, 131072);
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

   /* Data movement */
   test_moveq();
   test_move_l();
   test_move_d_d();
   test_movea();
   test_lea();

   /* Arithmetic */
   test_add();
   test_addq();
   test_sub();
   test_subq();
   test_neg();

   /* Logic */
   test_and();
   test_or();
   test_eor();
   test_not();
   test_clr();

   /* Misc */
   test_swap();
   test_ext();
   test_nop();

   /* Multiply / Divide */
   test_mulu();
   test_muls();
   test_divu();
   test_divs();

   /* Shifts / Rotates */
   test_lsl();
   test_lsr();
   test_asr();
   test_rol();
   test_ror();

   /* Bit operations */
   test_btst();
   test_bset();
   test_bclr();

   /* Compare / Test */
   test_cmp();
   test_tst();

   /* Control flow */
   test_bra();
   test_beq();
   test_bne();
   test_bsr_rts();

   /* Memory indirect */
   test_load_store_indirect();

   printf("\n=== Results: %d passed, %d failed ===\n", passes, fails);

   p_retro_unload_game();
   p_retro_deinit();
   dlclose(handle);
   free(dummy_rom);
   return fails > 0 ? 1 : 0;
}
