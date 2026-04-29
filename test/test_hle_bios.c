/* test_hle_bios.c -- HLE BIOS initialization tests.
 * Verifies that HLE (no-BIOS) boot produces the same hardware state as
 * the real BIOS: MEMCON1, clocks, endianness, GPU auth magic, OLP,
 * interrupts, TOM video registers, JERRY timers.
 *
 * Build: cc -o test/test_hle_bios test/test_hle_bios.c -ldl
 * Usage: ./test/test_hle_bios
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

/* TOM register offsets (from tom.c) */
#define TOM_MEMCON1  0x00
#define TOM_MEMCON2  0x02
#define TOM_HC       0x04
#define TOM_VC       0x06
#define TOM_OLP_LO   0x20
#define TOM_OLP_HI   0x22
#define TOM_VMODE    0x28
#define TOM_BORD1    0x2A
#define TOM_BORD2    0x2C
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
#define TOM_INT1     0xE0

/* GPU register addresses */
#define GPU_FLAGS_ADDR   0xF02100
#define GPU_G_END_ADDR   0xF0210C

/* DSP register addresses */
#define DSP_FLAGS_ADDR   0xF1A100
#define DSP_DORG_ADDR    0xF1A10C

/* JERRY register addresses */
#define JERRY_CLK2       0xF10012
#define JERRY_CLK3       0xF10014
#define JERRY_PIT0       0xF10000
#define JERRY_PIT1       0xF10002
#define JERRY_PIT2       0xF10004
#define JERRY_PIT3       0xF10006
#define JERRY_JINTCTRL   0xF10020
#define JERRY_SCLK       0xF1A152
#define JERRY_SMODE      0xF1A156
#define IRQ2_EXTERNAL    0x01
#define IRQ2_DSP         0x02
#define IRQ2_TIMER1      0x04
#define IRQ2_TIMER2      0x08
#define IRQ2_ASI         0x10
#define IRQ2_SSI         0x20

/* who enum values from vjag_memory.h */
#define WHO_M68K  6

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
static void (*p_retro_run)(void);
static void (*p_JaguarReset)(void);
static void (*p_JaguarApplyHLEBIOSState)(void);
static void (*p_HalflineCallback)(void);
static uint8_t (*p_TOMReadByte)(uint32_t, uint32_t);
static uint16_t (*p_TOMReadWord)(uint32_t, uint32_t);
static void (*p_TOMWriteByte)(uint32_t, uint8_t, uint32_t);
static void (*p_TOMWriteWord)(uint32_t, uint16_t, uint32_t);
static void (*p_TOMSetPendingVideoInt)(void);
static void (*p_TOMSetPendingTimerInt)(void);
static void (*p_JERRYWriteWord)(uint32_t, uint16_t, uint32_t);
static void (*p_JERRYWriteByte)(uint32_t, uint8_t, uint32_t);
static bool (*p_JERRYIRQEnabled)(int);
static void (*p_JERRYSetPendingIRQ)(int);
static void (*p_OPProcessList)(int, bool);
static void (*p_OPProcessScaledBitmap)(uint64_t, uint64_t, uint64_t, bool);
static void (*p_OPProcessFixedBitmap)(uint64_t, uint64_t, bool);

/* Emulator internals via dlsym */
static void *core_handle;
static uint8_t *p_tomRam8;
static uint8_t **p_jaguarMainRAM;
static uint8_t *p_jagMemSpace;
static uint8_t **p_sclk;
static uint32_t **p_smode;
static bool *p_lowerField;
static uint32_t (*p_GPUReadLong)(uint32_t, uint32_t);
static void (*p_GPUWriteLong)(uint32_t, uint32_t, uint32_t);
static void (*p_GPUSetIRQLine)(int, int);
static void (*p_GPUHandleIRQs)(void);
static uint32_t *p_gpu_pc;
static uint32_t (*p_DSPReadLong)(uint32_t, uint32_t);
static void (*p_DSPWriteLong)(uint32_t, uint32_t, uint32_t);
static void (*p_DSPSetIRQLine)(int, int);
static void (*p_DSPHandleIRQsNP)(void);
static uint32_t *p_dsp_pc;
static uint32_t *p_dsp_control;
static uint16_t (*p_JERRYReadWord)(uint32_t, uint32_t);
static struct VJSettings *p_vjs;
static uint32_t *p_jaguarLoadedRAMStart;
static uint32_t *p_jaguarLoadedRAMEnd;

struct VJSettings {
   bool hardwareTypeNTSC;
   bool useJaguarBIOS;
   bool useFastBlitter;
};

/* Stub callbacks */
static unsigned last_video_width;
static unsigned last_video_height;
static size_t last_video_pitch;
static int geometry_update_count;
static unsigned last_geometry_width;
static unsigned last_geometry_height;

static void video_refresh(const void *d, unsigned w, unsigned h, size_t p)
{
   (void)d;
   last_video_width = w;
   last_video_height = h;
   last_video_pitch = p;
}
static void audio_sample(int16_t l, int16_t r) { (void)l; (void)r; }
static size_t audio_batch(const int16_t *d, size_t f) { (void)d; return f; }
static void input_poll(void) {}
static int16_t input_state(unsigned p, unsigned d, unsigned i, unsigned id)
{ (void)p; (void)d; (void)i; (void)id; return 0; }

static int use_bios = 0;
static int use_pal = 0;

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
   case RETRO_ENVIRONMENT_GET_CORE_OPTIONS_VERSION:
   case RETRO_ENVIRONMENT_GET_PREFERRED_HW_RENDER:
      return true;
   case RETRO_ENVIRONMENT_SET_GEOMETRY: {
      const struct retro_system_av_info *info = (const struct retro_system_av_info *)data;
      geometry_update_count++;
      last_geometry_width = info->geometry.base_width;
      last_geometry_height = info->geometry.base_height;
      return true;
   }
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
      if (var->key && strcmp(var->key, "virtualjaguar_pal") == 0) {
         var->value = use_pal ? "enabled" : "disabled";
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

/* Mirror of struct regstruct from src/m68000/cpudefs.h. The core exports
 * `regs` as a global symbol; we dlsym it and access it via this layout to
 * observe the IPL line state set by m68k_set_irq -> m68k_set_irq2 path.
 * The layout MUST match cpudefs.h exactly up through `intLevel`. */
struct test_regstruct
{
   uint32_t regs[16];
   uint32_t usp, isp;
   uint16_t sr;
   uint8_t  s;
   uint8_t  stopped;
   int      intmask;
   int      intLevel;
   /* Other fields follow but we only need state up to intLevel. */
};
static struct test_regstruct *p_regs;

/* Helper: read 16-bit big-endian from tomRam8 */
static uint16_t tom_get16(uint16_t offset)
{
   return ((uint16_t)p_tomRam8[offset] << 8) | p_tomRam8[offset + 1];
}

/* Helper: read 32-bit big-endian from RAM */
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
   return ((uint16_t)ram[offset] << 8) | (uint16_t)ram[offset + 1];
}

static void tom_set16(uint16_t offset, uint16_t value)
{
   p_tomRam8[offset] = (uint8_t)(value >> 8);
   p_tomRam8[offset + 1] = (uint8_t)value;
}

static void ram_set32(uint32_t offset, uint32_t value)
{
   uint8_t *ram = *p_jaguarMainRAM;

   ram[offset] = (uint8_t)(value >> 24);
   ram[offset + 1] = (uint8_t)(value >> 16);
   ram[offset + 2] = (uint8_t)(value >> 8);
   ram[offset + 3] = (uint8_t)value;
}

/* ================================================================
 * Test 1: GPU Auth Magic
 * The BIOS GPU program writes $03D0DEAD to $F03000 on success.
 * HLE must replicate this.
 * ================================================================ */
static void test_gpu_auth_magic(void)
{
   uint32_t magic;

   printf("\n=== Test 1: GPU Auth Magic ($03D0DEAD @ $F03000) ===\n");

   magic = p_GPUReadLong(0xF03000, WHO_M68K);

   if (magic == 0x03D0DEAD)
      PASS("GPU auth magic = $%08X", magic);
   else
      FAIL("GPU auth magic = $%08X (expected $03D0DEAD)", magic);
}

/* ================================================================
 * Test 1b: HLE BIOS low-RAM workspace
 * The real BIOS leaves non-zero workspace data at $804. Battle Sphere
 * reaches this through a GPU blitter-idle polling path after menu load.
 * ================================================================ */
static void test_hle_low_ram_workspace(void)
{
   uint32_t value;

   printf("\n=== Test 1b: HLE BIOS Low-RAM Workspace ($804) ===\n");

   value = ram_get32(0x804);
   if (value & 0x00000001)
      PASS("RAM[$804] = $%08X has bit 0 set", value);
   else
      FAIL("RAM[$804] = $%08X (expected bit 0 set)", value);
}

/* ================================================================
 * Test 1c: HLE BIOS workspace apply contract
 * The save-state path reapplies this hook. It must populate missing
 * HLE state, preserve existing game/BIOS data, and stay inert for
 * real-BIOS mode.
 * ================================================================ */
static void test_hle_workspace_apply_contract(void)
{
   uint32_t value;
   bool old_use_bios;

   printf("\n=== Test 1c: HLE BIOS Workspace Apply Contract ===\n");

   ram_set32(0x804, 0x00000000);
   p_JaguarApplyHLEBIOSState();
   value = ram_get32(0x804);
   if (value == 0x00000001)
      PASS("zero workspace becomes $%08X", value);
   else
      FAIL("zero workspace became $%08X (expected $00000001)", value);

   ram_set32(0x804, 0xA5A5A5A5);
   p_JaguarApplyHLEBIOSState();
   value = ram_get32(0x804);
   if (value == 0xA5A5A5A5)
      PASS("non-zero workspace preserved as $%08X", value);
   else
      FAIL("non-zero workspace changed to $%08X", value);

   old_use_bios = p_vjs->useJaguarBIOS;
   p_vjs->useJaguarBIOS = true;
   ram_set32(0x804, 0x00000000);
   p_JaguarApplyHLEBIOSState();
   value = ram_get32(0x804);
   p_vjs->useJaguarBIOS = old_use_bios;

   if (value == 0x00000000)
      PASS("real-BIOS mode leaves workspace unchanged");
   else
      FAIL("real-BIOS mode changed workspace to $%08X", value);

   p_JaguarApplyHLEBIOSState();
}

/* ================================================================
 * Test 1d: HLE reset preserves RAM-loaded executable ranges
 * Homebrew/server formats are copied into main RAM before reset.
 * HLE RAM clearing must not erase that payload.
 * ================================================================ */
static void test_hle_preserves_ram_loaded_range(void)
{
   uint32_t old_start;
   uint32_t old_end;
   bool old_use_bios;

   printf("\n=== Test 1d: HLE Reset Preserves RAM-Loaded Range ===\n");

   old_start = *p_jaguarLoadedRAMStart;
   old_end = *p_jaguarLoadedRAMEnd;
   old_use_bios = p_vjs->useJaguarBIOS;

   p_vjs->useJaguarBIOS = false;
   *p_jaguarLoadedRAMStart = 0x00012000;
   *p_jaguarLoadedRAMEnd = 0x00012010;

   ram_set32(0x00011FFC, 0xFEEDFACE);
   ram_set32(0x00012000, 0xAABBCCDD);
   ram_set32(0x0001200C, 0x11223344);
   ram_set32(0x00012010, 0xCAFEBABE);

   p_JaguarReset();

   if (ram_get32(0x00012000) == 0xAABBCCDD
         && ram_get32(0x0001200C) == 0x11223344)
      PASS("RAM-loaded payload survived HLE reset");
   else
      FAIL("RAM-loaded payload was cleared: $%08X $%08X",
           ram_get32(0x00012000), ram_get32(0x0001200C));

   if (ram_get32(0x00011FFC) == 0 && ram_get32(0x00012010) == 0)
      PASS("HLE reset still clears RAM outside the loaded range");
   else
      FAIL("RAM outside loaded range not cleared: before=$%08X after=$%08X",
           ram_get32(0x00011FFC), ram_get32(0x00012010));

   *p_jaguarLoadedRAMStart = old_start;
   *p_jaguarLoadedRAMEnd = old_end;
   p_vjs->useJaguarBIOS = old_use_bios;
}

/* ================================================================
 * Test 1e: HLE exception and interrupt vectors
 * The HLE path installs safe RTE stubs, including vector 64 used by
 * Jaguar hardware interrupts.
 * ================================================================ */
static void test_hle_exception_vectors(void)
{
   uint32_t bus_vector;
   uint32_t address_vector;
   uint32_t generic_vector;
   uint32_t interrupt_vector;
   uint32_t last_vector;
   uint32_t bus_handler;
   uint16_t rte_handler;

   printf("\n=== Test 1e: HLE Exception Vectors ===\n");

   bus_vector = ram_get32(0x08);
   address_vector = ram_get32(0x0C);
   generic_vector = ram_get32(0x10);
   interrupt_vector = ram_get32(0x100);
   last_vector = ram_get32(0x3FC);
   bus_handler = ram_get32(0x400);
   rte_handler = ram_get16(0x404);

   if (bus_vector == 0x00000400 && address_vector == 0x00000400)
      PASS("bus/address vectors point to $0400");
   else
      FAIL("bus/address vectors are $%08X/$%08X", bus_vector, address_vector);

   if (generic_vector == 0x00000404 && interrupt_vector == 0x00000404
         && last_vector == 0x00000404)
      PASS("generic, interrupt, and last vectors point to $0404");
   else
      FAIL("generic/interrupt/last vectors are $%08X/$%08X/$%08X",
           generic_vector, interrupt_vector, last_vector);

   if (bus_handler == 0x508F4E73)
      PASS("bus/address handler = $%08X", bus_handler);
   else
      FAIL("bus/address handler = $%08X (expected $508F4E73)", bus_handler);

   if (rte_handler == 0x4E73)
      PASS("generic RTE handler = $%04X", rte_handler);
   else
      FAIL("generic RTE handler = $%04X (expected $4E73)", rte_handler);
}

/* ================================================================
 * Test 2: MEMCON1 Auto-Detect
 * HLE sets MEMCON1 = $1861 | (cart_header[$800400] & $1E).
 * With our dummy ROM (all zeros at $400), result should be $1861.
 * ================================================================ */
static void test_memcon1(void)
{
   uint16_t memcon1;
   uint8_t cart_type;
   uint16_t expected;

   printf("\n=== Test 2: MEMCON1 Auto-Detect ===\n");

   memcon1 = tom_get16(TOM_MEMCON1);
   cart_type = p_jagMemSpace[0x800400];
   expected = 0x1861 | (cart_type & 0x1E);

   if (memcon1 == expected)
      PASS("MEMCON1 = $%04X (cart type byte $%02X -> expected $%04X)",
           memcon1, cart_type, expected);
   else
      FAIL("MEMCON1 = $%04X (expected $%04X, cart type byte $%02X)",
           memcon1, expected, cart_type);
}

/* ================================================================
 * Test 3: MEMCON1 with Non-Zero Cart Type
 * Load a ROM with bits 1-4 set in the type byte and verify MEMCON1.
 * ================================================================ */
