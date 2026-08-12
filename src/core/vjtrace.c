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
#include "vjag_memory.h"             /* who enum: JAGUAR, DSP, GPU, ...; jaguarMainRAM */
#include "../m68000/m68kinterface.h" /* m68k_get_reg */
#include "../tom/tom.h"              /* tomRam8 for halfline (VC) and TOMREG snapshot section */
#include "../jerry/dsp.h"            /* DSP declarations: DSPGetRAM, DSPGetFlags */

/* gpu_pc and dsp_pc are plain globals defined in src/tom/gpu.c and
 * src/jerry/dsp.c respectively; neither is declared extern in gpu.h /
 * dsp.h, so declare them locally here (types match the definitions). */
extern uint32_t gpu_pc;
extern uint32_t dsp_pc;

/* GPU register/RAM diagnostic accessors (src/tom/gpu.c) and the JERRY
 * RAM window (src/jerry/jerry.c) are, like gpu_pc/dsp_pc above, not
 * declared in any header -- GPUGetReg predates vjtrace (issue #406);
 * GPUGetRAM/GPUGetFlags were added alongside vjtrace_snapshot() below
 * for the same not-in-shipped-ABI reason (see the comments at their
 * definitions). Prototype/extern them locally here rather than growing
 * gpu.h/jerry.h's public surface for a diagnostic-only consumer.
 * jerry_ram_8 is a plain (non-static) global array in jerry.c sized to
 * cover the full $F10000-$F1FFFF JERRY register/RAM window. */
extern uint32_t GPUGetReg(int n);
extern uint8_t *GPUGetRAM(void);
extern uint32_t GPUGetFlags(void);
extern uint32_t DSPGetReg(int n);
extern uint8_t jerry_ram_8[0x10000];

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

/* PC history rings for GPU/DSP -- 68K already has one (pcQueue/pcQPtr
 * in src/core/jaguar.c); mirror its convention exactly so a single
 * helper can service all three: the head index always points at the
 * NEXT slot to write, so the most recently written entry sits at
 * (head - 1) & (cap - 1).  _fill counts entries actually pushed,
 * capped at VJT_PCHIST_CAP, so vjtrace_backtrace() can tell "ring not
 * full yet" from "ring full" and never hand back a zero-filled
 * placeholder as if it were real history (see the fill-tracked note on
 * vjtrace_backtrace() in vjtrace.h). Static: called only through the
 * two functions below (a plain function call measured the same as an
 * inlined direct-write macro here -- see vjtrace.h's PERFORMANCE
 * NOTE -- so there is no hot-path reason to expose these). */
static uint32_t gpu_pchist[VJT_PCHIST_CAP];
static uint32_t gpu_pchist_head = 0;
static uint32_t gpu_pchist_fill = 0;
static uint32_t dsp_pchist[VJT_PCHIST_CAP];
static uint32_t dsp_pchist_head = 0;
static uint32_t dsp_pchist_fill = 0;

void vjtrace_pchist_gpu(uint32_t pc)
{
   gpu_pchist[gpu_pchist_head] = pc;
   gpu_pchist_head = (gpu_pchist_head + 1) & (VJT_PCHIST_CAP - 1);
   if (gpu_pchist_fill < VJT_PCHIST_CAP)
      gpu_pchist_fill++;
}

void vjtrace_pchist_dsp(uint32_t pc)
{
   dsp_pchist[dsp_pchist_head] = pc;
   dsp_pchist_head = (dsp_pchist_head + 1) & (VJT_PCHIST_CAP - 1);
   if (dsp_pchist_fill < VJT_PCHIST_CAP)
      dsp_pchist_fill++;
}

/* Copies up to maxn entries from a head-is-next-write-slot ring of the
 * given power-of-two capacity into out[], oldest first / newest last.
 * fill bounds how many of the cap slots actually hold real (pushed)
 * data -- pass cap itself for a ring with no fill tracking (M68K's
 * pcQueue; see the KNOWN LIMITATION note on vjtrace_backtrace() in
 * vjtrace.h) or the true running push count for a fill-tracked ring
 * (GPU/DSP), so a request made before the ring has filled returns only
 * genuinely-written entries instead of zero-filled placeholders. */
static void vjt_backtrace_ring(const uint32_t *ring_buf, uint32_t head,
                                uint32_t fill, uint32_t cap, uint32_t *out,
                                int maxn, int *count)
{
   int n;
   int i;
   n = maxn;
   if (n > (int)fill)
      n = (int)fill;
   if (n > (int)cap)
      n = (int)cap;
   if (n < 0)
      n = 0;
   for (i = 0; i < n; i++)
   {
      uint32_t age = (uint32_t)(n - 1 - i);
      uint32_t idx = (head - 1 - age) & (cap - 1);
      out[i] = ring_buf[idx];
   }
   *count = n;
}

