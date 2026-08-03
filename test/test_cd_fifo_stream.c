/*
 * test_cd_fifo_stream.c -- CD FIFO stream delivery contract test.
 *
 * Closes the gap left by test_cd_toc_contract.c: that test proves the
 * injected table matches CDIntf, but NOT that a seek delivers the right disc
 * BYTES to RAM.  This test does the end-to-end byte-compare:
 *
 *   1. Boot a disc in real-BIOS mode and run until the boot stub's first
 *      CD_read seek has streamed (the seek target LBA is read back via
 *      CDROMDiagGetFirstSeekBlock()).
 *   2. Independently reconstruct, from the disc image alone, what the CD
 *      BIOS GPU ISR must have accepted: scan forward from the seek LBA in
 *      I2S-unswapped bytes for the sync mark -- a run of >= 16 identical
 *      4-byte sentinel groups (the ISR at $F03248 counts 16 consecutive
 *      FIFO longwords equal to the D1 sentinel before starting the DMA).
 *   3. The 64 bytes FOLLOWING that run are the start of the game-code
 *      payload.  Assert they are present, contiguously, somewhere in main
 *      RAM.
 *
 * On the pre-fix core this is RED for every wall-stuck title: the FIFO
 * longword grouping started at sector byte 0 (even word phase), while
 * Jaguar CD discs are mastered with the sync mark at byte offset 2 (mod 4)
 * -- so the sentinel longword never assembled, the GPU ISR scanned the
 * disc forever, and the payload never reached RAM (the "streaming wall").
 * Verified on Primal Rage (sync DDL9=$44444C39 at LBA 117224 byte 42) and
 * Baldies (CINE=$43494E45 at LBA 20958 byte 46).
 *
 * SKIPs cleanly when no disc / CD BIOS is available (commercial images are
 * gitignored and absent in CI), like the other test_cd_* harnesses.
 *
 * Build:
 *   make -j8 TEST_EXPORTS=1 && cc -O0 -g -Wno-incompatible-pointer-types \
 *       -o test/test_cd_fifo_stream test/test_cd_fifo_stream.c -ldl
 * Run (local, needs disc + CD BIOS under test/roms/private):
 *   DYLD_LIBRARY_PATH=. \
 *       VJ_FIFO_DISC="test/roms/private/Primal Rage (USA)/Primal Rage (USA).cue" \
 *       test/test_cd_fifo_stream
 */

#include "test_framework.h"
#include "../libretro-common/include/libretro.h"

#include <dlfcn.h>

static struct vj_core C;
static const char *g_system_dir = "test/roms/private";

