/*
 * perf_counters.c - registry + dump for opt-in instrumentation counters.
 * Only compiled into the program when BENCH_PROFILE is defined; the header
 * provides no-op stubs otherwise.
 */
#include "perf_counters.h"

#ifdef BENCH_PROFILE

static perf_counter_entry_t *perf_head = (perf_counter_entry_t *)0;

void perf_counters_register(perf_counter_entry_t *entry)
{
   if (!entry || entry->next)
      return; /* already linked */
   entry->next = perf_head;
   perf_head = entry;
}

void perf_counters_reset(void)
{
   perf_counter_entry_t *e;
   for (e = perf_head; e; e = e->next)
      *e->value = 0;
}

void perf_counters_dump(FILE *out)
{
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
}

#endif /* BENCH_PROFILE */
