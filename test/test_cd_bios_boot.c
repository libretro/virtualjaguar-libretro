/*
 * test_cd_bios_boot.c -- Discovery-driven REAL-BIOS CD boot smoke test.
 *
 * Mirror of test_cd_hle_boot.c but forces the real Atari Jaguar CD BIOS
 * (loaded from VJ_TEST_CD_ROOT or test/roms/private) instead of HLE.
 *
 * For each *.cue / *.iso / *.cdi found under VJ_TEST_CD_ROOT:
 *   1. retro_load_game() with virtualjaguar_cd_boot_mode=bios
 *   2. Run N frames via retro_run()
 *   3. Diagnostics: PC in valid memory, not in tight self-loop,
 *      RAM has payload (BIOS-loaded data, boot stub, or game code).
 *
 * Build:
 *   make -j8 && cc -O0 -g -Wno-incompatible-pointer-types \
 *       -o test/test_cd_bios_boot test/test_cd_bios_boot.c -ldl
 *
 * Run:
 *   DYLD_LIBRARY_PATH=. test/test_cd_bios_boot
 *
 * Env knobs:
 *   VJ_TEST_CD_ROOT    disc image root (default: test/roms/private). The
 *                      Jaguar CD BIOS file must also live here, named one
 *                      of: jaguarcd_bios.bin / jagcd_bios.bin /
 *                      jaguarcd.bin / jagcd.bin /
 *                      "[BIOS] Atari Jaguar CD (World).j64" /
 *                      "[BIOS] Atari Jaguar Developer CD (World).j64".
 *   VJ_TEST_CD_FOCUS   substring filter for disc paths
 *   VJ_TEST_CD_FRAMES  frame budget per disc (default: 300)
 *   VJ_TEST_CD_EXTS    comma-separated extension list (default: cue,iso)
 *   VJ_TEST_CD_BIOS    "retail" (default) or "dev"
 */

#include "cd_assertions.h"
#include "../libretro-common/include/libretro.h"

#include <sys/wait.h>
#include <fcntl.h>
#include <unistd.h>
#include <signal.h>
#include <errno.h>

static struct vj_core C;

static const char *g_system_dir = "test/roms/private";

/* Single environment callback shared by all discs.  Distinguishes itself
 * from the HLE harness by:
 *   - exposing a REAL system_dir so libretro can find the CD BIOS
 *   - forcing virtualjaguar_cd_boot_mode = "bios" */
static bool cd_environment(unsigned cmd, void *data)
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
        if (strcmp(var->key, "virtualjaguar_bios") == 0)            { var->value = "enabled"; return true; }
        if (strcmp(var->key, "virtualjaguar_usefastblitter") == 0)  { var->value = "enabled"; return true; }
        if (strcmp(var->key, "virtualjaguar_cd_bios_type") == 0)    {
            const char *bt = getenv("VJ_TEST_CD_BIOS");
            var->value = (bt && strcmp(bt, "dev") == 0) ? "dev" : "retail";
            return true;
        }
        if (strcmp(var->key, "virtualjaguar_cd_boot_mode") == 0)    { var->value = "bios"; return true; }
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
/* Per-disc test runner (verbatim adaptation of the HLE harness)      */
/* ------------------------------------------------------------------ */

struct cd_disc_result {
    bool loaded;
    bool pc_stayed_in_ram;
    bool not_self_looping;
    bool not_thrashing;
    bool ram_has_payload;
    uint32_t final_pc;
    size_t   unique_pc_count;
    bool     unique_pc_overflow;
    size_t   ram_nonzero_bytes;
    char     load_error[256];

    /* Frozen snapshot at the moment PC first leaves the valid execute window.
     * Captured ONCE so the post-mortem reflects the actual transition rather
     * than the OP/blitter scribble that may keep mutating RAM afterwards. */
    bool     oob_snapshot_captured;
    uint32_t oob_pc;
    uint32_t oob_prev_pc;
    uint32_t oob_frame;
    uint32_t oob_regs[16];           /* D0..D7, A0..A7 (SP shadow) */
    uint8_t  oob_prev_pc_bytes[32];  /* RAM around prev_pc (the JMP/RTS that fired) */
    uint8_t  oob_sp_bytes[32];       /* RAM at SP (top of stack — likely RTS source) */
    uint32_t oob_sp_addr;
    uint8_t  oob_a0_bytes[32];
    uint8_t  oob_a1_bytes[32];
};