static void test_memcon1_nonzero_type(void)
{
   uint16_t memcon1;
   uint8_t cart_type;
   uint16_t expected;

   printf("\n=== Test 3: MEMCON1 with Non-Zero Cart Type ===\n");

   cart_type = p_jagMemSpace[0x800400];
   memcon1 = tom_get16(TOM_MEMCON1);
   expected = 0x1861 | (cart_type & 0x1E);

   if ((cart_type & 0x1E) != 0) {
      if (memcon1 == expected)
         PASS("MEMCON1 = $%04X with cart type $%02X", memcon1, cart_type);
      else
         FAIL("MEMCON1 = $%04X (expected $%04X) with cart type $%02X",
              memcon1, expected, cart_type);
   } else {
      PASS("Cart type bits 1-4 are zero ($%02X); base MEMCON1 $1861 correct", cart_type);
   }
}

/* ================================================================
 * Test 4: JERRY Clock Dividers
 * CLK3 ($F10014) = $0004
 * CLK2 ($F10012) = $00B5 (NTSC) or $00E2 (PAL)
 * ================================================================ */
static void test_jerry_clocks(void)
{
   uint16_t clk2, clk3;
   uint16_t expected_clk2;

   printf("\n=== Test 4: JERRY Clock Dividers ===\n");

   clk3 = p_JERRYReadWord(JERRY_CLK3, WHO_M68K);
   clk2 = p_JERRYReadWord(JERRY_CLK2, WHO_M68K);
   expected_clk2 = p_vjs->hardwareTypeNTSC ? 0x00B5 : 0x00E2;

   if (clk3 == 0x0004)
      PASS("CLK3 = $%04X", clk3);
   else
      FAIL("CLK3 = $%04X (expected $0004)", clk3);

   if (clk2 == expected_clk2)
      PASS("CLK2 = $%04X (%s)", clk2,
           p_vjs->hardwareTypeNTSC ? "NTSC" : "PAL");
   else
      FAIL("CLK2 = $%04X (expected $%04X for %s)", clk2, expected_clk2,
           p_vjs->hardwareTypeNTSC ? "NTSC" : "PAL");
}

/* ================================================================
 * Test 5: GPU/DSP Endianness Registers
 * G_END ($F0210C) = $00070007
 * D_ORG ($F1A10C-$F1A10E) = $0007/$0007
 * ================================================================ */
static void test_endianness_registers(void)
{
   uint32_t g_end;
   uint16_t dsp_dorg_hi, dsp_dorg_lo;

   printf("\n=== Test 5: GPU/DSP Endianness Registers ===\n");

   g_end = p_GPUReadLong(GPU_G_END_ADDR, WHO_M68K);

   if (g_end == 0x00070007)
      PASS("G_END = $%08X", g_end);
   else
      FAIL("G_END = $%08X (expected $00070007)", g_end);

   dsp_dorg_hi = p_JERRYReadWord(DSP_DORG_ADDR, WHO_M68K);
   dsp_dorg_lo = p_JERRYReadWord(DSP_DORG_ADDR + 2, WHO_M68K);

   if (dsp_dorg_hi == 0x0007)
      PASS("DSP_DORG high = $%04X", dsp_dorg_hi);
   else
      FAIL("DSP_DORG high = $%04X (expected $0007)", dsp_dorg_hi);

   if (dsp_dorg_lo == 0x0007)
      PASS("DSP_DORG low = $%04X", dsp_dorg_lo);
   else
      FAIL("DSP_DORG low = $%04X (expected $0007)", dsp_dorg_lo);
}

/* ================================================================
 * Test 6: Object Processor STOP List
 * RAM[$1000-$1007] should contain a STOP object (type 4).
 * OLP should point to $00001000.
 * ================================================================ */
static void test_op_stop_list(void)
{
   uint32_t obj_hi, obj_lo;
   uint16_t olp_lo, olp_hi;
   uint32_t olp;

   printf("\n=== Test 6: Object Processor STOP List ===\n");

   obj_hi = ram_get32(0x1000);
   obj_lo = ram_get32(0x1004);

   if (obj_hi == 0x00000000 && obj_lo == 0x00000004)
      PASS("STOP object at $1000: %08X %08X", obj_hi, obj_lo);
   else
      FAIL("STOP object at $1000: %08X %08X (expected 00000000 00000004)",
           obj_hi, obj_lo);

   olp_lo = tom_get16(TOM_OLP_LO);
   olp_hi = tom_get16(TOM_OLP_HI);
   olp = olp_lo | ((uint32_t)olp_hi << 16);

   if (olp == 0x00001000)
      PASS("OLP = $%08X", olp);
   else
      FAIL("OLP = $%08X (expected $00001000, lo=$%04X hi=$%04X)",
           olp, olp_lo, olp_hi);
}

/* ================================================================
 * Test 6a: OP Scaled Bitmap Small HScale Clipping
 * Small hscale values can make an integer-scaled phrase width zero.
 * Clipping must keep fractional precision and avoid divide-by-zero.
 * ================================================================ */
static void test_op_scaled_small_hscale_clip(void)
{
   uint64_t p0;
   uint64_t p1;
   uint64_t p2;

   printf("\n=== Test 6a: OP Scaled Bitmap Small HScale Clipping ===\n");

   p0 = (uint64_t)0x800000 << 40;
   p1 = ((uint64_t)3 << 12) | ((uint64_t)128 << 28) | 700;
   p2 = 1;

   p_OPProcessScaledBitmap(p0, p1, p2, true);
   PASS("small hscale right-edge clip completed");
}

/* ================================================================
 * Test 6b: OP Scaled Bitmap 1:1 Phase and firstPix
 * hscale=$20 is 1:1. The source cursor must advance after each
 * destination pixel and honor firstPix for the first source phrase.
 * ================================================================ */
static void test_op_scaled_firstpix_4bpp(void)
{
   uint64_t p0;
   uint64_t p1;
   uint64_t p2;
   unsigned i;

   printf("\n=== Test 6b: OP Scaled Bitmap 1:1 Phase and firstPix ===\n");

   memset(p_tomRam8 + 0x1800, 0, 64);
   for (i = 0; i < 16; i++)
   {
      p_tomRam8[0x400 + (i * 2)] = (uint8_t)i;
      p_tomRam8[0x401 + (i * 2)] = (uint8_t)i;
   }

   ram_set32(0x100000, 0x12345678);
   ram_set32(0x100004, 0x9ABCDEF0);

   p0 = (uint64_t)0x100000 << 40;
   p1 = ((uint64_t)1 << 28)
      | ((uint64_t)1 << 15)
      | ((uint64_t)2 << 12);
   p2 = 0x20;

   p_OPProcessScaledBitmap(p0, p1, p2, true);

   if (p_tomRam8[0x1800] == 0x01 && p_tomRam8[0x1801] == 0x01
         && p_tomRam8[0x1802] == 0x02 && p_tomRam8[0x1803] == 0x02)
      PASS("4bpp scaled hscale=$20 advances source at 1:1");
   else
      FAIL("4bpp scaled 1:1 first two pixels = %02X%02X %02X%02X (expected 0101 0202)",
           p_tomRam8[0x1800], p_tomRam8[0x1801],
           p_tomRam8[0x1802], p_tomRam8[0x1803]);

   memset(p_tomRam8 + 0x1800, 0, 64);

   p2 = 0x40;

   p_OPProcessScaledBitmap(p0, p1, p2, true);

   if (p_tomRam8[0x1800] == 0x01 && p_tomRam8[0x1801] == 0x01
         && p_tomRam8[0x1802] == 0x01 && p_tomRam8[0x1803] == 0x01
         && p_tomRam8[0x1804] == 0x02 && p_tomRam8[0x1805] == 0x02
         && p_tomRam8[0x1806] == 0x02 && p_tomRam8[0x1807] == 0x02)
      PASS("4bpp scaled hscale=$40 duplicates each source pixel");
   else
      FAIL("4bpp scaled 2x first four pixels = %02X%02X %02X%02X %02X%02X %02X%02X (expected 0101 0101 0202 0202)",
           p_tomRam8[0x1800], p_tomRam8[0x1801],
           p_tomRam8[0x1802], p_tomRam8[0x1803],
           p_tomRam8[0x1804], p_tomRam8[0x1805],
           p_tomRam8[0x1806], p_tomRam8[0x1807]);

   memset(p_tomRam8 + 0x1800, 0, 64);
   p2 = 0x30;

   p_OPProcessScaledBitmap(p0, p1, p2, true);

   if (p_tomRam8[0x1800] == 0x01 && p_tomRam8[0x1801] == 0x01
         && p_tomRam8[0x1802] == 0x01 && p_tomRam8[0x1803] == 0x01
         && p_tomRam8[0x1804] == 0x02 && p_tomRam8[0x1805] == 0x02
         && p_tomRam8[0x1806] == 0x03 && p_tomRam8[0x1807] == 0x03
         && p_tomRam8[0x1808] == 0x03 && p_tomRam8[0x1809] == 0x03
         && p_tomRam8[0x180A] == 0x04 && p_tomRam8[0x180B] == 0x04)
      PASS("4bpp scaled hscale=$30 uses stable 3:2 source stepping");
   else
      FAIL("4bpp scaled 3:2 first six pixels = %02X%02X %02X%02X %02X%02X %02X%02X %02X%02X %02X%02X (expected 0101 0101 0202 0303 0303 0404)",
           p_tomRam8[0x1800], p_tomRam8[0x1801],
           p_tomRam8[0x1802], p_tomRam8[0x1803],
           p_tomRam8[0x1804], p_tomRam8[0x1805],
           p_tomRam8[0x1806], p_tomRam8[0x1807],
           p_tomRam8[0x1808], p_tomRam8[0x1809],
           p_tomRam8[0x180A], p_tomRam8[0x180B]);

   memset(p_tomRam8 + 0x1800, 0, 64);
   p2 = 0x20;

   p1 = ((uint64_t)8 << 49)
      | ((uint64_t)1 << 28)
      | ((uint64_t)1 << 15)
      | ((uint64_t)2 << 12);

   p_OPProcessScaledBitmap(p0, p1, p2, true);

   if (p_tomRam8[0x1800] == 0x03 && p_tomRam8[0x1801] == 0x03
         && p_tomRam8[0x1802] == 0x04 && p_tomRam8[0x1803] == 0x04)
      PASS("4bpp scaled firstPix skipped to source index 3");
   else
      FAIL("4bpp scaled firstPix first two pixels = %02X%02X %02X%02X (expected 0303 0404)",
           p_tomRam8[0x1800], p_tomRam8[0x1801],
           p_tomRam8[0x1802], p_tomRam8[0x1803]);

   memset(p_tomRam8 + 0x1800, 0, 64);
   ram_set32(0x100008, 0x23456789);
   ram_set32(0x10000C, 0xABCDEF01);

   p1 = ((uint64_t)8 << 49)
      | ((uint64_t)3 << 28)
      | ((uint64_t)1 << 15)
      | ((uint64_t)2 << 12)
      | 0xFF0;

   p_OPProcessScaledBitmap(p0, p1, p2, true);

   if (p_tomRam8[0x1800] == 0x02 && p_tomRam8[0x1801] == 0x02)
      PASS("4bpp scaled clipped phrase ignores firstPix after source advance");
   else
      FAIL("4bpp scaled clipped first pixel = %02X%02X (expected 0202)",
           p_tomRam8[0x1800], p_tomRam8[0x1801]);

   memset(p_tomRam8 + 0x17F0, 0xEE, 0x50);
   p_tomRam8[0x1800] = 0;
   p_tomRam8[0x1801] = 0;

   p1 = ((uint64_t)1 << 28)
      | ((uint64_t)1 << 15)
      | ((uint64_t)2 << 12)
      | 0xFFF;

   p_OPProcessScaledBitmap(p0, p1, p2, true);

   if (p_tomRam8[0x17FE] == 0xEE && p_tomRam8[0x17FF] == 0xEE
         && p_tomRam8[0x1800] == 0x02 && p_tomRam8[0x1801] == 0x02)
      PASS("4bpp scaled partial left clip consumes source without pre-LBUF write");
   else
      FAIL("4bpp scaled partial left clip pre=%02X%02X first=%02X%02X (expected EEEE 0202)",
           p_tomRam8[0x17FE], p_tomRam8[0x17FF],
           p_tomRam8[0x1800], p_tomRam8[0x1801]);

   memset(p_tomRam8 + 0x1D98, 0xEE, 0x20);

   p1 = ((uint64_t)1 << 28)
      | ((uint64_t)1 << 15)
      | ((uint64_t)2 << 12)
      | 719;

   p_OPProcessScaledBitmap(p0, p1, p2, true);

   if (p_tomRam8[0x1D9E] == 0x01 && p_tomRam8[0x1D9F] == 0x01
         && p_tomRam8[0x1DA0] == 0xEE && p_tomRam8[0x1DA1] == 0xEE)
      PASS("4bpp scaled right edge stops at final LBUF pixel");
   else
      FAIL("4bpp scaled right edge last=%02X%02X post=%02X%02X (expected 0101 EEEE)",
           p_tomRam8[0x1D9E], p_tomRam8[0x1D9F],
           p_tomRam8[0x1DA0], p_tomRam8[0x1DA1]);

   memset(p_tomRam8 + 0x1D98, 0xEE, 0x20);

   p1 = ((uint64_t)1 << 45)
      | ((uint64_t)1 << 28)
      | ((uint64_t)1 << 15)
      | ((uint64_t)2 << 12)
      | 720;

   p_OPProcessScaledBitmap(p0, p1, p2, true);

   if (p_tomRam8[0x1D9E] == 0x02 && p_tomRam8[0x1D9F] == 0x02
         && p_tomRam8[0x1DA0] == 0xEE && p_tomRam8[0x1DA1] == 0xEE)
      PASS("4bpp reflected scaled right edge consumes offscreen source pixel");
   else
      FAIL("4bpp reflected right edge last=%02X%02X post=%02X%02X (expected 0202 EEEE)",
           p_tomRam8[0x1D9E], p_tomRam8[0x1D9F],
           p_tomRam8[0x1DA0], p_tomRam8[0x1DA1]);

   memset(p_tomRam8 + 0x1800, 0, 64);

   p1 = ((uint64_t)1 << 15)
      | ((uint64_t)2 << 12);

   p_OPProcessScaledBitmap(p0, p1, p2, true);

   if (p_tomRam8[0x1800] == 0x01 && p_tomRam8[0x1801] == 0x01)
      PASS("4bpp scaled iwidth=0 renders one source phrase");
   else
      FAIL("4bpp scaled iwidth=0 first pixel = %02X%02X (expected 0101)",
           p_tomRam8[0x1800], p_tomRam8[0x1801]);

   memset(p_tomRam8 + 0x1800, 0, 64);

   p1 = ((uint64_t)1 << 45)
      | ((uint64_t)1 << 28)
      | ((uint64_t)1 << 15)
      | ((uint64_t)2 << 12);

   p_OPProcessScaledBitmap(p0, p1, p2, true);

   if (p_tomRam8[0x1800] == 0x01 && p_tomRam8[0x1801] == 0x01)
      PASS("4bpp reflected scaled left edge keeps visible edge pixel");
   else
      FAIL("4bpp reflected left edge first pixel = %02X%02X (expected 0101)",
           p_tomRam8[0x1800], p_tomRam8[0x1801]);
}

