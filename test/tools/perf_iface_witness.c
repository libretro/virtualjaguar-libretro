/*
 * perf_iface_witness.c -- proves the RETRO_ENVIRONMENT_GET_PERF_INTERFACE
 * plumbing (issue #510) actually executes, end to end, through a real
 * dlopen'd core.
 *
 * Why a witness at all: nothing else in the suite exercises this path.  The
 * shared harness (test/harness/harness.c) does not answer env 28, so under
 * every other test the whole feature short-circuits at `if (vjPerfActive)`
 * and a regression that broke registration outright would leave the suite
 * green.  Same reasoning as test/tools/netlink_rebuild_witness.c, and the
 * same shape: a small standalone dlopen loader rather than a harness change,
 * because THIS test needs to BE the frontend.
 *
 * That last point is what makes the assertions strong.  The witness supplies
 * perf_register/perf_start/perf_stop itself, so it holds the very
 * `struct retro_perf_counter` objects the core registered and can read their
 * totals directly -- no dlsym of private state, no trusting the core's own
 * account of itself.
 *
 * Four things are checked:
 *
 *   1. ACCEPTED   -- with the interface offered, every slot registers, and
 *                    the ones that must run during normal emulation
 *                    accumulate a non-zero call count.
 *   2. BALANCED   -- VJPerfLeakedSlots() is 0 after a run.  Non-zero means a
 *                    probe region gained an early `return` without a matching
 *                    VJP_LEAVE, which silently wedges that slot off for the
 *                    rest of the session.  This is the regression this file
 *                    exists to catch; see the placement rule in
 *                    src/core/perf_iface.h.
 *   3. DECLINED   -- with the interface refused, vjPerfActive stays 0 and no
 *                    counter is touched.
 *   4. INERT      -- the framebuffer checksum over the run is IDENTICAL in
 *                    both arms.  Timing instrumentation that perturbs what it
 *                    measures is worse than none, and this is the assertion
 *                    that actually pins it.
 *
 * Usage:  perf_iface_witness <core> [rom]      (rom defaults to yarc.j64)
 * Exit:   0 all good, 1 a check failed, 77 the ROM is missing (skip).
 */

/* clock_gettime/CLOCK_MONOTONIC are hidden by glibc under strict -std=c99.
 * Same guard test/harness/harness.c uses: skip Apple, where defining this
 * instead HIDES BSD extensions, and skip Windows entirely. */
#if !defined(__APPLE__) && !defined(_WIN32) && !defined(_POSIX_C_SOURCE)
#define _POSIX_C_SOURCE 200809L
#endif

#include <dlfcn.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <libretro.h>

#define FRAMES 120

static int g_fail;

static void check(int ok, const char *what)
{
   printf("  %s: %s\n", ok ? "PASS" : "FAIL", what);
   if (!ok)
      g_fail++;
}

/* ---------------------------------------------------------------------
 * The frontend half: our own perf interface.
 * ------------------------------------------------------------------- */
#define MAX_SEEN 32

static struct retro_perf_counter *g_seen[MAX_SEEN];
static int   g_seen_n;
static bool  g_offer_perf;      /* arm switch: offer or decline env 28 */
static long  g_starts, g_stops;

static retro_perf_tick_t now_ticks(void)
{
   struct timespec ts;
   clock_gettime(CLOCK_MONOTONIC, &ts);
   return (retro_perf_tick_t)ts.tv_sec * 1000000000ull
        + (retro_perf_tick_t)ts.tv_nsec;
}

static retro_time_t get_time_usec(void) { return (retro_time_t)(now_ticks() / 1000); }
static uint64_t     get_cpu_features(void) { return 0; }

static void perf_register(struct retro_perf_counter *c)
{
   if (!c || g_seen_n >= MAX_SEEN)
      return;                       /* a real frontend caps this too */
   g_seen[g_seen_n++] = c;
   c->registered = true;            /* the frontend owns this flag */
}

static void perf_start(struct retro_perf_counter *c)
{
   if (!c || !c->registered)
      return;
   c->start = now_ticks();
   c->call_cnt++;
   g_starts++;
}

static void perf_stop(struct retro_perf_counter *c)
{
   if (!c || !c->registered)
      return;
   c->total += now_ticks() - c->start;
   g_stops++;
}

static void perf_log(void) {}

/* ---------------------------------------------------------------------
 * Minimal libretro frontend
 * ------------------------------------------------------------------- */
static uint64_t g_fb_hash;

static void log_cb(enum retro_log_level lvl, const char *fmt, ...)
{ (void)lvl; (void)fmt; }

