#include "blitter.h"
#include "blitter_internal.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "jaguar.h"
#include "log.h"
#include "state.h"

/* Per-write trace ring (instrumentation for fast-vs-accurate divergence work).
 * Triggered only when BlitterCompareEnable is on AND blit_cmp_total matches
 * the target blit number (env BLIT_TRACE_BLIT, default disabled). */
#define BLIT_TRACE_MAX 131072

typedef struct {
   uint8_t  phase;  /* 1=fast, 2=acc */
   uint8_t  bits;   /* 8, 16 or 32 */
   uint8_t  kind;   /* 0=write, 1=read */
   uint8_t  pad;
   uint32_t addr;
   uint32_t value;
} blit_trace_entry_t;

int blit_cmp_trace_phase = 0;

static blit_trace_entry_t blit_trace[BLIT_TRACE_MAX];
static int blit_trace_count       = 0;
static int blit_trace_fast_count  = 0;
static int blit_cmp_trace_blit    = -1; /* -1 = no tracing */

static void blit_trace_record(uint32_t addr, uint32_t value, int bits, int kind)
{
   if (blit_cmp_trace_phase == 0)
      return;
   if (blit_trace_count >= BLIT_TRACE_MAX)
      return;
   blit_trace[blit_trace_count].phase = (uint8_t)blit_cmp_trace_phase;
   blit_trace[blit_trace_count].bits  = (uint8_t)bits;
   blit_trace[blit_trace_count].kind  = (uint8_t)kind;
   blit_trace[blit_trace_count].addr  = addr;
   blit_trace[blit_trace_count].value = value;
   blit_trace_count++;
}

void BlitterCompareTraceWrite(uint32_t addr, uint32_t value, int bits)
{
   blit_trace_record(addr, value, bits, 0);
}

void BlitterCompareTraceRead(uint32_t addr, uint32_t value, int bits)
{
   blit_trace_record(addr, value, bits, 1);
}

#define A1_BASE         ((uint32_t)0x00)
#define A1_FLAGS        ((uint32_t)0x04)
#define A1_PIXEL        ((uint32_t)0x0C)
#define A2_BASE         ((uint32_t)0x24)
#define A2_FLAGS        ((uint32_t)0x28)
#define COMMAND         ((uint32_t)0x38)
#define PIXLINECOUNTER  ((uint32_t)0x3C)
#define PATTERNDATA     ((uint32_t)0x68)
#define INTENSITYINC    ((uint32_t)0x70)

#define REG(A) (((uint32_t)blitter_ram[(A)] << 24) | ((uint32_t)blitter_ram[(A) + 1] << 16) \
      | ((uint32_t)blitter_ram[(A) + 2] << 8) | (uint32_t)blitter_ram[(A) + 3])

#define BLIT_CMP_MAX_REGION  (256 * 1024)
#define BLIT_CMP_MAX_LOG     50
#define BLIT_CMP_STATE_SIZE  2048
#define BLIT_CMP_CMD_BUCKETS 16

static int blit_cmp_enabled = 0;
static uint32_t blit_cmp_total = 0;
static uint32_t blit_cmp_diffs = 0;
static uint32_t blit_cmp_skipped = 0;
static uint32_t blit_cmp_logged = 0;

static uint32_t blit_cmp_diff_cmds[BLIT_CMP_CMD_BUCKETS];
static uint32_t blit_cmp_diff_cmd_counts[BLIT_CMP_CMD_BUCKETS];
static int blit_cmp_diff_cmd_count = 0;

static uint8_t *blit_cmp_saved_region = NULL;
static uint8_t *blit_cmp_fast_region = NULL;
static uint8_t blit_cmp_state_buf[BLIT_CMP_STATE_SIZE];

