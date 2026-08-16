//
// OS agnostic CDROM interface functions
//
// by James Hammons
// (C) 2010 Underground Software
//
// CD image (CUE/BIN) support for Jaguar CD emulation
//

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>

#include <boolean.h>
#include <compat/posix_string.h>
#ifndef _MSC_VER
#include <strings.h>  /* strcasecmp (POSIX; the compat shim above is MSVC-only) */
#endif
#include <streams/file_stream.h>
#include <streams/file_stream_transforms.h>
#include "cdintf.h"
#include "jaguar.h"
#include "log.h"

#include <libchdr/chd.h>
#include <libchdr/cdrom.h>

#ifndef CDROM_SESSION_METADATA_TAG
#define CDROM_SESSION_METADATA_TAG CHD_MAKE_TAG('C','H','S','E')
#endif
#ifndef CDROM_SESSION_METADATA_FORMAT
#define CDROM_SESSION_METADATA_FORMAT "SESSION:%d"
#endif

// CDI (DiscJuggler) format support
static RFILE *cdi_file = NULL;
static bool ParseCDI(const char *cdiPath);

/* CHD (MAME compressed hunks) — libchdr, opened through libretro VFS. */
static chd_file *chd = NULL;
static RFILE *chd_rfile = NULL;
static uint8_t *chd_hunk = NULL;
static uint32_t chd_hunkbytes = 0;
static int32_t chd_hunknum = -1;
/* First CHD frame of each track's stored data (includes 4-frame padding
 * of earlier tracks; does NOT include virtual pregaps). */
static uint32_t chd_frame0[CDINTF_MAX_TRACKS];
static uint8_t chd_virtual_pregap[CDINTF_MAX_TRACKS];
static bool ParseCHD(const char *chdPath);
static void CDIntfCloseCHD(void);

/* Multi-file CUE sector reads used to rfopen/rfseek/rfread/rfclose the
 * track's BIN on EVERY 2352-byte sector (measured 18.3 us vs 0.4 us per
 * sector on APFS; the gap is syscall latency, so it is far worse on SD
 * cards and network mounts).  Cache one open handle and the resolved path
 * it serves, keyed by path rather than by track pointer: ParseCueSheet
 * writes binFilePath for every track, including single-file CUEs, where
 * all tracks share one file, so keying on the track pointer closed and
 * reopened the same file on every alternation between two tracks in it
 * (cdrom.c keeps two independent read cursors -- ssiBlock audio and block
 * data -- so that alternation is routine, not a corner case).  Keying on
 * the path instead means same-file track switches are cache hits.  A
 * handle shared across tracks is safe to reuse because every read below
 * does an absolute rfseek() computed from the CURRENT track's startLBA
 * and fileOffset before reading, so no stale file position can leak from
 * one track to the next.
 * CDIntfCloseImage owns the teardown: it is called both from
 * retro_unload_game (normal per-content close, and the iOS no-dlclose
 * static-reset path) and as the first statement of CDIntfOpenImage, so no
 * handle from a previous disc can ever survive into a new one's parse. */
static RFILE *track_file = NULL;
static char track_file_path[sizeof(((struct CDIntfTrack *)0)->binFilePath)];

#ifndef strncasecmp
static int cdintf_strncasecmp(const char *a, const char *b, size_t n)
{
   size_t i;
   for (i = 0; i < n && a[i] && b[i]; i++)
   {
      int ca = (a[i] >= 'A' && a[i] <= 'Z') ? a[i] + 32 : a[i];
      int cb = (b[i] >= 'A' && b[i] <= 'Z') ? b[i] + 32 : b[i];
      if (ca != cb)
         return ca - cb;
   }
   if (i < n)
      return (unsigned char)a[i] - (unsigned char)b[i];
   return 0;
}
#define strncasecmp cdintf_strncasecmp
#endif

// Private function prototypes
static bool ParseCueSheet(const char *cuePath);
static void CDIntfFinalizeSessions(void);
static void MSFFromLBA(uint32_t lba, uint8_t *m, uint8_t *s, uint8_t *f);
static uint32_t LBAFromMSF(uint8_t m, uint8_t s, uint8_t f);
static char *TrimWhitespace(char *str);
static bool GetDirectoryFromPath(const char *path, char *dir, size_t dirSize);

// The global disc state
static struct CDIntfDisc disc;

// Tracks whether the last CDIntfReadBlock() hit a virtual-pregap gap.
// Used by cdrom.c to correlate pregap-auth reads with the BIOS's subsequent
// STOP command so we can identify the auth-fail branch PC.
static bool lastReadVirtualPregap = false;
static uint32_t lastVirtualPregapLBA = 0;

bool CDIntfLastReadWasVirtualPregap(void)
{
   return lastReadVirtualPregap;
}

void CDIntfClearLastReadVirtualPregap(void)
{
   lastReadVirtualPregap = false;
}

uint32_t CDIntfLastVirtualPregapLBA(void)
{
   return lastVirtualPregapLBA;
}

// Helper: convert LBA to MSF
static void MSFFromLBA(uint32_t lba, uint8_t *m, uint8_t *s, uint8_t *f)
{
   *f = lba % 75;
   *s = (lba / 75) % 60;
   *m = lba / (75 * 60);
}

// Helper: convert MSF to LBA
static uint32_t LBAFromMSF(uint8_t m, uint8_t s, uint8_t f)
{
   return ((uint32_t)m * 60 + s) * 75 + f;
}

// Helper: trim leading/trailing whitespace
static char *TrimWhitespace(char *str)
{
   char *end;
   while (*str && isspace((unsigned char)*str))
      str++;
   if (*str == '\0')
      return str;
   end = str + strlen(str) - 1;
   while (end > str && isspace((unsigned char)*end))
      end--;
   end[1] = '\0';
   return str;
}

// Helper: extract directory part of a path
static bool GetDirectoryFromPath(const char *path, char *dir, size_t dirSize)
{
   const char *lastSlash = strrchr(path, '/');
   const char *lastBackslash = strrchr(path, '\\');
   const char *sep;

   if (lastBackslash && (!lastSlash || lastBackslash > lastSlash))
      sep = lastBackslash;
   else
      sep = lastSlash;

   if (sep)
   {
      size_t len = (sep - path) + 1;
      if (len >= dirSize)
         len = dirSize - 1;
      memcpy(dir, path, len);
      dir[len] = '\0';
      return true;
   }

   dir[0] = '\0';
   return false;
}

static void CDIntfFinalizeSessions(void)
{
   int i;
   uint32_t sess1Min = 99, sess1Max = 0;
   uint32_t sess2Min = 99, sess2Max = 0;

   disc.numSessions = 1;

   for (i = 0; i < (int)disc.numTracks; i++)
   {
      uint32_t trackNum = disc.tracks[i].number;
      uint32_t sess = disc.tracks[i].session;

      if (sess == 1)
      {
         if (trackNum < sess1Min) sess1Min = trackNum;
         if (trackNum > sess1Max) sess1Max = trackNum;
      }
      else if (sess == 2)
      {
         disc.numSessions = 2;
         if (trackNum < sess2Min) sess2Min = trackNum;
         if (trackNum > sess2Max) sess2Max = trackNum;
      }
   }

   disc.sessions[0].number = 1;
   disc.sessions[0].firstTrack = (sess1Min <= CDINTF_MAX_TRACKS) ? sess1Min : 1;
   disc.sessions[0].lastTrack = (sess1Max > 0) ? sess1Max : 1;

   if (disc.numSessions >= 2 && sess2Min <= CDINTF_MAX_TRACKS)
   {
      uint32_t leadOut = disc.tracks[sess2Min - 1].startLBA;
      disc.sessions[0].leadOutLBA = leadOut;
      MSFFromLBA(leadOut, &disc.sessions[0].leadOutM,
                 &disc.sessions[0].leadOutS, &disc.sessions[0].leadOutF);
   }
   else
   {
      uint32_t lastIdx = disc.sessions[0].lastTrack - 1;
      uint32_t leadOut = disc.tracks[lastIdx].startLBA + disc.tracks[lastIdx].lengthLBA;
      disc.sessions[0].leadOutLBA = leadOut;
      MSFFromLBA(leadOut, &disc.sessions[0].leadOutM,
                 &disc.sessions[0].leadOutS, &disc.sessions[0].leadOutF);
   }

   if (disc.numSessions >= 2)
   {
      uint32_t lastIdx, leadOut;
      disc.sessions[1].number = 2;
      disc.sessions[1].firstTrack = sess2Min;
      disc.sessions[1].lastTrack = sess2Max;

      lastIdx = sess2Max - 1;
      leadOut = disc.tracks[lastIdx].startLBA + disc.tracks[lastIdx].lengthLBA;
      disc.sessions[1].leadOutLBA = leadOut;
      MSFFromLBA(leadOut, &disc.sessions[1].leadOutM,
                 &disc.sessions[1].leadOutS, &disc.sessions[1].leadOutF);
   }
}

static uint64_t chd_vfs_fsize(void *argp)
{
   int64_t sz;
   sz = filestream_get_size((RFILE *)argp);
   if (sz < 0)
      return (uint64_t)-1;
   return (uint64_t)sz;
}

static size_t chd_vfs_fread(void *ptr, size_t size, size_t nmemb, void *argp)
{
   int64_t want;
   int64_t got;
   if (size == 0 || nmemb == 0)
      return 0;
   want = (int64_t)size * (int64_t)nmemb;
   got = filestream_read((RFILE *)argp, ptr, want);
   if (got < 0)
      return 0;
   return (size_t)(got / (int64_t)size);
}

static int chd_vfs_fclose(void *argp)
{
   /* We own the RFILE; chd_close must not fclose it (non-owner). */
   (void)argp;
   return 0;
}

static int chd_vfs_fseek(void *argp, int64_t offset, int whence)
{
   int pos;
   if (whence == SEEK_SET)
      pos = RETRO_VFS_SEEK_POSITION_START;
   else if (whence == SEEK_CUR)
      pos = RETRO_VFS_SEEK_POSITION_CURRENT;
   else if (whence == SEEK_END)
      pos = RETRO_VFS_SEEK_POSITION_END;
   else
      return -1;
   if (filestream_seek((RFILE *)argp, offset, pos) < 0)
      return -1;
   return 0;
}

static const core_file_callbacks chd_vfs_cb = {
   chd_vfs_fsize,
   chd_vfs_fread,
   chd_vfs_fclose,
   chd_vfs_fseek
};

static void CDIntfCloseCHD(void)
{
   if (chd)
   {
      chd_close(chd);
      chd = NULL;
   }
   if (chd_rfile)
   {
      rfclose(chd_rfile);
      chd_rfile = NULL;
   }
   if (chd_hunk)
   {
      free(chd_hunk);
      chd_hunk = NULL;
   }
   chd_hunkbytes = 0;
   chd_hunknum = -1;
   memset(chd_frame0, 0, sizeof(chd_frame0));
   memset(chd_virtual_pregap, 0, sizeof(chd_virtual_pregap));
}

