/*
 * test_cd_hle_idempotent.c -- HLE CD_read idempotency regression test.
 *
 * Encodes the contract established in Task 6C: the BIOS CD_read service is a
 * discrete, self-contained read.  D0 (packed MSF) fully specifies the source
 * position; when a boot stub / game re-issues a byte-identical CD_read (same
 * D0/D1/A0/A1), real hardware re-seeks to the SAME position and reproduces the
 * SAME data.  There is no per-call "continuation" that silently advances the
 * source LBA.
 *
 * Failure mode this guards against (the removed `+3 LBA/call` heuristic):
 *   Iron Soldier 2 (hle) gets its first read correct (sync block found), then
 *   re-issues the identical CD_read.  The old heuristic advanced the source
 *   LBA past the previously-transferred sectors, missed the sync block, and
 *   streamed RAW garbage into the SAME destination buffer -- corrupting the
 *   correct first-read data on every subsequent identical call.  The
 *   destination region therefore changed content on essentially every frame.
 *
 * Invariant checked (golden-free, game-agnostic):
 *   A CD stream targeting the watched region must never REWRITE a byte
 *   it (or a previous identical read) already populated — identical
 *   re-issued CD_reads deliver identical bytes.  Mutations are only
 *   counted when they are attributable to the CD: a CD_read whose
 *   destination range overlaps the region was armed recently enough
 *   that its stream is still landing (arm counter + dest/bytes from the
 *   JaguarCDHLEStream* accessors, window = stream duration + slack).
 *   Mutations outside such a window are the game's own writes: since
 *   the long-rounded delivery fix, IS2's checksum accepts the FIRST
 *   read, the stub relocates/patches the accepted section and jumps
 *   into it within this frame window — all legitimate CPU work.  Under
 *   the guarded drift bug the stub re-issues the identical read forever
 *   and every re-issued stream rewrites the region with different
 *   garbage inside its attribution window — the test stays RED for the
 *   regression class.  We count how many times previously-populated
 *   (nonzero) bytes change value after the first population:
 *     RED  (heuristic present): dozens/hundreds of transitions (drift loop
 *           streams different garbage over the good first read each call).
 *     GREEN (heuristic removed): zero transitions (idempotent re-reads).
 *
 *   CD_read has streamed at the real drive rate (352,800 B/s, ~5.9 KB per
 *   NTSC frame) since a1f1ac1 -- an in-flight transfer that starts mid-frame
 *   spans two frame samples, so the region fills across consecutive frames.
 *   A transition whose differing bytes were ALL previously zero is that
 *   fill continuing (zero -> data), not corruption; only transitions that
 *   rewrite previously-nonzero bytes count against maxChanges.
 *
 * This test needs the Iron Soldier 2 (Songbird) private disc image and is NOT
 * part of the default `make test` body -- like the other CD sweeps it walks
 * test/roms/private/ and SKIPs (passes) when the image is absent.
 *
 * Build:  make -j4 test/test_cd_hle_idempotent
 * Run:    DYLD_LIBRARY_PATH=. test/test_cd_hle_idempotent
 *
 * Env knobs:
 *   VJ_TEST_CD_ROOT   disc image root (default: test/roms/private)
 *   VJ_TEST_CD_FOCUS  disc basename substring (default: "Iron Soldier 2")
 *   VJ_TEST_CD_FRAMES frame count (default: 300)
 *   VJ_IDEMP_ADDR     destination region base in main RAM (default: 0xCB00)
 *   VJ_IDEMP_LEN      destination region length in bytes (default: 0x1500)
 *   VJ_IDEMP_MAXCHG   max allowed content transitions (default: 0)
 */

#include "cd_assertions.h"
#include "../libretro-common/include/libretro.h"

static struct vj_core C;

/* ------------------------------------------------------------------ */
/* libretro environment: force HLE CD boot (mirrors test_cd_hle_boot). */
/* ------------------------------------------------------------------ */

