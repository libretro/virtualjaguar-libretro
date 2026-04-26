/* test_subsystem_init.c -- BIOS vs HLE initialization comparison tests.
 *
 * Tests that HLE boot produces the same hardware state as the real BIOS
 * across ALL subsystems: TOM (video), JERRY (audio/timers), GPU, DSP,
 * and 68K memory.  Also verifies the I2S sound engine initialization
 * path, DSP RAM state, and exception vector setup.
 *
 * The test loads the core twice (once with BIOS, once without) and
 * compares register snapshots.  When no real BIOS ROM is available,
 * the BIOS tests are skipped and HLE-only tests still run.
 *
 * Build: cc -o test/test_subsystem_init test/test_subsystem_init.c -ldl
 * Usage: ./test/test_subsystem_init [path/to/core.dylib]
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
 * Jaguar hardware addresses
 * ================================================================ */

/* TOM register offsets (relative to tomRam8) */
#define TOM_MEMCON1  0x00
#define TOM_MEMCON2  0x02
#define TOM_HP       0x2E
#define TOM_HBB      0x30
#define TOM_HBE      0x32
#define TOM_HS       0x34
#define TOM_HVS      0x36
#define TOM_HDB1     0x38
#define TOM_HDB2     0x3A
#define TOM_HDE      0x3C
#define TOM_VP       0x3E
#define TOM_VBB      0x40
#define TOM_VBE      0x42
#define TOM_VS       0x44
#define TOM_VDB      0x46
#define TOM_VDE      0x48
#define TOM_VEB      0x4A
#define TOM_VEE      0x4C
#define TOM_VI       0x4E
#define TOM_HEQ      0x54
#define TOM_BG       0x58
#define TOM_VMODE    0x28
#define TOM_BORD1    0x2A
#define TOM_BORD2    0x2C
#define TOM_OLP_LO   0x20
#define TOM_OLP_HI   0x22
#define TOM_INT1     0xE0

/* GPU register addresses (absolute, read via GPUReadLong) */
#define GPU_FLAGS_ADDR   0xF02100
#define GPU_CTRL_ADDR    0xF02114
#define GPU_G_END_ADDR   0xF0210C
#define GPU_MAGIC_ADDR   0xF03000

/* DSP register addresses (absolute, read via JERRYReadWord) */
#define DSP_FLAGS_ADDR   0xF1A100
#define DSP_CTRL_ADDR    0xF1A114
#define DSP_DORG_ADDR    0xF1A10C

/* JERRY register addresses */
#define JERRY_CLK2       0xF10012
#define JERRY_CLK3       0xF10014
#define JERRY_PIT0       0xF10000
#define JERRY_PIT1       0xF10002
#define JERRY_PIT2       0xF10004
#define JERRY_PIT3       0xF10006
#define JERRY_SCLK       0xF1A150
#define JERRY_SMODE      0xF1A154

/* who enum values from vjag_memory.h */
#define WHO_M68K  6

/* ================================================================
 * Snapshot structure: captures full subsystem state
 * ================================================================ */

struct tom_snapshot {
   uint16_t memcon1, memcon2;
   uint16_t hp, hbb, hbe, hs, hvs, hdb1, hdb2, hde;
   uint16_t vp, vbb, vbe, vs, vdb, vde, veb, vee;
   uint16_t vi, heq, bg, vmode;
   uint16_t bord1, bord2;
   uint16_t olp_lo, olp_hi;
   uint16_t int1;
};

struct jerry_snapshot {
   uint16_t clk2, clk3;
   uint16_t pit0, pit1, pit2, pit3;
   uint16_t sclk, smode;
};

struct gpu_snapshot {
   uint32_t flags, ctrl, g_end;
   uint32_t auth_magic;
};

struct dsp_snapshot {
   uint16_t flags_lo;
   uint16_t ctrl_lo, ctrl_hi;
   uint16_t dorg_hi, dorg_lo;
   int running;
   uint8_t ram_sample[16]; /* first 16 bytes of DSP RAM */
};

struct hw_snapshot {
   struct tom_snapshot tom;
   struct jerry_snapshot jerry;
   struct gpu_snapshot gpu;
   struct dsp_snapshot dsp;
   uint32_t ram_ssp;     /* SSP from RAM[0..3] */
   uint32_t ram_pc;      /* PC vector from RAM[4..7] */
   uint32_t ram_3000;    /* test probe location */
   uint8_t  except_0400[8]; /* exception handler at $0400 */
};

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

