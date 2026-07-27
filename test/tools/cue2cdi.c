/*
 * cue2cdi.c — CUE/BIN -> DiscJuggler CDI converter for Jaguar CD images
 *
 * Atari Jaguar CD dumps commonly exist as multi-session CUE/BIN (redump
 * style: session 1 = short audio warning track(s), session 2 = "data"
 * tracks recorded as AUDIO-type tracks containing byte-swapped data).
 * BigPEmu and other tools prefer DiscJuggler .cdi. This tool converts
 * CUE/BIN to a CDI in the CANONICAL DiscJuggler layout, preserving session
 * structure and track types faithfully — tracks are NOT forced to data mode.
 *
 * Emitted variant: DiscJuggler V3 (trailer version id $80000005, absolute
 * header offset), DJ 3.00.780+ style track blocks. Canonical field offsets
 * inside the 0x57-byte per-track data block (packed — verified against
 * cdirip cdi.c, DreamShell modules/isofs/cdi.c struct CDI_track_data
 * __attribute__((packed)), and libmirage image-cdi/parser.c):
 *   +0x00 pregap_length  +0x04 length        +0x0e mode
 *   +0x1e start_lba      +0x22 total_length  +0x36 sector-size code
 * (sector-size code: 0=2048, 1=2336, 2=2352)
 *
 * NOTE: src/cd/cdintf.c ParseCDI currently reads these fields 2 bytes late
 * (+0x10/+0x20/+0x24/+0x38, an unpacked-struct misreading of the DreamShell
 * reference) and consumes a 0x70-byte block instead of 0x57 — so the core
 * cannot load the files this tool produces until that parser is fixed.
 * Canonical correctness wins: genuine DiscJuggler images and BigPEmu use
 * the layout emitted here.
 *
 * Usage:
 *   cue2cdi input.cue [output.cdi] [--verify] [--quiet] [--version]
 *   cue2cdi [--batch] DIR [--verify] [--force] [--quiet]
 *
 * --verify re-parses the produced CDI with the canonical cdirip walk and
 * byte-compares every track's payload against the source BIN(s).
 *
 * Batch mode (a directory argument implies --batch): recursively walks DIR
 * (hidden entries and symlinks skipped), converts every *.cue in place to
 * <same-dir>/<same-basename>.cdi, skipping targets already newer than their
 * sources (--force reconverts). Per-file failures are reported and the queue
 * continues; exit code is 1 if any file failed.
 *
 * Build:
 *   cc -O2 -Wall -std=c99 -o test/tools/cue2cdi test/tools/cue2cdi.c
 *
 * Part of virtualjaguar-libretro. GPLv3.
 */

#define _FILE_OFFSET_BITS 64

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <stdint.h>

/* Batch mode is host-side POSIX (opendir/readdir/lstat) */
#include <sys/types.h>
#include <sys/stat.h>
#include <dirent.h>

#define CUE2CDI_VERSION "1.1"

#define MAX_TRACKS   99
#define MAX_SESSIONS 16
#define PATH_MAX_LEN 4096

/* Mirrors src/cd/cdintf.c ParseCueSheet(): gap inserted between sessions of
 * a multi-file CUE (session 1 lead-out + run-out + session 2 lead-in). */
#define INTER_SESSION_GAP 11400

/* DiscJuggler trailer version ids (cdirip cdi.h) */
#define CDI_V2_ID  0x80000004u
#define CDI_V3_ID  0x80000005u
#define CDI_V35_ID 0x80000006u

/* Filler values observed in genuine DiscJuggler images (libmirage
 * image-cdi/parser.c expected-field tables) */
#define CDI_DISC_CAPACITY 0x00057E40u  /* 360000 sectors = 80 min */
#define CDI_MEDIUM_CD     0x0098u      /* 0x38 would be DVD */

/* Track start marker (cdirip cdi.c TRACK_START_MARK, twice) */
static const uint8_t cdi_track_start_marker[20] = {
   0x00,0x00,0x01,0x00,0x00,0x00,0xFF,0xFF,0xFF,0xFF,
   0x00,0x00,0x01,0x00,0x00,0x00,0xFF,0xFF,0xFF,0xFF
};

static int g_quiet = 0;

#define INFO(...) do { if (!g_quiet) fprintf(stderr, __VA_ARGS__); } while (0)
#define WARN(...) fprintf(stderr, "warning: " __VA_ARGS__)
#define ERR(...)  fprintf(stderr, "error: " __VA_ARGS__)

typedef struct {
   int      number;         /* track number from cue (1-based) */
   int      session;        /* session number (1-based) */
   int      mode;           /* 0 = audio, 1 = mode1, 2 = mode2 */
   uint32_t sectorSize;     /* 2048 / 2336 / 2352 */
   char     binPath[PATH_MAX_LEN];
   long long idx0;          /* INDEX 00 LBA (file-relative for multi-file), -1 if absent */
   long long idx1;          /* INDEX 01 LBA, -1 if absent */
   /* Computed layout */
   uint32_t startLBA;       /* disc-absolute start of track region (incl. pregap) */
   uint32_t pregapLen;      /* sectors between region start and INDEX 01 */
   uint32_t regionSectors;  /* total sectors in this track's region */
   uint64_t fileByteOffset; /* byte offset of region within binPath */
} Track;

typedef struct {
   Track tracks[MAX_TRACKS];
   int   numTracks;
   int   numSessions;
   int   multiFile;
} Disc;

/* -------------------------------------------------------------------------
 * Small helpers
 * ------------------------------------------------------------------------- */

static char *trim(char *s)
{
   char *end;
   while (*s && isspace((unsigned char)*s))
      s++;
   if (!*s)
      return s;
   end = s + strlen(s) - 1;
   while (end > s && isspace((unsigned char)*end))
      end--;
   end[1] = '\0';
   return s;
}

