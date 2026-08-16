/*
 * test_cd_chd.c -- CHD session gate (issue #322)
 *
 *   good      : test/roms/synth_jagcd.chd (CHSE, two sessions) must load
 *               with session-2 INDEX 01 at LBA 11404 (4-sector session 1
 *               + synthesized 11400-sector gap, PREGAP 0).
 *   nosession : test/roms/synth_jagcd_nosession.chd must be refused by
 *               ParseCHD (log contains "no session metadata (CHSE)"),
 *               not merely fail later at HLE extract.
 *
 * Fixtures are committed uncompressed CD CHDs. If one is missing, SKIP
 * rather than fail a checkout that forgot to git-add the files. There
 * is no in-test rebuild path (CI/Homebrew chdman cannot write CHSE).
 *
 * Build: make TEST_EXPORTS=1 test/test_cd_chd
 */

#include "cd_assertions.h"
#include "../libretro-common/include/libretro.h"

#include <stdarg.h>
#include <unistd.h>
#include <sys/stat.h>

static struct vj_core C;
static char g_log[8192];

static bool file_exists(const char *path)
{
    struct stat st;
    return stat(path, &st) == 0 && st.st_size > 0;
}

static void test_log(enum retro_log_level level, const char *fmt, ...)
{
    va_list ap;
    size_t n;
    int wrote;

    (void)level;
    n = strlen(g_log);
    if (n + 1 >= sizeof(g_log))
        return;
    va_start(ap, fmt);
    wrote = vsnprintf(g_log + n, sizeof(g_log) - n, fmt, ap);
    va_end(ap);
    (void)wrote;
}

static bool cd_load_game(const char *path)
{
    struct retro_game_info info;
    bool (*p_retro_load_game)(const struct retro_game_info *);
    memset(&info, 0, sizeof(info));
    info.path = path;
    p_retro_load_game = (bool (*)(const struct retro_game_info *))
                            dlsym(C.handle, "retro_load_game");
    if (!p_retro_load_game) return false;
    g_log[0] = '\0';
    return p_retro_load_game(&info);
}

static void cd_unload_game(void)
{
    void (*p)(void) = (void (*)(void))dlsym(C.handle, "retro_unload_game");
    if (p) p();
}

static uint32_t cd_num_sessions(void)
{
    uint32_t (*p)(void) = (uint32_t (*)(void))dlsym(C.handle, "CDIntfGetNumSessions");
    if (!p) return 0;
    return p();
}

static uint32_t cd_session2_lba(void)
{
    uint32_t (*p)(void) = (uint32_t (*)(void))dlsym(C.handle, "CDIntfGetSession2FirstTrackLBA");
    if (!p) return 0;
    return p();
}

static uint32_t cd_num_tracks(void)
{
    uint32_t (*p)(void) = (uint32_t (*)(void))dlsym(C.handle, "CDIntfGetNumTracks");
    if (!p) return 0;
    return p();
}

static uint8_t cd_track_session(uint32_t track)
{
    uint8_t (*p)(uint32_t) = (uint8_t (*)(uint32_t))dlsym(C.handle, "CDIntfGetTrackSession");
    if (!p) return 0;
    return p(track);
}

static bool cd_read_block(uint32_t lba, uint8_t *buf)
{
    bool (*p)(uint32_t, uint8_t *) =
        (bool (*)(uint32_t, uint8_t *))dlsym(C.handle, "CDIntfReadBlock");
    if (!p) return false;
    return p(lba, buf);
}

static bool cd_last_virtual_pregap(void)
{
    bool (*p)(void) = (bool (*)(void))dlsym(C.handle, "CDIntfLastReadWasVirtualPregap");
    if (!p) return false;
    return p();
}

static void cd_clear_virtual_pregap(void)
{
    void (*p)(void) = (void (*)(void))dlsym(C.handle, "CDIntfClearLastReadVirtualPregap");
    if (p) p();
}

static bool cd_extract_stub(uint8_t *buf, uint32_t bufsz, uint32_t *addr, uint32_t *len)
{
    bool (*p)(uint8_t *, uint32_t, uint32_t *, uint32_t *) =
        (bool (*)(uint8_t *, uint32_t, uint32_t *, uint32_t *))
            dlsym(C.handle, "CDIntfExtractBootStub");
    if (!p) return false;
    return p(buf, bufsz, addr, len);
}

static void swap16(uint8_t *buf, size_t n)
{
    size_t i;
    uint8_t t;
    for (i = 0; i + 1 < n; i += 2) {
        t = buf[i];
        buf[i] = buf[i + 1];
        buf[i + 1] = t;
    }
}

