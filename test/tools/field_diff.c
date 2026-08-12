/* test/tools/field_diff.c -- row/column diff of two per-field CSVs
 * written by test/harness/trace_probe.c's --field-csv option.
 *
 * Standalone C99, no harness dependency. Does not hardcode a column
 * list: column names come from each file's own header row, and the
 * two headers must name the same columns in the same order (this
 * tool refuses to guess an alignment for a schema mismatch -- that is
 * a malformed-input condition, not a diff).
 *
 * Build:
 *   cc -O2 -Wall -std=c99 -I. -o test/tools/field_diff test/tools/field_diff.c
 *
 * Usage:
 *   field_diff A.csv B.csv
 *
 * Exit: 0 = identical (same header, same row count, every field
 * equal), 1 = a difference was found, 2 = usage error, missing file,
 * empty file, or a ragged/malformed row (column count not matching
 * the header).
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

#define MAXCOLS 64
#define LINEBUF 8192

/* Splits `line` in place on ',' (trailing \n/\r stripped first),
 * filling out[] with pointers into `line` and returning the field
 * count, or -1 if there are more than MAXCOLS fields. */
static int split_csv(char *line, char *out[], int maxcols)
{
    int n = 0;
    char *p = line;
    size_t len = strlen(line);
    while (len > 0 && (line[len - 1] == '\n' || line[len - 1] == '\r')) {
        line[--len] = '\0';
    }
    if (len == 0)
        return 0;
    out[n++] = p;
    while (*p) {
        if (*p == ',') {
            *p = '\0';
            if (n >= maxcols)
                return -1;
            out[n++] = p + 1;
        }
        p++;
    }
    return n;
}

