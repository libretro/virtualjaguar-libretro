# Virtual Jaguar — Settings and Performance Guide

*Accurate as of core version 3.5.1.* Several statements below are pinned to this
release — that the enhancement-hook table ships empty, that the known-bad list
has no rows yet, and that DSP idle-skip is off by default pending a wider
compatibility pass. Check the release notes if you are on a newer build.

The core ships about forty options. Almost none of them need your attention.

This guide is for the person who opened *Quick Menu → Options*, saw a wall of
settings, and wants to know which three actually matter. It covers what the
defaults do, what the visual enhancements cost, what to change when a game runs
slow, and which combinations quietly cancel each other out.

It is not a list of every option — the in-menu descriptions cover those. It is
the map.

---

## 1. Start here: the defaults are already the right answer

Load a game and play it. The out-of-box configuration is:

- **Accurate blitter** — SIMD-accelerated (SSE2 on x86, NEON on ARM). The most
  compatible renderer.
- **Stock clocks** — 68000 at 1x, GPU/DSP at 1x. Real Jaguar speed.
- **All experimental timing models off.**
- **HLE BIOS** for cartridges and for Jaguar CD discs — the fastest and most
  broadly compatible boot path.
- **Per-title enhancement defaults on** — about twenty recognized games
  automatically get a visual upgrade that was verified safe for them.
- **Crash Detect on** — a cheap watchdog that writes a one-line diagnosis to the
  RetroArch log if a game hangs. Leave it on; it is what makes your bug report
  useful.

If a game boots and plays, you are done. Stop here.

### Options you can safely ignore forever

| Category | Ignore unless… |
|---|---|
| **Network Link** (`netlink*`, `uart_device`, `voice_chat*`) | you are playing Doom, AirCars, BattleSphere Gold or Ultra Vortek against another person. See `docs/netlink-user-guide.md`. |
| **Input Port 1 / Input Port 2** (the ~70 remap rows) | you need Numpad 7/8/9/`*`/`#` remapped, which RetroArch's own Controls menu cannot do. |
| **Controller Type** | you are playing Tempest 2000 with a spinner, White Men Can't Jump / NBA Jam T.E. with a Team Tap, BattleSphere with the paddle ADC, or Balloons with a light gun. See `docs/input-devices-user-guide.md`. |
| **Diagnostics** (`cd_trace`, `texture_dump`, `texdump_16bpp`) | you are filing a bug report or authoring a texture pack. |
| **Timing models** (`dram_timing`, `gpu_pipeline_timing`, `blitter_timing`) | you are investigating accuracy, not playing. They only cost performance. |
| **`enhancement_hooks`** | nothing — the patch table currently ships **empty**. The switch exists; there is nothing behind it yet. |
| **`blit_memo`** | you are testing it. It is a prototype, applies to no title by default, and refuses CD content. |

Four settings are worth knowing about: **Blitter**, **Internal Resolution**,
**DSP Idle-Loop Fast-Forward**, and — for CD games only — **CD Boot Mode**.
Everything below is about those.

---

## 2. Per-title enhancement defaults

**Per-Title Enhancement Defaults** (`virtualjaguar_pertitle_defaults`, default
**enabled**) lets the core recognize a cartridge by its CRC32 and pre-set
options that were verified to look better on that specific game.

Three rules make this safe:

1. **Your choice always wins.** A preset is only applied to an option you have
   left at its shipped default. Change the option yourself — to anything,
   including back to the default value by hand — and the preset stops touching
   it.
2. **Everything in the table is presentation-only** (see §3). No preset changes
   emulated state, clock speed, or timing.
3. **A known-bad value is refused, and a warning is logged.** The database also
   carries a negative list. If a preset would set something known to break that
   title, the core declines and logs it. If *you* set that value explicitly, the
   core honors you anyway — and logs `[titledb] … is known-bad for this title
   (explicit user choice honored)`. (The negative list currently ships with zero
   rows; the mechanism is in place ahead of the data.)

