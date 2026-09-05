/*
 * test_boot_config.c — BootConfig resolver tests.
 *
 * Part 1: Unit tests calling ResolveBootConfig() directly via dlsym to
 *          verify all input combinations produce the correct resolved
 *          boot configuration.
 *
 * Part 2: Integration tests loading actual disc images through
 *          retro_load_game() and verifying bootConfig matches the
 *          expected resolved state, exactly as RetroArch would.
 *
 * Build:
 *   make -j4 DEBUG=1 && make test/test_boot_config
 *
 * Run:
 *   DYLD_LIBRARY_PATH=. test/test_boot_config
 */

#include <stdio.h>
#include <stdlib.h>
#ifdef _MSC_VER
#include <compat/posix_string.h>  /* strcasecmp shim (no <strings.h> on Windows) */
#else
#include <strings.h>              /* strcasecmp (POSIX; glibc c99 lacks it in <string.h>) */
#endif
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <dlfcn.h>
#include <sys/stat.h>
#include <dirent.h>

#include "../libretro-common/include/libretro.h"

/* ------------------------------------------------------------------ */
/* Minimal test runner                                                 */
/* ------------------------------------------------------------------ */

static int tf_pass = 0, tf_fail = 0, tf_skip = 0;
static const char *tf_suite = "";
static const char *tf_name = "";
static bool tf_failed = false;

#define SUITE(n) do { tf_suite = (n); tf_pass = tf_fail = tf_skip = 0; \
    fprintf(stderr, "\n=== %s ===\n", tf_suite); } while(0)
#define TEST(n) static void test_##n(void)
#define RUN(n) do { tf_name = #n; tf_failed = false; test_##n(); \
    if (tf_failed) tf_fail++; \
    else { tf_pass++; fprintf(stderr, "  PASS  %s\n", #n); } } while(0)
#define SKIP(n, r) do { tf_skip++; fprintf(stderr, "  SKIP  %s (%s)\n", #n, r); } while(0)
#define REPORT() (fprintf(stderr, "\n--- %s: %d passed, %d failed, %d skipped ---\n\n", \
    tf_suite, tf_pass, tf_fail, tf_skip), tf_fail)
#define FAIL(fmt, ...) do { fprintf(stderr, "  FAIL  %s:%d: " fmt "\n", \
    tf_name, __LINE__, ##__VA_ARGS__); tf_failed = true; return; } while(0)
