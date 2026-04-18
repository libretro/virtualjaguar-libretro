/* test_cd_boot.c -- Minimal test harness for CD boot diagnostics.
 * Build: make -j4 && cc -o test/test_cd_boot test/test_cd_boot.c -L. -lvirtualjaguar_libretro -Wl,-rpath,.
 * Actually, just link against the dylib directly:
 *   cc -o test/test_cd_boot test/test_cd_boot.c -ldl
 * Or use the simpler approach: include retro API and call it. */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dlfcn.h>
#include <stdarg.h>
#include <stdbool.h>
#include "../libretro-common/include/libretro.h"

/* Function pointers for the libretro API */
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
static void (*p_retro_get_system_info)(struct retro_system_info *);
static void (*p_retro_get_system_av_info)(struct retro_system_av_info *);

/* m68k register access -- enum from m68kinterface.h:
   D0-D7=0-7, A0-A7=8-15, PC=16, SR=17, SP=18 */
#define M68K_REG_D0_T  0
#define M68K_REG_D1_T  1
#define M68K_REG_D2_T  2
#define M68K_REG_D3_T  3
#define M68K_REG_D4_T  4
#define M68K_REG_D5_T  5
#define M68K_REG_D6_T  6
#define M68K_REG_D7_T  7
#define M68K_REG_A0_T  8
#define M68K_REG_A1_T  9
#define M68K_REG_A2_T 10
#define M68K_REG_A3_T 11
#define M68K_REG_A4_T 12
#define M68K_REG_A5_T 13
#define M68K_REG_A6_T 14
#define M68K_REG_A7_T 15
#define M68K_REG_PC_T 16
#define M68K_REG_SR_T 17
#define M68K_REG_SP_T 18
static unsigned int (*p_m68k_get_reg)(void *, int);

/* Hardware register read functions (dlsym'd from core) */
static uint16_t (*p_TOMReadWord)(uint32_t offset, uint32_t who);
static uint16_t (*p_JERRYReadWord)(uint32_t offset, uint32_t who);
static uint16_t (*p_CDROMReadWord)(uint32_t offset, uint32_t who);

static unsigned frame_count = 0;
static uint32_t last_frame_hash = 0;
static unsigned width_seen = 0, height_seen = 0;
static bool got_video = false;

static void video_refresh(const void *data, unsigned width, unsigned height, size_t pitch)
{
   if (!data) return;
   got_video = true;
   width_seen = width;
   height_seen = height;

   /* Simple hash of video buffer to detect changes */
   const uint32_t *pixels = (const uint32_t *)data;
   uint32_t hash = 0;
   unsigned total = width * height;
   for (unsigned i = 0; i < total; i += 97)  /* sample every 97th pixel */
      hash = hash * 31 + pixels[i];

   if (hash != last_frame_hash)
   {
      /* Check if frame is all black (or near-black) */
      unsigned nonblack = 0;
      for (unsigned i = 0; i < total; i += 37)
      {
         uint32_t p = pixels[i] & 0x00FFFFFF;
         if (p > 0x010101)
            nonblack++;
      }
      printf("  Frame %u: %ux%u, hash=0x%08X, nonblack_samples=%u/%u\n",
             frame_count, width, height, hash, nonblack, total / 37);
      last_frame_hash = hash;
   }
}

static void audio_sample(int16_t left, int16_t right) { (void)left; (void)right; }
static size_t audio_sample_batch(const int16_t *data, size_t frames) { (void)data; return frames; }
static void input_poll(void) {}
static int16_t input_state(unsigned port, unsigned device, unsigned index, unsigned id)
{
   (void)port; (void)device; (void)index; (void)id;
   return 0;
}

