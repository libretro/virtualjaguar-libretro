/*
 * TEXDUMP.C
 *
 * Texture dump mode (issue #369, deliverable 1 of 2).  Design spec:
 * docs/texture-dump.md -- the spec is authoritative for the identity
 * contract; this file implements it.
 *
 * Capture happens ONCE per blit at the shared launch site (beside
 * BlitMemoLaunch, before fast/accurate engine dispatch): when the blit
 * reads source data (SRCEN), the source rectangle is walked exactly as
 * the blit registers describe it (source channel base, flags, pixel
 * size, pitch/width, B_COUNT) directly out of emulated memory, hashed,
 * and deduped.  On first sight a preview PNG and a manifest row are
 * written.  No per-pixel instrumentation in any engine; the bus model
 * never sees a cycle of it; the hash is identical under both engines.
 *
 * THE IDENTITY CONTRACT (docs/texture-dump.md):
 *
 *   key = FNV-1a 64 over
 *   'VJTD' | version=1 | src_bpp | width(px,u16 LE) | height(rows,u16 LE)
 *         | source bytes, row-major, packed exactly as stored in memory
 *
 * The palette is deliberately NOT part of the key: replacement happens
 * at blit time and the Jaguar blitter never sees a palette -- CLUT
 * lookup is an Object Processor display-time concept.  Two CLUT
 * variants of one indexed tile are indistinguishable at the swap point,
 * so palette information is advisory manifest metadata, never identity.
 * Do not "fix" this.
 *
 * Source-channel selection follows the B_CMD decode verified against
 * docs/jtrm-blitter.md ("SRCEN (bit 0): read source data from A2, or A1
 * if DSTA2 (bit 11)"), NOT source-code comments.  The per-pixel address
 * formulas below are transliterations of the engines' PIXEL_OFFSET_*
 * macros (src/tom/blitter.c) with plain integer x/y in place of 16.16.
 *
 * Everything here is host-transient: zero savestate fields, and the
 * capture only READS emulated state (blitter_ram, RAM/ROM, TOM CLUT).
 */

#include "texdump.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <streams/file_stream.h>
#include <file/file_path.h>

#include "blitter_internal.h"   /* blitter_ram */
#include "vjag_memory.h"        /* jaguarMainRAM/ROM, jagMemSpace, GET16 */
#include "jaggd.h"              /* JGD_BANKING / JGDReadROM8 */
#include "tom.h"                /* tomRam8 (CLUT at +0x400, VMODE) */
#include "crc32.h"
#include "titledb.h"            /* TitleDBContentCRC / TitleDBTitleName */
#include "log.h"

/* libretro VFS (libretro-common/streams/file_stream_transforms.c) --
 * same declaration pattern libretro.c uses. */
RFILE *rfopen(const char *path, const char *mode);
int rfclose(RFILE *stream);
int64_t rftell(RFILE *stream);
int64_t rfwrite(void const *buffer, size_t elem_size, size_t elem_count,
                RFILE *stream);

/* miniz PNG encoder, compiled into deps/libchdr/unity.o with
 * -DMINIZ_DEFLATE_APIS (see the unity.o rule in the Makefile).  Local
 * prototypes rather than miniz.h: the header is C99 and drags in the
 * whole miniz API surface.  mz_uint == unsigned int, mz_bool == int. */
void *tdefl_write_image_to_png_file_in_memory_ex(const void *pImage,
      int w, int h, int num_chans, size_t *pLen_out, unsigned int level,
      int flip);
void mz_free(void *p);

/* CRY/RGB16 -> XRGB8888 tables (filled by TOMFillLookupTables in
 * src/tom/tom.c) -- reused for the 16bpp/CLUT previews so there is no
 * second CRY implementation. */
extern uint32_t CRY16ToRGB32[0x10000];
extern uint32_t RGB16ToRGB32[0x10000];

/* ---- constants ------------------------------------------------------ */

#define TD_REG(A) (((uint32_t)blitter_ram[(A)] << 24)      \
                 | ((uint32_t)blitter_ram[(A) + 1] << 16)  \
                 | ((uint32_t)blitter_ram[(A) + 2] << 8)   \
                 |  (uint32_t)blitter_ram[(A) + 3])

