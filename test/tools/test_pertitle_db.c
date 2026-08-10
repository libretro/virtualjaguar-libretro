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
 *      shadowHiresN == 2, and the core logs a [titledb] line (proves the
 *      substitution actually happened, not just that both options
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
 *   5  Non-DB ROM (test/roms/yarc.j64) -- no CRC match, so no
 *      substitution and no [titledb] log line; shadowHiresN == 1.
 *
 * The [titledb] line is logged at RETRO_LOG_INFO via LOG_INF(), which the
 * harness's cb_log filters out below RETRO_LOG_WARN unless
 * VJ_HARNESS_LOG_INFO=1 is set (see harness.c) -- this test sets it before
 * the core can log anything, then redirects stderr to a temp file for the
 * duration of the core load so the captured text can be grepped afterward.
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
    FILE *capture;
    int fd = mkstemp(log_path);

    log_path_valid = 0;
    if (fd < 0)
        return;
    close(fd);

    fflush(stderr);
    saved_stderr_fd = dup(fileno(stderr));
    if (saved_stderr_fd < 0) {
        unlink(log_path);
        return;
    }
    capture = freopen(log_path, "w+", stderr);
    if (!capture) {
        dup2(saved_stderr_fd, fileno(stderr));
        close(saved_stderr_fd);
        saved_stderr_fd = -1;
        unlink(log_path);
        return;
    }
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
    harness_result results[4];
    unsigned nres = 0;
    int pass;

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
    if (case_num < 1 || case_num > 5) {
        fprintf(stderr,
                "usage: test_pertitle_db [core] <rom> --case N[1-5] "
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

    log_capture_begin();
    if (!harness_load_rom(&cfg)) {
        log_capture_end();
        fprintf(stderr, "test_pertitle_db: harness_load_rom failed for '%s'\n",
                cfg.rom_path);
        return 1;
    }
    log_capture_end();

    harness_run(&cfg);

    hires_n_ptr       = (int *)harness_dlsym(&cfg, "shadowHiresN");
    shadow_active_ptr = (int *)harness_dlsym(&cfg, "shadowFBActive");
    if (!hires_n_ptr || !shadow_active_ptr) {
        fprintf(stderr,
                "test_pertitle_db: missing shadowHiresN/shadowFBActive "
                "exports -- rebuild with `make TEST_EXPORTS=1`\n");
        harness_shutdown(&cfg);
        return 1;
    }

    switch (case_num) {
    case 1: {
        int hires_ok  = (*hires_n_ptr == 2);
        int logged_ok = log_contains("[titledb]");
        results[nres++] = mkres(hires_ok, "case1_hires_db_applied",
            hires_ok ? "shadowHiresN == 2 (DB applied at default)"
                     : "shadowHiresN != 2");
        results[nres++] = mkres(logged_ok, "case1_titledb_log_present",
            logged_ok ? "[titledb] substitution logged"
                      : "no [titledb] log line found");
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
        int hires_ok = (*hires_n_ptr == 1);
        int no_log   = !log_contains("[titledb]");
        results[nres++] = mkres(hires_ok, "case5_nondb_rom_stock",
            hires_ok ? "shadowHiresN == 1 (no DB entry for this CRC)"
                     : "shadowHiresN != 1");
        results[nres++] = mkres(no_log, "case5_no_titledb_log",
            no_log ? "no [titledb] log line (correctly no match)"
                   : "unexpected [titledb] log line for a non-DB ROM");
        pass = hires_ok && no_log;
        break;
    }
    default:
        pass = 0;
        break;
    }

    harness_report(&cfg, results, nres);
    harness_shutdown(&cfg);
    if (log_path_valid)
        unlink(log_path);

    return pass ? 0 : 1;
}
