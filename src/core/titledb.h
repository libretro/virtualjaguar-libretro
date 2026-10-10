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

/*
 * Row classes (issue #748) -- the tier-3 contract.
 *
 *   ENHANCEMENT    (0, the zero-fill default): a visual/perf lever.  The
 *                  enhancement profile may decline to apply it (see
 *                  enhancement_profile_governs() in libretro.c).
 *   COMPATIBILITY  (1): a setting the title needs to WORK at all -- boot
 *                  mode, a timing model, a controller type.  Never governed
 *                  by the enhancement profile.  `cite` is mandatory: the
 *                  measurement (probe, numbers, date) and the mechanism
 *                  ticket the row stands in for, as "#N".  test_titledb
 *                  fails the table if it is missing.  The row is RETIRED,
 *                  not kept, when that mechanism lands -- the ticket number
 *                  is how the retirement is found.
 *
 * Both classes share the DB's one hard rule: a row only substitutes for an
 * option left at its registered default, so an explicit user choice always
 * wins.  Existing two-field initializers stay valid (C89 6.5.7 zero-fills
 * cls and cite, i.e. an enhancement pair with no citation).
 */
#define TITLEDB_CLASS_ENHANCEMENT   0
#define TITLEDB_CLASS_COMPATIBILITY 1

typedef struct {
   const char *key;     /* core option key, e.g. "virtualjaguar_true_color" */
   const char *value;   /* value to apply when the user left the option at default */
   int         cls;     /* TITLEDB_CLASS_*; omitted = ENHANCEMENT */
   const char *cite;    /* COMPATIBILITY: evidence + "#N" mechanism ticket */
} TitleDBPair;

#define TITLEDB_MAX_PAIRS 4

/*
 * Negative / known-bad entries (issue #464) -- the half of the per-title
 * surface pairs[] cannot express: "this setting is known to BREAK this
 * title", not "apply this setting to this title".
 *
 * Behaviour on match (decided in libretro.c's get_variable_pertitle(),
 * not here -- this file only ANSWERS "is this value unsafe?"):
 *
 *   - If the value in question is a per-title DEFAULT substitution
 *     (pairs[] or otherwise) -- i.e. the user left the option untouched --
 *     the substitution is REFUSED and a warning is logged. A per-title
 *     default must never be allowed to be unsafe; refusing it costs the
 *     user nothing they asked for.
 *   - If the value in question is the user's own EXPLICIT choice, it is
 *     always HONOURED -- refusing it would violate the DB's one hard rule
 *     ("user-set values always win") -- but a warning is still logged so
 *     a bug report against that title starts from the right hypothesis.
 *
 * `value` is either an exact string to match, or the sentinel
 * TITLEDB_NEG_ANY_NONDEFAULT ("*"), meaning "any value other than this
 * option's own registered default is unsafe" -- the natural shape for an
 * enhancement slider like a clock-scale preset, where every non-1x step
 * is the thing being called unsafe, not one specific step.
 *
 * Evidence bar (mirrors the hooks[] checklist in titledb.c): a negative
 * row requires the SAME corpus-grade evidence positive rows do -- a
 * reproduced, headless-confirmed regression tying the specific value to
 * the specific title, not a hypothesis awaiting artifacts. A bug report
 * alone, or a plausible mechanism that hasn't been isolated from every
 * other explanation, does not clear the bar. See docs/enhancement-hooks.md.
 */
typedef struct {
   const char *key;     /* core option key */
   const char *value;   /* exact unsafe value, or TITLEDB_NEG_ANY_NONDEFAULT */
} TitleDBNegativePair;

#define TITLEDB_MAX_NEGATIVE_PAIRS 4
#define TITLEDB_NEG_ANY_NONDEFAULT "*"

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
   TitleDBNegativePair negative[TITLEDB_MAX_NEGATIVE_PAIRS];
                                          /* terminated by a {NULL, NULL}
                                           * pair. Omitted from an
                                           * initializer = zero-filled by
                                           * C89 6.5.7, i.e. no negative
                                           * rows -- every existing entry's
                                           * initializer stays valid
                                           * unchanged. */
} TitleDBEntry;

/* Load content for CRC lookup; NULL/0 clears. */
void TitleDBSetContent(const uint8_t *data, size_t size);

/* Internal: set the CRC directly (used by TitleDBSetContent and by tests). */
void TitleDBSetCRC(uint32_t crc);

