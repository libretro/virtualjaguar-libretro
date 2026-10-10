/*
 * test/tools/test_disk_control.c
 *
 * E2E test for the libretro disk control interface (issue #651): boot the
 * core with NO content, hand it a disc through the frontend-facing
 * callbacks, and assert that boot resolution actually RE-RAN against the
 * newly inserted disc.
 *
 * Why the assertion is on the resolved STRATEGY, not on the return value:
 * a reset against the PREVIOUS disc's boot config looks identical from
 * outside -- the machine restarts, the call returns true, frames keep
 * coming -- so "insert returned true" proves nothing at all.  What proves
 * the resolution re-ran is that bootConfig.strategy moved off the
 * no-content strategy and onto a CD one.  bootConfig and the four
 * strategy structs are exported under TEST_EXPORTS=1, and CDBootStrategy
 * carries a `name` ("none"/"hle"/"bios"/"cart"), so the check reads that
 * string rather than comparing pointers.
 *
 * Cases:
 *
 *   1  No-content boot, then insert a real disc.  The strategy must be
 *      "none" before the insert (proving the no-content path resolved)
 *      and a CD strategy after it (proving open_disc_and_resolve_boot()
 *      ran again).  Also asserts the core registered the ext interface at
 *      all -- without that, every later assertion is vacuous.
 *
 *   2  Audio-only (Red Book, one-session) disc inserted after launch must
 *      land on the real CD BIOS, since HLE synthesizes its boot stub from
 *      session-2 data an audio disc has none of.  NOT RUN: no one-session
 *      image exists in the corpus -- every CUE carries two REM SESSION
 *      markers and CDI headers declare numSessions=2.  Writing it against
 *      a data disc would pass for the wrong reason, so `make test` skips
 *      it via scripts/test-skip.sh rather than pretending to cover it.
 *
 *   3  A failed insert must be inert.  Insert a path that cannot be
 *      opened: the call returns false, the tray stays open, and -- the
 *      part that matters -- the resolved strategy is UNCHANGED.  Checking
 *      only the return value would pass against the inert stub this
 *      interface shipped with one commit earlier.
 *
 *   6  No-content boot exposes the Memory Track as SAVE_RAM from the very
 *      first query (#810).  Needs no disc.  The size and pointer must be
 *      CD_SAVE_SIZE and non-NULL straight after the load, and a marker the
 *      "frontend" writes into the buffer before frame 1 must reach mtMem --
 *      i.e. the .srm load a frontend does once, after retro_load_game,
 *      actually lands.  A late-exposure fix would pass a size>0 check after
 *      an insert and still lose every save, because that load already
 *      happened against a size of 0.
 *
 *   7  Same, then insert a disc.  Size and pointer must be unchanged, the
 *      marker must still be in mtMem (the insert's reboot must not wipe the
 *      Memory Track), and the NVM BIOS module cookie must be present in RAM.
 *
 *   8  Memory Track DISABLED (--option virtualjaguar_memory_track=disabled):
 *      nothing to persist on a bare no-content session, so SAVE_RAM is 0 --
 *      and it STAYS 0 after a disc is inserted.  Deliberate: a frontend sizes
 *      and loads the buffer once at load, so exposing it for the first time
 *      after an insert would be a write without a load (the file is
 *      overwritten at exit from a buffer that never saw it).  Guards against
 *      "fixing" #810 by keying SAVE_RAM on a mounted disc.
 *
 *   9  Per-title disc row on INSERT (#747).  --disc must be Baldies (Rev 1)
 *      (boot-stub CRC $82B88060).  A synthetic disc row keyed on that CRC
 *      sets cd_boot_mode=bios; CD Boot Mode is left at its default (hle).
 *      After the insert the strategy must be "bios": the insert path keys
 *      the new disc and re-reads the options before resolving the boot.
 *  10  Control for 9: same disc, no row installed -- the insert resolves
 *      "hle", so case 9's "bios" can only have come from the row.
 *
 * Run: test_disk_control <core> --disc <image> --case N [--quiet]
 *      (--disc is required for cases 1, 3, 4, 7 and 8; cases 5 and 6 are
 *       no-disc no-content boot checks.  Case 4 also needs --disc-b.)
 */

#define _DEFAULT_SOURCE 1

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "../harness/harness.h"
#include "../../libretro-common/include/libretro.h"
#include "../../src/core/titledb.h"