static bool ParseCHD(const char *chdPath)
{
   const chd_header *head;
   chd_error err;
   char metadata[256];
   int i;
   uint32_t sessionnum;
   uint32_t discLBA;
   uint32_t chdFrames;
   int prevSession;
   bool saw_chse;
   bool all_audio;
   bool warn_virtual;
   const uint32_t INTER_SESSION_GAP = 11400;

   CDIntfCloseCHD();
   memset(&disc, 0, sizeof(disc));

   /* Prefer libchdr's own path open (real FILE* inside unity.c).  This TU
    * remaps FILE to RFILE via file_stream_transforms.h, so we must not
    * pass our RFILE to chd_open_file().  Android content URIs fail that
    * path and succeed through VFS callbacks below. */
   err = chd_open(chdPath, CHD_OPEN_READ, NULL, &chd);
   if (err != CHDERR_NONE)
   {
      chd_rfile = rfopen(chdPath, "rb");
      if (!chd_rfile)
      {
         LOG_ERR("[CD-CHD] cannot open '%s' (%s)\n",
                 chdPath, chd_error_string(err));
         return false;
      }
      err = chd_open_core_file_callbacks(&chd_vfs_cb, chd_rfile,
                                        CHD_OPEN_READ, NULL, &chd);
      if (err != CHDERR_NONE)
      {
         LOG_ERR("[CD-CHD] chd_open failed: %s\n", chd_error_string(err));
         CDIntfCloseCHD();
         return false;
      }
   }

   head = chd_get_header(chd);
   if (!head || head->hunkbytes == 0 || (head->hunkbytes % CD_FRAME_SIZE) != 0)
   {
      LOG_ERR("[CD-CHD] not a CD CHD (hunk size %u)\n",
              head ? head->hunkbytes : 0);
      CDIntfCloseCHD();
      return false;
   }

   chd_hunkbytes = head->hunkbytes;
   chd_hunk = (uint8_t *)malloc(chd_hunkbytes);
   if (!chd_hunk)
   {
      CDIntfCloseCHD();
      return false;
   }
   chd_hunknum = -1;

   sessionnum = 1;
   discLBA = 0;
   chdFrames = 0;
   prevSession = 0;
   saw_chse = false;
   all_audio = true;
   warn_virtual = false;

   for (i = 0; i < CDINTF_MAX_TRACKS; i++)
   {
      int tracknum, frames, pregap, postgap;
      int sess;
      char type[16], subtype[16], pgtype[16], pgsub[16];
      uint32_t stored_pregap;
      uint32_t padded;

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
            sessionnum = sess;
            saw_chse = true;
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
         pregap = 0;
      }
      else
         break;

      if (tracknum != i + 1 || frames < 0 || pregap < 0)
      {
         LOG_ERR("[CD-CHD] bad track metadata at index %d\n", i);
         CDIntfCloseCHD();
         return false;
      }

      disc.tracks[i].number = (uint32_t)tracknum;
      disc.tracks[i].session = sessionnum;
      disc.tracks[i].sectorSize = 2352;
      snprintf(disc.tracks[i].binFilePath, sizeof(disc.tracks[i].binFilePath),
               "%s", chdPath);

      if (strcmp(type, "AUDIO") == 0)
         disc.tracks[i].type = CDINTF_TRACK_AUDIO;
      else if (strncmp(type, "MODE1", 5) == 0)
      {
         disc.tracks[i].type = CDINTF_TRACK_MODE1;
         all_audio = false;
      }
      else
      {
         disc.tracks[i].type = CDINTF_TRACK_MODE2;
         all_audio = false;
      }

      /* Virtual pregaps (PGTYPE starts with V) are silence, not stored.
       * Real pregaps live at the start of FRAMES. */
      stored_pregap = (uint32_t)pregap;
      if (pgtype[0] == 'V')
      {
         chd_virtual_pregap[i] = 1;
         warn_virtual = true;
      }
      else
      {
         chd_virtual_pregap[i] = 0;
         /* Pregap bytes are inside FRAMES; still use PREGAP for INDEX 01. */
      }

      if (prevSession != 0 && (int)sessionnum > prevSession)
      {
         /* MAME/CHD may encode the ~11400-sector session gap as a
          * per-track pregap. Only synthesize it when the metadata did
          * not already account for it. */
         if (stored_pregap < 10000)
            discLBA += INTER_SESSION_GAP;
      }
      prevSession = (int)sessionnum;

      disc.tracks[i].startLBA = discLBA;
      disc.tracks[i].dataLBA = discLBA + stored_pregap;
      if (chd_virtual_pregap[i])
         disc.tracks[i].lengthLBA = stored_pregap + (uint32_t)frames;
      else
         disc.tracks[i].lengthLBA = (uint32_t)frames;

      MSFFromLBA(disc.tracks[i].dataLBA,
                 &disc.tracks[i].startM,
                 &disc.tracks[i].startS,
                 &disc.tracks[i].startF);

      chd_frame0[i] = chdFrames;
      padded = (uint32_t)(((frames + CD_TRACK_PADDING - 1) / CD_TRACK_PADDING)
                          * CD_TRACK_PADDING);
      chdFrames += padded;
      discLBA += disc.tracks[i].lengthLBA;
      disc.numTracks = (uint32_t)(i + 1);
   }

   if (disc.numTracks == 0)
   {
      LOG_ERR("[CD-CHD] no tracks in '%s'\n", chdPath);
      CDIntfCloseCHD();
      return false;
   }

   /* Old chdman flattened Jaguar CD's two sessions and wrote no CHSE.
    * Those images cannot reconstruct the session-2 LBA; refuse them. */
   if (!saw_chse && all_audio && disc.numTracks >= 2)
   {
      LOG_ERR("[CD-CHD] this CHD has no session metadata (CHSE). "
              "It was almost certainly made with an old chdman that flattened "
              "Jaguar CD's two sessions. Reconvert from CUE/BIN or CDI with "
              "tools/jagcd (https://github.com/libretro/virtualjaguar-libretro/issues/322).\n");
      CDIntfCloseCHD();
      return false;
   }

   if (warn_virtual)
   {
      LOG_WRN("[CD-CHD] one or more tracks use virtual (silent) pregaps. "
               "HLE boot is fine; real-BIOS authentication may fail. "
               "CDI-class dumps preserve pregap audio; see docs/jagcd-chd.md.\n");
   }

   CDIntfFinalizeSessions();
   snprintf(disc.binPath, sizeof(disc.binPath), "%s", chdPath);
   disc.loaded = true;
   LOG_INF("[CD-CHD] loaded %u track(s), %u session(s) from %s\n",
           disc.numTracks, disc.numSessions, chdPath);
   return true;
}

static bool CDIntfReadBlockCHD(uint32_t sector, uint8_t *buffer)
{
   int i;
   struct CDIntfTrack *track;
   uint32_t rel;
   uint32_t frame;
   uint32_t frames_per_hunk;
   uint32_t hunknum;
   uint32_t offs;
   chd_error err;
   uint32_t b;
   uint8_t tmp;

   track = NULL;
   for (i = (int)disc.numTracks - 1; i >= 0; i--)
   {
      uint32_t tStart = disc.tracks[i].startLBA;
      uint32_t tEnd = tStart + disc.tracks[i].lengthLBA;
      if (sector >= tStart && sector < tEnd)
      {
         track = &disc.tracks[i];
         break;
      }
   }

   if (!track)
   {
      memset(buffer, 0, 2352);
      lastReadVirtualPregap = true;
      lastVirtualPregapLBA = sector;
      return true;
   }

   lastReadVirtualPregap = false;

   if (chd_virtual_pregap[track->number - 1] && sector < track->dataLBA)
   {
      memset(buffer, 0, 2352);
      return true;
   }

   if (chd_virtual_pregap[track->number - 1])
      rel = sector - track->dataLBA;
   else
      rel = sector - track->startLBA;

   frame = chd_frame0[track->number - 1] + rel;
   frames_per_hunk = chd_hunkbytes / CD_FRAME_SIZE;
   hunknum = frame / frames_per_hunk;
   offs = (frame % frames_per_hunk) * CD_FRAME_SIZE;

   if ((int32_t)hunknum != chd_hunknum)
   {
      err = chd_read(chd, hunknum, chd_hunk);
      if (err != CHDERR_NONE)
      {
         memset(buffer, 0, 2352);
         return false;
      }
      chd_hunknum = (int32_t)hunknum;
   }

   memcpy(buffer, chd_hunk + offs, 2352);
   /* CHD audio frames are host/LE PCM; Jaguar CUE/BINs are I2S
    * byte-swapped 16-bit words.  Restore BIN order so HLE extract and
    * the disc stream match ParseCueSheet. */
   if (track->type == CDINTF_TRACK_AUDIO)
   {
      for (b = 0; b < 2352; b += 2)
      {
         tmp = buffer[b];
         buffer[b] = buffer[b + 1];
         buffer[b + 1] = tmp;
      }
   }
   return true;
}

