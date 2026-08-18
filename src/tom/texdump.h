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
