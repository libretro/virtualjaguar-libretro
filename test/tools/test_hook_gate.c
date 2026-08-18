/*
 * test/tools/test_hook_gate.c
 *
 * End-to-end wiring test for per-title enhancement hooks (issue #370),
 * driven through the real dlopen'd core via the shared harness.
 *
 * The hook array is installed programmatically with
 * TitleDBSetHooksForTest() between harness_init_from_args() (which
 * dlopen's the core) and harness_load_rom() (which calls
 * retro_load_game, where the applier fires).  That is deliberate: a
 * canary row in the SHIPPED table keyed on test/roms/yarc.j64 would break
 * `test_pertitle_db --case 5`, which uses yarc as the non-DB control and
 * asserts that no CRC match happens.  Programmatic installation keeps the
 * shipped table clean and keeps case 5 true.
 *
 * expect[] is read out of the ROM file itself at run time, so the test
 * carries no hard-coded ROM bytes to go stale.
 *
 * Cases:
 *   on        gate enabled -- bytes are patched at load, the apply is
 *             logged, the patch survives retro_reset() and a
 *             serialize/unserialize round trip (cart ROM is outside the
 *             state blob and JaguarReset() never touches it), and a
 *             second, manually-applied hook returns 1.
 *   off       gate at its DEFAULT -- nothing is patched.
 *   mismatch  gate enabled, expect[] deliberately wrong -- nothing is
 *             patched and the refusal is logged.
 *
 * Build:  cc -O2 -Wall -std=c99 $(INCFLAGS) -Itest/harness \
 *           -o test/tools/test_hook_gate test/tools/test_hook_gate.c \
 *           test/harness/harness.c -ldl -lm
 * Run:    test_hook_gate <core> <rom> --case on|off|mismatch
 */

#define _DEFAULT_SOURCE 1

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "../harness/harness.h"
#include "../../src/core/titledb.h"

/* Payload offsets to patch.  Both land in the 0xFF padding at the tail of
 * test/roms/yarc.j64, far from the entry vector ($400..$407, which the
 * applier refuses on purpose) and from anything the ROM executes. */
#define HOOK_OFF_A 0x000FFFF0u
#define HOOK_OFF_B 0x000FFFE0u
#define HOOK_LEN   4

#define CART_BASE  0x800000u

static uint8_t expect_a[HOOK_LEN];
static uint8_t expect_b[HOOK_LEN];
static const uint8_t patch_a[HOOK_LEN] = { 0x4E, 0x71, 0x4E, 0x71 };
static const uint8_t patch_b[HOOK_LEN] = { 0x5A, 0xA5, 0x5A, 0xA5 };
static const uint8_t bogus[HOOK_LEN]   = { 0xDE, 0xAD, 0xBE, 0xEF };

static TitleDBHook hooks[2];

/* ---------------- stderr capture (same shape as test_pertitle_db) ------ */

static char log_path[] = "/tmp/vj_hook_gate_log_XXXXXX";
static int  log_path_valid = 0;
static int  saved_stderr_fd = -1;

static void log_capture_begin(void)
{
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
        if (strstr(line, needle)) { found = 1; break; }
    }
    fclose(f);
    return found;
}

/* ---------------- helpers ---------------------------------------------- */

/* Read `len` payload bytes at `off` straight out of the ROM file, applying
 * the same 512-byte header normalization the core does, so expect[] is
 * always the bytes actually loaded. */
