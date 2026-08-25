/*
 * TEXREPLACE.C
 *
 * Texture replacement pipeline (issue #369, deliverable 2 of 2).
 * Design + authoring guide: docs/texture-dump.md ("Replacement
 * pipeline") -- the identity contract there is authoritative; this file
 * consumes it through the shared helpers in texdump.c (TexDumpDescribe /
 * TexDumpSerialize / TexDumpHashKey), never a copy of them.
 *
 * ARCHITECTURE: entirely host-side, presentation-riding.
 *
 *   1. At game load (or first enable), every
 *      <system_dir>/vj_texpacks/<CRC32>/<hash16>.png is decoded into a
 *      host hash->pixels map.  One-off file I/O; never at blit time.
 *   2. At blit launch (PreBlit, before engine dispatch) the SOURCE
 *      window is hashed exactly as dump mode hashes it.  A map hit
 *      with matching dimensions arms the launch.
 *   3. After dispatch (PostBlit) the DESTINATION window is walked; for
 *      every pixel whose RAM word now equals the source pixel captured
 *      pre-dispatch (the per-pixel straight-copy witness), the pack
 *      pixel's RGB888 is stored into the true-color shadow
 *      framebuffer, tagged with that RAM value.  The OP's existing
 *      shadow presentation path (op.c -> ShadowFBLineFromRAM -> the
 *      scanline renderer) then presents the pack art.
 *
 * The per-pixel witness makes wrong models harmless: transparent
 * (BCOMPEN/DCOMPEN) pixels, shaded/Gouraud outputs, non-rectangular
 * step patterns -- anything whose destination value is not the source
 * value -- simply never stores, and the stock pixel presents.
 *
 * The emulated machine cannot observe any of this: no RAM writes, no
 * register writes, no bus-model time, ZERO savestate fields.  With the
 * option off (or no pack present) the launch site pays one predictable
 * branch and the output is bit-identical to stock.
 *
 * TIER 3 (>1x pack art, issue #369): when the internal-resolution
 * shadow surface is up at Nx, step 3 additionally stores an N*N block
 * of pack RGB per destination word into a parallel replacement plane
 * on that surface (ShadowHiresStoreReplBlock).  Pack art may then be
 * authored at exactly N times the dumped dimensions and is presented
 * per SUBPIXEL by the Nx scanline renderer.  1x art is stored there
 * too, replicated -- without that the Nx surface would MASK 1x packs,
 * because its line entries win wherever they hit.
 *
 * RGB16-DIRECT SCANOUT (issue #528): presentation is no longer
 * CRY-only.  Both 16bpp scanout modes present, at 1x and Nx together
 * -- the store side never cared (it captures the raw destination word;
 * TOM's video mode only decides how that word is INTERPRETED at
 * scanout), so this was a missing consumer plus one tag bit,
 * SHADOWFB_TAG_REPL, telling pack art apart from a true-color CRY
 * reconstruction.  A CRY reconstruction must NOT present on an RGB16
 * scanout; pack art is absolute RGB888 and is correct on either.  See
 * TomLinePackRGB in tom.c and the SHADOWFB_TAG_REPL comment in
 * shadowfb.h.
 *
 * Known limits (documented in docs/texture-dump.md): presentation
 * requires the 16bpp OP path; the shadow tag is value-checked, so a
 * later NON-blit write of the same 16-bit value to a replaced address
 * keeps presenting pack RGB until the next store to that word;
 * indexed (<=8bpp) sources are a future tier.
 */

#include "texreplace.h"

#include <compat/msvc.h>  /* snprintf shim for MSVC < 2015 (buildbot msvc05/10) */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <boolean.h>
#include <streams/file_stream.h>
#include <file/file_path.h>
#include <vfs/vfs_implementation.h>

#include "texdump.h"    /* shared identity-contract helpers */
#include "shadowfb.h"   /* ShadowFBStoreRGB / shadowFBActive */
#include "titledb.h"    /* TitleDBContentCRC */
#include "log.h"

/* libretro VFS (libretro-common/streams/file_stream_transforms.c) --
 * same declaration pattern texdump.c uses. */
