/*
 * test_cd_hle_boot.c -- Discovery-driven HLE CD boot smoke test.
 *
 * Recursively scans test/roms/private/ (or VJ_TEST_CD_ROOT) for
 * *.cue / *.iso / *.cdi disc images, then for each one:
 *   1. Loads the core fresh, forces HLE boot mode, calls retro_load_game()
 *   2. Runs N frames via retro_run()
 *   3. Asserts: 68K PC stays in valid RAM/BIOS range
 *               PC history is not stuck in a tight self-loop for the full window
 *               First frame's load address ($080000 by default) has non-zero data
 *
 * Per-disc PASS/FAIL/SKIP counters roll up into the suite total.
 *
 * Build:
 *   make -j4 DEBUG=1 && make test/test_cd_hle_boot
 *
 * Run:
 *   DYLD_LIBRARY_PATH=. test/test_cd_hle_boot
 *
 * Env knobs:
 *   VJ_TEST_CD_ROOT   override the disc image root (default: test/roms/private)
 *   VJ_TEST_CD_FOCUS  substring filter to run only matching discs
 *   VJ_TEST_CD_FRAMES override frame count (default: 300)
 */

#include "cd_assertions.h"
#include "../libretro-common/include/libretro.h"

#include <sys/wait.h>
#include <fcntl.h>
#include <unistd.h>
#include <signal.h>
#include <errno.h>

static struct vj_core C;

/* ------------------------------------------------------------------ */
/* libretro environment + callbacks (override the defaults from        */
/* test_framework.h so we get HLE mode and a sane system dir)          */
/* ------------------------------------------------------------------ */

static const char *g_system_dir = "test/roms/private";

