/* test_texreplace.c -- Test gates for the texture replacement pipeline
 * (issue #369, deliverable 2).  Spec: docs/texture-dump.md,
 * "Replacement pipeline".
 *
 * One binary runs the loaded ROM four times and asserts, in order:
 *
 *   1. nopack_inert      replacement ENABLED with no pack present is
 *                        bit-identical to disabled: per-frame
 *                        framebuffer hashes, savestate digests
 *                        (frames N/2, N) and every battery blit's
 *                        destination RAM bytes -- and the hot gate
 *                        (texReplaceEnabled) stays off.
 *   2. pack_machine_inert  replacement ENABLED with a synthetic pack:
 *                        the EMULATED MACHINE is still bit-identical
 *                        to disabled (framebuffer hashes, savestate
 *                        digests, battery destination RAM).  The
 *                        pipeline is host-side presentation only.
 *   3. pack_presents     the same run's shadow framebuffer carries the
 *                        pack art: for every replaced tile, every
 *                        destination word resolves (value-tagged) to
 *                        the pack pixel's RGB.  Dimension-mismatched,
 *                        alpha-holed and non-16bpp pack entries
 *                        produce exactly no stores.
 *   4. presentation_path the function the OP calls per 16bpp pixel
 *                        (ShadowFBLineFromRAM) lands pack RGB in the
 *                        shadow line buffer the scanline renderer
 *                        reads.
 *   5. determinism       two identical pack runs agree on everything.
 *   6. hires_machine_inert  the FOUR-WAY identity gate for tier 3
 *                        (>1x pack art): {1x, 2x} x {pack, no pack}
 *                        all agree on savestate digests and on every
 *                        battery destination, so neither the
 *                        resolution option nor a pack -- nor the two
 *                        together -- is observable to the machine.
 *                        Also asserts the Nx replacement plane stays
 *                        inert at 2x with no pack.
 *   7. hires_pack_presents  at 2x the Nx surface DELIVERS the art:
 *                        ShadowHiresLineFromRAM (the function op.c
 *                        calls per 16bpp pixel) lands the expected
 *                        per-SUBPIXEL RGB in the Nx line replacement
 *                        plane the scanline renderer reads -- for 2x
 *                        art at its own resolution, for 1x art
 *                        replicated, and for nothing at all where the
 *                        dimensions do not match -- and the 1x
 *                        representative recorded per replaced word is
 *                        the pack's TOP-LEFT subpixel, which is what a
 *                        hi-res resolve miss degrades to.
 *   8. hires_determinism  two identical 2x pack runs agree.
 *   9. hires_static_persist (#528) a tile blitted ONCE and then left
 *                        alone still delivers its Nx art after real
 *                        presented frames have carried the frame epoch
 *                        past HIRES_EPOCH_WINDOW.  The epoch is not
 *                        faked: harness_step drives retro_run, which
 *                        drives ShadowHiresFrameTick.  Guarded by a
 *                        precondition that the destination RAM is
 *                        byte-unchanged across those frames, so "art
 *                        gone" can only mean expiry.
 *  10. rgb16_presents   (#528) the RGB16-direct scanout paths present
 *                        the pack -- 1x and Nx through the same seam
 *                        (TomLinePackRGB) -- and present ONLY the pack:
 *                        a true-color CRY reconstruction in the same
 *                        shadow line plane must never substitute on an
 *                        RGB16 scanout.
 *
 * The synthetic pack is built from FIRST PRINCIPLES: the test computes
 * each battery tile's identity hash from the documented contract
 * (FNV-1a 64 over 'VJTD'|ver|bpp|w|h|bytes) and writes
 * <hash16>.png itself (stored-deflate PNG, no external encoder).  A
 * lookup hit therefore also cross-checks the frozen contract against
 * an independent implementation.
 *
 * Build (see the Makefile rule):
 *   cc -O2 -Wall -std=c99 -I./libretro-common/include \
 *      -o test/tools/test_texreplace test/tools/test_texreplace.c \
 *      test/harness/harness.c src/core/crc32.c -ldl -lm
 *
 * Usage: ./test/tools/test_texreplace <core> [rom] [--frames N]
 *          [--json] [--quiet]
 *
 * Exit: 0 PASS, 1 FAIL, 2 SKIP (ROM missing).
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <sys/stat.h>
#include <dirent.h>
#include <unistd.h>

#include "../harness/harness.h"
/* Real header, not mirrored constants: the Nx line-plane index formula
 * and the replacement-entry encoding are exactly what the OP and the
 * scanline renderer use, so a change there must break this test. */
#include "../../src/tom/shadowfb.h"

/* src/core/crc32.c (compiled into this binary): zlib/PNG CRC-32. */
int crc32_calcCheckSum(unsigned char *data, unsigned int length);

#define MAX_FRAMES 1200

#define FNV_OFFSET 0xCBF29CE484222325ULL
#define FNV_PRIME  0x00000100000001B3ULL

/* ---------- battery layout ---------- */

#define BAT_SRC  0x180000u
#define BAT_DST  0x1C0000u
#define BAT_DST_STRIDE 0x1000u

/* pack_kind */
#define PK_RGB   0   /* correct RGB pack PNG -> full replacement      */
#define PK_DIMS  1   /* wrong-dimension PNG  -> zero stores           */
#define PK_RGBA  2   /* RGBA with alpha holes -> partial replacement  */
#define PK_GRAY  3   /* gray PNG for an 8bpp tile -> tier skip        */
#define PK_RGB32 4   /* RGB PNG for a 32bpp tile -> tier skip         */

typedef struct {
    unsigned w, h;
    unsigned psize;      /* FLAGS pixel-size field                    */
    unsigned log2w;
    int      dsta2;      /* A1 is the source                          */
    int      pack_kind;
    unsigned expect_hits; /* pixels that must resolve to pack RGB     */
} bat_tile;

static const bat_tile tiles[] = {
    {  8,  8, 4, 3, 0, PK_RGB,   64 },
    {  8,  8, 4, 3, 0, PK_RGB,   64 },
    {  8,  8, 4, 3, 0, PK_RGB,   64 },
    {  8,  8, 4, 3, 1, PK_RGB,   64 },   /* DSTA2: A1 reads, A2 writes */
    { 16, 16, 4, 4, 0, PK_RGB,  256 },
    {  8,  8, 4, 3, 0, PK_DIMS,   0 },
    {  8,  8, 4, 3, 0, PK_RGBA,  62 },   /* 2 alpha holes              */
    { 16, 16, 3, 4, 0, PK_GRAY,   0 },   /* 8bpp source: future tier   */
    {  4,  4, 5, 2, 0, PK_RGB32,  0 },   /* 32bpp source: future tier  */
};
#define N_TILES ((unsigned)(sizeof(tiles) / sizeof(tiles[0])))

static unsigned tile_bpp(const bat_tile *t)
{
    return 1u << t->psize;
}

static unsigned tile_bytes(const bat_tile *t)
{
    return t->w * t->h * tile_bpp(t) / 8;
}

/* Deterministic source fill, shared between the RAM write and the
 * from-first-principles hash. */
static uint8_t fill_byte(unsigned tile, unsigned i)
{
    return (uint8_t)((i * 31u + (0x10u + tile) * 97u + (i >> 5)) & 0xFF);
}

/* Expected pack art, shared between PNG generation and assertion. */
static void pack_rgb(unsigned tile, unsigned x, unsigned y, uint8_t *rgb)
{
    rgb[0] = (uint8_t)((x * 37u + y * 11u + tile * 71u) & 0xFF);
    rgb[1] = (uint8_t)((x * 5u + y * 29u + 153u) & 0xFF);
    rgb[2] = (uint8_t)((x * 13u + y * 3u + tile) & 0xFF);
}

/* Author alpha ("keep the game's pixel") in PNG coordinates: the two
 * opposite corners of whatever size the PNG is. */