RFILE *rfopen(const char *path, const char *mode);
int rfclose(RFILE *stream);
int64_t rftell(RFILE *stream);
int64_t rfseek(RFILE *stream, int64_t offset, int origin);
int64_t rfread(void *buffer, size_t elem_size, size_t elem_count,
               RFILE *stream);

/* miniz inflate, compiled into deps/libchdr/unity.o (the decompression
 * APIs are never disabled there).  Local prototypes rather than
 * miniz.h, same as texdump.c's encoder declarations. */
void *tinfl_decompress_mem_to_heap(const void *pSrc_buf, size_t src_buf_len,
                                   size_t *pOut_len, int flags);
void mz_free(void *p);
#define TR_TINFL_PARSE_ZLIB_HEADER 1

/* ---- pack map ------------------------------------------------------- */

/* PNG color types we accept (8-bit depth, non-interlaced only). */
#define TR_CT_GRAY       0
#define TR_CT_RGB        2
#define TR_CT_PAL        3
#define TR_CT_GRAY_ALPHA 4
#define TR_CT_RGBA       6

/* conv[] pixel: low 24 bits RGB888; bit 31 = "skip" (author alpha).  */
#define TR_CONV_SKIP 0x80000000u

typedef struct
{
   uint64_t key;         /* identity-contract hash (the filename)      */
   uint16_t w, h;        /* PNG dimensions                             */
   uint8_t  ctype;       /* PNG color type (TR_CT_*)                   */
   uint8_t  channels;    /* bytes per pixel in raw[]                   */
   uint8_t *raw;         /* unfiltered scanlines, w*h*channels bytes   */
   uint8_t *pal;         /* 768-byte PLTE copy (TR_CT_PAL only)        */
   uint32_t *conv;       /* lazy RGB888(+skip) cache, w*h entries      */
} tr_entry;

#define TR_TAB_BITS  14
#define TR_TAB_SIZE  (1u << TR_TAB_BITS)
#define TR_TAB_MASK  (TR_TAB_SIZE - 1u)
#define TR_TAB_MAXLOAD ((TR_TAB_SIZE / 4u) * 3u)
#define TR_KEY_ZERO_REMAP 0xD6E8FEB86659FD93ULL

/* Per-PNG sanity caps: tiles are dump-window sized. */
#define TR_MAX_PNG_BYTES  (8u << 20)
#define TR_MAX_PIXELS     (1u << 20)

/* ---- state (ALL host-transient; reset in TexReplaceShutdown) -------- */

int texReplaceEnabled = 0;

static int        tr_want       = 0;   /* option state                  */
static int        tr_loaded     = 0;   /* pack load attempted           */
static int        tr_pack_avail = 0;   /* pack dir exists for content   */
static char       tr_base[1024];       /* system dir ("" = unset)       */
static tr_entry **tr_tab        = NULL;
static uint32_t   tr_count      = 0;
static uint8_t   *tr_scratch    = NULL; /* serialized source bytes      */
static int        tr_alloc_failed = 0;
static int        tr_dim_warned = 0;

/* Armed launch (PreBlit -> PostBlit).  Both windows are described
 * PRE-dispatch: the engines write back the pointer registers as they
 * run (UPDA1/UPDA2), so a post-dispatch register read would describe
 * the END of the blit, not its origin. */
static TexDumpDesc tr_desc;
static TexDumpDesc tr_dst;
static uint32_t    tr_cmd = 0;
static uint32_t    tr_len = 0;
static tr_entry   *tr_hit = NULL;
/* Pack-art scale of the armed entry: 1 (dumped dimensions) or N (the
 * active internal-resolution factor).  Tier 3, issue #369. */
static uint32_t    tr_scale = 1;

/* Stats for the unload summary line. */
static uint32_t tr_stat_hits       = 0; /* armed launches               */
static uint32_t tr_stat_stores     = 0; /* pixels stored to the shadow  */
static uint32_t tr_stat_skip_dims  = 0; /* pack PNG dims != window dims */
static uint32_t tr_stat_skip_tier  = 0; /* non-16bpp window hits        */
static uint32_t tr_stat_hi_stores  = 0; /* Nx blocks stored (tier 3)    */
static uint32_t tr_stat_hi_hits    = 0; /* armed launches at Nx art     */

