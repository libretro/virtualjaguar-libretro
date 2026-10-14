/* vjtrace.c -- core-side flight-recorder trace facility implementation.
 *
 * Entire body is compiled only when VJ_TRACE is defined (see
 * docs/vjtrace-design.md); the Makefile sets -DVJ_TRACE only inside the
 * TEST_EXPORTS=1 branch, so shipped builds carry none of this code and
 * export none of these symbols.
 */
#ifdef VJ_TRACE

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "vjtrace.h"
#include "vjag_memory.h"             /* who enum: JAGUAR, DSP, GPU, ...; jaguarMainRAM */
#include "../m68000/m68kinterface.h" /* m68k_get_reg */
#include "../tom/tom.h"              /* tomRam8 for halfline (VC) and TOMREG snapshot section */
#include "../jerry/dsp.h"            /* DSP declarations: DSPGetRAM, DSPGetFlags */
#include "bus_arbiter.h"             /* busArbiter MEMCON-derived costs (bus census) */
#include "perf_counters.h"           /* perf_counters_find (bus census) */

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
/* Default OFF: every hot-path recording site (vjtrace_emit() and the
 * GPU/DSP per-instruction PC-history hooks below) checks this first and
 * returns immediately when clear, so a VJ_TRACE build that never traces
 * pays one predictable, well-predicted branch per site instead of doing
 * real ring/history work -- see the PERFORMANCE NOTE in vjtrace.h. */
/* Non-static: the VJT_PCHIST_* macros in vjtrace.h test this at the
 * call site inside GPUExec()/DSPExec(), so it must be linkable. */
int vjtrace_armed = 0;

/* Bus census (S2) hooks used by vjtrace_emit()/vjtrace_pchist_dsp();
 * defined further down. */
static void vjt_bus_blit_cmd(uint32_t cmd);
static void vjt_bus_dsp_insn(uint32_t pc);
static void vjt_bus_inject_init(void);

void vjtrace_arm(void)    { vjtrace_armed = 1; }
void vjtrace_disarm(void) { vjtrace_armed = 0; }

void vjtrace_init(void)
{
   const char *env;
   uint64_t cap = (uint64_t)1 << 20;   /* default: 1M records (~32 MiB) */
   /* Hard ceiling on the record count: keeps calloc()'s byte count
    * representable in size_t on every host (32-bit included) and stops
    * a huge or typo'd VJ_TRACE_RING from demanding an unreasonable
    * allocation.  1<<25 (33.5M records, ~1 GiB at 32 bytes/record) sits
    * comfortably above any run this project has needed so far (see the
    * vjtrace flight recorder ring-sizing note in CLAUDE.md: a 1800-frame
    * yarc.j64 run needed VJ_TRACE_RING=6000000). */
   const uint64_t cap_max = (uint64_t)1 << 25;
   if (ring)
      return;
   env = getenv("VJ_TRACE_RING");
   if (env && env[0] != '\0' && env[0] != '-')
   {
      char *endptr = NULL;
      unsigned long parsed;
      errno = 0;
      parsed = strtoul(env, &endptr, 10);
      /* Accept only a fully-numeric, in-range, positive value: reject
       * "abc" (endptr == env, nothing consumed), trailing garbage
       * (*endptr != '\0'), and overflow (errno == ERANGE, e.g.
       * VJ_TRACE_RING=99999999999999999999).  Anything rejected falls
       * through and keeps the default cap above. */
      if (endptr != env && *endptr == '\0' && errno == 0 && parsed > 0)
         cap = (uint64_t)parsed;
   }
   if (cap > cap_max)
      cap = cap_max;
   if (cap < 1)
      cap = 1;
   ring = (vjtrace_ev *)calloc((size_t)cap, sizeof(vjtrace_ev));
   if (!ring && cap != ((uint64_t)1 << 20))
   {
      /* Requested cap couldn't be allocated -- fall back to the default
       * instead of leaving tracing silently disabled for the whole run. */
      cap = (uint64_t)1 << 20;
      ring = (vjtrace_ev *)calloc((size_t)cap, sizeof(vjtrace_ev));
   }
   ring_cap = ring ? cap : 0;
   vjt_bus_inject_init();
}