static int istrncmp(const char *a, const char *b, size_t n)
{
   size_t i;
   for (i = 0; i < n && a[i] && b[i]; i++) {
      int ca = tolower((unsigned char)a[i]);
      int cb = tolower((unsigned char)b[i]);
      if (ca != cb)
         return ca - cb;
   }
   if (i < n)
      return (unsigned char)a[i] - (unsigned char)b[i];
   return 0;
}

static void dirname_of(const char *path, char *dir, size_t dirSize)
{
   const char *s1 = strrchr(path, '/');
   const char *s2 = strrchr(path, '\\');
   const char *sep = (s2 && (!s1 || s2 > s1)) ? s2 : s1;
   if (sep) {
      size_t len = (size_t)(sep - path) + 1;
      if (len >= dirSize)
         len = dirSize - 1;
      memcpy(dir, path, len);
      dir[len] = '\0';
   } else {
      dir[0] = '\0';
   }
}

static const char *basename_of(const char *path)
{
   const char *s1 = strrchr(path, '/');
   const char *s2 = strrchr(path, '\\');
   const char *sep = (s2 && (!s1 || s2 > s1)) ? s2 : s1;
   return sep ? sep + 1 : path;
}

static long long file_size(const char *path)
{
   FILE *f = fopen(path, "rb");
   long long sz;
   if (!f)
      return -1;
   if (fseeko(f, 0, SEEK_END) != 0) {
      fclose(f);
      return -1;
   }
   sz = (long long)ftello(f);
   fclose(f);
   return sz;
}

static void put_le16(FILE *f, uint16_t v)
{
   uint8_t b[2];
   b[0] = (uint8_t)(v & 0xFF);
   b[1] = (uint8_t)(v >> 8);
   fwrite(b, 1, 2, f);
}

static void put_le32(FILE *f, uint32_t v)
{
   uint8_t b[4];
   b[0] = (uint8_t)(v & 0xFF);
   b[1] = (uint8_t)((v >> 8) & 0xFF);
   b[2] = (uint8_t)((v >> 16) & 0xFF);
   b[3] = (uint8_t)((v >> 24) & 0xFF);
   fwrite(b, 1, 4, f);
}

static void put_zeros(FILE *f, size_t n)
{
   static const uint8_t z[128];
   while (n) {
      size_t c = n > sizeof(z) ? sizeof(z) : n;
      fwrite(z, 1, c, f);
      n -= c;
   }
}