/* ---- pack map ------------------------------------------------------- */

static tr_entry *tr_find(uint64_t key)
{
   uint32_t idx;
   if (!tr_tab)
      return NULL;
   if (key == 0)
      key = TR_KEY_ZERO_REMAP;
   idx = (uint32_t)key & TR_TAB_MASK;
   for (;;)
   {
      tr_entry *e = tr_tab[idx];
      if (!e)
         return NULL;
      if (e->key == key)
         return e;
      idx = (idx + 1) & TR_TAB_MASK;
   }
}

static int tr_insert(tr_entry *e)
{
   uint32_t idx;
   if (tr_count >= TR_TAB_MAXLOAD)
      return 0;
   if (e->key == 0)
      e->key = TR_KEY_ZERO_REMAP;
   idx = (uint32_t)e->key & TR_TAB_MASK;
   for (;;)
   {
      if (!tr_tab[idx])
      {
         tr_tab[idx] = e;
         tr_count++;
         return 1;
      }
      if (tr_tab[idx]->key == e->key)
         return 0;                     /* duplicate hash: keep first */
      idx = (idx + 1) & TR_TAB_MASK;
   }
}

static void tr_free_entry(tr_entry *e)
{
   if (!e)
      return;
   free(e->raw);
   free(e->pal);
   free(e->conv);
   free(e);
}

/* ---- PNG decode (chunk walk + miniz inflate + unfilter) ------------- */

static uint32_t tr_be32(const uint8_t *p)
{
   return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16)
        | ((uint32_t)p[2] << 8) | (uint32_t)p[3];
}

static int tr_paeth(int a, int b, int c)
{
   int p  = a + b - c;
   int pa = p > a ? p - a : a - p;
   int pb = p > b ? p - b : b - p;
   int pc = p > c ? p - c : c - p;
   if (pa <= pb && pa <= pc)
      return a;
   if (pb <= pc)
      return b;
   return c;
}

/* Decode an 8-bit-depth, non-interlaced PNG into a fresh tr_entry
 * (without key).  Returns the entry or NULL (with a reason for the
 * one-line load log). */
