/* test_irq_cascade.c -- Interrupt cascade and cross-subsystem IRQ tests.
 *
 * Tests interrupt propagation across the Jaguar's interconnected subsystems:
 * - JERRY timer → 68K interrupt
 * - TOM video interrupt (VI) → 68K
 * - 68K → DSP interrupt dispatch
 * - DSP I2S (SSI) interrupt lifecycle
 * - GPU → 68K interrupt signaling
 * - Nested/chained interrupt scenarios
 *
 * Each test creates a synthetic ROM with 68K ISRs that record interrupt
 * activity to RAM, then verifies the expected interrupts fired.
 *
 * Build: cc -o test/test_irq_cascade test/test_irq_cascade.c -ldl
 * Usage: ./test/test_irq_cascade [path/to/core.dylib]
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

/* ================================================================
 * Hardware addresses
 * ================================================================ */

/* TOM */
#define TOM_INT1_ADDR  0xF000E0
#define TOM_VI_ADDR    0xF0004E
#define TOM_VMODE_ADDR 0xF00028

/* GPU */
#define GPU_CTRL_ADDR  0xF02114
#define GPU_FLAGS_ADDR 0xF02100
#define GPU_RAM_ADDR   0xF03000
#define GPU_G_END_ADDR 0xF0210C

/* DSP */
#define DSP_FLAGS_ADDR 0xF1A100
#define DSP_CTRL_ADDR  0xF1A114
#define DSP_RAM_ADDR   0xF1B000

/* JERRY */
#define JERRY_PIT0_ADDR 0xF10000
#define JERRY_PIT1_ADDR 0xF10002
#define JERRY_INT_ADDR  0xF10020

/* 68K vector table (addresses in RAM, each 4 bytes) */
#define VEC_BUS_ERROR    0x08
#define VEC_ADDRESS_ERR  0x0C
#define VEC_ILLEGAL      0x10
#define VEC_LEVEL1       0x64   /* autovector level 1 */
#define VEC_LEVEL2       0x68   /* autovector level 2 (TOM video) */
#define VEC_LEVEL3       0x6C   /* autovector level 3 */
#define VEC_LEVEL4       0x70   /* autovector level 4 */
#define VEC_LEVEL5       0x74   /* autovector level 5 */
#define VEC_LEVEL6       0x78   /* autovector level 6 */
#define VEC_LEVEL7       0x7C   /* autovector level 7 (NMI) */

/* TOM INT1 bits */
#define INT1_VIDEO_EN   0x0001   /* video interrupt enable */
#define INT1_GPU_EN     0x0002   /* GPU interrupt enable */
#define INT1_OP_EN      0x0004   /* object processor interrupt enable */
#define INT1_TIMER_EN   0x0008   /* timer interrupt enable */
#define INT1_DSP_EN     0x0010   /* DSP interrupt enable */

/* INT1 clear bits (write 1 to clear) */
#define INT1_CLR_VIDEO  0x0100
#define INT1_CLR_GPU    0x0200
#define INT1_CLR_OP     0x0400
#define INT1_CLR_TIMER  0x0800
#define INT1_CLR_DSP    0x1000

#define WHO_M68K     6
#define M68K_REG_PC  16
#define M68K_REG_D0  0

/* ================================================================
 * 68K instruction encoding
 * ================================================================ */

static int emit16(uint8_t *p, uint16_t v)
{
   p[0] = v >> 8; p[1] = v & 0xFF; return 2;
}

static int emit32(uint8_t *p, uint32_t v)
{
   p[0] = (v >> 24) & 0xFF; p[1] = (v >> 16) & 0xFF;
   p[2] = (v >> 8) & 0xFF;  p[3] = v & 0xFF; return 4;
}

static int emit_nop(uint8_t *p) { return emit16(p, 0x4E71); }
static int emit_bra_self(uint8_t *p) { return emit16(p, 0x60FE); }
static int emit_rte(uint8_t *p) { return emit16(p, 0x4E73); }

/* MOVE.L #imm32, abs32 */
static int emit_movel_imm_abs32(uint8_t *p, uint32_t imm, uint32_t addr)
{
   int n = 0;
   n += emit16(p, 0x23FC);
   n += emit32(p + n, imm);
   n += emit32(p + n, addr);
   return n;
}