#define ASSERT(cond) do { if (!(cond)) FAIL("expected true: %s", #cond); } while(0)
#define ASSERT_EQ(a, b) do { int _a=(int)(a), _b=(int)(b); \
    if (_a != _b) FAIL("%s == %s: got %d, want %d", #a, #b, _a, _b); } while(0)
#define ASSERT_STR(a, b) do { if (strcmp((a),(b))!=0) \
    FAIL("%s == %s: got \"%s\"", #a, #b, (a)); } while(0)

/* ------------------------------------------------------------------ */
/* Mirror of BootConfig/CDBootStrategy (must match settings.h/jagcd_boot.h) */
/* ------------------------------------------------------------------ */

typedef struct CDBootStrategy {
    const char *name;
    void *boot;
    void *instruction_hook;
    void *reset;
} CDBootStrategy;

struct BootConfig {
    bool isCDGame;
    bool showBootROM;
    bool cdBiosAvailable;
    const CDBootStrategy *strategy;
};

enum { CDBOOT_AUTO = 0, CDBOOT_HLE = 1, CDBOOT_BIOS = 2 };

/* ------------------------------------------------------------------ */
/* Core handle + dlsym pointers                                        */
/* ------------------------------------------------------------------ */

static void *g_handle;

typedef void (*resolve_fn)(struct BootConfig *, bool, bool, uint32_t, bool);
static resolve_fn p_ResolveBootConfig;
static struct BootConfig *p_bootConfig;

static const CDBootStrategy *p_strategy_hle;
static const CDBootStrategy *p_strategy_bios;
static const CDBootStrategy *p_strategy_cart;
static const CDBootStrategy *p_strategy_none;

#define IS_HLE(c)  ((c).strategy == p_strategy_hle)
#define IS_BIOS(c) ((c).strategy == p_strategy_bios)
#define IS_CART(c)  ((c).strategy == p_strategy_cart)
#define IS_NONE(c)  ((c).strategy == p_strategy_none)

static void (*p_retro_init)(void);
static void (*p_retro_deinit)(void);
static bool (*p_retro_load_game)(const struct retro_game_info *);
static void (*p_retro_unload_game)(void);
static void (*p_retro_run)(void);
static void (*p_retro_reset)(void);
static void (*p_retro_set_environment)(retro_environment_t);
static void (*p_retro_set_video_refresh)(retro_video_refresh_t);
static void (*p_retro_set_audio_sample)(retro_audio_sample_t);
static void (*p_retro_set_audio_sample_batch)(retro_audio_sample_batch_t);
static void (*p_retro_set_input_poll)(retro_input_poll_t);
static void (*p_retro_set_input_state)(retro_input_state_t);
static void *(*p_retro_get_memory_data)(unsigned);
static size_t (*p_retro_get_memory_size)(unsigned);

#define LOAD_SYM(name) do { \
    p_##name = dlsym(g_handle, #name); \
    if (!p_##name) { fprintf(stderr, "FATAL: dlsym(%s): %s\n", #name, dlerror()); exit(1); } \
} while(0)
#define LOAD_OPT(name) (p_##name = dlsym(g_handle, #name))

static bool load_core(void)
{
#ifdef __APPLE__
    const char *lib = "./virtualjaguar_libretro.dylib";
#elif defined(_WIN32)
    const char *lib = "./virtualjaguar_libretro.dll";
#else
    const char *lib = "./virtualjaguar_libretro.so";
#endif
    g_handle = dlopen(lib, RTLD_LAZY);
    if (!g_handle) { fprintf(stderr, "FATAL: dlopen: %s\n", dlerror()); return false; }

    LOAD_SYM(ResolveBootConfig);
    p_bootConfig = dlsym(g_handle, "bootConfig");
    if (!p_bootConfig) { fprintf(stderr, "FATAL: dlsym(bootConfig)\n"); return false; }
    p_strategy_hle  = dlsym(g_handle, "cd_boot_strategy_hle");
    p_strategy_bios = dlsym(g_handle, "cd_boot_strategy_bios");
    p_strategy_cart = dlsym(g_handle, "cd_boot_strategy_cart");
    p_strategy_none = dlsym(g_handle, "cd_boot_strategy_none");
    if (!p_strategy_hle || !p_strategy_bios || !p_strategy_cart || !p_strategy_none)
    { fprintf(stderr, "FATAL: dlsym(strategies)\n"); return false; }

    LOAD_SYM(retro_init);
    LOAD_SYM(retro_deinit);
    LOAD_SYM(retro_set_environment);
    LOAD_SYM(retro_set_video_refresh);
    LOAD_SYM(retro_set_audio_sample);
    LOAD_SYM(retro_set_audio_sample_batch);
    LOAD_SYM(retro_set_input_poll);
    LOAD_SYM(retro_set_input_state);

    p_retro_load_game = dlsym(g_handle, "retro_load_game");
    p_retro_unload_game = dlsym(g_handle, "retro_unload_game");
    p_retro_run = dlsym(g_handle, "retro_run");
    p_retro_reset = dlsym(g_handle, "retro_reset");
    p_retro_get_memory_data = dlsym(g_handle, "retro_get_memory_data");
    p_retro_get_memory_size = dlsym(g_handle, "retro_get_memory_size");

    return true;
}

/* ------------------------------------------------------------------ */
/* Part 1: Unit tests for ResolveBootConfig()                          */
/* ------------------------------------------------------------------ */

static void resolve(struct BootConfig *c, bool cd, bool biosLoaded, uint32_t mode, bool wantBIOS)
{
    memset(c, 0, sizeof(*c));
    p_ResolveBootConfig(c, cd, biosLoaded, mode, wantBIOS);
}

TEST(cart_bios_disabled)
{
    struct BootConfig c;
    resolve(&c, false, false, CDBOOT_AUTO, false);
    ASSERT_EQ(c.isCDGame, false);
    ASSERT_EQ(c.showBootROM, false);
    ASSERT(IS_CART(c));
}

TEST(cart_bios_enabled)
{
    struct BootConfig c;
    resolve(&c, false, false, CDBOOT_AUTO, true);
    ASSERT_EQ(c.isCDGame, false);
    ASSERT_EQ(c.showBootROM, true);
    ASSERT(IS_CART(c));
}

TEST(cd_hle_no_bios)
{
    struct BootConfig c;
    resolve(&c, true, false, CDBOOT_HLE, true);
    ASSERT_EQ(c.isCDGame, true);
    ASSERT_EQ(c.showBootROM, false);
    ASSERT(IS_HLE(c));
}

TEST(cd_hle_bios_available)
{
    struct BootConfig c;
    resolve(&c, true, true, CDBOOT_HLE, true);
    ASSERT_EQ(c.isCDGame, true);
    ASSERT_EQ(c.showBootROM, false);
    ASSERT(IS_HLE(c));
    ASSERT_EQ(c.cdBiosAvailable, true);
}

TEST(cd_bios_mode_with_bios)
{
    struct BootConfig c;
    resolve(&c, true, true, CDBOOT_BIOS, true);
    ASSERT_EQ(c.isCDGame, true);
    ASSERT_EQ(c.showBootROM, true);
    ASSERT(IS_BIOS(c));
    ASSERT_EQ(c.cdBiosAvailable, true);
}

TEST(cd_bios_mode_no_bios_fallback)
{
    struct BootConfig c;
    resolve(&c, true, false, CDBOOT_BIOS, true);
    ASSERT_EQ(c.isCDGame, true);
    ASSERT_EQ(c.showBootROM, false);
    ASSERT(IS_HLE(c));
    ASSERT_EQ(c.cdBiosAvailable, false);
}

TEST(cd_auto_with_bios)
{
    struct BootConfig c;
    resolve(&c, true, true, CDBOOT_AUTO, true);
    ASSERT_EQ(c.isCDGame, true);
    ASSERT_EQ(c.showBootROM, true);
    ASSERT(IS_BIOS(c));
}

TEST(cd_auto_no_bios)
{
    struct BootConfig c;
    resolve(&c, true, false, CDBOOT_AUTO, true);
    ASSERT_EQ(c.isCDGame, true);
    ASSERT_EQ(c.showBootROM, false);
    ASSERT(IS_HLE(c));
}

TEST(cd_auto_no_bios_user_bios_off)
{
    struct BootConfig c;
    resolve(&c, true, false, CDBOOT_AUTO, false);
    ASSERT_EQ(c.showBootROM, false);
    ASSERT(IS_HLE(c));
}

TEST(cd_bios_mode_user_bios_off)
{
    struct BootConfig c;
    resolve(&c, true, true, CDBOOT_BIOS, false);
    ASSERT_EQ(c.showBootROM, true);
    ASSERT(IS_BIOS(c));
}

TEST(strategy_names)
{
    ASSERT(strcmp(p_strategy_hle->name, "hle") == 0);
    ASSERT(strcmp(p_strategy_bios->name, "bios") == 0);
    ASSERT(strcmp(p_strategy_cart->name, "cart") == 0);
}

/* ------------------------------------------------------------------ */
/* Part 2: Integration tests through retro_load_game()                 */
/* ------------------------------------------------------------------ */

static const char *env_cd_boot_mode = "auto";
static const char *env_bios_enabled = "enabled";

/* BIOS directory, resolved at run time.  This used to be the literal
 * "test/roms/private", but the corpus keeps BIOS dumps in a ROMS/
 * subdirectory, so CD_BIOS_PATH never resolved and the two
 * "_with_bios" integration tests always reported SKIP -- with the suite
 * still exiting 0.  Same silent-skip class as the Skyhammer clipping
 * sentinel.  `make test` passes the real directory in via VJ_BIOS_DIR;
 * the old literal remains the fallback. */
#define BIOS_DIR_DEFAULT "test/roms/private"

static char bios_dir[512];
static char cd_bios_path[640];

static void init_bios_paths(void)
{
    const char *env = getenv("VJ_BIOS_DIR");

    snprintf(bios_dir, sizeof(bios_dir), "%s",
             (env && *env) ? env : BIOS_DIR_DEFAULT);
    snprintf(cd_bios_path, sizeof(cd_bios_path),
             "%s/[BIOS] Atari Jaguar CD (World).j64", bios_dir);
}

static const char *env_system_dir = BIOS_DIR_DEFAULT;

static void stub_video(const void *d, unsigned w, unsigned h, size_t p)
{ (void)d; (void)w; (void)h; (void)p; }
static void stub_audio(int16_t l, int16_t r) { (void)l; (void)r; }
static size_t stub_audio_batch(const int16_t *d, size_t f) { (void)d; return f; }
static void stub_input_poll(void) {}
static int16_t stub_input_state(unsigned p, unsigned d, unsigned i, unsigned id)
{ (void)p; (void)d; (void)i; (void)id; return 0; }

static bool env_callback(unsigned cmd, void *data)
{
    switch (cmd & 0xFF) {
    case RETRO_ENVIRONMENT_GET_LOG_INTERFACE:
        return false;
    case RETRO_ENVIRONMENT_SET_PIXEL_FORMAT:
        return true;
    case RETRO_ENVIRONMENT_GET_SYSTEM_DIRECTORY:
        *(const char **)data = env_system_dir;
        return true;
    case RETRO_ENVIRONMENT_GET_SAVE_DIRECTORY:
    case RETRO_ENVIRONMENT_GET_CORE_ASSETS_DIRECTORY:
        *(const char **)data = "/tmp";
        return true;
    case RETRO_ENVIRONMENT_SET_VARIABLES:
    case RETRO_ENVIRONMENT_SET_CORE_OPTIONS_V2:
        return true;
    case RETRO_ENVIRONMENT_GET_VARIABLE: {
        struct retro_variable *var = (struct retro_variable *)data;
        if (!var || !var->key) return false;
        if (strcmp(var->key, "virtualjaguar_bios") == 0)
            { var->value = env_bios_enabled; return true; }
        if (strcmp(var->key, "virtualjaguar_usefastblitter") == 0)
            { var->value = "enabled"; return true; }
        if (strcmp(var->key, "virtualjaguar_cd_bios_type") == 0)
            { var->value = "retail"; return true; }
        if (strcmp(var->key, "virtualjaguar_cd_boot_mode") == 0)
            { var->value = env_cd_boot_mode; return true; }
        var->value = NULL;
        return false;
    }
    case RETRO_ENVIRONMENT_GET_VARIABLE_UPDATE:
        *(bool *)data = false;
        return true;
    case RETRO_ENVIRONMENT_SET_SERIALIZATION_QUIRKS:
        return true;
    default:
        return false;
    }
}

static void core_init(void)
{
    p_retro_set_environment(env_callback);
    p_retro_set_video_refresh(stub_video);
    p_retro_set_audio_sample(stub_audio);
    p_retro_set_audio_sample_batch(stub_audio_batch);
    p_retro_set_input_poll(stub_input_poll);
    p_retro_set_input_state(stub_input_state);
    p_retro_init();
}

static bool core_load_disc(const char *path)
{
    struct retro_game_info info;
    memset(&info, 0, sizeof(info));
    info.path = path;
    return p_retro_load_game(&info);
}

static void core_teardown(void)
{
    if (p_retro_unload_game) p_retro_unload_game();
    p_retro_deinit();
}

static bool file_exists(const char *path)
{
    struct stat st;
    return stat(path, &st) == 0;
}

static char g_test_cue[4096] = {0};

static void find_first_cue(const char *dir)
{
    DIR *dp;
    struct dirent *de;
    dp = opendir(dir);
    if (!dp) return;
    while ((de = readdir(dp)) != NULL) {
        char path[4096];
        struct stat st;
        const char *dot;
        if (de->d_name[0] == '.') continue;
        snprintf(path, sizeof(path), "%s/%s", dir, de->d_name);
        if (stat(path, &st) != 0) continue;
        if (S_ISDIR(st.st_mode)) {
            find_first_cue(path);
            if (g_test_cue[0]) break;
            continue;
        }
        dot = strrchr(de->d_name, '.');
        if (dot && strcasecmp(dot, ".cue") == 0) {
            strncpy(g_test_cue, path, sizeof(g_test_cue) - 1);
            break;
        }
    }
    closedir(dp);
}

#define CD_BIOS_PATH cd_bios_path

TEST(integration_hle_mode)
{
    bool loaded;
    env_cd_boot_mode = "hle";
    env_bios_enabled = "enabled";
    env_system_dir = bios_dir;

    core_init();
    loaded = core_load_disc(g_test_cue);
    if (!loaded) FAIL("retro_load_game failed for %s", g_test_cue);

    fprintf(stderr, "        bootConfig: isCDGame=%d showBootROM=%d strategy=%s cdBiosAvail=%d\n",
            p_bootConfig->isCDGame, p_bootConfig->showBootROM,
            p_bootConfig->strategy ? p_bootConfig->strategy->name : "null",
            p_bootConfig->cdBiosAvailable);

    ASSERT_EQ(p_bootConfig->isCDGame, true);
    ASSERT_EQ(p_bootConfig->showBootROM, false);
    ASSERT(IS_HLE(*p_bootConfig));
    core_teardown();
}

TEST(integration_bios_mode_with_bios)
{
    bool loaded;
    env_cd_boot_mode = "bios";
    env_bios_enabled = "enabled";
    env_system_dir = bios_dir;

    core_init();
    loaded = core_load_disc(g_test_cue);
    if (!loaded) FAIL("retro_load_game failed for %s", g_test_cue);

    fprintf(stderr, "        bootConfig: isCDGame=%d showBootROM=%d strategy=%s cdBiosAvail=%d\n",
            p_bootConfig->isCDGame, p_bootConfig->showBootROM,
            p_bootConfig->strategy ? p_bootConfig->strategy->name : "null",
            p_bootConfig->cdBiosAvailable);

    ASSERT_EQ(p_bootConfig->isCDGame, true);
    ASSERT_EQ(p_bootConfig->showBootROM, true);
    ASSERT(IS_BIOS(*p_bootConfig));
    ASSERT_EQ(p_bootConfig->cdBiosAvailable, true);
    core_teardown();
}

TEST(integration_auto_mode_with_bios)
{
    bool loaded;
    env_cd_boot_mode = "auto";
    env_bios_enabled = "enabled";
    env_system_dir = bios_dir;

    core_init();
    loaded = core_load_disc(g_test_cue);
    if (!loaded) FAIL("retro_load_game failed for %s", g_test_cue);

    fprintf(stderr, "        bootConfig: isCDGame=%d showBootROM=%d strategy=%s cdBiosAvail=%d\n",
            p_bootConfig->isCDGame, p_bootConfig->showBootROM,
            p_bootConfig->strategy ? p_bootConfig->strategy->name : "null",
            p_bootConfig->cdBiosAvailable);

    ASSERT_EQ(p_bootConfig->isCDGame, true);
    ASSERT_EQ(p_bootConfig->showBootROM, true);
    ASSERT(IS_BIOS(*p_bootConfig));
    core_teardown();
}

/* No external BIOS file: the embedded CD BIOS is staged instead, so AUTO
 * still resolves to the real-BIOS strategy (zero-file real-BIOS boot). */
TEST(integration_auto_mode_no_bios)
{
    bool loaded;
    env_cd_boot_mode = "auto";
    env_bios_enabled = "enabled";
    env_system_dir = "/nonexistent";

    core_init();
    loaded = core_load_disc(g_test_cue);
    if (!loaded) FAIL("retro_load_game failed for %s", g_test_cue);

    fprintf(stderr, "        bootConfig: isCDGame=%d showBootROM=%d strategy=%s cdBiosAvail=%d\n",
            p_bootConfig->isCDGame, p_bootConfig->showBootROM,
            p_bootConfig->strategy ? p_bootConfig->strategy->name : "null",
            p_bootConfig->cdBiosAvailable);

    ASSERT_EQ(p_bootConfig->isCDGame, true);
    ASSERT_EQ(p_bootConfig->showBootROM, true);
    ASSERT(IS_BIOS(*p_bootConfig));
    ASSERT_EQ(p_bootConfig->cdBiosAvailable, true);
    core_teardown();
}

/* Same for explicit BIOS mode: no external file no longer falls back to
 * HLE — the embedded CD BIOS backs the real-BIOS path. */
TEST(integration_bios_mode_no_bios_fallback)
{
    bool loaded;
    env_cd_boot_mode = "bios";
    env_bios_enabled = "enabled";
    env_system_dir = "/nonexistent";

    core_init();
    loaded = core_load_disc(g_test_cue);
    if (!loaded) FAIL("retro_load_game failed for %s", g_test_cue);

    fprintf(stderr, "        bootConfig: isCDGame=%d showBootROM=%d strategy=%s cdBiosAvail=%d\n",
            p_bootConfig->isCDGame, p_bootConfig->showBootROM,
            p_bootConfig->strategy ? p_bootConfig->strategy->name : "null",
            p_bootConfig->cdBiosAvailable);

    ASSERT_EQ(p_bootConfig->isCDGame, true);
    ASSERT_EQ(p_bootConfig->showBootROM, true);
    ASSERT(IS_BIOS(*p_bootConfig));
    core_teardown();
}

TEST(integration_hle_bios_setting_off)
{
    bool loaded;
    env_cd_boot_mode = "hle";
    env_bios_enabled = "disabled";
    env_system_dir = bios_dir;

    core_init();
    loaded = core_load_disc(g_test_cue);
    if (!loaded) FAIL("retro_load_game failed for %s", g_test_cue);

    fprintf(stderr, "        bootConfig: isCDGame=%d showBootROM=%d strategy=%s cdBiosAvail=%d\n",
            p_bootConfig->isCDGame, p_bootConfig->showBootROM,
            p_bootConfig->strategy ? p_bootConfig->strategy->name : "null",
            p_bootConfig->cdBiosAvailable);

    ASSERT_EQ(p_bootConfig->showBootROM, false);
    ASSERT(IS_HLE(*p_bootConfig));
    core_teardown();
}

/* ------------------------------------------------------------------ */
/* Part 3: No-content boot (RETRO_ENVIRONMENT_SET_SUPPORT_NO_GAME,      */
/*          issue #646) -- retro_load_game(NULL).  Needs no ROM/disc,  */
/*          so these always run (never gated on the private corpus).   */
/* ------------------------------------------------------------------ */

static bool g_saw_no_game_env = false;

static bool env_callback_no_game(unsigned cmd, void *data)
{
    if ((cmd & 0xFF) == RETRO_ENVIRONMENT_SET_SUPPORT_NO_GAME)
    {
        g_saw_no_game_env = *(const bool *)data;
        return true;
    }
    return env_callback(cmd, data);
}

static void core_init_no_game(void)
{
    g_saw_no_game_env = false;
    p_retro_set_environment(env_callback_no_game);
    p_retro_set_video_refresh(stub_video);
    p_retro_set_audio_sample(stub_audio);
    p_retro_set_audio_sample_batch(stub_audio_batch);
    p_retro_set_input_poll(stub_input_poll);
    p_retro_set_input_state(stub_input_state);
    p_retro_init();
}

TEST(no_game_env_declared)
{
    core_init_no_game();
    ASSERT_EQ(g_saw_no_game_env, true);
    p_retro_deinit();
}

/* Renamed from no_game_boot_selects_none_strategy (#726).  It asserted
 * IS_NONE, which pinned the bare-console boot that made the feature
 * useless: cd_boot_strategy_none runs the boot ROM against a zeroed cart
 * window -- the empty-slot logo -- and the CD BIOS is mapped as a
 * cartridge, so nothing reachable from that screen could ever get to it.
 * No-content boot exists to be a place you insert a disc FROM, so the
 * BIOS is the correct resolution and this now pins that instead. */
TEST(no_game_boot_selects_cd_bios_strategy)
{
    bool loaded;

    core_init_no_game();
    loaded = p_retro_load_game(NULL);
    if (!loaded) FAIL("retro_load_game(NULL) failed");

    fprintf(stderr, "        bootConfig: isCDGame=%d showBootROM=%d strategy=%s\n",
            p_bootConfig->isCDGame, p_bootConfig->showBootROM,
            p_bootConfig->strategy ? p_bootConfig->strategy->name : "null");

    /* isCDGame stays false: the BIOS is up, but no disc is mounted.  That
     * pairing (bios + !isCDGame) is what the disk-control insert test uses
     * to tell "bare BIOS" from "disc mounted". */
    ASSERT_EQ(p_bootConfig->isCDGame, false);
    ASSERT_EQ(p_bootConfig->showBootROM, true);
    ASSERT_EQ(p_bootConfig->cdBiosAvailable, true);
    ASSERT(IS_BIOS(*p_bootConfig));
    ASSERT(!IS_NONE(*p_bootConfig));
    core_teardown();
}

TEST(no_game_save_ram_is_empty)
{
    bool loaded;

    core_init_no_game();
    loaded = p_retro_load_game(NULL);
    if (!loaded) FAIL("retro_load_game(NULL) failed");

    /* No cartridge -> no EEPROM chip.  A frontend must not be handed a
     * meaningless .srm for a bare-BIOS session. */
    ASSERT_EQ(p_retro_get_memory_size(RETRO_MEMORY_SAVE_RAM), 0);
    ASSERT(p_retro_get_memory_data(RETRO_MEMORY_SAVE_RAM) == NULL);
    core_teardown();
}

TEST(no_game_runs_and_resets_without_crashing)
{
    bool loaded;
    int i;

    core_init_no_game();
    loaded = p_retro_load_game(NULL);
    if (!loaded) FAIL("retro_load_game(NULL) failed");

    /* Real hardware boots the ROM (cube animation) and then sits with an
     * empty cart slot; run long enough to clear the animation and settle,
     * then reset and do it again.  A crash here is the whole point of the
     * test -- there is no assertion beyond "the process is still alive". */
    for (i = 0; i < 120; i++)
        p_retro_run();
    if (p_retro_reset)
        p_retro_reset();
    for (i = 0; i < 60; i++)
        p_retro_run();

    core_teardown();
}

/* ------------------------------------------------------------------ */
/* Main                                                                */
/* ------------------------------------------------------------------ */

int main(int argc, char *argv[])
{
    int total_fail;
    bool have_cue;
    bool have_cd_bios;
    (void)argc; (void)argv;
    total_fail = 0;

    init_bios_paths();
    fprintf(stderr, "  [INFO] BIOS dir:  %s\n", bios_dir);

    if (!load_core()) return 1;

    /* ---- Part 1: Unit tests (no disc needed) ---- */
    SUITE("BootConfig Resolver (unit)");
    RUN(cart_bios_disabled);
    RUN(cart_bios_enabled);
    RUN(cd_hle_no_bios);
    RUN(cd_hle_bios_available);
    RUN(cd_bios_mode_with_bios);
    RUN(cd_bios_mode_no_bios_fallback);
    RUN(cd_auto_with_bios);
    RUN(cd_auto_no_bios);
    RUN(cd_auto_no_bios_user_bios_off);
    RUN(cd_bios_mode_user_bios_off);
    RUN(strategy_names);
    total_fail += REPORT();

    /* ---- Part 2: Integration tests (need a disc image) ---- */
    SUITE("BootConfig Integration (retro_load_game)");

    find_first_cue("test/roms/private");
    if (!g_test_cue[0])
        find_first_cue("test/roms");

    have_cue = g_test_cue[0] != '\0';
    have_cd_bios = file_exists(CD_BIOS_PATH);

    fprintf(stderr, "  [INFO] Test disc: %s\n", have_cue ? g_test_cue : "NOT FOUND");
    fprintf(stderr, "  [INFO] CD BIOS:   %s\n", have_cd_bios ? "found" : "NOT FOUND");

    if (!have_cue) {
        SKIP(integration_hle_mode, "no disc image");
        SKIP(integration_bios_mode_with_bios, "no disc image");
        SKIP(integration_auto_mode_with_bios, "no disc image");
        SKIP(integration_auto_mode_no_bios, "no disc image");
        SKIP(integration_bios_mode_no_bios_fallback, "no disc image");
        SKIP(integration_hle_bios_setting_off, "no disc image");
    } else {
        RUN(integration_hle_mode);
        RUN(integration_auto_mode_no_bios);
        RUN(integration_bios_mode_no_bios_fallback);
        RUN(integration_hle_bios_setting_off);

        if (have_cd_bios) {
            RUN(integration_bios_mode_with_bios);
            RUN(integration_auto_mode_with_bios);
        } else {
            SKIP(integration_bios_mode_with_bios, "no CD BIOS file");
            SKIP(integration_auto_mode_with_bios, "no CD BIOS file");
        }
    }
    total_fail += REPORT();

    /* ---- Part 3: No-content boot (never gated -- needs no ROM) ---- */
    SUITE("No-Content Boot (RETRO_ENVIRONMENT_SET_SUPPORT_NO_GAME)");
    RUN(no_game_env_declared);
    RUN(no_game_boot_selects_cd_bios_strategy);
    RUN(no_game_save_ram_is_empty);
    RUN(no_game_runs_and_resets_without_crashing);
    total_fail += REPORT();

    if (g_handle) dlclose(g_handle);
    return total_fail;
}
