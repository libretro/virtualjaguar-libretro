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
 * Run: test_disk_control <core> --disc <image> --case N [--quiet]
 */

#define _DEFAULT_SOURCE 1

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "../harness/harness.h"
#include "../../libretro-common/include/libretro.h"

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
    harness_result results[6];
    unsigned nres = 0;
    const char *disc_path = NULL;
    int case_num = 0;
    int i;
    int pass = 0;
    char before[64];

    for (i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--case") == 0 && i + 1 < argc)
            case_num = atoi(argv[i + 1]);
        else if (strcmp(argv[i], "--disc") == 0 && i + 1 < argc)
            disc_path = argv[i + 1];
    }
    if (case_num != 1 && case_num != 3) {
        fprintf(stderr, "usage: test_disk_control <core> --disc <image> "
                        "--case N[1|3] [--quiet]\n"
                        "  (case 2 needs a one-session audio disc; none "
                        "exists in the corpus)\n");
        return 1;
    }
    if (!disc_path) {
        fprintf(stderr, "test_disk_control: no --disc given\n");
        return 1;
    }

    cfg.frames = 10;
    cfg.quiet  = 1;

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

    if (!harness_load_no_content(&cfg)) {
        fprintf(stderr, "test_disk_control: no-content load failed\n");
        return 1;
    }

    memset(&gi, 0, sizeof(gi));

    switch (case_num) {
    case 1: {
        int registered, was_none, added, inserted, now_cd;

        registered = cfg.disk_cb_registered
                  && cfg.disk_add_image_index
                  && cfg.disk_replace_image_index
                  && cfg.disk_set_eject_state;
        was_none   = strcmp(strategy_name(), "none") == 0;

        gi.path = disc_path;
        added    = registered
                && cfg.disk_add_image_index()
                && cfg.disk_replace_image_index(0, &gi)
                && cfg.disk_set_eject_state(true);
        inserted = added && cfg.disk_set_eject_state(false);
        now_cd   = strategy_is_cd();

        results[nres++] = mkres(registered, "case1_interface_registered",
            registered ? "core registered the disk control ext interface"
                       : "no disk control interface registered -- every "
                         "assertion below would be vacuous");
        results[nres++] = mkres(was_none, "case1_no_content_resolved",
            was_none ? "strategy was \"none\" before the insert"
                     : "strategy was not \"none\" at no-content boot");
        results[nres++] = mkres(inserted, "case1_insert_succeeded",
            inserted ? "set_eject_state(false) returned true"
                     : "insert was refused");
        results[nres++] = mkres(now_cd, "case1_boot_resolution_reran",
            now_cd ? "strategy is now a CD strategy -- resolution re-ran"
                   : "strategy did NOT move off \"none\" -- the insert "
                     "reset the machine without re-resolving");
        pass = registered && was_none && inserted && now_cd;
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