static tr_entry *tr_png_decode(const uint8_t *buf, size_t n,
                               const char **why)
{
   static const uint8_t sig[8] = { 137, 80, 78, 71, 13, 10, 26, 10 };
   static const uint8_t chan_for_ct[7] = { 1, 0, 3, 1, 2, 0, 4 };
   uint32_t w = 0, h = 0;
   uint8_t ctype = 0xFF;
   uint8_t *pal = NULL;
   uint8_t *idat = NULL;
   size_t idat_len = 0;
   uint8_t *inflated = NULL;
   size_t inflated_len = 0;
   tr_entry *e = NULL;
   size_t pos;
   uint32_t channels, stride;
   uint32_t r, i;

   *why = "corrupt";
   if (n < 8 + 25 || memcmp(buf, sig, 8) != 0)
      return NULL;

   /* Chunk walk: IHDR first, concatenate IDAT, stop at IEND.  Chunk
    * CRCs are not verified -- a corrupt stream fails the inflate/size
    * checks below instead. */
   pos = 8;
   while (pos + 12 <= n)
   {
      uint32_t clen = tr_be32(buf + pos);
      const uint8_t *tag  = buf + pos + 4;
      const uint8_t *body = buf + pos + 8;
      if (clen > n || pos + 12 + clen > n)
         goto fail;
      if (pos == 8)
      {
         if (memcmp(tag, "IHDR", 4) != 0 || clen != 13)
            goto fail;
         w     = tr_be32(body);
         h     = tr_be32(body + 4);
         ctype = body[9];
         if (body[8] != 8)
         {
            *why = "bit depth != 8";
            goto fail;
         }
         if (body[12] != 0)
         {
            *why = "interlaced";
            goto fail;
         }
         if (ctype > 6 || chan_for_ct[ctype] == 0)
         {
            *why = "unsupported color type";
            goto fail;
         }
         if (w == 0 || h == 0 || w > 0xFFFF || h > 0xFFFF
             || (uint64_t)w * h > TR_MAX_PIXELS)
         {
            *why = "bad dimensions";
            goto fail;
         }
      }
      else if (!memcmp(tag, "PLTE", 4))
      {
         if (clen > 768 || (clen % 3) != 0)
            goto fail;
         if (!pal)
         {
            pal = (uint8_t *)calloc(1, 768);
            if (!pal)
               goto fail;
            memcpy(pal, body, clen);
         }
      }
      else if (!memcmp(tag, "IDAT", 4))
      {
         uint8_t *grown = (uint8_t *)realloc(idat, idat_len + clen);
         if (!grown)
            goto fail;
         idat = grown;
         memcpy(idat + idat_len, body, clen);
         idat_len += clen;
      }
      else if (!memcmp(tag, "IEND", 4))
         break;
      pos += 12 + (size_t)clen;
   }
   if (ctype == 0xFF || !idat)
      goto fail;
   if (ctype == TR_CT_PAL && !pal)
      goto fail;

   channels = chan_for_ct[ctype];
   stride   = w * channels;

   inflated = (uint8_t *)tinfl_decompress_mem_to_heap(idat, idat_len,
                  &inflated_len, TR_TINFL_PARSE_ZLIB_HEADER);
   if (!inflated || inflated_len != (size_t)h * (stride + 1))
      goto fail;

   e = (tr_entry *)calloc(1, sizeof(tr_entry));
   if (!e)
      goto fail;
   e->raw = (uint8_t *)malloc((size_t)h * stride);
   if (!e->raw)
      goto fail;
   e->w = (uint16_t)w;
   e->h = (uint16_t)h;
   e->ctype = ctype;
   e->channels = (uint8_t)channels;
   e->pal = pal;
   pal = NULL;                        /* owned by e now */

   /* Unfilter (PNG filters 0-4; bpp for filtering = channels). */
   for (r = 0; r < h; r++)
   {
      const uint8_t *src = inflated + (size_t)r * (stride + 1);
      uint8_t *dst  = e->raw + (size_t)r * stride;
      const uint8_t *up = r ? dst - stride : NULL;
      uint8_t filter = src[0];
      src++;
      switch (filter)
      {
         case 0:
            memcpy(dst, src, stride);
            break;
         case 1:
            for (i = 0; i < stride; i++)
               dst[i] = (uint8_t)(src[i]
                      + (i >= channels ? dst[i - channels] : 0));
            break;
         case 2:
            for (i = 0; i < stride; i++)
               dst[i] = (uint8_t)(src[i] + (up ? up[i] : 0));
            break;
         case 3:
            for (i = 0; i < stride; i++)
            {
               unsigned a = i >= channels ? dst[i - channels] : 0;
               unsigned b = up ? up[i] : 0;
               dst[i] = (uint8_t)(src[i] + ((a + b) >> 1));
            }
            break;
         case 4:
            for (i = 0; i < stride; i++)
            {
               int a = i >= channels ? dst[i - channels] : 0;
               int b = up ? up[i] : 0;
               int c = (up && i >= channels) ? up[i - channels] : 0;
               dst[i] = (uint8_t)(src[i] + tr_paeth(a, b, c));
            }
            break;
         default:
            goto fail;
      }
   }

   mz_free(inflated);
   free(idat);
   *why = NULL;
   return e;

fail:
   if (inflated)
      mz_free(inflated);
   free(idat);
   free(pal);
   tr_free_entry(e);
   return NULL;
}

/* ---- pack loading --------------------------------------------------- */

static int tr_hex16(const char *s, uint64_t *out)
{
   uint64_t v = 0;
   int i;
   for (i = 0; i < 16; i++)
   {
      char c = s[i];
      v <<= 4;
      if (c >= '0' && c <= '9')
         v |= (uint64_t)(c - '0');
      else if (c >= 'a' && c <= 'f')
         v |= (uint64_t)(c - 'a' + 10);
      else if (c >= 'A' && c <= 'F')
         v |= (uint64_t)(c - 'A' + 10);
      else
         return 0;
   }
   *out = v;
   return 1;
}

static void tr_pack_dir(char *out, size_t out_size)
{
   snprintf(out, out_size, "%s/vj_texpacks/%08x", tr_base,
            (unsigned)TitleDBContentCRC());
}