static uint32_t get_le32(const uint8_t *p)
{
   return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
          ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static void store_le32(uint8_t *p, uint32_t v)
{
   p[0] = (uint8_t)(v & 0xFF);
   p[1] = (uint8_t)((v >> 8) & 0xFF);
   p[2] = (uint8_t)((v >> 16) & 0xFF);
   p[3] = (uint8_t)((v >> 24) & 0xFF);
}

static const char *mode_name(int mode)
{
   switch (mode) {
      case 0:  return "AUDIO";
      case 1:  return "MODE1";
      case 2:  return "MODE2";
      default: return "?";
   }
}

/* Same combination table as cdintf.c CDISectorSizeFromCode() (lines 579-591),
 * including the caller's fallback-to-2352 when the combo is unlisted. */
static uint32_t cdi_effective_sector_size(uint32_t mode, uint32_t code)
{
   uint32_t sz = 0;
   switch (mode) {
      case 0: sz = (code == 2) ? 2352 : 0; break;
      case 1: sz = (code == 0) ? 2048 : 0; break;
      case 2:
         if (code == 0) sz = 2048;
         else if (code == 1) sz = 2336;
         break;
      default: break;
   }
   return sz ? sz : 2352;
}

static uint32_t sector_code_from_size(uint32_t size)
{
   switch (size) {
      case 2048: return 0;
      case 2336: return 1;
      default:   return 2;   /* 2352 */
   }
}

/* -------------------------------------------------------------------------
 * CUE parsing (mirrors src/cd/cdintf.c ParseCueSheet semantics, but also
 * records INDEX 00 so per-track pregaps survive the conversion)
 * ------------------------------------------------------------------------- */

static int parse_cue(const char *cuePath, Disc *d)
{
   FILE *cue;
   char line[1024];
   char dir[PATH_MAX_LEN];
   char curBin[PATH_MAX_LEN] = { 0 };
   int  curTrack = -1;        /* index into d->tracks */
   int  curSession = 1;
   int  fileCount = 0;
   int  maxSession = 1;
   int  i;

   memset(d, 0, sizeof(*d));
   dirname_of(cuePath, dir, sizeof(dir));

   cue = fopen(cuePath, "r");
   if (!cue) {
      ERR("cannot open cue sheet: %s\n", cuePath);
      return -1;
   }

   while (fgets(line, sizeof(line), cue)) {
      char *t = trim(line);
      if (!t[0] || t[0] == ';')
         continue;

      if (istrncmp(t, "FILE", 4) == 0) {
         char *q1 = strchr(t, '"');
         char *q2 = q1 ? strchr(q1 + 1, '"') : NULL;
         if (!q1 || !q2) {
            ERR("unquoted FILE line in cue: %s\n", t);
            fclose(cue);
            return -1;
         }
         *q2 = '\0';
         if (dir[0])
            snprintf(curBin, sizeof(curBin), "%s%s", dir, q1 + 1);
         else
            snprintf(curBin, sizeof(curBin), "%s", q1 + 1);
         fileCount++;
      }
      else if (istrncmp(t, "TRACK", 5) == 0) {
         char *tok = t + 5;
         char typeStr[64] = { 0 };
         int trackNum, ti = 0;

         while (*tok && isspace((unsigned char)*tok)) tok++;
         trackNum = atoi(tok);
         while (*tok && !isspace((unsigned char)*tok)) tok++;
         while (*tok && isspace((unsigned char)*tok)) tok++;
         while (*tok && !isspace((unsigned char)*tok) && ti < 63)
            typeStr[ti++] = *tok++;
         typeStr[ti] = '\0';

         if (trackNum < 1 || trackNum > MAX_TRACKS) {
            ERR("track number %d out of range\n", trackNum);
            fclose(cue);
            return -1;
         }
         if (d->numTracks >= MAX_TRACKS) {
            ERR("too many tracks\n");
            fclose(cue);
            return -1;
         }
         if (!curBin[0]) {
            ERR("TRACK %02d appears before any FILE line\n", trackNum);
            fclose(cue);
            return -1;
         }

         curTrack = d->numTracks++;
         d->tracks[curTrack].number = trackNum;
         d->tracks[curTrack].session = curSession;
         d->tracks[curTrack].idx0 = -1;
         d->tracks[curTrack].idx1 = -1;
         d->tracks[curTrack].sectorSize = 2352;
         snprintf(d->tracks[curTrack].binPath,
                  sizeof(d->tracks[curTrack].binPath), "%s", curBin);

         if (istrncmp(typeStr, "AUDIO", 5) == 0 && typeStr[5] == '\0')
            d->tracks[curTrack].mode = 0;
         else if (istrncmp(typeStr, "MODE1", 5) == 0) {
            d->tracks[curTrack].mode = 1;
            if (strchr(typeStr, '/'))
               d->tracks[curTrack].sectorSize =
                  (uint32_t)atoi(strchr(typeStr, '/') + 1);
         }
         else if (istrncmp(typeStr, "MODE2", 5) == 0) {
            d->tracks[curTrack].mode = 2;
            if (strchr(typeStr, '/'))
               d->tracks[curTrack].sectorSize =
                  (uint32_t)atoi(strchr(typeStr, '/') + 1);
         }
         else {
            /* cdintf.c treats unknown types as AUDIO; do the same, loudly */
            WARN("track %02d: unknown type '%s', treating as AUDIO/2352\n",
                 trackNum, typeStr);
            d->tracks[curTrack].mode = 0;
         }
         if (d->tracks[curTrack].sectorSize != 2048 &&
             d->tracks[curTrack].sectorSize != 2336 &&
             d->tracks[curTrack].sectorSize != 2352) {
            ERR("track %02d: unsupported sector size %u (need 2048/2336/2352)\n",
                trackNum, d->tracks[curTrack].sectorSize);
            fclose(cue);
            return -1;
         }
         if (curSession > maxSession)
            maxSession = curSession;
      }
      else if (istrncmp(t, "INDEX", 5) == 0 && curTrack >= 0) {
         char *tok = t + 5;
         int idxNum, mm = 0, ss = 0, ff = 0;
         while (*tok && isspace((unsigned char)*tok)) tok++;
         idxNum = atoi(tok);
         while (*tok && !isspace((unsigned char)*tok)) tok++;
         while (*tok && isspace((unsigned char)*tok)) tok++;
         if (sscanf(tok, "%d:%d:%d", &mm, &ss, &ff) == 3) {
            long long lba = ((long long)mm * 60 + ss) * 75 + ff;
            if (idxNum == 0)
               d->tracks[curTrack].idx0 = lba;
            else if (idxNum == 1)
               d->tracks[curTrack].idx1 = lba;
            /* higher indices (sub-indexes) carry no layout info: ignore */
         }
      }
      else if (istrncmp(t, "REM", 3) == 0) {
         char *tok = t + 3;
         while (*tok && isspace((unsigned char)*tok)) tok++;
         if (istrncmp(tok, "SESSION", 7) == 0) {
            tok += 7;
            while (*tok && isspace((unsigned char)*tok)) tok++;
            curSession = atoi(tok);
            if (curSession < 1)
               curSession = 1;
            if (curSession > MAX_SESSIONS)
               curSession = MAX_SESSIONS;
            if (curSession > maxSession)
               maxSession = curSession;
         }
      }
      else if (istrncmp(t, "PREGAP", 6) == 0 || istrncmp(t, "POSTGAP", 7) == 0) {
         /* Virtual gap with no data in the BIN. cdintf.c ignores these too;
          * emitting them would desync payload byte-compare, so we punt. */
         WARN("ignoring '%s' (virtual gap not present in BIN data)\n", t);
      }
      /* FLAGS / CATALOG / ISRC / PERFORMER / TITLE / other REM: no layout info */
   }

   fclose(cue);

   if (d->numTracks == 0) {
      ERR("no tracks found in cue sheet\n");
      return -1;
   }

   for (i = 0; i < d->numTracks; i++) {
      if (d->tracks[i].idx1 < 0) {
         ERR("track %02d has no INDEX 01\n", d->tracks[i].number);
         return -1;
      }
      if (i > 0 && d->tracks[i].session < d->tracks[i - 1].session) {
         ERR("track %02d: session numbers must be non-decreasing\n",
             d->tracks[i].number);
         return -1;
      }
   }

   d->multiFile = (fileCount > 1);
   d->numSessions = maxSession;
   return 0;
}

/* Compute disc-absolute layout, mirroring cdintf.c ParseCueSheet:
 * - multi-file CUE: each FILE is one track region; disc LBAs accumulate
 *   file sizes with an INTER_SESSION_GAP inserted at each session crossing
 *   (cdintf.c lines 314-363).
 * - single-file CUE: INDEX LBAs are already disc-absolute; track regions
 *   are contiguous slices of the BIN (cdintf.c lines 364-391). No gap is
 *   inserted (the parser doesn't either — the BIN has no gap data). */
static int compute_layout(Disc *d)
{
   int i;

   if (d->multiFile) {
      uint32_t discLBA = 0;
      int prevSession = 0;
      for (i = 0; i < d->numTracks; i++) {
         Track *t = &d->tracks[i];
         long long sz = file_size(t->binPath);
         if (sz < 0) {
            ERR("cannot open BIN: %s\n", t->binPath);
            return -1;
         }
         if (sz % t->sectorSize)
            WARN("track %02d: BIN size %lld not a multiple of %u\n",
                 t->number, sz, t->sectorSize);
         if (prevSession != 0 && t->session > prevSession)
            discLBA += INTER_SESSION_GAP;
         prevSession = t->session;

         t->regionSectors  = (uint32_t)(sz / t->sectorSize);
         t->startLBA       = discLBA;
         t->pregapLen      = (uint32_t)t->idx1;   /* file-relative INDEX 01 */
         t->fileByteOffset = 0;
         if (t->pregapLen > t->regionSectors) {
            ERR("track %02d: INDEX 01 beyond end of BIN\n", t->number);
            return -1;
         }
         discLBA += t->regionSectors;
      }
   } else {
      long long sz = file_size(d->tracks[0].binPath);
      uint32_t totalSectors;
      if (sz < 0) {
         ERR("cannot open BIN: %s\n", d->tracks[0].binPath);
         return -1;
      }
      for (i = 1; i < d->numTracks; i++) {
         if (d->tracks[i].sectorSize != d->tracks[0].sectorSize) {
            ERR("single-file CUE with mixed sector sizes is not supported\n");
            return -1;
         }
      }
      totalSectors = (uint32_t)(sz / d->tracks[0].sectorSize);
      for (i = 0; i < d->numTracks; i++) {
         Track *t = &d->tracks[i];
         long long first = (t->idx0 >= 0) ? t->idx0 : t->idx1;
         long long end;
         if (i + 1 < d->numTracks) {
            Track *n = &d->tracks[i + 1];
            end = (n->idx0 >= 0) ? n->idx0 : n->idx1;
         } else {
            end = totalSectors;
         }
         if (end < first || first < 0 || end > totalSectors) {
            ERR("track %02d: bad INDEX ordering in single-file CUE\n",
                t->number);
            return -1;
         }
         t->startLBA       = (uint32_t)first;
         t->pregapLen      = (uint32_t)(t->idx1 - first);
         t->regionSectors  = (uint32_t)(end - first);
         t->fileByteOffset = (uint64_t)first * t->sectorSize;
      }
   }
   return 0;
}

/* -------------------------------------------------------------------------
 * CDI writing
 * ------------------------------------------------------------------------- */

static int copy_bytes(FILE *dst, const char *srcPath, uint64_t offset,
                      uint64_t count)
{
   FILE *src = fopen(srcPath, "rb");
   static uint8_t buf[1 << 20];
   if (!src) {
      ERR("cannot open BIN: %s\n", srcPath);
      return -1;
   }
   if (fseeko(src, (off_t)offset, SEEK_SET) != 0) {
      ERR("seek failed in %s\n", srcPath);
      fclose(src);
      return -1;
   }
   while (count) {
      size_t want = count > sizeof(buf) ? sizeof(buf) : (size_t)count;
      size_t got = fread(buf, 1, want, src);
      if (got == 0) {
         ERR("short read from %s\n", srcPath);
         fclose(src);
         return -1;
      }
      if (fwrite(buf, 1, got, dst) != got) {
         ERR("write failed (disk full?)\n");
         fclose(src);
         return -1;
      }
      count -= got;
   }
   fclose(src);
   return 0;
}

/* Emit one per-track header entry in the canonical DJ 3.00.780+ layout
 * shared by cdirip CDI_read_track(), DreamShell cdi_open() and libmirage
 * mirage_parser_cdi_load_track().
 *
 * isFirst: first track of its session. The reader's per-track prelude
 * ("read u32; if != 0 skip 8") consumes a 4-byte zero for the session's
 * first track; for later tracks it consumes the 12 inter-track bytes that
 * write_cdi() emits between entries (as genuine images do). */
static void write_track_header(FILE *f, const Track *t, int isFirst,
                               int totalTracks, int sessIdx0, int trkIdx0)
{
   uint8_t blk[0x57];
   uint8_t tail[87];
   uint8_t pre[19];
   const char *fname = basename_of(t->binPath);
   size_t fnameLen = strlen(fname);
   uint32_t dataLen = t->regionSectors - t->pregapLen;
   uint32_t code = sector_code_from_size(t->sectorSize);
   uint32_t ctl = (t->mode == 0) ? 0 : 4;    /* CTL: data bit for data tracks */

   if (fnameLen > 255)
      fnameLen = 255;

   if (isFirst)
      put_le32(f, 0);                        /* prelude u32 == 0: no skip */

   fwrite(cdi_track_start_marker, 1, 20, f); /* two 10-byte start marks */
   put_zeros(f, 3);
   fputc(totalTracks & 0xFF, f);             /* total tracks (libmirage hdr[15]) */
   fputc((int)fnameLen, f);                  /* filename length */
   fwrite(fname, 1, fnameLen, f);            /* filename */

   /* 19 bytes after filename; genuine images have 0x02 at byte 11 */
   memset(pre, 0, sizeof(pre));
   pre[11] = 0x02;
   fwrite(pre, 1, sizeof(pre), f);

   put_le32(f, 0x80000000u);                 /* DJ 3.00.780+ format tag */
   put_le32(f, CDI_DISC_CAPACITY);           /* disc capacity in sectors */
   put_zeros(f, 2);
   put_le16(f, CDI_MEDIUM_CD);               /* medium type */
   put_le16(f, 2);                           /* index entries: pregap + data */

   /* 0x57-byte track data block, canonical packed layout */
   memset(blk, 0, sizeof(blk));
   store_le32(blk + 0x00, t->pregapLen);     /* index 0 length */
   store_le32(blk + 0x04, dataLen);          /* index 1 length */
   /* +0x08 CD-TEXT block count (0), +0x0c u16 (0) */
   store_le32(blk + 0x0e, (uint32_t)t->mode);
   /* +0x12 unknown (0) */
   store_le32(blk + 0x16, (uint32_t)sessIdx0);   /* session index, 0-based */
   store_le32(blk + 0x1a, (uint32_t)trkIdx0);    /* track index, 0-based */
   store_le32(blk + 0x1e, t->startLBA);
   store_le32(blk + 0x22, t->regionSectors);     /* total length */
   /* +0x26 16 unknown bytes (0) */
   store_le32(blk + 0x36, code);                 /* read mode / sector size */
   store_le32(blk + 0x3a, ctl);                  /* track CTL */
   /* +0x3e 0, +0x3f repeated total length, +0x43 4 zeros */
   store_le32(blk + 0x3f, t->regionSectors);
   /* +0x47 ISRC (12 bytes, zero) + 0x53 ISRC-valid (0) */
   fwrite(blk, 1, sizeof(blk), f);

   /* 87-byte extension tail: readers skip 5, hit the 0xFFFFFFFF marker at
    * [5..8], then skip the 78-byte DJ 3.00.780+ extension. Filler bytes as
    * documented by libmirage ([25..26] = 0xAC44 = 44100 Hz). */
   memset(tail, 0, sizeof(tail));
   memset(tail + 1, 0xFF, 8);
   tail[9]  = 0x01;
   tail[13] = 0x80;
   tail[17] = 0x02;
   tail[21] = 0x10;
   tail[25] = 0x44;
   tail[26] = 0xAC;
   tail[71] = 0xFF;
   tail[72] = 0xFF;
   tail[73] = 0xFF;
   tail[74] = 0xFF;
   fwrite(tail, 1, sizeof(tail), f);
}

static int write_cdi(const Disc *d, const char *outPath)
{
   FILE *out = fopen(outPath, "wb");
   off_t headerOffset;
   int s, i;

   if (!out) {
      ERR("cannot create output: %s\n", outPath);
      return -1;
   }

   /* 1. Track payloads, in disc order, at native sector size */
   for (i = 0; i < d->numTracks; i++) {
      const Track *t = &d->tracks[i];
      uint64_t bytes = (uint64_t)t->regionSectors * t->sectorSize;
      INFO("  track %02d: %u sectors x %u bytes from %s\n",
           t->number, t->regionSectors, t->sectorSize,
           basename_of(t->binPath));
      if (copy_bytes(out, t->binPath, t->fileByteOffset, bytes) != 0) {
         fclose(out);
         remove(outPath);
         return -1;
      }
   }

   /* 2. Header table (canonical cdirip/DreamShell walk).
    * Inter-track bytes: the reader's per-track "read u32; if != 0 skip 8"
    * prelude; genuine images carry {01 00 00 00 | 00 00 01 00 00 00 FF FF}
    * between consecutive tracks of a session.
    * Per-session trailer: 12 bytes + 1 (non-V2); last four are 00 00 FF FF
    * in genuine images. */
   {
      static const uint8_t interTrack[12] =
         { 0x01,0x00,0x00,0x00, 0x00,0x00,0x01,0x00, 0x00,0x00,0xFF,0xFF };
      static const uint8_t sessTail[13] =
         { 0x00,0x00,0x00,0x00, 0x00,0x00,0x00,0x00, 0x00,0x00,0xFF,0xFF,
           0x00 };
      /* Empty end-of-disc session descriptor present in genuine images
       * (never read by the track walk) */
      static const uint8_t endDesc[14] =
         { 0x00,0x00, 0x00,0x00,0x00,0x00,0x00,0x00, 0x01, 0x00,0x00,0x00,
           0xFF,0xFF };
      int trkIdx0 = 0;

      headerOffset = ftello(out);
      put_le16(out, (uint16_t)d->numSessions);
      for (s = 1; s <= d->numSessions; s++) {
         uint16_t n = 0, k = 0;
         for (i = 0; i < d->numTracks; i++)
            if (d->tracks[i].session == s)
               n++;
         put_le16(out, n);
         for (i = 0; i < d->numTracks; i++) {
            if (d->tracks[i].session != s)
               continue;
            write_track_header(out, &d->tracks[i], k == 0,
                               d->numTracks, s - 1, trkIdx0);
            k++;
            trkIdx0++;
            if (k < n)
               fwrite(interTrack, 1, sizeof(interTrack), out);
         }
         fwrite(sessTail, 1, sizeof(sessTail), out);
      }
      fwrite(endDesc, 1, sizeof(endDesc), out);
   }

   /* 3. Trailer: version id + absolute header offset (V2/V3 semantics) */
   put_le32(out, CDI_V3_ID);
   put_le32(out, (uint32_t)headerOffset);

   if (ferror(out)) {
      ERR("write error on %s\n", outPath);
      fclose(out);
      remove(outPath);
      return -1;
   }
   fclose(out);
   return 0;
}

/* -------------------------------------------------------------------------
 * Verification: re-parse the CDI with the canonical cdirip/DreamShell walk
 * and byte-compare every track's payload against the source BIN region.
 * ------------------------------------------------------------------------- */

typedef struct {
   int      session;
   int      mode;
   uint32_t sectorSize;     /* effective, incl. parser's 2352 fallback */
   uint32_t startLBA;
   uint32_t dataLBA;
   uint32_t lengthLBA;
   uint64_t fileOffset;
} CdiTrack;

/* Canonical CDI track walk (cdirip cdi.c CDI_read_track / DreamShell
 * cdi_open). Returns track count, or -1. NOTE: src/cd/cdintf.c ParseCDI
 * deviates from this — it reads mode/start_lba/total_length/sector-size
 * 2 bytes late and a 0x70-byte block instead of 0x57. */
static int cdi_walk(FILE *f, CdiTrack *out, int maxTracks)
{
   uint8_t trailer[8], buf2[2];
   uint32_t version, headerOffset;
   off_t fileSize;
   uint16_t sessionCount;
   int s, trackCount = 0;
   uint64_t cdiByteOffset = 0;
   uint32_t discLBA = 0;

   if (fseeko(f, 0, SEEK_END) != 0)
      return -1;
   fileSize = ftello(f);
   if (fileSize < 8)
      return -1;
   if (fseeko(f, fileSize - 8, SEEK_SET) != 0)
      return -1;
   if (fread(trailer, 1, 8, f) != 8)
      return -1;
   version      = get_le32(trailer);
   headerOffset = get_le32(trailer + 4);
   if (version != CDI_V2_ID && version != CDI_V3_ID && version != CDI_V35_ID)
      return -1;

   if (version == CDI_V35_ID)
      fseeko(f, fileSize - (off_t)headerOffset, SEEK_SET);
   else
      fseeko(f, (off_t)headerOffset, SEEK_SET);

   if (fread(buf2, 1, 2, f) != 2)
      return -1;
   sessionCount = (uint16_t)(buf2[0] | (buf2[1] << 8));

   for (s = 0; s < sessionCount; s++) {
      uint16_t sessTrackCount;
      int t;
      if (fread(buf2, 1, 2, f) != 2)
         return -1;
      sessTrackCount = (uint16_t)(buf2[0] | (buf2[1] << 8));

      for (t = 0; t < sessTrackCount; t++) {
         uint8_t newFmt[4], marker[20], fnameLen;
         uint8_t trkData[0x57];
         uint32_t newFmtVal, pregapLen, length, mode, startLba;
         uint32_t totalLength, sectorCode, sectorSize;

         if (trackCount >= maxTracks)
            return -1;

         if (fread(newFmt, 1, 4, f) != 4)
            return -1;
         newFmtVal = get_le32(newFmt);
         if (newFmtVal != 0)
            fseeko(f, 8, SEEK_CUR);

         if (fread(marker, 1, 20, f) != 20)
            return -1;
         if (memcmp(marker, cdi_track_start_marker, 20) != 0)
            return -1;

         fseeko(f, 4, SEEK_CUR);
         if (fread(&fnameLen, 1, 1, f) != 1)
            return -1;
         fseeko(f, fnameLen, SEEK_CUR);
         fseeko(f, 19, SEEK_CUR);

         if (fread(newFmt, 1, 4, f) != 4)
            return -1;
         newFmtVal = get_le32(newFmt);
         if (newFmtVal == 0x80000000u)
            fseeko(f, 10, SEEK_CUR);
         else
            fseeko(f, 2, SEEK_CUR);

         if (fread(trkData, 1, 0x57, f) != 0x57)
            return -1;
         pregapLen   = get_le32(trkData + 0x00);
         length      = get_le32(trkData + 0x04);
         mode        = get_le32(trkData + 0x0e);
         startLba    = get_le32(trkData + 0x1e);
         totalLength = get_le32(trkData + 0x22);
         sectorCode  = get_le32(trkData + 0x36);

         sectorSize = cdi_effective_sector_size(mode, sectorCode);

         if (version != CDI_V2_ID) {
            uint8_t extMarker[4];
            fseeko(f, 5, SEEK_CUR);
            if (fread(extMarker, 1, 4, f) == 4) {
               if (get_le32(extMarker) == 0xFFFFFFFFu)
                  fseeko(f, 78, SEEK_CUR);
            }
         }

         out[trackCount].session    = s + 1;
         out[trackCount].mode       = (int)mode;
         out[trackCount].sectorSize = sectorSize;
         out[trackCount].startLBA   = (startLba != 0) ? startLba : discLBA;
         out[trackCount].dataLBA    = out[trackCount].startLBA + pregapLen;
         out[trackCount].lengthLBA  = totalLength ? totalLength
                                                  : (pregapLen + length);
         out[trackCount].fileOffset = cdiByteOffset;

         cdiByteOffset += (uint64_t)out[trackCount].lengthLBA * sectorSize;
         discLBA = out[trackCount].startLBA + out[trackCount].lengthLBA;
         trackCount++;
      }

      fseeko(f, 12, SEEK_CUR);
      if (version != CDI_V2_ID)
         fseeko(f, 1, SEEK_CUR);
   }

   return trackCount;
}

static int compare_payload(FILE *cdi, uint64_t cdiOffset, const char *binPath,
                           uint64_t binOffset, uint64_t count)
{
   static uint8_t a[1 << 20], b[1 << 20];
   FILE *bin = fopen(binPath, "rb");
   int result = 0;
   if (!bin)
      return -1;
   if (fseeko(cdi, (off_t)cdiOffset, SEEK_SET) != 0 ||
       fseeko(bin, (off_t)binOffset, SEEK_SET) != 0) {
      fclose(bin);
      return -1;
   }
   while (count) {
      size_t want = count > sizeof(a) ? sizeof(a) : (size_t)count;
      size_t ga = fread(a, 1, want, cdi);
      size_t gb = fread(b, 1, want, bin);
      if (ga != want || gb != want) {
         result = -1;
         break;
      }
      if (memcmp(a, b, want) != 0) {
         result = -1;
         break;
      }
      count -= want;
   }
   fclose(bin);
   return result;
}

static int verify_cdi(const Disc *d, const char *cdiPath)
{
   FILE *f = fopen(cdiPath, "rb");
   CdiTrack got[MAX_TRACKS];
   int n, i, failures = 0;

   if (!f) {
      ERR("cannot reopen %s for verification\n", cdiPath);
      return -1;
   }

   n = cdi_walk(f, got, MAX_TRACKS);
   if (n < 0) {
      ERR("produced CDI failed the cdintf.c parser walk\n");
      fclose(f);
      return -1;
   }
   if (n != d->numTracks) {
      ERR("parser saw %d tracks, source has %d\n", n, d->numTracks);
      fclose(f);
      return -1;
   }

   printf("sess  trk  type   secsz  startLBA  dataLBA   length    payload\n");
   printf("----  ---  -----  -----  --------  --------  --------  -------\n");
   for (i = 0; i < d->numTracks; i++) {
      const Track *src = &d->tracks[i];
      const CdiTrack *t = &got[i];
      int ok = 1;
      const char *why = "OK";

      if (t->session != src->session ||
          t->mode != src->mode ||
          t->sectorSize != src->sectorSize ||
          t->startLBA != src->startLBA ||
          t->dataLBA != src->startLBA + src->pregapLen ||
          t->lengthLBA != src->regionSectors) {
         ok = 0;
         why = "FAIL (metadata)";
      }
      else if (compare_payload(f, t->fileOffset, src->binPath,
                               src->fileByteOffset,
                               (uint64_t)t->lengthLBA * t->sectorSize) != 0) {
         ok = 0;
         why = "FAIL (bytes)";
      }

      printf("%-4d  %-3d  %-5s  %-5u  %-8u  %-8u  %-8u  %s\n",
             t->session, src->number, mode_name(t->mode), t->sectorSize,
             t->startLBA, t->dataLBA, t->lengthLBA, why);
      if (!ok)
         failures++;
   }

   fclose(f);
   if (failures) {
      ERR("%d track(s) failed verification\n", failures);
      return -1;
   }
   return 0;
}

/* -------------------------------------------------------------------------
 * Conversion driver (shared by single-file and batch modes)
 * ------------------------------------------------------------------------- */

/* Default output path: input with the extension replaced by .cdi
 * (same logic the single-file mode has always used). */
static void derive_out_path(const char *inPath, char *outPath, size_t size)
{
   const char *ext = strrchr(inPath, '.');
   size_t stem = ext ? (size_t)(ext - inPath) : strlen(inPath);
   if (stem > size - 5)
      stem = size - 5;
   memcpy(outPath, inPath, stem);
   strcpy(outPath + stem, ".cdi");
}

/* Layout + write + optional verify on an already-parsed Disc.
 * Returns 0 = OK, 1 = conversion failed, 2 = verify failed. */
static int convert_parsed(Disc *d, const char *inPath, const char *outPath,
                          int doVerify)
{
   if (compute_layout(d) != 0)
      return 1;

   INFO("%s: %d track(s), %d session(s), %s\n", basename_of(inPath),
        d->numTracks, d->numSessions,
        d->multiFile ? "multi-file BIN" : "single-file BIN");
   if (d->numSessions > 2)
      WARN("more than 2 sessions: this core's parser clamps numSessions to 2\n");
   if (d->numSessions == 1)
      INFO("note: single session — a bootable Jaguar CD needs 2 sessions\n");

   INFO("writing %s\n", outPath);
   if (write_cdi(d, outPath) != 0)
      return 1;

   if (doVerify) {
      if (verify_cdi(d, outPath) != 0)
         return 2;
      INFO("verify: all %d track(s) OK\n", d->numTracks);
   }

   INFO("done: %s\n", outPath);
   return 0;
}

/* -------------------------------------------------------------------------
 * Batch mode: recursively convert every *.cue under a directory
 * ------------------------------------------------------------------------- */

typedef struct {
   char **paths;
   int    count;
   int    cap;
} PathList;

static int pathlist_add(PathList *pl, const char *path)
{
   char *copy;
   if (pl->count == pl->cap) {
      int ncap = pl->cap ? pl->cap * 2 : 64;
      char **np = (char **)realloc(pl->paths, (size_t)ncap * sizeof(char *));
      if (!np)
         return -1;
      pl->paths = np;
      pl->cap = ncap;
   }
   copy = (char *)malloc(strlen(path) + 1);
   if (!copy)
      return -1;
   strcpy(copy, path);
   pl->paths[pl->count++] = copy;
   return 0;
}

static int cmp_paths(const void *a, const void *b)
{
   return strcmp(*(char *const *)a, *(char *const *)b);
}

/* Recursive walk: collect every regular *.cue (case-insensitive) under
 * dirPath. Hidden entries (leading '.', which also covers "."/"..") and
 * symlinks are skipped; subdirectories are followed. Unreadable
 * directories warn and are skipped. Returns -1 only on OOM. */
static int collect_cues(const char *dirPath, PathList *out)
{
   DIR *dp = opendir(dirPath);
   struct dirent *de;
   struct stat st;
   char full[PATH_MAX_LEN];
   int ret = 0;

   if (!dp) {
      WARN("cannot open directory: %s\n", dirPath);
      return 0;
   }
   while ((de = readdir(dp)) != NULL) {
      if (de->d_name[0] == '.')
         continue;
      snprintf(full, sizeof(full), "%s/%s", dirPath, de->d_name);
      if (lstat(full, &st) != 0)
         continue;
      if (S_ISLNK(st.st_mode))
         continue;
      if (S_ISDIR(st.st_mode)) {
         if (collect_cues(full, out) != 0)
            ret = -1;
      } else if (S_ISREG(st.st_mode)) {
         const char *ext = strrchr(de->d_name, '.');
         if (ext && istrncmp(ext, ".cue", 5) == 0)
            if (pathlist_add(out, full) != 0)
               ret = -1;
      }
   }
   closedir(dp);
   return ret;
}

/* 1 = target CDI exists and is strictly newer than the CUE and every
 * referenced BIN (safe to skip); 0 = needs (re)conversion. */
static int cdi_up_to_date(const char *cdiPath, const char *cuePath,
                          const Disc *d)
{
   struct stat stCdi, stSrc;
   int i;
   if (stat(cdiPath, &stCdi) != 0)
      return 0;
   if (stat(cuePath, &stSrc) != 0 || stSrc.st_mtime >= stCdi.st_mtime)
      return 0;
   for (i = 0; i < d->numTracks; i++) {
      if (stat(d->tracks[i].binPath, &stSrc) != 0 ||
          stSrc.st_mtime >= stCdi.st_mtime)
         return 0;
   }
   return 1;
}

static int batch_convert(const char *rootDir, int doVerify, int doForce)
{
   PathList pl = { NULL, 0, 0 };
   int i, nConverted = 0, nSkipped = 0, nFailed = 0;

   if (collect_cues(rootDir, &pl) != 0) {
      ERR("out of memory while scanning %s\n", rootDir);
      return 1;
   }
   if (pl.count == 0) {
      ERR("no .cue files found under %s\n", rootDir);
      return 1;
   }
   qsort(pl.paths, (size_t)pl.count, sizeof(char *), cmp_paths);

   /* The per-file progress lines are the batch output; silence the
    * inner per-track chatter (the --verify table still prints). */
   g_quiet = 1;

   for (i = 0; i < pl.count; i++) {
      const char *cue = pl.paths[i];
      char outPath[PATH_MAX_LEN];
      Disc d;
      int parsed, rc;

      derive_out_path(cue, outPath, sizeof(outPath));
      parsed = (parse_cue(cue, &d) == 0);

      if (parsed && !doForce && cdi_up_to_date(outPath, cue, &d)) {
         printf("[%d/%d] Skipping: %s (CDI up to date; --force to reconvert)\n",
                i + 1, pl.count, cue);
         nSkipped++;
         continue;
      }

      printf("[%d/%d] Converting: %s%s", i + 1, pl.count, cue,
             doVerify ? "\n" : " ... ");
      fflush(stdout);
      if (!parsed) {
         printf("%sFAILED\n", doVerify ? "  -> " : "");
         nFailed++;
         continue;
      }
      rc = convert_parsed(&d, cue, outPath, doVerify);
      if (rc == 0) {
         printf("%sOK (%d track%s, %d session%s)\n",
                doVerify ? "  -> " : "",
                d.numTracks, d.numTracks == 1 ? "" : "s",
                d.numSessions, d.numSessions == 1 ? "" : "s");
         nConverted++;
      } else {
         printf("%sFAILED%s\n", doVerify ? "  -> " : "",
                rc == 2 ? " (verify)" : "");
         nFailed++;
      }
   }

   printf("converted %d, skipped %d, failed %d\n",
          nConverted, nSkipped, nFailed);

   for (i = 0; i < pl.count; i++)
      free(pl.paths[i]);
   free(pl.paths);

   return nFailed ? 1 : 0;
}

/* -------------------------------------------------------------------------
 * main
 * ------------------------------------------------------------------------- */

static void usage(FILE *to)
{
   fprintf(to,
      "usage: cue2cdi input.cue [output.cdi] [--verify] [--quiet] [--version]\n"
      "       cue2cdi [--batch] DIR [--verify] [--force] [--quiet]\n"
      "\n"
      "Converts a (possibly multi-session) CUE/BIN image to DiscJuggler V3\n"
      ".cdi, preserving session structure and track types. Default output is\n"
      "the input path with the extension replaced by .cdi.\n"
      "\n"
      "With --batch (implied when the input is a directory), recursively\n"
      "finds every *.cue under DIR and converts each in place to\n"
      "<same-dir>/<same-basename>.cdi. Hidden directories and symlinks are\n"
      "skipped. Targets newer than their CUE + BIN(s) are skipped; failures\n"
      "are reported and the queue continues (exit 1 if any file failed).\n"
      "\n"
      "  --verify   re-parse the produced CDI (canonical cdirip walk) and\n"
      "             byte-compare every track payload against the BIN(s)\n"
      "  --batch    treat the input as a directory tree of CUE/BIN sets\n"
      "  --force    batch mode: reconvert even if the CDI is up to date\n"
      "  --quiet    suppress progress output\n"
      "  --version  print version and exit\n"
      "\n"
      "Plain .iso input is NOT supported: an ISO carries no track/session\n"
      "structure, and Jaguar CD boot requires the session-2 layout.\n");
}

int main(int argc, char **argv)
{
   const char *inPath = NULL;
   const char *outArg = NULL;
   char outPath[PATH_MAX_LEN];
   int doVerify = 0;
   int doBatch = 0;
   int doForce = 0;
   int isDir;
   struct stat st;
   int i;
   Disc d;

   for (i = 1; i < argc; i++) {
      if (strcmp(argv[i], "--verify") == 0)
         doVerify = 1;
      else if (strcmp(argv[i], "--batch") == 0)
         doBatch = 1;
      else if (strcmp(argv[i], "--force") == 0)
         doForce = 1;
      else if (strcmp(argv[i], "--quiet") == 0)
         g_quiet = 1;
      else if (strcmp(argv[i], "--version") == 0) {
         printf("cue2cdi %s\n", CUE2CDI_VERSION);
         return 0;
      }
      else if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
         usage(stdout);
         return 0;
      }
      else if (argv[i][0] == '-') {
         ERR("unknown option: %s\n", argv[i]);
         usage(stderr);
         return 1;
      }
      else if (!inPath)
         inPath = argv[i];
      else if (!outArg)
         outArg = argv[i];
      else {
         ERR("too many arguments\n");
         usage(stderr);
         return 1;
      }
   }

   if (!inPath) {
      usage(stderr);
      return 1;
   }

   /* Batch mode: explicit --batch, or a directory as the input path */
   isDir = (stat(inPath, &st) == 0 && S_ISDIR(st.st_mode));
   if (doBatch || isDir) {
      if (!isDir) {
         ERR("--batch requires a directory: %s\n", inPath);
         return 1;
      }
      if (outArg) {
         ERR("batch mode takes a single directory argument\n");
         return 1;
      }
      return batch_convert(inPath, doVerify, doForce);
   }

   {
      const char *ext = strrchr(inPath, '.');
      if (ext && istrncmp(ext, ".iso", 5) == 0) {
         ERR(".iso input is not supported: a plain ISO has no track or\n"
             "       session structure (Jaguar discs need the session-1 audio\n"
             "       + session-2 data layout). Use the CUE/BIN dump instead.\n");
         return 1;
      }
   }

   if (outArg)
      snprintf(outPath, sizeof(outPath), "%s", outArg);
   else
      derive_out_path(inPath, outPath, sizeof(outPath));

   if (parse_cue(inPath, &d) != 0)
      return 1;
   return convert_parsed(&d, inPath, outPath, doVerify);
}