static int pack_hole_at(unsigned tile, unsigned pw, unsigned ph,
                        unsigned X, unsigned Y)
{
    if (tiles[tile].pack_kind != PK_RGBA)
        return 0;
    return (X == 0 && Y == 0) || (X == pw - 1 && Y == ph - 1);
}

/* One battery tile in the 2x pack is deliberately left at 1x art: at Nx
 * the hi-res line entry wins wherever it hits, so 1x art must be
 * replicated into the Nx surface or it would be MASKED outright. */
#define HIRES_1X_TILE 2u

/* PNG scale for this tile in this pack.  PK_DIMS must stay invalid in
 * BOTH packs -- at 2x the old "twice the dumped size" mismatch is a
 * legal tier-3 entry, so it becomes 3x there. */
static unsigned pack_scale(unsigned tile, int hires)
{
    if (tiles[tile].pack_kind == PK_DIMS)
        return hires ? 3u : 2u;
    if (hires && tile != HIRES_1X_TILE)
        return 2u;
    return 1u;
}

/* Identity-contract hash, implemented independently of the core.  For
 * every battery tile the window is pixel-mode, pitch 0, origin (0,0)
 * with width == row length, so the serialized byte stream is exactly
 * the fill bytes in order. */
static uint64_t tile_hash(unsigned tile)
{
    const bat_tile *t = &tiles[tile];
    uint64_t h = FNV_OFFSET;
    uint8_t hdr[10];
    unsigned i, n = tile_bytes(t);

    hdr[0] = 'V'; hdr[1] = 'J'; hdr[2] = 'T'; hdr[3] = 'D';
    hdr[4] = 1;
    hdr[5] = (uint8_t)tile_bpp(t);
    hdr[6] = (uint8_t)(t->w & 0xFF);
    hdr[7] = (uint8_t)((t->w >> 8) & 0xFF);
    hdr[8] = (uint8_t)(t->h & 0xFF);
    hdr[9] = (uint8_t)((t->h >> 8) & 0xFF);
    for (i = 0; i < 10; i++) {
        h ^= hdr[i];
        h *= FNV_PRIME;
    }
    for (i = 0; i < n; i++) {
        h ^= fill_byte(tile, i);
        h *= FNV_PRIME;
    }
    return h;
}

static uint32_t tile_dst(unsigned tile)
{
    return BAT_DST + tile * BAT_DST_STRIDE;
}

/* ---------- minimal PNG writer (stored deflate) ---------- */

static uint32_t adler32(const uint8_t *p, size_t n)
{
    uint32_t a = 1, b = 0;
    size_t i;
    for (i = 0; i < n; i++) {
        a = (a + p[i]) % 65521u;
        b = (b + a) % 65521u;
    }
    return (b << 16) | a;
}

static void put_be32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)(v >> 24);
    p[1] = (uint8_t)(v >> 16);
    p[2] = (uint8_t)(v >> 8);
    p[3] = (uint8_t)v;
}

static void png_chunk(FILE *f, const char *tag, const uint8_t *data,
                      uint32_t len)
{
    uint8_t hdr[8], crcb[4];
    uint8_t *tmp = (uint8_t *)malloc(len + 4);
    uint32_t crc;

    put_be32(hdr, len);
    memcpy(hdr + 4, tag, 4);
    fwrite(hdr, 1, 8, f);
    if (len)
        fwrite(data, 1, len, f);
    memcpy(tmp, tag, 4);
    if (len)
        memcpy(tmp + 4, data, len);
    crc = (uint32_t)crc32_calcCheckSum(tmp, len + 4);
    put_be32(crcb, crc);
    fwrite(crcb, 1, 4, f);
    free(tmp);
}

/* Write an 8-bit PNG.  ctype 0 = gray (1ch), 2 = RGB (3ch),
 * 6 = RGBA (4ch).  raw = w*h*ch bytes. */
static int write_png(const char *path, unsigned w, unsigned h,
                     unsigned ctype, const uint8_t *raw)
{
    static const uint8_t sig[8] = { 137, 80, 78, 71, 13, 10, 26, 10 };
    unsigned ch = (ctype == 0) ? 1 : (ctype == 2) ? 3 : 4;
    unsigned stride = w * ch;
    size_t rawlen = (size_t)h * (stride + 1);
    uint8_t *scan = (uint8_t *)malloc(rawlen);
    size_t zmax = rawlen + 6 + 5 * (rawlen / 65535 + 1);
    uint8_t *z = (uint8_t *)malloc(zmax);
    size_t zlen = 0, off = 0;
    unsigned r;
    uint8_t ihdr[13];
    FILE *f;

    if (!scan || !z) {
        free(scan);
        free(z);
        return 0;
    }
    for (r = 0; r < h; r++) {
        scan[(size_t)r * (stride + 1)] = 0;   /* filter: None */
        memcpy(scan + (size_t)r * (stride + 1) + 1, raw + (size_t)r * stride,
               stride);
    }

    /* zlib wrapper + stored deflate blocks. */
    z[zlen++] = 0x78;
    z[zlen++] = 0x01;
    while (off < rawlen) {
        size_t blk = rawlen - off;
        int last;
        if (blk > 65535)
            blk = 65535;
        last = (off + blk == rawlen);
        z[zlen++] = (uint8_t)(last ? 1 : 0);
        z[zlen++] = (uint8_t)(blk & 0xFF);
        z[zlen++] = (uint8_t)(blk >> 8);
        z[zlen++] = (uint8_t)(~blk & 0xFF);
        z[zlen++] = (uint8_t)((~blk >> 8) & 0xFF);
        memcpy(z + zlen, scan + off, blk);
        zlen += blk;
        off += blk;
    }
    {
        uint32_t ad = adler32(scan, rawlen);
        z[zlen++] = (uint8_t)(ad >> 24);
        z[zlen++] = (uint8_t)(ad >> 16);
        z[zlen++] = (uint8_t)(ad >> 8);
        z[zlen++] = (uint8_t)ad;
    }

    f = fopen(path, "wb");
    if (!f) {
        free(scan);
        free(z);
        return 0;
    }
    fwrite(sig, 1, 8, f);
    put_be32(ihdr, w);
    put_be32(ihdr + 4, h);
    ihdr[8]  = 8;                /* bit depth  */
    ihdr[9]  = (uint8_t)ctype;
    ihdr[10] = 0;
    ihdr[11] = 0;
    ihdr[12] = 0;                /* non-interlaced */
    png_chunk(f, "IHDR", ihdr, 13);
    png_chunk(f, "IDAT", z, (uint32_t)zlen);
    png_chunk(f, "IEND", NULL, 0);
    fclose(f);
    free(scan);
    free(z);
    return 1;
}

/* Build the synthetic pack under <sysdir>/vj_texpacks/<crc8>/.
 * hires != 0 builds the tier-3 pack: 2x art for every replaceable tile
 * except HIRES_1X_TILE (1x art, which must replicate). */
static int build_pack(const char *sysdir, uint32_t crc, int hires)
{
    char dir[1200], path[1400];
    unsigned tile;

    snprintf(dir, sizeof(dir), "%s/vj_texpacks", sysdir);
    mkdir(dir, 0777);
    snprintf(dir, sizeof(dir), "%s/vj_texpacks/%08x", sysdir, (unsigned)crc);
    mkdir(dir, 0777);

    for (tile = 0; tile < N_TILES; tile++) {
        const bat_tile *t = &tiles[tile];
        uint64_t key = tile_hash(tile);
        unsigned s = pack_scale(tile, hires);
        unsigned pw = t->w * s, ph = t->h * s, ctype = 2, ch;
        uint8_t *raw;
        unsigned x, y;
        int ok;

        switch (t->pack_kind) {
            case PK_RGBA:
                ctype = 6;
                break;
            case PK_GRAY:
                ctype = 0;
                break;
            default:
                break;
        }
        ch = (ctype == 0) ? 1 : (ctype == 2) ? 3 : 4;
        raw = (uint8_t *)malloc((size_t)pw * ph * ch);
        if (!raw)
            return 0;
        for (y = 0; y < ph; y++)
            for (x = 0; x < pw; x++) {
                uint8_t *p = raw + ((size_t)y * pw + x) * ch;
                uint8_t rgb[3];
                pack_rgb(tile, x, y, rgb);
                if (ctype == 0)
                    p[0] = rgb[0];
                else {
                    p[0] = rgb[0];
                    p[1] = rgb[1];
                    p[2] = rgb[2];
                    if (ctype == 6)
                        p[3] = pack_hole_at(tile, pw, ph, x, y) ? 0x00 : 0xFF;
                }
            }
        snprintf(path, sizeof(path), "%s/%08x%08x.png", dir,
                 (unsigned)(key >> 32), (unsigned)(key & 0xFFFFFFFFu));
        ok = write_png(path, pw, ph, ctype, raw);
        free(raw);
        if (!ok)
            return 0;
    }
    return 1;
}

