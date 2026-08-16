/*
 * jagcd-chd-check -- inspect a CHD for Jaguar CD session metadata.
 *
 * Exit:
 *   0  loadable (CHSE present, or not Jaguar-shaped)
 *   1  missing CHSE on a Jaguar-shaped (all-audio, 2+ tracks) image
 *   2  CHSE present but one or more tracks use virtual (silent) pregaps
 *   3  not a CD CHD / open error
 *
 * Build:
 *   cc -O2 -std=c99 -I../../deps/libchdr/include \
 *      -I../../deps/libchdr/deps/lzma-25.01/include \
 *      -o jagcd-chd-check jagcd-chd-check.c ../../deps/libchdr/unity.c
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <libchdr/chd.h>
#include <libchdr/cdrom.h>

#ifndef CDROM_SESSION_METADATA_TAG
#define CDROM_SESSION_METADATA_TAG CHD_MAKE_TAG('C','H','S','E')
#endif
#ifndef CDROM_SESSION_METADATA_FORMAT
#define CDROM_SESSION_METADATA_FORMAT "SESSION:%d"
#endif

int main(int argc, char **argv)
{
   chd_file *chd;
   chd_error err;
   const chd_header *head;
   char metadata[256];
   int i, ntracks, saw_chse, all_audio, warn_virtual, max_sess;
   uint32_t sessionnum;

   if (argc != 2)
   {
      fprintf(stderr, "usage: %s <file.chd>\n", argv[0]);
      return 3;
   }

   err = chd_open(argv[1], CHD_OPEN_READ, NULL, &chd);
   if (err != CHDERR_NONE)
   {
      fprintf(stderr, "open failed: %s\n", chd_error_string(err));
      return 3;
   }

   head = chd_get_header(chd);
   if (!head || head->hunkbytes == 0 || (head->hunkbytes % CD_FRAME_SIZE) != 0)
   {
      fprintf(stderr, "not a CD CHD (hunkbytes=%u)\n",
              head ? head->hunkbytes : 0);
      chd_close(chd);
      return 3;
   }

   sessionnum = 1;
   ntracks = 0;
   saw_chse = 0;
   all_audio = 1;
   warn_virtual = 0;
   max_sess = 1;

   printf("file: %s\n", argv[1]);
   printf("hunkbytes: %u\n", head->hunkbytes);

   for (i = 0; i < 99; i++)
   {
      int tracknum, frames, pregap, postgap, sess;
      char type[16], subtype[16], pgtype[16], pgsub[16];

      type[0] = subtype[0] = pgtype[0] = pgsub[0] = '\0';
      tracknum = frames = pregap = postgap = 0;
      sess = 0;

      metadata[0] = '\0';
      if (chd_get_metadata(chd, CDROM_SESSION_METADATA_TAG, (uint32_t)i,
                           metadata, sizeof(metadata), NULL, NULL, NULL) == CHDERR_NONE)
      {
         sess = 1;
         if (sscanf(metadata, CDROM_SESSION_METADATA_FORMAT, &sess) == 1 && sess >= 1)
         {
            sessionnum = (uint32_t)sess;
            saw_chse = 1;
         }
      }

      metadata[0] = '\0';
      if (chd_get_metadata(chd, CDROM_TRACK_METADATA2_TAG, (uint32_t)i,
                           metadata, sizeof(metadata), NULL, NULL, NULL) == CHDERR_NONE)
      {
         if (sscanf(metadata, CDROM_TRACK_METADATA2_FORMAT,
                    &tracknum, type, subtype, &frames,
                    &pregap, pgtype, pgsub, &postgap) != 8)
            break;
      }
      else if (chd_get_metadata(chd, CDROM_TRACK_METADATA_TAG, (uint32_t)i,
                                metadata, sizeof(metadata), NULL, NULL, NULL) == CHDERR_NONE)
      {
         if (sscanf(metadata, CDROM_TRACK_METADATA_FORMAT,
                    &tracknum, type, subtype, &frames) != 4)
            break;
      }
      else
         break;

      if (strcmp(type, "AUDIO") != 0)
         all_audio = 0;
      if (pgtype[0] == 'V')
         warn_virtual = 1;
      if ((int)sessionnum > max_sess)
         max_sess = (int)sessionnum;

      printf("track %d session=%u type=%s frames=%d pregap=%d pgtype=%s\n",
             tracknum, sessionnum, type, frames, pregap,
             pgtype[0] ? pgtype : "-");
      ntracks++;
   }

   chd_close(chd);

   printf("tracks: %d\n", ntracks);
   printf("chse: %s\n", saw_chse ? "yes" : "no");
   printf("sessions_seen: %d\n", max_sess);
   printf("virtual_pregap: %s\n", warn_virtual ? "yes" : "no");

   if (ntracks == 0)
      return 3;
   if (!saw_chse && all_audio && ntracks >= 2)
   {
      fprintf(stderr, "REFUSE: Jaguar-shaped CHD with no CHSE. "
              "Reconvert with tools/jagcd (issue #322).\n");
      return 1;
   }
   if (warn_virtual)
   {
      fprintf(stderr, "WARN: virtual (silent) pregaps; HLE is fine, BIOS auth may fail.\n");
      return 2;
   }
   return 0;
}