#define TD_A1_BASE   0x00
#define TD_A1_FLAGS  0x04
#define TD_A1_PIXEL  0x0C
#define TD_A2_BASE   0x24
#define TD_A2_FLAGS  0x28
#define TD_A2_PIXEL  0x30
#define TD_COMMAND   0x38
#define TD_COUNT     0x3C

#define TD_FNV_OFFSET 0xCBF29CE484222325ULL
#define TD_FNV_PRIME  0x00000100000001B3ULL

/* Local aliases for the shared window-size constants (texdump.h). */
#define TD_MAX_WINDOW_BYTES TEXDUMP_MAX_WINDOW_BYTES
#define TD_SCRATCH_BYTES    TEXDUMP_SCRATCH_BYTES

/* Dedupe set: open-addressed uint64 table, fixed 2^17 entries (1 MB
 * host RAM).  0 is the empty sentinel; a real hash of 0 is remapped to
 * a fixed non-zero constant so it stays representable.  Insertion stops
 * (with a one-time warning) at 3/4 load so probe chains stay bounded. */
#define TD_SET_BITS    17
#define TD_SET_SIZE    (1u << TD_SET_BITS)
#define TD_SET_MASK    (TD_SET_SIZE - 1u)
#define TD_SET_MAXLOAD ((TD_SET_SIZE / 4u) * 3u)
#define TD_HASH_ZERO_REMAP 0xD6E8FEB86659FD93ULL

/* ---- state (ALL host-transient; reset in TexDumpShutdown) ----------- */

int texDumpEnabled = 0;

static int       td_mode16   = TEXDUMP_16BPP_CRY;
static uint64_t *td_set      = NULL;   /* dedupe table                  */
static uint32_t  td_entries  = 0;      /* live entries in td_set        */
static uint8_t  *td_scratch  = NULL;   /* serialized source bytes       */
static int       td_overflow_warned = 0;
static int       td_alloc_failed    = 0;
static int       td_png_warned      = 0;
static int       td_write_warned    = 0;
static uint32_t  td_frame    = 0;      /* manifest frame number         */
static char      td_base[1024];        /* system dir ("" = unset)       */
static char      td_dir[1200];         /* <base>/vj_texdump/<crc8>      */
static int       td_dir_ready = 0;
static RFILE    *td_manifest  = NULL;
/* Stats for the unload summary line. */
static uint32_t  td_stat_unique    = 0; /* first-sight tiles dumped     */
static uint32_t  td_stat_sightings = 0; /* extra-palette sighting rows  */
static uint32_t  td_stat_captures  = 0; /* SRCEN blits hashed           */
static uint32_t  td_stat_skipped   = 0; /* windows skipped (guards)     */

/* ---- dedupe set ----------------------------------------------------- */

/* Insert h (non-zero) into the set.  Returns 1 when h was NEW, 0 when
 * it was already present or the set is full. */
static int td_set_insert(uint64_t h)
{
   uint32_t idx;

   if (h == 0)
      h = TD_HASH_ZERO_REMAP;
   idx = (uint32_t)h & TD_SET_MASK;
   for (;;)
   {
      if (td_set[idx] == 0)
      {
         if (td_entries >= TD_SET_MAXLOAD)
         {
            if (!td_overflow_warned)
            {
               td_overflow_warned = 1;
               LOG_INF("[TEXDUMP] dedupe set full (%u entries); no further "
                       "new tiles will be recorded this session\n",
                       (unsigned)td_entries);
            }
            return 0;
         }
         td_set[idx] = h;
         td_entries++;
         return 1;
      }
      if (td_set[idx] == h)
         return 0;
      idx = (idx + 1) & TD_SET_MASK;
   }
}

/* ---- side-effect-free emulated-memory reads ------------------------- */

/* Populated, side-effect-free address space for texture sources: the
 * main-RAM mirror region, cartridge ROM (GameDrive-banked when active)
 * and the boot ROM window.  BUTCH/TOM/JERRY and unmapped space are
 * refused -- reads there can carry side effects, so a window touching
 * them is skipped entirely (same rule as blitter.c's
 * shadow_hires_read_src16_a1).  Shared with texreplace.c (texdump.h). */