static void rm_rf_pack(const char *sysdir)
{
    char base[1100], sub[1300], fp[2600];
    DIR *d, *d2;
    struct dirent *e, *e2;

    snprintf(base, sizeof(base), "%s/vj_texpacks", sysdir);
    d = opendir(base);
    if (d) {
        while ((e = readdir(d)) != NULL) {
            if (e->d_name[0] == '.')
                continue;
            snprintf(sub, sizeof(sub), "%s/%s", base, e->d_name);
            d2 = opendir(sub);
            if (d2) {
                while ((e2 = readdir(d2)) != NULL) {
                    if (e2->d_name[0] == '.')
                        continue;
                    snprintf(fp, sizeof(fp), "%s/%s", sub, e2->d_name);
                    unlink(fp);
                }
                closedir(d2);
            }
            rmdir(sub);
        }
        closedir(d);
    }
    rmdir(base);
    rmdir(sysdir);
}

/* ---------- per-frame capture ---------- */

static uint64_t fnv1a(uint64_t hh, const void *data, size_t len)
{
    const uint8_t *p = (const uint8_t *)data;
    size_t i;
    for (i = 0; i < len; i++) {
        hh ^= p[i];
        hh *= FNV_PRIME;
    }
    return hh;
}

typedef struct {
    uint64_t fb[MAX_FRAMES];
    unsigned fb_count;
    uint64_t digest_mid, digest_end;
    uint64_t dest_hash[N_TILES];   /* battery destination RAM, per blit */
    uint32_t crc;                  /* TitleDBContentCRC()               */
    int      gate;                 /* texReplaceEnabled after battery   */
    int      shadow_active;        /* shadowFBActive after battery      */
    unsigned hits[N_TILES];        /* dest words resolving to pack RGB  */
    unsigned wrong_rgb[N_TILES];   /* resolved but with the WRONG RGB   */
    unsigned line_rgb_ok;          /* presentation_path sample result   */
    /* Tier 3 (>1x pack art on the Nx surface). */
    int      hires_active;         /* shadowHiresActive                 */
    int      hires_n;              /* shadowHiresN                      */
    int      repl_active;          /* shadowHiresReplActive             */
    unsigned hi_words[N_TILES];    /* stock words with a pack block     */
    unsigned hi_sub_ok[N_TILES];   /* subpixels carrying the right RGB  */
    unsigned hi_sub_bad[N_TILES];  /* subpixels carrying the WRONG RGB  */
    unsigned hi_sub_hole[N_TILES]; /* subpixels left to the stock pixel */
    /* Issue #528. */
    unsigned post_frames;          /* frames stepped between the battery
                                    * blits and the probes (ages the Nx
                                    * epoch for the static-content gate) */
    uint64_t dest_hash2[N_TILES];  /* battery destination RAM re-hashed
                                    * AT PROBE TIME: proves the emulated
                                    * game did not stomp the tiles while
                                    * post_frames were stepped, so "art
                                    * gone" can only mean it aged out    */
    int      repl_1x_active;       /* shadowFBReplActive after battery   */
    unsigned rgb16_ok;             /* pack words TomLinePackRGB delivers */
    unsigned rgb16_bad;            /* ...delivers with the WRONG RGB     */
    unsigned rgb16_false;          /* NON-pack line entries it wrongly
                                    * claimed (a CRY reconstruction must
                                    * never present on an RGB16 scanout) */
} run_result;

static run_result *g_cur;

static void video_cb(void *ud, const void *data,
                     unsigned width, unsigned height, size_t pitch)
{
    unsigned y;
    const uint8_t *row = (const uint8_t *)data;
    uint64_t hh = FNV_OFFSET;
    (void)ud;
    if (!g_cur || g_cur->fb_count >= MAX_FRAMES)
        return;
    if (!data) {
        g_cur->fb[g_cur->fb_count] =
            g_cur->fb_count ? g_cur->fb[g_cur->fb_count - 1] : 0;
        return;
    }
    hh = fnv1a(hh, &width, sizeof(width));
    hh = fnv1a(hh, &height, sizeof(height));
    for (y = 0; y < height; y++)
        hh = fnv1a(hh, row + (size_t)y * pitch, (size_t)width * 4);
    g_cur->fb[g_cur->fb_count] = hh;
}

static uint64_t hash_file(const char *path)
{
    FILE *f = fopen(path, "rb");
    uint8_t buf[65536];
    size_t n;
    uint64_t hh = FNV_OFFSET;
    if (!f)
        return 0;
    while ((n = fread(buf, 1, sizeof(buf), f)) > 0)
        hh = fnv1a(hh, buf, n);
    fclose(f);
    return hh;
}

/* ---------- battery driver (same bus path as test_texdump's) ---------- */

typedef void (*jw32_fn)(uint32_t, uint32_t, uint32_t);
typedef uint32_t (*crc_fn)(void);
typedef int (*sfb_lookup_fn)(uint32_t, uint16_t, uint32_t *);
typedef void (*sfb_line_fn)(int, uint32_t, uint16_t);

typedef struct {
    jw32_fn  wl;
    uint8_t *ram;
} bat_ctx;

static uint32_t bat_flags(unsigned log2w, unsigned psize)
{
    return 0x10000u | ((uint32_t)log2w << 11) | ((uint32_t)psize << 3);
}

static void bat_blit(const bat_ctx *bc, unsigned tile)
{
    const bat_tile *t = &tiles[tile];
    uint32_t sflags = bat_flags(t->log2w, t->psize);
    uint32_t dflags = bat_flags(t->log2w, t->psize);
    uint32_t step   = ((uint32_t)1 << 16) | ((uint32_t)(-(int)t->w) & 0xFFFF);
    uint32_t cmd    = 0x01800000u    /* LFU = source (REPLACE: LFU2|LFU3) */
                    | 0x00000001u    /* SRCEN                  */
                    | 0x00000200u    /* UPDA1                  */
                    | 0x00000400u;   /* UPDA2                  */
    uint32_t sbase = BAT_SRC, dbase = tile_dst(tile);
    uint32_t a1base, a2base, a1flags, a2flags;

    if (t->dsta2) {
        cmd |= 0x00000800u;          /* DSTA2: A1 reads, A2 writes */
        a1base = sbase; a1flags = sflags;
        a2base = dbase; a2flags = dflags;
    } else {
        a1base = dbase; a1flags = dflags;
        a2base = sbase; a2flags = sflags;
    }

    bc->wl(0xF02200u, a1base, 0);
    bc->wl(0xF02204u, a1flags, 0);
    bc->wl(0xF0220Cu, 0, 0);
    bc->wl(0xF02210u, step, 0);
    bc->wl(0xF02218u, 0, 0);
    bc->wl(0xF02224u, a2base, 0);
    bc->wl(0xF02228u, a2flags, 0);
    bc->wl(0xF02230u, 0, 0);
    bc->wl(0xF02234u, step, 0);
    bc->wl(0xF0223Cu, ((uint32_t)t->h << 16) | t->w, 0);
    bc->wl(0xF02238u, cmd, 0);       /* B_CMD -> launch */
}

