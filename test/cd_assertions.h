/*
 * cd_assertions.h - Shared helpers for the CD HLE boot test suite.
 *
 * Provides:
 *   - discover_discs() : recursive scan of a disc-image root for cue/iso/cdi
 *   - per-frame assertion helpers that operate on a vj_core
 *   - SHA1 helpers used by the regression-baseline JSON sidecar
 *
 * Designed to be #included by test_cd_hle_boot.c (single TU; no separate .c).
 */

#ifndef CD_ASSERTIONS_H
#define CD_ASSERTIONS_H

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <strings.h>
#include <dirent.h>
#include <sys/stat.h>
#include <sys/types.h>

#include "test_framework.h"

#define CD_ASSERT_MAX_DISCS         64
#define CD_ASSERT_MAX_PATH_LEN      4096
#define CD_ASSERT_MAX_SCAN_DEPTH    8

/* m68k register IDs (mirrors test_hle_bios.c) */
#ifndef M68K_REG_PC
#define M68K_REG_D0  0
#define M68K_REG_D1  1
#define M68K_REG_A0  8
#define M68K_REG_A1  9
#define M68K_REG_PC 16
#define M68K_REG_SP 18
#endif

struct cd_disc_entry {
    char path[CD_ASSERT_MAX_PATH_LEN];
    char ext[8];                /* lowercase, no leading dot; copied so memmove-safe */
    size_t file_size;
};

struct cd_disc_list {
    struct cd_disc_entry entries[CD_ASSERT_MAX_DISCS];
    size_t count;
};

/* ------------------------------------------------------------------ */
/* String helpers                                                      */
/* ------------------------------------------------------------------ */

static inline const char *cd_disc_extension(const char *path)
{
    const char *dot = strrchr(path, '.');
    if (!dot || dot == path) return "";
    return dot + 1;
}

static inline bool cd_str_iequals(const char *a, const char *b)
{
    return strcasecmp(a, b) == 0;
}

static inline int cd_disc_priority(const char *ext)
{
    if (cd_str_iequals(ext, "cue")) return 0;
    if (cd_str_iequals(ext, "iso")) return 1;
    if (cd_str_iequals(ext, "cdi")) return 2;
    return -1;
}

/* Honors VJ_TEST_CD_EXTS (comma-separated) to filter which extensions to
 * enumerate. Default is "cue" — CDI/ISO are opt-in because:
 *   CDI: parser still has at least one disc that crashes (see baldies.cdi);
 *        opt in once the parser is hardened.
 *   ISO: Jaguar boot from ISO is fundamentally degraded (no session 2 audio);
 *        useful for read-only sanity but not boot smoke. */
static inline bool cd_ext_enabled(const char *ext)
{
    const char *list = getenv("VJ_TEST_CD_EXTS");
    size_t elen;
    const char *p;
    if (!list || !list[0]) list = "cue";
    if (cd_str_iequals(list, "all")) return cd_disc_priority(ext) >= 0;

    elen = strlen(ext);
    p = list;
    while (*p) {
        const char *q = p;
        size_t segLen;
        while (*q && *q != ',') q++;
        segLen = (size_t)(q - p);
        if (segLen == elen && strncasecmp(p, ext, elen) == 0)
            return true;
        p = (*q == ',') ? q + 1 : q;
    }
    return false;
}

static inline bool cd_should_skip_dir(const char *name)
{
    if (name[0] == '.') return true;
    if (cd_str_iequals(name, "BigPEmu_v121-DEV")) return true;
    /* Conventional BIOS directory marker we use in the corpus. */
    if (strncmp(name, "[BIOS]", 6) == 0) return true;
    return false;
}

/* ------------------------------------------------------------------ */
/* Discovery                                                           */
/* ------------------------------------------------------------------ */

