/*
 * padtest_probe.c -- drives darthcloud's AtariJaguarPadtest ROM through the
 * REAL delivery path (update_input(), not InputDevFeed*() directly) to
 * validate Team Tap (#513) and the 6D controller (#538) against a program
 * that reads $F14000/$F14002 the way an actual game would.
 *
 * Why this exists: PR #547 (6D controller) shipped with a register-level
 * test (sixd_decode_test) that calls InputDevFeed6D() directly and never
 * enters update_input().  That blind spot let Z and TY reach the wire as
 * ANALOG-BUTTON-only values; a frontend that does not implement analog
 * button reads answered 0 for a held shoulder, leaving two of six axes
 * permanently dead even though every register-level assertion passed.
 * This tool exercises the full RetroPad -> update_input() -> $F14000
 * path with a harness input_callback standing in for the frontend, so it
 * CAN see that class of defect.  See docs/input-devices-user-guide.md,
 * the "Validated end-to-end against a second test ROM" (Team Tap) and
 * "Attempted validation against AtariJaguarPadtest" (6D controller)
 * subsections.
 *
 * TWO FALSE-POSITIVE TRAPS (both padtest's own documented known issues,
 * neither an emulation bug -- see the app's README):
 *   1. Terrible frame rate: padtest re-detects every controller on every
 *      single frame by design (a real game does it once at boot).  Do not
 *      chase this as a performance regression, and this tool deliberately
 *      does not measure or report frame timing (see CLAUDE.md rule 6: no
 *      wall-clock numbers with a concurrent agent fleet on the host).
 *   2. Small visual glitches: JagStudio (the dev kit padtest links against)
 *      does its own controller polling that conflicts with padtest's main
 *      poll loop.  The author could not disable it.
 * A third trap belongs on this list, not in padtest's docs but in ours:
 * the 6D controller (like the analog/driving controller before it) stays
 * a plain RetroPad -- reported as STDPAD, not SIXDPAD -- until an axis
 * deflects past ANALOG_THRESHOLD.  A "rest" scenario that shows STDPAD is
 * the liveness gate working, not a delivery failure.
 *
 * Build:
 *   cc -O2 -Wall -std=c99 -I. -I./libretro-common/include \
 *      -o test/tools/padtest_probe test/tools/padtest_probe.c \
 *      test/harness/harness.c -ldl -lm
 *
 * Run (one scenario per invocation; see SCENARIOS below):
 *   VJ_EXPECT_BUILD=$(./scripts/build-id.sh) ./test/tools/padtest_probe \
 *      ./virtualjaguar_libretro.dylib test/roms/private/padtest/padtest.rom \
 *      --scenario 6d_live --option virtualjaguar_p1_device=6d \
 *      --out /tmp/padtest_6d_live.ppm
 *
 * The tool always prints, to stdout, the exact digital/analog values it
 * fed the core for the run -- read that alongside the screenshot rather
 * than trusting recollection of what a scenario "should" have sent.
 */

#include "../harness/harness.h"
#include "../../libretro-common/include/libretro.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define PT_MAX_BTN 17

typedef struct {
    const char *name;
    /* Port 0 (Jaguar port 1) digital buttons held, RETRO_DEVICE_ID_JOYPAD_*
     * ids, terminated by -1. */
    int p0_buttons[PT_MAX_BTN];
    /* Team Tap: three extra sockets on frontend ports 2, 3, 4 (teamtap_user[0]
     * in libretro.c), each its own digital button list. */
    int use_teamtap;
    int tap_buttons[3][PT_MAX_BTN];
    /* 6D controller: four analog axes plus four analog-button shoulder
     * reads (fed as real pressure unless digital_fallback is set, in which
     * case the analog-button read is always 0 and the fallback in
     * update_input() -- "if (sh[i]==0 && (ret&bit)) sh[i]=32767" -- has to
     * carry Z/TY on its own, exactly the frontend class that produced the
     * original defect). */
    int use_6d;
    int digital_fallback;
    int32_t lx, ly, rx, ry;      /* left stick X/Y, right stick X/Y */
    int32_t sh_l2, sh_r2, sh_l, sh_r; /* shoulder pressure (or digital press w/ 0 analog) */
} pt_scenario;

