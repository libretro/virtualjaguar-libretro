/*
 * test_chd_unit.c -- libchdr-only CHD fixture tests (no core dlopen).
 *
 * Runs on every host that can compile unity.c, including Windows MSYS2
 * where `make test` is skipped. Gates:
 *   - synth_jagcd.chd opens, has CHSE, two AUDIO tracks, PREGAP 0
 *   - session-2 INDEX 01 (CHD frame 4) stores native ATARI magic
 *   - synth_jagcd_nosession.chd opens, has no CHSE, same track layout
 *
 * Build: make test/test_chd_unit
 */

#include "test_framework.h"

#include <stdio.h>
#include <string.h>
#include <libchdr/chd.h>
#include <libchdr/cdrom.h>

#ifndef CDROM_SESSION_METADATA_TAG
#define CDROM_SESSION_METADATA_TAG CHD_MAKE_TAG('C','H','S','E')
#endif
#ifndef CDROM_SESSION_METADATA_FORMAT
#define CDROM_SESSION_METADATA_FORMAT "SESSION:%d"
#endif

#define GOOD_CHD      "test/roms/synth_jagcd.chd"
#define NOSESSION_CHD "test/roms/synth_jagcd_nosession.chd"
#define BOOT_MAGIC    "ATARI APPROVED DATA HEADER ATRI "

struct track_row {
    int tracknum;
    int frames;
    int pregap;
    uint32_t session;
    char type[16];
    char pgtype[16];
};

static int read_toc(const char *path, struct track_row *rows, int max_rows,
                    int *saw_chse, int *ntracks)
{
    chd_file *chd;
    chd_error err;
    char metadata[256];
    uint32_t sessionnum;
    int i;

    *saw_chse = 0;
    *ntracks = 0;
    sessionnum = 1;

    err = chd_open(path, CHD_OPEN_READ, NULL, &chd);
    if (err != CHDERR_NONE)
        return (int)err;

    for (i = 0; i < max_rows; i++) {
        int tracknum, frames, pregap, postgap, sess;
        char type[16], subtype[16], pgtype[16], pgsub[16];

        type[0] = subtype[0] = pgtype[0] = pgsub[0] = '\0';
        tracknum = frames = pregap = postgap = 0;
        sess = 0;

        metadata[0] = '\0';
        if (chd_get_metadata(chd, CDROM_SESSION_METADATA_TAG, (uint32_t)i,
                             metadata, sizeof(metadata), NULL, NULL, NULL) == CHDERR_NONE) {
            sess = 1;
            if (sscanf(metadata, CDROM_SESSION_METADATA_FORMAT, &sess) == 1 && sess >= 1) {
                sessionnum = (uint32_t)sess;
                *saw_chse = 1;
            }
        }

        metadata[0] = '\0';
        if (chd_get_metadata(chd, CDROM_TRACK_METADATA2_TAG, (uint32_t)i,
                             metadata, sizeof(metadata), NULL, NULL, NULL) == CHDERR_NONE) {
            if (sscanf(metadata, CDROM_TRACK_METADATA2_FORMAT,
                       &tracknum, type, subtype, &frames,
                       &pregap, pgtype, pgsub, &postgap) != 8)
                break;
        } else if (chd_get_metadata(chd, CDROM_TRACK_METADATA_TAG, (uint32_t)i,
                                    metadata, sizeof(metadata), NULL, NULL, NULL) == CHDERR_NONE) {
            if (sscanf(metadata, CDROM_TRACK_METADATA_FORMAT,
                       &tracknum, type, subtype, &frames) != 4)
                break;
            pregap = 0;
        } else {
            break;
        }

        rows[i].tracknum = tracknum;
        rows[i].frames = frames;
        rows[i].pregap = pregap;
        rows[i].session = sessionnum;
        strncpy(rows[i].type, type, sizeof(rows[i].type) - 1);
        rows[i].type[sizeof(rows[i].type) - 1] = '\0';
        strncpy(rows[i].pgtype, pgtype, sizeof(rows[i].pgtype) - 1);
        rows[i].pgtype[sizeof(rows[i].pgtype) - 1] = '\0';
        *ntracks = i + 1;
    }

    chd_close(chd);
    return 0;
}

TEST(good_chd_toc)
{
    struct track_row rows[4];
    int saw_chse, ntracks;

    ASSERT_EQ(read_toc(GOOD_CHD, rows, 4, &saw_chse, &ntracks), 0);
    ASSERT_TRUE(saw_chse);
    ASSERT_EQ(ntracks, 2);
    ASSERT_EQ(rows[0].tracknum, 1);
    ASSERT_EQ(rows[0].session, 1);
    ASSERT_EQ(rows[0].frames, 4);
    ASSERT_EQ(rows[0].pregap, 0);
    ASSERT_TRUE(strcmp(rows[0].type, "AUDIO") == 0);
    ASSERT_EQ(rows[1].tracknum, 2);
    ASSERT_EQ(rows[1].session, 2);
    ASSERT_EQ(rows[1].frames, 8);
    ASSERT_EQ(rows[1].pregap, 0);
    ASSERT_TRUE(strcmp(rows[1].type, "AUDIO") == 0);
}

TEST(good_chd_boot_magic_in_session2)
{
    chd_file *chd;
    const chd_header *head;
    uint8_t *hunk;
    uint32_t frames_per_hunk;
    uint32_t offs;

    ASSERT_EQ((int)chd_open(GOOD_CHD, CHD_OPEN_READ, NULL, &chd), (int)CHDERR_NONE);
    head = chd_get_header(chd);
    ASSERT_TRUE(head != NULL);
    ASSERT_TRUE(head->hunkbytes != 0);
    ASSERT_TRUE((head->hunkbytes % CD_FRAME_SIZE) == 0);

    hunk = (uint8_t *)malloc(head->hunkbytes);
    ASSERT_TRUE(hunk != NULL);
    ASSERT_EQ((int)chd_read(chd, 0, hunk), (int)CHDERR_NONE);

    /* Session 1 is 4 frames, padded to 4. Session 2 INDEX 01 is CHD frame 4. */
    frames_per_hunk = head->hunkbytes / CD_FRAME_SIZE;
    ASSERT_TRUE(frames_per_hunk > 4);
    offs = 4u * CD_FRAME_SIZE + 0x42u;
    ASSERT_TRUE(offs + 32u <= head->hunkbytes);
    ASSERT_TRUE(memcmp(hunk + offs, BOOT_MAGIC, 32) == 0);

    free(hunk);
    chd_close(chd);
}

TEST(nosession_chd_has_no_chse)
{
    struct track_row rows[4];
    int saw_chse, ntracks;

    ASSERT_EQ(read_toc(NOSESSION_CHD, rows, 4, &saw_chse, &ntracks), 0);
    ASSERT_FALSE(saw_chse);
    ASSERT_EQ(ntracks, 2);
    ASSERT_EQ(rows[0].session, 1);
    ASSERT_EQ(rows[1].session, 1);
    ASSERT_EQ(rows[0].frames, 4);
    ASSERT_EQ(rows[1].frames, 8);
}

int main(int argc, char *argv[])
{
    (void)argc;
    (void)argv;

    TEST_INIT("CHD libchdr unit");
    RUN_TEST(good_chd_toc);
    RUN_TEST(good_chd_boot_magic_in_session2);
    RUN_TEST(nosession_chd_has_no_chse);
    return TEST_REPORT();
}
