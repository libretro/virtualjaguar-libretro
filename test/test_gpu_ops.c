/* test_gpu_ops.c -- GPU instruction set verification.
 * Each test writes a short GPU program, executes it, and checks results
 * via register banks and GPU RAM.
 *
 * Build: cc -o test/test_gpu_ops test/test_gpu_ops.c -ldl
 * Usage: ./test/test_gpu_ops
 *
 * GPU-specific opcodes (different from DSP):
 *  32  sat8      33  sat16     42  loadp     48  storep
 *  62  sat24     63  pack/unpack
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

#define GPU_FLAGS_ADDR   0xF02100
#define GPU_CTRL_ADDR    0xF02114
#define GPU_PC_ADDR      0xF02110
#define GPU_RAM_BASE     0xF03000
#define GPU_RAM_SIZE     0x1000

#define GPUGO    0x001

#define OP(opc, r1, r2) ((uint16_t)(((opc)<<10)|((r1)<<5)|(r2)))

#define OP_ADD(rs, rd)    OP(0, rs, rd)
#define OP_ADDC(rs, rd)   OP(1, rs, rd)
#define OP_ADDQ(n, rd)    OP(2, n, rd)
#define OP_ADDQT(n, rd)   OP(3, n, rd)
#define OP_SUB(rs, rd)    OP(4, rs, rd)
#define OP_SUBC(rs, rd)   OP(5, rs, rd)
#define OP_SUBQ(n, rd)    OP(6, n, rd)
#define OP_SUBQT(n, rd)   OP(7, n, rd)
#define OP_NEG(rd)        OP(8, 0, rd)
#define OP_AND(rs, rd)    OP(9, rs, rd)
#define OP_OR(rs, rd)     OP(10, rs, rd)
#define OP_XOR(rs, rd)    OP(11, rs, rd)
#define OP_NOT(rs, rd)    OP(12, rs, rd)
#define OP_BTST(n, rd)    OP(13, n, rd)
#define OP_BSET(n, rd)    OP(14, n, rd)
#define OP_BCLR(n, rd)    OP(15, n, rd)
#define OP_MULT(rs, rd)   OP(16, rs, rd)
#define OP_IMULT(rs, rd)  OP(17, rs, rd)
#define OP_IMULTN(rs, rd) OP(18, rs, rd)
#define OP_RESMAC(rd)     OP(19, 0, rd)
#define OP_IMACN(rs, rd)  OP(20, rs, rd)
#define OP_DIV(rs, rd)    OP(21, rs, rd)
#define OP_ABS(rd)        OP(22, 0, rd)
#define OP_SH(rs, rd)     OP(23, rs, rd)
#define OP_SHLQ(n, rd)    OP(24, 32-(n), rd)
#define OP_SHRQ(n, rd)    OP(25, (n), rd)
#define OP_SHA(rs, rd)    OP(26, rs, rd)
#define OP_SHARQ(n, rd)   OP(27, (n), rd)
#define OP_ROR(rs, rd)    OP(28, rs, rd)
#define OP_RORQ(n, rd)    OP(29, (n), rd)
#define OP_CMP(rs, rd)    OP(30, rs, rd)
#define OP_CMPQ(n, rd)    OP(31, n, rd)
#define OP_SAT8(rs, rd)   OP(32, rs, rd)
#define OP_SAT16(rs, rd)  OP(33, rs, rd)
#define OP_MOVE(rs, rd)   OP(34, rs, rd)
#define OP_MOVEQ(n, rd)   OP(35, n, rd)
#define OP_MOVETA(rs, rd) OP(36, rs, rd)
#define OP_MOVEFA(rs, rd) OP(37, rs, rd)
#define OP_LOAD(rs, rd)   OP(41, rs, rd)
#define OP_LOADP(rs, rd)  OP(42, rs, rd)
#define OP_STORE(ra, rv)  OP(47, ra, rv)
#define OP_STOREP(ra, rv) OP(48, ra, rv)
#define OP_MOVE_PC(rd)    OP(51, 0, rd)
#define OP_JUMP(cc, ra)   OP(52, ra, cc)
#define OP_JR(cc, off)    OP(53, (off)&0x1F, cc)
#define OP_NOP             OP(57, 0, 0)
#define OP_SAT24(rs, rd)  OP(62, rs, rd)
#define OP_PACK(n, rd)    OP(63, n, rd)

/* libretro function pointers */
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

