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
void CDIntfSelectDrive(uint32_t driveNum);
uint32_t CDIntfGetCurrentDrive(void);
const uint8_t * CDIntfGetDriveName(uint32_t driveNum);
uint8_t CDIntfGetSessionInfo(uint32_t session, uint32_t offset);
uint8_t CDIntfGetTrackInfo(uint32_t track, uint32_t offset);
uint8_t CDIntfGetTrackSession(uint32_t track);

// Returns true if the given disc-image LBA falls within a session 2 track
// (Jaguar CD game data is in session 2; session 1 is audio)
bool CDIntfIsSession2Sector(uint32_t sector);

// True if the most recent CDIntfReadBlock() landed in a virtual-pregap gap
// (a sector the CHD does not actually store — typically the BIOS's pregap
// authentication read).  Consumed by cdrom.c to instrument the auth-fail
// STOP path and identify the BIOS's auth branch.
bool CDIntfLastReadWasVirtualPregap(void);
void CDIntfClearLastReadVirtualPregap(void);
// LBA targeted by the last virtual-pregap read (valid when the getter returns true).
uint32_t CDIntfLastVirtualPregapLBA(void);

// New functions for disc image loading
bool CDIntfOpenImage(const char *cuePath);
void CDIntfCloseImage(void);
bool CDIntfIsImageLoaded(void);

#ifdef __cplusplus
}
#endif

#endif	// __CDINTF_H__