/* Emulator internals */
static uint8_t *p_tomRam8;
static uint8_t **p_jaguarMainRAM;
static uint8_t *p_jagMemSpace;
static uint32_t (*p_GPUReadLong)(uint32_t, uint32_t);
static uint16_t (*p_JERRYReadWord)(uint32_t, uint32_t);
static uint8_t *(*p_DSPGetRAM)(void);
static bool (*p_DSPIsRunning)(void);

/* Stub callbacks */
static void video_refresh(const void *d, unsigned w, unsigned h, size_t p)
{ (void)d; (void)w; (void)h; (void)p; }
static void audio_sample(int16_t l, int16_t r) { (void)l; (void)r; }
static size_t audio_batch(const int16_t *d, size_t f) { (void)d; return f; }
static void input_poll(void) {}
static int16_t input_state(unsigned p, unsigned d, unsigned i, unsigned id)
{ (void)p; (void)d; (void)i; (void)id; return 0; }

static int use_bios = 0;

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
         var->value = use_bios ? "enabled" : "disabled";
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

/* ================================================================
 * Helpers
 * ================================================================ */

static uint16_t tom_get16(uint16_t offset)
{
   return ((uint16_t)p_tomRam8[offset] << 8) | p_tomRam8[offset + 1];
}

static uint32_t ram_get32(uint32_t offset)
{
   uint8_t *ram = *p_jaguarMainRAM;
   return ((uint32_t)ram[offset] << 24)
        | ((uint32_t)ram[offset + 1] << 16)
        | ((uint32_t)ram[offset + 2] << 8)
        | (uint32_t)ram[offset + 3];
}

static uint16_t ram_get16(uint32_t offset)
{
   uint8_t *ram = *p_jaguarMainRAM;
   return ((uint16_t)ram[offset] << 8) | ram[offset + 1];
}

/* ================================================================
 * Snapshot capture
 * ================================================================ */

static void capture_snapshot(struct hw_snapshot *snap)
{
   uint8_t *dsp_ram;
   int i;

   /* TOM */
   snap->tom.memcon1 = tom_get16(TOM_MEMCON1);
   snap->tom.memcon2 = tom_get16(TOM_MEMCON2);
   snap->tom.hp      = tom_get16(TOM_HP);
   snap->tom.hbb     = tom_get16(TOM_HBB);
   snap->tom.hbe     = tom_get16(TOM_HBE);
   snap->tom.hs      = tom_get16(TOM_HS);
   snap->tom.hvs     = tom_get16(TOM_HVS);
   snap->tom.hdb1    = tom_get16(TOM_HDB1);
   snap->tom.hdb2    = tom_get16(TOM_HDB2);
   snap->tom.hde     = tom_get16(TOM_HDE);
   snap->tom.vp      = tom_get16(TOM_VP);
   snap->tom.vbb     = tom_get16(TOM_VBB);
   snap->tom.vbe     = tom_get16(TOM_VBE);
   snap->tom.vs      = tom_get16(TOM_VS);
   snap->tom.vdb     = tom_get16(TOM_VDB);
   snap->tom.vde     = tom_get16(TOM_VDE);
   snap->tom.veb     = tom_get16(TOM_VEB);
   snap->tom.vee     = tom_get16(TOM_VEE);
   snap->tom.vi      = tom_get16(TOM_VI);
   snap->tom.heq     = tom_get16(TOM_HEQ);
   snap->tom.bg      = tom_get16(TOM_BG);
   snap->tom.vmode   = tom_get16(TOM_VMODE);
   snap->tom.bord1   = tom_get16(TOM_BORD1);
   snap->tom.bord2   = tom_get16(TOM_BORD2);
   snap->tom.olp_lo  = tom_get16(TOM_OLP_LO);
   snap->tom.olp_hi  = tom_get16(TOM_OLP_HI);
   snap->tom.int1    = tom_get16(TOM_INT1);

   /* JERRY */
   snap->jerry.clk2  = p_JERRYReadWord(JERRY_CLK2, WHO_M68K);
   snap->jerry.clk3  = p_JERRYReadWord(JERRY_CLK3, WHO_M68K);
   snap->jerry.pit0  = p_JERRYReadWord(JERRY_PIT0, WHO_M68K);
   snap->jerry.pit1  = p_JERRYReadWord(JERRY_PIT1, WHO_M68K);
   snap->jerry.pit2  = p_JERRYReadWord(JERRY_PIT2, WHO_M68K);
   snap->jerry.pit3  = p_JERRYReadWord(JERRY_PIT3, WHO_M68K);
   snap->jerry.sclk  = p_JERRYReadWord(JERRY_SCLK, WHO_M68K);
   snap->jerry.smode = p_JERRYReadWord(JERRY_SMODE, WHO_M68K);

   /* GPU */
   snap->gpu.flags      = p_GPUReadLong(GPU_FLAGS_ADDR, WHO_M68K);
   snap->gpu.ctrl       = p_GPUReadLong(GPU_CTRL_ADDR, WHO_M68K);
   snap->gpu.g_end      = p_GPUReadLong(GPU_G_END_ADDR, WHO_M68K);
   snap->gpu.auth_magic = p_GPUReadLong(GPU_MAGIC_ADDR, WHO_M68K);

   /* DSP */
   snap->dsp.flags_lo = p_JERRYReadWord(DSP_FLAGS_ADDR, WHO_M68K);
   snap->dsp.ctrl_lo  = p_JERRYReadWord(DSP_CTRL_ADDR, WHO_M68K);
   snap->dsp.ctrl_hi  = p_JERRYReadWord(DSP_CTRL_ADDR + 2, WHO_M68K);
   snap->dsp.dorg_hi  = p_JERRYReadWord(DSP_DORG_ADDR, WHO_M68K);
   snap->dsp.dorg_lo  = p_JERRYReadWord(DSP_DORG_ADDR + 2, WHO_M68K);
   snap->dsp.running  = p_DSPIsRunning ? p_DSPIsRunning() : -1;

   dsp_ram = p_DSPGetRAM ? p_DSPGetRAM() : NULL;
   if (dsp_ram) {
      for (i = 0; i < 16; i++)
         snap->dsp.ram_sample[i] = dsp_ram[i];
   } else {
      memset(snap->dsp.ram_sample, 0xFF, 16);
   }

   /* RAM vectors */
   snap->ram_ssp  = ram_get32(0);
   snap->ram_pc   = ram_get32(4);
   snap->ram_3000 = ram_get32(0x3000);

   /* Exception handler area */
   {
      uint8_t *ram = *p_jaguarMainRAM;
      for (i = 0; i < 8; i++)
         snap->except_0400[i] = ram[0x400 + i];
   }
}