/* Mirrors src/cd/jagcd_boot.h and src/core/settings.h.  Declared here
 * rather than including those headers: settings.h drags in the whole vjs
 * configuration surface (and MAX_PATH with it) for two fields this test
 * reads through a dlsym'd pointer.  Only the leading layout matters --
 * `strategy` is the last field of struct BootConfig, and `name` the first
 * of CDBootStrategy, so a trailing-field addition to either cannot
 * silently shift what is read here. */
struct dc_strategy
{
    const char *name;
    /* remaining function pointers unused by this test */
};

struct dc_bootconfig
{
    bool isCDGame;
    bool showBootROM;
    bool cdBiosAvailable;
    const struct dc_strategy *strategy;
};

static struct dc_bootconfig *bootcfg;

static const char *strategy_name(void)
{
    if (!bootcfg || !bootcfg->strategy || !bootcfg->strategy->name)
        return "(none resolved)";
    return bootcfg->strategy->name;
}

/* A CD strategy is anything the CD path can resolve to: HLE synthesizes a
 * boot stub from session-2 data, the real BIOS runs the retail CD BIOS.
 * Which one a given disc takes depends on CD Boot Mode and the session
 * count, and this test deliberately does not pin that -- case 1 asks only
 * whether resolution re-ran, not which way it went. */
static int strategy_is_cd(void)
{
    const char *n = strategy_name();
    return strcmp(n, "hle") == 0 || strcmp(n, "bios") == 0;
}

/* Frame-progression tracking for case 5 (issue #726).
 *
 * Cases 1/3/4 assert which boot STRATEGY resolved.  That is necessary and
 * not sufficient: a machine that resolves the right strategy and then
 * freezes on the boot ROM's red logo passes every one of them, which is
 * exactly how the v3.6.0 no-content regression shipped.  So this tracks
 * whether the picture actually CHANGES, which is the thing a user sees. */
static uint32_t vid_prev_hash;    /* FNV-1a over the previous frame       */
static unsigned vid_frames;       /* frames the core actually delivered   */
static unsigned vid_changes;      /* frames whose pixels differed         */
static unsigned vid_nonblack;     /* frames with any non-black pixel      */
static int      vid_bad_pitch;    /* set if the frame was not 4 bytes/px  */

static void vid_cb(void *ud, const void *data, unsigned w, unsigned h,
                   size_t pitch)
{
    const uint8_t *p = (const uint8_t *)data;
    uint32_t hsh = 2166136261u;
    unsigned y, x;
    int nonblack = 0;

    (void)ud;
    if (!p)                       /* duped frame: no new pixels */
        return;

    /* The harness video callback carries no pixel-format field, and this
     * indexes rows as uint32_t -- correct for the core's XRGB8888 output,
     * but a silent out-of-bounds read past each row if that ever became
     * RGB565.  Validate rather than assume: a format change should fail
     * loudly here, not read garbage and look like a flaky test.
     *
     * Alignment matters for the same reason: row[x] is a uint32_t load, and
     * an unaligned uint32_t access is undefined behaviour in C (and raises
     * SIGBUS on strict-alignment targets).  Row y starts at base + y*pitch,
     * so every row is 4-byte aligned only if BOTH the base pointer and the
     * pitch are multiples of 4; reject either otherwise. */
    if (pitch < (size_t)w * 4 || (pitch & 3u) != 0
        || ((uintptr_t)data & 3u) != 0) {
        vid_bad_pitch = 1;
        return;
    }
    vid_frames++;
    for (y = 0; y < h; y++)
    {
        const uint32_t *row = (const uint32_t *)(p + (size_t)y * pitch);
        for (x = 0; x < w; x++)
        {
            uint32_t px = row[x];
            hsh = (hsh ^ (px & 0x00FFFFFFu)) * 16777619u;
            if (px & 0x00FFFFFFu)
                nonblack = 1;
        }
    }
    if (nonblack)
        vid_nonblack++;
    if (vid_frames > 1 && hsh != vid_prev_hash)
        vid_changes++;
    vid_prev_hash = hsh;
}

/* Mirrors libretro.c: 128 B cart EEPROM + 128 B CD EEPROM + 128 KB Memory
 * Track.  Hard-coded on purpose -- a test that derived the expectation from
 * the core's own constant could not catch that constant being wrong. */
