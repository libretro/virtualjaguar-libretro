/*
 * TEXREPLACE.H
 *
 * Texture replacement pipeline (issue #369, deliverable 2 of 2).
 *
 * Consumes the identity contract dump mode (texdump.c) freezes: at blit
 * launch the SOURCE window is hashed with the shared texdump helpers,
 * looked up in a host-side pack map preloaded from
 * <system_dir>/vj_texpacks/<cart CRC32>/<hash16>.png, and on a hit the
 * launch's DESTINATION pixels are recorded into the true-color shadow
 * framebuffer (shadowfb.c) as value-tagged pack RGB.  The OP's existing
 * shadow presentation path then shows the pack art.
 *
 * The emulated machine is NEVER touched: no RAM/register writes, zero
 * savestate fields, no bus-model time.  Savestates, rewind, runahead
 * and netplay see a bit-identical machine whether or not a pack is
 * active; only the presented frame differs.  Design + authoring guide:
 * docs/texture-dump.md ("Replacement pipeline").
 */

#ifndef __TEXREPLACE_H__
#define __TEXREPLACE_H__

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Hot-path gate: option enabled AND the loaded pack has entries.  The
 * launch site tests this before calling the hooks so the disabled mode
 * costs one predictable branch. */
extern int texReplaceEnabled;

/* Option layer (libretro.c).  Runtime-toggleable; enabling triggers the
 * pack load (one-off host file I/O) if the base path and content CRC
 * are already known, otherwise the load happens when they arrive via
 * TexReplaceContentLoaded(). */
void TexReplaceSetEnabled(int on);

/* Base directory (the frontend's system dir).  Packs are read from
 * <base>/vj_texpacks/<cart CRC32 as 8 hex>/. */
void TexReplaceSetBasePath(const char *system_dir);

/* Content is loaded and TitleDBSetContent() has latched the CRC: probe
 * pack availability (for option visibility) and load the pack now if
 * the option is already on. */
void TexReplaceContentLoaded(void);

/* Nonzero when a pack directory exists for the loaded content
 * (regardless of the option state) -- gates option visibility. */
int TexReplacePackAvailable(void);

/* Nonzero when the loaded pack has at least one entry -- the libretro
 * layer uses this to force the shadow framebuffer on. */
int TexReplaceHasEntries(void);

/* Launch-site hooks (blitter_mmio.c, around engine dispatch).
 * PreBlit hashes the source window and arms a hit; PostBlit walks the
 * destination window and records value-tagged pack RGB into the shadow
 * framebuffer.  Call guarded:
 *    trBlit = texReplaceEnabled ? TexReplacePreBlit() : 0;
 *    ... dispatch ...
 *    if (trBlit) TexReplacePostBlit();  */
int  TexReplacePreBlit(void);
void TexReplacePostBlit(void);

/* Free the pack, reset every static (iOS never dlcloses).  Safe to
 * call repeatedly. */
void TexReplaceShutdown(void);

#ifdef __cplusplus
}
#endif

#endif /* __TEXREPLACE_H__ */
