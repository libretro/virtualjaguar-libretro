# Shadow-aware copy propagation — investigation notes (census §9.8 item 1)

**Date:** 2026-08-10
**Epic:** #338, track 1. **Status:** implementation written and building
clean, benefit **unverifiable** — retracting docs/hires-stage0-census.md
§9.5's mechanism claim. No feature commit; this document is the deliverable.

---

## 1. What was asked

`docs/hires-stage0-census.md` §9.8 item 1 specifies "shadow-aware copy
propagation": when a qualifying copy blit's *source* region is itself
shadow-tracked (real supersampled sub-pixel content, not Stage 1 box
replication), propagate the source's N×N block through to the
destination's shadow entry instead of overwriting it with a flat
replicated block. The motivating evidence was §9.5's claim that Hover
Strike renders its textured 3D view into an off-screen buffer at
`$100000`, and a later integer 1:1 copy blit discards that detail when it
composites the buffer onto the displayed framebuffer:

> `$100000` bucket: **9,207,596 (89%)** of accepted Stage 2 stores land
> there; render lookups there: **0**.

## 2. The implementation (built, C89-clean, not shipped)

Both engines' non-fractional identity-copy paths were extended to try a
source-side shadow fetch before falling back to Stage 1 replication.

### 2.1 New API — `src/tom/shadowfb.h` / `.c`

```c
/* Source-side counterpart to ShadowHiresStoreCryBlock, for a blit-time
 * (not OP-resolve-time) consumer. Value+epoch-checked block fetch: on a
 * hit, copies the source's N*N block into caller-supplied `out` and
 * returns nonzero. On miss (unallocated page, stale value, or expired
 * epoch) leaves `out` untouched and returns 0 -- the caller MUST fall
 * back to ShadowHiresStoreCry (Stage 1 replication).
 *
 * `current16` doubles as the identity-copy proof: pass the STOCK VALUE
 * JUST WRITTEN to the destination. A hit means the source's stock word
 * equals what was written -- i.e. this write really was a source
 * pass-through -- entirely via the existing tag/epoch check; no
 * separate equality test needed.
 *
 * Deliberately does NOT touch shadowHiresResolveHits/MissValue/
 * MissEpoch/MissNoPage: those are the OP per-scanline resolve pass's
 * diagnostic counters; this is a different consumer.
 *
 * Pure host-side shadow-state lookup: no bus-charged reads. */
int ShadowHiresFetchBlock(uint32_t addr, uint16_t current16,
                          shadowfb_sub *out);
```

Implementation in `shadowfb.c` is a copy of `shadow_hires_block`'s
tag/epoch check (the same one `ShadowHiresLineFromRAM`/the OP resolve
uses), minus the counter bumps, filling `out` instead of returning a
pointer:

```c
int ShadowHiresFetchBlock(uint32_t addr, uint16_t current16,
                          shadowfb_sub *out)
{
   uint32_t idx, page, word, tag, ep, nn, k;
   const shadowfb_sub *blk;

   if (!shadowHiresActive) return 0;
   addr &= 0xFFFFFF;
   if (addr >= 0x800000) return 0;
   idx  = (addr & 0x1FFFFE) >> 1;
   page = idx >> 12;
   if (!hiresPageTag[page]) return 0;
   word = idx & 0xFFF;
   tag  = hiresPageTag[page][word];
   if ((tag & 0x1FFFF) != ((uint32_t)current16 | SHADOWFB_TAG_VALID))
      return 0;
   ep = (tag >> HIRES_TAG_EPOCH_SHIFT) & 0xFF;
   if (((hiresEpoch - ep) & 0xFF) >= HIRES_EPOCH_WINDOW) return 0;

   nn  = (uint32_t)shadowHiresN * (uint32_t)shadowHiresN;
   blk = hiresPageSub[page] + word * nn;
   for (k = 0; k < nn; k++) out[k] = blk[k];
   return 1;
}
```

### 2.2 Fast engine (`blitter_generic`, `src/tom/blitter.c`)

Two directions, both handled (JTRM: `DSTA2` clear ⇒ A2 is source, A1 is
destination; `DSTA2` set ⇒ A1 is source, A2 is destination):

