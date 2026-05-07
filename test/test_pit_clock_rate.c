/*
 * test_pit_clock_rate.c -- regression guard for the recurring PIT clock
 * rate bug.
 *
 * Per JTRM (docs/jtrm-clocks-timing.md): "The PIT clock is the FULL
 * system clock (26.59 MHz), NOT half. ... If you see system_clock / 2
 * in PIT calculations, it is a bug."
 *
 * This bug has been re-introduced multiple times with the rationale
 * that "Doom plays correctly at half rate" -- but actually halving the
 * PIT rate breaks Doom and Rayman music timing.
 *
 * This test is a textual guard that fails the build if anyone schedules
 * JERRYPIT1/2 or TOMPIT callbacks using the M68K clock constants
 * (M68K_CYCLE_IN_USEC / M68K_CYCLE_PAL_IN_USEC) instead of the RISC
 * constants (RISC_CYCLE_IN_USEC / RISC_CYCLE_PAL_IN_USEC), or if the
 * frequency helpers use M68K_CLOCK_RATE_NTSC/PAL.
 *
 * If a real cycle-accurate emulation reason ever surfaces to change
 * this, update the test and the JTRM doc together -- don't just edit
 * the source.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int g_failures = 0;

static char *load_file(const char *path)
{
   FILE *f = fopen(path, "rb");
   long sz;
   char *buf;
   size_t got;
   if (!f)
   {
      fprintf(stderr, "FATAL: cannot open %s\n", path);
      exit(2);
   }
   fseek(f, 0, SEEK_END);
   sz = ftell(f);
   fseek(f, 0, SEEK_SET);
   buf = (char *)malloc((size_t)sz + 1);
   got = fread(buf, 1, (size_t)sz, f);
   buf[got] = 0;
   fclose(f);
   return buf;
}

/* Returns pointer to the start of `func_name`'s body in `src`, or NULL.
 * Does the dumb thing: finds "func_name(", then walks to the next '{'.
 * Returns the matching '}' end via *end_out.
 *
 * This is good enough for our two callers (well-formed C, no nested
 * function definitions, single open-brace function bodies).
 */
static char *find_func_body(const char *src, const char *func_name, char **end_out)
{
   const char *p;
   const char *open;
   int depth;
   size_t needle_len;
   char needle[128];
   snprintf(needle, sizeof(needle), "%s(", func_name);
   needle_len = strlen(needle);

   p = src;
   while ((p = strstr(p, needle)) != NULL)
   {
      /* Walk forward to first '{' that opens a body. Bail if we hit ';'
       * before '{' (that's a forward declaration, not a definition). */
      const char *q = p + needle_len;
      while (*q && *q != '{' && *q != ';')
         q++;
      if (*q != '{')
      {
         p = q;
         continue;
      }
      open = q;
      depth = 0;
      while (*q)
      {
         if (*q == '{')
            depth++;
         else if (*q == '}')
         {
            depth--;
            if (depth == 0)
            {
               *end_out = (char *)q;
               return (char *)open;
            }
         }
         q++;
      }
      return NULL;  /* unbalanced */
   }
   return NULL;
}

static void check_func_uses_risc(const char *path, const char *src, const char *func)
{
   char *end = NULL;
   char *body = find_func_body(src, func, &end);
   size_t len;
   char *snippet;
   int has_risc, has_m68k;

   if (!body || !end)
   {
      fprintf(stderr, "FAIL: %s: cannot locate function %s\n", path, func);
      g_failures++;
      return;
   }
   len = (size_t)(end - body);
   snippet = (char *)malloc(len + 1);
   memcpy(snippet, body, len);
   snippet[len] = 0;

   has_risc = (strstr(snippet, "RISC_CYCLE_IN_USEC") != NULL)
      || (strstr(snippet, "RISC_CYCLE_PAL_IN_USEC") != NULL)
      || (strstr(snippet, "RISC_CLOCK_RATE_NTSC") != NULL)
      || (strstr(snippet, "RISC_CLOCK_RATE_PAL") != NULL)
      || (strstr(snippet, "SYSTEM_CLOCK_RATE") != NULL);
   has_m68k = (strstr(snippet, "M68K_CYCLE_IN_USEC") != NULL)
      || (strstr(snippet, "M68K_CYCLE_PAL_IN_USEC") != NULL)
      || (strstr(snippet, "M68K_CLOCK_RATE_NTSC") != NULL)
      || (strstr(snippet, "M68K_CLOCK_RATE_PAL") != NULL);

   if (has_m68k)
   {
      fprintf(stderr,
            "FAIL: %s: function %s references M68K_*_IN_USEC / "
            "M68K_CLOCK_RATE_*\n"
            "      PIT clock must be RISC (full system clock) per JTRM.\n"
            "      See docs/jtrm-clocks-timing.md.\n",
            path, func);
      g_failures++;
   }
   if (!has_risc)
   {
      fprintf(stderr,
            "FAIL: %s: function %s does not reference any RISC_*_IN_USEC "
            "constant.\n"
            "      Expected the PIT scheduling to use RISC_CYCLE_IN_USEC / "
            "RISC_CYCLE_PAL_IN_USEC.\n",
            path, func);
      g_failures++;
   }
   free(snippet);
}

int main(int argc, char **argv)
{
   const char *jerry_path = "src/jerry/jerry.c";
   const char *tom_path = "src/tom/tom.c";
   char *jerry_src;
   char *tom_src;

   if (argc > 1)
      jerry_path = argv[1];
   if (argc > 2)
      tom_path = argv[2];

   jerry_src = load_file(jerry_path);
   tom_src = load_file(tom_path);

   check_func_uses_risc(jerry_path, jerry_src, "JERRYResetPIT1");
   check_func_uses_risc(jerry_path, jerry_src, "JERRYResetPIT2");
   check_func_uses_risc(jerry_path, jerry_src, "JERRYGetPIT1Frequency");
   check_func_uses_risc(jerry_path, jerry_src, "JERRYGetPIT2Frequency");
   check_func_uses_risc(tom_path, tom_src, "TOMResetPIT");

   free(jerry_src);
   free(tom_src);

   if (g_failures)
   {
      fprintf(stderr, "FAIL: %d PIT clock rate violation(s) detected.\n",
            g_failures);
      return 1;
   }
   printf("PASS: PIT scheduling functions use the RISC (full system clock) "
         "rate.\n");
   return 0;
}
