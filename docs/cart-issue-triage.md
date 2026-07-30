# Cartridge issue re-verification (post Jaguar-CD merge)

Re-measurement of the open **cartridge** bug reports against `develop` after the
Jaguar CD feature branch landed (PR #201).

| field | value |
|---|---|
| core build | `Virtual Jaguar v2.3.2 3ee002d` (clean, no `-dirty`) |
| base commit | `3ee002d` — *Merge pull request #201 from libretro/feature/jaguar-cd-support* |
| host | macOS (arm64), Clang, NEON blitter SIMD path |
| build | `make -j$(getconf _NPROCESSORS_ONLN) TEST_EXPORTS=1` |
| bus-contention option | **absent** on this commit (no `contention` key in `libretro_core_options.h`) — still only on PR #169, so it was not part of this matrix |

The CD branch shipped several general-accuracy changes that are not CD-specific
(G_CTRL `SINGLE_STEP` barrier, GPU IRQ-edge capture semantics, memory-map
write-mirror removal, blitter/OP corrections). This document records what those
did — and did not — do to the open cartridge tickets.

## Reading this document

Two classes of evidence are used, and they are **not** equally strong:

* **Core-internal signatures** (`src/core/crash_detect.c`: `gpu_pc_escape`,
  `dsp_pc_escape`, `gpu_wedge`, `dsp_wedge`, `video_stall`), **68K register /
  traceback dumps** from `cd_wedge_probe`, and **blitter-compare pixel/register
  counters**. These are immune to the headless read-path caveat and are the basis
  for every strong verdict below.
* **Framebuffer pixels**. CLAUDE.md's *Headless framebuffer caveat* applies: the
  headless read path is not RetroArch's composited output for every title
  (`jag_240p_test_suite` reads ~1k non-black pixels headless vs tens of thousands
  in RetroArch). Pixel-only verdicts are hedged explicitly where they occur.

## Triage table

| # | Title | Verdict | Basis |
|---|---|---|---|
| **138** | Pitfall: The Mayan Adventure — black screen | **STILL REPRODUCES** (worst offender) | deterministic hard wedge — `video_stall` at **3989** (run+jump script) / **3491** (no-input control, i.e. it dies sooner without input); 68K parked on an `RTE` stub at `$000404`; bit-identical under both blitters |
| **178** | Alien vs Predator — display issues | **PARTLY REPRODUCES** | cyan boot bar confirmed `rgb(0,255,255)` 320×3 at frame 0 (and it is **not** AvP-specific); in-game brown bottom bar confirmed rows 236–239; green dot / green bar **not** reproduced |
| **186** | Iron Soldier (cart) — unplayable | **CHANGED (DEMO path fixed; mission-start path untested)** | 3D DEMO mode renders + animates in **all 4** blitter×BIOS combos, no signature; fast-blitter missing wireframe tank still reproduces exactly; `*`+`#` is not a core bug; **mission start was never reached — see caveat** |
| *180* | BIOS cube top-edge pixels (referenced by #189) | **NO LONGER REPRODUCES** | all 37 BIOS-on frames, including the entire cube animation, are **byte-identical** between fast and accurate blitter |
| **187** | Tempest 2000 — graphical glitches | **NOT OBJECTIVELY VERIFIABLE HEADLESS** | no signature over 3600 BIOS-mode frames; 133 656 blits with **0** fast-vs-accurate pixel diffs; stable geometry. Glitch class cannot be distinguished from intended effects without a reference |
| **189** | accurate-blitter writeback divergence | **CHANGED (largely fixed)** | pixel divergence 676/3190 → **0/3190**; `A1_PIXEL` divergence gone; `A2_PIXEL` divergence remains; BSG visible symptom gone |

### Suggested disposition

* **#138** — keep open, **raise priority**. Now has a deterministic headless repro
  and a captured 68K crash state; this is the most actionable ticket of the five.
* **#178** — keep open but **split**. Two of four sub-symptoms are confirmed with
  exact pixel values; the cyan bar is a core-wide boot artefact and deserves its
  own ticket, not an AvP one. The green dot/bar needs a RetroArch check.
* **#186** — **edit the ticket, do not close.** The DEMO half of the headline
  claim is fixed in all four combos. The mission-start half was never reached, so
  it is still unverified. What is additionally confirmed is one fast-blitter
  rendering bug. Retitle and narrow.
* **#180** — **closeable.** Fast and accurate now render the BIOS cube
  byte-identically.
* **#187** — ask the reporter for a **video capture or a savestate at the glitch**.
  Without a reference this cannot be adjudicated headlessly, and the two obvious
  mechanisms (blitter divergence, geometry churn) are both ruled out.
* **#189** — **re-scope and downgrade**. Headline claim no longer holds; a
  narrower `A2_PIXEL` writeback bug remains with no known visible symptom.

## Common setup for every repro below

```bash
make -j$(getconf _NPROCESSORS_ONLN) TEST_EXPORTS=1

cc -O2 -Wall -std=c99 -I. -I./libretro-common/include \
   -o test/tools/cd_visual_verify test/tools/cd_visual_verify.c test/harness/harness.c -ldl -lm
cc -O2 -Wall -std=c99 -I. -I./libretro-common/include \
   -o test/tools/cd_wedge_probe  test/tools/cd_wedge_probe.c  test/harness/harness.c -ldl -lm
cc -O2 -Wall -std=c99 -I./libretro-common/include \
   -o test/tools/test_blitter_compare test/tools/test_blitter_compare.c -ldl

export VJ_EXPECT_BUILD=$(./scripts/build-id.sh)
R="test/roms/private/ROMS"          # 139 cart titles
S="test/roms/private/states"        # savestates + a few ROMs
# PPM -> PNG for reading:  sips -s format png frame_03300.ppm --out frame_03300.png
```

> Build the tools **in the tree you are testing** — a prebuilt binary from
> another branch links that branch's `harness.c`, and `VJ_EXPECT_BUILD` only
> guards the core, not the tool.

---

## #138 — Pitfall: The Mayan Adventure, black screen after ~1 min of level 1

**Verdict: STILL REPRODUCES. Now deterministic and headlessly reproducible.**

The reporter's attached RetroArch log
(`retroarch__2026_05_03__22_59_05.log`) contains **no** crash signature — it ends
with a clean core unload — so it was of no diagnostic use. It does confirm the
reporter's configuration: `usefastblitter=enabled`, `bios=disabled`, NTSC.

### The failure

Pitfall boots into an attract-mode demo of level 1 on its own. Pressing a button
leaves attract for the title menu; selecting START begins player-controlled
level 1. **~950 frames (~16 s) into real gameplay the game dies permanently.**

| frame | state | non-black |
|---|---|---|
| 3300 | normal gameplay — Harry running, score `000125`, ×3 lives, ×19 stones | full scene |
| 3600 | **level/background layer gone; HUD still drawn** (`000125`, ×3, ×18, ×00) | ~4 % |
| 3720 → 8999 | fully black, never recovers | **0 / 78240** |

```
[CRASH-DETECT] video_stall frame=3989 fb_hash=$292A41FD unchanged for 300 frames
               gpu_pc=$00F03E72 gpu_run=1 dsp_pc=$00F1B324 dsp_run=1
```

Audio dies with it: only 2 781 624 of 7 200 000 samples non-silent.

**The two-stage death is the most valuable clue**: the level layer disappears one
window *before* the HUD does. Those are different objects in the OP list, so
whatever breaks does not break all rendering at once.

### Blitter-independent, input-independent, fully deterministic

Two controls were run:

| run | stall frame | `fb_hash` | outcome |
|---|---|---|---|
| fast blitter, run+jump input | 3989 | `$292A41FD` | black from ~3720 |
| accurate blitter, same input | 3989 | `$292A41FD` | **bit-identical** to the above |
| fast blitter, **no input after START** | **3491** | `$292A41FD` | black to end of run |

* **Blitter-independent.** Identical stall frame, framebuffer hash, GPU PC and
  audio counts under both blitters — the blitter is ruled out.
* **Input-independent.** With `START` pressed and then *no further input at all*,
  the game still dies — in fact **~500 frames earlier** (3491 vs 3989), so player
  input slightly *delays* it. The wedge is intrinsic to running the level, not
  something the input script provokes.

The reporter says "randomly after a minute or so", whereas this repro is
deterministic and fires ~16 s into gameplay (~58 s after boot, which is close to
their wall-clock description). The determinism is a *gift* for bisecting; but note
the discrepancy in the "random" characterisation — it is possible the reporter's
longer survival time reflects different play, and that this repro is a
particularly early-firing instance of the same bug rather than a different one.
The frame-3600 evidence argues it is genuine and not an in-game death: score
`000125`, ×3 lives and the full HUD are all still drawn while only the background
layer has vanished.

### Bisect predicate — do NOT use the tool's exit status

`cd_visual_verify` **exits 0 on a fully wedged run** — its `video_rendered` and
`audio_present` checks both PASS while the screen is black ("2 passed, 0 failed").
A naive `git bisect run` using its exit code marks every commit *good*.

Verified-firing predicates on the run log (both confirmed against the wedged run):

```bash
./test/tools/cd_visual_verify ... > /tmp/pit.log 2>&1
grep -qE 'nonblack +0/'  /tmp/pit.log   # FIRES on the wedge
grep -q  'video_stall'   /tmp/pit.log   # FIRES on the wedge
```

Use one of those (inverted) as the `git bisect run` predicate between `v2.1.0` —
the reporter states the bug was absent there — and `3ee002d`. The no-input script
is the better bisect vehicle: fewer moving parts, and it fires earlier.

### Captured crash state (`cd_wedge_probe`)

```
[WEDGE-PROBE] framebuffer frozen at frame 3929 (240 identical frames)
[WEDGE-PROBE] 68K PC=$000404 SR=$2212 A7=$003FFE
[WEDGE-PROBE]   D0=$00000000 A0=$0003FF93
[WEDGE-PROBE]   D1=$00000007 A1=$00B3FD58
[WEDGE-PROBE]   D2=$00000404 A2=$00032248
[WEDGE-PROBE]   D3=$00000092 A3=$0000798C
[WEDGE-PROBE] recent 68K PCs (unique, newest first):  $4EF90000 $000404
[WEDGE-PROBE] RAM dump @ $0003E0:
[WEDGE-PROBE]   $0003E0: 0000 0404 0000 0404 0000 0404 0000 0404
[WEDGE-PROBE]   $0003F0: 0000 0404 0000 0404 0000 0404 0000 0404
[WEDGE-PROBE]   $000400: 508F 4E73 4E73 0000 0000 0000 0000 0000
[WEDGE-PROBE] RESULT: wedge caught
```

Decoding this:

* `$000404` holds opcode `$4E73` = **`RTE`**. The 68K is sitting on a bare
  return-from-exception stub.
* Vector slots `$0003E0`–`$0003FF` all contain `$00000404` — the game installed
  that stub as its catch-all handler. That is *intended* game setup, not
  corruption.
* `SR=$2212` → supervisor mode, interrupt mask 2. The 68K is **inside an
  exception context** and doing nothing but returning from it.

So the game's main loop is gone: the 68K only services an interrupt into a
do-nothing stub. `gpu_pc=$00F03E70/72` is a red herring — the frame-3300
snapshot (taken while gameplay was healthy) shows the GPU at **the same**
`$F03E72`, so that address is the GPU's normal idle/wait loop, **not** the wedge
site.

### Next-step lead

1. **Subsystem: 68K exception dispatch, not the blitter and not the GPU.** The
   question to answer first is *which vector fires*, and whether it is legitimate
   (a game-installed catch-all doing its job while the real main loop was
   derailed elsewhere) or spurious (the core dispatching an interrupt the
   hardware would not raise).
2. **First experiment:** log every 68K exception for frames 3400–3600 — vector
   number, the PC that was interrupted, and the stacked PC/SR. `A7=$003FFE`, so
   read the exception frame at `$003FFE` to recover the interrupted PC. That
   single number says whether the game jumped to the stub from its own code or
   was pushed there.
3. **Second experiment:** diff the two RAM snapshots this run already captured —
   `pit_snap_f3300.{ram,gpu,tom,dsp}` (healthy) vs `pit_snap_f3500.*` and the
   wedge dump. Compare **TOM's OLP / object list** specifically: the level layer
   dying before the HUD is exactly what a truncated or stomped OP list looks
   like. `test/tools/test_op_gpu_object.c` is the existing OP harness to extend.
4. **Bisect.** The reporter states this "was not present in version 2.1". The
   repro is deterministic and input-independent, so `git bisect` between `v2.1.0`
   and `3ee002d` is probably the cheapest path to the culprit commit — using a
   **log-grep predicate, not the tool's exit status** (see above).
5. **Tooling gap found:** `cd_wedge_probe` could not resolve `gpu_pc_ring`,
   `gpu_pc_ring_ptr`, `gpu_pc_ring_frozen`, so there is no GPU traceback ring on
   this build. Add one (or export the symbols) before the next deep dive.

### Repro

`scripts` used for this run are reproduced inline so no scratch files are needed:

```bash
# navigation: leave attract at 1600, choose START at 2600, then run/jump forever
ARGS=(--press 1600:b:8 --press 2600:b:10)
f=3000; while [ $f -lt 8850 ]; do
  ARGS+=(--press "$f:right:80" --press "$((f+100)):b:8"); f=$((f+220)); done

./test/tools/cd_visual_verify ./virtualjaguar_libretro.dylib \
  "$R/Pitfall - The Mayan Adventure (1995).jag" \
  --frames 9000 "${ARGS[@]}" \
  --option virtualjaguar_usefastblitter=enabled \
  --option virtualjaguar_crash_detect=verbose \
  --outdir /tmp/pit --shot-every 300 > /tmp/pit.log 2>&1
# expect: video_stall at 3989, non-black 0/78240 from ~3720 to the end.
# flip usefastblitter to disabled -> byte-identical result.
# NOTE: this command exits 0 even when wedged; grep the log (see above).

# minimal control: START only, then no input at all -> wedges EARLIER (3491)
./test/tools/cd_visual_verify ./virtualjaguar_libretro.dylib \
  "$R/Pitfall - The Mayan Adventure (1995).jag" \
  --frames 9000 --press 1600:b:8 --press 2600:b:10 \
  --option virtualjaguar_usefastblitter=enabled \
  --option virtualjaguar_crash_detect=verbose \
  --outdir /tmp/pit_noinput --shot-every 300 > /tmp/pit_noinput.log 2>&1
grep -E 'CRASH-DETECT|nonblack +0/' /tmp/pit_noinput.log | head

# crash-state capture (exits 42 when a wedge is caught)
./test/tools/cd_wedge_probe ./virtualjaguar_libretro.dylib \
  "$R/Pitfall - The Mayan Adventure (1995).jag" \
  --frames 5000 --arm 3000 --freeze-frames 240 \
  --snap 3300 --snap 3500 --snap-prefix /tmp/pit_snap \
  --ram-dump /tmp/pit_wedge "${ARGS[@]}"
```

---

## #178 — Alien vs Predator display issues

**Verdict: PARTLY REPRODUCES.** Two of the four reported artefacts are confirmed
with exact pixel values; one is not reproduced headlessly; one was not isolated.

The reporter's 4-cell matrix (blitter × BIOS, both set **at boot**) was run in
full, 900 frames each, screenshots every 25 frames.

### Confirmed 1 — cyan bar at launch, and it is NOT AvP-specific

> *"there is a brief flash of a cyan bar at the top of the screen when the game
> launches (within the first milliseconds)"*

Frame 0, BIOS off:

```
frame_00000.ppm  320x240
  TOP    rows 0..7 : 960 non-black px
      rgb(0, 255, 255)    n=960    x=0..319 y=0..2
```

Pure cyan `#00FFFF`, exactly 320 × 3 px, on the very first frame. **The same
960-pixel cyan bar appears at frame 0 of Pitfall**, so this is a **core-wide
first-frame artefact in HLE (BIOS-off) mode, not an AvP bug** — it should be
split into its own ticket.

*Lead:* the first frame is presented before the OP/TOM registers are
initialised, so the line buffer is showing an uninitialised background/`BORD`
colour. Look at what `retro_run` presents on frame 0 versus when TOM's
`BG`/`BORD1`/`BORD2` are first written, and at whether frame 0 should be dropped
entirely. Note the geometry is 320×240 on frame 0 and 326×240 from frame 1 on —
the artefact sits exactly in that pre-geometry window.

### Confirmed 2 — brown bar in the in-game bottom letterbox

> *"there is a brown bar in the bottom of the in-game black bar area (all
> screenshots). This did not appear in Virtual Jaguar 2.1.0"*

In-game (first-person corridor, SCORE HUD visible), bottom 30 rows:

```
  BOTTOM rows 210..239 : 7067 non-black px
      rgb(24, 12, 0)      n=186    x=8..320   y=236..239
      rgb(32, 28, 8)      n=103    x=23..322  y=236..239
      rgb(24,  4, 0)      n=60     x=8..272   y=236..239
      rgb(32, 16, 8)      n=57     x=21..321  y=236..239
      rgb(24, 16, 0)      n=50     x=9..296   y=236..239
```

Rows **236–239** carry ~456+ full-width pixels whose channels are consistently
`R > G > B` (brown/orange) where the letterbox should be black. This matches the
report, including that it is present in every configuration.

*Lead:* four rows at the very bottom of the visible field, full width — that is
OP/TOM territory, not the blitter (see below). Check where the active display
window ends versus where the OP stops emitting objects: the last 4 lines look
like they are rendering stale line-buffer content instead of the background
colour. `docs/jtrm-object-processor.md` (display pipeline / STOP object) and
`docs/jtrm-register-map.md` (`VDB`/`VDE`, `BORD1`/`BORD2`) are the references.

### Not reproduced — green dot / green bar on the title screen

Neither the *"green dot in the top-right of the title screen black bar area"*
(BIOS off) nor the *"green bar in the right of the title screen black bar area"*
(BIOS on) appeared. A sweep of every captured frame in both BIOS modes for
sparse non-black pixels in the top/bottom 24 rows found **only** the BIOS boot
animation's own grey/red pixels (frames 275/325/425) — nothing green anywhere.

Also, the headless title screen has **no black letterbox at all** in BIOS-off
mode (the artwork reaches the top edge: rows 0–23 are 98 % non-black dark blue),
whereas the reporter's screenshots show a letterboxed title screen at the same
326×240 geometry. That mismatch is itself a signal that this specific artefact
sits in the read-path/composition difference described in CLAUDE.md.
**Verdict for this sub-symptom: needs a RetroArch check; do not close on headless
evidence.**

### Not isolated — red background behind the shotgun (accurate blitter)

Not reached in this pass (it requires a specific in-game weapon state). One
relevant negative, though: for AvP, `usefastblitter=enabled` and `=disabled`
produced **byte-identical** 900-frame timelines (identical non-black counts,
motion counts and audio sample counts). Combined with Tempest's 0/133 656 pixel
diffs and BSG's 0/3190, the two blitter paths now agree on far more than they
used to — so a *blitter-dependent* AvP artefact should be re-confirmed on the
current build before it is investigated.

### Repro

```bash
for BL in enabled disabled; do for BI in "" --bios; do
  ./test/tools/cd_visual_verify ./virtualjaguar_libretro.dylib \
    "$R/Alien vs Predator (1994).jag" --frames 900 $BI \
    --option virtualjaguar_usefastblitter=$BL \
    --option virtualjaguar_crash_detect=verbose \
    --outdir "/tmp/avp_${BL}${BI}" --shot-every 25
done; done

# in-game (brown bar): navigate in, then scan the bottom band
./test/tools/cd_visual_verify ./virtualjaguar_libretro.dylib \
  "$R/Alien vs Predator (1994).jag" --frames 3000 \
  --press 700:b:8 --press 1000:b:8 --press 1300:b:8 --press 1600:b:8 --press 1900:b:8 \
  --outdir /tmp/avp_play --shot-every 200
```

Note the two AvP savestates in `test/roms/private/states/` (`.state6`,
`.state7`) are **rejected** by `retro_unserialize` on this build, while
`Battle Sphere Gold (World).state1` loads fine — worth a separate look at
savestate-version compatibility.

---

## #186 — Iron Soldier (cartridge), "unplayable"

**Verdict: CHANGED — the headline symptom is fixed. One of three sub-symptoms
remains, and one was never a core bug.**

### Sub-symptom 1 — black screen in 3D: DEMO path NO LONGER REPRODUCES; mission-start path NOT TESTED

> *"If you launch the game and try to start a mission (or load the 3D-rendered
> 'Demo' mode), Virtual Jaguar only shows a black screen (tested with all 4
> combinations of Fast Blitter and BIOS modes)"*