void BlitterCompareEnable(int enable)
{
   blit_cmp_enabled = enable;
   blit_cmp_total = 0;
   blit_cmp_diffs = 0;
   blit_cmp_skipped = 0;
   blit_cmp_logged = 0;
   blit_cmp_diff_cmd_count = 0;
   memset(blit_cmp_diff_cmds, 0, sizeof(blit_cmp_diff_cmds));
   memset(blit_cmp_diff_cmd_counts, 0, sizeof(blit_cmp_diff_cmd_counts));

   if (enable)
   {
      const char *env = getenv("BLIT_TRACE_BLIT");
      blit_cmp_trace_blit = env ? atoi(env) : -1;
      if (blit_cmp_trace_blit >= 0)
         LOG_WRN("[BLIT TRACE] tracing blit #%d (set BLIT_TRACE_BLIT to change)\n",
            blit_cmp_trace_blit);
   }
   else
   {
      blit_cmp_trace_blit = -1;
   }

   if (enable && !blit_cmp_saved_region)
   {
      blit_cmp_saved_region = (uint8_t *)malloc(BLIT_CMP_MAX_REGION);
      blit_cmp_fast_region = (uint8_t *)malloc(BLIT_CMP_MAX_REGION);
   }
   if (!enable && blit_cmp_saved_region)
   {
      free(blit_cmp_saved_region);
      free(blit_cmp_fast_region);
      blit_cmp_saved_region = NULL;
      blit_cmp_fast_region = NULL;
   }
}

int BlitterCompareIsEnabled(void)
{
   return blit_cmp_enabled;
}

void BlitterCompareGetStats(uint32_t *total, uint32_t *diffs, uint32_t *skipped)
{
   if (total) *total = blit_cmp_total;
   if (diffs) *diffs = blit_cmp_diffs;
   if (skipped) *skipped = blit_cmp_skipped;
}

void BlitterCompareDumpCmdStats(void)
{
   int i;
   LOG_INF("[BLIT CMP] Command distribution of differing blits (%d unique patterns):\n",
      blit_cmp_diff_cmd_count);
   for (i = 0; i < blit_cmp_diff_cmd_count; i++)
   {
      uint32_t c = blit_cmp_diff_cmds[i];
      LOG_INF("  cmd=%08X count=%u%s%s%s%s%s%s%s%s\n",
         (unsigned)c, (unsigned)blit_cmp_diff_cmd_counts[i],
         (c & 0x00000001) ? " SRCEN" : "",
         (c & 0x00000008) ? " DSTEN" : "",
         (c & 0x04000000) ? " BCOMPEN" : "",
         (c & 0x08000000) ? " DCOMPEN" : "",
         (c & 0x00010000) ? " PATDSEL" : "",
         (c & 0x00001000) ? " GOURD" : "",
         (c & 0x00002000) ? " GOURZ" : "",
         (c & 0x40000000) ? " SRCSHADE" : "");
   }
}

static void BlitterCompareDumpTrace(void)
{
   int i;
   int fast_n = blit_trace_fast_count;
   int acc_n  = blit_trace_count - blit_trace_fast_count;
   int n      = (fast_n > acc_n) ? fast_n : acc_n;
   int first_div = -1;

   LOG_WRN("[BLIT TRACE] blit #%d: fast writes=%d  accurate writes=%d\n",
      blit_cmp_trace_blit, fast_n, acc_n);

   for (i = 0; i < n; i++)
   {
      blit_trace_entry_t *fe = (i < fast_n) ? &blit_trace[i] : NULL;
      blit_trace_entry_t *ae = (i < acc_n)  ? &blit_trace[fast_n + i] : NULL;
      int diff = 0;
      if (!fe || !ae)
         diff = 1;
      else if (fe->addr != ae->addr || fe->value != ae->value || fe->bits != ae->bits)
         diff = 1;
      if (diff && first_div < 0)
         first_div = i;
      LOG_WRN("[BLIT TRACE] i=%4d  fast=%s  acc=%s%s\n",
         i,
         fe ? "" : "  -",
         ae ? "" : "  -",
         diff ? "  <-- DIFF" : "");
      if (fe)
         LOG_WRN("                  fast: %s %2u-bit addr=%06X value=%08X\n",
            fe->kind ? "RD" : "WR",
            (unsigned)fe->bits, (unsigned)fe->addr, (unsigned)fe->value);
      if (ae)
         LOG_WRN("                  acc : %s %2u-bit addr=%06X value=%08X\n",
            ae->kind ? "RD" : "WR",
            (unsigned)ae->bits, (unsigned)ae->addr, (unsigned)ae->value);
   }
   if (first_div >= 0)
      LOG_WRN("[BLIT TRACE] first divergence at write index %d\n", first_div);
   else
      LOG_WRN("[BLIT TRACE] no divergence detected in trace\n");
}

