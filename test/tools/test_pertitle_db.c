/*
 * test/tools/test_pertitle_db.c
 *
 * E2E behaviour test for the per-title enhancement defaults DB (issue
 * #368): apply / disable / user-override contract, driven through the
 * real dlopen'd core via the shared harness (test/harness/harness.h).
 *
 * One process per --case: the internal-resolution factor (shadowHiresN)
 * is fixed for the whole session at retro_load_game time, so each case
 * needs a fresh core load, exactly like a real frontend restart.
 *
 * Cases (see the Makefile `test:` target for the exact invocations):
 *
 *   1  AvP (CRC 0xDC187F82), default options -- the DB applies:
 *      shadowHiresN == 2, and the core logs a [titledb] SUBSTITUTION line
 *      (keyed on the "(option at default)" marker, not the bare [titledb]
 *      tag -- the per-title-DB-miss line added for CRC-unlisted content
 *      also carries the [titledb] prefix, so the marker is what actually
 *      proves the substitution happened, not just that both options
 *      independently defaulted to the DB's values).
 *   2  AvP, virtualjaguar_pertitle_defaults=disabled -- stock: shadowHiresN
 *      == 1 (disabling the gate must restore stock behaviour exactly).
 *   3  AvP, defaults disabled AND virtualjaguar_internal_resolution=2x set
 *      explicitly -- manual choice still works with the DB off:
 *      shadowHiresN == 2.
 *   4  AvP, virtualjaguar_true_color=disabled (== its own registered
 *      default) with the DB on -- a default-VALUED option is
 *      indistinguishable from an untouched one by design (see CLAUDE.md's
 *      "Known limitation to document, not fix"), so the DB still
 *      substitutes "enabled": shadowHiresN == 2 AND shadowFBActive != 0.
 *      Driven by the SYNTHETIC row below, not AvP's shipped one (#590).
 *   5  Non-DB ROM (test/roms/yarc.j64) -- no CRC match, so no [titledb]
 *      SUBSTITUTION line is logged (no "(option at default)" marker), but
 *      the [titledb] MISS line ("no per-title entry for CRC32 ...") is
 *      logged instead; shadowHiresN == 1.
 *   6  AvP, then retro_unload_game() in the same process -- the titledb
 *      cache clears immediately: TitleDBTitleName() is non-NULL after load
 *      and NULL after unload, so a later option read cannot see stale
 *      per-title overrides from the previous content.
 *   7  AvP, default options, PLUS a programmatically-installed negative
 *      entry (issue #464) flagging virtualjaguar_true_color=enabled --
 *      the exact value AvP's own positive row would substitute at
 *      default. The substitution must be REFUSED (shadowFBActive == 0,
 *      stock value kept) while the unrelated internal_resolution key is
 *      unaffected (shadowHiresN == 2), and a [titledb] warning names the
 *      refusal. Installed via TitleDBSetNegativeForTest(), the same
 *      "no canary row in the shipped table" reasoning test_hook_gate uses
 *      for hooks[] -- it would otherwise make AvP's real DB row unsafe
 *      for every user, not just this test process.  The POSITIVE row it
 *      refuses is synthetic too, see below.
 *   8  AvP, virtualjaguar_true_color=enabled set EXPLICITLY by the user,
 *      PLUS the same negative entry as case 7 -- the user's choice must
 *      still be HONOURED (shadowFBActive != 0) because a negative entry
 *      may refuse a per-title DEFAULT but must never override an
 *      explicit user choice; a [titledb] warning is still logged so a bug
 *      report against this title starts from the right hypothesis.
 *
 * Enhancement profile cases (P9, docs/perf-audit-2026-08.md):
 *
 *   9  AvP, virtualjaguar_enhancement_profile=performance -- the DB's
 *      enhancement defaults are NOT applied: shadowHiresN == 1, a [perf]
 *      "not applying" line is logged, and no "(option at default)"
 *      substitution line appears.
 *  10  AvP, profile=performance PLUS virtualjaguar_internal_resolution=2x
 *      set EXPLICITLY -- the user's own choice beats the profile:
 *      shadowHiresN == 2 (the profile governs DB defaults only).
 *  11  AvP, virtualjaguar_enhancement_profile=quality -- the DB defaults
 *      apply exactly as before the profile existed: shadowHiresN == 2
 *      with the substitution line logged.  (Case 1, which passes no
 *      profile option, doubles as the 'auto'-on-a-capable-host arm: the
 *      registered default is auto and this host is not 32-bit ARM.)
 *  12  AvP, default options (profile=auto), with the harness accepting
 *      the core's RETRO_ENVIRONMENT_SET_AUDIO_BUFFER_STATUS_CALLBACK
 *      registration -- the runtime demotion: the DB's 2x applies at load,
 *      then the test feeds "underrun likely" reports the way a struggling
 *      frontend would; after the demotion threshold the core must drop to
 *      shadowHiresN == 1 mid-session, log the [perf] demotion warning
 *      naming the measured overrun, and keep running.
 *
 * The [titledb] substitution/miss lines are logged at RETRO_LOG_INFO via
 * LOG_INF(), which the harness's cb_log filters out below RETRO_LOG_WARN
 * unless VJ_HARNESS_LOG_INFO=1 is set (see harness.c) -- this test sets it
 * before the core can log anything, then redirects stderr to a temp file
 * for the duration of the core load so the captured text can be grepped
 * afterward. The known-bad lines (cases 7/8) are LOG_WRN and are captured
 * either way -- same reasoning as test_hook_gate.c's apply/refuse pair.
 *
 * Build:  cc -O2 -Wall -std=c99 $(INCFLAGS) -o test/tools/test_pertitle_db \
 *           test/tools/test_pertitle_db.c test/harness/harness.c -ldl -lm
 * Run:    test_pertitle_db [core] <rom> --case N [--option KEY=VALUE ...]
 */

