/* test/tools/trace_dump.c -- print a "VJTR" vjtrace event ring dump
 * (see src/core/vjtrace.c:vjtrace_dump()) as text or JSONL.
 *
 * Standalone C99, no harness dependency: includes src/core/vjtrace.h
 * only for the vjtrace_ev struct / event enum, which are declared
 * outside the VJ_TRACE guard specifically so offline tools like this
 * one can use them without building the whole trace facility.
 *
 * File format (host-endian, native struct layout -- writer and reader
 * run on the same host, see vjtrace.h's vjtrace_snapshot() comment for
 * the sibling VJSN format's rationale):
 *   header: char magic[4]="VJTR"; uint32_t version=1; uint32_t ev_size;
 *           uint32_t pad; uint64_t count;
 *   then `count` vjtrace_ev records, oldest first.
 *
 * Build:
 *   cc -O2 -Wall -std=c99 -I. -o test/tools/trace_dump test/tools/trace_dump.c
 *
 * Usage:
 *   trace_dump FILE [--type NAME] [--frame A:B] [--who NAME] [--json]
 *
 * Exit: 0 = ran and printed (possibly zero matching records is still
 * success -- there is no "difference" concept here), 1 = unused by
 * this tool, 2 = usage error or malformed/truncated input file.
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

/* ---- static name tables -------------------------------------------- */

/* Index = enum value in vjtrace.h. Names are the enum tag with the
 * VJT_EV_ prefix stripped. */
static const char *type_names[VJT_EV__COUNT] = {
    "NONE", "IRQ_ASSERT", "IRQ_DISPATCH", "GPU_GO", "GPU_STOP",
    "OP_LIST_START", "OP_OBJECT", "OP_GPU_OBJ", "OP_BRANCH", "BLIT_CMD",
    "INPUT_EDGE", "WATCH_RD", "WATCH_WR", "SNAPSHOT", "MARK"
};

/* Index = enum value in src/core/vjag_memory.h:
 *   enum { UNKNOWN, JAGUAR, DSP, GPU, TOM, JERRY, M68K, BLITTER, OP, DEBUG }; */
#define WHO_COUNT 10
static const char *who_names[WHO_COUNT] = {
    "UNKNOWN", "JAGUAR", "DSP", "GPU", "TOM", "JERRY",
    "M68K", "BLITTER", "OP", "DEBUG"
};

/* Small register-symbolization table for the `addr` field. Every entry
 * verified against docs/jtrm-register-map.md -- do not add an address
 * without checking it there first; a wrong symbol is worse than none.
 * (The brief's illustrative example paired $F0223C with B_CMD; the
 * JTRM register map puts B_CMD at $F02238 and B_COUNT at $F0223C --
 * corrected here.) */
typedef struct { uint32_t addr; const char *name; } regsym_t;
static const regsym_t regsyms[] = {
    { 0x00F00006u, "VC" },
    { 0x00F02100u, "G_FLAGS" },
    { 0x00F02104u, "G_MTXC" },
    { 0x00F02108u, "G_MTXA" },
    { 0x00F0210Cu, "G_END" },
    { 0x00F02110u, "G_PC" },
    { 0x00F02114u, "G_CTRL" },
    { 0x00F02238u, "B_CMD" },
    { 0x00F0223Cu, "B_COUNT" },
    { 0x00F14000u, "JOYSTICK" },
    { 0x00F1A100u, "D_FLAGS" },
    { 0x00F1A110u, "D_PC" },
    { 0x00F1A114u, "D_CTRL" }
};
#define NREGSYMS (int)(sizeof(regsyms) / sizeof(regsyms[0]))

static const char *sym_lookup(uint32_t addr)
{
    int i;
    for (i = 0; i < NREGSYMS; i++)
        if (regsyms[i].addr == addr)
            return regsyms[i].name;
    return NULL;
}

static const char *type_name(unsigned t)
{
    static char buf[24];
    if (t < VJT_EV__COUNT)
        return type_names[t];
    snprintf(buf, sizeof(buf), "TYPE_%u", t);
    return buf;
}