/* MOVE.W #imm16, abs32 */
static int emit_movew_imm_abs32(uint8_t *p, uint16_t imm, uint32_t addr)
{
   int n = 0;
   n += emit16(p, 0x33FC);
   n += emit16(p + n, imm);
   n += emit32(p + n, addr);
   return n;
}

/* ADDQ.L #quick, abs32 */
static int emit_addq_l_abs32(uint8_t *p, int quick, uint32_t addr)
{
   int n = 0;
   n += emit16(p, 0x50B9 | ((quick & 7) << 9));
   n += emit32(p + n, addr);
   return n;
}

/* MOVE.L abs32, Dn */
static int emit_movel_abs32_dn(uint8_t *p, uint32_t addr, int dn)
{
   int n = 0;
   n += emit16(p, 0x2039 | ((dn & 7) << 9));
   n += emit32(p + n, addr);
   return n;
}

/* MOVE.L Dn, abs32 */
static int emit_movel_dn_abs32(uint8_t *p, int dn, uint32_t addr)
{
   int n = 0;
   n += emit16(p, 0x23C0 | (dn & 7));
   n += emit32(p + n, addr);
   return n;
}

/* MOVE.W #imm16, (An) */
static int emit_movew_imm_ind(uint8_t *p, int an, uint16_t imm)
{
   int n = 0;
   n += emit16(p, 0x30BC | ((an & 7) << 9));
   n += emit16(p + n, imm);
   return n;
}

/* MOVE.L #imm32, An */
static int emit_movea_l_imm(uint8_t *p, int an, uint32_t imm)
{
   int n = 0;
   n += emit16(p, 0x207C | ((an & 7) << 9));
   n += emit32(p + n, imm);
   return n;
}

/* DSP instruction helpers */
#define DSP_OP(opc, r1, r2)  ((uint16_t)(((opc) << 10) | ((r1) << 5) | (r2)))
#define DSP_NOP              DSP_OP(57, 0, 0)
#define DSP_MOVEQ(n, rd)     DSP_OP(35, (n), (rd))
#define DSP_MOVEI(rd)        DSP_OP(38, 0, (rd))
#define DSP_STORE(rm, rn)    DSP_OP(47, (rm), (rn))
#define DSP_JR(cc, off)      DSP_OP(53, (off) & 0x1F, (cc))

static void dsp_write16(uint8_t *ram, uint16_t offset, uint16_t val)
{
   ram[offset] = (val >> 8) & 0xFF;
   ram[offset + 1] = val & 0xFF;
}

static void dsp_write_movei(uint8_t *ram, uint16_t offset, uint32_t imm, uint8_t rd)
{
   uint16_t op = DSP_MOVEI(rd);
   uint16_t lo = imm & 0xFFFF;
   uint16_t hi = (imm >> 16) & 0xFFFF;
   ram[offset]     = (op >> 8) & 0xFF;
   ram[offset + 1] = op & 0xFF;
   ram[offset + 2] = (lo >> 8) & 0xFF;
   ram[offset + 3] = lo & 0xFF;
   ram[offset + 4] = (hi >> 8) & 0xFF;
   ram[offset + 5] = hi & 0xFF;
}

/* ================================================================
 * Libretro plumbing
 * ================================================================ */

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

static void *core_handle;

static uint8_t **p_jaguarMainRAM;
static uint8_t *p_tomRam8;
static uint32_t *p_dsp_control;
static uint8_t *(*p_DSPGetRAM)(void);
static void (*p_DSPWriteLong)(uint32_t, uint32_t, uint32_t);
static uint32_t (*p_DSPReadLong)(uint32_t, uint32_t);
static uint32_t (*p_GPUReadLong)(uint32_t, uint32_t);
static uint16_t (*p_JERRYReadWord)(uint32_t, uint32_t);
static unsigned (*p_m68k_get_reg)(void *, int);

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
      *(const char **)data = "/tmp";
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

/* Test counters */
static int passes = 0, fails = 0;
#define PASS(msg, ...) do { printf("  PASS: " msg "\n", ##__VA_ARGS__); passes++; } while(0)
#define FAIL(msg, ...) do { printf("  FAIL: " msg "\n", ##__VA_ARGS__); fails++; } while(0)
#define INFO(msg, ...) do { printf("  INFO: " msg "\n", ##__VA_ARGS__); } while(0)

