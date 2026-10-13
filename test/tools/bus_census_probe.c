/* test/tools/bus_census_probe.c -- timing-campaign S2 bus-traffic census
 * (docs/timing-campaign-plan.md S2; issues #408 #401).
 *
 * Measures every bus master's external traffic per HALFLINE through the
 * core's TEST_EXPORTS bus census (src/core/vjtrace.c, vjtrace_bus_*), plus
 * the gpu_busy work/spin split, and writes:
 *
 *   --out FILE     one CSV row per halfline in the window (fields lo..hi):
 *                  field, hl, vc, then every vjtrace_bus_hl column.
 *   --frames-csv FILE  one row per field: field, flip (presented image
 *                  changed, same FNV subsample as the S0 gate).
 *   stdout         per-field means, the MEMCON-derived costs in force, and
 *                  the hottest 68K PCs (sysclks), GPU-external and
 *                  DSP-external PCs over the window, with a hex dump of
 *                  the code around each hot 68K PC so the spin windows can
 *                  be checked by disassembly instead of assumed.
 *
 * Nothing here changes emulated time: the census only counts.  Needs a
 * TEST_EXPORTS=1 core; blitter phrase counts additionally need
 * BENCH_PROFILE=1 and the ACCURATE blitter (the fast blitter has no
 * counters -- the #422 false-negative trap), so the probe pins
 * virtualjaguar_usefastblitter=disabled unless overridden and reports
 * blit_prd/blit_pwr = 0 as "not measured", never as "no traffic".
 *
 * Usage:
 *   bus_census_probe <core> <rom> [--win-lo F] [--win-hi F] [--out FILE]
 *       [--frames-csv FILE] [--gpu-win LO:HI]... [--m68k-win LO:HI]...
 *       [--no-default-gpu-win] [--top N] [harness options]
 * Default GPU windows: Doom's three spin loops (S0 gate).  Repeatable
 * --m68k-win marks 68K spin loops (hex, [lo,hi)).
 */
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stddef.h>
#include "../harness/harness.h"
#include "../../src/debug/gdbdisasm.h"

#define MAX_FIELDS 20000
#define MAXWIN     8

/* Mirror of vjtrace_bus_hl (src/core/vjtrace.h) -- keep in sync. */
typedef struct
{
   uint32_t m68k_bus_work, m68k_bus_spin;
   uint32_t m68k_clk_work, m68k_clk_spin;
   uint32_t m68k_rom_bus;
   uint32_t m68k_stall_clk;
   uint32_t gpu_ext_work, gpu_ext_spin;
   uint32_t gpu_mem_work, gpu_mem_spin;
   uint32_t gpu_dmaen;
   uint32_t gpu_cyc_work, gpu_cyc_total;
   uint32_t dsp_ext, dsp_mem;
   uint32_t op_clk;
   uint32_t op_objs;
   uint32_t op_release;
   uint32_t blit_cmds, blit_bushi;
   uint32_t blit_prd, blit_pwr;
   uint32_t vc;
   uint32_t dsp_ext_spin;
   uint32_t dsp_ins_work, dsp_ins_spin;
} bus_hl;

static const char *col_names =
   "m68k_bus_work,m68k_bus_spin,m68k_clk_work,m68k_clk_spin,m68k_rom_bus,"
   "m68k_stall_clk,gpu_ext_work,gpu_ext_spin,gpu_mem_work,gpu_mem_spin,"
   "gpu_dmaen,gpu_cyc_work,gpu_cyc_total,dsp_ext,dsp_mem,op_clk,op_objs,"
   "op_release,blit_cmds,blit_bushi,blit_prd,blit_pwr,vc_dup,dsp_ext_spin,"
   "dsp_ins_work,dsp_ins_spin";
#define NCOL 26

/* BusArbiter layout prefix (src/core/bus_arbiter.h). */
typedef struct
{
   uint8_t dram_row_miss, rom_clocks, dram_refresh_clks, refrate, enabled,
           contention_scale;
} bus_arb_prefix;