/* ================================================================
 * Test 6c: OP Fixed Bitmap firstPix
 * The firstPix field skips pixels at the start of the first phrase.
 * ================================================================ */
static void test_op_fixed_firstpix_4bpp(void)
{
   uint64_t p0;
   uint64_t p1;
   unsigned i;

   printf("\n=== Test 6c: OP Fixed Bitmap firstPix ===\n");

   memset(p_tomRam8 + 0x1800, 0, 64);
   for (i = 0; i < 16; i++)
   {
      p_tomRam8[0x400 + (i * 2)] = (uint8_t)i;
      p_tomRam8[0x401 + (i * 2)] = (uint8_t)i;
   }

   ram_set32(0x100000, 0x12345678);
   ram_set32(0x100004, 0x9ABCDEF0);

   p0 = (uint64_t)0x100000 << 40;
   p1 = ((uint64_t)8 << 49)
      | ((uint64_t)1 << 28)
      | ((uint64_t)1 << 15)
      | ((uint64_t)2 << 12);

   p_OPProcessFixedBitmap(p0, p1, true);

   if (p_tomRam8[0x1800] == 0x03 && p_tomRam8[0x1801] == 0x03)
      PASS("4bpp firstPix skipped to palette index 3");
   else
      FAIL("4bpp firstPix first pixel = %02X %02X (expected 03 03)",
           p_tomRam8[0x1800], p_tomRam8[0x1801]);

   memset(p_tomRam8 + 0x1800, 0, 64);
   ram_set32(0x100008, 0x23456789);
   ram_set32(0x10000C, 0xABCDEF01);

   p0 = (uint64_t)0x100000 << 40;
   p1 = ((uint64_t)8 << 49)
      | ((uint64_t)3 << 28)
      | ((uint64_t)1 << 15)
      | ((uint64_t)2 << 12)
      | 0xFF0;

   p_OPProcessFixedBitmap(p0, p1, true);

   if (p_tomRam8[0x1800] == 0x02 && p_tomRam8[0x1801] == 0x02)
      PASS("4bpp clipped phrase ignores firstPix after source advance");
   else
      FAIL("4bpp clipped first pixel = %02X %02X (expected 02 02)",
           p_tomRam8[0x1800], p_tomRam8[0x1801]);
}

/* ================================================================
 * Test 6d: OP Bitmap write-back
 * Bitmap objects are consumed during list processing: height is
 * decremented and the source pointer advances by dwidth.
 * ================================================================ */
static void test_op_fixed_bitmap_writeback(void)
{
   uint32_t listAddr;
   uint32_t dataAddr;
   uint32_t stopAddr;
   uint64_t p0;
   uint64_t p1;
   uint32_t newP0Hi;
   uint32_t newP0Lo;
   uint32_t newData;
   uint32_t newHeight;

   printf("\n=== Test 6d: OP Fixed Bitmap write-back ===\n");

   listAddr = 0x00012000;
   dataAddr = 0x00100000;
   stopAddr = listAddr + 0x18;

   memset(p_tomRam8 + 0x1800, 0, 64);
   ram_set32(dataAddr, 0x11223344);
   ram_set32(dataAddr + 4, 0x55667788);

   p0 = ((uint64_t)dataAddr << 40)
      | ((uint64_t)stopAddr << 21)
      | ((uint64_t)2 << 14)
      | ((uint64_t)10 << 3);
   p1 = ((uint64_t)1 << 28)
      | ((uint64_t)1 << 18)
      | ((uint64_t)1 << 15)
      | ((uint64_t)4 << 12);

   ram_set32(listAddr, (uint32_t)(p0 >> 32));
   ram_set32(listAddr + 4, (uint32_t)p0);
   ram_set32(listAddr + 8, (uint32_t)(p1 >> 32));
   ram_set32(listAddr + 12, (uint32_t)p1);
   ram_set32(listAddr + 16, 0x00000000);
   ram_set32(listAddr + 20, 0x00000000);
   ram_set32(stopAddr, 0x00000000);
   ram_set32(stopAddr + 4, 0x00000004);

   tom_set16(TOM_OLP_LO, (uint16_t)listAddr);
   tom_set16(TOM_OLP_HI, (uint16_t)(listAddr >> 16));

   p_OPProcessList(10, true);

   newP0Hi = ram_get32(listAddr);
   newP0Lo = ram_get32(listAddr + 4);
   p0 = ((uint64_t)newP0Hi << 32) | newP0Lo;
   newData = (uint32_t)((p0 >> 40) & 0xFFFFF8);
   newHeight = (uint32_t)((p0 >> 14) & 0x3FF);

   if (newHeight == 1 && newData == dataAddr + 8)
      PASS("fixed bitmap write-back advanced data and decremented height");
   else
      FAIL("fixed bitmap write-back data=$%06X height=%u (expected data=$%06X height=1)",
           newData, newHeight, dataAddr + 8);
}

/* ================================================================
 * Test 7: Border Color Cleared
 * BORD1 and BORD2 should both be zero.
 * ================================================================ */
static void test_border_clear(void)
{
   uint16_t bord1, bord2;

   printf("\n=== Test 7: Border Color Cleared ===\n");

   bord1 = tom_get16(TOM_BORD1);
   bord2 = tom_get16(TOM_BORD2);

   if (bord1 == 0x0000)
      PASS("BORD1 = $%04X", bord1);
   else
      FAIL("BORD1 = $%04X (expected $0000)", bord1);

   if (bord2 == 0x0000)
      PASS("BORD2 = $%04X", bord2);
   else
      FAIL("BORD2 = $%04X (expected $0000)", bord2);
}

/* ================================================================
 * Test 8: Interrupts Cleared and Disabled
 * INT1 register: pending bits should be cleared, enables = 0.
 * ================================================================ */
static void test_interrupts_cleared(void)
{
   uint16_t int1;

   printf("\n=== Test 8: Interrupts Cleared and Disabled ===\n");

   int1 = tom_get16(TOM_INT1);

   if ((int1 & 0x001F) == 0)
      PASS("INT1 enables = 0 (INT1=$%04X)", int1);
   else
      FAIL("INT1 enables != 0: $%04X (bits 0-4 should be 0)", int1);
}

/* ================================================================
 * Test 9: JERRY PIT Timers Cleared
 * All four PIT registers should be zero.
 * ================================================================ */
static void test_jerry_pit_cleared(void)
{
   uint16_t pit0, pit1, pit2, pit3;

   printf("\n=== Test 9: JERRY PIT Timers Cleared ===\n");

   pit0 = p_JERRYReadWord(JERRY_PIT0, WHO_M68K);
   pit1 = p_JERRYReadWord(JERRY_PIT1, WHO_M68K);
   pit2 = p_JERRYReadWord(JERRY_PIT2, WHO_M68K);
   pit3 = p_JERRYReadWord(JERRY_PIT3, WHO_M68K);

   if (pit0 == 0 && pit1 == 0 && pit2 == 0 && pit3 == 0)
      PASS("PIT0-3 all zero");
   else
      FAIL("PIT not cleared: %04X %04X %04X %04X", pit0, pit1, pit2, pit3);
}

/* ================================================================
 * Test 9a: JERRY JINTCTRL Word Decode
 * JINTCTRL is the word at $F10020. The adjacent word at $F10022
 * must not alias interrupt enables or clear pending interrupts.
 * ================================================================ */
static void test_jerry_jintctrl_word_decode(void)
{
   uint16_t pending;

   printf("\n=== Test 9a: JERRY JINTCTRL Word Decode ===\n");

   p_JERRYWriteWord(JERRY_JINTCTRL, IRQ2_TIMER1, WHO_M68K);
   p_JERRYSetPendingIRQ(IRQ2_TIMER1);
   p_JERRYWriteWord(JERRY_JINTCTRL + 2, 0x0400, WHO_M68K);

   pending = p_JERRYReadWord(JERRY_JINTCTRL, WHO_M68K);

   if ((pending & IRQ2_TIMER1) && p_JERRYIRQEnabled(IRQ2_TIMER1))
      PASS("$F10022 write did not alias JINTCTRL");
   else
      FAIL("JINTCTRL alias: pending=$%04X timer1Enabled=%d",
            pending, p_JERRYIRQEnabled(IRQ2_TIMER1) ? 1 : 0);

   p_JERRYWriteWord(JERRY_JINTCTRL, 0x0400, WHO_M68K);
}

/* ================================================================
 * Test 9c: JERRY JINTCTRL multiple pending sources
 * Mirror of Test 10f for TOM INT1, but JINTCTRL has different byte/word
 * semantics:
 *   byte $F10020: clears pending bits matching data; mask untouched
 *   byte $F10021: replaces mask; pending untouched
 *   word $F10020: low byte = mask (replace), high byte = clear pending
 * ================================================================ */
static void test_jerry_jintctrl_multi_pending_selective_clear(void)
{
   uint16_t pending;
   uint8_t saved_mask = 0;

   printf("\n=== Test 9c: JERRY JINTCTRL Multi-Source Pending ===\n");

   if (p_JERRYIRQEnabled(IRQ2_EXTERNAL)) saved_mask |= IRQ2_EXTERNAL;
   if (p_JERRYIRQEnabled(IRQ2_DSP))      saved_mask |= IRQ2_DSP;
   if (p_JERRYIRQEnabled(IRQ2_TIMER1))   saved_mask |= IRQ2_TIMER1;
   if (p_JERRYIRQEnabled(IRQ2_TIMER2))   saved_mask |= IRQ2_TIMER2;
   if (p_JERRYIRQEnabled(IRQ2_ASI))      saved_mask |= IRQ2_ASI;
   if (p_JERRYIRQEnabled(IRQ2_SSI))      saved_mask |= IRQ2_SSI;

   /* Word write: high byte clears pending bits, low byte replaces mask.
    * 0x3F00 = clear all 6 IRQ pending sources, mask = 0. */
   p_JERRYWriteWord(JERRY_JINTCTRL, 0x3F00, WHO_M68K);
   pending = p_JERRYReadWord(JERRY_JINTCTRL, WHO_M68K);
   if ((pending & 0x3F) == 0)
      PASS("baseline: all pending cleared");
   else
      FAIL("baseline pending = $%04X (expected 0)", pending);

   p_JERRYSetPendingIRQ(IRQ2_TIMER1);
   p_JERRYSetPendingIRQ(IRQ2_TIMER2);
   pending = p_JERRYReadWord(JERRY_JINTCTRL, WHO_M68K);
   if ((pending & (IRQ2_TIMER1 | IRQ2_TIMER2)) == (IRQ2_TIMER1 | IRQ2_TIMER2))
      PASS("timer1+timer2 latched together (pending=$%02X)", pending & 0xFF);
   else
      FAIL("pending = $%04X (expected timer1|timer2)", pending);

   /* Byte write to $F10021 sets mask only; pending must not change. */
   p_JERRYWriteByte(JERRY_JINTCTRL + 1, IRQ2_TIMER1, WHO_M68K);
   pending = p_JERRYReadWord(JERRY_JINTCTRL, WHO_M68K);
   if ((pending & (IRQ2_TIMER1 | IRQ2_TIMER2)) == (IRQ2_TIMER1 | IRQ2_TIMER2)
         && p_JERRYIRQEnabled(IRQ2_TIMER1)
         && !p_JERRYIRQEnabled(IRQ2_TIMER2))
      PASS("$F10021 mask write keeps pending intact");
   else
      FAIL("pending=$%04X t1ena=%d t2ena=%d", pending,
           p_JERRYIRQEnabled(IRQ2_TIMER1) ? 1 : 0,
           p_JERRYIRQEnabled(IRQ2_TIMER2) ? 1 : 0);

   /* Byte write to $F10020 clears matching pending bits; mask must not change. */
   p_JERRYWriteByte(JERRY_JINTCTRL, IRQ2_TIMER1, WHO_M68K);
   pending = p_JERRYReadWord(JERRY_JINTCTRL, WHO_M68K);
   if ((pending & IRQ2_TIMER1) == 0
         && (pending & IRQ2_TIMER2) == IRQ2_TIMER2
         && p_JERRYIRQEnabled(IRQ2_TIMER1))
      PASS("byte clear of timer1 leaves timer2 latched and mask intact");
   else
      FAIL("pending=$%04X t1ena=%d (expected timer2 only, mask unchanged)",
           pending, p_JERRYIRQEnabled(IRQ2_TIMER1) ? 1 : 0);

   /* Word write at $F10020: high byte clears timer2; low byte replaces mask. */
   p_JERRYWriteWord(JERRY_JINTCTRL, ((uint16_t)IRQ2_TIMER2 << 8) | IRQ2_DSP, WHO_M68K);
   pending = p_JERRYReadWord(JERRY_JINTCTRL, WHO_M68K);
   if ((pending & 0x3F) == 0
         && p_JERRYIRQEnabled(IRQ2_DSP)
         && !p_JERRYIRQEnabled(IRQ2_TIMER1))
      PASS("word write clears pending and replaces mask");
   else
      FAIL("pending=$%04X mask: dsp=%d t1=%d", pending,
           p_JERRYIRQEnabled(IRQ2_DSP) ? 1 : 0,
           p_JERRYIRQEnabled(IRQ2_TIMER1) ? 1 : 0);

   /* Restore: clear any stray pending and replace mask with original. */
   p_JERRYWriteWord(JERRY_JINTCTRL, 0x3F00 | saved_mask, WHO_M68K);
}

/* ================================================================
 * Test 9d: GPU IRQ Latch & Re-dispatch
 * Pin the GPU's interrupt latch / enable / dispatch behavior. The
 * latch lives in gpu_control bits 6..10, mirrored as INT_LAT0..4.
 * GPUSetIRQLine(line, ASSERT_LINE) sets bit (6+line); the latch is
 * sticky until SW writes the matching CINTxFLAG bit (bits 9..13) to
 * gpu_flags via the memory-mapped F02100 path. With GPU off (HLE
 * mode) and INT_ENA cleared, GPUHandleIRQs must NOT advance gpu_pc.
 *
 * We deliberately keep INT_ENA0..4 = 0 so HandleIRQs takes the
 * "!bits" early-out and never dispatches; this lets us inspect the
 * latch directly without perturbing R31/SP or GPU RAM.
 *
 * Observables (memory-mapped via GPUReadLong):
 *   $F02100 - gpu_flags (returns gpu_flags & 0xFFFFC1FF)
 *   $F02110 - gpu_pc
 *   $F02114 - gpu_control (latch bits 6..10 visible)
 * ================================================================ */