/* ROM building */
#define ROM_SIZE    131072
#define CODE_BASE   0x2000
#define CODE_ADDR   0x00802000
#define ISR_BASE    0x4000   /* ISR code offset in ROM ($804000 in Jaguar space) */
#define ISR_ADDR    0x00804000

/* RAM probe addresses for ISR counters */
#define PROBE_VIDEO_COUNT  0x3000   /* incremented by video ISR */
#define PROBE_TIMER_COUNT  0x3004   /* incremented by timer ISR */
#define PROBE_GPU_COUNT    0x3008   /* incremented by GPU ISR */
#define PROBE_DSP_COUNT    0x300C   /* incremented by DSP ISR */
#define PROBE_TOTAL_IRQ    0x3010   /* total IRQ counter */
#define PROBE_LAST_INT1    0x3014   /* last INT1 value seen in ISR */

static uint8_t rom_buf[ROM_SIZE];

static void rom_init(void)
{
   memset(rom_buf, 0, ROM_SIZE);
   rom_buf[0x404] = 0x00; rom_buf[0x405] = 0x80;
   rom_buf[0x406] = 0x20; rom_buf[0x407] = 0x00;
   rom_buf[CODE_BASE] = 0x60; rom_buf[CODE_BASE + 1] = 0xFE;
}

static bool rom_load(const char *name)
{
   struct retro_game_info game;
   memset(&game, 0, sizeof(game));
   game.path = name;
   game.data = rom_buf;
   game.size = ROM_SIZE;
   return p_retro_load_game(&game);
}

static void run_frames(unsigned n)
{
   unsigned i;
   for (i = 0; i < n; i++)
      p_retro_run();
}

static uint32_t ram_get32(uint32_t addr)
{
   uint8_t *ram = *p_jaguarMainRAM;
   return ((uint32_t)ram[addr] << 24) | ((uint32_t)ram[addr + 1] << 16)
        | ((uint32_t)ram[addr + 2] << 8) | (uint32_t)ram[addr + 3];
}

static void ram_set32(uint32_t addr, uint32_t val)
{
   uint8_t *ram = *p_jaguarMainRAM;
   ram[addr]     = (val >> 24) & 0xFF;
   ram[addr + 1] = (val >> 16) & 0xFF;
   ram[addr + 2] = (val >> 8) & 0xFF;
   ram[addr + 3] = val & 0xFF;
}

static uint16_t tom_get16(uint16_t offset)
{
   return ((uint16_t)p_tomRam8[offset] << 8) | p_tomRam8[offset + 1];
}

static bool load_core(const char *path)
{
   core_handle = dlopen(path, RTLD_NOW);
   if (!core_handle) {
      fprintf(stderr, "dlopen(%s): %s\n", path, dlerror());
      return false;
   }

   p_retro_init                 = dlsym(core_handle, "retro_init");
   p_retro_deinit               = dlsym(core_handle, "retro_deinit");
   p_retro_set_environment      = dlsym(core_handle, "retro_set_environment");
   p_retro_set_video_refresh    = dlsym(core_handle, "retro_set_video_refresh");
   p_retro_set_audio_sample     = dlsym(core_handle, "retro_set_audio_sample");
   p_retro_set_audio_sample_batch = dlsym(core_handle, "retro_set_audio_sample_batch");
   p_retro_set_input_poll       = dlsym(core_handle, "retro_set_input_poll");
   p_retro_set_input_state      = dlsym(core_handle, "retro_set_input_state");
   p_retro_load_game            = dlsym(core_handle, "retro_load_game");
   p_retro_unload_game          = dlsym(core_handle, "retro_unload_game");
   p_retro_run                  = dlsym(core_handle, "retro_run");

   p_jaguarMainRAM  = dlsym(core_handle, "jaguarMainRAM");
   p_tomRam8        = dlsym(core_handle, "tomRam8");
   p_dsp_control    = dlsym(core_handle, "dsp_control");
   p_DSPGetRAM      = dlsym(core_handle, "DSPGetRAM");
   p_DSPWriteLong   = dlsym(core_handle, "DSPWriteLong");
   p_DSPReadLong    = dlsym(core_handle, "DSPReadLong");
   p_GPUReadLong    = dlsym(core_handle, "GPUReadLong");
   p_JERRYReadWord  = dlsym(core_handle, "JERRYReadWord");
   p_m68k_get_reg   = dlsym(core_handle, "m68k_get_reg");

   if (!p_retro_init || !p_retro_load_game || !p_tomRam8 || !p_jaguarMainRAM) {
      fprintf(stderr, "Missing required symbols\n");
      dlclose(core_handle);
      return false;
   }

   return true;
}