- **`DSTA2` branch (A1 source → A2 dest)**, ~line 954 area: the existing
  Stage 2 fractional-supersample block already computed a `copyGate`
  (`!inhibit && SRCEN && !BCOMPEN && A1 16bpp`). When that fractional
  path doesn't fire (`sh2` still 0), a new fallback tries
  `ShadowHiresFetchBlock(a1_addr + (PIXEL_OFFSET_16(a1) << 1),
  (uint16_t)writedata, sblk)`, gated additionally on
  `!GOURD && !PATDSEL && !ADDDSEL && !SRCSHADE`.
- **`!DSTA2` branch (A2 source → A1 dest)**, ~line 758 area: this branch
  had *no* Stage 2 logic at all before (A2 has no fractional address
  machinery, so it never needed any — see design §3.1). Added the same
  fetch-or-replicate logic, source address
  `a2_addr + (PIXEL_OFFSET_16(a2) << 1)`, destination
  `a1_addr + (PIXEL_OFFSET_16(a1) << 1)`.

### 2.3 Accurate engine (`BlitterMidsummer2`, `src/tom/blitter.c`)

This engine's pixel-mode `dwrite` site (~line 3680 in the pre-revert
tree) is shared by both directions via a `dsta2` bool, unlike the fast
engine's two separate branches. Two additions:

- **`addr_at_sread`**: a new persistent local (declared alongside the
  existing `a1_x_at_sread` cache, same lifetime), set to `address`
  inside both the `sreadx` and `sread` blocks. `address` is recomputed
  by `ADDRGEN` every tick based on `gena2i = (gensrc && !dsta2) ||
  (gendst && dsta2)`, so at the moment `sread`/`sreadx` fires, `address`
  already points at whichever of A1/A2 is acting as *source* this cycle
  — correct for either direction with no direction-specific
  recomputation. **This is exact, not approximate**: with `srcshift ==
  0` and `pixsize == 4`, `srcd1 >>= 48` leaves the *first* 16-bit word of
  the phrase read at `address` as the consumed value, so `addr_at_sread`
  is provably the address that produced `srcd`/`wdata`. Do not relax
  `srcshift == 0` to widen coverage — with a nonzero shift the consumed
  word comes out of the srcd2/srcd1 pipeline and the correspondence is
  no longer provable.
- **Unified gate**, replacing the old dsta2-only fallback:
  ```c
  if (!sh2 && shadowHiresN == 2 && !winhibit
        && srcen && !bcompen && srcshift == 0
        && !patdsel && !gourd && !adddsel
        && ((dsta2 && a1_pixsize == 4) || (!dsta2 && a2_pixsize == 4))
        && ShadowHiresFetchBlock(addr_at_sread, (uint16_t)wdata, sblk))
     sh2 = 1;
  ```
  A new helper `shadow_hires_addr_mid()` was factored out of the
  existing `shadow_hires_sub_mid()` (same ADDRGEN call, now shared) —
  used by the fractional supersample path's off-position sampling, not
  by the propagation fetch (which uses `addr_at_sread` directly, per
  the advisor review below).

None of the four call sites widened the *storage* gate (16bpp
destination, existing conditions) — they only chose block-propagation
over Stage 1 replication when the fractional path didn't already claim
the pixel and the source is a clean pass-through.

Reviewed and refined through two advisor passes; the reviewer's key
correction was to drop a manual `writedata == srcdata` equality check in
favor of letting `ShadowHiresFetchBlock`'s existing tag/epoch check
double as the identity proof — simpler and provably correct rather than
argued.

Full diff (uncommitted, includes throwaway debug instrumentation used
only during this investigation) is not part of the tree; available on
request from the session's scratchpad if needed.

## 3. Why the benefit is unverifiable — the census's mechanism does not exist

### 3.1 Baseline reproduces exactly