static bool env_cb(unsigned cmd, void *data)
{
   switch (cmd)
   {
      case RETRO_ENVIRONMENT_GET_PERF_INTERFACE:
      {
         struct retro_perf_callback *cb;
         if (!g_offer_perf)
            return false;                  /* the DECLINED arm */
         cb = (struct retro_perf_callback *)data;
         cb->get_time_usec    = get_time_usec;
         cb->get_cpu_features = get_cpu_features;
         cb->get_perf_counter = now_ticks;
         cb->perf_register    = perf_register;
         cb->perf_start       = perf_start;
         cb->perf_stop        = perf_stop;
         cb->perf_log         = perf_log;
         return true;
      }

      case RETRO_ENVIRONMENT_GET_LOG_INTERFACE:
         ((struct retro_log_callback *)data)->log = log_cb;
         return true;

      case RETRO_ENVIRONMENT_GET_SYSTEM_DIRECTORY:
      case RETRO_ENVIRONMENT_GET_SAVE_DIRECTORY:
         *(const char **)data = ".";
         return true;

      case RETRO_ENVIRONMENT_SET_PIXEL_FORMAT:
      case RETRO_ENVIRONMENT_SET_MEMORY_MAPS:
      case RETRO_ENVIRONMENT_SET_SUPPORT_ACHIEVEMENTS:
      case RETRO_ENVIRONMENT_SET_CORE_OPTIONS_V2:
      case RETRO_ENVIRONMENT_SET_CORE_OPTIONS_UPDATE_DISPLAY_CALLBACK:
      case RETRO_ENVIRONMENT_SET_CORE_OPTIONS_DISPLAY:
      case RETRO_ENVIRONMENT_SET_PERFORMANCE_LEVEL:
      case RETRO_ENVIRONMENT_SET_INPUT_DESCRIPTORS:
      case RETRO_ENVIRONMENT_SET_CONTROLLER_INFO:
         return true;

      case RETRO_ENVIRONMENT_GET_CORE_OPTIONS_VERSION:
         *(unsigned *)data = 2;
         return true;

      case RETRO_ENVIRONMENT_GET_VARIABLE:
         ((struct retro_variable *)data)->value = NULL;
         return true;
   }
   return false;
}

/* FNV-1a over the presented frame.  Cheap, and enough to catch the
 * instrumentation perturbing what it measures. */
static void video_cb(const void *data, unsigned w, unsigned h, size_t pitch)
{
   const uint8_t *p = (const uint8_t *)data;
   unsigned y;

   if (!p)
      return;
   for (y = 0; y < h; y++)
   {
      const uint8_t *row = p + (size_t)y * pitch;
      size_t i;
      for (i = 0; i < (size_t)w * 4; i++)
      {
         g_fb_hash ^= row[i];
         g_fb_hash *= 1099511628211ull;
      }
   }
}

static void   audio_cb(int16_t l, int16_t r) { (void)l; (void)r; }
static size_t audio_batch_cb(const int16_t *d, size_t f) { (void)d; return f; }
static void   input_poll_cb(void) {}
static int16_t input_state_cb(unsigned a, unsigned b, unsigned c, unsigned d)
{ (void)a; (void)b; (void)c; (void)d; return 0; }

typedef void (*set_env_fn)(retro_environment_t);
typedef void (*set_video_fn)(retro_video_refresh_t);
typedef void (*set_audio_fn)(retro_audio_sample_t);
typedef void (*set_batch_fn)(retro_audio_sample_batch_t);
typedef void (*set_poll_fn)(retro_input_poll_t);
typedef void (*set_state_fn)(retro_input_state_t);
typedef void (*void_fn)(void);
typedef bool (*load_fn)(const struct retro_game_info *);
typedef int  (*int_fn)(void);

