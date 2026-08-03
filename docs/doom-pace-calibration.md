# Jaguar Doom pace calibration — ground truth

Research record for the "Doom runs too fast" bug (monsters move/attack faster
than on real hardware). No emulator code changes were made for this document.

Source of truth for the game side: the **Jaguar DOOM Source Archive**, released
by Carl Forhan / Songbird Productions in April 2003 under id Software's Limited
Use Software License (`readme.txt`, `license.txt` in the archive). Mirror used:
`https://github.com/Arc0re/jaguardoom` (single `init` commit, tree matches the
Songbird archive: `readme.txt` names Carl Forhan / Songbird Productions and
documents `doom.abs` loading at `$4000` and `jagdoom.wad` at `$840000`, which is
what our ROM does). All `file:line` citations below are into that tree.

---

## 1. Coupling mechanism — what actually sets monster speed

### 1.1 One `gametic` == one rendered frame

`MiniLoop()` is Doom's universal game loop (`d_main.c:253`). Every iteration
does exactly one logic tic and one render:

```
d_main.c:274-280   do { gamevbls += vblsinframe; exit = ticker (); ...
d_main.c:337-340        while (!I_RefreshCompleted ()) ; S_UpdateSounds (); drawer ();
d_main.c:350            } while (!exit);
```

`ticker` for gameplay is `P_Ticker` (`g_game.c:374`, `:453`, `:477`), and
`P_Ticker` is the only place `gametic` is incremented:

```
p_tick.c:293       gametic++;
```

`drawer` is `P_Drawer`, which for the 3D view calls `R_RenderPlayerView()`
(`p_tick.c:470`). So:

> **gametic rate == rendered-frame rate.** There is no separate 35 Hz / 30 Hz
> logic ticker in Jaguar Doom. The observable `gametic`/s that our
> `test/tools/test_doom_ticrate` measures *is* renders per second.

### 1.2 Two different frame gates — and the one the 3D view uses

There are two copies of `I_Update` (the display-list swap + frame gate):

* **68K/C copy**, `jagonly.c:753`, used for the automap, menus and plaques
  (`p_tick.c:463`, `d_main.c` menu paths). Its gate is
  `while (junk-lastticcount < 3)` (`jagonly.c:1000-1003`), i.e. ≥3 VBLs → 19.98 fps.
  The comment above it ("15 fps is the maximum frame rate", `jagonly.c:739-741`)
  is stale relative to its own code.
* **GPU copy**, compiled into the last renderer phase `r_phase9.gas`, used for
  the 3D view. It is byte-for-byte the same routine (it writes `_isrvmode`
  = 3777 = `0xc1 + (7<<9)`, `_readylist_p`, `_lasttics`, `_lastticcount`,
  `_workpage`, `_worklist` — see `r_phase9.gas:576-630`) but its gate constant
  is **2**, not 3:

```
r_phase9.gas:559-563   L56: junk = ticcount
r_phase9.gas:566-578   L57: delta = junk - lastticcount
                            moveq #2,r1 ; cmp r0,r1 ; jump S_LT,(L56)
```

Decoding that jump: `S_LT .ccdef $15 ; PL+NE` (`r_phase9.gas:13`). `$15` =
`0b10101` = N clear (PL) + C clear (CC) + Z clear (NE) — the source comment
abbreviates it to "PL+NE"; the C-clear bit is also set, which does not change
the conclusion. Our GPU core computes `CMP Rn,Rm` as `res = RN - RM` with
`RM = first parameter`, `RN = second parameter` (`src/tom/gpu.c:282-283`,
`src/tom/gpu.c:1415-1419`), so `cmp r0,r1` → `res = 2 - delta`, and the branch
back to L56 is taken while `delta < 2`.

> **The 3D view's hard frame cap is `ticcount - lastticcount >= 2` — one display
> swap per 2 VBLs, i.e. 29.97 fps NTSC.**

`ticcount` is incremented once per video interrupt by the 68K frame ISR
(`init.s:339`, vector installed at `init.s:247`, `VI = a_vde|1` at
`init.s:250-252`). VI matches once per VC cycle, so `ticcount` runs at the field
rate, 59.94 Hz — the same in our core (`src/core/jaguar.c:886-910`: VC counts
half-lines, wraps at `VP+1`, one `TOMSetPendingVideoInt()` per wrap).

This dissolves the apparent contradiction with the stale "15 fps" comment: the
2-VBL gate is the one the shipped 3D renderer runs, and 59.94/2 = **29.97** is
exactly the number our headless demo measurement reports.

### 1.3 Player motion is frame-rate compensated; monsters are not

`vblsinframe` is the engine's variable timestep — the number of VBLs the
previous frame actually took:

```
d_main.c:284-295   if (demoplayback || demorecording) vblsinframe = 4;
                   else { vblsinframe = lasttics; if (vblsinframe > 8) vblsinframe = 8; }
r_phase9.gas:605-612   lasttics = ticcount - lastticcount;   (GPU, per display swap)
```

Everything that reads `vblsinframe` (full list from a tree-wide grep):

| Consumer | Citation | Effect |
|---|---|---|
| Player XY movement | `p_user.c:33-34` — `momx = vblsinframe*(mo->momx>>2)` | wall-clock invariant |
| Player **fast** (running) turn rate | `p_user.c:280-285` — `angleturn = ±((fastangleturn[turnheld]*vblsinframe)<<15)` | wall-clock invariant |
| Weapon (psprite) ticking | `p_pspr.c:847-851` — `ticremainder += vblsinframe; while (ticremainder >= 4) …` | wall-clock invariant, 15 Hz |
| Netgame sync byte | `jagonly.c:1788`, `:1802` | — |
| Debug HUD | `r_main.c:258` | — |

**Nothing else** — and the compensation is not even complete for the player:
the *slow* (walking) turn branch immediately below,
`angleturn = ±angleturn[turnheld]<<17` (`p_user.c:288-292`), has **no**
`vblsinframe` factor, so even the player's walking turn rate scales with the
frame rate. The asymmetry is narrower than "player compensated, monsters not":
it is *translation and running-turn compensated, everything else not*.

In particular `p_base.gas` (the RISC-compiled mobj thinker — it is `.gpu`
dialect but `.org $f1b140`, i.e. it runs on the **DSP**; see §5B.8 —
`_P_XYMovement` at `p_base.gas:345`, `_P_ZMovement` at `p_base.gas:942`) never
references `_vblsinframe`. Monster state timers decrement by a flat 1 per
thinker call:

```
p_base.gas:1315-1334   if (mobj->tics == -1) return;  mobj->tics -= 1; …
```

and `P_RunThinkers` runs once per `P_Ticker`, i.e. once per rendered frame.
Same for the `gametic&N`-gated world effects (`p_ceilng.c:30`, `p_floor.c:170`,
`p_spec.c:613-658`, `p_change.c:103`).

> **Verdict on coupling:** monster translation speed, monster attack/animation
> cadence, projectile speed, and door/lift/light timing are all *directly
> proportional to the rendered frame rate*. The player's translation, running
> turn and weapon cadence are the only things Jaguar Doom compensates — even
> the player's walking turn is not. That is exactly the reported symptom shape:
> "monsters are too fast" while the things the player feels most directly
> (running, strafing, firing) stay correct.

