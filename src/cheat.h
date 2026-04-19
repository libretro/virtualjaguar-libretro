#ifndef __CHEAT_H__
#define __CHEAT_H__

/*
 * Atari Jaguar cheat-code engine.
 *
 * Supports the Pro Action Replay / GameShark-style hex format commonly
 * distributed for Jaguar games. Separators (space, colon, hyphen, dot)
 * between the address and the value are optional and ignored, so the
 * following all parse to the same thing:
 *
 *    "00003D00 FFFF"       (PAR)
 *    "00003D00:FFFF"
 *    "0000:3D00-FFFF"
 *    "00003D00FFFF"
 *
 * A single `retro_cheat_set` string may contain multiple codes separated
 * by '+' or newlines; each is parsed and stored independently under the
 * same index so the frontend can toggle them as a group.
 *
 * Application is modelled as a callback: the engine is independent of
 * any specific memory implementation, which keeps the parser and list
 * management unit-testable in isolation from the emulator.
 */

#include <stdint.h>
#include <stddef.h>
#include <boolean.h>

#ifdef __cplusplus
extern "C" {
#endif

#define CHEAT_MAX_ENTRIES 256

typedef struct {
   uint32_t address;   /* 24-bit Jaguar bus address */
   uint32_t value;
   uint8_t  size;      /* 1 byte, 2 word, 4 long */
   uint8_t  tag;       /* retro cheat index (for removal on toggle) */
   bool     enabled;
} cheat_entry_t;

typedef struct {
   cheat_entry_t entries[CHEAT_MAX_ENTRIES];
   unsigned      count;
} cheat_list_t;

/* Parse a single code string into its components. Returns true iff the
 * string contains a well-formed hex address/value pair (after stripping
 * the ignored separators). On success, *addr is masked to 24 bits. */
bool cheat_parse_one(const char *code,
                     uint32_t *addr,
                     uint32_t *value,
                     uint8_t *size);

/* Remove every cheat tagged with `index`. Safe to call with no matches. */
void cheat_list_remove_index(cheat_list_t *list, unsigned index);

/* Parse `code` (which may contain multiple '+' or newline-separated
 * entries) and install each successfully-parsed entry under `index`.
 * When enabled=false, simply removes any existing entries for `index`. */
void cheat_list_set(cheat_list_t *list,
                    unsigned index,
                    bool enabled,
                    const char *code);

/* Clear every entry. */
void cheat_list_reset(cheat_list_t *list);

/* Callback used by cheat_list_apply to perform the memory write.
 * `size` is 1, 2, or 4 bytes. */
typedef void (*cheat_write_fn)(uint32_t addr, uint32_t value,
                               uint8_t size, void *user);

void cheat_list_apply(const cheat_list_t *list,
                      cheat_write_fn write,
                      void *user);

#ifdef __cplusplus
}
#endif

#endif /* __CHEAT_H__ */
