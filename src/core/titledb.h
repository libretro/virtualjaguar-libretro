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

typedef struct {
   uint32_t    crc32;                    /* header-normalized, same key as filedb */
   const char *name;                     /* for the log line */
   TitleDBPair pairs[TITLEDB_MAX_PAIRS]; /* terminated by a {NULL, NULL} pair */
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

/* Test-only introspection: the raw table. */
const TitleDBEntry *TitleDBTable(int *count);

#ifdef __cplusplus
}
#endif

#endif /* __TITLEDB_H__ */
