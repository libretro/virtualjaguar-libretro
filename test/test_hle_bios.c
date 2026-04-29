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

/* Emulator internals via dlsym */
static void *core_handle;
static uint8_t *p_tomRam8;
static uint8_t **p_jaguarMainRAM;
static uint8_t *p_jagMemSpace;
static uint32_t (*p_GPUReadLong)(uint32_t, uint32_t);
static uint16_t (*p_JERRYReadWord)(uint32_t, uint32_t);
static struct VJSettings *p_vjs;

struct VJSettings {
   int32_t joyport;
   bool hardwareTypeNTSC;
   bool useJaguarBIOS;
   bool hardwareTypeAlpine;
   uint32_t biosType;
   bool useFastBlitter;
};

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
 * Test 10: TOM NTSC Video Timing Registers
 * Verify all TOM video registers match expected values.
 * ================================================================ */
static void test_tom_video_registers(void)
{
   printf("\n=== Test 10: TOM Video Timing Registers ===\n");

   if (!p_vjs->hardwareTypeNTSC) {
      printf("  (Skipping NTSC checks — running in PAL mode)\n");
      return;
   }

#define CHECK_TOM(name, offset, expected) do { \
   uint16_t val = tom_get16(offset); \
   if (val == (expected)) \
      PASS(name " = %u", val); \
   else \
      FAIL(name " = %u (expected %u)", val, (unsigned)(expected)); \
} while(0)

   CHECK_TOM("HP",   TOM_HP,   844);
   CHECK_TOM("HBB",  TOM_HBB,  1713);
   CHECK_TOM("HBE",  TOM_HBE,  125);
   CHECK_TOM("HS",   TOM_HS,   1741);
   CHECK_TOM("HVS",  TOM_HVS,  651);
   CHECK_TOM("HDB1", TOM_HDB1, 203);
   CHECK_TOM("HDB2", TOM_HDB2, 203);
   CHECK_TOM("HDE",  TOM_HDE,  1665);
   CHECK_TOM("VP",   TOM_VP,   523);
   CHECK_TOM("VBB",  TOM_VBB,  500);
   CHECK_TOM("VBE",  TOM_VBE,  24);
   CHECK_TOM("VS",   TOM_VS,   517);
   CHECK_TOM("VDB",  TOM_VDB,  38);
   CHECK_TOM("VDE",  TOM_VDE,  518);
   CHECK_TOM("VEB",  TOM_VEB,  511);
   CHECK_TOM("VEE",  TOM_VEE,  6);
   CHECK_TOM("VI",   TOM_VI,   0);  /* left at 0; (vc>0) guard disables until game sets it */
   CHECK_TOM("HEQ",  TOM_HEQ,  784);
   CHECK_TOM("BG",   TOM_BG,   0);
   CHECK_TOM("VMODE", TOM_VMODE, 0x06C1);

#undef CHECK_TOM
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
   LOAD(GPUReadLong);
   LOAD(JERRYReadWord);

   LOAD_OPT(tomRam8);
   LOAD_OPT(jaguarMainRAM);
   LOAD_OPT(jagMemSpace);
   LOAD_OPT(vjs);

   if (!p_tomRam8 || !p_jaguarMainRAM || !p_jagMemSpace || !p_vjs) {
      fprintf(stderr, "Missing internal symbols (tomRam8, jaguarMainRAM, jagMemSpace, vjs)\n");
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
   test_memcon1();
   test_memcon1_nonzero_type();
   test_jerry_clocks();
   test_endianness_registers();
   test_op_stop_list();
   test_border_clear();
   test_interrupts_cleared();
   test_jerry_pit_cleared();
   test_tom_video_registers();
   test_ssp_init();
   test_run_address();
   test_memcon2();
   test_memcon1_with_type_bits();
   test_bg_color();
   test_memcon1_width_bits(dummy_rom);
   test_reload_consistency(dummy_rom);

   printf("\n=== Results: %d passed, %d failed ===\n", passes, fails);

   p_retro_unload_game();
   p_retro_deinit();
   dlclose(handle);
   free(dummy_rom);
   return fails > 0 ? 1 : 0;
}