int TexDumpAddrOK(uint32_t a)
{
   if (a < 0xDFFF00)
      return 1;
   if (a >= 0xE00000 && a < 0xE40000)
      return 1;
   return 0;
}

uint8_t TexDumpRead8(uint32_t a)
{
   if (a < 0x800000)
      return jaguarMainRAM[a & 0x1FFFFF];
   if (a < 0xDFFF00)
   {
      if (JGD_BANKING())
         return JGDReadROM8(a - 0x800000);
      return jaguarMainROM[a - 0x800000];
   }
   return jagMemSpace[a];   /* boot ROM window (TexDumpAddrOK guarded) */
}

/* ---- source-window walk --------------------------------------------- */

/* Byte offset of pixel (x, y) inside the channel's window, transliterated
 * from blitter.c's PIXEL_OFFSET_{1,2,4,8,16,32} macros with integer x/y
 * (the macros take 16.16 fixed point).  `pitch` is the phrase-gap value
 * ({0,1,3,2}[FLAGS&3], exactly as the engines decode it); the returned
 * offset is in BYTES for bpp <= 8 and in PIXELS for 16/32bpp (the caller
 * scales), matching the macros. */
static uint32_t td_pixel_offset(uint32_t x, uint32_t y, uint32_t width,
                                uint32_t pitch, uint32_t psizefield)
{
   switch (psizefield)
   {
      case 0:  /* 1bpp */
         return ((y * width / 8) + ((x >> 3) & ~7u)) * (1 + pitch)
              + ((x >> 3) & 7u);
      case 1:  /* 2bpp */
         return ((y * width / 4) + ((x >> 2) & ~7u)) * (1 + pitch)
              + ((x >> 2) & 7u);
      case 2:  /* 4bpp */
         return ((y * (width / 2)) + ((x >> 1) & ~7u)) * (1 + pitch)
              + ((x >> 1) & 7u);
      case 3:  /* 8bpp */
         return ((y * width) + (x & ~7u)) * (1 + pitch) + (x & 7u);
      case 4:  /* 16bpp (pixel units; byte offset = <<1) */
         return ((y * width) + (x & ~3u)) * (1 + pitch) + (x & 3u);
      default: /* 5 = 32bpp (pixel units; byte offset = <<2) */
         return ((y * width) + (x & ~1u)) * (1 + pitch) + (x & 1u);
   }
}

/* Byte offset of pixel (c, r) inside the described window, scaled to
 * bytes for every bpp (texdump.h; used by the replacement pipeline's
 * destination-window walk). */
uint32_t TexDumpPixelByteOffset(const TexDumpDesc *d, uint32_t c,
                                uint32_t r)
{
   uint32_t px  = (d->x0 + c) & 0xFFFF;
   uint32_t py  = (d->y0 + r) & 0xFFFF;
   uint32_t off = td_pixel_offset(px, py, d->width, d->pitch,
                                  d->psizefield);
   if (d->psizefield == 4)
      off <<= 1;
   else if (d->psizefield == 5)
      off <<= 2;
   return off;
}

/* Serialize the described window into buf, row-major, packed exactly
 * as stored (each covering byte appended once).  Returns the byte
 * count, or 0 when any byte falls outside populated address space or
 * the serialization overruns cap.  Shared with texreplace.c: this IS
 * the identity contract's byte stream -- do not change the walk. */
uint32_t TexDumpSerialize(const TexDumpDesc *d, uint8_t *buf, uint32_t cap)
{
   uint32_t len = 0;
   uint32_t r, c, k;
   uint32_t px, py, off, a, prev;

   for (r = 0; r < d->outer; r++)
   {
      py   = (d->y0 + r) & 0xFFFF;
      prev = 0xFFFFFFFFu;
      for (c = 0; c < d->inner; c++)
      {
         px  = (d->x0 + c) & 0xFFFF;
         off = td_pixel_offset(px, py, d->width, d->pitch, d->psizefield);
         if (d->psizefield == 4)
            off <<= 1;
         else if (d->psizefield == 5)
            off <<= 2;
         if (off == prev && d->bpp < 8)
            continue;               /* same stored byte as previous pixel */
         prev = off;
         k = (d->bpp <= 8) ? 1 : (d->bpp >> 3);
         if (len + k > cap)
            return 0;
         for (; k > 0; k--)
         {
            a = (d->base + off) & 0xFFFFFF;
            if (!TexDumpAddrOK(a))
               return 0;
            buf[len++] = TexDumpRead8(a);
            off++;
         }
      }
   }
   return len;
}