static void log_printf(enum retro_log_level level, const char *fmt, ...)
{
   va_list ap;
   const char *lvl_str[] = {"DEBUG", "INFO", "WARN", "ERROR"};
   printf("[%s] ", lvl_str[level < 4 ? level : 3]);
   va_start(ap, fmt);
   vprintf(fmt, ap);
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
      return true;
   case RETRO_ENVIRONMENT_GET_SYSTEM_DIRECTORY:
      /* Look for BIOS files in test/roms/private or current dir */
      *(const char **)data = "test/roms/private";
      return true;
   case RETRO_ENVIRONMENT_GET_SAVE_DIRECTORY:
      *(const char **)data = ".";
      return true;
   case RETRO_ENVIRONMENT_SET_VARIABLES:
   case RETRO_ENVIRONMENT_SET_CORE_OPTIONS_V2:
      return true;
   case RETRO_ENVIRONMENT_GET_VARIABLE:
   {
      struct retro_variable *var = (struct retro_variable *)data;
      /* Force CD BIOS on */
      if (var->key && strcmp(var->key, "virtualjaguar_bios") == 0)
      {
         var->value = "enabled";
         return true;
      }
      if (var->key && strcmp(var->key, "virtualjaguar_usefastblitter") == 0)
      {
         var->value = "enabled";
         return true;
      }
      if (var->key && strcmp(var->key, "virtualjaguar_cd_bios_type") == 0)
      {
         const char *env = getenv("VJ_CD_BIOS_TYPE");
         var->value = (env && strcmp(env, "dev") == 0) ? "dev" : "retail";
         return true;
      }
      var->value = NULL;
      return false;
   }
   case RETRO_ENVIRONMENT_GET_VARIABLE_UPDATE:
      *(bool *)data = false;
      return true;
   default:
      return false;
   }
}