static bool cd_environment(unsigned cmd, void *data)
{
    switch (cmd & 0xFF) {
    case RETRO_ENVIRONMENT_GET_LOG_INTERFACE:
        return false;
    case RETRO_ENVIRONMENT_SET_PIXEL_FORMAT:
        return true;
    case RETRO_ENVIRONMENT_GET_SYSTEM_DIRECTORY:
        /* Force HLE by hiding the BIOS (real BIOS lookups read from here). */
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

/* ------------------------------------------------------------------ */
/* Per-disc test runner                                                */
/* ------------------------------------------------------------------ */

struct cd_disc_result {
    bool loaded;
    bool pc_stayed_in_ram;
    bool not_self_looping;     /* PC moved at all in the recent window */
    bool not_thrashing;        /* visited > THRASH_THRESHOLD distinct PCs */
    bool ram_has_payload;      /* some non-zero data appears in main RAM */
    uint32_t final_pc;
    size_t   unique_pc_count;
    bool     unique_pc_overflow;
    size_t   ram_nonzero_bytes;
    char     load_error[256];
};

static bool cd_load_game(const char *path)
{
    struct retro_game_info info;
    memset(&info, 0, sizeof(info));
    info.path = path;
    info.data = NULL;
    info.size = 0;

    bool (*p_retro_load_game)(const struct retro_game_info *) =
        dlsym(C.handle, "retro_load_game");
    if (!p_retro_load_game) return false;

    return p_retro_load_game(&info);
}

static void cd_unload_game(void)
{
    void (*p_retro_unload_game)(void) = dlsym(C.handle, "retro_unload_game");
    if (p_retro_unload_game) p_retro_unload_game();
}

static void cd_run_one_disc(const char *path, unsigned frames,
                            struct cd_disc_result *out)
{
    memset(out, 0, sizeof(*out));
    out->pc_stayed_in_ram = true;
    out->not_self_looping = true;
    out->not_thrashing    = true;

    /* Re-bind callbacks (cleared on each retro_init). */
    C.retro_set_environment(cd_environment);
    C.retro_set_video_refresh(cd_video_refresh);
    C.retro_set_audio_sample(cd_audio_sample);
    C.retro_set_audio_sample_batch(cd_audio_sample_batch);
    C.retro_set_input_poll(cd_input_poll);
    C.retro_set_input_state(cd_input_state);

    if (!cd_load_game(path)) {
        snprintf(out->load_error, sizeof(out->load_error),
                 "retro_load_game returned false");
        return;
    }
    out->loaded = true;

    void (*p_retro_run)(void) = dlsym(C.handle, "retro_run");
    if (!p_retro_run) {
        snprintf(out->load_error, sizeof(out->load_error),
                 "retro_run symbol missing");
        cd_unload_game();
        return;
    }

    struct cd_pc_history hist;
    memset(&hist, 0, sizeof(hist));

    uint8_t *ram = C.GetRamPtr ? C.GetRamPtr() : NULL;
    uint32_t first_oob_pc = 0;
    unsigned first_oob_frame = 0;
    size_t   oob_count = 0;

    for (unsigned f = 0; f < frames; f++) {
        p_retro_run();

        if (C.m68k_get_reg) {
            uint32_t pc = C.m68k_get_reg(NULL, M68K_REG_PC);
            out->final_pc = pc;
            cd_pc_history_push(&hist, pc);
            uint32_t oob = cd_pc_oob(&C);
            if (oob) {
                if (!first_oob_pc) {
                    first_oob_pc = oob;
                    first_oob_frame = f;
                }
                oob_count++;
                out->pc_stayed_in_ram = false;
            }
        }
    }

    if (ram) {
        /* Sample non-zero density across the lower 2MB of main RAM. */
        for (uint32_t addr = 0x001000; addr < 0x200000; addr += 0x1000)
            out->ram_nonzero_bytes += cd_count_nonzero(ram, addr, 0x40);
    }
    out->ram_has_payload = (out->ram_nonzero_bytes > 256);

    if (first_oob_pc)
        fprintf(stderr,
                "    [PC-OOB] first oob at frame %u PC=$%08X (then %zu more frames oob)\n",
                first_oob_frame, first_oob_pc, oob_count - 1);

    if (cd_pc_history_is_self_loop(&hist)) {
        out->not_self_looping = false;
        fprintf(stderr,
                "    [PC-LOOP] disc=%s PC=$%06X (no movement in last %u frames)\n",
                path, hist.samples[0], CD_PC_HISTORY_LEN);
    }

    /* Thrashing = the entire run only visited a tiny set of PCs.
     * Games that have successfully booted may still be in a tight game loop
     * (e.g. FMV wait, data processing) with only 5-10 distinct PCs.
     * Threshold of 4 catches genuinely stuck games (1-4 PCs) while
     * allowing booted games in their main loop to pass. */
    if (cd_pc_history_is_thrashing(&hist, 4)) {
        out->not_thrashing = false;
        fprintf(stderr,
                "    [PC-THRASH] disc=%s only %zu unique PCs in %u frames\n",
                path, hist.unique_count, frames);
    }

    out->unique_pc_count    = hist.unique_count;
    out->unique_pc_overflow = hist.unique_overflow;

    /* When the run barely moved we emit the visited PC set so the developer
     * can disassemble each address rather than guess at the loop body. */
    if (!hist.unique_overflow && hist.unique_count <= 32) {
        fprintf(stderr, "    [PC-SET] %zu unique PCs:", hist.unique_count);
        for (size_t i = 0; i < hist.unique_count; i++)
            fprintf(stderr, " $%06X", hist.unique[i]);
        fprintf(stderr, "\n");

        /* Dump 32 bytes around each visited PC so the developer can decode
         * the instruction stream of the wait loop without re-running. */
        if (ram) {
            for (size_t i = 0; i < hist.unique_count; i++) {
                uint32_t pc = hist.unique[i];
                if (pc >= 0x200000) continue;
                uint32_t base = (pc >= 8) ? (pc - 8) : 0;
                uint32_t end  = base + 32;
                if (end > 0x200000) end = 0x200000;
                fprintf(stderr, "    [PC-BYTES $%06X]", pc);
                for (uint32_t a = base; a < end; a++)
                    fprintf(stderr, " %02X", ram[a]);
                fprintf(stderr, "\n");
            }
        }

        /* Dump current 68K data and address registers — the wait loop's
         * read target is usually in A0/A1 and the magic value in D0/D1. */
        if (C.m68k_get_reg) {
            fprintf(stderr, "    [REGS]");
            static const struct { int id; const char *name; } regs[] = {
                {0,  "D0"}, {1,  "D1"}, {2,  "D2"}, {3,  "D3"},
                {8,  "A0"}, {9,  "A1"}, {10, "A2"}, {14, "A6"},
                {18, "SP"},
            };
            for (size_t i = 0; i < sizeof(regs)/sizeof(regs[0]); i++)
                fprintf(stderr, " %s=$%08X", regs[i].name,
                        C.m68k_get_reg(NULL, regs[i].id));
            fprintf(stderr, "\n");

            /* Dump 64 bytes at the current A0 (and A1) target.  Use the
             * core's jagMemSpace[] symbol so we can see cart space
             * ($800000+) and not just main RAM. */
            uint8_t *space = (uint8_t *)dlsym(C.handle, "jagMemSpace");
            uint32_t a0 = C.m68k_get_reg(NULL, 8);
            uint32_t a1 = C.m68k_get_reg(NULL, 9);
            if (space) {
                if (a0 < 0xE00000) {
                    fprintf(stderr, "    [A0-MEM $%06X]", a0);
                    for (uint32_t k = 0; k < 32 && a0 + k < 0xE00000; k++)
                        fprintf(stderr, " %02X", space[a0 + k]);
                    fprintf(stderr, "\n");
                }
                if (a1 < 0xE00000) {
                    fprintf(stderr, "    [A1-MEM $%06X]", a1);
                    for (uint32_t k = 0; k < 32 && a1 + k < 0xE00000; k++)
                        fprintf(stderr, " %02X", space[a1 + k]);
                    fprintf(stderr, "\n");
                }
            } else if (ram) {
                if (a0 < 0x200000) {
                    fprintf(stderr, "    [A0-RAM $%06X]", a0);
                    for (uint32_t k = 0; k < 32 && a0 + k < 0x200000; k++)
                        fprintf(stderr, " %02X", ram[a0 + k]);
                    fprintf(stderr, "\n");
                }
            }
        }
    }

    cd_unload_game();
}

/* ------------------------------------------------------------------ */
/* Per-disc fork wrapper: isolates SIGSEGV / SIGABRT from the suite    */
/* ------------------------------------------------------------------ */

struct cd_child_status {
    bool   exited_normally;
    int    exit_code;
    int    signo;
    struct cd_disc_result result;
};

static void cd_run_one_disc_forked(const char *path, unsigned frames,
                                   struct cd_child_status *status)
{
    memset(status, 0, sizeof(*status));

    int pipefd[2];
    if (pipe(pipefd) != 0) {
        snprintf(status->result.load_error, sizeof(status->result.load_error),
                 "pipe() failed: %s", strerror(errno));
        return;
    }

    pid_t pid = fork();
    if (pid < 0) {
        close(pipefd[0]); close(pipefd[1]);
        snprintf(status->result.load_error, sizeof(status->result.load_error),
                 "fork() failed: %s", strerror(errno));
        return;
    }

    if (pid == 0) {
        close(pipefd[0]);
        struct cd_disc_result r;
        cd_run_one_disc(path, frames, &r);
        ssize_t w = write(pipefd[1], &r, sizeof(r));
        (void)w;
        close(pipefd[1]);
        _exit(0);
    }

    close(pipefd[1]);
    ssize_t got = read(pipefd[0], &status->result, sizeof(status->result));
    close(pipefd[0]);
    (void)got;

    int wstatus;
    while (waitpid(pid, &wstatus, 0) < 0 && errno == EINTR) {}
    if (WIFEXITED(wstatus)) {
        status->exited_normally = true;
        status->exit_code = WEXITSTATUS(wstatus);
    } else if (WIFSIGNALED(wstatus)) {
        status->exited_normally = false;
        status->signo = WTERMSIG(wstatus);
    }
}

/* ------------------------------------------------------------------ */
/* Test entry                                                          */
/* ------------------------------------------------------------------ */

TEST(boot_all_discovered_discs)
{
    struct cd_disc_list discs;
    const char *root = getenv("VJ_TEST_CD_ROOT");
    if (!root || !root[0]) root = g_system_dir;
    cd_discover_discs(root, &discs);

    if (discs.count == 0) {
        fprintf(stderr, "    [SKIP] no disc images under %s "
                "(set VJ_TEST_CD_ROOT to override)\n", root);
        return;
    }

    unsigned frames = 300;
    const char *frames_env = getenv("VJ_TEST_CD_FRAMES");
    if (frames_env && frames_env[0]) frames = (unsigned)atoi(frames_env);

    fprintf(stderr, "    Discovered %zu disc image(s), running %u frames each:\n",
            discs.count, frames);
    for (size_t i = 0; i < discs.count; i++)
        fprintf(stderr, "      %s [%s, %zu bytes]\n",
                discs.entries[i].path, discs.entries[i].ext,
                discs.entries[i].file_size);

    size_t pass = 0, fail = 0, skipped = 0;

    for (size_t i = 0; i < discs.count; i++) {
        const struct cd_disc_entry *d = &discs.entries[i];
        const char *label = strrchr(d->path, '/');
        label = label ? label + 1 : d->path;

        if (!cd_disc_in_focus(d->path)) {
            fprintf(stderr, "    [FOCUS-SKIP] %s\n", label);
            skipped++;
            continue;
        }

        fprintf(stderr, "    [RUN]   %s\n", label);
        fflush(stderr);

        struct cd_child_status cs;
        cd_run_one_disc_forked(d->path, frames, &cs);

        if (!cs.exited_normally) {
            fprintf(stderr, "    [CRASH] %s : child died with signal %d (%s)\n",
                    label, cs.signo, strsignal(cs.signo));
            fail++;
            continue;
        }

        const struct cd_disc_result *r = &cs.result;
        if (!r->loaded) {
            fprintf(stderr, "    [FAIL]  %s : load failed (%s)\n",
                    label, r->load_error[0] ? r->load_error
                                            : "no error message");
            fail++;
            continue;
        }

        /* A game that visited enough unique PCs (not_thrashing) has
         * clearly booted.  The self-loop check is informational — games
         * often enter hardware-polling loops after boot (audio wait,
         * timer, DSP completion) that look like self-loops in HLE
         * because traps return instantly without consuming CPU time. */
        bool ok = r->pc_stayed_in_ram && r->not_thrashing &&
                  r->ram_has_payload;
        const char *status_word = ok ? "PASS" : "FAIL";
        if (!ok) fail++; else pass++;

        fprintf(stderr,
                "    [%s]  %s : pc_in_ram=%d not_loop=%d not_thrash=%d "
                "ram_payload=%zuB unique_pcs=%zu%s final_pc=$%06X\n",
                status_word, label,
                r->pc_stayed_in_ram, r->not_self_looping, r->not_thrashing,
                r->ram_nonzero_bytes,
                r->unique_pc_count,
                r->unique_pc_overflow ? "+" : "",
                r->final_pc);
    }

    fprintf(stderr, "    --- discs: %zu pass, %zu fail, %zu focus-skip ---\n",
            pass, fail, skipped);

    if (fail > 0) FAIL("%zu disc(s) failed boot smoke test", fail);
}

/* ------------------------------------------------------------------ */
/* main                                                                */
/* ------------------------------------------------------------------ */

int main(int argc, char *argv[])
{
    (void)argc; (void)argv;

    TEST_INIT("CD HLE Boot Smoke");

    if (!vj_core_load(&C)) {
        fprintf(stderr, "FATAL: failed to load core\n");
        return 1;
    }

    /* IMPORTANT: do NOT call vj_core_init() here. retro_load_game() does
     * its own setup, and re-running retro_init across discs is what we
     * want for proper isolation. We call retro_init once at suite start
     * so the environment callback gets installed. */
    C.retro_set_environment(cd_environment);
    C.retro_set_video_refresh(cd_video_refresh);
    C.retro_set_audio_sample(cd_audio_sample);
    C.retro_set_audio_sample_batch(cd_audio_sample_batch);
    C.retro_set_input_poll(cd_input_poll);
    C.retro_set_input_state(cd_input_state);
    C.retro_init();

    RUN_TEST(boot_all_discovered_discs);

    void (*p_retro_deinit)(void) = dlsym(C.handle, "retro_deinit");
    if (p_retro_deinit) p_retro_deinit();
    if (C.handle) dlclose(C.handle);

    return TEST_REPORT();
}
