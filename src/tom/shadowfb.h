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
/* Bit 17: this entry's RGB888 is texture-pack art (ShadowFBStoreRGB),
 * not a CRY reconstruction (ShadowFBStoreCry).  Issue #528.
 *
 * The distinction only matters at presentation.  A CRY reconstruction is
 * meaningful ONLY on the CRY scanline path -- it is a decomposition of
 * the 16-bit word through the chroma tables, and running it on a word
 * TOM is scanning out as RGB16 would show an invented colour.  Pack art
 * is an ABSOLUTE RGB888 the author drew: it carries no format
 * assumption, so it is correct wherever the word it is tagged against
 * reaches the screen.  Hence the RGB16-direct renderers substitute on
 * this bit and this bit only, while the CRY renderers mask it out and
 * substitute on either kind.
 *
 * Tag value comparisons must therefore mask with 0x1FFFF, exactly as the
 * hi-res tag does for HIRES_TAG_REPL: an ordinary ShadowFBStoreCry
 * rewrites the tag WITHOUT this bit, so stale pack art self-clears with
 * no invalidation hook. */
#define SHADOWFB_TAG_REPL    0x20000
#define SHADOWFB_TAG_VMASK   0x1FFFF

/* Nonzero when the surface is on AND the buffers are allocated.  Hot
 * paths gate on this before doing any shadow work.  The surface can be
 * active for two reasons: the True Color option, or an active texture
 * replacement pack (#369 deliverable 2) presenting through it. */
extern int shadowFBActive;

/* Nonzero when the blitter engines should make Gouraud/SRCSHADE
 * precision stores (the True Color feature proper).  Always implies
 * shadowFBActive.  A replacement-only activation leaves this OFF so
 * every non-replaced pixel presents bit-identically to stock. */
extern int shadowFBPrecision;

/* Set by the True Color option (independent of surface allocation
 * order; the effective flag is recomputed on enable). */
void ShadowFBSetPrecision(int on);

/* Nonzero once a pack has stored at least one 1x replacement entry
 * (issue #528).  The RGB16-direct scanline renderers gate on this so a
 * run without a pack -- including a plain True Color run -- does zero
 * extra per-pixel work and stays bit-identical to stock.  Counterpart
 * of shadowHiresReplActive on the Nx surface. */
extern int shadowFBReplActive;

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

/* Texture-replacement store (issue #369 deliverable 2): record an
 * ARBITRARY RGB888 for a main-RAM word address, tagged against value16.
 * Only the replacement pipeline calls this (see texreplace.c); it is
 * not part of the blit memo's shadow-store replay. */
void ShadowFBStoreRGB(uint32_t addr, uint16_t value16, uint32_t rgb888);

/* Value-checked lookup: returns nonzero and fills *rgb888 only when the
 * entry's tag matches current16 (the value just read from RAM).  Both
 * kinds of entry (CRY reconstruction and pack art) hit. */
int ShadowFBLookup(uint32_t addr, uint16_t current16, uint32_t *rgb888);

/* As ShadowFBLookup, but hits ONLY on a texture-pack entry
 * (SHADOWFB_TAG_REPL).  The RGB16-direct presentation path uses this:
 * see the SHADOWFB_TAG_REPL comment for why a CRY reconstruction must
 * not be substituted on an RGB16 scanout. */
int ShadowFBLookupRepl(uint32_t addr, uint16_t current16, uint32_t *rgb888);

