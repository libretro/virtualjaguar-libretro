/*
 * test_cd_pregap.c -- session-2 track pregap handling in the CD boot-stub
 * extractor.
 *
 * The boot-stub reader seeks to the start of the session-2 track's REGION,
 * which begins with the track's pregap.  The Atari boot header lives at the
 * start of the USER DATA (INDEX 01), so a non-zero pregap has to be skipped
 * or the reader sees pregap silence and the magic check at +0x42 fails.
 *
 * This regressed nothing for a long time because nothing available exercised
 * it with a non-zero skip: test/tools/cue2cdi emits pregap == 0 on the
 * session-2 track, and every CUE in the (gitignored) commercial corpus has
 * INDEX 01 00:00:00 on its first session-2 track.  DiscJuggler does store the
 * pregap in-file.
 *
 * So this test SYNTHESISES both discs -- no commercial data, runs anywhere:
 *
 *   nopregap  track 02 BIN = header block            , INDEX 01 00:00:00
 *   pregap    track 02 BIN = 150 silent sectors + it , INDEX 00 00:00:00
 *                                                      INDEX 01 00:02:00
 *
 * Both declare the same user data, so both MUST extract a byte-identical boot
 * stub.  Before the fix the "pregap" disc fails to load at all.
 *
 * Build:
 *   make -j4 && make test/test_cd_pregap
 * Run:
 *   DYLD_LIBRARY_PATH=. test/test_cd_pregap
 */

#include "cd_assertions.h"
#include "../libretro-common/include/libretro.h"

#include <unistd.h>
#include <sys/stat.h>

static struct vj_core C;

/* ------------------------------------------------------------------ */
/* Synthetic disc construction                                         */
/* ------------------------------------------------------------------ */

#define SECTOR_BYTES   2352u
#define PREGAP_SECTORS 150u          /* 00:02:00 */
#define STUB_LOAD_ADDR 0x004000u
#define STUB_LENGTH    0x400u
#define HDR_MAGIC_OFF  0x42u
#define HDR_LOAD_OFF   0x62u
#define HDR_LEN_OFF    0x66u
#define HDR_PAYLOAD    0x6Au
#define TRACK2_DATA    (HDR_PAYLOAD + STUB_LENGTH + 0x100u)

static const char BOOT_MAGIC[32] = "ATARI APPROVED DATA HEADER ATRI ";

/* Build the session-2 user data exactly as the extractor expects it, in
 * "swapped" space, then byte-swap each 16-bit pair to get the on-disc bytes
 * (Jaguar I2S order -- the extractor un-swaps before reading the header). */
static void build_track2_userdata(uint8_t *out, size_t outLen)
{
    uint8_t *sw;
    size_t i;

    sw = (uint8_t *)calloc(1, outLen);
    if (!sw) return;

    memcpy(sw + HDR_MAGIC_OFF, BOOT_MAGIC, sizeof(BOOT_MAGIC));

    sw[HDR_LOAD_OFF + 0] = (uint8_t)(STUB_LOAD_ADDR >> 24);
    sw[HDR_LOAD_OFF + 1] = (uint8_t)(STUB_LOAD_ADDR >> 16);
    sw[HDR_LOAD_OFF + 2] = (uint8_t)(STUB_LOAD_ADDR >>  8);
    sw[HDR_LOAD_OFF + 3] = (uint8_t)(STUB_LOAD_ADDR);

    sw[HDR_LEN_OFF + 0]  = (uint8_t)(STUB_LENGTH >> 24);
    sw[HDR_LEN_OFF + 1]  = (uint8_t)(STUB_LENGTH >> 16);
    sw[HDR_LEN_OFF + 2]  = (uint8_t)(STUB_LENGTH >>  8);
    sw[HDR_LEN_OFF + 3]  = (uint8_t)(STUB_LENGTH);

    /* Payload: 68K "bra.s *" ($60FE) repeated -- a valid, harmless stub that
     * parks the CPU instead of executing random bytes. */
    for (i = HDR_PAYLOAD; i + 1 < HDR_PAYLOAD + STUB_LENGTH && i + 1 < outLen; i += 2) {
        sw[i]     = 0x60;
        sw[i + 1] = 0xFE;
    }

    for (i = 0; i + 1 < outLen; i += 2) {
        out[i]     = sw[i + 1];
        out[i + 1] = sw[i];
    }
    free(sw);
}

static bool write_file(const char *path, const uint8_t *data, size_t len)
{
    FILE *f = fopen(path, "wb");
    size_t n;
    if (!f) return false;
    n = len ? fwrite(data, 1, len, f) : 0;
    fclose(f);
    return n == len;
}

/* Writes <dir>/<name>.cue + track BINs.  withPregap selects whether the
 * session-2 track carries a 150-sector pregap (declared AND stored). */
