/* test_texdump.c -- Test gates for texture dump mode (issue #369,
 * deliverable 1).  Spec: docs/texture-dump.md, section "Test gates".
 *
 * One binary runs the loaded ROM four times and asserts, in order:
 *
 *   1. contract_freeze   dump-on manifest hash set equals the committed
 *                        golden list (test/expected/texdump_<rom>.txt).
 *                        The CI tripwire that makes "stable from day
 *                        one" real: any change that moves a hash fails
 *                        loudly and forces a deliberate contract v2.
 *   2. determinism       two consecutive dump-on runs produce identical
 *                        hash sets.
 *   3. engine_independence  fast vs accurate blitter yield identical
 *                        hash sets (the property that justifies the
 *                        register-described capture).
 *   4. inertness_on      dump ENABLED leaves per-frame framebuffer
 *                        hashes AND savestate digests (frames N/2, N)
 *                        identical to dump-off -- the emulated machine
 *                        cannot observe the feature.  (This subsumes
 *                        the weaker "dump off equals baseline" gate:
 *                        the off run IS the baseline here.)
 *   5. png_validity      every emitted PNG parses: signature, IHDR
 *                        dimensions matching the manifest row, and
 *                        every chunk's CRC-32 (via src/core/crc32.c --
 *                        no external decoder).
 *
 * Also asserts every first-sight manifest row has its PNG on disk, and
 * reports the unique-tile count (the spec wants >= 20 for a meaningful
 * tripwire; the golden list committed for yarc.j64 documents the real
 * number).
 *
 * Build (see the Makefile rule):
 *   cc -O2 -Wall -std=c99 -I./libretro-common/include \
 *      -o test/tools/test_texdump test/tools/test_texdump.c \
 *      test/harness/harness.c src/core/crc32.c -ldl -lm
 *
 * Usage: ./test/tools/test_texdump <core> [rom] [--frames N]
 *          [--golden FILE] [--update-golden] [--json] [--quiet]
 *
 * --update-golden rewrites the golden list from this run (for the
 * initial commit and for DELIBERATE contract bumps only -- say so in
 * the commit message).
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

#define MAX_FRAMES  1200
#define MAX_HASHES  8192
#define HASH_CHARS  16

typedef struct {
    char     hashes[MAX_HASHES][HASH_CHARS + 1];
    unsigned count;
    /* width/height per first-sight row, parallel to hashes[] */
    unsigned w[MAX_HASHES], h[MAX_HASHES];
} hashset;

typedef struct {
    uint64_t fb[MAX_FRAMES];
    unsigned fb_count;
    uint64_t digest_mid, digest_end;
    hashset  set;
    char     dumpdir[1024];   /* <sysdir>/vj_texdump/<crc8> */
} run_result;

/* ---------- small utils ---------- */

#define FNV_OFFSET 1469598103934665603ULL
#define FNV_PRIME  1099511628211ULL

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

static int cmp_hash(const void *a, const void *b)
{
    return strcmp((const char *)a, (const char *)b);
}

static void sort_set(hashset *s)
{
    /* w/h stay attached to the manifest parse order; only the hash list
     * itself needs to be order-free for comparison, so sort a copy...
     * simpler: sort in place and drop w/h association by re-reading the
     * manifest for the PNG check (see check_pngs).  Here w/h were
     * already consumed, so in-place is fine. */
    qsort(s->hashes, s->count, HASH_CHARS + 1, cmp_hash);
}

