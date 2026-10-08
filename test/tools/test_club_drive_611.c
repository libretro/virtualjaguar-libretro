/* test/tools/test_club_drive_611.c -- Club Drive must not run its GPU into
 * main RAM when the car turns (issue #611).
 *
 * Club Drive clips polygons against the screen with a four-entry corner
 * table indexed by (NORMI(outcode) << 3) & $1F.  NORMI used to return one
 * too many (it normalised to bit 22, not the IEEE hidden-bit position 23 the
 * JTRM and TOM's ARITH.NET give), so every inserted corner was rotated by
 * one.  A polygon crossing two screen edges then came out non-monotonic in
 * Y, the edge walker bailed while its return stack (r10) was borrowed as
 * scratch, and the GPU jumped through a garbage return address into main
 * RAM within a few dozen frames of steering.
 *
 * The check: from the user's mid-race savestate, hold accelerate (B) and
 * steer left (the shortest of the reproducing inputs: escape at frame 82 on
 * the broken build), and require that the GPU PC is inside GPU local RAM at
 * the end of every frame and that the picture keeps changing.  The title
 * never legitimately runs GPU code from main RAM.
 *
 * Usage:
 *   test_club_drive_611 <core> <rom> --load-state <raw state> [--frames N]
 * The savestate must be raw; RetroArch's rzip states go through
 * scripts/rzip_extract.py first (the Makefile does this).
 *
 * Build:
 *   cc -O2 -Wall -std=c99 -I. -I./src -I./libretro-common/include \
 *      -o test/tools/test_club_drive_611 test/tools/test_club_drive_611.c \
 *      test/harness/harness.c -ldl -lm
 */
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include "../harness/harness.h"
#include "../../libretro-common/include/libretro.h"

typedef struct {
    uint32_t (*get_pc)(void);
    harness_config *cfg;
    unsigned escape_frame;      /* 0 = none */
    uint32_t escape_pc;
    uint32_t prev_hash;
    unsigned transitions;
} cd611_state;

static bool cd611_frame(void *ud, unsigned frame)
{
    cd611_state *s = (cd611_state *)ud;
    uint32_t pc = s->get_pc();

    if (s->cfg->last_fb_hash != s->prev_hash)
        s->transitions++;
    s->prev_hash = s->cfg->last_fb_hash;

    if (pc < 0xF03000 || pc >= 0xF04000)
    {
        s->escape_frame = frame;
        s->escape_pc = pc;
        return false;
    }
    return true;
}

int main(int argc, char **argv)
{
    harness_config cfg = HARNESS_CONFIG_DEFAULT;
    cd611_state s;
    harness_result res[2];
    int ok_escape, ok_alive;
    char msg0[160], msg1[160];
    unsigned min_transitions;

    memset(&s, 0, sizeof(s));
    cfg.frames = 300;
    cfg.quiet = 1;
    if (!harness_init_from_args(&cfg, argc, argv)) return 1;
    if (!cfg.rom_path || !cfg.load_state_path)
    {
        fprintf(stderr, "usage: test_club_drive_611 <core> <rom> "
                        "--load-state <raw state> [--frames N]\n");
        return 2;
    }
    /* Stock settings: the shipped default is the accurate blitter, and the
     * harness default is the fast one. */
    harness_set_option(&cfg, "virtualjaguar_usefastblitter", "disabled");

    /* Accelerate for the whole run, steer left for two seconds. */
    harness_press(&cfg, 0, RETRO_DEVICE_ID_JOYPAD_B, 10, cfg.frames);
    harness_press(&cfg, 0, RETRO_DEVICE_ID_JOYPAD_LEFT, 10, 120);

    if (!harness_load_rom(&cfg)) return 1;

    s.get_pc = (uint32_t (*)(void))harness_dlsym(&cfg, "GPUGetPC");
    if (!s.get_pc)
    {
        fprintf(stderr, "GPUGetPC not exported (build with TEST_EXPORTS=1)\n");
        harness_shutdown(&cfg);
        return 1;
    }
    s.cfg = &cfg;
    cfg.want_fb_hash = 1;
    cfg.frame_callback = cd611_frame;
    cfg.frame_callback_data = &s;

    harness_run(&cfg);

    ok_escape = (s.escape_frame == 0);
    snprintf(msg0, sizeof(msg0), ok_escape
             ? "GPU stayed in local RAM for %u frames of driving"
             : "GPU PC left local RAM at frame %u (pc=$%08X)",
             ok_escape ? cfg.frames : s.escape_frame,
             (unsigned)s.escape_pc);
    res[0].status = ok_escape ? "PASS" : "FAIL";
    res[0].name = "club_drive_gpu_stays_local";
    res[0].detail = msg0;

    /* Guards against a vacuous pass (state not restored, game paused or
     * frozen): the healthy build changes the picture on ~2 of every 3
     * frames while driving. */
    min_transitions = cfg.frames / 3;
    ok_alive = !ok_escape || s.transitions >= min_transitions;
    snprintf(msg1, sizeof(msg1), "%u picture changes in %u frames (need %u)",
             s.transitions, cfg.frames, min_transitions);
    /* After an escape the run stopped early, so the count proves nothing. */
    res[1].status = !ok_escape ? "INFO" : ok_alive ? "PASS" : "FAIL";
    res[1].name = "club_drive_picture_moves";
    res[1].detail = msg1;

    harness_report(&cfg, res, 2);
    harness_shutdown(&cfg);
    return (ok_escape && ok_alive) ? 0 : 1;
}
