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
static void MSFFromLBA(uint32_t lba, uint8_t *m, uint8_t *s, uint8_t *f);
static uint32_t LBAFromMSF(uint8_t m, uint8_t s, uint8_t f);
static char *TrimWhitespace(char *str);
static bool GetDirectoryFromPath(const char *path, char *dir, size_t dirSize);

// The global disc state
static struct CDIntfDisc disc;

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

// Parse a CUE sheet and populate the disc structure
static bool ParseCueSheet(const char *cuePath)
{
   RFILE *cueFile;
   char line[1024];
   char dir[4096];
   char currentBinFile[4096] = {0};
   int currentTrack = -1;
   int currentSession = 1;
   uint32_t fileOffset = 0;
   uint32_t sectorSize = 2352;
   int trackCount = 0;
   int64_t binFileSize = 0;

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

            fileOffset = 0;
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

            if (strcasecmp(typeStr, "AUDIO") == 0)
               disc.tracks[currentTrack - 1].type = CDINTF_TRACK_AUDIO;
            else if (strncasecmp(typeStr, "MODE1", 5) == 0)
            {
               disc.tracks[currentTrack - 1].type = CDINTF_TRACK_MODE1;
               // Check for sector size after slash
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
               // Default to audio for Jaguar CD (all tracks are audio format)
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

               disc.tracks[currentTrack - 1].startLBA = lba;
               disc.tracks[currentTrack - 1].startM = mm;
               disc.tracks[currentTrack - 1].startS = ss;
               disc.tracks[currentTrack - 1].startF = ff;
               disc.tracks[currentTrack - 1].fileOffset = fileOffset + (lba * sectorSize);

               // For the Jaguar CD, all tracks in session 1 = audio, session 2 = data as audio
               // Simple heuristic: track 1 is session 1, tracks 2+ are session 2
               if (currentTrack == 1)
                  disc.tracks[currentTrack - 1].session = 1;
               else
                  disc.tracks[currentTrack - 1].session = 2;
            }
         }
      }
      // REM SESSION nn (non-standard but used by some CUE sheets)
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

   // Calculate track lengths and apply session info from track session markers
   {
      int i;
      // Determine bin file size for the last track's length
      RFILE *bf = rfopen(disc.binPath, "rb");
      if (bf)
      {
         rfseek(bf, 0, SEEK_END);
         binFileSize = rftell(bf);
         rfclose(bf);
      }

      for (i = 0; i < (int)disc.numTracks; i++)
      {
         if (i + 1 < (int)disc.numTracks)
         {
            disc.tracks[i].lengthLBA = disc.tracks[i + 1].startLBA - disc.tracks[i].startLBA;
         }
         else
         {
            // Last track: calculate from file size
            if (binFileSize > 0 && disc.tracks[i].sectorSize > 0)
            {
               uint32_t totalSectors = binFileSize / disc.tracks[i].sectorSize;
               if (disc.tracks[i].startLBA < totalSectors)
                  disc.tracks[i].lengthLBA = totalSectors - disc.tracks[i].startLBA;
               else
                  disc.tracks[i].lengthLBA = 0;
            }
         }

         // Apply session from REM SESSION if set, otherwise use heuristic
         if (currentSession > 1 && disc.tracks[i].session == 0)
            disc.tracks[i].session = (i == 0) ? 1 : 2;
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

   disc.loaded = true;
   return true;
}

bool CDIntfOpenImage(const char *cuePath)
{
   CDIntfCloseImage();

   if (!ParseCueSheet(cuePath))
      return false;

   // Open the BIN file for reading
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
   if (disc.binFile)
   {
      rfclose((RFILE *)disc.binFile);
      disc.binFile = NULL;
   }
   memset(&disc, 0, sizeof(disc));
}

bool CDIntfIsImageLoaded(void)
{
   return disc.loaded && disc.binFile != NULL;
}

bool CDIntfInit(void)
{
   return disc.loaded && disc.binFile != NULL;
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

   if (!disc.loaded || !disc.binFile || !buffer)
      return false;

   // Find which track contains this sector
   for (i = (int)disc.numTracks - 1; i >= 0; i--)
   {
      if (sector >= disc.tracks[i].startLBA)
      {
         track = &disc.tracks[i];
         break;
      }
   }

   if (!track)
   {
      // Sector is before the first track -- return zeros
      memset(buffer, 0, 2352);
      return true;
   }

   sectorSize = track->sectorSize;
   if (sectorSize == 0)
      sectorSize = 2352;

   // Calculate the file position
   // The track's fileOffset tells us where track data starts in the file.
   // Then we add the offset for the requested sector within the track.
   filePos = (int64_t)(sector - track->startLBA) * sectorSize + track->fileOffset;

   // For single-BIN CUE sheets, all tracks are in the same file and fileOffset
   // accounts for the absolute position. But for multi-index tracks where INDEX 01
   // is the actual start, fileOffset is based on INDEX 01's MSF offset.
   // Simpler approach: single BIN file, sectors are sequential.
   // File position = sector * sectorSize (for single-file BIN)
   filePos = (int64_t)sector * sectorSize;

   rfseek((RFILE *)disc.binFile, filePos, SEEK_SET);
   bytesRead = rfread(buffer, 1, 2352, (RFILE *)disc.binFile);

   if (bytesRead < 2352)
   {
      // Pad with zeros if we hit EOF
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

// Returns session info for use by cdrom.c
// offset == 0 -> min track for session
// offset == 1 -> max track for session
uint8_t CDIntfGetSessionInfo(uint32_t session, uint32_t offset)
{
   if (!disc.loaded || session < 1 || session > disc.numSessions)
      return 0xFF;

   switch (offset)
   {
      case 0:
         return (uint8_t)disc.sessions[session - 1].firstTrack;
      case 1:
         return (uint8_t)disc.sessions[session - 1].lastTrack;
      default:
         return 0xFF;
   }
}

// Returns track info for use by cdrom.c
// offset: 0 = minutes, 1 = seconds, 2 = frames of track start position
uint8_t CDIntfGetTrackInfo(uint32_t track, uint32_t offset)
{
   if (!disc.loaded || track < 1 || track > disc.numTracks)
      return 0xFF;

   switch (offset)
   {
      case 0:
         return disc.tracks[track - 1].startM;
      case 1:
         return disc.tracks[track - 1].startS;
      case 2:
         return disc.tracks[track - 1].startF;
      default:
         return 0xFF;
   }
}