#define ASSERT_LINE_LOCAL 1
#define CLEAR_LINE_LOCAL  0
static void test_gpu_irq_latch_redispatch(void)
{
   uint32_t saved_flags;
   uint32_t saved_control;
   uint32_t saved_pc;
   uint32_t ctrl;
   uint32_t pc_before;
   uint32_t pc_after;

   printf("\n=== Test 9d: GPU IRQ Latch & Re-dispatch ===\n");

   /* Save state so we don't perturb later tests. */
   saved_flags = p_GPUReadLong(0xF02100, WHO_M68K);
   saved_control = p_GPUReadLong(0xF02114, WHO_M68K);
   saved_pc = p_gpu_pc ? *p_gpu_pc : p_GPUReadLong(0xF02110, WHO_M68K);

   /* Establish baseline: clear gpu_flags (INT_ENA all 0, IMASK 0)
    * via the F02100 path, and clear any stray latch bits with
    * CINT04FLAGS. GPUWriteLong applies CINT bits to gpu_control. */
   p_GPUWriteLong(0xF02100, 0x00003E00, WHO_M68K); /* clear all CINTxFLAG */
   p_GPUWriteLong(0xF02100, 0x00000000, WHO_M68K); /* INT_ENA=0, IMASK=0 */
   pc_before = p_gpu_pc ? *p_gpu_pc : p_GPUReadLong(0xF02110, WHO_M68K);

   /* --- Sub-assert 1: latch survives without enable --- */
   p_GPUSetIRQLine(0, ASSERT_LINE_LOCAL);
   ctrl = p_GPUReadLong(0xF02114, WHO_M68K);
   pc_after = p_gpu_pc ? *p_gpu_pc : p_GPUReadLong(0xF02110, WHO_M68K);
   if ((ctrl & 0x40) && pc_after == pc_before)
      PASS("IRQ0 latch set without enable, gpu_pc stable");
   else
      FAIL("ctrl=$%08X pc:%08X->%08X (want bit6 set, pc unchanged)",
           ctrl, pc_before, pc_after);

   /* --- Sub-assert 2: idempotent latch (set twice, still bit6 only) --- */
   p_GPUSetIRQLine(0, ASSERT_LINE_LOCAL);
   ctrl = p_GPUReadLong(0xF02114, WHO_M68K);
   if ((ctrl & 0x7C0) == 0x40)
      PASS("re-asserting IRQ0 leaves only bit6 set in latch");
   else
      FAIL("ctrl=$%08X after second assert (want only bit6 in 6..10)", ctrl);

   /* --- Sub-assert 3: multi-source latch holds independent bits --- */
   p_GPUSetIRQLine(4, ASSERT_LINE_LOCAL);
   ctrl = p_GPUReadLong(0xF02114, WHO_M68K);
   if ((ctrl & 0x7C0) == (0x40 | 0x400))
      PASS("IRQ0 and IRQ4 both latched (bits 6 and 10)");
   else
      FAIL("ctrl=$%08X (want bits 6+10 set, others clear in 6..10)", ctrl);

   /* --- Sub-assert 4: gpu_pc invariant under latches without enable --- */
   pc_after = p_gpu_pc ? *p_gpu_pc : p_GPUReadLong(0xF02110, WHO_M68K);
   if (pc_after == pc_before)
      PASS("gpu_pc unchanged through 3 GPUSetIRQLine calls (IMASK gate)");
   else
      FAIL("gpu_pc moved %08X->%08X without dispatch", pc_before, pc_after);

   /* --- Sub-assert 5: explicit CLEAR_LINE clears that latch bit --- */
   p_GPUSetIRQLine(0, CLEAR_LINE_LOCAL);
   ctrl = p_GPUReadLong(0xF02114, WHO_M68K);
   if (!(ctrl & 0x40) && (ctrl & 0x400))
      PASS("CLEAR_LINE on IRQ0 clears bit6, IRQ4 latch retained");
   else
      FAIL("ctrl=$%08X (want bit6 clear, bit10 set)", ctrl);

   /* --- Sub-assert 6: SW clear via CINTxFLAG write to gpu_flags --- */
   /* Write CINT4FLAG (0x2000) to gpu_flags; this should clear latch bit 10
    * via the gpu_control &= ~((gpu_flags & CINT04FLAGS) >> 3) path. */
   p_GPUWriteLong(0xF02100, 0x00002000, WHO_M68K);
   ctrl = p_GPUReadLong(0xF02114, WHO_M68K);
   if (!(ctrl & 0x400))
      PASS("CINT4FLAG write to gpu_flags clears IRQ4 latch (bit10)");
   else
      FAIL("ctrl=$%08X after CINT4FLAG write (want bit10 clear)", ctrl);

   /* Calling GPUHandleIRQs with no latched+enabled IRQs is a safe no-op. */
   pc_before = p_gpu_pc ? *p_gpu_pc : p_GPUReadLong(0xF02110, WHO_M68K);
   p_GPUHandleIRQs();
   pc_after = p_gpu_pc ? *p_gpu_pc : p_GPUReadLong(0xF02110, WHO_M68K);
   if (pc_after == pc_before)
      PASS("GPUHandleIRQs with no enabled latches is a no-op");
   else
      FAIL("gpu_pc moved %08X->%08X via HandleIRQs no-op", pc_before, pc_after);

   /* Restore state. Clear any stray latch first, then restore the
    * pre-test gpu_flags and gpu_control. We can only fully restore
    * gpu_flags if we have a path to set IMASK (we don't) -- but
    * IMASK is 0 in HLE mode, so the visible portion (lower 14 bits
    * minus IMASK quirks) round-trips cleanly. */
   p_GPUWriteLong(0xF02100, 0x00003E00, WHO_M68K); /* clear all CINTxFLAG */
   p_GPUWriteLong(0xF02100, saved_flags & ~0x00003E00u, WHO_M68K);
   if (p_gpu_pc)
      *p_gpu_pc = saved_pc;
   /* gpu_control low bits writable; latch (F7C0) is masked off on write
    * but we already cleared the latch above, matching saved baseline. */
   p_GPUWriteLong(0xF02114, saved_control & ~0xF7C0u, WHO_M68K);
}

/* ================================================================
 * Test 9e: DSP IRQ Latch & Re-dispatch
 * DSP analog of Test 9d. The DSP shares the GPU RISC instruction set
 * but has 6 IRQ lines (vs GPU's 5). Latch layout in dsp_control:
 *   bits 6..10  = INT_LAT0..INT_LAT4
 *   bit  16     = INT_LAT5  (NON-CONTIGUOUS with LAT0..4)
 * Enable mask in dsp_flags:
 *   bits 4..8   = INT_ENA0..INT_ENA4
 *   bit  16     = INT_ENA5
 *   bit  3      = IMASK     (gates dispatch; HLE leaves it 0)
 * SW clear path: writing CINTxFLAG bits to dsp_flags ($F1A100) clears
 * the matching dsp_control latch via:
 *   dsp_control &= ~((dsp_flags & CINT04FLAGS) >> 3)   (bits 9..13 -> 6..10)
 *   dsp_control &= ~((dsp_flags & CINT5FLAG)   >> 1)   (bit 17 -> 16)
 *
 * As in Test 9d, we keep all INT_ENAx = 0 so DSPHandleIRQsNP() takes
 * the early-out and never dispatches; this lets us inspect the latch
 * directly without perturbing R30/R31/dsp_pc.
 *
 * Observables (memory-mapped via DSPReadLong):
 *   $F1A100 - dsp_flags (returned as dsp_flags & 0xFFFFC1FF)
 *   $F1A110 - dsp_pc
 *   $F1A114 - dsp_control (latch bits 6..10, 16 visible)
 *
 * Note: dsp_flags is a file-static in src/jerry/dsp.c and is not
 * dlsym-able, so we observe/mutate it only through the $F1A100 path.
 * dsp_pc and dsp_control are exported globals and are pinned via
 * dlsym for direct inspection (with $F1A110/$F1A114 fallbacks).
 * ================================================================ */
static void test_dsp_irq_latch_redispatch(void)
{
   uint32_t saved_flags;
   uint32_t saved_control;
   uint32_t saved_pc;
   uint32_t ctrl;
   uint32_t pc_before;
   uint32_t pc_after;

   printf("\n=== Test 9e: DSP IRQ Latch & Re-dispatch ===\n");

   /* Save state so we don't perturb later tests. */
   saved_flags = p_DSPReadLong(0xF1A100, WHO_M68K);
   saved_control = p_DSPReadLong(0xF1A114, WHO_M68K);
   saved_pc = p_dsp_pc ? *p_dsp_pc : p_DSPReadLong(0xF1A110, WHO_M68K);

   /* Establish baseline: clear all CINTxFLAGs (which also clears any
    * stray latch bits in dsp_control via the SW clear path), then set
    * dsp_flags = 0 (INT_ENAx all 0, IMASK 0).
    * CINT0..4FLAG are bits 9..13 (mask 0x3E00); CINT5FLAG is bit 17
    * (0x20000). */
   p_DSPWriteLong(0xF1A100, 0x00023E00, WHO_M68K); /* clear CINT0..5 latches */
   p_DSPWriteLong(0xF1A100, 0x00000000, WHO_M68K); /* INT_ENA=0, IMASK=0 */
   pc_before = p_dsp_pc ? *p_dsp_pc : p_DSPReadLong(0xF1A110, WHO_M68K);

   /* --- Sub-assert 1: latch survives without enable --- */
   p_DSPSetIRQLine(0, ASSERT_LINE_LOCAL);
   ctrl = p_DSPReadLong(0xF1A114, WHO_M68K);
   pc_after = p_dsp_pc ? *p_dsp_pc : p_DSPReadLong(0xF1A110, WHO_M68K);
   if ((ctrl & 0x40) && pc_after == pc_before)
      PASS("IRQ0 latch set without enable, dsp_pc stable");
   else
      FAIL("ctrl=$%08X pc:%08X->%08X (want bit6 set, pc unchanged)",
           ctrl, pc_before, pc_after);

   /* --- Sub-assert 2: idempotent latch (set twice, only bit6 in 6..10) --- */
   p_DSPSetIRQLine(0, ASSERT_LINE_LOCAL);
   ctrl = p_DSPReadLong(0xF1A114, WHO_M68K);
   if ((ctrl & 0x7C0) == 0x40 && !(ctrl & 0x10000))
      PASS("re-asserting IRQ0 leaves only bit6 set in latch");
   else
      FAIL("ctrl=$%08X after second assert (want only bit6 in {6..10,16})",
           ctrl);

   /* --- Sub-assert 3: multi-source latch holds independent bits ---
    * IRQ5 latches at bit 16, NOT bit 11 (DSP layout is non-contiguous). */
   p_DSPSetIRQLine(5, ASSERT_LINE_LOCAL);
   ctrl = p_DSPReadLong(0xF1A114, WHO_M68K);
   if ((ctrl & 0x40) && (ctrl & 0x10000) && (ctrl & 0x780) == 0)
      PASS("IRQ0 and IRQ5 both latched (bits 6 and 16)");
   else
      FAIL("ctrl=$%08X (want bits 6+16 set, bits 7..10 clear)", ctrl);

   /* --- Sub-assert 4: dsp_pc invariant under latches without enable --- */
   pc_after = p_dsp_pc ? *p_dsp_pc : p_DSPReadLong(0xF1A110, WHO_M68K);
   if (pc_after == pc_before)
      PASS("dsp_pc unchanged through 3 DSPSetIRQLine calls (IMASK gate)");
   else
      FAIL("dsp_pc moved %08X->%08X without dispatch", pc_before, pc_after);

   /* --- Sub-assert 5: explicit CLEAR_LINE clears that latch bit --- */
   p_DSPSetIRQLine(0, CLEAR_LINE_LOCAL);
   ctrl = p_DSPReadLong(0xF1A114, WHO_M68K);
   if (!(ctrl & 0x40) && (ctrl & 0x10000))
      PASS("CLEAR_LINE on IRQ0 clears bit6, IRQ5 latch retained");
   else
      FAIL("ctrl=$%08X (want bit6 clear, bit16 set)", ctrl);

   /* --- Sub-assert 6: SW clear via CINT5FLAG write to dsp_flags ---
    * Write CINT5FLAG (0x20000) to dsp_flags; this should clear latch
    * bit 16 via the dsp_control &= ~((dsp_flags & CINT5FLAG) >> 1)
    * path. */
   p_DSPWriteLong(0xF1A100, 0x00020000, WHO_M68K);
   ctrl = p_DSPReadLong(0xF1A114, WHO_M68K);
   if (!(ctrl & 0x10000))
      PASS("CINT5FLAG write to dsp_flags clears IRQ5 latch (bit16)");
   else
      FAIL("ctrl=$%08X after CINT5FLAG write (want bit16 clear)", ctrl);

   /* --- Sub-assert 7: DSPHandleIRQsNP no-op when nothing enabled --- */
   pc_before = p_dsp_pc ? *p_dsp_pc : p_DSPReadLong(0xF1A110, WHO_M68K);
   p_DSPHandleIRQsNP();
   pc_after = p_dsp_pc ? *p_dsp_pc : p_DSPReadLong(0xF1A110, WHO_M68K);
   if (pc_after == pc_before)
      PASS("DSPHandleIRQsNP with no enabled latches is a no-op");
   else
      FAIL("dsp_pc moved %08X->%08X via HandleIRQs no-op",
           pc_before, pc_after);

   /* Restore state. Clear any stray latch first (CINT0..5), then
    * restore the pre-test dsp_flags and dsp_control. We can only
    * fully restore dsp_flags if we have a path to set IMASK -- but
    * IMASK is 0 in HLE mode, so the visible portion round-trips. */
   p_DSPWriteLong(0xF1A100, 0x00023E00, WHO_M68K); /* clear all CINTxFLAG */
   p_DSPWriteLong(0xF1A100, saved_flags & ~0x00023E00u, WHO_M68K);
   if (p_dsp_pc)
      *p_dsp_pc = saved_pc;
   /* dsp_control: VERSION and INT_LATx bits are protected on write
    * (mask in DSPWriteLong); we already cleared the latches, matching
    * the saved baseline for the unprotected portion. */
   p_DSPWriteLong(0xF1A114, saved_control & ~0x0001F7C0u, WHO_M68K);
}

/* ================================================================
 * Test 9b: JERRY I2S Defaults
 * HLE configures I2S so DSP SSI interrupts are available before games
 * install their own DSP programs.
 * ================================================================ */
static void test_jerry_i2s_defaults(void)
{
   uint8_t sclk;
   uint16_t sstat;
   uint32_t smode;

   printf("\n=== Test 9b: JERRY I2S Defaults ===\n");

   sclk = **p_sclk;
   sstat = p_JERRYReadWord(JERRY_SCLK, WHO_M68K);
   smode = **p_smode;

   if (sclk == 0x0008)
      PASS("SCLK = $%02X", sclk);
   else
      FAIL("SCLK = $%02X (expected $08)", sclk);

   if (sstat == 0x0000)
      PASS("SSTAT = $%04X", sstat);
   else
      FAIL("SSTAT = $%04X (expected $0000)", sstat);

   if (smode == 0x0001)
      PASS("SMODE = $%08X", smode);
   else
      FAIL("SMODE = $%08X (expected $00000001)", smode);
}

/* ================================================================
 * Test 10: TOM NTSC Video Timing Registers
 * Verify all TOM video registers match expected values.
 * ================================================================ */