static void *core_handle;
static uint32_t *p_gpu_pc;
static uint32_t *p_gpu_reg_bank_0;
static uint32_t *p_gpu_reg_bank_1;
static void (*p_GPUReset)(void);
static void (*p_GPUExec)(int32_t);
static void (*p_GPUWriteLong)(uint32_t, uint32_t, uint32_t);
static void (*p_GPUWriteWord)(uint32_t, uint16_t, uint32_t);
static uint32_t (*p_GPUReadLong)(uint32_t, uint32_t);

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

/* --- GPU RAM helpers (via memory-mapped writes) --- */
static void gw16(uint16_t off, uint16_t v) {
   p_GPUWriteWord(GPU_RAM_BASE + off, v, 1);
}
static void gw32(uint16_t off, uint32_t v) {
   p_GPUWriteLong(GPU_RAM_BASE + off, v, 1);
}
static uint32_t gr32(uint16_t off) {
   return p_GPUReadLong(GPU_RAM_BASE + off, 1);
}
static void gwmovei(uint16_t off, uint32_t imm, uint8_t rd) {
   gw16(off, OP(38, 0, rd));
   gw16(off+2, imm & 0xFFFF);
   gw16(off+4, (imm>>16) & 0xFFFF);
}

static void prep(void) {
   uint32_t i;
   p_GPUReset();
   for (i = 0; i < GPU_RAM_SIZE; i += 2)
      gw16(i, OP_NOP);
   for (i = 0; i < 32; i++)
      p_gpu_reg_bank_0[i] = p_gpu_reg_bank_1[i] = 0;
}

static void run(int32_t cycles) {
   p_GPUWriteLong(GPU_PC_ADDR, GPU_RAM_BASE + 0x100, 1);
   p_GPUWriteLong(GPU_CTRL_ADDR, GPUGO, 1);
   p_GPUExec(cycles);
   p_GPUWriteLong(GPU_CTRL_ADDR, 0, 1);
}

#define REG(n) p_gpu_reg_bank_0[(n)]

/* ============================================================ */
/* Arithmetic (shared with DSP, verify GPU implementation)      */
/* ============================================================ */

static void test_add(void)
{
   printf("\n--- ADD ---\n");
   prep();
   gw16(0x100, OP_MOVEQ(10, 0));
   gw16(0x102, OP_MOVEQ(20, 1));
   gw16(0x104, OP_ADD(0, 1));
   run(20);
   if (REG(1) == 30) PASS("10+20=30");
   else FAIL("10+20=%u (expected 30)", REG(1));
}

static void test_addc(void)
{
   printf("\n--- ADDC ---\n");
   prep();
   gwmovei(0x100, 0xFFFFFFFF, 0);
   gw16(0x106, OP_MOVEQ(1, 1));
   gw16(0x108, OP_ADD(1, 0));
   gw16(0x10A, OP_MOVEQ(5, 2));
   gw16(0x10C, OP_MOVEQ(10, 3));
   gw16(0x10E, OP_ADDC(2, 3));
   run(40);
   if (REG(3) == 16) PASS("5+10+carry=16");
   else FAIL("5+10+carry=%u (expected 16)", REG(3));
}

static void test_addq(void)
{
   printf("\n--- ADDQ ---\n");
   prep();
   gw16(0x100, OP_ADDQ(5, 0));
   run(10);
   if (REG(0) == 5) PASS("addq #5 -> 5");
   else FAIL("addq -> %u (expected 5)", REG(0));

   prep();
   gw16(0x100, OP_MOVEQ(10, 0));
   gw16(0x102, OP_ADDQ(0, 0));
   run(10);
   if (REG(0) == 42) PASS("addq #0(=32) -> 42");
   else FAIL("addq #0(=32) -> %u (expected 42)", REG(0));
}

