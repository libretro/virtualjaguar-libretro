/*
 * shadowfb.c: True-color shadow precision framebuffer (epic #338 track 3)
 *
 * See shadowfb.h and docs/true-color-shadowfb-design.md for the
 * architecture.  All state here is a derived cache: never savestated,
 * cleared on savestate load / option toggle, fully reset in
 * ShadowFBShutdown (iOS cannot dlclose cores).
 */

#include <stdlib.h>
#include <string.h>

#include "shadowfb.h"
#include "log.h"

/* CRY chroma tables + stock LUT live in tom.c */
extern uint8_t redcv[16][16];
extern uint8_t greencv[16][16];
extern uint8_t bluecv[16][16];
extern uint32_t CRY16ToRGB32[0x10000];

/* Main RAM is 2MB = 1M 16-bit words, mirrored through the bottom 8MB of
 * the address space (see JaguarReadWord/JaguarWriteWord in jaguar.c). */
#define SHADOWFB_WORDS 0x100000

int shadowFBActive = 0;

uint32_t shadowLineRGB[SHADOWFB_LINE_PIXELS];
uint32_t shadowLineTag[SHADOWFB_LINE_PIXELS];

static uint32_t *shadowRGB = NULL;
static uint32_t *shadowTag = NULL;

uint32_t ShadowFBCryRGB(uint16_t value16, uint16_t frac16)
{
   uint32_t cyan = ((uint32_t)value16 & 0xF000) >> 12;
   uint32_t red  = ((uint32_t)value16 & 0x0F00) >> 8;
   uint32_t i24  = (((uint32_t)value16 & 0x00FF) << 16) | frac16;
   uint32_t r = ((uint32_t)redcv[cyan][red]   * i24) >> 24;
   uint32_t g = ((uint32_t)greencv[cyan][red] * i24) >> 24;
   uint32_t b = ((uint32_t)bluecv[cyan][red]  * i24) >> 24;
   return (r << 16) | (g << 8) | b;
}

void ShadowFBStoreCry(uint32_t addr, uint16_t value16, uint16_t frac16)
{
   uint32_t idx;
   if (!shadowFBActive)
      return;
   addr &= 0xFFFFFF;
   if (addr >= 0x800000)
      return;
   idx = (addr & 0x1FFFFE) >> 1;
   shadowRGB[idx] = ShadowFBCryRGB(value16, frac16);
   shadowTag[idx] = (uint32_t)value16 | SHADOWFB_TAG_VALID;
}

int ShadowFBLookup(uint32_t addr, uint16_t current16, uint32_t *rgb888)
{
   uint32_t idx;
   if (!shadowFBActive)
      return 0;
   addr &= 0xFFFFFF;
   if (addr >= 0x800000)
      return 0;
   idx = (addr & 0x1FFFFE) >> 1;
   if (shadowTag[idx] != ((uint32_t)current16 | SHADOWFB_TAG_VALID))
      return 0;
   *rgb888 = shadowRGB[idx];
   return 1;
}

void ShadowFBLineFromRAM(int idx, uint32_t srcAddr, uint16_t value16)
{
   uint32_t rgb;
   if (idx < 0 || idx >= SHADOWFB_LINE_PIXELS)
      return;
   if (!ShadowFBLookup(srcAddr, value16, &rgb))
      rgb = CRY16ToRGB32[value16] & 0x00FFFFFF;
   shadowLineRGB[idx] = rgb;
   shadowLineTag[idx] = (uint32_t)value16 | SHADOWFB_TAG_VALID;
}

void ShadowFBInvalidate(void)
{
   if (shadowTag)
      memset(shadowTag, 0, SHADOWFB_WORDS * sizeof(uint32_t));
   memset(shadowLineTag, 0, sizeof(shadowLineTag));
}

void ShadowFBSetEnabled(int enable)
{
   if (enable)
   {
      if (shadowFBActive)
         return;
      if (!shadowRGB)
         shadowRGB = (uint32_t *)malloc(SHADOWFB_WORDS * sizeof(uint32_t));
      if (!shadowTag)
         shadowTag = (uint32_t *)malloc(SHADOWFB_WORDS * sizeof(uint32_t));
      if (!shadowRGB || !shadowTag)
      {
         LOG_WRN("[SHADOWFB] allocation failed (8MB); true-color disabled, running stock\n");
         ShadowFBShutdown();
         return;
      }
      ShadowFBInvalidate();
      shadowFBActive = 1;
   }
   else
      ShadowFBShutdown();
}