static void test_tom_video_registers(void)
{
   bool ntsc;

   printf("\n=== Test 10: TOM Video Timing Registers ===\n");

   ntsc = p_vjs->hardwareTypeNTSC;
   printf("  Mode: %s\n", ntsc ? "NTSC" : "PAL");

#define CHECK_TOM(name, offset, expected) do { \
   uint16_t val = tom_get16(offset); \
   if (val == (expected)) \
      PASS(name " = %u", val); \
   else \
      FAIL(name " = %u (expected %u)", val, (unsigned)(expected)); \
} while(0)

   CHECK_TOM("HP",   TOM_HP,   ntsc ? 844 : 850);
   CHECK_TOM("HBB",  TOM_HBB,  ntsc ? 1713 : 1711);
   CHECK_TOM("HBE",  TOM_HBE,  ntsc ? 125 : 158);
   CHECK_TOM("HS",   TOM_HS,   ntsc ? 1741 : 1749);
   CHECK_TOM("HVS",  TOM_HVS,  ntsc ? 651 : 601);
   CHECK_TOM("HDB1", TOM_HDB1, 203);
   CHECK_TOM("HDB2", TOM_HDB2, 203);
   CHECK_TOM("HDE",  TOM_HDE,  1665);
   CHECK_TOM("VP",   TOM_VP,   ntsc ? 523 : 623);
   CHECK_TOM("VBB",  TOM_VBB,  ntsc ? 500 : 600);
   CHECK_TOM("VBE",  TOM_VBE,  ntsc ? 24 : 34);
   CHECK_TOM("VS",   TOM_VS,   ntsc ? 517 : 618);
   CHECK_TOM("VDB",  TOM_VDB,  38);
   CHECK_TOM("VDE",  TOM_VDE,  518);
   CHECK_TOM("VEB",  TOM_VEB,  ntsc ? 511 : 613);
   CHECK_TOM("VEE",  TOM_VEE,  6);
   CHECK_TOM("VI",   TOM_VI,   0);  /* left at 0; (vc>0) guard disables until game sets it */
   CHECK_TOM("HEQ",  TOM_HEQ,  ntsc ? 784 : 787);
   CHECK_TOM("BG",   TOM_BG,   0);
   CHECK_TOM("VMODE", TOM_VMODE, 0x06C1);

#undef CHECK_TOM
}

/* ================================================================
 * Test 10b: TOM VP controls frame rollover
 * Games may program custom vertical periods. The halfline scheduler
 * must honor TOM VP instead of a hard-coded NTSC/PAL frame length.
 * ================================================================ */
static void test_tom_vp_rollover(void)
{
   uint16_t old_vp;
   uint16_t old_vc;
   uint16_t vc;
   bool old_lower_field;

   printf("\n=== Test 10b: TOM VP Rollover ===\n");

   old_vp = tom_get16(TOM_VP);
   old_vc = tom_get16(TOM_VC);
   old_lower_field = *p_lowerField;

   p_TOMWriteWord(0xF0003E, 7, WHO_M68K);
   p_TOMWriteWord(0xF00006, 7, WHO_M68K);
   *p_lowerField = false;

   p_HalflineCallback();
   vc = tom_get16(TOM_VC);

   if (vc == 0x0800 && *p_lowerField)
      PASS("VC rolled over at custom VP and toggled lower field");
   else
      FAIL("VC after custom VP rollover = $%04X lowerField=%d", vc, *p_lowerField ? 1 : 0);

   p_TOMWriteWord(0xF0003E, old_vp, WHO_M68K);
   p_TOMWriteWord(0xF00006, old_vc, WHO_M68K);
   *p_lowerField = old_lower_field;
}

/* ================================================================
 * Test 10c: TOM video IRQ latch with disabled CPU enable
 * IRQ sources latch even when their CPU enable bit is clear; enabling
 * only gates whether the pending source asserts IPL2.
 * ================================================================ */
static void test_tom_video_irq_latches_when_disabled(void)
{
   uint16_t old_vp;
   uint16_t old_vc;
   uint16_t old_vi;
   uint16_t pending;
   bool old_lower_field;

   printf("\n=== Test 10c: TOM Video IRQ Latches While Disabled ===\n");

   old_vp = tom_get16(TOM_VP);
   old_vc = tom_get16(TOM_VC);
   old_vi = tom_get16(TOM_VI);
   old_lower_field = *p_lowerField;

   p_TOMWriteWord(0xF000E0, 0x0100, WHO_M68K);
   p_TOMWriteWord(0xF000E0, 0x0000, WHO_M68K);
   p_TOMWriteWord(0xF0003E, 7, WHO_M68K);
   p_TOMWriteWord(0xF0004E, 3, WHO_M68K);
   p_TOMWriteWord(0xF00006, 2, WHO_M68K);
   *p_lowerField = false;

   p_HalflineCallback();
   pending = p_TOMReadWord(0xF000E0, WHO_M68K);

   if (pending & 0x0001)
      PASS("video IRQ source latched while CPU enable was disabled");
   else
      FAIL("INT1 pending = $%04X (expected video bit set)", pending);

   p_TOMWriteWord(0xF000E0, 0x0100, WHO_M68K);
   p_TOMWriteWord(0xF0003E, old_vp, WHO_M68K);
   p_TOMWriteWord(0xF0004E, old_vi, WHO_M68K);
   p_TOMWriteWord(0xF00006, old_vc, WHO_M68K);
   *p_lowerField = old_lower_field;
}

/* ================================================================
 * Test 10d: TOM INT1 byte clear
 * Some software clears TOM IRQ sources with byte writes to INT1's
 * high byte. Those writes must clear the pending latch just like
 * word writes do.
 * ================================================================ */
static void test_tom_int1_byte_write_clears_pending(void)
{
   uint16_t old_vp;
   uint16_t old_vc;
   uint16_t old_vi;
   uint16_t pending;
   bool old_lower_field;

   printf("\n=== Test 10d: TOM INT1 Byte Clear ===\n");

   old_vp = tom_get16(TOM_VP);
   old_vc = tom_get16(TOM_VC);
   old_vi = tom_get16(TOM_VI);
   old_lower_field = *p_lowerField;

   p_TOMWriteWord(0xF000E0, 0x0100, WHO_M68K);
   p_TOMWriteWord(0xF000E0, 0x0000, WHO_M68K);
   p_TOMWriteWord(0xF0003E, 7, WHO_M68K);
   p_TOMWriteWord(0xF0004E, 3, WHO_M68K);
   p_TOMWriteWord(0xF00006, 2, WHO_M68K);
   *p_lowerField = false;

   p_HalflineCallback();
   pending = p_TOMReadWord(0xF000E0, WHO_M68K);

   if (pending & 0x0001)
      PASS("video IRQ source latched before byte clear");
   else
      FAIL("INT1 pending = $%04X (expected video bit before clear)", pending);

   if (p_TOMReadByte(0xF000E0, WHO_M68K) == 0
       && (p_TOMReadByte(0xF000E1, WHO_M68K) & 0x01))
      PASS("INT1 byte reads expose pending source bits");
   else
      FAIL("INT1 byte reads high=$%02X low=$%02X",
            p_TOMReadByte(0xF000E0, WHO_M68K),
            p_TOMReadByte(0xF000E1, WHO_M68K));

   p_TOMWriteByte(0xF000E0, 0x01, WHO_M68K);
   pending = p_TOMReadWord(0xF000E0, WHO_M68K);

   if (!(pending & 0x0001))
      PASS("INT1 high-byte write cleared video IRQ source");
   else
      FAIL("INT1 pending = $%04X (expected video bit clear)", pending);

   p_TOMWriteWord(0xF000E0, 0x0100, WHO_M68K);
   p_TOMWriteWord(0xF0003E, old_vp, WHO_M68K);
   p_TOMWriteWord(0xF0004E, old_vi, WHO_M68K);
   p_TOMWriteWord(0xF00006, old_vc, WHO_M68K);
   *p_lowerField = old_lower_field;
}

/* ================================================================
 * Test 10f: TOM INT1 multiple pending sources
 * Software can have video and timer (or other) sources latched at once.
 * Byte reads expose combined pending bits; high-byte clears are selective.
 * ================================================================ */
static void test_tom_int1_multi_pending_selective_clear(void)
{
   uint16_t old_int1;
   uint16_t pending;

   printf("\n=== Test 10f: TOM INT1 Multi-Source Pending ===\n");

   old_int1 = tom_get16(TOM_INT1);

   p_TOMWriteWord(0xF000E0, 0x1F00, WHO_M68K);
   p_TOMWriteWord(0xF000E0, 0x0000, WHO_M68K);

   p_TOMSetPendingVideoInt();
   p_TOMSetPendingTimerInt();
   pending = p_TOMReadWord(0xF000E0, WHO_M68K);

   if ((pending & 0x09) == 0x09)
      PASS("video and timer IRQ sources both latched");
   else
      FAIL("INT1 pending = $%04X (expected video+timer bits)", pending);

   p_TOMWriteByte(0xF000E1, 0x01, WHO_M68K);
   pending = p_TOMReadWord(0xF000E0, WHO_M68K);
   if ((pending & 0x09) == 0x09)
      PASS("enabling video only does not clear timer latch");
   else
      FAIL("INT1 pending = $%04X after enable write", pending);

   p_TOMWriteByte(0xF000E0, 0x01, WHO_M68K);
   pending = p_TOMReadWord(0xF000E0, WHO_M68K);
   if ((pending & 0x08) == 0x08 && (pending & 0x01) == 0)
      PASS("cleared video pending, timer still latched");
   else
      FAIL("INT1 pending = $%04X (expected timer only)", pending);

   p_TOMWriteByte(0xF000E0, 0x08, WHO_M68K);
   pending = p_TOMReadWord(0xF000E0, WHO_M68K);
   if ((pending & 0x1F) == 0)
      PASS("cleared timer pending");
   else
      FAIL("INT1 pending = $%04X (expected no sources)", pending);

   p_TOMWriteWord(0xF000E0, old_int1, WHO_M68K);
}

/* ================================================================
 * Test 10h: TOM PIT Reload Semantics
 * Pin down the byte/word write decode for the TOM PIT prescaler ($F00050)
 * and divider ($F00052). The PIT is observable through the same register
 * pair: word reads return the full 16-bit value, byte reads return the
 * high or low byte respectively. Writing zero to either field disables
 * the PIT (TOMResetPIT removes the scheduled callback); rewriting a
 * non-zero value re-arms it. We exercise the side effect by toggling
 * through several configurations and confirming the read-back register
 * state matches the write semantics in tom.c.
 * ================================================================ */
static void test_tom_pit_reload_semantics(void)
{
   uint16_t old_pre;
   uint16_t old_div;
   uint16_t v;
   uint8_t  b;
   int i;

   printf("\n=== Test 10h: TOM PIT Reload Semantics ===\n");

   old_pre = p_TOMReadWord(0xF00050, WHO_M68K);
   old_div = p_TOMReadWord(0xF00052, WHO_M68K);

   /* Disable PIT first so we have a known baseline. */
   p_TOMWriteWord(0xF00050, 0x0000, WHO_M68K);
   p_TOMWriteWord(0xF00052, 0x0000, WHO_M68K);

   v = p_TOMReadWord(0xF00050, WHO_M68K);
   if (v == 0x0000)
      PASS("prescaler=0 disables PIT, read-back=$0000");
   else
      FAIL("prescaler read-back after zero write = $%04X", v);

   v = p_TOMReadWord(0xF00052, WHO_M68K);
   if (v == 0x0000)
      PASS("divider=0 disables PIT, read-back=$0000");
   else
      FAIL("divider read-back after zero write = $%04X", v);

   /* Word write to $F00050 sets the full 16-bit prescaler. */
   p_TOMWriteWord(0xF00050, 0x1234, WHO_M68K);
   v = p_TOMReadWord(0xF00050, WHO_M68K);
   if (v == 0x1234)
      PASS("word write $F00050 sets prescaler=$1234");
   else
      FAIL("prescaler word read-back = $%04X (want $1234)", v);

   /* Byte reads at $F00050/$F00051 expose high/low halves. */
   b = p_TOMReadByte(0xF00050, WHO_M68K);
   if (b == 0x12)
      PASS("byte read $F00050 returns prescaler high byte ($12)");
   else
      FAIL("byte read $F00050 = $%02X (want $12)", b);

   b = p_TOMReadByte(0xF00051, WHO_M68K);
   if (b == 0x34)
      PASS("byte read $F00051 returns prescaler low byte ($34)");
   else
      FAIL("byte read $F00051 = $%02X (want $34)", b);

   /* Word write to $F00052 sets divider; byte reads mirror. */
   p_TOMWriteWord(0xF00052, 0xABCD, WHO_M68K);
   v = p_TOMReadWord(0xF00052, WHO_M68K);
   if (v == 0xABCD)
      PASS("word write $F00052 sets divider=$ABCD");
   else
      FAIL("divider word read-back = $%04X (want $ABCD)", v);

   b = p_TOMReadByte(0xF00052, WHO_M68K);
   if (b == 0xAB)
      PASS("byte read $F00052 returns divider high byte ($AB)");
   else
      FAIL("byte read $F00052 = $%02X (want $AB)", b);

   b = p_TOMReadByte(0xF00053, WHO_M68K);
   if (b == 0xCD)
      PASS("byte read $F00053 returns divider low byte ($CD)");
   else
      FAIL("byte read $F00053 = $%02X (want $CD)", b);

   /* Byte write to $F00050 replaces only the prescaler high byte. */
   p_TOMWriteWord(0xF00050, 0x1122, WHO_M68K);
   p_TOMWriteByte(0xF00050, 0x99, WHO_M68K);
   v = p_TOMReadWord(0xF00050, WHO_M68K);
   if (v == 0x9922)
      PASS("byte write $F00050 updates prescaler high byte only");
   else
      FAIL("after high-byte write, prescaler = $%04X (want $9922)", v);

   /* Byte write to $F00051 replaces only the prescaler low byte. */
   p_TOMWriteByte(0xF00051, 0x55, WHO_M68K);
   v = p_TOMReadWord(0xF00050, WHO_M68K);
   if (v == 0x9955)
      PASS("byte write $F00051 updates prescaler low byte only");
   else
      FAIL("after low-byte write, prescaler = $%04X (want $9955)", v);

   /* Byte write to $F00052 replaces only the divider high byte. */
   p_TOMWriteWord(0xF00052, 0x3344, WHO_M68K);
   p_TOMWriteByte(0xF00052, 0x77, WHO_M68K);
   v = p_TOMReadWord(0xF00052, WHO_M68K);
   if (v == 0x7744)
      PASS("byte write $F00052 updates divider high byte only");
   else
      FAIL("after high-byte write, divider = $%04X (want $7744)", v);

   /* Byte write to $F00053 replaces only the divider low byte. */
   p_TOMWriteByte(0xF00053, 0x66, WHO_M68K);
   v = p_TOMReadWord(0xF00052, WHO_M68K);
   if (v == 0x7766)
      PASS("byte write $F00053 updates divider low byte only");
   else
      FAIL("after low-byte write, divider = $%04X (want $7766)", v);

   /* Disabling via prescaler=0 leaves divider intact (no field cross-talk). */
   p_TOMWriteWord(0xF00050, 0xBEEF, WHO_M68K);
   p_TOMWriteWord(0xF00052, 0xCAFE, WHO_M68K);
   p_TOMWriteWord(0xF00050, 0x0000, WHO_M68K);
   v = p_TOMReadWord(0xF00050, WHO_M68K);
   if (v == 0x0000)
      PASS("prescaler clears to $0000 independently");
   else
      FAIL("prescaler after clear = $%04X", v);
   v = p_TOMReadWord(0xF00052, WHO_M68K);
   if (v == 0xCAFE)
      PASS("divider preserved while prescaler cleared");
   else
      FAIL("divider after prescaler clear = $%04X (want $CAFE)", v);

   /* Re-arming via word write replaces the prescaler cleanly. */
   p_TOMWriteWord(0xF00050, 0x00FF, WHO_M68K);
   v = p_TOMReadWord(0xF00050, WHO_M68K);
   if (v == 0x00FF)
      PASS("re-arming prescaler via word write yields $00FF");
   else
      FAIL("re-armed prescaler = $%04X (want $00FF)", v);

   /* Repeated reload writes do not corrupt the register state. */
   for (i = 0; i < 4; i++)
   {
      p_TOMWriteWord(0xF00050, (uint16_t)(0x0010 + i), WHO_M68K);
      p_TOMWriteWord(0xF00052, (uint16_t)(0x0020 + i), WHO_M68K);
   }
   v = p_TOMReadWord(0xF00050, WHO_M68K);
   if (v == 0x0013)
   {
      uint16_t v2 = p_TOMReadWord(0xF00052, WHO_M68K);
      if (v2 == 0x0023)
         PASS("repeated reloads land on the last written value");
      else
         FAIL("after repeated reloads, divider = $%04X (want $0023)", v2);
   }
   else
   {
      FAIL("after repeated reloads, prescaler = $%04X (want $0013)", v);
   }

   /* Restore previous values. */
   p_TOMWriteWord(0xF00050, old_pre, WHO_M68K);
   p_TOMWriteWord(0xF00052, old_div, WHO_M68K);
}