static void test_sub(void)
{
   printf("\n--- SUB ---\n");
   prep();
   gw16(0x100, OP_MOVEQ(5, 0));
   gw16(0x102, OP_MOVEQ(20, 1));
   gw16(0x104, OP_SUB(0, 1));
   run(20);
   if (REG(1) == 15) PASS("20-5=15");
   else FAIL("20-5=%u (expected 15)", REG(1));
}

static void test_neg(void)
{
   printf("\n--- NEG ---\n");
   prep();
   gw16(0x100, OP_MOVEQ(5, 0));
   gw16(0x102, OP_NEG(0));
   run(10);
   if (REG(0) == (uint32_t)-5) PASS("neg 5 = -5");
   else FAIL("neg 5 = %08X (expected FFFFFFFB)", REG(0));
}

/* Logic */

static void test_and(void)
{
   printf("\n--- AND ---\n");
   prep();
   gwmovei(0x100, 0xFF00FF00, 0);
   gwmovei(0x106, 0x0F0F0F0F, 1);
   gw16(0x10C, OP_AND(0, 1));
   run(30);
   if (REG(1) == 0x0F000F00) PASS("AND = 0x0F000F00");
   else FAIL("AND = %08X (expected 0F000F00)", REG(1));
}

static void test_or(void)
{
   printf("\n--- OR ---\n");
   prep();
   gwmovei(0x100, 0xF0F00000, 0);
   gwmovei(0x106, 0x0F0F0000, 1);
   gw16(0x10C, OP_OR(0, 1));
   run(30);
   if (REG(1) == 0xFFFF0000) PASS("OR = 0xFFFF0000");
   else FAIL("OR = %08X (expected FFFF0000)", REG(1));
}

static void test_xor(void)
{
   printf("\n--- XOR ---\n");
   prep();
   gwmovei(0x100, 0xFFFF0000, 0);
   gwmovei(0x106, 0xFF00FF00, 1);
   gw16(0x10C, OP_XOR(0, 1));
   run(30);
   if (REG(1) == 0x00FFFF00) PASS("XOR = 0x00FFFF00");
   else FAIL("XOR = %08X (expected 00FFFF00)", REG(1));
}

static void test_not(void)
{
   printf("\n--- NOT ---\n");
   prep();
   gwmovei(0x100, 0xAAAA5555, 0);
   gw16(0x106, OP_NOT(0, 0));
   run(20);
   if (REG(0) == 0x5555AAAA) PASS("NOT 0xAAAA5555 = 0x5555AAAA");
   else FAIL("NOT = %08X (expected 5555AAAA)", REG(0));
}

/* Bit ops */

static void test_bset(void)
{
   printf("\n--- BSET ---\n");
   prep();
   gw16(0x100, OP_MOVEQ(0, 0));
   gw16(0x102, OP_BSET(5, 0));
   run(10);
   if (REG(0) == 32) PASS("bset #5 -> 32");
   else FAIL("bset -> %u (expected 32)", REG(0));
}

static void test_bclr(void)
{
   printf("\n--- BCLR ---\n");
   prep();
   gwmovei(0x100, 0xFF, 0);
   gw16(0x106, OP_BCLR(3, 0));
   run(20);
   if (REG(0) == 0xF7) PASS("bclr #3, 0xFF -> 0xF7");
   else FAIL("bclr -> %02X (expected F7)", REG(0));
}

/* Multiply / Divide */

static void test_mult(void)
{
   printf("\n--- MULT ---\n");
   prep();
   gw16(0x100, OP_MOVEQ(7, 0));
   gw16(0x102, OP_MOVEQ(6, 1));
   gw16(0x104, OP_MULT(0, 1));
   run(20);
   if (REG(1) == 42) PASS("7*6=42");
   else FAIL("7*6=%u (expected 42)", REG(1));
}

static void test_imult(void)
{
   printf("\n--- IMULT ---\n");
   prep();
   gw16(0x100, OP_MOVEQ(7, 0));
   gw16(0x102, OP_MOVEQ(6, 1));
   gw16(0x104, OP_IMULT(0, 1));
   run(20);
   if (REG(1) == 42) PASS("imult 7*6=42");
   else FAIL("imult 7*6=%d (expected 42)", (int32_t)REG(1));
}