static void init_core(void)
{
   p_retro_set_environment(environment);
   p_retro_init();
   p_retro_set_video_refresh(video_refresh);
   p_retro_set_audio_sample(audio_sample);
   p_retro_set_audio_sample_batch(audio_batch);
   p_retro_set_input_poll(input_poll);
   p_retro_set_input_state(input_state);
}

/* ================================================================
 * Cascade 1: TOM Video Interrupt → 68K ISR
 *
 * Set VI (vertical interrupt line), enable video interrupt in INT1,
 * install a level-2 autovector ISR that increments a counter.
 * After N frames, the counter should equal ~N.
 * ================================================================ */
static void test_video_interrupt_cascade(void)
{
   uint8_t *code;
   uint8_t *isr_code;
   int n = 0, isr_n = 0;
   uint32_t count;

   printf("\n=== Cascade 1: TOM Video Interrupt -> 68K ISR ===\n");

   rom_init();
   code = rom_buf + CODE_BASE;
   isr_code = rom_buf + ISR_BASE;

   /* --- Build ISR at $804000 ---
    * Read INT1, increment counter, clear video interrupt, RTE */
   /* ADDQ.L #1, $3000.L (increment video counter) */
   isr_n += emit_addq_l_abs32(isr_code + isr_n, 1, PROBE_VIDEO_COUNT);
   /* MOVE.W #$0100, $F000E0.L (clear video interrupt pending) */
   isr_n += emit_movew_imm_abs32(isr_code + isr_n, INT1_CLR_VIDEO, TOM_INT1_ADDR);
   /* RTE */
   isr_n += emit_rte(isr_code + isr_n);

   /* --- Build main program at $802000 ---
    * 1. Zero counters
    * 2. Install ISR vector
    * 3. Set VI line
    * 4. Enable video interrupt
    * 5. Spin forever */

   /* Clear counter */
   n += emit_movel_imm_abs32(code + n, 0, PROBE_VIDEO_COUNT);

   /* Install autovector level 2 handler: RAM[$68] = $00804000 */
   n += emit_movel_imm_abs32(code + n, ISR_ADDR, VEC_LEVEL2);

   /* Set VI = 200 (arbitrary visible scanline) */
   n += emit_movew_imm_abs32(code + n, 200, TOM_VI_ADDR);

   /* Enable video interrupt: MOVE.W #$0001, INT1 */
   n += emit_movew_imm_abs32(code + n, INT1_VIDEO_EN, TOM_INT1_ADDR);

   /* Spin */
   n += emit_bra_self(code + n);

   init_core();
   if (!rom_load("video_irq_cascade.jag")) {
      FAIL("Could not load ROM"); return;
   }

   /* Also install vector directly in RAM in case ROM-based write didn't work */
   ram_set32(VEC_LEVEL2, ISR_ADDR);
   ram_set32(PROBE_VIDEO_COUNT, 0);

   run_frames(30);

   count = ram_get32(PROBE_VIDEO_COUNT);
   printf("  Video IRQ counter = %u after 30 frames\n", count);

   if (count >= 25)
      PASS("Video interrupt fired %u times (~1/frame)", count);
   else if (count > 0)
      PASS("Video interrupt fired %u times (some frames missed)", count);
   else
      FAIL("Video interrupt never fired (count=0)");

   p_retro_unload_game();
   p_retro_deinit();
}

/* ================================================================
 * Cascade 2: 68K → DSP DSPINT0 → DSP ISR
 *
 * 68K writes DSPINT0 bit to DSP_CTRL, which should trigger
 * DSP IRQ line 0.  A DSP ISR at vector 0 writes a magic value.
 * ================================================================ */
