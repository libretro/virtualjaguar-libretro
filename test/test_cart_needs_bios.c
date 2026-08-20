/*
 * test_cart_needs_bios.c — JaguarCartNeedsBIOS heuristic + default-HLE
 * autodetect regression (PR #473 review).
 *
 * Part 1: unit table via dlopen/dlsym (no harness lifecycle — shutdown
 *         without load_rom would call retro_unload with a null environ_cb).
 * Part 2: harness loads without --bios; GPU-only cart must force BIOS on,
 *         Rayman-Demo-shaped 2 MiB FF pad must stay HLE.
 *
 * Build: make TEST_EXPORTS=1 test/test_cart_needs_bios
 * Run:   VJ_EXPECT_BUILD=$(./scripts/build-id.sh) ./test/test_cart_needs_bios ./virtualjaguar_libretro.dylib
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <stdbool.h>
#include <dlfcn.h>
#include <unistd.h>

#include "harness/harness.h"

typedef bool (*needs_bios_fn)(const uint8_t *, uint32_t);

/* Minimal prefix of VJSettings — useJaguarBIOS is the second field. */
struct vjs_prefix {
   bool hardwareTypeNTSC;
   bool useJaguarBIOS;
};

struct case_row {
   const char *name;
   int want;
   int (*build)(uint8_t **buf, uint32_t *size);
};

static int build_tiny(uint8_t **buf, uint32_t *size)
{
   uint8_t *b = (uint8_t *)calloc(1, 64);
   if (!b)
      return 0;
   b[0] = 0xFC;
   *buf = b;
   *size = 64;
   return 1;
}

static int build_abs(uint8_t **buf, uint32_t *size)
{
   uint8_t *b = (uint8_t *)calloc(1, 0x2100);
   if (!b)
      return 0;
   b[0] = 0x60;
   b[1] = 0x1B;
   *buf = b;
   *size = 0x2100;
   return 1;
}

static int build_zero_entry(uint8_t **buf, uint32_t *size)
{
   uint8_t *b = (uint8_t *)calloc(1, 0x2100);
   if (!b)
      return 0;
   *buf = b;
   *size = 0x2100;
   return 1;
}

static int build_ff_pad_bootintro(uint8_t **buf, uint32_t *size)
{
   uint8_t *b;
   unsigned i;

   b = (uint8_t *)calloc(1, 0x8000);
   if (!b)
      return 0;
   b[0] = 0xFF;
   for (i = 0; i < 256; i++)
      b[0x2000 + i] = 0xFF;
   *buf = b;
   *size = 0x8000;
   return 1;
}

static int build_rayman_demo_shape(uint8_t **buf, uint32_t *size)
{
   uint8_t *b;
   unsigned i;

   b = (uint8_t *)calloc(1, 0x200000u);
   if (!b)
      return 0;
   b[0] = 0x33;
   b[1] = 0xFC;
   for (i = 0; i < 256; i++)
      b[0x2000 + i] = 0xFF;
   *buf = b;
   *size = 0x200000u;
   return 1;
}

static int build_fc_jagcrypt(uint8_t **buf, uint32_t *size)
{
   uint8_t *b;

   b = (uint8_t *)calloc(1, 0x2100);
   if (!b)
      return 0;
   b[0] = 0xFC;
   b[0x2000] = 0x12;
   b[0x2001] = 0x34;
   *buf = b;
   *size = 0x2100;
   return 1;
}

static int build_addq_fence(uint8_t **buf, uint32_t *size)
{
   uint8_t *b;

   b = (uint8_t *)calloc(1, 0x2100);
   if (!b)
      return 0;
   b[0] = 0x52;
   b[0x2000] = 0x12;
   b[0x2001] = 0x34;
   *buf = b;
   *size = 0x2100;
   return 1;
}

static int build_68k_move(uint8_t **buf, uint32_t *size)
{
   uint8_t *b;

   b = (uint8_t *)calloc(1, 0x2100);
   if (!b)
      return 0;
   b[0x2000] = 0x33;
   b[0x2001] = 0xFC;
   *buf = b;
   *size = 0x2100;
   return 1;
}

static int build_oob_window(uint8_t **buf, uint32_t *size)
{
   uint8_t *b = (uint8_t *)calloc(1, 0x2080);
   if (!b)
      return 0;
   b[0] = 0x11;
   b[0x2000] = 0x00;
   b[0x2001] = 0x00;
   *buf = b;
   *size = 0x2080;
   return 1;
}

static const struct case_row g_cases[] = {
   { "tiny_header", 1, build_tiny },
   { "abs_ram_magic", 0, build_abs },
   { "zero_entry_hle_dummy", 0, build_zero_entry },
   { "ff_pad_bootintro_32k", 1, build_ff_pad_bootintro },
   { "rayman_demo_2mib_ff", 0, build_rayman_demo_shape },
   { "fc_jagcrypt", 1, build_fc_jagcrypt },
   { "addq_non_fc_fence", 0, build_addq_fence },
   { "68k_move_entry", 0, build_68k_move },
   { "mid_size_oob_window", 0, build_oob_window },
   { NULL, 0, NULL }
};

static int run_unit_table(needs_bios_fn fn)
{
   int fail = 0;
   unsigned i;

   for (i = 0; g_cases[i].name; i++)
   {
      uint8_t *buf = NULL;
      uint32_t size = 0;
      int got;

      if (!g_cases[i].build(&buf, &size))
      {
         fprintf(stderr, "FAIL: %s: OOM building fixture\n", g_cases[i].name);
         fail++;
         continue;
      }
      got = fn(buf, size) ? 1 : 0;
      if (got != g_cases[i].want)
      {
         fprintf(stderr, "FAIL: %s: got %d want %d (size=%u)\n",
               g_cases[i].name, got, g_cases[i].want, (unsigned)size);
         fail++;
      }
      else
         fprintf(stderr, "PASS: %s\n", g_cases[i].name);
      free(buf);
   }
   return fail;
}