static int run_battery(harness_config *cfg, run_result *out)
{
    bat_ctx bc;
    void *sym;
    unsigned tile, i;

    bc.wl = (jw32_fn)harness_dlsym(cfg, "JaguarWriteLong");
    sym   = harness_dlsym(cfg, "jaguarMainRAM");
    if (!bc.wl || !sym)
        return 0;
    bc.ram = *(uint8_t **)sym;
    if (!bc.ram)
        return 0;

    for (tile = 0; tile < N_TILES; tile++) {
        unsigned n = tile_bytes(&tiles[tile]);
        for (i = 0; i < n; i++)
            bc.ram[(BAT_SRC + i) & 0x1FFFFF] = fill_byte(tile, i);
        bat_blit(&bc, tile);
        out->dest_hash[tile] = fnv1a(FNV_OFFSET,
                                     bc.ram + (tile_dst(tile) & 0x1FFFFF), n);
    }
    return 1;
}

/* Probe the shadow framebuffer against the expected pack art.  Runs in
 * every run (in off / no-pack runs everything must miss). */
static void probe_shadow(harness_config *cfg, run_result *out, int hires)
{
    sfb_lookup_fn lookup =
        (sfb_lookup_fn)harness_dlsym(cfg, "ShadowFBLookup");
    sfb_line_fn line_fn =
        (sfb_line_fn)harness_dlsym(cfg, "ShadowFBLineFromRAM");
    uint32_t *line_rgb = (uint32_t *)harness_dlsym(cfg, "shadowLineRGB");
    int *gate = (int *)harness_dlsym(cfg, "texReplaceEnabled");
    int *sfb  = (int *)harness_dlsym(cfg, "shadowFBActive");
    void *ram_sym = harness_dlsym(cfg, "jaguarMainRAM");
    uint8_t *ram;
    unsigned tile, x, y;

    if (!lookup || !line_fn || !line_rgb || !gate || !sfb || !ram_sym)
        return;
    ram = *(uint8_t **)ram_sym;
    out->gate = *gate;
    out->shadow_active = *sfb;

    for (tile = 0; tile < N_TILES; tile++) {
        const bat_tile *t = &tiles[tile];
        /* The 1x entry of a stock word is the pack's TOP-LEFT subpixel
         * -- at 2x that is PNG coordinate (x*s, y*s). */
        unsigned s = pack_scale(tile, hires);
        if (t->psize != 4)
            continue;                /* shadow words are 16bpp only */
        for (y = 0; y < t->h; y++)
            for (x = 0; x < t->w; x++) {
                uint32_t daddr = tile_dst(tile) + (y * t->w + x) * 2;
                uint16_t cur16 =
                    (uint16_t)(((uint16_t)ram[daddr & 0x1FFFFF] << 8)
                             | ram[(daddr + 1) & 0x1FFFFF]);
                uint32_t rgb = 0;
                uint8_t exp[3];
                if (!lookup(daddr, cur16, &rgb))
                    continue;
                pack_rgb(tile, x * s, y * s, exp);
                if (rgb == (((uint32_t)exp[0] << 16)
                          | ((uint32_t)exp[1] << 8) | exp[2]))
                    out->hits[tile]++;
                else
                    out->wrong_rgb[tile]++;
            }
    }

    /* presentation_path: drive the exact per-pixel function the OP's
     * 16bpp renderer calls and read the line-buffer RGB the scanline
     * renderer would present.  Sample: tile 0, pixel (2, 3). */
    {
        unsigned s = pack_scale(0, hires);
        uint32_t daddr = tile_dst(0) + (3 * tiles[0].w + 2) * 2;
        uint16_t cur16 = (uint16_t)(((uint16_t)ram[daddr & 0x1FFFFF] << 8)
                                  | ram[(daddr + 1) & 0x1FFFFF]);
        uint8_t exp[3];
        line_fn(7, daddr, cur16);
        pack_rgb(0, 2 * s, 3 * s, exp);
        out->line_rgb_ok = (line_rgb[7] == (((uint32_t)exp[0] << 16)
                                          | ((uint32_t)exp[1] << 8)
                                          | exp[2])) ? 1 : 0;
    }
}

/* ---------- issue #528: the RGB16-direct presentation seam ----------
 *
 * TomLinePackRGB is the ONE function both RGB16-direct renderers (1x
 * tom_render_16bpp_rgb_scanline and Nx tom_render_16bpp_rgb_scanline_hires)
 * call per presented pixel, so driving it is the RGB16 analogue of gate
 * 4 driving ShadowFBLineFromRAM.
 *
 * Two halves, and the second is the one that matters:
 *   - pack words must be DELIVERED with the author's RGB;
 *   - non-pack shadow entries must NOT be.  A true-color CRY
 *     reconstruction is a decomposition of the 16-bit word through the
 *     chroma tables; presenting it on a word TOM is scanning out as
 *     RGB16 would show a colour nothing ever drew.  "Substitute
 *     whenever the shadow line hits" would pass the first half and fail
 *     this one. */
typedef int (*tom_pack_fn)(int, uint16_t, uint32_t *);

static void probe_rgb16(harness_config *cfg, run_result *out, int hires)
{
    tom_pack_fn pack_fn = (tom_pack_fn)harness_dlsym(cfg, "TomLinePackRGB");
    sfb_line_fn line_fn =
        (sfb_line_fn)harness_dlsym(cfg, "ShadowFBLineFromRAM");
    int *ra = (int *)harness_dlsym(cfg, "shadowFBReplActive");
    void *ram_sym = harness_dlsym(cfg, "jaguarMainRAM");
    uint8_t *ram;
    unsigned tile, x, y;
    const int idx = 11;                 /* arbitrary line slot */

    if (!pack_fn || !line_fn || !ra || !ram_sym)
        return;
    out->repl_1x_active = *ra;
    ram = *(uint8_t **)ram_sym;

    for (tile = 0; tile < N_TILES; tile++) {
        const bat_tile *t = &tiles[tile];
        unsigned s = pack_scale(tile, hires);
        unsigned pw = t->w * s, ph = t->h * s;
        if (t->psize != 4)
            continue;
        for (y = 0; y < t->h; y++)
            for (x = 0; x < t->w; x++) {
                uint32_t daddr = tile_dst(tile) + (y * t->w + x) * 2;
                uint16_t cur16 =
                    (uint16_t)(((uint16_t)ram[daddr & 0x1FFFFF] << 8)
                             | ram[(daddr + 1) & 0x1FFFFF]);
                uint32_t got = 0;
                uint8_t exp[3];
                int want;
                /* Populate the line slot exactly as op.c does, then ask
                 * the renderer's seam what it would present. */
                line_fn(idx, daddr, cur16);
                /* A pack word is one whose 1x representative (the
                 * TOP-LEFT subpixel) is not an author alpha hole -- the
                 * same rule expect_1x_hits() derives independently. */
                want = out->repl_1x_active
                     && (s == 1 || (hires && s == 2))
                     && !pack_hole_at(tile, pw, ph, x * s, y * s);
                if (!pack_fn(idx, cur16, &got)) {
                    if (want)
                        out->rgb16_bad++;   /* pack art not delivered */
                    continue;
                }
                if (!want) {
                    out->rgb16_false++;     /* non-pack entry claimed */
                    continue;
                }
                pack_rgb(tile, x * s, y * s, exp);
                if (got == (0xFF000000u | ((uint32_t)exp[0] << 16)
                          | ((uint32_t)exp[1] << 8) | exp[2]))
                    out->rgb16_ok++;
                else
                    out->rgb16_bad++;
            }
    }
}

/* ---------- expectations, from the same first principles ---------- */

/* Words whose 1x shadow entry must carry pack RGB.  The 1x
 * representative of a stock word is the pack's TOP-LEFT subpixel, so at
 * 2x a hole there means no 1x entry (the Nx block still stores). */