### 1.4 The demo *tic rate* is the wrong observable — the demo *duration* is the right one

During `demoplayback`, `vblsinframe` is **forced to 4** (`d_main.c:286`).
Recorded input is consumed one command per tic (`d_main.c:228` `GetDemoCmd`),
so the demo advances one tic per rendered frame just like gameplay — but with a
fixed 4-VBL timestep regardless of how long the frame actually took.

Consequence: in the demo, *everything* — player included — scales with the
render rate, and the render rate is pinned at the 2-VBL cap on any host fast
enough to hit it. Our OFF-mode measurement of **29.97 tics/s is exactly the cap
(59.94/2)**, i.e. the measurement is *saturated*: it reports 29.97 whether our
68K is 1x, 2x or 10x too fast. It cannot discriminate. The ON-mode measurement
is now **also exactly 29.97** (re-measured 2026-08-03 against the wait-state-
only bus model, `fix/acid-reconciliation`; was 29.67, ~1% below the cap, under
the old double-counted model) — both saturated for practical purposes.

The only reason the earlier 8x-DRAM-self-cost experiment produced a meaningful
15.58 tics/s is that it pushed the machine *below* the cap.

But the same three facts that saturate the *rate* make the demo's **duration**
the single best calibration observable available:

* the demo's input is a recorded command stream consumed one command per tic
  (`d_main.c:228`, `:308`), so the tic count is fixed — 750 and 985 tics for
  Doom's two attract demos (`gametic` runs 0→749 and 0→984; measured, §3.1);
* `vblsinframe` is pinned at 4 (`d_main.c:286`), so the simulation advances
  identically regardless of how long a frame takes;
* therefore **the sequence of rendered images is bit-identical on silicon and
  on any emulator** — same camera path, same monsters, same geometry, same
  per-frame render load, frame for frame.

Only the wall-clock time to get through those tics differs. That makes demo
duration a **scene-matched, load-matched, single-number** ground truth: no
frame-differencing, no capture artefacts, no "was that scene as busy as ours?"
objection. It is the observable §4's verdict and §5's acceptance test are built
on.

> **Wrong observable: demo tic rate (saturated at the 2-field cap — reads 29.97
> whether we are correct or 10x too fast). Right observable: attract-demo
> wall-clock duration, with in-gameplay renders/s as a corroborating
> cross-check.**

---

## 2. Real-hardware pace — measured