void BlitterRunComparison(void)
{
   uint32_t cmd = GET32(blitter_ram, COMMAND);
   int dsta2 = (cmd & 0x00000800) ? 1 : 0;
   uint32_t dst_base;
   uint32_t save_start, save_size;
   size_t state_size;
   int diff_bytes, first_diff, last_diff;
   uint32_t i;

   blit_cmp_total++;

   if (!blit_cmp_saved_region || !blit_cmp_fast_region)
   {
      blit_cmp_skipped++;
      return;
   }

   if ((cmd & 0x00001000) && blit_cmp_logged < 5)
   {
      uint32_t a1flg = GET32(blitter_ram, A1_FLAGS);
      uint32_t a2flg = GET32(blitter_ram, A2_FLAGS);
      uint8_t a1addx = (a1flg >> 8) & 0x03;
      uint8_t a2addx = (a2flg >> 8) & 0x03;
      (void)a1addx;
      (void)a2addx;
      {
         uint64_t patd_in = GET64(blitter_ram, PATTERNDATA);
         uint32_t iinc_in = GET32(blitter_ram, INTENSITYINC);
         uint16_t a1x = GET16(blitter_ram, A1_PIXEL + 2);
         uint8_t pixAddr_est = (a1x * 2) & 0x07;
         LOG_WRN("[BLIT CMP] INPUT cmd=%08X A1pix=%08X(pixAddr=%u) "
            "cnt=%04X_%04X PATD=%04X_%04X_%04X_%04X IINC=%08X\n",
            (unsigned)cmd,
            (unsigned)GET32(blitter_ram, A1_PIXEL),
            (unsigned)pixAddr_est,
            (unsigned)GET16(blitter_ram, PIXLINECOUNTER),
            (unsigned)GET16(blitter_ram, PIXLINECOUNTER + 2),
            (unsigned)((patd_in >> 48) & 0xFFFF),
            (unsigned)((patd_in >> 32) & 0xFFFF),
            (unsigned)((patd_in >> 16) & 0xFFFF),
            (unsigned)(patd_in & 0xFFFF),
            (unsigned)iinc_in);
      }
   }

   dst_base = dsta2 ? (GET32(blitter_ram, A2_BASE) & 0xFFFFFFF8)
                    : (GET32(blitter_ram, A1_BASE) & 0xFFFFFFF8);

   save_start = dst_base & 0x1FFFFF;
   save_size = 0x200000 - save_start;
   if (save_size > BLIT_CMP_MAX_REGION)
      save_size = BLIT_CMP_MAX_REGION;

   memcpy(blit_cmp_saved_region, jaguarMainRAM + save_start, save_size);
   state_size = BlitterStateSave(blit_cmp_state_buf);
   if (state_size > BLIT_CMP_STATE_SIZE)
   {
      blit_cmp_skipped++;
      return;
   }

   {
      int trace_active = (blit_cmp_trace_blit >= 0
                        && (int)blit_cmp_total == blit_cmp_trace_blit);
      if (trace_active)
      {
         LOG_WRN("[BLIT TRACE] pre-FAST  A1_BASE=%08X A2_BASE=%08X "
            "A1_PIX=%08X A1_FPIX=%08X A2_PIX=%08X A1_FLAGS=%08X A2_FLAGS=%08X "
            "A1_STEP=%08X A1_FSTEP=%08X A2_STEP=%08X PIXLINE=%08X CMD=%08X\n",
            (unsigned)GET32(blitter_ram, A1_BASE),
            (unsigned)GET32(blitter_ram, A2_BASE),
            (unsigned)GET32(blitter_ram, A1_PIXEL),
            (unsigned)GET32(blitter_ram, 0x18 /* A1_FPIXEL */),
            (unsigned)GET32(blitter_ram, 0x30 /* A2_PIXEL */),
            (unsigned)GET32(blitter_ram, A1_FLAGS),
            (unsigned)GET32(blitter_ram, A2_FLAGS),
            (unsigned)GET32(blitter_ram, 0x10 /* A1_STEP */),
            (unsigned)GET32(blitter_ram, 0x14 /* A1_FSTEP */),
            (unsigned)GET32(blitter_ram, 0x34 /* A2_STEP */),
            (unsigned)GET32(blitter_ram, PIXLINECOUNTER),
            (unsigned)cmd);
         blit_trace_count = 0;
         blit_cmp_trace_phase = 1; /* fast */
      }

      blitter_blit(cmd);

      if (trace_active)
      {
         blit_trace_fast_count = blit_trace_count;
         blit_cmp_trace_phase = 2; /* accurate */
      }
   }

   memcpy(blit_cmp_fast_region, jaguarMainRAM + save_start, save_size);
   {
      uint8_t fast_regs[0x100];
      memcpy(fast_regs, blitter_ram, 0x100);

      if (blit_cmp_trace_blit >= 0 && (int)blit_cmp_total == blit_cmp_trace_blit)
         LOG_WRN("[BLIT TRACE] post-FAST A1_PIX=%08X A1_FPIX=%08X A2_PIX=%08X\n",
            (unsigned)GET32(fast_regs, A1_PIXEL),
            (unsigned)GET32(fast_regs, 0x18 /* A1_FPIXEL */),
            (unsigned)GET32(fast_regs, 0x30 /* A2_PIXEL */));

      memcpy(jaguarMainRAM + save_start, blit_cmp_saved_region, save_size);
      BlitterStateLoad(blit_cmp_state_buf);

      if (blit_cmp_trace_phase == 2)
      {
         LOG_WRN("[BLIT TRACE] pre-ACC   A1_BASE=%08X A2_BASE=%08X "
            "A1_PIX=%08X A1_FPIX=%08X A2_PIX=%08X A1_FLAGS=%08X A2_FLAGS=%08X "
            "A1_STEP=%08X A1_FSTEP=%08X A2_STEP=%08X PIXLINE=%08X CMD=%08X\n",
            (unsigned)GET32(blitter_ram, A1_BASE),
            (unsigned)GET32(blitter_ram, A2_BASE),
            (unsigned)GET32(blitter_ram, A1_PIXEL),
            (unsigned)GET32(blitter_ram, 0x18 /* A1_FPIXEL */),
            (unsigned)GET32(blitter_ram, 0x30 /* A2_PIXEL */),
            (unsigned)GET32(blitter_ram, A1_FLAGS),
            (unsigned)GET32(blitter_ram, A2_FLAGS),
            (unsigned)GET32(blitter_ram, 0x10 /* A1_STEP */),
            (unsigned)GET32(blitter_ram, 0x14 /* A1_FSTEP */),
            (unsigned)GET32(blitter_ram, 0x34 /* A2_STEP */),
            (unsigned)GET32(blitter_ram, PIXLINECOUNTER),
            (unsigned)cmd);
      }

      BlitterMidsummer2();

      if (blit_cmp_trace_phase == 2)
      {
         LOG_WRN("[BLIT TRACE] post-ACC  A1_PIX=%08X A1_FPIX=%08X A2_PIX=%08X\n",
            (unsigned)GET32(blitter_ram, A1_PIXEL),
            (unsigned)GET32(blitter_ram, 0x18 /* A1_FPIXEL */),
            (unsigned)GET32(blitter_ram, 0x30 /* A2_PIXEL */));
         blit_cmp_trace_phase = 0;
         BlitterCompareDumpTrace();
      }

      if (blit_cmp_logged < 10)
      {
         uint32_t fa1p = GET32(fast_regs, 0x0C);
         uint32_t aa1p = GET32(blitter_ram, 0x0C);
         uint32_t fa1f = GET32(fast_regs, 0x18);
         uint32_t aa1f = GET32(blitter_ram, 0x18);
         uint32_t fa2p = GET32(fast_regs, 0x30);
         uint32_t aa2p = GET32(blitter_ram, 0x30);
         uint64_t fpatd = ((uint64_t)GET32(fast_regs, PATTERNDATA) << 32) | GET32(fast_regs, PATTERNDATA + 4);
         uint64_t apatd = ((uint64_t)GET32(blitter_ram, PATTERNDATA) << 32) | GET32(blitter_ram, PATTERNDATA + 4);
         if (fa1p != aa1p || fa1f != aa1f || fa2p != aa2p)
         {
            LOG_WRN("[BLIT CMP] REG DIFF A1pix fast=%08X acc=%08X A2pix fast=%08X acc=%08X\n",
               (unsigned)fa1p, (unsigned)aa1p, (unsigned)fa2p, (unsigned)aa2p);
         }
         if (fpatd != apatd)
         {
            LOG_WRN("[BLIT CMP] PATD DIFF fast=%04X_%04X_%04X_%04X acc=%04X_%04X_%04X_%04X IINC=%08X\n",
               (unsigned)((fpatd >> 48) & 0xFFFF), (unsigned)((fpatd >> 32) & 0xFFFF),
               (unsigned)((fpatd >> 16) & 0xFFFF), (unsigned)(fpatd & 0xFFFF),
               (unsigned)((apatd >> 48) & 0xFFFF), (unsigned)((apatd >> 32) & 0xFFFF),
               (unsigned)((apatd >> 16) & 0xFFFF), (unsigned)(apatd & 0xFFFF),
               (unsigned)GET32(blitter_ram, INTENSITYINC));
         }
      }
   }

   diff_bytes = 0;
   first_diff = -1;
   last_diff = -1;
   for (i = 0; i < save_size; i++)
   {
      if (blit_cmp_fast_region[i] != jaguarMainRAM[save_start + i])
      {
         diff_bytes++;
         if (first_diff < 0)
            first_diff = (int)i;
         last_diff = (int)i;
      }
   }

   if (diff_bytes > 0)
   {
      int ci;
      int found_bucket = 0;
      blit_cmp_diffs++;

      for (ci = 0; ci < blit_cmp_diff_cmd_count; ci++)
      {
         if (blit_cmp_diff_cmds[ci] == cmd)
         {
            blit_cmp_diff_cmd_counts[ci]++;
            found_bucket = 1;
            break;
         }
      }
      if (!found_bucket && blit_cmp_diff_cmd_count < BLIT_CMP_CMD_BUCKETS)
      {
         blit_cmp_diff_cmds[blit_cmp_diff_cmd_count] = cmd;
         blit_cmp_diff_cmd_counts[blit_cmp_diff_cmd_count] = 1;
         blit_cmp_diff_cmd_count++;
      }

      if (blit_cmp_logged < BLIT_CMP_MAX_LOG)
      {
         uint16_t n_pix = GET16(blitter_ram, PIXLINECOUNTER + 2);
         uint16_t n_lin = GET16(blitter_ram, PIXLINECOUNTER);
         uint32_t dst_flags = dsta2 ? REG(A2_FLAGS) : REG(A1_FLAGS);
         uint8_t pixsz = (dst_flags >> 3) & 0x07;

         blit_cmp_logged++;
         LOG_WRN("[BLIT CMP] #%u DIFF cmd=%08X dst=%s base=%06X "
            "pix=%ux%u sz=%ubpp%s%s%s%s%s%s%s "
            "diff=%d bytes [%06X-%06X]\n",
            (unsigned)blit_cmp_total, (unsigned)cmd,
            dsta2 ? "A2" : "A1", (unsigned)dst_base,
            (unsigned)n_pix, (unsigned)n_lin,
            (unsigned)(1 << pixsz),
            (cmd & 0x00000001) ? " SRCEN" : "",
            (cmd & 0x00000008) ? " DSTEN" : "",
            (cmd & 0x04000000) ? " BCOMPEN" : "",
            (cmd & 0x08000000) ? " DCOMPEN" : "",
            (cmd & 0x00010000) ? " PATDSEL" : "",
            (cmd & 0x00001000) ? " GOURD" : "",
            ((dst_flags >> 16) & 0x03) == 0 ? " PHRASE" : "",
            diff_bytes,
            (unsigned)(save_start + first_diff),
            (unsigned)(save_start + last_diff));

         if (pixsz == 4 && blit_cmp_logged <= 10)
         {
            int shown = 0;
            for (i = first_diff & ~1u; i <= (uint32_t)last_diff && shown < 4; i += 2)
            {
               uint16_t fast_px = ((uint16_t)blit_cmp_fast_region[i] << 8) | blit_cmp_fast_region[i + 1];
               uint16_t acc_px = ((uint16_t)jaguarMainRAM[save_start + i] << 8) | jaguarMainRAM[save_start + i + 1];
               if (fast_px != acc_px)
               {
                  LOG_WRN("  @%06X fast=%04X acc=%04X\n",
                     (unsigned)(save_start + i), (unsigned)fast_px, (unsigned)acc_px);
                  shown++;
               }
            }
         }
      }
   }
}
