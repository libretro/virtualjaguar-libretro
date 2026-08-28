/*
 * HOOKFILE.C
 *
 * Parser for <system_dir>/vj_hooks.txt -- user-authored enhancement hooks
 * (issue #637).  See hookfile.h for the format and the fences.
 *
 * Dependency-light on purpose (stdio/string/log only, no core globals), so
 * the unit test links it without pulling in the emulator, exactly like
 * titledb.c.
 */

#include "hookfile.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <boolean.h>
#include <retro_inline.h>
#include "log.h"

#define HOOKFILE_LINE_MAX 512

static int hex_nibble(char c)
{
   if (c >= '0' && c <= '9') return c - '0';
   if (c >= 'a' && c <= 'f') return c - 'a' + 10;
   if (c >= 'A' && c <= 'F') return c - 'A' + 10;
   return -1;
}

/* Parse an even-length hex string into `dst`, at most `maxBytes`.
 * Returns the byte count, or -1 on any malformed input. */
static int parse_hex_bytes(const char *s, uint8_t *dst, int maxBytes)
{
   int n = 0;
   int hi, lo;

   if (!s || !*s)
      return -1;

   while (*s)
   {
      hi = hex_nibble(*s++);
      if (hi < 0 || !*s)
         return -1;
      lo = hex_nibble(*s++);
      if (lo < 0)
         return -1;
      if (n >= maxBytes)
         return -1;
      dst[n++] = (uint8_t)((hi << 4) | lo);
   }
   return n;
}

/* Strip a trailing comment, then leading/trailing whitespace, in place.
 * Returns the first non-space character. */
static char *trim_line(char *s)
{
   char *end;
   char *hash = strchr(s, '#');

   if (hash)
      *hash = '\0';

   while (*s == ' ' || *s == '\t' || *s == '\r' || *s == '\n')
      s++;

   end = s + strlen(s);
   while (end > s)
   {
      char c = end[-1];
      if (c != ' ' && c != '\t' && c != '\r' && c != '\n')
         break;
      end--;
   }
   *end = '\0';
   return s;
}

/* Parse one `hook=` value: "<name> <offset> <expect-hex> <patch-hex>".
 * Returns 1 on success, 0 on any malformed field. */
/* Split `s` into at most `max` whitespace-separated fields, in place.
 * Returns the field count, or -1 if there are more than `max` (trailing
 * junk is refused rather than guessed at).
 *
 * Hand-rolled rather than strtok_r: that is POSIX, and this core builds
 * under msvc05/msvc10 on the GitLab buildbot, where our C89 lint is
 * structurally blind (it would compile here and break there). */
static int split_fields(char *s, char **fields, int max)
{
   int n = 0;

   while (*s)
   {
      while (*s == ' ' || *s == '\t')
         s++;
      if (!*s)
         break;
      if (n >= max)
         return -1;
      fields[n++] = s;
      while (*s && *s != ' ' && *s != '\t')
         s++;
      if (*s)
         *s++ = '\0';
   }
   return n;
}

static int parse_hook_line(char *val, HookFileSet *out, int slot)
{
   char *fields[4];
   char *name, *offs, *exps, *pats;
   uint8_t *expectDst, *patchDst;
   unsigned long offset;
   char *endp;
   int elen, plen;

   if (split_fields(val, fields, 4) != 4)
      return 0;
   name = fields[0];
   offs = fields[1];
   exps = fields[2];
   pats = fields[3];

   if (strlen(name) >= HOOKFILE_MAX_NAME)
      return 0;

   offset = strtoul(offs, &endp, 0);  /* 0x... or decimal */
   if (endp == offs || *endp != '\0')
      return 0;
   if (offset > 0xFFFFFFFFUL)
      return 0;

   /* Each slot owns a fixed 2*MAX_BYTES window of the arena: expect first,
    * then patch.  No packing, so a short hook cannot let a later one
    * overrun. */
   expectDst = out->bytes + (size_t)slot * TITLEDB_HOOK_MAX_BYTES * 2;
   patchDst  = expectDst + TITLEDB_HOOK_MAX_BYTES;

   elen = parse_hex_bytes(exps, expectDst, TITLEDB_HOOK_MAX_BYTES);
   plen = parse_hex_bytes(pats, patchDst,  TITLEDB_HOOK_MAX_BYTES);
   if (elen <= 0 || plen <= 0 || elen != plen)
      return 0;

   strcpy(out->names[slot], name);

   out->hooks[slot].kind   = TITLEDB_HOOK_ROM_PATCH;
   out->hooks[slot].len    = (uint8_t)elen;
   out->hooks[slot].offset = (uint32_t)offset;
   out->hooks[slot].expect = expectDst;
   out->hooks[slot].patch  = patchDst;
   out->hooks[slot].name   = out->names[slot];
   return 1;
}

int HookFileLoad(const char *path, uint32_t crc, HookFileSet *out)
{
   FILE *f;
   char  line[HOOKFILE_LINE_MAX];
   int   in_section = 0;
   int   n = 0;
   int   seen_section = 0;

   if (!out)
      return 0;
   memset(out, 0, sizeof(*out));
   if (!path || !*path)
      return 0;

   f = fopen(path, "r");
   if (!f)
      return 0;                       /* absent is the normal case */

   while (fgets(line, sizeof(line), f))
   {
      char *s = trim_line(line);
      char *val;

      if (*s == '\0')
         continue;

      if (strncmp(s, "crc=", 4) == 0)
      {
         char         *endp;
         unsigned long v = strtoul(s + 4, &endp, 16);

         if (endp == s + 4 || *endp != '\0')
         {
            LOG_WRN("[hooks] %s: malformed crc= line -- ignoring the whole "
                    "file\n", path);
            fclose(f);
            memset(out, 0, sizeof(*out));
            return 0;
         }
         in_section = ((uint32_t)v == crc);
         if (in_section)
            seen_section = 1;
         continue;
      }

      if (strncmp(s, "hook=", 5) == 0)
      {
         if (!in_section)
            continue;                 /* another title's hook */

         if (n >= TITLEDB_MAX_HOOKS)
         {
            LOG_WRN("[hooks] %s: more than %d hooks for CRC32 $%08X -- "
                    "ignoring every hook in this file\n",
                    path, TITLEDB_MAX_HOOKS, (unsigned)crc);
            fclose(f);
            memset(out, 0, sizeof(*out));
            return 0;
         }

         val = s + 5;
         if (!parse_hook_line(val, out, n))
         {
            /* All-or-nothing per file: a half-understood patch set is
             * worse than none, and the applier's own all-or-nothing rule
             * would otherwise be enforced over hooks we only partly
             * parsed. */
            LOG_WRN("[hooks] %s: malformed hook= line for CRC32 $%08X -- "
                    "ignoring every hook in this file.  Expected: "
                    "hook=<name> <offset> <expect-hex> <patch-hex>\n",
                    path, (unsigned)crc);
            fclose(f);
            memset(out, 0, sizeof(*out));
            return 0;
         }
         n++;
         continue;
      }

      LOG_WRN("[hooks] %s: unrecognized line '%s' -- ignoring every hook in "
              "this file\n", path, s);
      fclose(f);
      memset(out, 0, sizeof(*out));
      return 0;
   }

   fclose(f);

   if (n == 0)
   {
      if (seen_section)
         LOG_INF("[hooks] %s: section for CRC32 $%08X carries no hooks\n",
                 path, (unsigned)crc);
      return 0;
   }

   out->count = n;
   LOG_INF("[hooks] %s: parsed %d user hook(s) for CRC32 $%08X\n",
           path, n, (unsigned)crc);
   return n;
}