No published frame-time analysis of Jaguar Doom exists (searched; reviews only
say "slows down when busy"). So this was measured directly from real-hardware
captures, using the observable §1 identified: **unique rendered images per
second**, and the distribution of gaps between them in 59.94 Hz field units
(that gap *is* Doom's own `lasttics`).

Method (`framediff.py`, reproduced in the appendix): `yt-dlp` the clip →
`ffmpeg` crop to the 3D viewport (status bar excluded) → downscale to
160x100 grey → mean-absolute-difference between consecutive capture frames →
Otsu threshold on the diff histogram → count new-image events and their gaps.

```
yt-dlp -f 298 -o jagdoom60.mp4 https://www.youtube.com/watch?v=YxQgwE4B1eE
python3 framediff.py jagdoom60.mp4 640:400:320:110 60 20 190
yt-dlp -f 135 -o jagdoom30.mp4 https://www.youtube.com/watch?v=ZJa4CHi3H7I
python3 framediff.py jagdoom30.mp4 580:355:35:14 29.97 40 300
```

### The load-bearing proof: gap parity

Before any authenticity argument from titles or overlays, the **parity of the
gap histogram settles it**. On a 59.94 Hz timeline, a source rendering at the
engine's 29.97 fps cap can only ever produce **even** gaps — one new image
every 2 fields, exactly. A 30 fps re-encode, an emulator recording pinned at
the cap (which is what our core produces, §3), or any 2-field-quantised source
is *incapable* of producing an odd gap as its mode.

Clip A's mode is **gap 3, at 77.6 %**, with gap 2 at 0.5 %. That is only
producible by a source whose frames genuinely hold for three fields. No
re-encode, deinterlace or capture artefact converts a 2-field cadence into a
77.6 % 3-field cadence. Clip B, captured at 29.97 Hz on a different chain,
shows the aliased signature of the same 3-field cadence (36.2 % gap 1 +
53.6 % gap 2 — 19.98 fps sampled at 29.97 Hz alternates 1,2,2), which a 30 fps
source could not produce either (it would be a flat gap 1).

The clip titles ("captured using the new RetroTink 2X Multiformat"; "Capture on
real hardware NTSC") and Clip A's on-screen capture-settings overlay are
*consistent* with real silicon, but they are not what the argument rests on.

### Clip A — 60 fps capture, real hardware

`https://www.youtube.com/watch?v=YxQgwE4B1eE` — "Atari Jaguar - DOOM (Level 1)
captured using the new RetroTink 2X Multiformat". 1280x720 **60 fps** upload
(format 298). Segment analysed: t = 20…210 s, level 1 gameplay including
combat. Crop `640:400:320:110` — verified by extracting a still
(`ffmpeg -ss 120 -i jagdoom60.mp4 -frames:v 1 shot.jpg`): the game window sits
at x≈316-966, y≈108-518 with the status bar starting at y≈518, so the crop
covers the 3D viewport and excludes the HUD, the title text and the letterbox.

Diff histogram (strongly bimodal — 7557 held frames near zero, ~2400 changed
frames above 6, almost nothing between 3 and 6):

```
  diff [  0.00,   0.25) : 7557        diff [  6.00,   8.00) :  181
  diff [  0.25,   0.50) :  439        diff [  8.00,  12.00) :  425
  diff [  0.50,   1.00) :  482        diff [ 12.00,  16.00) :  567
  diff [  1.00,   2.00) :  198        diff [ 16.00,  24.00) :  919
  diff [  2.00,   3.00) :   98        diff [ 24.00,  32.00) :  270
  diff [  3.00,   4.00) :   52        diff [ 32.00,    inf) :   56
  diff [  4.00,   6.00) :  155        otsu threshold = 5.95
```

Gap histogram (11400 frames, 2421 new images):

| gap (capture frames = 59.94 Hz fields) | count | share | implied rate |
|---|---|---|---|
| 2 | 11 | **0.5 %** | 29.97 fps |
| **3** | **1878** | **77.6 %** | **19.98 fps** |
| 4 | 326 | 13.5 % | 14.99 fps |
| 5 | 12 | 0.5 % | |
| 6 | 32 | 1.3 % | |
| 7 | 9 | 0.4 % | |
| 8 | 33 | 1.4 % | |
| 9 | 17 | 0.7 % | |
| 11–36 | 70 | 2.9 % | static / paused view |

Per-second unique-frame counts (t = 20…210 s, one number per second):

```
0 0 0 1 0 0 0 0 0 1 0 0 0 0 0 1 7 1 10 11 4 8 6 8 7 8 0 3 11 18 19 18 11 0
7 8 3 5 6 17 20 20 20 15 15 17 20 20 17 20 11 19 11 18 17 18 17 17 6 11 15
15 15 16 20 20 19 20 10 12 8 14 5 8 3 5 18 21 18 20 20 19 19 11 19 18 11 5
9 16 19 8 15 13 5 14 8 7 17 11 8 11 12 6 8 6 7 20 20 20 19 20 20 20 20 18
20 20 20 18 20 13 18 19 20 19 15 17 13 18 20 21 20 20 20 18 20 15 7 8 6 14
19 14 17 9 12 19 15 17 7 17 12 9 19 19 14 20 20 20 20 20 14 16 20 18 19 17
20 20 20 20 20 19 11 14 19 15 16 20 16 19 1 0 0 0 0 0 0 0
```

The ceiling in that timeline is **20–21**, hit repeatedly and never exceeded.
The leading/trailing zeros are the clip's intro/outro cards.

### Clip B — 30 fps capture, different hardware chain

`https://www.youtube.com/watch?v=ZJa4CHi3H7I` — "Doom, Atari Jaguar, Longplay,
Capture on real hardware NTSC" (Atari Jaguar and Lynx Garage), 654x480
**29.97 fps**. Segment t = 40…340 s. Crop `580:355:35:14` — verified from a
still at t = 90 s: full-screen output, game window x≈30-618, y≈12-372, status
bar below.

At a 29.97 Hz sampling rate: 19.98 fps content aliases to alternating gaps of
1 and 2 (mean 1.5); 14.99 fps content gives a flat gap of 2; 29.97 fps content
gives a flat gap of 1.

```
  diff [  0.00,   0.25) : 2800        diff [  6.00,   8.00) : 1288
  diff [  0.25,   0.50) :  507        diff [  8.00,  12.00) : 1818
  diff [  0.50,   1.00) :  308        diff [ 12.00,  16.00) :  547
  diff [  1.00,   2.00) :  245        diff [ 16.00,  24.00) :  108
  diff [  2.00,   3.00) :  204        diff [ 24.00,  32.00) :   11
  diff [  3.00,   4.00) :  220        otsu threshold = 4.95
  diff [  4.00,   6.00) :  935
```

| gap | count | share |
|---|---|---|
| 1 | 1569 | 36.2 % |
| 2 | 2321 | 53.6 % |
| 3 | 158 | 3.6 % |
| 4 | 84 | 1.9 % |
| 5–24 | ~200 | 4.7 % |

Mean over the non-static portion (gap ≤ 3) = 1.65 capture frames → **18.2 fps**.
Peak per-second unique-frame count across the whole 300 s: **20–21**, never
higher.

### Bias direction

Frame-differencing can only **under**-count: an identical (or near-identical)
rendered image is invisible to it. Note the sensitivity is *not* symmetric with
§3's measurement of our own core — there, consecutive identical frames diff to
exactly 0 and the threshold can be 0.05, whereas YouTube re-encoding forces a
threshold of ~5, which discards small genuine changes. So §2's numbers are
biased low **more** than §3's are. That asymmetry does not weaken the verdict:
it makes silicon look *faster* than it is, and silicon is already the slower
side. The comparison is anchored by the parity argument above and by §3's
demo-duration measurement, which involves no differencing at all.

> **Real hardware, Doom level 1: ~20 renders/s typical (3 fields per frame),
> ~15 renders/s in heavier scenes (4 fields), ceiling 20–21/s across 490 s of
> footage from two independent captures. It effectively never reaches the
> engine's 30 fps cap.**

By §1.3 this is also the monster-think rate: **~20 monster tics/s on silicon.**

### Attract demo not present in either clip

Both clips were checked for the attract demo, which would have given the
scene-matched duration ground truth directly. Clip A opens on a capture-settings
test card (blue window, t≈0-20 s) and then goes straight to gameplay. Clip B
opens *already in gameplay* (t = 0 shows the HUD at AREA 11, HEALTH 100 %) —
it is a mid-session longplay, not a power-on recording. Neither contains a
title screen or attract demo, so the demo duration on silicon remains the one
open number (see §5, "Open cross-check").

---

## 3. Our numbers

Measured with a scratch harness (`doom_renderrate.c`, appendix; built against
`test/harness/harness.c`, nothing added to the repo). Core:
`virtualjaguar_libretro.dylib` @ `62e8f98-dirty`, ROM
`test/roms/private/ROMS/Doom - Evil Unleashed (1994).jag`, NTSC, real BIOS.

> **Recalibrated 2026-08-03 @ `9222148`** against `fix/acid-reconciliation`'s
> wait-state-only 68K bus model (see `src/core/bus_arbiter.c`:
> `bus_arbiter_m68k_access()` now charges the 68K only the excess of an
> access over its own 8-sysclk bus cycle, not the absolute access cost —
> DRAM/IO now cost the 68K ~0, only cart ROM adds 1 cycle/access). The
> original numbers below (from `62e8f98-dirty`) measured `dram_timing`
> **on** against the old, double-counted per-access charge in addition to
> the OP-fetch/DRAM-refresh occupancy this doc is actually about; that
> extra per-access charge is what produced the +2.0 % / +0.7 % deltas.
> With it corrected, the ON numbers are reproduced below and the deltas
> collapse to within run-to-run noise (§3.1). The occupancy-model
> mechanism itself (OP fetch + refresh, `HalflineCallback`,
> `src/core/jaguar.c:914-937`) is untouched by this fix and still applies
> under `dram_timing=on` — it was simply never the dominant term in the
> old ON measurement.

### 3.1 Primary: attract-demo duration

Measured by watching `gametic` at `$04080C` go from its first non-zero value to
its last before reset (`SHOW_RESETS=1`, no `--press` — the demos play on their
own), over a 9000-frame run that covers two full attract cycles:

| demo | tics | dram_timing **off** | dram_timing **on** | delta |
|---|---|---|---|---|
| first  | 749 | **27.59 s / 27.69 s** | **27.69 s / 27.61 s** | **+0.0 %** |
| second | 984 | **34.98 s / 34.97 s** | **35.05 s / 35.00 s** | **+0.1 %** |

(Two instances of each demo per run; both listed to show run-to-run spread is
under 0.2 %.) Equivalent render rates: 27.1 → 27.1 renders/s (first demo,
within noise), 28.1 → 28.1 (second, within noise).

**Superseded finding, kept for record:** under the old, double-counted
per-access 68K bus model, this row read off `27.59 s / 27.69 s` →
`28.18 s / 28.13 s` (+2.0 %) and `34.98 s / 34.97 s` → `35.22 s / 35.24 s`
(+0.7 %), and the write-up argued the asymmetry between the two demos was a
real signal that a demo leaving the 68K with more slack absorbed more of the
occupancy charge without lengthening. That reasoning doesn't survive the
wait-state-only bus fix (`fix/acid-reconciliation`, `src/core/bus_arbiter.c`):
most of the old ON delta was the absolute-cost double-count on ordinary DRAM
fetches/stores, which the two demos' differing 68K workloads happened to
scale differently — not a signal about the OP-fetch/refresh occupancy model,
which is unaffected by this fix and remains active under `dram_timing=on`.
With the double-count gone, both demos land within the ~0.2 % run-to-run
spread of each other, off vs on.

```
SHOW_RESETS=1 ANALYZE_FROM=600 VJ_EXPECT_BUILD=$(./scripts/build-id.sh) \
  ./doom_renderrate ./virtualjaguar_libretro.dylib "<Doom rom>" \
  --frames 9000 --quiet [--option virtualjaguar_dram_timing=enabled]
```

> **With the 68K bus model corrected to wait-states-only, the OP-fetch +
> DRAM-refresh occupancy model lengthens the attract demo by only +0.0 % /
> +0.1 % — within run-to-run noise.** (Previously measured +2.0 % / +0.7 %
> against the double-counted model; see the superseded-finding note above.)
> This is measured on the load-matched, scene-matched observable, not on the
> saturated tic rate, and the conclusion is unchanged from before the fix:
> **the 68000 was never the frame-time critical path** (§4) — it is now
> measurably even less of one.

### 3.2 Corroborating: unique renders/s

Same runs, frame-differenced with the §2 method (threshold 0.05 — our
framebuffer is not re-encoded, so held frames diff to exactly 0):

| Scenario | option | gap-2 share | renders/s (first→last new image) |
|---|---|---|---|
| **Attract demo** (walks the level, fights) | off | **98.7 %** | 25.5 |
| **Attract demo** | **on** | **98.6 %** | 25.0 |
| Gameplay, level 1, spinning in the start room | off | 98.2 % | 28.9 |
| Gameplay, level 1, spinning in the start room | on | 98.2 % | 28.9 |

(Attract-demo row recalibrated 2026-08-03 against the wait-state-only bus
model; was 97.9 % / 24.9 under the old double-counted model. Gameplay rows
were already identical off vs on before the fix and were not re-measured.)

Renders/s here is below 29.97 because the demo window includes the level-load
pause and the title/credits cut, during which nothing renders; the gap-2 share
is the load-bearing figure. (Two rates are printed by the tool — over the whole
analysis window, and over first-to-last new image. The latter is quoted; the
former additionally divides by lead-in/tail-out dead frames and reads ~1 lower.
Neither is the ground truth — `gametic` is, and §3.1 uses it.)

**The gameplay rows are corroboration only and must not be used for
calibration.** A framebuffer dump at frame 5500 confirms the player is standing
in the E1M1 start room (AREA 1, HEALTH 100 %, no monsters in view) after
walking into a wall — a far lighter render load than §2's combat footage.
Calibrating against it would demand that our *light* scene run as slowly as
silicon's *heavy* scene, which a physically correct model would fail. The
demo (§3.1) has no such problem: its rendered image sequence is identical on
both machines by construction (§1.4).

> **We sit on the engine's 2-field hard cap — 98.6–98.7 % of demo frames, 98.2 %
> of gameplay frames — with and without the occupancy model.** (Was
> 97.9–98.7 % before the wait-state-only bus-model fix narrowed the on/off
> spread; see §3.1, §3.2.)

---

## 4. Verdict

**The occupancy model is not the fix, and the demo tic rate was the wrong
observable.**

1. **Demo tic rate is saturated and therefore uninformative.** 29.97 tics/s is
   *exactly* the engine's 2-field cap (§1.2). Both the OFF result (29.97) and
   the ON result (29.97, re-measured against the wait-state-only bus model —
   was 29.67) are the cap; they would read the same if our machine were 5x
   too fast. It cannot discriminate and should not be used for calibration
   again. The replacement is attract-demo **duration** (§1.4, §3.1).
2. **The gap is real and large.** Real hardware 19.98 fps (mode) / ~15 fps
   (heavy) vs our 98.2–98.7 % gap-2. That is **~1.5x too fast typical, 2x in
   heavy scenes**, and by §1.3 it lands on monsters, projectiles, walking-turn
   rate and door/lift timing while the player's translation and running turn
   stay correct — exactly the reported symptom.
3. **The OP-fetch + DRAM-refresh occupancy model closes ~0 % of it.** On the
   load-matched demo it lengthens demo 1 by +0.0 % and demo 2 by +0.1 %
   (§3.1) — within run-to-run noise, once the 68K bus model correctly
   charges only wait states instead of double-counting the 68K's own bus
   cycle (`fix/acid-reconciliation`). (Originally measured +2.0 % / +0.7 %
   against the double-counted model; that gap was mostly an artifact of the
   bug, not the occupancy mechanism.) It charges only the **68000**, and the
   68000 is not the frame-time critical path — now demonstrated even more
   directly than before.

### What *is* on the critical path

Runtime sensitivity, measured by patching the exported per-opcode cycle tables
from the harness (`gpu_opcode_cycles[]`, `src/tom/gpu.c:166-176`, and
`dsp_opcode_cycles[]`, `src/jerry/dsp.c:223`, **both all 1s in the shipping
core**):

| lever | scenario | result |
|---|---|---|
| 68K bus occupancy (`dram_timing`) | attract demo | +0.0–0.1 % duration (was +2.0 % under the double-counted 68K bus model; see §3.1) |
| DSP `all:6` | attract demo | demo 1: 27.61 s → **29.70 s** (+7.6 %), gap-2 89.9 % |
| DSP `all:12` | attract demo | 13.8 renders/s, gap-4 dominant (41.8 %) |
| GPU `all:4` | gameplay (light scene) | gap-2 62.3 %, 25.2 renders/s |
| GPU `all:6` | gameplay (light scene) | gap-2 44.8 %, 19.9 renders/s |

Both RISCs are on the critical path. The DSP matters because Doom runs its mobj
thinkers there (§5.5) *and* because `MiniLoop` blocks on it unconditionally
every frame (`d_main.c:346-347`, `while ( DSPRead(&dspfinished) != 0xdef6 );`).
The GPU matters because it is the renderer. The 68000 barely matters.

### Sizing — an upper bound, not a target

The GPU sweep rows above reach silicon's ~20 fps at a uniform cost of ~6 clocks
per instruction, but **that number is an upper bound on the required cost, for
two independent reasons**:

* It was measured on the light start-room scene (the demo path could not be
  swept — see below). A physically correct model renders a light scene *faster*
  than combat; silicon's 20 fps came from combat footage. Demanding 20 fps from
  the light scene therefore over-charges.
* A uniform per-instruction multiplier cannot reproduce silicon's *shape*.
  Silicon is tightly clustered (77.6 % gap-3, 13.5 % gap-4, 0.5 % gap-2). GPU
  `all:6` matches the mean (3.02 fields) while smeared across gaps 2/3/4/5 at
  44.8/18.5/26.7/10.0 %; DSP `all:12` is likewise smeared across 3/4/5. Real
  frame time varies with visible geometry and then quantises hard to a field
  boundary — a flat multiplier scales every scene identically and cannot do
  that. This is independent evidence, separate from the JTRM citations in §5,
  that the missing term is **per-access / per-hazard**, not a multiplier.

A third reason not to trust `all:N` as anything but a probe: **at GPU `all:3`
and above the no-input boot path deadlocks outright** (12000-frame run, zero
framebuffer change from frame 518 on, attract demo never reached). That is an
emulator bug in its own right and a prerequisite for the next PR — see §5C for
the crash-detect classification. The gameplay-path sweep rows above survive
only because the `--press` chain skips the boot animation, and a framebuffer
dump at GPU `all:6` confirms the scene still renders correctly.
`all:N` is not physics and must not ship.

---

## 5. Next physics lever

Silicon's cost plausibly decomposes into a **load-independent per-frame floor**
and a **load-proportional** term, and our model is weak on both. The evidence
for the floor is suggestive, not conclusive: Clip A shows only 0.5 % gap-2
across 190 s, so *something* keeps even the cheapest observed frame off the
2-field cap. But that inference assumes the footage contains frames as light as
the ones we render in 2 fields, and Doom always draws a full-viewport set of
spans — silicon's dynamic range may simply be narrow. Note the whole observed
spread is only gap-3 → gap-4, i.e. **1.33x**, which a purely load-proportional
model could also produce.

**Ranking caveat:** 5A below is listed first because each item is an
*unambiguous omission* — the code does the work and we charge nothing for it —
**not** because it is known to be the larger term. 5A is unsized; 5B is sized
(bounded above). **Step 0 of the next PR should be to size 5A.1 and 5A.3**
(blitter busy-time and the DSP rendezvous) before choosing where to spend
effort; both are cheap to probe with the same runtime-patching technique used
in §4.

### 5A. Load-independent floor — real omissions, size unknown

1. **Blitter busy-time.** The sprite phase programs the blitter and then
   busy-waits on it: `r_phase8.gas:117-121`
   (`movei #$f02238,r0` / `blitwait1: load (r0),r1 / btst #0,r1 / jr EQ,blitwait1`)
   — B_CMD bit 0 is the blitter-idle flag. On silicon that wait is real
   pixel-proportional bus time and it also *blocks the GPU* while it runs; our
   blitter completes instantly, so the wait always falls through on the first
   poll. This is partly load-proportional (pixels) and partly a fixed
   setup/drain cost per blit; it is listed first because it is a hard,
   uncontroversial omission with a JTRM-defined completion time.
2. **Per-frame GPU/DSP code-overlay loading.** Every renderer phase is an
   overlay: `r_phase1..9.gas` all `.org $f03100`, `p_base.gas`/`p_move.gas`/
   `p_shoot.gas`/`p_sight.gas`/`p_slide.gas` all `.org $f1b140`. A resident
   loader copies the next overlay from DRAM into local RAM with a
   10-instruction loop moving 8 bytes per iteration (`load (r1),r5 /
   load (r3),r6 / store r5,(r0) / store r6,(r4)` + pointer bumps).

   The two loaders are **not** the same, and the difference matters for sizing:

   * `gpubase.gas:122-156` copies **unconditionally** — it clears
     `gpucodestart`, reads the length word below the start address and runs the
     loop every time. The renderer walks phases 1→9 in order every frame, so
     each is a guaranteed miss: **nine DRAM→local-RAM code moves per rendered
     frame**, independent of scene complexity.
   * `dspbase.gas:153-158` first compares the requested start against
     `_olddspcodestart` and `jump EQ,(skipload)` when it matches — the copy is
     skipped when the requested overlay is already resident. DSP-side overlay
     cost is therefore **conditional on the thinker call mix** (a frame that
     only calls `p_base` repeatedly pays once), not a fixed per-frame charge.

   Size of the GPU-side term: the nine phases total 6863 static instructions of
   which 1139 are MOVEI (6 bytes: 2-byte opcode + 4-byte immediate) and 5724
   are 2-byte — `(6863-1139)*2 + 1139*6` = **18 282 bytes ≈ 18.3 KB per
   frame**, i.e. ~2290 loop iterations ≈ 23k GPU instructions and ~4600 DRAM
   reads. Today our core charges those DRAM reads **nothing** with
   `dram_timing` off, and `DRAM_PAGE_CYCLE + dram_row_miss` = 5 clocks each
   with it on (`src/core/bus_arbiter.c:27`, `:51`, `:88-91`, via
   `GPU_EXT_ACCESS`, `src/tom/gpu.c:52-56`) — with no dependent-instruction
   stall. That is a few percent of a two-field budget today versus a materially
   larger fixed cost on silicon, where every one of those 4600 loads stalls.
3. **The unconditional per-frame DSP rendezvous.** `d_main.c:346-347` blocks
   every `MiniLoop` iteration until the DSP posts `0xdef6`. Whatever fixed cost
   the DSP's per-frame work carries is therefore added to every frame,
   regardless of scene. (Do **not** cite `p_tick.c:286-288` for this — that
   block is inside `#ifdef JAGAUR`, a typo for `JAGUAR`, and is dead code.)
   The DSP sweep in §4 confirms this path is live: DSP `all:6` alone lengthens
   the demo 7.6 %.

### 5B. Load-proportional cost (partly modelled, badly)

4. **Score-boarding / RAW stalls.** `docs/jtrm-gpu-dsp.md:40`, `:263` —
   *"Not modeling this makes code run too fast."* The renderer is
   compiler-generated code with constant `load (rN),rN` → immediate-use
   patterns; on silicon each stalls for the external load latency. Likely the
   single biggest missing load-proportional term, and it compounds with (5).
5. **External LOAD/STORE latency as a *stall*, not just a bus charge.**
   `docs/jtrm-gpu-dsp.md:140`, `:267`. `GPU_EXT_ACCESS` charges ~5 clocks but
   models no dependent-instruction stall. Load/store is **24.6 %** of the
   renderer's static instructions (1687 of 6863 across `r_phase1..9.gas`,
   counted with `grep -cE "^[[:space:]]+(load|store)[bwp]?"`).
6. **MOVEI costs 3 cycles, not 1.** `docs/jtrm-gpu-dsp.md:134` — the two
   immediate words occupy two extra fetch slots. MOVEI is **16.6 %** of the
   renderer's static instructions (1139 of 6863; per-phase range 13.1 %
   (`r_phase7`) to 21.3 % (`r_phase9`)), giving a **1.33x upper bound**. It is
   an upper bound because static density over-represents setup code: inner
   loops keep addresses in registers and re-load them rarely, so the *dynamic*
   MOVEI share in the hot span loops is lower than 16.6 %.
