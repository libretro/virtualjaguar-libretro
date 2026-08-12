/*
 * test_memory_map.c — Verify RetroAchievements memory map advertisement.
 *
 * Loads the core, feeds a minimal dummy ROM, and checks that
 * RETRO_ENVIRONMENT_SET_MEMORY_MAPS is advertised and SET_SUPPORT_ACHIEVEMENTS
 * is called with a non-NULL pointer to bool true (per libretro.h).
 *
 * Build:
 *   cc -o test/tools/test_memory_map test/tools/test_memory_map.c -ldl  (Linux)
 *   cc -o test/tools/test_memory_map test/tools/test_memory_map.c       (macOS)
 *
 * Usage:
 *   ./test/tools/test_memory_map <core.dylib|so>
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <stdint.h>
#include <stdbool.h>

#ifdef __APPLE__
#include <dlfcn.h>
#define LIBEXT "dylib"
#else
#include <dlfcn.h>
#define LIBEXT "so"
#endif

#include "../../libretro-common/include/libretro.h"

typedef void   (*retro_init_t)(void);
typedef void   (*retro_deinit_t)(void);
typedef void   (*retro_set_environment_t)(retro_environment_t);
typedef void   (*retro_set_video_refresh_t)(retro_video_refresh_t);
typedef void   (*retro_set_audio_sample_t)(retro_audio_sample_t);
typedef void   (*retro_set_audio_sample_batch_t)(retro_audio_sample_batch_t);
typedef void   (*retro_set_input_poll_t)(retro_input_poll_t);
typedef void   (*retro_set_input_state_t)(retro_input_state_t);
typedef void   (*retro_get_system_info_t)(struct retro_system_info *);
typedef bool   (*retro_load_game_t)(const struct retro_game_info *);
typedef void   (*retro_unload_game_t)(void);
typedef void  *(*retro_get_memory_data_t)(unsigned);
typedef size_t (*retro_get_memory_size_t)(unsigned);

static bool got_memory_maps;
static bool got_achievements;
static bool achievements_data_ok;
static bool achievements_enabled_true;
static const struct retro_memory_map *captured_memmap;

#define MAX_OVERRIDES 8
static struct retro_system_content_info_override captured_overrides[MAX_OVERRIDES];
static unsigned captured_override_count;

static bool env_cb(unsigned cmd, void *data)
{
    switch (cmd)
    {
    case RETRO_ENVIRONMENT_SET_MEMORY_MAPS:
        got_memory_maps = true;
        captured_memmap = (const struct retro_memory_map *)data;
        return true;
    case RETRO_ENVIRONMENT_SET_SUPPORT_ACHIEVEMENTS:
        got_achievements = true;
        if (data)
        {
            achievements_data_ok = true;
            achievements_enabled_true = *(const bool *)data;
        }
        return true;
    case RETRO_ENVIRONMENT_SET_PIXEL_FORMAT:
    case RETRO_ENVIRONMENT_SET_SUPPORT_NO_GAME:
        return true;
    case RETRO_ENVIRONMENT_GET_VARIABLE:
        return false;
    case RETRO_ENVIRONMENT_GET_VARIABLE_UPDATE:
        if (data) *(bool *)data = false;
        return true;
    case RETRO_ENVIRONMENT_GET_SAVE_DIRECTORY:
    case RETRO_ENVIRONMENT_GET_SYSTEM_DIRECTORY:
        if (data) *(const char **)data = ".";
        return true;
    case RETRO_ENVIRONMENT_GET_CORE_OPTIONS_VERSION:
        if (data) *(unsigned *)data = 0;
        return true;
    case RETRO_ENVIRONMENT_GET_INPUT_BITMASKS:
        return true;
    case RETRO_ENVIRONMENT_SET_CONTENT_INFO_OVERRIDE:
    {
        const struct retro_system_content_info_override *ov =
            (const struct retro_system_content_info_override *)data;
        captured_override_count = 0;
        if (ov)
            for (; ov[captured_override_count].extensions &&
                   captured_override_count < MAX_OVERRIDES;
                 captured_override_count++)
                captured_overrides[captured_override_count] =
                    ov[captured_override_count];
        return true;
    }
    default:
        return false;
    }
}

static void video_cb(const void *d, unsigned w, unsigned h, size_t p)
{ (void)d; (void)w; (void)h; (void)p; }
static void audio_cb(int16_t l, int16_t r)
{ (void)l; (void)r; }
static size_t audio_batch(const int16_t *d, size_t f)
{ (void)d; return f; }
static void input_poll(void) {}
static int16_t input_state(unsigned p, unsigned d, unsigned i, unsigned id)
{ (void)p; (void)d; (void)i; (void)id; return 0; }

static void *load_sym(void *handle, const char *name)
{
    void *sym = dlsym(handle, name);
    if (!sym)
    {
        fprintf(stderr, "ERROR: Missing symbol: %s\n", name);
        exit(1);
    }
    return sym;
}

static bool ext_in_list(const char *list, const char *ext)
{
    size_t elen = strlen(ext);
    const char *p = list;
    while (p && *p)
    {
        const char *bar = strchr(p, '|');
        size_t len = bar ? (size_t)(bar - p) : strlen(p);
        if (len == elen && strncasecmp(p, ext, elen) == 0)
            return true;
        p = bar ? bar + 1 : NULL;
    }
    return false;
}

/*
 * Build a minimal Jaguar ROM with a valid header.
 * The ROM header at 0x400 sets the entry point at 0x802000.
 * We place an infinite loop (BRA.S $802000) at the entry point.
 */
