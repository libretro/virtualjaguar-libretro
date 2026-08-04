/*
 * test/tools/vjss_info.c — print Virtual Jaguar savestate header fields.
 *
 * Headers are host-endian: STATE_SAVE_VAR is a plain memcpy of uint32_t
 * (see src/core/state.h). On little-endian hosts the magic 0x564A5353
 * ("VJSS") appears on disk as bytes 53 53 4A 56 — do NOT string-compare
 * against ASCII "VJSS".
 *
 * Build:
 *   cc -O2 -Wall -std=c89 -o test/tools/vjss_info test/tools/vjss_info.c
 *
 * Usage:
 *   vjss_info <statefile>
 *
 * Exit: 0 = magic matched (any verdict), 1 = I/O or bad_magic, 2 = usage.
 */

#include <stdio.h>
#include <stdint.h>

/* Numeric copies of src/core/state.h — keep the tool standalone. */
#define VJSS_MAGIC       0x564A5353u  /* "VJSS" */
#define VJSS_VERSION     7u
#define VJSS_MIN_VERSION 2u

static uint32_t rd_le(const uint8_t *p)
{
   return (uint32_t)p[0]
        | ((uint32_t)p[1] << 8)
        | ((uint32_t)p[2] << 16)
        | ((uint32_t)p[3] << 24);
}

static uint32_t rd_be(const uint8_t *p)
{
   return ((uint32_t)p[0] << 24)
        | ((uint32_t)p[1] << 16)
        | ((uint32_t)p[2] << 8)
        | (uint32_t)p[3];
}

int main(int argc, char **argv)
{
   FILE *f;
   uint8_t buf[16];
   size_t nread;
   uint32_t magic, version, flags, reserved;
   const char *endian;
   const char *verdict;

   if (argc != 2)
   {
      fprintf(stderr, "usage: vjss_info <statefile>\n");
      return 2;
   }

   f = fopen(argv[1], "rb");
   if (!f)
   {
      perror(argv[1]);
      return 1;
   }

   nread = fread(buf, 1, sizeof(buf), f);
   fclose(f);
   if (nread < sizeof(buf))
   {
      fprintf(stderr, "vjss_info: short read (%zu bytes)\n", nread);
      return 1;
   }

   if (rd_le(buf) == VJSS_MAGIC)
   {
      endian   = "le";
      magic    = rd_le(buf);
      version  = rd_le(buf + 4);
      flags    = rd_le(buf + 8);
      reserved = rd_le(buf + 12);
   }
   else if (rd_be(buf) == VJSS_MAGIC)
   {
      endian   = "be";
      magic    = rd_be(buf);
      version  = rd_be(buf + 4);
      flags    = rd_be(buf + 8);
      reserved = rd_be(buf + 12);
   }
   else
   {
      printf("magic=0x%08X endian=? version=? flags=? reserved=? verdict=bad_magic\n",
             (unsigned)rd_le(buf));
      return 1;
   }

   if (version < VJSS_MIN_VERSION)
      verdict = "too_old";
   else if (version > VJSS_VERSION)
      verdict = "too_new";
   else
      verdict = "loadable";

   printf("magic=0x%08X endian=%s version=%u flags=0x%08X reserved=0x%08X verdict=%s\n",
          (unsigned)magic, endian, (unsigned)version,
          (unsigned)flags, (unsigned)reserved, verdict);

   return 0;
}
