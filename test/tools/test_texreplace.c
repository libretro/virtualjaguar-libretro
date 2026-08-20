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

static int pack_alpha_hole(const bat_tile *t, unsigned x, unsigned y)
{
    return (x == 0 && y == 0) || (x == t->w - 1 && y == t->h - 1);
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

/* Build the synthetic pack under <sysdir>/vj_texpacks/<crc8>/. */
static int build_pack(const char *sysdir, uint32_t crc)
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
        unsigned pw = t->w, ph = t->h, ctype = 2, ch;
        uint8_t *raw;
        unsigned x, y;
        int ok;

        switch (t->pack_kind) {
            case PK_DIMS:
                pw = t->w * 2;    /* deliberately wrong */
                ph = t->h * 2;
                break;
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
                        p[3] = pack_alpha_hole(t, x, y) ? 0x00 : 0xFF;
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
static void probe_shadow(harness_config *cfg, run_result *out)
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
                pack_rgb(tile, x, y, exp);
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
        uint32_t daddr = tile_dst(0) + (3 * tiles[0].w + 2) * 2;
        uint16_t cur16 = (uint16_t)(((uint16_t)ram[daddr & 0x1FFFFF] << 8)
                                  | ram[(daddr + 1) & 0x1FFFFF]);
        uint8_t exp[3];
        line_fn(7, daddr, cur16);
        pack_rgb(0, 2, 3, exp);
        out->line_rgb_ok = (line_rgb[7] == (((uint32_t)exp[0] << 16)
                                          | ((uint32_t)exp[1] << 8)
                                          | exp[2])) ? 1 : 0;
    }
}

/* ---------- one emulation run ---------- */

static int do_run(const char *core, const char *rom, unsigned frames,
                  int replace_on, const char *sysdir, run_result *out)
{
    harness_config cfg = HARNESS_CONFIG_DEFAULT;
    unsigned i;
    char state_path[1200];
    crc_fn crc_get;

    memset(out, 0, sizeof(*out));
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
    probe_shadow(&cfg, out);

    harness_shutdown(&cfg);
    unlink(state_path);
    return 1;
}

/* ---------- comparisons ---------- */

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
    char dir_o[64], dir_n[64], dir_p[64];
    harness_result results[8];
    unsigned nres = 0;
    int failed = 0;
    static char d_nopack[224], d_inert[224], d_pres[320], d_line[128],
                d_det[224];

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
    if (!ro || !rn || !rp || !rq) {
        fprintf(stderr, "FAIL: out of memory\n");
        return 1;
    }

    snprintf(dir_o, sizeof(dir_o), "/tmp/vj_texrep_o_%d", (int)getpid());
    snprintf(dir_n, sizeof(dir_n), "/tmp/vj_texrep_n_%d", (int)getpid());
    snprintf(dir_p, sizeof(dir_p), "/tmp/vj_texrep_p_%d", (int)getpid());
    mkdir(dir_o, 0777);
    mkdir(dir_n, 0777);
    mkdir(dir_p, 0777);

    /* Run O: replacement disabled (the machine baseline). */
    if (!do_run(argcfg.core_path, argcfg.rom_path, frames, 0, dir_o, ro))
        goto run_fail;
    /* Run N: enabled, but no pack directory exists in this sysdir. */
    if (!do_run(argcfg.core_path, argcfg.rom_path, frames, 1, dir_n, rn))
        goto run_fail;
    /* Build the synthetic pack from first principles, then run P
     * (enabled, pack present) twice for determinism. */
    if (!build_pack(dir_p, ro->crc)) {
        fprintf(stderr, "FAIL: could not build the synthetic pack\n");
        goto run_fail;
    }
    if (!do_run(argcfg.core_path, argcfg.rom_path, frames, 1, dir_p, rp))
        goto run_fail;
    if (!do_run(argcfg.core_path, argcfg.rom_path, frames, 1, dir_p, rq))
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
            if (rp->hits[t] != tiles[t].expect_hits || rp->wrong_rgb[t])
                ok = 0;
            snprintf(frag, sizeof(frag), " t%u=%u/%u", t, rp->hits[t],
                     tiles[t].expect_hits);
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

    {
        harness_config rcfg = HARNESS_CONFIG_DEFAULT;
        rcfg.json_output = json;
        rcfg.quiet = quiet;
        harness_report(&rcfg, results, nres);
    }

    rm_rf_pack(dir_o);
    rm_rf_pack(dir_n);
    rm_rf_pack(dir_p);
    free(ro); free(rn); free(rp); free(rq);
    return failed ? 1 : 0;

run_fail:
    fprintf(stderr, "FAIL: emulation run did not complete\n");
    rm_rf_pack(dir_o);
    rm_rf_pack(dir_n);
    rm_rf_pack(dir_p);
    free(ro); free(rn); free(rp); free(rq);
    return 1;
}
