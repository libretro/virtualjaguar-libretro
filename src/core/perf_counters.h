/*
 * perf_counters.h - lightweight, opt-in instrumentation counters.
 *
 * Define BENCH_PROFILE at compile time to enable. Otherwise every macro
 * expands to (void)0 and there is no runtime, code-size, or symbol cost.
 *
 * Usage:
 *
 *   #include "perf_counters.h"
 *
 *   PERF_COUNTER(blitter_inner);
 *   PERF_COUNTER(blitter_phrase_reads);
 *
 *   void hot(void) {
 *       PERF_INC(blitter_inner);
 *       PERF_ADD(blitter_phrase_reads, 2);
 *   }
 *
 *   // Somewhere at shutdown (e.g., test harness atexit):
 *   perf_counters_dump(stderr);
 *
 * Counters self-register via constructor functions, so PERF_COUNTER must
 * appear at file scope. Only one definition per name across the program.
 *
 * C89-clean. No designated initializers, no mid-block declarations.
 */
#ifndef VJ_PERF_COUNTERS_H
#define VJ_PERF_COUNTERS_H

#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Registry types and entry points are *always* declared so the test
 * ABI can export them unconditionally.  When BENCH_PROFILE is undefined
 * the bodies (in perf_counters.c) become no-ops and no PERF_COUNTER
 * macro registers anything, so the registry stays empty. */

typedef struct perf_counter_entry
{
   const char *name;
   unsigned long long *value;
   struct perf_counter_entry *next;
} perf_counter_entry_t;

void perf_counters_register(perf_counter_entry_t *entry);
void perf_counters_dump(FILE *out);
void perf_counters_reset(void);
/* Return a pointer to the named counter's value, or NULL if unknown.
 * Lets harnesses snapshot a counter before/after retro_run for
 * per-frame deltas without exporting individual symbols. */
unsigned long long *perf_counters_find(const char *name);

#ifdef BENCH_PROFILE

#define PERF_COUNTER(name) \
   static unsigned long long perf_##name = 0; \
   static perf_counter_entry_t perf_entry_##name = \
      { #name, &perf_##name, (perf_counter_entry_t *)0 }; \
   __attribute__((constructor)) \
   static void perf_register_##name(void) { \
      perf_counters_register(&perf_entry_##name); \
   } \
   typedef int perf_##name##_decl_semicolon_eater

/* PERF_INC / PERF_ADD are expressions of integer type (not statements),
 * so they can be embedded in declaration initializers via the comma
 * operator without violating C89's no-decl-after-statement rule:
 *   uint32_t cmd = (PERF_INC(my_event), real_value());
 */
#define PERF_INC(name)    (++perf_##name)
#define PERF_ADD(name, n) (perf_##name += (unsigned long long)(n))

#else /* !BENCH_PROFILE */

#define PERF_COUNTER(name) typedef int perf_##name##_unused
/* No-op forms remain expressions of integer type (not void) so callers
 * can use them inside comma operators without code changes. */
#define PERF_INC(name)        (0)
#define PERF_ADD(name, n)     ((void)(n), 0)

#endif /* BENCH_PROFILE */

#ifdef __cplusplus
}
#endif

#endif /* VJ_PERF_COUNTERS_H */