// Parse a CUE sheet and populate the disc structure
static bool ParseCueSheet(const char *cuePath)
{
   RFILE *cueFile;
   char line[1024];
   char dir[4096];
   char currentBinFile[4096] = {0};
   int currentTrack = -1;
   int currentSession = 1;
   uint32_t sectorSize = 2352;
   int trackCount = 0;
   int fileCount = 0;
   bool isMultiFile = false;

   memset(&disc, 0, sizeof(disc));
   GetDirectoryFromPath(cuePath, dir, sizeof(dir));

   cueFile = rfopen(cuePath, "r");
   if (!cueFile)
      return false;

   while (rfgets(line, sizeof(line), cueFile))
   {
      char *trimmed = TrimWhitespace(line);
      if (trimmed[0] == '\0' || trimmed[0] == ';')
         continue;

      // FILE "filename" BINARY
      if (strncasecmp(trimmed, "FILE", 4) == 0)
      {
         char *quote1 = strchr(trimmed, '"');
         char *quote2 = quote1 ? strchr(quote1 + 1, '"') : NULL;

         if (quote1 && quote2)
         {
            size_t nameLen = quote2 - quote1 - 1;
            char binName[4096];

            if (nameLen >= sizeof(binName))
               nameLen = sizeof(binName) - 1;
            memcpy(binName, quote1 + 1, nameLen);
            binName[nameLen] = '\0';

            // Build full path
            if (dir[0])
               snprintf(currentBinFile, sizeof(currentBinFile), "%s%s", dir, binName);
            else
               snprintf(currentBinFile, sizeof(currentBinFile), "%s", binName);

            // If we don't have a bin path set yet, set it as the primary
            if (!disc.binPath[0])
               snprintf(disc.binPath, sizeof(disc.binPath), "%s", currentBinFile);

            fileCount++;
            if (fileCount > 1)
               isMultiFile = true;
         }
      }
      // TRACK nn AUDIO|MODE1/2352|MODE2/2352
      else if (strncasecmp(trimmed, "TRACK", 5) == 0)
      {
         char *token = trimmed + 5;
         int trackNum;
         char typeStr[64] = {0};

         while (*token && isspace((unsigned char)*token)) token++;
         trackNum = atoi(token);

         while (*token && !isspace((unsigned char)*token)) token++;
         while (*token && isspace((unsigned char)*token)) token++;

         // Copy track type
         {
            int i = 0;
            while (*token && !isspace((unsigned char)*token) && i < 63)
               typeStr[i++] = *token++;
            typeStr[i] = '\0';
         }

         if (trackNum > 0 && trackNum <= CDINTF_MAX_TRACKS)
         {
            currentTrack = trackNum;
            trackCount++;

            disc.tracks[currentTrack - 1].number = trackNum;
            disc.tracks[currentTrack - 1].sectorSize = 2352;
            disc.tracks[currentTrack - 1].session = currentSession;

            // Store per-track BIN file path (needed for multi-file CUEs)
            snprintf(disc.tracks[currentTrack - 1].binFilePath,
                     sizeof(disc.tracks[currentTrack - 1].binFilePath),
                     "%s", currentBinFile);

            if (strcasecmp(typeStr, "AUDIO") == 0)
               disc.tracks[currentTrack - 1].type = CDINTF_TRACK_AUDIO;
            else if (strncasecmp(typeStr, "MODE1", 5) == 0)
            {
               disc.tracks[currentTrack - 1].type = CDINTF_TRACK_MODE1;
               if (strchr(typeStr, '/'))
                  disc.tracks[currentTrack - 1].sectorSize = atoi(strchr(typeStr, '/') + 1);
            }
            else if (strncasecmp(typeStr, "MODE2", 5) == 0)
            {
               disc.tracks[currentTrack - 1].type = CDINTF_TRACK_MODE2;
               if (strchr(typeStr, '/'))
                  disc.tracks[currentTrack - 1].sectorSize = atoi(strchr(typeStr, '/') + 1);
            }
            else
            {
               disc.tracks[currentTrack - 1].type = CDINTF_TRACK_AUDIO;
            }

            if (disc.tracks[currentTrack - 1].sectorSize == 0)
               disc.tracks[currentTrack - 1].sectorSize = 2352;
         }
      }
      // INDEX nn mm:ss:ff
      else if (strncasecmp(trimmed, "INDEX", 5) == 0 && currentTrack > 0)
      {
         char *token = trimmed + 5;
         int indexNum;
         int mm = 0, ss = 0, ff = 0;

         while (*token && isspace((unsigned char)*token)) token++;
         indexNum = atoi(token);

         while (*token && !isspace((unsigned char)*token)) token++;
         while (*token && isspace((unsigned char)*token)) token++;

         // Parse MSF
         if (sscanf(token, "%d:%d:%d", &mm, &ss, &ff) == 3)
         {
            if (indexNum == 1 || (indexNum == 0 && currentTrack == 1))
            {
               uint32_t lba = LBAFromMSF(mm, ss, ff);
               sectorSize = disc.tracks[currentTrack - 1].sectorSize;

               // For multi-file CUEs, startLBA is set later after computing
               // cumulative file sizes. Store the file-relative offset for now.
               disc.tracks[currentTrack - 1].startLBA = lba;
               disc.tracks[currentTrack - 1].startM = mm;
               disc.tracks[currentTrack - 1].startS = ss;
               disc.tracks[currentTrack - 1].startF = ff;
               // fileOffset = byte offset within this track's BIN file
               disc.tracks[currentTrack - 1].fileOffset = lba * sectorSize;
            }
         }
      }
      // REM SESSION nn (used by Redump and other CUE sheets for multisession)
      else if (strncasecmp(trimmed, "REM", 3) == 0)
      {
         char *token = trimmed + 3;
         while (*token && isspace((unsigned char)*token)) token++;

         if (strncasecmp(token, "SESSION", 7) == 0)
         {
            token += 7;
            while (*token && isspace((unsigned char)*token)) token++;
            currentSession = atoi(token);
            if (currentSession < 1) currentSession = 1;
            if (currentSession > CDINTF_MAX_SESSIONS) currentSession = CDINTF_MAX_SESSIONS;
         }
      }
   }

   rfclose(cueFile);

   disc.numTracks = trackCount;

   // For multi-file CUEs: calculate disc-absolute LBAs from file sizes.
   // Each FILE has its own BIN, so INDEX offsets are file-relative. We need
   // to accumulate the sizes of all preceding BIN files to get disc positions.
   //
   // Multi-session discs (Jaguar CD): the second session does not start
   // immediately after session 1 on a real disc — there is a session boundary
   // gap (session 1 lead-out + run-out + session 2 lead-in). MAME/CHD encodes
   // this as a per-track pregap on the first track of the new session, with
   // a typical value of ~11400 sectors. We apply the same constant here so
   // the TOC reports the correct session-2 start LBA. The pregap data itself
   // is not stored in redump-style BIN dumps; reads landing in the gap return
   // silence (the BIOS's pregap-audio auth still requires a format that
   // preserves that data, e.g. CDI).
   if (isMultiFile)
   {
      const uint32_t INTER_SESSION_GAP = 11400;
      uint32_t discLBA = 0;
      int prevSession = 0;
      int i;

      for (i = 0; i < (int)disc.numTracks; i++)
      {
         RFILE *bf;
         uint32_t fileSectors;
         uint32_t fileRelativeLBA = disc.tracks[i].startLBA; // INDEX 01 offset in file

         // Insert inter-session gap when crossing into a new session (after session 1)
         if (prevSession != 0 && (int)disc.tracks[i].session > prevSession)
            discLBA += INTER_SESSION_GAP;
         prevSession = (int)disc.tracks[i].session;

         // startLBA = beginning of this track's file on disc (includes pregap)
         disc.tracks[i].startLBA = discLBA;
         // dataLBA = INDEX 01 position on disc (used for TOC MSF)
         disc.tracks[i].dataLBA = discLBA + fileRelativeLBA;
         // fileOffset = 0 because startLBA maps to the file start
         disc.tracks[i].fileOffset = 0;

         // Get the BIN file size to determine total sectors
         bf = rfopen(disc.tracks[i].binFilePath, "rb");
         if (bf)
         {
            int64_t fsize;
            rfseek(bf, 0, SEEK_END);
            fsize = rftell(bf);
            rfclose(bf);
            fileSectors = (uint32_t)(fsize / disc.tracks[i].sectorSize);
         }
         else
            fileSectors = 0;

         disc.tracks[i].lengthLBA = fileSectors;

         // MSF reflects the INDEX 01 (data start) position for TOC
         MSFFromLBA(disc.tracks[i].dataLBA,
                    &disc.tracks[i].startM,
                    &disc.tracks[i].startS,
                    &disc.tracks[i].startF);

         // Advance disc LBA by the full BIN file size
         discLBA += fileSectors;
      }
   }
   else
   {
      // Single-file CUE: original logic — LBAs from INDEX are already disc-absolute
      int i;
      int64_t binFileSize = 0;
      RFILE *bf = rfopen(disc.binPath, "rb");
      if (bf)
      {
         rfseek(bf, 0, SEEK_END);
         binFileSize = rftell(bf);
         rfclose(bf);
      }

      for (i = 0; i < (int)disc.numTracks; i++)
      {
         // For single-file CUE, dataLBA = startLBA (already absolute)
         disc.tracks[i].dataLBA = disc.tracks[i].startLBA;

         if (i + 1 < (int)disc.numTracks)
            disc.tracks[i].lengthLBA = disc.tracks[i + 1].startLBA - disc.tracks[i].startLBA;
         else if (binFileSize > 0 && disc.tracks[i].sectorSize > 0)
         {
            uint32_t totalSectors = (uint32_t)(binFileSize / disc.tracks[i].sectorSize);
            disc.tracks[i].lengthLBA = (disc.tracks[i].startLBA < totalSectors)
                                        ? totalSectors - disc.tracks[i].startLBA : 0;
         }
      }
   }

   CDIntfFinalizeSessions();

   {
      int i;
      for (i = 0; i < (int)disc.numTracks; i++)
      {
         if (disc.tracks[i].session >= 2 || i >= (int)disc.numTracks - 5)
            LOG_DBG("[CD-LAYOUT] track %2u sess=%u startLBA=%u dataLBA=%u "
                    "len=%u MSF=%02u:%02u:%02u BIN=%s\n",
                    disc.tracks[i].number, disc.tracks[i].session,
                    disc.tracks[i].startLBA, disc.tracks[i].dataLBA,
                    disc.tracks[i].lengthLBA,
                    disc.tracks[i].startM, disc.tracks[i].startS, disc.tracks[i].startF,
                    disc.tracks[i].binFilePath[0] ? "yes" : "no");
      }
   }

   disc.loaded = true;
   return true;
}

// ---------------------------------------------------------------------------
// ISO parser
//
// Plain ISO files are single-track Mode1 data dumps with a fixed 2048-byte
// sector size and no metadata (no audio session, no pregap, no cue sheet).
//
// Jaguar CD games shipped with a session 1 audio program and session 2 game
// data — neither is preserved in a Mode1 ISO. So booting a Jaguar game from
// .iso is fundamentally degraded:
//   - CDIntfExtractBootStub() requires numSessions >= 2 and will return
//     false here, so the HLE boot path will fail cleanly rather than
//     executing random RAM.
//   - The real-BIOS path will fail authentication for the same reason.
//
// What we *can* do is load the ISO as a single-session, single-track disc
// so reads succeed for the data area. That at least keeps `retro_load_game`
// honest (no false-positive PC-OOB) and lets future tooling read ISO data.
// ---------------------------------------------------------------------------
// CDI (DiscJuggler) parser
//
// Reference: DreamShell modules/isofs/cdi.c. The trailer at end-of-file gives
// version + offset to the header table (V3.5 stores offset-from-end, V2/V3
// stores absolute offset). The header table contains per-session, per-track
// metadata including absolute disc start_lba — exactly what Jaguar CD auth
// needs since pregap data is preserved inline.
// ---------------------------------------------------------------------------
#define CDI_V2_ID  0x80000004
#define CDI_V3_ID  0x80000005
#define CDI_V35_ID 0x80000006

static const uint8_t cdi_track_start_marker[20] = {
   0x00,0x00,0x01,0x00,0x00,0x00,0xFF,0xFF,0xFF,0xFF,
   0x00,0x00,0x01,0x00,0x00,0x00,0xFF,0xFF,0xFF,0xFF
};

static uint32_t CDISectorSizeFromCode(uint32_t mode, uint32_t code)
{
   switch (mode)
   {
      case 0: return (code == 2) ? 2352 : 0;            // Audio
      case 1: return (code == 0) ? 2048 : 0;            // Mode1
      case 2:
         if (code == 0) return 2048;
         if (code == 1) return 2336;
         return 0;
      default: return 0;
   }
}

/* "ATARI APPROVED DATA HEADER ATRI " with each 16-bit pair byte-swapped,
 * i.e. how it appears raw in the image.  Matched in full: a 16-byte prefix
 * collides far too easily inside a 128 KB scan window, and a false match
 * here would shift *every* track. */
static const char cdi_needle[32] = "TARA IPARPVODED TA AEHDAREA RT I";

/* The constant 98-byte prefix every Jaguar boot track begins with, in raw
 * file byte order (word-swapped): 2 zero bytes, the sync preamble ("ATRI"
 * repeated 16 times, stored "TAIR"), then the 32-byte magic above.  The
 * title-specific load address / length / code follow at +0x62 (logical).
 * Byte-identical across every reference rip in the corpus (World Tour
 * Racing, Iron Soldier 2, Vid Grid, Myst demo), as the BIOS demands. */
#define CDI_CANON_LEN 98
static const uint8_t cdi_canon_prefix[CDI_CANON_LEN] = {
   0x00, 0x00,
   'T','A','I','R','T','A','I','R','T','A','I','R','T','A','I','R',
   'T','A','I','R','T','A','I','R','T','A','I','R','T','A','I','R',
   'T','A','I','R','T','A','I','R','T','A','I','R','T','A','I','R',
   'T','A','I','R','T','A','I','R','T','A','I','R','T','A','I','R',
   'T','A','R','A',' ','I','P','A','R','P','V','O','D','E','D',' ',
   'T','A',' ','A','E','H','D','A','R','E','A',' ','R','T',' ','I'
};

