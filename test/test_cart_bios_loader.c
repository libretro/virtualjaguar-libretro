/*
 * test_cart_bios_loader.c — custom cart boot ROM loader (issue #469).
 *
 * Exercises stage_cart_boot_rom()'s 'Cart BIOS Type' = Custom path end to
 * end through retro_load_game(): a synthetic cart image is handed to the
 * dlopen'd core the way a frontend does (info->data / info->size, no ROM
 * on disk needed -- same technique as test_cart_format.c), while a real
 * temp directory stands in for the libretro system directory so the
 * loader's file search has something to find (or not find).
 *
 * Covers:
 *   - custom + file present  -> the file's bytes land at $E00000 (its CRC
 *     is deliberately NOT one of the four known dumps, so this also
 *     exercises the "unrecognized image, load it anyway" path)
 *   - custom + no file       -> falls back to the embedded Series K image
 *   - k / m selections       -> unchanged: still load their embedded image
 *
 * The embedded K/M images are linked into THIS binary directly (from
 * src/bios/jagbios.c / jagbios_m.c) purely as independent comparison
 * data -- the core under test is still exercised only via the dlopen'd
 * .dylib/.so, exactly as every other end-to-end test in this tree does.
 *
 * Build: cc -O2 -std=c99 -o test/test_cart_bios_loader \
 *           test/test_cart_bios_loader.c -ldl
 * Run:   ./test/test_cart_bios_loader
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <dlfcn.h>
#include <unistd.h>
#include <sys/stat.h>

#include "../libretro-common/include/libretro.h"

/* ------------------------------------------------------------------ */
/* Embedded boot ROM images, linked directly for comparison only.      */
/* ------------------------------------------------------------------ */
extern uint8_t jaguarBootROM[];
extern uint8_t jaguarBootROM_M[];

/* ------------------------------------------------------------------ */
/* Minimal test runner (mirrors test_cart_format.c's style)            */
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
#define ASSERT_EQ_MEM(a, b, n, what) do { \
    if (memcmp((a), (b), (n)) != 0) FAIL("%s differs from expected", what); \
    } while(0)

/* ------------------------------------------------------------------ */
/* Core plumbing                                                       */
/* ------------------------------------------------------------------ */

#define MIB 1048576u

static void  *core;
static void  (*p_retro_init)(void);
static void  (*p_retro_deinit)(void);
static void  (*p_retro_set_environment)(retro_environment_t);
static bool  (*p_retro_load_game)(const struct retro_game_info *);
static void  (*p_retro_unload_game)(void);
static uint8_t *p_jagMemSpace;

/* Mutable environment state the test cases point wherever they need. */
static const char *g_system_dir = "/tmp";
static const char *g_bios_type_value = NULL; /* NULL == option unset -> core default 'k' */