static unsigned expect_1x_hits(unsigned tile, int hires)
{
    const bat_tile *t = &tiles[tile];
    unsigned s = pack_scale(tile, hires);
    unsigned pw, ph, x, y, c = 0;

    if (t->psize != 4)
        return 0;                       /* non-16bpp: future tier */
    if (s != 1 && !(hires && s == 2))
        return 0;                       /* dimension mismatch */
    pw = t->w * s;
    ph = t->h * s;
    for (y = 0; y < t->h; y++)
        for (x = 0; x < t->w; x++)
            if (!pack_hole_at(tile, pw, ph, x * s, y * s))
                c++;
    return c;
}

/* Nx expectations: stock words carrying a pack block, and the split of
 * their subpixels into replaced / left-to-the-game. */
static void expect_hires(unsigned tile, int n, unsigned *words,
                         unsigned *ok, unsigned *hole)
{
    const bat_tile *t = &tiles[tile];
    unsigned s = pack_scale(tile, 1);
    unsigned pw, ph, x, y, sy, sx;

    *words = *ok = *hole = 0;
    if (t->psize != 4)
        return;
    if (s != 1 && s != (unsigned)n)
        return;                         /* dimension mismatch */
    pw = t->w * s;
    ph = t->h * s;
    for (y = 0; y < t->h; y++)
        for (x = 0; x < t->w; x++) {
            unsigned o = 0, h = 0;
            for (sy = 0; sy < (unsigned)n; sy++)
                for (sx = 0; sx < (unsigned)n; sx++) {
                    unsigned X = (s > 1) ? x * s + sx : x;
                    unsigned Y = (s > 1) ? y * s + sy : y;
                    if (pack_hole_at(tile, pw, ph, X, Y))
                        h++;
                    else
                        o++;
                }
            if (o) {
                (*words)++;
                *ok   += o;
                *hole += h;
            }
        }
}

/* ---------- tier 3 probe: the Nx surface ---------- */

typedef int (*hires_line_fn)(int, uint32_t, uint16_t);

/* Drive the exact function op.c calls per 16bpp pixel at Nx
 * (ShadowHiresLineFromRAM) and read the Nx line replacement plane the
 * scanline renderer reads.  This is the tier-3 analogue of
 * presentation_path: it asserts the art is DELIVERED, not merely
 * stored -- the hi-res failure mode that produces no other symptom. */
static void probe_shadow_hires(harness_config *cfg, run_result *out,
                               int hires)
{
    hires_line_fn line_fn =
        (hires_line_fn)harness_dlsym(cfg, "ShadowHiresLineFromRAM");
    int *act = (int *)harness_dlsym(cfg, "shadowHiresActive");
    int *nsym = (int *)harness_dlsym(cfg, "shadowHiresN");
    int *ra  = (int *)harness_dlsym(cfg, "shadowHiresReplActive");
    uint32_t **lrepl = (uint32_t **)harness_dlsym(cfg, "shadowHiresLineRepl");
    void *ram_sym = harness_dlsym(cfg, "jaguarMainRAM");
    uint8_t *ram;
    uint32_t *repl;
    int n;
    const int idx = 3;                  /* arbitrary line slot */
    unsigned tile, x, y, sy, sx;

    if (!line_fn || !act || !nsym || !ra || !lrepl || !ram_sym)
        return;
    out->hires_active = *act;
    out->hires_n      = *nsym;
    out->repl_active  = *ra;
    if (!*act || !*lrepl)
        return;
    n    = *nsym;
    repl = *lrepl;
    ram  = *(uint8_t **)ram_sym;

    for (tile = 0; tile < N_TILES; tile++) {
        const bat_tile *t = &tiles[tile];
        unsigned s = pack_scale(tile, hires);
        if (t->psize != 4)
            continue;
        for (y = 0; y < t->h; y++)
            for (x = 0; x < t->w; x++) {
                uint32_t daddr = tile_dst(tile) + (y * t->w + x) * 2;
                uint16_t cur16 =
                    (uint16_t)(((uint16_t)ram[daddr & 0x1FFFFF] << 8)
                             | ram[(daddr + 1) & 0x1FFFFF]);
                unsigned any = 0;
                line_fn(idx, daddr, cur16);
                for (sy = 0; sy < (unsigned)n; sy++)
                    for (sx = 0; sx < (unsigned)n; sx++) {
                        uint32_t v = repl[((uint32_t)sy * SHADOWFB_LINE_PIXELS
                                           + (uint32_t)idx) * (uint32_t)n + sx];
                        unsigned X, Y;
                        uint8_t exp[3];
                        if (!(v & SHADOWFB_HIRES_REPL_VALID)) {
                            out->hi_sub_hole[tile]++;
                            continue;
                        }
                        any = 1;
                        X = (s > 1) ? x * s + sx : x;
                        Y = (s > 1) ? y * s + sy : y;
                        pack_rgb(tile, X, Y, exp);
                        if ((v & 0x00FFFFFFu) == (((uint32_t)exp[0] << 16)
                                                | ((uint32_t)exp[1] << 8)
                                                | exp[2]))
                            out->hi_sub_ok[tile]++;
                        else
                            out->hi_sub_bad[tile]++;
                    }
                if (any)
                    out->hi_words[tile]++;
                else
                    /* No block here: the N*N "holes" just counted are
                     * the absence of a block, not author alpha. */
                    out->hi_sub_hole[tile] -= (unsigned)(n * n);
            }
    }
}

/* ---------- one emulation run ---------- */

static int do_run(const char *core, const char *rom, unsigned frames,
                  int replace_on, int hires, const char *sysdir,
                  unsigned post_frames, run_result *out)
{
    harness_config cfg = HARNESS_CONFIG_DEFAULT;
    unsigned i;
    char state_path[1200];
    crc_fn crc_get;

    memset(out, 0, sizeof(*out));
    out->post_frames = post_frames;
    cfg.core_path  = core;
    cfg.rom_path   = rom;
    cfg.frames     = frames;
    cfg.quiet      = 1;
    cfg.system_dir = sysdir;
    cfg.options[cfg.num_options].key   = "virtualjaguar_texture_replace";
    cfg.options[cfg.num_options].value = replace_on ? "enabled" : "disabled";
    cfg.num_options++;
    /* True Color stays OFF: the pipeline must force the shadow surface
     * it presents through all by itself. */
    cfg.options[cfg.num_options].key   = "virtualjaguar_true_color";
    cfg.options[cfg.num_options].value = "disabled";
    cfg.num_options++;
    cfg.options[cfg.num_options].key   = "virtualjaguar_texture_dump";
    cfg.options[cfg.num_options].value = "disabled";
    cfg.num_options++;
    /* Tier 3: the Nx shadow surface the >1x pack art rides.  Applied at
     * content load (the option is restart-scoped), which is why every
     * run sets it explicitly rather than toggling mid-run. */
    cfg.options[cfg.num_options].key   = "virtualjaguar_internal_resolution";
    cfg.options[cfg.num_options].value = hires ? "2x" : "1x";
    cfg.num_options++;
    cfg.video_callback = video_cb;

    if (!harness_load_core(&cfg))
        return 0;
    if (!harness_load_rom(&cfg)) {
        harness_shutdown(&cfg);
        return 0;
    }

    {
        crc_get = (crc_fn)harness_dlsym(&cfg, "TitleDBContentCRC");
        if (crc_get)
            out->crc = crc_get();
    }

    snprintf(state_path, sizeof(state_path), "%s/texreplace_probe.state",
             sysdir);
    g_cur = out;
    for (i = 0; i < frames; i++) {
        harness_step(&cfg);
        out->fb_count++;
        if (i + 1 == frames / 2) {
            if (harness_save_state(&cfg, state_path))
                out->digest_mid = hash_file(state_path);
        }
        if (i + 1 == frames) {
            if (harness_save_state(&cfg, state_path))
                out->digest_end = hash_file(state_path);
        }
    }
    g_cur = NULL;

    if (!run_battery(&cfg, out)) {
        fprintf(stderr, "texreplace: battery needs the TEST_EXPORTS wide "
                "ABI (JaguarWriteLong/jaguarMainRAM not exported)\n");
        harness_shutdown(&cfg);
        return 0;
    }
    /* Issue #528: step real presented frames between the battery blits
     * and the probes.  Nothing is faked -- harness_step drives
     * retro_run, which drives ShadowHiresFrameTick, which is the ONLY
     * thing that advances the epoch.  post_frames must clear
     * HIRES_EPOCH_WINDOW (16) and stay far from the 256-frame epoch
     * wrap, which clears every tag for an unrelated reason. */
    if (post_frames) {
        void *ram_sym = harness_dlsym(&cfg, "jaguarMainRAM");
        uint8_t *ram = ram_sym ? *(uint8_t **)ram_sym : NULL;
        unsigned t;
        for (i = 0; i < post_frames; i++)
            harness_step(&cfg);
        if (ram)
            for (t = 0; t < N_TILES; t++)
                out->dest_hash2[t] =
                    fnv1a(FNV_OFFSET, ram + (tile_dst(t) & 0x1FFFFF),
                          tile_bytes(&tiles[t]));
    }
    probe_shadow(&cfg, out, hires);
    probe_shadow_hires(&cfg, out, hires);
    probe_rgb16(&cfg, out, hires);

    harness_shutdown(&cfg);
    unlink(state_path);
    return 1;
}

