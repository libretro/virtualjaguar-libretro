/* test/tools/menu_step_probe.c -- how many menu items does ONE tap move?
 *
 * This is the actual user-facing bug of #399: in Jaguar Doom's menu a
 * short D-pad tap jumps two or three entries.  The menu is a DIFFERENT
 * path from the attract demo -- M_Drawer never calls I_Update(), so the
 * menu loop carries no tick gate at all and paces purely at whatever
 * rate the renderer completes.  Measuring the demo (field-quantized,
 * clock-immune) says nothing about it.
 *
 * Two modes:
 *   --scan            locate the menu cursor variable empirically:
 *                     snapshot RAM, tap down, snapshot again, and list
 *                     small-valued words that advanced by 1-3.
 *   --addr HEX        measure steps-per-tap at that address for each
 *                     --hold value.
 *
 * Doom reaches the menu by pressing A/B/C during the attract demo
 * (d_main.c: buttons & (BT_A|BT_B|BT_C) -> ga_exitdemo).
 *
 * Build:
 *   cc -O2 -Wall -std=c99 -I./libretro-common/include \
 *      -o test/tools/menu_step_probe test/tools/menu_step_probe.c \
 *      test/harness/harness.c -ldl -lm
 */
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <libretro.h>
#include "../harness/harness.h"

#define RAMSZ 0x200000u

static uint8_t *ram;
static uint8_t *snapA;
static uint32_t addr;         /* cursor variable, 0 = scan mode */
static unsigned menu_frame = 420;  /* press A to leave the demo */
static unsigned hold = 4;
static unsigned taps = 5;
static unsigned tap0 = 540;   /* first tap */
static unsigned gap = 90;     /* frames between taps */
static uint32_t prev_val;
static unsigned steps_total, taps_done;

/* Fixed-frame scripted input is INVALID for this measurement: any
 * timing option changes how far the demo has progressed by frame N, so
 * a hard-coded "press A at 420" lands in a different game state and
 * silently measures nothing (observed: cursor pinned at 0 for the whole
 * run).  Drive the sequence off observed game state instead.
 *
 * Doom's gametic advances while the attract demo runs and stops once
 * the menu is up, so: confirm demo -> press A -> wait for gametic to go
 * quiet -> only then tap. */
#define GAMETIC_ADDR 0x04080CU
enum { PH_WAIT_DEMO, PH_PRESS_A, PH_WAIT_MENU, PH_TAP, PH_DONE };
static int phase = PH_WAIT_DEMO;
static unsigned phase_frame, quiet_frames, tap_i, next_tap_frame;
static uint32_t last_tic;
static int press_a, press_down;
static int loop_rate;

static uint32_t rd32(const uint8_t *m, uint32_t a)
{
    return ((uint32_t)m[a] << 24) | ((uint32_t)m[a + 1] << 16)
         | ((uint32_t)m[a + 2] << 8) | (uint32_t)m[a + 3];
}

