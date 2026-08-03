/*
 * test/tools/tom_window_trace.c — trace TOM's vertical display window.
 *
 * Prints VDB / VDE / VP / presented geometry / written-row extent whenever
 * any of them changes, plus the framebuffer contents of the last few rows so
 * an unwritten tail is directly visible.  Written to pin down the AvP
 * "brown bar" window mismatch (#178).
 *
 * Build:
 *   cc -O2 -Wall -std=c99 -I. -I./src -I./src/core -I./src/tom -I./src/jerry \
 *      -I./src/cd -I./src/bios -I./src/m68000 -I./libretro-common/include \
 *      -o test/tools/tom_window_trace test/tools/tom_window_trace.c \
 *      test/harness/harness.c -ldl -lm
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#include "../harness/harness.h"

typedef struct {
    const uint8_t *tom_ram;
    uint32_t (*get_extent)(void);
    uint32_t (*get_height)(void);
    unsigned frame;
    unsigned last_vdb, last_vde, last_vp, last_w, last_h, last_ext;
    int      have_last;
    /* last presented frame, for tail-row inspection */
    uint32_t tail[8][4];
    unsigned tail_rows;
    unsigned top_nonblack;   /* non-black rows among the top 16 */
} trace_state;

/* Count rows in the top `n` of the presented frame that are not entirely
 * opaque black.  Used to check that a reset does not present the previous
 * session's pixels in the rows above VDB. */
static unsigned nonblack_top_rows(const uint32_t *fb, unsigned w, unsigned h,
                                  size_t pitch, unsigned n)
{
    unsigned r, c, count = 0;
    if (n > h)
        n = h;
    for (r = 0; r < n; r++)
    {
        const uint32_t *line =
            (const uint32_t *)((const uint8_t *)fb + (size_t)r * pitch);
        for (c = 0; c < w; c++)
            if (line[c] != 0xFF000000u)
            {
                count++;
                break;
            }
    }
    return count;
}

static unsigned rd16(const uint8_t *p, unsigned off)
{
    return ((unsigned)p[off] << 8) | p[off + 1];
}

static void on_video(void *ud, const void *data, unsigned w, unsigned h,
                     size_t pitch)
{
    trace_state *st = (trace_state *)ud;
    unsigned vdb, vde, vp, ext;
    unsigned r;

    if (!data || !st->tom_ram)
        return;

    vdb = rd16(st->tom_ram, 0x46);
    vde = rd16(st->tom_ram, 0x48);
    vp  = rd16(st->tom_ram, 0x3E);
    ext = st->get_extent ? st->get_extent() : 0;

    if (!st->have_last || vdb != st->last_vdb || vde != st->last_vde ||
        vp != st->last_vp || w != st->last_w || h != st->last_h ||
        ext != st->last_ext)
    {
        printf("frame %5u: VDB=%-5u VDE=%-5u VP=%-5u  present=%ux%u  "
               "modeHeight=%u  writtenExtent=%u%s\n",
               st->frame, vdb, vde, vp, w, h,
               st->get_height ? st->get_height() : 0, ext,
               (ext < h) ? "   <-- TAIL GAP" : "");
        st->have_last = 1;
        st->last_vdb = vdb; st->last_vde = vde; st->last_vp = vp;
        st->last_w = w; st->last_h = h; st->last_ext = ext;
    }

    /* Keep the bottom 8 rows' first 4 pixels so the caller can see whether
     * the tail is stale content, border colour, or black. */
    st->tail_rows = (h < 8) ? h : 8;
    for (r = 0; r < st->tail_rows; r++)
    {
        const uint32_t *line =
            (const uint32_t *)((const uint8_t *)data +
                               (size_t)(h - st->tail_rows + r) * pitch);
        memcpy(st->tail[r], line, sizeof(uint32_t) * 4);
    }

    st->top_nonblack = nonblack_top_rows((const uint32_t *)data, w, h, pitch, 16);
}

static int arg_matches_frame(const char *flag, unsigned f, int argc, char **argv)
{
    int i;
    for (i = 1; i < argc; i++)
        if (!strcmp(argv[i], flag) && i + 1 < argc &&
            (unsigned)atoi(argv[i + 1]) == f)
            return 1;
    return 0;
}

int main(int argc, char **argv)
{
    harness_config cfg = HARNESS_CONFIG_DEFAULT;
    trace_state st;
    void (*lr_reset)(void);
    unsigned f;

    memset(&st, 0, sizeof(st));

    cfg.frames = 3000;
    if (!harness_init_from_args(&cfg, argc, argv))
        return 1;

    st.tom_ram    = (const uint8_t *)harness_dlsym(&cfg, "tomRam8");
    st.get_extent = (uint32_t (*)(void))harness_dlsym(&cfg, "TOMGetWrittenRowExtent");
    st.get_height = (uint32_t (*)(void))harness_dlsym(&cfg, "TOMGetVideoModeHeight");
    if (!st.tom_ram)
    {
        fprintf(stderr, "tom_window_trace: tomRam8 not exported "
                        "(build the core with TEST_EXPORTS=1)\n");
        return 1;
    }

    cfg.video_callback      = on_video;
    cfg.video_callback_data = &st;

    if (!harness_load_rom(&cfg))
        return 1;

    lr_reset = (void (*)(void))harness_dlsym(&cfg, "retro_reset");

    for (f = 0; f < cfg.frames; f++)
    {
        st.frame = f;
        harness_step(&cfg);

        /* --reset-at N: reset after frame N, then report how many of the
         * top 16 presented rows are still non-black on the next frame.
         * Those rows are the ones TOM cannot write until the game
         * reprograms a video register, so a non-zero count means the
         * reset presented the previous session's pixels. */
        if (lr_reset && arg_matches_frame("--reset-at", f, argc, argv))
        {
            printf("  frame %u: top-16 non-black rows before reset: %u\n",
                   f, st.top_nonblack);
            lr_reset();
            st.frame = f + 1;
            harness_step(&cfg);
            printf("  frame %u: top-16 non-black rows after reset:  %u%s\n",
                   f + 1, st.top_nonblack,
                   st.top_nonblack ? "   <-- STALE" : "   (clean)");
        }

        if (arg_matches_frame("--dump-at", f, argc, argv))
        {
            unsigned r, c;
            printf("  tail rows at frame %u (bottom %u rows, first 4 px):\n",
                   f, st.tail_rows);
            for (r = 0; r < st.tail_rows; r++)
            {
                printf("    row %-3u:", st.last_h - st.tail_rows + r);
                for (c = 0; c < 4; c++)
                    printf(" %08X", st.tail[r][c]);
                printf("\n");
            }
        }
    }

    harness_shutdown(&cfg);
    return 0;
}