uint64_t TexDumpHashKey(const TexDumpDesc *d, const uint8_t *bytes,
                        uint32_t len)
{
   uint64_t h = TD_FNV_OFFSET;
   uint8_t hdr[10];
   uint32_t i;

   hdr[0] = 'V';
   hdr[1] = 'J';
   hdr[2] = 'T';
   hdr[3] = 'D';
   hdr[4] = 1;                              /* contract version        */
   hdr[5] = (uint8_t)d->bpp;
   hdr[6] = (uint8_t)(d->inner & 0xFF);     /* width  u16 LE           */
   hdr[7] = (uint8_t)((d->inner >> 8) & 0xFF);
   hdr[8] = (uint8_t)(d->outer & 0xFF);     /* height u16 LE           */
   hdr[9] = (uint8_t)((d->outer >> 8) & 0xFF);

   for (i = 0; i < 10; i++)
   {
      h ^= hdr[i];
      h *= TD_FNV_PRIME;
   }
   for (i = 0; i < len; i++)
   {
      h ^= bytes[i];
      h *= TD_FNV_PRIME;
   }
   return h;
}

/* Decode ONE address channel into *d with the given counts (texdump.h;
 * the replacement pipeline models a launch's destination window with
 * this).  Returns 0 on a garbage pixel-size encoding. */
int TexDumpDescribeChannel(TexDumpDesc *d, int use_a1,
                           uint32_t inner, uint32_t outer)
{
   uint32_t m, e;

   if (use_a1)
   {
      d->base  = TD_REG(TD_A1_BASE) & 0xFFFFFFF8;
      d->flags = TD_REG(TD_A1_FLAGS);
      d->x0    = TD_REG(TD_A1_PIXEL) & 0xFFFF;
      d->y0    = (TD_REG(TD_A1_PIXEL) >> 16) & 0xFFFF;
   }
   else
   {
      d->base  = TD_REG(TD_A2_BASE) & 0xFFFFFFF8;
      d->flags = TD_REG(TD_A2_FLAGS);
      d->x0    = TD_REG(TD_A2_PIXEL) & 0xFFFF;
      d->y0    = (TD_REG(TD_A2_PIXEL) >> 16) & 0xFFFF;
   }

   d->psizefield = (d->flags >> 3) & 0x07;
   if (d->psizefield > 5)            /* garbage pixel-size encoding */
      return 0;
   d->bpp = 1u << d->psizefield;
   {
      static const uint32_t pitch_lut[4] = { 0, 1, 3, 2 };
      d->pitch = pitch_lut[d->flags & 0x03];
   }
   m = (d->flags >> 9) & 0x03;
   e = (d->flags >> 11) & 0x0F;
   d->width = ((0x04 | m) << e) >> 2;

   d->inner = inner;
   d->outer = outer;
   d->src_addr = (d->base + TexDumpPixelByteOffset(d, 0, 0)) & 0xFFFFFF;
   return 1;
}

/* Decode the current launch's SOURCE window (texdump.h).  Returns 1
 * when this is a sane SRCEN blit worth hashing, 0 for no-source /
 * garbage-register launches, and -1 for a sane-looking window larger
 * than the 1 MB guard (counted as a skip by dump mode). */