static bool cd_environment(unsigned cmd, void *data)
{
    switch (cmd & 0xFF) {
    case RETRO_ENVIRONMENT_GET_LOG_INTERFACE:
        return false;
    case RETRO_ENVIRONMENT_SET_PIXEL_FORMAT:
        return true;
    case RETRO_ENVIRONMENT_GET_SYSTEM_DIRECTORY:
        *(const char **)data = "/nonexistent";
        return true;
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
        if (strcmp(var->key, "virtualjaguar_bios") == 0)            { var->value = "enabled";  return true; }
        if (strcmp(var->key, "virtualjaguar_usefastblitter") == 0)  { var->value = "enabled";  return true; }
        if (strcmp(var->key, "virtualjaguar_cd_bios_type") == 0)    { var->value = "retail";   return true; }
        if (strcmp(var->key, "virtualjaguar_cd_boot_mode") == 0)    { var->value = "hle";      return true; }
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

static void cd_video_refresh(const void *d, unsigned w, unsigned h, size_t p)
{ (void)d; (void)w; (void)h; (void)p; }
static void cd_audio_sample(int16_t l, int16_t r) { (void)l; (void)r; }
static size_t cd_audio_sample_batch(const int16_t *d, size_t f) { (void)d; return f; }
static void cd_input_poll(void) {}
static int16_t cd_input_state(unsigned p, unsigned d, unsigned i, unsigned id)
{ (void)p; (void)d; (void)i; (void)id; return 0; }

/* Cheap FNV-1a hash of a RAM region. */
static uint32_t region_hash(const uint8_t *ram, uint32_t addr, uint32_t len)
{
    uint32_t h = 2166136261u;
    uint32_t i;
    for (i = 0; i < len; i++) {
        h ^= ram[addr + i];
        h *= 16777619u;
    }
    return h;
}

static bool region_nonzero(const uint8_t *ram, uint32_t addr, uint32_t len)
{
    uint32_t i;
    for (i = 0; i < len; i++)
        if (ram[addr + i] != 0) return true;
    return false;
}

TEST(cd_read_is_idempotent)
{
    const char *root;
    const char *focus_env;
    const char *frames_env;
    struct cd_disc_list discs;
    struct retro_game_info info;
    bool (*p_retro_load_game)(const struct retro_game_info *);
    void (*p_retro_run)(void);
    void (*p_retro_unload_game)(void);
    const char *disc_path = NULL;
    uint8_t *ram;
    uint8_t *snap;
    unsigned frames, f;
    uint32_t addr, len, maxChanges;
    uint32_t prevHash, changes, firstPopFrame;
    bool firstPopSeen;
    uint32_t (*p_arm_count)(void);
    uint32_t (*p_stream_dest)(void);
    uint32_t (*p_stream_bytes)(void);
    uint32_t prevArm;
    unsigned cdWindowEnd;      /* last frame (inclusive) attributable to CD */
    uint32_t ignoredCpu;       /* mutations outside any CD window (game writes) */
    size_t i;
    const char *env;

    root = getenv("VJ_TEST_CD_ROOT");
    if (!root || !root[0]) root = "test/roms/private";

    focus_env = getenv("VJ_TEST_CD_FOCUS");
    if (!focus_env || !focus_env[0]) focus_env = "Iron Soldier 2";

    cd_discover_discs(root, &discs);
    for (i = 0; i < discs.count; i++) {
        const char *label = strrchr(discs.entries[i].path, '/');
        label = label ? label + 1 : discs.entries[i].path;
        if (strstr(label, focus_env)) { disc_path = discs.entries[i].path; break; }
    }

    if (!disc_path) {
        fprintf(stderr, "    [SKIP] no disc matching '%s' under %s\n", focus_env, root);
        return;   /* pass: private image not present (e.g. CI) */
    }

    frames = 300;
    frames_env = getenv("VJ_TEST_CD_FRAMES");
    if (frames_env && frames_env[0]) frames = (unsigned)atoi(frames_env);

    addr = 0xCB00;
    env = getenv("VJ_IDEMP_ADDR"); if (env && env[0]) addr = (uint32_t)strtoul(env, NULL, 0);
    len = 0x1500;
    env = getenv("VJ_IDEMP_LEN"); if (env && env[0]) len = (uint32_t)strtoul(env, NULL, 0);
    maxChanges = 0;
    env = getenv("VJ_IDEMP_MAXCHG"); if (env && env[0]) maxChanges = (uint32_t)strtoul(env, NULL, 0);

    if (addr + len > 0x200000) FAIL("region $%06X+$%X exceeds main RAM", addr, len);

    C.retro_set_environment(cd_environment);
    C.retro_set_video_refresh(cd_video_refresh);
    C.retro_set_audio_sample(cd_audio_sample);
    C.retro_set_audio_sample_batch(cd_audio_sample_batch);
    C.retro_set_input_poll(cd_input_poll);
    C.retro_set_input_state(cd_input_state);

    memset(&info, 0, sizeof(info));
    info.path = disc_path;
    p_retro_load_game = (bool (*)(const struct retro_game_info *))dlsym(C.handle, "retro_load_game");
    if (!p_retro_load_game) FAIL("retro_load_game not resolved");
    if (!p_retro_load_game(&info)) FAIL("retro_load_game returned false");

    p_retro_run = (void (*)(void))dlsym(C.handle, "retro_run");
    p_retro_unload_game = (void (*)(void))dlsym(C.handle, "retro_unload_game");
    if (!p_retro_run) FAIL("retro_run not resolved");

    ram = C.GetRamPtr ? C.GetRamPtr() : NULL;
    if (!ram) FAIL("GetRamPtr not resolved");

    fprintf(stderr, "    disc=%s frames=%u region=$%06X..$%06X\n",
            disc_path, frames, addr, addr + len - 1);

    snap = (uint8_t *)calloc(len, 1);
    if (!snap) FAIL("out of memory for %u-byte region snapshot", len);

    /* CD-attribution accessors (see header comment).  When absent the
     * test falls back to attributing every mutation to the CD (the
     * pre-accessor, stricter behavior). */
    p_arm_count    = (uint32_t (*)(void))dlsym(C.handle, "JaguarCDHLEStreamArmCount");
    p_stream_dest  = (uint32_t (*)(void))dlsym(C.handle, "JaguarCDHLEStreamDest");
    p_stream_bytes = (uint32_t (*)(void))dlsym(C.handle, "JaguarCDHLEStreamBytes");

    prevHash      = region_hash(ram, addr, len);   /* all-zero baseline */
    changes       = 0;
    firstPopSeen  = false;
    firstPopFrame = 0;
    prevArm       = p_arm_count ? p_arm_count() : 0;
    cdWindowEnd   = 0;
    ignoredCpu    = 0;

    for (f = 0; f < frames; f++) {
        uint32_t h;
        bool cdAttrib = true;
        p_retro_run();
        if (p_arm_count && p_stream_dest && p_stream_bytes) {
            uint32_t arm = p_arm_count();
            if (arm != prevArm) {
                uint32_t d = p_stream_dest();
                uint32_t n = p_stream_bytes();
                if (d < addr + len && d + n > addr) {
                    /* CD_read overlapping the watched region armed this
                     * frame: attribute mutations until its stream has
                     * fully landed (352,800 B/s ~= 5.9 KB/frame NTSC)
                     * plus 2 frames of slack. */
                    unsigned dur = (unsigned)(n / 5900u) + 2u;
                    if (f + dur > cdWindowEnd) cdWindowEnd = f + dur;
                }
                prevArm = arm;
            }
            cdAttrib = (f <= cdWindowEnd);
        }
        h = region_hash(ram, addr, len);
        if (h != prevHash) {
            if (!firstPopSeen && region_nonzero(ram, addr, len)) {
                /* zero -> first CD_read data landed: not a corruption. */
                firstPopSeen  = true;
                firstPopFrame = f;
                memcpy(snap, &ram[addr], len);
            } else if (firstPopSeen) {
                /* Streamed CD_read fills the region across frames: a
                 * transition whose differing bytes were ALL zero in the
                 * previous snapshot is population continuing.  Rewriting a
                 * previously-nonzero byte is the guarded corruption. */
                uint32_t k;
                bool mutated = false;
                for (k = 0; k < len; k++) {
                    if (snap[k] != ram[addr + k] && snap[k] != 0) {
                        mutated = true;
                        break;
                    }
                }
                if (mutated && cdAttrib) {
                    changes++;
                    if (changes <= 8)
                        fprintf(stderr,
                                "    [CHANGE] frame %u: populated byte at "
                                "$%06X mutated $%02X -> $%02X "
                                "(hash $%08X, change #%u)\n",
                                f, addr + k, snap[k], ram[addr + k],
                                h, changes);
                } else if (mutated) {
                    /* Outside any CD window: the game's own write. */
                    ignoredCpu++;
                }
                memcpy(snap, &ram[addr], len);
            }
            prevHash = h;
        }
    }

    free(snap);

    if (p_retro_unload_game) p_retro_unload_game();

    fprintf(stderr,
            "    first_populated_frame=%u post_population_changes=%u (max allowed %u)"
            " cpu_writes_ignored=%u\n",
            firstPopSeen ? firstPopFrame : 0u, changes, maxChanges,
            ignoredCpu);

    if (!firstPopSeen)
        FAIL("destination region was never populated by a CD_read");
    if (changes > maxChanges)
        FAIL("CD_read not idempotent: %u post-population content changes "
             "(max %u) -- continuation drift corrupted the buffer",
             changes, maxChanges);
}

int main(int argc, char *argv[])
{
    (void)argc; (void)argv;

    TEST_INIT("CD HLE CD_read Idempotency");

    if (!vj_core_load(&C)) {
        fprintf(stderr, "FATAL: failed to load core\n");
        return 1;
    }

    C.retro_set_environment(cd_environment);
    C.retro_set_video_refresh(cd_video_refresh);
    C.retro_set_audio_sample(cd_audio_sample);
    C.retro_set_audio_sample_batch(cd_audio_sample_batch);
    C.retro_set_input_poll(cd_input_poll);
    C.retro_set_input_state(cd_input_state);
    C.retro_init();

    RUN_TEST(cd_read_is_idempotent);

    {
        void (*p_retro_deinit)(void) = (void (*)(void))dlsym(C.handle, "retro_deinit");
        if (p_retro_deinit) p_retro_deinit();
    }
    if (C.handle) dlclose(C.handle);

    return TEST_REPORT();
}
