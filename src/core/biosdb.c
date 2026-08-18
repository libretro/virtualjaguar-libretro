//
// biosdb.c: known Jaguar cartridge boot ROM image identification
//
// See biosdb.h for the table's provenance.
//

#include "biosdb.h"
#include "crc32.h"

typedef struct
{
	uint32_t    crc32;
	const char *name;
} BIOSDBEntry;

const char * const BIOSDB_UNKNOWN_NAME = "custom/unrecognized image";

static const BIOSDBEntry biosdb_table[] =
{
	{ 0xFB731AAAu, "Series K boot ROM" },
	{ 0xAE25BDF5u, "Model M boot ROM" },
	{ 0xE60277BBu, "Stubulator '93 dev boot ROM" },
	{ 0x8D15DBC6u, "Stubulator '94 dev boot ROM" }
};

#define BIOSDB_TABLE_COUNT (sizeof(biosdb_table) / sizeof(biosdb_table[0]))

const char *BIOSDBLookupCRC(uint32_t crc)
{
	unsigned int i;

	for (i = 0; i < BIOSDB_TABLE_COUNT; i++)
	{
		if (biosdb_table[i].crc32 == crc)
			return biosdb_table[i].name;
	}

	return BIOSDB_UNKNOWN_NAME;
}

const char *BIOSDBIdentify(const uint8_t *data, uint32_t len, uint32_t *crc_out)
{
	uint32_t crc;

	crc = (uint32_t)crc32_calcCheckSum((unsigned char *)data, (unsigned int)len);
	if (crc_out)
		*crc_out = crc;

	return BIOSDBLookupCRC(crc);
}