int TexDumpDescribe(TexDumpDesc *d, uint32_t *cmd_out)
{
   uint32_t cmd, count;
   uint64_t bytes_est;
   int src_is_a1;

   cmd = TD_REG(TD_COMMAND);
   if (cmd_out)
      *cmd_out = cmd;
   if (!(cmd & 0x00000001))         /* SRCEN: no source read, no tile */
      return 0;

   /* JTRM B_CMD bit 0: source data comes from A2, or from A1 when
    * DSTA2 (bit 11) makes A2 the destination. */
   src_is_a1 = (cmd & 0x00000800) != 0;

   count = TD_REG(TD_COUNT);
   if ((count & 0xFFFF) == 0 || (count >> 16) == 0)
      return 0;
   if (!TexDumpDescribeChannel(d, src_is_a1, count & 0xFFFF,
                               (count >> 16) & 0xFFFF))
      return 0;

   /* Garbage-register guard: > 1 MB described windows are skipped. */
   bytes_est = ((uint64_t)d->inner * d->outer * d->bpp + 7) / 8;
   if (bytes_est > TD_MAX_WINDOW_BYTES)
      return -1;
   return 1;
}

/* ---- output plumbing ------------------------------------------------ */

/* Lazily create <base>/vj_texdump/<crc8>/ and open manifest.tsv for
 * append (header written when the file is new).  Returns 1 when the
 * manifest is ready. */
static int td_ensure_output(void)
{
   char path[1400];
   uint32_t crc;
   const char *title;

   if (td_manifest)
      return 1;
   if (td_dir_ready < 0 || td_base[0] == '\0')
      return 0;

   crc = TitleDBContentCRC();
   snprintf(td_dir, sizeof(td_dir), "%s/vj_texdump/%08x", td_base,
            (unsigned)crc);
   if (!path_mkdir(td_dir))
   {
      if (!td_write_warned)
      {
         td_write_warned = 1;
         LOG_INF("[TEXDUMP] cannot create %s -- dumping disabled\n", td_dir);
      }
      td_dir_ready = -1;
      return 0;
   }
   td_dir_ready = 1;

   snprintf(path, sizeof(path), "%s/manifest.tsv", td_dir);
   td_manifest = rfopen(path, "ab");
   if (!td_manifest)
      td_manifest = rfopen(path, "wb");
   if (!td_manifest)
   {
      if (!td_write_warned)
      {
         td_write_warned = 1;
         LOG_INF("[TEXDUMP] cannot open %s -- dumping disabled\n", path);
      }
      td_dir_ready = -1;
      return 0;
   }
   if (rftell(td_manifest) == 0)
   {
      char hdr[256];
      int n;
      title = TitleDBTitleName();
      n = snprintf(hdr, sizeof(hdr), "# texdump v1\tcrc=%08x\ttitle=%s\n",
                   (unsigned)crc, title ? title : "unknown");
      if (n > 0)
         rfwrite(hdr, 1, (size_t)n, td_manifest);
   }
   return 1;
}

static void td_manifest_write(const char *line, int len)
{
   if (len > 0 && td_manifest)
      rfwrite(line, 1, (size_t)len, td_manifest);
}

/* Encode an RGB8 buffer as PNG and write it via the VFS. */
static void td_write_png(const char *path, const uint8_t *rgb,
                         uint32_t w, uint32_t h)
{
   size_t png_len = 0;
   void *png = tdefl_write_image_to_png_file_in_memory_ex(rgb, (int)w,
         (int)h, 3, &png_len, 6, 0);

   if (!png)
   {
      if (!td_png_warned)
      {
         td_png_warned = 1;
         LOG_INF("[TEXDUMP] PNG encode failed (%ux%u) -- manifest rows "
                 "continue without previews for such tiles\n",
                 (unsigned)w, (unsigned)h);
      }
      return;
   }
   {
      RFILE *f = rfopen(path, "wb");
      if (f)
      {
         rfwrite(png, 1, png_len, f);
         rfclose(f);
      }
      else if (!td_write_warned)
      {
         td_write_warned = 1;
         LOG_INF("[TEXDUMP] cannot write %s\n", path);
      }
   }
   mz_free(png);
}

/* ---- preview rendering (advisory, never identity) ------------------- */

/* CLUT snapshot -> XRGB table choice: the CLUT feeds the display in the
 * current video mode, so RGB16 entries are decoded as RGB when VMODE
 * says RGB16 and as CRY otherwise.  Advisory only. */