static int read_rom_bytes(const char *path, uint32_t off, uint8_t *dst,
                          unsigned len)
{
    FILE *f = fopen(path, "rb");
    long size;
    long hdr = 0;

    if (!f)
        return 0;
    fseek(f, 0, SEEK_END);
    size = ftell(f);
    if (size > 512 && (size % 1024) == 512)
        hdr = 512;
    if ((long)(off + len) > (size - hdr)) { fclose(f); return 0; }
    if (fseek(f, hdr + (long)off, SEEK_SET) != 0) { fclose(f); return 0; }
    if (fread(dst, 1, len, f) != len) { fclose(f); return 0; }
    fclose(f);
    return 1;
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
    const char *mode = NULL;
    int i;
    int pass = 0;
    unsigned nres = 0;
    harness_result results[8];
    uint8_t *jagmem;
    void (*set_hooks)(const TitleDBHook *, int);
    char statefile[64];

    /* Per-process path: a fixed name collides when two `make test`
     * suites run concurrently in different worktrees/sessions. */
    snprintf(statefile, sizeof(statefile), "/tmp/vj_hook_gate_state_%ld.raw",
              (long)getpid());

    for (i = 1; i < argc; i++)
        if (strcmp(argv[i], "--case") == 0 && i + 1 < argc)
            mode = argv[i + 1];

    if (!mode || (strcmp(mode, "on") && strcmp(mode, "off")
                  && strcmp(mode, "mismatch"))) {
        fprintf(stderr,
                "usage: test_hook_gate [core] <rom> --case on|off|mismatch\n");
        return 1;
    }

    /* The apply line is LOG_INF; the refusal is LOG_WRN.  Turn INFO on so
     * both are captured.  Must precede the core's first log call. */
    setenv("VJ_HARNESS_LOG_INFO", "1", 1);

    cfg.frames = 10;
    cfg.quiet  = 1;

    if (!harness_init_from_args(&cfg, argc, argv))
        return 1;
    if (!cfg.rom_path) {
        fprintf(stderr, "test_hook_gate: no ROM path given\n");
        return 1;
    }

    if (!read_rom_bytes(cfg.rom_path, HOOK_OFF_A, expect_a, HOOK_LEN)
        || !read_rom_bytes(cfg.rom_path, HOOK_OFF_B, expect_b, HOOK_LEN)) {
        fprintf(stderr, "test_hook_gate: cannot read ROM bytes from '%s'\n",
                cfg.rom_path);
        return 1;
    }

    set_hooks = (void (*)(const TitleDBHook *, int))
        harness_dlsym(&cfg, "TitleDBSetHooksForTest");
    if (!set_hooks) {
        fprintf(stderr, "test_hook_gate: TitleDBSetHooksForTest not exported "
                        "-- rebuild with `make TEST_EXPORTS=1`\n");
        return 1;
    }

    memset(hooks, 0, sizeof(hooks));
    hooks[0].kind   = TITLEDB_HOOK_ROM_PATCH;
    hooks[0].len    = HOOK_LEN;
    hooks[0].offset = HOOK_OFF_A;
    hooks[0].expect = (strcmp(mode, "mismatch") == 0) ? bogus : expect_a;
    hooks[0].patch  = patch_a;
    hooks[0].name   = "e2e-canary";
    hooks[1].kind   = TITLEDB_HOOK_NONE;

    /* Installed BEFORE retro_load_game, which is where the applier runs. */
    set_hooks(hooks, 2);

    if (strcmp(mode, "off") != 0)
        harness_set_option(&cfg, "virtualjaguar_enhancement_hooks", "enabled");

    log_capture_begin();
    if (!harness_load_rom(&cfg)) {
        log_capture_end();
        fprintf(stderr, "test_hook_gate: harness_load_rom failed for '%s'\n",
                cfg.rom_path);
        return 1;
    }
    log_capture_end();

    harness_run(&cfg);

    jagmem = (uint8_t *)harness_dlsym(&cfg, "jagMemSpace");
    if (!jagmem) {
        fprintf(stderr, "test_hook_gate: jagMemSpace not exported\n");
        harness_shutdown(&cfg);
        return 1;
    }

    if (strcmp(mode, "on") == 0) {
        int applied = memcmp(jagmem + CART_BASE + HOOK_OFF_A,
                             patch_a, HOOK_LEN) == 0;
        int logged  = log_contains("[hooks]") && log_contains("e2e-canary");
        int survived_reset;
        int survived_state;
        int manual_ok;
        void (*do_reset)(void);
        int (*apply)(void);

        results[nres++] = mkres(applied, "gate_on_patch_applied",
            applied ? "cart ROM holds patch[] after load"
                    : "cart ROM does NOT hold patch[] after load");
        results[nres++] = mkres(logged, "gate_on_apply_logged",
            logged ? "[hooks] apply line names the hook"
                   : "no [hooks] apply line naming the hook");

        /* JaguarReset() must not disturb cart ROM -- that is the whole
         * reason hooks need no re-apply on retro_reset(). */
        do_reset = (void (*)(void))harness_dlsym(&cfg, "retro_reset");
        if (do_reset)
            do_reset();
        survived_reset = do_reset && memcmp(jagmem + CART_BASE + HOOK_OFF_A,
                                            patch_a, HOOK_LEN) == 0;
        results[nres++] = mkres(survived_reset, "patch_survives_retro_reset",
            survived_reset ? "still patched after retro_reset()"
                           : "retro_reset() lost the patch (or no symbol)");

        /* Cart ROM is not in the state blob, so a round trip must not
         * disturb it either. */
        survived_state = harness_save_state(&cfg, statefile)
                      && harness_load_state(&cfg, statefile)
                      && memcmp(jagmem + CART_BASE + HOOK_OFF_A,
                                patch_a, HOOK_LEN) == 0;
        results[nres++] = mkres(survived_state, "patch_survives_state_roundtrip",
            survived_state ? "still patched after serialize/unserialize"
                           : "state round trip lost the patch");
        unlink(statefile);

        /* Return value, through the real core: install a second, distinct
         * hook and apply it by hand. */
        apply = (int (*)(void))harness_dlsym(&cfg, "TitleHookApplyROM");
        memset(hooks, 0, sizeof(hooks));
        hooks[0].kind   = TITLEDB_HOOK_ROM_PATCH;
        hooks[0].len    = HOOK_LEN;
        hooks[0].offset = HOOK_OFF_B;
        hooks[0].expect = expect_b;
        hooks[0].patch  = patch_b;
        hooks[0].name   = "e2e-manual";
        hooks[1].kind   = TITLEDB_HOOK_NONE;
        set_hooks(hooks, 2);
        manual_ok = apply && apply() == 1
                 && memcmp(jagmem + CART_BASE + HOOK_OFF_B,
                           patch_b, HOOK_LEN) == 0;
        results[nres++] = mkres(manual_ok, "apply_returns_hook_count",
            manual_ok ? "TitleHookApplyROM() returned 1 and wrote the bytes"
                      : "TitleHookApplyROM() did not return 1 / did not write "
                        "(is TitleHook* in link-test.T + exports-test.list?)");

        pass = applied && logged && survived_reset && survived_state
            && manual_ok;
    } else if (strcmp(mode, "off") == 0) {
        int untouched = memcmp(jagmem + CART_BASE + HOOK_OFF_A,
                               expect_a, HOOK_LEN) == 0;
        int no_apply  = !log_contains("e2e-canary");
        results[nres++] = mkres(untouched, "gate_off_rom_untouched",
            untouched ? "cart ROM still holds the original bytes"
                      : "cart ROM was patched with the gate at its default!");
        results[nres++] = mkres(no_apply, "gate_off_nothing_logged",
            no_apply ? "no [hooks] line for the canary"
                     : "a [hooks] line fired with the gate off");
        pass = untouched && no_apply;
    } else { /* mismatch */
        int untouched = memcmp(jagmem + CART_BASE + HOOK_OFF_A,
                               expect_a, HOOK_LEN) == 0;
        int not_patch = memcmp(jagmem + CART_BASE + HOOK_OFF_A,
                               patch_a, HOOK_LEN) != 0;
        int refused   = log_contains("REFUSED");
        results[nres++] = mkres(untouched && not_patch,
            "mismatch_rom_untouched",
            (untouched && not_patch)
                ? "cart ROM unchanged after a failed precondition"
                : "cart ROM was written despite an expect[] mismatch!");
        results[nres++] = mkres(refused, "mismatch_refusal_logged",
            refused ? "[hooks] REFUSED line logged"
                    : "no [hooks] REFUSED line logged");
        pass = untouched && not_patch && refused;
    }

    harness_report(&cfg, results, nres);
    harness_shutdown(&cfg);
    if (log_path_valid)
        unlink(log_path);

    return pass ? 0 : 1;
}
