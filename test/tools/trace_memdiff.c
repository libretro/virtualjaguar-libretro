/* test/tools/trace_memdiff.c -- diff two "VJSN" vjtrace state snapshots
 * (see src/core/vjtrace.c:vjtrace_snapshot()).
 *
 * Standalone C99, no harness dependency. Includes src/core/vjtrace.h
 * for parity with the other three analyzers, though the VJSN format
 * this tool reads has no dedicated struct in vjtrace.h -- the writer
 * builds each section header from individual field writes precisely
 * to stay struct-padding-independent (see vjt_snap_section_header()
 * in vjtrace.c), so this tool parses it the same way, field by field.
 *
 * File format (host-endian):
 *   header: char magic[4]="VJSN"; uint32_t version=1; uint32_t nsections;
 *   then nsections of: char name[8] (NUL-padded, not NUL-terminated);
 *   uint32_t base; uint32_t len; followed immediately by len raw bytes.
 *   Fixed v1 section order: MAINRAM, GPURAM, DSPRAM, TOMREG, JERRYREG,
 *   REGS68K (18 uint32), REGSGPU (34 uint32), REGSDSP (34 uint32).
 *
 * Build:
 *   cc -O2 -Wall -std=c99 -I. -o test/tools/trace_memdiff test/tools/trace_memdiff.c
 *
 * Usage:
 *   trace_memdiff A B [--section NAME]
 *
 * Exit: 0 = no differences in the sections compared, 1 = at least one
 * difference found, 2 = usage error, malformed/truncated VJSN file,
 * a fixed-format register section with an unexpected length, or
 * --section naming a section absent from either file.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <errno.h>

#include "src/core/vjtrace.h"

typedef struct {
    char name[9]; /* 8 + NUL */
    uint32_t base;
    uint32_t len;
    uint8_t *data;
} vjsn_section;

typedef struct {
    vjsn_section sec[16];
    int nsec;
} vjsn_file;

/* ---- register/region symbol tables ---------------------------------- */

static const char *regs68k_names[18] = {
    "D0", "D1", "D2", "D3", "D4", "D5", "D6", "D7",
    "A0", "A1", "A2", "A3", "A4", "A5", "A6", "A7",
    "PC", "SR"
};

/* R0..R31, PC, FLAGS. Slot 33 is GPUGetFlags()/DSPGetFlags() (see
 * vjtrace_snapshot() in vjtrace.c and the REGSGPU/REGSDSP section
 * comment in vjtrace.h) -- the GPU/DSP flags register (G_FLAGS
 * $F02100 / D_FLAGS $F1A100 conceptually), NOT the control register
 * (G_CTRL $F02114 / D_CTRL $F1A114), which is a different register at
 * a different address and isn't captured by this snapshot format at
 * all. An earlier draft of this tool labeled the slot CTRL per a
 * literal reading of the task brief's ambiguity-resolution text; a
 * task-6 review (Critical #2) caught the mismatch against the writer
 * and vjtrace.h's own format comment and this was corrected. */
static char gpu_dsp_reg_names_buf[34][8];
static const char **gpu_dsp_reg_names(void)
{
    static const char *ptrs[34];
    static int built = 0;
    int i;
    if (!built) {
        for (i = 0; i < 32; i++) {
            snprintf(gpu_dsp_reg_names_buf[i], sizeof(gpu_dsp_reg_names_buf[i]), "R%d", i);
            ptrs[i] = gpu_dsp_reg_names_buf[i];
        }
        ptrs[32] = "PC";
        ptrs[33] = "FLAGS";
        built = 1;
    }
    return ptrs;
}

/* Small register-symbolization table, verified against
 * docs/jtrm-register-map.md (same table as trace_dump.c). */
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

/* Global Jaguar memory-map regions, from the header comment at the top
 * of src/core/vjag_memory.c (RAM/expansion/cart/BUTCH/BIOS/TOM/JERRY
 * boundaries). Used to annotate MAINRAM diffs with which named region
 * of the address space the differing byte falls in. */