static bool cd_load_game(const char *path)
{
    struct retro_game_info info;
    memset(&info, 0, sizeof(info));
    info.path = path;

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

    C.retro_set_environment(cd_environment);
    C.retro_set_video_refresh(cd_video_refresh);
    C.retro_set_audio_sample(cd_audio_sample);
    C.retro_set_audio_sample_batch(cd_audio_sample_batch);
    C.retro_set_input_poll(cd_input_poll);
    C.retro_set_input_state(cd_input_state);

    if (!cd_load_game(path)) {
        snprintf(out->load_error, sizeof(out->load_error),
                 "retro_load_game returned false (BIOS missing or disc parse failed)");
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
    uint32_t prev_pc = 0;

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

                    /* Freeze diagnostic state immediately — anything we read
                     * later might be corrupted by OP/Blitter chasing garbage. */
                    if (!out->oob_snapshot_captured) {
                        out->oob_snapshot_captured = true;
                        out->oob_pc      = oob;
                        out->oob_prev_pc = prev_pc;
                        out->oob_frame   = f;

                        for (int r = 0; r < 16; r++)
                            out->oob_regs[r] = C.m68k_get_reg(NULL, r);

                        if (ram) {
                            uint32_t a0 = out->oob_regs[8];
                            uint32_t a1 = out->oob_regs[9];
                            uint32_t sp = C.m68k_get_reg(NULL, M68K_REG_SP);
                            out->oob_sp_addr = sp;

                            uint32_t pbase = (prev_pc >= 8 && prev_pc < 0x200000)
                                             ? (prev_pc - 8) : 0;
                            for (int i = 0; i < 32; i++) {
                                uint32_t a = pbase + i;
                                out->oob_prev_pc_bytes[i] = (a < 0x200000) ? ram[a] : 0;
                            }
                            for (int i = 0; i < 32; i++) {
                                uint32_t a = sp + i;
                                out->oob_sp_bytes[i] = (a < 0x200000) ? ram[a] : 0;
                            }
                            for (int i = 0; i < 32; i++) {
                                uint32_t a = a0 + i;
                                out->oob_a0_bytes[i] = (a < 0x200000) ? ram[a] : 0;
                            }
                            for (int i = 0; i < 32; i++) {
                                uint32_t a = a1 + i;
                                out->oob_a1_bytes[i] = (a < 0x200000) ? ram[a] : 0;
                            }
                        }
                    }
                }
                oob_count++;
                out->pc_stayed_in_ram = false;
            }
            prev_pc = pc;
        }
    }

    if (ram) {
        for (uint32_t addr = 0x001000; addr < 0x200000; addr += 0x1000)
            out->ram_nonzero_bytes += cd_count_nonzero(ram, addr, 0x40);
    }
    out->ram_has_payload = (out->ram_nonzero_bytes > 1024);

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
    if (cd_pc_history_is_thrashing(&hist, 8)) {
        out->not_thrashing = false;
        fprintf(stderr,
                "    [PC-THRASH] disc=%s only %zu unique PCs in %u frames\n",
                path, hist.unique_count, frames);
    }

    out->unique_pc_count    = hist.unique_count;
    out->unique_pc_overflow = hist.unique_overflow;

    if (!hist.unique_overflow && hist.unique_count <= 32) {
        fprintf(stderr, "    [PC-SET] %zu unique PCs:", hist.unique_count);
        for (size_t i = 0; i < hist.unique_count; i++)
            fprintf(stderr, " $%06X", hist.unique[i]);
        fprintf(stderr, "\n");

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
        }
    }

    cd_unload_game();
}

/* Run one disc in a forked child so a SIGSEGV in the core does not bring
 * down the whole sweep. */
struct cd_child_status {
    bool exited_normally;
    int  signo;
    struct cd_disc_result result;
};