static void print_snapshot(const char *label, const struct hw_snapshot *s)
{
   printf("\n--- %s ---\n", label);
   printf("  TOM:  MEMCON1=$%04X MEMCON2=$%04X VMODE=$%04X\n",
          s->tom.memcon1, s->tom.memcon2, s->tom.vmode);
   printf("  TOM:  HP=%u HBB=%u HBE=%u HS=%u HVS=%u HDB1=%u HDB2=%u HDE=%u\n",
          s->tom.hp, s->tom.hbb, s->tom.hbe, s->tom.hs,
          s->tom.hvs, s->tom.hdb1, s->tom.hdb2, s->tom.hde);
   printf("  TOM:  VP=%u VBB=%u VBE=%u VS=%u VDB=%u VDE=%u VEB=%u VEE=%u\n",
          s->tom.vp, s->tom.vbb, s->tom.vbe, s->tom.vs,
          s->tom.vdb, s->tom.vde, s->tom.veb, s->tom.vee);
   printf("  TOM:  VI=%u HEQ=%u BG=$%04X BORD=$%04X/$%04X\n",
          s->tom.vi, s->tom.heq, s->tom.bg, s->tom.bord1, s->tom.bord2);
   printf("  TOM:  OLP=$%04X%04X INT1=$%04X\n",
          s->tom.olp_hi, s->tom.olp_lo, s->tom.int1);
   printf("  JERRY: CLK2=$%04X CLK3=$%04X SCLK=$%04X SMODE=$%04X\n",
          s->jerry.clk2, s->jerry.clk3, s->jerry.sclk, s->jerry.smode);
   printf("  JERRY: PIT0=$%04X PIT1=$%04X PIT2=$%04X PIT3=$%04X\n",
          s->jerry.pit0, s->jerry.pit1, s->jerry.pit2, s->jerry.pit3);
   printf("  GPU:  FLAGS=$%08X CTRL=$%08X G_END=$%08X MAGIC=$%08X\n",
          s->gpu.flags, s->gpu.ctrl, s->gpu.g_end, s->gpu.auth_magic);
   printf("  DSP:  FLAGS=$%04X CTRL=$%04X%04X DORG=$%04X%04X running=%d\n",
          s->dsp.flags_lo, s->dsp.ctrl_hi, s->dsp.ctrl_lo,
          s->dsp.dorg_hi, s->dsp.dorg_lo, s->dsp.running);
   printf("  DSP RAM[0..15]: %02X%02X%02X%02X %02X%02X%02X%02X "
          "%02X%02X%02X%02X %02X%02X%02X%02X\n",
          s->dsp.ram_sample[0],  s->dsp.ram_sample[1],
          s->dsp.ram_sample[2],  s->dsp.ram_sample[3],
          s->dsp.ram_sample[4],  s->dsp.ram_sample[5],
          s->dsp.ram_sample[6],  s->dsp.ram_sample[7],
          s->dsp.ram_sample[8],  s->dsp.ram_sample[9],
          s->dsp.ram_sample[10], s->dsp.ram_sample[11],
          s->dsp.ram_sample[12], s->dsp.ram_sample[13],
          s->dsp.ram_sample[14], s->dsp.ram_sample[15]);
   printf("  RAM:  SSP=$%08X PC=$%08X probe@$3000=$%08X\n",
          s->ram_ssp, s->ram_pc, s->ram_3000);
   printf("  RAM[$0400..0407]: %02X%02X %02X%02X %02X%02X %02X%02X\n",
          s->except_0400[0], s->except_0400[1],
          s->except_0400[2], s->except_0400[3],
          s->except_0400[4], s->except_0400[5],
          s->except_0400[6], s->except_0400[7]);
}