static uint32_t td_clut_rgb32(uint16_t v)
{
   unsigned mode = (GET16(tomRam8, 0x28) >> 1) & 0x03;   /* VMODE */
   if (mode == 3)
      return RGB16ToRGB32[v];
   return CRY16ToRGB32[v];
}

static void td_store_rgb(uint8_t *dst, uint32_t xrgb)
{
   dst[0] = (uint8_t)((xrgb >> 16) & 0xFF);
   dst[1] = (uint8_t)((xrgb >> 8) & 0xFF);
   dst[2] = (uint8_t)(xrgb & 0xFF);
}

/* Extract pixel (c, r) of the described window from emulated memory
 * (same addressing as td_serialize).  Returns the raw pixel value. */
static uint32_t td_pixel_value(const TexDumpDesc *d, uint32_t c, uint32_t r)
{
   uint32_t px  = (d->x0 + c) & 0xFFFF;
   uint32_t py  = (d->y0 + r) & 0xFFFF;
   uint32_t off = td_pixel_offset(px, py, d->width, d->pitch, d->psizefield);
   uint32_t a, b0, b1, b2, b3;

   switch (d->psizefield)
   {
      case 0:
         a = (d->base + off) & 0xFFFFFF;
         return (TexDumpRead8(a) >> ((~px) & 7)) & 0x01;
      case 1:
         a = (d->base + off) & 0xFFFFFF;
         return (TexDumpRead8(a) >> (((~px) << 1) & 6)) & 0x03;
      case 2:
         a = (d->base + off) & 0xFFFFFF;
         return (TexDumpRead8(a) >> (((~px) << 2) & 4)) & 0x0F;
      case 3:
         a = (d->base + off) & 0xFFFFFF;
         return TexDumpRead8(a);
      case 4:
         a = (d->base + (off << 1)) & 0xFFFFFF;
         b0 = TexDumpRead8(a);
         b1 = TexDumpRead8((a + 1) & 0xFFFFFF);
         return (b0 << 8) | b1;
      default:
         a = (d->base + (off << 2)) & 0xFFFFFF;
         b0 = TexDumpRead8(a);
         b1 = TexDumpRead8((a + 1) & 0xFFFFFF);
         b2 = TexDumpRead8((a + 2) & 0xFFFFFF);
         b3 = TexDumpRead8((a + 3) & 0xFFFFFF);
         return (b0 << 24) | (b1 << 16) | (b2 << 8) | b3;
   }
}

/* Render the window as RGB8 rows.  mode16 selects the 16bpp
 * interpretation (TEXDUMP_16BPP_CRY / _RGB); clut16 is the snapshotted
 * CLUT (256 x u16, host order) for <=8bpp windows. */
static uint8_t *td_render(const TexDumpDesc *d, int mode16,
                          const uint16_t *clut16)
{
   uint8_t *rgb;
   uint32_t r, c, v, xrgb;

   rgb = (uint8_t *)malloc((size_t)d->inner * d->outer * 3);
   if (!rgb)
      return NULL;
   for (r = 0; r < d->outer; r++)
   {
      uint8_t *row = rgb + (size_t)r * d->inner * 3;
      for (c = 0; c < d->inner; c++)
      {
         v = td_pixel_value(d, c, r);
         if (d->bpp <= 8)
            xrgb = td_clut_rgb32(clut16[v & 0xFF]);
         else if (d->bpp == 16)
            xrgb = (mode16 == TEXDUMP_16BPP_RGB)
                 ? RGB16ToRGB32[v & 0xFFFF] : CRY16ToRGB32[v & 0xFFFF];
         else
            /* 32bpp: stored phrase bytes are G, R, x, B (the Jaguar's
             * 24-bit line-buffer order; see tom_render_24bpp_scanline
             * in src/tom/tom.c). */
            xrgb = (((v >> 16) & 0xFF) << 16)      /* R (byte 1) */
                 | (((v >> 24) & 0xFF) << 8)       /* G (byte 0) */
                 | (v & 0xFF);                     /* B (byte 3) */
         td_store_rgb(row + (size_t)c * 3, xrgb);
      }
   }
   return rgb;
}

/* ---- first-sight dump ----------------------------------------------- */