static pt_scenario g_sc;

static const int PT_NONE[PT_MAX_BTN] = {
    -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1
};

/* -------------------------------------------------------------------
 * Scenario table.  One entry per padtest_probe --scenario NAME.
 * ------------------------------------------------------------------- */

static void pt_scenario_standard(pt_scenario *sc)
{
    memset(sc, 0, sizeof(*sc));
    sc->name = "standard";
    /* Baseline: plain joypad on port 1, hold Up + A so padtest's
     * print_stdpad_btns highlights two distinct characters we can read
     * off the screenshot ('^' and 'A'). */
    sc->p0_buttons[0] = RETRO_DEVICE_ID_JOYPAD_UP;
    sc->p0_buttons[1] = RETRO_DEVICE_ID_JOYPAD_A;
    sc->p0_buttons[2] = -1;
}

/* Diagnostic only: press every digital button padtest's stdpad table
 * covers (U/D/L/R + P/O/C/B/A), to see whether direction glyphs ever
 * render "pressed" the way letter glyphs visibly do. */
static void pt_scenario_standard_all(pt_scenario *sc)
{
    memset(sc, 0, sizeof(*sc));
    sc->name = "standard_all";
    sc->p0_buttons[0] = RETRO_DEVICE_ID_JOYPAD_UP;
    sc->p0_buttons[1] = RETRO_DEVICE_ID_JOYPAD_DOWN;
    sc->p0_buttons[2] = RETRO_DEVICE_ID_JOYPAD_LEFT;
    sc->p0_buttons[3] = RETRO_DEVICE_ID_JOYPAD_RIGHT;
    sc->p0_buttons[4] = RETRO_DEVICE_ID_JOYPAD_SELECT; /* Pause -> 'P' */
    sc->p0_buttons[5] = RETRO_DEVICE_ID_JOYPAD_START;  /* Option -> 'O' */
    sc->p0_buttons[6] = RETRO_DEVICE_ID_JOYPAD_Y;      /* C */
    sc->p0_buttons[7] = RETRO_DEVICE_ID_JOYPAD_B;      /* B */
    sc->p0_buttons[8] = RETRO_DEVICE_ID_JOYPAD_A;      /* A */
    sc->p0_buttons[9] = -1;
}

static void pt_scenario_dirs_only(pt_scenario *sc)
{
    memset(sc, 0, sizeof(*sc));
    sc->name = "dirs_only";
    sc->p0_buttons[0] = RETRO_DEVICE_ID_JOYPAD_UP;
    sc->p0_buttons[1] = RETRO_DEVICE_ID_JOYPAD_DOWN;
    sc->p0_buttons[2] = RETRO_DEVICE_ID_JOYPAD_LEFT;
    sc->p0_buttons[3] = RETRO_DEVICE_ID_JOYPAD_RIGHT;
    sc->p0_buttons[4] = -1;
}

static void pt_scenario_up_only(pt_scenario *sc)
{
    memset(sc, 0, sizeof(*sc));
    sc->name = "up_only";
    sc->p0_buttons[0] = RETRO_DEVICE_ID_JOYPAD_UP;
    sc->p0_buttons[1] = -1;
}

static void pt_scenario_letters_only(pt_scenario *sc)
{
    memset(sc, 0, sizeof(*sc));
    sc->name = "letters_only";
    sc->p0_buttons[0] = RETRO_DEVICE_ID_JOYPAD_A;
    sc->p0_buttons[1] = RETRO_DEVICE_ID_JOYPAD_B;
    sc->p0_buttons[2] = -1;
}

static void pt_scenario_up_and_pause(pt_scenario *sc)
{
    memset(sc, 0, sizeof(*sc));
    sc->name = "up_and_pause";
    sc->p0_buttons[0] = RETRO_DEVICE_ID_JOYPAD_UP;
    sc->p0_buttons[1] = RETRO_DEVICE_ID_JOYPAD_SELECT; /* Pause -> 'P', row0 B0 */
    sc->p0_buttons[2] = -1;
}