void vjtrace_backtrace(int who, uint32_t *out, int maxn, int *count)
{
   if (!count)
      return;
   if (!out || maxn <= 0)
   {
      *count = 0;
      return;
   }

   if (who == M68K)
   {
      extern uint32_t pcQueue[];
      extern uint32_t pcQPtr;
      /* pcQueue has no fill counter (see the KNOWN LIMITATION note on
       * vjtrace_backtrace() in vjtrace.h) -- assume full (fill = cap). */
      vjt_backtrace_ring(pcQueue, pcQPtr, VJT_PCHIST_CAP, VJT_PCHIST_CAP,
                          out, maxn, count);
   }
   else if (who == GPU)
   {
      vjt_backtrace_ring(gpu_pchist, gpu_pchist_head, gpu_pchist_fill,
                          VJT_PCHIST_CAP, out, maxn, count);
   }
   else if (who == DSP)
   {
      vjt_backtrace_ring(dsp_pchist, dsp_pchist_head, dsp_pchist_fill,
                          VJT_PCHIST_CAP, out, maxn, count);
   }
   else
   {
      *count = 0;
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

/* fwrite() wrapper that collapses the short-item-count failure mode
 * (disk full, I/O error) to a single boolean so every call site in
 * vjtrace_snapshot() below can be checked -- a VJSN file that exists
 * on disk must be complete; a truncated write must never look like
 * success. */
static int vjt_write(FILE *f, const void *buf, size_t size, size_t n)
{
   return (fwrite(buf, size, n, f) == n) ? 1 : 0;
}

/* Writes one VJSN section header: an 8-byte name field (NUL-padded, not
 * required to be NUL-terminated -- an 8-character name like "JERRYREG"
 * fills the field with no terminator), then base/len as individual
 * uint32_t writes (no struct-layout fwrite, so there is no compiler
 * padding to reason about -- see vjtrace_snapshot()'s header comment
 * in vjtrace.h). Caller writes the len bytes of section data itself
 * immediately after this call. Returns 1 on success, 0 on any short
 * write (see vjt_write() above). */
static int vjt_snap_section_header(FILE *f, const char *name,
                                    uint32_t base, uint32_t len)
{
   char namebuf[8];
   size_t namelen;
   memset(namebuf, 0, sizeof(namebuf));
   namelen = strlen(name);
   if (namelen > sizeof(namebuf))
      namelen = sizeof(namebuf);
   memcpy(namebuf, name, namelen);
   if (!vjt_write(f, namebuf, 1, sizeof(namebuf)))
      return 0;
   if (!vjt_write(f, &base, sizeof(uint32_t), 1))
      return 0;
   if (!vjt_write(f, &len, sizeof(uint32_t), 1))
      return 0;
   return 1;
}

int vjtrace_snapshot(const char *path)
{
   FILE *f;
   uint32_t version, nsections;
   uint32_t regs68k[18];
   uint32_t regsgpu[34];
   uint32_t regsdsp[34];
   int i;
   int ok;
   static uint32_t snapshot_ordinal = 0;

   if (!ring)
      return -1;
   f = fopen(path, "wb");
   if (!f)
      return -1;

   version = 1;
   nsections = 8;
   ok = 1;
   if (ok && !vjt_write(f, "VJSN", 1, 4))
      ok = 0;
   if (ok && !vjt_write(f, &version, sizeof(uint32_t), 1))
      ok = 0;
   if (ok && !vjt_write(f, &nsections, sizeof(uint32_t), 1))
      ok = 0;

   /* MAINRAM: low 2 MB of the Jaguar address space (jaguarMainRAM points
    * into jagMemSpace; see src/core/vjag_memory.c). NOTE: vjag_memory.c
    * also exports dead `gpuRAM`/`dspRAM` pointers into that same
    * jagMemSpace array at $F03000/$F1B000 -- those are NOT where the
    * live GPU/DSP work RAM actually lives (gpu_ram_8/dsp_ram_8, private
    * statics in gpu.c/dsp.c, read/written by every GPU/DSP memory
    * access instead); GPURAM/DSPRAM below go through GPUGetRAM() /
    * DSPGetRAM() specifically to avoid that trap. */
   if (ok && !vjt_snap_section_header(f, "MAINRAM", 0x00000000, 0x00200000))
      ok = 0;
   if (ok && !vjt_write(f, jaguarMainRAM, 1, 0x00200000))
      ok = 0;

   /* GPURAM: GPU local work RAM (gpu_ram_8, private static in gpu.c). */
   if (ok && !vjt_snap_section_header(f, "GPURAM", 0x00F03000, 0x1000))
      ok = 0;
   if (ok && !vjt_write(f, GPUGetRAM(), 1, 0x1000))
      ok = 0;

   /* DSPRAM: DSP local work RAM (dsp_ram_8, private static in dsp.c). */
   if (ok && !vjt_snap_section_header(f, "DSPRAM", 0x00F1B000, 0x2000))
      ok = 0;
   if (ok && !vjt_write(f, DSPGetRAM(), 1, 0x2000))
      ok = 0;

   /* TOMREG: full TOM register/RAM window (tomRam8, extern in tom.h). */
   if (ok && !vjt_snap_section_header(f, "TOMREG", 0x00F00000, 0x4000))
      ok = 0;
   if (ok && !vjt_write(f, tomRam8, 1, 0x4000))
      ok = 0;

   /* JERRYREG: full JERRY register/RAM window, incl. DSP control regs
    * and wavetable ROM (jerry_ram_8, plain global in jerry.c). */
   if (ok && !vjt_snap_section_header(f, "JERRYREG", 0x00F10000, 0x10000))
      ok = 0;
   if (ok && !vjt_write(f, jerry_ram_8, 1, 0x10000))
      ok = 0;

   /* REGS68K: D0-D7, A0-A7, PC, SR -- 18 uint32, via m68k_get_reg().
    * Plain register reads, no I/O -- always safe to compute even if an
    * earlier section already failed; the ok-guarded writes below just
    * skip emitting them. */
   regs68k[0] = m68k_get_reg(NULL, M68K_REG_D0);
   regs68k[1] = m68k_get_reg(NULL, M68K_REG_D1);
   regs68k[2] = m68k_get_reg(NULL, M68K_REG_D2);
   regs68k[3] = m68k_get_reg(NULL, M68K_REG_D3);
   regs68k[4] = m68k_get_reg(NULL, M68K_REG_D4);
   regs68k[5] = m68k_get_reg(NULL, M68K_REG_D5);
   regs68k[6] = m68k_get_reg(NULL, M68K_REG_D6);
   regs68k[7] = m68k_get_reg(NULL, M68K_REG_D7);
   regs68k[8] = m68k_get_reg(NULL, M68K_REG_A0);
   regs68k[9] = m68k_get_reg(NULL, M68K_REG_A1);
   regs68k[10] = m68k_get_reg(NULL, M68K_REG_A2);
   regs68k[11] = m68k_get_reg(NULL, M68K_REG_A3);
   regs68k[12] = m68k_get_reg(NULL, M68K_REG_A4);
   regs68k[13] = m68k_get_reg(NULL, M68K_REG_A5);
   regs68k[14] = m68k_get_reg(NULL, M68K_REG_A6);
   regs68k[15] = m68k_get_reg(NULL, M68K_REG_A7);
   regs68k[16] = m68k_get_reg(NULL, M68K_REG_PC);
   regs68k[17] = m68k_get_reg(NULL, M68K_REG_SR);
   if (ok && !vjt_snap_section_header(f, "REGS68K", 0, (uint32_t)sizeof(regs68k)))
      ok = 0;
   if (ok && !vjt_write(f, regs68k, sizeof(uint32_t), 18))
      ok = 0;

   /* REGSGPU: 32 regs of the CURRENT bank (GPUGetReg), then PC
    * (gpu_pc), then GPU_FLAGS (GPUGetFlags). Alternate bank not
    * captured -- see vjtrace_snapshot()'s header comment in vjtrace.h. */
   for (i = 0; i < 32; i++)
      regsgpu[i] = GPUGetReg(i);
   regsgpu[32] = gpu_pc;
   regsgpu[33] = GPUGetFlags();
   if (ok && !vjt_snap_section_header(f, "REGSGPU", 0, (uint32_t)sizeof(regsgpu)))
      ok = 0;
   if (ok && !vjt_write(f, regsgpu, sizeof(uint32_t), 34))
      ok = 0;

   /* REGSDSP: 32 regs of the CURRENT bank (DSPGetReg), then PC
    * (dsp_pc), then the DSP flags/control register (DSPGetFlags).
    * Alternate bank not captured. */
   for (i = 0; i < 32; i++)
      regsdsp[i] = DSPGetReg(i);
   regsdsp[32] = dsp_pc;
   regsdsp[33] = DSPGetFlags();
   if (ok && !vjt_snap_section_header(f, "REGSDSP", 0, (uint32_t)sizeof(regsdsp)))
      ok = 0;
   if (ok && !vjt_write(f, regsdsp, sizeof(uint32_t), 34))
      ok = 0;

   fclose(f);

   if (!ok)
   {
      /* Invariant: a VJSN file that exists on disk is complete, and a
       * VJT_EV_SNAPSHOT event implies a complete file. A mid-write
       * failure (disk full, I/O error) must leave neither a truncated
       * file nor a ring event claiming success. */
      remove(path);
      return -1;
   }

   vjtrace_emit(VJT_EV_SNAPSHOT, (uint8_t)JAGUAR, 0, snapshot_ordinal);
   snapshot_ordinal++;
   return 0;
}

#else /* !VJ_TRACE */

typedef int vjtrace_not_built;

#endif /* VJ_TRACE */