static void td_first_sight(const TexDumpDesc *d, uint64_t h, uint32_t cmd,
                           uint32_t clut_crc, const uint16_t *clut16)
{
   char path[1500];
   char line[256];
   int n;
   const char *fl;
   uint8_t *rgb;

   td_stat_unique++;
   if (!td_ensure_output())
      return;

   /* Manifest first-sight row.  flags= records the transparency-
    * related B_CMD bits observed on the first-seen blit (bit
    * comparator BCOMPEN, data comparator DCOMPEN -- B_CMD has no
    * "TRANSEN" bit; docs/jtrm-blitter.md).  Transparency is a blit
    * property, not a tile property, so it is advisory metadata. */
   if ((cmd & 0x04000000) && (cmd & 0x08000000))
      fl = "bcompen,dcompen";
   else if (cmd & 0x04000000)
      fl = "bcompen";
   else if (cmd & 0x08000000)
      fl = "dcompen";
   else
      fl = "-";

   if (d->bpp <= 8)
      n = snprintf(line, sizeof(line),
            "%08x%08x\t%ux%u\t%u\tframe=%u\tsrc=%06x\tclut=%08x\tflags=%s\n",
            (unsigned)(h >> 32), (unsigned)(h & 0xFFFFFFFFu),
            (unsigned)d->inner, (unsigned)d->outer, (unsigned)d->bpp,
            (unsigned)td_frame, (unsigned)d->src_addr,
            (unsigned)clut_crc, fl);
   else
      n = snprintf(line, sizeof(line),
            "%08x%08x\t%ux%u\t%u\tframe=%u\tsrc=%06x\tclut=-\tflags=%s\n",
            (unsigned)(h >> 32), (unsigned)(h & 0xFFFFFFFFu),
            (unsigned)d->inner, (unsigned)d->outer, (unsigned)d->bpp,
            (unsigned)td_frame, (unsigned)d->src_addr, fl);
   td_manifest_write(line, n);

   /* Preview PNG(s). */
   if (d->bpp == 16 && td_mode16 == TEXDUMP_16BPP_BOTH)
   {
      rgb = td_render(d, TEXDUMP_16BPP_CRY, clut16);
      if (rgb)
      {
         snprintf(path, sizeof(path), "%s/%08x%08x-cry.png", td_dir,
                  (unsigned)(h >> 32), (unsigned)(h & 0xFFFFFFFFu));
         td_write_png(path, rgb, d->inner, d->outer);
         free(rgb);
      }
      rgb = td_render(d, TEXDUMP_16BPP_RGB, clut16);
      if (rgb)
      {
         snprintf(path, sizeof(path), "%s/%08x%08x-rgb.png", td_dir,
                  (unsigned)(h >> 32), (unsigned)(h & 0xFFFFFFFFu));
         td_write_png(path, rgb, d->inner, d->outer);
         free(rgb);
      }
   }
   else
   {
      rgb = td_render(d, td_mode16 == TEXDUMP_16BPP_RGB
                         ? TEXDUMP_16BPP_RGB : TEXDUMP_16BPP_CRY, clut16);
      if (rgb)
      {
         snprintf(path, sizeof(path), "%s/%08x%08x.png", td_dir,
                  (unsigned)(h >> 32), (unsigned)(h & 0xFFFFFFFFu));
         td_write_png(path, rgb, d->inner, d->outer);
         free(rgb);
      }
   }
}

/* ---- the launch hook ------------------------------------------------ */

