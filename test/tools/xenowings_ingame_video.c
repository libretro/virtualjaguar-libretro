/*
 * xenowings_ingame_video.c -- regression check for issue #811: Xenowings
 * (Dune, 2023) reached its title and menus but went black at the start of
 * gameplay while its music kept playing.
 *
 * Root cause: the GPU swaps its object-list double buffer with
 *
 *     load  (r0),r2        ; r0 = $7CED4, main DRAM
 *     moveq #0,r2
 *     ...
 *     store r2,(r4)        ; r4 = OLP
 *
 * On silicon the external load's data lands AFTER the moveq (JTRM Rev 8
 * p.136, TOM/JERRY bug 13 "Scoreboard failure on successive writes"), so
 * OLP gets the list address.  The core used to let the moveq win: OLP=0,
 * the OP walked the STOP object at $000000 every field, and the screen
 * stayed black.
 *
 * The check is driven by observed state, never by fixed frame numbers:
 *   1. wait for the main menu: the only 652-pixel-wide, well-lit screen
 *      the game shows (the intro logos and the in-game screen are 326
 *      wide), held for 60 frames;
 *   2. press B (starts ARCADE, the default item);
 *   3. wait for the "LOADING MISSION DATA" screen: 326 wide, a line of
 *      text on black (lit, but under 10% coverage);
 *   4. within the next 1200 frames, require at least 600 CONSECUTIVE
 *      326-wide frames with >= 50% non-black coverage -- sustained
 *      gameplay -- and no return of the loading screen after gameplay
 *      starts.
 *
 * Two bugs fail step 4:
 *   - black gameplay (above): no lit frame at all;
 *   - the game's anti-tamper check at 68K $350C: `ori.l #$04000400,(a1)`
 *     patches the next two `addq.l #1,d6` into `addq.l #3,d6`, and the
 *     check (d6 == $12071971) only passes because a real 68000 already
 *     holds both words in its prefetch queue and runs the old pair.
 *     Without a queue model d6 ends at $12071975, the game jumps back to
 *     its loader a few frames into gameplay, and only ~6 lit frames
 *     render.  The model lives in src/m68000/m68kinterface.c;
 *     test/test_m68k_prefetch.c pins it without a ROM.
 *
 * 5. Input reaches the ship.  The run is made twice from one process (a
 *    forked child is the reference arm): identical up to the first lit
 *    gameplay frame, then the input arm holds RIGHT for the rest of the
 *    window.  Within INPUT_WINDOW frames of that press the two frame-hash
 *    streams must diverge, in at least MIN_DIFF_FRAMES frames.  Steps 1-4
 *    are judged on the reference (no-input) arm.
 *
 *    The bug this catches: the DSP joypad reader ends with
 *
 *        div   r9,r8          ; r8 = joypad bits
 *        store r8,(r14+1)     ; -> $F1C3A8, read by the game
 *
 *    JTRM Rev 8 p.134, TOM/JERRY bug 2: indexed-store data is not
 *    scoreboarded, so on silicon the store writes r8 BEFORE the divide's
 *    quotient lands -- the joypad bits.  The core stored the quotient,
 *    the game saw junk, and the ship never moved: the held-input stream
 *    was identical to the no-input one.  Requires the DSP bug-13 fix too
 *    (`load (r0=$F14000),r1 / moveq #0,r1` at F1BBA0), else r8 is 0.
 *
 * Exit status: 0 pass, 1 fail.
 *
 * Usage:
 *   ./test/tools/xenowings_ingame_video [core] <xenowings.rom> [--quiet]
 *
 * Build:
 *   cc -O2 -Wall -std=c99 -I. -I./test/harness -I./libretro-common/include \
 *      -o test/tools/xenowings_ingame_video test/tools/xenowings_ingame_video.c \
 *      test/harness/harness.c -ldl -lm
 */

/* fork/waitpid under -std=c99 on glibc.  Must precede the first system
 * header include. */
#define _POSIX_C_SOURCE 200809L

#include "../harness/harness.h"
#include "libretro.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

#define MENU_MIN_WIDTH     600u   /* menu is 652 wide; everything else 326 */
#define MENU_LIT_PCT       20.0
#define MENU_HOLD_FRAMES   60u
#define MENU_TIMEOUT       4000u  /* menu appears at ~1200 on develop */
#define PRESS_HOLD         10u
#define LOADING_MAX_PCT    10.0
#define LOADING_TIMEOUT    900u   /* frames after the press */
#define GAME_WINDOW        1200u  /* frames after the loading screen */
#define GAME_LIT_PCT       50.0
#define MIN_RUN_FRAMES     600u   /* consecutive lit gameplay frames */
#define INPUT_WINDOW       600u   /* first divergence after the press; ~380 measured */
#define MIN_DIFF_FRAMES    60u    /* frames that must differ in the window */

