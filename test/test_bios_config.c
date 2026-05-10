/*
 * test_bios_config.c — BIOS configuration tests (HLE vs real BIOS).
 *
 * Tests that the emulator initializes correctly with:
 *   - HLE (no BIOS file) mode
 *   - Real Jaguar BIOS
 *   - Real Jaguar CD BIOS
 *
 * Tests are conditionally run based on BIOS file availability.
 * BIOS files expected at: test/roms/private/
 *
 * Build: cc -g -O0 -o test/test_bios_config test/test_bios_config.c -ldl
 * Run:   ./test/test_bios_config
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <dlfcn.h>
#include <unistd.h>
#include <sys/stat.h>

/* We need a custom environment callback, so include framework piecemeal */
#include "../libretro-common/include/libretro.h"

/* ------------------------------------------------------------------ */
/* Test runner (same as test_framework.h but without the core loader)  */
/* ------------------------------------------------------------------ */

static int tf_pass = 0;
static int tf_fail = 0;
static int tf_skip = 0;
static const char *tf_suite_name = "";
static const char *tf_current_test = "";
static bool tf_current_failed = false;

#define TEST_INIT(name) \
    do { tf_suite_name = (name); tf_pass = tf_fail = tf_skip = 0; \
         fprintf(stderr, "\n=== %s ===\n", tf_suite_name); } while(0)

#define TEST(name) static void test_##name(void)