/* ================================================================
 * Test 10i: TOM Video Timing Register Symmetry
 * Pin byte/word read/write behavior of the 11-bit TOM video timing
 * registers VP ($F0003E), VDB ($F00046), VDE ($F00048),
 * HDB1 ($F00038), HDB2 ($F0003A), HDE ($F0003C).
 *
 * Observed behavior in src/tom/tom.c:
 *  - Word writes to offsets $30..$4E are masked to 11 bits (& $07FF)
 *    before being decomposed into two TOMWriteByte calls.
 *  - Byte writes go straight to tomRam8 (no mask), so a byte write to
 *    the high byte CAN deposit bits 11-15.
 *  - Reads are unconditional fetches from tomRam8.
 *
 * The test pins:
 *   1. Word write of an in-range pattern round-trips exactly.
 *   2. Word write of an out-of-range pattern is observed masked to 11 bits.
 *   3. Byte write to high byte updates only the high byte.
 *   4. Byte write to low byte updates only the low byte.
 *   5. Byte reads at offset N / N+1 mirror (word>>8)&0xFF / word&0xFF.
 *
 * Original word values are restored at the end so later tests are
 * not perturbed.
 * ================================================================ */
static void test_tom_video_timing_symmetry(void)
{
   /* Names + addresses for the six registers we exercise. */
   struct vt_reg { const char *name; uint32_t addr; };
   static const struct vt_reg regs[6] = {
      { "VP",   0xF0003E },
      { "VDB",  0xF00046 },
      { "VDE",  0xF00048 },
      { "HDB1", 0xF00038 },
      { "HDB2", 0xF0003A },
      { "HDE",  0xF0003C }
   };
   uint16_t saved[6];
   int i;
   uint16_t pat;
   uint16_t v;
   uint8_t  bh, bl;

   printf("\n=== Test 10i: TOM Video Timing Register Symmetry ===\n");

   /* Snapshot original values for restoration. */
   for (i = 0; i < 6; i++)
      saved[i] = p_TOMReadWord(regs[i].addr, WHO_M68K);

   /* (1) Word write of an in-range (11-bit) pattern round-trips exactly.
    * Use a different pattern per-register so no value gets lucky. */
   for (i = 0; i < 6; i++)
   {
      pat = (uint16_t)(0x0500 + (i * 0x11));   /* all <= $07FF */
      p_TOMWriteWord(regs[i].addr, pat, WHO_M68K);
      v = p_TOMReadWord(regs[i].addr, WHO_M68K);
      if (v == pat)
         PASS("%s: word write $%04X round-trips", regs[i].name, pat);
      else
         FAIL("%s: word write $%04X read-back $%04X",
            regs[i].name, pat, v);
   }

   /* (2) Word write of an out-of-range pattern is masked to 11 bits.
    * Pick one register (VP) to exercise the mask path. */
   p_TOMWriteWord(0xF0003E, 0xF234, WHO_M68K);
   v = p_TOMReadWord(0xF0003E, WHO_M68K);
   if (v == (0xF234 & 0x07FF))
      PASS("VP: word write $F234 masked to $%04X", v);
   else
      FAIL("VP: word write $F234 read-back $%04X (want $%04X)",
         v, 0xF234 & 0x07FF);

   /* (3) Byte write to the high byte updates the high half only.
    * After the masked $F234 write above, VP reads back $0234. We
    * write $05 to the high byte and expect $0534 (byte writes are
    * not masked, so writing $05 lands cleanly). */
   p_TOMWriteByte(0xF0003E, 0x05, WHO_M68K);
   v = p_TOMReadWord(0xF0003E, WHO_M68K);
   if (v == 0x0534)
      PASS("VP: byte write to high byte updates high half only");
   else
      FAIL("VP: after high-byte write, word=$%04X (want $0534)", v);

   /* (4) Byte write to the low byte updates the low half only. */
   p_TOMWriteByte(0xF0003F, 0xAB, WHO_M68K);
   v = p_TOMReadWord(0xF0003E, WHO_M68K);
   if (v == 0x05AB)
      PASS("VP: byte write to low byte updates low half only");
   else
      FAIL("VP: after low-byte write, word=$%04X (want $05AB)", v);

   /* (5) Byte reads at N / N+1 mirror the word value's halves.
    * Run this matrix for all six registers, after re-establishing a
    * known in-range word value per register. */
   for (i = 0; i < 6; i++)
   {
      pat = (uint16_t)(0x0640 + (i * 0x07));   /* in 11-bit range */
      p_TOMWriteWord(regs[i].addr, pat, WHO_M68K);
      v = p_TOMReadWord(regs[i].addr, WHO_M68K);
      bh = p_TOMReadByte(regs[i].addr, WHO_M68K);
      bl = p_TOMReadByte(regs[i].addr + 1, WHO_M68K);
      if (bh == ((v >> 8) & 0xFF) && bl == (v & 0xFF))
         PASS("%s: byte reads mirror word halves ($%02X $%02X)",
            regs[i].name, bh, bl);
      else
         FAIL("%s: byte reads $%02X $%02X vs word $%04X",
            regs[i].name, bh, bl, v);
   }

   /* Restore original values. (Mask to 11 bits to avoid asserting an
    * unrelated mask change here.) */
   for (i = 0; i < 6; i++)
      p_TOMWriteWord(regs[i].addr, saved[i] & 0x07FF, WHO_M68K);
}

/* ================================================================
 * Test 10l: TOM VMODE Register Bit-Field Read/Write ($F00028)
 *
 * Pin byte/word read/write behavior of the TOM VMODE register at
 * $F00028. Bit layout (per src/tom/tom.c lines 52-61, 345-347):
 *   bits 11-9 : PWIDTH (pixel width in clocks; value+1)
 *   bit 8     : VARMOD (mixed CRY/RGB16 mode)
 *   bit 7     : BGEN   (background enable)
 *   bit 3     : GENLOCK
 *   bits 2-1  : MODE  (00=CRY16, 01=RGB24, 10=DIRECT16, 11=RGB16)
 *   bit 0     : VIDEN
 *
 * Observed write-path behavior (src/tom/tom.c TOMWriteWord, lines
 * 1114-1205):
 *  - Word writes to $F00028 are NOT masked: the 11-bit mask at
 *    line 1175 only applies to offsets 0x30..0x4E, and the 10-bit
 *    masks (line 1177) target $2E/$36/$54. So bits 12-15 of a write
 *    to $28 land in tomRam8 just as written.
 *  - The word write is decomposed into two TOMWriteByte calls
 *    (lines 1181-1182) which deposit straight into tomRam8.
 *  - After the byte writes, code at lines 1191-1204 may recompute
 *    tomWidth/tomHeight; this is a side effect on geometry but does
 *    NOT alter the stored VMODE word.
 *  - Reads (TOMReadWord/Byte) for $28 are unconditional fetches
 *    from tomRam8 (lines 1033-1034, 991).
 *
 * Strategy: snapshot the live VMODE word and restore it at the end
 * so the running render loop is not destabilized. Use the lower
 * MODE bit (bit 1) as the CRY(0)/RGB(1) toggle and bits 11-9 as
 * PWIDTH.
 * ================================================================ */
static void test_tom_vmode_bitfields(void)
{
   const uint32_t addr_w = 0xF00028;
   const uint32_t addr_h = 0xF00028;   /* high byte */
   const uint32_t addr_l = 0xF00029;   /* low byte */
   uint16_t saved;
   uint16_t v;
   uint16_t pat;
   uint8_t  bh, bl;

   printf("\n=== Test 10l: TOM VMODE Bit-Field Read/Write ===\n");

   /* (1) Snapshot original VMODE so we can restore. */
   saved = p_TOMReadWord(addr_w, WHO_M68K);
   PASS("VMODE: snapshot original value $%04X", saved);

   /* (2) Word write: PWIDTH=4 (field=011 -> bits 11..9 = 0x0600),
    *     CRY mode (MODE bits 2..1 = 00), VIDEN=1, all other bits 0.
    *     Pattern: $0601. Verify exact round-trip (no masking). */
   pat = 0x0601;
   p_TOMWriteWord(addr_w, pat, WHO_M68K);
   v = p_TOMReadWord(addr_w, WHO_M68K);
   if (v == pat)
      PASS("VMODE: word write $%04X (PWIDTH=4,CRY) round-trips", pat);
   else
      FAIL("VMODE: word write $%04X read-back $%04X", pat, v);
   if ((v & 0x0E00) == 0x0600)
      PASS("VMODE: PWIDTH bits 11-9 preserved as 011");
   else
      FAIL("VMODE: PWIDTH bits = $%04X (want $0600)", v & 0x0E00);
   if ((v & 0x0006) == 0x0000)
      PASS("VMODE: MODE bits 2-1 preserved as CRY (00)");
   else
      FAIL("VMODE: MODE bits = $%04X (want $0000)", v & 0x0006);

   /* (3) Word write: PWIDTH=2 (field=001 -> $0200), RGB16 mode
    *     (MODE bits 2..1 = 11 -> $0006), VIDEN=1. Pattern: $0207. */
   pat = 0x0207;
   p_TOMWriteWord(addr_w, pat, WHO_M68K);
   v = p_TOMReadWord(addr_w, WHO_M68K);
   if (v == pat)
      PASS("VMODE: word write $%04X (PWIDTH=2,RGB16) round-trips", pat);
   else
      FAIL("VMODE: word write $%04X read-back $%04X", pat, v);
   if ((v & 0x0E00) == 0x0200 && (v & 0x0006) == 0x0006)
      PASS("VMODE: PWIDTH+MODE updated together");
   else
      FAIL("VMODE: PWIDTH/MODE = $%04X / $%04X",
         v & 0x0E00, v & 0x0006);

   /* (4) Byte write to high byte changes only PWIDTH/high-byte bits;
    *     low byte (MODE/VIDEN/etc.) untouched. Current word is
    *     $0207; write $08 to high byte -> expect $0807. */
   p_TOMWriteByte(addr_h, 0x08, WHO_M68K);
   v = p_TOMReadWord(addr_w, WHO_M68K);
   if (v == 0x0807)
      PASS("VMODE: byte write to high byte updates only high half");
   else
      FAIL("VMODE: after high-byte write, word=$%04X (want $0807)", v);

   /* (5) Byte write to low byte changes MODE/CRY-RGB bits;
    *     high byte (PWIDTH) untouched. Write $01 (CRY+VIDEN). */
   p_TOMWriteByte(addr_l, 0x01, WHO_M68K);
   v = p_TOMReadWord(addr_w, WHO_M68K);
   if (v == 0x0801)
      PASS("VMODE: byte write to low byte updates only low half");
   else
      FAIL("VMODE: after low-byte write, word=$%04X (want $0801)", v);
   if ((v & 0x0006) == 0x0000)
      PASS("VMODE: CRY mode (bit 1=0) set via low-byte write");
   else
      FAIL("VMODE: MODE bits = $%04X (want $0000)", v & 0x0006);

   /* (6) Byte reads at $28/$29 mirror (word>>8)&FF / word&FF. */
   bh = p_TOMReadByte(addr_h, WHO_M68K);
   bl = p_TOMReadByte(addr_l, WHO_M68K);
   if (bh == ((v >> 8) & 0xFF) && bl == (v & 0xFF))
      PASS("VMODE: byte reads mirror word halves ($%02X $%02X)", bh, bl);
   else
      FAIL("VMODE: byte reads $%02X $%02X vs word $%04X", bh, bl, v);

   /* (7) Restore original VMODE so the running render loop sees the
    *     same video mode it had before this test. */
   p_TOMWriteWord(addr_w, saved, WHO_M68K);
   v = p_TOMReadWord(addr_w, WHO_M68K);
   if (v == saved)
      PASS("VMODE: restored to original $%04X", saved);
   else
      FAIL("VMODE: restore failed, word=$%04X (want $%04X)", v, saved);
}

/* ================================================================
 * Test 10j: TOM IPL2 Reassert After Selective Clear
 *
 * Pin the contract that TOMAssertEnabledIRQs (re-)raises IPL2 on the
 * 68K whenever ANY enabled+pending TOM source remains, and does not
 * raise it when no source remains. Concretely: with both video and
 * timer enabled and pending, clearing video alone must keep IPL2
 * asserted because timer is still pending+enabled.
 *
 * Observability: src/m68000/m68kinterface.c routes m68k_set_irq through
 * m68k_set_irq2 only synchronously when regs.stopped is true, in which
 * case it sets regs.intLevel directly. We force regs.stopped=1 and
 * regs.intmask=7 so the level is recorded but never dispatched. We
 * clear regs.intLevel before each TOMAssertEnabledIRQs trigger and
 * inspect it afterward to see whether IPL2 was raised by that call.
 *
 * Limitation: TOMAssertEnabledIRQs only raises (m68k_set_irq) when a
 * source is pending+enabled; it never lowers the line. The 68K core
 * lowers intLevel only on interrupt acknowledge. So step 11 ("IPL2
 * deasserted after final clear") is observed indirectly: after we
 * zero regs.intLevel and trigger TOMAssertEnabledIRQs with no
 * remaining pending+enabled source, intLevel must STAY zero (i.e.
 * m68k_set_irq must NOT have been called).
 *
 * Trigger path: byte writes to $F000E0 invoke TOMClearPendingIRQs
 * followed by TOMAssertEnabledIRQs (see TOMWriteByte in src/tom/tom.c
 * around the INT1 case). Byte writes to $F000E1 (enable byte) also
 * call TOMAssertEnabledIRQs.
 * ================================================================ */