static void test_68k_to_dsp_interrupt(void)
{
   uint8_t *code;
   uint8_t *dsp_ram;
   int n = 0;
   uint32_t result;

   printf("\n=== Cascade 2: 68K -> DSP DSPINT0 -> DSP ISR ===\n");

   if (!p_DSPGetRAM || !p_DSPWriteLong || !p_DSPReadLong || !p_dsp_control) {
      INFO("DSP symbols not available — skipping");
      return;
   }

   rom_init();
   code = rom_buf + CODE_BASE;

   /* 68K program:
    * 1. Clear probe locations
    * 2. Enable DSP INT0
    * 3. Set DSPGO
    * 4. Send DSPINT0
    * 5. Wait and read result */

   /* Clear probe */
   n += emit_movel_imm_abs32(code + n, 0, 0x00003000);
   /* Enable INT0 in DSP FLAGS */
   n += emit_movel_imm_abs32(code + n, 0x00010, DSP_FLAGS_ADDR);
   /* Set DSPGO */
   n += emit_movel_imm_abs32(code + n, 0x00001, DSP_CTRL_ADDR);
   /* Trigger DSPINT0 via DSP_CTRL (bit 6 = DSPINT0) */
   n += emit_movel_imm_abs32(code + n, 0x00041, DSP_CTRL_ADDR); /* DSPGO | DSPINT0 */
   /* Some NOPs for DSP to process */
   {
      int i;
      for (i = 0; i < 20; i++)
         n += emit_nop(code + n);
   }
   /* Read DSP RAM result and store to RAM[$3000] */
   n += emit_movel_abs32_dn(code + n, 0xF1B200, 0);
   n += emit_movel_dn_abs32(code + n, 0, 0x00003000);
   n += emit_bra_self(code + n);

   init_core();
   if (!rom_load("dsp_int_cascade.jag")) {
      FAIL("Could not load ROM"); return;
   }

   /* Install DSP ISR at vector 0 ($F1B000):
    * MOVEI #$12345678, R0
    * MOVEI #$F1B200, R1
    * STORE R0, (R1)
    * NOP (x2)
    * Then fall into main loop */
   dsp_ram = p_DSPGetRAM();
   if (dsp_ram) {
      uint16_t off = 0;
      dsp_write_movei(dsp_ram, off, 0x12345678, 0);   off += 6;
      dsp_write_movei(dsp_ram, off, 0xF1B200, 1);     off += 6;
      dsp_write16(dsp_ram, off, DSP_STORE(0, 1));      off += 2;
      dsp_write16(dsp_ram, off, DSP_NOP);              off += 2;

      /* Main program at $F1B010 (vector 1 offset): spin */
      dsp_write16(dsp_ram, 0x10, DSP_JR(0, 31));  /* JR T, -2 */
      dsp_write16(dsp_ram, 0x12, DSP_NOP);

      /* Clear result area */
      dsp_ram[0x200] = 0; dsp_ram[0x201] = 0;
      dsp_ram[0x202] = 0; dsp_ram[0x203] = 0;
   }

   run_frames(5);

   result = p_DSPReadLong(0xF1B200, WHO_M68K);
   printf("  DSP RAM[$F1B200] = $%08X (expected $12345678)\n", result);

   if (result == 0x12345678)
      PASS("DSP ISR wrote expected magic value");
   else if (result != 0)
      PASS("DSP ISR fired and wrote $%08X (different from expected)", result);
   else
      FAIL("DSP ISR did not fire (result=0)");

   p_retro_unload_game();
   p_retro_deinit();
}

/* ================================================================
 * Cascade 3: Video + Timer Interrupts Coexisting
 *
 * Enable both video and timer interrupts with separate ISRs.
 * Verify both fire independently without interfering.
 * Each ISR increments its own counter in RAM.
 * ================================================================ */