static uint32_t (*p_head)(void);
static int (*p_read)(uint32_t, bus_hl *);
static uint32_t drained;
static unsigned lo = 600, hi = 1500;
static FILE *out;
static double sum[NCOL];
static unsigned long nhl;
static uint64_t last_hash;
static int cur_flip;
static uint8_t flip_at[MAX_FIELDS + 2];
static unsigned nfields;
static uint32_t **p_m68k_pch, **p_gpu_pch, **p_dsp_pch, **p_dsp_ipch;
static uint32_t *snap_m68k, *snap_gpu, *snap_dsp, *snap_dspi;
static uint64_t ring_lost;

static void on_video(void *ud, const void *data, unsigned w, unsigned h,
                     size_t pitch)
{
   const uint8_t *p = (const uint8_t *)data;
   uint64_t hv = 1469598103934665603ull;
   unsigned y, x;
   (void)ud;
   if (!p)
      return;
   for (y = 0; y < h; y += 8)
   {
      const uint8_t *row = p + y * pitch;
      for (x = 0; x < w * 4; x += 16)
      {
         hv ^= row[x];
         hv *= 1099511628211ull;
      }
   }
   if (hv != last_hash)
   {
      last_hash = hv;
      cur_flip = 1;
   }
}

static void snapshot(void)
{
   memcpy(snap_m68k, *p_m68k_pch, 0x100000 * sizeof(uint32_t));
   memcpy(snap_gpu, *p_gpu_pch, 0x800 * sizeof(uint32_t));
   memcpy(snap_dsp, *p_dsp_pch, 0x1000 * sizeof(uint32_t));
   memcpy(snap_dspi, *p_dsp_ipch, 0x1000 * sizeof(uint32_t));
}

static bool on_frame(void *ud, unsigned frame)
{
   uint32_t h;
   unsigned hl = 0;
   bus_hl r;
   (void)ud;
   if (frame > MAX_FIELDS)
      return false;
   flip_at[frame] = (uint8_t)cur_flip;
   cur_flip = 0;
   nfields = frame;
   h = p_head();
   if (h - drained > 8192u)
   {
      ring_lost += (h - drained) - 8192u;
      drained = h - 8192u;
   }
   while (drained < h)
   {
      if (!p_read(drained, &r))
      {
         drained++;
         continue;
      }
      drained++;
      if (frame > lo && frame <= hi)
      {
         const uint32_t *v = (const uint32_t *)&r;
         int c;
         if (out)
         {
            fprintf(out, "%u,%u,%u", frame, hl, r.vc);
            for (c = 0; c < NCOL; c++)
               fprintf(out, ",%u", v[c]);
            fputc('\n', out);
         }
         for (c = 0; c < NCOL; c++)
            sum[c] += v[c];
         nhl++;
      }
      hl++;
   }
   if (frame == lo)
      snapshot();
   return frame < hi;
}

typedef struct { uint32_t idx, val; } pcval;
static int cmp_pcval(const void *a, const void *b)
{
   const pcval *x = (const pcval *)a, *y = (const pcval *)b;
   return (x->val < y->val) - (x->val > y->val);
}

static void top_list(const char *title, const uint32_t *now, const uint32_t *base,
                     uint32_t n, uint32_t pcbase, unsigned top, double denom,
                     const uint8_t *code, uint32_t codebase, uint32_t codelen)
{
   pcval *v = (pcval *)malloc(n * sizeof(pcval));
   uint32_t i, m = 0;
   unsigned k;
   double tot = 0;
   for (i = 0; i < n; i++)
   {
      uint32_t d = now[i] - base[i];
      if (d)
      {
         v[m].idx = i;
         v[m].val = d;
         m++;
         tot += d;
      }
   }
   qsort(v, m, sizeof(pcval), cmp_pcval);
   printf("%s (total %.0f = %.1f per field):\n", title, tot, denom > 0 ? tot / denom : 0);
   for (k = 0; k < top && k < m; k++)
   {
      uint32_t pc = pcbase + v[k].idx * 2;
      printf("  $%06X  %10u  %5.1f%%", pc, v[k].val, tot > 0 ? 100.0 * v[k].val / tot : 0);
      if (code && pc >= codebase && pc + 16 <= codebase + codelen)
      {
         uint32_t o = pc - codebase, j;
         printf("  ");
         for (j = 0; j < 16; j += 2)
            printf("%02X%02X ", code[o + j], code[o + j + 1]);
      }
      printf("\n");
   }
   free(v);
}