/* Read one pack PNG and insert it.  Returns 1 on success. */
static int tr_load_file(const char *dir, const char *name, uint64_t key)
{
   char path[1400];
   RFILE *f;
   int64_t sz;
   uint8_t *buf;
   tr_entry *e;
   const char *why = NULL;

   snprintf(path, sizeof(path), "%s/%s", dir, name);
   f = rfopen(path, "rb");
   if (!f)
      return 0;
   rfseek(f, 0, SEEK_END);
   sz = rftell(f);
   rfseek(f, 0, SEEK_SET);
   if (sz <= 0 || sz > (int64_t)TR_MAX_PNG_BYTES)
   {
      rfclose(f);
      LOG_INF("[TEXREPLACE] %s: skipped (bad size %lld)\n", name,
              (long long)sz);
      return 0;
   }
   buf = (uint8_t *)malloc((size_t)sz);
   if (!buf)
   {
      rfclose(f);
      return 0;
   }
   if (rfread(buf, 1, (size_t)sz, f) != sz)
   {
      free(buf);
      rfclose(f);
      return 0;
   }
   rfclose(f);

   e = tr_png_decode(buf, (size_t)sz, &why);
   free(buf);
   if (!e)
   {
      LOG_INF("[TEXREPLACE] %s: skipped (%s)\n", name, why);
      return 0;
   }
   e->key = key;
   if (!tr_insert(e))
   {
      tr_free_entry(e);
      return 0;
   }
   return 1;
}

static void tr_load_pack(void)
{
   char dir[1200];
   libretro_vfs_implementation_dir *d;
   unsigned scanned = 0;

   tr_loaded = 1;
   if (tr_base[0] == '\0')
      return;

   tr_pack_dir(dir, sizeof(dir));
   d = retro_vfs_opendir_impl(dir, false);
   if (!d)
      return;

   if (!tr_tab)
      tr_tab = (tr_entry **)calloc(TR_TAB_SIZE, sizeof(tr_entry *));
   if (!tr_scratch)
      tr_scratch = (uint8_t *)malloc(TEXDUMP_SCRATCH_BYTES);
   if (!tr_tab || !tr_scratch)
   {
      free(tr_tab);
      free(tr_scratch);
      tr_tab = NULL;
      tr_scratch = NULL;
      tr_alloc_failed = 1;
      retro_vfs_closedir_impl(d);
      LOG_INF("[TEXREPLACE] allocation failed -- replacement unavailable\n");
      return;
   }

   while (retro_vfs_readdir_impl(d))
   {
      const char *name = retro_vfs_dirent_get_name_impl(d);
      uint64_t key;
      if (!name)
         continue;
      /* Exactly <16 hex>.png -- anything else in the directory is the
       * author's business (manifest copies, notes, PSDs). */
      if (strlen(name) != 20 || strcmp(name + 16, ".png") != 0)
         continue;
      if (!tr_hex16(name, &key))
         continue;
      scanned++;
      tr_load_file(dir, name, key);
   }
   retro_vfs_closedir_impl(d);

   LOG_INF("[TEXREPLACE] pack %s: %u/%u PNGs loaded\n", dir,
           (unsigned)tr_count, scanned);
}

/* ---- conversion (lazy, once per entry) ------------------------------ */

/* Build the RGB888(+skip) cache from the decoded PNG.  Pack art is
 * presented in true color through the shadow framebuffer, so there is
 * deliberately NO RGB->CRY quantization anywhere in this pipeline. */
