/*
 * test/tools/overscan_probe.c -- overscan-band diagnostic (issue #266).
 *
 * Alien vs Predator presents a 326-column framebuffer while its picture is
 * 320 pixels wide, so the rightmost columns show OP line-buffer content the
 * game parked outside its own picture.  This tool measures that band.
 *
 *   1. COLOUR CENSUS over x >= 320.  A full census, not "first non-black
 *      pixel in row-major order" -- the latter under-reports whenever an
 *      earlier column in the band is also non-black.
 *
 *   2. POKE PERSISTENCE (--poke FRAME).  Stamps the whole band with a
 *      sentinel colour after frame FRAME is presented and reports how many
 *      sentinel pixels survive.  Survivors mean nothing rewrites the band
 *      (a stale-buffer bug); a wipe means the band is re-rendered every
 *      frame (a rendering / cropping bug).
 *
 *   3. RAM-SOURCE CHECK.  For AvP the band is a 1-bpp object at XPOS=320
 *      whose DATA points at main RAM $000000, 32 bytes per display row,
 *      drawn through CLUT[1].  Verifies pixel-for-pixel that display
 *      columns 323/324/325 on row r equal bits 7/6/5 of mainRAM[r * 32].
 *
 *   4. TAILRISK line.  Reports startPos (the number of border columns the
 *      scanline renderer prepends) and how much non-black content sits in
 *      the same number of columns at the right edge -- i.e. what a
 *      symmetric border-fill fix would erase for this title.
 *
 * Build:
 *   cc -O2 -Wall -std=c99 -I. -I./libretro-common/include \
 *      -o test/tools/overscan_probe test/tools/overscan_probe.c \
 *      test/harness/harness.c -ldl -lm
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#include "../harness/harness.h"

#define BAND_X0        320
#define MAX_W          1024
#define MAX_H          512
#define GREEN          0xFF00FC38u
#define SENTINEL       0xFFFF00FFu
#define MAX_CENSUS     24
#define LEFT_HC        188      /* DEFAULT_LEFT_VISIBLE_HC, NTSC */

/* TOM register offsets (see src/tom/tom.c). */
#define R_VMODE  0x28
#define R_BORD1  0x2A
#define R_BORD2  0x2C
#define R_HP     0x2E
#define R_HBB    0x30
#define R_HBE    0x32
#define R_HDB1   0x38
#define R_HDE    0x3C
#define R_VP     0x3E
#define R_VDB    0x46
#define R_VDE    0x48
#define R_BG     0x58

typedef struct {
    uint32_t colour;
    uint32_t count;
} census_entry;

typedef struct {
    unsigned  width, height;
    uint8_t  *tomRam8;
    uint32_t **videoBuffer;
    uint8_t  **mainRAM;
    int      *game_width;
    int      *game_height;

    unsigned  poke_frame;
    unsigned  dump_frame;
    unsigned  first_green_frame;
    int       poked;

    unsigned  frame;
    unsigned  green_count;
    unsigned  sentinel_count;
    unsigned  band_cols[MAX_W];
    unsigned  green_rows_min, green_rows_max;
    census_entry census[MAX_CENSUS];
    unsigned  census_n;
    int       verbose_dump;
    uint8_t   green_map[MAX_H][8];
    uint32_t *fb_last;
} probe;

static uint32_t px(const uint8_t *line, unsigned x)
{
    uint32_t v;
    memcpy(&v, line + (size_t)x * 4, 4);
    return v;
}

static uint16_t reg(const probe *p, unsigned off)
{
    if (!p->tomRam8)
        return 0;
    return (uint16_t)((p->tomRam8[off] << 8) | p->tomRam8[off + 1]);
}

static void census_add(probe *p, uint32_t colour)
{
    unsigned i;
    for (i = 0; i < p->census_n; i++)
    {
        if (p->census[i].colour == colour)
        {
            p->census[i].count++;
            return;
        }
    }
    if (p->census_n < MAX_CENSUS)
    {
        p->census[p->census_n].colour = colour;
        p->census[p->census_n].count = 1;
        p->census_n++;
    }
}

static void on_video(void *ud, const void *data, unsigned width,
                     unsigned height, size_t pitch)
{
    probe *p = (probe *)ud;
    const uint8_t *base = (const uint8_t *)data;
    unsigned y, x;

    if (!data || width == 0 || height == 0)
        return;

    p->width = width;
    p->height = height;
    p->green_count = 0;
    p->sentinel_count = 0;
    p->census_n = 0;
    p->green_rows_min = 0xFFFFFFFFu;
    p->green_rows_max = 0;
    memset(p->band_cols, 0, sizeof(p->band_cols));
    memset(p->green_map, 0, sizeof(p->green_map));

    if (!p->fb_last)
        p->fb_last = (uint32_t *)malloc((size_t)MAX_W * MAX_H * 4);
    if (p->fb_last && width <= MAX_W && height <= MAX_H)
    {
        for (y = 0; y < height; y++)
            memcpy(p->fb_last + (size_t)y * width,
                   base + (size_t)y * pitch, (size_t)width * 4);
    }

    if (width <= BAND_X0)
        return;

    for (y = 0; y < height; y++)
    {
        const uint8_t *line = base + (size_t)y * pitch;
        for (x = BAND_X0; x < width && x < MAX_W; x++)
        {
            uint32_t v = px(line, x);
            census_add(p, v);
            if (v == GREEN)
            {
                p->green_count++;
                p->band_cols[x]++;
                if (y < MAX_H && x - BAND_X0 < 8)
                    p->green_map[y][x - BAND_X0] = 1;
                if (y < p->green_rows_min)
                    p->green_rows_min = y;
                if (y > p->green_rows_max)
                    p->green_rows_max = y;
            }
            if (v == SENTINEL)
                p->sentinel_count++;
        }
    }

    if (p->green_count && p->first_green_frame == 0xFFFFFFFFu)
        p->first_green_frame = p->frame;
}

