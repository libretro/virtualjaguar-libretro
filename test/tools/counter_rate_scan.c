/* test/tools/counter_rate_scan.c -- find 32-bit big-endian RAM words
 * that behave like frame/tic counters and report their advance rate in
 * counts per field.  Used to locate Doom's _ticcount and measure how
 * fast the VI handler actually runs it (hardware: exactly 1 per video
 * field; anything else means our VI delivery for this title's register
 * setup is wrong).
 *
 * Method: snapshot 2MB RAM at frame A (--from), diff at frame B
 * (--to == A+window), list addresses whose word advanced by between
 * window*0.5 and window*4 counts monotonically at 4 checkpoints.
 */
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "../harness/harness.h"

#define RAMSZ 0x200000u
#define CHECKPOINTS 5

static uint8_t *ram;
static uint8_t *snap[CHECKPOINTS];
static unsigned snap_frames[CHECKPOINTS];
static unsigned snap_n, from_frame = 600, window = 300;

static uint32_t rd32(const uint8_t *m, uint32_t a)
{
    return ((uint32_t)m[a] << 24) | ((uint32_t)m[a + 1] << 16)
         | ((uint32_t)m[a + 2] << 8) | (uint32_t)m[a + 3];
}

static bool on_frame(void *ud, unsigned frame)
{
    (void)ud;
    if (snap_n < CHECKPOINTS && frame == from_frame + snap_n * (window / (CHECKPOINTS - 1)))
    {
        snap[snap_n] = (uint8_t *)malloc(RAMSZ);
        memcpy(snap[snap_n], ram, RAMSZ);
        snap_frames[snap_n] = frame;
        snap_n++;
    }
    return true;
}

int main(int argc, char **argv)
{
    harness_config cfg = HARNESS_CONFIG_DEFAULT;
    uint32_t a;
    cfg.frames = 1200;
    if (!harness_init_from_args(&cfg, argc, argv))
        return 1;
    cfg.frame_callback = on_frame;
    if (!harness_load_rom(&cfg))
        return 1;
    {
        uint8_t **ramp = (uint8_t **)harness_dlsym(&cfg, "jaguarMainRAM");
        ram = ramp ? *ramp : NULL;
    }
    if (!ram) { fprintf(stderr, "no jaguarMainRAM\n"); return 1; }
    harness_run(&cfg);
    if (snap_n < CHECKPOINTS) { fprintf(stderr, "not enough frames\n"); return 1; }
    for (a = 0; a + 4 <= RAMSZ; a += 2)
    {
        uint32_t v0 = rd32(snap[0], a), vN = rd32(snap[CHECKPOINTS - 1], a);
        uint32_t total_frames = snap_frames[CHECKPOINTS - 1] - snap_frames[0];
        uint32_t d = vN - v0;
        int ok = 1, i;
        if (d < total_frames / 2 || d > total_frames * 4)
            continue;
        for (i = 1; i < CHECKPOINTS; i++)
        {
            uint32_t p = rd32(snap[i - 1], a), c = rd32(snap[i], a);
            uint32_t step = c - p;
            uint32_t fr = snap_frames[i] - snap_frames[i - 1];
            if (c <= p || step < fr / 2 || step > fr * 4) { ok = 0; break; }
        }
        if (ok)
            printf("$%06X rate=%.3f/field (delta %u over %u fields)\n",
                   a, (double)d / total_frames, d, total_frames);
    }
    harness_shutdown(&cfg);
    return 0;
}
