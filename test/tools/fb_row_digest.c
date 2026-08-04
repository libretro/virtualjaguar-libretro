/*
 * test/tools/fb_row_digest.c — per-frame framebuffer + audio digest dumper.
 *
 * Runs a ROM headlessly and writes a compact binary digest: geometry, a
 * whole-frame hash and a per-row hash for every presented row, plus a
 * per-frame audio hash.  Two builds can then be diffed row-exactly by
 * test/tools/fb_row_diff.py without keeping raw framebuffers around.
 *
 * Written for the AvP "brown bar" A/B sweep (#178): the claim under test is
 * that the fix only ever changes rows the core previously left unwritten,
 * and only ever changes them to opaque black.  fb_row_diff.py checks exactly
 * that, so a 46-title sweep is validated mechanically instead of by eye.
 *
 * Build:
 *   cc -O2 -Wall -std=c99 -I. -I./src -I./src/core -I./src/tom -I./src/jerry \
 *      -I./src/cd -I./src/bios -I./src/m68000 -I./libretro-common/include \
 *      -o test/tools/fb_row_digest test/tools/fb_row_digest.c \
 *      test/harness/harness.c -ldl -lm
 *
 * Usage:
 *   fb_row_digest <core> <rom> --frames N --out FILE [harness flags...]
 *
 * File format.  Every uint32 is written LITTLE-ENDIAN regardless of host, so
 * the dump is stable and matches fb_row_diff.py, which unpacks with "<".
 *
 * (The row hashes themselves are taken over the raw host framebuffer bytes,
 * so digests are only meaningful when compared between runs on the same
 * host -- which is the intended use, two builds on one machine.  The reader's
 * all-black row constant likewise assumes little-endian XRGB8888 in memory.)
 *
 *   magic  "VJFBDIG3"                        8 bytes
 *   uint32 video_frame_count
 *   uint32 audio_frame_count
 *   video_frame_count records of:
 *     uint32 width, rows, frame_hash, written_extent
 *       (rows is the stored row count, min(presented height, MAX_ROWS=512),
 *        not the presented height; readers must size row_hash[] by it)
 *       (written_extent is 0xFFFFFFFF when the core does not export
 *        TOMGetWrittenRowExtent, i.e. for a pre-fix reference build)
 *     uint32 row_hash[rows]
 *     uint32 band_x0, band_width, band_hash, band_nonblack,
 *            band_first_x, band_first_y
 *       (overscan strip: columns x >= 320, hashed over the same stored rows.
 *        When width > 320: band_x0=320, band_width=width-320, band_hash is
 *        FNV-1a over the strip, band_nonblack counts pixels whose RGB is not
 *        zero, and band_first_x/band_first_y are the first such pixel in
 *        row-major order, or 0xFFFFFFFF each when the band is all black.
 *        When width <= 320 the band block is skipped and the fields are
 *        written as band_x0=0, band_width=0, band_hash=0, band_nonblack=0,
 *        band_first_x=0xFFFFFFFF, band_first_y=0xFFFFFFFF.)
 *   audio_frame_count records of:
 *     uint32 audio_hash   (samples, peaks, non-silent count, RMS L/R)
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#include "../harness/harness.h"

#define MAX_ROWS 512
#define FNV_SEED 2166136261u

typedef struct {
    FILE     *out;
    uint32_t  frames_written;
    uint32_t (*get_extent)(void);
} digest_state;

/* FNV-1a, 32-bit. */
static uint32_t fnv1a(const void *data, size_t len, uint32_t hash)
{
    const uint8_t *p = (const uint8_t *)data;
    size_t i;
    for (i = 0; i < len; i++)
    {
        hash ^= p[i];
        hash *= 16777619u;
    }
    return hash;
}

/* Write little-endian explicitly rather than dumping the host's uint32.
 * fb_row_diff.py unpacks with "<", so a big-endian host writing native order
 * would produce a digest the reader silently misparses. */
static void put_u32(FILE *f, uint32_t v)
{
    uint8_t b[4];
    b[0] = (uint8_t)(v & 0xFF);
    b[1] = (uint8_t)((v >> 8) & 0xFF);
    b[2] = (uint8_t)((v >> 16) & 0xFF);
    b[3] = (uint8_t)((v >> 24) & 0xFF);
    fwrite(b, 1, sizeof(b), f);
}