void vjtrace_frame_tick(uint32_t frame) { cur_frame = frame; }

static uint32_t vjt_pc_of(uint32_t who)
{
   if (who == M68K || who == JAGUAR)
   {
      /* m68k_get_reg(M68K_REG_PC) is regs.pc AT THE INSTANT OF THIS
       * CALL, which is almost never the PC of the instruction that
       * made the access being recorded: m68ki_incpc() (or the
       * per-handler equivalent) advances regs.pc at or near the TOP of
       * nearly every opcode handler (859 of 861, measured empirically),
       * before that handler's own memory access runs, so by the time
       * a read/write hook fires regs.pc already names the START of the
       * *next* instruction's fetch.
       *
       * pcQueue/pcQPtr (src/core/jaguar.c) is the fix, and is already
       * exactly what vjtrace_backtrace() below relies on for the same
       * reason: M68KInstructionHook() latches regs.pc into
       * pcQueue[pcQPtr] and only then increments pcQPtr, once per
       * instruction, called from src/m68000/m68kinterface.c
       * immediately before that instruction's opcode dispatch -- i.e.
       * strictly before any memory access the instruction makes. So
       * the most recently queued entry, at
       * pcQueue[(pcQPtr - 1) & (VJT_PCHIST_CAP - 1)], is always the
       * start PC of whichever instruction is CURRENTLY executing: the
       * one actually responsible for the access, regardless of how far
       * its handler has since advanced regs.pc.
       *
       * pcQPtr can only index a possibly-unwritten slot before the
       * very first 68K instruction of the whole session has executed
       * (pcQueue is a plain BSS array; an unwritten slot reads back as
       * PC=0 rather than crashing or returning garbage -- the same
       * KNOWN LIMITATION already documented on vjtrace_backtrace() in
       * vjtrace.h). M68KInstructionHook() runs unconditionally from
       * that first instruction on and always runs before any memory
       * access that instruction makes, so by the time this function is
       * reachable from a real M68K-attributed bus access, pcQPtr is
       * already >= 1 and that pre-first-instruction case cannot
       * actually occur for who == M68K; it remains possible in
       * principle for who == JAGUAR (host/init-time direct pokes
       * before the 68K core has run at all). */
      extern uint32_t pcQueue[];
      extern uint32_t pcQPtr;
      return pcQueue[(pcQPtr - 1) & (VJT_PCHIST_CAP - 1)];
   }
   if (who == GPU)
      return gpu_pc;
   if (who == DSP)
      return dsp_pc;
   return 0;
}