int main(int argc, char **argv)
{
   harness_config cfg = HARNESS_CONFIG_DEFAULT;
   const char *out_path = NULL, *frames_path = NULL;
   unsigned a, i, top = 30, f;
   uint32_t glo[MAXWIN], ghi[MAXWIN], mlo[MAXWIN], mhi[MAXWIN], dlo[MAXWIN], dhi[MAXWIN];
   unsigned ng = 0, nm = 0, nd = 0, ndis = 0;
   uint32_t dis_at[16], dis_n[16];
   int dis_dsp[16];
   void (*trace_arm)(void);
   void (*dsp_setwin)(uint32_t, uint32_t, uint32_t);
   int default_gpu = 1;
   void (*busy_arm)(int);
   void (*busy_reset)(void);
   void (*busy_setwin)(uint32_t, uint32_t, uint32_t);
   void (*bus_arm)(int);
   void (*bus_setwin)(uint32_t, uint32_t, uint32_t);
   bus_arb_prefix *arb;
   uint8_t **ram;
   double nf;
   char *build;

   cfg.frames = 1500;
   cfg.quiet = 1;
   for (a = 1; (int)a < argc; a++)
   {
      if (!strcmp(argv[a], "--win-lo") && (int)a + 1 < argc) lo = (unsigned)strtoul(argv[++a], NULL, 10);
      else if (!strcmp(argv[a], "--win-hi") && (int)a + 1 < argc) hi = (unsigned)strtoul(argv[++a], NULL, 10);
      else if (!strcmp(argv[a], "--out") && (int)a + 1 < argc) out_path = argv[++a];
      else if (!strcmp(argv[a], "--frames-csv") && (int)a + 1 < argc) frames_path = argv[++a];
      else if (!strcmp(argv[a], "--top") && (int)a + 1 < argc) top = (unsigned)strtoul(argv[++a], NULL, 10);
      else if (!strcmp(argv[a], "--no-default-gpu-win")) default_gpu = 0;
      else if ((!strcmp(argv[a], "--disasm-gpu") || !strcmp(argv[a], "--disasm-dsp"))
               && (int)a + 1 < argc && ndis < 16)
      {
         char *c;
         dis_dsp[ndis] = !strcmp(argv[a], "--disasm-dsp");
         dis_at[ndis] = (uint32_t)strtoul(argv[++a], &c, 16);
         dis_n[ndis] = (*c == ':') ? (uint32_t)strtoul(c + 1, NULL, 10) : 16;
         ndis++;
      }
      else if ((!strcmp(argv[a], "--gpu-win") || !strcmp(argv[a], "--m68k-win")
                || !strcmp(argv[a], "--dsp-win")) && (int)a + 1 < argc)
      {
         int kind = !strcmp(argv[a], "--gpu-win") ? 0 : !strcmp(argv[a], "--m68k-win") ? 1 : 2;
         char *c;
         uint32_t l = (uint32_t)strtoul(argv[++a], &c, 16), h2;
         h2 = (*c == ':') ? (uint32_t)strtoul(c + 1, NULL, 16) : l + 2;
         if (kind == 0 && ng < MAXWIN) { glo[ng] = l; ghi[ng] = h2; ng++; }
         else if (kind == 1 && nm < MAXWIN) { mlo[nm] = l; mhi[nm] = h2; nm++; }
         else if (kind == 2 && nd < MAXWIN) { dlo[nd] = l; dhi[nd] = h2; nd++; }
      }
   }
   if (!harness_init_from_args(&cfg, argc, argv))
      return 2;
   if (!cfg.rom_path)
   {
      fprintf(stderr, "usage: bus_census_probe <core> <rom> [options]\n");
      return 2;
   }
   {
      FILE *rf = fopen(cfg.rom_path, "rb");
      if (!rf)
      {
         printf("SKIP: ROM not present (%s)\n", cfg.rom_path);
         return 77;
      }
      fclose(rf);
   }
   if (hi > MAX_FIELDS || lo >= hi || lo < 2)
   {
      fprintf(stderr, "bad window %u..%u\n", lo, hi);
      return 2;
   }
   if (cfg.frames < hi)
      cfg.frames = hi;
   {
      int have = 0;
      for (i = 0; i < cfg.num_options; i++)
         if (!strcmp(cfg.options[i].key, "virtualjaguar_usefastblitter"))
            have = 1;
      if (!have)
         harness_set_option(&cfg, "virtualjaguar_usefastblitter", "disabled");
   }
   if (default_gpu && ng == 0)
   {
      /* Doom (Evil Unleashed 1994) GPU spin loops -- same as the S0 gate. */
      glo[0] = 0xF0308Au; ghi[0] = 0xF030A0u;
      glo[1] = 0xF03672u; ghi[1] = 0xF0369Cu;
      glo[2] = 0xF0362Cu; ghi[2] = 0xF0365Au;
      ng = 3;
   }

   if (!harness_load_rom(&cfg))
      return 2;
   busy_arm    = (void (*)(int))harness_dlsym(&cfg, "gpu_busy_arm");
   busy_reset  = (void (*)(void))harness_dlsym(&cfg, "gpu_busy_reset");
   busy_setwin = (void (*)(uint32_t, uint32_t, uint32_t))harness_dlsym(&cfg, "gpu_busy_set_window");
   bus_arm     = (void (*)(int))harness_dlsym(&cfg, "vjtrace_bus_arm");
   bus_setwin  = (void (*)(uint32_t, uint32_t, uint32_t))harness_dlsym(&cfg, "vjtrace_bus_set_m68k_window");
   p_head      = (uint32_t (*)(void))harness_dlsym(&cfg, "vjtrace_bus_ring_head");
   p_read      = (int (*)(uint32_t, bus_hl *))harness_dlsym(&cfg, "vjtrace_bus_ring_read");
   p_m68k_pch  = (uint32_t **)harness_dlsym(&cfg, "vjtrace_bus_m68k_pch");
   p_gpu_pch   = (uint32_t **)harness_dlsym(&cfg, "vjtrace_bus_gpu_pch");
   p_dsp_pch   = (uint32_t **)harness_dlsym(&cfg, "vjtrace_bus_dsp_pch");
   p_dsp_ipch  = (uint32_t **)harness_dlsym(&cfg, "vjtrace_bus_dsp_ipch");
   trace_arm   = (void (*)(void))harness_dlsym(&cfg, "vjtrace_arm");
   dsp_setwin  = (void (*)(uint32_t, uint32_t, uint32_t))harness_dlsym(&cfg, "vjtrace_bus_set_dsp_window");
   arb         = (bus_arb_prefix *)harness_dlsym(&cfg, "busArbiter");
   ram         = (uint8_t **)harness_dlsym(&cfg, "jaguarMainRAM");
   if (!busy_arm || !busy_reset || !busy_setwin || !bus_arm || !bus_setwin
       || !p_head || !p_read || !p_m68k_pch || !p_gpu_pch || !p_dsp_pch || !arb
       || !p_dsp_ipch || !trace_arm || !dsp_setwin)
   {
      fprintf(stderr, "bus_census_probe: census symbols not exported -- "
                      "build the core with TEST_EXPORTS=1\n");
      harness_shutdown(&cfg);
      return 2;
   }
   for (i = 0; i < ng; i++)
      busy_setwin(i, glo[i], ghi[i]);
   for (i = 0; i < nm; i++)
      bus_setwin(i, mlo[i], mhi[i]);
   for (i = 0; i < nd; i++)
      dsp_setwin(i, dlo[i], dhi[i]);
   busy_reset();
   busy_arm(1);
   bus_arm(1);
   trace_arm();      /* DSP per-instruction hook rides vjtrace_armed */
   if (!*p_m68k_pch)
   {
      fprintf(stderr, "bus_census_probe: census allocation failed\n");
      harness_shutdown(&cfg);
      return 2;
   }
   snap_m68k = (uint32_t *)calloc(0x100000, sizeof(uint32_t));
   snap_gpu  = (uint32_t *)calloc(0x800, sizeof(uint32_t));
   snap_dsp  = (uint32_t *)calloc(0x1000, sizeof(uint32_t));
   snap_dspi = (uint32_t *)calloc(0x1000, sizeof(uint32_t));
   drained = p_head();
   if (out_path)
   {
      out = fopen(out_path, "w");
      if (out)
         fprintf(out, "field,hl,vc,%s\n", col_names);
   }

   cfg.frame_callback = on_frame;
   cfg.video_callback = on_video;
   harness_run(&cfg);
   if (out)
      fclose(out);
   if (nfields < hi)
   {
      fprintf(stderr, "run stopped at field %u (< window end %u)\n", nfields, hi);
      harness_shutdown(&cfg);
      return 2;
   }
   if (frames_path)
   {
      FILE *ff = fopen(frames_path, "w");
      if (ff)
      {
         fprintf(ff, "field,flip\n");
         for (f = lo + 1; f <= hi; f++)
            fprintf(ff, "%u,%d\n", f, flip_at[f]);
         fclose(ff);
      }
   }

   build = getenv("VJ_EXPECT_BUILD");
   nf = (double)(hi - lo);
   printf("bus census: %s\n", cfg.rom_path);
   printf("  window fields %u..%u  build=%s  halflines=%lu (%.1f/field)  ring_lost=%llu\n",
          lo, hi, build ? build : "(unset)", nhl, nhl / nf, (unsigned long long)ring_lost);
   printf("  options:");
   for (i = 0; i < cfg.num_options; i++)
      printf(" %s=%s", cfg.options[i].key, cfg.options[i].value);
   printf("\n  MEMCON-derived: dram_row_miss=%u rom_clocks=%u refresh_clks=%u refrate=%u "
          "dram_timing=%u contention_scale=%u\n",
          arb->dram_row_miss, arb->rom_clocks, arb->dram_refresh_clks, arb->refrate,
          arb->enabled, arb->contention_scale);
   {
      int nflip = 0;
      for (f = lo + 1; f <= hi; f++)
         nflip += flip_at[f];
      printf("  flips=%d (%.3f fields/flip)\n", nflip, nflip ? nf / nflip : 0.0);
   }
   printf("  per-field means:\n");
   {
      const char *p = col_names;
      int c;
      for (c = 0; c < NCOL; c++)
      {
         char name[32];
         int k = 0;
         while (*p && *p != ',' && k < 31)
            name[k++] = *p++;
         name[k] = 0;
         if (*p == ',')
            p++;
         printf("    %-16s %12.1f\n", name, sum[c] / nf);
      }
   }
   printf("BUS_CENSUS_RESULT build=%s win=%u-%u", build ? build : "-", lo, hi);
   {
      const char *p = col_names;
      int c;
      for (c = 0; c < NCOL; c++)
      {
         char name[32];
         int k = 0;
         while (*p && *p != ',' && k < 31)
            name[k++] = *p++;
         name[k] = 0;
         if (*p == ',')
            p++;
         printf(" %s=%.1f", name, sum[c] / nf);
      }
   }
   printf("\n");
   top_list("68K sysclks by instruction PC", *p_m68k_pch, snap_m68k, 0x100000, 0,
            top, nf, ram ? *ram : NULL, 0, 0x200000);
   top_list("GPU external transactions by GPU PC", *p_gpu_pch, snap_gpu, 0x800,
            0xF03000, top, nf, NULL, 0, 0);
   top_list("DSP external 16-bit transactions by DSP PC", *p_dsp_pch, snap_dsp, 0x1000,
            0xF1B000, top, nf, NULL, 0, 0);
   top_list("DSP instructions by DSP PC", *p_dsp_ipch, snap_dspi, 0x1000,
            0xF1B000, top, nf, NULL, 0, 0);
   {
      uint16_t (*rdw)(uint32_t, uint32_t) =
         (uint16_t (*)(uint32_t, uint32_t))harness_dlsym(&cfg, "GPUReadWord");
      for (i = 0; i < ndis && rdw; i++)
      {
         uint32_t k;
         printf("disassembly (%s) at $%06X:\n", dis_dsp[i] ? "DSP" : "GPU", dis_at[i]);
         for (k = 0; k < dis_n[i]; k++)
         {
            uint32_t addr = dis_at[i] + k * 2;
            uint16_t w, w2, w3;
            char line[128];
            int n;
            w  = rdw(addr, 0);
            w2 = ((w >> 10) == 38) ? rdw(addr + 2, 0) : 0;
            w3 = ((w >> 10) == 38) ? rdw(addr + 4, 0) : 0;
            n = GDBDisasmOne(dis_dsp[i] ? GDBDisasmDSPMnemonics : GDBDisasmGPUMnemonics,
                             addr, w, w2, w3, line, (int)sizeof(line) - 1);
            line[n] = '\0';
            printf("  %s\n", line);
         }
      }
   }
   harness_shutdown(&cfg);
   return 0;
}