static int tr_convert(tr_entry *e)
{
   uint32_t npix = (uint32_t)e->w * e->h;
   uint32_t i;

   if (e->conv)
      return 1;
   e->conv = (uint32_t *)malloc((size_t)npix * 4);
   if (!e->conv)
      return 0;
   for (i = 0; i < npix; i++)
   {
      const uint8_t *p = e->raw + (size_t)i * e->channels;
      uint32_t v;
      switch (e->ctype)
      {
         case TR_CT_GRAY:
            v = ((uint32_t)p[0] << 16) | ((uint32_t)p[0] << 8) | p[0];
            break;
         case TR_CT_GRAY_ALPHA:
            v = ((uint32_t)p[0] << 16) | ((uint32_t)p[0] << 8) | p[0];
            if (p[1] < 0x80)
               v |= TR_CONV_SKIP;
            break;
         case TR_CT_PAL:
            v = ((uint32_t)e->pal[p[0] * 3] << 16)
              | ((uint32_t)e->pal[p[0] * 3 + 1] << 8)
              |  (uint32_t)e->pal[p[0] * 3 + 2];
            break;
         case TR_CT_RGBA:
            v = ((uint32_t)p[0] << 16) | ((uint32_t)p[1] << 8) | p[2];
            if (p[3] < 0x80)
               v |= TR_CONV_SKIP;
            break;
         default:  /* TR_CT_RGB */
            v = ((uint32_t)p[0] << 16) | ((uint32_t)p[1] << 8) | p[2];
            break;
      }
      e->conv[i] = v;
   }
   return 1;
}

/* ---- launch hooks --------------------------------------------------- */

int TexReplacePreBlit(void)
{
   uint64_t h;
   tr_entry *e;

   tr_hit = NULL;
   if (!tr_tab || !tr_scratch || tr_count == 0)
      return 0;
   if (TexDumpDescribe(&tr_desc, &tr_cmd) != 1)
      return 0;
   tr_len = TexDumpSerialize(&tr_desc, tr_scratch, TEXDUMP_SCRATCH_BYTES);
   if (tr_len == 0)
      return 0;
   h = TexDumpHashKey(&tr_desc, tr_scratch, tr_len);
   e = tr_find(h);
   if (!e)
      return 0;

   /* Tier 1: 16bpp source windows.  Indexed (<=8bpp) and 32bpp tiers
    * are future work (docs/texture-dump.md). */
   if (tr_desc.bpp != 16)
   {
      tr_stat_skip_tier++;
      return 0;
   }
   /* Dimension contract: EXACTLY the dumped size (1x art), or exactly
    * N times it in both axes when the hi-res shadow surface is up
    * (tier 3 -- the pack rides the Stage 2 Nx surface).  Nothing in
    * between: an arbitrary resize has no defined mapping onto stock
    * pixel words, and silently rescaling would make the pipeline's
    * per-pixel straight-copy witness meaningless. */
   tr_scale = 0;
   if ((uint32_t)e->w == tr_desc.inner && (uint32_t)e->h == tr_desc.outer)
      tr_scale = 1;
   else if (shadowHiresActive && shadowHiresN > 1
            && (uint32_t)e->w == tr_desc.inner * (uint32_t)shadowHiresN
            && (uint32_t)e->h == tr_desc.outer * (uint32_t)shadowHiresN)
      tr_scale = (uint32_t)shadowHiresN;
   if (tr_scale == 0)
   {
      tr_stat_skip_dims++;
      if (!tr_dim_warned)
      {
         tr_dim_warned = 1;
         if (shadowHiresActive && shadowHiresN > 1)
            LOG_INF("[TEXREPLACE] %016llx.png is %ux%u but the tile is "
                    "%ux%u -- pack art must be exactly that, or exactly "
                    "%ux%u (%dx, matching Internal Resolution) (further "
                    "mismatches not logged)\n",
                    (unsigned long long)e->key,
                    (unsigned)e->w, (unsigned)e->h,
                    (unsigned)tr_desc.inner, (unsigned)tr_desc.outer,
                    (unsigned)(tr_desc.inner * (uint32_t)shadowHiresN),
                    (unsigned)(tr_desc.outer * (uint32_t)shadowHiresN),
                    shadowHiresN);
         else
            LOG_INF("[TEXREPLACE] %016llx.png is %ux%u but the tile is "
                    "%ux%u -- pack art must keep the dumped size; larger "
                    "art needs Internal Resolution at that same factor "
                    "(further mismatches not logged)\n",
                    (unsigned long long)e->key,
                    (unsigned)e->w, (unsigned)e->h,
                    (unsigned)tr_desc.inner, (unsigned)tr_desc.outer);
      }
      return 0;
   }
   if (!tr_convert(e))
      return 0;

   /* Describe the DESTINATION window now, before the engines write the
    * pointer registers back.  The destination is the channel the source
    * is not: A1 normally, A2 under DSTA2 (JTRM B_CMD bit 11). */
   if (!TexDumpDescribeChannel(&tr_dst, (tr_cmd & 0x00000800) == 0,
                               tr_desc.inner, tr_desc.outer))
      return 0;
   if (tr_dst.psizefield != 4)     /* 16bpp destinations only (tier 1) */
   {
      tr_stat_skip_tier++;
      return 0;
   }

   tr_hit = e;
   tr_stat_hits++;
   if (tr_scale > 1)
      tr_stat_hi_hits++;
   return 1;
}