/* CRC32 (header-normalized) of the currently loaded content, as last set by
 * TitleDBSetContent/TitleDBSetCRC; 0 when no content is loaded.  Cartridge
 * content only: a disc match (TitleDBSetDisc) never sets it, so texture
 * dump/replace directories keyed on it are unaffected by CD rows. */
uint32_t TitleDBContentCRC(void);

/*
 * Disc rows (issue #747).  A Jaguar CD title is keyed by the CRC32 of its
 * boot stub as CDIntfExtractBootStub() returns it (word-swap undone,
 * header-validated): measured stable across CUE, CHD and CDI rips of the
 * same pressing, where the session/track/sector layout is not (CDI rips
 * carry +150 sectors).  Disc rows live in their OWN table, whose `crc32`
 * field holds the stub CRC, so a cartridge CRC can never match a disc row
 * or the reverse.  Same pairs[] / negative[] semantics as cart rows; disc
 * rows carry no hooks[] (those patch cartridge ROM).
 *
 * Known limits of the key: it identifies the boot LOADER, not the game
 * program -- seven homebrew discs share one loader ($DE7145EE) and
 * Songbird re-releases share the original's stub.  Discs with no
 * extractable stub (audio-only, damaged CDI V2 rips) have no key.
 *
 * TitleDBSetDisc(0) clears the match.  Like TitleDBSetCRC, it replaces
 * whatever row was current.
 */
void TitleDBSetDisc(uint32_t boot_stub_crc);

/* Test-only: TitleDBSetDisc() scans these rows instead of the shipped disc
 * table; NULL restores it.  Mirrors TitleDBSetPairsForTest(). */
void TitleDBSetDiscRowsForTest(const TitleDBEntry *rows, int count);

/* Test-only introspection: the raw disc table. */
const TitleDBEntry *TitleDBDiscTable(int *count);

/* Lookup: return the preset value for a key in the loaded content, or NULL. */
const char *TitleDBOverride(const char *key);

/* Class (TITLEDB_CLASS_*) of the pair TitleDBOverride() would answer from
 * for `key`, or -1 when there is no such pair (issue #748). */
int TitleDBOverrideClass(const char *key);

/* Negative-entry lookup (issue #464): does the loaded content's row mark
 * `key`=`value` as known-bad?  `option_default` is the key's registered
 * core-option default, as already resolved by the caller -- this function
 * does no option-system lookups of its own, so titledb.c stays linkable
 * standalone (test/test_titledb links only titledb.c + crc32.c, no core).
 * Returns non-zero on a match: either an exact {key, value} negative row,
 * or a {key, TITLEDB_NEG_ANY_NONDEFAULT} row where value != option_default.
 * No match (including no content loaded) returns 0. */
int TitleDBUnsafeValue(const char *key, const char *value,
                        const char *option_default);

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

/* Test-only: install a negative-pair array that TitleDBUnsafeValue()
 * consults regardless of CRC match; NULL restores normal table lookup.
 * Mirrors TitleDBSetHooksForTest() -- same reasoning, same "no canary row
 * in the shipped table" motivation. Array need not be kind-terminated the
 * way hooks are; pass the exact element count. A {NULL, NULL}-terminated
 * array also works since the scan stops at a NULL key either way. */
void TitleDBSetNegativeForTest(const TitleDBNegativePair *pairs, int count);

/* Test-only: install a positive-pair array that TitleDBOverride() answers
 * from regardless of CRC match; NULL restores normal table lookup.  Pass
 * the exact element count (a {NULL, NULL} terminator also stops the scan).
 *
 * Mirrors TitleDBSetHooksForTest()/TitleDBSetNegativeForTest(), and exists
 * for the same reason plus one more (issue #590): a per-title-defaults test
 * that asserts against a REAL shipped row breaks every time that row is
 * legitimately edited -- test_pertitle_db cases 4 and 7 were left failing on
 * clean develop when #551 removed AvP's virtualjaguar_true_color pair.  A
 * synthetic row keeps the contract under test (default-valued option is
 * substituted; a negative entry refuses that substitution) independent of
 * what the shipped table happens to say this month.
 *
 * The override REPLACES the table lookup: with one installed, a key that is
 * not in the array reads as "no per-title entry" even if the loaded title's
 * real row has one.  So a fixture must carry every key its case exercises. */
void TitleDBSetPairsForTest(const TitleDBPair *pairs, int count);

/* Test-only introspection: the raw table. */
const TitleDBEntry *TitleDBTable(int *count);

#ifdef __cplusplus
}
#endif

#endif /* __TITLEDB_H__ */
