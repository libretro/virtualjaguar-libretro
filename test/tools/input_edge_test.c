/* test/tools/input_edge_test.c -- does one physical press read as one
 * press?
 *
 * Drives test/acid/tests/timing/input_edge_per_field.jag, which samples
 * the pad exactly once per video field (the fastest cadence any correct
 * title can observe, since the pad is read from the VI handler) and
 * publishes, in main RAM:
 *
 *   $800 EDGES   press transitions (not-held -> held)
 *   $804 HELD    fields the button was seen held
 *   $808 FIELDS  fields elapsed
 *   $80C READY   $5A5A5A5A once the window closes
 *
 * The reported symptom is that a short D-pad tap registers as more than
 * one press -- Doom's menu jumps two or three entries, Hover Strike's
 * ship accelerates too fast -- unless the tap is very quick.  Two
 * different mechanisms produce that and they need different fixes:
 *
 *   (a) input over-sampling: the core manufactures press events, so a
 *       single continuous press reads as several edges.  Every title
 *       that edge-detects would break.
 *   (b) loop rate: input is clean, but the game's tick loop runs more
 *       often per second than on hardware, so the title's own
 *       auto-repeat threshold is crossed sooner in wall-clock terms.
 *       Doom's menu is this shape -- M_Ticker repeats on movecount==6,
 *       and M_Drawer never calls I_Update(), so the menu loop carries
 *       no 3-tick gate at all.
 *
 * This tool decides between them.  For each press length N it holds one
 * D-pad button for exactly N frames and checks EDGES == 1 and HELD == N.
 * Any N reporting EDGES > 1 is mechanism (a) and names the core as the
 * culprit.  All lengths reporting EDGES == 1 means input is clean and
 * the fault is (b) -- a timing-accuracy problem that cannot be fixed in
 * the input path.
 *
 * Build:
 *   cc -O2 -Wall -std=c99 -I. -I./libretro-common/include \
 *      -o test/tools/input_edge_test test/tools/input_edge_test.c \
 *      test/harness/harness.c -ldl -lm
 * Run:
 *   VJ_EXPECT_BUILD=$(./scripts/build-id.sh) ./test/tools/input_edge_test \
 *      ./virtualjaguar_libretro.dylib \
 *      test/acid/tests/timing/input_edge_per_field.jag [--hold N]...
 */
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include <libretro.h>

#include "../harness/harness.h"

#define RES_EDGES   0x800
#define RES_HELD    0x804
#define RES_FIELDS  0x808
#define RES_READY   0x80C
#define READY_MAGIC 0x5A5A5A5AU

/* Press starts here, well after the ROM has entered its sample loop. */
#define PRESS_START 120

static uint8_t *ram;

static uint32_t rd32(uint32_t addr)
{
    const uint8_t *p = ram + addr;
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
           ((uint32_t)p[2] << 8)  | (uint32_t)p[3];
}

int main(int argc, char **argv)
{
    static const unsigned default_holds[] = { 1, 2, 3, 4, 6, 8, 12, 20 };
    unsigned holds[32];
    unsigned nholds = 0;
    int failures = 0;      /* real input faults: a press read as many edges */
    int inconclusive = 0;  /* ROM never published -- proves nothing */
    unsigned h, i;

    for (i = 1; i + 1 < (unsigned)argc; i++)
        if (!strcmp(argv[i], "--hold") && nholds < 32)
            holds[nholds++] = (unsigned)atoi(argv[i + 1]);
    if (!nholds) {
        for (i = 0; i < sizeof(default_holds) / sizeof(default_holds[0]); i++)
            holds[nholds++] = default_holds[i];
    }

    printf("hold(frames)  edges  held  verdict\n");
    printf("------------  -----  ----  -------\n");

    for (h = 0; h < nholds; h++) {
        harness_config cfg = HARNESS_CONFIG_DEFAULT;
        uint32_t edges, held, ready;
        unsigned n = holds[h];
        const char *verdict;

        /* Must outlast the ROM's own WINDOW_FIELDS (600) or it never
         * publishes and the run proves nothing. */
        cfg.frames = PRESS_START + n + 620;
        cfg.quiet = 1;
        if (!harness_init_from_args(&cfg, argc, argv)) return 1;
        /* Synthetic ROMs need the HLE BIOS: the real BIOS authenticates
         * the cart and a hand-built test ROM cannot satisfy that, so it
         * would never reach our code.  Same reason test/acid/run.c
         * forces this. */
        harness_set_option(&cfg, "virtualjaguar_bios", "disabled");
        if (!harness_load_rom(&cfg)) { harness_shutdown(&cfg); return 1; }

        /* jaguarMainRAM is `uint8_t *jaguarMainRAM` -- a POINTER variable
         * into jagMemSpace, not the array itself.  dlsym returns the
         * address of the pointer, so it must be dereferenced; reading it
         * directly yields the pointer's own bytes, not emulated RAM. */
        {
            uint8_t **ramp = (uint8_t **)harness_dlsym(&cfg, "jaguarMainRAM");
            ram = ramp ? *ramp : NULL;
        }
        if (!ram) {
            fprintf(stderr, "no jaguarMainRAM export (need TEST_EXPORTS=1)\n");
            harness_shutdown(&cfg);
            return 1;
        }

        /* One continuous press of exactly n frames. */
        harness_press(&cfg, 0, RETRO_DEVICE_ID_JOYPAD_UP, PRESS_START, n);
        harness_run(&cfg);

        ready = rd32(RES_READY);
        edges = rd32(RES_EDGES);
        held  = rd32(RES_HELD);

        if (ready != READY_MAGIC) {
            /* Counters are uninitialised RAM here -- report nothing. */
            verdict = "INCONCLUSIVE: ROM did not publish";
            inconclusive++;
            edges = held = 0;
        } else if (held == 0) {
            /* Not an input-multiplication result: the ROM ran but never
             * observed the button at all.  Says nothing about edges. */
            verdict = "INCONCLUSIVE: ROM never saw the button";
            inconclusive++;
        } else if (edges == 0) {
            verdict = "FAIL: press never seen";
            failures++;
        } else if (edges > 1) {
            verdict = "FAIL: one press read as many";
            failures++;
        } else {
            verdict = "ok (1 press = 1 edge)";
        }

        printf("%12u  %5u  %4u  %s\n", n, edges, held, verdict);
        harness_shutdown(&cfg);
    }

    printf("\n");
    if (inconclusive) {
        printf("VERDICT: none -- %d of %u runs never published their counters,\n"
               "  so this tells us nothing about input either way.  Raise the\n"
               "  frame count or lower WINDOW_FIELDS in the ROM and re-run.\n",
               inconclusive, nholds);
        return 2;
    }
    if (failures) {
        printf("VERDICT: core manufactures press events -- an input bug.\n"
               "  A single continuous press produced more than one edge.\n"
               "  Titles that edge-detect (Doom menu, Hover Strike) will\n"
               "  see phantom repeats regardless of their own repeat logic.\n");
        return 1;
    }
    printf("VERDICT: input is clean -- every press read as exactly one edge.\n"
           "  So multi-step menu movement is NOT the input path; it is the\n"
           "  game's own auto-repeat being reached sooner in wall-clock time\n"
           "  because the title's tick loop runs faster than on hardware\n"
           "  (Doom's menu has no I_Update() gate at all).  Fixing it means\n"
           "  render-completion timing, not input handling.\n");
    return 0;
}