/* Display columns 323/324/325 must equal bits 7/6/5 of mainRAM[row * 32]
 * if the band really is the parked 1-bpp object reading low main RAM. */
static void verify_ram_source(const probe *p)
{
    unsigned r, k, match = 0, mismatch = 0, predicted = 0, observed = 0;
    const uint8_t *ram;

    if (!p->mainRAM || !*p->mainRAM)
        return;
    ram = *p->mainRAM;

    for (r = 0; r < p->height && r < MAX_H; r++)
    {
        for (k = 0; k < 3; k++)
        {
            unsigned bit = (ram[(r * 32) & 0x1FFFFF] >> (7 - k)) & 1;
            unsigned got = p->green_map[r][3 + k];
            predicted += bit;
            observed += got;
            if (bit == got)
                match++;
            else
                mismatch++;
        }
    }
    printf("  RAM-source check (mainRAM[row*32] bits 7,6,5 -> x=323,324,325):"
           " match=%u mismatch=%u predicted=%u observed=%u\n",
           match, mismatch, predicted, observed);
    printf("  mainRAM[32*32=$400]=$%02X (bit6=%u -> x=324,y=32)\n",
           ram[0x400], (unsigned)((ram[0x400] >> 6) & 1));
}

static void dump_regs(const probe *p, const char *tag)
{
    uint16_t b1 = reg(p, R_BORD1), b2 = reg(p, R_BORD2);

    printf("  [%s] VMODE=%04X BORD1=%04X BORD2=%04X BG=%04X "
           "HP=%u HBB=%u HBE=%u HDB1=%u HDE=%u VP=%u VDB=%u VDE=%u\n",
           tag, reg(p, R_VMODE), b1, b2, reg(p, R_BG), reg(p, R_HP),
           reg(p, R_HBB), reg(p, R_HBE), reg(p, R_HDB1), reg(p, R_HDE),
           reg(p, R_VP), reg(p, R_VDB), reg(p, R_VDE));
    /* tom.c border fill: g = tomRam8[BORD1], r = tomRam8[BORD1+1],
     * b = tomRam8[BORD2+1] */
    printf("  [%s] border pixel = 0xFF%02X%02X%02X\n",
           tag, b1 & 0xFF, (b1 >> 8) & 0xFF, b2 & 0xFF);
}

static void print_census(const probe *p)
{
    unsigned i, x;

    printf("  band colour census (x>=%d, %u cols x %u rows):\n",
           BAND_X0, p->width > BAND_X0 ? p->width - BAND_X0 : 0, p->height);
    for (i = 0; i < p->census_n; i++)
        printf("      0x%08X  x%u\n", p->census[i].colour, p->census[i].count);
    printf("  green per column:");
    for (x = BAND_X0; x < p->width && x < MAX_W; x++)
        printf(" [%u]=%u", x, p->band_cols[x]);
    printf("\n");
    if (p->green_count)
        printf("  green rows %u..%u, total %u\n",
               p->green_rows_min, p->green_rows_max, p->green_count);
}

