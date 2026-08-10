/*
 * test_titledb.c
 *
 * Table-integrity + lookup unit test. Links titledb.c + crc32.c directly;
 * no core dlopen, no ROMs — CI-safe. c99 allowed.
 */

#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <stddef.h>
#include "../../src/core/titledb.h"

/* Stub: the test calls TitleDBSetCRC directly, not TitleDBSetContent.
 * In the full core build, this is overridden by file.c's real implementation.
 */
uint32_t DetectPrependedHeaderSize(uint8_t *buffer, uint32_t size)
{
   (void)buffer;
   (void)size;
   return 0;
}

static int fails = 0;

#define CHECK(cond, msg) do { \
   if (cond) printf("  PASS: %s\n", msg); \
   else { printf("  FAIL: %s\n", msg); fails++; } \
} while (0)

int main(void)
{
   int count = 0;
   int i = 0;
   int j = 0;
   const TitleDBEntry *t = TitleDBTable(&count);

   CHECK(count > 0, "table is non-empty");

   for (i = 0; i < count; i++)
   {
      CHECK(t[i].crc32 != 0, "entry crc32 is non-zero");
      CHECK(t[i].name && t[i].name[0], "entry has a name");
      CHECK(t[i].pairs[0].key != NULL, "entry has at least one pair");

      for (j = i + 1; j < count; j++)
         CHECK(t[i].crc32 != t[j].crc32, "no duplicate crc32 keys");

      for (j = 0; j < TITLEDB_MAX_PAIRS && t[i].pairs[j].key; j++)
      {
         CHECK(strncmp(t[i].pairs[j].key, "virtualjaguar_", 14) == 0,
               "pair key is a core option key");
         CHECK(t[i].pairs[j].value && t[i].pairs[j].value[0],
               "pair value is non-empty");
      }
   }

   /* Lookup behaviour without content: no override. */
   TitleDBSetCRC(0);
   CHECK(TitleDBOverride("virtualjaguar_true_color") == NULL,
         "no content -> no override");
   CHECK(TitleDBTitleName() == NULL, "no content -> no title");

   /* Unknown content (CRC of 0x12345678): no override. */
   TitleDBSetCRC(0x12345678);
   CHECK(TitleDBOverride("virtualjaguar_true_color") == NULL,
         "unknown content -> no override");

   printf("%s (%d failures)\n", fails ? "FAILED" : "OK", fails);
   return fails ? 1 : 0;
}