static void pt_scenario_down_and_a(pt_scenario *sc)
{
    memset(sc, 0, sizeof(*sc));
    sc->name = "down_and_a";
    sc->p0_buttons[0] = RETRO_DEVICE_ID_JOYPAD_DOWN;
    sc->p0_buttons[1] = RETRO_DEVICE_ID_JOYPAD_A;
    sc->p0_buttons[2] = -1;
}

static void pt_scenario_teamtap(pt_scenario *sc)
{
    memset(sc, 0, sizeof(*sc));
    sc->name = "teamtap";
    sc->use_teamtap = 1;
    /* Socket 0 (the main port-1 pad): Up */
    sc->p0_buttons[0] = RETRO_DEVICE_ID_JOYPAD_UP;
    sc->p0_buttons[1] = -1;
    /* Socket 1 (frontend port 2): Down */
    sc->tap_buttons[0][0] = RETRO_DEVICE_ID_JOYPAD_DOWN;
    sc->tap_buttons[0][1] = -1;
    /* Socket 2 (frontend port 3): Left */
    sc->tap_buttons[1][0] = RETRO_DEVICE_ID_JOYPAD_LEFT;
    sc->tap_buttons[1][1] = -1;
    /* Socket 3 (frontend port 4): Right */
    sc->tap_buttons[2][0] = RETRO_DEVICE_ID_JOYPAD_RIGHT;
    sc->tap_buttons[2][1] = -1;
}

static void pt_scenario_6d_rest(pt_scenario *sc)
{
    memset(sc, 0, sizeof(*sc));
    sc->name = "6d_rest";
    sc->use_6d = 1;
    sc->p0_buttons[0] = -1;
    /* Every axis centred: liveness must NOT trip. Documents false-positive
     * trap #3 (see file header) -- padtest should show STDPAD, not
     * SIXDPAD, and that is correct, not a bug. */
}

/* Distinct nonzero value per axis so a single screenshot identifies which
 * displayed field corresponds to which fed axis, and so the X sign
 * (TR V10 p.23's prose/figure contradiction) can be read directly: LEFT
 * stick X is pushed RIGHT (positive host value). All buttons pressed too,
 * to check bank-0 (A-D) and bank-1 (E/F/G, descending) against
 * INPUTDEV_SW_* -- Rezero is fed but padtest's own btns_6d table has no
 * glyph for it, so it will NOT appear on screen; that is padtest's
 * limitation, not ours (see docs). */
static void pt_scenario_6d_live(pt_scenario *sc)
{
    memset(sc, 0, sizeof(*sc));
    sc->name = "6d_live";
    sc->use_6d = 1;
    sc->lx = 20500;    /* host RIGHT on left stick -> X (sign under test) */
    sc->ly = -10000;   /* host UP on left stick    -> Y */
    sc->rx = 8000;     /* host RIGHT on right stick -> TX */
    sc->ry = -12000;   /* host UP on right stick    -> TZ */
    sc->sh_r2 = 20000; sc->sh_l2 = 0;   /* Z  = R2 - L2, real pressure */
    sc->sh_r  = 15000; sc->sh_l  = 0;   /* TY = R  - L,  real pressure */
    sc->p0_buttons[0] = RETRO_DEVICE_ID_JOYPAD_A;   /* -> SW_A */
    sc->p0_buttons[1] = RETRO_DEVICE_ID_JOYPAD_B;   /* -> SW_B */
    sc->p0_buttons[2] = RETRO_DEVICE_ID_JOYPAD_Y;   /* -> SW_C */
    sc->p0_buttons[3] = RETRO_DEVICE_ID_JOYPAD_X;   /* -> SW_D */
    sc->p0_buttons[4] = RETRO_DEVICE_ID_JOYPAD_L3;  /* -> SW_E */
    sc->p0_buttons[5] = RETRO_DEVICE_ID_JOYPAD_R3;  /* -> SW_F */
    sc->p0_buttons[6] = RETRO_DEVICE_ID_JOYPAD_START;  /* -> SW_G */
    sc->p0_buttons[7] = RETRO_DEVICE_ID_JOYPAD_SELECT; /* -> SW_REZERO (not shown by padtest) */
    sc->p0_buttons[8] = RETRO_DEVICE_ID_JOYPAD_L2;  /* digital companion to sh_r2/l2 */
    sc->p0_buttons[9] = RETRO_DEVICE_ID_JOYPAD_R2;
    sc->p0_buttons[10] = RETRO_DEVICE_ID_JOYPAD_L;
    sc->p0_buttons[11] = RETRO_DEVICE_ID_JOYPAD_R;
    sc->p0_buttons[12] = -1;
}