static void cd_run_one_disc_forked(const char *path, unsigned frames,
                                   struct cd_child_status *status)
{
    int p2c_pipe[2];
    if (pipe(p2c_pipe) != 0) {
        memset(status, 0, sizeof(*status));
        return;
    }
    pid_t pid = fork();
    if (pid < 0) {
        close(p2c_pipe[0]); close(p2c_pipe[1]);
        memset(status, 0, sizeof(*status));
        return;
    }
    if (pid == 0) {
        close(p2c_pipe[0]);
        struct cd_disc_result r;
        cd_run_one_disc(path, frames, &r);
        ssize_t _ = write(p2c_pipe[1], &r, sizeof(r));
        (void)_;
        close(p2c_pipe[1]);
        _exit(0);
    }
    close(p2c_pipe[1]);
    struct cd_disc_result r;
    memset(&r, 0, sizeof(r));
    ssize_t got = read(p2c_pipe[0], &r, sizeof(r));
    (void)got;
    close(p2c_pipe[0]);
    int wstatus = 0;
    waitpid(pid, &wstatus, 0);
    status->result = r;
    if (WIFEXITED(wstatus)) {
        status->exited_normally = true;
        status->signo = 0;
    } else {
        status->exited_normally = false;
        status->signo = WTERMSIG(wstatus);
    }
}

/* ------------------------------------------------------------------ */
/* Test entry                                                          */
/* ------------------------------------------------------------------ */

TEST(boot_all_discovered_discs_real_bios)
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

    /* Real BIOS path is heavy: full 68K BIOS + game code per frame.  600 frames
     * (~10 s simulated) is enough for every disc in our corpus to either reach
     * its game-code entry point or visibly stall — anything more is wasted CI
     * time.  Override with VJ_TEST_CD_FRAMES if you need to chase a deeper hang. */
    unsigned frames = 600;
    const char *frames_env = getenv("VJ_TEST_CD_FRAMES");
    if (frames_env && frames_env[0]) frames = (unsigned)atoi(frames_env);

    fprintf(stderr, "    Discovered %zu disc image(s), running %u frames each "
                    "(real-BIOS path):\n",
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

        bool ok = r->pc_stayed_in_ram && r->not_self_looping &&
                  r->not_thrashing && r->ram_has_payload;
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

        if (r->oob_snapshot_captured) {
            fprintf(stderr,
                    "    [OOB-FROZEN] frame=%u prev_pc=$%06X -> oob_pc=$%08X\n",
                    r->oob_frame, r->oob_prev_pc, r->oob_pc);
            fprintf(stderr,
                    "    [OOB-REGS] D0=$%08X D1=$%08X D2=$%08X D3=$%08X "
                    "A0=$%08X A1=$%08X A6=$%08X SP=$%08X\n",
                    r->oob_regs[0], r->oob_regs[1], r->oob_regs[2], r->oob_regs[3],
                    r->oob_regs[8], r->oob_regs[9], r->oob_regs[14], r->oob_sp_addr);
            fprintf(stderr, "    [OOB-PREVBYTES $%06X]", r->oob_prev_pc & 0xFFFFFF);
            for (int i = 0; i < 32; i++)
                fprintf(stderr, " %02X", r->oob_prev_pc_bytes[i]);
            fprintf(stderr, "\n");
            fprintf(stderr, "    [OOB-SPBYTES   $%06X]", r->oob_sp_addr & 0xFFFFFF);
            for (int i = 0; i < 32; i++)
                fprintf(stderr, " %02X", r->oob_sp_bytes[i]);
            fprintf(stderr, "\n");
            fprintf(stderr, "    [OOB-A0BYTES   $%06X]", r->oob_regs[8] & 0xFFFFFF);
            for (int i = 0; i < 32; i++)
                fprintf(stderr, " %02X", r->oob_a0_bytes[i]);
            fprintf(stderr, "\n");
            fprintf(stderr, "    [OOB-A1BYTES   $%06X]", r->oob_regs[9] & 0xFFFFFF);
            for (int i = 0; i < 32; i++)
                fprintf(stderr, " %02X", r->oob_a1_bytes[i]);
            fprintf(stderr, "\n");
        }
    }

    fprintf(stderr, "    --- discs: %zu pass, %zu fail, %zu focus-skip ---\n",
            pass, fail, skipped);

    if (fail > 0) FAIL("%zu disc(s) failed real-BIOS boot smoke test", fail);
}

int main(int argc, char *argv[])
{
    (void)argc; (void)argv;

    TEST_INIT("CD Real-BIOS Boot Smoke");

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

    RUN_TEST(boot_all_discovered_discs_real_bios);

    void (*p_retro_deinit)(void) = dlsym(C.handle, "retro_deinit");
    if (p_retro_deinit) p_retro_deinit();
    if (C.handle) dlclose(C.handle);

    return TEST_REPORT();
}