typedef struct { uint32_t lo, hi; const char *name; } region_t;
static const region_t regions[] = {
    { 0x000000u, 0x1FFFFFu, "Main RAM" },
    { 0x200000u, 0x7FFFFFu, "Expansion/mirrored RAM" },
    { 0x800000u, 0xDFFEFFu, "Cartridge ROM" },
    { 0xDFFF00u, 0xDFFFFFu, "BUTCH (Jaguar CD)" },
    { 0xE00000u, 0xEFFFFFu, "System BIOS" },
    { 0xF00000u, 0xF0FFFFu, "TOM" },
    { 0xF10000u, 0xF1FFFFu, "JERRY" }
};
#define NREGIONS (int)(sizeof(regions) / sizeof(regions[0]))

static const char *region_lookup(uint32_t addr)
{
    int i;
    for (i = 0; i < NREGIONS; i++)
        if (addr >= regions[i].lo && addr <= regions[i].hi)
            return regions[i].name;
    return NULL;
}

/* ---- VJSN reader ------------------------------------------------------ */

static int rd_u32(FILE *f, uint32_t *v)
{
    return fread(v, sizeof(uint32_t), 1, f) == 1;
}

static int load_vjsn(const char *path, vjsn_file *out)
{
    FILE *f;
    char magic[4];
    uint32_t version, nsections;
    unsigned i;

    out->nsec = 0;
    f = fopen(path, "rb");
    if (!f) {
        fprintf(stderr, "trace_memdiff: cannot open '%s': %s\n", path, strerror(errno));
        return 0;
    }
    if (fread(magic, 1, 4, f) != 4 || memcmp(magic, "VJSN", 4) != 0) {
        fprintf(stderr, "trace_memdiff: '%s': bad magic (not a VJSN snapshot)\n", path);
        fclose(f);
        return 0;
    }
    if (!rd_u32(f, &version) || version != 1) {
        fprintf(stderr, "trace_memdiff: '%s': unsupported/truncated version field\n", path);
        fclose(f);
        return 0;
    }
    if (!rd_u32(f, &nsections)) {
        fprintf(stderr, "trace_memdiff: '%s': truncated header (no nsections)\n", path);
        fclose(f);
        return 0;
    }
    if (nsections == 0 || nsections > 16) {
        fprintf(stderr, "trace_memdiff: '%s': implausible nsections=%u\n",
                path, (unsigned)nsections);
        fclose(f);
        return 0;
    }

    for (i = 0; i < nsections; i++) {
        vjsn_section *s = &out->sec[i];
        uint32_t base, len;
        if (fread(s->name, 1, 8, f) != 8) {
            fprintf(stderr, "trace_memdiff: '%s': truncated section %u name\n", path, i);
            fclose(f);
            return 0;
        }
        s->name[8] = '\0';
        if (!rd_u32(f, &base) || !rd_u32(f, &len)) {
            fprintf(stderr, "trace_memdiff: '%s': truncated section %u header\n", path, i);
            fclose(f);
            return 0;
        }
        s->base = base;
        s->len = len;
        if (len > 0) {
            s->data = (uint8_t *)malloc(len);
            if (!s->data) {
                fprintf(stderr, "trace_memdiff: out of memory (section %s, %u bytes)\n",
                        s->name, (unsigned)len);
                fclose(f);
                return 0;
            }
            if (fread(s->data, 1, len, f) != len) {
                fprintf(stderr, "trace_memdiff: '%s': truncated section '%s' data "
                                 "(wanted %u bytes)\n", path, s->name, (unsigned)len);
                free(s->data);
                fclose(f);
                return 0;
            }
        } else {
            s->data = NULL;
        }
    }
    out->nsec = (int)nsections;
    fclose(f);
    return 1;
}