void TexReplacePostBlit(void)
{
   const TexDumpDesc *dd = &tr_dst;
   tr_entry *e = tr_hit;
   uint32_t r, c;
   /* Nx block scratch.  Fixed at the compile-time maximum -- N is a
    * runtime value and C89 has no VLAs. */
   uint32_t blk[SHADOWFB_HIRES_MAX_N * SHADOWFB_HIRES_MAX_N];
   uint32_t n;

   tr_hit = NULL;
   if (!e || !shadowFBActive)
      return;

   /* Tier 3: with the Nx surface up, EVERY hit also writes an Nx block
    * -- 1x art replicated N*N, Nx art at its own resolution.  Without
    * this, hi-res would mask 1x packs outright: at Nx the hi-res line
    * entry wins wherever it hits, and the 1x shadow only shows through
    * on a miss. */
   n = (shadowHiresActive && shadowHiresN > 1)
     ? (uint32_t)shadowHiresN : 1u;
   if (n > SHADOWFB_HIRES_MAX_N)
      n = SHADOWFB_HIRES_MAX_N;
   if (tr_scale > n)
      return;                      /* Nx art, surface went away */

   for (r = 0; r < dd->outer; r++)
   {
      /* Source pixel words sit in tr_scratch exactly 2 bytes per pixel,
       * row-major (the 16bpp serialization appends each pixel's two
       * covering bytes; the sub-byte dedupe only applies below 8bpp). */
      const uint8_t *srow = tr_scratch + (size_t)r * dd->inner * 2;
      const uint32_t *crow = e->conv + (size_t)r * tr_scale * e->w;
      for (c = 0; c < dd->inner; c++)
      {
         uint32_t daddr, conv, sy, sx, opaque;
         uint16_t src16, cur16;

         /* The 1x representative of this stock word is the pack's
          * top-left subpixel: predictable for authors and for the test
          * gate, and never a colour the author did not draw. */
         conv = crow[c * tr_scale];
         if (n == 1 && (conv & TR_CONV_SKIP))
            continue;              /* author alpha: keep the stock pixel */
         src16 = (uint16_t)(((uint16_t)srow[c * 2] << 8) | srow[c * 2 + 1]);
         daddr = (dd->base + TexDumpPixelByteOffset(dd, c, r)) & 0xFFFFFF;
         if (daddr >= 0x7FFFFE)
            continue;              /* shadow covers main RAM words only */
         cur16 = (uint16_t)(((uint16_t)TexDumpRead8(daddr) << 8)
                          | TexDumpRead8(daddr + 1));
         if (cur16 != src16)
            continue;              /* not a straight-copied pixel */

         if (n > 1)
         {
            opaque = 0;
            for (sy = 0; sy < n; sy++)
            {
               /* Nx art: this stock pixel's own N*N patch.  1x art
                * (tr_scale == 1): the single pixel, replicated. */
               const uint32_t *prow = e->conv
                  + (size_t)(r * tr_scale + (tr_scale > 1 ? sy : 0)) * e->w;
               for (sx = 0; sx < n; sx++)
               {
                  uint32_t p = prow[c * tr_scale + (tr_scale > 1 ? sx : 0)];
                  if (p & TR_CONV_SKIP)
                     blk[sy * n + sx] = 0;   /* fall through to stock */
                  else
                  {
                     blk[sy * n + sx] = SHADOWFB_HIRES_REPL_VALID
                                      | (p & 0x00FFFFFF);
                     opaque++;
                  }
               }
            }
            if (opaque)
            {
               ShadowHiresStoreReplBlock(daddr, cur16, blk);
               tr_stat_hi_stores++;
            }
            if (conv & TR_CONV_SKIP)
               continue;           /* no 1x fallback colour to record */
         }

         ShadowFBStoreRGB(daddr, cur16, conv);
         tr_stat_stores++;
      }
   }
}