void ShadowFBShutdown(void)
{
   if (shadowRGB)
      free(shadowRGB);
   if (shadowTag)
      free(shadowTag);
   shadowRGB = NULL;
   shadowTag = NULL;
   shadowFBActive = 0;
   memset(shadowLineRGB, 0, sizeof(shadowLineRGB));
   memset(shadowLineTag, 0, sizeof(shadowLineTag));
}

/* ==================================================================
 * Hi-res (Nx) shadow surface -- epic #338 track 1, Stage 1.
 * See shadowfb.h and docs/hires-upscaling-design.md.
 * ================================================================== */

/* Tag layout (design section 3.4):
 *   bits  0-15  value16 (the stock word the block was derived from)
 *   bit  16     VALID (so zero-init never false-matches RAM zeros)
 *   bits 17-24  frame epoch (entry trusted iff epoch == now or now-1)
 */
#define HIRES_TAG_EPOCH_SHIFT 17

int shadowHiresActive = 0;
int shadowHiresN = 1;

shadowfb_sub *shadowHiresLineSub = NULL;
uint32_t shadowHiresLineTag[SHADOWFB_LINE_PIXELS];

/* Per-page pointers: one malloc per page, tag block first then the
 * subpixel block.  NULL = not (yet) allocated. */
static uint32_t *hiresPageTag[SHADOWFB_HIRES_PAGES];
static shadowfb_sub *hiresPageSub[SHADOWFB_HIRES_PAGES];
static uint32_t hiresEpoch = 0;
static uint32_t hiresBytesAllocated = 0;
static int hiresAllocStopped = 0;   /* cap hit or malloc failed: log once,
                                     * degrade to NN for unshadowed pages */

/* Allocate (or return) the page covering stock word index `idx`.
 * Returns 0 when allocation is stopped (cap / failure): callers then
 * simply skip the store, and readers fall back to replication. */
static int shadow_hires_page(uint32_t page)
{
   uint32_t nn;
   uint32_t bytes;
   uint8_t *base;

   if (hiresPageTag[page])
      return 1;
   if (hiresAllocStopped)
      return 0;

   nn    = (uint32_t)shadowHiresN * (uint32_t)shadowHiresN;
   bytes = SHADOWFB_HIRES_PAGE_WORDS * (uint32_t)sizeof(uint32_t)
         + SHADOWFB_HIRES_PAGE_WORDS * nn * (uint32_t)sizeof(shadowfb_sub);

   if (hiresBytesAllocated + bytes > SHADOWFB_HIRES_CAP_BYTES)
   {
      LOG_WRN("[SHADOWFB] hi-res page cap reached (%u bytes); further pages degrade to nearest-neighbour\n",
              (unsigned)hiresBytesAllocated);
      hiresAllocStopped = 1;
      return 0;
   }

   base = (uint8_t *)malloc(bytes);
   if (!base)
   {
      LOG_WRN("[SHADOWFB] hi-res page allocation failed (%u bytes); degrading to nearest-neighbour\n",
              (unsigned)bytes);
      hiresAllocStopped = 1;
      return 0;
   }

   /* Zeroed tags never match (VALID bit clear); sub content can stay
    * uninitialised behind them. */
   memset(base, 0, SHADOWFB_HIRES_PAGE_WORDS * sizeof(uint32_t));
   hiresPageTag[page] = (uint32_t *)base;
   hiresPageSub[page] =
      (shadowfb_sub *)(base + SHADOWFB_HIRES_PAGE_WORDS * sizeof(uint32_t));
   hiresBytesAllocated += bytes;
   return 1;
}

void ShadowHiresStoreCry(uint32_t addr, uint16_t value16, uint16_t frac16)
{
   uint32_t idx, page, word, nn, k;
   shadowfb_sub *sub;

   if (!shadowHiresActive)
      return;
   addr &= 0xFFFFFF;
   if (addr >= 0x800000)
      return;
   idx  = (addr & 0x1FFFFE) >> 1;
   page = idx >> 12;
   word = idx & 0xFFF;
   if (!shadow_hires_page(page))
      return;

   nn  = (uint32_t)shadowHiresN * (uint32_t)shadowHiresN;
   sub = hiresPageSub[page] + word * nn;
   /* Stage 1: box replication -- every subpixel carries the stock
    * value (+ the true-color fraction when the write site had one).
    * Stage 2 replaces this loop with real sub-pixel content. */
   for (k = 0; k < nn; k++)
   {
      sub[k].value16 = value16;
      sub[k].frac16  = frac16;
   }
   hiresPageTag[page][word] = (uint32_t)value16 | SHADOWFB_TAG_VALID
                            | (hiresEpoch << HIRES_TAG_EPOCH_SHIFT);
}