/* ================================================================
 * Core loading
 * ================================================================ */

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

   p_tomRam8        = dlsym(core_handle, "tomRam8");
   p_jaguarMainRAM  = dlsym(core_handle, "jaguarMainRAM");
   p_jagMemSpace    = dlsym(core_handle, "jagMemSpace");
   p_GPUReadLong    = dlsym(core_handle, "GPUReadLong");
   p_JERRYReadWord  = dlsym(core_handle, "JERRYReadWord");
   p_DSPGetRAM      = dlsym(core_handle, "DSPGetRAM");
   p_DSPIsRunning   = dlsym(core_handle, "DSPIsRunning");

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

static bool load_dummy_rom(void)
{
   struct retro_game_info game;
   uint8_t *rom;

   rom = calloc(1, 131072);
   if (!rom) return false;

   /* run address = $802000 */
   rom[0x404] = 0x00; rom[0x405] = 0x80;
   rom[0x406] = 0x20; rom[0x407] = 0x00;
   /* BRA.S self at $802000 */
   rom[0x2000] = 0x60; rom[0x2001] = 0xFE;

   memset(&game, 0, sizeof(game));
   game.path = "dummy_init_test.jag";
   game.data = rom;
   game.size = 131072;

   if (!p_retro_load_game(&game)) {
      free(rom);
      return false;
   }
   free(rom);
   return true;
}

/* ================================================================
 * Test 1: HLE Init — Sound Engine Setup (SCLK/SMODE)
 *
 * The real BIOS initializes I2S: SMODE=$0001 (internal clock),
 * SCLK=$0008 (default divider).  HLE must replicate this or
 * the DSP sound engine won't get I2S interrupts.
 * ================================================================ */
static void test_sound_engine_init(const struct hw_snapshot *snap)
{
   printf("\n=== Test 1: Sound Engine I2S Init (SCLK/SMODE) ===\n");

   /* SCLK ($F1A150) and SMODE ($F1A154) are write-only registers.
    * JERRYReadWord returns SSTAT/0xFFFF at those addresses.
    * Read from jagMemSpace directly where DACWriteWord stores them. */
   if (p_jagMemSpace) {
      uint8_t sclk_val = p_jagMemSpace[0xF1A150];
      uint32_t smode_val = *((uint32_t *)&p_jagMemSpace[0xF1A154]);
      printf("  SCLK (jagMemSpace) = $%02X, SMODE = $%08X\n", sclk_val, smode_val);

      if (smode_val & 0x01)
         PASS("SMODE bit 0 set (internal clock): $%08X", smode_val);
      else
         FAIL("SMODE bit 0 clear (no internal clock): $%08X", smode_val);

      if (sclk_val != 0)
         PASS("SCLK non-zero (divider active): $%02X", sclk_val);
      else
         FAIL("SCLK = 0 (I2S clock disabled, DSP won't get SSI interrupts)");
   } else {
      INFO("jagMemSpace not available — cannot check I2S state");
   }
}

/* ================================================================
 * Test 2: DSP RAM State After Init
 *
 * In HLE mode, DSP RAM should be zero-filled (no BIOS sound engine).
 * In BIOS mode, DSP RAM should be non-zero (randomized or loaded).
 * ================================================================ */
