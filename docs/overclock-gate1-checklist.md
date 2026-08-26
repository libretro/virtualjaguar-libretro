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

### Measured 2026-08-25 — all candidates

Gate 1 is **complete**. One title passes.

| Title | control 0.5x | RISC 1.5x | RISC 2x | verdict |
|---|---:|---:|---:|---|
| **Cybermorph (1993)** | −48.7% | **+44.7%** | **+72.8%** | **PASSES** |
| **Cybermorph (1994 ROM)** | −40.6% | **+28.6%** | **+52.3%** | **PASSES** — independent confirmation |
| I-War | −18.2% | +6.3% | +1.1% | fails — noise, non-monotonic |
| Missile Command 3D | −0.2% | **−14.3%** | −11.2% | fails — overclock makes it *worse* |
| Doom | ±0.0% | ±0.0% | ±0.0% | flat lock, 593/1800 |
| Club Drive | ±0.0% | ±0.0% | ±0.0% | flat lock, 1471/1800 |
| Iron Soldier | −0.9% | ±0.0% | ±0.0% | flat lock, 116/900 |
| Super Burnout | SATURATED | SATURATED | SATURATED | unmeasurable — already at the ceiling |
| Val D'Isere | −1.6% | −1.6% | −1.6% | fails — RISC inert; 68K "gain" is an artefact |

**One title passes, on two independent ROM revisions.** The structural-null hypothesis from #378 survives as the *general* rule —
most Jaguar titles are paced by a field lock or their own frame cap, not by clock speed — but it
is not universal, and Cybermorph is the counterexample that disproves the strong form of it.

#### Cybermorph (1993) — PASSES

All seven cells, one 2400-frame window (saturation ceiling 2399):

| cell | transitions | ≈ fps | vs stock |
|---|---:|---:|---:|
| RISC 0.5x (control) | 697/2400 | ~17 | **−48.7%** |
| **RISC 1x, 68K 1x (stock)** | **1359/2400** | **~34** | — |
| RISC 1.5x | 1967/2400 | ~49 | **+44.7%** |
| RISC 2x | 2349/2400 | ~59 | **+72.8%** |
| RISC 1x, 68K 1.5x | 1362/2400 | ~34 | +0.2% |
| RISC 1.5x, 68K 1.5x | 1994/2400 | ~50 | +46.7% |
| RISC 2x, 68K 1.5x | 2311/2400 | ~58 | +70.1% |

The 68K axis is inert (+0.2% on its own): RISC-bound title, so only `risc_clock_scale` matters,
and stacking the 68K scale on top adds nothing.

**The control is what makes the rest trustworthy.** RISC 0.5x costs 48.7% — near-linear against
the +44.7% that 1.5x gains — so the metric tracks clock speed in both directions here. Every
figure comes from the same 2400-frame window; nothing is spliced across window lengths.

At 2x, 2349 of 2400 fields carry a new frame: it is presenting at essentially the display rate.
That is a real ceiling rather than the trap-2 counter artefact, but it does mean **+72.8% is a
floor on the 2x gain, not the exact figure**.