7. **The GPU also loses the bus to the OP, blitter and refresh.** Today
   `HalflineCallback` (`src/core/jaguar.c:914-933`) charges OP-fetch and
   refresh occupancy exclusively to the 68000. Per the JTRM bus priorities
   cited at that site (refresh 2, OP 6, CPU 11) the GPU is also below the OP
   and refresh, so it should lose the same clocks during the ~200 active
   display lines. Real, but a fraction of a halfline — a ~1.1–1.3x-class term.
8. **The DSP needs the same treatment as the GPU.** `dsp_opcode_cycles[]`
   (`src/jerry/dsp.c:223`) is all 1s and has no external-access charge at all.
   Doom's mobj thinkers run on the **DSP** — `p_base.gas:1-5` is `.gpu` dialect
   but `.org $f1b140`, inside DSP local RAM ($F1B000–$F1CFFF). The §4 sweep
   shows the DSP is on the critical path, so this is not optional.

Explicitly **not** recommended: scaling anything until the numbers match. The
target is the cycle tables and the stall/blitter/overlay models becoming
*right*, with §3.1's demo duration as the acceptance test.

### 5C. Prerequisite / risk: slowing the GPU already deadlocks the boot path

This is an **emulator bug, not a probe artefact**, and the next PR drives
straight into the regime that triggers it. With `GPU_CYCLES=all:3` (or higher)
and **no input**, the framebuffer never changes from frame 518 to frame 12000.
Re-run with `--option virtualjaguar_crash_detect=verbose`:

```
[CRASH-DETECT] video_stall frame=518 fb_hash=$38C94527 unchanged for 300 frames
               gpu_pc=$00F0309A gpu_run=1 dsp_pc=$00F1B092 dsp_run=1
[CRASH-DETECT] heartbeat frame=1200  gpu_pc=$00F03090 gpu_run=1 dsp_pc=$00F1B09A dsp_run=1 fb_hash=$38C94527
… identical through …
[CRASH-DETECT] heartbeat frame=12000 gpu_pc=$00F03094 gpu_run=1 dsp_pc=$00F1B09C dsp_run=1 fb_hash=$38C94527
```

Classification:

* Signature fired: **`video_stall`** — and only that one. No `gpu_wedge`, no
  `gpu_pc_escape`, no `dsp_pc_escape`, no `dsp_wedge`.
* `gpu_wedge` does **not** fire because the GPU PC is not stuck — it cycles
  through `$F0308A`–`$F0309E`, a ~10-instruction loop at the bottom of GPU
  local RAM, with `gpu_run=1` for all 12000 frames. That is `gpubase.gas`'s
  `nothingwaiting` / `waitcmd` idle spin (`gpubase.gas:117-127`): the GPU is
  alive and polling `_gpucodestart` for a command that never arrives. The DSP
  is doing the same in `$F1B020`–`$F1B09C` (`dspbase.gas:141-148`).
