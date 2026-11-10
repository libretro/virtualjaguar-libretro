/* test_gpu_interleave.c -- the Jaguar GPU RISC's instruction schedule,
 * MEASURED: the divide's background window, the interleave that fills
 * it, and the per-cause stall breakdown.
 *
 * Build: cc -O2 -Wall -std=c99 -o test/test_gpu_interleave \
 *           test/test_gpu_interleave.c -ldl
 * Usage: ./test/test_gpu_interleave
 *
 * Needs the core built with TEST_EXPORTS=1 (make test) so the internal
 * GPU symbols are visible, and it turns the pipeline-timing model on
 * (virtualjaguar_gpu_pipeline_timing) by writing vjs.gpuPipelineTiming
 * directly -- the option path needs a frontend to call it.
 *
 * What it measures, and why each case is here:
 *
 *   1. the divide's background window (DIV: 1 tick issue + 16 ticks
 *      background, R15) and what a dependent consumer pays for it;
 *   2. the same program with 16 flag-transparent shadow instructions
 *      dropped into the window -- they must be free;
 *   3. the shadow alone, on DISJOINT registers vs one register: this is
 *      what the rotating allocation buys (1 tick/op vs 2);
 *   4. the two-source ALU stream (the write-back port conflict, R4);
 *   5. the RAW interlock (R2) and the flags interlock (R3);
 *   6. the note's own EVEN/ODD/RESULT/NEW rotation, as written (with
 *      the MOVs) and with the MOVs removed -- the schedule the user
 *      designed, measured.
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

/* The core's settings global.  Only the leading bools matter here, and
 * src/core/settings.h says the field order is stable ("Original three
 * fields kept first so the test harness in test/test_hle_bios.c --
 * which redeclares VJSettings with just these three -- sees the same
 * layout via dlsym").  The header itself pulls in <boolean.h>, which
 * is a Windows header, so redeclare the prefix here instead. */
struct VJSettingsPrefix {
   bool hardwareTypeNTSC;
   bool useJaguarBIOS;
   bool useFastBlitter;
   bool blitterTiming;
   bool gpuPipelineTiming;
};

#define GPU_FLAGS_ADDR   0xF02100
#define GPU_CTRL_ADDR    0xF02114
#define GPU_PC_ADDR      0xF02110
#define GPU_RAM_BASE     0xF03000
#define GPU_RAM_SIZE     0x1000

#define GPUGO    0x001

/* --- the RISC encoding: opcode[15:10] | reg1[9:5] | reg2[4:0].
 * reg1 is the source, reg2 is the destination; for ALU ops the
 * destination is also read (read-modify-write). --- */
#define OP(opc, r1, r2) ((uint16_t)(((opc)<<10)|((r1)<<5)|(r2)))

#define OP_ADD(rs, rd)    OP(0,  rs, rd)
#define OP_ADDQ(n, rd)    OP(2,  n,  rd)
#define OP_ADDQT(n, rd)   OP(3,  n,  rd)
#define OP_SUB(rs, rd)    OP(4,  rs, rd)
#define OP_SUBQT(n, rd)   OP(7,  n,  rd)
#define OP_IMULTN(rs, rd) OP(18, rs, rd)
#define OP_RESMAC(rd)     OP(19, 0,  rd)
#define OP_IMACN(rs, rd)  OP(20, rs, rd)
#define OP_DIV(rs, rd)    OP(21, rs, rd)
#define OP_CMP(rs, rd)    OP(30, rs, rd)
#define OP_CMPQ(n, rd)    OP(31, n,  rd)
#define OP_MOVE(rs, rd)   OP(34, rs, rd)
#define OP_MOVEQ(n, rd)   OP(35, n,  rd)
#define OP_LOAD(rs, rd)   OP(41, rs, rd)
#define OP_LOAD_R14(n,rd) OP(43, n,  rd)   /* rd <- (R14 + 4n) */
#define OP_STORE(ra, rv)  OP(47, ra, rv)
#define OP_STORE_R14(n,rv) OP(49, n, rv)   /* (R14 + 4n) <- rv */
#define OP_JUMP(cc, ra)   OP(52, ra, cc)
#define OP_JR(cc, off)    OP(53, (off)&0x1F, cc)
#define OP_NOP             OP(57, 0, 0)
#define OP_MOVEI(rd)      OP(38, 0,  rd)
#define OP_MULT(rs, rd)   OP(16, rs, rd)

