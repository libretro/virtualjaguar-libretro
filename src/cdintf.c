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
#include <streams/file_stream.h>
#include <streams/file_stream_transforms.h>
#include "cdintf.h"
#include "jaguar.h"
#include "log.h"

// CDI (DiscJuggler) format support
static RFILE *cdi_file = NULL;
static bool ParseCDI(const char *cdiPath);

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
static bool ParseIso(const char *isoPath);
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

/* Auth-data redirect for redump-style multi-session dumps.
 *
 * Jaguar CD BIOS authenticates session 2 by seeking to a hardcoded position
 * (computed from session 2 lead-out: `leadout - 453`) and DSP-checksumming
 * 149 sectors of audio there.  On a real disc those 149 sectors are the
 * pregap-audio "ATARI" signature.  Redump-style dumps strip that pregap and
 * place the signature at the *start of the first session-2 track's BIN file*
 * (verified: track 30 begins with `72 d7 54 41 49 52 54 41 49 52 ...` =
 * `TAIRTAIR` byte-swapped).
 *
 * Our CUE parser places session-2 tracks contiguously after a small inter-
 * session gap, so the BIOS's hardcoded seek target (near lead-out) lands in
 * silence inside whatever track happens to occupy that LBA range.  This
 * function detects that case and reads the auth data straight from track 30's
 * BIN file — auth then runs on real data and passes legitimately.
 *
 * Returns true if it filled `buffer` (caller must skip normal track lookup). */