static int sets_equal(const hashset *a, const hashset *b)
{
    unsigned i;
    if (a->count != b->count)
        return 0;
    for (i = 0; i < a->count; i++)
        if (strcmp(a->hashes[i], b->hashes[i]) != 0)
            return 0;
    return 1;
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

/* ---------- per-frame capture ---------- */

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
        /* Duped frame: carry the previous hash forward (it presents the
         * previous image), matching the harness's own convention. */
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

/* ---------- manifest / dump dir ---------- */

static int find_dumpdir(const char *sysdir, char *out, size_t out_size)
{
    char base[1024];
    DIR *d;
    struct dirent *e;
    int found = 0;

    snprintf(base, sizeof(base), "%s/vj_texdump", sysdir);
    d = opendir(base);
    if (!d)
        return 0;
    while ((e = readdir(d)) != NULL) {
        if (e->d_name[0] == '.')
            continue;
        snprintf(out, out_size, "%s/%s", base, e->d_name);
        found = 1;
        break;
    }
    closedir(d);
    return found;
}

/* Parse manifest.tsv: collect first-sight rows (those whose second
 * tab-separated field is WxH).  Returns 0 on open failure. */
static int read_manifest(const char *dumpdir, hashset *s)
{
    char path[1200];
    char line[512];
    FILE *f;

    s->count = 0;
    snprintf(path, sizeof(path), "%s/manifest.tsv", dumpdir);
    f = fopen(path, "r");
    if (!f)
        return 0;
    while (fgets(line, sizeof(line), f)) {
        char *tab1, *tab2;
        unsigned w, h;
        if (line[0] == '#')
            continue;
        tab1 = strchr(line, '\t');
        if (!tab1 || (size_t)(tab1 - line) != HASH_CHARS)
            continue;
        tab2 = strchr(tab1 + 1, '\t');
        if (!tab2)
            continue;
        if (sscanf(tab1 + 1, "%ux%u", &w, &h) != 2)
            continue;   /* palette-sighting row, not first sight */
        if (s->count >= MAX_HASHES)
            break;
        memcpy(s->hashes[s->count], line, HASH_CHARS);
        s->hashes[s->count][HASH_CHARS] = '\0';
        s->w[s->count] = w;
        s->h[s->count] = h;
        s->count++;
    }
    fclose(f);
    return 1;
}

/* ---------- PNG validation ---------- */

static uint32_t be32(const uint8_t *p)
{
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16)
         | ((uint32_t)p[2] << 8) | (uint32_t)p[3];
}

/* Full structural parse: signature, IHDR first with expected dims,
 * every chunk CRC verified, ends with IEND.  Returns NULL on success or
 * a static error string. */
static const char *check_png(const char *path, unsigned exp_w, unsigned exp_h)
{
    static const uint8_t sig[8] = { 137, 80, 78, 71, 13, 10, 26, 10 };
    static char err[256];
    FILE *f = fopen(path, "rb");
    uint8_t *buf;
    long len;
    size_t pos;
    int first = 1, saw_iend = 0;

    if (!f) {
        snprintf(err, sizeof(err), "missing file %s", path);
        return err;
    }
    fseek(f, 0, SEEK_END);
    len = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (len < 8 + 25) {
        fclose(f);
        snprintf(err, sizeof(err), "%s: too short (%ld bytes)", path, len);
        return err;
    }
    buf = (uint8_t *)malloc((size_t)len);
    if (!buf || fread(buf, 1, (size_t)len, f) != (size_t)len) {
        free(buf);
        fclose(f);
        snprintf(err, sizeof(err), "%s: read failed", path);
        return err;
    }
    fclose(f);

    if (memcmp(buf, sig, 8) != 0) {
        free(buf);
        snprintf(err, sizeof(err), "%s: bad PNG signature", path);
        return err;
    }
    pos = 8;
    while (pos + 12 <= (size_t)len) {
        uint32_t clen = be32(buf + pos);
        uint32_t crc_stored, crc_calc;
        if (pos + 12 + clen > (size_t)len) {
            free(buf);
            snprintf(err, sizeof(err), "%s: chunk overruns file", path);
            return err;
        }
        crc_stored = be32(buf + pos + 8 + clen);
        crc_calc = (uint32_t)crc32_calcCheckSum(buf + pos + 4, clen + 4);
        if (crc_stored != crc_calc) {
            free(buf);
            snprintf(err, sizeof(err), "%s: chunk CRC mismatch "
                     "(%08x != %08x)", path, crc_stored, crc_calc);
            return err;
        }
        if (first) {
            uint32_t w, h;
            if (memcmp(buf + pos + 4, "IHDR", 4) != 0) {
                free(buf);
                snprintf(err, sizeof(err), "%s: first chunk not IHDR", path);
                return err;
            }
            w = be32(buf + pos + 8);
            h = be32(buf + pos + 12);
            if (w != exp_w || h != exp_h) {
                free(buf);
                snprintf(err, sizeof(err), "%s: IHDR %ux%u != manifest %ux%u",
                         path, w, h, exp_w, exp_h);
                return err;
            }
            first = 0;
        }
        if (memcmp(buf + pos + 4, "IEND", 4) == 0)
            saw_iend = 1;
        pos += 12 + clen;
    }
    free(buf);
    if (!saw_iend) {
        snprintf(err, sizeof(err), "%s: no IEND chunk", path);
        return err;
    }
    return NULL;
}

