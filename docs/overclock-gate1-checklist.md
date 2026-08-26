# Overclock gate-1 checklist (#378)

Working checklist for the **mechanical gate** of #378: *does a clock scale actually lift the
internal frame rate of this title?* A preset only gets a per-title DB row if it clears this
gate **and** the human play-test gate that follows it.

Gate 1 is cheap and fully automatable — but it has produced **two retracted result tables**
already. Both failures were measurement bugs, not emulator bugs. Read §1 before running
anything.

---

## 1. The three traps that invalidated earlier runs

**Trap 1 — no input measures the title screen.** A run with no `--press` and no savestate
profiles the game wherever it lands unattended. A menu is by construction the *not
compute-bound* case, so a flat result across 1x/1.5x/2x says nothing about the game.

```
Cybermorph, RISC 1x, frames 300-900
  no input       64 transitions
  with input    599 transitions      <- 9.4x
```

**Trap 2 — the transitions counter saturates at the window length.** Over frames 300–900 the
ceiling is 600. Driven, Cybermorph and I-War both read exactly 600/600/600. A saturated
counter cannot show a gain.

> **`transitions == window length` is UNMEASURED, not a result.** The grid script flags this
> as `SATURATED` — if you see it, lengthen the window or pick a quieter scene, and do not
> compare arms.

**Trap 3 — a shared input mash perturbs the game.** Driving every title with one generic
press sequence made Doom read **0** (a press paused it) and halved Missile Command 3D at 1.5x.
Those are input artefacts, not clock-scale effects.

**The fix for all three is the same: a savestate dropped into real gameplay.** It removes the
perturbation entirely and puts every arm in an identical starting state.

---

## 2. What a good savestate looks like

Load the game in RetroArch, play into a **representative, sustained-load** moment, and save.

- **In gameplay, not a menu, not a cutscene, not a pause.** The whole point is to measure the
  engine under load.
- **Somewhere the scene keeps changing on its own** for the next ~15 seconds without input —
  driving forward, a firefight, a slope. If the picture goes static the counter reads a false
  null.
- **Avoid a scene so busy that every single frame differs** — that is what saturates the
  counter. A mid-load scene discriminates best.
- **Stock settings when you save.** 1x clocks, idle-skip off. The grid sets its own options;
  a state saved under a non-stock config biases every arm.

Drop the files in `jaguar-roms-private/states/`. Any filename is fine — the runner takes an
explicit path.

**Compressed states work — convert them first.** If RetroArch has "Save State Compression"
on it writes an rzip container, which `retro_unserialize` rejects on size (it sees ~270 KB
where the core wants 2.5 MB). Convert instead of re-saving:

```bash
python3 scripts/rzip_extract.py "<state>" /tmp/out.state
```

Files that are already raw are passed through unchanged, so it is safe to run over anything.
Old savestate *versions* are fine — the core accepts back to `STATE_MIN_VERSION`, and both
states measured above were older versions that loaded with only a size warning.

---

## 3. Status

ROMs for all eight candidates are present in `jaguar-roms-private/ROMS/`.

### Measured 2026-08-25

| Title | Savestate | Result |
|---|---|---|
| **Cybermorph** | `Cybermorph (1993).state1` | **CLEARS GATE 1** — +44.7% at RISC 1.5x, +72.8% at 2x |
| Iron Soldier | `iron_soldier_v104_f2400.state` | Null — clock-insensitive, confirmed by underclock control |
| Val D'Isere | `... (1994).state.auto` | **UNMEASURED** — near-static scene, needs a real gameplay state |

**Cybermorph is the first title to clear gate 1.** All seven cells, one 2400-frame window
(saturation ceiling 2399):

| cell | transitions | ≈ fps | vs stock |
|---|---:|---:|---:|
| RISC 0.5x (control) | 697/2400 | ~17 | **−48.7%** |
| **RISC 1x, 68K 1x (stock)** | **1359/2400** | **~34** | — |
| RISC 1.5x | 1967/2400 | ~49 | **+44.7%** |
| RISC 2x | 2349/2400 | ~59 | **+72.8%** |
| RISC 1x, 68K 1.5x | 1362/2400 | ~34 | +0.2% |
| RISC 1.5x, 68K 1.5x | 1994/2400 | ~50 | +46.7% |
| RISC 2x, 68K 1.5x | 2311/2400 | ~58 | +70.1% |

The 68K axis is inert (+0.2% on its own): this is a RISC-bound title, so only
`risc_clock_scale` matters, and stacking the 68K scale on top adds nothing.

**The control is what makes the rest trustworthy.** RISC 0.5x costs 48.7% — near-linear
against the +44.7% that 1.5x gains — so the metric tracks clock speed in both directions on
this title. Every figure above comes from the same 2400-frame window; nothing is spliced
across window lengths.

At 2x, 2349 of 2400 fields carry a new frame — Cybermorph is now presenting at essentially
the display rate. That is a real ceiling rather than the trap-2 counter artefact, but it does
mean **+72.8% is a floor on the 2x gain, not the exact figure**; the engine may have headroom
the display cannot show.