/* Value+epoch-checked block lookup.  Returns the N*N block or NULL. */
static const shadowfb_sub *shadow_hires_block(uint32_t addr, uint16_t current16)
{
   uint32_t idx, page, word, tag, ep;

   addr &= 0xFFFFFF;
   if (addr >= 0x800000)
      return NULL;
   idx  = (addr & 0x1FFFFE) >> 1;
   page = idx >> 12;
   if (!hiresPageTag[page])
      return NULL;
   word = idx & 0xFFF;
   tag  = hiresPageTag[page][word];
   if ((tag & 0x1FFFF) != ((uint32_t)current16 | SHADOWFB_TAG_VALID))
      return NULL;
   ep = (tag >> HIRES_TAG_EPOCH_SHIFT) & 0xFF;
   if (ep != hiresEpoch && ep != ((hiresEpoch - 1) & 0xFF))
      return NULL;
   return hiresPageSub[page]
        + word * (uint32_t)shadowHiresN * (uint32_t)shadowHiresN;
}

void ShadowHiresLineFromRAM(int idx, uint32_t srcAddr, uint16_t value16)
{
   const shadowfb_sub *blk;
   shadowfb_sub *dst;
   int n, sy, sx;

   if (!shadowHiresActive)
      return;
   if (idx < 0 || idx >= SHADOWFB_LINE_PIXELS)
      return;

   n   = shadowHiresN;
   blk = shadow_hires_block(srcAddr, value16);
   for (sy = 0; sy < n; sy++)
   {
      dst = shadowHiresLineSub
          + ((uint32_t)sy * SHADOWFB_LINE_PIXELS + (uint32_t)idx) * (uint32_t)n;
      for (sx = 0; sx < n; sx++)
      {
         if (blk)
            dst[sx] = blk[sy * n + sx];
         else
         {
            dst[sx].value16 = value16;
            dst[sx].frac16  = 0;
         }
      }
   }
   shadowHiresLineTag[idx] = (uint32_t)value16 | SHADOWFB_TAG_VALID;
}

/* Clear every allocated page tag (VALID bit off).  Cost is per stock
 * word, independent of N. */
static void shadow_hires_clear_tags(void)
{
   unsigned p;
   for (p = 0; p < SHADOWFB_HIRES_PAGES; p++)
      if (hiresPageTag[p])
         memset(hiresPageTag[p], 0,
                SHADOWFB_HIRES_PAGE_WORDS * sizeof(uint32_t));
}

void ShadowHiresFrameTick(void)
{
   if (!shadowHiresActive)
      return;
   hiresEpoch = (hiresEpoch + 1) & 0xFF;
   /* On epoch wrap, entries stamped 256/257 frames ago would re-enter
    * the trusted window; clearing all tags at the wrap closes that
    * hole for ~4MB of memset every 256 frames, worst case. */
   if (hiresEpoch == 0)
      shadow_hires_clear_tags();
}

void ShadowHiresInvalidate(void)
{
   shadow_hires_clear_tags();
   memset(shadowHiresLineTag, 0, sizeof(shadowHiresLineTag));
}

void ShadowHiresShutdown(void)
{
   unsigned p;
   for (p = 0; p < SHADOWFB_HIRES_PAGES; p++)
   {
      if (hiresPageTag[p])
         free(hiresPageTag[p]);
      hiresPageTag[p] = NULL;
      hiresPageSub[p] = NULL;
   }
   if (shadowHiresLineSub)
      free(shadowHiresLineSub);
   shadowHiresLineSub = NULL;
   memset(shadowHiresLineTag, 0, sizeof(shadowHiresLineTag));
   hiresEpoch = 0;
   hiresBytesAllocated = 0;
   hiresAllocStopped = 0;
   shadowHiresActive = 0;
   shadowHiresN = 1;
}

void ShadowHiresSetN(int n)
{
   size_t lineEntries;

   ShadowHiresShutdown();
   if (n <= 1)
      return;
   if (n > SHADOWFB_HIRES_MAX_N)
      n = SHADOWFB_HIRES_MAX_N;

   /* N sub-rows of 720*N entries. */
   lineEntries = (size_t)n * (size_t)n * (size_t)SHADOWFB_LINE_PIXELS;
   shadowHiresLineSub =
      (shadowfb_sub *)malloc(lineEntries * sizeof(shadowfb_sub));
   if (!shadowHiresLineSub)
   {
      LOG_WRN("[SHADOWFB] hi-res line buffer allocation failed; running at 1x\n");
      ShadowHiresShutdown();
      return;
   }
   memset(shadowHiresLineSub, 0, lineEntries * sizeof(shadowfb_sub));

   shadowHiresN = n;
   shadowHiresActive = 1;
   LOG_INF("[SHADOWFB] internal resolution %dx active (Stage 1: box replication)\n", n);
}