/* Byte offset of the descriptor-declared user-data start (INDEX 01) of a
 * track within the image file. */
static int64_t CDITrackDataFileOffset(const struct CDIntfTrack *t)
{
   int64_t off = (int64_t)t->fileOffset;
   if (t->dataLBA > t->startLBA)
      off += (int64_t)(t->dataLBA - t->startLBA) * (int64_t)t->sectorSize;
   return off;
}

/* Measure the undescribed leading data offset of a CDI rip (see the call
 * site at the end of ParseCDI) and fold it into every track's fileOffset.
 *
 * The Jaguar boot header is a fixed 32-byte signature that must sit at
 * +0x42 of the first session-2 track's user data, stored word-swapped
 * (I2S order).  That makes it a self-validating landmark: find it, and the
 * distance from where the descriptors said the payload begins IS the
 * offset.  No external reference and no per-title table needed.
 *
 * The search is bounded to +/-64 KB.  Observed shifts are -48..+4360; a
 * 32-byte ASCII signature inside that window is not something audio data
 * produces by accident.  No landmark (or a zero delta) leaves the tracks
 * exactly as parsed.
 *
 * Two guards keep a decoy from shifting the disc (issue #230):
 *   - every candidate must be preceded by the constant preamble
 *     (cdi_canon_prefix), which a mid-data second copy of the header also
 *     carries but random payload does not;
 *   - a POSITIVE delta additionally requires the gap between the declared
 *     data start and the claimed one to be all zeros -- a true forward
 *     slide leaves slid pregap silence there, while a second header copy
 *     deeper in the track leaves real payload, which rejects it.
 * Candidates are tried nearest-first, since the true landmark is rip
 * jitter (tens of bytes.. a few KB) and decoys sit sectors away.
 *
 * V2 images never reach this function: their damage is per-track, not a
 * global slide (measured boot-track displacements differ from their other
 * tracks' by thousands of bytes), so ParseCDI routes them to
 * CDIRepairV2BootTrack below instead. */
static void CDIDetectGlobalDataOffset(void)
{
   const int32_t  WINDOW  = 65536;
   uint8_t       *buf;
   int64_t        base = 0, start, got;
   uint32_t       i, s2 = 0;
   bool           found  = false;
   int64_t        expect = 0;
   int64_t        bestAbs = 0;

   if (!cdi_file || disc.numTracks == 0)
      return;

   for (i = 0; i < disc.numTracks; i++)
   {
      if (disc.tracks[i].session == 2)
         { s2 = i; found = true; break; }
   }
   if (!found)
      return;
   found = false;

   expect = CDITrackDataFileOffset(&disc.tracks[s2]);

   start = expect - WINDOW;
   if (start < 0)
      start = 0;

   buf = (uint8_t *)malloc((size_t)WINDOW * 2);
   if (!buf)
      return;

   rfseek(cdi_file, start, SEEK_SET);
   got = rfread(buf, 1, (size_t)WINDOW * 2, cdi_file);
   if (got <= 0)
   {
      /* rfread reports errors as a negative count; casting that straight to an
       * unsigned length would send the scan off the end of buf. */
      free(buf);
      return;
   }

   /* The header begins on an even boundary (word-swapped pairs). */
   for (i = 0; (int64_t)i + (int64_t)sizeof(cdi_needle) <= got; i += 2)
   {
      int64_t contentStart, delta, gap;

      if (memcmp(buf + i, cdi_needle, sizeof(cdi_needle)) != 0)
         continue;

      contentStart = start + (int64_t)i - 0x42;
      delta = contentStart - expect;

      /* Preamble validation: the 0x40 bytes before the magic must be the
       * sync preamble ("TAIR" x 16).  The 2 bytes before THAT are not
       * checked -- they are zero on most discs but e.g. Primal Rage
       * carries $72D7 there, so demanding zeros rejects real landmarks. */
      if ((int64_t)i < 0x42)
         continue;
      if (memcmp(buf + i - 0x40, cdi_canon_prefix + 2, 0x40) != 0)
         continue;

      /* Positive slide: the vacated gap must read as slid pregap silence. */
      if (delta > 0)
      {
         bool zeros = true;
         for (gap = expect - start; gap < contentStart - start; gap++)
         {
            if (gap >= 0 && gap < got && buf[gap] != 0)
               { zeros = false; break; }
         }
         if (!zeros)
            continue;
      }

      if (!found || ((delta < 0 ? -delta : delta) < bestAbs))
      {
         base = delta;
         bestAbs = (delta < 0 ? -delta : delta);
         found = true;
      }
   }
   free(buf);

   if (!found || base == 0)
      return;

   /* Guard: a shift larger than the window means the landmark was not what
    * we think it was. */
   if (base <= -(int64_t)WINDOW || base >= (int64_t)WINDOW)
      return;

   LOG_INF("[CD] CDI data offset: image data is shifted %+lld byte(s) from "
           "the descriptor layout; applying to all %u track(s)\n",
           (long long)base, disc.numTracks);

   /* Fold into every track, clamping per-track: a negative base can push an
    * early track's offset below the file start (track 1 sits at offset 0);
    * clamp that track to 0 instead of abandoning the whole correction --
    * the affected leading bytes are session-1 audio jitter, not data. */
   for (i = 0; i < disc.numTracks; i++)
   {
      int64_t no = (int64_t)disc.tracks[i].fileOffset + base;
      if (no < 0)
      {
         LOG_WRN("[CD] CDI data offset: track %u offset clamped to 0 "
                 "(would be %lld)\n", disc.tracks[i].number, (long long)no);
         no = 0;
      }
      disc.tracks[i].fileOffset = (uint32_t)no;
   }
}

/* Repair the boot track of a damaged CDI V2 rip.
 *
 * The four V2 images in circulation (ironsoldier2, mystdemo, vidgrid,
 * worldtourracing -- trailer version 0x80000004) were written by a ripper
 * that placed each track's content at a per-track byte displacement from
 * the descriptor layout, and lost the leading bytes of a track whenever
 * the displacement was negative (the missing bytes fell before the point
 * where the drive started returning data).  Measured against redump
 * references:
 *
 *   mystdemo        boot track slid  -1116, nothing lost (the content
 *                   moved into the stored pregap silence, header intact);
 *                   its DATA track sits at +1236 -- proof the damage is
 *                   per-track, so a global fileOffset shift is wrong here.
 *   worldtourracing boot track content = BIN[76:], i.e. the first 76
 *                   bytes (preamble + 10 magic bytes) are gone.
 *   ironsoldier2    boot track content = BIN[112:]: all 98 constant bytes
 *                   plus the 14 title-specific bytes after them (load
 *                   address, length, first 6 code bytes) are gone, and no
 *                   second copy of the header exists anywhere in the file.
 *   vidgrid         the first sectors of every boot-track copy are ripper
 *                   filler (0x0DC0E0AD / 0x075CEC71 patterns), destroying
 *                   the whole boot executable.  Unrecoverable.
 *
 * So the repair is scoped to the first session-2 track only (the one the
 * BIOS boots from), leaving the other tracks' jitter alone -- games
 * tolerate stream slop on data reads (a real drive cannot seek
 * byte-exact), but the boot path is strict about the header.
 *
 * Three recognized shapes:
 *   1. conformant: magic at +0x42 of the data start.  No-op.
 *   2. slide: the full header found nearby, preceded by the constant
 *      preamble.  Shift this track's reads by the measured delta.
 *   3. head-loss: the data start holds a proper suffix of the constant
 *      98-byte prefix (the ripper dropped the first Z bytes).  Shift
 *      reads by -Z and synthesize the constant prefix over the zeros
 *      that reads then expose at the data start.  Fully reconstructs any
 *      Z <= 92; larger losses ate title-specific bytes no heuristic can
 *      restore (ironsoldier2's Z=112), which is logged and left alone. */
static void CDIRepairV2BootTrack(void)
{
   const int32_t WINDOW = 65536;
   uint8_t      *buf;
   int64_t       dataOff, start, got, dzOff;
   uint32_t      i, s2 = 0, Z;
   bool          found = false;
   int64_t       pregapBytes;
   const uint8_t *dz;
   int64_t       dzLen;

   if (!cdi_file || disc.numTracks == 0)
      return;

   for (i = 0; i < disc.numTracks; i++)
   {
      if (disc.tracks[i].session == 2)
         { s2 = i; found = true; break; }
   }
   if (!found)
      return;
   found = false;

   dataOff = CDITrackDataFileOffset(&disc.tracks[s2]);
   pregapBytes = dataOff - (int64_t)disc.tracks[s2].fileOffset;

   start = dataOff - WINDOW;
   if (start < (int64_t)disc.tracks[s2].fileOffset)
      start = (int64_t)disc.tracks[s2].fileOffset;

   buf = (uint8_t *)malloc((size_t)WINDOW * 2);
   if (!buf)
      return;

   rfseek(cdi_file, start, SEEK_SET);
   got = rfread(buf, 1, (size_t)WINDOW * 2, cdi_file);
   dzOff = dataOff - start;              /* data start within buf */
   if (got < dzOff + CDI_CANON_LEN)
   {
      free(buf);
      return;
   }
   dz    = buf + dzOff;
   dzLen = got - dzOff;

   /* Shape 1: conformant. */
   if (memcmp(dz + 0x42, cdi_needle, sizeof(cdi_needle)) == 0)
   {
      free(buf);
      return;
   }

   /* Shape 2: intact header at a per-track displacement (mystdemo -1116).
    * Nearest validated match wins; positive candidates must have a silent
    * gap (see CDIDetectGlobalDataOffset for the decoy rationale). */
   {
      int64_t base = 0, bestAbs = 0;

      for (i = 0; (int64_t)i + (int64_t)sizeof(cdi_needle) <= got; i += 2)
      {
         int64_t contentStart, delta, gap;

         if (memcmp(buf + i, cdi_needle, sizeof(cdi_needle)) != 0)
            continue;
         if ((int64_t)i < 0x42)
            continue;
         /* Sync-preamble check only; the 2 bytes before it vary by title
          * (see the identical check in CDIDetectGlobalDataOffset). */
         if (memcmp(buf + i - 0x40, cdi_canon_prefix + 2, 0x40) != 0)
            continue;

         contentStart = start + (int64_t)i - 0x42;
         delta = contentStart - dataOff;

         if (delta < 0 && -delta > pregapBytes)
            continue;              /* would read before the track region */

         if (delta > 0)
         {
            bool zeros = true;
            for (gap = dzOff; gap < contentStart - start; gap++)
            {
               if (buf[gap] != 0)
                  { zeros = false; break; }
            }
            if (!zeros)
               continue;
         }

         if (!found || ((delta < 0 ? -delta : delta) < bestAbs))
         {
            base = delta;
            bestAbs = (delta < 0 ? -delta : delta);
            found = true;
         }
      }

      if (found && base != 0)
      {
         LOG_INF("[CD] CDI V2 repair: boot track %u content displaced "
                 "%+lld byte(s); shifting its reads to match\n",
                 disc.tracks[s2].number, (long long)base);
         disc.tracks[s2].dataShift = (int32_t)base;
         free(buf);
         return;
      }
      if (found)
      {
         free(buf);
         return;
      }
   }

   /* Shape 3: head-loss.  The data start holds cdi_canon_prefix[Z:] --
    * the ripper dropped the first Z bytes of the track.  The needle part
    * of the suffix pins Z uniquely (the preamble alone repeats every 4
    * bytes, but any candidate Z < true Z would demand needle bytes where
    * the file has none).  Demand at least 6 surviving suffix bytes; a
    * shorter match no longer identifies the header. */
   for (Z = 1; Z <= CDI_CANON_LEN - 6; Z++)
   {
      if ((int64_t)(CDI_CANON_LEN - Z) <= dzLen &&
          memcmp(dz, cdi_canon_prefix + Z, CDI_CANON_LEN - Z) == 0)
      {
         /* Shifting reads back by Z must stay inside this track's own
          * stored pregap.  The synthesized prefix overwrites those first
          * Z bytes anyway (Z <= CDI_CANON_LEN - 6), so a short pregap
          * would only ever expose bytes the overlay hides -- but refuse
          * rather than read outside the track's declared extent. */
         if ((int64_t)Z > pregapBytes)
         {
            LOG_ERR("[CD] CDI V2 repair: boot track %u lost %u byte(s) but "
                    "declares only %lld byte(s) of pregap; refusing to shift "
                    "reads outside the track\n",
                    disc.tracks[s2].number, Z, (long long)pregapBytes);
            break;
         }
         LOG_INF("[CD] CDI V2 repair: boot track %u lost its first %u "
                 "byte(s) (constant header prefix); shifting reads and "
                 "synthesizing the lost bytes\n",
                 disc.tracks[s2].number, Z);
         disc.tracks[s2].dataShift = -(int32_t)Z;
         disc.tracks[s2].synthBootHeader = true;
         free(buf);
         return;
      }
   }

   /* Unrecoverable.  Say precisely why so this stops being re-filed as an
    * unsupported format. */
   {
      int64_t r = 0;
      while (r < dzLen && dz[r] == 0)
         r++;
      if (r >= CDI_CANON_LEN)
         LOG_ERR("[CD] CDI V2 repair: boot track %u data starts with %lld "
                 "zero byte(s) and no recognizable boot header follows -- "
                 "the rip lost or corrupted the boot sectors themselves "
                 "(vidgrid-style damage); this image cannot boot\n",
                 disc.tracks[s2].number, (long long)r);
      else
         LOG_ERR("[CD] CDI V2 repair: boot track %u lost more than the "
                 "reconstructible %u-byte constant header prefix (load "
                 "address/length are title-specific and absent from the "
                 "file); this image cannot boot\n",
                 disc.tracks[s2].number, (unsigned)CDI_CANON_LEN);
   }
   free(buf);
}

