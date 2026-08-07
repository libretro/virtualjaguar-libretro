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