enum { PH_MENU, PH_LOADING, PH_GAME, PH_DONE };

typedef struct {
    harness_config *cfg;
    int      phase;
    unsigned w;
    double   pct;
    uint64_t hash;          /* FNV-1a of the last real frame */
    int      held;          /* input arm: hold RIGHT from first_lit */
    uint64_t hashes[GAME_WINDOW + 1];   /* by frame - loading_frame */
    unsigned menu_run;
    unsigned press_frame;
    unsigned loading_frame;
    unsigned lit_frames;
    unsigned first_lit;
    unsigned run;           /* current streak of lit 326-wide frames */
    unsigned best_run;
    unsigned reloads;       /* loading screen seen again after gameplay */
    int      in_loading;
    double   best_pct;
    double   run_min_pct;   /* dimmest frame inside the current streak */
    double   best_run_min_pct;
    const char *fail;
} xw_state;

static void xw_video(void *ud, const void *data, unsigned width,
                     unsigned height, size_t pitch)
{
    xw_state *s = (xw_state *)ud;
    unsigned x, y, lit = 0;
    uint64_t h = 1469598103934665603ULL;

    if (!data)
        return;               /* duped frame: keep the previous reading */
    for (y = 0; y < height; y++) {
        const uint32_t *row = (const uint32_t *)((const uint8_t *)data + y * pitch);
        for (x = 0; x < width; x++) {
            if (row[x] & 0x00FFFFFFu)
                lit++;
            h = (h ^ (row[x] & 0x00FFFFFFu)) * 1099511628211ULL;
        }
    }
    s->hash = h;
    s->w   = width;
    s->pct = (width && height) ? 100.0 * (double)lit / ((double)width * height) : 0.0;
}

static bool xw_frame(void *ud, unsigned f)
{
    xw_state *s = (xw_state *)ud;

    switch (s->phase) {
    case PH_MENU:
        if (s->w >= MENU_MIN_WIDTH && s->pct >= MENU_LIT_PCT)
            s->menu_run++;
        else
            s->menu_run = 0;
        if (s->menu_run >= MENU_HOLD_FRAMES) {
            harness_press(s->cfg, 0, RETRO_DEVICE_ID_JOYPAD_B, f + 1, PRESS_HOLD);
            s->press_frame = f + 1;
            s->phase = PH_LOADING;
        } else if (f >= MENU_TIMEOUT) {
            s->fail = "main menu never appeared";
            s->phase = PH_DONE;
        }
        break;
    case PH_LOADING:
        if (f > s->press_frame + PRESS_HOLD && s->w < MENU_MIN_WIDTH
                && s->pct > 0.0 && s->pct < LOADING_MAX_PCT) {
            s->loading_frame = f;
            s->phase = PH_GAME;
        } else if (f >= s->press_frame + LOADING_TIMEOUT) {
            s->fail = "B on the menu never reached the loading screen";
            s->phase = PH_DONE;
        }
        break;
    case PH_GAME:
        if (f - s->loading_frame <= GAME_WINDOW)
            s->hashes[f - s->loading_frame] = s->hash;
        if (s->w < MENU_MIN_WIDTH) {
            if (s->pct > s->best_pct)
                s->best_pct = s->pct;
            if (s->pct >= GAME_LIT_PCT) {
                if (!s->lit_frames) {
                    s->first_lit = f;
                    if (s->held)
                        harness_press(s->cfg, 0, RETRO_DEVICE_ID_JOYPAD_RIGHT,
                                      f + 1, GAME_WINDOW);
                }
                s->lit_frames++;
                if (!s->run || s->pct < s->run_min_pct)
                    s->run_min_pct = s->pct;
                s->run++;
                if (s->run > s->best_run) {
                    s->best_run = s->run;
                    s->best_run_min_pct = s->run_min_pct;
                }
            } else {
                s->run = 0;
            }
        } else {
            s->run = 0;
        }
        /* The loader's "LOADING MISSION DATA" screen coming back after
         * gameplay started is the #811 anti-tamper failure: the game
         * jumped back to its loader at $80200A. */
        if (s->lit_frames && s->w < MENU_MIN_WIDTH
                && s->pct > 0.0 && s->pct < LOADING_MAX_PCT) {
            if (!s->in_loading)
                s->reloads++;
            s->in_loading = 1;
        } else {
            s->in_loading = 0;
        }
        if (f >= s->loading_frame + GAME_WINDOW)
            s->phase = PH_DONE;
        break;
    default:
        break;
    }
    return s->phase != PH_DONE;
}