static bool ParseCDI(const char *cdiPath)
{
   uint8_t trailer[8];
   uint32_t version, headerOffset;
   int64_t fileSize;
   uint16_t sessionCount;
   int s;
   uint32_t trackCount = 0;
   uint32_t cdiByteOffset = 0;  // Cumulative file-byte offset for next track's data
   uint32_t discLBA = 0;        // Tracked separately from start_lba (used as fallback)

   memset(&disc, 0, sizeof(disc));

   cdi_file = rfopen(cdiPath, "rb");
   if (!cdi_file)
      return false;

   rfseek(cdi_file, 0, SEEK_END);
   fileSize = rftell(cdi_file);
   if (fileSize < 8)
      goto fail;

   rfseek(cdi_file, fileSize - 8, SEEK_SET);
   if (rfread(trailer, 1, 8, cdi_file) != 8)
      goto fail;

   // Trailer is little-endian
   version      = (uint32_t)trailer[0] | ((uint32_t)trailer[1] << 8) |
                  ((uint32_t)trailer[2] << 16) | ((uint32_t)trailer[3] << 24);
   headerOffset = (uint32_t)trailer[4] | ((uint32_t)trailer[5] << 8) |
                  ((uint32_t)trailer[6] << 16) | ((uint32_t)trailer[7] << 24);

   if (version != CDI_V2_ID && version != CDI_V3_ID && version != CDI_V35_ID)
      goto fail;

   if (version == CDI_V35_ID)
      rfseek(cdi_file, fileSize - (int64_t)headerOffset, SEEK_SET);
   else
      rfseek(cdi_file, headerOffset, SEEK_SET);

   {
      uint8_t buf2[2];
      if (rfread(buf2, 1, 2, cdi_file) != 2)
         goto fail;
      sessionCount = (uint16_t)buf2[0] | ((uint16_t)buf2[1] << 8);
   }

   snprintf(disc.binPath, sizeof(disc.binPath), "%s", cdiPath);

   for (s = 0; s < sessionCount; s++)
   {
      uint16_t sessTrackCount;
      int t;
      uint8_t buf2[2];
      if (rfread(buf2, 1, 2, cdi_file) != 2)
         goto fail;
      sessTrackCount = (uint16_t)buf2[0] | ((uint16_t)buf2[1] << 8);

      for (t = 0; t < sessTrackCount; t++)
      {
         uint8_t newFmt[4], marker[20];
         uint32_t newFmtVal;
         uint8_t fnameLen;
         uint8_t trkData[256];  // holds the 0x57-byte packed track-data block
         uint32_t pregapLen, length, mode, startLba, totalLength, sectorCode;
         uint32_t sectorSize;

         if (trackCount >= CDINTF_MAX_TRACKS)
            goto fail;

         if (rfread(newFmt, 1, 4, cdi_file) != 4)
            goto fail;
         newFmtVal = (uint32_t)newFmt[0] | ((uint32_t)newFmt[1] << 8) |
                     ((uint32_t)newFmt[2] << 16) | ((uint32_t)newFmt[3] << 24);
         if (newFmtVal != 0)
            rfseek(cdi_file, 8, SEEK_CUR);     // skip extras (DJ 3.00.780+)

         if (rfread(marker, 1, 20, cdi_file) != 20)
            goto fail;
         if (memcmp(marker, cdi_track_start_marker, 20) != 0)
            goto fail;

         rfseek(cdi_file, 4, SEEK_CUR);
         if (rfread(&fnameLen, 1, 1, cdi_file) != 1)
            goto fail;
         rfseek(cdi_file, fnameLen, SEEK_CUR);
         rfseek(cdi_file, 19, SEEK_CUR);

         if (rfread(newFmt, 1, 4, cdi_file) != 4)
            goto fail;
         newFmtVal = (uint32_t)newFmt[0] | ((uint32_t)newFmt[1] << 8) |
                     ((uint32_t)newFmt[2] << 16) | ((uint32_t)newFmt[3] << 24);
         if (newFmtVal == 0x80000000)
            rfseek(cdi_file, 10, SEEK_CUR);    // DJ4 extra u64 + index count (u16, normally 2)
         else
            rfseek(cdi_file, 2, SEEK_CUR);     // index count (u16, normally 2)

         // Read the track-data block. We only need the documented fields;
         // the offsets within the block are fixed regardless of CDI version.
         // DreamShell's CDI_track_data is __attribute__((packed)):
         // sizeof = 4+4+6+4+4+4+4+4+4+0x10+4+1+0x1c = 0x57 bytes exactly.
         memset(trkData, 0, sizeof(trkData));
         if (rfread(trkData, 1, 0x57, cdi_file) != 0x57)
            goto fail;

         // Field offsets per DreamShell CDI_track_data (packed) layout,
         // cross-checked against cdirip and libmirage:
         //   +0x00 pregap_length (u32)
         //   +0x04 length (u32)
         //   +0x08 unknown (6 bytes)
         //   +0x0e mode (u32)
         //   +0x12 unknown (4 bytes)
         //   +0x16 session_idx (u32)
         //   +0x1a track_idx (u32)
         //   +0x1e start_lba (u32)
         //   +0x22 total_length (u32)
         //   +0x26 unknown (16 bytes)
         //   +0x36 sector_size (u32, code: 0=2048, 1=2336, 2=2352)
         //   +0x3a track CTL (u8), then 0x1c unknown bytes to +0x57
         #define LE32(p, o) ((uint32_t)(p)[(o)] | ((uint32_t)(p)[(o)+1] << 8) | \
                             ((uint32_t)(p)[(o)+2] << 16) | ((uint32_t)(p)[(o)+3] << 24))
         pregapLen   = LE32(trkData, 0x00);
         length      = LE32(trkData, 0x04);
         mode        = LE32(trkData, 0x0e);
         startLba    = LE32(trkData, 0x1e);
         totalLength = LE32(trkData, 0x22);
         sectorCode  = LE32(trkData, 0x36);
         #undef LE32

         sectorSize = CDISectorSizeFromCode(mode, sectorCode);
         if (sectorSize == 0)
            sectorSize = 2352;

         // Tail past CDI_track_data block (V2 stops here, others have a marker)
         if (version != CDI_V2_ID)
         {
            uint8_t extMarker[4];
            rfseek(cdi_file, 5, SEEK_CUR);
            if (rfread(extMarker, 1, 4, cdi_file) == 4)
            {
               uint32_t emv = (uint32_t)extMarker[0] | ((uint32_t)extMarker[1] << 8) |
                              ((uint32_t)extMarker[2] << 16) | ((uint32_t)extMarker[3] << 24);
               if (emv == 0xFFFFFFFF)
                  rfseek(cdi_file, 78, SEEK_CUR);
            }
         }

         // Populate track entry. start_lba is authoritative; if zero (rare),
         // fall back to running disc-LBA accumulator.
         disc.tracks[trackCount].number      = trackCount + 1;
         disc.tracks[trackCount].sectorSize  = sectorSize;
         disc.tracks[trackCount].startLBA    = (startLba != 0) ? startLba : discLBA;
         disc.tracks[trackCount].dataLBA     = disc.tracks[trackCount].startLBA + pregapLen;
         disc.tracks[trackCount].lengthLBA   = totalLength ? totalLength : (pregapLen + length);
         // CDI byte offset: pregap data sits at the start of this track's region in the file.
         disc.tracks[trackCount].fileOffset  = cdiByteOffset;
         disc.tracks[trackCount].session     = (uint32_t)(s + 1);
         disc.tracks[trackCount].type        = (mode == 0) ? CDINTF_TRACK_AUDIO :
                                                ((mode == 1) ? CDINTF_TRACK_MODE1 : CDINTF_TRACK_MODE2);
         MSFFromLBA(disc.tracks[trackCount].dataLBA,
                    &disc.tracks[trackCount].startM,
                    &disc.tracks[trackCount].startS,
                    &disc.tracks[trackCount].startF);

         cdiByteOffset += disc.tracks[trackCount].lengthLBA * sectorSize;
         discLBA = disc.tracks[trackCount].startLBA + disc.tracks[trackCount].lengthLBA;
         trackCount++;
      }

      // Per-session trailer
      rfseek(cdi_file, 12, SEEK_CUR);
      if (version != CDI_V2_ID)
         rfseek(cdi_file, 1, SEEK_CUR);
   }

   if (trackCount == 0)
      goto fail;

   disc.numTracks   = trackCount;
   disc.numSessions = (sessionCount > CDINTF_MAX_SESSIONS) ? CDINTF_MAX_SESSIONS : sessionCount;

   // Build session info
   {
      uint32_t sess1Min = 99, sess1Max = 0;
      uint32_t sess2Min = 99, sess2Max = 0;
      uint32_t i;

      for (i = 0; i < disc.numTracks; i++)
      {
         uint32_t tn = disc.tracks[i].number;
         uint32_t sess = disc.tracks[i].session;
         if (sess == 1) { if (tn < sess1Min) sess1Min = tn; if (tn > sess1Max) sess1Max = tn; }
         else if (sess == 2) { if (tn < sess2Min) sess2Min = tn; if (tn > sess2Max) sess2Max = tn; }
      }

      disc.sessions[0].number     = 1;
      disc.sessions[0].firstTrack = (sess1Min <= CDINTF_MAX_TRACKS) ? sess1Min : 1;
      disc.sessions[0].lastTrack  = (sess1Max > 0) ? sess1Max : 1;

      if (disc.numSessions >= 2 && sess2Min <= CDINTF_MAX_TRACKS)
      {
         uint32_t lastIdx, leadOut;
         disc.sessions[0].leadOutLBA = disc.tracks[sess2Min - 1].startLBA;
         MSFFromLBA(disc.sessions[0].leadOutLBA, &disc.sessions[0].leadOutM,
                    &disc.sessions[0].leadOutS, &disc.sessions[0].leadOutF);
         disc.sessions[1].number     = 2;
         disc.sessions[1].firstTrack = sess2Min;
         disc.sessions[1].lastTrack  = sess2Max;
         lastIdx = sess2Max - 1;
         leadOut = disc.tracks[lastIdx].startLBA + disc.tracks[lastIdx].lengthLBA;
         disc.sessions[1].leadOutLBA = leadOut;
         MSFFromLBA(leadOut, &disc.sessions[1].leadOutM,
                    &disc.sessions[1].leadOutS, &disc.sessions[1].leadOutF);
      }
      else
      {
         uint32_t lastIdx = disc.sessions[0].lastTrack - 1;
         uint32_t leadOut = disc.tracks[lastIdx].startLBA + disc.tracks[lastIdx].lengthLBA;
         disc.sessions[0].leadOutLBA = leadOut;
         MSFFromLBA(leadOut, &disc.sessions[0].leadOutM,
                    &disc.sessions[0].leadOutS, &disc.sessions[0].leadOutF);
      }
   }

   /* Widely-circulating CDI rips carry a leading byte offset the format does
    * not describe: the payload is slid forward by N bytes and N bytes are
    * lost off the tail, so every track's data sits N bytes past where the
    * descriptors put it.  The images are self-inconsistent -- summing
    * total_length*sector_size plus the header equals the file size exactly,
    * leaving no room for the shift -- but they are the rips most people have
    * (their hashes match the databases), so refusing them helps nobody.
    * cdirip mislocates them identically, so this is not a parse error on our
    * side; there is simply nothing in the image that states the offset.
    *
    * N is constant across every track of an image, so it is measured once
    * here and folded into all track offsets.  Doing it globally (rather than
    * only for the boot-stub read) is what keeps in-game sector reads aligned.
    * Conformant images measure 0 and are left untouched.
    *
    * V2 rips are damaged differently -- per-track displacements plus lost
    * leading bytes -- and get their own boot-track repair instead (see
    * CDIRepairV2BootTrack). */
   if (version == CDI_V2_ID)
      CDIRepairV2BootTrack();
   else
      CDIDetectGlobalDataOffset();

   disc.loaded = true;
   return true;

