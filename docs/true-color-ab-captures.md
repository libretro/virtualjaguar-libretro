# True-color A/B captures — measured, issue #506

Evidence pass for issue #506 part 2 (colour/upscaling A/B media). Scope was
`virtualjaguar_true_color` ("True Color (Gouraud Precision)") — the CRY
16bpp-only Gouraud-banding reducer, **not** the general CRY/RGB16 -> RGB888
path (#529, still open). Goal: produce additional true-color A/B pairs beyond
the one already published (Cybermorph, `site/assets/truecolor_ab_cybermorph.png`
+ `cyber_off_850.png`/`cyber_on_850.png`, from PR #341's validation run).

**Result: no new pairs met the "genuinely visible" bar.** Every titledb row
sampled beyond Cybermorph measured **0.0000% changed pixels** between
`true_color=disabled` and `true_color=enabled` at a fixed, scripted frame.
This is recorded here — with the reproduction recipe — instead of a padded
site addition, per the issue's explicit rule that a null published as a win
is worse than no pair at all.

## Headline finding: the titledb heuristic does not predict a visible difference

`src/core/titledb.c`'s derivation policy (top-of-file comment) sets
`virtualjaguar_true_color=enabled` whenever a title's census measured
`(shaded blits / frames) >= 10` — a blit-count heuristic. Its own comment
states only the seed entries (AvP, Cybermorph) were checked at all, and only
**Cybermorph** was ever screenshot-verified to actually change the displayed
frame (the PR #341 capture this doc's Cybermorph run reproduces below).

Three other rows that clear the threshold by wide margins — **Alien vs
Predator** (544.3k shaded / 4800f = 113/f), **Missile Command 3D** (3.96M
shaded / 5.73M blits), and **Doom** (~1.07M shaded, effectively all of its
blits) — were measured here and every one produced **zero** changed pixels
at a sampled gameplay frame with true-color on vs off. The heuristic
threshold does not imply a visible result; it implies enough *blitter*
Gouraud traffic somewhere in the run, which is not the same claim.

This is not a report that true-color "does nothing" on these titles in
general — see the per-title caveats below — but it does mean the issue's
implicit premise (more true-color candidates are sitting in titledb ready
to screenshot) did not hold for any row tried. Recorded here rather than
edited in titledb.c, which is out of scope for this no-code issue.

## Method — deterministic, one option changed

Built once per session:

```bash
DEVELOPER_DIR=/Library/Developer/CommandLineTools make -j"$(getconf _NPROCESSORS_ONLN)" TEST_EXPORTS=1
cc -O2 -Wall -std=c99 -I. -I./test/harness -I./libretro-common/include \
   -o test/tools/hires_shot test/tools/hires_shot.c test/harness/harness.c -ldl -lm
```

Every capture pair uses the **same four explicit options** except the one
under test, so the only variable is `virtualjaguar_true_color`:

```
--option virtualjaguar_internal_resolution=1x        # isolates true-color from hi-res
--option virtualjaguar_usefastblitter=disabled        # Accurate engine, the shipped default
--option virtualjaguar_pertitle_defaults=disabled     # explicit options only, no titledb override
--option virtualjaguar_true_color=disabled|enabled    # the variable under test
```

Same ROM, same `--press` ladder, same `--shot` frame for both runs of a pair
— `hires_shot` dumps a PPM per shot; the two PPMs are diffed pixel-for-pixel
with a small Python/PIL script (unique-color count + % changed pixels,
matching the methodology already used for the shipped Cybermorph figure).

Reproduce any row below with:

```bash
VJ_EXPECT_BUILD=$(./scripts/build-id.sh) ./test/tools/hires_shot \
  ./virtualjaguar_libretro.dylib "<rom>" \
  --option virtualjaguar_internal_resolution=1x \
  --option virtualjaguar_usefastblitter=disabled \
  --option virtualjaguar_pertitle_defaults=disabled \
  --option virtualjaguar_true_color=<disabled|enabled> \
  <--press ladder> --frames <N+1> --shot <N> \
  --out-prefix /tmp/<label>/<name>
```

## Control: Cybermorph reproduces the shipped result

`test/roms/private/ROMS/Cybermorph (1993).jag`, press ladder
`200:a 400:a 600:a 900:a 1200:a`, shot frame 1400 (reaches the in-flight HUD
scene, pods-remaining view):

| | unique colors | changed pixels |
|---|---|---|
| off (`disabled`) | 303 | — |
| on (`enabled`) | 498 | 30548 / 78240 = **39.04%** |

This is the same magnitude as the shipped figure's caption (304 -> 450 unique
colors, 45.9% of pixels, from a different captured frame/session) — close
enough on an independently-scripted boot-to-gameplay run to confirm the
capture pipeline genuinely detects true-color's effect when it exists, and
that the headless framebuffer read path (see `CLAUDE.md`'s caveat) is not
masking anything here.

**Caution logged in the process:** the frame number alone is not enough to
identify a scene. Frame 850 with *no* input at all lands on the closing
credits screen (43 unique colors, of course 0% diff off vs on) — completely
different from the frame 850 used for the shipped `cyber_off_850.png` /
`cyber_on_850.png`, which was reached by starting a mission first. A
capture recipe must record its full input ladder, not just a frame number.

## Measured nulls

All four options above held constant except `true_color`; ROM paths are
under `test/roms/private/ROMS/`.

### Alien vs Predator (1994).jag — 0.0000%, most thoroughly checked

Press ladder from `docs/avp-renderer-analysis.md` §9 (the same recipe behind
the existing `hires_ab_avp_*` site assets):
`3300:a 3600:a 3900:a 4200:a 4500:a 5000:a`, shot frame 6000 (static corridor,
facing a door).

- Static corridor (frame 6000): 0 / 78240 changed, unique colors identical
  (8701 == 8701).
- Moving variant (`+ --press 5600:up:400`, same shot frame): 0 / 78240
  changed, unique colors identical (4304 == 4304).
- Full sweep, `--shot-every 100` across frames 100-6000 (61 sampled frames,
  spanning the title screen, menu, static corridor, and the moving-into-door
  scene): **maximum diff across all 61 frames was 0.0000%.**
- Ruled out the video-mode hypothesis (RGB16 mode 3 has no chroma/intensity
  split for true-color to refine, per the comment in `src/tom/tom.c`'s RGB16
  hires renderer): probed `TOMGetVideoMode()` via `harness_dlsym` at the
  captured frame — returns `0` (CRY), not `3`. So this is not an
  RGB16-video-mode exclusion; the option is live and applicable, and still
  produces zero change.

AvP is the one row in titledb whose true-color entry the top-of-file comment
says was independently checked against its own thresholds (not just fallen
out of the blanket policy pass) — and it is also the one with the widest,
most-varied sampling here, and it never showed a difference.

### Missile Command 3D (1995).jag — 0.0000% at the sampled frame

Press ladder `200:a 500:a 800:a 1100:a 1400:a`, shot frame 1400 — reaches
"Original 3D mode" gameplay: mountains against a blue sky gradient, a strong
visual banding candidate if true-color were reaching the displayed pixels
here. 0 / 78240 changed pixels, unique colors identical (1880 == 1880).
Video mode confirmed 0 (CRY). Single frame only — not swept like AvP.

### Doom (World) EX.j64 — 0.0000% at the sampled frame

No `--press` needed; the title autoplays its E1M1 attract demo. Shot frame
1600 (in-level combat, matches the scene family already used for
`hires_ab_doomex_*`). 0 / 78240 changed pixels, unique colors identical
(525 == 525). Video mode confirmed 0 (CRY). Single frame only.

### I-War (1995).jag — gameplay not reached, not a null

Tried `200:a 500:a 800:a 1100:a 1400:a 1700:a` and a `--shot-every 200` sweep
to frame 2400: landed on the ship-select screen (frame ~1000) and a
"PREPARE FOR NODE ALPHA MATRIX" transition screen (frame ~1600), never
in-flight gameplay. I-War's menu flow needs a different input sequence than
the generic "mash `a`" ladder that worked for the other titles here. Not
tested further this pass — report as unreached, not as a measured zero.

## Issue #529 follow-up: Iron Soldier 2 and Battle Morph

The rows above were captured for issue #506's narrower scope. Issue #529
(the general CRY/RGB16 -> RGB888 "true color" ask) separately named Iron
Soldier 2 and Battle Morph as the titles with the "biggest visual win" —
neither had ever been measured or given a titledb row. Re-verified with the
exact same method as the rest of this document (same four held-constant
options, same PPM pixel-diff, same `TOMGetVideoMode()` probe).

### Iron Soldier 2 (World).j64 — 0.0000%, real gameplay, 4-frame sweep

Press ladder to reach in-mission, standing-in-city 3D gameplay from a cold
boot (main menu -> mission-select gallery -> mission briefing -> weapon
loadout -> gameplay; every screen confirms with `b`, not `a` — `a` alone
never advances any menu in this title, which is why an early attempt mashing
just `a` sat on the main menu forever):

```
--press 1250:b:20 --press 1700:b:20 --press 1900:b:20
--press 2300:right:10 --press 2330:right:10 --press 2360:right:10 --press 2390:right:10
--press 2420:right:10 --press 2450:right:10 --press 2480:right:10 --press 2510:right:10
--press 2600:b:20 --press 3000:left:40 --press 3200:left:40
```

The eight `right` presses at 2300-2510 walk the weapon-loadout cursor off
the end of the 8-slot weapon grid onto the screen's EXIT icon (confirmed by
screenshot — the icon lights up green only after the 8th `right`); `b` at
2600 then exits the loadout into gameplay. The two `left` turns at
3000/3200 rotate the mech in place from a city-block view to a desert/road
view, giving two distinct shaded scenes without needing forward-movement
collision logic.

Shot frames 2900 (city buildings, cockpit HUD), 3100 (same, slightly
different lighting), 3300 and 3500 (post-turn desert/road view) — all
confirmed real gameplay by screenshot (mech cockpit frame, shaded 3D
buildings/terrain, live radar), not a menu or static screen.

| frame | video mode | unique colors off | unique colors on | changed pixels |
|---|---|---|---|---|
| 2900 | 0 (CRY) | 938 | 938 | 0 / 78240 = 0.0000% |
| 3100 | 0 (CRY) | 999 | 999 | 0 / 78240 = 0.0000% |
| 3300 | 0 (CRY) | 1013 | 1013 | 0 / 78240 = 0.0000% |
| 3500 | 0 (CRY) | 1014 | 1014 | 0 / 78240 = 0.0000% |

All four sampled frames confirmed CRY mode (not RGB16/mixed) via
`TOMGetVideoMode()` at the exact shot frame. Maximum diff across the sweep
was 0.0000% — the same null pattern as AvP/MC3D/Doom above. No titledb row
added (CRC `0xD6C19E34`, `src/core/filedb.c` line 103, available if this is
ever re-measured and comes back positive).

Reproduce with:

```bash
./test/tools/hires_shot ./virtualjaguar_libretro.dylib \
  "test/roms/private/ROMS/Iron Soldier 2 (World).j64" \
  --option virtualjaguar_internal_resolution=1x \
  --option virtualjaguar_usefastblitter=disabled \
  --option virtualjaguar_pertitle_defaults=disabled \
  --option virtualjaguar_true_color=<disabled|enabled> \
  --press 1250:b:20 --press 1700:b:20 --press 1900:b:20 \
  --press 2300:right:10 --press 2330:right:10 --press 2360:right:10 --press 2390:right:10 \
  --press 2420:right:10 --press 2450:right:10 --press 2480:right:10 --press 2510:right:10 \
  --press 2600:b:20 --press 3000:left:40 --press 3200:left:40 \
  --frames 3601 --shot 2900 --shot 3100 --shot 3300 --shot 3500 \
  --out-prefix /tmp/is2/<label>
```

### Battle Morph (USA).cue — measured POSITIVE, but cannot ship (CD, no titledb path)

Battle Morph is CD-only. `libretro.c`'s `retro_load_game()` explicitly skips
the titledb CRC match for CD content (`is_cd_content` guard, comment: "v1
only covers cartridge CRCs") — **no titledb row is possible for Battle
Morph regardless of measurement outcome**, positive or null. This result is
recorded here as documentation only; it cannot become an enhancement
default until titledb grows CD-content keying (out of scope for this pass).

The two committed savestates under
`test/roms/private/Jaguar CD/BinCue/Battle Morph (USA)/*.state*` are stale —
both rejected by `retro_unserialize` (`core state is 616071/569044 bytes,
core expects 2621440`), i.e. from a savestate format predating the current
version. Reached gameplay from a cold boot instead, boot mode `hle` (the
boot-matrix-recommended, default mode for this title — `docs/cd-boot-matrix.md`
shows `bios` mode hitting a `cd_seek_wedge`/`pc_escape` on this same disc).

#### Press ladder exploration

Adapting the Cybermorph ladder (`200:a 400:a 600:a 900:a 1200:a`) got the
disc to boot and land on a "SELECT GAME" screen (`- NEW GAME -` x6, "B TO
ENTER" prompt) by frame ~400, but every subsequent screen in this title also
confirms with `b`, not `a` — same convention as Iron Soldier 2 above, and
the same trap: an `a`-only ladder sits on the select-game screen forever.
From there the full menu chain to gameplay, confirmed screen-by-screen by
screenshot, needed six more `b`-gated screens plus one name-entry sub-screen
and one weapon-loadout sub-screen navigated with `down`/`right`:

1. `500:b` — SELECT GAME -> highlights "- NEW GAME -", enters name entry
   (an on-screen A-Z/0-9 keyboard grid with a checkmark at the end).
2. `4100:b` — types the letter "A" (cursor starts on the grid's "A" cell).
3. `4200-4320`: five `down` presses (10-frame holds, 30 frames apart) walk
   the keyboard cursor down to the row with `0` and the checkmark.
4. `4400-4520`: five `right` presses walk across that row to the `0` cell
   (one short of the checkmark — confirmed by screenshot, cursor sitting on
   `0` with the entered name showing "A_").
5. `4700:right` — one more step lands on the checkmark.
6. `4800:b` — confirms the name -> "SELECT DIFFICULTY" screen (EASY /
   **MEDIUM** / HARD, MEDIUM pre-highlighted).
7. `6100:b` — confirms MEDIUM -> back to a game-slot "SELECT GAME" screen,
   now showing the new save ("A", Zephyr Cluster, Score 0, Ships 2) with a
   SELECT/ERASE choice, SELECT pre-highlighted.
8. `6900:b` — confirms SELECT -> "SELECT PLANET" screen (rotating starfield
   / planet map, Zephyr Cluster / Planet Penter).
9. `8200:b` — confirms the highlighted planet -> "PENTER BRIEFING" mission
   text screen, ACCEPT/REJECT choice, ACCEPT pre-highlighted.
10. `9200:b` — confirms ACCEPT -> "SELECT WEAPON" pre-launch loadout screen
    (BAY A-D slots, all empty, LAUNCH/BRIEF choices below the bay list).
11. `10200-10290`: four `down` presses walk the cursor past BAY A/B/C/D
    onto LAUNCH.
12. `10400:b` — confirms LAUNCH -> a launch cinematic (large, multi-frame
    swings in the `hires_shot` VARIANCE metric from frame ~10700 to
    ~11500, including one all-black frame at 11500 — a fade transition),
    settling into stable in-flight gameplay by frame ~11600: cockpit-view
    ship flying over a shaded canyon/mesa landscape, HUD (shield count,
    radar, altimeter) overlaid.

#### Measurement

Sampled 5 frames across the confirmed in-flight scene (frames 11800-13400,
all screenshot-verified as the cockpit-over-terrain gameplay view, not a
menu or HUD-only screen; frame-to-frame `hires_shot` VARIANCE moves
continuously through this whole range, consistent with the terrain
scrolling under the ship rather than a static frame):

| frame | video mode | unique colors off | unique colors on | changed pixels |
|---|---|---|---|---|
| 11800 | 0 (CRY) | 808 | 1038 | 13472 / 78240 = 17.2188% |
| 12200 | — | 796 | 1039 | 13894 / 78240 = 17.7582% |
| 12600 | 0 (CRY) | 759 | 955 | 15642 / 78240 = 19.9923% |
| 13000 | — | 696 | 885 | 15042 / 78240 = 19.2255% |
| 13400 | — | 605 | 751 | 9870 / 78240 = 12.6150% |

Video mode confirmed CRY (0) at the two sampled endpoints (11800, 12600);
the whole 11800-13400 span is one continuous flight scene with no
menu/video-mode transition observed in the exploratory sweep, so the
endpoints stand for the range. Every frame's `max_per_channel_delta` was 1
(`avg_delta_over_changed` 1.00) — the identical signature as the Cybermorph
control run above (also delta 1, avg 1.00) — consistent with true_color's
actual mechanism (recovering sub-integer Gouraud precision that 8-bit CRY
quantizes away), not an unrelated rendering change. Visually the off/on
screenshots at frame 12600 are near-identical at a glance (subtle terrain/sky
gradient smoothing) — smaller in magnitude than Cybermorph's 39.04% but the
same kind of effect, swept consistently positive across all 5 frames
(12.6%-20.0%, never near zero), with unique-color counts jumping 24-30% at
every sample. This clears the "genuinely visible, not noise" bar the doc
uses elsewhere.

**Outcome: real, measured, but not actionable.** Because Battle Morph is
CD-only and titledb cannot key on CD content (see above), this cannot become
a shipped enhancement default under the current mechanism. Recorded here so
a future CD-content-keying effort (or a manual per-title note in
documentation/UX) has the evidence already in hand rather than needing to
re-derive this 12-step boot ladder.

Reproduce with (swap `--option virtualjaguar_true_color=` and diff the
PPMs):

```bash
./test/tools/hires_shot ./virtualjaguar_libretro.dylib \
  "test/roms/private/Jaguar CD/BinCue/Battle Morph (USA)/Battle Morph (USA).cue" \
  --option virtualjaguar_internal_resolution=1x \
  --option virtualjaguar_usefastblitter=disabled \
  --option virtualjaguar_pertitle_defaults=disabled \
  --option virtualjaguar_true_color=<disabled|enabled> \
  --option virtualjaguar_cd_boot_mode=hle \
  --press 200:a --press 500:b:20 \
  --press 4100:b:15 \
  --press 4200:down:10 --press 4230:down:10 --press 4260:down:10 --press 4290:down:10 --press 4320:down:10 \
  --press 4400:right:10 --press 4430:right:10 --press 4460:right:10 --press 4490:right:10 --press 4520:right:10 \
  --press 4700:right:10 \
  --press 4800:b:15 \
  --press 6100:b:15 \
  --press 6900:b:15 \
  --press 8200:b:15 \
  --press 9200:b:15 \
  --press 10200:down:10 --press 10230:down:10 --press 10260:down:10 --press 10290:down:10 \
  --press 10400:b:15 \
  --frames 13401 --shot 11800 --shot 12200 --shot 12600 --shot 13000 --shot 13400 \
  --out-prefix /tmp/bm/<label>
```

## Texture replacement (#528 tier 1) — blocked, no pack exists

Searched for an actual texture-replacement pack to pair a "before" capture
with a real "after":

- Repo tree: `find . -iname "*texpack*"` — no hits.
- Private ROM tree: `find -L test/roms/private -iname "*texpack*" -o -iname "*texture*pack*"` — no hits.
- Home directory: `find -L /Users/jmattiello -maxdepth 4 -iname "*texpack*"` — no hits.

Only the mechanism exists: `src/tom/texdump.c` / `src/tom/texreplace.c`
(implementation), `test/tools/test_texdump.c` / `test/tools/test_texreplace.c`
(synthetic test fixtures, not real game art), `docs/texture-dump.md` (design
doc). Per the task guide, authoring replacement art is explicitly out of
scope — stopping here rather than fabricating a pack to screenshot.

## Outcome

No new `site/assets/*.png` were added and no `site/pages/*.html` changed.
For the #506 pass, the only genuinely visible true-color pair remained the
already-published Cybermorph one. This document exists so the next attempt
does not have to re-derive the capture recipe or re-discover which titledb
rows are heuristic-only versus actually verified.

**Update (#529 follow-up, see section above):** Battle Morph also measured
genuinely visible (12.6%-20.0% changed pixels across a 5-frame in-flight
sweep) — but as CD content it cannot get a titledb row under the current
v1 (cartridge-CRC-only) mechanism, so it is recorded here rather than
shipped. Iron Soldier 2 measured a clean null (0.0000%, real gameplay,
confirmed CRY mode), joining AvP/MC3D/Doom above.