static bool env_cb(unsigned cmd, void *data)
{
    if (cmd == RETRO_ENVIRONMENT_GET_LOG_INTERFACE) {
        struct retro_log_callback *cb = (struct retro_log_callback *)data;
        cb->log = test_log;
        return true;
    }
    if (cmd == RETRO_ENVIRONMENT_GET_VARIABLE) {
        struct retro_variable *var = (struct retro_variable *)data;
        if (strcmp(var->key, "virtualjaguar_cd_boot_mode") == 0) {
            var->value = "hle";
            return true;
        }
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

TEST(chd_without_chse_is_refused)
{
    const char *fix = "test/roms/synth_jagcd_nosession.chd";
    if (!file_exists(fix)) {
        SKIP_TEST(chd_without_chse_is_refused, "missing test/roms/synth_jagcd_nosession.chd");
        return;
    }
    ASSERT_TRUE(!cd_load_game(fix));
    ASSERT_TRUE(strstr(g_log, "no session metadata (CHSE)") != NULL);
    cd_unload_game();
}

TEST(chd_with_chse_has_two_sessions)
{
    const char *fix = "test/roms/synth_jagcd.chd";
    if (!file_exists(fix)) {
        SKIP_TEST(chd_with_chse_has_two_sessions, "missing test/roms/synth_jagcd.chd");
        return;
    }
    ASSERT_TRUE(cd_load_game(fix));
    ASSERT_EQ_U32(cd_num_sessions(), 2u);
    ASSERT_EQ_U32(cd_num_tracks(), 2u);
    ASSERT_EQ(cd_track_session(1), 1);
    ASSERT_EQ(cd_track_session(2), 2);
    /* 4-sector session 1 + synthesized 11400-sector gap, PREGAP 0. */
    ASSERT_EQ_U32(cd_session2_lba(), 11404u);
    cd_unload_game();
}

TEST(chd_gap_and_session1_are_silence)
{
    const char *fix = "test/roms/synth_jagcd.chd";
    uint8_t buf[2352];
    size_t i;
    int nonzero;

    if (!file_exists(fix)) {
        SKIP_TEST(chd_gap_and_session1_are_silence, "missing test/roms/synth_jagcd.chd");
        return;
    }
    ASSERT_TRUE(cd_load_game(fix));

    ASSERT_TRUE(cd_read_block(0, buf));
    nonzero = 0;
    for (i = 0; i < sizeof(buf); i++) {
        if (buf[i]) { nonzero = 1; break; }
    }
    ASSERT_FALSE(nonzero);

    cd_clear_virtual_pregap();
    ASSERT_TRUE(cd_read_block(100, buf));
    ASSERT_TRUE(cd_last_virtual_pregap());
    nonzero = 0;
    for (i = 0; i < sizeof(buf); i++) {
        if (buf[i]) { nonzero = 1; break; }
    }
    ASSERT_FALSE(nonzero);

    cd_unload_game();
}

TEST(chd_session2_header_and_extract)
{
    const char *fix = "test/roms/synth_jagcd.chd";
    static const char magic[32] = "ATARI APPROVED DATA HEADER ATRI ";
    uint8_t raw[2352];
    uint8_t stub[0x800];
    uint32_t addr, len;

    if (!file_exists(fix)) {
        SKIP_TEST(chd_session2_header_and_extract, "missing test/roms/synth_jagcd.chd");
        return;
    }
    ASSERT_TRUE(cd_load_game(fix));

    ASSERT_TRUE(cd_read_block(11404u, raw));
    /* ReadBlock returns CUE/BIN I2S order; unswap to host/header space. */
    swap16(raw, sizeof(raw));
    ASSERT_TRUE(memcmp(raw + 0x42, magic, 32) == 0);

    addr = 0;
    len = 0;
    ASSERT_TRUE(cd_extract_stub(stub, sizeof(stub), &addr, &len));
    ASSERT_TRUE(len > 0);
    ASSERT_TRUE(len <= sizeof(stub));

    cd_unload_game();
}

int main(int argc, char *argv[])
{
    (void)argc; (void)argv;

    TEST_INIT("CD CHD session gate");

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

    RUN_TEST(chd_without_chse_is_refused);
    RUN_TEST(chd_with_chse_has_two_sessions);
    RUN_TEST(chd_gap_and_session1_are_silence);
    RUN_TEST(chd_session2_header_and_extract);

    {
        void (*p)(void) = (void (*)(void))dlsym(C.handle, "retro_deinit");
        if (p) p();
    }
    if (C.handle) dlclose(C.handle);

    return TEST_REPORT();
}
