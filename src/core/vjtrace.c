/* vjtrace.c -- core-side flight-recorder trace facility implementation.
 *
 * Entire body is compiled only when VJ_TRACE is defined (see
 * docs/vjtrace-design.md); the Makefile sets -DVJ_TRACE only inside the
 * TEST_EXPORTS=1 branch, so shipped builds carry none of this code and
 * export none of these symbols.
 */
#ifdef VJ_TRACE

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "vjtrace.h"
#include "vjag_memory.h"             /* who enum: JAGUAR, DSP, GPU, ... */
#include "../m68000/m68kinterface.h" /* m68k_get_reg */
#include "../tom/tom.h"              /* tomRam8 for halfline (VC) */
#include "../jerry/dsp.h"            /* DSP declarations */

/* gpu_pc and dsp_pc are plain globals defined in src/tom/gpu.c and
 * src/jerry/dsp.c respectively; neither is declared extern in gpu.h /
 * dsp.h, so declare them locally here (types match the definitions). */
extern uint32_t gpu_pc;
extern uint32_t dsp_pc;

typedef struct { uint32_t lo, hi; unsigned rw; } vjt_watch;

vjtrace_counters_t vjtrace_counters;
uint32_t vjtrace_nwatch = 0;
static vjt_watch watches[16];
static vjtrace_ev *ring = NULL;
static uint64_t ring_cap = 0;     /* power of two not required; use modulo */
static uint64_t ring_head = 0;    /* total events ever emitted */
static uint64_t seq_ctr = 0;
static uint32_t cur_frame = 0;

void vjtrace_init(void)
{
   const char *env;
   uint64_t cap = (uint64_t)1 << 20;
   if (ring)
      return;
   env = getenv("VJ_TRACE_RING");
   if (env && atol(env) > 0)
      cap = (uint64_t)atol(env);
   ring = (vjtrace_ev *)calloc((size_t)cap, sizeof(vjtrace_ev));
   ring_cap = ring ? cap : 0;
}

void vjtrace_frame_tick(uint32_t frame) { cur_frame = frame; }

static uint32_t vjt_pc_of(uint32_t who)
{
   if (who == M68K || who == JAGUAR)
      return m68k_get_reg(NULL, M68K_REG_PC);
   if (who == GPU)
      return gpu_pc;
   if (who == DSP)
      return dsp_pc;
   return 0;
}

void vjtrace_emit(uint8_t type, uint8_t who, uint32_t addr, uint32_t value)
{
   vjtrace_ev *e;
   if (!ring)
      return;
   e = &ring[ring_head % ring_cap];
   e->seq = seq_ctr++;
   e->frame = cur_frame;
   e->halfline = (uint16_t)((tomRam8[0x06] << 8) | tomRam8[0x07]);
   e->type = type;
   e->who = (uint8_t)who;
   e->pc = vjt_pc_of(who);
   e->addr = addr;
   e->value = value;
   ring_head++;
   if (type < VJT_EV__COUNT)
      vjtrace_counters.ev[type]++;
}

int vjtrace_watch_add(uint32_t lo, uint32_t hi, unsigned rw)
{
   if (vjtrace_nwatch >= 16)
      return -1;
   watches[vjtrace_nwatch].lo = lo;
   watches[vjtrace_nwatch].hi = hi;
   watches[vjtrace_nwatch].rw = rw;
   return (int)vjtrace_nwatch++;
}

void vjtrace_watch_clear(void) { vjtrace_nwatch = 0; }

void vjtrace_watch_check(uint32_t addr, uint32_t value, uint32_t who, int is_write)
{
   uint32_t i;
   unsigned need = is_write ? 2u : 1u;
   for (i = 0; i < vjtrace_nwatch; i++)
   {
      if (addr >= watches[i].lo && addr <= watches[i].hi && (watches[i].rw & need))
      {
         vjtrace_emit(is_write ? VJT_EV_WATCH_WR : VJT_EV_WATCH_RD,
                      (uint8_t)who, addr, value);
         return;
      }
   }
}

int vjtrace_dump(const char *path)
{
   FILE *f;
   uint64_t n, start, i;
   struct { char magic[4]; uint32_t version, ev_size, pad; uint64_t count; } hdr;
   if (!ring)
      return -1;
   f = fopen(path, "wb");
   if (!f)
      return -1;
   n = (ring_head < ring_cap) ? ring_head : ring_cap;
   start = ring_head - n;
   memcpy(hdr.magic, "VJTR", 4);
   hdr.version = 1; hdr.ev_size = (uint32_t)sizeof(vjtrace_ev); hdr.pad = 0;
   hdr.count = n;
   fwrite(&hdr, sizeof(hdr), 1, f);
   for (i = 0; i < n; i++)
      fwrite(&ring[(start + i) % ring_cap], sizeof(vjtrace_ev), 1, f);
   fclose(f);
   return 0;
}

#else /* !VJ_TRACE */

typedef int vjtrace_not_built;

#endif /* VJ_TRACE */