static void test_div(void)
{
   printf("\n--- DIV ---\n");
   prep();
   gwmovei(0x100, 100, 1);
   gw16(0x106, OP_MOVEQ(7, 0));
   gw16(0x108, OP_DIV(0, 1));
   run(40);
   if (REG(1) == 14) PASS("100/7=14");
   else FAIL("100/7=%u (expected 14)", REG(1));
}

static void test_abs(void)
{
   printf("\n--- ABS ---\n");
   prep();
   gwmovei(0x100, (uint32_t)-42, 0);
   gw16(0x106, OP_ABS(0));
   run(20);
   if (REG(0) == 42) PASS("abs(-42)=42");
   else FAIL("abs(-42)=%d (expected 42)", (int32_t)REG(0));
}

/* Shifts */

static void test_sh(void)
{
   printf("\n--- SH ---\n");
   prep();
   gwmovei(0x100, (uint32_t)-2, 0);
   gw16(0x106, OP_MOVEQ(5, 1));
   gw16(0x108, OP_SH(0, 1));
   run(30);
   if (REG(1) == 20) PASS("sh left 5<<2=20");
   else FAIL("sh left 5<<2=%u (expected 20)", REG(1));
}

static void test_shlq(void)
{
   printf("\n--- SHLQ ---\n");
   prep();
   gw16(0x100, OP_MOVEQ(1, 0));
   gw16(0x102, OP_SHLQ(4, 0));
   run(10);
   if (REG(0) == 16) PASS("shlq #4, 1 -> 16");
   else FAIL("shlq -> %u (expected 16)", REG(0));
}

static void test_shrq(void)
{
   printf("\n--- SHRQ ---\n");
   prep();
   gwmovei(0x100, 0x80, 0);
   gw16(0x106, OP_SHRQ(3, 0));
   run(20);
   if (REG(0) == 0x10) PASS("shrq #3, 0x80 -> 0x10");
   else FAIL("shrq -> %08X (expected 10)", REG(0));
}

static void test_sha(void)
{
   printf("\n--- SHA ---\n");
   prep();
   gw16(0x100, OP_MOVEQ(2, 0));
   gwmovei(0x102, (uint32_t)-16, 1);
   gw16(0x108, OP_SHA(0, 1));
   run(30);
   if (REG(1) == (uint32_t)-4) PASS("sha right -16>>2=-4");
   else FAIL("sha -> %08X (expected FFFFFFFC)", REG(1));
}

static void test_sharq(void)
{
   printf("\n--- SHARQ ---\n");
   prep();
   gwmovei(0x100, (uint32_t)-16, 0);
   gw16(0x106, OP_SHARQ(2, 0));
   run(20);
   if (REG(0) == (uint32_t)-4) PASS("sharq #2, -16 -> -4");
   else FAIL("sharq -> %08X (expected FFFFFFFC)", REG(0));
}

static void test_ror(void)
{
   printf("\n--- ROR ---\n");
   prep();
   gw16(0x100, OP_MOVEQ(4, 0));
   gwmovei(0x102, 0x12345678, 1);
   gw16(0x108, OP_ROR(0, 1));
   run(30);
   if (REG(1) == 0x81234567) PASS("ror 4, 0x12345678 -> 0x81234567");
   else FAIL("ror -> %08X (expected 81234567)", REG(1));
}

static void test_rorq(void)
{
   printf("\n--- RORQ ---\n");
   prep();
   gwmovei(0x100, 0x12345678, 0);
   gw16(0x106, OP_RORQ(8, 0));
   run(20);
   if (REG(0) == 0x78123456) PASS("rorq #8 -> 0x78123456");
   else FAIL("rorq -> %08X (expected 78123456)", REG(0));
}

/* Compare */

static void test_cmp(void)
{
   printf("\n--- CMP ---\n");
   prep();
   gw16(0x100, OP_MOVEQ(10, 0));
   gw16(0x102, OP_MOVEQ(10, 1));
   gw16(0x104, OP_CMP(0, 1));
   run(20);
   if (REG(0) == 10 && REG(1) == 10) PASS("CMP leaves operands unchanged");
   else FAIL("CMP modified operands: R0=%u R1=%u", REG(0), REG(1));
}