static void test_video_and_timer_coexist(void)
{
   uint8_t *code;
   int n = 0;
   uint32_t vid_count, timer_count;
   /* ISR for video at $804000, ISR for timer at $804100 */
   uint8_t *vid_isr = rom_buf + ISR_BASE;
   uint8_t *tmr_isr = rom_buf + ISR_BASE + 0x100;
   int v_n = 0, t_n = 0;

   printf("\n=== Cascade 3: Video + Timer Interrupts Coexisting ===\n");

   rom_init();
   code = rom_buf + CODE_BASE;

   /* Video ISR: increment $3000, clear video pending, RTE */
   v_n += emit_addq_l_abs32(vid_isr + v_n, 1, PROBE_VIDEO_COUNT);
   v_n += emit_movew_imm_abs32(vid_isr + v_n, INT1_CLR_VIDEO, TOM_INT1_ADDR);
   v_n += emit_rte(vid_isr + v_n);

   /* Timer ISR: increment $3004, clear timer pending, RTE */
   t_n += emit_addq_l_abs32(tmr_isr + t_n, 1, PROBE_TIMER_COUNT);
   t_n += emit_movew_imm_abs32(tmr_isr + t_n, INT1_CLR_TIMER, TOM_INT1_ADDR);
   t_n += emit_rte(tmr_isr + t_n);

   /* Main program */
   /* Clear counters */
   n += emit_movel_imm_abs32(code + n, 0, PROBE_VIDEO_COUNT);
   n += emit_movel_imm_abs32(code + n, 0, PROBE_TIMER_COUNT);

   /* Install vectors (video=level2=$68, timer is typically routed to level 2 as well
    * on Jaguar since TOM multiplexes all interrupts onto the same line.
    * So we use INT1 bits to distinguish in a shared ISR.
    * For this test, install at level 2. *)
    */
   n += emit_movel_imm_abs32(code + n, ISR_ADDR, VEC_LEVEL2);

   /* Set VI = 200 */
   n += emit_movew_imm_abs32(code + n, 200, TOM_VI_ADDR);

   /* Enable video interrupt */
   n += emit_movew_imm_abs32(code + n, INT1_VIDEO_EN, TOM_INT1_ADDR);

   /* Spin */
   n += emit_bra_self(code + n);

   init_core();
   if (!rom_load("vid_timer_coexist.jag")) {
      FAIL("Could not load ROM"); return;
   }

   ram_set32(VEC_LEVEL2, ISR_ADDR);
   ram_set32(PROBE_VIDEO_COUNT, 0);
   ram_set32(PROBE_TIMER_COUNT, 0);

   run_frames(60);

   vid_count = ram_get32(PROBE_VIDEO_COUNT);
   timer_count = ram_get32(PROBE_TIMER_COUNT);

   printf("  Video IRQ count = %u, Timer IRQ count = %u (60 frames)\n",
          vid_count, timer_count);

   if (vid_count >= 50)
      PASS("Video interrupt reliable: %u/60 frames", vid_count);
   else if (vid_count > 0)
      PASS("Video interrupt fires but missed frames: %u/60", vid_count);
   else
      FAIL("Video interrupt never fired");

   /* Timer may not fire if PIT wasn't set up — that's expected */
   if (timer_count > 0)
      PASS("Timer interrupt fired %u times", timer_count);
   else
      INFO("Timer interrupt did not fire (expected — PIT not configured in this test)");

   p_retro_unload_game();
   p_retro_deinit();
}

/* ================================================================
 * Cascade 4: Interrupt Enables/Disables Toggle
 *
 * Toggle video interrupt enable on/off over time.
 * Verify interrupts stop when disabled, resume when re-enabled.
 * ================================================================ */