/* Every first-sight row must have its PNG, and every PNG must parse. */
static const char *check_pngs(const run_result *r)
{
    static char err[300];
    unsigned i;
    for (i = 0; i < r->set.count; i++) {
        char path[1300];
        const char *e;
        snprintf(path, sizeof(path), "%s/%s.png", r->dumpdir,
                 r->set.hashes[i]);
        if (access(path, R_OK) != 0) {
            /* 16bpp 'both' mode would use -cry/-rgb suffixes, but this
             * test runs the default cry mode, so plain <hash>.png is
             * the contract. */
            snprintf(err, sizeof(err), "no PNG for manifest row %s",
                     r->set.hashes[i]);
            return err;
        }
        e = check_png(path, r->set.w[i], r->set.h[i]);
        if (e)
            return e;
    }
    return NULL;
}

/* ---------- synthetic blit battery ----------
 *
 * Neither in-tree public ROM blits more than its two boot-time code
 * copies through the blitter with SRCEN (yarc and jagniccc render via
 * the OP / pattern fills), so a ROM run alone is a 2-hash tripwire.
 * This battery drives real B_CMD launches through the core's own bus
 * (JaguarWriteLong -> TOM -> BlitterWriteWord -> TexDumpLaunch) over
 * tile bytes this test wrote into emulated RAM: every bpp, both source
 * channels (A2 and DSTA2's A1), and a palette-change re-blit that must
 * NOT mint a new hash (bytes-only identity) but must append a clut
 * sighting row.  Needs the TEST_EXPORTS wide ABI for JaguarWriteLong /
 * jaguarMainRAM. */

typedef void (*jw32_fn)(uint32_t, uint32_t, uint32_t);
typedef void (*jw16_fn)(uint32_t, uint16_t, uint32_t);

typedef struct {
    jw16_fn  ww;
    jw32_fn  wl;
    uint8_t *ram;
} synth_ctx;

#define SYNTH_SRC  0x180000u   /* tile bytes (well above yarc's use)  */
#define SYNTH_DST  0x1A0000u   /* blit destination scratch            */

/* A1/A2 FLAGS for a pixel-mode window: width 2^e pixels, pitch 0. */
static uint32_t synth_flags(unsigned log2_width, unsigned psize)
{
    return 0x10000u                       /* XADDCTL = add pixel size */
         | ((uint32_t)log2_width << 11)   /* width float: m=0, e      */
         | ((uint32_t)psize << 3);
}

/* Launch one W x H copy blit with source at src_addr.  psize is the
 * FLAGS pixel-size field (0..5); log2w must satisfy W <= 2^log2w.
 * dsta2 flips the channels (A1 becomes the source). */
static void synth_blit(const synth_ctx *sc, uint32_t src_addr,
                       unsigned w, unsigned h, unsigned log2w,
                       unsigned psize, int dsta2)
{
    uint32_t sflags = synth_flags(log2w, psize);
    uint32_t dflags = synth_flags(log2w, psize);
    uint32_t step   = ((uint32_t)1 << 16) | ((uint32_t)(-(int)w) & 0xFFFF);
    uint32_t cmd    = 0x00600000u   /* LFU = source (REPLACE)  */
                    | 0x00000001u   /* SRCEN                   */
                    | 0x00000200u   /* UPDA1                   */
                    | 0x00000400u;  /* UPDA2                   */
    uint32_t sbase = src_addr, dbase = SYNTH_DST;
    uint32_t a1base, a2base, a1flags, a2flags;

    if (dsta2) {
        cmd |= 0x00000800u;         /* DSTA2: A1 reads, A2 writes */
        a1base = sbase; a1flags = sflags;
        a2base = dbase; a2flags = dflags;
    } else {
        a1base = dbase; a1flags = dflags;
        a2base = sbase; a2flags = sflags;
    }

    sc->wl(0xF02200u, a1base, 0);   /* A1_BASE  */
    sc->wl(0xF02204u, a1flags, 0);  /* A1_FLAGS */
    sc->wl(0xF0220Cu, 0, 0);        /* A1_PIXEL */
    sc->wl(0xF02210u, step, 0);     /* A1_STEP  */
    sc->wl(0xF02218u, 0, 0);        /* A1_FPIXEL*/
    sc->wl(0xF02224u, a2base, 0);   /* A2_BASE  */
    sc->wl(0xF02228u, a2flags, 0);  /* A2_FLAGS */
    sc->wl(0xF02230u, 0, 0);        /* A2_PIXEL */
    sc->wl(0xF02234u, step, 0);     /* A2_STEP  */
    sc->wl(0xF0223Cu, ((uint32_t)h << 16) | w, 0);  /* B_COUNT */
    sc->wl(0xF02238u, cmd, 0);      /* B_CMD -> launch */
}

static void synth_set_clut(const synth_ctx *sc, unsigned seed)
{
    unsigned i;
    for (i = 0; i < 256; i++)
        sc->ww(0xF00400u + i * 2, (uint16_t)(i * 257u ^ seed), 0);
}