fail:
   if (cdi_file)
   {
      rfclose(cdi_file);
      cdi_file = NULL;
   }
   memset(&disc, 0, sizeof(disc));
   return false;
}

// Read a sector from a CDI file
static bool CDIntfReadBlockCDI(uint32_t sector, uint8_t *buffer)
{
   int i, trackIdx = -1;
   int64_t filePos;
   int64_t bytesRead;
   uint32_t sectorSize;

   if (!cdi_file)
      return false;

   for (i = (int)disc.numTracks - 1; i >= 0; i--)
   {
      uint32_t tStart = disc.tracks[i].startLBA;
      uint32_t tEnd = tStart + disc.tracks[i].lengthLBA;
      if (sector >= tStart && sector < tEnd)
      {
         trackIdx = i;
         break;
      }
   }

   if (trackIdx < 0)
   {
      memset(buffer, 0, 2352);
      lastReadVirtualPregap = true;
      lastVirtualPregapLBA = sector;
      return true;
   }

   lastReadVirtualPregap = false;
   sectorSize = disc.tracks[trackIdx].sectorSize;
   if (sectorSize == 0) sectorSize = 2352;

   filePos = (int64_t)disc.tracks[trackIdx].fileOffset
           + (int64_t)(sector - disc.tracks[trackIdx].startLBA) * sectorSize
           + (int64_t)disc.tracks[trackIdx].dataShift;
   if (filePos < 0)
      filePos = 0;

   rfseek(cdi_file, filePos, SEEK_SET);
   bytesRead = rfread(buffer, 1, 2352, cdi_file);
   if (bytesRead < 2352)
   {
      if (bytesRead > 0)
         memset(buffer + bytesRead, 0, 2352 - bytesRead);
      else
      {
         memset(buffer, 0, 2352);
         return false;
      }
   }

   /* CDI V2 head-loss repair: the first data sector of a repaired boot
    * track begins with zeros where the ripper lost the constant header
    * prefix (dataShift re-aligned everything after it).  Overlay the
    * prefix so the BIOS finds the header it streams for.  Idempotent:
    * the bytes past the lost run already equal the constant. */
   if (disc.tracks[trackIdx].synthBootHeader &&
       sector == disc.tracks[trackIdx].dataLBA)
      memcpy(buffer, cdi_canon_prefix, CDI_CANON_LEN);

   return true;
}

bool CDIntfOpenImage(const char *path)
{
   const char *ext;
   CDIntfCloseImage();

   ext = strrchr(path, '.');

   if (ext && strcasecmp(ext + 1, "cdi") == 0)
      return ParseCDI(path);

   if (ext && strcasecmp(ext + 1, "chd") == 0)
      return ParseCHD(path);

   /* Bare ISO is refused deliberately: a 2048-byte-sector image cannot
    * carry the multi-session layout a Jaguar CD requires (session 1 audio
    * warning, session 2 data-as-audio 2352-byte tracks, lead-in offsets),
    * so no retail title can ever boot from one.  Refuse loudly rather
    * than raise false hopes with a BIOS screen that goes nowhere. */
   if (ext && strcasecmp(ext + 1, "iso") == 0)
   {
      LOG_ERR("[CD-ISO] bare .iso images cannot represent a Jaguar CD "
              "(no session/track layout) -- use CUE/BIN or CDI\n");
      return false;
   }

   // CUE/BIN path
   if (!ParseCueSheet(path))
      return false;

   // For multi-file CUEs, each track opens its own BIN in CDIntfReadBlock.
   // For single-file CUEs, open the monolithic BIN here.
   if (disc.tracks[0].binFilePath[0] && disc.numTracks > 1 &&
       strcmp(disc.tracks[0].binFilePath, disc.tracks[1].binFilePath) != 0)
   {
      // Multi-file: no single BIN file to open
      disc.binFile = NULL;
      return true;
   }

   disc.binFile = rfopen(disc.binPath, "rb");
   if (!disc.binFile)
   {
      memset(&disc, 0, sizeof(disc));
      return false;
   }

   return true;
}

void CDIntfCloseImage(void)
{
   CDIntfCloseCHD();

   if (cdi_file)
   {
      rfclose(cdi_file);
      cdi_file = NULL;
   }

   if (disc.binFile)
   {
      rfclose((RFILE *)disc.binFile);
      disc.binFile = NULL;
   }

   if (track_file)
   {
      rfclose(track_file);
      track_file = NULL;
      track_file_path[0] = '\0';
   }
   memset(&disc, 0, sizeof(disc));
}

bool CDIntfIsImageLoaded(void)
{
   if (!disc.loaded)
      return false;
   if (chd)
      return true;
   if (cdi_file)
      return true;
   // Multi-file CUE: binFile is NULL, but tracks have their own file paths
   if (disc.tracks[0].binFilePath[0])
      return true;
   return disc.binFile != NULL;
}

bool CDIntfInit(void)
{
   return CDIntfIsImageLoaded();
}

void CDIntfDone(void)
{
   CDIntfCloseImage();
}

// Read a raw 2352-byte sector from the disc image
// sector is an absolute LBA (from the start of the disc)
bool CDIntfReadBlock(uint32_t sector, uint8_t *buffer)
{
   int i;
   int64_t filePos;
   int64_t bytesRead;
   struct CDIntfTrack *track = NULL;
   uint32_t sectorSize;

   if (!disc.loaded || !buffer)
      return false;

   if (cdi_file)
      return CDIntfReadBlockCDI(sector, buffer);

   if (chd)
      return CDIntfReadBlockCHD(sector, buffer);

   // Find which track contains this sector. A sector belongs to a track only
   // if it falls within [startLBA, startLBA + lengthLBA). Sectors in the
   // inter-session gap belong to no track and are returned as silence.
   for (i = (int)disc.numTracks - 1; i >= 0; i--)
   {
      uint32_t tStart = disc.tracks[i].startLBA;
      uint32_t tEnd = tStart + disc.tracks[i].lengthLBA;
      if (sector >= tStart && sector < tEnd)
      {
         track = &disc.tracks[i];
         break;
      }
   }

   if (!track)
   {
      // True inter-session gap. Return silence; tracks lookup will fall through.
      memset(buffer, 0, 2352);
      lastReadVirtualPregap = true;
      lastVirtualPregapLBA = sector;
      return true;
   }

   lastReadVirtualPregap = false;

   sectorSize = track->sectorSize;
   if (sectorSize == 0)
      sectorSize = 2352;

   // Multi-file CUE: each track has its own BIN file.
   // fileOffset = byte offset within the track's file where data starts (from INDEX 01).
   // Sector offset within the track is (sector - startLBA).
   if (track->binFilePath[0])
   {
      if (track_file && strcmp(track->binFilePath, track_file_path) != 0)
      {
         rfclose(track_file);
         track_file = NULL;
         track_file_path[0] = '\0';
      }
      if (!track_file)
      {
         track_file = rfopen(track->binFilePath, "rb");
         if (!track_file)
         {
            memset(buffer, 0, 2352);
            return false;
         }
         snprintf(track_file_path, sizeof(track_file_path), "%s",
                  track->binFilePath);
      }

      filePos = (int64_t)(sector - track->startLBA) * sectorSize + track->fileOffset;
      rfseek(track_file, filePos, SEEK_SET);
      bytesRead = rfread(buffer, 1, 2352, track_file);

      if (bytesRead < 2352)
      {
         if (bytesRead > 0)
            memset(buffer + bytesRead, 0, 2352 - bytesRead);
         else
         {
            /* Read failure: drop the cached handle so the next call
             * reopens fresh instead of retrying a dead stream. */
            rfclose(track_file);
            track_file = NULL;
            track_file_path[0] = '\0';
            memset(buffer, 0, 2352);
            return false;
         }
      }
      return true;
   }

   // Single-file CUE: all tracks in one BIN file.
   if (!disc.binFile)
      return false;

   filePos = (int64_t)(sector - track->startLBA) * sectorSize + track->fileOffset;
   rfseek((RFILE *)disc.binFile, filePos, SEEK_SET);
   bytesRead = rfread(buffer, 1, 2352, (RFILE *)disc.binFile);

   if (bytesRead < 2352)
   {
      if (bytesRead > 0)
         memset(buffer + bytesRead, 0, 2352 - bytesRead);
      else
      {
         memset(buffer, 0, 2352);
         return false;
      }
   }

   return true;
}