#define _DEFAULT_SOURCE 1

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "../harness/harness.h"
#include "../../src/core/titledb.h"

/* Cases 7/8 (issue #464): a fixed negative row flagging the true_color
 * positive default as known-bad, installed programmatically so the shipped
 * table stays clean (see the case-7 header comment). */
static TitleDBNegativePair negative_true_color[2];

/* Cases 4/7 (issue #590): the POSITIVE row those two cases assert against,
 * installed programmatically instead of read out of the shipped table.
 *
 * Both cases used to lean on Alien vs Predator's real row carrying a
 * virtualjaguar_true_color pair.  #551 removed that pair after a pixel-diffed
 * A/B found it changed nothing for AvP, and both cases have been failing on
 * clean develop ever since -- the fixture was asserting a fact about the
 * shipped table, not the behaviour it claims to test.  A synthetic row keeps
 * the contract (a default-VALUED option is still substituted; a negative
 * entry refuses that substitution while an unrelated key is untouched) true
 * no matter how the shipped table evolves.
 *
 * It must carry BOTH keys: TitleDBSetPairsForTest() replaces the table
 * lookup outright, so internal_resolution would otherwise read as "no
 * per-title entry" and case 7's unrelated-key assertion would fail.
 * true_color stays the substituted key so the Makefile's existing
 * `--option virtualjaguar_true_color=disabled` on case 4 is still the
 * default-valued option under test. */
static TitleDBPair synthetic_pairs[3];

/* ----------------------------------------------------------------
 * stderr capture: redirect around the core load so the [titledb]
 * LOG_INF line can be grepped back out afterward.  Restored before any
 * further diagnostic output so a failing assertion still prints normally.
 * ---------------------------------------------------------------- */

static char log_path[] = "/tmp/vj_pertitle_db_log_XXXXXX";
static int  log_path_valid = 0;
static int  saved_stderr_fd = -1;

static void log_capture_begin(void)
{
    /* Redirect stderr onto the mkstemp() fd directly -- no close() +
     * reopen-by-path, so there is no TOCTOU window on log_path.  The
     * path is kept only to read the log back and unlink it. */
    int fd = mkstemp(log_path);

    log_path_valid = 0;
    if (fd < 0)
        return;

    fflush(stderr);
    saved_stderr_fd = dup(fileno(stderr));
    if (saved_stderr_fd < 0) {
        close(fd);
        unlink(log_path);
        return;
    }
    if (dup2(fd, fileno(stderr)) < 0) {
        close(fd);
        close(saved_stderr_fd);
        saved_stderr_fd = -1;
        unlink(log_path);
        return;
    }
    close(fd);
    log_path_valid = 1;
}

