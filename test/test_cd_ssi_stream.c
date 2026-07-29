/*
 * test_cd_ssi_stream.c -- SSI (DSP CD-audio) sample stream contract test.
 *
 * The DSP's CD-audio mix ISR reads LRXD/RRXD ($F1A148/$F1A14C), fed by
 * SetSSIWordsXmittedFromButch() from the SSI head's sector buffer.  This
 * test drives a real seek+play over the DSA protocol and asserts the
 * delivered sample stream against the disc image bytes:
 *
 *   1. LRXD carries the LEFT channel = little-endian bytes [4i+0..1] and
 *      RRXD the RIGHT channel = bytes [4i+2..3] of each Red Book sample
 *      (the pre-fix code had the channels swapped and the head misaligned
 *      by the BUTCH FIFO capture skew, which belongs to the data path).
 *   2. The stream is sample-aligned from byte 0 of the seek target sector
 *      and crosses sector boundaries linearly (the pre-fix head replayed
 *      the seek sector twice: ssiBlock was not advanced past the sector
 *      preloaded at seek time).
 *   3. A paused drive delivers silence and holds position; unpausing
 *      resumes with the very next sample (no bytes lost or skipped).
 *
 * SKIPs cleanly when no disc is available (commercial images are
 * gitignored and absent in CI), like the other test_cd_* harnesses.
 *
 * Build:
 *   make -j8 TEST_EXPORTS=1 && cc -O2 -Wall -std=c99 -I. \
 *       -o test/test_cd_ssi_stream test/test_cd_ssi_stream.c -ldl
 * Run (local, needs a disc with an audio track under test/roms/private):
 *   VJ_SSI_DISC="test/roms/private/Primal Rage (USA)/Primal Rage (USA).cue" \
 *       test/test_cd_ssi_stream
 */

#include "test_framework.h"
#include "../libretro-common/include/libretro.h"

#include <dlfcn.h>

static struct vj_core C;
static const char *g_system_dir = "test/roms/private";

/* BUTCH register byte offsets as CDROMWriteWord/CDROMReadWord see them */
#define REG_DS_DATA  0x0A

/* ---- libretro environment (BIOS-mode CD boot, same as fifo test) ------- */
static bool ssi_environment(unsigned cmd, void *data)
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

static void ssi_video(const void *d, unsigned w, unsigned h, size_t p)
{ (void)d; (void)w; (void)h; (void)p; }
static void ssi_audio(int16_t l, int16_t r) { (void)l; (void)r; }
static size_t ssi_audio_batch(const int16_t *d, size_t f) { (void)d; return f; }
static void ssi_input_poll(void) {}
static int16_t ssi_input_state(unsigned p, unsigned d, unsigned i, unsigned id)
{ (void)p; (void)d; (void)i; (void)id; return 0; }

/* ---- exported internals (TEST_EXPORTS builds) --------------------------- */
static int      (*p_CDIntfReadBlock)(uint32_t, uint8_t *);
static uint8_t  (*p_CDIntfGetTrackInfo)(uint32_t, uint32_t);
static uint32_t (*p_CDIntfGetNumTracks)(void);
static void     (*p_CDROMWriteWord)(uint32_t, uint16_t, uint32_t);
static uint16_t (*p_CDROMReadWord)(uint32_t, uint32_t);
static void     (*p_BUTCHExec)(uint32_t);
static void     (*p_SetSSI)(void);
static uint16_t *p_lrxd, *p_rrxd;

#define CHECK_SAMPLES  (2352u / 4u * 3u)   /* three full sectors */

static uint16_t le16(const uint8_t *p) { return (uint16_t)((p[1] << 8) | p[0]); }