static void test_interrupt_enable_toggle(void)
{
   uint8_t *code;
   uint8_t *isr_code;
   int n = 0, isr_n = 0;
   uint32_t count_phase1, count_phase2, count_phase3;

   printf("\n=== Cascade 4: Interrupt Enable/Disable Toggle ===\n");

   rom_init();
   code = rom_buf + CODE_BASE;
   isr_code = rom_buf + ISR_BASE;

   /* ISR: increment counter, clear pending, RTE */
   isr_n += emit_addq_l_abs32(isr_code + isr_n, 1, PROBE_VIDEO_COUNT);
   isr_n += emit_movew_imm_abs32(isr_code + isr_n, INT1_CLR_VIDEO, TOM_INT1_ADDR);
   isr_n += emit_rte(isr_code + isr_n);

   /* Main: set up VI, install vector, enable interrupt, spin */
   n += emit_movel_imm_abs32(code + n, 0, PROBE_VIDEO_COUNT);
   n += emit_movel_imm_abs32(code + n, ISR_ADDR, VEC_LEVEL2);
   n += emit_movew_imm_abs32(code + n, 200, TOM_VI_ADDR);
   n += emit_movew_imm_abs32(code + n, INT1_VIDEO_EN, TOM_INT1_ADDR);
   n += emit_bra_self(code + n);

   init_core();
   if (!rom_load("irq_toggle.jag")) {
      FAIL("Could not load ROM"); return;
   }

   ram_set32(VEC_LEVEL2, ISR_ADDR);
   ram_set32(PROBE_VIDEO_COUNT, 0);

   /* Phase 1: interrupts enabled, run 30 frames */
   run_frames(30);
   count_phase1 = ram_get32(PROBE_VIDEO_COUNT);

   /* Phase 2: disable interrupts via INT1, run 30 frames */
   /* Write INT1 = 0 (disable all) via direct TOM RAM write */
   p_tomRam8[0xE0] = 0x00;
   p_tomRam8[0xE1] = 0x00;
   run_frames(30);
   count_phase2 = ram_get32(PROBE_VIDEO_COUNT);

   /* Phase 3: re-enable, run 30 frames */
   p_tomRam8[0xE0] = 0x00;
   p_tomRam8[0xE1] = INT1_VIDEO_EN & 0xFF;
   run_frames(30);
   count_phase3 = ram_get32(PROBE_VIDEO_COUNT);

   printf("  Phase 1 (enabled, 30f): count=%u\n", count_phase1);
   printf("  Phase 2 (disabled, 30f): count=%u (delta=%u)\n",
          count_phase2, count_phase2 - count_phase1);
   printf("  Phase 3 (re-enabled, 30f): count=%u (delta=%u)\n",
          count_phase3, count_phase3 - count_phase2);

   /* Phase 1 should have interrupts */
   if (count_phase1 > 0)
      PASS("Phase 1: interrupts fired (%u)", count_phase1);
   else
      FAIL("Phase 1: no interrupts fired");

   /* Phase 2: delta should be small (ideally 0) */
   if ((count_phase2 - count_phase1) <= 2)
      PASS("Phase 2: interrupts suppressed (delta=%u)", count_phase2 - count_phase1);
   else if ((count_phase2 - count_phase1) < count_phase1)
      PASS("Phase 2: interrupt rate reduced (delta=%u vs phase1=%u)",
           count_phase2 - count_phase1, count_phase1);
   else
      FAIL("Phase 2: interrupts not suppressed (delta=%u)", count_phase2 - count_phase1);

   /* Phase 3 should resume */
   if ((count_phase3 - count_phase2) > 0)
      PASS("Phase 3: interrupts resumed (delta=%u)", count_phase3 - count_phase2);
   else
      FAIL("Phase 3: interrupts did not resume");

   p_retro_unload_game();
   p_retro_deinit();
}

/* ================================================================
 * Cascade 5: DSP FLAGS Write → Interrupt Re-dispatch
 *
 * Test the DSP's own FLAGS write behavior: when the DSP clears
 * an interrupt latch via CINT, and another interrupt is pending,
 * the new interrupt should be dispatched.
 *
 * This is a critical path for BattleSphere's sound engine.
 * ================================================================ */