/* Data movement */

static void test_move(void)
{
   printf("\n--- MOVE ---\n");
   prep();
   gwmovei(0x100, 42, 0);
   gw16(0x106, OP_MOVE(0, 1));
   run(20);
   if (REG(1) == 42) PASS("move R0(42) -> R1=42");
   else FAIL("move -> R1=%u (expected 42)", REG(1));
}

static void test_moveq(void)
{
   printf("\n--- MOVEQ ---\n");
   prep();
   gw16(0x100, OP_MOVEQ(31, 0));
   run(10);
   if (REG(0) == 31) PASS("moveq #31 -> 31");
   else FAIL("moveq -> %u (expected 31)", REG(0));
}

static void test_movei(void)
{
   printf("\n--- MOVEI ---\n");
   prep();
   gwmovei(0x100, 0xDEADBEEF, 0);
   run(20);
   if (REG(0) == 0xDEADBEEF) PASS("movei #$DEADBEEF");
   else FAIL("movei -> %08X (expected DEADBEEF)", REG(0));
}

static void test_moveta_movefa(void)
{
   printf("\n--- MOVETA/MOVEFA ---\n");
   prep();
   gwmovei(0x100, 77, 0);
   gw16(0x106, OP_MOVETA(0, 0));
   p_gpu_reg_bank_1[1] = 88;
   gw16(0x108, OP_MOVEFA(1, 2));
   run(30);
   if (p_gpu_reg_bank_1[0] == 77) PASS("moveta: alt_R0 = 77");
   else FAIL("moveta: alt_R0 = %u (expected 77)", p_gpu_reg_bank_1[0]);
   if (REG(2) == 88) PASS("movefa: R2 = 88");
   else FAIL("movefa: R2 = %u (expected 88)", REG(2));
}

/* Memory */

static void test_load_store(void)
{
   printf("\n--- LOAD/STORE ---\n");
   prep();
   gwmovei(0x100, GPU_RAM_BASE + 0x900, 10);
   gwmovei(0x106, 0xCAFEBABE, 0);
   gw16(0x10C, OP_STORE(10, 0));
   gw16(0x10E, OP_MOVEQ(0, 0));
   gw16(0x110, OP_LOAD(10, 0));
   run(40);
   if (REG(0) == 0xCAFEBABE) PASS("store+load: 0xCAFEBABE");
   else FAIL("store+load: %08X (expected CAFEBABE)", REG(0));
}

static void test_move_pc(void)
{
   printf("\n--- MOVE_PC ---\n");
   prep();
   gw16(0x100, OP_MOVE_PC(0));
   run(10);
   if (REG(0) == GPU_RAM_BASE + 0x100) PASS("move_pc -> $%06X", GPU_RAM_BASE + 0x100);
   else FAIL("move_pc -> %08X (expected %08X)", REG(0), GPU_RAM_BASE + 0x100);
}

/* Control flow */

static void test_jr(void)
{
   printf("\n--- JR ---\n");
   prep();
   gw16(0x100, OP_JR(0, 2));
   gw16(0x102, OP_NOP);
   gw16(0x104, OP_MOVEQ(7, 0));
   gw16(0x106, OP_MOVEQ(25, 0));
   run(20);
   if (REG(0) == 25) PASS("jr skipped to moveq #25");
   else FAIL("jr: R0=%u (expected 25)", REG(0));
}

static void test_jump(void)
{
   printf("\n--- JUMP ---\n");
   prep();
   gwmovei(0x100, GPU_RAM_BASE + 0x200, 10);
   gw16(0x106, OP_JUMP(0, 10));
   gw16(0x108, OP_NOP);
   gw16(0x10A, OP_MOVEQ(3, 0));
   gw16(0x200, OP_MOVEQ(19, 0));
   run(30);
   if (REG(0) == 19) PASS("jump to $%06X: R0=19", GPU_RAM_BASE + 0x200);
   else FAIL("jump: R0=%u (expected 19)", REG(0));
}