/* OP 16bpp write site helper: resolve srcAddr against the shadow RAM
 * (hit -> shadow RGB888, miss -> stock CRY16ToRGB32 conversion) and
 * store the result + tag into shadow line entry `idx`.  Out-of-range
 * idx is ignored (renderer then falls back via tag mismatch).
 *
 * The line tag carries SHADOWFB_TAG_REPL through from the RAM entry, so
 * the scanline renderers can tell pack art from a CRY reconstruction. */
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
 * is only trusted when it was written within the last few presented
 * frames (HIRES_EPOCH_WINDOW in shadowfb.c -- sized for slow
 * double-buffered engines like Doom's ~15Hz renderer), which bounds
 * the stale-structure artifact class that the 1x bounded-error
 * argument does not cover at Nx (design section 5.4).
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

/* OP resolve hit/miss counters -- permanent diagnostics for the one hi-res
 * failure mode that produces no other symptom.  The blitter can store every
 * supersampled block correctly while the OP resolve rejects 100% of them at
 * the value+epoch check, yielding 0.0000% supersampled output with nothing
 * in the log: production and delivery are separate failure points and only
 * delivery fails silently (Doom and Alien vs Predator have both hit it, both
 * epoch-expired).  Monotonic since the last ShadowHiresShutdown; only ever
 * bumped while shadowHiresActive, so a 1x run leaves them all zero.
 *
 * hits + missValue + missEpoch + missNoPage == resolves attempted:
 *   missNoPage -- no shadow page for the address (production never got here)
 *   missValue  -- entry exists, RAM value no longer matches its tag
 *   missEpoch  -- value matched, entry rejected for age only (the silent one)
 *
 * The buckets describe the STOCK supersampled block only.  Texture-pack
 * art is exempt from the age check (issue #528; see the REPL section
 * below), so an epoch-expired word carrying pack art bumps missEpoch and
 * still delivers its replacement plane -- missEpoch climbing while pack
 * art is on screen is expected, not a contradiction.
 *
 * Stage 3 (design section 6.4) note: OP scaled-bitmap pixels resolve
 * against the RAM shadow FIRST (ShadowHiresLineFromRAM, counted here
 * like any other resolve) and only fall back to the freshly-read
 * half-step point samples (op_hires_scale_peek /
 * ShadowHiresLineFromScaledSamples in op.c) when that resolve misses.
 * So every scaled pixel still bumps exactly one counter and the
 * "all zero while shadowHiresActive" heartbeat diagnostic stays valid;
 * a scaled-object title that never blits its source bitmap simply
 * shows its scaled pixels in the miss buckets (normal, expected --
 * static image data was never a shadow-tracked destination).
 *
 * Reported by crash_detect.c's verbose heartbeat; also exported in the test
 * ABI (link-test.T / exports-test.list) so harnesses can dlsym and assert. */
extern uint64_t shadowHiresResolveHits;
extern uint64_t shadowHiresResolveMissValue;
extern uint64_t shadowHiresResolveMissEpoch;
extern uint64_t shadowHiresResolveMissNoPage;
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

/* Savestate access to the frame epoch (issue #400).
 *
 * The epoch is not emulated-machine state, but it IS presented-frame
 * state: ShadowHiresFrameTick() drops every cached block when the
 * modulo-256 counter wraps, and that clear changes the picture on the
 * frame it happens.  A state restored without the epoch replays the wrap
 * at a different frame, so the same rollback renders differently -- the
 * failure behind savestate_features = 3 not actually holding.
 *
 * Restoring it is enough on its own: the cached blocks themselves are
 * rebuilt by the blits that re-execute after the rollback, so only the
 * wrap phase has to be carried across. */
uint32_t ShadowHiresGetEpoch(void);
void     ShadowHiresSetEpoch(uint32_t epoch);

/* Re-stamp the epoch of every VALID entry covering one 4KB main-RAM
 * page (page index = RAM address >> 12).  For the blit memo
 * (blit_memo.c): a skipped blit leaves its destination bytes -- and
 * therefore its shadow content -- bit-frozen, so refreshing the epoch
 * preserves exactly the resolve outcome of the live cycle instead of
 * letting HIRES_EPOCH_WINDOW silently age the content out.  Callers
 * must only use this on pages verified untouched since the content
 * was stored. */
void ShadowHiresRestampRamPage(uint32_t ramPage4k);

/* Free pages + line buffer and reset ALL statics (retro_deinit /
 * retro_unload_game / iOS reload). */
void ShadowHiresShutdown(void);

/* ------------------------------------------------------------------
 * Texture-replacement overlay on the Nx surface (issue #369 tier 3).
 *
 * Pack art is an ARBITRARY RGB888 per SUBPIXEL -- it is not derived
 * from the stock word, so it cannot live in a `shadowfb_sub`
 * ({value16, frac16} is a CRY decomposition).  Rather than widening
 * every entry to 8 bytes for every user, replacement rides a PARALLEL
 * per-page plane of N*N uint32 entries, allocated only for pages a
 * pack actually touches, and marked by one extra bit in the SAME word
 * tag.  Consequences that make this the cheap option:
 *
 *   - any ordinary ShadowHiresStoreCry/Block to the word rewrites the
 *     tag WITHOUT the replacement bit, so stale pack art self-clears
 *     through the machinery that already exists;
 *   - the value coherence check is unchanged and shared;
 *   - a 1x run, or a 2x run with no pack, allocates nothing and pays
 *     one already-predicted branch.
 *
 * The EPOCH half of the coherence check does not apply to pack art
 * (issue #528).  HIRES_EPOCH_WINDOW bounds stale STRUCTURE -- an N*N
 * interior derived from a write RAM no longer holds -- and for pack art
 * that class is already unbounded and already shipping at 1x, where the
 * tier-1 shadow carries no epoch at all and is presenting the same
 * tile's 1x representative at the same pixel on the same value check.
 * Ageing the Nx block out therefore prevented no artifact; it only
 * dropped a statically-blitted tile (HUD, menu, title card) from the
 * author's art to a flat 1x block after ~16 frames.  The stock sub
 * block behind an author alpha hole IS still refused on age, so holes
 * fall back to box replication of the word RAM holds -- never invented
 * detail.
 *
 * Entry encoding: SHADOWFB_HIRES_REPL_VALID | RGB888 to replace that
 * subpixel, or 0 to keep the stock/supersampled content underneath
 * (author alpha).
 * ------------------------------------------------------------------ */

#define SHADOWFB_HIRES_REPL_VALID 0x80000000u

/* Nonzero once a pack has stored at least one replacement block.  Hot
 * paths (the OP resolve, the Nx scanline renderer) gate on this so a
 * no-pack run does zero extra per-subpixel work. */
extern int shadowHiresReplActive;

/* Nx line-buffer replacement plane, parallel to shadowHiresLineSub and
 * indexed identically: entry(sy, stockIdx, sx) =
 * shadowHiresLineRepl[(sy*720 + stockIdx)*N + sx].  Only meaningful
 * where shadowHiresLineTag[stockIdx] validates. */
extern uint32_t *shadowHiresLineRepl;

/* Record an N*N block of pack RGB for a main-RAM word (issue #369
 * tier 3, called only from texreplace.c's post-blit witness walk).
 * `stock16` is the tag key -- the value RAM actually holds -- and
 * `rgb` holds N*N entries in sub-row-major order (sy*N + sx), each
 * either SHADOWFB_HIRES_REPL_VALID|RGB888 or 0 for "keep stock".
 *
 * Deliberately NOT logged to the blit memo: the replacement walk
 * re-runs on every launch (memo skips included), and the memo's
 * shadow-store replay deserializes `shadowfb_sub` blocks -- feeding it
 * RGB words would silently reinterpret them.  Same reasoning as
 * ShadowFBStoreRGB. */
void ShadowHiresStoreReplBlock(uint32_t addr, uint16_t stock16,
                               const uint32_t *rgb);

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
 * pass, producing all N sub-rows at once (design section 6.1).
 * Returns nonzero on a shadow-block hit -- or on a pack-art hit with no
 * usable stock block (issue #528) -- and 0 on any miss.  The Stage 3
 * caller in op.c uses this to fall back to point samples ONLY when no
 * real content exists for the word: ShadowHiresLineFromScaledSamples
 * zeroes the line replacement plane, so reporting a miss for a word
 * that just delivered pack art would wipe it. */
int ShadowHiresLineFromRAM(int idx, uint32_t srcAddr, uint16_t value16);

/* Stage 3 (design section 6.4): OP scaled-bitmap miss fallback.  Used
 * only after ShadowHiresLineFromRAM reported a miss for this word --
 * a RAM-shadow hit (the source bitmap was itself a supersampled blit
 * destination, a normal Jaguar idiom) carries strictly more
 * information and always wins.  The content here is N freshly
 * point-sampled source pixels, one per horizontal sub-column, already
 * resolved by the caller via a LOCAL copy of the HSCALE walk
 * (op_hires_scale_peek in op.c).  `cols[N]` is in output column order
 * (sx = 0..N-1, left-to-right in the Nx line buffer); `value16` is the
 * stock pixel this destination write produced (the tag key, unchanged
 * semantics).  Fills every sub-row identically -- Stage 3 as shipped
 * supersamples HSCALE only, not VSCALE, so all N sub-rows repeat the
 * same N columns (see the design section 6.4 note in op.c for why). */
void ShadowHiresLineFromScaledSamples(int idx, const shadowfb_sub *cols,
                                       uint16_t value16);

#ifdef __cplusplus
}
#endif

#endif	/* __SHADOWFB_H__ */
