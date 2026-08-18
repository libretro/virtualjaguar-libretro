/*
 * test/tools/blitter_static_leak.c -- cross-load static-state leak probe
 * (issue #479).
 *
 * WHAT THIS CATCHES
 *
 * A static that the FAST blitter engine mutates and that survives
 * retro_unload_game + retro_deinit, so it lands in the NEXT session's
 * savestate blob.  On desktop a fresh dlopen re-zeroes the library's
 * statics and hides this entirely; on iOS dlclose is impossible, the
 * core stays resident for the life of the app, and the residue is
 * live.  CLAUDE.md's rule is that every static is reset in
 * retro_deinit -- this probe is the enforcement.
 *
 * THE SHAPE OF THE TEST
 *
 * One process, four full sessions against the same ROM, with an extra
 * dlopen reference held across all of them so the image is never
 * unloaded (mirroring iOS):
 *
 *   run 1  accurate            -> reference
 *   run 2  accurate            -> proves the probe itself is stable
 *   run 3  fast                -> the contaminating run
 *   run 4  accurate            -> must equal runs 1 and 2
 *
 * Run 2 is not redundant.  Without it a failure in run 4 cannot be
 * distinguished from "this harness simply is not reproducible across
 * loads", which would make the whole result meaningless.
 *
 * Savestate blobs are captured mid-run (frame 300) and at the end
 * (frame 600).  Framebuffer hashes are collected alongside them
 * because the two disagree in the interesting way: the leak moves
 * SAVESTATE bytes while leaving the PICTURE identical, so a test that
 * only compared frames would report all-clear.
 *
 * Exit status: 0 = clean, 1 = leak detected, 2 = harness/setup error.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dlfcn.h>

#include "../harness/harness.h"

#define RUN_COUNT      4
#define SNAP_A         300
#define SNAP_B         600
#define TOTAL_FRAMES   600

typedef struct {
    const char *label;
    const char *fastblitter;      /* "enabled" / "disabled" */
    uint8_t    *snap[2];          /* blobs at SNAP_A, SNAP_B */
    size_t      snap_len[2];
    uint32_t    fb_hash[2];
} run_result;

/* Serialize hooks resolved from the resident image, so this probe does
 * not depend on the harness growing an in-memory serialize accessor. */
static size_t (*p_serialize_size)(void);
static bool   (*p_serialize)(void *, size_t);

static run_result g_runs[RUN_COUNT];
static run_result *g_active;
static harness_config *g_cfg;

/* Called after every emulated frame.  Grabs a blob at the two snapshot
 * points.  Kept deliberately dumb: no comparison here, only capture. */
static bool frame_hook(void *user, unsigned frame)
{
    unsigned slot;
    size_t   size;

    (void)user;

    if (frame == SNAP_A)
        slot = 0;
    else if (frame == SNAP_B)
        slot = 1;
    else
        return true;

    if (!g_active || !g_cfg || !p_serialize || !p_serialize_size)
        return true;

    size = p_serialize_size();
    if (size == 0)
        return true;

    g_active->snap[slot] = (uint8_t *)malloc(size);
    if (!g_active->snap[slot])
        return true;

    if (!p_serialize(g_active->snap[slot], size)) {
        free(g_active->snap[slot]);
        g_active->snap[slot] = NULL;
        return true;
    }
    g_active->snap_len[slot] = size;
    g_active->fb_hash[slot]  = g_cfg->last_fb_hash;
    return true;
}

/* One complete session: load core, load ROM, run, snapshot, tear down. */
static bool do_run(run_result *r, const char *core, const char *rom)
{
    harness_config cfg = HARNESS_CONFIG_DEFAULT;

    cfg.core_path            = core;
    cfg.rom_path             = rom;
    cfg.frames               = TOTAL_FRAMES;
    cfg.quiet                = true;
    cfg.want_fb_hash         = true;
    cfg.frame_callback       = frame_hook;
    cfg.frame_callback_data  = NULL;

    cfg.options[0].key   = "virtualjaguar_usefastblitter";
    cfg.options[0].value = r->fastblitter;
    cfg.num_options      = 1;

    g_active = r;
    g_cfg    = &cfg;

    if (!harness_load_core(&cfg)) {
        fprintf(stderr, "[%s] harness_load_core failed\n", r->label);
        return false;
    }

    p_serialize_size = (size_t (*)(void))
        harness_dlsym(&cfg, "retro_serialize_size");
    p_serialize = (bool (*)(void *, size_t))
        harness_dlsym(&cfg, "retro_serialize");
    if (!p_serialize || !p_serialize_size) {
        fprintf(stderr, "[%s] core lacks retro_serialize\n", r->label);
        harness_shutdown(&cfg);
        return false;
    }

    if (!harness_load_rom(&cfg)) {
        fprintf(stderr, "[%s] harness_load_rom failed\n", r->label);
        harness_shutdown(&cfg);
        return false;
    }

    harness_run(&cfg);
    harness_shutdown(&cfg);     /* retro_unload_game + retro_deinit + dlclose */
    g_active = NULL;
    g_cfg    = NULL;

    if (!r->snap[0] || !r->snap[1]) {
        fprintf(stderr, "[%s] missing snapshot(s)\n", r->label);
        return false;
    }
    return true;
}