/* ---- option layer --------------------------------------------------- */

static void tr_update_gate(void)
{
   texReplaceEnabled = (tr_want && tr_tab && tr_scratch && tr_count > 0)
                     ? 1 : 0;
}

void TexReplaceSetEnabled(int on)
{
   tr_want = on ? 1 : 0;
   if (tr_want && !tr_loaded && !tr_alloc_failed)
   {
      /* Re-probe on enable: the author may have created the pack
       * directory after the content was loaded. */
      if (!tr_pack_avail && tr_base[0] != '\0')
      {
         char dir[1200];
         tr_pack_dir(dir, sizeof(dir));
         tr_pack_avail = path_is_directory(dir) ? 1 : 0;
      }
      if (tr_pack_avail)
         tr_load_pack();
   }
   tr_update_gate();
}

void TexReplaceSetBasePath(const char *system_dir)
{
   if (system_dir && system_dir[0])
   {
      strncpy(tr_base, system_dir, sizeof(tr_base) - 1);
      tr_base[sizeof(tr_base) - 1] = '\0';
   }
   else
      tr_base[0] = '\0';
}

void TexReplaceContentLoaded(void)
{
   char dir[1200];

   tr_pack_avail = 0;
   if (tr_base[0] != '\0')
   {
      tr_pack_dir(dir, sizeof(dir));
      tr_pack_avail = path_is_directory(dir) ? 1 : 0;
   }
   if (tr_want && !tr_loaded && !tr_alloc_failed && tr_pack_avail)
      tr_load_pack();
   tr_update_gate();
}

int TexReplacePackAvailable(void)
{
   return tr_pack_avail;
}

int TexReplaceHasEntries(void)
{
   return tr_count > 0;
}

void TexReplaceShutdown(void)
{
   if (tr_stat_hits || tr_stat_skip_dims || tr_stat_skip_tier)
      LOG_INF("[TEXREPLACE] session summary: %u armed launches (%u at Nx "
              "art), %u pixels stored, %u Nx blocks stored, %u dimension "
              "mismatches, %u non-16bpp hits\n",
              (unsigned)tr_stat_hits, (unsigned)tr_stat_hi_hits,
              (unsigned)tr_stat_stores, (unsigned)tr_stat_hi_stores,
              (unsigned)tr_stat_skip_dims, (unsigned)tr_stat_skip_tier);
   if (tr_tab)
   {
      uint32_t i;
      for (i = 0; i < TR_TAB_SIZE; i++)
         tr_free_entry(tr_tab[i]);
      free(tr_tab);
      /* The pack's value-tagged RGB entries must not outlive the pack:
       * if the surface stays allocated across a title boundary, a
       * same-value coincidence in the next title would present orphaned
       * pack pixels.  (No-op when the surface is already down.) */
      if (tr_stat_stores)
         ShadowFBInvalidate();
      /* Same argument one surface up: an Nx replacement block outliving
       * its pack would present orphaned pack art on a same-value
       * coincidence in the next title. */
      if (tr_stat_hi_stores)
         ShadowHiresInvalidate();
   }
   free(tr_scratch);
   tr_tab     = NULL;
   tr_scratch = NULL;
   tr_count   = 0;
   texReplaceEnabled = 0;
   tr_want       = 0;
   tr_loaded     = 0;
   tr_pack_avail = 0;
   tr_alloc_failed = 0;
   tr_dim_warned = 0;
   tr_base[0]  = '\0';
   tr_cmd = 0;
   tr_len = 0;
   tr_hit = NULL;
   memset(&tr_desc, 0, sizeof(tr_desc));
   memset(&tr_dst, 0, sizeof(tr_dst));
   tr_scale = 1;
   tr_stat_hits      = 0;
   tr_stat_stores    = 0;
   tr_stat_skip_dims = 0;
   tr_stat_skip_tier = 0;
   tr_stat_hi_stores = 0;
   tr_stat_hi_hits   = 0;
}