static void cd_disc_list_add(struct cd_disc_list *list,
                             const char *path, const char *ext, size_t size)
{
    struct cd_disc_entry *e;
    size_t i;
    if (list->count >= CD_ASSERT_MAX_DISCS) return;
    e = &list->entries[list->count++];
    snprintf(e->path, sizeof(e->path), "%s", path);
    snprintf(e->ext, sizeof(e->ext), "%s", ext ? ext : "");
    /* lowercase the ext so cd_disc_priority/cd_str_iequals work uniformly */
    for (i = 0; e->ext[i]; i++)
        if (e->ext[i] >= 'A' && e->ext[i] <= 'Z') e->ext[i] += 32;
    e->file_size = size;
}

/* Drop ISO/CDI entries that share a directory with an already-recorded CUE.
 * CUE wins because it carries pregap/track-type metadata that ISO lacks. */
static void cd_disc_list_dedup(struct cd_disc_list *list)
{
    size_t i, j;
    for (i = 0; i < list->count; i++) {
        const char *cue_dir_end;
        size_t cue_dir_len;
        if (!cd_str_iequals(list->entries[i].ext, "cue")) continue;

        cue_dir_end = strrchr(list->entries[i].path, '/');
        if (!cue_dir_end) continue;
        cue_dir_len = cue_dir_end - list->entries[i].path;

        for (j = 0; j < list->count; ) {
            const char *other_dir_end;
            size_t other_dir_len;
            if (j == i) { j++; continue; }
            if (cd_str_iequals(list->entries[j].ext, "cue")) { j++; continue; }
            other_dir_end = strrchr(list->entries[j].path, '/');
            other_dir_len = other_dir_end ?
                (size_t)(other_dir_end - list->entries[j].path) : 0;
            if (other_dir_len == cue_dir_len &&
                strncmp(list->entries[i].path, list->entries[j].path, cue_dir_len) == 0) {
                /* Remove entry j */
                memmove(&list->entries[j], &list->entries[j + 1],
                        (list->count - j - 1) * sizeof(list->entries[0]));
                list->count--;
                if (j < i) i--;
            } else {
                j++;
            }
        }
    }
}

static int cd_disc_compare(const void *a, const void *b)
{
    const struct cd_disc_entry *ea = (const struct cd_disc_entry *)a;
    const struct cd_disc_entry *eb = (const struct cd_disc_entry *)b;
    int pa = cd_disc_priority(ea->ext);
    int pb = cd_disc_priority(eb->ext);
    if (pa != pb) return pa - pb;
    return strcmp(ea->path, eb->path);
}

static void cd_discover_walk(const char *root, struct cd_disc_list *list, int depth)
{
    DIR *dir;
    struct dirent *de;
    if (depth > CD_ASSERT_MAX_SCAN_DEPTH) return;
    if (list->count >= CD_ASSERT_MAX_DISCS) return;

    dir = opendir(root);
    if (!dir) return;

    while ((de = readdir(dir)) != NULL && list->count < CD_ASSERT_MAX_DISCS) {
        char path[CD_ASSERT_MAX_PATH_LEN];
        struct stat st;
        const char *ext;
        if (cd_should_skip_dir(de->d_name)) continue;

        snprintf(path, sizeof(path), "%s/%s", root, de->d_name);

        if (stat(path, &st) != 0) continue;

        if (S_ISDIR(st.st_mode)) {
            cd_discover_walk(path, list, depth + 1);
            continue;
        }
        if (!S_ISREG(st.st_mode)) continue;

        ext = cd_disc_extension(de->d_name);
        if (cd_disc_priority(ext) < 0) continue;
        if (!cd_ext_enabled(ext)) continue;

        cd_disc_list_add(list, path, ext, (size_t)st.st_size);
    }

    closedir(dir);
}

static void cd_discover_discs(const char *root, struct cd_disc_list *list)
{
    const char *env_root;
    memset(list, 0, sizeof(*list));

    /* Honor optional override: VJ_TEST_CD_ROOT */
    env_root = getenv("VJ_TEST_CD_ROOT");
    if (env_root && env_root[0]) root = env_root;

    cd_discover_walk(root, list, 0);
    cd_disc_list_dedup(list);
    qsort(list->entries, list->count, sizeof(list->entries[0]), cd_disc_compare);
}

/* True if the disc basename contains the substring in VJ_TEST_CD_FOCUS (case-insensitive),
 * or if the env var is unset (no filter). */