* So both RISCs are idle-waiting for the 68000 to dispatch, while the 68000 is
  (by construction of `MiniLoop`) waiting on `gpufinished` / `dspfinished`.
  A mutual wait — a **lost-wakeup deadlock**, not a slow renderer.

This repo has a documented history of exactly this failure class under changed
RISC timing (stale GPU IRQ latch at restart, lost-wakeup CPUINT delivery,
IS2 `SINGLE_STEP` ownership). Treat it as a **prerequisite**: the next PR must
reproduce and root-cause this deadlock *before* or *alongside* adding GPU cost,
because any correct slowdown lands in the same regime. The gameplay sweep rows
in §4 survive only because the `--press` chain skips the boot animation, which
is a workaround, not a fix.

Reproduce:

```
VJ_HARNESS_LOG_INFO=1 GPU_CYCLES=all:3 ./doom_renderrate <core> "<Doom rom>" \
  --frames 12000 --option virtualjaguar_crash_detect=verbose
```

### Acceptance test for the next change

**Primary — attract-demo duration** (scene-matched and load-matched by
construction, §1.4; no frame-differencing involved):

```
SHOW_RESETS=1 VJ_EXPECT_BUILD=$(./scripts/build-id.sh) \
  ./doom_renderrate <core> "<Doom rom>" --frames 9000 --quiet
```

| | first demo (749 tics) | second demo (984 tics) |
|---|---|---|
| today (off) | 27.6 s | 35.0 s |
| today (on) | 27.6 s | 35.0 s |
| **target** | **silicon's measured duration — see below** | |
| provisional prediction | ~41 s | ~52 s |

(`today (on)` recalibrated 2026-08-03 against the wait-state-only bus model
-- was 28.2 s / 35.2 s before `fix/acid-reconciliation`; see §3.1.)

The provisional targets are today's durations scaled by 29.97/19.98 = 1.50, the
ratio of our gap-2 cadence to silicon's measured gap-3 cadence (§2). A second,
independent derivation agrees: computing the render time directly as
*tics × 3 fields / 59.94* gives 749 × 3 / 59.94 = **37.5 s** and
984 × 3 / 59.94 = **49.2 s**, and adding the non-rendering overhead our own
measurement carries (level-load pause plus the title cut — the difference
between 749/27.59 s = 27.1 renders/s and the 29.97 cap, i.e. ~2.5 s and ~2.0 s
respectively) gives **≈40 s** and **≈51 s**. The two derivations agree within
2–3 %. Both are *predictions*, not measurements, and must be replaced by the
stopwatch number in the open cross-check below before anything is called
calibrated.

**Secondary — distribution shape, measured on the same attract demo.** Run the
frame-differ over the demo (the run above already produces it) and require the
gap histogram to move from today's **98.7 % gap-2 (off) / 98.6 % gap-2 (on)**
toward a *tight* cluster with gap-3 dominant and the tail in gap-4, matching
§2. A change that lands the right mean while smeared across gaps 2–5 (as GPU
`all:6` and DSP `all:12` both do) is a fail. Both halves of the acceptance test
therefore use the **same** scene-matched observable. Do **not** substitute the
spin-in-the-start-room scene — it is lighter than §2's footage and a correct
model will legitimately render it faster than 20 fps.

Guard against overshoot in both directions: demo 1 longer than ~45 s, or a
gameplay mode of gap-4, means the engine is now slower than silicon.

### Open cross-check for a hardware owner

Neither captured clip contains the attract demo, and the reported device
impression was that the *demo* pace looks right while in-game monsters are too
fast. Under §1.4 the demo's apparent speed is also proportional to render rate,
so if silicon runs the demo at 20 fps it should look 1.5x fast to us too. One
stopwatch settles the primary acceptance target:

> On real hardware (or BigPEmu), let Doom sit at the title until the attract
> demo starts, and time from the first frame of demo gameplay to the moment it
> cuts back to the title/credits. There are two demos that alternate; time both
> if possible.

**The two windows match.** Our number is measured from the frame where
`gametic` first leaves 0 to the frame of the last tic before it resets. Doom
sets `gametic = 0` in `G_InitNew` (`g_game.c:325`) and takes the first tic in
the first `P_Ticker` call, whose `P_Drawer` produces the first demo image — so
our `t0f` is the first rendered demo frame. The reset happens when the demo
ends and the next `G_InitNew` runs, which is the cut back to the title. The
stopwatch window and the counter window therefore differ by at most one or two
video frames (≈0.03 s), far below the ~13 s the two hypotheses are separated
by.

| | our core (measured) | if silicon runs the demo at 19.98 fps |
|---|---|---|
| first demo | 27.6 s | ~41 s |
| second demo | 35.0 s | ~52 s |

