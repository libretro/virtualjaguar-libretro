/*
 * perf_iface.c -- RETRO_ENVIRONMENT_GET_PERF_INTERFACE plumbing (issue #510).
 *
 * See perf_iface.h for what these counters mean, why they overlap, and the
 * placement rule for probe sites.
 */

#include <string.h>

#include "perf_iface.h"
#include "log.h"

int vjPerfActive = 0;

/*
 * Identifiers the frontend displays.  Prefixed so they are attributable in a
 * RetroArch performance-counter list that also holds the frontend's own.
 * Order must match the enum in perf_iface.h.
 */
static const char * const vjperf_idents[VJP_COUNT] =
{
   "vj_m68k_slice",
   "vj_gpu_exec",
   "vj_dsp_exec",
   "vj_gpu_sync",
   "vj_dsp_sync",
   "vj_op_halfline",
   "vj_blitter",
   "vj_dac_mix"
};

static struct retro_perf_callback vjperf_cb;
static struct retro_perf_counter vjperf_counters[VJP_COUNT];

/*
 * Nesting depth per slot.  Required, not defensive: GPUExec() is genuinely
 * re-entrant -- the Object Processor runs the GPU inline from a halfline
 * callback (src/tom/op.c), and the 68K bus dispatch runs it from
 * GPUSyncToM68K().  perf_start() overwrites the counter's start tick, so an
 * unguarded re-entry would make the OUTER span measure only the inner one and
 * inflate call_cnt.  Counting only the outermost span is both correct and the
 * number a reader wants.
 */
static unsigned vjperf_depth[VJP_COUNT];

static int vjperf_registered;

void VJPerfInit(retro_environment_t env_cb)
{
   int i;

   vjPerfActive      = 0;
   vjperf_registered = 0;
   memset(&vjperf_cb, 0, sizeof(vjperf_cb));
   memset(vjperf_counters, 0, sizeof(vjperf_counters));
   memset(vjperf_depth, 0, sizeof(vjperf_depth));

   if (!env_cb)
      return;

   if (!env_cb(RETRO_ENVIRONMENT_GET_PERF_INTERFACE, &vjperf_cb))
   {
      /* Entirely normal.  Plenty of frontends do not implement env 28, and
       * a core that whinges about it in the log every launch is noise. */
      memset(&vjperf_cb, 0, sizeof(vjperf_cb));
      return;
   }

   /* A frontend may answer true and still leave members NULL -- the struct
    * is not versioned, so treat every pointer as optional and require only
    * the three we actually call. */
   if (!vjperf_cb.perf_register || !vjperf_cb.perf_start || !vjperf_cb.perf_stop)
   {
      LOG_WRN("[PERF] frontend offered an incomplete perf interface; profiling counters disabled\n");
      memset(&vjperf_cb, 0, sizeof(vjperf_cb));
      return;
   }

   for (i = 0; i < VJP_COUNT; i++)
   {
      /* The contract: ident set, everything else zero, before registering.
       * memset above did the rest. */
      vjperf_counters[i].ident = vjperf_idents[i];
      vjperf_cb.perf_register(&vjperf_counters[i]);
      if (vjperf_counters[i].registered)
         vjperf_registered++;
   }

   /* Registration can fail per-counter: the frontend has a fixed maximum and
    * ours are not the only ones competing for it.  Partial success is still
    * useful, so go active on any, and say how many landed rather than
    * implying all of them did. */
   if (vjperf_registered > 0)
   {
      vjPerfActive = 1;
      LOG_INF("[PERF] performance counters active (%d of %d registered)\n",
              vjperf_registered, (int)VJP_COUNT);
   }
   else
   {
      LOG_WRN("[PERF] frontend accepted no performance counters; profiling disabled\n");
      memset(&vjperf_cb, 0, sizeof(vjperf_cb));
   }
}

void VJPerfDeinit(void)
{
   /* Ask the frontend to print totals on the way out.  RetroArch shows the
    * same numbers in its UI, but a log line is what can actually be got off
    * a locked-down tvOS device -- which is the case this exists for. */
   if (vjPerfActive && vjperf_cb.perf_log)
      vjperf_cb.perf_log();

   vjPerfActive      = 0;
   vjperf_registered = 0;
   memset(&vjperf_cb, 0, sizeof(vjperf_cb));
   memset(vjperf_depth, 0, sizeof(vjperf_depth));
}

void VJPerfEnter(int slot)
{
   if (slot < 0 || slot >= VJP_COUNT)
      return;

   if (vjperf_depth[slot]++ == 0 && vjperf_counters[slot].registered)
      vjperf_cb.perf_start(&vjperf_counters[slot]);
}

void VJPerfLeave(int slot)
{
   if (slot < 0 || slot >= VJP_COUNT)
      return;

   /* Depth 0 here means a LEAVE without a matching ENTER.  It happens for one
    * benign reason: the frontend can hand us the interface partway through a
    * session, so a region already in flight reaches its LEAVE having never
    * run an ENTER.  Clamp rather than wrap the unsigned to a huge value,
    * which would wedge the slot off for the rest of the run. */
   if (vjperf_depth[slot] == 0)
      return;

   if (--vjperf_depth[slot] == 0 && vjperf_counters[slot].registered)
      vjperf_cb.perf_stop(&vjperf_counters[slot]);
}

int VJPerfRegisteredCount(void)
{
   return vjperf_registered;
}

int VJPerfLeakedSlots(void)
{
   int i;
   int leaked = 0;

   for (i = 0; i < VJP_COUNT; i++)
   {
      if (vjperf_depth[i] != 0)
         leaked++;
   }

   return leaked;
}