/* Byte-diff two blobs; report the first differing offset and a count. */
static size_t blob_diff(const uint8_t *a, size_t alen,
                        const uint8_t *b, size_t blen,
                        size_t *first_off)
{
    size_t i, n, diff = 0;

    *first_off = (size_t)-1;
    if (alen != blen) {
        *first_off = 0;
        return (alen > blen) ? alen : blen;
    }
    n = alen;
    for (i = 0; i < n; i++) {
        if (a[i] != b[i]) {
            if (*first_off == (size_t)-1)
                *first_off = i;
            diff++;
        }
    }
    return diff;
}

static void report_pair(const run_result *x, const run_result *y,
                        unsigned slot, int *failures)
{
    size_t first, diff;

    diff = blob_diff(x->snap[slot], x->snap_len[slot],
                     y->snap[slot], y->snap_len[slot], &first);

    printf("  %-28s frame %3u : ", "", (slot == 0) ? SNAP_A : SNAP_B);
    printf("%s vs %s -> ", x->label, y->label);
    if (diff == 0) {
        printf("identical (%u bytes)\n", (unsigned)x->snap_len[slot]);
    } else {
        printf("DIFFER: %u byte(s), first at offset %u (0x%X)\n",
               (unsigned)diff, (unsigned)first, (unsigned)first);
        {
            size_t k, shown = 0;
            for (k = 0; k < x->snap_len[slot] && shown < 64; k++) {
                if (x->snap[slot][k] != y->snap[slot][k]) {
                    printf("      0x%06X: %02X -> %02X\n", (unsigned)k,
                           x->snap[slot][k], y->snap[slot][k]);
                    shown++;
                }
            }
        }
        (*failures)++;
    }
    printf("  %-28s            fb_hash %08X vs %08X %s\n", "",
           x->fb_hash[slot], y->fb_hash[slot],
           (x->fb_hash[slot] == y->fb_hash[slot]) ? "(same picture)"
                                                  : "(PICTURE ALSO MOVED)");
}

int main(int argc, char **argv)
{
    const char *core = "./virtualjaguar_libretro.dylib";
    const char *rom  = "test/roms/yarc.j64";
    void       *pin;
    unsigned    i;
    int         failures = 0;
    int         rc;

    if (argc > 1) core = argv[1];
    if (argc > 2) rom  = argv[2];

    /* Pin the image for the whole test.  harness_shutdown() dlcloses its
     * own handle after every run; this extra reference keeps the library
     * mapped and its statics alive, which is the iOS regime the bug
     * lives in.  Without this the test cannot fail, because each run
     * gets freshly-zeroed statics. */
    pin = dlopen(core, RTLD_LAZY | RTLD_LOCAL);
    if (!pin) {
        fprintf(stderr, "cannot pin core '%s': %s\n", core, dlerror());
        return 2;
    }

    memset(g_runs, 0, sizeof(g_runs));
    g_runs[0].label = "run1-accurate";  g_runs[0].fastblitter = "disabled";
    g_runs[1].label = "run2-accurate";  g_runs[1].fastblitter = "disabled";
    g_runs[2].label = "run3-FAST";      g_runs[2].fastblitter = "enabled";
    g_runs[3].label = "run4-accurate";  g_runs[3].fastblitter = "disabled";

    printf("=== Blitter cross-load static leak (#479) ===\n");
    printf("core: %s\nrom:  %s\n", core, rom);
    printf("image pinned (extra dlopen ref) -- statics persist across runs\n\n");

    for (i = 0; i < RUN_COUNT; i++) {
        printf("running %s (usefastblitter=%s)...\n",
               g_runs[i].label, g_runs[i].fastblitter);
        if (!do_run(&g_runs[i], core, rom)) {
            dlclose(pin);
            return 2;
        }
    }

    printf("\n--- control: run1 vs run2 (both accurate, no fast run between) ---\n");
    {
        int control_fail = 0;
        report_pair(&g_runs[0], &g_runs[1], 0, &control_fail);
        report_pair(&g_runs[0], &g_runs[1], 1, &control_fail);
        if (control_fail) {
            printf("\nCONTROL FAILED: two identical back-to-back sessions already\n"
                   "differ, so this probe cannot attribute anything to the fast\n"
                   "engine.  Fix the probe before trusting a run4 result.\n");
            dlclose(pin);
            return 2;
        }
        printf("  control OK: repeated sessions are byte-identical\n");
    }

    printf("\n--- subject: run1 vs run4 (accurate, after a FAST run) ---\n");
    report_pair(&g_runs[0], &g_runs[3], 0, &failures);
    report_pair(&g_runs[0], &g_runs[3], 1, &failures);

    printf("\n=== %s ===\n",
           failures ? "LEAK DETECTED" : "clean: no cross-load residue");

    rc = failures ? 1 : 0;

    for (i = 0; i < RUN_COUNT; i++) {
        free(g_runs[i].snap[0]);
        free(g_runs[i].snap[1]);
    }
    dlclose(pin);
    return rc;
}
