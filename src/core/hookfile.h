/*
 * HOOKFILE.H
 *
 * User-supplied enhancement hooks (issue #637): the parser for
 * <system_dir>/vj_hooks.txt, which lets a user author the one thing the
 * built-in table cannot be authored for without a PR -- a byte patch for
 * a specific title.
 *
 * Line-based, not JSON, on purpose.  libretro-common vendors no JSON
 * parser, and hand-rolling one would add a large parsing surface to a
 * feature whose entire job is writing bytes into a ROM image.  The format
 * is the same shape as <system_dir>/vj_netlink.txt, which this core
 * already reads.
 *
 *     # comments run to end of line
 *     crc=DC187F82
 *     hook=framecap-uncap 0x0012A4 4E71 4E75
 *
 * `crc=` opens a section; every `hook=` line under it belongs to that
 * title.  A hook line is: name, payload-relative offset, expected bytes,
 * replacement bytes.  expect and patch are hex strings of equal length,
 * 1..TITLEDB_HOOK_MAX_BYTES bytes each.
 *
 * The parser is pure: it takes a path and a CRC, fills a caller-owned
 * arena, and touches no core global -- so test/tools/test_titlehook.c can
 * drive it directly, the same reasoning that keeps
 * TitleHookApplyToBuffer() separate from TitleHookApplyROM().
 *
 * FENCES (all inherited from docs/enhancement-hooks.md, none relaxed):
 *   - expect[] is mandatory.  A hook line without it does not parse.
 *   - All-or-nothing per FILE: any malformed line discards every hook
 *     parsed for that CRC and returns 0.  A half-understood patch set is
 *     more dangerous than none, and the applier's own all-or-nothing rule
 *     would otherwise be applied to a set we only partly understood.
 *   - A malformed or absent file NEVER refuses to load the game; it logs
 *     and yields zero hooks.
 *   - Nothing here allocates.  Every bound is a compile-time constant, so
 *     a file cannot drive allocation or overrun by declaring a count.
 */

#ifndef __HOOKFILE_H__
#define __HOOKFILE_H__

#include <stdint.h>
#include "titledb.h"

#ifdef __cplusplus
extern "C" {
#endif

#define HOOKFILE_MAX_NAME 32

/* Caller-owned arena.  TitleDBHook holds const pointers into `bytes` and
 * `names`, so this whole struct must outlive any use of hooks[]. */
typedef struct {
   TitleDBHook hooks[TITLEDB_MAX_HOOKS];
   uint8_t     bytes[TITLEDB_MAX_HOOKS * TITLEDB_HOOK_MAX_BYTES * 2];
   char        names[TITLEDB_MAX_HOOKS][HOOKFILE_MAX_NAME];
   int         count;
} HookFileSet;

/*
 * Parse `path`, collecting the hooks declared under `crc`.
 *
 * Returns the number of hooks parsed (0 when the file is absent, has no
 * section for this CRC, or is malformed -- none of which is an error).
 * `out` is fully zeroed first, and hooks[] is left kind==0 terminated so
 * it can be handed straight to TitleHookApplyToBuffer().
 */
int HookFileLoad(const char *path, uint32_t crc, HookFileSet *out);

#ifdef __cplusplus
}
#endif

#endif /* __HOOKFILE_H__ */