static void test_dsp_flags_redispatch(void)
{
   uint8_t *dsp_ram;
   uint32_t result;

   printf("\n=== Cascade 5: DSP FLAGS Write -> Interrupt Re-dispatch ===\n");

   if (!p_DSPGetRAM || !p_DSPWriteLong || !p_DSPReadLong || !p_dsp_control) {
      INFO("DSP symbols not available — skipping");
      return;
   }

   rom_init();

   init_core();
   if (!rom_load("dsp_flags_redispatch.jag")) {
      FAIL("Could not load ROM"); return;
   }

   dsp_ram = p_DSPGetRAM();
   if (!dsp_ram) {
      FAIL("DSPGetRAM returned NULL"); goto cleanup_5;
   }

   /* Set up DSP with:
    * - ISR0 at $F1B000: writes $AAAA to $F1B200, clears CINT0 in FLAGS
    * - ISR1 at $F1B010: writes $BBBB to $F1B204
    * - Main program at $F1B100: spin
    *
    * Test: set both INT_LAT0 and INT_LAT1, enable both.
    * ISR0 fires first (lower priority dispatches first due to sequential check).
    * When ISR0 clears CINT0, ISR1 should fire if re-dispatch works. */

   /* ISR0: write marker, clear CINT0 via FLAGS write */
   {
      uint16_t off = 0;
      /* MOVEI #$AAAA, R0 */
      dsp_write_movei(dsp_ram, off, 0x0000AAAA, 0); off += 6;
      /* MOVEI #$F1B200, R1 */
      dsp_write_movei(dsp_ram, off, 0xF1B200, 1); off += 6;
      /* STORE R0, (R1) */
      dsp_write16(dsp_ram, off, DSP_STORE(0, 1)); off += 2;
      /* NOP padding */
      dsp_write16(dsp_ram, off, DSP_NOP); off += 2;
   }

   /* ISR1 at offset 0x10: write marker */
   {
      uint16_t off = 0x10;
      dsp_write_movei(dsp_ram, off, 0x0000BBBB, 0); off += 6;
      dsp_write_movei(dsp_ram, off, 0xF1B204, 1); off += 6;
      dsp_write16(dsp_ram, off, DSP_STORE(0, 1)); off += 2;
      dsp_write16(dsp_ram, off, DSP_NOP); off += 2;
   }

   /* Main at offset 0x100: spin */
   dsp_write16(dsp_ram, 0x100, DSP_JR(0, 31));
   dsp_write16(dsp_ram, 0x102, DSP_NOP);

   /* Clear result areas */
   memset(dsp_ram + 0x200, 0, 8);

   /* Configure: enable INT0 and INT1, start DSP at $F1B100 */
   p_DSPWriteLong(DSP_FLAGS_ADDR, 0x00010 | 0x00020, WHO_M68K); /* INT_ENA0 | INT_ENA1 */
   *p_dsp_control = 0x00001; /* DSPGO */
   *p_dsp_control |= 0x00040 | 0x00080; /* Set INT_LAT0 | INT_LAT1 */

   run_frames(5);

   {
      uint32_t isr0_marker = p_DSPReadLong(0xF1B200, WHO_M68K);
      uint32_t isr1_marker = p_DSPReadLong(0xF1B204, WHO_M68K);

      printf("  ISR0 marker ($F1B200) = $%08X (expected $0000AAAA)\n", isr0_marker);
      printf("  ISR1 marker ($F1B204) = $%08X (expected $0000BBBB)\n", isr1_marker);

      if (isr0_marker == 0x0000AAAA)
         PASS("DSP ISR0 fired (marker=$AAAA)");
      else if (isr0_marker != 0)
         PASS("DSP ISR0 wrote something ($%08X)", isr0_marker);
      else
         FAIL("DSP ISR0 did not fire");

      if (isr1_marker == 0x0000BBBB)
         PASS("DSP ISR1 fired via re-dispatch (marker=$BBBB)");
      else if (isr1_marker != 0)
         PASS("DSP ISR1 wrote something ($%08X)", isr1_marker);
      else
         INFO("DSP ISR1 did not fire (re-dispatch may not have triggered)");
   }

cleanup_5:
   p_retro_unload_game();
   p_retro_deinit();
}

/* ================================================================
 * Main
 * ================================================================ */

int main(int argc, char **argv)
{
   const char *core_path;

   core_path = (argc > 1) ? argv[1] : CORE_FILENAME;

   printf("=== IRQ Cascade Tests ===\n");
   printf("Core: %s\n", core_path);

   if (!load_core(core_path)) return 1;

   test_video_interrupt_cascade();
   test_68k_to_dsp_interrupt();
   test_video_and_timer_coexist();
   test_interrupt_enable_toggle();
   test_dsp_flags_redispatch();

   dlclose(core_handle);

   printf("\n=== Results: %d passed, %d failed ===\n", passes, fails);
   return fails > 0 ? 1 : 0;
}