static void on_video(void *userdata, const void *data,
                     unsigned width, unsigned height, size_t pitch)
{
    digest_state *st = (digest_state *)userdata;
    uint32_t row_hash[MAX_ROWS];
    uint32_t frame_hash = FNV_SEED;
    unsigned row;
    unsigned rows = height;
    uint32_t band_x0 = 0;
    uint32_t band_width = 0;
    uint32_t band_hash = 0;
    uint32_t band_nonblack = 0;
    uint32_t band_first_x = 0xFFFFFFFFu;
    uint32_t band_first_y = 0xFFFFFFFFu;
    unsigned col;

    if (!data || !st->out)
        return;
    if (rows > MAX_ROWS)
        rows = MAX_ROWS;

    for (row = 0; row < rows; row++)
    {
        const uint8_t *line = (const uint8_t *)data + (size_t)row * pitch;
        row_hash[row] = fnv1a(line, (size_t)width * 4, FNV_SEED);
        frame_hash = fnv1a(&row_hash[row], sizeof(uint32_t), frame_hash);
    }

    /* Overscan / border strip: columns at and past the 320-wide active area.
     * AvP presents ~326 wide; every prior check folded these into row hashes. */
    if (width > 320)
    {
        band_x0 = 320;
        band_width = width - 320;
        band_hash = FNV_SEED;
        for (row = 0; row < rows; row++)
        {
            const uint8_t *line = (const uint8_t *)data + (size_t)row * pitch;
            uint32_t pix;
            band_hash = fnv1a(line + (size_t)band_x0 * 4,
                              (size_t)band_width * 4, band_hash);
            for (col = band_x0; col < width; col++)
            {
                /* memcpy, not a byte-wise OR: pitch is a byte stride with no
                 * 4-byte-multiple guarantee, so casting line to uint32_t* and
                 * indexing it is UB on strict-alignment hosts.  Reassembling
                 * the pixel by hand would instead pick the wrong three bytes
                 * on a big-endian host, where XRGB8888-as-uint32 puts RGB at
                 * bytes 1..3 rather than 0..2. */
                memcpy(&pix, line + (size_t)col * 4, sizeof(pix));
                if ((pix & 0x00FFFFFFu) != 0)
                {
                    band_nonblack++;
                    if (band_first_x == 0xFFFFFFFFu)
                    {
                        band_first_x = col;
                        band_first_y = row;
                    }
                }
            }
        }
    }

    put_u32(st->out, width);
    put_u32(st->out, rows);
    put_u32(st->out, frame_hash);
    put_u32(st->out, st->get_extent ? st->get_extent() : 0xFFFFFFFFu);
    for (row = 0; row < rows; row++)
        put_u32(st->out, row_hash[row]);
    put_u32(st->out, band_x0);
    put_u32(st->out, band_width);
    put_u32(st->out, band_hash);
    put_u32(st->out, band_nonblack);
    put_u32(st->out, band_first_x);
    put_u32(st->out, band_first_y);

    st->frames_written++;
}

int main(int argc, char **argv)
{
    harness_config cfg = HARNESS_CONFIG_DEFAULT;
    digest_state st;
    const char *out_path = NULL;
    uint32_t run_audio_hash = FNV_SEED;
    unsigned i;

    memset(&st, 0, sizeof(st));

    for (i = 1; i < (unsigned)argc; i++)
    {
        if (!strcmp(argv[i], "--out") && i + 1 < (unsigned)argc)
        {
            out_path = argv[i + 1];
            /* Neutralise both args so the harness parser ignores them. */
            argv[i] = (char *)"--quiet";
            argv[i + 1] = (char *)"--quiet";
            i++;
        }
    }

    if (!out_path)
    {
        fprintf(stderr, "fb_row_digest: --out FILE is required\n");
        return 2;
    }

    cfg.frames = 1200;
    if (!harness_init_from_args(&cfg, argc, argv))
        return 1;

    st.out = fopen(out_path, "wb");
    if (!st.out)
    {
        fprintf(stderr, "fb_row_digest: cannot write %s\n", out_path);
        return 1;
    }

    /* Only a build carrying the fix exports this. */
    st.get_extent = (uint32_t (*)(void))harness_dlsym(&cfg, "TOMGetWrittenRowExtent");

    cfg.video_callback      = on_video;
    cfg.video_callback_data = &st;

    fwrite("VJFBDIG3", 1, 8, st.out);
    put_u32(st.out, 0);   /* video frame count, patched below */
    put_u32(st.out, 0);   /* audio frame count, patched below */

    if (!harness_load_rom(&cfg))
    {
        fclose(st.out);
        return 1;
    }

    harness_run(&cfg);

    /* Audio trailer.  A video-window change must not touch audio, so these
     * have to match bit-exactly between the two builds.  RMS is included
     * because peak + non-silent count alone are too coarse to notice a
     * changed waveform with the same envelope. */
    for (i = 0; i < cfg.audio.frame_count; i++)
    {
        const harness_audio_frame *af = &cfg.audio.frames[i];
        uint32_t h = FNV_SEED;
        h = fnv1a(&af->samples,   sizeof(af->samples),   h);
        h = fnv1a(&af->peak_l,    sizeof(af->peak_l),    h);
        h = fnv1a(&af->peak_r,    sizeof(af->peak_r),    h);
        h = fnv1a(&af->nonsilent, sizeof(af->nonsilent), h);
        h = fnv1a(&af->rms_l,     sizeof(af->rms_l),     h);
        h = fnv1a(&af->rms_r,     sizeof(af->rms_r),     h);
        put_u32(st.out, h);
        run_audio_hash = fnv1a(&h, sizeof(h), run_audio_hash);
    }

    fseek(st.out, 8, SEEK_SET);
    put_u32(st.out, st.frames_written);
    put_u32(st.out, cfg.audio.frame_count);
    fclose(st.out);

    printf("frames=%u audio_frames=%u total_samples=%zu audio_digest=%08X "
           "extent_export=%s\n",
           st.frames_written, cfg.audio.frame_count, cfg.audio.total_samples,
           run_audio_hash, st.get_extent ? "yes" : "no");

    harness_shutdown(&cfg);
    return 0;
}