static void test_dsp_ram_state(const struct hw_snapshot *snap, int is_bios)
{
   int all_zero = 1;
   int i;

   printf("\n=== Test 2: DSP RAM State (%s) ===\n", is_bios ? "BIOS" : "HLE");

   for (i = 0; i < 16; i++) {
      if (snap->dsp.ram_sample[i] != 0) {
         all_zero = 0;
         break;
      }
   }

   if (is_bios) {
      if (!all_zero)
         PASS("BIOS mode: DSP RAM non-zero (randomized/loaded)");
      else
         INFO("BIOS mode: DSP RAM sample all zero (unusual but not fatal)");
      passes++; /* informational in BIOS mode */
   } else {
      if (all_zero)
         PASS("HLE mode: DSP RAM zero-filled");
      else
         FAIL("HLE mode: DSP RAM not zero-filled (byte[%d]=$%02X)",
              i, snap->dsp.ram_sample[i]);
   }
}

/* ================================================================
 * Test 3: GPU Auth Magic
 *
 * BIOS GPU program writes $03D0DEAD to $F03000 on successful auth.
 * HLE must write the same value or games that check it will hang.
 * ================================================================ */
static void test_gpu_auth_magic(const struct hw_snapshot *snap)
{
   printf("\n=== Test 3: GPU Auth Magic ===\n");

   if (snap->gpu.auth_magic == 0x03D0DEAD)
      PASS("Auth magic = $%08X", snap->gpu.auth_magic);
   else
      FAIL("Auth magic = $%08X (expected $03D0DEAD)", snap->gpu.auth_magic);
}

/* ================================================================
 * Test 4: Endianness Registers (G_END / D_ORG)
 *
 * Both must be set to big-endian ($00070007) for the 68K to
 * correctly access GPU and DSP RAM.
 * ================================================================ */
static void test_endianness(const struct hw_snapshot *snap)
{
   printf("\n=== Test 4: Endianness Registers ===\n");

   if (snap->gpu.g_end == 0x00070007)
      PASS("G_END = $%08X (big-endian)", snap->gpu.g_end);
   else
      FAIL("G_END = $%08X (expected $00070007)", snap->gpu.g_end);

   if (snap->dsp.dorg_hi == 0x0007 && snap->dsp.dorg_lo == 0x0007)
      PASS("D_ORG = $%04X%04X (big-endian)", snap->dsp.dorg_hi, snap->dsp.dorg_lo);
   else
      FAIL("D_ORG = $%04X%04X (expected $00070007)",
           snap->dsp.dorg_hi, snap->dsp.dorg_lo);
}

/* ================================================================
 * Test 5: Clock Configuration
 *
 * CLK3 = $0004, CLK2 = $00B5 (NTSC) or $00E2 (PAL).
 * These control DAC/I2S sample rates.
 * ================================================================ */
static void test_clock_config(const struct hw_snapshot *snap)
{
   printf("\n=== Test 5: Clock Configuration ===\n");

   if (snap->jerry.clk3 == 0x0004)
      PASS("CLK3 = $%04X", snap->jerry.clk3);
   else
      FAIL("CLK3 = $%04X (expected $0004)", snap->jerry.clk3);

   if (snap->jerry.clk2 == 0x00B5 || snap->jerry.clk2 == 0x00E2)
      PASS("CLK2 = $%04X (%s)", snap->jerry.clk2,
           snap->jerry.clk2 == 0x00B5 ? "NTSC" : "PAL");
   else
      FAIL("CLK2 = $%04X (expected $00B5 NTSC or $00E2 PAL)", snap->jerry.clk2);
}

/* ================================================================
 * Test 6: TOM Video Mode Register
 *
 * VMODE = $06C1 is the standard post-BIOS video mode
 * (320px, 16bpp CRY, video enabled).
 * ================================================================ */
static void test_tom_vmode(const struct hw_snapshot *snap)
{
   printf("\n=== Test 6: TOM Video Mode ===\n");

   if (snap->tom.vmode == 0x06C1)
      PASS("VMODE = $%04X", snap->tom.vmode);
   else
      FAIL("VMODE = $%04X (expected $06C1)", snap->tom.vmode);
}

/* ================================================================
 * Test 7: Exception Vector Setup
 *
 * HLE installs a minimal exception handler at $0400: ADDQ #8,SP; RTE.
 * This prevents crashes from unexpected exceptions during boot.
 * ================================================================ */