uint32_t CDIntfGetNumSessions(void)
{
   if (!disc.loaded)
      return 0;
   return disc.numSessions;
}

uint32_t CDIntfGetNumTracks(void)
{
   if (!disc.loaded)
      return 0;
   return disc.numTracks;
}

void CDIntfSelectDrive(uint32_t driveNum)
{
   // Not applicable for disc images
   (void)driveNum;
}

uint32_t CDIntfGetCurrentDrive(void)
{
   return 0;
}

const uint8_t *CDIntfGetDriveName(uint32_t driveNum)
{
   (void)driveNum;

   if (disc.loaded)
      return (const uint8_t *)"CD Image";

   return (const uint8_t *)"NONE";
}

// Returns true if the given disc-image LBA falls within a session 2 track.
// Jaguar CD game data is always in session 2 (the second session).
// All Jaguar CD tracks are typed as AUDIO in CUE sheets, so we can't use
// the track type — session membership is the correct discriminator.
bool CDIntfIsSession2Sector(uint32_t sector)
{
   int i;
   if (!disc.loaded || disc.numSessions < 2)
      return false;

   // Find which track contains this sector and check its session
   for (i = (int)disc.numTracks - 1; i >= 0; i--)
   {
      if (sector >= disc.tracks[i].startLBA)
         return disc.tracks[i].session == 2;
   }
   return false;
}

/* Q-channel subcode position lookup — see cdintf.h.  Track containment
 * uses the same [startLBA, startLBA + lengthLBA) rule as
 * CDIntfReadBlock() so the Q data always describes the sector actually
 * being streamed. */
bool CDIntfGetQPosition(uint32_t lba, uint32_t *trackNum, uint32_t *idx,
                        uint32_t *relLBA, bool *isData)
{
   int i;
   struct CDIntfTrack *track = NULL;

   if (!disc.loaded)
      return false;

   for (i = (int)disc.numTracks - 1; i >= 0; i--)
   {
      uint32_t tStart = disc.tracks[i].startLBA;
      uint32_t tEnd = tStart + disc.tracks[i].lengthLBA;
      if (lba >= tStart && lba < tEnd)
      {
         track = &disc.tracks[i];
         break;
      }
   }
   if (!track)
      return false;

   if (trackNum)
      *trackNum = track->number;
   /* Q CONTROL bit 2 (data track).  track->type alone is not enough:
    * Jaguar CD CUE sheets routinely mark the session-2 DATA track as
    * AUDIO (the game data is mastered inside an audio-type track), so
    * trusting the type would report a data track as audio -- and that
    * is exactly the bit the CD player's VLM uses as its mute gate.
    * Treat anything in session 2 as data regardless of declared type,
    * matching CDIntfIsSession2Sector's reasoning. */
   if (isData)
      *isData = (track->type != CDINTF_TRACK_AUDIO) || (track->session == 2);
   if (lba < track->dataLBA)
   {
      /* INDEX 00 pregap: relative time counts down to 0 at INDEX 01. */
      if (idx)
         *idx = 0;
      if (relLBA)
         *relLBA = track->dataLBA - lba;
   }
   else
   {
      if (idx)
         *idx = 1;
      if (relLBA)
         *relLBA = lba - track->dataLBA;
   }
   return true;
}

// Returns session info for use by cdrom.c
// Session numbering matches the DSA command operand (per MiSTer FPGA):
//   Session 0 → disc.sessions[0] (first session, typically audio)
//   Session 1 → disc.sessions[1] (second session, typically data)
// offset == 0 -> min track for session
// offset == 1 -> max track for session
// offset == 2/3/4 -> leadout min/sec/frame
uint8_t CDIntfGetSessionInfo(uint32_t session, uint32_t offset)
{
   if (!disc.loaded || session >= disc.numSessions)
      return 0xFF;

   switch (offset)
   {
      case 0:
         return (uint8_t)disc.sessions[session].firstTrack;
      case 1:
         return (uint8_t)disc.sessions[session].lastTrack;
      case 2:
      case 3:
      case 4:
      {
         // Convert disc-image LBA to absolute MSF (add 150-frame lead-in)
         uint32_t absLBA = disc.sessions[session].leadOutLBA + 150;
         uint8_t m, s, f;
         MSFFromLBA(absLBA, &m, &s, &f);
         if (offset == 2) return m;
         if (offset == 3) return s;
         return f;
      }
      default:
         return 0xFF;
   }
}

// Returns track info for use by cdrom.c
// offset: 0 = minutes, 1 = seconds, 2 = frames of track start position
// Returns absolute MSF (with standard 150-frame CD lead-in offset).
// CD-ROM TOCs always use absolute MSF: LBA 0 = MSF 00:02:00.
// Uses dataLBA (INDEX 01 position) for the TOC, not startLBA (file start).
uint8_t CDIntfGetTrackInfo(uint32_t track, uint32_t offset)
{
   uint32_t tocLBA;
   uint32_t absLBA;
   uint8_t m, s, f;

   if (!disc.loaded || track < 1 || track > disc.numTracks)
      return 0xFF;

   // Use dataLBA if set (multi-file CUE), otherwise fall back to startLBA
   tocLBA = disc.tracks[track - 1].dataLBA
              ? disc.tracks[track - 1].dataLBA
              : disc.tracks[track - 1].startLBA;
   // Convert disc-image LBA to absolute MSF (add 150-frame lead-in)
   absLBA = tocLBA + 150;
   MSFFromLBA(absLBA, &m, &s, &f);

   switch (offset)
   {
      case 0:
         return m;
      case 1:
         return s;
      case 2:
         return f;
      default:
         return 0xFF;
   }
}

// Returns one byte of the track's playing duration as MSF (offset 0/1/2 =
// minutes/seconds/frames).  Duration is a sector-count delta, NOT a disc
// position, so no 150-frame lead-in offset applies.  The pregap (startLBA
// to dataLBA) is excluded: playback and the $2C00 TOC both start at
// INDEX 01.  Games size their CD-audio playback from this: Primal Rage's
// music player loads TOC bytes [5..7] (length MSF) into its DSP sector
// countdown at $F1B278 -- a zero length there stops the track after one
// sector (~13 ms).
uint8_t CDIntfGetTrackDuration(uint32_t track, uint32_t offset)
{
   uint32_t pregap, durLBA;
   uint8_t m, s, f;

   if (!disc.loaded || track < 1 || track > disc.numTracks)
      return 0;

   pregap = disc.tracks[track - 1].dataLBA
              ? (disc.tracks[track - 1].dataLBA - disc.tracks[track - 1].startLBA)
              : 0;
   durLBA = (disc.tracks[track - 1].lengthLBA > pregap)
              ? (disc.tracks[track - 1].lengthLBA - pregap)
              : 0;
   MSFFromLBA(durLBA, &m, &s, &f);

   switch (offset)
   {
      case 0:
         return m;
      case 1:
         return s;
      case 2:
         return f;
      default:
         return 0;
   }
}

// Returns the session number (1-based) for a given track
uint8_t CDIntfGetTrackSession(uint32_t track)
{
   if (!disc.loaded || track < 1 || track > disc.numTracks)
      return 0;

   return (uint8_t)disc.tracks[track - 1].session;
}

/* Extract the game boot stub from the start of session 2.
 *
 * Jaguar CD bootable discs encode the universal-header + boot-loader at the
 * very start of the first session-2 track.  The 32-byte ATARI APPROVED magic
 * lives at byte +0x42 of the (word-swapped) data, immediately followed by:
 *   +0x62: 4-byte load address (typically $00080000)
 *   +0x66: 4-byte length
 *   +0x6A: code bytes (length bytes)
 *
 * The on-disc data is word-swapped because the Jaguar's I2S audio path swaps
 * each 16-bit word during read.  We undo that swap, validate the magic, then
 * the caller injects the resulting stub directly into main RAM at the load
 * address — bypassing the BIOS streaming path entirely.
 *
 * On success: writes load address to *outLoadAddr, length to *outLength, and
 * fills outBuf (size outBufSize) with the code bytes.  Returns true. */
/* Disc LBA one past the end of the extracted boot executable (see
 * CDIntfExtractBootStub); 0 until an extraction succeeds. */
static uint32_t bootStubEndLBA = 0;

uint32_t CDIntfGetBootStubEndLBA(void)
{
   return bootStubEndLBA;
}

