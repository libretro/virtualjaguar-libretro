/*
 * test_cart_format.c — cartridge container detection.
 *
 * Covers the prepended copier/dumper header path added for images like
 * "Brutal Sports Football (1994) (Telegames).jag": 2 MiB + 512 bytes, whose
 * payload CRC32 matches the FF_VERIFIED row in filedb.c while the whole-file
 * CRC is catalogued FF_BAD_DUMP.  The loader must skip the header rather than
 * refuse the file, and must do so before the CRC is taken so the core reports
 * the payload's identity.
 *
 * Images are synthesised in memory and handed to retro_load_game() the way a
 * frontend does (info->data / info->size), so no ROM on disk is required.
 *
 * The negative cases matter as much as the positive one: a bare
 * "size overhangs a MiB by 512" rule would start accepting arbitrary junk,
 * so detection also requires the cartridge universal-header marker at the
 * offset measured from the payload.
 *
 * Build:
 *   make -j4 && make TEST_EXPORTS=1 test/test_cart_format
 *
 * Run:
 *   test/test_cart_format [core.dylib]
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <dlfcn.h>

#include "../libretro-common/include/libretro.h"

/* ------------------------------------------------------------------ */
/* Minimal test runner                                                 */
/* ------------------------------------------------------------------ */

static int tf_pass = 0, tf_fail = 0;
static const char *tf_name = "";
static bool tf_failed = false;