#define DC_CD_SAVE_SIZE   (128u + 128u + 0x20000u)
#define DC_MT_SAVE_OFFSET 256u
#define DC_MARKER_OFF     0x40u    /* offset inside the Memory Track        */
#define DC_NVM_COOKIE_RAM 0x2400u  /* src/core/nvmbios.h NVM_COOKIE_ADDR    */

static size_t (*lr_mem_size)(unsigned);
static void  *(*lr_mem_data)(unsigned);
static const uint8_t *mt_mem;      /* the core's real Memory Track array    */

static const uint8_t dc_marker[8] = { 0xA5, 0x5A, 0xC3, 0x3C,
                                      0x81, 0x18, 0xE7, 0x7E };

/* Stand in for the frontend loading a .srm: it asks for the size and the
 * pointer once, after retro_load_game, and copies the file into the buffer. */
static int seed_srm_marker(void)
{
    uint8_t *buf = (uint8_t *)lr_mem_data(RETRO_MEMORY_SAVE_RAM);

    if (!buf || lr_mem_size(RETRO_MEMORY_SAVE_RAM) != DC_CD_SAVE_SIZE)
        return 0;
    memcpy(buf + DC_MT_SAVE_OFFSET + DC_MARKER_OFF, dc_marker,
           sizeof(dc_marker));
    return 1;
}

/* jaguarMainRAM is a POINTER variable into jagMemSpace, so dlsym returns
 * the address of the pointer, not of the RAM (cf. cd_wedge_probe.c). */
static const uint8_t *main_ram(harness_config *c)
{
    uint8_t **ramp = (uint8_t **)harness_dlsym(c, "jaguarMainRAM");

    return ramp ? *ramp : NULL;
}

static int marker_in_mtmem(void)
{
    return memcmp(mt_mem + DC_MARKER_OFF, dc_marker, sizeof(dc_marker)) == 0;
}

static harness_result mkres(int ok, const char *name, const char *detail)
{
    harness_result r;
    r.status = ok ? "PASS" : "FAIL";
    r.name   = name;
    r.detail = detail;
    return r;
}