static bool cd_disc_in_focus(const char *path)
{
    const char *needle;
    size_t nlen, plen, i;
    needle = getenv("VJ_TEST_CD_FOCUS");
    if (!needle || !needle[0]) return true;

    /* Naive case-insensitive substring search */
    nlen = strlen(needle);
    plen = strlen(path);
    if (nlen > plen) return false;
    for (i = 0; i + nlen <= plen; i++) {
        size_t k = 0;
        while (k < nlen) {
            char a, b;
            a = path[i + k]; if (a >= 'A' && a <= 'Z') a += 32;
            b = needle[k];   if (b >= 'A' && b <= 'Z') b += 32;
            if (a != b) break;
            k++;
        }
        if (k == nlen) return true;
    }
    return false;
}

/* ------------------------------------------------------------------ */
/* Per-frame assertions                                                */
/*                                                                     */
/* These return true on success and emit a one-line diagnostic on      */
/* failure. They DO NOT abort the test loop — caller decides whether   */
/* a single frame failure terminates the per-disc test.                 */
/* ------------------------------------------------------------------ */

/* Returns 0 = in-range, otherwise the offending PC. Caller decides whether to log. */
static inline uint32_t cd_pc_oob(struct vj_core *core)
{
    uint32_t pc;
    if (!core->m68k_get_reg) return 0;
    pc = core->m68k_get_reg(NULL, M68K_REG_PC);
    if (pc < 0x200000) return 0;
    if (pc >= 0xE00000 && pc < 0xE20000) return 0;  /* boot ROM */
    if (pc >= 0x800000 && pc < 0x900000) return 0;  /* cart / CD BIOS */
    return pc;
}

#define CD_PC_HISTORY_LEN 64
#define CD_PC_UNIQUE_CAP  256

/* Tracks both a sliding window of recent PCs (for "stuck self-loop"
 * detection) and a bounded set of unique PCs seen over the whole run
 * (for "tight 2-instruction retry-loop" detection). */
struct cd_pc_history {
    uint32_t samples[CD_PC_HISTORY_LEN];
    size_t   write_idx;
    size_t   filled;

    uint32_t unique[CD_PC_UNIQUE_CAP];
    size_t   unique_count;
    bool     unique_overflow;  /* set when we exceed the cap (= healthy variety) */
};

static inline void cd_pc_history_push(struct cd_pc_history *h, uint32_t pc)
{
    size_t i;
    h->samples[h->write_idx] = pc;
    h->write_idx = (h->write_idx + 1) % CD_PC_HISTORY_LEN;
    if (h->filled < CD_PC_HISTORY_LEN) h->filled++;

    if (h->unique_overflow) return;
    for (i = 0; i < h->unique_count; i++)
        if (h->unique[i] == pc) return;
    if (h->unique_count >= CD_PC_UNIQUE_CAP) {
        h->unique_overflow = true;
        return;
    }
    h->unique[h->unique_count++] = pc;
}

/* True if every PC sample in the recent window is identical (tight self-loop). */
static inline bool cd_pc_history_is_self_loop(const struct cd_pc_history *h)
{
    uint32_t first;
    size_t i;
    if (h->filled < CD_PC_HISTORY_LEN) return false;
    first = h->samples[0];
    for (i = 1; i < h->filled; i++)
        if (h->samples[i] != first) return false;
    return true;
}

/* True if the run only ever visited <= max_unique distinct PCs.
 * Catches the "CD_read -> CD_poll -> CD_fifo_disable -> retry" tight loop
 * (Iron Soldier 2 bounces between two PCs the entire run). */
static inline bool cd_pc_history_is_thrashing(const struct cd_pc_history *h,
                                              size_t max_unique)
{
    if (h->unique_overflow) return false;
    return h->unique_count <= max_unique;
}

/* Counts how many bytes in the given RAM range are non-zero. */
static inline size_t cd_count_nonzero(const uint8_t *ram, uint32_t addr, uint32_t len)
{
    size_t n = 0;
    uint32_t i;
    for (i = 0; i < len; i++) if (ram[addr + i]) n++;
    return n;
}