/* One full run.  `held` selects the input arm. */
static void xw_run_arm(int argc, char **argv, int held, xw_state *s)
{
    harness_config cfg = HARNESS_CONFIG_DEFAULT;

    memset(s, 0, sizeof *s);
    s->cfg = &cfg;
    s->phase = PH_MENU;
    s->held = held;
    cfg.frames = MENU_TIMEOUT + LOADING_TIMEOUT + GAME_WINDOW + 60u;

    if (!harness_init_from_args(&cfg, argc, argv)) {
        s->fail = "harness init failed";
        return;
    }
    cfg.video_callback      = xw_video;
    cfg.video_callback_data = s;
    cfg.frame_callback      = xw_frame;
    cfg.frame_callback_data = s;
    if (!harness_load_rom(&cfg)) {
        s->fail = "ROM load failed";
        return;
    }
    harness_run(&cfg);
    if (!s->fail && s->phase != PH_DONE)
        s->fail = "frame budget ran out before the in-game window closed";
    harness_shutdown(&cfg);
    s->cfg = NULL;
}

int main(int argc, char **argv)
{
    harness_config cfg = HARNESS_CONFIG_DEFAULT;
    static xw_state s, in;
    char detail[512];
    harness_result res;
    FILE *xfer;
    pid_t pid;
    int status = 0, pass, input_ok = 0, child_failed;
    unsigned i, ndiff = 0, first_diff = 0, press_rel = 0;

    /* The reference (no-input) arm runs in a forked child, so each arm
     * gets fresh core statics, and hands its state back through a temp
     * file; the parent runs the input arm concurrently.  `fail` points
     * into the child's address space, so only its non-NULL-ness crosses. */
    xfer = tmpfile();
    if (!xfer) {
        perror("tmpfile");
        return 1;
    }
    fflush(NULL);
    pid = fork();
    if (pid < 0) {
        perror("fork");
        return 1;
    }
    if (pid == 0) {
        xw_run_arm(argc, argv, 0, &s);
        fwrite(&s, sizeof s, 1, xfer);
        fflush(xfer);
        _exit(0);
    }
    xw_run_arm(argc, argv, 1, &in);
    if (waitpid(pid, &status, 0) != pid || !WIFEXITED(status)
            || fseek(xfer, 0, SEEK_SET) != 0 || fread(&s, sizeof s, 1, xfer) != 1) {
        fprintf(stderr, "xenowings_ingame_video: reference arm died\n");
        return 1;
    }
    fclose(xfer);
    child_failed = s.fail != NULL;
    s.fail = child_failed ? "reference (no-input) arm failed to reach gameplay" : NULL;

    if (!s.fail && !in.fail && in.loading_frame == s.loading_frame
            && in.first_lit == s.first_lit && s.first_lit) {
        press_rel = s.first_lit + 1 - s.loading_frame;
        for (i = press_rel; i <= GAME_WINDOW; i++) {
            if (in.hashes[i] != s.hashes[i]) {
                if (!ndiff)
                    first_diff = i;
                ndiff++;
            }
        }
        input_ok = ndiff >= MIN_DIFF_FRAMES && first_diff - press_rel <= INPUT_WINDOW;
    }

    pass = !s.fail && !in.fail && s.best_run >= MIN_RUN_FRAMES && s.reloads == 0
        && input_ok;
    if (s.fail || in.fail)
        snprintf(detail, sizeof detail, "%s", s.fail ? s.fail : in.fail);
    else
        snprintf(detail, sizeof detail,
                 "menu B at frame %u, loading at %u, %u in-game frame(s) >= %.0f%% lit "
                 "(first %u, best %.1f%%), longest streak %u (dimmest %.1f%%, need %u), "
                 "%u return(s) to the loader; RIGHT held from +%u: %u frame(s) differ "
                 "from no input, first at +%u (need >= %u, first within +%u)%s",
                 s.press_frame, s.loading_frame, s.lit_frames, GAME_LIT_PCT,
                 s.first_lit, s.best_pct, s.best_run, s.best_run_min_pct,
                 MIN_RUN_FRAMES, s.reloads, press_rel, ndiff, first_diff,
                 MIN_DIFF_FRAMES, INPUT_WINDOW,
                 pass ? "" :
                 !s.lit_frames ? " -- gameplay is black (#811: OLP=0 from the load/moveq race?)"
                 : (s.best_run < MIN_RUN_FRAMES || s.reloads)
                               ? " -- gameplay did not last (#811: 68000 prefetch / anti-tamper at $350C?)"
                 : (in.loading_frame != s.loading_frame || in.first_lit != s.first_lit)
                               ? " -- arms diverged before any input (nondeterminism?)"
                               : " -- input has no effect (#811: DSP joypad reader, JTRM bugs 2/13?)");

    /* harness_report only needs the parsed output mode. */
    if (!harness_init_from_args(&cfg, argc, argv))
        return 1;
    res.status = pass ? "PASS" : "FAIL";
    res.name   = "xenowings_ingame_video";
    res.detail = detail;
    harness_report(&cfg, &res, 1);
    return pass ? 0 : 1;
}