/* Fill a source tile with deterministic bytes.  Returns bytes used. */
static uint32_t synth_fill(const synth_ctx *sc, uint32_t addr,
                           uint32_t nbytes, unsigned seed)
{
    uint32_t i;
    for (i = 0; i < nbytes; i++)
        sc->ram[(addr + i) & 0x1FFFFF] =
            (uint8_t)((i * 31u + seed * 97u + (i >> 5)) & 0xFF);
    return nbytes;
}

/* Expected battery population (keep in sync with the loops below):
 * 8 x 8bpp + 4 x 4bpp + 4 x 16bpp + 4 x 32bpp + 2 x 1bpp + 2 x 2bpp
 * + 2 x DSTA2 8bpp = 26 unique tiles; 1 palette sighting (re-blit of
 * an already-seen tile under a different CLUT). */
#define SYNTH_EXPECTED_UNIQUE 26

static int synth_battery(harness_config *cfg)
{
    synth_ctx sc;
    void *sym;
    unsigned k;
    uint32_t src = SYNTH_SRC;

    sc.wl = (jw32_fn)harness_dlsym(cfg, "JaguarWriteLong");
    sc.ww = (jw16_fn)harness_dlsym(cfg, "JaguarWriteWord");
    sym   = harness_dlsym(cfg, "jaguarMainRAM");
    if (!sc.wl || !sc.ww || !sym)
        return 0;                    /* slim ABI: battery unavailable */
    sc.ram = *(uint8_t **)sym;
    if (!sc.ram)
        return 0;

    synth_set_clut(&sc, 0x0000);

    /* 8 x 8bpp 16x16 */
    for (k = 0; k < 8; k++) {
        synth_fill(&sc, src, 16 * 16, 0x10 + k);
        synth_blit(&sc, src, 16, 16, 4, 3, 0);
        src += 0x200;
    }
    /* 4 x 4bpp 16x8 */
    for (k = 0; k < 4; k++) {
        synth_fill(&sc, src, 16 * 8 / 2, 0x20 + k);
        synth_blit(&sc, src, 16, 8, 4, 2, 0);
        src += 0x100;
    }
    /* 4 x 16bpp 8x8 */
    for (k = 0; k < 4; k++) {
        synth_fill(&sc, src, 8 * 8 * 2, 0x30 + k);
        synth_blit(&sc, src, 8, 8, 3, 4, 0);
        src += 0x100;
    }
    /* 4 x 32bpp 4x4 */
    for (k = 0; k < 4; k++) {
        synth_fill(&sc, src, 4 * 4 * 4, 0x40 + k);
        synth_blit(&sc, src, 4, 4, 2, 5, 0);
        src += 0x100;
    }
    /* 2 x 1bpp 32x8 */
    for (k = 0; k < 2; k++) {
        synth_fill(&sc, src, 32 * 8 / 8, 0x50 + k);
        synth_blit(&sc, src, 32, 8, 5, 0, 0);
        src += 0x100;
    }
    /* 2 x 2bpp 16x8 */
    for (k = 0; k < 2; k++) {
        synth_fill(&sc, src, 16 * 8 / 4, 0x60 + k);
        synth_blit(&sc, src, 16, 8, 4, 1, 0);
        src += 0x100;
    }
    /* 2 x DSTA2 (A1 as source) 8bpp 16x16 */
    for (k = 0; k < 2; k++) {
        synth_fill(&sc, src, 16 * 16, 0x70 + k);
        synth_blit(&sc, src, 16, 16, 4, 3, 1);
        src += 0x200;
    }

    /* Palette-change re-blit: same bytes as tile #1, new CLUT.  Must
     * NOT create a new hash (palette is never identity) -- it appends a
     * clut= sighting row instead. */
    synth_set_clut(&sc, 0x5A5A);
    synth_fill(&sc, SYNTH_SRC, 16 * 16, 0x10);
    synth_blit(&sc, SYNTH_SRC, 16, 16, 4, 3, 0);

    return 1;
}

/* Count palette-sighting rows (hash TAB clut= ...) in the manifest. */
static unsigned count_sightings(const char *dumpdir)
{
    char path[1200];
    char line[512];
    FILE *f;
    unsigned n = 0;

    snprintf(path, sizeof(path), "%s/manifest.tsv", dumpdir);
    f = fopen(path, "r");
    if (!f)
        return 0;
    while (fgets(line, sizeof(line), f)) {
        char *tab1 = strchr(line, '\t');
        if (line[0] == '#' || !tab1)
            continue;
        if (!strncmp(tab1 + 1, "clut=", 5))
            n++;
    }
    fclose(f);
    return n;
}