static bool TryReadAuthRedirect(uint32_t sector, uint8_t *buffer)
{
   uint32_t i;
   uint32_t firstS2Idx = 0;
   uint32_t s2Leadout;
   uint32_t authStart, authEnd;
   uint32_t fileSector;
   int64_t bytesRead;
   bool foundS2 = false;
   RFILE *trackFile;

   if (disc.numSessions < 2)
      return false;

   s2Leadout = disc.sessions[1].leadOutLBA;
   if (s2Leadout < 453)
      return false;

   /* BIOS seeks 453 frames before session-2 lead-out and reads 149 frames. */
   authStart = s2Leadout - 453;
   authEnd   = authStart + 149;

   if (sector < authStart || sector >= authEnd)
      return false;

   for (i = 0; i < disc.numTracks; i++)
   {
      if (disc.tracks[i].session >= 2)
      {
         firstS2Idx = i;
         foundS2 = true;
         break;
      }
   }
   if (!foundS2 || !disc.tracks[firstS2Idx].binFilePath[0])
      return false;

   fileSector = sector - authStart;
   trackFile = rfopen(disc.tracks[firstS2Idx].binFilePath, "rb");
   if (!trackFile)
      return false;

   rfseek(trackFile, (int64_t)fileSector * 2352, SEEK_SET);
   bytesRead = rfread(buffer, 1, 2352, trackFile);
   rfclose(trackFile);

   if (bytesRead < 2352)
   {
      if (bytesRead > 0)
         memset(buffer + bytesRead, 0, 2352 - bytesRead);
      else
         return false;
   }
   return true;
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

   // Build session info
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

      // Session 1
      disc.sessions[0].number = 1;
      disc.sessions[0].firstTrack = (sess1Min <= CDINTF_MAX_TRACKS) ? sess1Min : 1;
      disc.sessions[0].lastTrack = (sess1Max > 0) ? sess1Max : 1;

      // Session 1 lead-out: start of session 2 first track, or end of session 1 last track
      if (disc.numSessions >= 2 && sess2Min <= CDINTF_MAX_TRACKS)
      {
         uint32_t leadOut = disc.tracks[sess2Min - 1].startLBA;
         disc.sessions[0].leadOutLBA = leadOut;
         MSFFromLBA(leadOut, &disc.sessions[0].leadOutM,
                    &disc.sessions[0].leadOutS, &disc.sessions[0].leadOutF);
      }
      else
      {
         // Single session: lead-out after last track
         uint32_t lastIdx = disc.sessions[0].lastTrack - 1;
         uint32_t leadOut = disc.tracks[lastIdx].startLBA + disc.tracks[lastIdx].lengthLBA;
         disc.sessions[0].leadOutLBA = leadOut;
         MSFFromLBA(leadOut, &disc.sessions[0].leadOutM,
                    &disc.sessions[0].leadOutS, &disc.sessions[0].leadOutF);
      }

      // Session 2
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
static bool ParseIso(const char *isoPath)
{
   RFILE *isoFile;
   int64_t fileSize;
   uint32_t totalSectors;

   memset(&disc, 0, sizeof(disc));

   isoFile = rfopen(isoPath, "rb");
   if (!isoFile)
   {
      LOG_ERR("[CD-ISO] Cannot open %s\n", isoPath);
      return false;
   }
   rfseek(isoFile, 0, SEEK_END);
   fileSize = rftell(isoFile);
   rfclose(isoFile);

   if (fileSize < 2048)
   {
      LOG_ERR("[CD-ISO] %s is too small (%lld bytes)\n", isoPath, (long long)fileSize);
      return false;
   }

   // Mode1 sector size is 2048 bytes.
   totalSectors = (uint32_t)(fileSize / 2048);

   snprintf(disc.binPath, sizeof(disc.binPath), "%s", isoPath);

   disc.numTracks   = 1;
   disc.numSessions = 1;

   disc.tracks[0].number      = 1;
   disc.tracks[0].session     = 1;
   disc.tracks[0].type        = CDINTF_TRACK_MODE1;
   disc.tracks[0].startLBA    = 0;
   disc.tracks[0].dataLBA     = 0;
   disc.tracks[0].lengthLBA   = totalSectors;
   disc.tracks[0].fileOffset  = 0;
   disc.tracks[0].sectorSize  = 2048;
   MSFFromLBA(0, &disc.tracks[0].startM,
                 &disc.tracks[0].startS,
                 &disc.tracks[0].startF);
   snprintf(disc.tracks[0].binFilePath,
            sizeof(disc.tracks[0].binFilePath),
            "%s", isoPath);

   disc.sessions[0].number     = 1;
   disc.sessions[0].firstTrack = 1;
   disc.sessions[0].lastTrack  = 1;
   disc.sessions[0].leadOutLBA = totalSectors;
   MSFFromLBA(totalSectors,
              &disc.sessions[0].leadOutM,
              &disc.sessions[0].leadOutS,
              &disc.sessions[0].leadOutF);

   disc.loaded = true;

   LOG_INF("[CD-ISO] Loaded %s as single-track Mode1 disc (%u sectors)\n",
           isoPath, totalSectors);
   LOG_WRN("[CD-ISO] Jaguar boot from .iso is not supported — needs session 2 audio "
           "pregap + game data. Use BIN/CUE or CDI for bootable images.\n");

   return true;
}

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
         uint8_t trkData[256];  // 0x70-ish bytes
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
            rfseek(cdi_file, 10, SEEK_CUR);
         else
            rfseek(cdi_file, 2, SEEK_CUR);

         // Read the track-data block. We only need the documented fields;
         // the offsets within the block are fixed regardless of CDI version.
         // sizeof(CDI_track_data) = 4+4+6+4+0xc+4+4+0x10+4+0x1d = 0x55+? — use 0x70 to be safe.
         memset(trkData, 0, sizeof(trkData));
         if (rfread(trkData, 1, 0x70, cdi_file) != 0x70)
            goto fail;

         // Field offsets per DreamShell CDI_track_data layout:
         //   +0x00 pregap_length (u32)
         //   +0x04 length (u32)
         //   +0x0a unknown (6 bytes)
         //   +0x10 mode (u32)
         //   +0x14 unknown (12 bytes)
         //   +0x20 start_lba (u32)
         //   +0x24 total_length (u32)
         //   +0x28 unknown (16 bytes)
         //   +0x38 sector_size (u32, code: 0=2048, 1=2336, 2=2352)
         #define LE32(p, o) ((uint32_t)(p)[(o)] | ((uint32_t)(p)[(o)+1] << 8) | \
                             ((uint32_t)(p)[(o)+2] << 16) | ((uint32_t)(p)[(o)+3] << 24))
         pregapLen   = LE32(trkData, 0x00);
         length      = LE32(trkData, 0x04);
         mode        = LE32(trkData, 0x10);
         startLba    = LE32(trkData, 0x20);
         totalLength = LE32(trkData, 0x24);
         sectorCode  = LE32(trkData, 0x38);
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
           + (int64_t)(sector - disc.tracks[trackIdx].startLBA) * sectorSize;

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
   return true;
}

bool CDIntfOpenImage(const char *path)
{
   const char *ext;
   CDIntfCloseImage();

   ext = strrchr(path, '.');

   if (ext && strcasecmp(ext + 1, "cdi") == 0)
      return ParseCDI(path);

   if (ext && strcasecmp(ext + 1, "iso") == 0)
      return ParseIso(path);

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
   memset(&disc, 0, sizeof(disc));
}