/* ---------- comparisons ---------- */

/* Cross-resolution comparison: savestate digests + every battery blit's
 * destination RAM.  Framebuffer hashes are deliberately NOT compared --
 * a 2x run presents twice the pixels by design, and the claim under
 * test is that the EMULATED MACHINE cannot observe either enhancement,
 * which is exactly what these two carry. */
static int machine_equal_state(const run_result *a, const run_result *b,
                               const char *an, const char *bn,
                               char *why, size_t why_size)
{
    unsigned t;
    if (!a->digest_mid || !a->digest_end || !b->digest_mid || !b->digest_end) {
        snprintf(why, why_size, "savestate capture failed (%s/%s)", an, bn);
        return 0;
    }
    if (a->digest_mid != b->digest_mid || a->digest_end != b->digest_end) {
        snprintf(why, why_size, "%s vs %s: savestate digest differs "
                 "(@mid: %d, @end: %d)", an, bn,
                 a->digest_mid != b->digest_mid,
                 a->digest_end != b->digest_end);
        return 0;
    }
    for (t = 0; t < N_TILES; t++)
        if (a->dest_hash[t] != b->dest_hash[t]) {
            snprintf(why, why_size, "%s vs %s: battery tile %u destination "
                     "RAM differs", an, bn, t);
            return 0;
        }
    return 1;
}

static int machine_equal(const run_result *a, const run_result *b,
                         unsigned frames, char *why, size_t why_size)
{
    unsigned f, t;
    for (f = 0; f < frames; f++)
        if (a->fb[f] != b->fb[f]) {
            snprintf(why, why_size, "framebuffer diverges at frame %u", f + 1);
            return 0;
        }
    if (!a->digest_mid || !a->digest_end) {
        snprintf(why, why_size, "savestate capture failed");
        return 0;
    }
    if (a->digest_mid != b->digest_mid || a->digest_end != b->digest_end) {
        snprintf(why, why_size, "savestate digest differs (@mid: %d, @end: %d)",
                 a->digest_mid != b->digest_mid, a->digest_end != b->digest_end);
        return 0;
    }
    for (t = 0; t < N_TILES; t++)
        if (a->dest_hash[t] != b->dest_hash[t]) {
            snprintf(why, why_size, "battery tile %u destination RAM differs", t);
            return 0;
        }
    snprintf(why, why_size, "%u frames + digests + %u battery destinations "
             "identical", frames, (unsigned)N_TILES);
    return 1;
}

/* ---------- main ---------- */