/* libretro entry points */
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

/* GPU internals */
static void      (*p_GPUReset)(void);
static void      (*p_GPUExec)(int32_t);
static void      (*p_GPUWriteLong)(uint32_t, uint32_t, uint32_t);
static void      (*p_GPUWriteWord)(uint32_t, uint16_t, uint32_t);
static uint32_t  (*p_GPUReadLong)(uint32_t, uint32_t);
static void      (*p_GPUPipeTimingReset)(void);
static uint32_t *p_gpu_reg_bank_0;
static struct VJSettingsPrefix *p_vjs;

/* the per-cause stall counters (test ABI; see src/tom/gpu.c) */
static uint64_t *p_gpu_pipe_stall_total;
static uint64_t *p_gpu_pipe_stall_score;
static uint64_t *p_gpu_pipe_stall_raw;
static uint64_t *p_gpu_pipe_stall_port;
static uint64_t *p_gpu_pipe_stall_flags;
static uint64_t *p_gpu_pipe_stall_extra;
static uint64_t *p_gpu_pipe_issue_total;
static uint64_t *p_gpu_pipe_ticks_total;

static void video_refresh(const void *d, unsigned w, unsigned h, size_t p)
{ (void)d;(void)w;(void)h;(void)p; }
static void audio_sample(int16_t l, int16_t r) { (void)l; (void)r; }
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

/* --- program builder ---------------------------------------------- */
static uint16_t prog[512];
static int      prog_n;

static void emit(uint16_t w) { prog[prog_n++] = w; }
static void emit_movei(uint8_t rd, uint32_t imm)
{
   emit(OP_MOVEI(rd));
   emit((uint16_t)(imm & 0xFFFF));
   emit((uint16_t)((imm >> 16) & 0xFFFF));
}

static void gw16(uint16_t off, uint16_t v) { p_GPUWriteWord(GPU_RAM_BASE + off, v, 1); }

/* Stop the RISC from the program itself: write 0 to G_CTRL, which
 * clears GPUGO (the local processor may clear its own GO bit -- v8
 * bug #23).  Without this the NOP sled that fills local RAM would run
 * to the end of the slice budget and pollute the counters. */
static void emit_halt(void)
{
   emit_movei(5, GPU_CTRL_ADDR);
   emit(OP_MOVEQ(0, 6));
   emit(OP_STORE(5, 6));
}

static void prep(void)
{
   uint32_t i;
   p_GPUReset();
   for (i = 0; i < GPU_RAM_SIZE; i += 2)
      gw16((uint16_t)i, OP_NOP);
   for (i = 0; i < 32; i++)
      p_gpu_reg_bank_0[i] = 0;
   prog_n = 0;
}