static uint8_t *make_dummy_rom(size_t *size_out)
{
    size_t sz = 131072;
    uint8_t *rom = calloc(1, sz);
    if (!rom) { perror("calloc"); exit(1); }

    /* Jaguar ROM header at offset 0x400 */
    /* 0x404: run address (big-endian) = 0x00802000 */
    rom[0x404] = 0x00; rom[0x405] = 0x80;
    rom[0x406] = 0x20; rom[0x407] = 0x00;

    /* At offset 0x2000 (maps to 0x802000): BRA.S self (0x60FE) */
    rom[0x2000] = 0x60; rom[0x2001] = 0xFE;

    *size_out = sz;
    return rom;
}

int main(int argc, char **argv)
{
    void *handle;
    retro_init_t                core_init;
    retro_deinit_t              core_deinit;
    retro_set_environment_t     core_set_env;
    retro_set_video_refresh_t   core_set_video;
    retro_set_audio_sample_t    core_set_audio;
    retro_set_audio_sample_batch_t core_set_audio_batch;
    retro_set_input_poll_t      core_set_input_poll;
    retro_set_input_state_t     core_set_input_state;
    retro_load_game_t           core_load_game;
    retro_unload_game_t         core_unload_game;
    retro_get_memory_data_t     core_get_memory_data;
    retro_get_memory_size_t     core_get_memory_size;
    struct retro_game_info info;
    uint8_t *rom;
    size_t rom_size;
    int failures = 0;

    if (argc < 2)
    {
        fprintf(stderr, "Usage: %s <core." LIBEXT ">\n", argv[0]);
        return 1;
    }

    handle = dlopen(argv[1], RTLD_LAZY);
    if (!handle) { fprintf(stderr, "dlopen: %s\n", dlerror()); return 1; }

    core_init             = (retro_init_t)load_sym(handle, "retro_init");
    core_deinit           = (retro_deinit_t)load_sym(handle, "retro_deinit");
    core_set_env          = (retro_set_environment_t)load_sym(handle, "retro_set_environment");
    core_set_video        = (retro_set_video_refresh_t)load_sym(handle, "retro_set_video_refresh");
    core_set_audio        = (retro_set_audio_sample_t)load_sym(handle, "retro_set_audio_sample");
    core_set_audio_batch  = (retro_set_audio_sample_batch_t)load_sym(handle, "retro_set_audio_sample_batch");
    core_set_input_poll   = (retro_set_input_poll_t)load_sym(handle, "retro_set_input_poll");
    core_set_input_state  = (retro_set_input_state_t)load_sym(handle, "retro_set_input_state");
    core_load_game        = (retro_load_game_t)load_sym(handle, "retro_load_game");
    core_unload_game      = (retro_unload_game_t)load_sym(handle, "retro_unload_game");
    core_get_memory_data  = (retro_get_memory_data_t)load_sym(handle, "retro_get_memory_data");
    core_get_memory_size  = (retro_get_memory_size_t)load_sym(handle, "retro_get_memory_size");

    got_memory_maps = false;
    got_achievements = false;
    achievements_data_ok = false;
    achievements_enabled_true = false;
    captured_memmap = NULL;
    captured_override_count = 0;

    core_set_env(env_cb);

    /* Test 1: must fire from retro_set_environment before retro_init (not after init) */
    printf("Test 1: SET_SUPPORT_ACHIEVEMENTS (true) ... ");
    if (got_achievements && achievements_data_ok && achievements_enabled_true)
        printf("PASS\n");
    else if (!got_achievements)
    {
        printf("FAIL (not called during retro_set_environment)\n");
        failures++;
    }
    else if (!achievements_data_ok)
    {
        printf("FAIL (NULL data)\n");
        failures++;
    }
    else
    {
        printf("FAIL (expected true, got false)\n");
        failures++;
    }

    /* Test 1b: cartridge extensions must stay memory-loaded.  RetroArch
     * soft patching (IPS/BPS/UPS/xdelta) only applies when the frontend
     * loads content into memory itself: the global need_fullpath must be
     * false AND no SET_CONTENT_INFO_OVERRIDE entry may flip a cartridge
     * extension to need_fullpath=true.  Guarding only the global flag
     * left exactly that hole (issue #409 final-review finding M5). */
    {
        static const char *cart_exts[] =
            { "j64", "jag", "rom", "abs", "cof", "bin", "prg" };
        struct retro_system_info sysinfo;
        unsigned i, j;
        int bad = 0;
        retro_get_system_info_t core_get_system_info =
            (retro_get_system_info_t)load_sym(handle, "retro_get_system_info");
        memset(&sysinfo, 0, sizeof(sysinfo));
        core_get_system_info(&sysinfo);
        printf("Test 1b: cartridge content memory-loaded (soft patching) ... ");
        if (sysinfo.need_fullpath)
        {
            printf("FAIL (global need_fullpath is true; frontend soft patching disabled)\n");
            failures++;
        }
        else
        {
            for (i = 0; i < captured_override_count; i++)
                if (captured_overrides[i].need_fullpath)
                    for (j = 0; j < sizeof(cart_exts) / sizeof(cart_exts[0]); j++)
                        if (ext_in_list(captured_overrides[i].extensions,
                                        cart_exts[j]))
                        {
                            printf("FAIL (override declares '%s' need_fullpath=true) ",
                                   cart_exts[j]);
                            bad = 1;
                        }
            if (bad)
            {
                printf("\n");
                failures++;
            }
            else
                printf("PASS\n");
        }
    }

    /* Test 1c: CD extensions must be declared path-loaded via
     * SET_CONTENT_INFO_OVERRIDE.  Without it the frontend reads a whole
     * .cdi image into RAM (measured: 557 MB peak RSS for a 396 MB image)
     * only for the core to reopen it by path and never touch the buffer. */
    {
        static const char *cd_exts[] = { "cue", "cdi" };
        unsigned i, j;
        int missing = 0;
        printf("Test 1c: CD extensions declared path-loaded (override) ... ");
        for (j = 0; j < sizeof(cd_exts) / sizeof(cd_exts[0]); j++)
        {
            int found = 0;
            for (i = 0; i < captured_override_count; i++)
                if (captured_overrides[i].need_fullpath &&
                    ext_in_list(captured_overrides[i].extensions, cd_exts[j]))
                    found = 1;
            if (!found)
            {
                printf("[missing '%s'] ", cd_exts[j]);
                missing = 1;
            }
        }
        if (missing)
        {
            printf("FAIL\n");
            failures++;
        }
        else
            printf("PASS\n");
    }

    core_set_video(video_cb);
    core_set_audio(audio_cb);
    core_set_audio_batch(audio_batch);
    core_set_input_poll(input_poll);
    core_set_input_state(input_state);
    core_init();

    /* Load the dummy ROM to trigger SET_MEMORY_MAPS */
    rom = make_dummy_rom(&rom_size);
    memset(&info, 0, sizeof(info));
    info.path = "dummy.j64";
    info.data = rom;
    info.size = rom_size;

    if (!core_load_game(&info))
    {
        fprintf(stderr, "ERROR: retro_load_game returned false\n");
        free(rom);
        core_deinit();
        dlclose(handle);
        return 1;
    }

    /* Test 2: SET_MEMORY_MAPS called during retro_load_game */
    printf("Test 2: SET_MEMORY_MAPS called ... ");
    if (got_memory_maps)
        printf("PASS\n");
    else
    {
        printf("FAIL (not called)\n");
        failures++;
    }

    /* Test 3: Memory map has exactly 1 descriptor */
    printf("Test 3: num_descriptors == 1 ... ");
    if (captured_memmap && captured_memmap->num_descriptors == 1)
    {
        if (captured_memmap->descriptors != NULL)
            printf("PASS\n");
        else
        {
            printf("FAIL (descriptors is NULL)\n");
            failures++;
        }
    }
    else
    {
        printf("FAIL (got %u)\n",
               captured_memmap ? (unsigned)captured_memmap->num_descriptors : 0);
        failures++;
    }

    /* Tests 4–9: descriptor [0] (requires non-NULL descriptors table) */
    if (!captured_memmap || captured_memmap->num_descriptors < 1)
    {
        printf("Tests 4-9: SKIPPED (no descriptor)\n");
        failures += 6;
    }
    else if (!captured_memmap->descriptors)
    {
        printf("Tests 4-9: FAIL (descriptors is NULL)\n");
        failures += 6;
    }
    else
    {
        const struct retro_memory_descriptor *d = &captured_memmap->descriptors[0];

        printf("Test 4: descriptor start == 0x000000 ... ");
        if (d->start == 0x000000)
            printf("PASS\n");
        else
        {
            printf("FAIL (0x%06zx)\n", d->start);
            failures++;
        }

        printf("Test 5: descriptor len == 0x200000 ... ");
        if (d->len == 0x200000)
            printf("PASS\n");
        else
        {
            printf("FAIL (0x%06zx)\n", d->len);
            failures++;
        }

        printf("Test 6: RETRO_MEMDESC_SYSTEM_RAM flag set ... ");
        if (d->flags & RETRO_MEMDESC_SYSTEM_RAM)
            printf("PASS\n");
        else
        {
            printf("FAIL (flags=0x%llx)\n", (unsigned long long)d->flags);
            failures++;
        }

        printf("Test 7: RETRO_MEMDESC_BIGENDIAN flag set ... ");
        if (d->flags & RETRO_MEMDESC_BIGENDIAN)
            printf("PASS\n");
        else
        {
            printf("FAIL (flags=0x%llx)\n", (unsigned long long)d->flags);
            failures++;
        }

        printf("Test 8: descriptor ptr is non-NULL ... ");
        if (d->ptr != NULL)
            printf("PASS\n");
        else
        {
            printf("FAIL\n");
            failures++;
        }

        printf("Test 9: descriptor ptr matches retro_get_memory_data(SYSTEM_RAM) ... ");
        {
            void *sysram = core_get_memory_data(RETRO_MEMORY_SYSTEM_RAM);
            if (d->ptr == sysram)
                printf("PASS\n");
            else
            {
                printf("FAIL (map=%p, get_memory=%p)\n", d->ptr, sysram);
                failures++;
            }
        }
    }

    /* Test 10: retro_get_memory_size(SYSTEM_RAM) == 0x200000 */
    printf("Test 10: retro_get_memory_size(SYSTEM_RAM) == 0x200000 ... ");
    {
        size_t sz = core_get_memory_size(RETRO_MEMORY_SYSTEM_RAM);
        if (sz == 0x200000)
            printf("PASS\n");
        else
        {
            printf("FAIL (0x%zx)\n", sz);
            failures++;
        }
    }

    core_unload_game();
    core_deinit();
    free(rom);
    dlclose(handle);

    printf("\n%s: %d test(s) failed.\n",
           failures ? "FAIL" : "OK", failures);
    return failures ? 1 : 0;
}