bool CDIntfIsImageLoaded(void)
{
   if (!disc.loaded)
      return false;
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

   // BIOS auth zone redirect: when sector falls in [s2_leadout-453, s2_leadout-304),
   // return real TAIRTAIR data from the start of the first session-2 track BIN.
   // Redump-style BIN/CUE strips the 149-frame pregap so the auth signature lives
   // at the start of the track file rather than at the BIOS's hardcoded seek target.
   if (TryReadAuthRedirect(sector, buffer))
   {
      static uint32_t authHits = 0;
      if (authHits < 5)
         LOG_INF("[CD-AUTH-REDIRECT] sector=%u served from track-30 BIN (hit #%u)\n", sector, ++authHits);
      else
         authHits++;
      lastReadVirtualPregap = false;
      return true;
   }

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
      // True inter-session gap (outside the redirected pregap window).  Return
      // silence; the auth bypass at $050A9C still installs as a safety net for
      // cases where the redirect window doesn't cover what BIOS actually reads.
      memset(buffer, 0, 2352);
      lastReadVirtualPregap = true;
      lastVirtualPregapLBA = sector;
      JaguarInstallCDAuthBypass();
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
      RFILE *trackFile = rfopen(track->binFilePath, "rb");
      if (!trackFile)
      {
         memset(buffer, 0, 2352);
         return false;
      }

      filePos = (int64_t)(sector - track->startLBA) * sectorSize + track->fileOffset;
      rfseek(trackFile, filePos, SEEK_SET);
      bytesRead = rfread(buffer, 1, 2352, trackFile);
      rfclose(trackFile);

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
   if (!disc.loaded || track < 1 || track > disc.numTracks)
      return 0xFF;

   // Use dataLBA if set (multi-file CUE), otherwise fall back to startLBA
   uint32_t tocLBA = disc.tracks[track - 1].dataLBA
                      ? disc.tracks[track - 1].dataLBA
                      : disc.tracks[track - 1].startLBA;
   // Convert disc-image LBA to absolute MSF (add 150-frame lead-in)
   uint32_t absLBA = tocLBA + 150;
   uint8_t m, s, f;
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
   uint32_t loadAddr, length;

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
   if (!foundS2 || !disc.tracks[firstS2Idx].binFilePath[0])
   {
      LOG_WRN("[CD-BOOTSTUB] No session-2 track found (foundS2=%d, pathEmpty=%d)\n",
              foundS2, foundS2 ? !disc.tracks[firstS2Idx].binFilePath[0] : -1);
      return false;
   }

   LOG_INF("[CD-BOOTSTUB] Opening track %u BIN: %s\n",
           disc.tracks[firstS2Idx].number, disc.tracks[firstS2Idx].binFilePath);
   trackFile = rfopen(disc.tracks[firstS2Idx].binFilePath, "rb");
   if (!trackFile)
   {
      LOG_ERR("[CD-BOOTSTUB] rfopen failed for %s\n",
              disc.tracks[firstS2Idx].binFilePath);
      return false;
   }

   rfseek(trackFile, 0, SEEK_SET);
   bytesRead = rfread(raw, 1, sizeof(raw), trackFile);
   rfclose(trackFile);
   LOG_INF("[CD-BOOTSTUB] Read %lld bytes from track BIN\n", (long long)bytesRead);
   if (bytesRead < 0x6A + 4)
   {
      LOG_ERR("[CD-BOOTSTUB] Too few bytes read (%lld < %d)\n",
              (long long)bytesRead, 0x6A + 4);
      return false;
   }

   /* Word-swap each 16-bit pair (Jaguar I2S byte order). */
   for (i = 0; i + 1 < (uint32_t)bytesRead; i += 2)
   {
      swapped[i]     = raw[i + 1];
      swapped[i + 1] = raw[i];
   }

   LOG_DBG("[CD-BOOTSTUB] Raw bytes 0x40-0x6F (pre-swap): ");
   for (i = 0x40; i < 0x70 && i < (uint32_t)bytesRead; i++)
      LOG_DBG("%02X ", raw[i]);
   LOG_DBG("\n");
   LOG_DBG("[CD-BOOTSTUB] Swapped bytes 0x40-0x6F: ");
   for (i = 0x40; i < 0x70 && i < (uint32_t)bytesRead; i++)
      LOG_DBG("%02X ", swapped[i]);
   LOG_DBG("\n");
   LOG_DBG("[CD-BOOTSTUB] Swapped as text: '%.32s'\n", swapped + 0x42);

   if (memcmp(swapped + 0x42, MAGIC, sizeof(MAGIC)) != 0)
   {
      LOG_ERR("[CD-BOOTSTUB] Magic mismatch at +0x42 of session-2 track BIN\n");
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
