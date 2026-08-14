/*
 * TITLEDB.H
 *
 * Per-title enhancement defaults database — keyed by CRC32, applied at option-read time.
 * User-changed options always win; presets only apply when left at registered defaults.
 */

#ifndef __TITLEDB_H__
#define __TITLEDB_H__

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
   const char *key;     /* core option key, e.g. "virtualjaguar_true_color" */
   const char *value;   /* value to apply when the user left the option at default */
} TitleDBPair;

#define TITLEDB_MAX_PAIRS 4

/*
 * Enhancement hooks (issue #370) — the half of the per-title surface a
 * {key, value} string pair cannot carry: writes into emulated memory.
 *
 * SCOPE DISCRIMINATOR.  If an intervention can be expressed as a core
 * option value, it is a TitleDBPair and it ships today.  hooks[] exists
 * ONLY for byte patches.  A hook is a workaround for a *game* defect or a
 * deliberate enhancement to game data; it is never a substitute for
 * fixing an emulator bug (that gets re-filed against the accuracy track).
 *
 * The `kind` tag exists so adding a kind later is an additive change
 * rather than a reinterpretation of rows already shipped.  kind == 0
 * terminates the array.
 */
#define TITLEDB_HOOK_NONE       0
#define TITLEDB_HOOK_ROM_PATCH  1

#define TITLEDB_MAX_HOOKS       4
#define TITLEDB_HOOK_MAX_BYTES  64

typedef struct {
   uint8_t        kind;     /* TITLEDB_HOOK_*; 0 terminates the array       */
   uint8_t        len;      /* bytes in expect[] and patch[]; 1..64         */
   uint32_t       offset;   /* PAYLOAD-relative cart offset.  Bus address =
                             * $800000 + offset.  DetectPrependedHeaderSize
                             * strips a 512-byte header before BOTH the CRC
                             * and the memcpy into the cart window, so a
                             * file offset taken from a headered dump is
                             * wrong by 512 while the CRC — the table key —
                             * is identical for both dumps.               */
   const uint8_t *expect;   /* required current bytes, len of them.  NEVER
                             * NULL: a hook with no precondition is
                             * rejected by the table self-test.  titledb
                             * already aliases rows across CRCs (the Doom
                             * EX rows inherit retail Doom's pairs);
                             * settings inherit safely across a romhack,
                             * bytes do not.                              */
   const uint8_t *patch;    /* replacement bytes, len of them              */
   const char    *name;     /* short stable id for the log line, e.g.
                             * "framecap-uncap"                           */
} TitleDBHook;

typedef struct {
   uint32_t    crc32;                    /* header-normalized, same key as filedb */
   const char *name;                     /* for the log line */
   TitleDBPair pairs[TITLEDB_MAX_PAIRS]; /* terminated by a {NULL, NULL} pair */
   TitleDBHook hooks[TITLEDB_MAX_HOOKS]; /* terminated by a kind==0 hook.
                                          * Omitted from an initializer =
                                          * zero-filled by C89 6.5.7, i.e.
                                          * TITLEDB_HOOK_NONE. */
} TitleDBEntry;

/* Load content for CRC lookup; NULL/0 clears. */
void TitleDBSetContent(const uint8_t *data, size_t size);

/* Internal: set the CRC directly (used by TitleDBSetContent and by tests). */
void TitleDBSetCRC(uint32_t crc);

/* CRC32 (header-normalized) of the currently loaded content, as last set by
 * TitleDBSetContent/TitleDBSetCRC; 0 when no content is loaded. */
uint32_t TitleDBContentCRC(void);

/* Lookup: return the preset value for a key in the loaded content, or NULL. */
const char *TitleDBOverride(const char *key);

/* Return the title name of the loaded content match, or NULL. */
const char *TitleDBTitleName(void);

/* Enhancement hooks for the loaded content, or NULL on a miss.  *count
 * receives TITLEDB_MAX_HOOKS; the array is kind==0 terminated. */
const TitleDBHook *TitleDBHooks(int *count);

/* Test-only: install a hook array that TitleDBHooks() returns regardless of
 * CRC match; NULL restores normal table lookup.  Exists so the end-to-end
 * gate test needs no fake row in the shipped table (a canary row on
 * test/roms/yarc.j64 would break test_pertitle_db --case 5, which uses yarc
 * as the deliberate non-DB control).
 *
 * Compiled unconditionally, exported only under the wide test ABI
 * (link-test.T / exports-test.list).  TEST_EXPORTS in this Makefile is a
 * LINK-time concept — it selects a symbol-export script and defines only
 * -DVJ_TRACE — so an #ifdef here would be false in every build.  Making it
 * a compile-time define instead would put titledb.o into the
 * VJTRACE_HOOKED_OBJS mode-transition delete list; hiding it at link time
 * costs a few bytes and no build-mode hazard. */
void TitleDBSetHooksForTest(const TitleDBHook *hooks, int count);

/* Test-only introspection: the raw table. */
const TitleDBEntry *TitleDBTable(int *count);

#ifdef __cplusplus
}
#endif

#endif /* __TITLEDB_H__ */