static bool make_disc(const char *dir, bool withPregap, char *cueOut, size_t cueOutLen)
{
    char path[1024];
    uint8_t *t1, *t2;
    size_t t1Len, t2Len, dataOff;
    FILE *cue;
    bool ok = false;

    t1Len = 4u * SECTOR_BYTES;                       /* session 1 filler */
    dataOff = withPregap ? (PREGAP_SECTORS * SECTOR_BYTES) : 0u;
    t2Len = dataOff + TRACK2_DATA;

    t1 = (uint8_t *)calloc(1, t1Len);
    t2 = (uint8_t *)calloc(1, t2Len);
    if (!t1 || !t2) { free(t1); free(t2); return false; }

    build_track2_userdata(t2 + dataOff, TRACK2_DATA);

    snprintf(path, sizeof(path), "%s/track01.bin", dir);
    if (!write_file(path, t1, t1Len)) goto done;
    snprintf(path, sizeof(path), "%s/track02.bin", dir);
    if (!write_file(path, t2, t2Len)) goto done;

    snprintf(cueOut, cueOutLen, "%s/disc.cue", dir);
    cue = fopen(cueOut, "wb");
    if (!cue) goto done;
    fprintf(cue,
            "REM SESSION 01\n"
            "FILE \"track01.bin\" BINARY\n"
            "  TRACK 01 AUDIO\n"
            "    INDEX 01 00:00:00\n"
            "REM SESSION 02\n"
            "FILE \"track02.bin\" BINARY\n"
            "  TRACK 02 AUDIO\n");
    if (withPregap)
        fprintf(cue, "    INDEX 00 00:00:00\n    INDEX 01 00:02:00\n");
    else
        fprintf(cue, "    INDEX 01 00:00:00\n");
    fclose(cue);
    ok = true;

done:
    free(t1);
    free(t2);
    return ok;
}

/* ------------------------------------------------------------------ */

static bool cd_load_game(const char *path)
{
    struct retro_game_info info;
    bool (*p_retro_load_game)(const struct retro_game_info *);
    memset(&info, 0, sizeof(info));
    info.path = path;
    p_retro_load_game = (bool (*)(const struct retro_game_info *))
                            dlsym(C.handle, "retro_load_game");
    if (!p_retro_load_game) return false;
    return p_retro_load_game(&info);
}

static void cd_unload_game(void)
{
    void (*p)(void) = (void (*)(void))dlsym(C.handle, "retro_unload_game");
    if (p) p();
}

static bool env_cb(unsigned cmd, void *data)
{
    if (cmd == RETRO_ENVIRONMENT_GET_VARIABLE) {
        struct retro_variable *var = (struct retro_variable *)data;
        if (strcmp(var->key, "virtualjaguar_cd_boot_mode") == 0) { var->value = "hle"; return true; }
        return false;
    }
    if (cmd == RETRO_ENVIRONMENT_GET_SYSTEM_DIRECTORY) {
        *(const char **)data = ".";
        return true;
    }
    if (cmd == RETRO_ENVIRONMENT_SET_PIXEL_FORMAT) return true;
    return false;
}

static void vcb(const void *d, unsigned w, unsigned h, size_t p)
{ (void)d; (void)w; (void)h; (void)p; }
static void acb(int16_t l, int16_t r) { (void)l; (void)r; }
static size_t abcb(const int16_t *d, size_t f) { (void)d; return f; }
static void ipcb(void) {}
static int16_t iscb(unsigned p, unsigned d, unsigned i, unsigned id)
{ (void)p; (void)d; (void)i; (void)id; return 0; }

/* ------------------------------------------------------------------ */

TEST(session2_pregap_is_skipped)
{
    char base[512];
    char subA[1024], subB[1024], cueA[1024], cueB[1024];

    /* Deterministic per-process scratch dir: mkdtemp() is hidden behind
     * different feature-test macros on Darwin vs glibc, and this needs no
     * uniqueness beyond the running process. */
    snprintf(base, sizeof(base), "/tmp/vj_cd_pregap_%ld", (long)getpid());
    mkdir(base, 0755);

    snprintf(subA, sizeof(subA), "%s/nopregap", base);
    snprintf(subB, sizeof(subB), "%s/pregap", base);
    ASSERT_TRUE(mkdir(subA, 0755) == 0);
    ASSERT_TRUE(mkdir(subB, 0755) == 0);

    ASSERT_TRUE(make_disc(subA, false, cueA, sizeof(cueA)));
    ASSERT_TRUE(make_disc(subB, true,  cueB, sizeof(cueB)));

    /* Control: pregap == 0. Must load (it did before the fix too). */
    ASSERT_TRUE(cd_load_game(cueA));
    cd_unload_game();

    /* Regression: same user data behind a declared 150-sector pregap.
     * Before the fix the reader seeks to the region start, sees silence,
     * and the +0x42 magic check fails -> retro_load_game() returns false. */
    ASSERT_TRUE(cd_load_game(cueB));
    cd_unload_game();
}

int main(int argc, char *argv[])
{
    (void)argc; (void)argv;

    TEST_INIT("CD session-2 pregap");

    if (!vj_core_load(&C)) {
        fprintf(stderr, "FATAL: failed to load core\n");
        return 1;
    }

    C.retro_set_environment(env_cb);
    C.retro_set_video_refresh(vcb);
    C.retro_set_audio_sample(acb);
    C.retro_set_audio_sample_batch(abcb);
    C.retro_set_input_poll(ipcb);
    C.retro_set_input_state(iscb);
    C.retro_init();

    RUN_TEST(session2_pregap_is_skipped);

    {
        void (*p)(void) = (void (*)(void))dlsym(C.handle, "retro_deinit");
        if (p) p();
    }
    if (C.handle) dlclose(C.handle);

    return TEST_REPORT();
}