/* ---------- one emulation run ---------- */

static int do_run(const char *core, const char *rom, unsigned frames,
                  int dump_on, int fast_blitter, int synth,
                  const char *sysdir, run_result *out, int quiet)
{
    harness_config cfg = HARNESS_CONFIG_DEFAULT;
    unsigned i;
    char state_path[256];

    memset(out, 0, sizeof(*out));
    cfg.core_path  = core;
    cfg.rom_path   = rom;
    cfg.frames     = frames;
    cfg.quiet      = 1;
    cfg.system_dir = sysdir;
    cfg.options[cfg.num_options].key   = "virtualjaguar_texture_dump";
    cfg.options[cfg.num_options].value = dump_on ? "enabled" : "disabled";
    cfg.num_options++;
    cfg.options[cfg.num_options].key   = "virtualjaguar_usefastblitter";
    cfg.options[cfg.num_options].value = fast_blitter ? "enabled" : "disabled";
    cfg.num_options++;
    cfg.options[cfg.num_options].key   = "virtualjaguar_texdump_16bpp";
    cfg.options[cfg.num_options].value = "cry";
    cfg.num_options++;
    cfg.video_callback = video_cb;

    if (!harness_load_core(&cfg))
        return 0;
    if (!harness_load_rom(&cfg)) {
        harness_shutdown(&cfg);
        return 0;
    }

    snprintf(state_path, sizeof(state_path), "%s/texdump_probe.state", sysdir);
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
    if (getenv("TEXDUMP_DEBUG"))
        fprintf(stderr, "DBG run dump=%d fast=%d mid=%016llx end=%016llx\n",
                dump_on, fast_blitter,
                (unsigned long long)out->digest_mid,
                (unsigned long long)out->digest_end);
    if (synth && !synth_battery(&cfg)) {
        fprintf(stderr, "texdump: synthetic battery needs the TEST_EXPORTS "
                "wide ABI (JaguarWriteLong/jaguarMainRAM not exported)\n");
        harness_shutdown(&cfg);
        return 0;
    }
    /* Unload BEFORE reading the manifest: retro_unload_game closes it
     * (and logs the session summary). */
    harness_shutdown(&cfg);
    unlink(state_path);

    if (dump_on) {
        if (!find_dumpdir(sysdir, out->dumpdir, sizeof(out->dumpdir))) {
            if (!quiet)
                fprintf(stderr, "texdump: no dump directory under %s\n", sysdir);
            return 0;
        }
        if (!read_manifest(out->dumpdir, &out->set)) {
            if (!quiet)
                fprintf(stderr, "texdump: no manifest in %s\n", out->dumpdir);
            return 0;
        }
    }
    return 1;
}

/* ---------- golden list ---------- */

static int read_golden(const char *path, hashset *s)
{
    FILE *f = fopen(path, "r");
    char line[64];
    s->count = 0;
    if (!f)
        return 0;
    while (fgets(line, sizeof(line), f)) {
        size_t n = strspn(line, "0123456789abcdef");
        if (n != HASH_CHARS)
            continue;
        if (s->count >= MAX_HASHES)
            break;
        memcpy(s->hashes[s->count], line, HASH_CHARS);
        s->hashes[s->count][HASH_CHARS] = '\0';
        s->count++;
    }
    fclose(f);
    return 1;
}

static int write_golden(const char *path, const hashset *s)
{
    FILE *f = fopen(path, "w");
    unsigned i;
    if (!f)
        return 0;
    fprintf(f, "# texdump v1 golden hash list -- see docs/texture-dump.md.\n");
    fprintf(f, "# Regenerate ONLY for a deliberate contract bump:\n");
    fprintf(f, "#   ./test/tools/test_texdump <core> <rom> --update-golden\n");
    for (i = 0; i < s->count; i++)
        fprintf(f, "%s\n", s->hashes[i]);
    fclose(f);
    return 1;
}

/* ---------- scratch dirs ---------- */