int main(int argc, char **argv)
{
    harness_config argcfg = HARNESS_CONFIG_DEFAULT;
    int json = 0, quiet = 0;
    unsigned frames;
    int i;
    run_result *ro, *rn, *rp, *rq;
    run_result *rh, *rH, *rI, *rJ;
    char dir_o[64], dir_n[64], dir_p[64], dir_h[64];
    harness_result results[14];
    unsigned nres = 0;
    int failed = 0;
    static char d_nopack[224], d_inert[224], d_pres[320], d_line[128],
                d_det[224], d_hinert[320], d_hpres[512], d_hdet[224],
                d_stat[448], d_rgb[384];

    for (i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--json"))
            json = 1;
        else if (!strcmp(argv[i], "--quiet"))
            quiet = 1;
    }

    argcfg.frames = 600;
    if (!harness_init_from_args(&argcfg, argc, argv)) {
        fprintf(stderr, "usage: %s <core> [rom] [--frames N] [--json] "
                "[--quiet]\n", argv[0]);
        return 1;
    }
    if (!argcfg.rom_path)
        argcfg.rom_path = "test/roms/yarc.j64";
    if (access(argcfg.rom_path, R_OK) != 0) {
        printf("SKIP: ROM not available (%s)\n", argcfg.rom_path);
        return 2;
    }
    frames = argcfg.frames;
    if (frames > MAX_FRAMES)
        frames = MAX_FRAMES;

    ro = (run_result *)calloc(1, sizeof(run_result));
    rn = (run_result *)calloc(1, sizeof(run_result));
    rp = (run_result *)calloc(1, sizeof(run_result));
    rq = (run_result *)calloc(1, sizeof(run_result));
    rh = (run_result *)calloc(1, sizeof(run_result));
    rH = (run_result *)calloc(1, sizeof(run_result));
    rI = (run_result *)calloc(1, sizeof(run_result));
    rJ = (run_result *)calloc(1, sizeof(run_result));
    if (!ro || !rn || !rp || !rq || !rh || !rH || !rI || !rJ) {
        fprintf(stderr, "FAIL: out of memory\n");
        return 1;
    }

    snprintf(dir_o, sizeof(dir_o), "/tmp/vj_texrep_o_%d", (int)getpid());
    snprintf(dir_n, sizeof(dir_n), "/tmp/vj_texrep_n_%d", (int)getpid());
    snprintf(dir_p, sizeof(dir_p), "/tmp/vj_texrep_p_%d", (int)getpid());
    snprintf(dir_h, sizeof(dir_h), "/tmp/vj_texrep_h_%d", (int)getpid());
    mkdir(dir_o, 0777);
    mkdir(dir_n, 0777);
    mkdir(dir_p, 0777);
    mkdir(dir_h, 0777);

    /* Run O: replacement disabled (the machine baseline). */
    if (!do_run(argcfg.core_path, argcfg.rom_path, frames, 0, 0, dir_o, 0, ro))
        goto run_fail;
    /* Run N: enabled, but no pack directory exists in this sysdir. */
    if (!do_run(argcfg.core_path, argcfg.rom_path, frames, 1, 0, dir_n, 0, rn))
        goto run_fail;
    /* Build the synthetic pack from first principles, then run P
     * (enabled, pack present) twice for determinism. */
    if (!build_pack(dir_p, ro->crc, 0)) {
        fprintf(stderr, "FAIL: could not build the synthetic pack\n");
        goto run_fail;
    }
    if (!do_run(argcfg.core_path, argcfg.rom_path, frames, 1, 0, dir_p, 0, rp))
        goto run_fail;
    if (!do_run(argcfg.core_path, argcfg.rom_path, frames, 1, 0, dir_p, 0, rq))
        goto run_fail;

    /* Tier 3: internal resolution 2x, with no pack (H) and with a 2x
     * pack (I, run twice).  dir_o never gains a pack, so it doubles as
     * the no-pack sysdir for the 2x baseline. */
    if (!do_run(argcfg.core_path, argcfg.rom_path, frames, 0, 1, dir_o, 0, rh))
        goto run_fail;
    if (!build_pack(dir_h, ro->crc, 1)) {
        fprintf(stderr, "FAIL: could not build the synthetic 2x pack\n");
        goto run_fail;
    }
    if (!do_run(argcfg.core_path, argcfg.rom_path, frames, 1, 1, dir_h, 0, rH))
        goto run_fail;
    if (!do_run(argcfg.core_path, argcfg.rom_path, frames, 1, 1, dir_h, 0, rI))
        goto run_fail;
    /* Run J (issue #528): the same 2x pack run, but with REAL presented
     * frames stepped between the battery blits and the probes -- past
     * HIRES_EPOCH_WINDOW (16), nowhere near the 256-frame epoch wrap.
     * This is what the existing battery could never see: it blits after
     * the frame loop and probes at the current epoch, so nothing ever
     * ages.  Nothing here fakes the epoch; harness_step drives
     * retro_run, which is the only thing that advances it. */
#define POST_FRAMES 24u
    if (!do_run(argcfg.core_path, argcfg.rom_path, frames, 1, 1, dir_h,
                POST_FRAMES, rJ))
        goto run_fail;

    /* Gate 1: no pack -> bit-identical machine AND gate off. */
    {
        char why[192];
        int ok = machine_equal(ro, rn, frames, why, sizeof(why));
        if (ok && rn->gate != 0) {
            ok = 0;
            snprintf(why, sizeof(why),
                     "texReplaceEnabled=%d with no pack (want 0)", rn->gate);
        }
        if (ok && rn->shadow_active != 0) {
            ok = 0;
            snprintf(why, sizeof(why),
                     "shadowFBActive=%d with no pack and True Color off "
                     "(want 0)", rn->shadow_active);
        }
        snprintf(d_nopack, sizeof(d_nopack), "%s", why);
        results[nres].status = ok ? "PASS" : "FAIL";
        results[nres].name   = "nopack_inert";
        results[nres].detail = d_nopack;
        if (!ok) failed = 1;
        nres++;
    }

    /* Gate 2: pack present -> the machine is STILL bit-identical. */
    {
        char why[192];
        int ok = machine_equal(ro, rp, frames, why, sizeof(why));
        snprintf(d_inert, sizeof(d_inert), "%s", why);
        results[nres].status = ok ? "PASS" : "FAIL";
        results[nres].name   = "pack_machine_inert";
        results[nres].detail = d_inert;
        if (!ok) failed = 1;
        nres++;
    }

    /* Gate 3: pack present -> the shadow framebuffer presents it. */
    {
        int ok = (rp->gate == 1) && (rp->shadow_active == 1);
        unsigned t;
        char frag[64];
        snprintf(d_pres, sizeof(d_pres), "gate=%d shadow=%d hits:",
                 rp->gate, rp->shadow_active);
        for (t = 0; t < N_TILES; t++) {
            /* Two independent expectations must agree: the hand-written
             * per-tile count in tiles[] and the one derived from the
             * pack-generation rules (which is what the 2x gates use). */
            unsigned want = expect_1x_hits(t, 0);
            if (want != tiles[t].expect_hits || rp->hits[t] != want
                || rp->wrong_rgb[t])
                ok = 0;
            snprintf(frag, sizeof(frag), " t%u=%u/%u", t, rp->hits[t], want);
            strncat(d_pres, frag, sizeof(d_pres) - strlen(d_pres) - 1);
            if (rp->wrong_rgb[t]) {
                snprintf(frag, sizeof(frag), "(%u wrong)", rp->wrong_rgb[t]);
                strncat(d_pres, frag, sizeof(d_pres) - strlen(d_pres) - 1);
            }
        }
        results[nres].status = ok ? "PASS" : "FAIL";
        results[nres].name   = "pack_presents";
        results[nres].detail = d_pres;
        if (!ok) failed = 1;
        nres++;
    }

    /* Gate 4: the OP's per-pixel presentation function shows pack RGB. */
    {
        int ok = rp->line_rgb_ok == 1;
        snprintf(d_line, sizeof(d_line), "ShadowFBLineFromRAM -> "
                 "shadowLineRGB %s pack art",
                 ok ? "carries" : "DOES NOT carry");
        results[nres].status = ok ? "PASS" : "FAIL";
        results[nres].name   = "presentation_path";
        results[nres].detail = d_line;
        if (!ok) failed = 1;
        nres++;
    }

    /* Gate 5: two identical pack runs agree on everything. */
    {
        char why[192];
        int ok = machine_equal(rp, rq, frames, why, sizeof(why));
        unsigned t;
        for (t = 0; t < N_TILES && ok; t++)
            if (rp->hits[t] != rq->hits[t]
                || rp->wrong_rgb[t] != rq->wrong_rgb[t]) {
                ok = 0;
                snprintf(why, sizeof(why), "shadow hits differ on tile %u", t);
            }
        if (ok && rp->line_rgb_ok != rq->line_rgb_ok) {
            ok = 0;
            snprintf(why, sizeof(why), "presentation sample differs");
        }
        snprintf(d_det, sizeof(d_det), "%s", why);
        results[nres].status = ok ? "PASS" : "FAIL";
        results[nres].name   = "determinism";
        results[nres].detail = d_det;
        if (!ok) failed = 1;
        nres++;
    }

    /* Gate 6 (tier 3): the FOUR-WAY identity gate.  {1x, 2x} x {pack,
     * no pack} must agree on savestate digests and on every battery
     * destination -- i.e. neither the resolution option nor a texture
     * pack is observable to the emulated machine, alone or together. */
    {
        char why[224];
        int ok = 1;
        if (ok) ok = machine_equal_state(ro, rh, "1x-off", "2x-off",
                                         why, sizeof(why));
        if (ok) ok = machine_equal_state(ro, rp, "1x-off", "1x-pack",
                                         why, sizeof(why));
        if (ok) ok = machine_equal_state(ro, rH, "1x-off", "2x-pack",
                                         why, sizeof(why));
        if (ok && rh->hires_active != 1) {
            ok = 0;
            snprintf(why, sizeof(why), "2x run did not activate the hi-res "
                     "surface (shadowHiresActive=%d, N=%d)",
                     rh->hires_active, rh->hires_n);
        }
        if (ok && rh->repl_active != 0) {
            ok = 0;
            snprintf(why, sizeof(why), "shadowHiresReplActive=%d at 2x with "
                     "no pack (want 0)", rh->repl_active);
        }
        if (ok)
            snprintf(why, sizeof(why), "1x/2x x pack/no-pack: digests + %u "
                     "battery destinations identical across all four; "
                     "N=%d, repl plane inert without a pack",
                     (unsigned)N_TILES, rh->hires_n);
        snprintf(d_hinert, sizeof(d_hinert), "%s", why);
        results[nres].status = ok ? "PASS" : "FAIL";
        results[nres].name   = "hires_machine_inert";
        results[nres].detail = d_hinert;
        if (!ok) failed = 1;
        nres++;
    }

    /* Gate 7 (tier 3): the Nx surface DELIVERS the pack art.  Driven
     * through ShadowHiresLineFromRAM -- the function op.c calls per
     * 16bpp pixel -- and read out of the Nx line replacement plane the
     * scanline renderer reads. */
    {
        int ok = (rH->hires_active == 1) && (rH->repl_active == 1);
        unsigned t;
        char frag[112];
        snprintf(d_hpres, sizeof(d_hpres), "N=%d repl=%d words:",
                 rH->hires_n, rH->repl_active);
        for (t = 0; t < N_TILES; t++) {
            unsigned w = 0, o = 0, h = 0;
            /* The 1x representative recorded alongside every Nx block
             * is the pack's TOP-LEFT subpixel -- what a hi-res resolve
             * miss (epoch expiry, unshadowed page) degrades to.  Assert
             * it explicitly: bottom-right, an average, or storing
             * nothing would otherwise pass every other gate here. */
            unsigned lo = expect_1x_hits(t, 1);
            expect_hires(t, rH->hires_n ? rH->hires_n : 1, &w, &o, &h);
            if (rH->hi_words[t] != w || rH->hi_sub_ok[t] != o
                || rH->hi_sub_bad[t] || rH->hi_sub_hole[t] != h
                || rH->hits[t] != lo || rH->wrong_rgb[t])
                ok = 0;
            snprintf(frag, sizeof(frag), " t%u=%u/%u(%u/%u sub,%u/%u 1x%s)",
                     t, rH->hi_words[t], w, rH->hi_sub_ok[t], o,
                     rH->hits[t], lo,
                     (rH->hi_sub_bad[t] || rH->wrong_rgb[t]) ? ",WRONG" : "");
            strncat(d_hpres, frag, sizeof(d_hpres) - strlen(d_hpres) - 1);
        }
        if (ok && !rH->line_rgb_ok) {
            ok = 0;
            snprintf(d_hpres, sizeof(d_hpres), "the 1x presentation path "
                     "does not carry pack art at 2x");
        }
        /* The 1x-art tile proves replication: without it the Nx line
         * entry would win and mask the pack outright. */
        if (ok && rH->hi_words[HIRES_1X_TILE] == 0) {
            ok = 0;
            snprintf(d_hpres, sizeof(d_hpres),
                     "1x pack art was not replicated into the Nx surface");
        }
        results[nres].status = ok ? "PASS" : "FAIL";
        results[nres].name   = "hires_pack_presents";
        results[nres].detail = d_hpres;
        if (!ok) failed = 1;
        nres++;
    }

    /* Gate 8 (tier 3): two identical 2x pack runs agree on everything. */
    {
        char why[224];
        int ok = machine_equal(rH, rI, frames, why, sizeof(why));
        unsigned t;
        for (t = 0; t < N_TILES && ok; t++)
            if (rH->hi_words[t] != rI->hi_words[t]
                || rH->hi_sub_ok[t] != rI->hi_sub_ok[t]
                || rH->hi_sub_bad[t] != rI->hi_sub_bad[t]
                || rH->hi_sub_hole[t] != rI->hi_sub_hole[t]) {
                ok = 0;
                snprintf(why, sizeof(why), "Nx pack blocks differ on tile %u",
                         t);
            }
        snprintf(d_hdet, sizeof(d_hdet), "%s", why);
        results[nres].status = ok ? "PASS" : "FAIL";
        results[nres].name   = "hires_determinism";
        results[nres].detail = d_hdet;
        if (!ok) failed = 1;
        nres++;
    }

    /* Gate 9 (issue #528): STATIC content keeps its Nx pack art.
     *
     * A tile blitted once and left on screen -- HUD, menu, title card --
     * used to show crisp Nx art for HIRES_EPOCH_WINDOW frames and then
     * age out to the 1x representative: flat blocky colour.  Run J
     * blits the battery, steps POST_FRAMES real presented frames, and
     * then asks the Nx surface for the same art run H got at age zero.
     *
     * The precondition is checked FIRST and is not optional: if the
     * emulated game wrote to the battery destinations during those
     * frames, "the art is gone" would mean a coherence miss, not
     * expiry, and the gate would be measuring the wrong thing.
     * dest_hash2 is the destination RAM re-hashed at probe time. */
    {
        int ok = 1;
        unsigned t;
        char frag[96];
        for (t = 0; t < N_TILES && ok; t++)
            if (rJ->dest_hash2[t] != rJ->dest_hash[t]) {
                ok = 0;
                snprintf(d_stat, sizeof(d_stat),
                         "precondition failed: the game overwrote battery "
                         "tile %u during the %u aged frames -- this gate "
                         "cannot distinguish expiry from a coherence miss",
                         t, rJ->post_frames);
            }
        if (ok && (rJ->hires_active != 1 || rJ->repl_active != 1)) {
            ok = 0;
            snprintf(d_stat, sizeof(d_stat), "2x pack run did not arm "
                     "(hires=%d repl=%d)", rJ->hires_active,
                     rJ->repl_active);
        }
        if (ok) {
            snprintf(d_stat, sizeof(d_stat),
                     "after %u presented frames (window=16), RAM unchanged; "
                     "Nx words:", rJ->post_frames);
            for (t = 0; t < N_TILES; t++) {
                if (rJ->hi_words[t]   != rH->hi_words[t]
                    || rJ->hi_sub_ok[t]  != rH->hi_sub_ok[t]
                    || rJ->hi_sub_bad[t] || rJ->hi_sub_hole[t] != rH->hi_sub_hole[t])
                    ok = 0;
                snprintf(frag, sizeof(frag), " t%u=%u/%u(%u/%u sub%s)",
                         t, rJ->hi_words[t], rH->hi_words[t],
                         rJ->hi_sub_ok[t], rH->hi_sub_ok[t],
                         rJ->hi_sub_bad[t] ? ",WRONG" : "");
                strncat(d_stat, frag, sizeof(d_stat) - strlen(d_stat) - 1);
            }
        }
        results[nres].status = ok ? "PASS" : "FAIL";
        results[nres].name   = "hires_static_persist";
        results[nres].detail = d_stat;
        if (!ok) failed = 1;
        nres++;
    }

    /* Gate 10 (issue #528): the RGB16-direct paths present the pack --
     * and present ONLY the pack.
     *
     * Driven through TomLinePackRGB, the one seam both the 1x and the
     * Nx RGB16 renderers call per presented pixel.  Wired at both
     * resolutions together on purpose: half of it would make a pack look
     * different at 2x than at 1x for a reason unrelated to resolution.
     *
     * rgb16_false is the half that carries the correctness claim -- a
     * true-color CRY reconstruction sitting in the same shadow line
     * plane must NOT present on an RGB16 scanout, so "substitute
     * whenever the line entry hits" fails here while passing everything
     * else. */
    {
        int ok = 1;
        unsigned want1 = 0, want2 = 0, t;
        for (t = 0; t < N_TILES; t++) {
            want1 += expect_1x_hits(t, 0);
            want2 += expect_1x_hits(t, 1);
        }
        if (rn->repl_1x_active != 0 || rn->rgb16_ok || rn->rgb16_false)
            ok = 0;
        if (rp->repl_1x_active != 1 || rp->rgb16_ok != want1
            || rp->rgb16_bad || rp->rgb16_false)
            ok = 0;
        if (rH->repl_1x_active != 1 || rH->rgb16_ok != want2
            || rH->rgb16_bad || rH->rgb16_false)
            ok = 0;
        snprintf(d_rgb, sizeof(d_rgb),
                 "TomLinePackRGB: 1x %u/%u delivered (%u wrong, %u "
                 "non-pack entries wrongly claimed); 2x %u/%u (%u wrong, "
                 "%u wrongly claimed); no-pack run inert "
                 "(shadowFBReplActive=%d, %u claims)",
                 rp->rgb16_ok, want1, rp->rgb16_bad, rp->rgb16_false,
                 rH->rgb16_ok, want2, rH->rgb16_bad, rH->rgb16_false,
                 rn->repl_1x_active, rn->rgb16_ok + rn->rgb16_false);
        results[nres].status = ok ? "PASS" : "FAIL";
        results[nres].name   = "rgb16_presents";
        results[nres].detail = d_rgb;
        if (!ok) failed = 1;
        nres++;
    }

    {
        harness_config rcfg = HARNESS_CONFIG_DEFAULT;
        rcfg.json_output = json;
        rcfg.quiet = quiet;
        harness_report(&rcfg, results, nres);
    }

    rm_rf_pack(dir_o);
    rm_rf_pack(dir_n);
    rm_rf_pack(dir_p);
    rm_rf_pack(dir_h);
    free(ro); free(rn); free(rp); free(rq);
    free(rh); free(rH); free(rI); free(rJ);
    return failed ? 1 : 0;

run_fail:
    fprintf(stderr, "FAIL: emulation run did not complete\n");
    rm_rf_pack(dir_o);
    rm_rf_pack(dir_n);
    rm_rf_pack(dir_p);
    rm_rf_pack(dir_h);
    free(ro); free(rn); free(rp); free(rq);
    free(rh); free(rH); free(rI); free(rJ);
    return 1;
}
