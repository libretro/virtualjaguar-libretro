/*
 * test_biosdb.c
 *
 * Known cart boot ROM image table unit test (issue #469). Links biosdb.c +
 * crc32.c + the two embedded boot ROM blobs directly; no core dlopen, no
 * private ROMs required -- CI-safe. c99 allowed.
 *
 * The embedded K/M images ARE the "known-good dumps" the table's CRC32
 * constants describe, so hashing them here both exercises the lookup path
 * and is a standing regression check that the constants in biosdb.c still
 * match what's actually baked into the core (a bit-rotted constant would
 * silently mislabel a user's external file).
 */

#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <stdlib.h>
#include <sys/stat.h>
#include "../../src/core/biosdb.h"

extern uint8_t jaguarBootROM[];
extern uint8_t jaguarBootROM_M[];

static int fails = 0;

#define CHECK(cond, msg) do { \
   if (cond) printf("  PASS: %s\n", msg); \
   else { printf("  FAIL: %s\n", msg); fails++; } \
} while (0)

/* The four documented CRC32s from issue #469's hash table. Re-declared
 * here (rather than reused from biosdb.c) so a typo in one place doesn't
 * silently cancel out a typo in the other. */
#define CRC_K    0xFB731AAAu
#define CRC_M    0xAE25BDF5u
#define CRC_STB1 0xE60277BBu
#define CRC_STB2 0x8D15DBC6u

int main(void)
{
   uint32_t crc;
   const char *name;
   unsigned char filler[0x20000];
   unsigned int i;

   /* ---------------------------------------------------------------- */
   /* Known images: the embedded K and M boot ROMs must identify        */
   /* exactly as issue #469 documented them.                            */
   /* ---------------------------------------------------------------- */
   crc = 0;
   name = BIOSDBIdentify(jaguarBootROM, 0x20000, &crc);
   CHECK(crc == CRC_K, "embedded Series K image hashes to the documented CRC32");
   CHECK(name != NULL && strcmp(name, "Series K boot ROM") == 0,
         "embedded Series K image identifies as 'Series K boot ROM'");

   crc = 0;
   name = BIOSDBIdentify(jaguarBootROM_M, 0x20000, &crc);
   CHECK(crc == CRC_M, "embedded Model M image hashes to the documented CRC32");
   CHECK(name != NULL && strcmp(name, "Model M boot ROM") == 0,
         "embedded Model M image identifies as 'Model M boot ROM'");

   /* ---------------------------------------------------------------- */
   /* Direct CRC lookup for the two dumps we don't have bytes for       */
   /* (Stubulator '93 / '94 were dropped from the embedded set --       */
   /* see biosdb.h). Confirms BIOSDBLookupCRC()'s table entries match   */
   /* the issue's documented values independent of BIOSDBIdentify().   */
   /* ---------------------------------------------------------------- */
   name = BIOSDBLookupCRC(CRC_STB1);
   CHECK(name != NULL && strcmp(name, "Stubulator '93 dev boot ROM") == 0,
         "Stubulator '93 CRC32 identifies correctly");

   name = BIOSDBLookupCRC(CRC_STB2);
   CHECK(name != NULL && strcmp(name, "Stubulator '94 dev boot ROM") == 0,
         "Stubulator '94 CRC32 identifies correctly");

   /* ---------------------------------------------------------------- */
   /* Unknown image: a deterministic filler buffer whose CRC32 does not */
   /* collide with any table entry must fall back to the unknown name,  */
   /* not be silently mislabelled.                                      */
   /* ---------------------------------------------------------------- */
   for (i = 0; i < sizeof(filler); i++)
      filler[i] = (unsigned char)(i * 7u + 0x5Au);

   crc = 0;
   name = BIOSDBIdentify(filler, (uint32_t)sizeof(filler), &crc);
   CHECK(crc != CRC_K && crc != CRC_M && crc != CRC_STB1 && crc != CRC_STB2,
         "synthetic filler does not collide with a known-image CRC32");
   CHECK(name == BIOSDB_UNKNOWN_NAME,
         "unrecognized image reports BIOSDB_UNKNOWN_NAME");
   CHECK(strcmp(name, "custom/unrecognized image") == 0,
         "unrecognized image's label text is stable");

   /* Direct lookup path (no buffer) agrees with the identify path. */
   CHECK(BIOSDBLookupCRC(crc) == name,
         "BIOSDBLookupCRC() agrees with BIOSDBIdentify() for the same CRC");

   /* crc_out is optional. */
   name = BIOSDBIdentify(jaguarBootROM, 0x20000, NULL);
   CHECK(name != NULL && strcmp(name, "Series K boot ROM") == 0,
         "BIOSDBIdentify() works with crc_out == NULL");

   /* No accidental collisions between the four known entries themselves. */
   CHECK(CRC_K != CRC_M && CRC_K != CRC_STB1 && CRC_K != CRC_STB2 &&
         CRC_M != CRC_STB1 && CRC_M != CRC_STB2 && CRC_STB1 != CRC_STB2,
         "the four known-image CRC32s are pairwise distinct");

   /* ---------------------------------------------------------------- */
   /* Optional: a real '[BIOS] Atari Jaguar (World).j64' dump, if the   */
   /* private corpus has one, must independently identify as Series K. */
   /* Skips cleanly (not a failure) when the corpus isn't present --    */
   /* CI has no private ROMs, and this file is gitignored everywhere.   */
   /* ---------------------------------------------------------------- */
   {
      static const char *candidates[] = {
         "test/roms/private/ROMS/[BIOS] Atari Jaguar (World).j64",
         "test/roms/private/[BIOS] Atari Jaguar (World).j64",
         NULL
      };
      const char *env_dir = getenv("VJ_BIOS_DIR");
      char envpath[1024];
      const char *path = NULL;
      struct stat st;
      unsigned int i;

      if (env_dir && env_dir[0])
      {
         snprintf(envpath, sizeof(envpath),
                  "%s/[BIOS] Atari Jaguar (World).j64", env_dir);
         if (stat(envpath, &st) == 0)
            path = envpath;
      }
      for (i = 0; !path && candidates[i]; i++)
         if (stat(candidates[i], &st) == 0)
            path = candidates[i];

      if (!path)
      {
         printf("  SKIP: real '[BIOS] Atari Jaguar (World).j64' identifies "
                "as Series K (private corpus not present)\n");
      }
      else
      {
         FILE *f = fopen(path, "rb");
         if (!f)
         {
            printf("  SKIP: real '[BIOS] Atari Jaguar (World).j64' identifies "
                   "as Series K (found but could not open %s)\n", path);
         }
         else
         {
            unsigned char *buf = (unsigned char *)malloc(0x20000);
            size_t n = buf ? fread(buf, 1, 0x20000, f) : 0;
            fclose(f);

            if (!buf || n != 0x20000)
            {
               printf("  SKIP: real '[BIOS] Atari Jaguar (World).j64' identifies "
                      "as Series K (%s is not exactly 131072 bytes)\n", path);
            }
            else
            {
               crc = 0;
               name = BIOSDBIdentify(buf, 0x20000, &crc);
               CHECK(crc == CRC_K,
                     "real '[BIOS] Atari Jaguar (World).j64' hashes to the K CRC32");
               CHECK(name != NULL && strcmp(name, "Series K boot ROM") == 0,
                     "real '[BIOS] Atari Jaguar (World).j64' identifies as Series K");
            }
            free(buf);
         }
      }
   }

   printf("%s (%d failures)\n", fails ? "FAILED" : "OK", fails);
   return fails ? 1 : 0;
}