static int write_rom(const char *path, uint8_t *buf, uint32_t size)
{
   FILE *f;
   size_t nw;

   f = fopen(path, "wb");
   if (!f)
      return 0;
   nw = fwrite(buf, 1, size, f);
   fclose(f);
   return nw == size;
}

static int check_boot_flag(harness_config *cfg, int want_bios, const char *label)
{
   struct vjs_prefix *vjs;

   vjs = (struct vjs_prefix *)harness_dlsym(cfg, "vjs");
   if (!vjs)
   {
      fprintf(stderr, "FAIL: %s: vjs not exported\n", label);
      return 0;
   }
   if ((vjs->useJaguarBIOS ? 1 : 0) != want_bios)
   {
      fprintf(stderr, "FAIL: %s: useJaguarBIOS=%d want %d\n",
            label, (int)vjs->useJaguarBIOS, want_bios);
      return 0;
   }
   fprintf(stderr, "PASS: %s (useJaguarBIOS=%d)\n", label, want_bios);
   return 1;
}

static int run_autodetect_arm(int argc, char **argv, const char *rom_path,
      int want_bios, const char *label)
{
   harness_config cfg = HARNESS_CONFIG_DEFAULT;

   if (!harness_init_from_args(&cfg, argc, argv))
      return 1;
   cfg.use_bios = 0;
   cfg.rom_path = rom_path;
   cfg.frames = 1;
   if (!harness_load_rom(&cfg))
   {
      fprintf(stderr, "FAIL: %s: load_rom\n", label);
      harness_shutdown(&cfg);
      return 1;
   }
   if (!check_boot_flag(&cfg, want_bios, label))
   {
      harness_shutdown(&cfg);
      return 1;
   }
   harness_shutdown(&cfg);
   return 0;
}

static const char *default_core_path(void)
{
#ifdef __APPLE__
   return "./virtualjaguar_libretro.dylib";
#elif defined(_WIN32)
   return "./virtualjaguar_libretro.dll";
#else
   return "./virtualjaguar_libretro.so";
#endif
}

int main(int argc, char **argv)
{
   void *handle;
   needs_bios_fn needs;
   uint8_t *buf = NULL;
   uint32_t size = 0;
   int fail = 0;
   const char *core;
   char ff_path[64];
   char rayman_path[64];
   char addq_path[64];

   /* Per-process scratch paths: two concurrent `make test` suites would
    * otherwise write/read the same fixture ROMs. */
   snprintf(ff_path, sizeof(ff_path), "/tmp/vj_needs_bios_ff_%ld.j64",
            (long)getpid());
   snprintf(rayman_path, sizeof(rayman_path),
            "/tmp/vj_needs_bios_rayman_%ld.j64", (long)getpid());
   snprintf(addq_path, sizeof(addq_path), "/tmp/vj_needs_bios_addq_%ld.j64",
            (long)getpid());

   core = (argc > 1 && argv[1] && argv[1][0] != '-') ? argv[1]
         : default_core_path();

   handle = dlopen(core, RTLD_LAZY);
   if (!handle)
   {
      fprintf(stderr, "FAIL: dlopen(%s): %s\n", core, dlerror());
      return 1;
   }
   needs = (needs_bios_fn)dlsym(handle, "JaguarCartNeedsBIOS");
   if (!needs)
   {
      fprintf(stderr, "FAIL: JaguarCartNeedsBIOS not exported "
            "(rebuild with TEST_EXPORTS=1)\n");
      dlclose(handle);
      return 1;
   }

   fprintf(stderr, "=== JaguarCartNeedsBIOS unit table ===\n");
   fail += run_unit_table(needs);
   dlclose(handle);

   fprintf(stderr, "=== default-HLE autodetect ===\n");

   if (!build_ff_pad_bootintro(&buf, &size) ||
         !write_rom(ff_path, buf, size))
   {
      fprintf(stderr, "FAIL: write ff pad rom\n");
      free(buf);
      return 1;
   }
   free(buf);
   buf = NULL;
   if (run_autodetect_arm(argc, argv, ff_path, 1,
         "autodetect_ff_pad"))
      fail++;
   remove(ff_path);

   if (!build_rayman_demo_shape(&buf, &size) ||
         !write_rom(rayman_path, buf, size))
   {
      fprintf(stderr, "FAIL: write rayman-shape rom\n");
      free(buf);
      return 1;
   }
   free(buf);
   buf = NULL;
   if (run_autodetect_arm(argc, argv, rayman_path, 0,
         "autodetect_rayman_demo_shape"))
      fail++;
   remove(rayman_path);

   if (!build_addq_fence(&buf, &size) ||
         !write_rom(addq_path, buf, size))
   {
      fprintf(stderr, "FAIL: write addq rom\n");
      free(buf);
      return 1;
   }
   free(buf);
   if (run_autodetect_arm(argc, argv, addq_path, 0,
         "autodetect_addq_fence"))
      fail++;
   remove(addq_path);

   if (fail)
   {
      fprintf(stderr, "\n%d failure(s)\n", fail);
      return 1;
   }
   fprintf(stderr, "\nAll cart-needs-BIOS checks passed\n");
   return 0;
}