int main(int argc, char **argv)
{
    harness_config cfg = HARNESS_CONFIG_DEFAULT;
    struct retro_game_info gi;
    harness_result results[8];
    unsigned nres = 0;
    const char *disc_path = NULL;
    const char *disc_path_b = NULL;
    int case_num = 0;
    int i;
    int pass = 0;
    char before[64];

    for (i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--case") == 0 && i + 1 < argc)
            case_num = atoi(argv[i + 1]);
        else if (strcmp(argv[i], "--disc") == 0 && i + 1 < argc)
            disc_path = argv[i + 1];
        else if (strcmp(argv[i], "--disc-b") == 0 && i + 1 < argc)
            disc_path_b = argv[i + 1];
    }
    if (case_num != 1 && case_num != 3 && case_num != 4
        && case_num != 5 && case_num != 6 && case_num != 7
        && case_num != 8 && case_num != 9 && case_num != 10) {
        fprintf(stderr, "usage: test_disk_control <core> --case N "
                        "[--disc <image>] [--disc-b <image>] [--quiet]\n"
                        "  N is one of 1|3|4|5|6|7|8|9|10.  Cases 1, 3, 4, 7-10 "
                        "need --disc <image>; case 4 also needs\n"
                        "  --disc-b <image>.  Cases 5 and 6 (no-content "
                        "boot) take no disc.  Case 8 also wants\n"
                        "  --option virtualjaguar_memory_track=disabled.\n"
                        "  (case 2 needs a one-session audio disc; none "
                        "exists in the corpus)\n");
        return 1;
    }
    if (!disc_path && case_num != 5 && case_num != 6) {
        fprintf(stderr, "test_disk_control: no --disc given\n");
        return 1;
    }

    cfg.frames = 10;
    cfg.quiet  = 1;
    if (case_num == 5)
    {
        /* Long enough for the boot ROM to get past its first screen if it
         * is going to; short enough to stay a unit test. */
        cfg.frames         = 600;
        cfg.video_callback = vid_cb;
    }

    /* Must be set before the load: the core registers disk control during
     * retro_load_game, and the harness refuses the env call unless this
     * is on (matching the accept_audio_buf_cb convention). */
    cfg.accept_disk_control_cb = 1;

    if (!harness_init_from_args(&cfg, argc, argv))
        return 1;

    bootcfg = (struct dc_bootconfig *)harness_dlsym(&cfg, "bootConfig");
    if (!bootcfg) {
        fprintf(stderr, "test_disk_control: bootConfig not exported -- "
                        "rebuild with `make TEST_EXPORTS=1`\n");
        return 1;
    }

    lr_mem_size = (size_t (*)(unsigned))harness_dlsym(&cfg,
                                            "retro_get_memory_size");
    lr_mem_data = (void *(*)(unsigned))harness_dlsym(&cfg,
                                            "retro_get_memory_data");
    mt_mem      = (const uint8_t *)harness_dlsym(&cfg, "mtMem");
    if (!lr_mem_size || !lr_mem_data || !mt_mem) {
        fprintf(stderr, "test_disk_control: memory accessors / mtMem not "
                        "exported -- rebuild with `make TEST_EXPORTS=1`\n");
        return 1;
    }

    /* Case 9 (#747): the synthetic disc row must be in place before the
     * insert's titledb probe runs; installing it before the load is the
     * simplest way to guarantee that. */
    if (case_num == 9) {
        static TitleDBEntry row[1];
        void (*set_disc)(const TitleDBEntry *, int) =
            (void (*)(const TitleDBEntry *, int))
            harness_dlsym(&cfg, "TitleDBSetDiscRowsForTest");
        if (!set_disc) {
            fprintf(stderr, "test_disk_control: TitleDBSetDiscRowsForTest "
                            "not exported -- rebuild with `make TEST_EXPORTS=1`\n");
            return 1;
        }
        memset(row, 0, sizeof(row));
        row[0].crc32 = 0x82B88060u;            /* Baldies (Rev 1) boot stub */
        row[0].name  = "Synthetic disc row";
        row[0].pairs[0].key   = "virtualjaguar_cd_boot_mode";
        row[0].pairs[0].value = "bios";
        row[0].pairs[0].cls   = TITLEDB_CLASS_COMPATIBILITY;
        row[0].pairs[0].cite  = "synthetic test row (#747)";
        set_disc(row, 1);
    }

    if (!harness_load_no_content(&cfg)) {
        fprintf(stderr, "test_disk_control: no-content load failed\n");
        return 1;
    }

    memset(&gi, 0, sizeof(gi));

    switch (case_num) {
    case 1: {
        int registered, was_bare_bios, added, inserted, now_cd;

        registered = cfg.disk_cb_registered
                  && cfg.disk_add_image_index
                  && cfg.disk_replace_image_index
                  && cfg.disk_set_eject_state;
        /* No-content boot resolves to the CD BIOS (#726), so the strategy
         * NAME alone no longer separates before-insert from after: a
         * real-BIOS disc resolves to "bios" as well, and asserting
         * strategy_is_cd() on both sides would be vacuous.  isCDGame is
         * what actually moves -- false for the bare BIOS sitting on its
         * insert-disc screen, true once a disc is mounted -- so both
         * halves below pair the name with it. */
        was_bare_bios = strcmp(strategy_name(), "bios") == 0
                     && !bootcfg->isCDGame;

        gi.path = disc_path;
        added    = registered
                && cfg.disk_add_image_index()
                && cfg.disk_replace_image_index(0, &gi)
                && cfg.disk_set_eject_state(true);
        inserted = added && cfg.disk_set_eject_state(false);
        now_cd   = strategy_is_cd() && bootcfg->isCDGame;

        results[nres++] = mkres(registered, "case1_interface_registered",
            registered ? "core registered the disk control ext interface"
                       : "no disk control interface registered -- every "
                         "assertion below would be vacuous");
        results[nres++] = mkres(was_bare_bios, "case1_no_content_resolved",
            was_bare_bios ? "no-content boot came up in the CD BIOS with no "
                            "disc mounted"
                          : "no-content boot did not resolve to the CD BIOS "
                            "(#726: it must, or the insert-disc screen is "
                            "unreachable)");
        results[nres++] = mkres(inserted, "case1_insert_succeeded",
            inserted ? "set_eject_state(false) returned true"
                     : "insert was refused");
        results[nres++] = mkres(now_cd, "case1_boot_resolution_reran",
            now_cd ? "a disc is mounted and a CD strategy resolved -- "
                     "resolution re-ran"
                   : "isCDGame never went true -- the insert reset the "
                     "machine without re-resolving");
        pass = registered && was_bare_bios && inserted && now_cd;
        break;
    }
    case 9:
    case 10: {
        const char *want = (case_num == 9) ? "bios" : "hle";
        int inserted, ok;

        gi.path  = disc_path;
        inserted = cfg.disk_cb_registered
                && cfg.disk_add_image_index()
                && cfg.disk_replace_image_index(0, &gi)
                && cfg.disk_set_eject_state(true)
                && cfg.disk_set_eject_state(false);
        ok = inserted && bootcfg->isCDGame
          && strcmp(strategy_name(), want) == 0;
        results[nres++] = mkres(ok,
            case_num == 9 ? "case9_disc_row_applies_on_insert"
                          : "case10_insert_without_row_is_default",
            ok ? (case_num == 9 ? "insert resolved bios (disc row applied)"
                                : "insert resolved hle (no row)")
               : strategy_name());
        pass = ok;
        break;
    }
    case 4: {
        /* Serialize on disc A, mount disc B, assert the load is REFUSED --
         * and then assert the state still loads on its OWN disc.  That
         * second half is the control: a serializer that refused EVERY
         * state would satisfy the mismatch assertion on its own. */
        int saved, cross_refused, own_ok;
        const char *state_path = "/tmp/vj_disk_control_case4.state";

        if (!disc_path_b) {
            fprintf(stderr, "test_disk_control: case 4 needs --disc-b\n");
            return 77;   /* ledgered skip, not a pass */
        }

        gi.path = disc_path;
        if (!(cfg.disk_add_image_index()
              && cfg.disk_replace_image_index(0, &gi)
              && cfg.disk_set_eject_state(true)
              && cfg.disk_set_eject_state(false))) {
            fprintf(stderr, "test_disk_control: could not mount disc A "
                            "(damaged rip?) -- skipping\n");
            return 77;   /* corpus property, not a code failure */
        }
        saved = harness_save_state(&cfg, state_path);

        gi.path = disc_path_b;
        cfg.disk_set_eject_state(true);
        cfg.disk_replace_image_index(0, &gi);
        if (!cfg.disk_set_eject_state(false)) {
            fprintf(stderr, "test_disk_control: could not mount disc B "
                            "(damaged rip?) -- skipping\n");
            return 77;   /* several CDI V2 rips cannot boot at all */
        }
        cross_refused = !harness_load_state(&cfg, state_path);

        gi.path = disc_path;
        cfg.disk_set_eject_state(true);
        cfg.disk_replace_image_index(0, &gi);
        cfg.disk_set_eject_state(false);
        own_ok = harness_load_state(&cfg, state_path);

        results[nres++] = mkres(saved, "case4_state_saved",
            saved ? "state written while disc A was mounted"
                  : "could not write a state");
        results[nres++] = mkres(cross_refused, "case4_mismatch_refused",
            cross_refused ? "state from disc A refused against disc B"
                          : "A WRONG-DISC STATE WAS ACCEPTED");
        results[nres++] = mkres(own_ok, "case4_own_disc_roundtrips",
            own_ok ? "state still loads on the disc it was taken from"
                   : "state refused on its OWN disc -- the check is too "
                     "strict, not just strict");
        pass = saved && cross_refused && own_ok;
        break;
    }
    case 5: {
        /* No-content boot must RENDER, not just resolve (issue #726).
         *
         * Two assertions, and the second is the one that matters:
         *   - the boot ROM puts something on screen at all, and
         *   - the picture keeps CHANGING.
         *
         * A frozen red Jaguar logo satisfies "non-black" forever, so
         * non-black alone would be another vacuous check. Motion is what
         * separates "booting" from "wedged on the logo".
         *
         * Deliberately no assertion about WHICH screen: this test should
         * survive a future change to what a bare console shows. */
        int rendered, moving;

        harness_run(&cfg);

        rendered = (vid_frames > 0 && vid_nonblack > 0 && !vid_bad_pitch);
        /* >=2 distinct images over the window. Generous on purpose: the
         * bar is "not frozen", not "animates smoothly". */
        moving   = (vid_changes >= 2);

        results[nres++] = mkres(rendered, "case5_no_content_renders",
            rendered ? "bare-console boot put a non-black image on screen"
                     : (vid_bad_pitch
                        ? "frame was not XRGB8888 (pitch < w*4) -- this test "
                          "reads rows as uint32_t and cannot score it"
                        : "bare-console boot rendered nothing (or all black)"));
        results[nres++] = mkres(moving, "case5_no_content_progresses",
            moving ? "the picture changes -- the machine is running"
                   : "the picture NEVER CHANGES -- wedged on the boot screen "
                     "(this is issue #726: strategy resolves, machine does "
                     "not run)");

        fprintf(stderr, "  [case5] frames=%u nonblack=%u changes=%u\n",
                vid_frames, vid_nonblack, vid_changes);

        pass = rendered && moving;
        break;
    }
    case 6: {
        /* #810: the buffer must be exposed from the FIRST query, because a
         * frontend sizes and loads it exactly once, right after the load.
         * (Memory Track enabled is the default; it is plugged in from frame
         * 0 on this path, see the no-content branch of retro_load_game.) */
        int sized, seeded, landed, stable;
        size_t size_at_load = lr_mem_size(RETRO_MEMORY_SAVE_RAM);
        void  *ptr_at_load  = lr_mem_data(RETRO_MEMORY_SAVE_RAM);

        sized  = size_at_load == DC_CD_SAVE_SIZE && ptr_at_load != NULL;
        seeded = sized && seed_srm_marker();
        harness_step(&cfg);                 /* first frame unpacks the .srm */
        landed = seeded && marker_in_mtmem();
        harness_step(&cfg);
        stable = lr_mem_data(RETRO_MEMORY_SAVE_RAM) == ptr_at_load
              && lr_mem_size(RETRO_MEMORY_SAVE_RAM) == DC_CD_SAVE_SIZE;

        results[nres++] = mkres(sized, "case6_save_ram_exposed_at_load",
            sized ? "no-content boot reports CD_SAVE_SIZE and a buffer at "
                    "load time"
                  : "SAVE_RAM is size 0 / NULL on a no-content boot with "
                    "the Memory Track plugged in (#810)");
        results[nres++] = mkres(landed, "case6_srm_load_reaches_mtmem",
            landed ? "data the frontend wrote into SAVE_RAM reached the "
                     "Memory Track"
                   : "frontend-loaded save data never reached mtMem");
        results[nres++] = mkres(stable, "case6_buffer_stable",
            stable ? "pointer and size unchanged across frames"
                   : "pointer or size moved under the frontend");
        pass = sized && seeded && landed && stable;
        break;
    }
    case 7: {
        int sized, seeded, inserted, same, kept, nvm, nvm0, i;
        const uint8_t *ram;
        void *ptr_before = lr_mem_data(RETRO_MEMORY_SAVE_RAM);

        sized  = lr_mem_size(RETRO_MEMORY_SAVE_RAM) == DC_CD_SAVE_SIZE
              && ptr_before != NULL;
        seeded = sized && seed_srm_marker();
        /* Control for the cookie probe below: it must be present straight
         * after the no-content load, or "missing after the insert" would
         * only mean the probe is looking at the wrong address. */
        ram    = main_ram(&cfg);
        nvm0   = ram && memcmp(ram + DC_NVM_COOKIE_RAM, "_NVM", 4) == 0;
        harness_step(&cfg);                 /* first frame unpacks the .srm */

        gi.path  = disc_path;
        inserted = cfg.disk_cb_registered
                && cfg.disk_add_image_index()
                && cfg.disk_replace_image_index(0, &gi)
                && cfg.disk_set_eject_state(true)
                && cfg.disk_set_eject_state(false);
        /* Sampled BEFORE any frame runs: the boot has just reset the machine,
         * so nothing the game does can have touched $2400 yet, and a missing
         * cookie here is the insert path's doing, not the game's. */
        ram  = main_ram(&cfg);
        nvm  = ram && memcmp(ram + DC_NVM_COOKIE_RAM, "_NVM", 4) == 0;
        for (i = 0; i < 30; i++)
            harness_step(&cfg);

        same = lr_mem_size(RETRO_MEMORY_SAVE_RAM) == DC_CD_SAVE_SIZE
            && lr_mem_data(RETRO_MEMORY_SAVE_RAM) == ptr_before;
        kept = seeded && marker_in_mtmem();

        results[nres++] = mkres(seeded, "case7_exposed_before_insert",
            seeded ? "SAVE_RAM exposed and seeded before the insert"
                   : "SAVE_RAM not exposed before the insert");
        results[nres++] = mkres(inserted, "case7_insert_succeeded",
            inserted ? "disc inserted" : "insert was refused");
        results[nres++] = mkres(same, "case7_save_ram_after_insert",
            same ? "size and pointer unchanged by the insert"
                 : "SAVE_RAM size/pointer changed or vanished after the "
                   "insert (#810)");
        results[nres++] = mkres(kept, "case7_memory_track_survives_insert",
            kept ? "Memory Track contents survived the insert reboot"
                 : "the insert reboot wiped the Memory Track -- the next "
                   "frontend save would write it back blank");
        results[nres++] = mkres(nvm0, "case7_nvm_module_at_load",
            nvm0 ? "'_NVM' cookie present after the no-content load"
                 : "'_NVM' cookie missing straight after the no-content "
                   "load -- the probe address is wrong");
        results[nres++] = mkres(nvm, "case7_nvm_module_present",
            nvm ? "'_NVM' cookie present after the insert reboot"
                : "'_NVM' cookie missing after the insert reboot -- games "
                  "launched by insert would not see the Memory Track");
        pass = seeded && nvm0 && inserted && same && kept && nvm;
        break;
    }
    case 8: {
        /* Memory Track disabled: a bare no-content session has nothing to
         * persist and keeps reporting 0 (the pre-#810 contract).  Mounting a
         * disc must NOT flip that mid-session -- see the header comment. */
        int bare_zero, inserted, still_zero;

        bare_zero = lr_mem_size(RETRO_MEMORY_SAVE_RAM) == 0
                 && lr_mem_data(RETRO_MEMORY_SAVE_RAM) == NULL;

        gi.path  = disc_path;
        inserted = cfg.disk_cb_registered
                && cfg.disk_add_image_index()
                && cfg.disk_replace_image_index(0, &gi)
                && cfg.disk_set_eject_state(true)
                && cfg.disk_set_eject_state(false);
        harness_step(&cfg);
        still_zero = lr_mem_size(RETRO_MEMORY_SAVE_RAM) == 0
                  && lr_mem_data(RETRO_MEMORY_SAVE_RAM) == NULL;

        results[nres++] = mkres(bare_zero, "case8_bare_session_has_no_save_ram",
            bare_zero ? "no Memory Track, no disc: SAVE_RAM is 0/NULL"
                      : "bare session exposes SAVE_RAM with nothing to save "
                        "(is the memory_track option actually disabled?)");
        results[nres++] = mkres(inserted, "case8_insert_succeeded",
            inserted ? "disc inserted" : "insert was refused");
        results[nres++] = mkres(still_zero, "case8_save_ram_stays_unexposed",
            still_zero ? "SAVE_RAM still 0/NULL after the insert (no write "
                         "without a load)"
                       : "SAVE_RAM appeared mid-session: the frontend never "
                         "loaded the .srm into it, so its exit save would "
                         "clobber the previous session's file");
        pass = bare_zero && inserted && still_zero;
        break;
    }
    case 3: {
        int added, refused, still_ejected, unchanged, still_runs;

        strncpy(before, strategy_name(), sizeof(before) - 1);
        before[sizeof(before) - 1] = '\0';

        gi.path = "/nonexistent/definitely-not-a-disc.cue";
        added   = cfg.disk_cb_registered
               && cfg.disk_add_image_index()
               && cfg.disk_replace_image_index(0, &gi)
               && cfg.disk_set_eject_state(true);

        refused       = added && !cfg.disk_set_eject_state(false);
        still_ejected = cfg.disk_get_eject_state
                     && cfg.disk_get_eject_state();
        unchanged     = strcmp(before, strategy_name()) == 0;

        harness_step(&cfg);
        still_runs = 1;   /* reaching here without a crash is the check */

        results[nres++] = mkres(refused, "case3_bad_insert_refused",
            refused ? "set_eject_state(false) returned false"
                    : "a broken image was accepted");
        results[nres++] = mkres(still_ejected, "case3_tray_stays_open",
            still_ejected ? "get_eject_state() still true after failure"
                          : "tray reported closed after a failed insert");
        results[nres++] = mkres(unchanged, "case3_strategy_unchanged",
            unchanged ? "resolved strategy untouched by the failed insert"
                      : "failed insert changed the resolved strategy -- "
                        "the machine is now half-configured");
        results[nres++] = mkres(still_runs, "case3_machine_still_runs",
            "core still stepped a frame after the failed insert");
        pass = refused && still_ejected && unchanged && still_runs;
        break;
    }
    default:
        return 1;
    }

    harness_report(&cfg, results, nres);
    harness_shutdown(&cfg);
    return pass ? 0 : 1;
}