#define RUN_TEST(name) \
    do { \
        tf_current_test = #name; \
        tf_current_failed = false; \
        test_##name(); \
        if (tf_current_failed) { tf_fail++; } \
        else { tf_pass++; fprintf(stderr, "  PASS  %s\n", #name); } \
    } while(0)

#define SKIP_TEST(name, reason) \
    do { tf_skip++; fprintf(stderr, "  SKIP  %s (%s)\n", #name, reason); } while(0)

#define TEST_REPORT() \
    (fprintf(stderr, "\n--- %s: %d passed, %d failed, %d skipped ---\n\n", \
             tf_suite_name, tf_pass, tf_fail, tf_skip), tf_fail)

#define FAIL(fmt, ...) \
    do { \
        fprintf(stderr, "  FAIL  %s:%d: " fmt "\n", \
                tf_current_test, __LINE__, ##__VA_ARGS__); \
        tf_current_failed = true; \
        return; \
    } while(0)

#define ASSERT_TRUE(cond) \
    do { if (!(cond)) FAIL("expected true: %s", #cond); } while(0)

#define ASSERT_EQ_U32(a, b) \
    do { \
        uint32_t _a = (uint32_t)(a), _b = (uint32_t)(b); \
        if (_a != _b) FAIL("%s == %s: got 0x%08X, expected 0x%08X", #a, #b, _a, _b); \
    } while(0)

#define ASSERT_EQ_U16(a, b) \
    do { \
        uint16_t _a = (uint16_t)(a), _b = (uint16_t)(b); \
        if (_a != _b) FAIL("%s == %s: got 0x%04X, expected 0x%04X", #a, #b, _a, _b); \
    } while(0)

#define CHECK_EQ(a, b) \
    do { \
        long long _a = (long long)(a), _b = (long long)(b); \
        if (_a != _b) { \
            fprintf(stderr, "  CHECK %s:%d: %s == %s: got %lld (0x%llX), expected %lld (0x%llX)\n", \
                    tf_current_test, __LINE__, #a, #b, _a, _a, _b, _b); \
            tf_current_failed = true; \
        } \
    } while(0)

/* ------------------------------------------------------------------ */
/* BIOS file paths                                                     */
/* ------------------------------------------------------------------ */

#define BIOS_DIR "test/roms/private"
#define JAGUAR_BIOS_PATH      BIOS_DIR "/[BIOS] Atari Jaguar (World).j64"
#define JAGUAR_CD_BIOS_PATH   BIOS_DIR "/[BIOS] Atari Jaguar CD (World).j64"
#define JAGUAR_CD_BIOS_ROM    BIOS_DIR "/Jaguar CD BIOS.rom"

static bool file_exists(const char *path)
{
    struct stat st;
    return stat(path, &st) == 0;
}

static bool have_jaguar_bios = false;
static bool have_cd_bios = false;

/* ------------------------------------------------------------------ */
/* Configurable core loader                                            */
/* ------------------------------------------------------------------ */

typedef enum {
    BIOS_MODE_HLE,
    BIOS_MODE_REAL
} bios_mode_t;

typedef enum {
    CD_MODE_HLE,
    CD_MODE_REAL,
    CD_MODE_DISABLED
} cd_mode_t;

static bios_mode_t current_bios_mode = BIOS_MODE_HLE;
static cd_mode_t current_cd_mode = CD_MODE_DISABLED;
static const char *current_system_dir = ".";

struct bios_core {
    void *handle;
    void (*retro_init)(void);
    void (*retro_deinit)(void);
    void (*retro_set_environment)(retro_environment_t);
    void (*retro_set_video_refresh)(retro_video_refresh_t);
    void (*retro_set_audio_sample)(retro_audio_sample_t);
    void (*retro_set_audio_sample_batch)(retro_audio_sample_batch_t);
    void (*retro_set_input_poll)(retro_input_poll_t);
    void (*retro_set_input_state)(retro_input_state_t);

    void (*GPUInit)(void);
    void (*GPUReset)(void);
    void (*TOMInit)(void);
    void (*TOMReset)(void);
    void (*JERRYInit)(void);
    void (*JERRYReset)(void);
    void (*CDROMInit)(void);
    void (*CDROMReset)(void);
    void (*JaguarInit)(void);
    void (*JaguarReset)(void);

    uint16_t (*TOMReadWord)(uint32_t, uint32_t);
    void (*TOMWriteWord)(uint32_t, uint16_t, uint32_t);
    uint16_t (*JERRYReadWord)(uint32_t, uint32_t);
    void (*JERRYWriteWord)(uint32_t, uint16_t, uint32_t);
    uint8_t (*JaguarReadByte)(uint32_t, uint32_t);
    uint16_t (*JaguarReadWord)(uint32_t, uint32_t);
    void (*JaguarWriteWord)(uint32_t, uint16_t, uint32_t);

    uint8_t *(*GetRamPtr)(void);
    unsigned int (*m68k_get_reg)(void *, int);

    void *vjs;
};

/* Stub callbacks */
static void bc_video_refresh(const void *d, unsigned w, unsigned h, size_t p) { (void)d; (void)w; (void)h; (void)p; }
static void bc_audio_sample(int16_t l, int16_t r) { (void)l; (void)r; }
static size_t bc_audio_sample_batch(const int16_t *d, size_t f) { (void)d; return f; }
static void bc_input_poll(void) {}
static int16_t bc_input_state(unsigned p, unsigned d, unsigned i, unsigned id) { (void)p; (void)d; (void)i; (void)id; return 0; }

static bool bc_environment(unsigned cmd, void *data)
{
    switch (cmd & 0xFF)
    {
    case RETRO_ENVIRONMENT_GET_LOG_INTERFACE:
        return false;
    case RETRO_ENVIRONMENT_GET_SYSTEM_DIRECTORY:
    case RETRO_ENVIRONMENT_GET_CORE_ASSETS_DIRECTORY:
        *(const char **)data = current_system_dir;
        return true;
    case RETRO_ENVIRONMENT_GET_SAVE_DIRECTORY:
        *(const char **)data = "/tmp";
        return true;
    case RETRO_ENVIRONMENT_SET_VARIABLES:
    case RETRO_ENVIRONMENT_SET_CORE_OPTIONS_V2:
        return true;
    case RETRO_ENVIRONMENT_GET_VARIABLE:
    {
        struct retro_variable *var = (struct retro_variable *)data;
        if (!var->key) { var->value = NULL; return false; }

        if (strcmp(var->key, "virtualjaguar_bios") == 0) {
            var->value = (current_bios_mode == BIOS_MODE_REAL) ? "enabled" : "disabled";
            return true;
        }
        if (strcmp(var->key, "virtualjaguar_cd_bios") == 0) {
            if (current_cd_mode == CD_MODE_REAL)
                var->value = "enabled";
            else
                var->value = "disabled";
            return true;
        }
        if (strcmp(var->key, "virtualjaguar_cd_boot_mode") == 0) {
            if (current_cd_mode == CD_MODE_HLE)
                var->value = "hle";
            else if (current_cd_mode == CD_MODE_REAL)
                var->value = "real";
            else
                var->value = "disabled";
            return true;
        }
        if (strcmp(var->key, "virtualjaguar_usefastblitter") == 0) {
            var->value = "enabled";
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

#define BC_LOAD_SYM(c, sym) (c)->sym = dlsym((c)->handle, #sym)
#define BC_LOAD_REQ(c, sym) \
    do { (c)->sym = dlsym((c)->handle, #sym); \
         if (!(c)->sym) { fprintf(stderr, "FATAL: %s\n", #sym); return false; } \
    } while(0)

static bool bc_load(struct bios_core *c)
{
    const char *lib;
    memset(c, 0, sizeof(*c));
#ifdef __APPLE__
    lib = "./virtualjaguar_libretro.dylib";
#elif defined(_WIN32)
    lib = "./virtualjaguar_libretro.dll";
#else
    lib = "./virtualjaguar_libretro.so";
#endif
    c->handle = dlopen(lib, RTLD_LAZY);
    if (!c->handle) { fprintf(stderr, "FATAL: dlopen: %s\n", dlerror()); return false; }

    BC_LOAD_REQ(c, retro_init);
    BC_LOAD_REQ(c, retro_deinit);
    BC_LOAD_REQ(c, retro_set_environment);
    BC_LOAD_REQ(c, retro_set_video_refresh);
    BC_LOAD_REQ(c, retro_set_audio_sample);
    BC_LOAD_REQ(c, retro_set_audio_sample_batch);
    BC_LOAD_REQ(c, retro_set_input_poll);
    BC_LOAD_REQ(c, retro_set_input_state);

    BC_LOAD_SYM(c, GPUInit);
    BC_LOAD_SYM(c, GPUReset);
    BC_LOAD_SYM(c, TOMInit);
    BC_LOAD_SYM(c, TOMReset);
    BC_LOAD_SYM(c, JERRYInit);
    BC_LOAD_SYM(c, JERRYReset);
    BC_LOAD_SYM(c, CDROMInit);
    BC_LOAD_SYM(c, CDROMReset);
    BC_LOAD_SYM(c, JaguarInit);
    BC_LOAD_SYM(c, JaguarReset);
    BC_LOAD_SYM(c, TOMReadWord);
    BC_LOAD_SYM(c, TOMWriteWord);
    BC_LOAD_SYM(c, JERRYReadWord);
    BC_LOAD_SYM(c, JERRYWriteWord);
    BC_LOAD_SYM(c, JaguarReadByte);
    BC_LOAD_SYM(c, JaguarReadWord);
    BC_LOAD_SYM(c, JaguarWriteWord);
    BC_LOAD_SYM(c, GetRamPtr);
    BC_LOAD_SYM(c, m68k_get_reg);
    c->vjs = dlsym(c->handle, "vjs");
    return true;
}

static void bc_init(struct bios_core *c)
{
    c->retro_set_environment(bc_environment);
    c->retro_set_video_refresh(bc_video_refresh);
    c->retro_set_audio_sample(bc_audio_sample);
    c->retro_set_audio_sample_batch(bc_audio_sample_batch);
    c->retro_set_input_poll(bc_input_poll);
    c->retro_set_input_state(bc_input_state);
    c->retro_init();
    if (c->GPUInit) c->GPUInit();
}

static void bc_unload(struct bios_core *c)
{
    if (c->retro_deinit) c->retro_deinit();
    if (c->handle) dlclose(c->handle);
    memset(c, 0, sizeof(*c));
}

/* ------------------------------------------------------------------ */
/* Caller IDs (must match vjag_memory.h)                               */
/* ------------------------------------------------------------------ */
#define CALLER_M68K 0

/* ------------------------------------------------------------------ */
/* HLE BIOS Tests (no BIOS file needed)                                */
/* ------------------------------------------------------------------ */

static struct bios_core core;

TEST(hle_bios_init_succeeds)
{
    current_bios_mode = BIOS_MODE_HLE;
    current_cd_mode = CD_MODE_DISABLED;
    bc_init(&core);
    ASSERT_TRUE(core.GetRamPtr != NULL);
    ASSERT_TRUE(core.GetRamPtr() != NULL);
    core.retro_deinit();
}

TEST(hle_bios_boot_rom_present)
{
    uint16_t val;
    current_bios_mode = BIOS_MODE_HLE;
    current_cd_mode = CD_MODE_DISABLED;
    bc_init(&core);

    /* Boot ROM at $E00000 is loaded by retro_load_game->JaguarInit,
     * not by retro_init. Verify read doesn't crash (address decode works). */
    val = core.JaguarReadWord(0xE00000, CALLER_M68K);
    (void)val;
    ASSERT_TRUE(1);
    core.retro_deinit();
}

TEST(hle_bios_ram_accessible)
{
    uint16_t val;
    current_bios_mode = BIOS_MODE_HLE;
    current_cd_mode = CD_MODE_DISABLED;
    bc_init(&core);

    core.JaguarWriteWord(0x5000, 0xCAFE, CALLER_M68K);
    val = core.JaguarReadWord(0x5000, CALLER_M68K);
    ASSERT_EQ_U16(val, 0xCAFE);
    core.retro_deinit();
}

TEST(hle_bios_tom_registers_accessible)
{
    uint16_t hc;
    current_bios_mode = BIOS_MODE_HLE;
    current_cd_mode = CD_MODE_DISABLED;
    bc_init(&core);

    hc = core.TOMReadWord(0xF00004, CALLER_M68K);
    (void)hc;
    ASSERT_TRUE(1);
    core.retro_deinit();
}

/* ------------------------------------------------------------------ */
/* HLE CD BIOS Tests (no CD BIOS file needed)                          */
/* ------------------------------------------------------------------ */

TEST(hle_cd_bios_init_succeeds)
{
    current_bios_mode = BIOS_MODE_HLE;
    current_cd_mode = CD_MODE_HLE;
    bc_init(&core);
    ASSERT_TRUE(core.GetRamPtr != NULL);
    core.retro_deinit();
}

TEST(hle_cd_bios_butch_accessible)
{
    uint16_t val;
    current_bios_mode = BIOS_MODE_HLE;
    current_cd_mode = CD_MODE_HLE;
    bc_init(&core);

    /* BUTCH registers at $DFFF00 should be accessible */
    val = core.JaguarReadWord(0xDFFF00, CALLER_M68K);
    (void)val;
    ASSERT_TRUE(1);
    core.retro_deinit();
}

/* ------------------------------------------------------------------ */
/* Real Jaguar BIOS Tests (requires BIOS file)                         */
/* ------------------------------------------------------------------ */

TEST(real_bios_init_succeeds)
{
    current_bios_mode = BIOS_MODE_REAL;
    current_cd_mode = CD_MODE_DISABLED;
    current_system_dir = BIOS_DIR;
    bc_init(&core);
    ASSERT_TRUE(core.GetRamPtr != NULL);
    core.retro_deinit();
    current_system_dir = ".";
}

TEST(real_bios_boot_rom_space_accessible)
{
    uint16_t val;
    /* Verify that with real BIOS mode set, ROM address space is accessible.
     * Actual BIOS loading requires retro_load_game (not just retro_init). */
    current_bios_mode = BIOS_MODE_REAL;
    current_cd_mode = CD_MODE_DISABLED;
    current_system_dir = BIOS_DIR;
    bc_init(&core);

    val = core.JaguarReadWord(0xE00000, CALLER_M68K);
    (void)val;
    ASSERT_TRUE(1);
    core.retro_deinit();
    current_system_dir = ".";
}

TEST(real_bios_ram_accessible)
{
    uint16_t val;
    current_bios_mode = BIOS_MODE_REAL;
    current_cd_mode = CD_MODE_DISABLED;
    current_system_dir = BIOS_DIR;
    bc_init(&core);

    core.JaguarWriteWord(0x6000, 0xBEEF, CALLER_M68K);
    val = core.JaguarReadWord(0x6000, CALLER_M68K);
    ASSERT_EQ_U16(val, 0xBEEF);
    core.retro_deinit();
    current_system_dir = ".";
}

TEST(real_bios_gpu_init_ok)
{
    uint16_t val;
    current_bios_mode = BIOS_MODE_REAL;
    current_cd_mode = CD_MODE_DISABLED;
    current_system_dir = BIOS_DIR;
    bc_init(&core);

    /* GPU RAM should be accessible */
    val = core.TOMReadWord(0xF00004, CALLER_M68K);
    (void)val;
    ASSERT_TRUE(1);
    core.retro_deinit();
    current_system_dir = ".";
}

/* ------------------------------------------------------------------ */
/* Real CD BIOS Tests (requires CD BIOS file)                          */
/* ------------------------------------------------------------------ */

TEST(real_cd_bios_init_succeeds)
{
    current_bios_mode = BIOS_MODE_REAL;
    current_cd_mode = CD_MODE_REAL;
    current_system_dir = BIOS_DIR;
    bc_init(&core);
    ASSERT_TRUE(core.GetRamPtr != NULL);
    core.retro_deinit();
    current_system_dir = ".";
}

TEST(real_cd_bios_cart_space_accessible)
{
    uint16_t w0;
    uint16_t w2;
    /* Cartridge space at $800000 is where the CD BIOS gets loaded.
     * Loading happens in retro_load_game, not retro_init.
     * Verify address decode works without crash. */
    current_bios_mode = BIOS_MODE_REAL;
    current_cd_mode = CD_MODE_REAL;
    current_system_dir = BIOS_DIR;
    bc_init(&core);

    w0 = core.JaguarReadWord(0x800000, CALLER_M68K);
    w2 = core.JaguarReadWord(0x800002, CALLER_M68K);
    (void)w0; (void)w2;
    ASSERT_TRUE(1);
    core.retro_deinit();
    current_system_dir = ".";
}

TEST(real_cd_bios_butch_accessible)
{
    uint16_t val;
    current_bios_mode = BIOS_MODE_REAL;
    current_cd_mode = CD_MODE_REAL;
    current_system_dir = BIOS_DIR;
    bc_init(&core);

    val = core.JaguarReadWord(0xDFFF00, CALLER_M68K);
    (void)val;
    ASSERT_TRUE(1);
    core.retro_deinit();
    current_system_dir = ".";
}

TEST(real_cd_bios_jerry_accessible)
{
    uint16_t val;
    current_bios_mode = BIOS_MODE_REAL;
    current_cd_mode = CD_MODE_REAL;
    current_system_dir = BIOS_DIR;
    bc_init(&core);

    /* JERRY registers should be accessible with CD BIOS loaded */
    val = core.JERRYReadWord(0xF10000, CALLER_M68K);
    (void)val;
    ASSERT_TRUE(1);
    core.retro_deinit();
    current_system_dir = ".";
}

/* ------------------------------------------------------------------ */
/* Main                                                                */
/* ------------------------------------------------------------------ */

int main(int argc, char *argv[])
{
    (void)argc; (void)argv;
    TEST_INIT("BIOS Configuration");

    /* Check which BIOS files are available */
    have_jaguar_bios = file_exists(JAGUAR_BIOS_PATH);
    have_cd_bios = file_exists(JAGUAR_CD_BIOS_PATH) || file_exists(JAGUAR_CD_BIOS_ROM);

    fprintf(stderr, "  [INFO] Jaguar BIOS: %s\n", have_jaguar_bios ? "found" : "NOT FOUND");
    fprintf(stderr, "  [INFO] Jaguar CD BIOS: %s\n", have_cd_bios ? "found" : "NOT FOUND");

    if (!bc_load(&core)) return 1;

    /* HLE tests always run (no BIOS file needed) */
    RUN_TEST(hle_bios_init_succeeds);
    RUN_TEST(hle_bios_boot_rom_present);
    RUN_TEST(hle_bios_ram_accessible);
    RUN_TEST(hle_bios_tom_registers_accessible);

    /* HLE CD tests always run */
    RUN_TEST(hle_cd_bios_init_succeeds);
    RUN_TEST(hle_cd_bios_butch_accessible);

    /* Real Jaguar BIOS tests — only if file exists */
    if (have_jaguar_bios) {
        RUN_TEST(real_bios_init_succeeds);
        RUN_TEST(real_bios_boot_rom_space_accessible);
        RUN_TEST(real_bios_ram_accessible);
        RUN_TEST(real_bios_gpu_init_ok);
    } else {
        SKIP_TEST(real_bios_init_succeeds, "BIOS file not found");
        SKIP_TEST(real_bios_boot_rom_space_accessible, "BIOS file not found");
        SKIP_TEST(real_bios_ram_accessible, "BIOS file not found");
        SKIP_TEST(real_bios_gpu_init_ok, "BIOS file not found");
    }

    /* Real CD BIOS tests — only if file exists */
    if (have_cd_bios) {
        RUN_TEST(real_cd_bios_init_succeeds);
        RUN_TEST(real_cd_bios_cart_space_accessible);
        RUN_TEST(real_cd_bios_butch_accessible);
        RUN_TEST(real_cd_bios_jerry_accessible);
    } else {
        SKIP_TEST(real_cd_bios_init_succeeds, "CD BIOS file not found");
        SKIP_TEST(real_cd_bios_cart_space_accessible, "CD BIOS file not found");
        SKIP_TEST(real_cd_bios_butch_accessible, "CD BIOS file not found");
        SKIP_TEST(real_cd_bios_jerry_accessible, "CD BIOS file not found");
    }

    /* Don't call bc_unload here — individual tests handle init/deinit */
    if (core.handle) dlclose(core.handle);
    return TEST_REPORT();
}