/* ------------------------------------------------------------------ */
/* SHA1 (small, dependency-free, for baseline sidecar)                 */
/* ------------------------------------------------------------------ */

struct cd_sha1_ctx {
    uint32_t state[5];
    uint64_t bits;
    uint8_t  buf[64];
    size_t   buflen;
};

static inline uint32_t cd_sha1_rol(uint32_t v, unsigned n) { return (v << n) | (v >> (32 - n)); }

static void cd_sha1_block(struct cd_sha1_ctx *ctx, const uint8_t *block)
{
    uint32_t w[80];
    uint32_t a, b, c, d, e;
    int i;
    for (i = 0; i < 16; i++)
        w[i] = ((uint32_t)block[i*4] << 24) | ((uint32_t)block[i*4+1] << 16) |
               ((uint32_t)block[i*4+2] << 8) | (uint32_t)block[i*4+3];
    for (i = 16; i < 80; i++)
        w[i] = cd_sha1_rol(w[i-3] ^ w[i-8] ^ w[i-14] ^ w[i-16], 1);

    a = ctx->state[0]; b = ctx->state[1]; c = ctx->state[2];
    d = ctx->state[3]; e = ctx->state[4];
    for (i = 0; i < 80; i++) {
        uint32_t f, k, t;
        if (i < 20)      { f = (b & c) | ((~b) & d);             k = 0x5A827999; }
        else if (i < 40) { f = b ^ c ^ d;                        k = 0x6ED9EBA1; }
        else if (i < 60) { f = (b & c) | (b & d) | (c & d);      k = 0x8F1BBCDC; }
        else             { f = b ^ c ^ d;                        k = 0xCA62C1D6; }
        t = cd_sha1_rol(a, 5) + f + e + k + w[i];
        e = d; d = c; c = cd_sha1_rol(b, 30); b = a; a = t;
    }
    ctx->state[0] += a; ctx->state[1] += b; ctx->state[2] += c;
    ctx->state[3] += d; ctx->state[4] += e;
}

static void cd_sha1_init(struct cd_sha1_ctx *ctx)
{
    ctx->state[0] = 0x67452301; ctx->state[1] = 0xEFCDAB89;
    ctx->state[2] = 0x98BADCFE; ctx->state[3] = 0x10325476;
    ctx->state[4] = 0xC3D2E1F0;
    ctx->bits = 0; ctx->buflen = 0;
}

static void cd_sha1_update(struct cd_sha1_ctx *ctx, const uint8_t *data, size_t len)
{
    ctx->bits += (uint64_t)len * 8;
    while (len) {
        size_t take;
        take = 64 - ctx->buflen;
        if (take > len) take = len;
        memcpy(ctx->buf + ctx->buflen, data, take);
        ctx->buflen += take; data += take; len -= take;
        if (ctx->buflen == 64) { cd_sha1_block(ctx, ctx->buf); ctx->buflen = 0; }
    }
}

static void cd_sha1_final(struct cd_sha1_ctx *ctx, char out_hex[41])
{
    static const uint8_t pad[64] = { 0x80 };
    static const char hex[] = "0123456789abcdef";
    uint64_t bits;
    uint8_t length_be[8];
    size_t pad_len;
    int i, j;
    bits = ctx->bits;
    for (i = 0; i < 8; i++) length_be[i] = (uint8_t)(bits >> (56 - 8*i));

    pad_len = (ctx->buflen < 56) ? (56 - ctx->buflen) : (120 - ctx->buflen);
    cd_sha1_update(ctx, pad, pad_len);
    cd_sha1_update(ctx, length_be, 8);

    for (i = 0; i < 5; i++) {
        for (j = 0; j < 4; j++) {
            uint8_t v = (uint8_t)(ctx->state[i] >> (24 - j*8));
            out_hex[i*8 + j*2 + 0] = hex[v >> 4];
            out_hex[i*8 + j*2 + 1] = hex[v & 0xF];
        }
    }
    out_hex[40] = '\0';
}

#endif /* CD_ASSERTIONS_H */