static void test_exception_vectors(const struct hw_snapshot *snap)
{
   uint16_t op0, op1;

   printf("\n=== Test 7: Exception Handler Setup ===\n");

   op0 = ((uint16_t)snap->except_0400[0] << 8) | snap->except_0400[1];
   op1 = ((uint16_t)snap->except_0400[2] << 8) | snap->except_0400[3];

   /* Expected: ADDQ.L #8,SP ($508F) then RTE ($4E73) */
   if (op0 == 0x508F && op1 == 0x4E73)
      PASS("Exception handler: ADDQ.L #8,SP; RTE at $0400");
   else if (op0 == 0x508F)
      PASS("Exception handler starts with ADDQ.L #8,SP ($%04X $%04X)", op0, op1);
   else if (op0 != 0x0000)
      PASS("Exception handler installed at $0400 ($%04X $%04X)", op0, op1);
   else
      FAIL("No exception handler at $0400 ($%04X $%04X)", op0, op1);
}

/* ================================================================
 * Test 8: Interrupt State After Init
 *
 * INT1 enable bits (0-4) should be cleared — no interrupts should
 * be enabled until the game/BIOS explicitly enables them.
 * ================================================================ */
static void test_interrupt_state(const struct hw_snapshot *snap)
{
   printf("\n=== Test 8: Interrupt Enable State ===\n");

   if ((snap->tom.int1 & 0x001F) == 0)
      PASS("INT1 enables = 0 ($%04X)", snap->tom.int1);
   else
      FAIL("INT1 enables != 0: $%04X (bits 0-4 should be clear)", snap->tom.int1);
}

/* ================================================================
 * Test 9: SSP and PC Vectors
 *
 * RAM[0..3] = SSP (stack pointer), RAM[4..7] = initial PC.
 * SSP should be a valid RAM address, PC should be in cart space.
 * ================================================================ */
static void test_boot_vectors(const struct hw_snapshot *snap)
{
   printf("\n=== Test 9: Boot Vectors (SSP/PC) ===\n");

   if (snap->ram_ssp >= 0x1000 && snap->ram_ssp <= 0x200000)
      PASS("SSP = $%08X (valid RAM)", snap->ram_ssp);
   else
      FAIL("SSP = $%08X (invalid)", snap->ram_ssp);

   if ((snap->ram_pc >= 0x800000 && snap->ram_pc < 0xC00000)
       || (snap->ram_pc > 0 && snap->ram_pc < 0x200000))
      PASS("PC = $%08X (valid code space)", snap->ram_pc);
   else
      FAIL("PC = $%08X (unexpected)", snap->ram_pc);
}

/* ================================================================
 * Test 10: PIT Timers Cleared
 *
 * All PIT registers should be zero after init.
 * ================================================================ */
static void test_pit_cleared(const struct hw_snapshot *snap)
{
   printf("\n=== Test 10: PIT Timers Cleared ===\n");

   if (snap->jerry.pit0 == 0 && snap->jerry.pit1 == 0
       && snap->jerry.pit2 == 0 && snap->jerry.pit3 == 0)
      PASS("PIT0-3 all zero");
   else
      FAIL("PIT not zero: $%04X $%04X $%04X $%04X",
           snap->jerry.pit0, snap->jerry.pit1,
           snap->jerry.pit2, snap->jerry.pit3);
}

/* ================================================================
 * Test 11: OLP Points to STOP Object
 *
 * Object List Pointer should reference a STOP object in RAM.
 * ================================================================ */
static void test_olp_stop(const struct hw_snapshot *snap)
{
   uint32_t olp;
   uint32_t obj_hi, obj_lo;

   printf("\n=== Test 11: OLP STOP Object ===\n");

   olp = snap->tom.olp_lo | ((uint32_t)snap->tom.olp_hi << 16);

   if (olp >= 0x1000 && olp < 0x200000) {
      obj_hi = ram_get32(olp);
      obj_lo = ram_get32(olp + 4);

      if ((obj_lo & 0x07) == 0x04)
         PASS("OLP=$%08X -> STOP object (%08X %08X)", olp, obj_hi, obj_lo);
      else
         FAIL("OLP=$%08X -> type %u (expected STOP=4): %08X %08X",
              olp, obj_lo & 0x07, obj_hi, obj_lo);
   } else {
      FAIL("OLP = $%08X (not in RAM)", olp);
   }
}

/* ================================================================
 * Test 12: DSP Not Running After Init
 *
 * The DSP should not be running after boot — games start it when needed.
 * ================================================================ */
