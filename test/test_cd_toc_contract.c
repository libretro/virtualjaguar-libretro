/*
 * test_cd_toc_contract.c -- $2C00 TOC table contract test.
 *
 * After a real-BIOS CD boot reaches the boot-stub / TOC injection point,
 * this reads the actual $2C00 table out of main RAM and runs BOTH game
 * boot-stub scanner algorithms against it, asserting each lands on the
 * session-2 (data) game track.  The two scanners were reverse-engineered
 * from real game boot stubs and the CD BIOS's own $2C00 TOC builder — a
 * 68K routine in the BIOS ROM at $808BE8 that polls BUTCH DSA responses
 * directly (not the DSP code the BIOS uploads to $F1B000, which handles
 * drive-level transport); see the "$2C00 TOC track-indexed layout" section
 * of docs/cd-boot-matrix.md for the layout derivation.
 *
 * SCOPE: this is a table-format contract test, not an end-to-end boot test.
 * The Primal Rage MSF assertion below compares the injected table against
 * CDIntfGetTrackInfo() — the injector's own data source — so it proves the
 * injector copies track data faithfully into the layout the scanners
 * expect, NOT that the MSF->LBA seek mapping is correct (an H1-class
 * pregap/offset bug would still pass here).  Primal Rage's
 * marker-then-NEXT-entry path is also not yet exercised end-to-end in-game
 * (the title is blocked downstream of the TOC scan).
 *
 *   Scanner A -- Baldies boot-stub $4E18:
 *       movea.l #$2C08,a0                 ; entries start at track 1 ($2C08)
 *       loop: movem.l (a0)+,d2-d3         ; d2 = bytes[0..3], d3 = bytes[4..7]
 *             tst.l d2; beq -> return -1  ; zero first longword = end/ERROR
 *             rol.l #8,d3; cmp.b key,d3   ; match byte[4] (session) against key=1
 *             ... find (index)th match
 *       Boot ok iff it returns a NON-negative result (a byte[4]==1 entry is
 *       reachable without a zero-longword terminator interrupting the scan).
 *
 *   Scanner B -- Primal Rage boot-stub $0803E2:
 *       lea $2C00,a0; addq #8,a0          ; start at $2C08 (skip header)
 *       loop: byte[4]!=1 -> a0+=8         ; find first session-2 entry
 *             found      -> a0+=8         ; advance to the NEXT entry
 *       read NEXT entry bytes[1..3] as {min,sec,frm} base MSF.
 *       Correct table => NEXT entry is (firstDataTrack + 1); its byte[0] is
 *       that track number and its MSF matches CDIntfGetTrackInfo().
 *
 * The authoritative layout (track-indexed 8-byte entries, entry for track N
 * at $2C00 + N*8, byte[4] = 0-based session number) makes BOTH scanners land
 * on real data-session track entries.  The pre-fix injector wrote a
 * standalone marker slot with a zero first longword -> Baldies' scan hits the
 * zero terminator and returns -1 (RED); Primal Rage lands one track early.
 *
 * SKIPs cleanly when no CD image / CD BIOS is available (commercial ROMs are
 * gitignored and absent in CI), matching the other test_cd_* harnesses.
 *
 * Build:
 *   make -j8 && cc -O0 -g -Wno-incompatible-pointer-types \
 *       -o test/test_cd_toc_contract test/test_cd_toc_contract.c -ldl
 * Run (local, needs disc + CD BIOS under test/roms/private):
 *   DYLD_LIBRARY_PATH=. VJ_TOC_DISC="test/roms/private/baldies.cdi" \
 *       test/test_cd_toc_contract
 */

#include "test_framework.h"
#include "../libretro-common/include/libretro.h"

#include <dlfcn.h>

static struct vj_core C;
static const char *g_system_dir = "test/roms/private";

