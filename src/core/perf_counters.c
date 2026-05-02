/*
 * perf_counters.c - registry + dump for opt-in instrumentation counters.
 *
 * The register/dump/reset functions are *always* defined so they can be
 * exported through the test ABI without conditional linker scripts.
 * In !BENCH_PROFILE builds the bodies are no-ops and no PERF_COUNTER
 * calls perf_counters_register, so the registry stays empty.
 */
#include "perf_counters.h"

#ifdef BENCH_PROFILE
static perf_counter_entry_t *perf_head = (perf_counter_entry_t *)0;
#endif

void perf_counters_register(perf_counter_entry_t *entry)
{
#ifdef BENCH_PROFILE
   if (!entry || entry->next)
      return; /* already linked */
   entry->next = perf_head;
   perf_head = entry;
#else
   (void)entry;
#endif
}

void perf_counters_reset(void)
{
#ifdef BENCH_PROFILE
   perf_counter_entry_t *e;
   for (e = perf_head; e; e = e->next)
      *e->value = 0;
#endif
}

void perf_counters_dump(FILE *out)
{
#ifdef BENCH_PROFILE
   perf_counter_entry_t *e;
   if (!out)
      out = stderr;
   if (!perf_head) {
      fprintf(out, "[perf] no counters registered\n");
      return;
   }
   fprintf(out, "[perf] counter dump:\n");
   for (e = perf_head; e; e = e->next)
      fprintf(out, "[perf]   %-40s %llu\n", e->name, *e->value);
#else
   (void)out;
#endif
}