static void test_tom_ipl2_reassert_after_selective_clear(void)
{
   uint16_t old_int1;
   int      old_intmask;
   int      old_intLevel;
   uint8_t  old_stopped;

   printf("\n=== Test 10j: TOM IPL2 Reassert After Selective Clear ===\n");

   if (!p_regs)
   {
      FAIL("regs symbol not exported; cannot observe IPL2 line state");
      return;
   }

   /* Save state we will mutate. */
   old_int1     = tom_get16(TOM_INT1);
   old_intmask  = p_regs->intmask;
   old_intLevel = p_regs->intLevel;
   old_stopped  = p_regs->stopped;

   /* Force the synchronous IRQ path so m68k_set_irq writes regs.intLevel
    * directly via m68k_set_irq2, and mask all interrupts so the change
    * is recorded but no exception dispatches. */
   p_regs->stopped = 1;
   p_regs->intmask = 7;

   /* Clear any stale pending bits and program enables: video (bit 0) +
    * timer (bit 3) -> $09 in INT1+1 ($F000E1). Word write to $F000E0
    * with 0x1F00 high byte clears all pending sources. */
   p_TOMWriteWord(0xF000E0, 0x1F00, WHO_M68K);
   p_TOMWriteByte(0xF000E1, 0x09, WHO_M68K);

   /* (1) Latch video pending; should call m68k_set_irq(2). */
   p_regs->intLevel = 0;
   p_TOMSetPendingVideoInt();
   if (p_regs->intLevel == 2)
      PASS("video pending+enabled raises IPL2");
   else
      FAIL("after SetPendingVideoInt, intLevel=%d (want 2)", p_regs->intLevel);

   /* (2) Also latch timer pending; should also raise IPL2. */
   p_regs->intLevel = 0;
   p_TOMSetPendingTimerInt();
   if (p_regs->intLevel == 2)
      PASS("timer pending+enabled raises IPL2 with both sources latched");
   else
      FAIL("after SetPendingTimerInt, intLevel=%d (want 2)", p_regs->intLevel);

   /* Sanity: TOM still reports both sources pending in INT1 high byte. */
   {
      uint16_t pending = p_TOMReadWord(0xF000E0, WHO_M68K);
      if ((pending & 0x09) == 0x09)
         PASS("INT1 reports video+timer both pending pre-clear");
      else
         FAIL("INT1 pending=$%04X pre-clear (want video+timer bits)", pending);
   }

   /* (3) Clear video pending only via byte write to $F000E0 = $01. The
    * write triggers TOMAssertEnabledIRQs; timer is still pending+enabled,
    * so IPL2 must be reasserted. */
   p_regs->intLevel = 0;
   p_TOMWriteByte(0xF000E0, 0x01, WHO_M68K);
   if (p_regs->intLevel == 2)
      PASS("clearing video alone keeps IPL2 asserted (timer still pending)");
   else
      FAIL("after clearing video, intLevel=%d (want 2; timer still pending)",
           p_regs->intLevel);

   /* (4) Clear timer pending via byte write to $F000E0 = $08. Now
    * nothing is pending+enabled, so TOMAssertEnabledIRQs must NOT call
    * m68k_set_irq -- intLevel must stay 0. */
   p_regs->intLevel = 0;
   p_TOMWriteByte(0xF000E0, 0x08, WHO_M68K);
   if (p_regs->intLevel == 0)
      PASS("clearing last pending source leaves IPL2 unraised");
   else
      FAIL("after clearing timer (last source), intLevel=%d (want 0)",
           p_regs->intLevel);

   /* (5) Sanity post-clear: INT1 reports no pending sources. */
   {
      uint16_t pending = p_TOMReadWord(0xF000E0, WHO_M68K);
      if ((pending & 0x1F) == 0)
         PASS("INT1 reports no pending sources after both cleared");
      else
         FAIL("INT1 pending=$%04X post-clear (want 0)", pending);
   }

   /* Restore mutated state. */
   p_TOMWriteWord(0xF000E0, 0x1F00, WHO_M68K);   /* clear pending bits */
   p_TOMWriteWord(0xF000E0, old_int1, WHO_M68K); /* restore enables byte */
   p_regs->intLevel = old_intLevel;
   p_regs->intmask  = old_intmask;
   p_regs->stopped  = old_stopped;
}

/* ================================================================
 * Test 10k: PAL vs NTSC Video Timing Defaults
 *
 * Pin the documented difference between NTSC and PAL HLE init values
 * for the core vertical/horizontal timing registers. Source of truth
 * is TOMReset() in src/tom/tom.c (NTSC ~ lines 898-923, PAL ~ lines
 * 924-947): the two modes share VDB=38, VDE=518, HDB1=203, HDB2=203,
 * HDE=1665, VEE=6, VI=0, but differ on VP (523 vs 623), VBB, VBE, VS,
 * VEB, HP, HBB, HBE, HS, HVS, HEQ.
 *
 * This test runs in both passes (NTSC pre-load and PAL post-load) and
 * asserts:
 *   1) The mode-specific VP, VBB, VBE, VS values match the constants
 *      from TOMReset() exactly.
 *   2) The mode-INVARIANT defaults (VDB/VDE/HDB1/HDE/VI) are stable
 *      across PAL and NTSC.
 *   3) The PAL-vs-NTSC delta is internally consistent: PAL VP - NTSC VP
 *      = 100, PAL VBB - NTSC VBB = 100, matching ~50 extra scanlines.
 *      (Captured on the NTSC pass and re-checked on the PAL pass via
 *      file-scope statics so a single function covers both modes.)
 * ================================================================ */
static int    saw_ntsc_pass = 0;
static uint16_t saved_ntsc_vp  = 0;
static uint16_t saved_ntsc_vbb = 0;
static uint16_t saved_ntsc_vbe = 0;
static uint16_t saved_ntsc_vs  = 0;
static uint16_t saved_ntsc_veb = 0;
static uint16_t saved_ntsc_vdb = 0;
static uint16_t saved_ntsc_vde = 0;
static uint16_t saved_ntsc_hdb1 = 0;
static uint16_t saved_ntsc_hde  = 0;
static uint16_t saved_ntsc_vi   = 0;

static void test_video_timing_defaults_pal_ntsc(void)
{
   bool ntsc;
   uint16_t vp, vbb, vbe, vs, veb;
   uint16_t vdb, vde, hdb1, hde, vi;

   printf("\n=== Test 10k: PAL vs NTSC Video Timing Defaults ===\n");

   ntsc = p_vjs->hardwareTypeNTSC;
   printf("  Mode: %s\n", ntsc ? "NTSC" : "PAL");

   vp   = tom_get16(TOM_VP);
   vbb  = tom_get16(TOM_VBB);
   vbe  = tom_get16(TOM_VBE);
   vs   = tom_get16(TOM_VS);
   veb  = tom_get16(TOM_VEB);
   vdb  = tom_get16(TOM_VDB);
   vde  = tom_get16(TOM_VDE);
   hdb1 = tom_get16(TOM_HDB1);
   hde  = tom_get16(TOM_HDE);
   vi   = tom_get16(TOM_VI);

   if (ntsc)
   {
      /* Mode-specific values from TOMReset() NTSC branch. */
      if (vp == 523)   PASS("NTSC VP = 523 (524 vertical lines)"); else FAIL("NTSC VP = %u (want 523)", vp);
      if (vbb == 500)  PASS("NTSC VBB = 500"); else FAIL("NTSC VBB = %u (want 500)", vbb);
      if (vbe == 24)   PASS("NTSC VBE = 24");  else FAIL("NTSC VBE = %u (want 24)", vbe);
      if (vs == 517)   PASS("NTSC VS = 517");  else FAIL("NTSC VS = %u (want 517)", vs);
      if (veb == 511)  PASS("NTSC VEB = 511"); else FAIL("NTSC VEB = %u (want 511)", veb);

      /* Snapshot for the PAL pass to verify the documented delta. */
      saved_ntsc_vp   = vp;
      saved_ntsc_vbb  = vbb;
      saved_ntsc_vbe  = vbe;
      saved_ntsc_vs   = vs;
      saved_ntsc_veb  = veb;
      saved_ntsc_vdb  = vdb;
      saved_ntsc_vde  = vde;
      saved_ntsc_hdb1 = hdb1;
      saved_ntsc_hde  = hde;
      saved_ntsc_vi   = vi;
      saw_ntsc_pass = 1;
   }
   else
   {
      /* Mode-specific values from TOMReset() PAL branch. */
      if (vp == 623)   PASS("PAL VP = 623 (624 vertical lines)"); else FAIL("PAL VP = %u (want 623)", vp);
      if (vbb == 600)  PASS("PAL VBB = 600"); else FAIL("PAL VBB = %u (want 600)", vbb);
      if (vbe == 34)   PASS("PAL VBE = 34");  else FAIL("PAL VBE = %u (want 34)", vbe);
      if (vs == 618)   PASS("PAL VS = 618");  else FAIL("PAL VS = %u (want 618)", vs);
      if (veb == 613)  PASS("PAL VEB = 613"); else FAIL("PAL VEB = %u (want 613)", veb);

      /* Cross-mode delta: PAL has ~50 extra scanlines vs NTSC, which is
       * 100 halflines. VP and VBB both step by exactly 100. */
      if (saw_ntsc_pass)
      {
         if ((uint16_t)(vp - saved_ntsc_vp) == 100)
            PASS("PAL VP - NTSC VP = 100 (50 extra scanlines)");
         else
            FAIL("PAL VP - NTSC VP = %u (want 100)", (unsigned)(vp - saved_ntsc_vp));

         if ((uint16_t)(vbb - saved_ntsc_vbb) == 100)
            PASS("PAL VBB - NTSC VBB = 100");
         else
            FAIL("PAL VBB - NTSC VBB = %u (want 100)", (unsigned)(vbb - saved_ntsc_vbb));

         /* Mode-invariant registers must match across both modes. */
         if (vdb == saved_ntsc_vdb && vde == saved_ntsc_vde
             && hdb1 == saved_ntsc_hdb1 && hde == saved_ntsc_hde
             && vi == saved_ntsc_vi)
            PASS("VDB/VDE/HDB1/HDE/VI are mode-invariant across PAL+NTSC");
         else
            FAIL("mode-invariant regs drifted: VDB %u/%u VDE %u/%u HDB1 %u/%u HDE %u/%u VI %u/%u",
               vdb, saved_ntsc_vdb, vde, saved_ntsc_vde,
               hdb1, saved_ntsc_hdb1, hde, saved_ntsc_hde,
               vi, saved_ntsc_vi);
      }
   }
}

/* ================================================================
 * Test 10g: Libretro Geometry Update Ordering
 * TOM can change video dimensions while a frame is being rendered.
 * The frame must be submitted with the pitch used to render it; the
 * new geometry should apply to the following frame.
 * ================================================================ */
static void test_libretro_geometry_update_order(void)
{
   uint16_t old_hdb1;
   int old_geometry_count;

   printf("\n=== Test 10g: Libretro Geometry Update Ordering ===\n");

   old_hdb1 = tom_get16(TOM_HDB1);
   old_geometry_count = geometry_update_count;
   last_video_width = 0;
   last_video_height = 0;
   last_video_pitch = 0;
   last_geometry_width = 0;
   last_geometry_height = 0;

   p_TOMWriteWord(0xF00036, 203, WHO_M68K);
   p_retro_run();

   if (last_video_width == 320 && last_video_pitch == (320U << 2))
      PASS("first frame after TOM size change used previous pitch");
   else
      FAIL("first frame after TOM size change was %ux%u pitch=%lu",
            last_video_width, last_video_height, (unsigned long)last_video_pitch);

   if (geometry_update_count > old_geometry_count && last_geometry_width == 326)
      PASS("geometry update queued for next frame at width %u", last_geometry_width);
   else
      FAIL("geometry update missing or wrong width: count %d->%d width=%u",
            old_geometry_count, geometry_update_count, last_geometry_width);

   p_retro_run();

   if (last_video_width == 326 && last_video_pitch == (326U << 2))
      PASS("following frame used updated pitch");
   else
      FAIL("following frame was %ux%u pitch=%lu",
            last_video_width, last_video_height, (unsigned long)last_video_pitch);

   p_TOMWriteWord(0xF00036, old_hdb1, WHO_M68K);
}

/* ================================================================
 * Test 11: SSP (Stack Pointer) Initialization
 * RAM[0..3] should contain a valid SSP for HLE boot ($00004000).
 * ================================================================ */
static void test_ssp_init(void)
{
   uint32_t ssp;

   printf("\n=== Test 11: SSP Initialization ===\n");

   ssp = ram_get32(0);

   if (ssp == 0x00004000)
      PASS("SSP = $%08X (HLE default)", ssp);
   else if (ssp >= 0x1000 && ssp <= 0x200000)
      PASS("SSP = $%08X (valid RAM address)", ssp);
   else
      FAIL("SSP = $%08X (invalid or zero)", ssp);
}

/* ================================================================
 * Test 12: Run Address (PC Vector)
 * RAM[4..7] should contain the run address from the cart header.
 * ================================================================ */
static void test_run_address(void)
{
   uint32_t run_addr;
   uint32_t *p_jaguarRunAddress;

   printf("\n=== Test 12: Run Address Vector ===\n");

   p_jaguarRunAddress = dlsym(core_handle, "jaguarRunAddress");
   run_addr = ram_get32(4);

   if (p_jaguarRunAddress) {
      if (run_addr == *p_jaguarRunAddress)
         PASS("Run address = $%08X (matches jaguarRunAddress)", run_addr);
      else
         FAIL("Run address = $%08X (expected $%08X from jaguarRunAddress)",
              run_addr, *p_jaguarRunAddress);
   } else {
      if (run_addr >= 0x800000 && run_addr < 0xC00000)
         PASS("Run address = $%08X (in cart ROM space)", run_addr);
      else if (run_addr > 0 && run_addr < 0x200000)
         PASS("Run address = $%08X (in RAM)", run_addr);
      else
         FAIL("Run address = $%08X (unexpected)", run_addr);
   }
}

/* ================================================================
 * Test 13: MEMCON2 Default
 * TOMReset sets MEMCON2 = $35CC in both NTSC and PAL.
 * ================================================================ */
static void test_memcon2(void)
{
   uint16_t memcon2;

   printf("\n=== Test 13: MEMCON2 Default ===\n");

   memcon2 = tom_get16(TOM_MEMCON2);

   if (memcon2 == 0x35CC)
      PASS("MEMCON2 = $%04X", memcon2);
   else
      FAIL("MEMCON2 = $%04X (expected $35CC)", memcon2);
}

/* ================================================================
 * Test 14: Cart Type Byte Non-Zero Variant
 * Create a second ROM with cart type byte bits set and verify
 * MEMCON1 picks them up.
 * ================================================================ */