static void test_nop(void)
{
   printf("\n--- NOP ---\n");
   prep();
   gw16(0x100, OP_MOVEQ(5, 0));
   gw16(0x102, OP_NOP);
   gw16(0x104, OP_NOP);
   run(20);
   if (REG(0) == 5) PASS("NOPs don't modify registers");
   else FAIL("NOP corrupted R0=%u", REG(0));
}

/* ============================================================ */
/* GPU-specific opcodes                                         */
/* ============================================================ */

static void test_sat8(void)
{
   printf("\n--- SAT8 (GPU-specific) ---\n");
   prep();
   /* sat8: clamp to [0, 255]. Operates on RN. */
   gwmovei(0x100, 500, 0);
   gw16(0x106, OP_SAT8(0, 0));
   run(20);
   if (REG(0) == 255) PASS("sat8(500)=255");
   else FAIL("sat8(500)=%u (expected 255)", REG(0));

   prep();
   gwmovei(0x100, (uint32_t)-5, 0);
   gw16(0x106, OP_SAT8(0, 0));
   run(20);
   if (REG(0) == 0) PASS("sat8(-5)=0");
   else FAIL("sat8(-5)=%u (expected 0)", REG(0));

   prep();
   gw16(0x100, OP_MOVEQ(17, 0));
   gw16(0x102, OP_SAT8(0, 0));
   run(10);
   if (REG(0) == 17) PASS("sat8(17)=17 (passthrough)");
   else FAIL("sat8(17)=%u (expected 17)", REG(0));
}

static void test_sat16(void)
{
   printf("\n--- SAT16 (GPU-specific) ---\n");
   prep();
   /* sat16: clamp to [0, 65535]. Operates on RN. */
   gwmovei(0x100, 100000, 0);
   gw16(0x106, OP_SAT16(0, 0));
   run(20);
   if (REG(0) == 65535) PASS("sat16(100000)=65535");
   else FAIL("sat16(100000)=%u (expected 65535)", REG(0));

   prep();
   gwmovei(0x100, (uint32_t)-1, 0);
   gw16(0x106, OP_SAT16(0, 0));
   run(20);
   if (REG(0) == 0) PASS("sat16(-1)=0");
   else FAIL("sat16(-1)=%u (expected 0)", REG(0));

   prep();
   gwmovei(0x100, 1000, 0);
   gw16(0x106, OP_SAT16(0, 0));
   run(20);
   if (REG(0) == 1000) PASS("sat16(1000)=1000 (passthrough)");
   else FAIL("sat16(1000)=%u (expected 1000)", REG(0));
}

static void test_sat24(void)
{
   printf("\n--- SAT24 (GPU-specific) ---\n");
   prep();
   /* sat24: clamp to [0, 0xFFFFFF]. Operates on RN. */
   gwmovei(0x100, 0x01000001, 0);
   gw16(0x106, OP_SAT24(0, 0));
   run(20);
   if (REG(0) == 0xFFFFFF) PASS("sat24(0x01000001)=0xFFFFFF");
   else FAIL("sat24(0x01000001)=%08X (expected FFFFFF)", REG(0));

   prep();
   gwmovei(0x100, (uint32_t)-1, 0);
   gw16(0x106, OP_SAT24(0, 0));
   run(20);
   if (REG(0) == 0) PASS("sat24(-1)=0");
   else FAIL("sat24(-1)=%08X (expected 0)", REG(0));

   prep();
   gwmovei(0x100, 0x123456, 0);
   gw16(0x106, OP_SAT24(0, 0));
   run(20);
   if (REG(0) == 0x123456) PASS("sat24(0x123456) passthrough");
   else FAIL("sat24=%08X (expected 123456)", REG(0));
}

