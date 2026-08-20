//
// CDINTF.H: OS agnostic CDROM access functions
//
// by James L. Hammons
// CD image support added for Jaguar CD emulation
//

#ifndef __CDINTF_H__
#define __CDINTF_H__

#include <stdint.h>
#include <boolean.h>

#ifdef __cplusplus
extern "C" {
#endif

// Maximum tracks per disc
#define CDINTF_MAX_TRACKS 99
#define CDINTF_MAX_SESSIONS 2

// Track type
enum CDIntfTrackType {
   CDINTF_TRACK_AUDIO = 0,
   CDINTF_TRACK_MODE1,
   CDINTF_TRACK_MODE2
};

// Track info structure
struct CDIntfTrack {
   uint32_t number;              // Track number (1-based)
   uint32_t session;             // Session number (1-based)
   enum CDIntfTrackType type;    // Track type
   uint32_t startLBA;            // Start LBA (disc-absolute, includes pregap)
   uint32_t dataLBA;             // Data LBA (disc-absolute INDEX 01 position, for TOC)
   uint32_t lengthLBA;           // Length in sectors (entire file)
   uint32_t fileOffset;          // Byte offset into this track's BIN file
   uint32_t sectorSize;          // Sector size in bytes (usually 2352)
   uint8_t startM, startS, startF; // Start MSF (of INDEX 01 / data start)
   /* Byte displacement of this track's stored content relative to the
    * descriptor layout (CDI V2 rip-jitter repair): the byte that belongs at
    * descriptor position P actually sits at P + dataShift in the file, so
    * reads add dataShift.  0 for conformant images and for all CUE tracks. */
   int32_t dataShift;
   /* CDI V2 head-loss repair: the first bytes of this track's user data were
    * lost by the ripper and reads of the first data sector must overlay the
    * constant 98-byte Jaguar boot-header prefix (sync preamble + magic) over
    * the zeros that dataShift exposes there.  See CDIRepairV2BootTrack. */
   bool synthBootHeader;
   char binFilePath[4096];       // Path to this track's BIN file (multi-file CUE)
};

// Session info structure
struct CDIntfSession {
   uint32_t number;              // Session number (1-based)
   uint32_t firstTrack;          // First track number
   uint32_t lastTrack;           // Last track number
   uint32_t leadOutLBA;          // Lead-out LBA
   uint8_t leadOutM, leadOutS, leadOutF; // Lead-out MSF
};

// Disc info
struct CDIntfDisc {
   bool loaded;
   uint32_t numTracks;
   uint32_t numSessions;
   struct CDIntfTrack tracks[CDINTF_MAX_TRACKS];
   struct CDIntfSession sessions[CDINTF_MAX_SESSIONS];
   char binPath[4096];           // Path to BIN file
   void *binFile;                // File handle (RFILE*)
};

bool CDIntfInit(void);
void CDIntfDone(void);
bool CDIntfReadBlock(uint32_t sector, uint8_t * buffer);
uint32_t CDIntfGetNumSessions(void);
uint32_t CDIntfGetNumTracks(void);
void CDIntfSelectDrive(uint32_t driveNum);
uint32_t CDIntfGetCurrentDrive(void);
const uint8_t * CDIntfGetDriveName(uint32_t driveNum);
uint8_t CDIntfGetSessionInfo(uint32_t session, uint32_t offset);
uint8_t CDIntfGetTrackInfo(uint32_t track, uint32_t offset);
uint8_t CDIntfGetTrackDuration(uint32_t track, uint32_t offset);
uint8_t CDIntfGetTrackSession(uint32_t track);

// Returns true if the given disc-image LBA falls within a session 2 track
// (Jaguar CD game data is in session 2; session 1 is audio)
bool CDIntfIsSession2Sector(uint32_t sector);

/* Q-channel subcode position lookup (VLM / audio-CD, issue #291): for a
 * disc-image LBA, return the containing track's number, the Red Book
 * index (0 = INDEX 00 pregap, 1 = program area), the track-relative LBA
 * (distance to/from INDEX 01: counts DOWN to the index in the pregap,
 * UP from it in the program area) and whether the track is a data track
 * (Q CONTROL bit 2).  Returns false when the LBA falls in no track
 * (inter-session gap / past the last track). */
bool CDIntfGetQPosition(uint32_t lba, uint32_t *trackNum, uint32_t *idx,
                        uint32_t *relLBA, bool *isData);

// True if the most recent CDIntfReadBlock() returned synthesized silence:
// an inter-session / unowned LBA, or a CHD virtual pregap (PGTYPE starts
// with 'V').  Consumed by cdrom.c to instrument the auth-fail STOP path.
bool CDIntfLastReadWasVirtualPregap(void);
void CDIntfClearLastReadVirtualPregap(void);
// LBA targeted by the last virtual-pregap read (valid when the getter returns true).
uint32_t CDIntfLastVirtualPregapLBA(void);

uint32_t CDIntfGetDiscTotalSectors(void);
uint32_t CDIntfGetSession2GameDataLBA(void);
/* startLBA of the FIRST session-2 track (i.e. the boot-stub track).
 * Used by HLE CD_read as a sentinel-scan fallback: some games embed
 * their sync block right after the boot stub data in this same track. */
uint32_t CDIntfGetSession2FirstTrackLBA(void);
/* Number of session-2 tracks. */
uint32_t CDIntfGetSession2TrackCount(void);
/* startLBA (or dataLBA when present) of the i-th session-2 track. */
uint32_t CDIntfGetSession2TrackLBA(uint32_t which);

// New functions for disc image loading
bool CDIntfOpenImage(const char *cuePath);
void CDIntfCloseImage(void);
bool CDIntfIsImageLoaded(void);

/* Extract the game boot stub from the start of session 2.
 * Reads the first ~12 sectors of the first session-2 track, undoes the
 * I2S word-swap, validates the universal-header magic, and returns the
 * boot loader code bytes that should be written into main RAM at
 * *outLoadAddr (typically $00080000) — overwriting the CD Player UI
 * fallback before the BIOS issues `JSR $080000`.
 *
 * outBuf must be at least *outLength bytes; pass outBufSize as a guard.
 * Returns true on success. */
uint32_t CDIntfGetBootStubEndLBA(void);
bool CDIntfExtractBootStub(uint8_t *outBuf, uint32_t outBufSize,
                           uint32_t *outLoadAddr, uint32_t *outLength);

#ifdef __cplusplus
}
#endif

#endif	// __CDINTF_H__