bool CDIntfExtractBootStub(uint8_t *outBuf, uint32_t outBufSize,
                           uint32_t *outLoadAddr, uint32_t *outLength)
{
   static const uint8_t MAGIC[32] =
      "ATARI APPROVED DATA HEADER ATRI ";
   uint32_t i;
   uint32_t firstS2Idx = 0;
   bool foundS2 = false;
   RFILE *trackFile;
   /* Battle Morph (USA) ships a ~414KB boot stub. Provide headroom up to
    * ~600KB of raw sector data (~256 sectors at 2352 B/sector). Anything
    * smaller was silently truncating large stubs to "bad length" failures. */
   static uint8_t raw[2352 * 256];
   static uint8_t swapped[sizeof(raw)];
   int64_t bytesRead;
   int64_t trackFileBase;
   uint32_t loadAddr, length;
   uint32_t nsec;
   uint32_t s;

   if (!disc.loaded || disc.numSessions < 2)
   {
      LOG_WRN("[CD-BOOTSTUB] Early exit: loaded=%d numSessions=%u\n",
              disc.loaded, disc.numSessions);
      return false;
   }

   for (i = 0; i < disc.numTracks; i++)
   {
      if (disc.tracks[i].session >= 2)
      {
         firstS2Idx = i;
         foundS2 = true;
         break;
      }
   }
   if (!foundS2 || (!chd && !disc.tracks[firstS2Idx].binFilePath[0] && !disc.binPath[0]))
   {
      LOG_WRN("[CD-BOOTSTUB] No session-2 track found (foundS2=%d, pathEmpty=%d)\n",
              foundS2, foundS2 ? !disc.tracks[firstS2Idx].binFilePath[0] : -1);
      return false;
   }

   bytesRead = 0;
   if (chd)
   {
      /* The track "BIN" path is the CHD container.  Stream INDEX 01
       * through the same TOC used at runtime. */
      LOG_INF("[CD-BOOTSTUB] Reading session-2 track %u from CHD at LBA %u\n",
              disc.tracks[firstS2Idx].number,
              disc.tracks[firstS2Idx].dataLBA);
      nsec = (uint32_t)(sizeof(raw) / 2352);
      for (s = 0; s < nsec; s++)
      {
         if (!CDIntfReadBlock(disc.tracks[firstS2Idx].dataLBA + s,
                              raw + s * 2352u))
            break;
         bytesRead += 2352;
      }
      LOG_INF("[CD-BOOTSTUB] Read %lld bytes from CHD TOC\n", (long long)bytesRead);
   }
   else
   {
   /* Both layouts start the track REGION with the track's pregap, and the
    * Atari boot header sits at the start of the user data (INDEX 01), so the
    * pregap has to be skipped -- otherwise we read pregap silence and the
    * magic check below fails.
    *
    * dataLBA - startLBA is the pregap length in sectors in BOTH cases:
    *   - multi-file CUE: the parser stores the track's INDEX 01 offset within
    *     its own BIN as (dataLBA - startLBA), and fileOffset is 0 because
    *     startLBA maps to the file start.
    *   - single-file CDI / single-BIN CUE: ParseCDI sets
    *     dataLBA = startLBA + pregap, and fileOffset is the region start.
    * So one expression covers both: fileOffset + pregap * sectorSize.
    *
    * This stayed hidden because nothing we had exercised it with a non-zero
    * skip: test/tools/cue2cdi emits pregap == 0 on the session-2 track, and
    * every CUE in the test corpus has INDEX 01 00:00:00 on its first
    * session-2 track.  DiscJuggler does store it (150 sectors on the
    * session-2 track of the ROM-set Baldies image). */
      uint32_t pregapSectors;
      pregapSectors = 0;
      if (disc.tracks[firstS2Idx].dataLBA > disc.tracks[firstS2Idx].startLBA)
         pregapSectors = disc.tracks[firstS2Idx].dataLBA -
                         disc.tracks[firstS2Idx].startLBA;

      trackFileBase = (int64_t)disc.tracks[firstS2Idx].fileOffset +
                      (int64_t)pregapSectors *
                      (int64_t)disc.tracks[firstS2Idx].sectorSize +
                      (int64_t)disc.tracks[firstS2Idx].dataShift;
      if (trackFileBase < 0)
         trackFileBase = 0;

      if (disc.tracks[firstS2Idx].binFilePath[0])
      {
         LOG_INF("[CD-BOOTSTUB] Opening track %u BIN: %s "
                 "(+%u pregap sector(s) -> offset $%llX)\n",
                 disc.tracks[firstS2Idx].number,
                 disc.tracks[firstS2Idx].binFilePath, (unsigned)pregapSectors,
                 (unsigned long long)trackFileBase);
         trackFile = rfopen(disc.tracks[firstS2Idx].binFilePath, "rb");
      }
      else
      {
         LOG_INF("[CD-BOOTSTUB] Opening track %u in single-file image: %s "
                 "(region $%X + %u pregap sector(s) -> offset $%llX)\n",
                 disc.tracks[firstS2Idx].number, disc.binPath,
                 disc.tracks[firstS2Idx].fileOffset, (unsigned)pregapSectors,
                 (unsigned long long)trackFileBase);
         trackFile = rfopen(disc.binPath, "rb");
      }
      if (!trackFile)
      {
         LOG_ERR("[CD-BOOTSTUB] rfopen failed for %s\n",
                 disc.tracks[firstS2Idx].binFilePath[0] ?
                    disc.tracks[firstS2Idx].binFilePath : disc.binPath);
         return false;
      }

      rfseek(trackFile, trackFileBase, SEEK_SET);
      bytesRead = rfread(raw, 1, sizeof(raw), trackFile);
      rfclose(trackFile);
      LOG_INF("[CD-BOOTSTUB] Read %lld bytes from track BIN\n", (long long)bytesRead);
   }
   if (bytesRead < 0x6A + 4)
   {
      LOG_ERR("[CD-BOOTSTUB] Too few bytes read (%lld < %d)\n",
              (long long)bytesRead, 0x6A + 4);
      return false;
   }

   /* CDI V2 head-loss repair: overlay the constant header prefix over the
    * zeros the dataShift re-alignment exposes at the data start (see
    * CDIRepairV2BootTrack; same overlay as CDIntfReadBlockCDI). */
   if (disc.tracks[firstS2Idx].synthBootHeader &&
       (uint32_t)bytesRead >= CDI_CANON_LEN)
      memcpy(raw, cdi_canon_prefix, CDI_CANON_LEN);

   /* Word-swap each 16-bit pair (Jaguar I2S byte order). */
   for (i = 0; i + 1 < (uint32_t)bytesRead; i += 2)
   {
      swapped[i]     = raw[i + 1];
      swapped[i + 1] = raw[i];
   }

   /* One LOG_DBG per byte produced 48 separate log lines: the frontend's
    * log callback prefixes and newline-terminates every call, so a
    * partial-line idiom does not concatenate.  Build each dump with
    * fixed indexing (no sprintf in a loop: the bounds are then obvious
    * by construction and no format string is involved) and emit it as a
    * single line. */
   {
      static const char hexd[] = "0123456789ABCDEF";
      char rawhex[0x30 * 3 + 1];
      char swphex[0x30 * 3 + 1];
      uint32_t last = (0x70 < (uint32_t)bytesRead) ? 0x70 : (uint32_t)bytesRead;
      uint32_t n = 0;

      for (i = 0x40; i < last; i++, n += 3)
      {
         rawhex[n]     = hexd[(raw[i] >> 4) & 0x0F];
         rawhex[n + 1] = hexd[raw[i] & 0x0F];
         rawhex[n + 2] = ' ';
         swphex[n]     = hexd[(swapped[i] >> 4) & 0x0F];
         swphex[n + 1] = hexd[swapped[i] & 0x0F];
         swphex[n + 2] = ' ';
      }
      rawhex[n] = '\0';
      swphex[n] = '\0';

      LOG_DBG("[CD-BOOTSTUB] Raw bytes 0x40-0x%02X (pre-swap): %s\n",
              (unsigned)(last ? last - 1 : 0), rawhex);
      LOG_DBG("[CD-BOOTSTUB] Swapped bytes 0x40-0x%02X: %s\n",
              (unsigned)(last ? last - 1 : 0), swphex);
   }
   LOG_DBG("[CD-BOOTSTUB] Swapped as text: '%.32s'\n", swapped + 0x42);

   if (memcmp(swapped + 0x42, MAGIC, sizeof(MAGIC)) != 0)
   {
      uint32_t matched;
      uint32_t all_zero;
      uint32_t j;

      matched = 0;
      all_zero = 1;
      for (j = 0; j < (uint32_t)sizeof(MAGIC); j++)
      {
         if (swapped[0x42 + j] == MAGIC[j])
            matched++;
         if (swapped[0x42 + j] != 0)
            all_zero = 0;
      }

      if (all_zero)
      {
         /* Bad CDI V2 rips: boot header region is zeros in the file itself.
          * No offset fix recovers absent data - refuse with an actionable
          * message so users stop re-filing this as an unsupported format. */
         LOG_ERR("[CD-BOOTSTUB] Boot header region is zero-filled at +0x42 - "
                 "this image is an incomplete / bad rip, not an unsupported "
                 "format\n");
      }
      else
      {
         LOG_ERR("[CD-BOOTSTUB] Magic mismatch at +0x42 of session-2 track BIN "
                 "(matched %u/%u bytes)\n",
                 (unsigned)matched, (unsigned)sizeof(MAGIC));
      }
      return false;
   }

   loadAddr = ((uint32_t)swapped[0x62] << 24) | ((uint32_t)swapped[0x63] << 16)
            | ((uint32_t)swapped[0x64] <<  8) |  (uint32_t)swapped[0x65];
   length   = ((uint32_t)swapped[0x66] << 24) | ((uint32_t)swapped[0x67] << 16)
            | ((uint32_t)swapped[0x68] <<  8) |  (uint32_t)swapped[0x69];

   if (length == 0 || length > outBufSize
       || (uint64_t)0x6A + length > (uint64_t)bytesRead)
   {
      LOG_ERR("[CD-BOOTSTUB] Bad length $%X (loadAddr=$%06X, bufSize=%u, available=%lld)\n",
              length, loadAddr, outBufSize, (long long)bytesRead - 0x6A);
      return false;
   }

   memcpy(outBuf, swapped + 0x6A, length);
   *outLoadAddr = loadAddr;
   *outLength   = length;

   /* Remember where the boot executable ends on disc.  The real BIOS
    * leaves the drive head parked just past the executable it streamed;
    * games that continue with a bare Unpause (Battle Morph: $0400 pause,
    * $0500 unpause, no seek) expect data to flow from there, not from
    * block 0. */
   bootStubEndLBA = disc.tracks[firstS2Idx].startLBA
                  + (0x6A + length + 2351) / 2352;

   LOG_INF("[CD-BOOTSTUB] Extracted $%X bytes for load addr $%06X (track %u BIN: %s)\n",
           length, loadAddr,
           disc.tracks[firstS2Idx].number, disc.tracks[firstS2Idx].binFilePath);
   return true;
}

uint32_t CDIntfGetDiscTotalSectors(void)
{
   if (!disc.loaded)
      return 0;

   if (disc.numSessions >= 2)
      return disc.sessions[1].leadOutLBA;

   return disc.sessions[0].leadOutLBA;
}

uint32_t CDIntfGetSession2TrackCount(void)
{
   uint32_t i, n = 0;
   if (!disc.loaded || disc.numSessions < 2)
      return 0;
   for (i = 0; i < disc.numTracks; i++)
      if (disc.tracks[i].session >= 2)
         n++;
   return n;
}

uint32_t CDIntfGetSession2TrackLBA(uint32_t which)
{
   uint32_t i, n = 0;
   if (!disc.loaded || disc.numSessions < 2)
      return 0;
   for (i = 0; i < disc.numTracks; i++)
   {
      if (disc.tracks[i].session < 2)
         continue;
      if (n == which)
         return disc.tracks[i].dataLBA
                  ? disc.tracks[i].dataLBA
                  : disc.tracks[i].startLBA;
      n++;
   }
   return 0;
}

uint32_t CDIntfGetSession2FirstTrackLBA(void)
{
   uint32_t i;

   if (!disc.loaded || disc.numSessions < 2)
      return 0;

   for (i = 0; i < disc.numTracks; i++)
   {
      if (disc.tracks[i].session >= 2)
         return disc.tracks[i].dataLBA
                  ? disc.tracks[i].dataLBA
                  : disc.tracks[i].startLBA;
   }
   return 0;
}

uint32_t CDIntfGetSession2GameDataLBA(void)
{
   uint32_t i;
   uint32_t bestIdx = UINT32_MAX;
   uint32_t bestLen = 0;

   if (!disc.loaded || disc.numSessions < 2)
      return 0;

   for (i = 0; i < disc.numTracks; i++)
   {
      if (disc.tracks[i].session >= 2)
      {
         LOG_DBG("[CD-S2TRACK] track %u: startLBA=%u dataLBA=%u len=%u sess=%u\n",
                 disc.tracks[i].number, disc.tracks[i].startLBA,
                 disc.tracks[i].dataLBA, disc.tracks[i].lengthLBA,
                 disc.tracks[i].session);
         if (disc.tracks[i].lengthLBA > bestLen)
         {
            bestLen = disc.tracks[i].lengthLBA;
            bestIdx = i;
         }
      }
   }

   if (bestIdx != UINT32_MAX)
   {
      uint32_t lba = disc.tracks[bestIdx].dataLBA
                       ? disc.tracks[bestIdx].dataLBA
                       : disc.tracks[bestIdx].startLBA;
      LOG_INF("[CD-S2TRACK] Selected largest track %u (len=%u) dataLBA=%u\n",
              disc.tracks[bestIdx].number, bestLen, lba);
      return lba;
   }

   return 0;
}
