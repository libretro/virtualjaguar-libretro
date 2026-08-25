# Virtual Jaguar libretro v3.5.1 — Settings clarity

A small fast-follow to v3.5.0. No emulation behaviour changes: this release
makes the core options honest about what they do to each other, shortens the
descriptions that had grown into walls of text, and fixes a test fixture that
kept `make test` red on a clean tree.

---

## The compounding-settings warning (#595)

**DSP Idle-Loop Fast-Forward** (`virtualjaguar_risc_idle_skip`) is the largest
speed lever the core has — 66-87% less DSP interpretation on the titles
measured in #569 — and `DSPExec()` switches it off entirely while certain
other options are active. Nothing said so. A user who raised the RISC
overclock on a borderline-slow title paid the overclock's own cost *and*
silently forfeited the bigger win, which reads as "overclocking made it much
slower" with no explanation available anywhere.

The core now logs one `[perf]` line naming the actual suppressor:

```
[perf] virtualjaguar_risc_idle_skip is enabled but suppressed by:
virtualjaguar_risc_clock_scale -- the DSP idle-loop fast-forward is doing
nothing, so this combination can run slower than idle-skip alone. Your
settings are honored, not overridden.
```

**It warns; it never overrides.** User-set values always win — the same rule
the per-title database already honours for known-bad entries (#464). Latched
once per content load, so it does not repeat every time the options menu is
opened.

The four suppressors are `virtualjaguar_risc_clock_scale` (any non-1x value),
`virtualjaguar_dram_timing`, `virtualjaguar_gpu_pipeline_timing`, and
`virtualjaguar_blit_memo`. Each of those options now says so in its own
description, and the idle-skip option lists what turns it off.

### Two options that look like suppressors and are not

Worth stating plainly, because both this feature's original issue text and
some earlier notes claimed otherwise before the gate was read and measured:

- **`virtualjaguar_m68k_clock_scale` does NOT disable idle-skip.**
  `m68kClockScalePct` appears in the DSP's budget arithmetic but nowhere in
  the gate. The 68K overclock and idle-skip compose fine.
- **`virtualjaguar_blitter_timing` does not either.** `vjs.blitterTiming`
  never reaches `busArbiter.enabled`; only `virtualjaguar_dram_timing` sets
  that.

Measured on Alien vs Predator, 120 frames:

| Configuration | Idle-skip fires | DSP opcodes/frame |
| --- | ---: | ---: |
| idle-skip alone | 112,398 | 419,949 |
| + `m68k_clock_scale` 3x | 127,525 | 476,343 |
| + `blitter_timing` | 112,316 | 419,776 |
| + `risc_clock_scale` 2x | **0** | 917,593 |
| + `dram_timing` | **0** | 363,628 |

Warning on either of the first two would have false-positived on the most
likely overclock combination there is.

Note also that despite the `risc_` prefix, idle-skip is **DSP-only** — the GPU
idles too and gets nothing from it — and it still ships **off by default**
pending a listening pass, so it has to be enabled by hand.

---

## Core-option text pass (#596)

23 `info` strings rewritten; the rest already read as a single idea and were
left alone. Net −700 characters even after adding the new interaction text
above.

Cut: internal issue numbers, source and doc paths, wire-protocol names, and
boilerplate restated on option after option. Kept every load-bearing caveat —
"takes effect on restart", "off by default", "bit-identical either way",
cartridge-vs-CD applicability, and "report bugs only at 1x".

Largest reductions: Network Link Speed (1090→893), Cart BIOS (682→513), CD Boot
Mode (734→651), Voice Chat (599→481), Widescreen (297→239).

Category descriptions were considered as a home for the repeated
presentation-only boilerplate and **deliberately not used**: `libretro.h`
documents `retro_core_option_v2_category.info` only as "secondary help text",
which is spec intent rather than evidence that any frontend renders it
somewhere a user actually reads. Losing a caveat costs more than duplicating
one.

---

## `make test` exits 0 again (#590)

`test_pertitle_db` cases 4 and 7 asserted against Alien vs Predator's shipped
titledb row expecting a `virtualjaguar_true_color` pair. #551 removed that pair
after a pixel-diffed A/B found it changed 0.0000% of pixels — so the test had
been failing on unmodified `develop` ever since, and every contributor since
has had to know which two failures were "expected".

Fixed at the fixture, not the table: a new `TitleDBSetPairsForTest()` override
(mirroring the existing hooks and negative-pair overrides) lets those two cases
drive off a synthetic row instead of real, evolving shipped data. A test that
breaks every time the database is legitimately edited was the actual defect.

---

## Known limitations

- The warning's **mid-session path** — raising the RISC overclock from the
  options menu during play, rather than having it set at load — is reasoned
  from `content_loaded` being true at that point, not measured. The load-time
  path is verified headlessly.
- Nothing here changes emulation. Everything still open against v3.5.0 stays
  open, including World Tour Racing's second freeze (#589) and the on-device
  A10X capture (#601).

---

## Stats

```
6 files changed, 232 insertions(+), 29 deletions(-)
3 commits since v3.5.0
```

## Maintainers

Joseph Mattiello, with the Virtual Jaguar libretro contributors.
Original Virtual Jaguar by David Raingeard (Potato Emulation) and
James Hammons.