This claim covers **two** entry points into the 3D engine. Only one was reached.

**DEMO mode — works, in all four combinations.** Driven from the main menu
(3 × down → B) and run in the reporter's full blitter × BIOS matrix:

| blitter | BIOS | crash signature | steady-state |
|---|---|---|---|
| fast | off | none | 30/60 frames changing, 85.7 % non-black |
| fast | on | none | identical (modulo a 600-frame BIOS boot offset) |
| accurate | off | none | identical |
| accurate | on | none | identical |

All four timelines agree frame-for-frame once the BIOS boot delay is subtracted.
Screenshots read directly from the **accurate + BIOS-on** run (the combination
furthest from the default) show the full first-person cockpit — 3D city with
shaded buildings, an enemy mech, radar dome, damage bars, weapon readout
("HAND GRENADE") and explosion effects. This is the 3D renderer working, not a
static frame.

**Mission start — NOT REACHED, therefore NOT VERIFIED.** Four separate input
scripts failed to get past the loadout screens into a mission: walking the weapon
grid down to `EXIT` (`down`×5 → `B`), repeated `B` presses, and `right`→`B` aimed
at the `PLAY` button on the briefing screen. Each run ended parked on the
briefing or SELECT WEAPONS screen at ~91 % non-black with no signature. The
reporter's screenshot shows `B` on an `EXIT` icon starting the mission, so the
navigation is evidently more specific than these scripts.

