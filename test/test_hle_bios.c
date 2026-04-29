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
#define IRQ2_TIMER1      0x04

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
static void (*p_TOMWriteWord)(uint32_t, uint16_t, uint32_t);
static void (*p_JERRYWriteWord)(uint32_t, uint16_t, uint32_t);
static bool (*p_JERRYIRQEnabled)(int);
static void (*p_JERRYSetPendingIRQ)(int);
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
 * Test 10c: Libretro Geometry Update Ordering
 * TOM can change video dimensions while a frame is being rendered.
 * The frame must be submitted with the pitch used to render it; the
 * new geometry should apply to the following frame.
 * ================================================================ */
static void test_libretro_geometry_update_order(void)
{
   uint16_t old_hdb1;
   int old_geometry_count;

   printf("\n=== Test 10c: Libretro Geometry Update Ordering ===\n");

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
   LOAD(TOMWriteWord);
   LOAD(GPUReadLong);
   LOAD(JERRYReadWord);
   LOAD(JERRYWriteWord);
   LOAD(JERRYIRQEnabled);
   LOAD(JERRYSetPendingIRQ);
   LOAD(OPProcessScaledBitmap);
   LOAD(OPProcessFixedBitmap);

   LOAD_OPT(tomRam8);
   LOAD_OPT(jaguarMainRAM);
   LOAD_OPT(jagMemSpace);
   LOAD_OPT(sclk);
   LOAD_OPT(smode);
   LOAD_OPT(lowerField);
   LOAD_OPT(vjs);
   LOAD_OPT(jaguarLoadedRAMStart);
   LOAD_OPT(jaguarLoadedRAMEnd);

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
   test_border_clear();
   test_interrupts_cleared();
   test_jerry_pit_cleared();
   test_jerry_jintctrl_word_decode();
   test_jerry_i2s_defaults();
   test_tom_video_registers();
   test_tom_vp_rollover();
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
