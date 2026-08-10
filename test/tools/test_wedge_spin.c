/*
 * test/tools/test_wedge_spin.c — the crash watchdog must not report a
 * healthy GPU wait/spin loop as gpu_wedge.
 *
 * Regression for the Super Burnout false positive found during the #378
 * overclock pilot: the game's GPU spins a small wait loop (~446k opcodes
 * per frame at $F03064) during attract, and deterministic per-frame slice
 * budgets land the end-of-slice PC on the same loop instruction every
 * frame — so the old "same sampled PC for N frames" predicate fired
 * gpu_wedge on a perfectly healthy title.  The fixed predicate requires
 * the processor to be flagged running while having executed ZERO opcodes
 * across the window (a real emulator fault); mere PC-sample stability is
 * spin-aliasing, not evidence of a hang.
 *
 * Assertions (needs the Super Burnout ROM; caller skips by name if absent):
 *   1. 620 frames of Super Burnout log NO "gpu_wedge" line.
 *   2. gpu_exec_opcode_count advances (the liveness signal the fixed
 *      detector relies on actually counts).
 *
 * The true-positive direction (a genuinely wedged GPU still fires) is not
 * covered here: reaching "flagged running, zero opcodes executed" needs a
 * corrupted core state no clean ROM produces on demand.  video_stall
 * remains the user-visible-freeze layer regardless.
 *
 * Build: cc -O2 -Wall -std=c99 $(INCFLAGS) -o test/tools/test_wedge_spin \
 *          test/tools/test_wedge_spin.c test/harness/harness.c -ldl -lm
 * Needs the wide test ABI (make TEST_EXPORTS=1).
 */

/* glibc hides mkstemp()/fileno() under strict -std=c99 unless a
 * feature-test macro is set; macOS exposes them regardless, which is why
 * this only broke on the Linux CI builders.  Same macro test_pertitle_db.c
 * uses.  Must precede every #include. */
#define _DEFAULT_SOURCE 1

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <unistd.h>

#include "../harness/harness.h"

static char log_path[] = "/tmp/wedge_spin_log_XXXXXX";
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
    while (fgets(line, sizeof(line), f))
        if (strstr(line, needle)) {
            found = 1;
            break;
        }
    fclose(f);
    return found;
}

int main(int argc, char **argv)
{
    harness_config cfg = HARNESS_CONFIG_DEFAULT;
    uint32_t *opcount;
    harness_result res[2];
    int wedge_free, counting;

    cfg.frames = 620;   /* comfortably past WEDGE_FRAMES_GPU (180) */
    cfg.quiet = 1;
    if (!harness_init_from_args(&cfg, argc, argv))
        return 1;
    if (!cfg.rom_path) {
        fprintf(stderr, "test_wedge_spin: no ROM path given\n");
        return 1;
    }

    log_capture_begin();
    if (!harness_load_rom(&cfg)) {
        log_capture_end();
        fprintf(stderr, "test_wedge_spin: load failed for '%s'\n",
                cfg.rom_path);
        return 1;
    }
    harness_run(&cfg);
    log_capture_end();

    opcount = (uint32_t *)harness_dlsym(&cfg, "gpu_exec_opcode_count");

    wedge_free = !log_contains("gpu_wedge");
    counting   = (opcount != NULL && *opcount > 0);

    res[0].status = wedge_free ? "PASS" : "FAIL";
    res[0].name   = "spin_loop_not_wedge";
    res[0].detail = wedge_free
        ? "no gpu_wedge logged across 620 frames of a healthy spin loop"
        : "gpu_wedge logged for a healthy GPU wait/spin loop";
    res[1].status = counting ? "PASS" : "FAIL";
    res[1].name   = "opcode_counter_alive";
    res[1].detail = counting
        ? "gpu_exec_opcode_count advanced"
        : "gpu_exec_opcode_count missing or zero";
    harness_report(&cfg, res, 2);

    if (log_path_valid)
        unlink(log_path);
    harness_shutdown(&cfg);
    return (wedge_free && counting) ? 0 : 1;
}