If the stopwatch gives ~41 s / ~52 s, the §4 verdict covers the whole game and
the acceptance target above is confirmed. If it gives ~28 s / ~35 s, the demo
really does hit the 30 fps cap on silicon and only heavier in-game scenes are
mispaced — which would mean the missing cost is predominantly
**load-proportional** (§5B) and the load-independent floor (§5A) is smaller
than Clip A's 0.5 % gap-2 suggests. Either answer directly reprioritises §5.

---
## Provenance

* Jaguar Doom source: Songbird Productions / Carl Forhan archive (April 2003),
  id Software Limited Use license; mirror `github.com/Arc0re/jaguardoom`.
  Every source citation in §1 and §5 was independently re-verified.
* Real-hardware footage: `youtube.com/watch?v=YxQgwE4B1eE` (RetroTink 2X, 60 fps),
  `youtube.com/watch?v=ZJa4CHi3H7I` (Atari Jaguar and Lynx Garage, NTSC longplay).
  Authenticity rests on the gap-parity argument in §2, not on the clip titles.
* No published frame-rate analysis of Jaguar Doom was found; reviews describe
  the slowdown only qualitatively. The numbers in §2 are original measurements.
* Measurement tools are reproduced in the appendix below so every number here
  can be re-derived from a clean checkout.

---

## Appendix — measurement tools

Both tools were written for this investigation and are reproduced here so the
numbers above stay reproducible. Neither is wired into the build.

### A1. `doom_renderrate.c` — our-side render-rate and demo-duration measurement

Build (from the repo root, with a `TEST_EXPORTS=1` core in the tree):

```sh
cc -O2 -Wall -std=c99 -I./libretro-common/include -I./test/harness \
   -o /tmp/doom_renderrate /tmp/doom_renderrate.c test/harness/harness.c -ldl -lm
```

Env knobs: `SHOW_RESETS=1` (print each attract demo's tic count, frame span and
wall-clock duration — the §3.1 primary observable), `ANALYZE_FROM` (first frame
of the frame-diff window), `SHOT_AT` + `SHOT_OUT` (dump a PPM to confirm what is
actually on screen), `GPU_CYCLES` / `DSP_CYCLES` (`"38:3,21:16"` or `"all:6"`)
to patch the exported `gpu_opcode_cycles[]` / `dsp_opcode_cycles[]` tables at
runtime for sensitivity probes.

```c
/* doom_renderrate.c — measure how many *unique rendered images* Jaguar Doom
 * produces per second inside our core, using the same frame-difference
 * method applied to real-hardware YouTube captures, so the two numbers are
 * directly comparable.  Also samples gametic ($04080C) for cross-check.
 *
 * Scratch tool (lives outside the repo).  Build:
 *   cc -O2 -Wall -std=c99 -I<repo>/libretro-common/include \
 *      -o doom_renderrate doom_renderrate.c <repo>/test/harness/harness.c -ldl -lm
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include "harness.h"

#define MAXF 20000
#define MAXPX (1024*576)

static uint8_t *mainram;
static uint32_t prev[MAXPX];
static int have_prev = 0;
static double diffs[MAXF];
static uint32_t tics[MAXF];
static unsigned nframes = 0;
static unsigned shot_at = 0;
static const char *shot_path = NULL;

static void on_video(void *ud, const void *data, unsigned w, unsigned h,
                     size_t pitch)
{
    const uint32_t *p = (const uint32_t *)data;
    unsigned x, y, y0, y1, n = 0;
    double acc = 0.0;
    (void)ud;
    if (!p || nframes >= MAXF) { if (nframes < MAXF) diffs[nframes] = 0.0; return; }
    y0 = 0; y1 = (h * 3) / 4;                 /* 3D viewport, drop status bar */
    for (y = y0; y < y1; y++) {
        const uint32_t *row = (const uint32_t *)((const uint8_t *)p + y * pitch);
        for (x = 0; x < w; x += 2) {
            uint32_t c = row[x];
            uint32_t g = ((c >> 16 & 0xFF) * 77 + (c >> 8 & 0xFF) * 151
                          + (c & 0xFF) * 28) >> 8;
            uint32_t o = prev[n];
            acc += (double)(g > o ? g - o : o - g);
            prev[n++] = g;
        }
    }
    diffs[nframes] = have_prev ? acc / (double)n : 1e9;
    have_prev = 1;
    if (shot_path && nframes == shot_at) {
        FILE *f = fopen(shot_path, "wb");
        if (f) {
            fprintf(f, "P6\n%u %u\n255\n", w, h);
            for (y = 0; y < h; y++) {
                const uint32_t *row = (const uint32_t *)((const uint8_t *)p + y * pitch);
                for (x = 0; x < w; x++) {
                    uint8_t rgb[3];
                    rgb[0] = row[x] >> 16; rgb[1] = row[x] >> 8; rgb[2] = row[x];
                    fwrite(rgb, 1, 3, f);
                }
            }
            fclose(f);
        }
    }
}

static bool on_frame(void *ud, unsigned frame)
{
    (void)ud; (void)frame;
    if (nframes < MAXF) {
        tics[nframes] = mainram
            ? (((uint32_t)mainram[0x04080C] << 24) | ((uint32_t)mainram[0x04080D] << 16)
             | ((uint32_t)mainram[0x04080E] << 8)  |  (uint32_t)mainram[0x04080F])
            : 0;
        nframes++;
    }
    return true;
}

int main(int argc, char **argv)
{
    harness_config cfg = HARNESS_CONFIG_DEFAULT;
    unsigned i, start = 0;
    double thr = 0.0;
    unsigned gaps[64];
    unsigned last = 0, ngap = 0, nnew = 0;
    double gapsum = 0.0;
    const char *env;

    cfg.frames = 6000;
    cfg.frame_callback = on_frame;
    cfg.video_callback = on_video;
    if (!harness_init_from_args(&cfg, argc, argv)) return 1;
    env = getenv("SHOT_AT");   if (env) shot_at = (unsigned)atoi(env);
    shot_path = getenv("SHOT_OUT");
    env = getenv("ANALYZE_FROM"); if (env) start = (unsigned)atoi(env);
    if (!harness_load_rom(&cfg)) return 1;
    { uint8_t **rp = (uint8_t **)harness_dlsym(&cfg, "jaguarMainRAM");
      mainram = rp ? *rp : NULL; }
    /* Optional experiment: patch the GPU per-opcode cycle table
     * (all 1 in the shipping core).  GPU_CYCLES="38:3,21:16" etc. */
    { const char *spec = getenv("GPU_CYCLES");
      const char *dspec = getenv("DSP_CYCLES");
      uint8_t *dtbl = (uint8_t *)harness_dlsym(&cfg, "dsp_opcode_cycles");
      uint8_t *tbl = (uint8_t *)harness_dlsym(&cfg, "gpu_opcode_cycles");
      if (dspec && dtbl) {
          char buf[256]; char *t; int idx, val;
          strncpy(buf, dspec, sizeof buf - 1); buf[sizeof buf - 1] = 0;
          for (t = strtok(buf, ","); t; t = strtok(NULL, ",")) {
              idx = -1; val = 1;
              if (sscanf(t, "%d:%d", &idx, &val) == 2 && idx >= 0 && idx < 64)
                  dtbl[idx] = (uint8_t)val;
              else if (sscanf(t, "all:%d", &val) == 1) memset(dtbl, val, 64);
          }
          printf("patched dsp_opcode_cycles: %s\n", dspec);
      }
      if (spec && tbl) {
          char buf[256]; char *t; strncpy(buf, spec, sizeof buf - 1);
          buf[sizeof buf - 1] = 0;
          for (t = strtok(buf, ","); t; t = strtok(NULL, ",")) {
              int idx = -1, val = 1;
              if (sscanf(t, "%d:%d", &idx, &val) == 2 && idx >= 0 && idx < 64)
                  tbl[idx] = (uint8_t)val;
              else if (sscanf(t, "all:%d", &val) == 1)
                  memset(tbl, val, 64);
          }
          printf("patched gpu_opcode_cycles: %s\n", spec);
      } else if (spec) printf("gpu_opcode_cycles NOT exported\n");
    }
    harness_run(&cfg);

    /* threshold: a rendered image change moves far more than codec-free
     * identical frames (which are exactly 0 here -- no re-encode). */
    thr = 0.05;
    memset(gaps, 0, sizeof gaps);
    for (i = start; i < nframes; i++) {
        if (diffs[i] >= thr && diffs[i] < 1e8) {
            nnew++;
            if (last) {
                unsigned g = i - last;
                gapsum += g;
                if (g < 64) gaps[g]++;
                ngap++;
            }
            last = i;
        }
    }
    if (getenv("SHOW_RESETS")) {
        /* For each demo instance: the frame where gametic leaves 0 (first
         * rendered tic) to the frame of the last tic before it resets.  That
         * span, divided by 59.94, is the demo's wall-clock duration -- the
         * load-matched, scene-matched observable (the recorded input stream
         * is identical on every machine, so only duration varies). */
        unsigned t0f = 0;
        for (i = 1; i < nframes; i++) {
            if (tics[i] > 0 && tics[i-1] == 0 && tics[i] < 0x1000) t0f = i;
            if (tics[i] < tics[i-1] && tics[i-1] < 0x10000000) {
                if (t0f && i > t0f)
                    printf("demo: %u tics over frames %u..%u = %u frames "
                           "= %.2f s  (%.2f renders/s)\n", tics[i-1], t0f, i,
                           i - t0f, (i - t0f) / 59.94,
                           tics[i-1] * 59.94 / (double)(i - t0f));
                t0f = 0;
            }
        }
    }
    printf("frames analysed = %u (from %u)\n", nframes - start, start);
    printf("gametic %u..%u\n", tics[start], tics[nframes ? nframes-1 : 0]);
    printf("unique rendered images = %u -> %.2f /s over the whole window; "
           "%.2f /s over first..last new image\n",
           nnew, nnew * 59.94 / (double)(nframes - start),
           gapsum > 0 ? (nnew - 1) * 59.94 / gapsum : 0.0);
    printf("gap histogram (emulated video frames between new images):\n");
    for (i = 1; i < 64; i++)
        if (gaps[i]) printf("   gap %2u : %5u (%5.1f%%)\n", i, gaps[i],
                            100.0 * gaps[i] / ngap);
    if (ngap) printf("mean gap = %.2f -> %.2f fps\n", gapsum / ngap,
                     59.94 / (gapsum / ngap));
    printf("per-second unique counts:\n");
    for (i = start; i + 60 <= nframes; i += 60) {
        unsigned j, c = 0;
        for (j = i; j < i + 60; j++)
            if (diffs[j] >= thr && diffs[j] < 1e8) c++;
        printf("%u ", c);
    }
    printf("\n");
    harness_shutdown(&cfg);
    return 0;
}
```