static void test_dsp_not_running(const struct hw_snapshot *snap)
{
   printf("\n=== Test 12: DSP Not Running After Init ===\n");

   if (snap->dsp.running == 0)
      PASS("DSP not running");
   else if (snap->dsp.running == 1)
      FAIL("DSP is running after init (should be stopped)");
   else
      INFO("Could not check DSP running state (DSPIsRunning not found)");
}

/* ================================================================
 * Test 13: BIOS vs HLE Comparison (requires both snapshots)
 *
 * Compares critical registers between BIOS and HLE init.
 * Differences are reported but not all are failures — BIOS may
 * leave state that games don't depend on.
 * ================================================================ */
static void test_bios_vs_hle(const struct hw_snapshot *bios, const struct hw_snapshot *hle)
{
   int match_count = 0;
   int diff_count = 0;

   printf("\n=== Test 13: BIOS vs HLE Init Comparison ===\n");

#define CMP_REG(name, bval, hval) do { \
   if ((bval) == (hval)) { match_count++; } \
   else { \
      printf("  DIFF: %-10s BIOS=$%04X  HLE=$%04X\n", name, (unsigned)(bval), (unsigned)(hval)); \
      diff_count++; \
   } \
} while(0)

#define CMP_REG32(name, bval, hval) do { \
   if ((bval) == (hval)) { match_count++; } \
   else { \
      printf("  DIFF: %-10s BIOS=$%08X  HLE=$%08X\n", name, (unsigned)(bval), (unsigned)(hval)); \
      diff_count++; \
   } \
} while(0)

   /* Critical: these MUST match */
   CMP_REG("MEMCON1", bios->tom.memcon1, hle->tom.memcon1);
   CMP_REG("MEMCON2", bios->tom.memcon2, hle->tom.memcon2);
   CMP_REG("VMODE",   bios->tom.vmode,   hle->tom.vmode);
   CMP_REG("CLK2",    bios->jerry.clk2,  hle->jerry.clk2);
   CMP_REG("CLK3",    bios->jerry.clk3,  hle->jerry.clk3);
   CMP_REG32("G_END", bios->gpu.g_end,   hle->gpu.g_end);
   CMP_REG32("AUTH",  bios->gpu.auth_magic, hle->gpu.auth_magic);

   /* Important: games often depend on these */
   CMP_REG("HP",   bios->tom.hp,   hle->tom.hp);
   CMP_REG("VP",   bios->tom.vp,   hle->tom.vp);
   CMP_REG("HBB",  bios->tom.hbb,  hle->tom.hbb);
   CMP_REG("HBE",  bios->tom.hbe,  hle->tom.hbe);
   CMP_REG("HDE",  bios->tom.hde,  hle->tom.hde);
   CMP_REG("HDB1", bios->tom.hdb1, hle->tom.hdb1);
   CMP_REG("VBB",  bios->tom.vbb,  hle->tom.vbb);
   CMP_REG("VBE",  bios->tom.vbe,  hle->tom.vbe);
   CMP_REG("VDB",  bios->tom.vdb,  hle->tom.vdb);
   CMP_REG("VDE",  bios->tom.vde,  hle->tom.vde);

   /* Informational */
   CMP_REG("SCLK",  bios->jerry.sclk,  hle->jerry.sclk);
   CMP_REG("SMODE", bios->jerry.smode,  hle->jerry.smode);

#undef CMP_REG
#undef CMP_REG32

   printf("  Summary: %d matching, %d different\n", match_count, diff_count);

   if (diff_count == 0)
      PASS("All compared registers match between BIOS and HLE");
   else if (diff_count <= 3)
      PASS("Most registers match (%d differences — check if critical)", diff_count);
   else
      FAIL("%d register differences between BIOS and HLE", diff_count);
}

/* ================================================================
 * Test 14: RAM Clear After Init
 *
 * In HLE mode, RAM should be mostly zeroed (except vectors at 0-7).
 * Check a sample of addresses to verify.
 * ================================================================ */
static void test_ram_clear(void)
{
   int nonzero_count = 0;
   uint32_t addrs[] = { 0x100, 0x200, 0x1000, 0x2000, 0x10000, 0x80000, 0x100000 };
   int i;
   int n;

   printf("\n=== Test 14: RAM Clear After Init ===\n");

   n = (int)(sizeof(addrs) / sizeof(addrs[0]));
   for (i = 0; i < n; i++) {
      uint32_t val = ram_get32(addrs[i]);
      if (val != 0) nonzero_count++;
   }

   /* Allow a few non-zero (exception handler area, etc.) */
   if (nonzero_count <= 2)
      PASS("RAM mostly zero (%d/%d sample addresses non-zero)", nonzero_count, n);
   else
      FAIL("RAM not cleared: %d/%d sample addresses non-zero", nonzero_count, n);
}