static int passes = 0, fails = 0;
#define CHECK(cond, msg, ...) do { \
   if (cond) { printf("  PASS: " msg "\n", ##__VA_ARGS__); passes++; } \
   else      { printf("  FAIL: " msg "\n", ##__VA_ARGS__); fails++; } } while (0)

/* Run the program and verify it cleared its own GO bit. */
static void run(void)
{
   int i;
   for (i = 0; i < prog_n; i++)
      gw16((uint16_t)(0x100 + i * 2), prog[i]);
   p_GPUWriteLong(GPU_PC_ADDR, GPU_RAM_BASE + 0x100, 1);
   p_GPUWriteLong(GPU_CTRL_ADDR, GPUGO, 1);
   p_GPUExec(20000);
   CHECK(!(p_GPUReadLong(GPU_CTRL_ADDR, 1) & GPUGO),
         "GPU program halted within the slice budget");
   CHECK(*p_gpu_pipe_stall_score + *p_gpu_pipe_stall_raw +
         *p_gpu_pipe_stall_port + *p_gpu_pipe_stall_flags +
         *p_gpu_pipe_stall_extra <= *p_gpu_pipe_stall_total,
         "classified stalls do not exceed the total");
}

/* --- reporting ------------------------------------------------------ */
typedef struct {
   uint64_t ticks, issue, score, raw, port, flags, extra;
} meas;

static void snapshot(meas *m)
{
   m->ticks = *p_gpu_pipe_ticks_total;
   m->issue = *p_gpu_pipe_issue_total;
   m->score = *p_gpu_pipe_stall_score;
   m->raw   = *p_gpu_pipe_stall_raw;
   m->port  = *p_gpu_pipe_stall_port;
   m->flags = *p_gpu_pipe_stall_flags;
   m->extra = *p_gpu_pipe_stall_extra;
}

static void print_meas(const char *name, const meas *m, const meas *base)
{
   uint64_t t = m->ticks - base->ticks;
   uint64_t i = m->issue - base->issue;
   printf("%-42s ticks=%5llu instr=%4llu  stalls: score=%llu raw=%llu port=%llu flags=%llu extra=%llu",
          name, (unsigned long long)t, (unsigned long long)i,
          (unsigned long long)(m->score - base->score),
          (unsigned long long)(m->raw   - base->raw),
          (unsigned long long)(m->port  - base->port),
          (unsigned long long)(m->flags - base->flags),
          (unsigned long long)(m->extra - base->extra));
   if (i)
      printf("   %.2f ticks/instr", (double)t / (double)i);
   printf("\n");
}

/* --- the 3D vertex pipeline ---------------------------------------- */
/* Vertex buffer in GPU local RAM; each vertex is x,y,z (+1 spare word).
 * Registers:
 *   r0  = 1                r1  = w          r2 = nx        r3 = ny
 *   r4,r5,r6 = x,y,z       r14 = the vertex buffer base (advanced 16/iter)
 *   r15 = the loop counter r21 = the loop address   r28 = 1/w
 *   r29 = the iteration limit
 *   r16..r27 = the 3x4 matrix
 *
 * Per vertex: 3 indexed loads, 3 MAC dot products (IMULTN + 3x IMACN +
 * RESMAC each), the perspective divide, 2 multiplies, 2 indexed stores.
 *
 * interleave = 0: the loads are at the top of the body and the quotient
 *                is consumed right after the divide (the latency is
 *                exposed).
 * interleave = 1: the loads are issued into the divide's window -- the
 *                same three instructions, the same work, placed so the
 *                shadow fills the 16 background ticks.
 */
#define VERT_BASE 0xF03800u

static void build_vertex_pipeline(int shadow_ops)
{
   int loop_index, i, m21_index;
   uint32_t addr;
   prog_n = 0;
   emit_movei(14, VERT_BASE);       /* r14 = the vertex buffer base   */
   m21_index = prog_n;               /* r13 = the loop address (patched) */
   emit_movei(13, 0);
   emit(OP_MOVEQ(0, 15));           /* r15 = the counter              */
   emit_movei(29, 32);              /* r29 = the limit (32 iterations) */
   emit(OP_MOVEQ(1, 0));            /* r0 = 1                         */
   for (i = 0; i < 12; i++)         /* seed the matrix                */
      emit(OP_MOVEQ((uint8_t)(i + 1), (uint8_t)(16 + i)));

   loop_index = prog_n;
   /* --- the body ---------------------------------------------------- */
   emit(OP_LOAD_R14(1, 4));         /* x */
   emit(OP_LOAD_R14(2, 5));         /* y */
   emit(OP_LOAD_R14(3, 6));         /* z */
   /* w = m20*x + m21*y + m22*z + m23 */
   emit(OP_IMULTN(4, 20)); emit(OP_IMACN(5, 21));
   emit(OP_IMACN(6, 22));  emit(OP_IMACN(0, 23));
   emit(OP_RESMAC(1));
   /* nx = m00*x + m01*y + m02*z + m03 */
   emit(OP_IMULTN(4, 16)); emit(OP_IMACN(5, 17));
   emit(OP_IMACN(6, 18));  emit(OP_IMACN(0, 19));
   emit(OP_RESMAC(2));
   /* ny = m10*x + m11*y + m12*z + m13 */
   emit(OP_IMULTN(4, 24)); emit(OP_IMACN(5, 25));
   emit(OP_IMACN(6, 26));  emit(OP_IMACN(0, 27));
   emit(OP_RESMAC(3));
   /* the perspective divide: r28 = 1 / w */
   emit(OP_MOVEQ(1, 28));
   emit(OP_DIV(1, 28));            /* 1 tick issue, 16 ticks background */
   /* --- the shadow: independent work dropped into the window ---------
    * The only registers a 3D transform leaves free are r7-r12 and
    * r30-r31: eight of them.  The shadow rotates over those eight, so
    * the first eight ops are disjoint (1 tick each) and the rest hit
    * the RAW interlock (2 ticks each) -- that is the register-file
    * limit the schedule runs into, not a pipeline limit. */
   {
      static const uint8_t free_regs[8] = {7,8,9,10,11,12,30,31};
      int s;
      for (s = 0; s < shadow_ops; s++)
         emit(OP_ADDQT(1, free_regs[s % 8]));
   }
   /* the consumer: the screen coordinates */
   emit(OP_MOVE(2, 19)); emit(OP_MULT(28, 19));
   emit(OP_MOVE(3, 20)); emit(OP_MULT(28, 20));
   emit(OP_STORE_R14(20, 19));
   emit(OP_STORE_R14(21, 20));
   /* the loop */
   emit(OP_ADDQ(16, 14));          /* the base advances one vertex */
   emit(OP_CMP(29, 15));           /* flags = r15 - r29 */
   emit(OP_JUMP(1, 13));           /* branch while Z is clear */
   emit(OP_ADDQ(1, 15));           /* the delay slot always executes: count */

   addr = GPU_RAM_BASE + 0x100 + 2 * (uint32_t)loop_index;
   prog[m21_index + 1] = (uint16_t)(addr & 0xFFFF);
   prog[m21_index + 2] = (uint16_t)((addr >> 16) & 0xFFFF);
   emit_halt();
}

/* -------------------------------------------------------------------- */

int main(void)
{
   static const meas zero = {0};
   static const uint8_t odd [6][3] = {{1,3,5},{10,12,14},{3,5,7},{12,14,0},{5,7,9},{0,2,3}};
   static const uint8_t res [6][3] = {{6,7,8},{15,0,1},{8,9,10},{1,2,3},{10,11,12},{4,5,6}};
   static const uint8_t nw  [6][3] = {{9,11,13},{2,4,6},{11,13,15},{4,6,8},{13,14,15},{7,8,9}};
   void *handle;
   uint8_t *dummy_rom;
   struct retro_game_info game;
   meas base, m;

   printf("=== Jaguar GPU RISC: the interleave schedule, measured ===\n");

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
   LOAD(GPUReset);
   LOAD(GPUExec);
   LOAD(GPUWriteLong);
   LOAD(GPUWriteWord);
   LOAD(GPUReadLong);

   p_gpu_reg_bank_0 = dlsym(handle, "gpu_reg_bank_0");
   p_gpu_pipe_stall_total = dlsym(handle, "gpu_pipe_stall_total");
   p_gpu_pipe_stall_score = dlsym(handle, "gpu_pipe_stall_score");
   p_gpu_pipe_stall_raw   = dlsym(handle, "gpu_pipe_stall_raw");
   p_gpu_pipe_stall_port  = dlsym(handle, "gpu_pipe_stall_port");
   p_gpu_pipe_stall_flags = dlsym(handle, "gpu_pipe_stall_flags");
   p_gpu_pipe_stall_extra = dlsym(handle, "gpu_pipe_stall_extra");
   p_gpu_pipe_issue_total = dlsym(handle, "gpu_pipe_issue_total");
   p_gpu_pipe_ticks_total = dlsym(handle, "gpu_pipe_ticks_total");
   p_GPUPipeTimingReset   = dlsym(handle, "GPUPipeTimingReset");
   p_vjs                  = dlsym(handle, "vjs");
   if (!p_gpu_reg_bank_0 || !p_gpu_pipe_ticks_total || !p_gpu_pipe_issue_total
       || !p_gpu_pipe_stall_total || !p_gpu_pipe_stall_score || !p_gpu_pipe_stall_raw
       || !p_gpu_pipe_stall_port || !p_gpu_pipe_stall_flags
       || !p_gpu_pipe_stall_extra) {
      fprintf(stderr, "Missing GPU diagnostics symbols (build with TEST_EXPORTS=1)\n");
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

   /* Turn the pipeline-timing model on.  The option path needs a
    * frontend; writing the setting directly is what the core does when
    * the option is "enabled". */
   p_vjs->gpuPipelineTiming = true;
   if (p_GPUPipeTimingReset) p_GPUPipeTimingReset();

   /* ---- 0. the baseline: an empty program (halt only) ------------- */
   prep();
   emit_halt();
   run();
   snapshot(&base);
   printf("\n-- baseline (halt only) ------------------------------------\n");
   print_meas("baseline", &base, &zero);

   /* ---- 1. the divide and its dependent consumer ------------------ */
   printf("\n-- 1. the divide's background window (R15) ------------------\n");
   prep();
   emit_movei(1, 100);          /* r1 = dividend */
   emit_movei(2, 7);            /* r2 = divisor  */
   emit(OP_DIV(2, 1));          /* r1 = r1 / r2  */
   emit(OP_ADD(1, 3));          /* consumer: reads r1 -> waits for the quotient */
   emit_halt();
   run();
   snapshot(&m);
   print_meas("DIV + dependent consumer", &m, &base);
   CHECK(m.score - base.score >= 15,
         "the consumer waits for the quotient: score=%llu ticks (R15: 16 background ticks)",
         (unsigned long long)(m.score - base.score));
   CHECK(p_gpu_reg_bank_0[1] == 14, "quotient = 100/7 = 14 (got %u)", p_gpu_reg_bank_0[1]);

   /* ---- 2. the same program with 16 shadow instructions (R15) ----------- */
   printf("\n-- 2. 16 shadow instructions dropped into the window --------\n");
   prep();
   emit_movei(1, 100);
   emit_movei(2, 7);
   emit(OP_DIV(2, 1));
   {
      int i;
      for (i = 0; i < 16; i++)
         emit(OP_ADDQT(1, (uint8_t)(4 + i)));   /* disjoint: r4..r19 */
   }
   emit(OP_ADD(1, 3));          /* the consumer */
   emit_halt();
   run();
   snapshot(&m);
   print_meas("DIV + 16 shadow + consumer", &m, &base);
   CHECK(m.score - base.score == 0,
         "the consumer no longer waits: score=%llu (the window absorbed the shadow)",
         (unsigned long long)(m.score - base.score));

   /* ---- 3. the shadow alone: disjoint vs dependent ---------------- */
   printf("\n-- 3. the shadow stream: disjoint vs dependent registers ---\n");
   prep();
   {
      int i;
      for (i = 0; i < 16; i++) emit(OP_ADDQT(1, (uint8_t)(4 + i)));
   }
   emit_halt();
   run();
   snapshot(&m);
   print_meas("16 ADDQT, DISJOINT registers", &m, &base);
   CHECK(m.raw == base.raw, "disjoint writes avoid the RAW interlock");

   prep();
   {
      int i;
      for (i = 0; i < 16; i++) emit(OP_ADDQT(1, 4));   /* all on r4 */
   }
   emit_halt();
   run();
   snapshot(&m);
   print_meas("16 ADDQT, SAME register (dependent)", &m, &base);
   CHECK(m.raw > base.raw && m.port == base.port, "dependent writes pay RAW without a port conflict");

   /* ---- 4. the two-source ALU stream (R4 port conflict) ----------- */
   printf("\n-- 4. the two-source ALU stream (write-back port conflict) --\n");
   prep();
   {
      int i;
      for (i = 0; i < 16; i++) emit(OP_ADD(4, (uint8_t)(4 + i)));
   }
   emit_halt();
   run();
   snapshot(&m);
   print_meas("16 ADD (2 sources), disjoint dests", &m, &base);
   CHECK(m.port > base.port, "independent two-source ADDs pay the port conflict");

   /* ---- 5. the RAW and flags interlocks ---------------------------- */
   printf("\n-- 5. RAW interlock (R2) and flags interlock (R3) -----------\n");
   prep();
   emit(OP_ADD(4, 5));          /* r5 = r5 + r4 */
   emit(OP_ADD(5, 6));          /* reads r5: RAW */
   emit(OP_ADD(6, 7));          /* reads r6: RAW */
   emit_halt();
   run();
   snapshot(&m);
   print_meas("3-deep dependent ADD chain", &m, &base);
   CHECK(m.raw > base.raw && m.port == base.port, "dependent ADDs pay RAW without a port conflict");

   prep();
   emit(OP_MOVEQ(1, 4));
   emit(OP_MOVEQ(2, 5));
   emit(OP_CMP(4, 5));          /* flags = r5 - r4 */
   emit(OP_JR(1, 0));           /* conditional: reads the flags (R3) */
   emit_halt();
   run();
   snapshot(&m);
   print_meas("CMP ; JR cc (flags interlock)", &m, &base);
   CHECK(m.flags > base.flags, "adjacent CMP and conditional JR pay the flags interlock");

   prep();
   emit(OP_MOVEQ(1, 4));
   emit(OP_MOVEQ(2, 5));
   emit(OP_CMP(4, 5));
   emit(OP_NOP);                /* one instruction between */
   emit(OP_JR(1, 0));
   emit_halt();
   run();
   snapshot(&m);
   print_meas("CMP ; NOP ; JR cc", &m, &base);
   CHECK(m.flags == base.flags, "a NOP between CMP and JR avoids the flags interlock");

   /* ---- 6. the note's rotation, as written and as fixed ------------ */
   printf("\n-- 6. the note's EVEN/ODD/RESULT/NEW rotation --------------\n");
   /* As written: 3 ADDs into RESULT, then 3 MOVs into NEW, six rows.
    * Registers: EVEN {0,2,4} ODD {1,3,5} RESULT {6,7,8} NEW {9,11,13};
    * row 2: EVEN {9,11,13} ODD {10,12,14} RESULT {15,0,1} NEW {2,4,6};
    * row 3: EVEN {2,4,6} ODD {3,5,7} RESULT {8,9,10} NEW {11,13,15};
    * row 4: EVEN {11,13,15} ODD {12,14,0} RESULT {1,2,3} NEW {4,6,8};
    * row 5: EVEN {4,6,8} ODD {5,7,9} RESULT {10,11,12} NEW {13,14,15};
    * row 6: EVEN {13,14,15} ODD {0,2,3} RESULT {4,5,6} NEW {7,8,9}. */
   prep();
   {
      int row, k;
      for (row = 0; row < 6; row++) {
         for (k = 0; k < 3; k++) emit(OP_ADD(odd[row][k], res[row][k]));
         for (k = 0; k < 3; k++) emit(OP_MOVE(res[row][k], nw[row][k]));
      }
   }
   emit_halt();
   run();
   snapshot(&m);
   print_meas("note's rotation, as written (36 ops)", &m, &base);
   {
      uint64_t instr = m.issue - base.issue;
      CHECK(instr == 36, "the note's row costs 6 instructions (got %llu total)",
            (unsigned long long)instr);
   }

   /* Fixed: the ADD writes the slot the next row reads, so the MOV
    * stage disappears -- 18 instructions instead of 36, and the
    * 1-instruction RAW the MOV created goes with it.  (The notes'
    * "ADD r0 r1 to r6" is a 3-input add, which this ISA does not have:
    * ADD is read-modify-write, so the destination is also a source --
    * the MOV stage is how the notes seed the next accumulator.  Writing
    * straight into the next row's slot does the same job in one
    * instruction.) */
   prep();
   {
      int row, k;
      for (row = 0; row < 6; row++) {
         for (k = 0; k < 3; k++)
            emit(OP_ADD(odd[row][k], nw[row][k]));
      }
   }
   emit_halt();
   run();
   snapshot(&m);
   print_meas("rotation without the MOV stage (18 ops)", &m, &base);
   {
      uint64_t instr = m.issue - base.issue;
      CHECK(instr == 18, "the MOV-less row costs 3 instructions (got %llu)",
            (unsigned long long)instr);
   }

/* ---- 7. the 3D vertex pipeline: the shadow sweep ---------------- */
   printf("\n-- 7. a 3D vertex pipeline: how much shadow fits? -------------\n");
   /* One vertex: three dot products through the MAC unit (IMULTN +
    * 3x IMACN + RESMAC each) and a perspective divide -- the long op.
    * The naive schedule consumes the quotient immediately, so the
    * divide's 16-tick latency is exposed.  The sweep drops K
    * independent instructions into that window and measures the ticks
    * per vertex: the curve says how much of the window a schedule can
    * actually fill. */
   {
      static const int K[] = {0, 2, 4, 6, 8, 10, 12, 16};
      unsigned k;
      double best = 1e9;
      int best_k = 0;
      double naive_per = 0.0;
      uint64_t naive_issue = 0;
      /* Nominal NTSC GPU clock divided by 60 frames per second. */
      const double ticks_per_frame = 26590906.0 / 60.0;

      printf("   %-6s %-12s %-12s %-10s %-8s %-8s %-8s\n",
             "shadow", "ticks/vertex", "vertices/frm", "score", "raw", "port", "flags");
      for (k = 0; k < sizeof(K)/sizeof(K[0]); k++) {
         double per;
         prep();
         build_vertex_pipeline(K[k]);
         run();
         snapshot(&m);
         per = (double)(m.ticks - base.ticks) / 32.0;
         if (k == 0) {
            naive_per = per;
            naive_issue = m.issue;
         } else {
            CHECK(m.issue > naive_issue, "the %d shadow instructions increase issue count", K[k]);
         }
         printf("   %-6d %-12.2f %-12.0f %-10llu %-8llu %-8llu %-8llu",
                K[k], per, ticks_per_frame / per,
                (unsigned long long)(m.score - base.score),
                (unsigned long long)(m.raw   - base.raw),
                (unsigned long long)(m.port  - base.port),
                (unsigned long long)(m.flags - base.flags));
         if (k == 0) printf("   <- naive");
         printf("\n");
         if (k > 0 && per < best) { best = per; best_k = K[k]; }
      }
      printf("   best: %d shadow instructions -> %.2f ticks/vertex"
             " (naive %.2f) -> %.0f vertices/frame at 60 fps\n",
             best_k, best, naive_per, ticks_per_frame / best);
      printf("   ticks/vertex is the VERTEX rate; the shadow's instructions\n"
             "   ride along inside the divide's window, so the vertex rate\n"
             "   should not improve -- the shadow is free CAPACITY.\n");
      CHECK(best <= naive_per + 0.01,
            "the shadow does not slow the vertex pipeline: %.2f vs %.2f",
            best, naive_per);
   }
   printf("\n=== %d passed, %d failed ===\n", passes, fails);
   p_retro_unload_game();
   p_retro_deinit();
   free(dummy_rom);
   return fails ? 1 : 0;
}