static const char *who_name(unsigned w)
{
    static char buf[24];
    if (w < WHO_COUNT)
        return who_names[w];
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

static int who_from_name(const char *name)
{
    int i;
    for (i = 0; i < WHO_COUNT; i++)
        if (ci_eq(name, who_names[i]))
            return i;
    return -1;
}

/* ---- header ---------------------------------------------------------- */

typedef struct {
    char magic[4];
    uint32_t version, ev_size, pad;
    uint64_t count;
} vjtr_header;

/* Reads the on-disk header as individual field reads (not one struct
 * fwrite/fread) so this tool never depends on this compiler's struct
 * padding matching the writer's -- see vjtrace.c's vjt_write() comment
 * for why the VJSN writer does the same for its own header. */
static int read_header(FILE *f, vjtr_header *h)
{
    if (fread(h->magic, 1, 4, f) != 4) return 0;
    if (fread(&h->version, sizeof(uint32_t), 1, f) != 1) return 0;
    if (fread(&h->ev_size, sizeof(uint32_t), 1, f) != 1) return 0;
    if (fread(&h->pad, sizeof(uint32_t), 1, f) != 1) return 0;
    if (fread(&h->count, sizeof(uint64_t), 1, f) != 1) return 0;
    return 1;
}

static void print_record_text(const vjtrace_ev *e)
{
    const char *sym = sym_lookup(e->addr);
    printf("seq=%llu f=%u hl=%u %s who=%s pc=%X addr=%08X",
           (unsigned long long)e->seq, (unsigned)e->frame,
           (unsigned)e->halfline, type_name(e->type), who_name(e->who),
           (unsigned)e->pc, (unsigned)e->addr);
    if (sym)
        printf(" sym=%s", sym);
    printf(" val=%X\n", (unsigned)e->value);
}

static void print_record_json(const vjtrace_ev *e)
{
    const char *sym = sym_lookup(e->addr);
    printf("{\"seq\":%llu,\"frame\":%u,\"halfline\":%u,\"type\":\"%s\","
           "\"who\":\"%s\",\"pc\":%u,\"addr\":%u,\"sym\":",
           (unsigned long long)e->seq, (unsigned)e->frame,
           (unsigned)e->halfline, type_name(e->type), who_name(e->who),
           (unsigned)e->pc, (unsigned)e->addr);
    if (sym)
        printf("\"%s\"", sym);
    else
        printf("null");
    printf(",\"val\":%u}\n", (unsigned)e->value);
}

int main(int argc, char **argv)
{
    const char *path = NULL;
    int have_type = 0, want_type = -1;
    int have_who = 0, want_who = -1;
    int have_frame = 0;
    unsigned frame_lo = 0, frame_hi = 0;
    int json = 0;
    int i;
    FILE *f;
    vjtr_header hdr;
    uint64_t idx;

    for (i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--type") == 0 && i + 1 < argc) {
            want_type = type_from_name(argv[++i]);
            if (want_type < 0) {
                fprintf(stderr, "trace_dump: unknown --type '%s'\n", argv[i]);
                return 2;
            }
            have_type = 1;
        } else if (strcmp(argv[i], "--who") == 0 && i + 1 < argc) {
            want_who = who_from_name(argv[++i]);
            if (want_who < 0) {
                fprintf(stderr, "trace_dump: unknown --who '%s'\n", argv[i]);
                return 2;
            }
            have_who = 1;
        } else if (strcmp(argv[i], "--frame") == 0 && i + 1 < argc) {
            if (sscanf(argv[++i], "%u:%u", &frame_lo, &frame_hi) != 2) {
                fprintf(stderr, "trace_dump: bad --frame '%s' (want A:B)\n", argv[i]);
                return 2;
            }
            have_frame = 1;
        } else if (strcmp(argv[i], "--json") == 0) {
            json = 1;
        } else if (!path) {
            path = argv[i];
        } else {
            fprintf(stderr, "trace_dump: unexpected arg '%s'\n", argv[i]);
            return 2;
        }
    }

    if (!path) {
        fprintf(stderr,
                "usage: trace_dump FILE [--type NAME] [--frame A:B] "
                "[--who NAME] [--json]\n");
        return 2;
    }

    f = fopen(path, "rb");
    if (!f) {
        fprintf(stderr, "trace_dump: cannot open '%s': %s\n", path, strerror(errno));
        return 2;
    }

    if (!read_header(f, &hdr)) {
        fprintf(stderr, "trace_dump: '%s': truncated header (not a VJTR file?)\n", path);
        fclose(f);
        return 2;
    }
    if (memcmp(hdr.magic, "VJTR", 4) != 0) {
        fprintf(stderr, "trace_dump: '%s': bad magic (not a VJTR ring dump)\n", path);
        fclose(f);
        return 2;
    }
    if (hdr.version != 1) {
        fprintf(stderr, "trace_dump: '%s': unsupported version %u (want 1)\n",
                path, (unsigned)hdr.version);
        fclose(f);
        return 2;
    }
    if (hdr.ev_size != (uint32_t)sizeof(vjtrace_ev)) {
        fprintf(stderr,
                "trace_dump: '%s': record size %u does not match this tool's "
                "vjtrace_ev (%u) -- built against a different vjtrace.h?\n",
                path, (unsigned)hdr.ev_size, (unsigned)sizeof(vjtrace_ev));
        fclose(f);
        return 2;
    }

    for (idx = 0; idx < hdr.count; idx++) {
        vjtrace_ev ev;
        if (fread(&ev, sizeof(ev), 1, f) != 1) {
            fprintf(stderr,
                    "trace_dump: '%s': truncated record stream (header said "
                    "%llu records, only %llu present)\n",
                    path, (unsigned long long)hdr.count, (unsigned long long)idx);
            fclose(f);
            return 2;
        }
        if (have_type && ev.type != (uint8_t)want_type) continue;
        if (have_who && ev.who != (uint8_t)want_who) continue;
        if (have_frame && (ev.frame < frame_lo || ev.frame > frame_hi)) continue;
        if (json)
            print_record_json(&ev);
        else
            print_record_text(&ev);
    }

    fclose(f);
    return 0;
}