static void free_vjsn(vjsn_file *v)
{
    int i;
    for (i = 0; i < v->nsec; i++)
        free(v->sec[i].data);
}

static vjsn_section *find_section(vjsn_file *v, const char *name)
{
    int i;
    for (i = 0; i < v->nsec; i++)
        if (strcmp(v->sec[i].name, name) == 0)
            return &v->sec[i];
    return NULL;
}

static int is_reg_section(const char *name)
{
    return strcmp(name, "REGS68K") == 0 || strcmp(name, "REGSGPU") == 0 ||
           strcmp(name, "REGSDSP") == 0;
}

/* Compares a fixed-format register section as a uint32 array, printing
 * one line per differing register: "NAME REG: $old -> $new". Returns
 * the number of differing registers, or -1 if the length doesn't match
 * the expected v1 layout (18 for REGS68K, 34 for REGSGPU/REGSDSP). */
static long diff_regs(const char *secname, vjsn_section *a, vjsn_section *b)
{
    int expect_n;
    const char **names;
    uint32_t *ra, *rb;
    int i;
    long ndiff = 0;

    if (strcmp(secname, "REGS68K") == 0) {
        expect_n = 18;
        names = regs68k_names;
    } else {
        expect_n = 34;
        names = gpu_dsp_reg_names();
    }

    if (a->len != (uint32_t)(expect_n * 4) || b->len != (uint32_t)(expect_n * 4)) {
        fprintf(stderr,
                "trace_memdiff: section '%s' has unexpected length (a=%u b=%u, "
                "want %u) -- not a v1 register block\n",
                secname, (unsigned)a->len, (unsigned)b->len, (unsigned)(expect_n * 4));
        return -1;
    }

    ra = (uint32_t *)a->data;
    rb = (uint32_t *)b->data;
    for (i = 0; i < expect_n; i++) {
        if (ra[i] != rb[i]) {
            printf("%s %s: $%08X -> $%08X\n", secname, names[i],
                   (unsigned)ra[i], (unsigned)rb[i]);
            ndiff++;
        }
    }
    return ndiff;
}

/* Bytewise diff with run coalescing: consecutive differing bytes join
 * into one run, and a gap of up to 4 identical bytes still joins (i.e.
 * if another difference appears within 4 bytes, it's folded into the
 * same run rather than reported separately). */
#define MAX_SHOWN_BYTES 16

static long diff_bytes(const char *secname, uint32_t base,
                        const uint8_t *a, const uint8_t *b, uint32_t len)
{
    uint32_t i;
    long nruns = 0;

    i = 0;
    while (i < len) {
        uint32_t run_start, run_end, j;
        uint32_t gap;
        uint32_t addr;
        const char *sym, *region;
        uint32_t shown, k;

        if (a[i] == b[i]) { i++; continue; }

        run_start = i;
        run_end = i;
        j = i + 1;
        gap = 0;
        while (j < len) {
            if (a[j] != b[j]) {
                run_end = j;
                gap = 0;
            } else {
                gap++;
                if (gap > 4)
                    break;
            }
            j++;
        }
        i = run_end + 1;
        nruns++;

        addr = base + run_start;
        sym = sym_lookup(addr);
        region = (strcmp(secname, "MAINRAM") == 0) ? region_lookup(addr) : NULL;

        printf("%s $%06X", secname, (unsigned)addr);
        if (sym) printf(" (%s)", sym);
        else if (region) printf(" (%s)", region);
        printf(":");

        shown = run_end - run_start + 1;
        if (shown > MAX_SHOWN_BYTES) shown = MAX_SHOWN_BYTES;

        printf(" ");
        for (k = 0; k < shown; k++)
            printf("%02X%s", a[run_start + k], (k + 1 < shown) ? " " : "");
        if (run_end - run_start + 1 > MAX_SHOWN_BYTES) printf(" ...");
        printf(" ->");
        for (k = 0; k < shown; k++)
            printf(" %02X", b[run_start + k]);
        if (run_end - run_start + 1 > MAX_SHOWN_BYTES) printf(" ...");
        printf(" (run %u bytes)\n", (unsigned)(run_end - run_start + 1));
    }
    return nruns;
}