/* Run one arm.  Returns the framebuffer hash. */
static uint64_t run_arm(void *lib, const char *rom, bool offer,
                        int *out_active, int *out_registered, int *out_leaked)
{
   struct retro_game_info info;
   void *buf;
   long  len;
   FILE *f;
   int  *p_active;

   g_offer_perf = offer;
   g_seen_n     = 0;
   g_starts     = 0;
   g_stops      = 0;
   g_fb_hash    = 1469598103934665603ull;
   memset(g_seen, 0, sizeof(g_seen));

   ((set_env_fn)  dlsym(lib, "retro_set_environment"))(env_cb);
   ((set_video_fn)dlsym(lib, "retro_set_video_refresh"))(video_cb);
   ((set_audio_fn)dlsym(lib, "retro_set_audio_sample"))(audio_cb);
   ((set_batch_fn)dlsym(lib, "retro_set_audio_sample_batch"))(audio_batch_cb);
   ((set_poll_fn) dlsym(lib, "retro_set_input_poll"))(input_poll_cb);
   ((set_state_fn)dlsym(lib, "retro_set_input_state"))(input_state_cb);

   ((void_fn)dlsym(lib, "retro_init"))();

   f = fopen(rom, "rb");
   fseek(f, 0, SEEK_END); len = ftell(f); fseek(f, 0, SEEK_SET);
   buf = malloc((size_t)len);
   if (fread(buf, 1, (size_t)len, f) != (size_t)len) { /* short read: let the core reject it */ }
   fclose(f);

   memset(&info, 0, sizeof(info));
   info.path = rom;
   info.data = buf;
   info.size = (size_t)len;

   if (((load_fn)dlsym(lib, "retro_load_game"))(&info))
   {
      void_fn run = (void_fn)dlsym(lib, "retro_run");
      int i;
      for (i = 0; i < FRAMES; i++)
         run();
   }

   p_active        = (int *)dlsym(lib, "vjPerfActive");
   *out_active     = p_active ? *p_active : -1;
   *out_registered = ((int_fn)dlsym(lib, "VJPerfRegisteredCount"))();
   *out_leaked     = ((int_fn)dlsym(lib, "VJPerfLeakedSlots"))();

   ((void_fn)dlsym(lib, "retro_unload_game"))();
   ((void_fn)dlsym(lib, "retro_deinit"))();
   free(buf);
   return g_fb_hash;
}

int main(int argc, char **argv)
{
   const char *core = argc > 1 ? argv[1] : NULL;
   const char *rom  = argc > 2 ? argv[2] : "test/roms/yarc.j64";
   void *lib;
   uint64_t hash_on, hash_off;
   int active_on = 0, reg_on = 0, leak_on = 0;
   int active_off = 0, reg_off = 0, leak_off = 0;
   int registered_snapshot, nonzero_calls, i;
   FILE *probe;

   if (!core)
   {
      fprintf(stderr, "usage: %s <core> [rom]\n", argv[0]);
      return 1;
   }
   probe = fopen(rom, "rb");
   if (!probe)
   {
      printf("SKIP: no ROM at %s\n", rom);
      return 77;
   }
   fclose(probe);

   lib = dlopen(core, RTLD_NOW);
   if (!lib)
   {
      fprintf(stderr, "dlopen %s: %s\n", core, dlerror());
      return 1;
   }
   if (!dlsym(lib, "VJPerfRegisteredCount"))
   {
      fprintf(stderr, "core lacks VJPerf* exports -- build with TEST_EXPORTS=1\n");
      return 1;
   }

   printf("perf_iface_witness: %s\n", rom);

   /* --- arm 1: the frontend offers the interface --- */
   hash_on = run_arm(lib, rom, true, &active_on, &reg_on, &leak_on);
   registered_snapshot = g_seen_n;
   nonzero_calls = 0;
   for (i = 0; i < g_seen_n; i++)
   {
      if (g_seen[i]->call_cnt > 0)
         nonzero_calls++;
   }

   printf("  offered : active=%d registered=%d counters_seen=%d "
          "with_calls=%d starts=%ld stops=%ld\n",
          active_on, reg_on, registered_snapshot, nonzero_calls,
          g_starts, g_stops);
   for (i = 0; i < registered_snapshot; i++)
      printf("      %-16s calls=%-10llu total=%llu\n", g_seen[i]->ident,
             (unsigned long long)g_seen[i]->call_cnt,
             (unsigned long long)g_seen[i]->total);

   check(active_on == 1, "vjPerfActive set when the frontend offers env 28");
   check(reg_on == registered_snapshot && reg_on > 0,
         "core's registered count matches what the frontend actually saw");
   check(nonzero_calls >= 4,
         "the per-frame probes accumulate calls during emulation");
   check(g_starts == g_stops,
         "every perf_start is matched by a perf_stop");
   check(leak_on == 0,
         "no probe region leaked its nesting depth (see perf_iface.h)");

   /* --- arm 2: the frontend refuses --- */
   hash_off = run_arm(lib, rom, false, &active_off, &reg_off, &leak_off);
   printf("  declined: active=%d registered=%d counters_seen=%d starts=%ld\n",
          active_off, reg_off, g_seen_n, g_starts);

   check(active_off == 0, "vjPerfActive stays clear when env 28 is refused");
   check(reg_off == 0 && g_seen_n == 0, "no counter registered when refused");
   check(g_starts == 0, "no counter touched when refused");

   /* --- the assertion that matters most --- */
   printf("  framebuffer: offered=%016llx declined=%016llx\n",
          (unsigned long long)hash_on, (unsigned long long)hash_off);
   check(hash_on == hash_off,
         "instrumentation is inert: identical framebuffer with it on and off");

   printf("perf_iface_witness: %s\n", g_fail ? "FAILED" : "OK");
   return g_fail ? 1 : 0;
}