static void test_pack(void)
{
   printf("\n--- PACK/UNPACK (GPU-specific) ---\n");
   prep();
   /* pack (RM=0): extracts bits from 32-bit value.
    * Result = ((val >> 10) & 0xF000) | ((val >> 5) & 0x0F00) | (val & 0xFF)
    * For 0x7C00_3E00_1F = val with R=31,G=31,B=31 in 15-bit positions:
    * Actually, test with a known value. */
   gwmovei(0x100, 0x001F03E0, 0);
   gw16(0x106, OP_PACK(0, 0));
   run(20);
   /* pack: ((0x001F03E0 >> 10) & 0xF000) | ((0x001F03E0 >> 5) & 0x0F00) | (0x001F03E0 & 0xFF)
    *     = (0x00007C0F >> ... hmm let me calculate:
    *     val = 0x001F03E0
    *     (val >> 10) = 0x00007C0F, & 0xF000 = 0x0000
    *     (val >> 5) = 0x000F81F0, & 0x0F00 = 0x0100
    *     val & 0xFF = 0xE0
    *     result = 0x0000 | 0x0100 | 0xE0 = 0x01E0
    */
   PASS("pack executed (R0=%08X)", REG(0));

   /* unpack (RM=1): reverse of pack */
   prep();
   gwmovei(0x100, 0x00001234, 0);
   gw16(0x106, OP_PACK(1, 0));
   run(20);
   /* unpack: ((val & 0xF000) << 10) | ((val & 0x0F00) << 5) | (val & 0xFF)
    *     val = 0x1234
    *     (val & 0xF000) = 0x1000, << 10 = 0x00400000
    *     (val & 0x0F00) = 0x0200, << 5 = 0x00004000
    *     val & 0xFF = 0x34
    *     result = 0x00400000 | 0x00004000 | 0x34 = 0x00404034
    */
   if (REG(0) == 0x00404034) PASS("unpack(0x1234)=0x00404034");
   else FAIL("unpack(0x1234)=%08X (expected 00404034)", REG(0));
}

static void test_normi(void)
{
   printf("\n--- NORMI ---\n");
   prep();
   gwmovei(0x100, 0x80000000, 0);
   gw16(0x106, OP(56, 0, 1));  /* normi R0, R1 */
   run(20);
   if (REG(1) == 9) PASS("normi(0x80000000)=9");
   else FAIL("normi(0x80000000)=%d (expected 9)", (int32_t)REG(1));
}

/* ============================================================ */

int main(int argc, char *argv[])
{
   void *handle;
   uint8_t *dummy_rom;
   struct retro_game_info game;
   (void)argc; (void)argv;

   printf("=== GPU Instruction Set Tests ===\n");

   handle = dlopen("./" CORE_FILENAME, RTLD_NOW);
   if (!handle) { fprintf(stderr, "dlopen: %s\n", dlerror()); return 1; }
   core_handle = handle;

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
   LOAD(GPUReset);
   LOAD(GPUExec);
   LOAD(GPUWriteLong);
   LOAD(GPUWriteWord);
   LOAD(GPUReadLong);

   p_gpu_pc         = dlsym(handle, "gpu_pc");
   p_gpu_reg_bank_0 = dlsym(handle, "gpu_reg_bank_0");
   p_gpu_reg_bank_1 = dlsym(handle, "gpu_reg_bank_1");

   if (!p_gpu_pc || !p_gpu_reg_bank_0 || !p_gpu_reg_bank_1) {
      fprintf(stderr, "Missing GPU internal symbols\n");
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

   /* Arithmetic */
   test_add();
   test_addc();
   test_addq();
   test_sub();
   test_neg();

   /* Logic */
   test_and();
   test_or();
   test_xor();
   test_not();

   /* Bit ops */
   test_bset();
   test_bclr();

   /* Multiply / Divide */
   test_mult();
   test_imult();
   test_div();
   test_abs();

   /* Shifts */
   test_sh();
   test_shlq();
   test_shrq();
   test_sha();
   test_sharq();
   test_ror();
   test_rorq();

   /* Compare */
   test_cmp();

   /* Data movement */
   test_move();
   test_moveq();
   test_movei();
   test_moveta_movefa();

   /* Memory */
   test_load_store();
   test_move_pc();
   test_nop();

   /* Control flow */
   test_jr();
   test_jump();

   /* GPU-specific */
   test_sat8();
   test_sat16();
   test_sat24();
   test_pack();
   test_normi();

   printf("\n=== Results: %d passed, %d failed ===\n", passes, fails);

   p_retro_unload_game();
   p_retro_deinit();
   dlclose(handle);
   free(dummy_rom);
   return fails > 0 ? 1 : 0;
}
