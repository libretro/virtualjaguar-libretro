# Blit memoization

**Status:** prototype, **off by default and tagged to no title.**
Output-identical in every configuration measured (1x/2x x true colour
on/off); see "Measured effect" for what it currently buys.
Issue [#411](https://github.com/libretro/virtualjaguar-libretro/issues/411).

Some titles re-render an identical scene every engine cycle while the
player is idle. Measured on Alien vs Predator: a fixed 5-field cycle
(one 1,446-blit heavy frame, one 4-blit HUD frame, three empty fields)
runs at the same cost whether the player moves or stands still, and
every idle heavy frame issues **one bit-identical blit stream**, while
the presented image cycles among 21 distinct frames. The measurements
are in #411.

This module skips such a blit when it can prove the destination already
holds the bytes the blit would produce. It is memoization, not a
heuristic: the skip condition makes output bit-identical by
construction. It knows nothing about any game's state.

## The skip condition

A blit is skipped when **both** hold:

1. Its full pre-launch state equals a recorded entry's, and
2. every main-RAM page that entry read or wrote is unchanged since.

Entries chain by successor, so a repeated stream matches as a
prefix-matched run rather than being defeated by writes on shared
destination pages.

## Five things that are load-bearing

Each was found by A/B against the memo-off run, and each had produced a
visibly divergent framebuffer first. They are the non-obvious part of
the design; do not "simplify" any of them without re-running the A/B.

**1. Identity is the whole blitter state, not `blitter_ram`.**
The engines carry decode/iterator state in file-scope variables across
launches, so equal registers do not imply equal behaviour. Identity is
the canonical `BlitterStateSave` blob. Matching on `blitter_ram` alone
produced 1,967 unsound would-be skips on AvP's texture blits.

**2. A skipped chain's writes are replayed, not dropped.**
Skipping a *prefix* of a stream leaves the previous pass's **final**
bytes in RAM where a live run would have had intermediates. Each entry
therefore carries a write log (addr, len, value), materialized before
any live engine execution and at the frame boundary. Replaying bytes
that are already present is idempotent, so this is always sound; it
costs memcpy-class time, not blit time.

**3. Write generations need two domains.**
Writes made *by chain members* must invalidate READ footprints (a
member rewriting a page someone sources from changes that blit's input)
but **not** WRITE footprints — their ordering relative to skipped
entries is already preserved by the log replay. Foreign writes (68K,
GPU, DSP, OP, cheats) invalidate both. Without the split, one live
full-buffer clear re-dirties the whole stream every cycle: dirty count
1,058,653 with one domain versus 62 with two.

**4. Every path that writes main RAM must be hooked.**
`m68k_write_memory_32` stores to `jaguarMainRAM` directly and bypasses
`JaguarWrite*` and every other funnel. It was invisible to the memo
until hooked explicitly, and page generations silently lied. Any future
RAM-touching path needs the same treatment.

**5. Shadow stores are replayed too, or nothing works with true colour
or hi-res.**
Both shadow surfaces are populated from *inside* the blitter engines
(`ShadowFBStoreCry`, `ShadowHiresStoreCry`, `ShadowHiresStoreCryBlock`).
A skipped blit never runs them, while its RAM bytes *are* replayed from
the write log — so the surface and RAM disagree about who wrote each
word last, and the presented frame differs from a live run. Before this
was fixed, the memo was bit-identical only at 1x with true colour off,
and diverged from frame 1544 in every other configuration.

Each recorded blit therefore logs its shadow stores as well, replayed
beside the RAM writes. Three things make that affordable and safe:

- **Hooked in `shadowfb.c`, not at the ten `blitter.c` call sites.**
  One funnel cannot be half-covered; a missed site fails silently,
  which is exactly the trap `m68k_write_memory_32` set on the RAM side.
- **One shared arena, not a per-entry array.** A worst-case per-entry
  buffer would multiply the pool by an order of magnitude for a payload
  most blits never use. Entries take contiguous slices; the arena is
  reclaimed wholesale by a flush when it fills, and any blit whose log
  does not fit is marked exec-through so it is never skipped.
- **One record per pixel, not two.** Both surfaces store the same pixel
  back to back, so records coalesce. Without that, 2x with true colour
  emitted two records per pixel and a single heavy AvP frame overran
  the arena — measured: 94 arena flushes and **zero** skips, versus 13
  flushes and 99,618 skips after coalescing.

The hi-res epoch re-stamp is still required on top of this: it refreshes
the *age* of entries whose content is already correct.

`verify` mode compares the shadow log too. It did not before, which is
precisely how a divergence that only appears with true colour or hi-res
survived a clean 2.5-million-check corpus sweep.

## Boundaries

- **Blits touching anything outside main RAM re-execute live**
  ("exec-through"): device space, mutable cart, the unpopulated
  `$200000-$7FFFFF` hole. Also anything whose write log overflows
  256 records (full-buffer clears).
- **CD content is refused.** The CD HLE writes main RAM without passing
  the write hooks, so page generations would be wrong.
- **Intermediate states are not reproduced.** While a chain is skipped,
  RAM holds the stream's final bytes. A title whose 68K/GPU reads the
  destination buffer *mid-stream* could observe the difference. No
  tracked title does — and `verify` mode exists to falsify exactly this
  before any title is tagged.
- **Timing is unaffected.** The bus-occupancy model
  (`BlitDurationSysclks`) is computed analytically from the pre-launch
  registers at the launch site, before this module runs.

## Measured effect

AvP idle, host wall-clock normalized to zero-blit frames within each run
(this cancels machine-wide noise, which otherwise swamps the effect),
3 reps per config:

| config | heavy/zero frame ratio, memo off | memo on |
|---|---|---|
| 2x + true colour | 1.945 (3 reps: 1.954 / 1.908 / 1.972) | 1.802 (1.900 / 1.800 / 1.706) |

Render overhead is `ratio - 1`, so that is roughly **-15%** of the
redundant render cost at 2x.

Re-measured on a quieter host (load 5-6 rather than 70): ratios 1.929 /
1.920 / 2.006 memo-off against 1.755 / 1.899 / 1.778 memo-on, i.e.
overhead 0.952 -> 0.811, **-14.8%** — the same answer the loaded run
gave, so -15% is reproducible. Absolute per-frame times remain noisy on
this box (10-41ms across reps); the ratio is the stable metric.

An earlier revision of this document claimed **-34%** at 2x. That was
measured before shadow stores were replayed -- i.e. while the memo was
skipping work it was not entitled to skip, and producing a different
frame. The replay is real work (a store per pixel per surface), so the
correct figure is necessarily smaller. The 1x-with-true-colour-off
figure of -10% predates the shadow work and is unaffected by it.

## Verifying a title before tagging it

`virtualjaguar_blit_memo=verify` never skips. It runs every would-be
skip live and compares the write log and post-launch state against what
the memo would have replayed, so any divergence means enabling the memo
on that title would change emulation.

```bash
cc -O2 -Wall -std=c99 -I. -I./test/harness -I./libretro-common/include \
   -o test/tools/blit_memo_verify test/tools/blit_memo_verify.c \
   test/harness/harness.c -ldl -lm

VJ_EXPECT_BUILD=$(./scripts/build-id.sh) ./test/tools/blit_memo_verify \
  ./virtualjaguar_libretro.dylib "test/roms/private/ROMS/<title>" --frames 4800
```

**A zero-divergence result only counts when the checker actually ran.**
The tool exits 3 ("thin") when fewer than `--min-runs` checks happened —
a title that sits in attract and never repeats a stream is *unverified*,
not clean. Drive it into gameplay with `--press` or a fixture.

Corpus sweep over every cartridge:

```bash
FRAMES=3000 JOBS=6 bash test/tools/blit_memo_sweep.sh

# ...and with the shadow surfaces on, which is what exercises the
# shadow-store replay.  A sweep without this never touches that path.
EXTRA_OPTS="--option virtualjaguar_internal_resolution=2x \
            --option virtualjaguar_true_color=enabled \
            --option virtualjaguar_pertitle_defaults=disabled" \
  bash test/tools/blit_memo_sweep.sh
```

Neither in-repo public ROM (`yarc`, `jagniccc`) ever repeats a blit
stream, so there is no CI gate for this — the private-corpus sweep is
the gate, same as the audio tests and the CD boot matrix.

## Sweep status

**Retail sweep complete — 64 commercial cartridges, 0 divergences.**
Run twice: once at stock 1x, and once at **2x + true colour**, which is
the configuration that exercises shadow-store replay. Both runs produce
the **same 19 clean titles**; enabling the surfaces demoted nothing.

| verdict | 1x | 2x + true colour | meaning |
|---|---|---|---|
| clean | 19 (2,546,482 checks) | 19 (**2,134,856 checks**) | 0 divergences in both |
| thin | 43 | 43 | no verdict — never repeated a stream in the window |
| noload | 2 | 2 | core refuses the dump (prototype/alpha format) |

The 2x run's checks compare shadow content as well as the write log and
blitter state, so it covers the class that the original sweep was blind
to.

Clean, with check counts: Kasumi Ninja (648,213 — two dumps), Towers II
(311,317), Missile Command 3D (226,853), Doom EX (170,303), Doom
(144,658), Atari Karts (134,216), Alien vs Predator (121,455), Trevor
McFur (69,836), Wolfenstein 3D (26,474), Cybermorph (8,722 — two
dumps), Zool 2, Tempest 2000, Pitfall, Bubsy, Aircars, Skyhammer,
Syndicate.

That spans span-rasterizers (Doom, AvP, Wolf3D), polygon engines
(Cybermorph, Towers II), sprite/scroller titles and a fighter — the
skip condition held on every one.

A separate partial run over the full 159-cartridge tree (including PD
demos) scored 58 titles with the same result: 18 clean, 2,241,276
verifications, 0 divergences.

`thin` is the sweep's remaining weakness, not a warning sign: the
generic attract-buster input never reaches gameplay in those titles, so
most show almost no blit activity at all (Double Dragon V: 4 blits,
Fever Pitch Soccer: 0). Two exceptions blit heavily but never repeat a
matching stream — Club Drive (394,581 misses) and Checkered Flag
(94,792). Those want real input fixtures before they can be tagged.

## Open items

0. **Re-measure on a quiet host** (see "Measured effect") and check
   the memory cost: the shadow arena adds ~9.6MB on top of the 14.6MB
   entry pool whenever a surface is active.
1. **Give thin titles real input fixtures**, especially Club Drive and
   Checkered Flag, which blit heavily but never repeated a stream under
   generic input. Also sweep the PD/homebrew remainder.
2. **Tagging AvP declined on measured benefit, not soundness.** AvP is
   the best-evidenced title (710,433 checks, 0 divergences, bit-identical
   A/B over 8,000 frames) and the corpus sweep is clean, but the memo only
   skips idle-window redundant blits -- themselves ~15% of idle wall time
   at 2x (see #411) -- so the -15%-of-that-slice effect measured above is
   roughly 2% of idle time, unmeasurable in RetroArch. Tagging turns the
   memo on by default for every AvP user for that gain, plus the 14.6MB
   entry pool + 9.6MB shadow arena (item 3) and an untested run-ahead/
   rollback interaction (item 6). Not tagged; revisit only if the benefit
   is remeasured larger or those costs shrink. Doom, Kasumi Ninja, Towers
   II and Missile Command 3D have six-figure clean check counts too but
   have not been through this same cost/benefit pass.
3. **Shrink the pool.** 4096 entries × 3,744 B = **14.6 MB**, too much
   for iOS. Most of an entry is two 768-byte state blobs and a 2 KB
   write log.
4. **Measure the disabled path.** The hooks sit on every write path for
   every title; `make benchmark` A/B has not been run.
5. **Widen coverage.** Represent large writes as page-range descriptors
   instead of per-write records, so full-buffer fills stop falling back
   to exec-through.
6. **Re-run runahead/rollback determinism tests with the memo on** —
   they currently exercise it off, since it is default-disabled.
