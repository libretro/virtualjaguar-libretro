//
// biosdb.h: known Jaguar cartridge boot ROM image identification
//
// See docs/enhancement-hooks.md-style header comments elsewhere for the
// pattern this follows: a small, self-contained lookup table plus two
// pure functions.  Identification is by CRC32 of the full 128 KB dump --
// filenames only steer search order (see load_external_cart_boot_rom() in
// libretro.c), they never decide identity.
//
// Table sourced from libretro/virtualjaguar-libretro issue #469 (hash
// table contributed there after cross-checking every boot ROM image that
// has ever lived in this tree against known dumps).  BJL and the Alpine
// board's own boot ROM are intentionally not listed yet -- their hashes
// are unconfirmed (see the issue thread); adding either is a one-line
// table entry once confirmed.
//

#ifndef __BIOSDB_H__
#define __BIOSDB_H__

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// Name returned for a CRC32 that is not in the known-image table.
// Exposed so callers can branch on "was this recognized?" without
// string-comparing the table's exact wording.
extern const char * const BIOSDB_UNKNOWN_NAME;

// Look up a known Jaguar cartridge boot ROM image by CRC32 of the full
// 128 KB dump.  Returns a name from the table, or BIOSDB_UNKNOWN_NAME if
// crc does not match a known image -- an unrecognized image is still
// usable, just unidentified.
const char *BIOSDBLookupCRC(uint32_t crc);

// Convenience: CRC32 the given buffer, look it up, and hand the checksum
// back through *crc_out (may be NULL if the caller doesn't need it).
const char *BIOSDBIdentify(const uint8_t *data, uint32_t len, uint32_t *crc_out);

#ifdef __cplusplus
}
#endif

#endif	// __BIOSDB_H__
