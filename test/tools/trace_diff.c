/* test/tools/trace_diff.c -- structurally diff two "VJTR" vjtrace event
 * ring dumps (see src/core/vjtrace.c:vjtrace_dump()).
 *
 * Standalone C99, no harness dependency: includes src/core/vjtrace.h
 * only for the vjtrace_ev struct / event enum (declared outside the
 * VJ_TRACE guard for exactly this kind of offline tool).
 *
 * Comparison key is (type, who, addr) in filtered stream order -- NOT
 * frame/seq, since two runs of the same ROM can offset by a frame or
 * two and still be "the same shape" event-for-event. Default filter
 * excludes VJT_EV_OP_OBJECT (fires once per displayed object, per-
 * object noise that swamps everything else); --types LIST replaces
 * the filter outright with exactly the named types.
 *
 * Build:
 *   cc -O2 -Wall -std=c99 -I. -o test/tools/trace_diff test/tools/trace_diff.c
 *
 * Usage:
 *   trace_diff A B [--types LIST]     (LIST = comma-separated type names)
 *
 * Exit: 0 = filtered streams identical, 1 = divergence found,
 * 2 = usage error or malformed/truncated input file.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <errno.h>

#include "src/core/vjtrace.h"

/* Case-insensitive exact-match compare, local rather than strcasecmp():
 * Apple libc declares strcasecmp() in <strings.h> unconditionally, but
 * glibc hides it under -std=c99 (strict-ANSI suppresses the
 * _DEFAULT_SOURCE feature-test macro that guards it), so a build that
 * is clean here can warn "implicit declaration" on Linux. */
static int ci_eq(const char *a, const char *b)
{
    while (*a && *b) {
        char ca = *a, cb = *b;
        if (ca >= 'a' && ca <= 'z') ca = (char)(ca - 'a' + 'A');
        if (cb >= 'a' && cb <= 'z') cb = (char)(cb - 'a' + 'A');
        if (ca != cb) return 0;
        a++; b++;
    }
    return *a == '\0' && *b == '\0';
}

static const char *type_names[VJT_EV__COUNT] = {
    "NONE", "IRQ_ASSERT", "IRQ_DISPATCH", "GPU_GO", "GPU_STOP",
    "OP_LIST_START", "OP_OBJECT", "OP_GPU_OBJ", "OP_BRANCH", "BLIT_CMD",
    "INPUT_EDGE", "WATCH_RD", "WATCH_WR", "SNAPSHOT", "MARK"
};

#define WHO_COUNT 10
static const char *who_names[WHO_COUNT] = {
    "UNKNOWN", "JAGUAR", "DSP", "GPU", "TOM", "JERRY",
    "M68K", "BLITTER", "OP", "DEBUG"
};

static const char *type_name(unsigned t)
{
    static char buf[24];
    if (t < VJT_EV__COUNT) return type_names[t];
    snprintf(buf, sizeof(buf), "TYPE_%u", t);
    return buf;
}

static const char *who_name(unsigned w)
{
    static char buf[24];
    if (w < WHO_COUNT) return who_names[w];
    snprintf(buf, sizeof(buf), "WHO_%u", w);
    return buf;
}

static int type_from_name(const char *name)
{
    int i;
    for (i = 0; i < VJT_EV__COUNT; i++)
        if (ci_eq(name, type_names[i]))
            return i;
    return -1;
}

typedef struct {
    char magic[4];
    uint32_t version, ev_size, pad;
    uint64_t count;
} vjtr_header;

static int read_header(FILE *f, vjtr_header *h)
{
    if (fread(h->magic, 1, 4, f) != 4) return 0;
    if (fread(&h->version, sizeof(uint32_t), 1, f) != 1) return 0;
    if (fread(&h->ev_size, sizeof(uint32_t), 1, f) != 1) return 0;
    if (fread(&h->pad, sizeof(uint32_t), 1, f) != 1) return 0;
    if (fread(&h->count, sizeof(uint64_t), 1, f) != 1) return 0;
    return 1;
}

static void print_record(const char *tag, long idx, const vjtrace_ev *e)
{
    printf("%s[%ld] seq=%llu f=%u hl=%u %s who=%s pc=%X addr=%08X val=%X\n",
           tag, idx, (unsigned long long)e->seq, (unsigned)e->frame,
           (unsigned)e->halfline, type_name(e->type), who_name(e->who),
           (unsigned)e->pc, (unsigned)e->addr, (unsigned)e->value);
}

