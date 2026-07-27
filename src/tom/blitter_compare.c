#include "blitter.h"
#include "blitter_internal.h"

#include <stdlib.h>
#include <string.h>
#include "jaguar.h"
#include "log.h"
#include "settings.h"
#include "state.h"

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

/* Filter state — independent of enable/disable.  Setters can be called
 * before or after BlitterCompareEnable. */
static uint32_t blit_cmp_frame = 0;
static uint32_t blit_cmp_frame_first = 0;
static uint32_t blit_cmp_frame_last = 0xFFFFFFFFu;
static uint32_t blit_cmp_filter_mask = 0;     /* 0 = accept all */
static uint32_t blit_cmp_filter_value = 0;
static int blit_cmp_verbose = 0;

/* Run the non-compare default blit path.  Used when a filter rejects
 * a blit so game state still advances normally — mirrors the dispatch
 * in BlitterWriteWord at offset 0x3A. */
static void blit_cmp_run_default(uint32_t cmd)
{
   if (vjs.useFastBlitter)
      blitter_blit(cmd);
   else
      BlitterMidsummer2();
}

/* Verbose diff dump.  Prints pre-blit register snapshot, fast vs accurate
 * post-blit register deltas, and a side-by-side hexdump of the first 32
 * bytes of the differing destination region.  Caller gates by
 * blit_cmp_verbose so this only fires when explicitly requested.
 *
 * pre_regs : 0x100-byte snapshot of blitter_ram BEFORE either path ran
 * fast_regs: 0x100-byte snapshot AFTER the fast path ran
 * acc_regs : current blitter_ram (AFTER the accurate path ran) */
static void blit_cmp_verbose_dump(uint32_t cmd, int dsta2, uint32_t dst_base,
                                  const uint8_t *pre_regs,
                                  const uint8_t *fast_regs,
                                  const uint8_t *acc_regs,
                                  uint32_t save_start,
                                  const uint8_t *fast_region,
                                  int first_diff, int last_diff)
{
   /* Local register-decode aliases (offsets match blitter_mmio.c). */
   uint32_t a1_base   = GET32(pre_regs, 0x00);
   uint32_t a1_flags  = GET32(pre_regs, 0x04);
   uint32_t a1_pixel  = GET32(pre_regs, 0x0C);
   uint32_t a1_step   = GET32(pre_regs, 0x10);
   uint32_t a1_fstep  = GET32(pre_regs, 0x14);
   uint32_t a2_base   = GET32(pre_regs, 0x24);
   uint32_t a2_flags  = GET32(pre_regs, 0x28);
   uint32_t a2_pixel  = GET32(pre_regs, 0x30);
   uint32_t a2_step   = GET32(pre_regs, 0x34);
   uint16_t pix_cnt   = GET16(pre_regs, 0x3E);  /* PIXLINECOUNTER lower */
   uint16_t lin_cnt   = GET16(pre_regs, 0x3C);  /* PIXLINECOUNTER upper */
   uint64_t srcd      = GET64(pre_regs, 0x40);
   uint64_t dstd      = GET64(pre_regs, 0x48);
   uint64_t dstz      = GET64(pre_regs, 0x50);
   uint64_t srcz1     = GET64(pre_regs, 0x58);
   uint64_t srcz2     = GET64(pre_regs, 0x60);
   uint64_t patd      = GET64(pre_regs, 0x68);
   uint32_t iinc      = GET32(pre_regs, 0x70);
   uint32_t zinc      = GET32(pre_regs, 0x74);

   uint32_t fast_a1pix = GET32(fast_regs, 0x0C);
   uint32_t fast_a1fp  = GET32(fast_regs, 0x18);
   uint32_t fast_a2pix = GET32(fast_regs, 0x30);
   uint64_t fast_patd  = GET64(fast_regs, 0x68);

   uint32_t acc_a1pix = GET32(acc_regs, 0x0C);
   uint32_t acc_a1fp  = GET32(acc_regs, 0x18);
   uint32_t acc_a2pix = GET32(acc_regs, 0x30);
   uint64_t acc_patd  = GET64(acc_regs, 0x68);

   uint32_t dump_start;
   uint32_t dump_end;
   uint32_t off;
   char fast_hex[3 * 32 + 1];
   char acc_hex[3 * 32 + 1];

   LOG_WRN("[BLIT CMP V] cmd=%08X dst=%s base=%06X frame=%u\n",
      (unsigned)cmd, dsta2 ? "A2" : "A1", (unsigned)dst_base,
      (unsigned)blit_cmp_frame);
   LOG_WRN("[BLIT CMP V]   PRE  A1 BASE=%08X FLAGS=%08X PIXEL=%08X STEP=%08X FSTEP=%08X\n",
      (unsigned)a1_base, (unsigned)a1_flags, (unsigned)a1_pixel,
      (unsigned)a1_step, (unsigned)a1_fstep);
   LOG_WRN("[BLIT CMP V]   PRE  A2 BASE=%08X FLAGS=%08X PIXEL=%08X STEP=%08X\n",
      (unsigned)a2_base, (unsigned)a2_flags, (unsigned)a2_pixel,
      (unsigned)a2_step);
   LOG_WRN("[BLIT CMP V]   PRE  CNT=%04X_%04X PATD=%016llX SRCD=%016llX DSTD=%016llX\n",
      (unsigned)lin_cnt, (unsigned)pix_cnt,
      (unsigned long long)patd, (unsigned long long)srcd,
      (unsigned long long)dstd);
   LOG_WRN("[BLIT CMP V]   PRE  DSTZ=%016llX SRCZ1=%016llX SRCZ2=%016llX\n",
      (unsigned long long)dstz, (unsigned long long)srcz1,
      (unsigned long long)srcz2);
   LOG_WRN("[BLIT CMP V]   PRE  IINC=%08X ZINC=%08X\n",
      (unsigned)iinc, (unsigned)zinc);
   LOG_WRN("[BLIT CMP V]   FAST POST A1pix=%08X A1fp=%08X A2pix=%08X PATD=%016llX\n",
      (unsigned)fast_a1pix, (unsigned)fast_a1fp, (unsigned)fast_a2pix,
      (unsigned long long)fast_patd);
   LOG_WRN("[BLIT CMP V]   ACC  POST A1pix=%08X A1fp=%08X A2pix=%08X PATD=%016llX\n",
      (unsigned)acc_a1pix, (unsigned)acc_a1fp, (unsigned)acc_a2pix,
      (unsigned long long)acc_patd);

   /* Dump first 16 bytes of source RAM (A2 base for !DSTA2 blits, A1
    * base for DSTA2 blits).  Restored to pre-blit state at this point
    * (fast's writes were rolled back; accurate had its writes in flight).
    * Lets us see whether the source data matches fast's writeback. */
   {
      uint32_t src_base = dsta2
         ? (GET32(pre_regs, 0x00) & 0xFFFFFFF8)   /* A1_BASE */
         : (GET32(pre_regs, 0x24) & 0xFFFFFFF8);  /* A2_BASE */
      uint32_t src_off = src_base & 0x1FFFFF;
      char src_hex[3 * 16 + 1];
      char *sp = src_hex;
      uint32_t k;
      for (k = 0; k < 16; k++)
      {
         int n = sprintf(sp, "%02X ", jaguarMainRAM[(src_off + k) & 0x1FFFFF]);
         if (n > 0) sp += n;
      }
      *sp = '\0';
      LOG_WRN("[BLIT CMP V]   SRC @%06X (%s base): %s\n",
         (unsigned)src_base, dsta2 ? "A1" : "A2", src_hex);
   }

   /* Side-by-side hex of 32 bytes starting at first_diff (clamped to
    * last_diff + 1). */
   if (first_diff < 0)
      return;
   dump_start = (uint32_t)first_diff;
   dump_end = dump_start + 32;
   if ((int)dump_end > last_diff + 1)
      dump_end = (uint32_t)(last_diff + 1);
   if (dump_end > dump_start + 32)
      dump_end = dump_start + 32;

   {
      char *fp = fast_hex;
      char *ap = acc_hex;
      for (off = dump_start; off < dump_end; off++)
      {
         int n;
         n = sprintf(fp, "%02X ", fast_region[off]);
         if (n > 0) fp += n;
         n = sprintf(ap, "%02X ", jaguarMainRAM[save_start + off]);
         if (n > 0) ap += n;
      }
      *fp = '\0';
      *ap = '\0';
   }
   LOG_WRN("[BLIT CMP V]   DST @%06X fast: %s\n",
      (unsigned)(save_start + dump_start), fast_hex);
   LOG_WRN("[BLIT CMP V]   DST @%06X acc:  %s\n",
      (unsigned)(save_start + dump_start), acc_hex);
}

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