static void log_capture_end(void)
{
    fflush(stderr);
    if (saved_stderr_fd >= 0) {
        dup2(saved_stderr_fd, fileno(stderr));
        close(saved_stderr_fd);
        saved_stderr_fd = -1;
        clearerr(stderr);
    }
}

static int log_contains(const char *needle)
{
    FILE *f;
    char line[1024];
    int found = 0;

    if (!log_path_valid)
        return 0;
    f = fopen(log_path, "r");
    if (!f)
        return 0;
    while (fgets(line, sizeof(line), f)) {
        if (strstr(line, needle)) {
            found = 1;
            break;
        }
    }
    fclose(f);
    return found;
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
    int case_num = 0;
    int i;
    int *hires_n_ptr;
    int *shadow_active_ptr;
    const char *(*title_name_fn)(void);
    void (*retro_unload_game_fn)(void);
    harness_result results[6];
    unsigned nres = 0;
    int pass;
    int did_manual_unload = 0;

    /* Pre-parse --case: harness_init_from_args skips unknown flags one
     * token at a time (see its comment "Unknown flag -- skip"), and the
     * numeric value that follows falls into the positional-arg branches --
     * harmlessly ignored there because core_path/rom_path are already set
     * by the earlier positional args every test tool invocation uses
     * (core, then rom, then flags). Same convention as cd_visual_verify.c's
     * --outdir/--shot-every pre-parse. */
    for (i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--case") == 0 && i + 1 < argc)
            case_num = atoi(argv[i + 1]);
    }
    if (case_num < 1 || case_num > 12) {
        fprintf(stderr,
                "usage: test_pertitle_db [core] <rom> --case N[1-12] "
                "[--option KEY=VALUE ...]\n");
        return 1;
    }

    /* Must be set before the core's first LOG_INF call (harness's cb_log
     * latches its level filter on first use). */
    setenv("VJ_HARNESS_LOG_INFO", "1", 1);

    cfg.frames = 30;
    cfg.quiet  = 1;

    if (!harness_init_from_args(&cfg, argc, argv))
        return 1;
    if (!cfg.rom_path) {
        fprintf(stderr, "test_pertitle_db: no ROM path given\n");
        return 1;
    }

    /* Cases 4/7 (issue #590): install the synthetic positive row BEFORE
     * harness_load_rom(), same ordering reason as the negative row below. */
    if (case_num == 4 || case_num == 7) {
        void (*set_pairs)(const TitleDBPair *, int);

        set_pairs = (void (*)(const TitleDBPair *, int))
            harness_dlsym(&cfg, "TitleDBSetPairsForTest");
        if (!set_pairs) {
            fprintf(stderr, "test_pertitle_db: TitleDBSetPairsForTest not "
                            "exported -- rebuild with `make TEST_EXPORTS=1`\n");
            return 1;
        }
        synthetic_pairs[0].key   = "virtualjaguar_internal_resolution";
        synthetic_pairs[0].value = "2x";
        synthetic_pairs[1].key   = "virtualjaguar_true_color";
        synthetic_pairs[1].value = "enabled";
        synthetic_pairs[2].key   = NULL;
        synthetic_pairs[2].value = NULL;
        set_pairs(synthetic_pairs, 2);
    }

    /* Cases 7/8 (issue #464): install the negative row BEFORE
     * harness_load_rom(), which is where retro_load_game() -> check_
     * variables() -> get_variable_pertitle() runs and consults it --
     * mirrors test_hook_gate.c's ordering for TitleDBSetHooksForTest(). */
    if (case_num == 7 || case_num == 8) {
        void (*set_negative)(const TitleDBNegativePair *, int);

        set_negative = (void (*)(const TitleDBNegativePair *, int))
            harness_dlsym(&cfg, "TitleDBSetNegativeForTest");
        if (!set_negative) {
            fprintf(stderr, "test_pertitle_db: TitleDBSetNegativeForTest not "
                            "exported -- rebuild with `make TEST_EXPORTS=1`\n");
            return 1;
        }
        negative_true_color[0].key   = "virtualjaguar_true_color";
        negative_true_color[0].value = "enabled";
        negative_true_color[1].key   = NULL;
        negative_true_color[1].value = NULL;
        set_negative(negative_true_color, 1);
    }

    /* Case 12 (P9): accept the core's audio-buffer status callback
     * registration so the test can feed synthetic underrun reports, and
     * keep the stderr capture open across the feeding loop below -- the
     * [perf] demotion warning is logged mid-run, not at load. */
    if (case_num == 12)
        cfg.accept_audio_buf_cb = 1;

    log_capture_begin();
    if (!harness_load_rom(&cfg)) {
        log_capture_end();
        fprintf(stderr, "test_pertitle_db: harness_load_rom failed for '%s'\n",
                cfg.rom_path);
        return 1;
    }
    if (case_num != 12)
        log_capture_end();

    harness_run(&cfg);

    hires_n_ptr       = (int *)harness_dlsym(&cfg, "shadowHiresN");
    shadow_active_ptr = (int *)harness_dlsym(&cfg, "shadowFBActive");
    if (!hires_n_ptr || !shadow_active_ptr) {
        log_capture_end();   /* still open for case 12 */
        fprintf(stderr,
                "test_pertitle_db: missing shadowHiresN/shadowFBActive "
                "exports -- rebuild with `make TEST_EXPORTS=1`\n");
        harness_shutdown(&cfg);
        return 1;
    }
    title_name_fn = (const char *(*)(void))harness_dlsym(&cfg, "TitleDBTitleName");
    retro_unload_game_fn = (void (*)(void))harness_dlsym(&cfg, "retro_unload_game");

    switch (case_num) {
    case 1: {
        int hires_ok  = (*hires_n_ptr == 2);
        /* Key on the substitution marker, not the bare [titledb] tag --
         * the per-title-DB-miss line (added for CRC-unlisted content) also
         * carries the [titledb] prefix, so the bare tag no longer
         * distinguishes "a substitution happened" from "a miss was
         * logged". */
        int logged_ok = log_contains("(option at default)");
        results[nres++] = mkres(hires_ok, "case1_hires_db_applied",
            hires_ok ? "shadowHiresN == 2 (DB applied at default)"
                     : "shadowHiresN != 2");
        results[nres++] = mkres(logged_ok, "case1_titledb_log_present",
            logged_ok ? "[titledb] substitution logged "
                        "(option at default)"
                      : "no [titledb] substitution log line found");
        pass = hires_ok && logged_ok;
        break;
    }
    case 2: {
        int hires_ok = (*hires_n_ptr == 1);
        int tc_ok    = (*shadow_active_ptr == 0);
        results[nres++] = mkres(hires_ok, "case2_gate_disabled_is_stock_hires",
            hires_ok ? "shadowHiresN == 1 (defaults disabled -> stock)"
                     : "shadowHiresN != 1");
        results[nres++] = mkres(tc_ok, "case2_gate_disabled_is_stock_truecolor",
            tc_ok ? "shadowFBActive == 0 (defaults disabled -> stock)"
                  : "shadowFBActive != 0");
        pass = hires_ok && tc_ok;
        break;
    }
    case 3: {
        int hires_ok = (*hires_n_ptr == 2);
        results[nres++] = mkres(hires_ok, "case3_manual_choice_with_db_off",
            hires_ok ? "shadowHiresN == 2 (explicit user choice honored)"
                     : "shadowHiresN != 2");
        pass = hires_ok;
        break;
    }
    case 4: {
        int hires_ok = (*hires_n_ptr == 2);
        int tc_ok     = (*shadow_active_ptr != 0);
        results[nres++] = mkres(hires_ok, "case4_hires_still_applied",
            hires_ok ? "shadowHiresN == 2" : "shadowHiresN != 2");
        results[nres++] = mkres(tc_ok, "case4_default_valued_option_substituted",
            tc_ok ? "shadowFBActive != 0 (true_color=disabled == default, "
                    "DB substituted enabled)"
                  : "shadowFBActive == 0");
        pass = hires_ok && tc_ok;
        break;
    }
    case 5: {
        int hires_ok    = (*hires_n_ptr == 1);
        /* No substitution should have been logged for this CRC. */
        int no_sub_log  = !log_contains("(option at default)");
        /* The Task 2 miss line SHOULD be logged -- it also carries the
         * [titledb] prefix, which is exactly why case1/case5 can no longer
         * key on the bare tag. */
        int miss_logged = log_contains("no per-title entry");
        results[nres++] = mkres(hires_ok, "case5_nondb_rom_stock",
            hires_ok ? "shadowHiresN == 1 (no DB entry for this CRC)"
                     : "shadowHiresN != 1");
        results[nres++] = mkres(no_sub_log, "case5_no_substitution_log",
            no_sub_log ? "no [titledb] substitution log line "
                         "(correctly no match)"
                       : "unexpected [titledb] substitution log line for "
                         "a non-DB ROM");
        results[nres++] = mkres(miss_logged, "case5_miss_logged",
            miss_logged ? "[titledb] miss line logged for unlisted CRC"
                        : "no [titledb] miss line found for unlisted CRC");
        pass = hires_ok && no_sub_log && miss_logged;
        break;
    }
    case 6: {
        const char *title_before = title_name_fn ? title_name_fn() : NULL;
        int had_title = (title_before != NULL);
        if (retro_unload_game_fn) {
            retro_unload_game_fn();
            did_manual_unload = 1;
        }
        results[nres++] = mkres(had_title, "case6_title_cached_after_load",
            had_title ? "TitleDBTitleName() non-NULL after matched load"
                      : "TitleDBTitleName() was NULL after matched load");
        results[nres++] = mkres(title_name_fn && retro_unload_game_fn
                                && title_name_fn() == NULL,
            "case6_unload_clears_titledb",
            (title_name_fn && retro_unload_game_fn && title_name_fn() == NULL)
                ? "retro_unload_game() cleared titledb state"
                : "retro_unload_game() left stale titledb state");
        pass = had_title && title_name_fn && retro_unload_game_fn
            && title_name_fn() == NULL;
        break;
    }
    case 7: {
        /* Default substitution refused: true_color stays OFF (the negative
         * row wins over the positive one for this key), internal_resolution
         * is untouched (a different key, still substituted normally). */
        int tc_refused  = (*shadow_active_ptr == 0);
        int hires_ok    = (*hires_n_ptr == 2);
        int warn_logged = log_contains("true_color=enabled is known-bad")
                        && log_contains("refusing the per-title default");
        results[nres++] = mkres(tc_refused, "case7_unsafe_default_refused",
            tc_refused ? "shadowFBActive == 0 (known-bad default refused)"
                       : "shadowFBActive != 0 (unsafe default was applied!)");
        results[nres++] = mkres(hires_ok, "case7_unrelated_key_unaffected",
            hires_ok ? "shadowHiresN == 2 (internal_resolution still applied)"
                     : "shadowHiresN != 2");
        results[nres++] = mkres(warn_logged, "case7_refusal_logged",
            warn_logged ? "[titledb] refusal warning logged"
                        : "no [titledb] refusal warning found");
        pass = tc_refused && hires_ok && warn_logged;
        break;
    }
    case 8: {
        /* Explicit user choice honoured despite the same negative row. */
        int tc_honored  = (*shadow_active_ptr != 0);
        int warn_logged = log_contains("true_color=enabled is known-bad")
                        && log_contains("explicit user choice honored");
        results[nres++] = mkres(tc_honored, "case8_user_choice_honored",
            tc_honored ? "shadowFBActive != 0 (explicit user value honored)"
                       : "shadowFBActive == 0 (user's explicit choice was "
                         "overridden!)");
        results[nres++] = mkres(warn_logged, "case8_warning_logged",
            warn_logged ? "[titledb] known-bad warning logged"
                        : "no [titledb] known-bad warning found");
        pass = tc_honored && warn_logged;
        break;
    }
    case 9: {
        /* performance profile: the DB's enhancement defaults must not
         * apply -- exactly as if the title had no row. */
        int hires_ok   = (*hires_n_ptr == 1);
        int perf_log   = log_contains("not applying");
        int no_sub_log = !log_contains("(option at default)");
        results[nres++] = mkres(hires_ok, "case9_performance_suppresses_db",
            hires_ok ? "shadowHiresN == 1 (DB default suppressed)"
                     : "shadowHiresN != 1 (DB default applied despite "
                       "performance profile!)");
        results[nres++] = mkres(perf_log, "case9_perf_log_present",
            perf_log ? "[perf] suppression line logged"
                     : "no [perf] suppression line found");
        results[nres++] = mkres(no_sub_log, "case9_no_substitution_log",
            no_sub_log ? "no [titledb] substitution line (correctly none)"
                       : "unexpected [titledb] substitution line under "
                         "performance profile");
        pass = hires_ok && perf_log && no_sub_log;
        break;
    }
    case 10: {
        /* performance profile + EXPLICIT internal_resolution=2x: the
         * user's own choice is out of the profile's reach. */
        int hires_ok = (*hires_n_ptr == 2);
        results[nres++] = mkres(hires_ok, "case10_explicit_beats_profile",
            hires_ok ? "shadowHiresN == 2 (explicit user choice honored)"
                     : "shadowHiresN != 2 (profile overrode an explicit "
                       "user choice!)");
        pass = hires_ok;
        break;
    }
    case 11: {
        /* quality profile: pre-profile behaviour, DB defaults apply. */
        int hires_ok  = (*hires_n_ptr == 2);
        int logged_ok = log_contains("(option at default)");
        results[nres++] = mkres(hires_ok, "case11_quality_applies_db",
            hires_ok ? "shadowHiresN == 2 (DB applied under quality)"
                     : "shadowHiresN != 2");
        results[nres++] = mkres(logged_ok, "case11_titledb_log_present",
            logged_ok ? "[titledb] substitution logged (option at default)"
                      : "no [titledb] substitution log line found");
        pass = hires_ok && logged_ok;
        break;
    }
    case 12: {
        /* Runtime demotion (profile=auto): the DB's 2x applied at load;
         * now feed "underrun likely" once per frame the way a struggling
         * frontend would, past the core's consecutive-frame threshold
         * (180), and the core must drop to 1x mid-session and say why.
         * The loop keeps stepping past the demotion (the core unregisters
         * the callback once the watch is spent -- the harness then holds
         * NULL) to prove the session keeps running at 1x. */
        int hires_before = *hires_n_ptr;
        int cb_ok        = (cfg.audio_buf_cb != NULL);
        int hires_after;
        int demote_logged;
        int j;

        for (j = 0; j < 250 && !cfg.stop_requested; j++) {
            if (cfg.audio_buf_cb)
                cfg.audio_buf_cb(true, 0, true);
            harness_step(&cfg);
        }
        log_capture_end();

        hires_after   = *hires_n_ptr;
        demote_logged = log_contains("enhancement profile 'auto'")
                     && log_contains("measured frame-budget overrun");
        results[nres++] = mkres(hires_before == 2, "case12_db_applied_at_load",
            hires_before == 2 ? "shadowHiresN == 2 before the underrun feed"
                              : "shadowHiresN != 2 at load (nothing to demote)");
        results[nres++] = mkres(cb_ok, "case12_buffer_cb_registered",
            cb_ok ? "core registered the audio-buffer status callback"
                  : "core never registered the audio-buffer status callback");
        results[nres++] = mkres(hires_after == 1, "case12_runtime_demotion",
            hires_after == 1 ? "shadowHiresN == 1 after sustained underrun"
                             : "shadowHiresN != 1 (no runtime demotion)");
        results[nres++] = mkres(demote_logged, "case12_demotion_logged",
            demote_logged ? "[perf] demotion warning logged (measured overrun)"
                          : "no [perf] demotion warning found");
        pass = (hires_before == 2) && cb_ok && (hires_after == 1)
            && demote_logged;
        break;
    }
    default:
        pass = 0;
        break;
    }

    harness_report(&cfg, results, nres);
    if (!did_manual_unload)
        harness_shutdown(&cfg);
    if (log_path_valid)
        unlink(log_path);

    return pass ? 0 : 1;
}