static bool env_cb(unsigned cmd, void *data)
{
   switch (cmd)
   {
      case RETRO_ENVIRONMENT_GET_SYSTEM_DIRECTORY:
         *(const char **)data = g_system_dir;
         return true;
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
      case RETRO_ENVIRONMENT_GET_VARIABLE:
      {
         struct retro_variable *var = (struct retro_variable *)data;
         if (!var->key) { var->value = NULL; return false; }
         if (strcmp(var->key, "virtualjaguar_bios_type") == 0)
         {
            var->value = g_bios_type_value;
            return g_bios_type_value != NULL;
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

/* Synthetic cart image the loader accepts as JST_ROM: 1 MiB of
 * deterministic filler carrying the universal-header marker and a
 * plausible run address at $400, same shape as test_cart_format.c's
 * make_image(). What matters here is only that retro_load_game()
 * succeeds -- stage_cart_boot_rom() runs before the cart body is even
 * parsed, so its outcome doesn't depend on this image's content. */
static uint8_t *make_cart_image(unsigned *out_size)
{
   uint8_t *img = (uint8_t *)malloc(MIB);
   unsigned i;

   if (!img)
      return NULL;

   for (i = 0; i < MIB; i++)
      img[i] = (uint8_t)(i * 7u + 0x5Au);

   img[0x400] = 0x04;
   img[0x401] = 0x04;
   img[0x402] = 0x04;
   img[0x403] = 0x04;
   img[0x404] = 0x00;
   img[0x405] = 0x80;
   img[0x406] = 0x20;
   img[0x407] = 0x00;

   *out_size = MIB;
   return img;
}

static bool load_cart(void)
{
   struct retro_game_info info;
   unsigned size = 0;
   uint8_t *img = make_cart_image(&size);
   bool ok;

   if (!img)
      return false;

   memset(&info, 0, sizeof(info));
   info.data = img;
   info.size = size;
   info.path = NULL;

   ok = p_retro_load_game(&info);
   free(img);
   return ok;
}

/* Writes `len` bytes of the given fill pattern to `path`. */
static bool write_pattern_file(const char *path, unsigned len, uint8_t seed)
{
   FILE *f = fopen(path, "wb");
   uint8_t *buf;
   unsigned i;
   size_t written;

   if (!f)
      return false;

   buf = (uint8_t *)malloc(len);
   if (!buf) { fclose(f); return false; }

   for (i = 0; i < len; i++)
      buf[i] = (uint8_t)(i * 13u + seed);

   written = fwrite(buf, 1, len, f);
   fclose(f);
   free(buf);
   return written == len;
}

static bool expect_pattern(const uint8_t *data, unsigned len, uint8_t seed)
{
   unsigned i;
   for (i = 0; i < len; i++)
      if (data[i] != (uint8_t)(i * 13u + seed))
         return false;
   return true;
}

/* Locates a real '[BIOS] Atari Jaguar (World).j64' dump in the private
 * corpus, if present. Returns NULL when it isn't -- callers must skip
 * cleanly rather than fail, since CI (and any fresh checkout) has no
 * private ROMs at all. */
static const char *find_real_k_dump(void)
{
   static const char *candidates[] = {
      "test/roms/private/ROMS/[BIOS] Atari Jaguar (World).j64",
      "test/roms/private/[BIOS] Atari Jaguar (World).j64",
      NULL
   };
   unsigned int i;
   struct stat st;

   for (i = 0; candidates[i]; i++)
      if (stat(candidates[i], &st) == 0)
         return candidates[i];
   return NULL;
}

/* ------------------------------------------------------------------ */
/* Tests                                                               */
/* ------------------------------------------------------------------ */

TEST(custom_with_file_present_loads_it)
{
   /* mkdtemp() is hidden behind different feature-test macros on Darwin
    * vs glibc under -std=c99, and glibc Clang treats the resulting
    * implicit declaration as a hard error (see test_cd_pregap.c /
    * test_cd_synth_cdda.c for the same workaround) -- a getpid()-keyed
    * name needs no uniqueness beyond the running process. */
   char dirbuf[512];
   char *dir = dirbuf;
   char path[512];

   snprintf(dirbuf, sizeof(dirbuf), "/tmp/vj_cartbios_present_%ld", (long)getpid());
   ASSERT(mkdir(dir, 0755) == 0);
   snprintf(path, sizeof(path), "%s/jagboot.rom", dir);
   ASSERT(write_pattern_file(path, 0x20000, 0x11));

   g_system_dir = dir;
   g_bios_type_value = "custom";

   ASSERT(load_cart());
   ASSERT(p_jagMemSpace != NULL);
   ASSERT(expect_pattern(p_jagMemSpace + 0xE00000, 0x20000, 0x11));

   p_retro_unload_game();
   remove(path);
   rmdir(dir);
}

TEST(custom_without_file_falls_back_to_embedded_k)
{
   char dirbuf[512];
   char *dir = dirbuf;

   snprintf(dirbuf, sizeof(dirbuf), "/tmp/vj_cartbios_nofile_%ld", (long)getpid());
   ASSERT(mkdir(dir, 0755) == 0);
   /* Deliberately empty: no jagboot.rom / boot.rom / boot0.rom / etc. */

   g_system_dir = dir;
   g_bios_type_value = "custom";

   ASSERT(load_cart());
   ASSERT(p_jagMemSpace != NULL);
   ASSERT_EQ_MEM(p_jagMemSpace + 0xE00000, jaguarBootROM, 0x20000,
                 "fallback boot ROM");

   p_retro_unload_game();
   rmdir(dir);
}

TEST(k_series_unchanged)
{
   char dirbuf[512];
   char *dir = dirbuf;
   char path[512];

   snprintf(dirbuf, sizeof(dirbuf), "/tmp/vj_cartbios_kseries_%ld", (long)getpid());
   ASSERT(mkdir(dir, 0755) == 0);
   /* A jagboot.rom sitting right there must be ignored -- 'k' never
    * consults the custom search path. */
   snprintf(path, sizeof(path), "%s/jagboot.rom", dir);
   ASSERT(write_pattern_file(path, 0x20000, 0x22));

   g_system_dir = dir;
   g_bios_type_value = "k";

   ASSERT(load_cart());
   ASSERT(p_jagMemSpace != NULL);
   ASSERT_EQ_MEM(p_jagMemSpace + 0xE00000, jaguarBootROM, 0x20000,
                 "Series K boot ROM");

   p_retro_unload_game();
   remove(path);
   rmdir(dir);
}

TEST(model_m_unchanged)
{
   char dirbuf[512];
   char *dir = dirbuf;

   snprintf(dirbuf, sizeof(dirbuf), "/tmp/vj_cartbios_modelm_%ld", (long)getpid());
   ASSERT(mkdir(dir, 0755) == 0);
   /* No jagboot_m.rom present -> embedded Model M image, same as
    * develop's existing behaviour. */

   g_system_dir = dir;
   g_bios_type_value = "m";

   ASSERT(load_cart());
   ASSERT(p_jagMemSpace != NULL);
   ASSERT_EQ_MEM(p_jagMemSpace + 0xE00000, jaguarBootROM_M, 0x20000,
                 "Model M boot ROM");

   p_retro_unload_game();
   rmdir(dir);
}

TEST(custom_wrong_size_file_is_skipped)
{
   char dirbuf[512];
   char *dir = dirbuf;
   char path[512];

   snprintf(dirbuf, sizeof(dirbuf), "/tmp/vj_cartbios_wrongsize_%ld", (long)getpid());
   ASSERT(mkdir(dir, 0755) == 0);
   /* Present but the wrong size -- must be rejected, not loaded, and the
    * search must fall through to the embedded-K fallback exactly like
    * "no file at all". */
   snprintf(path, sizeof(path), "%s/jagboot.rom", dir);
   ASSERT(write_pattern_file(path, 0x1000, 0x33));

   g_system_dir = dir;
   g_bios_type_value = "custom";

   ASSERT(load_cart());
   ASSERT(p_jagMemSpace != NULL);
   ASSERT_EQ_MEM(p_jagMemSpace + 0xE00000, jaguarBootROM, 0x20000,
                 "fallback boot ROM (wrong-size candidate skipped)");

   p_retro_unload_game();
   remove(path);
   rmdir(dir);
}

TEST(custom_finds_file_in_subdirectory)
{
   char dirbuf[512];
   char *dir = dirbuf;
   char subdir[600];
   char path[700];

   snprintf(dirbuf, sizeof(dirbuf), "/tmp/vj_cartbios_subdir_%ld", (long)getpid());
   ASSERT(mkdir(dir, 0755) == 0);
   snprintf(subdir, sizeof(subdir), "%s/Atari - Jaguar", dir);
   ASSERT(mkdir(subdir, 0755) == 0);
   snprintf(path, sizeof(path), "%s/boot0.rom", subdir);
   ASSERT(write_pattern_file(path, 0x20000, 0x44));

   g_system_dir = dir;
   g_bios_type_value = "custom";

   ASSERT(load_cart());
   ASSERT(p_jagMemSpace != NULL);
   ASSERT(expect_pattern(p_jagMemSpace + 0xE00000, 0x20000, 0x44));

   p_retro_unload_game();
   remove(path);
   rmdir(subdir);
   rmdir(dir);
}

/* Filename priority is the outer loop (see load_external_cart_boot_rom()'s
 * comment in libretro.c): a higher-priority filename several
 * sub-directories down must still beat a lower-priority filename sitting
 * right in the system directory root. Pins that ordering decision rather
 * than leaving it incidental -- without this, custom_finds_file_in_
 * subdirectory above would still pass even if the loop order were
 * accidentally reversed, because it never puts two different candidate
 * names in play at once. */
TEST(custom_filename_priority_beats_subdirectory)
{
   char dirbuf[512];
   char *dir = dirbuf;
   char subdir[600];
   char lo_path[700];  /* boot0.rom: lower priority, in the root */
   char hi_path[700];  /* jagboot.rom: higher priority, in a sub-dir */

   snprintf(dirbuf, sizeof(dirbuf), "/tmp/vj_cartbios_priority_%ld", (long)getpid());
   ASSERT(mkdir(dir, 0755) == 0);
   snprintf(lo_path, sizeof(lo_path), "%s/boot0.rom", dir);
   ASSERT(write_pattern_file(lo_path, 0x20000, 0x55));

   snprintf(subdir, sizeof(subdir), "%s/jaguar", dir);
   ASSERT(mkdir(subdir, 0755) == 0);
   snprintf(hi_path, sizeof(hi_path), "%s/jagboot.rom", subdir);
   ASSERT(write_pattern_file(hi_path, 0x20000, 0x66));

   g_system_dir = dir;
   g_bios_type_value = "custom";

   ASSERT(load_cart());
   ASSERT(p_jagMemSpace != NULL);
   /* jagboot.rom (higher-priority name) wins even though it is the one
    * buried in a sub-directory. */
   ASSERT(expect_pattern(p_jagMemSpace + 0xE00000, 0x20000, 0x66));

   p_retro_unload_game();
   remove(lo_path);
   remove(hi_path);
   rmdir(subdir);
   rmdir(dir);
}

/* Optional: a real '[BIOS] Atari Jaguar (World).j64' dump, if the private
 * corpus has one, loaded through the full 'custom' path must land at
 * $E00000 byte-identical to the embedded Series K image -- the strongest
 * evidence available (short of running frames) that both the raw-byte
 * read path and the recognized-CRC identification path are correct, not
 * just the unrecognized-CRC path every other case above exercises.
 * Skips cleanly (not a failure) when the private corpus isn't present. */
TEST(custom_recognized_real_image_matches_embedded_k)
{
   const char *src_path = find_real_k_dump();
   char dirbuf[512];
   char *dir;
   char dst_path[512];
   FILE *src, *dst;
   unsigned char *buf;
   size_t n;

   ASSERT(src_path != NULL); /* main() only RUNs this when a dump exists */

   dir = dirbuf;
   snprintf(dirbuf, sizeof(dirbuf), "/tmp/vj_cartbios_realdump_%ld", (long)getpid());
   ASSERT(mkdir(dir, 0755) == 0);

   src = fopen(src_path, "rb");
   ASSERT(src != NULL);
   buf = (uint8_t *)malloc(0x20000);
   ASSERT(buf != NULL);
   n = fread(buf, 1, 0x20000, src);
   fclose(src);
   ASSERT(n == 0x20000);

   snprintf(dst_path, sizeof(dst_path), "%s/jagboot.rom", dir);
   dst = fopen(dst_path, "wb");
   ASSERT(dst != NULL);
   ASSERT(fwrite(buf, 1, 0x20000, dst) == 0x20000);
   fclose(dst);
   free(buf);

   g_system_dir = dir;
   g_bios_type_value = "custom";

   ASSERT(load_cart());
   ASSERT(p_jagMemSpace != NULL);
   ASSERT_EQ_MEM(p_jagMemSpace + 0xE00000, jaguarBootROM, 0x20000,
                 "real recognized K dump loaded via 'custom'");

   p_retro_unload_game();
   remove(dst_path);
   rmdir(dir);
}

int main(int argc, char **argv)
{
   const char *core_path = (argc > 1) ? argv[1]
      : "./virtualjaguar_libretro.dylib";

   core = dlopen(core_path, RTLD_LAZY);
   if (!core)
   {
      /* Fall back to the .so name for non-macOS hosts run without an
       * explicit path argument. */
      core = dlopen("./virtualjaguar_libretro.so", RTLD_LAZY);
   }
   if (!core)
   {
      fprintf(stderr, "FATAL: cannot dlopen core: %s\n", dlerror());
      return 1;
   }

   p_retro_init            = (void (*)(void))dlsym(core, "retro_init");
   p_retro_deinit          = (void (*)(void))dlsym(core, "retro_deinit");
   p_retro_set_environment = (void (*)(retro_environment_t))dlsym(core, "retro_set_environment");
   p_retro_load_game       = (bool (*)(const struct retro_game_info *))dlsym(core, "retro_load_game");
   p_retro_unload_game     = (void (*)(void))dlsym(core, "retro_unload_game");
   p_jagMemSpace           = (uint8_t *)dlsym(core, "jagMemSpace");

   if (!p_retro_init || !p_retro_set_environment || !p_retro_load_game
         || !p_retro_unload_game || !p_retro_deinit)
   {
      fprintf(stderr, "FATAL: core is missing required retro_* symbols\n");
      return 1;
   }

   if (!p_jagMemSpace)
   {
      fprintf(stderr, "FATAL: jagMemSpace not exported "
                      "-- build with TEST_EXPORTS=1\n");
      return 1;
   }

   fprintf(stderr, "\n=== custom cart boot ROM loader ===\n");

   p_retro_set_environment(env_cb);
   p_retro_init();

   RUN(custom_with_file_present_loads_it);
   RUN(custom_without_file_falls_back_to_embedded_k);
   RUN(k_series_unchanged);
   RUN(model_m_unchanged);
   RUN(custom_wrong_size_file_is_skipped);
   RUN(custom_finds_file_in_subdirectory);
   RUN(custom_filename_priority_beats_subdirectory);

   if (find_real_k_dump())
      RUN(custom_recognized_real_image_matches_embedded_k);
   else
      fprintf(stderr, "  SKIP  custom_recognized_real_image_matches_embedded_k "
                      "(private corpus not present)\n");

   p_retro_deinit();

   fprintf(stderr, "\n--- custom cart boot ROM loader: %d passed, %d failed ---\n\n",
         tf_pass, tf_fail);
   return tf_fail ? 1 : 0;
}