**Do not treat sub-symptom 1 as fully fixed.** The correct next step is a
**savestate taken on the SELECT WEAPONS screen** (there is no Iron Soldier state
in `test/roms/private/states/` yet); `--load-state` then makes mission start a
one-press test and removes all navigation guesswork.

Earlier stages all render correctly too: title screen (frame 300), intro
cinematic (1500), main menu (700–950), mission select (1100), mission briefing
(1300), SELECT WEAPONS / SELECT MOUNT with a solid-shaded polygon mech (1800,
2400).

The `video_stall` lines seen at frames 1576/4217/5025 fire on **static menu
screens at 85–92 % non-black** — a menu that legitimately stops changing, not a
black screen. This is the same known-benign class documented in CLAUDE.md.

### Sub-symptom 2 — missing wireframe tank with Fast Blitter ON: STILL REPRODUCES

> *"There is no wireframe polygon rotating tank visual in the mission briefing
> window with Fast Blitter **ON**."*

Reproduced exactly, at the mission-briefing screen (frame 1350), identical input
script, only the blitter option changed:

| `virtualjaguar_usefastblitter` | left briefing panel |
|---|---|
| `enabled` (fast, default) | **empty green grid — no tank** |
| `disabled` (accurate) | **wireframe tank clearly drawn** |