To turn the whole thing off, set **Per-Title Enhancement Defaults** to
*disabled*. Every title then runs stock.

Everything the core does per title is logged with a `[titledb]` prefix, so the
RetroArch log always tells you exactly what was applied.

### Titles that configure themselves

Twenty-one entries as of v3.5.0. All of them set internal resolution, true
color, or both — nothing else.

| Title | Internal Resolution | True Color |
|---|---|---|
| Alien vs Predator | 2x | — |
| Cybermorph (both revisions) | — | on |
| Air Cars | — | on |
| Doom (retail) | 2x | — |
| Doom EX / Doom II EX (9 homebrew variants) | 2x | — |
| Hover Strike | 2x | — |
| I-War | 2x | on |
| Kasumi Ninja | — | on |
| Missile Command 3D | 2x | — |
| Skyhammer | 2x | on |
| Tempest 2000 | — | on |
| Towers II | 2x | — |

Note what is **not** in the table: no preset overclocks anything, and no preset
enables blit memoization. If you see a game running faster or slower than
stock, it is not the title database.

**A separate switch, easy to confuse:** *Per-Title Enhancement **Hooks***
(`virtualjaguar_enhancement_hooks`, default **disabled**) is a different
mechanism — it applies per-game **byte patches to the cartridge image** for
game-side bugs that no option can express. Cartridges only, applied at load,
restart to change. Each patch verifies the bytes it expects before writing
anything, so it can never corrupt a dump it was not written for. **The patch
table currently ships empty**, so this switch does nothing today.

---

## 3. Enhancements (visual, presentation-only)

Four options change how the picture looks without changing the machine:

- **Internal Resolution** (`internal_resolution`) — `1x` / `2x`. Renders
  internally at a multiple of native resolution. **Restart required**: applied
  when content is loaded.
- **True Color** (`true_color`) — renders Gouraud-shaded pixels at full
  precision to reduce banding in 3D games. CRY 16bpp video modes only.
  Combines with Internal Resolution.
- **Texture Replacement** (`texture_replace`) — presents community texture-pack
  art in place of the game's own blitter tiles. Only appears in the menu when a
  pack directory exists for the loaded game. See §3.1.
- **Widescreen** (`widescreen`) — reports a 16:9 aspect ratio to the frontend
  instead of 4:3. This is a cosmetic stretch and nothing more; the Jaguar has no
  wider display mode and the emulated framebuffer is identical either way.
  Global only — there is no per-title widescreen preset, because no corpus
  evidence exists for which games look acceptable stretched.

**The reassuring part, said once:** all four are *presentation-only*. The
emulated Jaguar — its RAM, its registers, the framebuffer the game itself sees —
is bit-identical whether these are on or off. Save states made with them on load
correctly with them off. Netplay peers with different settings stay in sync.
Run-ahead is unaffected. You can turn them on and off freely without risking a
save.

**What they cost.** Internal Resolution and True Color are the only two
enhancements with a real performance price, and it is not small: **2x + true
color together** were measured at roughly **30% of frame time** on Alien vs
Predator on a desktop host. Note AvP's shipped preset is **2x only** — #551
dropped true color from it after a pixel-diff found it changed 0.0000% of
pixels — so disabling AvP's preset recovers the 2x share of that figure, not
all of it. On a slow device these are still the first things to turn off; see
§4.

### 3.1 Texture packs

Texture packs are art assets from the community, not something the core
generates.

To **use** one: put it in `<system dir>/vj_texpacks/<cart CRC32>/` and enable
*Texture Replacement*. No restart needed. The option only appears once a pack
directory exists.

To **author** one: enable *Texture Dump Mode* (Diagnostics), play through the
game, and the core writes every unique blitter source tile to
`<system dir>/vj_texdump/<cart CRC32>/` as a PNG plus a manifest row. Redraw
each tile keeping the same pixel dimensions (or an exact N× multiple if the pack
targets N× internal resolution), save under the same filename into the
`vj_texpacks` directory, and enable replacement. A pack pixel with alpha below
128 means "keep the original pixel."