### A2. `framediff.py` — real-hardware footage measurement

```sh
python3 framediff.py <video> <ffmpeg-crop w:h:x:y> <fps> [start-s] [duration-s]
```

Prints the diff histogram (check it is bimodal before trusting the result), the
Otsu threshold, the gap histogram in capture-frame units, and a per-second
unique-frame timeline.

```python
#!/usr/bin/env python3
"""Count unique rendered frames per second in a video by frame-differencing
a cropped region.  Prints the diff histogram (to verify bimodality), the
per-second new-frame rate, and the distribution of gaps between new-frame
events in capture-frame units."""
import subprocess, sys, numpy as np

vid   = sys.argv[1]
crop  = sys.argv[2]            # ffmpeg crop=w:h:x:y
W, H  = 160, 100
fps   = float(sys.argv[3]) if len(sys.argv) > 3 else 60.0
t0    = sys.argv[4] if len(sys.argv) > 4 else None
dur   = sys.argv[5] if len(sys.argv) > 5 else None

cmd = ["ffmpeg", "-v", "error"]
if t0:  cmd += ["-ss", t0]
if dur: cmd += ["-t", dur]
cmd += ["-i", vid, "-vf", f"crop={crop},scale={W}:{H}", "-pix_fmt", "gray",
        "-f", "rawvideo", "-"]
raw = subprocess.run(cmd, capture_output=True).stdout
n = len(raw) // (W * H)
a = np.frombuffer(raw, dtype=np.uint8)[:n*W*H].reshape(n, H*W).astype(np.int16)
d = np.abs(np.diff(a, axis=0)).mean(axis=1)

print(f"frames={n}  duration={n/fps:.2f}s")
hist, edges = np.histogram(d, bins=[0,.25,.5,1,2,3,4,6,8,12,16,24,32,1e9])
for c, lo, hi in zip(hist, edges[:-1], edges[1:]):
    if c: print(f"  diff [{lo:6.2f},{hi:7.2f}) : {c}")

# threshold: midpoint of the bimodal valley, searched between 0.3 and 6
best, thr = None, 1.0
for t in np.arange(0.3, 6.0, 0.05):
    lo = d[d < t]; hi = d[d >= t]
    if len(lo) < 10 or len(hi) < 10: continue
    # maximise between-class variance (Otsu)
    v = len(lo)*len(hi)*(lo.mean()-hi.mean())**2
    if best is None or v > best: best, thr = v, t
print(f"otsu threshold = {thr:.2f}")

new = d >= thr
idx = np.flatnonzero(new)
gaps = np.diff(idx)
print(f"new frames = {new.sum()}  -> {new.sum()*fps/n:.2f} unique fps (mean)")
g, cg = np.unique(gaps, return_counts=True)
print("gap histogram (capture frames between new frames):")
for gg, cc in zip(g, cg):
    if cc >= 3: print(f"   gap {gg:2d} : {cc:5d}  ({100*cc/len(gaps):5.1f}%)")
print(f"mean gap = {gaps.mean():.2f} frames = {fps/gaps.mean():.2f} fps")

# per-second timeline
print("per-second unique-frame counts:")
sec = int(fps)
line = []
for s in range(0, n//sec):
    line.append(int(new[s*sec:(s+1)*sec].sum()))
print(" ".join(f"{v}" for v in line))
```