> **Two blockers before this becomes a DB preset.** Cybermorph is *also* the title with the open
> overclock crash report — #463, "Codex-level crash + erratic ship movement under overclock",
> `blocked` for want of reporter artefacts. The one title measured to benefit is the one with an
> unconfirmed overclock crash against it, so #463 has to be settled first. And gate 2 still
> applies: a title can render more frames while its logic runs too fast (#401).

#### Missile Command 3D — fails, and interestingly

The only title where overclocking is actively **harmful**: −14.3% at RISC 1.5x, −11.2% at 2x,
while the 0.5x control is flat (−0.2%). Not compute-bound at stock, and giving the RISC more
cycles *costs* presented frames. Worth a look on its own terms — a title that renders less when
given more cycles suggests a timing-sensitive path, not merely an absent benefit.

#### I-War — fails, capped at stock

Control drops cleanly (−18.2%), so the sweep is valid, but the gains are noise and
non-monotonic: +6.3% at 1.5x, +1.1% at 2x, −3.7% at 2x with the 68K scale. Underclocking hurts
while overclocking does nothing — the signature of a cap sitting at or just above stock.

#### Doom, Club Drive, Iron Soldier — flat locks

Identical counts in all seven cells, control included. Iron Soldier reads 115 at 0.5x against
116 at 1x, so halving the clock costs it nothing.

A flat control would normally void a sweep, and the script says so. It does not here, because
the harness is demonstrably live on the same binary in the same session: Cybermorph, I-War and
Missile Command 3D all responded. These are genuine caps, not dead measurements.

**Doom's number is worth recording for #401.** 593/1800 is one new frame per 3.03 fields,
**≈19.8 fps** — and real hardware Doom level 1 is ~20 renders/s typical. In *gameplay* we may
be far closer to hardware than the attract demo suggests, where #434 measured us at 2.00
fields/flip against hardware's 4.0. One savestate is not a calibration, and the demo and
gameplay are different code paths, so this is a lead rather than a result.

#### Super Burnout — unmeasurable, and the reason matters

1799/1800 in every cell: every frame differs, so the counter is pinned to the ceiling and a gain
has nowhere to show. Lengthening the window does **not** help — a title that renders a new frame
every field is saturated at any window length.

But the reason it saturates is itself the answer: **stock already presents at the display rate**,
so there is no headroom for an overclock to deliver. Formally unmeasured by this metric;
practically, there is nothing to win.

#### Cybermorph 1994 ROM — PASSES, confirming the 1993 result

A different ROM revision (`Cybermorph_(1994).jag`), a different savestate, same
conclusion: control −40.6%, **+28.6% at RISC 1.5x, +52.3% at 2x**, 68K axis inert
(−1.4% on its own). Two independent ROMs now agree that this title is genuinely
RISC-bound.

Its 2x cells read **1796/1800** and **1798/1800** — functionally pinned to the
ceiling. See the guard note below; those two figures are floors, not measurements.

#### Val D'Isere — FAILS, and it nearly fooled the method

The new gameplay savestate is valid (193/1800 at stock, live and unsaturated —
the old `.state.auto` was a static screen at 15/900). The result:

| cell | transitions | vs stock |
|---|---:|---:|
| RISC 0.5x (control) | 190/1800 | −1.6% |
| **stock** | 193/1800 | — |
| RISC 1.5x / 2x | 190/1800 | −1.6% |
| **68K 1.5x** | 224/1800 | **+16.1%** |

The RISC axis is completely inert. The 68K column moving +16.1% looked like the
first **68K-bound** title in the corpus — below the ≥20% bar, but qualitatively new.

**It is not.** Sweeping the 68K axis with its own underclock arm:

| m68k | 0.5x | 1x | 1.5x | 2x | 3x |
|---|---:|---:|---:|---:|---:|
| transitions | **240** | 193 | 224 | 189 | 190 |

**Underclocking to 0.5x produces the *highest* count of all**, and the axis is
non-monotonic throughout. That is not a compute bound — it is aliasing between
the 68K rate and the render cadence shuffling *which* frames differ, not
producing more of them. Val D'Isere fails.

> **Method fix, and the reason for it.** The grid's control underclocks **RISC
> only**, so on a title whose RISC axis is flat it proves nothing about the 68K
> column. The `+16.1%` was reported as a real finding before the 68K control was
> run, and the control retracted it. **If the 68K column moves while RISC is
> flat, sweep the 68K axis separately including its own 0.5x arm before
> believing it.** Recorded in `scripts/ocgrid.sh`.

> **Second method fix: near-saturation.** The guard fired only at exactly
> `n-1`, so Cybermorph 1994's 1796/1800 and 1798/1800 sailed through and printed
> confident percentages while functionally pinned. The threshold is now **98% of
> the ceiling**. Any figure at or above it is a floor, not a measurement.

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
