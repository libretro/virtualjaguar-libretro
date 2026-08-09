/*
 * shadowfb.h: True-color shadow precision framebuffer (epic #338 track 3)
 *
 * Two lazily-allocated arrays mirror main RAM's 1M 16-bit pixel words:
 *   shadowRGB[1M] -- packed RGB888 full-precision conversion of the pixel
 *   shadowTag[1M] -- the stock 16-bit value the entry was derived from,
 *                    OR'd with a valid bit (so zero-init never
 *                    false-matches RAM zeros)
 *
 * Coherence is by value-check at READ time, never by invalidation hooks:
 * a reader compares RAM's current 16-bit value with the entry's tag and
 * falls back to the stock LUT on mismatch.  No CPU/GPU/DSP/OP write path
 * is touched, so the stock pipeline stays provably intact.
 *
 * The shadow LINE buffer mirrors the OP line buffer at tomRam8[0x1800]
 * (one entry per 16-bit pixel) and uses the same tag scheme, so the CRY
 * scanline renderer only substitutes a full-precision pixel when the
 * entry provably corresponds to the 16-bit value actually in the line
 * buffer.
 *
 * Lifecycle: derived cache only.  Never savestated; invalidated on
 * savestate load and option toggle; freed and all statics reset in
 * retro_deinit (iOS cannot dlclose cores).
 *
 * See docs/true-color-shadowfb-design.md.
 */
#ifndef __SHADOWFB_H__
#define __SHADOWFB_H__

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define SHADOWFB_LINE_PIXELS 720
#define SHADOWFB_TAG_VALID   0x10000

/* Nonzero when the core option is on AND the buffers are allocated.
 * Hot paths gate on this before doing any shadow work. */
extern int shadowFBActive;

/* Shadow line buffer, parallel to tomRam8[0x1800..], one entry per
 * 16-bit line-buffer pixel.  Tag = value16 | SHADOWFB_TAG_VALID. */
extern uint32_t shadowLineRGB[SHADOWFB_LINE_PIXELS];
extern uint32_t shadowLineTag[SHADOWFB_LINE_PIXELS];

/* Option toggle: allocates+clears (on) or frees (off).  Allocation
 * failure logs a warning and leaves the feature off (core runs stock). */
void ShadowFBSetEnabled(int enable);

/* Invalidate every entry (savestate load). */
void ShadowFBInvalidate(void);

/* Free buffers and reset ALL statics (retro_deinit / iOS reload). */
void ShadowFBShutdown(void);

/* Full-precision CRY -> RGB888 conversion: chroma tables x 24-bit
 * intensity, where intensity24 = (value16 low byte << 16) | frac16.
 * Any frac16 yields a color within one stock intensity quantization
 * step of CRY16ToRGB32[value16], so results are structurally bounded. */
uint32_t ShadowFBCryRGB(uint16_t value16, uint16_t frac16);

/* Record a full-precision pixel for a main-RAM word address (blit time).
 * Addresses outside the bottom-8MB RAM mirror window are ignored. */
void ShadowFBStoreCry(uint32_t addr, uint16_t value16, uint16_t frac16);

/* Value-checked lookup: returns nonzero and fills *rgb888 only when the
 * entry's tag matches current16 (the value just read from RAM). */
int ShadowFBLookup(uint32_t addr, uint16_t current16, uint32_t *rgb888);

/* OP 16bpp write site helper: resolve srcAddr against the shadow RAM
 * (hit -> shadow RGB888, miss -> stock CRY16ToRGB32 conversion) and
 * store the result + tag into shadow line entry `idx`.  Out-of-range
 * idx is ignored (renderer then falls back via tag mismatch). */
void ShadowFBLineFromRAM(int idx, uint32_t srcAddr, uint16_t value16);