#define TEST(n) static void test_##n(void)
#define RUN(n) do { tf_name = #n; tf_failed = false; test_##n(); \
    if (tf_failed) tf_fail++; \
    else { tf_pass++; fprintf(stderr, "  PASS  %s\n", #n); } } while(0)
#define FAIL(fmt, ...) do { fprintf(stderr, "  FAIL  %s:%d: " fmt "\n", \
    tf_name, __LINE__, ##__VA_ARGS__); tf_failed = true; return; } while(0)
#define ASSERT(cond) do { if (!(cond)) FAIL("expected true: %s", #cond); } while(0)
#define ASSERT_EQ_U(a, b) do { unsigned _a=(unsigned)(a), _b=(unsigned)(b); \
    if (_a != _b) FAIL("%s == %s: got %u, want %u", #a, #b, _a, _b); } while(0)

/* ------------------------------------------------------------------ */
/* Core plumbing                                                       */
/* ------------------------------------------------------------------ */

#define MIB              1048576u
#define HEADER_SIZE      512u
#define MARKER_OFFSET    0x400u

static void  *core;
static void  (*p_retro_init)(void);
static void  (*p_retro_deinit)(void);
static void  (*p_retro_set_environment)(retro_environment_t);
static bool  (*p_retro_load_game)(const struct retro_game_info *);
static void  (*p_retro_unload_game)(void);
static uint32_t *p_crc;
static uint32_t *p_romsize;

static bool env_cb(unsigned cmd, void *data)
{
   switch (cmd)
   {
      case RETRO_ENVIRONMENT_GET_SYSTEM_DIRECTORY:
      case RETRO_ENVIRONMENT_GET_SAVE_DIRECTORY:
         *(const char **)data = "/tmp";
         return true;
      case RETRO_ENVIRONMENT_SET_PIXEL_FORMAT:
      case RETRO_ENVIRONMENT_SET_INPUT_DESCRIPTORS:
      case RETRO_ENVIRONMENT_SET_VARIABLES:
      case RETRO_ENVIRONMENT_SET_CORE_OPTIONS_V2:
      case RETRO_ENVIRONMENT_SET_MEMORY_MAPS:
      case RETRO_ENVIRONMENT_SET_SUPPORT_ACHIEVEMENTS:
      case RETRO_ENVIRONMENT_SET_CONTROLLER_INFO:
         return true;
      default:
         return false;
   }
}

/* A cartridge image the loader will take as JST_ROM: `payload` bytes of
 * filler carrying the universal-header marker and run address at $400.
 * `header` bytes of copier junk are prepended when non-zero. */
static uint8_t *make_image(unsigned payload, unsigned header, bool with_marker,
                           unsigned *out_size)
{
   uint8_t *img = (uint8_t *)malloc(header + payload);
   uint8_t *body;
   unsigned i;

   if (!img)
      return NULL;

   /* Deterministic filler — the test asserts CRC equality between two
    * images, so the payload bytes must be reproducible. */
   for (i = 0; i < header + payload; i++)
      img[i] = (uint8_t)(i * 7u + 0x5Au);

   /* Mimic the observed real header: near-zero-fill with a few magic bytes. */
   if (header)
   {
      memset(img, 0, header);
      img[1] = 0x01;
      img[2] = 0x30;
      img[8] = 0xAA;
      img[9] = 0xBB;
      img[10] = 0x04;
   }

   body = img + header;

   if (with_marker)
   {
      body[MARKER_OFFSET + 0] = 0x04;
      body[MARKER_OFFSET + 1] = 0x04;
      body[MARKER_OFFSET + 2] = 0x04;
      body[MARKER_OFFSET + 3] = 0x04;
      /* Run address at $404, as every commercial image carries. */
      body[MARKER_OFFSET + 4] = 0x00;
      body[MARKER_OFFSET + 5] = 0x80;
      body[MARKER_OFFSET + 6] = 0x20;
      body[MARKER_OFFSET + 7] = 0x00;
   }
   else
   {
      body[MARKER_OFFSET + 0] = 0x11;
      body[MARKER_OFFSET + 1] = 0x22;
      body[MARKER_OFFSET + 2] = 0x33;
      body[MARKER_OFFSET + 3] = 0x44;
   }

   *out_size = header + payload;
   return img;
}

static bool load_image(const uint8_t *img, unsigned size)
{
   struct retro_game_info info;
   memset(&info, 0, sizeof(info));
   info.data = img;
   info.size = size;
   info.path = NULL;
   return p_retro_load_game(&info);
}

/* ------------------------------------------------------------------ */
/* Tests                                                               */
/* ------------------------------------------------------------------ */

/* Baseline: an exact-MiB image with no header still loads unchanged. */
TEST(plain_rom_loads)
{
   unsigned size = 0;
   uint8_t *img = make_image(MIB, 0, true, &size);

   ASSERT(img != NULL);
   ASSERT_EQ_U(size, MIB);
   ASSERT(load_image(img, size));
   ASSERT_EQ_U(*p_romsize, MIB);
   p_retro_unload_game();
   free(img);
}

/* The fix: the same payload with 512 bytes of copier junk in front loads,
 * and the core settles on the payload's size and CRC — not the file's. */
TEST(headered_rom_loads_as_payload)
{
   unsigned plain_size = 0, headered_size = 0;
   uint32_t plain_crc;
   uint8_t *plain = make_image(2u * MIB, 0, true, &plain_size);
   uint8_t *headered = make_image(2u * MIB, HEADER_SIZE, true, &headered_size);

   ASSERT(plain != NULL);
   ASSERT(headered != NULL);
   ASSERT_EQ_U(headered_size, plain_size + HEADER_SIZE);

   /* Both images must carry identical payload bytes for the CRC comparison
    * below to mean anything. */
   ASSERT(memcmp(plain, headered + HEADER_SIZE, plain_size) == 0);

   ASSERT(load_image(plain, plain_size));
   ASSERT_EQ_U(*p_romsize, 2u * MIB);
   plain_crc = *p_crc;
   p_retro_unload_game();

   ASSERT(load_image(headered, headered_size));
   ASSERT_EQ_U(*p_romsize, 2u * MIB);
   /* Taken over the payload, so the headered file reports the same identity
    * as the stripped one. */
   ASSERT_EQ_U(*p_crc, plain_crc);
   p_retro_unload_game();

   free(plain);
   free(headered);
}

/* Guard: the size shape alone must not be enough, or the loader would start
 * accepting arbitrary 512-over-a-MiB junk as a cartridge. */
TEST(headered_without_marker_rejected)
{
   unsigned size = 0;
   uint8_t *img = make_image(MIB, HEADER_SIZE, false, &size);

   ASSERT(img != NULL);
   ASSERT(!load_image(img, size));
   free(img);
}

/* Guard: only a 512-byte overhang is a header.  Anything else stays
 * unrecognised, exactly as before. */
TEST(other_overhang_rejected)
{
   unsigned size = 0;
   uint8_t *img = make_image(MIB, 256, true, &size);

   ASSERT(img != NULL);
   ASSERT_EQ_U(size, MIB + 256u);
   ASSERT(!load_image(img, size));
   free(img);
}

/* Guard: a lone 512-byte file must not be read past its end. */
TEST(header_sized_file_rejected)
{
   uint8_t tiny[HEADER_SIZE];
   memset(tiny, 0, sizeof(tiny));
   ASSERT(!load_image(tiny, (unsigned)sizeof(tiny)));
}

int main(int argc, char **argv)
{
   const char *core_path = (argc > 1) ? argv[1]
      : "./virtualjaguar_libretro.dylib";

   core = dlopen(core_path, RTLD_LAZY);
   if (!core)
   {
      fprintf(stderr, "FATAL: cannot dlopen %s: %s\n", core_path, dlerror());
      return 1;
   }

   p_retro_init            = (void (*)(void))dlsym(core, "retro_init");
   p_retro_deinit          = (void (*)(void))dlsym(core, "retro_deinit");
   p_retro_set_environment = (void (*)(retro_environment_t))dlsym(core, "retro_set_environment");
   p_retro_load_game       = (bool (*)(const struct retro_game_info *))dlsym(core, "retro_load_game");
   p_retro_unload_game     = (void (*)(void))dlsym(core, "retro_unload_game");
   p_crc                   = (uint32_t *)dlsym(core, "jaguarMainROMCRC32");
   p_romsize               = (uint32_t *)dlsym(core, "jaguarROMSize");

   if (!p_retro_init || !p_retro_set_environment || !p_retro_load_game
         || !p_retro_unload_game || !p_retro_deinit)
   {
      fprintf(stderr, "FATAL: core is missing required retro_* symbols\n");
      return 1;
   }

   if (!p_crc || !p_romsize)
   {
      fprintf(stderr, "FATAL: jaguarMainROMCRC32 / jaguarROMSize not exported "
                      "-- build with TEST_EXPORTS=1\n");
      return 1;
   }

   fprintf(stderr, "\n=== cart container format ===\n");

   p_retro_set_environment(env_cb);
   p_retro_init();

   RUN(plain_rom_loads);
   RUN(headered_rom_loads_as_payload);
   RUN(headered_without_marker_rejected);
   RUN(other_overhang_rejected);
   RUN(header_sized_file_rejected);

   p_retro_deinit();

   fprintf(stderr, "\n--- cart container format: %d passed, %d failed ---\n\n",
         tf_pass, tf_fail);
   return tf_fail ? 1 : 0;
}