/* Loads FILE, validates the VJTR header, and returns a malloc'd array
 * of only the events passing the type filter (types[] of length
 * ntypes; NULL/0 means "all types"). *out_n receives the filtered
 * count. Returns NULL and leaves *out_n at -1 on malformed input
 * (caller exits 2); returns a valid (possibly zero-length, non-NULL
 * for n>0) buffer otherwise. */
static vjtrace_ev *load_filtered(const char *path, const int *keep_mask,
                                  long *out_n)
{
    FILE *f;
    vjtr_header hdr;
    vjtrace_ev *buf;
    long n = 0;
    uint64_t idx;
    long header_bytes, filesize, data_bytes;
    uint64_t actual_count;
    size_t alloc_count;

    *out_n = -1;
    f = fopen(path, "rb");
    if (!f) {
        fprintf(stderr, "trace_diff: cannot open '%s': %s\n", path, strerror(errno));
        return NULL;
    }
    if (!read_header(f, &hdr)) {
        fprintf(stderr, "trace_diff: '%s': truncated header (not a VJTR file?)\n", path);
        fclose(f);
        return NULL;
    }
    if (memcmp(hdr.magic, "VJTR", 4) != 0) {
        fprintf(stderr, "trace_diff: '%s': bad magic (not a VJTR ring dump)\n", path);
        fclose(f);
        return NULL;
    }
    if (hdr.version != 1) {
        fprintf(stderr, "trace_diff: '%s': unsupported version %u (want 1)\n",
                path, (unsigned)hdr.version);
        fclose(f);
        return NULL;
    }
    if (hdr.ev_size != (uint32_t)sizeof(vjtrace_ev)) {
        fprintf(stderr,
                "trace_diff: '%s': record size %u does not match this tool's "
                "vjtrace_ev (%u)\n", path, (unsigned)hdr.ev_size,
                (unsigned)sizeof(vjtrace_ev));
        fclose(f);
        return NULL;
    }

    /* hdr.count is attacker/corruption-controlled: a crafted file can
     * claim ~2^59 records while containing none, which would wrap
     * `count * sizeof(vjtrace_ev)` in a plain size_t multiply into a
     * tiny allocation that the read loop below then overruns (task-6
     * review Critical #1). Don't trust it -- derive the real record
     * count from the file's actual length and require it to match
     * exactly before allocating anything sized by it. */
    header_bytes = ftell(f);
    if (header_bytes < 0) {
        fprintf(stderr, "trace_diff: '%s': ftell failed after header\n", path);
        fclose(f);
        return NULL;
    }
    if (fseek(f, 0, SEEK_END) != 0) {
        fprintf(stderr, "trace_diff: '%s': seek to end failed\n", path);
        fclose(f);
        return NULL;
    }
    filesize = ftell(f);
    if (filesize < 0) {
        fprintf(stderr, "trace_diff: '%s': ftell failed at end\n", path);
        fclose(f);
        return NULL;
    }
    if (fseek(f, header_bytes, SEEK_SET) != 0) {
        fprintf(stderr, "trace_diff: '%s': seek back to records failed\n", path);
        fclose(f);
        return NULL;
    }
    data_bytes = filesize - header_bytes;
    if (data_bytes < 0 || (uint64_t)data_bytes % hdr.ev_size != 0) {
        fprintf(stderr,
                "trace_diff: '%s': file length (%ld bytes of record data) is "
                "not a whole number of %u-byte records -- truncated or corrupt\n",
                path, data_bytes < 0 ? 0L : data_bytes, (unsigned)hdr.ev_size);
        fclose(f);
        return NULL;
    }
    actual_count = (uint64_t)data_bytes / hdr.ev_size;
    if (actual_count != hdr.count) {
        fprintf(stderr,
                "trace_diff: '%s': header claims %llu records but the file "
                "actually contains %llu -- truncated or corrupt\n",
                path, (unsigned long long)hdr.count, (unsigned long long)actual_count);
        fclose(f);
        return NULL;
    }
    if (actual_count > SIZE_MAX / sizeof(vjtrace_ev)) {
        fprintf(stderr, "trace_diff: '%s': record count too large to allocate\n", path);
        fclose(f);
        return NULL;
    }

    alloc_count = (actual_count > 0) ? (size_t)actual_count : 1; /* n=0 placeholder */
    buf = (vjtrace_ev *)malloc(alloc_count * sizeof(vjtrace_ev));
    if (!buf) {
        fprintf(stderr, "trace_diff: out of memory reading '%s'\n", path);
        fclose(f);
        return NULL;
    }

    for (idx = 0; idx < hdr.count; idx++) {
        vjtrace_ev ev;
        if (fread(&ev, sizeof(ev), 1, f) != 1) {
            fprintf(stderr,
                    "trace_diff: '%s': truncated record stream (header said "
                    "%llu records, only %llu present)\n",
                    path, (unsigned long long)hdr.count, (unsigned long long)idx);
            free(buf);
            fclose(f);
            return NULL;
        }
        if (idx == 0 && ev.seq != 0) {
            fprintf(stderr,
                    "WARNING: '%s': ring wrapped -- the first %llu events "
                    "(seq 0..%llu) were evicted before the dump. Raise "
                    "VJ_TRACE_RING and re-run.\n",
                    path, (unsigned long long)ev.seq,
                    (unsigned long long)(ev.seq - 1));
        }
        if (ev.type < VJT_EV__COUNT && !keep_mask[ev.type])
            continue;
        buf[n++] = ev;
    }

    fclose(f);
    *out_n = n;
    return buf;
}