void vjtrace_emit(uint8_t type, uint8_t who, uint32_t addr, uint32_t value)
{
   vjtrace_ev *e;
   if (type == VJT_EV_BLIT_CMD && vjtrace_bus_armed)
      vjt_bus_blit_cmd(addr);
   /* Gated centrally here rather than at each of the ~15 VJT_EMIT call
    * sites (tom.c, op.c, blitter.c, gpu.c, m68kinterface.c): one check
    * covers all of them, including IRQ/OP/blitter events that fire every
    * frame regardless of whether anyone is recording. */
   if (!vjtrace_armed || !ring)
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

/* Live ring readback -- see the contract on the declarations in
 * vjtrace.h.  ring != NULL implies ring_cap > 0 (vjtrace_init() only
 * publishes a capacity when the allocation succeeded), which is the
 * same invariant vjtrace_emit() relies on for its modulo. */
uint64_t vjtrace_ring_head(void)
{
   return ring_head;
}

int vjtrace_ring_read(uint64_t idx, vjtrace_ev *out)
{
   if (!ring || !out)
      return 0;
   if (idx >= ring_head)          /* not written yet */
      return 0;
   if (ring_head - idx > ring_cap) /* already overwritten */
      return 0;
   *out = ring[idx % ring_cap];
   return 1;
}

int vjtrace_watch_add(uint32_t lo, uint32_t hi, unsigned rw)
{
   if (vjtrace_nwatch >= 16)
      return -1;
   /* Defense at the core boundary: trace_probe.c already rejects an
    * inverted/overflowing range at parse time (tp_parse_watch(), fixed
    * in 7df363a), but this function has other callers too, and an
    * inverted range or an out-of-{1,2,3} rw value would otherwise
    * silently install a watch that can never fire. */
   if (hi < lo)
   {
      uint32_t tmp = lo;
      lo = hi;
      hi = tmp;
   }
   rw &= 3u;
   if (rw == 0)
      rw = 2u;   /* default to writes, matching tp_parse_watch()'s default */
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
   /* Called from GPUExec() on EVERY GPU instruction, armed or not -- the
    * armed check must be the very first thing this function does (see
    * the PERFORMANCE NOTE in vjtrace.h: this was the ~6-8% wall-clock
    * regression before the armed gate existed). */
   if (!vjtrace_armed)
      return;
   gpu_pchist[gpu_pchist_head] = pc;
   gpu_pchist_head = (gpu_pchist_head + 1) & (VJT_PCHIST_CAP - 1);
   if (gpu_pchist_fill < VJT_PCHIST_CAP)
      gpu_pchist_fill++;
}

void vjtrace_pchist_dsp(uint32_t pc)
{
   /* Called from DSPExec() on EVERY DSP instruction -- see the note on
    * vjtrace_pchist_gpu() above. */
   if (!vjtrace_armed)
      return;
   if (vjtrace_bus_armed)
      vjt_bus_dsp_insn(pc);
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

/* fwrite() wrapper that collapses the short-item-count failure mode
 * (disk full, I/O error) to a single boolean so every call site in
 * vjtrace_dump() and vjtrace_snapshot() below can be checked -- a VJTR
 * or VJSN file that exists on disk must be complete; a truncated write
 * must never look like success. */
static int vjt_write(FILE *f, const void *buf, size_t size, size_t n)
{
   return (fwrite(buf, size, n, f) == n) ? 1 : 0;
}

int vjtrace_dump(const char *path)
{
   FILE *f;
   uint64_t n, start, i;
   struct { char magic[4]; uint32_t version, ev_size, pad; uint64_t count; } hdr;
   int ok;

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

   ok = vjt_write(f, &hdr, sizeof(hdr), 1);
   for (i = 0; ok && i < n; i++)
      ok = vjt_write(f, &ring[(start + i) % ring_cap], sizeof(vjtrace_ev), 1);

   if (fclose(f) != 0)
      ok = 0;

   if (!ok)
   {
      /* Invariant: a VJTR file that exists on disk is complete -- mirrors
       * vjtrace_snapshot()'s VJSN invariant below. A mid-write failure
       * (disk full, I/O error) must leave neither a truncated file on
       * disk nor a caller believing the dump succeeded. */
      remove(path);
      return -1;
   }
   return 0;
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

/* Counterpart to vjtrace_init(): frees the ring and resets every module
 * static to its pre-init state, so a later vjtrace_init() call (the
 * reload path -- iOS cannot dlclose cores, so retro_init()/retro_deinit()
 * can both run again in the same process for a later title) allocates a
 * fresh ring instead of hitting the "if (ring) return" early-out with a
 * freed pointer.  Idempotent and safe to call when vjtrace_init() was
 * never called (ring already NULL -- free(NULL) is a no-op) or already
 * shut down.
 *
 * Ordering: callers must not still be reading the ring afterward.  The
 * flight-recorder's own consumers (trace_probe_finish() and friends,
 * test/harness/trace_probe.c) run BEFORE the core's shutdown call in
 * every caller in this tree (see harness_shutdown(), which calls
 * retro_unload_game() then retro_deinit() last), so a call from
 * retro_deinit() is always after any ring dump has already happened. */
void vjtrace_shutdown(void)
{
   if (ring)
      free(ring);
   ring = NULL;
   ring_cap = 0;
   ring_head = 0;
   seq_ctr = 0;
   cur_frame = 0;
   vjtrace_armed = 0;   /* back to the documented default-OFF state */
   vjtrace_nwatch = 0;
   memset(watches, 0, sizeof(watches));
   memset(&vjtrace_counters, 0, sizeof(vjtrace_counters));
   memset(gpu_pchist, 0, sizeof(gpu_pchist));
   gpu_pchist_head = 0;
   gpu_pchist_fill = 0;
   memset(dsp_pchist, 0, sizeof(dsp_pchist));
   dsp_pchist_head = 0;
   dsp_pchist_fill = 0;
   /* vjtrace_snapshot()'s snapshot_ordinal is a function-local static
    * (monotonic filename counter, not part of the leak or the ring
    * state) -- deliberately left alone here; it just continues counting
    * across a reload in the same process, which is cosmetic only. */
}

/* ------------------------------------------------------------------ *
 * Bus-traffic census (timing campaign S2) -- contract in vjtrace.h.
 * ------------------------------------------------------------------ */

#define VJT_BUS_RING 8192u                 /* halflines (~15 fields) */
#define VJT_BUS_M68K_PCH 0x100000u          /* 68K PCs < $200000, by word */
#define VJT_BUS_GPU_PCH  0x800u             /* GPU local RAM, by word */
#define VJT_BUS_DSP_PCH  0x1000u            /* DSP local RAM, by word */

int vjtrace_bus_armed = 0;
/* Cumulative per-PC histograms, allocated on first arm and exported so a
 * probe can name the loops the windows should cover:
 *   vjtrace_bus_m68k_pch[pc>>1]   68K sysclks by instruction-start PC
 *   vjtrace_bus_gpu_pch[(pc-$F03000)>>1]  GPU external transactions by PC
 *   vjtrace_bus_dsp_pch[(pc-$F1B000)>>1]  DSP external transactions by PC */
uint32_t *vjtrace_bus_m68k_pch = NULL;
uint32_t *vjtrace_bus_gpu_pch  = NULL;
uint32_t *vjtrace_bus_dsp_pch  = NULL;
/* DSP instructions by PC (local RAM, by word); needs vjtrace_arm(). */
uint32_t *vjtrace_bus_dsp_ipch = NULL;
static uint32_t bus_dsp_nwin = 0;
static uint32_t bus_dsp_lo[VJT_BUS_MAX_WIN], bus_dsp_hi[VJT_BUS_MAX_WIN];

static vjtrace_bus_hl bus_cur;
static vjtrace_bus_hl bus_ring[VJT_BUS_RING];
static uint32_t bus_head = 0;
static uint32_t bus_m68k_nwin = 0;
static uint32_t bus_m68k_lo[VJT_BUS_MAX_WIN], bus_m68k_hi[VJT_BUS_MAX_WIN];
static uint32_t bus_row_gpu = 0xFFFFFFFFu, bus_row_dsp = 0xFFFFFFFFu;
static uint32_t bus_m68k_pc = 0;
static int32_t  bus_m68k_rem = 0;
static int      bus_m68k_valid = 0;
static int      bus_m68k_spin = 0;
static uint64_t bus_gpu_work0 = 0, bus_gpu_total0 = 0;
static unsigned long long *bus_perf_prd = NULL, *bus_perf_pwr = NULL;
static unsigned long long bus_prd0 = 0, bus_pwr0 = 0;

/* 68K instruction-start PC ring (src/core/jaguar.c, see vjt_pc_of()). */
extern uint32_t pcQueue[];
extern uint32_t pcQPtr;

/* gpu_busy census (src/tom/gpu.c, VJ_TRACE): windows + cumulative cycles. */
extern uint32_t gpu_busy_nwin;
extern uint32_t gpu_busy_win_lo[];
extern uint32_t gpu_busy_win_hi[];
extern uint64_t gpu_busy_work_cycles;
extern uint64_t gpu_busy_total_cycles;

/* ---- S2 cross-check: inject the FIXED latency function (dev only) ----
 *
 * VJ_S2_INJECT=1|2|3 (low / central / high constants of the function in
 * docs/timing-campaign-plan.md S2, the same numbers as
 * test/tools/contention_model.py) auto-arms the census and charges every
 * GPU external access the expected arbitration wait computed from the
 * PREVIOUS video line's measured occupancy:
 *    OP + refresh burst B          B^2 / (2T)
 *    blitter (outstanding debt)    U_blit * c / 2
 *    DSP in flight                 U_dsp * (2 x mean DSP price) / 2
 *    68K default-owner handoff     P_idle * H
 * via GPUChargeBusStall (wall-time sysclks).  It exists only to let the
 * emulator itself map the predicted extra GPU time onto the present rate
 * (the critical-path question) -- run once per level, never iterated.
 * Compiled only under VJ_TRACE; absent from every shipped build. */
extern void GPUChargeBusStall(uint32_t sysclks);
static int      bus_inject = 0;
static uint32_t inj_B = 0;          /* last line's OP+refresh burst, sysclks */
static uint32_t inj_dsp = 0;        /* last line's DSP occupancy, sysclks */
static uint32_t inj_blit_debt = 0;  /* blitter bus time not yet elapsed */
static uint32_t inj_line_op = 0, inj_line_dsp = 0, inj_hl = 0;
static uint32_t inj_line_objs = 0, inj_w_op256 = 0;   /* OP wait, x256 */
static uint32_t inj_frac = 0;       /* 1/256-sysclk remainder */
static uint32_t inj_blit_c = 0, inj_H = 0, inj_ref = 0;   /* x256 */
uint64_t vjtrace_bus_inject_sysclks = 0;

static void vjt_bus_inject_init(void)
{
   const char *env = getenv("VJ_S2_INJECT");
   int lvl = env ? atoi(env) : 0;
   if (lvl < 1 || lvl > 3)
      return;
   bus_inject = lvl;
   /* (low, central, high) -- must match contention_model.py P[] */
   inj_H      = (lvl == 1 ? 2 : lvl == 2 ? 6 : 12) * 256u;
   inj_blit_c = (lvl == 1 ? 512u : lvl == 2 ? 896u : 1280u);   /* 2 / 3.5 / 5 */
   inj_ref    = (lvl == 3 ? 9u : 3u) * 256u;                   /* per refresh */
   vjtrace_bus_arm(1);
}

static void vjt_bus_inject_halfline(void)
{
   const uint32_t T = 1690;
   uint32_t room;
   inj_line_op  += bus_cur.op_clk;
   inj_line_objs += bus_cur.op_objs;
   inj_line_dsp += bus_cur.dsp_mem;
   inj_blit_debt += (bus_cur.blit_prd + bus_cur.blit_pwr) * inj_blit_c / 256u;
   if (++inj_hl < 2)
      return;
   /* one video line = two halflines; refresh grouped after the OP */
   {
      uint32_t ref = (uint32_t)((T / 384.0) * inj_ref / 256u);
      if (inj_line_op > T)
         inj_line_op = T;
      inj_B = inj_line_op + ref;
      if (inj_B > T)
         inj_B = T;
      /* low level: per-object bursts (gaps between objects let the GPU
       * in); central/high: one contiguous burst per line (RELEASE clear) */
      if (bus_inject == 1 && inj_line_objs > 1)
         inj_w_op256 = (inj_line_op * inj_line_op / inj_line_objs * 256u
                        + ref * ref * 256u) / (2u * T);
      else
         inj_w_op256 = (inj_B * inj_B * 256u) / (2u * T);
   }
   inj_dsp = inj_line_dsp;
   room = (inj_B < T) ? T - inj_B : 0;
   inj_blit_debt = (inj_blit_debt > room) ? inj_blit_debt - room : 0;
   inj_line_op = inj_line_dsp = inj_hl = inj_line_objs = 0;
}

static void vjt_bus_inject_charge(void)
{
   const uint32_t T = 1690;
   uint32_t busy, w256, ub256, ud256, pidle256, B;
   B = inj_B > T ? T : inj_B;
   busy = B;
   ub256 = 0;
   if (inj_blit_debt)
   {
      ub256 = (uint32_t)(((T - B) * 256u) / T);
      busy = T;
   }
   ud256 = (uint32_t)((inj_dsp * 256u) / T);
   if (ud256 > 256u)
      ud256 = 256u;
   busy += inj_dsp;
   pidle256 = (busy >= T) ? 0 : (uint32_t)(((T - busy) * 256u) / T);
   w256 = inj_w_op256                                   /* OP + refresh */
        + ub256 * inj_blit_c / 512u                      /* blit resid. */
        + ud256 * 27u / 10u                              /* DSP 2x2.7/2 */
        + (pidle256 * inj_H) / 256u;                     /* handoff */
   inj_frac += w256;
   if (inj_frac >= 256u)
   {
      GPUChargeBusStall(inj_frac >> 8);
      vjtrace_bus_inject_sysclks += inj_frac >> 8;
      inj_frac &= 0xFFu;
   }
}

static uint32_t vjt_bus_price(uint32_t addr, uint32_t *row)
{
   uint32_t r;
   addr &= 0xFFFFFF;
   if (addr < 0x200000)
   {
      r = addr >> 12;
      if (r == *row)
         return 2;
      *row = r;
      return 2u + busArbiter.dram_row_miss;
   }
   if (addr >= 0x800000 && addr < 0xE40000)
      return busArbiter.rom_clocks;
   if (addr >= 0xDFFF00)
      return 2;
   return 2u + busArbiter.dram_row_miss;
}

static int vjt_bus_m68k_in_spin(uint32_t pc)
{
   uint32_t i;
   for (i = 0; i < bus_m68k_nwin; i++)
      if (pc >= bus_m68k_lo[i] && pc < bus_m68k_hi[i])
         return 1;
   return 0;
}

void vjtrace_bus_arm(int on)
{
   if (on && !vjtrace_bus_m68k_pch)
   {
      vjtrace_bus_m68k_pch = (uint32_t *)calloc(VJT_BUS_M68K_PCH, sizeof(uint32_t));
      vjtrace_bus_gpu_pch  = (uint32_t *)calloc(VJT_BUS_GPU_PCH, sizeof(uint32_t));
      vjtrace_bus_dsp_pch  = (uint32_t *)calloc(VJT_BUS_DSP_PCH, sizeof(uint32_t));
      vjtrace_bus_dsp_ipch = (uint32_t *)calloc(VJT_BUS_DSP_PCH, sizeof(uint32_t));
      if (!vjtrace_bus_m68k_pch || !vjtrace_bus_gpu_pch || !vjtrace_bus_dsp_pch
          || !vjtrace_bus_dsp_ipch)
         return;
   }
   if (on)
   {
      memset(&bus_cur, 0, sizeof(bus_cur));
      bus_m68k_valid = 0;
      bus_gpu_work0  = gpu_busy_work_cycles;
      bus_gpu_total0 = gpu_busy_total_cycles;
      bus_perf_prd = perf_counters_find("blitter_phrase_reads");
      bus_perf_pwr = perf_counters_find("blitter_phrase_writes");
      bus_prd0 = bus_perf_prd ? *bus_perf_prd : 0;
      bus_pwr0 = bus_perf_pwr ? *bus_perf_pwr : 0;
   }
   vjtrace_bus_armed = on ? 1 : 0;
}

void vjtrace_bus_set_m68k_window(uint32_t i, uint32_t lo, uint32_t hi)
{
   if (i >= VJT_BUS_MAX_WIN)
      return;
   bus_m68k_lo[i] = lo;
   bus_m68k_hi[i] = hi;
   if (i + 1 > bus_m68k_nwin)
      bus_m68k_nwin = i + 1;
}

void vjtrace_bus_set_dsp_window(uint32_t i, uint32_t lo, uint32_t hi)
{
   if (i >= VJT_BUS_MAX_WIN)
      return;
   bus_dsp_lo[i] = lo;
   bus_dsp_hi[i] = hi;
   if (i + 1 > bus_dsp_nwin)
      bus_dsp_nwin = i + 1;
}

static int vjt_bus_dsp_in_spin(uint32_t pc)
{
   uint32_t i;
   for (i = 0; i < bus_dsp_nwin; i++)
      if (pc >= bus_dsp_lo[i] && pc < bus_dsp_hi[i])
         return 1;
   return 0;
}

static void vjt_bus_dsp_insn(uint32_t pc)
{
   if (vjt_bus_dsp_in_spin(pc))
      bus_cur.dsp_ins_spin++;
   else
      bus_cur.dsp_ins_work++;
   if (vjtrace_bus_dsp_ipch && pc >= 0xF1B000 && pc < 0xF1D000)
      vjtrace_bus_dsp_ipch[(pc - 0xF1B000) >> 1]++;
}

uint32_t vjtrace_bus_ring_head(void)
{
   return bus_head;
}

int vjtrace_bus_ring_read(uint32_t idx, vjtrace_bus_hl *out)
{
   if (!out || idx >= bus_head || bus_head - idx > VJT_BUS_RING)
      return 0;
   *out = bus_ring[idx % VJT_BUS_RING];
   return 1;
}

void vjtrace_bus_m68k(uint32_t addr, uint32_t naccesses)
{
   uint32_t pc;
   addr &= 0xFFFFFF;
   pc = pcQueue[(pcQPtr - 1) & 0x3FF];
   if (vjt_bus_m68k_in_spin(pc))
      bus_cur.m68k_bus_spin += naccesses;
   else
      bus_cur.m68k_bus_work += naccesses;
   if (addr >= 0x800000 && addr < 0xE40000)
      bus_cur.m68k_rom_bus += naccesses;
}

/* Called before each 68K instruction: the clocks since the previous call
 * belong to the previous instruction.  A slice boundary (remaining went
 * UP) is closed by vjtrace_bus_m68k_slice_end() instead. */
void vjtrace_bus_m68k_insn(uint32_t pc, int32_t remaining)
{
   int32_t d;
   if (bus_m68k_valid && remaining <= bus_m68k_rem)
   {
      d = bus_m68k_rem - remaining;
      if (bus_m68k_spin)
         bus_cur.m68k_clk_spin += 2u * (uint32_t)d;
      else
         bus_cur.m68k_clk_work += 2u * (uint32_t)d;
      if (vjtrace_bus_m68k_pch && bus_m68k_pc < 0x200000)
         vjtrace_bus_m68k_pch[bus_m68k_pc >> 1] += 2u * (uint32_t)d;
   }
   bus_m68k_pc = pc;
   bus_m68k_spin = vjt_bus_m68k_in_spin(pc);
   bus_m68k_rem = remaining;
   bus_m68k_valid = 1;
}

void vjtrace_bus_m68k_slice_end(int32_t remaining)
{
   if (bus_m68k_valid)
      vjtrace_bus_m68k_insn(bus_m68k_pc, remaining);
   bus_m68k_valid = 0;
}

void vjtrace_bus_m68k_stall(uint32_t cycles68k)
{
   bus_cur.m68k_stall_clk += 2u * cycles68k;
}

void vjtrace_bus_gpu(uint32_t addr)
{
   uint32_t pc, i, price;
   int spin = 0;
   addr &= 0xFFFFFF;
   if (addr >= 0xF02000 && addr <= 0xF03FFF)
      return;                       /* GPU local bus: no external cycle */
   pc = gpu_pc - 2;                 /* load/store opcodes are one word */
   for (i = 0; i < gpu_busy_nwin; i++)
      if (pc >= gpu_busy_win_lo[i] && pc < gpu_busy_win_hi[i])
      {
         spin = 1;
         break;
      }
   price = vjt_bus_price(addr, &bus_row_gpu);
   if (spin)
   {
      bus_cur.gpu_ext_spin++;
      bus_cur.gpu_mem_spin += price;
   }
   else
   {
      bus_cur.gpu_ext_work++;
      bus_cur.gpu_mem_work += price;
   }
   if (GPUGetFlags() & 0x8000)
      bus_cur.gpu_dmaen++;
   if (bus_inject)
      vjt_bus_inject_charge();
   if (vjtrace_bus_gpu_pch && pc >= 0xF03000 && pc < 0xF04000)
      vjtrace_bus_gpu_pch[(pc - 0xF03000) >> 1]++;
}

void vjtrace_bus_dsp(uint32_t addr, uint32_t n16)
{
   uint32_t pc;
   addr &= 0xFFFFFF;
   if (addr >= 0xF10000 && addr <= 0xF1FFFF)
      return;                       /* JERRY-internal: not the main bus */
   bus_cur.dsp_ext += n16;
   bus_cur.dsp_mem += n16 * vjt_bus_price(addr, &bus_row_dsp);
   pc = dsp_pc;
   if (vjt_bus_dsp_in_spin(pc))
      bus_cur.dsp_ext_spin += n16;
   if (vjtrace_bus_dsp_pch && pc >= 0xF1B000 && pc < 0xF1D000)
      vjtrace_bus_dsp_pch[(pc - 0xF1B000) >> 1] += n16;
}

void vjtrace_bus_op(uint32_t sysclks)
{
   bus_cur.op_clk += sysclks;
}

void vjtrace_bus_op_object(int release)
{
   bus_cur.op_objs++;
   if (release)
      bus_cur.op_release++;
}

static void vjt_bus_blit_cmd(uint32_t cmd)
{
   bus_cur.blit_cmds++;
   if (cmd & 0x20000000u)           /* B_CMD bit 29 BUSHI (JTRM v8 p.75) */
      bus_cur.blit_bushi++;
}

void vjtrace_bus_halfline(uint32_t vc)
{
   uint64_t w, t;
   w = gpu_busy_work_cycles;
   t = gpu_busy_total_cycles;
   bus_cur.gpu_cyc_work  = (uint32_t)(w - bus_gpu_work0);
   bus_cur.gpu_cyc_total = (uint32_t)(t - bus_gpu_total0);
   bus_gpu_work0 = w;
   bus_gpu_total0 = t;
   if (bus_perf_prd)
   {
      bus_cur.blit_prd = (uint32_t)(*bus_perf_prd - bus_prd0);
      bus_prd0 = *bus_perf_prd;
   }
   if (bus_perf_pwr)
   {
      bus_cur.blit_pwr = (uint32_t)(*bus_perf_pwr - bus_pwr0);
      bus_pwr0 = *bus_perf_pwr;
   }
   bus_cur.vc = vc;
   if (bus_inject)
      vjt_bus_inject_halfline();
   bus_ring[bus_head % VJT_BUS_RING] = bus_cur;
   bus_head++;
   memset(&bus_cur, 0, sizeof(bus_cur));
}

#else /* !VJ_TRACE */

typedef int vjtrace_not_built;

#endif /* VJ_TRACE */
