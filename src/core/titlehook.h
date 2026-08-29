/*
 * TITLEHOOK.H
 *
 * Per-title enhancement hooks (issue #370): the applier for the byte
 * patches carried in TitleDBEntry::hooks[].
 *
 * Design contract, in one paragraph.  A hook is a write into the loaded
 * CARTRIDGE ROM image, applied exactly once, at content load, after the
 * boot strategy has populated the cart window and before the first
 * retro_run.  That is the only trigger: cart ROM is not in the savestate
 * blob and JaguarReset() never touches it, so a patch survives reset with
 * no re-apply and is invisible to serialize/unserialize -- no savestate
 * field, no version bump.  Every apply is all-or-nothing per title: all
 * hooks are validated first, and one failure means zero bytes are
 * written.  Every hook carries a mandatory expect[] precondition; a
 * mismatch is refused and logged, never "best effort" applied.
 *
 * Lives in its own translation unit rather than in titledb.c because the
 * applier needs jagMemSpace / jaguarROMSize / the log macros, and
 * titledb.c is deliberately dependency-light so test/tools/test_titledb.c
 * can link it with two files and one stub.
 *
 * This file separates the pure part from the wiring on purpose:
 * TitleHookApplyToBuffer() takes the ROM image as a parameter and touches
 * no core global, so the host unit test (test/tools/test_titlehook.c)
 * exercises every fence against a malloc'd buffer with a single stub.
 * TitleHookApplyROM() is the thin wrapper that reads the globals.
 */

#ifndef __TITLEHOOK_H__
#define __TITLEHOOK_H__

#include <stdint.h>
#include "titledb.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Latch the gate (core option virtualjaguar_enhancement_hooks).  Read raw
 * via environ_cb in retro_load_game, never through get_variable_pertitle(),
 * so a DB row cannot carry {"virtualjaguar_enhancement_hooks","enabled"}
 * in its own pairs[] and switch its own gate on.  Default: disabled. */
void TitleHookSetEnabled(int enabled);
int  TitleHookGetEnabled(void);

/*
 * Pure applier.  Validates every hook in `hooks` against `rom` and, only
 * if every one passes, writes them all.  Returns the number of hooks
 * applied, or 0 on any refusal (nothing is written in that case).
 *
 *   rom       cartridge image base (payload-relative offset 0)
 *   romSize   bytes of `rom` that are loaded
 *   hooks     kind==0-terminated array, at most `count` entries
 *   title     name for the log line; may be NULL
 *   gdActive  non-zero when the Jaguar GameDrive banked path owns the
 *             image (see the fence table in titlehook.c)
 *
 * The gate is NOT consulted here -- callers that want gating go through
 * TitleHookApplyROM(), which checks it.  Keeping the gate out of the pure
 * function is what lets the unit test drive the fences directly.
 */
int TitleHookApplyToBuffer(uint8_t *rom, uint32_t romSize,
                           const TitleDBHook *hooks, int count,
                           const char *title, int gdActive);

/*
 * Wrapper for the load path: reads the gate, the titledb match and the
 * cartridge globals, then calls TitleHookApplyToBuffer().  Returns the
 * number of hooks applied (0 on any refusal, including gate-off and
 * table-miss, which are the common cases and are not errors).
 */
/* Path to the user hook file (<system_dir>/vj_hooks.txt, issue #637), or
 * NULL/empty to disable it.  Set from retro_load_game once the system
 * directory is known.  Kept as a setter rather than read here so this
 * translation unit stays free of environ_cb and remains unit-testable. */
void TitleHookSetUserFilePath(const char *path);

int TitleHookApplyROM(void);

#ifdef __cplusplus
}
#endif

#endif /* __TITLEHOOK_H__ */