/* ---- libretro environment: force real-BIOS CD boot mode ---------------- */
static bool toc_environment(unsigned cmd, void *data)
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
        if (strcmp(var->key, "virtualjaguar_cd_boot_mode") == 0)   {
            const char *m = getenv("VJ_TOC_MODE");
            var->value = (m && strcmp(m, "hle") == 0) ? "hle" : "bios";
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

static void toc_video(const void *d, unsigned w, unsigned h, size_t p)
{ (void)d; (void)w; (void)h; (void)p; }
static void toc_audio(int16_t l, int16_t r) { (void)l; (void)r; }
static size_t toc_audio_batch(const int16_t *d, size_t f) { (void)d; return f; }
static void toc_input_poll(void) {}
static int16_t toc_input_state(unsigned p, unsigned d, unsigned i, unsigned id)
{ (void)p; (void)d; (void)i; (void)id; return 0; }

/* ---- CDIntf accessors (exported for TEST_EXPORTS builds) --------------- */
static uint32_t (*p_CDIntfGetNumTracks)(void);
static uint8_t  (*p_CDIntfGetTrackSession)(uint32_t);
static uint8_t  (*p_CDIntfGetTrackInfo)(uint32_t, uint32_t);

#define TOC_BASE 0x2C00u
#define TOC_END  (0x2C00u + 0x400u)

/* Faithful emulation of Baldies' $4E18 scanner.  Returns the RAM address of
 * the (index)-th entry whose byte[4]==key, or -1 if a zero first-longword
 * terminates the scan first (the deliberate-ILLEGAL error path). */
static long baldies_scan(const uint8_t *ram, uint8_t key, uint32_t index)
{
    uint32_t a0 = TOC_BASE + 8;   /* movea.l #$2C08,a0 */
    uint32_t count = 0;
    while (a0 + 8 <= TOC_END) {
        uint32_t first_lw = ((uint32_t)ram[a0] << 24) | ((uint32_t)ram[a0+1] << 16)
                          | ((uint32_t)ram[a0+2] << 8) | (uint32_t)ram[a0+3];
        uint8_t  b4 = ram[a0 + 4];     /* rol.l #8,d3; cmp.b -> compares byte[4] */
        if (first_lw == 0)             /* tst.l d2; beq -> return -1 */
            return -1;
        if (b4 == key) {
            if (count == index)
                return (long)a0;
            count++;
        }
        a0 += 8;
    }
    return -1;
}

/* Faithful emulation of Primal Rage's $0803E2 scanner.  Finds the first entry
 * with byte[4]==1, advances to the NEXT entry, and returns its RAM address (or
 * -1 if no session-2 entry is found before the table end). */
static long primal_scan(const uint8_t *ram)
{
    uint32_t a0 = TOC_BASE + 8;   /* lea $2C00,a0 ; addq #8,a0 */
    while (a0 + 8 <= TOC_END) {
        if (ram[a0 + 4] == 1) {
            a0 += 8;              /* adda.w #8,a0 -> NEXT entry */
            return (long)a0;
        }
        a0 += 8;
    }
    return -1;
}

static void test_toc_contract(void)
{
    const char *disc = getenv("VJ_TOC_DISC");
    struct retro_game_info info;
    bool (*p_retro_load_game)(const struct retro_game_info *);
    void (*p_retro_run)(void);
    void (*p_retro_unload_game)(void);
    uint8_t *ram;
    uint32_t numTracks, t, firstData, nextTrack, f;
    long baldies_hit, primal_hit;
    uint8_t exp_min, exp_sec, exp_frm;

    if (!disc || !disc[0]) {
        SKIP_TEST(toc_contract, "set VJ_TOC_DISC to a CD image path");
        return;
    }

    p_retro_load_game   = dlsym(C.handle, "retro_load_game");
    p_retro_run         = dlsym(C.handle, "retro_run");
    p_retro_unload_game = dlsym(C.handle, "retro_unload_game");
    p_CDIntfGetNumTracks    = dlsym(C.handle, "CDIntfGetNumTracks");
    p_CDIntfGetTrackSession = dlsym(C.handle, "CDIntfGetTrackSession");
    p_CDIntfGetTrackInfo    = dlsym(C.handle, "CDIntfGetTrackInfo");
    if (!p_retro_load_game || !p_retro_run || !C.GetRamPtr
        || !p_CDIntfGetNumTracks || !p_CDIntfGetTrackSession || !p_CDIntfGetTrackInfo) {
        SKIP_TEST(toc_contract, "core missing required exports (build with TEST_EXPORTS=1)");
        return;
    }

    memset(&info, 0, sizeof(info));
    info.path = disc;
    if (!p_retro_load_game(&info)) {
        SKIP_TEST(toc_contract, "retro_load_game failed (CD BIOS missing or disc parse failed)");
        return;
    }

    /* Run until the BIOS boot reaches the $050176 injection hook (empirically
     * ~frame 437).  Detect injection by watching $2C00 flip from PRNG-dense
     * RAM fill to the injector's post-memset table (mostly zero).  Stop as soon
     * as it does: some target titles deliberately pc-escape shortly after
     * injection (that IS the bug under test), so running a fixed large budget
     * risks a host crash while the guest executes garbage. */
    ram = C.GetRamPtr();
    for (f = 0; f < 900; f++) {
        uint32_t di, nz = 0;
        p_retro_run();
        for (di = 0; di < 0x100; di++)
            if (ram[TOC_BASE + di]) nz++;
        if (nz < 64)          /* post-memset table: only track entries nonzero */
            break;
    }

    numTracks = p_CDIntfGetNumTracks();
    if (numTracks < 2) {
        if (p_retro_unload_game) p_retro_unload_game();
        FAIL("disc has < 2 tracks (numTracks=%u); not a Jaguar CD image", numTracks);
    }

    /* First session-2 (data) track, 1-based track number. */
    firstData = 0;
    for (t = 1; t <= numTracks; t++) {
        if (p_CDIntfGetTrackSession(t) >= 2) { firstData = t; break; }
    }
    if (firstData == 0) {
        if (p_retro_unload_game) p_retro_unload_game();
        FAIL("no session-2 track found on disc");
    }

    fprintf(stderr, "    disc=%s numTracks=%u firstDataTrack=%u\n",
            disc, numTracks, firstData);
    fprintf(stderr, "    $2C08: %02X %02X %02X %02X %02X %02X %02X %02X\n",
            ram[0x2C08], ram[0x2C09], ram[0x2C0A], ram[0x2C0B],
            ram[0x2C0C], ram[0x2C0D], ram[0x2C0E], ram[0x2C0F]);

    /* --- Contract 1: track 1's entry lives at $2C08, byte[0]==1 --------- */
    CHECK_EQ(ram[0x2C08], 1);

    /* --- Contract 2: Baldies $4E18 scan must SUCCEED (key = session 1,
     *     0-based == the data session).  Pre-fix it returns -1 because a
     *     zero-first-longword marker slot terminates the scan early. ------ */
    baldies_hit = baldies_scan(ram, 1, 0);
    if (baldies_hit < 0)
        FAIL("Baldies $4E18 scan returned -1 (zero-longword terminator before "
             "a byte[4]==1 entry) -> boot stub would ILLEGAL-halt");
    /* The found entry must be a real data-session track (nonzero track#). */
    CHECK_EQ(ram[baldies_hit + 4], 1);
    ASSERT_NEQ(ram[baldies_hit + 0], 0);
    fprintf(stderr, "    Baldies scan -> $%04lX track#=%u session=%u\n",
            baldies_hit, ram[baldies_hit + 0], ram[baldies_hit + 4]);

    /* --- Contract 3: Primal Rage $0803E2 scan lands on the NEXT entry
     *     after the first session-2 track, i.e. (firstDataTrack + 1), with an
     *     MSF matching CDIntf.  Pre-fix the standalone marker shifts this one
     *     track early (lands on firstDataTrack).
     *     NOTE: the expected MSF comes from CDIntfGetTrackInfo — the same
     *     source the injector copies from — so this checks copy fidelity into
     *     the scanner-visible layout, not seek-target/LBA correctness. ----- */
    nextTrack = firstData + 1;
    if (nextTrack > numTracks) {
        fprintf(stderr, "    (data session has a single track; skipping Primal NEXT-entry check)\n");
    } else {
        primal_hit = primal_scan(ram);
        if (primal_hit < 0)
            FAIL("Primal Rage $0803E2 scan found no session-2 entry");
        exp_min = p_CDIntfGetTrackInfo(nextTrack, 0);
        exp_sec = p_CDIntfGetTrackInfo(nextTrack, 1);
        exp_frm = p_CDIntfGetTrackInfo(nextTrack, 2);
        fprintf(stderr, "    Primal scan -> $%04lX track#=%u MSF=%02u:%02u:%02u "
                "(expect track %u MSF=%02u:%02u:%02u)\n",
                primal_hit, ram[primal_hit + 0],
                ram[primal_hit + 1], ram[primal_hit + 2], ram[primal_hit + 3],
                nextTrack, exp_min, exp_sec, exp_frm);
        CHECK_EQ(ram[primal_hit + 0], nextTrack);
        CHECK_EQ(ram[primal_hit + 1], exp_min);
        CHECK_EQ(ram[primal_hit + 2], exp_sec);
        CHECK_EQ(ram[primal_hit + 3], exp_frm);
    }

    if (p_retro_unload_game) p_retro_unload_game();
}

int main(int argc, char *argv[])
{
    void (*p_retro_deinit)(void);
    (void)argc; (void)argv;

    TEST_INIT("CD $2C00 TOC table contract");

    if (!vj_core_load(&C)) {
        fprintf(stderr, "FATAL: failed to load core\n");
        return 1;
    }

    C.retro_set_environment(toc_environment);
    C.retro_set_video_refresh(toc_video);
    C.retro_set_audio_sample(toc_audio);
    C.retro_set_audio_sample_batch(toc_audio_batch);
    C.retro_set_input_poll(toc_input_poll);
    C.retro_set_input_state(toc_input_state);
    C.retro_init();

    RUN_TEST(toc_contract);

    p_retro_deinit = dlsym(C.handle, "retro_deinit");
    if (p_retro_deinit) p_retro_deinit();
    if (C.handle) dlclose(C.handle);

    return TEST_REPORT();
}
