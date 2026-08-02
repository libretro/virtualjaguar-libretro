#ifndef _VIRTUALJAGUAR_NVMBIOS_H
#define _VIRTUALJAGUAR_NVMBIOS_H

/*
 * nvmbios.h: HLE of the Memory Track "NVM BIOS" module.
 *
 * On hardware, the CD BIOS boot runs the Memory Track cartridge's stub,
 * which installs a 2K module in RAM: a '_NVM' magic cookie at $2400 and a
 * dispatcher at $2404.  Games detect the Memory Track by checking the
 * cookie (Vid Grid: `cmpi.l #'_NVM',$2400.w` at $012B72) and use it by
 * pushing an opcode + args and JSRing $2404 — they never touch the flash
 * hardware themselves; the module does.
 *
 * We install the cookie plus an RTS stub and implement the module's eleven
 * calls in C, operating directly on the Memory Track NVRAM image (mtMem).
 *
 * Semantics follow the Atari NVM BIOS v1.01 source and its specification
 * ("Non Volatile Memory - Bios calls", Atari, Feb 1995), both preserved in
 * https://github.com/cubanismo/skunk_mtrk — which is also where the $2400
 * contract was documented.  See docs/memory-track.md.
 */

#include <stdint.h>
#include <stddef.h>
#include <boolean.h>

#define NVM_COOKIE_ADDR    0x2400
#define NVM_DISPATCH_ADDR  0x2404
#define NVM_COOKIE_MAGIC   0x5F4E564Du   /* '_NVM' */

/* Filesystem geometry (from nvm.h / nvm.inc, Atari NVM BIOS v1.01). */
#define NVM_BLOCKSIZE      512
#define NVM_TOTALBLOCKS    256
#define NVM_FIRSTDATABLOCK 8
#define NVM_DATABLOCKS     248
#define NVM_NUMDIRENTRIES  199
#define NVM_DIRENTRYSIZE   18
#define NVM_DIROFFSET      512
#define NVM_NUMFILES       3
#define NVM_APPPACKSIZE    5            /* packed appname, 16-bit words */
#define NVM_FILEPACKSIZE   3            /* packed filename, 16-bit words */
#define NVM_APPNAMELEN     15
#define NVM_FILENAMELEN    9
#define NVM_SEARCHBUFSIZE  (4 + NVM_APPNAMELEN + 1 + NVM_FILENAMELEN + 1)

/* Error codes (sign-extended into D0). */
#define NVM_ENOINIT  (-1)
#define NVM_ENOSPC   (-2)
#define NVM_EFILNF   (-3)
#define NVM_EINVFN   (-4)
#define NVM_ERANGE   (-5)
#define NVM_ENFILES  (-6)
#define NVM_EIHNDL   (-7)

/* Emulator integration */
void NVMBiosInstall(void);        /* write cookie + RTS stub into main RAM */
void NVMBiosReset(void);          /* forget runtime state (power-on/reset) */
bool NVMBiosHook(uint32_t pc);    /* pre-instruction hook for $2404 */
size_t NVMBiosStateSave(uint8_t *buf);
size_t NVMBiosStateLoad(const uint8_t *buf);

/* The eleven calls, host-pointer based so they are unit-testable without a
 * 68K.  Return values / error codes exactly as the module's dispatcher
 * leaves them in D0. */
int32_t NVMInitialize(const char *appname);
int32_t NVMCreate(const char *filename, int32_t size);
int32_t NVMOpen(const char *filename);
int32_t NVMClose(int16_t handle);
int32_t NVMDelete(const char *appname_or_null, const char *filename);
int32_t NVMReadFile(int16_t handle, uint8_t *buf, int32_t count);
int32_t NVMWriteFile(int16_t handle, const uint8_t *buf, int32_t count);
int32_t NVMSearchFirst(uint8_t sbuf[NVM_SEARCHBUFSIZE], int32_t flags);
int32_t NVMSearchNext(uint8_t sbuf[NVM_SEARCHBUFSIZE]);
int32_t NVMSeek(int16_t handle, int32_t offset, int16_t flag);
int32_t NVMInquire(uint32_t *total, uint32_t *freebytes);

#endif