static void pt_scenario_6d_x_negative(pt_scenario *sc)
{
    pt_scenario_6d_live(sc);
    sc->name = "6d_x_negative";
    sc->lx = -20500;   /* host LEFT on left stick: the other sign */
}

/* Same axes as 6d_live, but the analog-button reads for L2/R2/L/R are
 * forced to 0 -- the exact "frontend with no analog-button support"
 * scenario that left Z and TY dead in the merged PR.  Only the digital
 * JOYPAD_MASK bit says the shoulder is held; update_input()'s fallback
 * ("if (sh[i]==0 && (ret&bit)) sh[i]=32767") is the only thing that can
 * carry Z/TY across in this run.  If Z or TY reads 0 here while 6d_live
 * shows them nonzero, the fallback is broken -- a real, reportable
 * defect, not a false positive. */
static void pt_scenario_6d_digital_fallback(pt_scenario *sc)
{
    pt_scenario_6d_live(sc);
    sc->name = "6d_digital_fallback";
    sc->digital_fallback = 1;
}

typedef void (*pt_scenario_fn)(pt_scenario *);

static const struct { const char *name; pt_scenario_fn fn; } SCENARIOS[] = {
    { "standard",            pt_scenario_standard },
    { "standard_all",        pt_scenario_standard_all },
    { "dirs_only",            pt_scenario_dirs_only },
    { "up_only",              pt_scenario_up_only },
    { "letters_only",         pt_scenario_letters_only },
    { "up_and_pause",         pt_scenario_up_and_pause },
    { "down_and_a",           pt_scenario_down_and_a },
    { "teamtap",              pt_scenario_teamtap },
    { "6d_rest",              pt_scenario_6d_rest },
    { "6d_live",              pt_scenario_6d_live },
    { "6d_x_negative",        pt_scenario_6d_x_negative },
    { "6d_digital_fallback",  pt_scenario_6d_digital_fallback },
    { NULL, NULL }
};

static int pt_has(const int *list, int id)
{
    int i;
    for (i = 0; i < PT_MAX_BTN && list[i] >= 0; i++)
        if (list[i] == id) return 1;
    return 0;
}

static int16_t pt_input_cb(void *ud, unsigned port, unsigned device,
                            unsigned index, unsigned id)
{
    const pt_scenario *sc = (const pt_scenario *)ud;
    const int *list = NULL;

    if (device == RETRO_DEVICE_JOYPAD)
    {
        if (port == 0)
            list = sc->p0_buttons;
        else if (sc->use_teamtap && port == 2)
            list = sc->tap_buttons[0];
        else if (sc->use_teamtap && port == 3)
            list = sc->tap_buttons[1];
        else if (sc->use_teamtap && port == 4)
            list = sc->tap_buttons[2];
        else
            list = PT_NONE;

        if (id == RETRO_DEVICE_ID_JOYPAD_MASK)
        {
            int16_t mask = 0;
            int i;
            for (i = 0; i < PT_MAX_BTN && list[i] >= 0; i++)
                mask = (int16_t)(mask | (1 << list[i]));
            return mask;
        }
        return (int16_t)(pt_has(list, (int)id) ? 1 : 0);
    }

    if (device == RETRO_DEVICE_ANALOG && port == 0 && sc->use_6d)
    {
        if (index == RETRO_DEVICE_INDEX_ANALOG_LEFT)
        {
            if (id == RETRO_DEVICE_ID_ANALOG_X) return (int16_t)sc->lx;
            if (id == RETRO_DEVICE_ID_ANALOG_Y) return (int16_t)sc->ly;
        }
        else if (index == RETRO_DEVICE_INDEX_ANALOG_RIGHT)
        {
            if (id == RETRO_DEVICE_ID_ANALOG_X) return (int16_t)sc->rx;
            if (id == RETRO_DEVICE_ID_ANALOG_Y) return (int16_t)sc->ry;
        }
        else if (index == RETRO_DEVICE_INDEX_ANALOG_BUTTON)
        {
            if (sc->digital_fallback)
                return 0;
            if (id == RETRO_DEVICE_ID_JOYPAD_L2) return (int16_t)sc->sh_l2;
            if (id == RETRO_DEVICE_ID_JOYPAD_R2) return (int16_t)sc->sh_r2;
            if (id == RETRO_DEVICE_ID_JOYPAD_L)  return (int16_t)sc->sh_l;
            if (id == RETRO_DEVICE_ID_JOYPAD_R)  return (int16_t)sc->sh_r;
        }
    }

    return 0;
}