This is a **fast-blitter** bug (the fast path fails to draw geometry the accurate
path draws) — the *opposite* direction from #189, where accurate was the wrong
one.

*Lead:* the tank is a wireframe, so it is drawn as **lines**, most likely
Gouraud-shaded / one-pixel-wide blits — compare the two `GOURD` paths in
`src/tom/blitter.c` (`blitter_blit`/`blitter_generic` vs `BlitterMidsummer2`).
First experiment: run
`test_blitter_compare` on Iron Soldier with a frame window over the briefing
screen and a `--cmd-filter` selecting `GOURD` (`cmd & 0x00001000`) blits, and
compare pixel output for those specific blits:

```bash
./test/tools/test_blitter_compare ./virtualjaguar_libretro.dylib \
  "$R/Iron Soldier (1994).jag" 1400 \
  --frame-window 1250 1400 --cmd-filter 0x00001000 0x00001000 --verbose-dump
```

### Sub-symptom 3 — `*` + `#` reset not recognised: NOT A CORE BUG

> *"It is not possible to reset the game … by holding both the Asterisk (\*) and
> Pound (#) buttons"*

The full 12-key Jaguar keypad **is** implemented and mappable. From
`libretro_core_options.h`:

```c
{ "num_7",  "Numpad 7" }, { "num_8", "Numpad 8" }, { "num_9", "Numpad 9" },
{ "star",   "Numpad *" }, { "hash",  "Numpad #" },
```

with matching `BUTTON_s` / `BUTTON_d` entries in `libretro.c:190-191`. The
reporter's own log confirms the cause: the **defaults** bind only `num_0`–`num_6`
across the available RetroPad buttons, so `*` and `#` are simply unbound out of
the box. Holding both additionally needs two buttons bound simultaneously.

This is a defaults/documentation matter, not an emulation bug. Recommend closing
this sub-item with a note on how to bind `star`/`hash`.

### Repro

```bash
# 3D DEMO mode, all 4 combos (main menu -> 3x down -> B).
# BIOS mode shifts every menu step ~600 frames later, hence the offset.
for BL in enabled disabled; do for BI in off on; do
  BIOSFLAG=""; O=0
  [ "$BI" = on ] && { BIOSFLAG=--bios; O=600; }
  ./test/tools/cd_visual_verify ./virtualjaguar_libretro.dylib \
    "$R/Iron Soldier (1994).jag" --frames $((3400+O)) $BIOSFLAG \
    --press $((400+O)):b:8   --press $((600+O)):b:8 \
    --press $((750+O)):down:6 --press $((810+O)):down:6 \
    --press $((870+O)):down:6 --press $((950+O)):b:10 \
    --option virtualjaguar_usefastblitter=$BL \
    --option virtualjaguar_crash_detect=verbose \
    --outdir "/tmp/isdemo_${BL}_${BI}" --shot-every 200
done; done
# read frame_02000/frame_02600 of any combo -> cockpit + 3D city, no black screen

# wireframe-tank A/B at the mission briefing (compare frame_01350 of each)
for M in enabled disabled; do
  ./test/tools/cd_visual_verify ./virtualjaguar_libretro.dylib \
    "$R/Iron Soldier (1994).jag" --frames 1400 \
    --press 400:b:8 --press 600:b:8 --press 800:b:8 --press 1200:b:8 \
    --option virtualjaguar_usefastblitter=$M \
    --outdir /tmp/is_$M --shot-every 50
done
```

---

## #187 — Tempest 2000 graphical glitches

**Verdict: NOT OBJECTIVELY VERIFIABLE HEADLESS.** Reported as such deliberately —
the two mechanisms that *could* have been measured are both ruled out, but the
symptom itself cannot be adjudicated without a reference.

The reported symptom is *"part of the scene flickers or renders weird content for
a few frames"*. Tempest 2000 is continuous high-motion vector-style rendering, so
no headless metric available here can separate "wrong content for a few frames"
from an intended particle/warp effect:

* `test/tools/flicker_detect.c` exists but is the wrong instrument for this: its
  score is a 16-frame per-pixel temporal standard deviation, which saturates on
  any high-motion title. It also **no-ops its log callback**, so it surfaces zero
  crash signatures, and it has **no `--option`**, so the blitter cannot be A/B'd
  with it.
* A frame-to-frame delta timeline has the same problem — a spike is
  indistinguishable from a legitimate explosion without a known-good reference.

### What *was* measured (all negative)

Run in the reporter's configuration — **Real BIOS + Fast Blitter** — driven into
gameplay, 3600 frames:

| measurement | result |
|---|---|
| crash-detect signatures (verbose) | **none** — no `gpu_pc_escape`, `dsp_pc_escape`, `gpu_wedge`, `dsp_wedge`, `video_stall` |
| geometry churn | **none** — stable 326×240 for all 3600 frames, single geometry negotiation |
| frame delivery | 3600/3600 frames rendered, 60/60 frames changing in every window from gameplay onward |
| audio | RMS 1530, 2 664 099 / 2 880 000 samples non-silent, 25 dropouts |
| **fast vs accurate blitter** | **133 656 blits compared, 0 pixel differences (0.00 %, "IDENTICAL")** |

Gameplay screenshots read directly show the web/tube, the claw ship, enemies and
starfield rendering correctly.

The blitter result is the most useful of these: across 900 frames the two blitter
paths produce **pixel-identical** output for every one of 133 656 blits. The only
divergence is the same `A2_PIXEL` writeback issue tracked in #189
(`fast=0x00000348` vs `acc=0x0000034C`, a one-phrase difference), with `A1_PIXEL`
identical. **So whatever the reporter is seeing is not a fast-vs-accurate blitter
divergence, and it is not geometry churn.**

### Next-step lead

1. **Ask the reporter for a reference.** A short video capture, or better a
   savestate saved a second before a glitch. With a savestate this becomes a
   two-command investigation; without one, any verdict is manufactured.
2. Their comparison target is BigPEmu, and their platform is **RetroArch
   Android** — worth confirming whether the same glitch appears in RetroArch on
   desktop, since a platform-specific presentation path would change the
   subsystem entirely.
3. If a savestate arrives, the first objective instrument is a **per-frame
   framebuffer hash plus an OP object-list dump** on the glitching frames
   (extend `test/tools/test_op_gpu_object.c`), not the blitter — the blitter is
   already exonerated for this title.

### Repro

```bash
./test/tools/test_blitter_compare ./virtualjaguar_libretro.dylib \
  "$R/Tempest 2000 (1994).jag" 900              # -> 133656 blits, 0 diffs

./test/tools/cd_visual_verify ./virtualjaguar_libretro.dylib \
  "$R/Tempest 2000 (1994).jag" --frames 3600 --bios \
  --press 700:b:8 --press 1000:b:8 --press 1300:b:8 --press 1600:b:8 \
  --option virtualjaguar_usefastblitter=enabled \
  --option virtualjaguar_crash_detect=verbose \
  --outdir /tmp/t2k --shot-every 200
```

---

## #189 — accurate blitter `A1_PIXEL`/`A2_PIXEL` writeback divergence

**Verdict: CHANGED — largely fixed. The headline claim no longer holds; a
narrower `A2_PIXEL`-only divergence remains with no known visible symptom.**

### Control: identical workload

The issue measured Battle Sphere Gold at frame 60 from
`Battle Sphere Gold (World).state1` and reported **676 differing blits out of
3190**. The same state on `3ee002d` produces **exactly 3190 blits**, confirming
the pre-merge savestate still restores the same workload and that the two numbers
are directly comparable.

### Pixel-output divergence: eliminated

```
=== BLITTER COMPARISON SUMMARY ===
Total blits compared:  3190
Blits with differences: 0
Blits skipped:         0
Difference rate:       0.00%
Result: IDENTICAL
```

676/3190 (21.2 %) → **0/3190 (0.00 %)**.

### `A1_PIXEL` divergence: eliminated

The issue's smoking-gun lines showed *both* pointers diverging:

```
REG DIFF A1pix fast=001F0044 acc=FFF7003E A2pix fast=00920085 acc=009100AD
REG DIFF A1pix fast=0001FFFA acc=00120006 A2pix fast=00E100A1 acc=00E000A5
```

On `3ee002d`, `A1pix` is **identical on every emitted line**:

```
REG DIFF A1pix fast=00F00000 acc=00F00000 A2pix fast=00002F8C acc=0000038C
REG DIFF A1pix fast=00F00000 acc=00F00000 A2pix fast=00002E76 acc=00000276
REG DIFF A1pix fast=00840000 acc=00840000 A2pix fast=0000071C acc=000001A4
REG DIFF A1pix fast=00170958 acc=00170958 A2pix fast=00850E60 acc=00850000
```

`A1_PIXEL` was the register the issue identified as the chained-divergence
carrier ("games that chain sprite blits read those registers back to set up the
next blit"), so this is the substantive part of the report being fixed.

### `A2_PIXEL` divergence: STILL REPRODUCES

Every `REG DIFF` line above is triggered solely by `A2_PIXEL`. The dominant
pattern is a **constant `0x2C00` offset in the X field** with the low 10 bits
agreeing:

| fast X | acc X | `fast & 0x3FF` | `acc & 0x3FF` | delta |
|---|---|---|---|---|
| `0x2F8C` | `0x038C` | `0x38C` | `0x38C` | `0x2C00` |
| `0x2E76` | `0x0276` | `0x276` | `0x276` | `0x2C00` |

The low-10-bit agreement points at a **field-width / masking difference in the A2
X writeback**, not at the inner-loop step math the issue blamed. Two lines do
*not* fit that pattern (`0x071C` vs `0x01A4`, and `0x0E60` vs `0x0000`), so there
is likely a second, smaller effect as well. The same class shows up in Tempest
2000 (`0x348` vs `0x34C`), so it is not BSG-specific.

Note the `REG DIFF` emitter is capped at `blit_cmp_logged < 10`
(`src/tom/blitter_compare.c:373`), so line count is **not** a magnitude measure —
only presence/absence is meaningful.

### Visible symptom: NO LONGER REPRODUCES

The issue states accurate "renders a solid black square where the crosshair /
hyperdrive effect should be transparent (fast renders correctly)". Measured at
the hyperdrive-activated scene in `state1`, 240 frames, both blitter modes:

| blitter | max non-black | screenshot |
|---|---|---|
| accurate (`usefastblitter=disabled`) | 20478 / 78240 (26.2 %) | hyperdrive sparkle renders |
| fast (`usefastblitter=enabled`) | 20664 / 78240 (26.4 %) | hyperdrive sparkle renders |

A 0.9 % non-black delta with no structural difference. Screenshots at frame 180
were read directly and are visually indistinguishable — the blue/violet
hyperdrive sparkle, the "HYPERDRIVE ACTIVATED" text, both radar domes and both
side gauges render in both modes. **There is no black square in accurate mode.**

### Next-step lead

The remaining `A2_PIXEL` divergence has **no known visible symptom** on
`3ee002d`, so this should be re-scoped and downgraded, not closed:

1. Subsystem: `src/tom/blitter.c` — the **A2 X writeback specifically**, not the
   A1 step math. Fast keeps packed 16.16 (`blitter.c` ~899-904); accurate uses
   the `ADDAMUX`/`ADDBMUX`/`ADDRADD` pipeline (~1807-1818, 3336-3355).
2. First experiment: decide which side is JTRM-correct for the **A2 X field
   width**. Check `docs/jtrm-blitter.md` (address generators / A2 Step Value) for
   whether A2 X is 12-bit or 16-bit, then hand-compute the expected writeback for
   the `0x2F8C`/`0x038C` case. A constant `0x2C00` offset with matching low 10
   bits is exactly what a missing or extra mask looks like.
3. Replace the `blit_cmp_logged < 10` cap with a dedicated `blit_cmp_reg_diffs`
   counter (split A1 vs A2) so magnitude is measurable before/after a fix.
4. Independently verify the two secondary findings from the issue, which this
   pass did **not** re-check: fast not reading destination Z from memory when
   `DSTENZ=1`, and the fast Z-compare lane selection.
5. **#180 was re-checked in this pass and is also fixed** — see below. Both
   visible symptoms the issue claimed as consequences of the writeback divergence
   are now gone, which is what justifies downgrading rather than closing: the
   register divergence persists but nothing visibly depends on it.

### Bonus: #180 (BIOS cube edges) — NO LONGER REPRODUCES

#189 names **#180** ("multi-coloured pixels on the BIOS animation cube top edge
when fast blitter is off") as the same root cause. The AvP BIOS-on matrix already
captured the whole cube animation at `--shot-every 25` under both blitters, so
this is decidable by byte comparison:

```
identical=37 differing=0
  frame_00275: IDENTICAL   frame_00350: IDENTICAL   frame_00425: IDENTICAL
  frame_00300: IDENTICAL   frame_00375: IDENTICAL   frame_00450: IDENTICAL
  frame_00325: IDENTICAL   frame_00400: IDENTICAL
```

**All 37 BIOS-on frames — the entire boot cube animation — are byte-identical
between fast and accurate.** Not merely equal non-black counts: identical pixels.
#180 is closeable.

```bash
# reproduce: run the AvP matrix (below) for both blitters with --bios, then
for f in /tmp/avp_enabled--bios/frame_*.ppm; do
  cmp -s "$f" "/tmp/avp_disabled--bios/$(basename "$f")" || echo "DIFFERS: $f"
done
```

### Repro

```bash
./test/tools/test_blitter_compare ./virtualjaguar_libretro.dylib \
  "$S/Battle Sphere Gold (World).j64" 60 \
  --load-state "$S/Battle Sphere Gold (World).state1"

for M in enabled disabled; do
  ./test/tools/cd_visual_verify ./virtualjaguar_libretro.dylib \
    "$S/Battle Sphere Gold (World).j64" --frames 240 \
    --load-state "$S/Battle Sphere Gold (World).state1" \
    --option virtualjaguar_usefastblitter=$M \
    --outdir /tmp/bsg_$M --shot-every 60
done
```

---

## Cross-cutting observations

1. **The two blitter paths now agree far more than they used to.** Zero pixel
   differences across BSG (0/3190), Tempest 2000 (0/133 656) and the AvP BIOS
   cube (37/37 frames byte-identical). Only `A2_PIXEL` writeback still diverges.
   Any open ticket that blames "fast vs accurate divergence" should be
   re-measured on this build before work starts. **The one genuine exception
   found is Iron Soldier's wireframe tank (#186), where *fast* is the broken
   path** — so the remaining blitter divergences are in the fast path, not the
   accurate one, inverting the historical assumption.
2. **Headless PASS/FAIL is not a crash oracle.** `cd_visual_verify` reports
   "2 passed, 0 failed" and exits 0 on Pitfall's fully-wedged run, because
   frames are still delivered and audio still flows. Any automation built on
   these harnesses (bisect scripts especially) must grep the log for
   `nonblack +0/` or a `CRASH-DETECT` line rather than trusting the exit status.
3. **`video_stall` fires on legitimately static menus.** Iron Soldier
   (frames 1576/4217/5025) and AvP (1644) all triggered it at 85–92 % non-black.
   Always corroborate a `video_stall` line with the non-black count before
   treating it as a crash — the Pitfall case is a real wedge precisely because
   non-black is **0** and stays there.
4. **The cyan first-frame bar is core-wide, not per-title.** Identical
   `rgb(0,255,255)` 320×3 bar at frame 0 of both AvP and Pitfall in BIOS-off
   mode. Worth one small fix and one new ticket.
5. **Savestate compatibility is uneven.** `Battle Sphere Gold (World).state1`
   restores correctly (verified by an exact 3190-blit match against a pre-merge
   measurement), but both AvP states are rejected by `retro_unserialize`.
6. **Tooling gaps found:** no GPU traceback ring symbols for `cd_wedge_probe`
   (`gpu_pc_ring*` unresolved); `flicker_detect.c` swallows core logs and cannot
   set core options; `test_blitter_compare`'s register-diff reporting is capped at
   10 lines with no counter.

## `Brutal Sports Football (1994) (Telegames).jag` rejected at load — prepended copier header

Found incidentally during a 40-title sweep: this dump is refused while every
other cartridge in the same local set loads.

```
[CART] JaguarLoadFile rejected the content
[Virtual Jaguar] unsupported or invalid content format
```

**Cause: a 512-byte copier/dumper header glued to the front of a byte-perfect
image.** Not a bad dump, and not a size-gate bug.

| field | value |
|---|---|
| file size | 2 097 664 = 2 MiB **+ 512** |
| header bytes | `00 01 30 00  00 00 00 00  AA BB 04 00`, then zero-fill to `0x200` |
| whole-file CRC32 | `0x0FDCEB66` → `src/core/filedb.c` *"Brutal Sports Football (World)"*, `FF_ROM \| FF_BAD_DUMP` |
| payload CRC32 (skip 512) | `0xBCB1A4BF` → same title, `FF_ROM \| FF_VERIFIED` |

The payload is **byte-identical** to the verified dump — only the container is
wrong — so the loader now skips the header instead of refusing the file.

### Detection

`DetectPrependedHeaderSize` (`src/core/file.c`) requires *both*:

* the file overhangs a whole number of megabytes by exactly 512 bytes, and
* the cartridge universal-header marker `$04040404` — which precedes the run
  address `JaguarLoadFile` reads from ROM offset `$404` — appears at that offset
  measured **from the payload** (file offset `0x600` here, vs `0x400` for a
  headerless image).

The size test alone would start accepting arbitrary junk, so both must hold.
Stripping happens before the CRC is taken, before `EepromInit`, and before type
detection, so the core reports the payload's identity (`0xBCB1A4BF`) rather than
the file's — a headered image and a `dd`-stripped one are indistinguishable
downstream.

Across the 139-image local set, this file is the only one with that size shape,
and it carries the marker.

### Not a power-of-two gate

The size rule is `size % 1 MiB == 0` (plus the 131 072-byte Memory Track size),
never a power of two: **84 of 87** distinct file sizes in the local set are
non-power-of-two and load fine via `JST_ALPINE`, the ABS/COFF headers, or the
raw-binary heuristic. There is no "rejects any cart whose size is not a power of
two" class of breakage.

### Verified

Same build, headered original vs `dd bs=512 skip=1` payload, 1200 frames each —
identical on every measure:

| check | headered | stripped |
|---|---|---|
| load | OK | OK |
| `jaguarMainROMCRC32` | `BCB1A4BF` | `BCB1A4BF` |
| `jaguarROMSize` | 2 097 152 | 2 097 152 |
| non-black peak | 71 856 / 78 240 (91.8 %) | 71 856 / 78 240 |
| non-black mean | 37.77 % | 37.77 % |
| audio | RMS 997.3, onset frame 118 | RMS 997.3, onset frame 118 |

Regression coverage is `test/test_cart_format.c` (in `make test`): synthesises
images in memory, asserts the headered one loads with the payload's CRC and
size, and asserts a 512-over-a-MiB image *without* the marker, a 256-byte
overhang, and a lone 512-byte file all stay rejected.
