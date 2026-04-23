/*
 * test_rcheevos_e2e.c — Load Virtual Jaguar, feed rcheevos rc_libretro with the same
 * SET_MEMORY_MAPS descriptor RetroArch receives, then verify address resolution matches
 * host RAM (offline; no RetroAchievements server).
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>

#include <dlfcn.h>

#include "../../libretro-common/include/libretro.h"
#include "rc_libretro.h"
#include "rc_consoles.h"

typedef void   (*retro_init_t)(void);
typedef void   (*retro_deinit_t)(void);
typedef void   (*retro_set_environment_t)(retro_environment_t);
typedef void   (*retro_set_video_refresh_t)(retro_video_refresh_t);
typedef void   (*retro_set_audio_sample_t)(retro_audio_sample_t);
typedef void   (*retro_set_audio_sample_batch_t)(retro_audio_sample_batch_t);
typedef void   (*retro_set_input_poll_t)(retro_input_poll_t);
typedef void   (*retro_set_input_state_t)(retro_input_state_t);
typedef bool   (*retro_load_game_t)(const struct retro_game_info *);
typedef void   (*retro_unload_game_t)(void);
typedef void  *(*retro_get_memory_data_t)(unsigned);
typedef size_t (*retro_get_memory_size_t)(unsigned);

static const struct retro_memory_map *g_mmap;
static retro_get_memory_data_t g_get_memory_data;
static retro_get_memory_size_t g_get_memory_size;

static bool env_cb(unsigned cmd, void *data)
{
    switch (cmd)
    {
    case RETRO_ENVIRONMENT_SET_MEMORY_MAPS:
        g_mmap = (const struct retro_memory_map *)data;
        return true;
    case RETRO_ENVIRONMENT_SET_SUPPORT_ACHIEVEMENTS:
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
    default:
        return false;
    }
}

static void libretro_get_core_memory_info(uint32_t id, rc_libretro_core_memory_info_t *info)
{
    if (!g_get_memory_data || !g_get_memory_size)
    {
        info->data = NULL;
        info->size = 0;
        return;
    }
    info->data = (uint8_t *)g_get_memory_data(id);
    info->size = g_get_memory_size(id);
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

static uint8_t *make_dummy_rom(size_t *size_out)
{
    size_t sz = 12288;
    uint8_t *rom = calloc(1, sz);
    if (!rom) { perror("calloc"); exit(1); }
    rom[0x404] = 0x00; rom[0x405] = 0x80;
    rom[0x406] = 0x20; rom[0x407] = 0x00;
    rom[0x2000] = 0x60; rom[0x2001] = 0xFE;
    *size_out = sz;
    return rom;
}

int main(int argc, char **argv)
{
    void *handle;
    retro_init_t core_init;
    retro_deinit_t core_deinit;
    retro_set_environment_t core_set_env;
    retro_set_video_refresh_t core_set_video;
    retro_set_audio_sample_t core_set_audio;
    retro_set_audio_sample_batch_t core_set_audio_batch;
    retro_set_input_poll_t core_set_input_poll;
    retro_set_input_state_t core_set_input_state;
    retro_load_game_t core_load_game;
    retro_unload_game_t core_unload_game;
    retro_get_memory_data_t core_get_memory_data;
    retro_get_memory_size_t core_get_memory_size;
    struct retro_game_info info;
    uint8_t *rom;
    size_t rom_size;
    rc_libretro_memory_regions_t regions;
    uint8_t *sysram;
    uint8_t buf[4];
    uint32_t avail;
    uint8_t *pfind;
    int failures = 0;
    int mmap_ok;
    int regions_inited = 0;

    if (argc < 2)
    {
        fprintf(stderr, "Usage: %s <core.so|dylib>\n", argv[0]);
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

    g_mmap = NULL;
    g_get_memory_data = core_get_memory_data;
    g_get_memory_size = core_get_memory_size;

    core_set_env(env_cb);
    core_set_video(video_cb);
    core_set_audio(audio_cb);
    core_set_audio_batch(audio_batch);
    core_set_input_poll(input_poll);
    core_set_input_state(input_state);
    core_init();

    rom = make_dummy_rom(&rom_size);
    memset(&info, 0, sizeof(info));
    info.path = "dummy.j64";
    info.data = rom;
    info.size = rom_size;

    if (!core_load_game(&info))
    {
        fprintf(stderr, "retro_load_game failed\n");
        free(rom);
        core_deinit();
        dlclose(handle);
        return 1;
    }

    mmap_ok = (g_mmap != NULL && g_mmap->descriptors != NULL
               && g_mmap->num_descriptors >= 1);

    printf("Test 1: SET_MEMORY_MAPS captured ... ");
    if (!mmap_ok)
    {
        printf("FAIL\n");
        failures++;
    }
    else
        printf("PASS\n");

    memset(&regions, 0, sizeof(regions));

    if (!mmap_ok)
    {
        printf("Test 2: rc_libretro_memory_init(Jaguar) ... SKIPPED (no SET_MEMORY_MAPS)\n");
        printf("Test 3: rc_libretro_memory_read(0xABCD) ... SKIPPED\n");
        printf("Test 4: rc_libretro_memory_find(0xABCD) matches host ptr ... SKIPPED\n");
        printf("Test 5: cross-boundary read at end of RAM ... SKIPPED\n");
        printf("Test 6: rc_libretro_memory_find_avail ... SKIPPED\n");
    }
    else
    {
        printf("Test 2: rc_libretro_memory_init(Jaguar) ... ");
        if (!rc_libretro_memory_init(&regions, g_mmap, libretro_get_core_memory_info,
                                     RC_CONSOLE_ATARI_JAGUAR))
        {
            printf("FAIL\n");
            failures++;
        }
        else
        {
            printf("PASS\n");
            regions_inited = 1;
        }

        if (!regions_inited)
        {
            printf("Test 3: rc_libretro_memory_read(0xABCD) ... SKIPPED\n");
            printf("Test 4: rc_libretro_memory_find(0xABCD) matches host ptr ... SKIPPED\n");
            printf("Test 5: cross-boundary read at end of RAM ... SKIPPED\n");
            printf("Test 6: rc_libretro_memory_find_avail ... SKIPPED\n");
        }
        else
        {
            sysram = (uint8_t *)core_get_memory_data(RETRO_MEMORY_SYSTEM_RAM);
            if (sysram)
            {
                sysram[0xABCD] = 0x42;
                sysram[0x1FFFFE] = 0x11;
                sysram[0x1FFFFF] = 0x22;
            }

            printf("Test 3: rc_libretro_memory_read(0xABCD) ... ");
            memset(buf, 0, sizeof(buf));
            if (rc_libretro_memory_read(&regions, 0xABCDU, buf, 1) == 1 && buf[0] == 0x42)
                printf("PASS\n");
            else
            {
                printf("FAIL\n");
                failures++;
            }

            printf("Test 4: rc_libretro_memory_find(0xABCD) matches host ptr ... ");
            pfind = rc_libretro_memory_find(&regions, 0xABCDU);
            if (sysram && pfind == sysram + 0xABCD)
                printf("PASS\n");
            else
            {
                printf("FAIL\n");
                failures++;
            }

            printf("Test 5: cross-boundary read at end of RAM ... ");
            memset(buf, 0, sizeof(buf));
            if (rc_libretro_memory_read(&regions, 0x1FFFFEU, buf, 2) == 2
                && buf[0] == 0x11 && buf[1] == 0x22)
                printf("PASS\n");
            else
            {
                printf("FAIL\n");
                failures++;
            }

            printf("Test 6: rc_libretro_memory_find_avail ... ");
            pfind = rc_libretro_memory_find_avail(&regions, 0x1FFFFEU, &avail);
            if (pfind && avail >= 2 && sysram && pfind == sysram + 0x1FFFFE)
                printf("PASS\n");
            else
            {
                printf("FAIL\n");
                failures++;
            }
        }

        if (regions_inited)
            rc_libretro_memory_destroy(&regions);
    }

    core_unload_game();
    core_deinit();
    free(rom);
    dlclose(handle);

    printf("\n%s: %d test(s) failed.\n", failures ? "FAIL" : "OK", failures);
    return failures ? 1 : 0;
}