/* -------------------------------------------------------------------
 * Screenshot: PPM, same convert-with-sips convention as
 * test/tools/cd_visual_verify.c.
 * ------------------------------------------------------------------- */

typedef struct {
    uint32_t *px;
    unsigned  w, h;
    unsigned  cur_frame;
} pt_video;

static void pt_video_cb(void *ud, const void *data, unsigned w, unsigned h,
                         size_t pitch)
{
    pt_video *v = (pt_video *)ud;
    unsigned y;
    const uint8_t *src = (const uint8_t *)data;

    if (!data || !w || !h) return;
    free(v->px);
    v->px = (uint32_t *)malloc((size_t)w * h * 4);
    if (!v->px) return;
    for (y = 0; y < h; y++)
        memcpy(&v->px[y * w], src + y * pitch, (size_t)w * 4);
    v->w = w; v->h = h;
}

static bool pt_frame_cb(void *ud, unsigned frame)
{
    pt_video *v = (pt_video *)ud;
    v->cur_frame = frame;
    return true;
}

static void pt_write_ppm(const pt_video *v, const char *path)
{
    FILE *f;
    unsigned x, y;
    if (!v->px || !v->w || !v->h || !path) return;
    f = fopen(path, "wb");
    if (!f) { fprintf(stderr, "padtest_probe: cannot write %s\n", path); return; }
    fprintf(f, "P6\n%u %u\n255\n", v->w, v->h);
    for (y = 0; y < v->h; y++)
        for (x = 0; x < v->w; x++)
        {
            uint32_t p = v->px[y * v->w + x];
            unsigned char rgb[3];
            rgb[0] = (unsigned char)((p >> 16) & 0xFF);
            rgb[1] = (unsigned char)((p >> 8) & 0xFF);
            rgb[2] = (unsigned char)(p & 0xFF);
            fwrite(rgb, 1, 3, f);
        }
    fclose(f);
    fprintf(stderr, "padtest_probe: wrote %s (%ux%u)\n", path, v->w, v->h);
}

static void pt_dump_scenario(const pt_scenario *sc)
{
    int i;
    printf("padtest_probe: scenario=%s\n", sc->name);
    printf("  p0_buttons:");
    for (i = 0; i < PT_MAX_BTN && sc->p0_buttons[i] >= 0; i++)
        printf(" %d", sc->p0_buttons[i]);
    printf("\n");
    if (sc->use_teamtap)
    {
        int s;
        for (s = 0; s < 3; s++)
        {
            printf("  tap_socket[%d]_buttons:", s + 1);
            for (i = 0; i < PT_MAX_BTN && sc->tap_buttons[s][i] >= 0; i++)
                printf(" %d", sc->tap_buttons[s][i]);
            printf("\n");
        }
    }
    if (sc->use_6d)
    {
        printf("  lx=%d ly=%d rx=%d ry=%d\n", sc->lx, sc->ly, sc->rx, sc->ry);
        printf("  sh_l2=%d sh_r2=%d sh_l=%d sh_r=%d digital_fallback=%d\n",
               sc->sh_l2, sc->sh_r2, sc->sh_l, sc->sh_r, sc->digital_fallback);
    }
}