int main(int argc, char **argv)
{
    const char *pathA = NULL, *pathB = NULL;
    const char *only_section = NULL;
    int i;
    vjsn_file A, B;
    long total_diffs = 0;
    int usage_error = 0;

    for (i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--section") == 0 && i + 1 < argc) {
            only_section = argv[++i];
        } else if (!pathA) {
            pathA = argv[i];
        } else if (!pathB) {
            pathB = argv[i];
        } else {
            fprintf(stderr, "trace_memdiff: unexpected arg '%s'\n", argv[i]);
            return 2;
        }
    }
    if (!pathA || !pathB) {
        fprintf(stderr, "usage: trace_memdiff A B [--section NAME]\n");
        return 2;
    }

    if (!load_vjsn(pathA, &A)) return 2;
    if (!load_vjsn(pathB, &B)) { free_vjsn(&A); return 2; }

    if (only_section) {
        vjsn_section *sa = find_section(&A, only_section);
        vjsn_section *sb = find_section(&B, only_section);
        if (!sa || !sb) {
            fprintf(stderr, "trace_memdiff: section '%s' not present in %s\n",
                    only_section, !sa ? pathA : pathB);
            free_vjsn(&A); free_vjsn(&B);
            return 2;
        }
        if (is_reg_section(only_section)) {
            long d = diff_regs(only_section, sa, sb);
            if (d < 0) { free_vjsn(&A); free_vjsn(&B); return 2; }
            total_diffs += d;
        } else {
            if (sa->len != sb->len) {
                fprintf(stderr,
                        "trace_memdiff: section '%s' length differs between "
                        "files (%s=%u %s=%u) -- incompatible snapshots\n",
                        only_section, pathA, (unsigned)sa->len, pathB, (unsigned)sb->len);
                free_vjsn(&A); free_vjsn(&B);
                return 2;
            }
            total_diffs += diff_bytes(only_section, sa->base, sa->data, sb->data, sa->len);
        }
    } else {
        for (i = 0; i < A.nsec; i++) {
            vjsn_section *sa = &A.sec[i];
            vjsn_section *sb = find_section(&B, sa->name);
            if (!sb) {
                fprintf(stderr,
                        "trace_memdiff: section '%s' present in %s but not in %s\n",
                        sa->name, pathA, pathB);
                total_diffs++; /* asymmetric snapshots count as a difference */
                continue;
            }
            if (is_reg_section(sa->name)) {
                long d = diff_regs(sa->name, sa, sb);
                if (d < 0) { usage_error = 1; continue; }
                total_diffs += d;
            } else {
                if (sa->len != sb->len) {
                    fprintf(stderr,
                            "trace_memdiff: section '%s' length differs between "
                            "files (%s=%u %s=%u) -- incompatible snapshots\n",
                            sa->name, pathA, (unsigned)sa->len, pathB, (unsigned)sb->len);
                    usage_error = 1;
                    continue;
                }
                total_diffs += diff_bytes(sa->name, sa->base, sa->data, sb->data, sa->len);
            }
        }
        for (i = 0; i < B.nsec; i++) {
            if (!find_section(&A, B.sec[i].name)) {
                fprintf(stderr,
                        "trace_memdiff: section '%s' present in %s but not in %s\n",
                        B.sec[i].name, pathB, pathA);
                total_diffs++;
            }
        }
    }

    free_vjsn(&A);
    free_vjsn(&B);

    if (usage_error)
        return 2;

    if (total_diffs == 0) {
        printf("trace_memdiff: no differences\n");
        return 0;
    }
    printf("trace_memdiff: %ld difference(s) reported\n", total_diffs);
    return 1;
}