void BlitterCompareSetFrame(uint32_t frame)
{
   blit_cmp_frame = frame;
}

void BlitterCompareSetFrameWindow(uint32_t first, uint32_t last)
{
   blit_cmp_frame_first = first;
   blit_cmp_frame_last = last;
}

void BlitterCompareSetCmdMask(uint32_t mask, uint32_t value)
{
   blit_cmp_filter_mask = mask;
   blit_cmp_filter_value = value;
}

void BlitterCompareSetVerbose(int verbose)
{
   blit_cmp_verbose = verbose ? 1 : 0;
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

void BlitterRunComparison(void)
{
   uint32_t cmd = GET32(blitter_ram, COMMAND);
   int dsta2 = (cmd & 0x00000800) ? 1 : 0;
   uint32_t dst_base;
   uint32_t save_start, save_size;
   size_t state_size;
   int diff_bytes, first_diff, last_diff;
   uint32_t i;
   uint8_t pre_regs[0x100];
   uint8_t fast_regs[0x100];

   blit_cmp_total++;

   /* Frame-window filter.  Outside [first,last] we don't compare; we
    * just run the blit so game state advances. */
   if (blit_cmp_frame < blit_cmp_frame_first
         || blit_cmp_frame > blit_cmp_frame_last)
   {
      blit_cmp_skipped++;
      blit_cmp_run_default(cmd);
      return;
   }

   /* Cmd-mask filter: compare only when (cmd & mask) == value. */
   if (blit_cmp_filter_mask != 0
         && (cmd & blit_cmp_filter_mask) != blit_cmp_filter_value)
   {
      blit_cmp_skipped++;
      blit_cmp_run_default(cmd);
      return;
   }

   if (!blit_cmp_saved_region || !blit_cmp_fast_region)
   {
      blit_cmp_skipped++;
      blit_cmp_run_default(cmd);
      return;
   }

   /* Snapshot the pre-blit register file so the verbose dump on diff
    * can show the inputs both paths started from. */
   memcpy(pre_regs, blitter_ram, sizeof(pre_regs));

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

   blitter_blit(cmd);

   memcpy(blit_cmp_fast_region, jaguarMainRAM + save_start, save_size);
   memcpy(fast_regs, blitter_ram, sizeof(fast_regs));
   {
      memcpy(jaguarMainRAM + save_start, blit_cmp_saved_region, save_size);
      BlitterStateLoad(blit_cmp_state_buf);

      BlitterMidsummer2();

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

         if (blit_cmp_verbose)
            blit_cmp_verbose_dump(cmd, dsta2, dst_base,
               pre_regs, fast_regs, blitter_ram,
               save_start, blit_cmp_fast_region,
               first_diff, last_diff);
      }
   }
}