int main(int argc, char **argv)
{
    harness_config cfg = HARNESS_CONFIG_DEFAULT;
    pt_video video;
    const char *scenario_name = NULL;
    const char *out_path = "/tmp/padtest_probe.ppm";
    int i;

    memset(&video, 0, sizeof(video));

    for (i = 1; i < argc; i++)
    {
        if (strcmp(argv[i], "--scenario") == 0 && i + 1 < argc)
            scenario_name = argv[++i];
        else if (strcmp(argv[i], "--out") == 0 && i + 1 < argc)
            out_path = argv[++i];
    }

    if (!scenario_name)
    {
        fprintf(stderr,
            "usage: padtest_probe <core> <padtest.rom> --scenario NAME "
            "[--out FILE.ppm] [--option K=V ...] [--frames N]\n"
            "scenarios:");
        for (i = 0; SCENARIOS[i].name; i++)
            fprintf(stderr, " %s", SCENARIOS[i].name);
        fprintf(stderr, "\n");
        return 1;
    }

    for (i = 0; SCENARIOS[i].name; i++)
        if (strcmp(SCENARIOS[i].name, scenario_name) == 0) break;
    if (!SCENARIOS[i].name)
    {
        fprintf(stderr, "padtest_probe: unknown scenario '%s'\n", scenario_name);
        return 1;
    }
    SCENARIOS[i].fn(&g_sc);
    pt_dump_scenario(&g_sc);

    cfg.frames = 120;
    cfg.video_callback = pt_video_cb;
    cfg.video_callback_data = &video;
    cfg.frame_callback = pt_frame_cb;
    cfg.frame_callback_data = &video;
    cfg.input_callback = pt_input_cb;
    cfg.input_callback_data = &g_sc;

    if (!harness_init_from_args(&cfg, argc, argv)) return 1;
    if (!cfg.rom_path)
    {
        fprintf(stderr, "padtest_probe: missing ROM path\n");
        return 1;
    }
    if (!harness_load_rom(&cfg)) return 1;

    harness_run(&cfg);

    /* Ground-truth check, independent of padtest's own rendering: read the
     * core's OWN joypad0Buttons[] array directly.  BUTTON_U=0, D=1, L=2,
     * R=3, A=16, B=17, C=18, PAUSE=19, OPTION=20 (src/jerry/joystick.h). */
    {
        uint8_t *jb = (uint8_t *)harness_dlsym(&cfg, "joypad0Buttons");
        if (jb)
            printf("padtest_probe: joypad0Buttons[U,D,L,R,A,B,C,PAUSE,OPTION] "
                   "= %u %u %u %u %u %u %u %u %u\n",
                   jb[0], jb[1], jb[2], jb[3], jb[16], jb[17], jb[18], jb[19], jb[20]);
        else
            fprintf(stderr, "padtest_probe: joypad0Buttons not exported -- "
                            "build the core with TEST_EXPORTS=1\n");
    }

    if (g_sc.use_6d)
    {
        typedef int (*get_type_fn)(int);
        typedef int (*any_attached_fn)(void);
        get_type_fn get_type = (get_type_fn)harness_dlsym(&cfg, "InputDevGetType");
        any_attached_fn any_attached =
            (any_attached_fn)harness_dlsym(&cfg, "InputDevAnyAttached");
        if (get_type)
            printf("padtest_probe: InputDevGetType(0)=%d "
                   "(INPUTDEV_6D is the last enum value; see inputdev.h)\n",
                   get_type(0));
        if (any_attached)
            printf("padtest_probe: InputDevAnyAttached()=%d\n", any_attached());
    }

    /* Direct-register discriminator: replicate padtest's own two probes by
     * poking $F14000/$F14002 through JoystickWriteWord()/JoystickReadWord()
     * (exported under TEST_EXPORTS), bypassing BOTH the 68K and padtest's
     * own (for a bare bank-switching device, broken) decode entirely.  This
     * answers the question InputDevGetType()/InputDevAnyAttached() cannot:
     * not "does the core believe a 6D is attached" but "what does the core
     * actually put on the bus when the exact nibbles padtest uses are
     * written".
     *
     * Probe 1 -- padtest's Team-Tap-detect read: nibble $A on port 1
     * (sockets_row_codes[3][1]=0x5A, low nibble = socket 3 row 1).  This is
     * the ONLY probe detect_ctrl() branches on; it must read cbits_mask
     * (JOYBUTS bit 0) SET for "no adapter" (padtest's else branch, observed
     * as STDPAD) or CLEAR for "adapter present" (padtest's per-socket scan,
     * which is what reaches get_banked_type()).
     *
     * Probe 2 -- padtest's own get_basic_type(port,0) BANKED signature:
     * row2 (nibble $B) must read C-bit CLEAR and row3 (nibble $7) must read
     * C-bit SET for a bank-switching device to be recognised as BANKED
     * *if* detect_ctrl() ever called it on socket 0.  This is independent
     * of probe 1 and tells us whether our 6D's own identification is
     * correct on the socket it actually occupies. */
    {
        typedef void (*write_fn)(uint32_t, uint16_t);
        typedef uint16_t (*read_fn)(uint32_t);
        write_fn wr = (write_fn)harness_dlsym(&cfg, "JoystickWriteWord");
        read_fn  rd = (read_fn)harness_dlsym(&cfg, "JoystickReadWord");

        if (wr && rd)
        {
            uint16_t joybuts;

            /* Probe 1: socket 3 row 1 (Team-Tap detect nibble, port1=$A). */
            wr(0, (uint16_t)(0x8000 | 0x5A));
            joybuts = rd(2);
            printf("padtest_probe: probe1 (socket3/row1, TeamTap-detect) "
                   "JOYBUTS=0x%04X bit0(cbits_mask[port1])=%d "
                   "-> padtest takes the %s branch\n",
                   joybuts, joybuts & 1,
                   (joybuts & 1) ? "STDPAD/hardcode (else)" : "per-socket scan (if)");

            /* Probe 2: socket 0 row 2 (nibble $B) -- BANKED needs C-bit 0. */
            wr(0, (uint16_t)(0x8000 | 0xDB));
            joybuts = rd(2);
            printf("padtest_probe: probe2a (socket0/row2, nibble $B) "
                   "JOYBUTS=0x%04X bit0=%d (BANKED needs 0)\n",
                   joybuts, joybuts & 1);

            /* Probe 2: socket 0 row 3 (nibble $7) -- BANKED needs C-bit 1. */
            wr(0, (uint16_t)(0x8000 | 0xE7));
            joybuts = rd(2);
            printf("padtest_probe: probe2b (socket0/row3, nibble $7) "
                   "JOYBUTS=0x%04X bit0=%d (BANKED needs 1)\n",
                   joybuts, joybuts & 1);
        }
        else
        {
            fprintf(stderr, "padtest_probe: JoystickWriteWord/ReadWord not "
                            "exported -- build the core with TEST_EXPORTS=1\n");
        }
    }

    if (g_sc.use_teamtap)
    {
        typedef bool (*get_tap_fn)(int);
        get_tap_fn get_tap = (get_tap_fn)harness_dlsym(&cfg, "JoystickGetTeamTap");
        uint8_t *jb = (uint8_t *)harness_dlsym(&cfg, "joypad0Buttons");
        if (get_tap)
            printf("padtest_probe: after run, JoystickGetTeamTap(0)=%d "
                   "JoystickGetTeamTap(1)=%d\n", get_tap(0), get_tap(1));
        if (jb)
        {
            int s;
            printf("padtest_probe: joypad0Buttons ground truth "
                   "(U,D,L,R,...,A,B,C @ socket*21):\n");
            for (s = 0; s < 4; s++)
                printf("  socket%d: U=%u D=%u L=%u R=%u A=%u B=%u C=%u\n",
                       s, jb[s*21+0], jb[s*21+1], jb[s*21+2], jb[s*21+3],
                       jb[s*21+16], jb[s*21+17], jb[s*21+18]);
        }
    }

    pt_write_ppm(&video, out_path);
    free(video.px);
    harness_shutdown(&cfg);
    return 0;
}