static void test_ssi_stream(void)
{
    const char *disc = getenv("VJ_SSI_DISC");
    struct retro_game_info info;
    bool (*p_retro_load_game)(const struct retro_game_info *);
    void (*p_retro_unload_game)(void);
    uint8_t m, s, f;
    uint32_t startLBA, i, tick;
    static uint8_t ref[2352 * 4];

    if (!disc || !disc[0]) {
        SKIP_TEST(ssi_stream, "set VJ_SSI_DISC to a CD image path");
        return;
    }

    p_retro_load_game    = dlsym(C.handle, "retro_load_game");
    p_retro_unload_game  = dlsym(C.handle, "retro_unload_game");
    p_CDIntfReadBlock    = dlsym(C.handle, "CDIntfReadBlock");
    p_CDIntfGetTrackInfo = dlsym(C.handle, "CDIntfGetTrackInfo");
    p_CDIntfGetNumTracks = dlsym(C.handle, "CDIntfGetNumTracks");
    p_CDROMWriteWord     = dlsym(C.handle, "CDROMWriteWord");
    p_CDROMReadWord      = dlsym(C.handle, "CDROMReadWord");
    p_BUTCHExec          = dlsym(C.handle, "BUTCHExec");
    p_SetSSI             = dlsym(C.handle, "SetSSIWordsXmittedFromButch");
    p_lrxd               = dlsym(C.handle, "lrxd");
    p_rrxd               = dlsym(C.handle, "rrxd");
    if (!p_retro_load_game || !p_CDIntfReadBlock || !p_CDIntfGetTrackInfo
        || !p_CDIntfGetNumTracks || !p_CDROMWriteWord || !p_CDROMReadWord
        || !p_BUTCHExec || !p_SetSSI || !p_lrxd || !p_rrxd) {
        SKIP_TEST(ssi_stream,
                  "core missing required exports (build with TEST_EXPORTS=1)");
        return;
    }

    memset(&info, 0, sizeof(info));
    info.path = disc;
    if (!p_retro_load_game(&info)) {
        SKIP_TEST(ssi_stream,
                  "retro_load_game failed (CD BIOS missing or disc parse failed)");
        return;
    }

    /* Track 2 is an audio track on every Jaguar CD (session 1 audio,
     * session 2 data). */
    if (p_CDIntfGetNumTracks() < 2) {
        if (p_retro_unload_game) p_retro_unload_game();
        SKIP_TEST(ssi_stream, "disc has no track 2");
        return;
    }
    m = p_CDIntfGetTrackInfo(2, 0);
    s = p_CDIntfGetTrackInfo(2, 1);
    f = p_CDIntfGetTrackInfo(2, 2);
    if (m == 0xFF) {
        if (p_retro_unload_game) p_retro_unload_game();
        SKIP_TEST(ssi_stream, "no track info for track 2");
        return;
    }
    startLBA = (((uint32_t)m * 60u + s) * 75u + f) - 150u;

    /* Reference bytes straight from the disc image */
    for (i = 0; i < 4; i++)
        p_CDIntfReadBlock(startLBA + i, ref + i * 2352);

    /* Seek over the DSA protocol, then tick BUTCH until the seek's
     * 100-tick delay elapses (sets cdPlaying and preloads the SSI head). */
    p_CDROMWriteWord(REG_DS_DATA, (uint16_t)(0x1000 | m), 0);
    p_CDROMWriteWord(REG_DS_DATA, (uint16_t)(0x1100 | s), 0);
    p_CDROMWriteWord(REG_DS_DATA, (uint16_t)(0x1200 | f), 0);
    for (tick = 0; tick < 200; tick++)
        p_BUTCHExec(1);
    /* Consume the Found response (drain up to a few stale queue entries
     * that load-time init may have left behind). */
    for (tick = 0; tick < 8; tick++)
        if (p_CDROMReadWord(REG_DS_DATA, 0) == 0x0100)
            break;
    if (tick == 8) {
        if (p_retro_unload_game) p_retro_unload_game();
        FAIL("seek did not complete with Found ($0100)");
    }

    /* Pump the SSI delivery directly and compare each stereo sample
     * against the disc bytes: L = [4i+0..1], R = [4i+2..3], starting at
     * byte 0 of the seek sector and continuing linearly across sector
     * boundaries. */
    for (i = 0; i < CHECK_SAMPLES; i++) {
        uint16_t expL = le16(ref + i * 4 + 0);
        uint16_t expR = le16(ref + i * 4 + 2);
        p_SetSSI();
        if (*p_lrxd != expL || *p_rrxd != expR) {
            fprintf(stderr,
                    "    sample %u (sector +%u byte %u): got L=$%04X R=$%04X "
                    "want L=$%04X R=$%04X\n",
                    i, (i * 4) / 2352, (i * 4) % 2352,
                    *p_lrxd, *p_rrxd, expL, expR);
            if (p_retro_unload_game) p_retro_unload_game();
            FAIL("SSI stream does not match disc bytes (alignment/channel/"
                 "sector-sequence bug)");
        }
    }
    fprintf(stderr, "    %u samples verified across %u sector boundaries\n",
            CHECK_SAMPLES, (CHECK_SAMPLES * 4) / 2352);

    /* Pause: silence + hold.  Consume the $0400 ack first. */
    p_CDROMWriteWord(REG_DS_DATA, 0x0400, 0);
    if (p_CDROMReadWord(REG_DS_DATA, 0) != 0x0400) {
        if (p_retro_unload_game) p_retro_unload_game();
        FAIL("pause did not ack with $0400");
    }
    for (tick = 0; tick < 32; tick++) {
        p_SetSSI();
        if (*p_lrxd != 0 || *p_rrxd != 0) {
            if (p_retro_unload_game) p_retro_unload_game();
            FAIL("paused drive delivered non-silence (L=$%04X R=$%04X)",
                 *p_lrxd, *p_rrxd);
        }
    }

    /* Unpause: the very next sample continues the stream. */
    p_CDROMWriteWord(REG_DS_DATA, 0x0500, 0);
    if (p_CDROMReadWord(REG_DS_DATA, 0) != 0x0400) {
        if (p_retro_unload_game) p_retro_unload_game();
        FAIL("unpause did not ack with $0400");
    }
    for (i = CHECK_SAMPLES; i < CHECK_SAMPLES + 256u; i++) {
        uint16_t expL = le16(ref + i * 4 + 0);
        uint16_t expR = le16(ref + i * 4 + 2);
        p_SetSSI();
        if (*p_lrxd != expL || *p_rrxd != expR) {
            fprintf(stderr,
                    "    post-unpause sample %u: got L=$%04X R=$%04X "
                    "want L=$%04X R=$%04X\n",
                    i, *p_lrxd, *p_rrxd, expL, expR);
            if (p_retro_unload_game) p_retro_unload_game();
            FAIL("stream did not resume at held position after unpause");
        }
    }
    fprintf(stderr, "    pause held position; resumed exactly in-stream\n");

    if (p_retro_unload_game) p_retro_unload_game();
}

int main(int argc, char *argv[])
{
    (void)argc; (void)argv;

    TEST_INIT("CD SSI (DSP audio) stream contract");

    if (!vj_core_load(&C)) {
        fprintf(stderr, "FATAL: failed to load core\n");
        return 1;
    }

    C.retro_set_environment(ssi_environment);
    C.retro_set_video_refresh(ssi_video);
    C.retro_set_audio_sample(ssi_audio);
    C.retro_set_audio_sample_batch(ssi_audio_batch);
    C.retro_set_input_poll(ssi_input_poll);
    C.retro_set_input_state(ssi_input_state);
    C.retro_init();

    RUN_TEST(ssi_stream);

    C.retro_deinit();
    vj_core_unload(&C);

    return TEST_REPORT();
}