Known limits: indexed sources of 8bpp or less are not yet presented (those tiles
fall through to stock pixels), and GPU-computed surfaces — Doom's texture mapper,
for example — are out of scope entirely. Only 8-bit-depth PNGs are accepted.

Full detail: `docs/texture-dump.md`.

---

## 4. Performance tuning

### 4.1 Where the frame time actually goes

Profiling (`docs/perf-audit-2026-08.md`, `docs/op-perf-profile.md`; host arm64,
mid-game save states) found the cost concentrated in one place:

| Subsystem | Share of frame time |
|---|---|
| **DSP** | **33–67%**, and it is the top cost on every title measured — **50–67%** on the DSP-heavy ones (Iron Soldier 67%, Alien vs Predator 63%), ~33% on Skyhammer |
| **GPU** | large and title-dependent — e.g. ~24% on Skyhammer |
| Blitter (fast) | 5–17% |
| Blitter (accurate) | up to ~34% on Alien vs Predator |
| Object Processor | ~1% |
| 68000 | 0.7–2.6% |

The striking finding: **the DSP is parked in a 3–5 instruction wait loop for
90–99.7% of every frame** on the titles measured (Iron Soldier 99.7%, Doom 90%,
Alien vs Predator 74%) — while the emulator still interprets **416k–590k
opcodes per frame** doing it (Iron Soldier 590k, AvP 532k, Doom 461k, niccc
417k). Note those counts *exceed* what real silicon could retire in a frame:
26.59 MHz over a 60.05 Hz field is ~443k cycles, and no Jaguar RISC instruction
takes less than one. The emulated DSP outruns the hardware because pipeline
hazards are not modelled ([#313](https://github.com/libretro/virtualjaguar-libretro/issues/313))
— which is the same root cause behind titles that run too fast. So this is
interpreter work being done, not a hardware cycle budget being filled, and
that is precisely why skipping it is free. The GPU does the same thing on
some titles (Alien vs Predator 80%, jagniccc 73%).

Two consequences for you:

- **The 68000 and the Object Processor are not your problem.** They are noise.
- **The single biggest available saving is not making the emulator faster — it
  is not emulating the waiting.** That is what idle-skip does.

*(All figures above are host profiles on desktop arm64, not device measurements.
See §4.5.)*

### 4.2 What to try first when a game runs slow

In order. Stop as soon as it is fast enough.

1. **Turn off Per-Title Enhancement Defaults.** If your game is in the §2 table,
   it is rendering at 2x and/or full-precision Gouraud right now. This alone is
   worth ~30% of the frame on Alien vs Predator. On a low-end device this is
   step one, not step three.
2. **Set Internal Resolution to 1x and True Color off** — the manual version of
   step 1, if you want to keep per-title defaults for other games.
3. **Turn on DSP Idle-Loop Fast-Forward** (`risc_idle_skip`). See §4.3 — this is
   the biggest lever and it is **off by default**, so nobody gets it by accident.
4. **Switch Blitter to Fast.** Measured 1.21× faster typical (Iron Soldier), up
   to 1.58× on the title that gains most (Skyhammer). It breaks some games —
   wireframe artifacts and missing geometry are the usual symptom — so switch
   back the moment anything looks wrong.
5. **Only then consider overclocking**, and read §4.4 first, because it can make
   things worse.

Note what is *not* on this list: there is no "frame skip" option and no internal
speed limiter. Pacing is entirely your frontend's job (§4.6).

### 4.3 DSP Idle-Loop Fast-Forward — the big lever, and the trap

**What it does.** When the DSP is spinning in a provably redundant wait loop, the
core computes where that loop would have ended and jumps there instead of
interpreting every iteration. It is exact, not an approximation: registers,
flags, cycles charged and instruction count all end up precisely where
interpretation would have left them. Save states, run-ahead and netplay are
unaffected. The saving reported with the feature in the v3.5.0 release notes is
**66–87% fewer DSP opcodes interpreted per frame** on the titles A/B'd. (That
figure comes from the shipped A/B, not from the profiling documents cited in
§4.1 — those establish the 50–67% and 90–99.7% numbers, not this one.)

**It ships off by default.** It was verified byte-identical on framebuffer,
audio and periodic save-state digest across a nine-title corpus including two CD
games — but nine titles is not a ~200-title library, and a silent audio or video
divergence nobody can attribute is a worse failure than an opt-in toggle. So it
waits one release cycle. **If you are chasing performance, you have to turn this
on yourself.** If a title looks or sounds wrong with it on, turn it off and
please report it.

**It is DSP-only, despite the option key.** The key is `risc_idle_skip`, but only
the DSP has the fast-forward path. The GPU spins in idle loops too and this
option does nothing about that.

#### The compounding trap

**Idle-skip silently disables itself** when any of the following is in effect:

| Setting | Value that disables idle-skip |
|---|---|
| **RISC (GPU/DSP) Clock Scale** | anything other than `1x` |
| **DRAM Timing** | enabled |
| **GPU Pipeline Timing** | enabled |
| **Blit Memoization** | enabled or verify |

It stays active under **M68K Clock Scale** at any value, and under **Blitter Bus
Timing**. (Verified against `src/jerry/dsp.c`, `DSPExec()`.)

Since **v3.5.1** the core says so: enable idle-skip alongside any of the above
and one `[perf]` line lands in your frontend log naming the exact setting that
is suppressing it. It warns only — your settings are always honoured, never
overridden. Before v3.5.1 this happened silently. So:

> **Overclocking the RISC to make a slow game faster can make it slower.**
> Setting *RISC Clock Scale* to `2x` gives the GPU and DSP twice the cycles —
> and simultaneously forfeits a 66–87% reduction in DSP work. On a
> DSP-bound title you have traded away the bigger win to buy the smaller one.

**The rule that follows from the profile data:**

- **DSP-bound title** (Iron Soldier is the extreme case: 67% of frame in the
  DSP, 99.7% of it spinning) → turn idle-skip **on**, leave RISC clock at **1x**.
  Overclocking here buys cycles the game does not need and costs you the skip.
- **GPU-bound title** → RISC overclock may genuinely help, and the idle-skip
  loss may be acceptable. Try each on its own, one at a time, and compare.
- **Never both.** They are mutually exclusive by construction, not by
  preference.

### 4.4 Overclocking

Two independent scales, both under **Timing**:

| Option | Values | Scales |
|---|---|---|
| `m68k_clock_scale` | 0.5x, **1x**, 1.5x, 2x, 3x | the 68000 (~13.3 MHz stock) |
| `risc_clock_scale` | 0.5x, **1x**, 1.5x, 2x | the GPU and DSP (~26.6 MHz stock) |

Timers, audio pacing and bus costs stay at stock speed in both cases, so nothing
pitch-shifts and music does not speed up.

**These are enhancements, not accuracy fixes.** They can smooth framerate-limited
games, and they can break titles that depend on stock timing. If you file a bug,
reproduce it at 1x first — a bug report from an overclocked session cannot be
acted on.

**Known bad cases** (all reported against overclock; status per row):

| Title | Setting | Symptom | Status |
|---|---|---|---|
| Defender 2000 | `m68k_clock_scale = 3x` | hangs at level start (GPU runs away into a data buffer) | Root-caused to a GPU task-dispatcher race, **not fixed** (#460). **2x and below are clean** — drop to 2x. |
| Cybermorph | RISC overclock | Codex-level crash, erratic ship movement | **Reported but unconfirmed** — not reproducible headlessly, needs artifacts (#463). Treat as a caution, not an established fact. |
| Club Drive | `risc_clock_scale = 2x` | crash | **Fixed** in v3.5.0 (#565). |

If an overclocked game misbehaves, the experimental timing models (§5) sometimes
help — but note that two of them disable idle-skip, so you may be stacking costs.

### 4.5 Low-end platforms

Honest position first: **no on-device profile has been captured for the Apple TV
/ A10X class yet.** Issue #601 tracks that capture (its parent epic #509 closed
once every other child shipped). Every number
in §4.1 is a desktop host profile. Anyone quoting per-subsystem device
percentages for tvOS is quoting a projection.

What *is* known and actionable:

- **Raspberry Pi and other ARM Linux / Android builds.** v3.5.0 fixed a real bug
  where cross-compiled ARM64 Linux binaries shipped the *scalar* blitter instead
  of NEON (#560), and added per-SoC compiler tuning for the nine Raspberry Pi
  targets. Make sure you are on 3.5.0 or newer — this is a large, free win that
  costs you no settings.
- **Whatever the device, the first move is the same:** turn off per-title
  enhancement defaults (§4.2 step 1). The 2x/true-color presets were chosen on
  desktop-class hardware; on a Pi or an Apple TV they are the single most
  expensive thing running.
- **Then turn on idle-skip.** It removes work rather than doing it faster, which
  is exactly the shape of win a weak CPU needs.
- **If audio still crackles, enable Frameskip** (`frameskip`, default
  **disabled**). When the frontend reports its audio buffer draining, the core
  skips *presenting* a frame — the scanline conversion and framebuffer copy —
  while the emulated machine still runs that frame in full. `auto` skips only
  when the frontend says an under-run is imminent; `auto_threshold_15/30/45`
  skip earlier, whenever buffer occupancy falls below that percentage (45 =
  most aggressive). **Frameskip Maximum** (`frameskip_max`, 1–4, default 3)
  caps consecutive skips so the picture never freezes. Presentation-only:
  save states, run-ahead and netplay are unaffected, and with it disabled the
  output is bit-identical to previous releases. Needs a frontend new enough to
  support audio buffer status reporting (RetroArch 1.9.1+); on older frontends
  every value behaves as disabled.
- **Fast blitter last**, because it costs correctness.
- **Do not overclock on a device that is already struggling.** It asks for more
  work, not less — and it takes idle-skip away.

### 4.6 Pacing, fast-forward and VRR

**The core has no internal frame limiter.** It never sleeps, never blocks inside
`retro_run()`, and does not register a frame-time callback. Speed and pacing
are entirely your frontend's responsibility. (The one audio callback it can
register — the buffer-status report driving `frameskip`, §4.5 — only sheds
presentation work; it never slows or blocks the run loop.)

Practical consequences:

- The core advertises **60.05445 fps / 48043.6 Hz** (NTSC) and **50.08013 fps /
  48076.9 Hz** (PAL). These are the Jaguar's real field rates, not 60/50, and
  they are derived rather than hard-coded: a non-interlaced field is 524
  halflines NTSC / 624 PAL (`VJ_HALFLINES_PER_FIELD_*` in `src/core/jaguar.h`),
  so the rate falls out as `1e6 / (halflines × halfline_µs)`. The sample rate
  follows from it in `retro_get_system_av_info()` — the core hands the frontend
  a fixed batch of pairs exactly once per field, so the true output rate *is*
  pairs × field rate. Advertising a flat 48000 against the corrected fps would
  over-deliver by ~0.09% forever and slowly drain or overfill the frontend's
  audio buffer.
- **Fast-forward doing nothing is usually a RetroArch setting**, not a core bug.
  Check *Frame Throttle → Fast-Forward Ratio* (1.0× means no speed-up),
  `vrr_runloop_enable`, and that audio sync goes non-blocking during
  fast-forward.
- **Fast-forward is capped by the core's own throughput.** If a heavy title
  already takes close to 16.7 ms per frame on your hardware, holding
  fast-forward will produce little or no visible speed-up — everything is
  working as designed, there is simply no headroom.
- **A backgrounded or occluded window gets throttled by the OS.** The same core
  and config measured 120.8 fps foreground and 59.5 fps backgrounded on one
  macOS machine. That looks exactly like a broken fast-forward and is not.

Detail: `docs/frontend-pacing.md`.

---

## 5. Accuracy and timing models

Three options under **Timing**, all **off by default**, all marked experimental
and all still being calibrated. They exist because the emulated machine is, in
specific ways, *too fast* — the GPU finishes renders 2–4× faster than silicon,
and blits complete in zero time. Games that pace themselves on render or blit
completion (Doom's menus and demo, Hover Strike) therefore run too fast.

| Option | Models | Note |
|---|---|---|
| `dram_timing` | realistic DRAM access cost for the GPU and 68000 once they leave their local buses | Each processor pays only its own costs, so relative CPU/GPU timing is preserved. **Disables idle-skip.** |
| `gpu_pipeline_timing` | the GPU's real instruction costs — single external-memory gateway, register scoreboard, ALU interlocks | **Disables idle-skip.** |
| `blitter_timing` | charges the 68000 the bus time each blit really takes (on hardware the blitter freezes the cacheless 68000 while it runs) | Does **not** disable idle-skip. |

**These are for accuracy investigation, not for everyday play.** They all cost
performance, by design — they add work the emulator was previously skipping.
Turn them on if you are comparing against real hardware, or if an overclocked
game misbehaves and you want to see whether restoring realistic timing settles
it. Otherwise leave them alone.

---

## 6. Jaguar CD

CD discs ignore the cartridge BIOS setting entirely. **CD Boot Mode**
(`cd_boot_mode`, **restart required**) decides everything:

| Value | What it does |
|---|---|
| **HLE** (default) | The core emulates the CD BIOS services directly and runs with the console boot ROM off. Fastest and most broadly compatible. Start here. |
| **Real BIOS** | Runs an actual CD BIOS with the boot ROM on. More faithful; verified clean across all 5 tested FMV titles (Dragon's Lair, Space Ace, BrainDead 13, Blue Lightning, Highlander) in 15,000-frame probes -- see `docs/fmv-bios-verify-notes.md`. |
| **Auto** | Currently identical to Real BIOS. |

No files are required for either. Both CD BIOS images are built into the core; a
CD BIOS ROM in your system directory is preferred over the embedded one when
present, and **CD BIOS Type** (`cd_bios_type`, Retail / Developer) picks which
wins. The Developer BIOS applies less strict disc checks and can boot images the
Retail BIOS refuses. If a real-BIOS mode is chosen and no CD BIOS can be staged
at all, the core falls back to HLE rather than failing.

**Audio-only (Red Book) CDs are a special case.** HLE synthesizes its boot
stub from session-2 game data; a plain music CD has no such session, so HLE
can never produce a boot for one. The core detects this from the disc's
session layout (one session, versus two on every Jaguar CD data disc) and
always routes that specific disc through the real CD BIOS -- whose own
player front-end includes the Virtual Light Machine visualizer -- regardless
of this setting. Every other disc, including the known-damaged CDI rips,
is unaffected and still uses whichever mode you picked.

**When to switch to Real BIOS.** HLE is more compatible overall, but it is a
reimplementation, and a title occasionally trips over something HLE gets wrong
that the real BIOS handles. The documented case is **World Tour Racing**: it
freezes in HLE mode around frame 660 during FMV playback, while real-BIOS mode
dips at the same point and then recovers into sustained motion (#589, open;
follows the separate first-freeze fix in #564). Note that real BIOS is a
*workaround, not a fix* — that title still cannot progress fully in either
mode, and on an incomplete disc rip neither mode boots at all.

If a CD game hangs or an FMV stops mid-playback, trying Real BIOS is worth a
minute before you file a report — and say which modes you tried, because "fails
in both" and "works in one" are different bugs.

**CD Read Speed** (`cd_read_speed`, HLE boot mode only):

| Value | Effect |
|---|---|
| 1x | 150 KB/s |
| **2x** (default) | 300 KB/s — matches the real drive, hardware-accurate |
| 4x / 8x | shorter loads, increasing risk |
| Instant | each read completes in one tick; most likely to hang |

Higher speeds shorten load times but break timing-sensitive titles — some games
pace code overlays, music cues and load handshakes off the drive rate. If a CD
game hangs after you raised this, put it back to 2x before anything else.
Real-BIOS mode always uses the accurate rate regardless of this setting. The
speed is applied per read, so a transfer already in flight keeps the speed it
started with.

**Memory Track** (`memory_track`, default **enabled**, restart required)
emulates the Memory Track save cartridge alongside the CD unit. CD games detect
it and save settings, progress and high scores to its 128 KB NVRAM. Leave it on
unless you specifically want games to warn that progress cannot be saved.

---

## 7. Options that need a restart

Changing these mid-game does nothing visible. Reload the content.

| Option | Menu name |
|---|---|
| `internal_resolution` | Internal Resolution |
| `pal` | PAL |
| `bios_type` | Cart BIOS Type — *which* boot ROM (Series K / Model M / Custom) |
| `cd_bios_type` | CD BIOS Type |
| `cd_boot_mode` | CD Boot Mode |
| `memory_track` | Memory Track |
| `jgd` | Jaguar GameDrive |
| `enhancement_hooks` | Per-Title Enhancement Hooks |

Everything else — blitter mode, true color, texture replacement, clock scales,
timing models, idle-skip, CD read speed — takes effect immediately.

---

## 8. Troubleshooting

| Symptom | Likely cause | Try this |
|---|---|---|
| **Game runs too fast** (menus fly past, input feels doubled) | The emulated GPU finishes renders 2–4× faster than silicon, so loops paced on render or blit completion outrun hardware. This is a known accuracy gap, not your configuration. | Turn on `gpu_pipeline_timing` and/or `blitter_timing`. Both cost performance and are still being calibrated. Also confirm you have not left a clock scale above 1x. |
| **Game runs slow** | See §4.2. | Per-title defaults off → 1x / true color off → idle-skip on → fast blitter. |
| **Fast-forward does nothing** | Frontend setting, or no headroom left. | Check RetroArch's Fast-Forward Ratio and audio sync. If the title already runs near 16.7 ms/frame, there is nothing to gain. Also check the window is focused — a backgrounded window gets OS-throttled. |
| **Audio crackles or drops out** | Usually frontend audio buffering, or the device cannot render every frame in time. | Raise your frontend's audio latency first. On hardware that is genuinely too slow, set `frameskip` = auto (§4.5) — it drops presentation, not emulation, when the buffer drains. If it started after you enabled `risc_idle_skip`, turn it back off and please report it — that is exactly the feedback the opt-in period exists for. |
| **Audio pitch changed after overclocking** | Should not happen — timers and audio pacing stay at stock speed under both clock scales. | Report it. |
| **Black screen on a CD game** | Boot path, or a bad rip. | Switch `cd_boot_mode` to Real BIOS and restart. If that fails too, try `cd_bios_type` = Developer. Check the log for `[CD-BOOTSTUB]` — "zero-filled" or "magic mismatch" means the image itself is an incomplete rip, and no setting will fix it. |
| **CD game hangs after loading got faster** | `cd_read_speed` above 2x. | Set it back to 2x (accurate). |
| **FMV freezes mid-playback** | Known HLE gap on at least one title. | Try `cd_boot_mode` = Real BIOS. For World Tour Racing specifically this is a known open bug (#589). |
| **CD game says it cannot save** | Memory Track disabled. | Set `memory_track` = enabled and restart. |
| **Controller not detected by the game** | The game wants a peripheral you have not selected, or a bank-switching device that has not been "woken". | Set the port's Controller Type explicitly. Bank-switching devices (analog, driving, 6D) present as a plain RetroPad until an axis actually moves, so deflect the stick before the game probes. Tempest 2000's rotary needs an in-game unlock (Option on pad 1 at SELECT GAME TYPE, then Pause on both pads). See `docs/input-devices-user-guide.md`. |
| **Extra Team Tap pads do nothing** | They live on other RetroArch ports. | A Team Tap on port 1 puts players on RetroArch ports 1, 3, 4, 5; on port 2, ports 2, 6, 7, 8. Remap the extra pads from RetroArch's own Controls menu — the core's per-port remap rows apply to socket 0 only. |
| **Numpad 7/8/9/`*`/`#` cannot be remapped** | RetroArch's Controls menu limitation. | Enable `alt_inputs` (Enable Core Options Remapping) and use the core's own Input Port rows. Delete any existing remap file first — the two systems conflict. |
| **Wireframe / missing geometry** | Fast blitter. | Set Blitter back to Accurate. |
| **Homebrew hangs at boot** | GameDrive-locked image. | Set `jgd` = Enabled (force) and restart. |
| **A game freezes and you want to report it** | — | Leave Crash Detect on (or set it to verbose), reproduce, and attach the RetroArch log. It names the failing subsystem: `gpu_pc_escape`, `dsp_pc_escape`, `gpu_wedge`, `dsp_wedge`, `video_stall`, `cd_seek_wedge`. For CD problems, also enable `cd_trace`. |

### Before you file a bug

- Set **both clock scales back to 1x**. Overclocked reports cannot be acted on.
- Turn **off** `risc_idle_skip`, `blit_memo`, the fast blitter, and all three
  timing models — then confirm the problem still happens.
- Say whether per-title defaults were on, and whether the title is in the §2
  table.
- Attach the RetroArch log. The `[titledb]` and crash-watchdog lines are what
  make a report actionable.

---

## Quick reference

| Option key | Default | Change it when… |
|---|---|---|
| `usefastblitter` | Accurate | you need speed and can accept rendering bugs |
| `true_color` | off | banding bothers you (and you have headroom) |
| `internal_resolution` | 1x | you want sharper 3D (restart) |
| `widescreen` | off | you prefer a stretched 16:9 picture |
| `pertitle_defaults` | **on** | you are on slow hardware, or want stock behavior |
| `enhancement_hooks` | off | never, today — the table is empty |
| `blit_memo` | off | you are testing it |
| `crash_detect` | **on** | leave it on |
| `m68k_clock_scale` | 1x | a 68K-bound game stutters (avoid 3x — see #460) |
| `risc_clock_scale` | 1x | a GPU-bound game stutters — **and never with idle-skip** |
| `risc_idle_skip` | **off** | you want the single biggest speed-up available |
| `frameskip` | off | audio crackles on hardware too slow to render every frame |
| `frameskip_max` | 3 | frameskip is on and you want a different smoothness/audio trade |
| `dram_timing` / `gpu_pipeline_timing` / `blitter_timing` | off | accuracy investigation only |
| `bios` (cartridges) | HLE | a cartridge needs the real boot ROM. *Distinct from* `bios_type`, which picks **which** ROM once this is set to Real |
| `cd_boot_mode` | HLE | a CD game hangs or an FMV freezes (restart) |
| `cd_read_speed` | 2x | loads feel slow — and revert if anything hangs |
| `memory_track` | **on** | leave it on |
| `texture_replace` | off | you installed a texture pack |

---

### See also

- `docs/input-devices-user-guide.md` — controllers, mice, spinners, light gun
- `docs/netlink-user-guide.md` — link play and voice chat
- `docs/texture-dump.md` — authoring texture packs
- `docs/frontend-pacing.md` — pacing, fast-forward, VRR
- `docs/enhancement-hooks.md` — the per-title policy in full
- `docs/perf-audit-2026-08.md`, `docs/op-perf-profile.md` — the profiling data
  behind §4