static void rm_rf_dump(const char *sysdir)
{
    /* Remove <sysdir>/vj_texdump/<crc>/ contents + dirs + sysdir.  Only
     * files this test created; no recursion beyond the known layout. */
    char base[1100], sub[1200], fp[2400];
    DIR *d, *d2;
    struct dirent *e, *e2;

    snprintf(base, sizeof(base), "%s/vj_texdump", sysdir);
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

/* ---------- main ---------- */

int main(int argc, char **argv)
{
    harness_config argcfg = HARNESS_CONFIG_DEFAULT;
    const char *golden_path = "test/expected/texdump_yarc.txt";
    int update_golden = 0;
    int json = 0, quiet = 0;
    unsigned frames;
    int i;
    run_result *ra, *rb, *rc, *rd, *re;
    hashset golden;
    char dir_a[64], dir_b[64], dir_c[64], dir_d[64], dir_e[64];
    harness_result results[12];
    unsigned nres = 0;
    int failed = 0;
    static char d_freeze[256], d_det[128], d_eng[128], d_inert[192],
                d_png[320], d_count[160], d_synth[320], d_spng[320],
                d_sight[160];

    /* Pre-parse this tool's own flags and REMOVE them from the argv the
     * harness sees: its parser skips unknown flags but would read
     * --golden's value as a positional ROM path. */
    {
        char **fargv = (char **)malloc(sizeof(char *) * (size_t)argc);
        int fargc = 0;
        if (!fargv)
            return 1;
        fargv[fargc++] = argv[0];
        for (i = 1; i < argc; i++) {
            if (!strcmp(argv[i], "--golden") && i + 1 < argc)
                golden_path = argv[++i];
            else if (!strcmp(argv[i], "--update-golden"))
                update_golden = 1;
            else {
                if (!strcmp(argv[i], "--json"))
                    json = 1;
                else if (!strcmp(argv[i], "--quiet"))
                    quiet = 1;
                fargv[fargc++] = argv[i];
            }
        }
        argc = fargc;
        argv = fargv;
    }

    argcfg.frames = 600;
    if (!harness_init_from_args(&argcfg, argc, argv)) {
        fprintf(stderr, "usage: %s <core> [rom] [--frames N] [--golden F] "
                "[--update-golden] [--json] [--quiet]\n", argv[0]);
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

    ra = (run_result *)calloc(1, sizeof(run_result));
    rb = (run_result *)calloc(1, sizeof(run_result));
    rc = (run_result *)calloc(1, sizeof(run_result));
    rd = (run_result *)calloc(1, sizeof(run_result));
    re = (run_result *)calloc(1, sizeof(run_result));
    if (!ra || !rb || !rc || !rd || !re) {
        fprintf(stderr, "FAIL: out of memory\n");
        return 1;
    }

    snprintf(dir_a, sizeof(dir_a), "/tmp/vj_texdump_test_a_%d", (int)getpid());
    snprintf(dir_b, sizeof(dir_b), "/tmp/vj_texdump_test_b_%d", (int)getpid());
    snprintf(dir_c, sizeof(dir_c), "/tmp/vj_texdump_test_c_%d", (int)getpid());
    snprintf(dir_d, sizeof(dir_d), "/tmp/vj_texdump_test_d_%d", (int)getpid());
    snprintf(dir_e, sizeof(dir_e), "/tmp/vj_texdump_test_e_%d", (int)getpid());
    mkdir(dir_a, 0777);
    mkdir(dir_b, 0777);
    mkdir(dir_c, 0777);
    mkdir(dir_d, 0777);
    mkdir(dir_e, 0777);

    /* Run order note: the fast-blitter run goes LAST.  These runs share
     * one process, and harness_init_from_args() itself holds a dlopen
     * reference, so core statics stay resident across runs (the iOS
     * no-dlclose regime).  A fast->accurate engine flip leaves a
     * residue that moves the NEXT run's savestate digest -- reproduced
     * with texture dump never enabled at all, so it is a pre-existing
     * cross-load leak, not a texdump effect.  With the off-baseline (D)
     * before the fast run (C), every comparison below is between runs
     * whose predecessors used the same engine. */

    /* Run A: dump on, accurate blitter (the reference run). */
    if (!do_run(argcfg.core_path, argcfg.rom_path, frames, 1, 0, 0, dir_a, ra, quiet))
        goto run_fail;
    /* Run B: identical -> determinism. */
    if (!do_run(argcfg.core_path, argcfg.rom_path, frames, 1, 0, 0, dir_b, rb, quiet))
        goto run_fail;
    /* Run D: dump off, accurate -> the inertness baseline. */
    if (!do_run(argcfg.core_path, argcfg.rom_path, frames, 0, 0, 0, dir_d, rd, quiet))
        goto run_fail;
    /* Run E: synthetic blit battery (a few boot frames, then driven
     * B_CMD launches over test-authored tile bytes -- every bpp, both
     * source channels, palette-change re-blit). */
    if (!do_run(argcfg.core_path, argcfg.rom_path, 10, 1, 0, 1, dir_e, re, quiet))
        goto run_fail;
    /* Run C: fast blitter -> engine independence of the hash set. */
    if (!do_run(argcfg.core_path, argcfg.rom_path, frames, 1, 1, 0, dir_c, rc, quiet))
        goto run_fail;

    /* PNG check needs w/h in manifest order; run before sorting. */
    {
        const char *e = check_pngs(ra);
        snprintf(d_png, sizeof(d_png), "%s",
                 e ? e : "all PNGs parse (signature, IHDR dims, chunk CRCs)");
        results[nres].status = e ? "FAIL" : "PASS";
        results[nres].name   = "png_validity";
        results[nres].detail = d_png;
        if (e) failed = 1;
        nres++;
    }

    sort_set(&ra->set);
    sort_set(&rb->set);
    sort_set(&rc->set);

    /* Gate: determinism (two identical runs, identical sets). */
    {
        int ok = sets_equal(&ra->set, &rb->set);
        snprintf(d_det, sizeof(d_det), "run A %u vs run B %u hashes",
                 ra->set.count, rb->set.count);
        results[nres].status = ok ? "PASS" : "FAIL";
        results[nres].name   = "determinism";
        results[nres].detail = d_det;
        if (!ok) failed = 1;
        nres++;
    }

    /* Gate: engine independence. */
    {
        int ok = sets_equal(&ra->set, &rc->set);
        snprintf(d_eng, sizeof(d_eng), "accurate %u vs fast %u hashes",
                 ra->set.count, rc->set.count);
        results[nres].status = ok ? "PASS" : "FAIL";
        results[nres].name   = "engine_independence";
        results[nres].detail = d_eng;
        if (!ok) failed = 1;
        nres++;
    }

    /* Gate: dump-on inertness (fb hashes + savestate digests vs off). */
    {
        int first_diff = -1;
        unsigned f;
        int ok;
        for (f = 0; f < frames; f++) {
            if (ra->fb[f] != rd->fb[f]) {
                first_diff = (int)f;
                break;
            }
        }
        ok = (first_diff < 0)
          && ra->digest_mid && ra->digest_end
          && ra->digest_mid == rd->digest_mid
          && ra->digest_end == rd->digest_end;
        if (first_diff >= 0)
            snprintf(d_inert, sizeof(d_inert),
                     "framebuffer diverges at frame %d", first_diff + 1);
        else if (!ra->digest_mid || !ra->digest_end)
            snprintf(d_inert, sizeof(d_inert), "savestate capture failed");
        else if (ok)
            snprintf(d_inert, sizeof(d_inert),
                     "%u frames + savestate digests @%u/%u identical on/off",
                     frames, frames / 2, frames);
        else
            snprintf(d_inert, sizeof(d_inert),
                     "savestate digest differs (@%u: %d, @%u: %d)",
                     frames / 2, ra->digest_mid != rd->digest_mid,
                     frames, ra->digest_end != rd->digest_end);
        results[nres].status = ok ? "PASS" : "FAIL";
        results[nres].name   = "inertness_on";
        results[nres].detail = d_inert;
        if (!ok) failed = 1;
        nres++;
    }

    /* Synthetic battery gates: PNG validity across every bpp, unique
     * count exactly as scripted (ROM boot tiles + battery), and exactly
     * one palette-sighting row -- the bytes-only identity claim made
     * falsifiable: a CLUT change must NOT mint a new hash. */
    {
        const char *e = check_pngs(re);
        snprintf(d_spng, sizeof(d_spng), "%s",
                 e ? e : "battery PNGs parse across 1/2/4/8/16/32bpp");
        results[nres].status = e ? "FAIL" : "PASS";
        results[nres].name   = "synth_png_validity";
        results[nres].detail = d_spng;
        if (e) failed = 1;
        nres++;
    }
    {
        unsigned expect = ra->set.count + SYNTH_EXPECTED_UNIQUE;
        int ok = (re->set.count == expect);
        snprintf(d_synth, sizeof(d_synth),
                 "battery dumped %u unique tiles (expected %u: %u ROM boot "
                 "+ %u scripted)", re->set.count, expect, ra->set.count,
                 (unsigned)SYNTH_EXPECTED_UNIQUE);
        results[nres].status = ok ? "PASS" : "FAIL";
        results[nres].name   = "synth_population";
        results[nres].detail = d_synth;
        if (!ok) failed = 1;
        nres++;
    }
    {
        unsigned s = count_sightings(re->dumpdir);
        int ok = (s == 1);
        snprintf(d_sight, sizeof(d_sight),
                 "%u palette sighting row(s) (expected 1: CLUT change never "
                 "mints a new hash)", s);
        results[nres].status = ok ? "PASS" : "FAIL";
        results[nres].name   = "palette_not_identity";
        results[nres].detail = d_sight;
        if (!ok) failed = 1;
        nres++;
    }

    /* Tile population sanity: the golden list is only a meaningful
     * tripwire with a real population behind it.  No in-tree public ROM
     * blits more than its two boot copies, so the battery run supplies
     * the population (docs/texture-dump.md open item 3). */
    {
        int ok = re->set.count >= 20;
        snprintf(d_count, sizeof(d_count), "%u unique tiles in the battery "
                 "manifest (tripwire needs >= 20)", re->set.count);
        results[nres].status = ok ? "PASS" : "FAIL";
        results[nres].name   = "tile_population";
        results[nres].detail = d_count;
        if (!ok) failed = 1;
        nres++;
    }

    sort_set(&re->set);

    /* Gate: contract freeze vs the committed golden list.  The frozen
     * set is the UNION of the ROM run (run A) and the battery run (run
     * E), so the identity contract is pinned across every bpp and both
     * source channels, not just what the ROM happens to blit. */
    {
        static hashset combined;
        unsigned ia = 0, ie = 0;
        combined.count = 0;
        while ((ia < ra->set.count || ie < re->set.count)
               && combined.count < MAX_HASHES) {
            int c;
            if (ia >= ra->set.count) c = 1;
            else if (ie >= re->set.count) c = -1;
            else c = strcmp(ra->set.hashes[ia], re->set.hashes[ie]);
            if (c < 0)
                strcpy(combined.hashes[combined.count++], ra->set.hashes[ia++]);
            else if (c > 0)
                strcpy(combined.hashes[combined.count++], re->set.hashes[ie++]);
            else {
                strcpy(combined.hashes[combined.count++], ra->set.hashes[ia++]);
                ie++;
            }
        }

        if (update_golden) {
            int ok = write_golden(golden_path, &combined);
            snprintf(d_freeze, sizeof(d_freeze), "golden list %s: %u hashes %s",
                     golden_path, combined.count, ok ? "written" : "WRITE FAILED");
            results[nres].status = ok ? "INFO" : "FAIL";
            results[nres].name   = "contract_freeze";
            results[nres].detail = d_freeze;
            if (!ok) failed = 1;
            nres++;
        } else {
            int ok = 0;
            if (!read_golden(golden_path, &golden)) {
                snprintf(d_freeze, sizeof(d_freeze),
                         "golden list missing: %s (run with --update-golden "
                         "for a DELIBERATE contract (re)freeze)", golden_path);
            } else {
                ok = sets_equal(&combined, &golden);
                if (ok)
                    snprintf(d_freeze, sizeof(d_freeze),
                             "%u hashes match %s", combined.count, golden_path);
                else {
                    unsigned ic = 0, ig = 0, miss = 0, add = 0;
                    while (ic < combined.count || ig < golden.count) {
                        int c;
                        if (ic >= combined.count) c = 1;
                        else if (ig >= golden.count) c = -1;
                        else c = strcmp(combined.hashes[ic], golden.hashes[ig]);
                        if (c == 0) { ic++; ig++; }
                        else if (c < 0) { add++; ic++; }
                        else { miss++; ig++; }
                    }
                    snprintf(d_freeze, sizeof(d_freeze),
                             "IDENTITY CONTRACT MOVED: %u new / %u missing vs "
                             "%s (%u run, %u golden) -- an intentional change "
                             "needs --update-golden AND a contract-version bump "
                             "rationale in the commit", add, miss, golden_path,
                             combined.count, golden.count);
                }
            }
            results[nres].status = ok ? "PASS" : "FAIL";
            results[nres].name   = "contract_freeze";
            results[nres].detail = d_freeze;
            if (!ok) failed = 1;
            nres++;
        }
    }

    /* Report through the shared harness formatting (json aware). */
    {
        harness_config rcfg = HARNESS_CONFIG_DEFAULT;
        rcfg.json_output = json;
        rcfg.quiet = quiet;
        harness_report(&rcfg, results, nres);
    }

    rm_rf_dump(dir_a);
    rm_rf_dump(dir_b);
    rm_rf_dump(dir_c);
    rm_rf_dump(dir_d);
    rm_rf_dump(dir_e);
    free(ra); free(rb); free(rc); free(rd); free(re);
    return failed ? 1 : 0;

run_fail:
    fprintf(stderr, "FAIL: emulation run did not complete\n");
    rm_rf_dump(dir_a);
    rm_rf_dump(dir_b);
    rm_rf_dump(dir_c);
    rm_rf_dump(dir_d);
    rm_rf_dump(dir_e);
    free(ra); free(rb); free(rc); free(rd); free(re);
    return 1;
}