static bool on_frame(void *ud, unsigned frame)
{
    probe *p = (probe *)ud;

    p->frame = frame;

    if (p->verbose_dump || frame == p->dump_frame
        || (p->green_count && p->first_green_frame == frame))
    {
        printf("frame %u: %ux%u green=%u sentinel=%u\n",
               frame, p->width, p->height, p->green_count, p->sentinel_count);
        print_census(p);
        dump_regs(p, "regs");
        if (p->tomRam8)
        {
            unsigned k;
            printf("  CLUT[0..7]:");
            for (k = 0; k < 8; k++)
                printf(" %u=%04X", k,
                       (unsigned)((p->tomRam8[0x400 + k * 2] << 8)
                                  | p->tomRam8[0x400 + k * 2 + 1]));
            printf("\n");
        }
        verify_ram_source(p);
    }

    /* If the emulator rewrites the band every frame the sentinel is gone
     * next frame; if it survives, nothing writes there. */
    if (p->poke_frame != 0xFFFFFFFFu && frame == p->poke_frame
        && p->videoBuffer && *p->videoBuffer
        && p->game_width && *p->game_width > BAND_X0)
    {
        unsigned y, x;
        unsigned w = (unsigned)*p->game_width;
        unsigned h = (unsigned)*p->game_height;
        uint32_t *vb = *p->videoBuffer;
        unsigned pre_match = 0, pre_total = 0;

        /* Prove the poke targets the buffer that was just presented: the
         * band must currently hold exactly what the video callback saw.
         * Without this, a poke into an unused buffer is indistinguishable
         * from "the band is rewritten every frame". */
        for (y = 0; y < h; y++)
            for (x = BAND_X0; x < w; x++)
            {
                pre_total++;
                if (p->fb_last
                    && vb[(size_t)y * w + x] == p->fb_last[(size_t)y * w + x])
                    pre_match++;
            }

        for (y = 0; y < h; y++)
            for (x = BAND_X0; x < w; x++)
                vb[(size_t)y * w + x] = SENTINEL;

        p->poked = 1;
        printf("frame %u: POKE-TARGET CHECK videoBuffer==presented for %u/%u "
               "band pixels\n", frame, pre_match, pre_total);
        printf("frame %u: POKED band x=%u..%u y=0..%u with 0x%08X "
               "(readback[%u][%u]=0x%08X)\n",
               frame, BAND_X0, w - 1, h - 1, SENTINEL, 32u, 324u,
               vb[(size_t)32 * w + 324]);
    }

    if (p->poked && frame > p->poke_frame && frame <= p->poke_frame + 5)
        printf("frame %u: post-poke sentinel=%u green=%u\n",
               frame, p->sentinel_count, p->green_count);

    return true;
}

/* How many columns of line-buffer content sit past the game's picture, and
 * how much of it is non-black -- the blast radius of a symmetric-border fix. */
static void report_tail_risk(const probe *p, const char *rom)
{
    uint16_t vmode = reg(p, R_VMODE);
    unsigned pwidth = ((vmode & 0x0E00) >> 9) + 1;
    unsigned scale = (pwidth >= 8) ? (pwidth / 4) : 1;
    int hdb1 = (int)reg(p, R_HDB1);
    int sp = (hdb1 - LEFT_HC) / (int)pwidth;
    unsigned spd = (sp > 0) ? (unsigned)sp * scale : 0;
    unsigned tail_nonblack = 0, y, x;

    if (p->fb_last && spd > 0 && p->width > spd)
    {
        for (y = 0; y < p->height; y++)
            for (x = p->width - spd; x < p->width; x++)
                if (p->fb_last[(size_t)y * p->width + x] & 0x00FFFFFF)
                    tail_nonblack++;
    }
    printf("TAILRISK rom=%s w=%u h=%u pwidth=%u hdb1=%d startPos=%d "
           "tailcols=%u tail_nonblack=%u green=%u\n",
           rom ? rom : "?", p->width, p->height, pwidth, hdb1, sp, spd,
           tail_nonblack, p->green_count);
}

int main(int argc, char **argv)
{
    harness_config cfg = HARNESS_CONFIG_DEFAULT;
    probe p;
    int i;

    memset(&p, 0, sizeof(p));
    p.poke_frame = 0xFFFFFFFFu;
    p.dump_frame = 0xFFFFFFFFu;
    p.first_green_frame = 0xFFFFFFFFu;

    for (i = 1; i < argc; i++)
    {
        if (!strcmp(argv[i], "--poke") && i + 1 < argc)
            p.poke_frame = (unsigned)strtoul(argv[++i], NULL, 10);
        else if (!strcmp(argv[i], "--dump") && i + 1 < argc)
            p.dump_frame = (unsigned)strtoul(argv[++i], NULL, 10);
        else if (!strcmp(argv[i], "--verbose-dump"))
            p.verbose_dump = 1;
    }

    cfg.frames = 400;
    if (!harness_init_from_args(&cfg, argc, argv))
        return 1;
    cfg.video_callback = on_video;
    cfg.video_callback_data = &p;
    cfg.frame_callback = on_frame;
    cfg.frame_callback_data = &p;

    if (!harness_load_rom(&cfg))
        return 1;

    p.tomRam8     = (uint8_t *)harness_dlsym(&cfg, "tomRam8");
    p.videoBuffer = (uint32_t **)harness_dlsym(&cfg, "videoBuffer");
    p.mainRAM     = (uint8_t **)harness_dlsym(&cfg, "jaguarMainRAM");
    p.game_width  = (int *)harness_dlsym(&cfg, "game_width");
    p.game_height = (int *)harness_dlsym(&cfg, "game_height");

    harness_run(&cfg);

    report_tail_risk(&p, cfg.rom_path);

    printf("\n=== summary ===\n");
    printf("first green frame: %u\n", p.first_green_frame);
    printf("final frame %u: green=%u sentinel=%u\n",
           p.frame, p.green_count, p.sentinel_count);
    print_census(&p);
    dump_regs(&p, "final");
    verify_ram_source(&p);

    free(p.fb_last);
    harness_shutdown(&cfg);
    return 0;
}
