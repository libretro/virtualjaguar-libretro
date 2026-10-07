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

#include "../harness/harness.h"
#include "libretro.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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

enum { PH_MENU, PH_LOADING, PH_GAME, PH_DONE };

typedef struct {
    harness_config *cfg;
    int      phase;
    unsigned w;
    double   pct;
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

    if (!data)
        return;               /* duped frame: keep the previous reading */
    for (y = 0; y < height; y++) {
        const uint32_t *row = (const uint32_t *)((const uint8_t *)data + y * pitch);
        for (x = 0; x < width; x++)
            if (row[x] & 0x00FFFFFFu)
                lit++;
    }
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
        if (s->w < MENU_MIN_WIDTH) {
            if (s->pct > s->best_pct)
                s->best_pct = s->pct;
            if (s->pct >= GAME_LIT_PCT) {
                if (!s->lit_frames)
                    s->first_lit = f;
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

int main(int argc, char **argv)
{
    harness_config cfg = HARNESS_CONFIG_DEFAULT;
    xw_state s;
    char detail[256];
    harness_result res;
    int pass;

    memset(&s, 0, sizeof s);
    s.cfg = &cfg;
    s.phase = PH_MENU;
    cfg.frames = MENU_TIMEOUT + LOADING_TIMEOUT + GAME_WINDOW + 60u;

    if (!harness_init_from_args(&cfg, argc, argv))
        return 1;
    cfg.video_callback      = xw_video;
    cfg.video_callback_data = &s;
    cfg.frame_callback      = xw_frame;
    cfg.frame_callback_data = &s;
    if (!harness_load_rom(&cfg))
        return 1;

    harness_run(&cfg);

    if (!s.fail && s.phase != PH_DONE)
        s.fail = "frame budget ran out before the in-game window closed";
    pass = !s.fail && s.best_run >= MIN_RUN_FRAMES && s.reloads == 0;
    if (s.fail)
        snprintf(detail, sizeof detail, "%s", s.fail);
    else
        snprintf(detail, sizeof detail,
                 "menu B at frame %u, loading at %u, %u in-game frame(s) >= %.0f%% lit "
                 "(first %u, best %.1f%%), longest streak %u (dimmest %.1f%%, need %u), "
                 "%u return(s) to the loader%s",
                 s.press_frame, s.loading_frame, s.lit_frames, GAME_LIT_PCT,
                 s.first_lit, s.best_pct, s.best_run, s.best_run_min_pct,
                 MIN_RUN_FRAMES, s.reloads,
                 pass ? "" :
                 !s.lit_frames ? " -- gameplay is black (#811: OLP=0 from the load/moveq race?)"
                               : " -- gameplay did not last (#811: 68000 prefetch / anti-tamper at $350C?)");

    res.status = pass ? "PASS" : "FAIL";
    res.name   = "xenowings_ingame_video";
    res.detail = detail;
    harness_report(&cfg, &res, 1);
    harness_shutdown(&cfg);
    return pass ? 0 : 1;
}