int main(int argc, char **argv)
{
    const char *pathA, *pathB;
    FILE *fa, *fb;
    char lineA[LINEBUF], lineB[LINEBUF];
    char *colsA[MAXCOLS], *colsB[MAXCOLS];
    char *fieldsA[MAXCOLS], *fieldsB[MAXCOLS];
    char names[MAXCOLS][64];
    int ncols, i;
    long row;
    long mismatch_count[MAXCOLS];
    long row_row = -1;
    int row_col = -1;
    char row_a[128], row_b[128];
    int have_first = 0;
    long total_rows_a = 0, total_rows_b = 0;
    int extra_a = 0, extra_b = 0;

    if (argc != 3) {
        fprintf(stderr, "usage: field_diff A.csv B.csv\n");
        return 2;
    }
    pathA = argv[1];
    pathB = argv[2];

    fa = fopen(pathA, "r");
    if (!fa) {
        fprintf(stderr, "field_diff: cannot open '%s': %s\n", pathA, strerror(errno));
        return 2;
    }
    fb = fopen(pathB, "r");
    if (!fb) {
        fprintf(stderr, "field_diff: cannot open '%s': %s\n", pathB, strerror(errno));
        fclose(fa);
        return 2;
    }

    if (!fgets(lineA, sizeof(lineA), fa)) {
        fprintf(stderr, "field_diff: '%s' is empty (no header)\n", pathA);
        fclose(fa); fclose(fb);
        return 2;
    }
    if (!fgets(lineB, sizeof(lineB), fb)) {
        fprintf(stderr, "field_diff: '%s' is empty (no header)\n", pathB);
        fclose(fa); fclose(fb);
        return 2;
    }

    {
        int ncolsA = split_csv(lineA, colsA, MAXCOLS);
        int ncolsB = split_csv(lineB, colsB, MAXCOLS);
        if (ncolsA < 0 || ncolsB < 0) {
            fprintf(stderr, "field_diff: header has more than %d columns\n", MAXCOLS);
            fclose(fa); fclose(fb);
            return 2;
        }
        if (ncolsA != ncolsB) {
            fprintf(stderr,
                    "field_diff: header column count mismatch ('%s' has %d, "
                    "'%s' has %d) -- different --field-csv schema, cannot align\n",
                    pathA, ncolsA, pathB, ncolsB);
            fclose(fa); fclose(fb);
            return 2;
        }
        ncols = ncolsA;
        for (i = 0; i < ncols; i++) {
            if (strcmp(colsA[i], colsB[i]) != 0) {
                fprintf(stderr,
                        "field_diff: header column %d name mismatch ('%s' vs '%s') "
                        "-- cannot align columns\n", i, colsA[i], colsB[i]);
                fclose(fa); fclose(fb);
                return 2;
            }
            strncpy(names[i], colsA[i], sizeof(names[i]) - 1);
            names[i][sizeof(names[i]) - 1] = '\0';
            mismatch_count[i] = 0;
        }
    }

    row = 0;
    for (;;) {
        char *gotA = fgets(lineA, sizeof(lineA), fa);
        char *gotB = fgets(lineB, sizeof(lineB), fb);
        int nA, nB;

        if (!gotA && !gotB)
            break;
        if (gotA && !gotB) { extra_a = 1; break; }
        if (!gotA && gotB) { extra_b = 1; break; }

        row++;
        nA = split_csv(lineA, fieldsA, MAXCOLS);
        nB = split_csv(lineB, fieldsB, MAXCOLS);
        if (nA < 0 || nB < 0) {
            fprintf(stderr, "field_diff: row %ld has more than %d columns\n", row, MAXCOLS);
            fclose(fa); fclose(fb);
            return 2;
        }
        if (nA == 0 || nB == 0)
            continue; /* tolerate a trailing blank line at EOF */
        if (nA != ncols || nB != ncols) {
            fprintf(stderr,
                    "field_diff: row %ld is ragged ('%s' has %d fields, '%s' has "
                    "%d, header has %d)\n", row, pathA, nA, pathB, nB, ncols);
            fclose(fa); fclose(fb);
            return 2;
        }
        total_rows_a++;
        total_rows_b++;
        for (i = 0; i < ncols; i++) {
            if (strcmp(fieldsA[i], fieldsB[i]) != 0) {
                mismatch_count[i]++;
                if (!have_first) {
                    have_first = 1;
                    row_row = row;
                    row_col = i;
                    strncpy(row_a, fieldsA[i], sizeof(row_a) - 1); row_a[sizeof(row_a)-1] = '\0';
                    strncpy(row_b, fieldsB[i], sizeof(row_b) - 1); row_b[sizeof(row_b)-1] = '\0';
                }
            }
        }
    }
    /* count any remaining rows on the longer side (for the row-count
     * note) -- the line that triggered extra_a/extra_b is already in
     * lineA/lineB, so count it before fetching further. */
    if (extra_a) {
        if (split_csv(lineA, fieldsA, MAXCOLS) > 0) total_rows_a++;
        while (fgets(lineA, sizeof(lineA), fa))
            if (split_csv(lineA, fieldsA, MAXCOLS) > 0) total_rows_a++;
    }
    if (extra_b) {
        if (split_csv(lineB, fieldsB, MAXCOLS) > 0) total_rows_b++;
        while (fgets(lineB, sizeof(lineB), fb))
            if (split_csv(lineB, fieldsB, MAXCOLS) > 0) total_rows_b++;
    }

    fclose(fa);
    fclose(fb);

    if (!have_first && total_rows_a == total_rows_b) {
        printf("field_diff: %ld rows, %d columns, identical\n", total_rows_a, ncols);
        return 0;
    }

    if (have_first)
        printf("field_diff: first diff at row=%ld col=%s a=%s b=%s\n",
               row_row, names[row_col], row_a, row_b);
    if (total_rows_a != total_rows_b)
        printf("field_diff: row count mismatch -- %s has %ld rows, %s has %ld rows\n",
               pathA, total_rows_a, pathB, total_rows_b);

    printf("field_diff: per-column mismatch summary (over %ld compared rows):\n",
           row);
    for (i = 0; i < ncols; i++)
        if (mismatch_count[i] > 0)
            printf("  %-16s %ld/%ld mismatches\n", names[i], mismatch_count[i], row);

    return 1;
}