/* ==================================================================
 * Hi-res (Nx) shadow surface -- epic #338 track 1, Stage 1.
 * See docs/hires-upscaling-design.md.
 *
 * Each stock 16-bit CRY pixel word in main RAM owns an N*N block of
 * shadow subpixels ({value16, frac16} -- composes with true-color,
 * design section 3.2), stored in lazily-allocated pages of 8KB of
 * stock RAM (4096 words -> tag[4096] + 4096*N*N entries).
 *
 * Coherence is the same value-check-at-read scheme as the 1x shadow,
 * PLUS a frame-epoch field in the tag (design section 3.4): an entry
 * is only trusted when its epoch is the current or previous frame,
 * which bounds the stale-structure artifact class that the 1x
 * bounded-error argument does not cover at Nx (design section 5.4).
 *
 * Stage 1 semantics: every writer stores the stock pixel replicated
 * N*N times (box replication), so output is bit-exactly the Nx box
 * replication of the 1x frame.  Stage 2 will store real sub-pixel
 * content in the same entries.
 *
 * Lifecycle: derived cache, never savestated; N is fixed at content
 * load (restart required); freed and all statics reset in
 * ShadowHiresShutdown (iOS cannot dlclose cores).
 * ================================================================== */

/* Scope fence: Stage 1 ships N=2 only. */
#define SHADOWFB_HIRES_MAX_N       2
/* 2MB stock RAM / 8KB per page. */
#define SHADOWFB_HIRES_PAGES       256
#define SHADOWFB_HIRES_PAGE_WORDS  4096
/* Hard cap on total page allocation (design section 3.3): on cap,
 * stop allocating and degrade to nearest-neighbour.  Never evict. */
#define SHADOWFB_HIRES_CAP_BYTES   (64u * 1024u * 1024u)

/* One shadow subpixel: stock-format CRY value + sub-quantization
 * intensity fraction (true-color's frac16).  4 bytes. */
typedef struct
{
   uint16_t value16;
   uint16_t frac16;
} shadowfb_sub;

/* Nonzero when the option requested N>1 AND allocation succeeded. */
extern int shadowHiresActive;
/* Replication factor.  1 whenever hi-res is off, so callers may
 * multiply by it unconditionally. */
extern int shadowHiresN;

/* Nx shadow line buffer: N sub-rows of (720*N) entries, flattened as
 * entry(sy, stockIdx, sx) = shadowHiresLineSub[sy*720*N + stockIdx*N + sx].
 * One tag per STOCK pixel (all N*N subpixels of a stock pixel are
 * written together): tag = value16 | SHADOWFB_TAG_VALID. */
extern shadowfb_sub *shadowHiresLineSub;
extern uint32_t shadowHiresLineTag[SHADOWFB_LINE_PIXELS];

/* Fix N at content load.  n <= 1 disables; allocation failure logs a
 * warning and the core runs at 1x. */
void ShadowHiresSetN(int n);

/* Advance the frame epoch (call once per presented frame). */
void ShadowHiresFrameTick(void);

/* Invalidate every entry (savestate load).  Cost is per stock word --
 * independent of N. */
void ShadowHiresInvalidate(void);

/* Free pages + line buffer and reset ALL statics (retro_deinit /
 * retro_unload_game / iOS reload). */
void ShadowHiresShutdown(void);

/* Record a 16bpp CRY destination write (blit time): fills the stock
 * word's N*N block with {value16, frac16} and stamps the epoch tag.
 * Addresses outside the bottom-8MB RAM mirror window are ignored. */
void ShadowHiresStoreCry(uint32_t addr, uint16_t value16, uint16_t frac16);

/* Stage 2: record a 16bpp CRY destination write whose N*N block carries
 * real per-subpixel content (fractional-walk source supersampling).
 * `stock16` is the stock 16-bit value the blit wrote (the tag key --
 * readers value-check against it); `blk` holds N*N entries in
 * sub-row-major order (sy*N + sx).  Same address rules as
 * ShadowHiresStoreCry. */
void ShadowHiresStoreCryBlock(uint32_t addr, uint16_t stock16,
                              const shadowfb_sub *blk);

/* OP 16bpp resolve site: copy the RAM shadow block for srcAddr into
 * the Nx line buffer at stock pixel `idx` (tag+epoch checked against
 * value16, the word the OP just read); on miss, replicate
 * {value16, 0} N*N times.  Runs inside the OP's single per-scanline
 * pass, producing all N sub-rows at once (design section 6.1). */
void ShadowHiresLineFromRAM(int idx, uint32_t srcAddr, uint16_t value16);

#ifdef __cplusplus
}
#endif

#endif	/* __SHADOWFB_H__ */