int main(int argc, char *argv[])
{
   if (argc < 2)
   {
      fprintf(stderr, "Usage: %s <path-to-cue-or-chd> [num_frames]\n", argv[0]);
      return 1;
   }

   const char *image_path = argv[1];
   unsigned num_frames = argc > 2 ? atoi(argv[2]) : 300;

   /* Load the core */
   void *handle = dlopen("./virtualjaguar_libretro.dylib", RTLD_NOW);
   if (!handle)
   {
      fprintf(stderr, "Failed to load core: %s\n", dlerror());
      return 1;
   }

#define LOAD_SYM(sym) do { \
   p_##sym = dlsym(handle, #sym); \
   if (!p_##sym) { fprintf(stderr, "Missing symbol: %s\n", #sym); return 1; } \
} while(0)

   LOAD_SYM(retro_init);
   LOAD_SYM(retro_deinit);
   LOAD_SYM(retro_set_environment);
   LOAD_SYM(retro_set_video_refresh);
   LOAD_SYM(retro_set_audio_sample);
   LOAD_SYM(retro_set_audio_sample_batch);
   LOAD_SYM(retro_set_input_poll);
   LOAD_SYM(retro_set_input_state);
   LOAD_SYM(retro_load_game);
   LOAD_SYM(retro_unload_game);
   LOAD_SYM(retro_run);
   LOAD_SYM(retro_get_system_info);
   LOAD_SYM(retro_get_system_av_info);

   /* m68k_get_reg is not part of the libretro API but is exported */
   p_m68k_get_reg = dlsym(handle, "m68k_get_reg");
   if (!p_m68k_get_reg)
      printf("Warning: m68k_get_reg not exported\n");

   /* Hardware register read functions for CD diagnostic dumps */
   p_TOMReadWord = dlsym(handle, "TOMReadWord");
   if (!p_TOMReadWord)
      printf("Warning: TOMReadWord not exported\n");
   p_JERRYReadWord = dlsym(handle, "JERRYReadWord");
   if (!p_JERRYReadWord)
      printf("Warning: JERRYReadWord not exported\n");
   p_CDROMReadWord = dlsym(handle, "CDROMReadWord");
   if (!p_CDROMReadWord)
      printf("Warning: CDROMReadWord not exported\n");

   p_retro_set_environment(environment);
   p_retro_set_video_refresh(video_refresh);
   p_retro_set_audio_sample(audio_sample);
   p_retro_set_audio_sample_batch(audio_sample_batch);
   p_retro_set_input_poll(input_poll);
   p_retro_set_input_state(input_state);

   p_retro_init();

   struct retro_game_info game = {0};
   game.path = image_path;

   printf("Loading CD image: %s\n", image_path);
   if (!p_retro_load_game(&game))
   {
      fprintf(stderr, "retro_load_game failed!\n");
      p_retro_deinit();
      dlclose(handle);
      return 1;
   }

   printf("Game loaded successfully. Running %u frames...\n", num_frames);

   /* Check initial RAM state */
   uint8_t *(*get_ram)(void) = dlsym(handle, "GetRamPtr");
   if (get_ram)
   {
      uint8_t *ram = get_ram();
      uint32_t sp = (ram[0]<<24) | (ram[1]<<16) | (ram[2]<<8) | ram[3];
      uint32_t pc = (ram[4]<<24) | (ram[5]<<16) | (ram[6]<<8) | ram[7];
      printf("Initial vectors: SP=0x%08X, PC=0x%08X\n", sp, pc);

      /* Check what's at $E00000 (BIOS ROM area) */
      /* jagMemSpace isn't exported, but jaguarMainRAM is at offset 0 in jagMemSpace */
      /* The BIOS is at 0xE00000 in the memory space */

      /* Check cart ROM area ($800000) */
      /* Can't access directly, but we can check some BIOS-related globals */
      bool *cart_inserted = dlsym(handle, "jaguarCartInserted");
      if (cart_inserted)
         printf("jaguarCartInserted: %s\n", *cart_inserted ? "true" : "false");

      uint32_t *run_addr = dlsym(handle, "jaguarRunAddress");
      if (run_addr)
         printf("jaguarRunAddress: 0x%08X\n", *run_addr);

      bool *cd_bios_ext = dlsym(handle, "cd_bios_loaded_externally");
      if (cd_bios_ext)
         printf("cd_bios_loaded_externally: %s\n", *cd_bios_ext ? "true" : "false");
   }

   /* After loading, dump key code areas to help disassemble the boot loop */
   if (get_ram)
   {
      uint8_t *ram = get_ram();
      /* Dump code around PC=$05015A (BUTCH clear) and $050246 (BUTCH set) */
      printf("\nRAM dump at $050100-$050300 (BIOS loop code):\n");
      for (unsigned a = 0x050100; a < 0x050300; a += 16)
      {
         printf("%06X:", a);
         for (unsigned b = 0; b < 16; b += 2)
            printf(" %02X%02X", ram[a+b], ram[a+b+1]);
         printf("\n");
      }
      printf("\nRAM dump at $083100-$083140 (EEPROM read code):\n");
      for (unsigned a = 0x083100; a < 0x083140; a += 16)
      {
         printf("%06X:", a);
         for (unsigned b = 0; b < 16; b += 2)
            printf(" %02X%02X", ram[a+b], ram[a+b+1]);
         printf("\n");
      }
   }

   for (frame_count = 0; frame_count < num_frames; frame_count++)
   {
      p_retro_run();

      /* After first frame, dump key vectors and BIOS state */
      if (frame_count == 0 && get_ram)
      {
         uint8_t *ram = get_ram();
         /* irq_ack_handler returns vector 64, so handler addr is at $100 */
         uint32_t vec64 = (ram[0x100]<<24) | (ram[0x101]<<16) | (ram[0x102]<<8) | ram[0x103];
         printf("\nAfter frame 0: Vector 64 (user int #0) handler at $%08X\n", vec64);

         /* Also dump several key vectors */
         for (unsigned v = 0; v < 72; v++)
         {
            uint32_t addr = v * 4;
            uint32_t val = (ram[addr]<<24) | (ram[addr+1]<<16) | (ram[addr+2]<<8) | ram[addr+3];
            if (val != 0 && val != 0xFFFFFFFF && (v == 0 || v == 1 || v == 2 || v == 3 ||
                v == 4 || v == 24 || v == 25 || v == 26 || v == 27 ||
                v == 64 || v == 65 || v == 66 || v == 67 || v == 68 || v == 69 || v == 70 || v == 71))
               printf("  Vector %2u ($%03X): $%08X\n", v, addr, val);
         }

         /* Dump the VBlank handler code */
         if (vec64 > 0 && vec64 < 0x200000)
         {
            printf("VBlank handler code at $%06X:\n", vec64);
            for (unsigned a = vec64; a < vec64 + 128; a += 16)
            {
               printf("%06X:", a);
               for (unsigned b = 0; b < 16; b += 2)
                  printf(" %02X%02X", ram[a+b], ram[a+b+1]);
               printf("\n");
            }
         }
         else if (vec64 >= 0x800000 && vec64 < 0xA00000)
         {
            printf("VBlank handler is in cart ROM at $%08X (can't dump from RAM)\n", vec64);
         }
      }

      /* Dump BIOS error state variables at transition frames */
      if (get_ram && (frame_count >= 60 && frame_count <= 75))
      {
         uint8_t *ram = get_ram();
         unsigned pc = p_m68k_get_reg ? p_m68k_get_reg(NULL, M68K_REG_PC_T) : 0;
         uint32_t val_721c = (ram[0x3721C]<<24) | (ram[0x3721D]<<16) | (ram[0x3721E]<<8) | ram[0x3721F];
         uint16_t val_722a = (ram[0x3722A]<<8) | ram[0x3722B];
         uint16_t val_3727c = (ram[0x3727C]<<8) | ram[0x3727D];
         printf("  Frame %u: PC=$%06X  $3721C=%08X  $3722A=%04X  $3727C=%04X\n",
                frame_count, pc, val_721c, val_722a, val_3727c);
      }
      /* At frame 67, dump key BIOS data structures and all regs */
      if (frame_count == 67 && get_ram && p_m68k_get_reg)
      {
         uint8_t *ram = get_ram();
         printf("\n=== PRE-CRASH DUMP (frame 67) ===\n");
         printf("D0=$%08X D1=$%08X D6=$%08X D7=$%08X\n",
                p_m68k_get_reg(NULL, M68K_REG_D0_T),
                p_m68k_get_reg(NULL, M68K_REG_D1_T),
                p_m68k_get_reg(NULL, M68K_REG_D0_T + 6),
                p_m68k_get_reg(NULL, M68K_REG_D0_T + 7));
         printf("A0=$%08X A1=$%08X A2=$%08X A4=$%08X\n",
                p_m68k_get_reg(NULL, M68K_REG_A0_T),
                p_m68k_get_reg(NULL, M68K_REG_A0_T + 1),
                p_m68k_get_reg(NULL, M68K_REG_A0_T + 2),
                p_m68k_get_reg(NULL, M68K_REG_A0_T + 4));
         /* BIOS data structure at $37088 (A2 in $005774) */
         printf("RAM $37080-$370C0 (A2 data struct):\n");
         for (unsigned a = 0x37080; a < 0x370C0; a += 16)
         {
            printf("  %06X:", a);
            for (unsigned b = 0; b < 16; b += 2)
               printf(" %02X%02X", ram[a+b], ram[a+b+1]);
            printf("\n");
         }
         /* BIOS data structure at $37110 (A1 in main loop / $005774) */
         printf("RAM $37100-$37160 (A1 data struct):\n");
         for (unsigned a = 0x37100; a < 0x37160; a += 16)
         {
            printf("  %06X:", a);
            for (unsigned b = 0; b < 16; b += 2)
               printf(" %02X%02X", ram[a+b], ram[a+b+1]);
            printf("\n");
         }
         /* Dump code at $005E20-$005E70 (GPU RAM test) */
         printf("RAM $005E20-$005E70 (GPU RAM test code):\n");
         for (unsigned a = 0x005E20; a < 0x005E70; a += 16)
         {
            printf("  %06X:", a);
            for (unsigned b = 0; b < 16; b += 2)
               printf(" %02X%02X", ram[a+b], ram[a+b+1]);
            printf("\n");
         }
         printf("=== END PRE-CRASH DUMP ===\n\n");
      }

      /* Dump $192000 (CD data buffer) at key frames to verify injection format */
      if (get_ram && (frame_count == 70 || frame_count == 80 || frame_count == 100))
      {
         uint8_t *ram = get_ram();
         printf("\n=== CD DATA BUFFER $192000 DUMP (frame %u) ===\n", frame_count);
         for (unsigned a = 0x192000; a < 0x192040; a += 16)
         {
            printf("  %06X:", a);
            for (unsigned b = 0; b < 16; b += 2)
               printf(" %02X%02X", ram[a+b], ram[a+b+1]);
            printf("\n");
         }
         /* Also dump BIOS CD flags */
         uint16_t fd418 = (ram[0x1FD418]<<8) | ram[0x1FD419];
         uint16_t ae02a = (ram[0x1AE02A]<<8) | ram[0x1AE02B];
         printf("  $1FD418=%04X $1AE02A=%04X\n", fd418, ae02a);
         printf("=== END CD DATA BUFFER DUMP ===\n\n");
      }

      /* Print 68K PC and vector state at key frames */
      if (frame_count <= 5 || frame_count == 10 || frame_count == 30 ||
          (frame_count >= 60 && frame_count <= 80) ||
          (frame_count >= 100 && frame_count <= 150) ||
          frame_count % 50 == 0 || frame_count == 299)
      {
         if (p_m68k_get_reg)
         {
            unsigned pc = p_m68k_get_reg(NULL, M68K_REG_PC_T);
            unsigned sr = p_m68k_get_reg(NULL, M68K_REG_SR_T);
            unsigned sp = p_m68k_get_reg(NULL, M68K_REG_SP_T);
            printf("  Frame %u: PC=$%06X SR=$%04X SP=$%06X", frame_count, pc, sr & 0xFFFF, sp);
            if (get_ram)
            {
               uint8_t *ram = get_ram();
               uint32_t v64 = (ram[0x100]<<24) | (ram[0x101]<<16) | (ram[0x102]<<8) | ram[0x103];
               printf(" vec64=$%08X", v64);
            }
            printf("\n");
         }
         if (!got_video)
            printf("  Frame %u: no video output\n", frame_count);
      }

      /* Detailed diagnostic dump at frame 120 to capture hang state */
      if (frame_count == 120)
      {
         printf("\n=== DETAILED DIAGNOSTIC DUMP (frame 120) ===\n");

         /* Dump broader code regions to trace BIOS control flow */
         if (get_ram)
         {
            uint8_t *ram = get_ram();
            printf("RAM dump $005000-$005100 (full BIOS main loop + error handler):\n");
            for (unsigned a = 0x005000; a < 0x005100; a += 16)
            {
               printf("  %06X:", a);
               for (unsigned b = 0; b < 16; b += 2)
                  printf(" %02X%02X", ram[a+b], ram[a+b+1]);
               printf("\n");
            }
            printf("RAM dump $005740-$0057C0 (subroutine at $005774):\n");
            for (unsigned a = 0x005740; a < 0x0057C0; a += 16)
            {
               printf("  %06X:", a);
               for (unsigned b = 0; b < 16; b += 2)
                  printf(" %02X%02X", ram[a+b], ram[a+b+1]);
               printf("\n");
            }
            printf("RAM dump $005960-$005A20 (animation loop at $005A04):\n");
            for (unsigned a = 0x005960; a < 0x005A20; a += 16)
            {
               printf("  %06X:", a);
               for (unsigned b = 0; b < 16; b += 2)
                  printf(" %02X%02X", ram[a+b], ram[a+b+1]);
               printf("\n");
            }
            /* Key BIOS variables */
            printf("BIOS vars: $3721C=%08X $3722A=%04X $37198=%08X $3727C=%04X\n",
                   (ram[0x3721C]<<24)|(ram[0x3721D]<<16)|(ram[0x3721E]<<8)|ram[0x3721F],
                   (ram[0x3722A]<<8)|ram[0x3722B],
                   (ram[0x37198]<<24)|(ram[0x37199]<<16)|(ram[0x3719A]<<8)|ram[0x3719B],
                   (ram[0x3727C]<<8)|ram[0x3727D]);
            /* Dump the continuation of $0050BA subroutine */
            printf("RAM dump $0050F0-$005200 ($0050BA continuation):\n");
            for (unsigned a = 0x0050F0; a < 0x005200; a += 16)
            {
               printf("  %06X:", a);
               for (unsigned b = 0; b < 16; b += 2)
                  printf(" %02X%02X", ram[a+b], ram[a+b+1]);
               printf("\n");
            }
            /* Dump stack contents */
            printf("Stack dump $003FC0-$003FE0:\n");
            for (unsigned a = 0x003FC0; a < 0x003FE0; a += 16)
            {
               printf("  %06X:", a);
               for (unsigned b = 0; b < 16; b += 2)
                  printf(" %02X%02X", ram[a+b], ram[a+b+1]);
               printf("\n");
            }
            /* Exception vectors at crash time */
            printf("Exception vectors:\n");
            for (unsigned v = 0; v < 8; v++)
            {
               uint32_t addr = v * 4;
               uint32_t val = (ram[addr]<<24)|(ram[addr+1]<<16)|(ram[addr+2]<<8)|ram[addr+3];
               printf("  Vec %u ($%03X) = $%08X\n", v, addr, val);
            }
            /* Search for 60FE (BRA.S self) in $005000-$005200 */
            printf("All 60FE (BRA.S self) in $5000-$5200:\n");
            for (unsigned a = 0x005000; a < 0x005200; a += 2)
            {
               if (ram[a] == 0x60 && ram[a+1] == 0xFE)
                  printf("  $%06X: 60FE\n", a);
            }
         }

         /* Print all 68K data and address registers */
         if (p_m68k_get_reg)
         {
            printf("68K registers:\n");
            for (int r = 0; r <= 7; r++)
               printf("  D%d=$%08X", r, p_m68k_get_reg(NULL, M68K_REG_D0_T + r));
            printf("\n");
            for (int r = 0; r <= 7; r++)
               printf("  A%d=$%08X", r, p_m68k_get_reg(NULL, M68K_REG_A0_T + r));
            printf("\n");
            printf("  PC=$%08X  SR=$%04X  SP=$%08X\n",
                   p_m68k_get_reg(NULL, M68K_REG_PC_T),
                   p_m68k_get_reg(NULL, M68K_REG_SR_T) & 0xFFFF,
                   p_m68k_get_reg(NULL, M68K_REG_SP_T));
         }

         /* Read key I/O registers via hardware read functions */
         printf("I/O register state:\n");
         if (p_CDROMReadWord)
         {
            printf("  $DFFF00 (BUTCH int ctrl) = $%04X\n", p_CDROMReadWord(0xDFFF00, 0));
            printf("  $DFFF02 (BUTCH status)   = $%04X\n", p_CDROMReadWord(0xDFFF02, 0));
            /* NOTE: DO NOT read DS_DATA ($DFFF0A) here — it pops the DSA response queue
             * and corrupts the CD boot state. The seek response ($0100) would be consumed
             * by the test harness instead of the BIOS. */
            printf("  $DFFF12 (I2CNTRL)        = $%04X\n", p_CDROMReadWord(0xDFFF12, 0));
         }
         else
            printf("  (CDROMReadWord not available)\n");
         if (p_TOMReadWord)
         {
            printf("  $F00004 (TOM HC)         = $%04X\n", p_TOMReadWord(0xF00004, 0));
            printf("  $F00006 (TOM VC)         = $%04X\n", p_TOMReadWord(0xF00006, 0));
         }
         else
            printf("  (TOMReadWord not available)\n");

         printf("=== END DIAGNOSTIC DUMP ===\n\n");
      }
   }

   /* === Post-loop diagnostic dump === */
   printf("\n=== POST-LOOP DIAGNOSTIC DUMP ===\n");

   if (get_ram)
   {
      uint8_t *ram = get_ram();

      /* Dump RAM at $005080-$005100 — code around the hang point $0050B6 */
      printf("RAM dump $005080-$005100 (code around hang point $0050B6):\n");
      for (unsigned a = 0x005080; a < 0x005100; a += 16)
      {
         printf("  %06X:", a);
         for (unsigned b = 0; b < 16; b += 2)
            printf(" %02X%02X", ram[a+b], ram[a+b+1]);
         printf("\n");
      }

      /* Dump the stuck loop code at $050500-$050A00 */
      printf("\nRAM dump $050500-$050A00 (BIOS loop + continuation):\n");
      for (unsigned a = 0x050500; a < 0x050A00; a += 16)
      {
         printf("  %06X:", a);
         for (unsigned b = 0; b < 16; b += 2)
            printf(" %02X%02X", ram[a+b], ram[a+b+1]);
         printf("\n");
      }

      /* Dump $002C00 mailbox area */
      printf("\nRAM dump $002C00-$002C20 (GPU mailbox):\n");
      for (unsigned a = 0x002C00; a < 0x002C20; a += 16)
      {
         printf("  %06X:", a);
         for (unsigned b = 0; b < 16; b += 2)
            printf(" %02X%02X", ram[a+b], ram[a+b+1]);
         printf("\n");
      }

      /* Dump the flag at $001FD400-$001FD440 */
      printf("\nRAM dump $001FD400-$001FD440 (CD flags incl $1FD418):\n");
      for (unsigned a = 0x001FD400; a < 0x001FD440; a += 16)
      {
         printf("  %06X:", a);
         for (unsigned b = 0; b < 16; b += 2)
            printf(" %02X%02X", ram[a+b], ram[a+b+1]);
         printf("\n");
      }

      /* Dump RAM at $005A00-$005A20 — earlier loop point */
      printf("\nRAM dump $005A00-$005A20 (earlier loop point):\n");
      for (unsigned a = 0x005A00; a < 0x005A20; a += 16)
      {
         printf("  %06X:", a);
         for (unsigned b = 0; b < 16; b += 2)
            printf(" %02X%02X", ram[a+b], ram[a+b+1]);
         printf("\n");
      }

      /* Key BIOS RAM flags for CD data flow */
      {
         uint16_t ae02a = (ram[0x1AE02A]<<8) | ram[0x1AE02B];
         uint16_t af06c = (ram[0x1AF06C]<<8) | ram[0x1AF06D];
         uint16_t fd418 = (ram[0x1FD418]<<8) | ram[0x1FD419];
         uint16_t fd414 = (ram[0x1FD414]<<8) | ram[0x1FD415];
         printf("\nCD BIOS flags: $1AE02A=%04X $1AF06C=%04X $1FD418=%04X $1FD414=%04X\n",
                ae02a, af06c, fd418, fd414);
      }

      /* Dump CD BIOS code at $194D00-$194D60 — this is where PC=$194D18 hangs */
      printf("\nRAM dump $194D00-$194D60 (CD BIOS poll loop at $194D18):\n");
      for (unsigned a = 0x194D00; a < 0x194D60; a += 16)
      {
         printf("  %06X:", a);
         for (unsigned b = 0; b < 16; b += 2)
            printf(" %02X%02X", ram[a+b], ram[a+b+1]);
         printf("\n");
      }

      /* Dump CD BIOS code at $195E00-$195F00 — the loop at $195E34 */
      printf("\nRAM dump $195E00-$195F00 (CD BIOS loop at $195E34):\n");
      for (unsigned a = 0x195E00; a < 0x195F00; a += 16)
      {
         printf("  %06X:", a);
         for (unsigned b = 0; b < 16; b += 2)
            printf(" %02X%02X", ram[a+b], ram[a+b+1]);
         printf("\n");
      }

      /* Dump CD BIOS code at $195F00-$196100 — data formatter at $196028 */
      printf("\nRAM dump $195F00-$196100 (CD BIOS code at $196028):\n");
      for (unsigned a = 0x195F00; a < 0x196100; a += 16)
      {
         printf("  %06X:", a);
         for (unsigned b = 0; b < 16; b += 2)
            printf(" %02X%02X", ram[a+b], ram[a+b+1]);
         printf("\n");
      }

      /* Dump key CD BIOS data structures and variables */
      printf("\nRAM dump $1A0000-$1A0100 (CD BIOS data area):\n");
      for (unsigned a = 0x1A0000; a < 0x1A0100; a += 16)
      {
         printf("  %06X:", a);
         for (unsigned b = 0; b < 16; b += 2)
            printf(" %02X%02X", ram[a+b], ram[a+b+1]);
         printf("\n");
      }
   }

   /* Read and print key I/O registers */
   printf("\nFinal I/O register state:\n");
   if (p_CDROMReadWord)
   {
      printf("  $DFFF00 (BUTCH int ctrl) = $%04X\n", p_CDROMReadWord(0xDFFF00, 0));
      printf("  $DFFF02 (BUTCH status)   = $%04X\n", p_CDROMReadWord(0xDFFF02, 0));
      /* DO NOT read DS_DATA — it pops the DSA queue and corrupts state */
      printf("  $DFFF12 (I2CNTRL)        = $%04X\n", p_CDROMReadWord(0xDFFF12, 0));
   }
   else
      printf("  (CDROMReadWord not available — cannot read BUTCH/CD registers)\n");

   if (p_JERRYReadWord)
   {
      printf("  $F10020 (JERRY INTCTRL)  = $%04X\n", p_JERRYReadWord(0xF10020, 0));
   }

   if (p_TOMReadWord)
   {
      printf("  $F00004 (TOM HC)         = $%04X\n", p_TOMReadWord(0xF00004, 0));
      printf("  $F00006 (TOM VC)         = $%04X\n", p_TOMReadWord(0xF00006, 0));
   }
   else
      printf("  (TOMReadWord not available)\n");

   /* Dump BIOS timer counter at $1AE4D2 */
   {
      uint8_t *ram = get_ram();
      if (ram)
         printf("  $1AE4D2 (BIOS timer)     = $%02X%02X\n", ram[0x1AE4D2], ram[0x1AE4D3]);
   }

   /* Final 68K state */
   if (p_m68k_get_reg)
   {
      printf("\nFinal 68K state:\n");
      printf("  PC=$%08X  SR=$%04X  SP=$%08X\n",
             p_m68k_get_reg(NULL, M68K_REG_PC_T),
             p_m68k_get_reg(NULL, M68K_REG_SR_T) & 0xFFFF,
             p_m68k_get_reg(NULL, M68K_REG_SP_T));
   }

   printf("=== END POST-LOOP DIAGNOSTIC DUMP ===\n");

   printf("\nDone. Total frames: %u\n", num_frames);

   p_retro_unload_game();
   p_retro_deinit();
   dlclose(handle);
   return 0;
}