`hires_shot` on unmodified `libretro/develop` (`b48f854`), Hover Strike
(cart), 2x, screenshot-verified mission cockpit (terrain + ALERT HUD,
identical to census §9.3's description) at frames 2400/3200/4000:

| Engine | 2400 | 3200 | 4000 | Census §9.3 |
|---|---|---|---|---|
| Accurate | 0.0000% | 0.0000% | 0.0000% | 0.00/0.00/0.00% |
| Fast | 0.5509% | 0.0000% | 0.3208% | 0.55/0.00/0.32% |

Both match the census almost to the last digit. §9.3's on-screen numbers
are correct and reproduce on current develop.

### 3.2 But there is no off-screen RAM buffer, and no copy blit

Instrumenting both engines' actual production sites (throwaway counters,
same pattern as the census's own methodology, reverted before commit)
over the same screenshot-verified mission-gameplay window:

**Accurate engine**, frames 0–4000, gameplay window (~2200–4000):
- `storeBlockAny` (genuine Stage 2 blocks stored, i.e. non-uniform
  content) = **581,993**, frozen solid from frame ~2500 through frame
  4000 — no new detailed content is produced once the mission-select
  screen ends.
- All 581,993 land in `[$000000–$060000)` — the displayed framebuffer's
  own address range, not an off-screen buffer.
- The propagation gate (both directions, identical to the shipped
  fractional Stage 2 gate minus the fractional-walk requirement) fires
  **1,533,623** times in gameplay; **0** hits. 1,532,632 miss for
  "no page allocated at all" (the identity-copy's actual source address
  never received a shadow store from anything).
- A one-shot address print at the copy site found the recurring
  candidate: `DSTA2=0`, `A2_BASE≈$0ED590` → `A1_BASE≈$005780`. That
  region ($0ED590) was never itself a Stage 2 store target — it is
  presumably a static HUD/cockpit-frame graphic, not rendered 3D
  content. Correctly falls through to Stage 1 replication; nothing to
  propagate.
- Extending the SRCEN-read address histogram across the whole gameplay
  window: **zero** reads ever touch `[$000000–$060000)` after the
  frame-600 mission-select screen. The only place real detail is
  produced (menu, frame 600, 3.2464% nonuniform, screenshot-verified as
  a scaled thumbnail on the "SELECT LEVEL 1 MISSIONS" screen — a real
  but incidental Stage 3-shaped hit, unrelated to this item) is never
  read back by anything. (Caveat: this SRCEN-read histogram is only
  instrumented in the accurate engine's `sread`/`sreadx` sites; the fast
  engine's read side was not instrumented, so "never read back" is
  scoped to the accurate engine.)

**Fast engine**, same window:
- `storeBlockAny` = **23,327,104** — real production, unlike Accurate.
- Of those, **21,504,898 (92%)** target addresses `$F00000–$F20000`.
  A one-shot print pinned the exact range: `$F03000–$F0300E`+ — **GPU
  local program/data RAM** (`$F03000–$F03FFF`, the same 4KB region
  `src/core/crash_detect.c`'s `gpu_pc_escape` uses as the valid GPU PC
  window). This is not a framebuffer at all; it's the blitter staging
  data into the GPU's own scratchpad, presumably for a procedural
  terrain algorithm the GPU then runs itself (heightfield/lookup table,
  not pixel data). Both `ShadowHiresStoreCry` and `ShadowHiresStoreCryBlock`
  correctly reject it at their `if (addr >= 0x800000) return;` guard —
  by design, the shadow only tracks the bottom-8MB main-RAM mirror.
- Only 1,822,206 (8%) land in real RAM, again concentrated in
  `[$000000–$060000)`, again mostly during the frame-600 menu.
- The propagation gate fires 9,731 times in gameplay; 0 hits (same
  reasoning as Accurate — the identity-copy's source was never itself a
  Stage 2 target).

### 3.3 Reconciling with §9.5's "$100000" claim: 2 MB aliasing

`$0xF00000 mod 0x200000` (2MB, the shadow's own `addr & 0x1FFFFE`
masking period) `= 0x100000`, exactly. §9.5's own methodology note (§1)
describes counting into RAM buckets via address masking; if that
throwaway instrumentation counted *all* `pixsize==4` writes into a
2MB-periodic bucket without first excluding the `>= $800000` boundary
the way the shipped shadow functions do, a GPU-RAM write at `$F03000`
would land in the exact same bucket as a real RAM write at `$100000`.
That reproduces §9.5's numbers precisely: a 9.2M-store concentration,
zero render lookups (because no such RAM buffer is ever read — GPU RAM
isn't scanned by the OP), and — tellingly — §9.5's own table lists only
three buckets (`$000000–$040000`, `$050000–$0B0000`, `$100000`) with no
separate `$F00000` row, which is what you'd expect if raw addresses in
that range were never distinguished from their 2MB alias.

**Conclusion: §9.5's "off-screen buffer at `$100000`, discarded by an
integer copy" is not what is happening.** There is no off-screen RAM
buffer. The apparent "9.2M stores, 89%, zero render lookups" signal is
GPU-local-RAM traffic aliased into the RAM address space by the
census's own bucketing arithmetic, not main-RAM framebuffer content.
Hover Strike's actual on-screen non-zero (Fast: 0.55/0.00/0.32%) comes
entirely from the documented engine-predicate-parity gap (census §9.8
item 2), not from any copy.

## 4. Why this blocks item 1 specifically

Shadow-aware copy propagation requires a *source-side shadow entry* to
propagate from. In Hover Strike's actual mission gameplay:

- The accurate engine produces no real Stage 2 content at all after the
  menu ends (its fractional gate never fires for the terrain renderer —
  a different, unmeasured gap, not this item's target).
- The fast engine produces abundant real Stage 2 content, but 92% of it
  targets GPU RAM (structurally outside `< $800000`, and rightly so —
  copying GPU scratch data into a CRY framebuffer would be nonsense),
  and the remaining 8% is menu-phase content that nothing ever reads
  back with a qualifying copy shape.

No title in reach (measured: Hover Strike; not independently
re-measured for I-War, Towers II, or any other §6 A1-table title, since
§9.5's specific evidence was for Hover Strike) presents the "render to a
trackable RAM buffer, then integer-copy it to the display" shape the
census described. The propagation mechanism itself (§2) is sound,
C89-clean, and passes advisor review on its coherence properties — it
is the premise that does not hold, not the implementation.

## 5. Recommendation

- **Retract** docs/hires-stage0-census.md §9.5 and §9.8 item 1's
  mechanism claim (not §9.3's on-screen numbers, which reproduce
  exactly).
- **Do not ship** the propagation code in this state: it adds a shadow
  lookup to every non-fractional 16bpp copy pixel in both blitter hot
  paths for zero measured benefit on the one title it was built for.
- If a future investigation finds a *real* qualifying title (a genuine
  RAM-resident off-screen buffer discarded by an integer copy — I-War
  and Towers II were ruled out for a different reason in §9.6, but
  weren't checked against *this* mechanism specifically), §2 of this
  document is a complete, working starting point: the API, both engines'
  call sites, and the `addr_at_sread` exactness argument all transfer
  directly.
- Extending shadow tracking to the OP line buffer (`shadowHiresLineSub`,
  already exists per design §6.2) or to GPU-local RAM writes that
  eventually reach the display via a GPU-internal path is a materially
  different, larger project — out of scope for a "copy propagation"
  item, and not recommended on the strength of one title's traffic
  pattern.

## 6. Reproduction

```bash
# Baseline reproduction of census §9.3's numbers (both engines):
cc -O2 -Wall -std=c99 -I. -I./test/harness -I./libretro-common/include \
   -o test/tools/hires_shot test/tools/hires_shot.c \
   test/harness/harness.c -ldl -lm

VJ_EXPECT_BUILD=$(bash scripts/build-id.sh) ./test/tools/hires_shot \
  ./virtualjaguar_libretro.dylib "Hover Strike (1995).jag" \
  --option virtualjaguar_internal_resolution=2x \
  [--option virtualjaguar_usefastblitter=disabled] \
  --frames 4000 \
  --press 200:a --press 400:a --press 700:a --press 1000:a \
  --press 1300:a --press 1600:a --press 1900:a --press 2200:a \
  --shot 2400 --shot 3200 --shot 4000 --out-prefix /tmp/shot --quiet
```

The address-histogram / gate-reach instrumentation used to produce §3.2's
numbers was throwaway (temporary counters in `blitter.c`/`shadowfb.c`,
exported via `exports-test.list`, read via `harness_dlsym` from a small
probe tool) and was reverted before this commit, matching the pattern
`docs/hires-stage0-census.md` §8 and §9.9 already document for this
project's other throwaway census instrumentation.