void TexDumpLaunch(void)
{
   TexDumpDesc d;
   uint32_t cmd;
   uint32_t len;
   uint64_t h;
   int rc;
   int is_new;

   if (!td_set)
      return;                       /* enable-time allocation failed */

   rc = TexDumpDescribe(&d, &cmd);
   if (rc == 0)
      return;
   if (rc < 0)                      /* > 1 MB described window */
   {
      td_stat_skipped++;
      return;
   }

   len = TexDumpSerialize(&d, td_scratch, TD_SCRATCH_BYTES);
   if (len == 0)                    /* outside populated space / overrun */
   {
      td_stat_skipped++;
      return;
   }
   td_stat_captures++;

   h = TexDumpHashKey(&d, td_scratch, len);
   is_new = td_set_insert(h);

   if (d.bpp <= 8)
   {
      /* Palette is advisory metadata: snapshot the CLUT, and dedupe
       * (key, CLUT CRC) pairs through the same set so a key later seen
       * under a NEW palette appends one sighting row (never a second
       * PNG -- same key, one file). */
      uint16_t clut16[256];
      uint32_t clut_crc;
      uint64_t hs;
      unsigned i;
      int pair_new;

      for (i = 0; i < 256; i++)
         clut16[i] = (uint16_t)((tomRam8[0x400 + i * 2] << 8)
                              | tomRam8[0x400 + i * 2 + 1]);
      clut_crc = (uint32_t)crc32_calcCheckSum(&tomRam8[0x400], 0x200);

      hs = h;
      for (i = 0; i < 4; i++)
      {
         hs ^= (clut_crc >> (i * 8)) & 0xFF;
         hs *= TD_FNV_PRIME;
      }
      pair_new = td_set_insert(hs);

      if (is_new)
         td_first_sight(&d, h, cmd, clut_crc, clut16);
      else if (pair_new && td_ensure_output())
      {
         char line[128];
         int n = snprintf(line, sizeof(line), "%08x%08x\tclut=%08x\tframe=%u\n",
                          (unsigned)(h >> 32), (unsigned)(h & 0xFFFFFFFFu),
                          (unsigned)clut_crc, (unsigned)td_frame);
         td_manifest_write(line, n);
         td_stat_sightings++;
      }
   }
   else if (is_new)
      td_first_sight(&d, h, cmd, 0, NULL);
}

/* ---- option layer --------------------------------------------------- */

void TexDumpSetEnabled(int on)
{
   if (on && !td_set && !td_alloc_failed)
   {
      td_set = (uint64_t *)calloc(TD_SET_SIZE, sizeof(uint64_t));
      td_scratch = (uint8_t *)malloc(TD_SCRATCH_BYTES);
      if (!td_set || !td_scratch)
      {
         free(td_set);
         free(td_scratch);
         td_set = NULL;
         td_scratch = NULL;
         td_alloc_failed = 1;
         LOG_INF("[TEXDUMP] allocation failed -- texture dump unavailable\n");
      }
      else
         LOG_INF("[TEXDUMP] enabled -- dumping to %s/vj_texdump/\n",
                 td_base[0] ? td_base : "<system dir unset>");
   }
   texDumpEnabled = (on && td_set) ? 1 : 0;
}

void TexDumpSet16bppMode(int mode)
{
   td_mode16 = mode;
}

void TexDumpSetBasePath(const char *system_dir)
{
   if (system_dir && system_dir[0])
   {
      strncpy(td_base, system_dir, sizeof(td_base) - 1);
      td_base[sizeof(td_base) - 1] = '\0';
   }
   else
      td_base[0] = '\0';
}

void TexDumpFrame(void)
{
   td_frame++;
}

void TexDumpShutdown(void)
{
   if (td_stat_captures || td_stat_unique)
      LOG_INF("[TEXDUMP] session summary: %u unique tiles dumped, "
              "%u palette sightings, %u source blits hashed, %u skipped\n",
              (unsigned)td_stat_unique, (unsigned)td_stat_sightings,
              (unsigned)td_stat_captures, (unsigned)td_stat_skipped);
   if (td_manifest)
      rfclose(td_manifest);
   td_manifest = NULL;
   free(td_set);
   free(td_scratch);
   td_set     = NULL;
   td_scratch = NULL;
   td_entries = 0;
   texDumpEnabled = 0;
   td_mode16 = TEXDUMP_16BPP_CRY;
   td_overflow_warned = 0;
   td_alloc_failed = 0;
   td_png_warned = 0;
   td_write_warned = 0;
   td_frame = 0;
   td_base[0] = '\0';
   td_dir[0] = '\0';
   td_dir_ready = 0;
   td_stat_unique = 0;
   td_stat_sightings = 0;
   td_stat_captures = 0;
   td_stat_skipped = 0;
}
