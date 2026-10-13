/*
 * jaggd_fs.h -- JagGD SD-card file API against a host directory (#783)
 *
 * The GameDrive's SD card is a FAT volume driven through FatFs; the
 * GDBIOS file functions are thin wrappers over it (the open-mode bits and
 * the info struct are FatFs's FA_* values and FILINFO, see
 * docs/jgd-interface-notes.md).  This module answers those calls from a
 * folder on the host acting as the card.  It knows nothing about the 68K:
 * jaggd.c reads the call's registers, moves bytes in and out of emulated
 * memory, and calls in here with host buffers.
 *
 * Every path is resolved strictly under the root.  ".." components,
 * drive letters other than a leading "0:", control characters and (on
 * POSIX hosts) symlinks are refused -- the ROM is untrusted input.  FAT is
 * case-insensitive, so each component is matched case-insensitively
 * against what exists on the host.
 *
 * Return convention mirrors the GDBIOS: >= 0 success (or a handle),
 * -1 failure (all 32 bits set, so .w and .l sign tests both see it).
 */

#ifndef __JAGGD_FS_H__
#define __JAGGD_FS_H__

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define JGDFS_MAX_FILES   8
#define JGDFS_MAX_DIRS    4
#define JGDFS_PATH_MAX    256      /* bytes incl. NUL, card-relative */

/* Card access (core option virtualjaguar_jgd_sd). */
#define JGDFS_ACCESS_OFF        0
#define JGDFS_ACCESS_READONLY   1
#define JGDFS_ACCESS_READWRITE  2

/* GD_FOpen mode (FatFs FA_*). */
#define JGDFS_FOPEN_READ           0x01
#define JGDFS_FOPEN_WRITE          0x02
#define JGDFS_FOPEN_CREATE_NEW     0x04
#define JGDFS_FOPEN_CREATE_ALWAYS  0x08
#define JGDFS_FOPEN_OPEN_ALWAYS    0x10
#define JGDFS_FOPEN_OPEN_APPEND    0x30

/* GD_FSeek whence. */
#define JGDFS_SEEK_SET 0
#define JGDFS_SEEK_CUR 1
#define JGDFS_SEEK_END 2

/* CGDFileInfoShort / CGDFileInfoLong as the 68K sees them (big-endian,
 * no padding): u32 size @0, u16 date @4, u16 time @6, u8 attrib @8,
 * char altname[13] @9, then (long form only) char name[256] @22. */
#define JGDFS_INFO_SHORT_SIZE 22
#define JGDFS_INFO_LONG_SIZE  278

/* Card configuration.  The root is created lazily on first use.  NULL or
 * "" means no card. */
void     JGDFSSetRoot(const char *dir);
void     JGDFSSetAccess(int access);
int      JGDFSGetAccess(void);

/* GD_CardIn: 1 when a usable root exists (or could be created). */
int      JGDFSCardIn(void);

int32_t  JGDFSOpen(const char *gdpath, uint32_t mode);
int32_t  JGDFSClose(uint32_t handle);
int32_t  JGDFSSeek(uint32_t handle, uint32_t whence, int32_t offset);
/* Reads up to len bytes; *got receives the count (short at end of file,
 * which is still success, as FatFs f_read). */
int32_t  JGDFSRead(uint32_t handle, uint8_t *buf, uint32_t len,
                   uint32_t *got);
int32_t  JGDFSWrite(uint32_t handle, const uint8_t *buf, uint32_t len);
uint32_t JGDFSTell(uint32_t handle);   /* 0xFFFFFFFF on failure */
uint32_t JGDFSSize(uint32_t handle);   /* 0xFFFFFFFF on failure */

/* Fill `out` with the 22- or 278-byte info record. */
int32_t  JGDFSInfo(const char *gdpath, uint8_t *out, int long_name);

int32_t  JGDFSDirOpen(const char *gdpath);
/* Next entry ("." and ".." skipped).  End of directory returns -1 with
 * the name bytes zeroed, so either loop shape a caller writes ends. */
int32_t  JGDFSDirRead(uint32_t handle, uint8_t *out, int long_name);
int32_t  JGDFSDirClose(uint32_t handle);

/* Close every handle (reset / unload / older savestate). */
void     JGDFSCloseAll(void);
/* Power-on: close all, forget root and access (iOS no-dlclose rule). */
void     JGDFSDone(void);

/* Savestate chunk ("JGF1"): fixed size.  Handles are stored as
 * card-relative paths plus position, and restored by reopening with the
 * original access only -- never re-applying a create/truncate/append
 * disposition, so loading a state can never clobber a file.  A handle
 * already open on the same path and access is only re-seeked (run-ahead
 * loads a state every frame).  A file that no longer exists comes back
 * closed. */
size_t   JGDFSStateSize(void);
size_t   JGDFSStateSave(uint8_t *buf);
size_t   JGDFSStateLoad(const uint8_t *buf);

#ifdef __cplusplus
}
#endif

#endif /* __JAGGD_FS_H__ */