/* ---- libretro environment: force real-BIOS CD boot mode ---------------- */
static bool fifo_environment(unsigned cmd, void *data)
{
    switch (cmd & 0xFF) {
    case RETRO_ENVIRONMENT_GET_LOG_INTERFACE:
        return false;
    case RETRO_ENVIRONMENT_SET_PIXEL_FORMAT:
        return true;
    case RETRO_ENVIRONMENT_GET_SYSTEM_DIRECTORY: {
        const char *root = getenv("VJ_TEST_CD_ROOT");
        *(const char **)data = (root && root[0]) ? root : g_system_dir;
        return true;
    }
    case RETRO_ENVIRONMENT_GET_SAVE_DIRECTORY:
    case RETRO_ENVIRONMENT_GET_CORE_ASSETS_DIRECTORY:
        *(const char **)data = ".";
        return true;
    case RETRO_ENVIRONMENT_SET_VARIABLES:
    case RETRO_ENVIRONMENT_SET_CORE_OPTIONS_V2:
        return true;
    case RETRO_ENVIRONMENT_GET_VARIABLE: {
        struct retro_variable *var = (struct retro_variable *)data;
        if (!var || !var->key) return false;
        if (strcmp(var->key, "virtualjaguar_bios") == 0)           { var->value = "enabled"; return true; }
        if (strcmp(var->key, "virtualjaguar_usefastblitter") == 0) { var->value = "enabled"; return true; }
        if (strcmp(var->key, "virtualjaguar_cd_bios_type") == 0)   { var->value = "retail"; return true; }
        if (strcmp(var->key, "virtualjaguar_cd_boot_mode") == 0)   { var->value = "bios"; return true; }
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

static void fifo_video(const void *d, unsigned w, unsigned h, size_t p)
{ (void)d; (void)w; (void)h; (void)p; }
static void fifo_audio(int16_t l, int16_t r) { (void)l; (void)r; }
static size_t fifo_audio_batch(const int16_t *d, size_t f) { (void)d; return f; }
static void fifo_input_poll(void) {}
static int16_t fifo_input_state(unsigned p, unsigned d, unsigned i, unsigned id)
{ (void)p; (void)d; (void)i; (void)id; return 0; }

/* ---- exported internals (TEST_EXPORTS builds) --------------------------- */
static int      (*p_CDIntfReadBlock)(uint32_t, uint8_t *);
static uint32_t (*p_CDROMDiagGetFirstSeekBlock)(void);
static uint32_t (*p_GPUReadLong)(uint32_t, uint32_t);

#define SYNC_MIN_GROUPS 16u   /* the GPU ISR's consecutive-match requirement */
#define SCAN_SECTORS    64u   /* boot seeks land ~10 sectors before the mark */
#define PAYLOAD_LEN     64u

/* Retail CD BIOS GPU ISR state block (uploaded to GPU RAM by the 68K
 * CD_read at $303C): +0 dest-4, +4 end, +8 flag, +12 sentinel-match
 * counter, +16 the D1 sync sentinel the ISR scans the FIFO stream for. */
#define GPU_STATE_BLOCK   0x00F03118u
#define GPU_STATE_SENTINEL (GPU_STATE_BLOCK + 16u)

/* Scan forward from `startLBA` (in I2S-unswapped bytes) for the first run of
 * >= SYNC_MIN_GROUPS consecutive occurrences of the 4-byte `sentinel` --
 * exactly the acceptance rule of the CD BIOS GPU ISR.  On success writes the
 * PAYLOAD_LEN bytes that follow the run into `payload` and returns 1;
 * returns 0 if none found. */
static int find_sync_payload(uint32_t startLBA, uint32_t sentinel,
                             uint8_t *payload)
{
    /* Rolling window of 3 sectors so runs/payloads can cross boundaries. */
    static uint8_t buf[2352 * 3];
    uint8_t pat[4];
    uint32_t s, i, j;

    pat[0] = (uint8_t)(sentinel >> 24);
    pat[1] = (uint8_t)(sentinel >> 16);
    pat[2] = (uint8_t)(sentinel >> 8);
    pat[3] = (uint8_t)sentinel;

    for (s = 0; s < SCAN_SECTORS; s++) {
        uint32_t k, run, runEnd;
        for (k = 0; k < 3; k++) {
            if (!p_CDIntfReadBlock(startLBA + s + k, buf + k * 2352))
                memset(buf + k * 2352, 0, 2352);
        }
        /* I2S un-swap (byte swap within each 16-bit word). */
        for (i = 0; i + 1 < sizeof(buf); i += 2) {
            uint8_t t = buf[i];
            buf[i] = buf[i + 1];
            buf[i + 1] = t;
        }
        /* Look for the run starting within the FIRST sector of the window
         * (later starts are found by later iterations). */
        for (i = 0; i + 4 <= 2352; i += 2) {
            if (memcmp(buf + i, pat, 4) != 0)
                continue;
            run = 0;
            j = i;
            while (j + 4 <= sizeof(buf) && memcmp(buf + j, pat, 4) == 0) {
                run++;
                j += 4;
            }
            if (run >= SYNC_MIN_GROUPS) {
                runEnd = j;
                if (runEnd + PAYLOAD_LEN > sizeof(buf))
                    return 0;   /* payload past window -- not expected */
                memcpy(payload, buf + runEnd, PAYLOAD_LEN);
                fprintf(stderr,
                        "    sync mark %02X%02X%02X%02X x%u at LBA %u byte %u "
                        "(payload from byte %u)\n",
                        pat[0], pat[1], pat[2], pat[3], run,
                        startLBA + s, i, runEnd);
                return 1;
            }
            i = (j - 4) & ~1u;  /* resume after the (too-short) run */
        }
    }
    return 0;
}

/* Search main RAM for `needle` (PAYLOAD_LEN bytes). Returns address or -1. */
static long ram_find(const uint8_t *ram, const uint8_t *needle)
{
    uint32_t a;
    for (a = 0x1000; a + PAYLOAD_LEN <= 0x200000; a += 2) {
        if (ram[a] == needle[0] && memcmp(ram + a, needle, PAYLOAD_LEN) == 0)
            return (long)a;
    }
    return -1;
}

static void test_fifo_stream_delivery(void)
{
    const char *disc = getenv("VJ_FIFO_DISC");
    struct retro_game_info info;
    bool (*p_retro_load_game)(const struct retro_game_info *);
    void (*p_retro_run)(void);
    void (*p_retro_unload_game)(void);
    uint8_t *ram;
    uint8_t payload[PAYLOAD_LEN];
    uint32_t f, frames, seekLBA;
    int have_payload;
    long hit;

    if (!disc || !disc[0]) {
        SKIP_TEST(fifo_stream_delivery, "set VJ_FIFO_DISC to a CD image path");
        return;
    }

    p_retro_load_game   = dlsym(C.handle, "retro_load_game");
    p_retro_run         = dlsym(C.handle, "retro_run");
    p_retro_unload_game = dlsym(C.handle, "retro_unload_game");
    p_CDIntfReadBlock            = dlsym(C.handle, "CDIntfReadBlock");
    p_CDROMDiagGetFirstSeekBlock = dlsym(C.handle, "CDROMDiagGetFirstSeekBlock");
    p_GPUReadLong                = dlsym(C.handle, "GPUReadLong");
    if (!p_retro_load_game || !p_retro_run || !C.GetRamPtr
        || !p_CDIntfReadBlock || !p_CDROMDiagGetFirstSeekBlock
        || !p_GPUReadLong) {
        SKIP_TEST(fifo_stream_delivery,
                  "core missing required exports (build with TEST_EXPORTS=1)");
        return;
    }

    memset(&info, 0, sizeof(info));
    info.path = disc;
    if (!p_retro_load_game(&info)) {
        SKIP_TEST(fifo_stream_delivery,
                  "retro_load_game failed (CD BIOS missing or disc parse failed)");
        return;
    }

    ram = C.GetRamPtr();
    frames = 1800;
    {
        const char *fe = getenv("VJ_FIFO_FRAMES");
        if (fe && fe[0]) frames = (uint32_t)atoi(fe);
    }

    /* Run the boot.  Once the first seek is known, poll the GPU state block
     * for the sync sentinel (the 68K CD_read writes it around the same time
     * it issues the DSA seek -- ordering varies per title, so re-read each
     * poll until the disc scan confirms a plausible value), reconstruct the
     * expected payload from the disc image, then early-out as soon as the
     * payload shows up in RAM. */
    have_payload = 0;
    hit = -1;
    seekLBA = 0xFFFFFFFFu;
    for (f = 0; f < frames; f++) {
        p_retro_run();
        if ((f % 25) != 0)
            continue;
        if (!have_payload) {
            seekLBA = p_CDROMDiagGetFirstSeekBlock();
            if (seekLBA != 0xFFFFFFFFu) {
                uint32_t sentinel = p_GPUReadLong(GPU_STATE_SENTINEL, 0);
                if (sentinel != 0 && sentinel != 0xFFFFFFFFu
                    && find_sync_payload(seekLBA, sentinel, payload)) {
                    fprintf(stderr,
                            "    frame %u: seek LBA=%u sentinel=$%08X -> payload known\n",
                            f, seekLBA, sentinel);
                    have_payload = 1;
                }
            }
        }
        if (have_payload) {
            hit = ram_find(ram, payload);
            if (hit >= 0)
                break;
        }
    }

    if (seekLBA == 0xFFFFFFFFu) {
        if (p_retro_unload_game) p_retro_unload_game();
        FAIL("boot never issued a CD seek within %u frames", frames);
    }
    if (!have_payload) {
        if (p_retro_unload_game) p_retro_unload_game();
        SKIP_TEST(fifo_stream_delivery,
                  "no >=16-group sentinel sync mark identified near the first "
                  "seek LBA (disc not streaming-testable this way)");
        return;
    }

    if (hit < 0)
        hit = ram_find(ram, payload);

    fprintf(stderr, "    first seek LBA=%u  payload[0..7]=%02X %02X %02X %02X "
            "%02X %02X %02X %02X  ram_hit=%ld\n",
            p_CDROMDiagGetFirstSeekBlock(),
            payload[0], payload[1], payload[2], payload[3],
            payload[4], payload[5], payload[6], payload[7], hit);

    if (hit < 0)
        FAIL("post-sync payload (64 bytes) not found anywhere in main RAM -- "
             "the CD FIFO stream never delivered the game data (streaming wall)");

    if (p_retro_unload_game) p_retro_unload_game();
}

int main(int argc, char *argv[])
{
    void (*p_retro_deinit)(void);
    (void)argc; (void)argv;

    TEST_INIT("CD FIFO stream delivery contract");

    if (!vj_core_load(&C)) {
        fprintf(stderr, "FATAL: failed to load core\n");
        return 1;
    }

    C.retro_set_environment(fifo_environment);
    C.retro_set_video_refresh(fifo_video);
    C.retro_set_audio_sample(fifo_audio);
    C.retro_set_audio_sample_batch(fifo_audio_batch);
    C.retro_set_input_poll(fifo_input_poll);
    C.retro_set_input_state(fifo_input_state);
    C.retro_init();

    RUN_TEST(fifo_stream_delivery);

    p_retro_deinit = dlsym(C.handle, "retro_deinit");
    if (p_retro_deinit) p_retro_deinit();
    if (C.handle) dlclose(C.handle);

    return TEST_REPORT();
}