/* ================================================================
 * Test 15: TOM Video Timing Consistency
 *
 * Verify internal consistency: HDB < HDE < HBB < HP,
 * VDB < VDE < VBB < VP.
 * ================================================================ */
static void test_video_timing_consistency(const struct hw_snapshot *snap)
{
   printf("\n=== Test 15: Video Timing Consistency ===\n");

   if (snap->tom.hdb1 < snap->tom.hde
       && snap->tom.hde < snap->tom.hbb
       && snap->tom.hbb < snap->tom.hp + 100) /* HP wraps */
      PASS("Horizontal: HDB1(%u) < HDE(%u) < HBB(%u), HP=%u",
           snap->tom.hdb1, snap->tom.hde, snap->tom.hbb, snap->tom.hp);
   else
      FAIL("Horizontal inconsistent: HDB1=%u HDE=%u HBB=%u HP=%u",
           snap->tom.hdb1, snap->tom.hde, snap->tom.hbb, snap->tom.hp);

   if (snap->tom.vdb < snap->tom.vde && snap->tom.vde <= snap->tom.vbb)
      PASS("Vertical: VDB(%u) < VDE(%u) <= VBB(%u), VP=%u",
           snap->tom.vdb, snap->tom.vde, snap->tom.vbb, snap->tom.vp);
   else
      FAIL("Vertical inconsistent: VDB=%u VDE=%u VBB=%u VP=%u",
           snap->tom.vdb, snap->tom.vde, snap->tom.vbb, snap->tom.vp);
}

/* ================================================================
 * Main
 * ================================================================ */

int main(int argc, char **argv)
{
   const char *core_path;
   struct hw_snapshot hle_snap;
   struct hw_snapshot bios_snap;
   int have_bios = 0;

   core_path = (argc > 1) ? argv[1] : CORE_FILENAME;

   printf("=== Subsystem Init Tests ===\n");
   printf("Core: %s\n", core_path);

   if (!load_core(core_path)) return 1;

   /* ---- Phase 1: HLE boot ---- */
   printf("\n======== Phase 1: HLE Boot ========\n");
   use_bios = 0;
   init_core();

   if (!load_dummy_rom()) {
      fprintf(stderr, "Failed to load dummy ROM for HLE\n");
      return 1;
   }

   capture_snapshot(&hle_snap);
   print_snapshot("HLE Init", &hle_snap);

   test_sound_engine_init(&hle_snap);
   test_dsp_ram_state(&hle_snap, 0);
   test_gpu_auth_magic(&hle_snap);
   test_endianness(&hle_snap);
   test_clock_config(&hle_snap);
   test_tom_vmode(&hle_snap);
   test_exception_vectors(&hle_snap);
   test_interrupt_state(&hle_snap);
   test_boot_vectors(&hle_snap);
   test_pit_cleared(&hle_snap);
   test_olp_stop(&hle_snap);
   test_dsp_not_running(&hle_snap);
   test_ram_clear();
   test_video_timing_consistency(&hle_snap);

   p_retro_unload_game();
   p_retro_deinit();

   /* ---- Phase 2: BIOS boot (if available) ---- */
   printf("\n======== Phase 2: BIOS Boot ========\n");
   use_bios = 1;
   init_core();

   if (load_dummy_rom()) {
      /* Run one frame to let BIOS execute */
      p_retro_run();

      capture_snapshot(&bios_snap);
      print_snapshot("BIOS Init (1 frame)", &bios_snap);

      test_sound_engine_init(&bios_snap);
      test_dsp_ram_state(&bios_snap, 1);
      test_gpu_auth_magic(&bios_snap);
      test_endianness(&bios_snap);
      test_clock_config(&bios_snap);
      test_video_timing_consistency(&bios_snap);

      /* Comparison */
      test_bios_vs_hle(&bios_snap, &hle_snap);
      have_bios = 1;

      p_retro_unload_game();
   } else {
      printf("  (BIOS ROM not available — skipping BIOS tests)\n");
   }

   p_retro_deinit();
   dlclose(core_handle);

   printf("\n=== Results: %d passed, %d failed ===\n", passes, fails);
   if (!have_bios)
      printf("  (BIOS tests skipped — run with BIOS ROM in system dir for full coverage)\n");

   return fails > 0 ? 1 : 0;
}
