/*
 * TEXDUMP.H
 *
 * Texture dump mode (issue #369, deliverable 1 of 2).
 *
 * Captures every unique blitter SOURCE window at blit launch, keyed by
 * an FNV-1a 64 hash over the raw source bytes (the identity contract
 * the v3.5 replacement pipeline will look up), and writes a preview PNG
 * plus a manifest row the first time each key is seen.  Design spec:
 * docs/texture-dump.md.
 *
 * Everything here is host-transient: ZERO savestate fields, no effect
 * on the emulated machine (the capture only READS blitter_ram, main
 * RAM/cart ROM and TOM's CLUT).  When the option is off the launch site
 * pays exactly one predictable branch on texDumpEnabled.
 */

#ifndef __TEXDUMP_H__
#define __TEXDUMP_H__

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* 16bpp preview interpretation (virtualjaguar_texdump_16bpp).  The
 * blitter cannot know whether 16-bit values are CRY or RGB16 -- that is
 * display-time interpretation -- so the option only changes the PNG
 * preview, never the hash. */
#define TEXDUMP_16BPP_CRY   0
#define TEXDUMP_16BPP_RGB   1
#define TEXDUMP_16BPP_BOTH  2

/* Hot-path gate: the launch site tests this before calling the hook so
 * the disabled mode costs one predictable branch. */
extern int texDumpEnabled;

/* ---- shared identity-contract machinery (deliverable 2 consumer) ----
 *
 * The replacement pipeline (texreplace.c, deliverable 2 of #369) must
 * compute EXACTLY the key a tile dumped under, so the decode / walk /
 * hash helpers below are the single implementation both modules use.
 * They are pure reads of blitter_ram + emulated memory: no dump state,
 * no allocation, callable whether or not dump mode is enabled. */

/* Register-described blit window (docs/texture-dump.md).  Filled by
 * TexDumpDescribe / TexDumpDescribeChannel. */
typedef struct
{
   uint32_t base;        /* phrase-aligned channel base                */
   uint32_t flags;       /* channel FLAGS register                     */
   uint32_t psizefield;  /* (flags >> 3) & 7                           */
   uint32_t bpp;         /* 1 << psizefield                            */
   uint32_t pitch;       /* phrase-gap ({0,1,3,2}[flags & 3])          */
   uint32_t width;       /* window width in pixels (6-bit float)       */
   uint32_t x0, y0;      /* integer pixel origin                       */
   uint32_t inner;       /* pixels per row (B_COUNT low)               */
   uint32_t outer;       /* rows (B_COUNT high)                        */
   uint32_t src_addr;    /* byte address of pixel (x0, y0)             */
} TexDumpDesc;

/* Garbage-register guard: described windows larger than 1 MB are never
 * hashed; the serialization buffer adds a phrase of per-row slack. */
#define TEXDUMP_MAX_WINDOW_BYTES (1u << 20)
#define TEXDUMP_SCRATCH_BYTES    (TEXDUMP_MAX_WINDOW_BYTES + 0x10000)

/* Decode the current launch's SOURCE window from blitter_ram.  Returns
 * 1 when this is a sane SRCEN blit (cmd stored to *cmd_out when non-
 * NULL); 0 for no-source / garbage-register launches; -1 for a sane
 * window larger than the 1 MB guard. */
int TexDumpDescribe(TexDumpDesc *d, uint32_t *cmd_out);

/* Decode ONE address channel (nonzero use_a1 = A1) with the given
 * inner/outer counts -- the replacement pipeline uses this to model the
 * DESTINATION window of a launch.  Returns 1 unless the channel's
 * pixel-size encoding is garbage. */
int TexDumpDescribeChannel(TexDumpDesc *d, int use_a1,
                           uint32_t inner, uint32_t outer);

/* Serialize the described window into buf (row-major covering bytes,
 * the identity contract's byte stream).  Returns the byte count, or 0
 * when the window leaves populated address space or overruns cap. */
uint32_t TexDumpSerialize(const TexDumpDesc *d, uint8_t *buf, uint32_t cap);

/* FNV-1a 64 over the frozen header + the serialized bytes. */
uint64_t TexDumpHashKey(const TexDumpDesc *d, const uint8_t *bytes,
                        uint32_t len);

/* BYTE offset of pixel (c, r) inside the described window (already
 * scaled for 16/32bpp).  Add d->base and mask to 24 bits for the
 * address; sub-byte pixel position for bpp < 8 follows the stored
 * big-endian packing (pixel x sits (~x & mask) sub-positions up). */
uint32_t TexDumpPixelByteOffset(const TexDumpDesc *d, uint32_t c,
                                uint32_t r);

/* Side-effect-free emulated-memory read used by the walkers (main RAM
 * mirror, cart ROM incl. GameDrive banking, boot ROM).  TexDumpAddrOK
 * bounds the populated, side-effect-free space. */
int     TexDumpAddrOK(uint32_t a);
uint8_t TexDumpRead8(uint32_t a);

/* Option layer (libretro.c).  Runtime-toggleable: enabling allocates
 * the dedupe set + capture scratch on FIRST enable; disabling keeps
 * them (the session's "seen" set survives a toggle) until
 * TexDumpShutdown() frees everything. */
void TexDumpSetEnabled(int on);
void TexDumpSet16bppMode(int mode);

/* Base output directory (the frontend's system dir).  Dumps land in
 * <base>/vj_texdump/<cart CRC32 as 8 hex>/.  Must be set before the
 * first capture can write anything; with no base path set, capture
 * still hashes/dedupes but writes nothing. */
void TexDumpSetBasePath(const char *system_dir);

/* Once per retro_run: advances the frame number recorded in manifest
 * rows.  Call guarded:  if (texDumpEnabled) TexDumpFrame(); */
void TexDumpFrame(void);

/* The launch site (blitter_mmio.c, B_CMD dispatch), BEFORE engine
 * dispatch.  Reads the register-described source window, hashes it,
 * and on first sight renders + writes PNG and manifest row.
 * Call guarded:  if (texDumpEnabled) TexDumpLaunch(); */
void TexDumpLaunch(void);

/* Close the manifest, log a summary, free the set + scratch and reset
 * every static (iOS never dlcloses).  Safe to call repeatedly. */
void TexDumpShutdown(void);

#ifdef __cplusplus
}
#endif

#endif /* __TEXDUMP_H__ */