static void test_memcon1_with_type_bits(void)
{
   uint16_t memcon1;
   uint16_t expected;

   printf("\n=== Test 14: MEMCON1 Cart Type Bits ===\n");

   memcon1 = tom_get16(TOM_MEMCON1);
   expected = 0x1861 | (p_jagMemSpace[0x800400] & 0x1E);

   if (memcon1 == expected) {
      PASS("MEMCON1 = $%04X matches formula $1861 | ($%02X & $1E)",
           memcon1, p_jagMemSpace[0x800400]);
   } else {
      FAIL("MEMCON1 = $%04X, expected $%04X from formula",
           memcon1, expected);
   }
}

/* ================================================================
 * Test 15: Background Color
 * BG register should be 0 (black) after HLE init.
 * ================================================================ */
static void test_bg_color(void)
{
   uint16_t bg;

   printf("\n=== Test 15: Background Color ===\n");

   bg = tom_get16(TOM_BG);

   if (bg == 0)
      PASS("BG = $%04X (black)", bg);
   else
      FAIL("BG = $%04X (expected $0000)", bg);
}

/* ================================================================
 * Test 16: Cart Type With Width Bits
 * Reload with a ROM that has $0A at $800400 (bits 1,3 set).
 * Expected MEMCON1 = $1861 | ($0A & $1E) = $1861 | $0A = $186B.
 * ================================================================ */
static void test_memcon1_width_bits(uint8_t *dummy_rom)
{
   struct retro_game_info game;
   uint16_t memcon1;

   printf("\n=== Test 16: MEMCON1 Width Bits (cart type $0A) ===\n");

   p_retro_unload_game();

   dummy_rom[0x400] = 0x0A;

   memset(&game, 0, sizeof(game));
   game.path = "dummy_0A.jag";
   game.data = dummy_rom;
   game.size = 131072;

   if (!p_retro_load_game(&game)) {
      FAIL("retro_load_game failed for cart type $0A ROM");
      return;
   }

   memcon1 = tom_get16(TOM_MEMCON1);

   if (memcon1 == 0x186B)
      PASS("MEMCON1 = $%04X with cart type $0A", memcon1);
   else
      FAIL("MEMCON1 = $%04X (expected $186B) with cart type $0A", memcon1);

   dummy_rom[0x400] = 0x00;
}

/* ================================================================
 * Test 17: HLE Init Idempotency
 * Unload/reload should produce identical register state.
 * ================================================================ */
static void test_reload_consistency(uint8_t *dummy_rom)
{
   struct retro_game_info game;
   uint16_t memcon1_a, memcon1_b;
   uint32_t magic_a, magic_b;
   uint32_t olp_a, olp_b;

   printf("\n=== Test 17: HLE Init Idempotency (Reload) ===\n");

   /* Reload first to ensure clean state with cart type $00 */
   p_retro_unload_game();
   dummy_rom[0x400] = 0x00;

   memset(&game, 0, sizeof(game));
   game.path = "dummy_baseline.jag";
   game.data = dummy_rom;
   game.size = 131072;

   if (!p_retro_load_game(&game)) {
      FAIL("retro_load_game failed on baseline load");
      return;
   }

   memcon1_a = tom_get16(TOM_MEMCON1);
   magic_a = p_GPUReadLong(0xF03000, WHO_M68K);
   olp_a = tom_get16(TOM_OLP_LO) | ((uint32_t)tom_get16(TOM_OLP_HI) << 16);

   p_retro_unload_game();

   memset(&game, 0, sizeof(game));
   game.path = "dummy_reload.jag";
   game.data = dummy_rom;
   game.size = 131072;

   if (!p_retro_load_game(&game)) {
      FAIL("retro_load_game failed on reload");
      return;
   }

   memcon1_b = tom_get16(TOM_MEMCON1);
   magic_b = p_GPUReadLong(0xF03000, WHO_M68K);
   olp_b = tom_get16(TOM_OLP_LO) | ((uint32_t)tom_get16(TOM_OLP_HI) << 16);

   if (memcon1_a == memcon1_b)
      PASS("MEMCON1 consistent across reload: $%04X", memcon1_a);
   else
      FAIL("MEMCON1 changed: $%04X -> $%04X", memcon1_a, memcon1_b);

   if (magic_a == magic_b)
      PASS("GPU auth magic consistent: $%08X", magic_a);
   else
      FAIL("GPU auth magic changed: $%08X -> $%08X", magic_a, magic_b);

   if (olp_a == olp_b)
      PASS("OLP consistent: $%08X", olp_a);
   else
      FAIL("OLP changed: $%08X -> $%08X", olp_a, olp_b);
}

/* ================================================================
 * Test 18: Recognized Raw Homebrew Load
 * Some homebrew is headerless but has a recognizable absolute-address
 * startup pattern. Only those layouts should be accepted.
 * ================================================================ */
static void test_recognized_raw_homebrew_load(void)
{
   struct retro_game_info game;
   uint8_t raw_bin[96];
   uint32_t *p_jaguarRunAddress;

   printf("\n=== Test 18: Recognized Raw Homebrew Load ===\n");

   p_retro_unload_game();
   memset(raw_bin, 0, sizeof(raw_bin));

   raw_bin[0x00] = 0x23;
   raw_bin[0x01] = 0xFC;
   raw_bin[0x02] = 0x00;
   raw_bin[0x03] = 0x07;
   raw_bin[0x04] = 0x00;
   raw_bin[0x05] = 0x07;
   raw_bin[0x06] = 0x00;
   raw_bin[0x07] = 0xF0;
   raw_bin[0x08] = 0x21;
   raw_bin[0x09] = 0x0C;
   raw_bin[0x0A] = 0x4E;
   raw_bin[0x0B] = 0xB9;
   raw_bin[0x0E] = 0x40;
   raw_bin[0x0F] = 0x20;
   raw_bin[0x10] = 0x41;
   raw_bin[0x11] = 0xF9;
   raw_bin[0x14] = 0x40;
   raw_bin[0x15] = 0x30;
   raw_bin[0x20] = 0x60;
   raw_bin[0x21] = 0xFE;

   memset(&game, 0, sizeof(game));
   game.path = "recognized_raw_homebrew.jag";
   game.data = raw_bin;
   game.size = sizeof(raw_bin);

   if (!p_retro_load_game(&game)) {
      FAIL("Recognized raw homebrew was rejected");
      return;
   }

   p_jaguarRunAddress = dlsym(core_handle, "jaguarRunAddress");
   if (p_jaguarRunAddress && *p_jaguarRunAddress == 0x00004000)
      PASS("Recognized raw run address = $%08X", *p_jaguarRunAddress);
   else if (p_jaguarRunAddress)
      FAIL("Recognized raw run address = $%08X (expected $00004000)",
           *p_jaguarRunAddress);
   else
      FAIL("Missing jaguarRunAddress symbol");

   if (memcmp(p_jagMemSpace + 0x4000, raw_bin, sizeof(raw_bin)) == 0)
      PASS("Recognized raw copied to inferred base $4000");
   else
      FAIL("Recognized raw bytes not present at inferred base $4000");
}

/* ================================================================
 * Test 19: Unknown Headerless BIN Rejection
 * Unknown headerless files still have no reliable load/run metadata.
 * ================================================================ */
static void test_headerless_bin_rejected(void)
{
   struct retro_game_info game;
   uint8_t raw_bin[64];

   printf("\n=== Test 19: Unknown Headerless BIN Rejection ===\n");

   p_retro_unload_game();
   memset(raw_bin, 0, sizeof(raw_bin));

   raw_bin[0] = 0x23;
   raw_bin[1] = 0xFC;
   raw_bin[2] = 0x00;
   raw_bin[3] = 0x07;
   raw_bin[4] = 0x00;
   raw_bin[5] = 0x07;
   raw_bin[6] = 0x00;
   raw_bin[7] = 0xF0;
   raw_bin[8] = 0x21;
   raw_bin[9] = 0x0C;

   memset(&game, 0, sizeof(game));
   game.path = "raw_headerless.bin";
   game.data = raw_bin;
   game.size = sizeof(raw_bin);

   if (!p_retro_load_game(&game))
      PASS("Headerless BIN rejected instead of booting invalid RAM");
   else {
      FAIL("Headerless BIN unexpectedly loaded");
      p_retro_unload_game();
   }
}

/* ================================================================
 * Main
 * ================================================================ */
int main(int argc, char *argv[])
{
   void *handle;
   uint8_t *dummy_rom;
   struct retro_game_info game;
   (void)argc; (void)argv;

   printf("=== HLE BIOS Initialization Tests ===\n");

   handle = dlopen("./" CORE_FILENAME, RTLD_NOW);
   if (!handle) {
      fprintf(stderr, "dlopen: %s\n", dlerror());
      return 1;
   }
   core_handle = handle;

#define LOAD(sym) do { \
   p_##sym = dlsym(handle, #sym); \
   if (!p_##sym) { fprintf(stderr, "Missing: %s\n", #sym); return 1; } \
} while(0)

#define LOAD_OPT(sym) do { p_##sym = dlsym(handle, #sym); } while(0)

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
   LOAD(JaguarReset);
   LOAD(JaguarApplyHLEBIOSState);
   LOAD(HalflineCallback);
   LOAD(TOMReadByte);
   LOAD(TOMReadWord);
   LOAD(TOMWriteByte);
   LOAD(TOMWriteWord);
   LOAD(TOMSetPendingVideoInt);
   LOAD(TOMSetPendingTimerInt);
   LOAD(GPUReadLong);
   LOAD(GPUWriteLong);
   LOAD(GPUSetIRQLine);
   LOAD(GPUHandleIRQs);
   LOAD(DSPReadLong);
   LOAD(DSPWriteLong);
   LOAD(DSPSetIRQLine);
   LOAD(DSPHandleIRQsNP);
   LOAD(JERRYReadWord);
   LOAD(JERRYWriteWord);
   LOAD(JERRYWriteByte);
   LOAD(JERRYIRQEnabled);
   LOAD(JERRYSetPendingIRQ);
   LOAD(OPProcessList);
   LOAD(OPProcessScaledBitmap);
   LOAD(OPProcessFixedBitmap);

   LOAD_OPT(tomRam8);
   LOAD_OPT(jaguarMainRAM);
   LOAD_OPT(jagMemSpace);
   /* `regs` is the exported global m68k register state from
    * src/m68000/cpuextra.c. Used by Test 10j to observe IPL2 line
    * state. Optional: if absent, Test 10j will skip its sub-asserts. */
   p_regs = (struct test_regstruct *)dlsym(handle, "regs");
   LOAD_OPT(sclk);
   LOAD_OPT(smode);
   LOAD_OPT(lowerField);
   LOAD_OPT(vjs);
   LOAD_OPT(jaguarLoadedRAMStart);
   LOAD_OPT(jaguarLoadedRAMEnd);
   LOAD_OPT(gpu_pc);
   LOAD_OPT(dsp_pc);
   LOAD_OPT(dsp_control);

   if (!p_tomRam8 || !p_jaguarMainRAM || !p_jagMemSpace || !p_sclk
         || !p_smode || !p_lowerField || !p_vjs
         || !p_jaguarLoadedRAMStart || !p_jaguarLoadedRAMEnd) {
      fprintf(stderr, "Missing internal symbols (tomRam8, jaguarMainRAM, jagMemSpace, sclk, smode, lowerField, vjs, jaguarLoadedRAMStart/End)\n");
      return 1;
   }

   /* Set up libretro callbacks */
   p_retro_set_environment(environment);
   p_retro_set_video_refresh(video_refresh);
   p_retro_set_audio_sample(audio_sample);
   p_retro_set_audio_sample_batch(audio_batch);
   p_retro_set_input_poll(input_poll);
   p_retro_set_input_state(input_state);
   p_retro_init();

   /* Create a minimal 128K dummy ROM */
   dummy_rom = calloc(1, 131072);
   if (!dummy_rom) {
      fprintf(stderr, "Cannot allocate dummy ROM\n");
      return 1;
   }
   /* Set the run address at $800404 to point to $802000 */
   dummy_rom[0x404] = 0x00;
   dummy_rom[0x405] = 0x80;
   dummy_rom[0x406] = 0x20;
   dummy_rom[0x407] = 0x00;
   /* Put an infinite loop (68K: BRA self = 0x60FE) at $802000 */
   dummy_rom[0x2000] = 0x60;
   dummy_rom[0x2001] = 0xFE;

   memset(&game, 0, sizeof(game));
   game.path = "dummy.jag";
   game.data = dummy_rom;
   game.size = 131072;

   if (!p_retro_load_game(&game)) {
      fprintf(stderr, "retro_load_game failed with dummy ROM\n");
      p_retro_deinit();
      free(dummy_rom);
      return 1;
   }

   /* Run HLE-mode tests (BIOS disabled) */
   printf("\n========== HLE Mode (no BIOS) ==========\n");

   test_gpu_auth_magic();
   test_hle_low_ram_workspace();
   test_hle_workspace_apply_contract();
   test_hle_preserves_ram_loaded_range();
   test_hle_exception_vectors();
   test_memcon1();
   test_memcon1_nonzero_type();
   test_jerry_clocks();
   test_endianness_registers();
   test_op_stop_list();
   test_op_scaled_small_hscale_clip();
   test_op_scaled_firstpix_4bpp();
   test_op_fixed_firstpix_4bpp();
   test_op_fixed_bitmap_writeback();
   test_border_clear();
   test_interrupts_cleared();
   test_jerry_pit_cleared();
   test_jerry_jintctrl_word_decode();
   test_jerry_jintctrl_multi_pending_selective_clear();
   test_gpu_irq_latch_redispatch();
   test_dsp_irq_latch_redispatch();
   test_jerry_i2s_defaults();
   test_tom_video_registers();
   test_tom_vp_rollover();
   test_tom_video_irq_latches_when_disabled();
   test_tom_int1_byte_write_clears_pending();
   test_tom_int1_multi_pending_selective_clear();
   test_tom_pit_reload_semantics();
   test_tom_video_timing_symmetry();
   test_tom_vmode_bitfields();
   test_video_timing_defaults_pal_ntsc();
   test_tom_ipl2_reassert_after_selective_clear();
   test_libretro_geometry_update_order();
   test_ssp_init();
   test_run_address();
   test_memcon2();
   test_memcon1_with_type_bits();
   test_bg_color();
   test_memcon1_width_bits(dummy_rom);
   test_reload_consistency(dummy_rom);

   p_retro_unload_game();
   use_pal = 1;

   memset(&game, 0, sizeof(game));
   game.path = "dummy_pal.jag";
   game.data = dummy_rom;
   game.size = 131072;

   if (!p_retro_load_game(&game)) {
      fprintf(stderr, "retro_load_game failed with PAL dummy ROM\n");
      p_retro_deinit();
      dlclose(handle);
      free(dummy_rom);
      return 1;
   }

   printf("\n========== HLE Mode PAL Timing ==========\n");

   test_hle_low_ram_workspace();
   test_jerry_clocks();
   test_tom_video_registers();
   test_video_timing_defaults_pal_ntsc();
   test_memcon2();
   test_bg_color();

   test_recognized_raw_homebrew_load();
   test_headerless_bin_rejected();

   printf("\n=== Results: %d passed, %d failed ===\n", passes, fails);

   p_retro_deinit();
   dlclose(handle);
   free(dummy_rom);
   return fails > 0 ? 1 : 0;
}