static bool on_frame(void *ud, unsigned frame)
{
    uint32_t tic;
    (void)ud;
    if (!addr)
    {
        if (frame == tap0 - 10)
        {
            snapA = (uint8_t *)malloc(RAMSZ);
            memcpy(snapA, ram, RAMSZ);
        }
        else if (frame == tap0 + 60 && snapA)
        {
            uint32_t a;
            unsigned shown = 0;
            printf("candidates (old -> new, +1..3, small values):\n");
            for (a = 0; a + 4 <= RAMSZ && shown < 40; a += 2)
            {
                uint32_t o = rd32(snapA, a), n = rd32(ram, a);
                if (n > o && (n - o) <= 3 && o < 16 && n < 32)
                {
                    printf("  $%06X  %u -> %u\n", a, o, n);
                    shown++;
                }
            }
            return false;
        }
        return true;
    }

    tic = rd32(ram, GAMETIC_ADDR);
    press_a = press_down = 0;

    switch (phase)
    {
    case PH_WAIT_DEMO:
        /* demo is running once gametic is advancing */
        if (frame > 120 && tic != last_tic)
        {
            phase = PH_PRESS_A;
            phase_frame = frame;
        }
        break;
    case PH_PRESS_A:
        press_a = 1;
        if (frame - phase_frame >= 4)
        {
            phase = PH_WAIT_MENU;
            quiet_frames = 0;
        }
        break;
    case PH_WAIT_MENU:
        /* menu is up once gametic has stopped advancing for a while */
        quiet_frames = (tic == last_tic) ? quiet_frames + 1 : 0;
        if (quiet_frames >= 45)
        {
            phase = PH_TAP;
            prev_val = rd32(ram, addr);
            next_tap_frame = frame + 60;   /* let the menu settle + A release */
            tap_i = 0;
            printf("  [menu reached at frame %u, cursor=%u]\n", frame, prev_val);
        }
        break;
    case PH_TAP:
        /* --loop-rate: find what the menu loop itself is ticking at by
         * locating RAM words that advance once per MiniLoop pass. */
        if (loop_rate)
        {
            if (!snapA)
            {
                snapA = (uint8_t *)malloc(RAMSZ);
                memcpy(snapA, ram, RAMSZ);
                phase_frame = frame;
            }
            else if (frame - phase_frame >= 120)
            {
                uint32_t a; unsigned shown = 0;
                printf("menu-loop candidates (advance per field over %u fields):\n",
                       frame - phase_frame);
                for (a = 0; a + 4 <= RAMSZ && shown < 12; a += 2)
                {
                    uint32_t o = rd32(snapA, a), n = rd32(ram, a);
                    uint32_t d = n - o;
                    if (n > o && d >= 60 && d <= 500)
                    {
                        printf("  $%06X  +%u  = %.2f/field\n", a, d,
                               (double)d / (frame - phase_frame));
                        shown++;
                    }
                }
                return false;
            }
            break;
        }
        if (frame >= next_tap_frame && frame < next_tap_frame + hold)
            press_down = 1;
        else if (frame == next_tap_frame + gap)
        {
            uint32_t v = rd32(ram, addr);
            int32_t d = (int32_t)(v - prev_val);
            if (d < 0) d = -d;
            if (d > 8) d = 8;   /* menu wrap guard */
            printf("  tap %u: %u -> %u (moved %d)\n", tap_i + 1, prev_val, v, d);
            steps_total += (unsigned)d;
            taps_done++;
            prev_val = v;
            next_tap_frame = frame + 30;
            if (++tap_i >= taps)
                phase = PH_DONE;
        }
        break;
    default:
        return false;
    }
    last_tic = tic;
    return true;
}

static int16_t input_cb(void *ud, unsigned port, unsigned device,
                        unsigned index, unsigned id)
{
    (void)ud; (void)device; (void)index;
    if (port != 0)
        return 0;
    if (id == RETRO_DEVICE_ID_JOYPAD_A)
        return press_a ? 1 : 0;
    if (id == RETRO_DEVICE_ID_JOYPAD_DOWN)
        return press_down ? 1 : 0;
    return 0;
}

int main(int argc, char **argv)
{
    harness_config cfg = HARNESS_CONFIG_DEFAULT;
    int i;
    cfg.frames = 1400;
    for (i = 1; i < argc; i++)
    {
        if (!strcmp(argv[i], "--addr") && i + 1 < argc)
            addr = (uint32_t)strtoul(argv[++i], NULL, 16);
        else if (!strcmp(argv[i], "--hold") && i + 1 < argc)
            hold = (unsigned)strtoul(argv[++i], NULL, 10);
        else if (!strcmp(argv[i], "--taps") && i + 1 < argc)
            taps = (unsigned)strtoul(argv[++i], NULL, 10);
        else if (!strcmp(argv[i], "--loop-rate"))
            loop_rate = 1;
        else if (!strcmp(argv[i], "--menu-frame") && i + 1 < argc)
            menu_frame = (unsigned)strtoul(argv[++i], NULL, 10);
    }
    if (!harness_init_from_args(&cfg, argc, argv))
        return 1;
    cfg.frame_callback = on_frame;
    if (!harness_load_rom(&cfg))
        return 1;
    {
        uint8_t **ramp = (uint8_t **)harness_dlsym(&cfg, "jaguarMainRAM");
        ram = ramp ? *ramp : NULL;
    }
    if (!ram) { fprintf(stderr, "jaguarMainRAM not exported\n"); return 1; }

    cfg.input_callback = input_cb;
    cfg.frames = 4000;   /* state machine ends the run itself */

    harness_run(&cfg);
    if (addr && taps_done)
        printf("RESULT hold=%u frames: %.2f items per tap (%u taps)\n",
               hold, (double)steps_total / taps_done, taps_done);
    harness_shutdown(&cfg);
    return 0;
}