> **Two blockers before this becomes a DB preset.** Cybermorph is *also* the title with the
> open overclock crash report — #463, "Codex-level crash + erratic ship movement under
> overclock", currently `blocked` for want of reporter artefacts. The one title measured to
> benefit is the one with an unconfirmed overclock crash against it, so #463 has to be settled
> first. And gate 2 still applies: a title can render more frames while its logic runs too
> fast (#401). Play-test before writing a row.

**Iron Soldier is a genuine null, and the control proves it.** 0.5x reads 115 against 1x's
116 — halving the RISC clock costs it *nothing*, so it was never clock-bound at 1x. That fits
the profile data: Iron Soldier is the extreme DSP case, 67% of frame time in the DSP with
99.7% of that spinning in an idle loop. It needs **idle-skip**, not overclock.

> **Always run the underclock arm.** A flat 1x/1.5x/2x row is ambiguous — genuine cap, or dead
> measurement? 0.5x separates them. Cybermorph's control (−48.7% at 0.5x, against +44.7% at
> 1.5x) is what proves the metric was live for the whole sweep. `scripts/ocgrid.sh` runs this
> arm unconditionally so it cannot be forgotten.

**Val D'Isere is not a null — it is unmeasured.** 15 transitions in 900 frames, identical even
at 0.5x. That is one changed frame every ~60, i.e. a static or paused screen; the state is a
`.state.auto` autosave that landed wherever the session ended. Needs a real gameplay save
(see below).

### Need a savestate — checklist

- [ ] **Doom** — `Doom - Evil Unleashed (1994).jag`
      *Where:* in a level, walking down a corridor with at least one monster active.
      **Not** the attract demo (its pace is the #401 subject and it desyncs across cycle
      changes, so it is invalid evidence either way) and **not** the menu (its speed is the
      game's own code — see #396 notes).
- [ ] **Club Drive** — `Club Drive (1994).jag`
      *Where:* mid-race, car moving, scenery scrolling.
      **Note:** this title used to abort at `risc_clock_scale=2x` (`dsp_pc_escape`, #565).
      That is **fixed** as of v3.5.0, so the 2x arm should now complete — if it dies, that is
      a regression worth its own issue.
- [ ] **I-War** — `I-War (1995).jag`
      *Where:* in flight with geometry on screen. Earlier runs **saturated** here (600/600),
      so favour a calmer stretch and consider `FRAMES=1800`.
- [ ] **Missile Command 3D** — `Missile Command 3D (1995).jag`
      *Where:* a wave in progress with incoming missiles.
      Earlier runs were corrupted by generic input (halved at 1.5x) — a savestate fixes that.
- [ ] **Super Burnout** — `Super Burnout (1995).jag`
      *Where:* mid-race at speed.

### Already measured out — do not re-run

| Title | Why | Where |
|---|---|---|
| **AvP** | Field-locked at one frame per 5 fields. Identical in every grid cell; RISC 2x costs +69% instructions for **0%** gain. | #378 |
| **Checkered Flag** | Software frame cap at ROM `$8026EE` (budget at RAM `$43B0`). ~59/s across the whole grid for up to +56% instructions. **A clock scale cannot lift a software cap.** Its lever is the #370 enhancement hooks. | #378, #370 |

---

## 4. Running it

```bash
FRAMES=900 scripts/ocgrid.sh out.txt "<rom path>" "<state path>"
```

Results are written to `out.txt` and echoed to stdout. `CORE=` overrides the library path if
you are not on macOS (`CORE=./virtualjaguar_libretro.so ...`).

**Seven cells:** the six-cell grid — {1x, 1.5x, 2x} RISC × {1x, 1.5x} 68K — plus a **0.5x
underclock control**, which the script always runs. Each is quoted as a percentage against
the stock cell.

The control is not optional and the script does not let you skip it. A flat 1x/1.5x/2x row
cannot distinguish a real frame cap from a measurement that was never responding, and that
ambiguity is the direct cause of both retracted tables on #378. **If 0.5x does not drop, the
whole sweep is void** regardless of what the other arms say.

The run is two-phase — every cell is measured before anything is printed — because the stock
cell has to be known before the control can be quoted against it. There is no incremental
output; a 7-cell 2400-frame sweep takes a few minutes.

**Do not run this while anything else is building.** A concurrent `make` relinks
`virtualjaguar_libretro.dylib` mid-sweep and the arms end up measuring different binaries.
Check with `pgrep -f make` first.

Host load does *not* matter here: the metric counts emulated framebuffer transitions, not
wall-clock, so it is deterministic and load-immune. That is why this gate uses it rather than
an fps measurement.

### Reading the output

- `SATURATED (unmeasured)` — trap 2. Not a null; no information at all.
- `baseline 0` — the state is static or paused. Re-save it.
- A percentage — real, **provided** the stock cell is materially below the window length.

**Qualifying bar: ≥ +20%.** Below that it does not justify a DB row, because the cost is real
(RISC 2x is +69% host instructions on AvP) and the risk is real (two crash classes so far:
Defender 2000 at `m68k 3x` #460, Club Drive at `risc 2x` #565).

---

## 5. What clears gate 1 still is not a preset

Gate 2 is a human play-test in RetroArch: **game speed must still feel stock.** A title can
render more frames while its logic runs too fast — that is #401's entire subject, and it is
the failure mode a frame counter cannot see.

Also note what gate 1 does **not** measure: `risc_idle_skip` and `blit_memo` are absent from
the grid on purpose. They are bit-exact — identical output, less host work — so they cannot
change the internal frame rate by construction. They matter for whether a *slow device* hits
full speed, which is a host-cost question (#601), not a pacing one.

---

## 6. Prior art worth not re-deriving

- **#378** — the issue, methodology, and the two removals above.
- **#401 / #408** — render-bound loops tick ~2x fast. Read #408 before theorising about pace.
- **Doom is the only title with a real-hardware framerate capture.** Silicon runs level 1 at
  ~20 renders/s typical and ~15 in heavy scenes; we run it faster. `docs/doom-pace-calibration.md`.
  For that title the accuracy work aims to make it **slower**, so an overclock preset moves the
  wrong way.