int main(int argc, char **argv)
{
    const char *pathA = NULL, *pathB = NULL;
    int keep_mask[VJT_EV__COUNT];
    int i, t;
    long nA = 0, nB = 0;
    vjtrace_ev *A, *B;
    long divergence = -1;
    int len_diverges = 0;

    for (i = 0; i < VJT_EV__COUNT; i++)
        keep_mask[i] = 1;
    keep_mask[VJT_EV_OP_OBJECT] = 0; /* default filter */

    for (i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--types") == 0 && i + 1 < argc) {
            char *list = strdup(argv[++i]);
            char *tok;
            if (!list) {
                fprintf(stderr, "trace_diff: out of memory\n");
                return 2;
            }
            for (t = 0; t < VJT_EV__COUNT; t++)
                keep_mask[t] = 0;
            tok = strtok(list, ",");
            while (tok) {
                int ty = type_from_name(tok);
                if (ty < 0) {
                    fprintf(stderr, "trace_diff: unknown type '%s' in --types\n", tok);
                    free(list);
                    return 2;
                }
                keep_mask[ty] = 1;
                tok = strtok(NULL, ",");
            }
            free(list);
        } else if (!pathA) {
            pathA = argv[i];
        } else if (!pathB) {
            pathB = argv[i];
        } else {
            fprintf(stderr, "trace_diff: unexpected arg '%s'\n", argv[i]);
            return 2;
        }
    }

    if (!pathA || !pathB) {
        fprintf(stderr, "usage: trace_diff A B [--types LIST]\n");
        return 2;
    }

    A = load_filtered(pathA, keep_mask, &nA);
    if (nA < 0) return 2;
    B = load_filtered(pathB, keep_mask, &nB);
    if (nB < 0) { free(A); return 2; }

    {
        long minlen = (nA < nB) ? nA : nB;
        long i2;
        for (i2 = 0; i2 < minlen; i2++) {
            if (A[i2].type != B[i2].type || A[i2].who != B[i2].who ||
                A[i2].addr != B[i2].addr) {
                divergence = i2;
                break;
            }
        }
        if (divergence < 0 && nA != nB) {
            divergence = minlen;
            len_diverges = 1;
        }
    }

    if (divergence < 0) {
        printf("trace_diff: %ld records compared (filtered), no divergence\n", nA);
        free(A);
        free(B);
        return 0;
    }

    if (len_diverges)
        printf("trace_diff: streams diverge at filtered index %ld -- "
               "A has %ld records, B has %ld records\n", divergence, nA, nB);
    else
        printf("trace_diff: streams diverge at filtered index %ld "
               "(key mismatch: type/who/addr)\n", divergence);

    printf("== %s ==\n", pathA);
    {
        long lo, hi, i2;
        lo = divergence - 5; if (lo < 0) lo = 0;
        hi = divergence + 5; if (hi > nA - 1) hi = nA - 1;
        for (i2 = lo; i2 <= hi; i2++)
            print_record(i2 == divergence ? ">>>A" : "   A", i2, &A[i2]);
    }
    printf("== %s ==\n", pathB);
    {
        long lo, hi, i2;
        lo = divergence - 5; if (lo < 0) lo = 0;
        hi = divergence + 5; if (hi > nB - 1) hi = nB - 1;
        for (i2 = lo; i2 <= hi; i2++)
            print_record(i2 == divergence ? ">>>B" : "   B", i2, &B[i2]);
    }

    free(A);
    free(B);
    return 1;
}
