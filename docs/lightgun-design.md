# Design: Lightgun support (#438)

Spec for issue [#438](https://github.com/libretro/virtualjaguar-libretro/issues/438), under
epic [#428](https://github.com/libretro/virtualjaguar-libretro/issues/428).

Status: **IMPLEMENTED** (branch `feat/438-lightgun`). Everything below the line marked
"AS BUILT" in §0.1 is the original research document, kept unedited so the reasoning that
led to the design — and the two places it turned out to be wrong — stays readable.
**Read §0.1 first: two load-bearing decisions in §3.2 and §3.4 are wrong**, and the
corrections are backed by the ROM's own disassembly plus a measured A/B.

---

## 0.1 AS BUILT — what changed once the ROM could actually be run

Balloons landed in the private corpus after this document was written, which turned three
of its open questions into measurements. Two of the answers contradict the design.

### Deviation 1 — the LP latch is CONTINUOUS, not trigger-gated (§3.4 was wrong)

§3.4 says to synthesize `LPH`/`LPV` "on the trigger's rising edge". That is not how a
light gun works and it does not work with Balloons.

Balloons' main loop (`$4266`, `$43D6`, `$452C` — the calibration and gameplay loops) is:

```
stop   #$2000                  ; wait for the video interrupt
move.w LPH,$7E50               ; read the pair UNCONDITIONALLY, every field
move.w LPV,$7E52
...
move.w d0,$4E28                ; ...and write the decoded position straight
move.w d0,$4E2A                ;    into the CROSSHAIR sprite's X / Y
```

The trigger is tested *separately and afterwards*, only to decide whether a shot happened.
So the registers must update every field regardless of the trigger, or the crosshair
freezes. This is also the only thing that makes sense electrically: a real gun's
photodiode pulses `LP` every time the beam sweeps past the barrel, and **there is no
"`LP` was pulsed" status bit anywhere in the register map** — software cannot detect a
shot from `LPH`/`LPV` at all, so the trigger has to be something else.

As built: `TOMLightgunHalfline()` is called from `TOMExecHalfline()` for every half-line
and latches when the beam reaches the aimed-at row. That is cheaper *and* closer to the
hardware than the trigger-edge model, and it needs no beam hit-test window.

### Deviation 2 — the trigger is BUTTON_B, not BUTTON_PAUSE (§3.2 was wrong)

§3.2 reasons from TR10's "pin 6 of Port 1, shared with B0" to "the trigger drives
`joypad0Buttons[BUTTON_PAUSE]`". The pin-sharing fact may well be true of the silicon, but
it is not how a game reads a shot — and given Deviation 1 it *cannot* be, because `LP`
carries the beam pulse, not the switch.

Balloons scans all four rows into a bitmask (`$4A42`), edge-detects it into `$4C60`, and
tests **bit 25**. Simulating `joystick.c`'s decode against that exact scan sequence maps
bit 25 to row 1 / `JOYBUTS` bit 1 = **`BUTTON_B`**. (`BUTTON_PAUSE` lands on bit 28 and is
never tested.)

As built: `RETRO_DEVICE_ID_LIGHTGUN_TRIGGER` drives `joypad0Buttons[BUTTON_B]`, with
`AUX_A`/`AUX_B`/`START`/`SELECT` on `A`/`C`/`OPTION`/`PAUSE` so a title wanting a different
switch is still reachable. Mechanism is the rotary's (ordinary matrix slots), as §3.2
concluded — just a different slot.

### Deviation 3 — `LPH` is emitted as a GAP-FREE count, not TOM's bit-10 encoding

§3.4 left the encoding to the implementer. It matters, and only the ROM could settle it.

TOM encodes `HC` as bit 10 = "second half-line" plus a `0..HP` offset, so raw values jump
`845 -> 1024` mid-line (NTSC) — a 179-count hole. Balloons decodes with a plain
`(LPH - calibration_LPH) / PWIDTH` and no bit-10 handling. Both encodings were built and
swept across 21 aim points (`test/tools/test_lightgun.c`):

| encoding | result |
|---|---|
| gap-free (shipped) | exact at every column, 0 to the right edge |
| bit-10 raw `HC` | exact to column 237; column 238 decodes to object X **318** instead of 259 — `+59 px`, i.e. `179 / PWIDTH(3)`, exactly at Balloons' half-line boundary |

**Balloons' calibration screen passes under both**, because it only records the raw `LPH`
it is handed and range-checks `LPV`. So §6's "does it calibrate cleanly" test (open
question 2) is *not* sufficient on its own, and a single-point aim check on the left half
would have shipped the wrong encoding. The full-width sweep is the discriminator.

Nothing else in this core reads `LPH`, so there is no second consumer to keep consistent.

### Confirmed as designed

- **§1.3 savestates are free.** `tomRam8` is saved wholesale; no new chunk, no
  `STATE_VERSION` bump. Verified — the feature adds zero savestate code.
- **§3.1 port 1 only, additive to the pad.** `InputDevSetType()` refuses a gun on port 2.
- **§3.3 no controller-ID bits.** Nothing added.
- **§3.5 hi-res.** `libretro.c` divides `shadowHiresN` out before the aim reaches the core;
  `test_lightgun` asserts 1x and 2x latch byte-identical `LPH`/`LPV` (#400's guardrail).
- **§3.6 off-screen suppresses the latch.** Asserted.
- **§3.7 default off.** `virtualjaguar_p1_device` gains `lightgun`; the default stays
  `auto` → pad, and `joymatrix_identity`'s digests are untouched.
- **§4 non-goals** all hold: no crosshair overlay, no `_RELOAD`, no Bullets/MiSTer fudge
  constants (none were needed — the direct-inverse transform has no residual to cancel),
  no Followthrough claim.

### Open questions now answered

| # | Answer |
|---|---|
| 1 | Constants come from `TOMGetLeftVisibleHC()` (HC origin of framebuffer column 0) and `PWIDTH` from `VMODE` bits 9-11, read live. Vertically, `LPV = topVisible + 2*row`, because that is where `TOMExecHalfline()` writes row `r`. No SDK constants, no fudges. |
| 2 | Yes — but see Deviation 3: calibration converging proves less than §6 assumed. |
| 6 | Resolved: `BALLOONS.BIN`, 15904 bytes, md5 `cda44eeb071b5bd29582665ce8405eaa`. |

Still open: §5.3 (Team Tap), §5.4 (gun controller-ID bits), §5.5 (building Bullets).
Bullets remains untestable, so which button *it* reads is unverified; if it turns out to
want `A`, that is `AUX_A` today and a mapping question, not a design one.

---


Primary sources:

| Ref | Source | Used for |
|---|---|---|
| **TR8** | `docs/atari-jaguar-1999/Technical Reference v8.pdf` (register list) | `LPH $F00008 RO`, `LPV $F0000A RO`, `XLP` pin name |
| **TR10** | `docs/atari-jaguar-1999/Technical Reference v10.pdf`, §"Signals and Pin outs" | pin 6 = "Button input / Light Gun on Port 1"; LP rising-edge latch prose; four-way controller-ID scheme (no lightgun entry) |
| **RM** | `docs/jtrm-register-map.md` | distilled confirmation: `$F00008 LPH RO 16 "Light pen horizontal (11 bits)"`, `$F0000A LPV RO 16 "Light pen vertical (11 bits, half-lines)"` |
| **BULLETS** | `https://github.com/cubanismo/bullets` — `lightgun.s` (fetched in full, commits `9eea1b41`, `c2380d93`), `bullets.c` | the only known real Jaguar lightgun decode algorithm; commit message correcting the manual's own claim about LPH |
| **MISTER** | `https://github.com/MiSTer-devel/Jaguar_MiSTer/blob/master/rtl/jag_lightgun.sv` (fetched in full) | the only other open-source implementation; beam-window hit-test + pulse-generator behavioral model, tuning constants |
| **BALLOON-BIN** | `Balloons.bin` (from `http://www.mdgames.de/Balloons.zip`, fetched and disassembled by byte-pattern for this document, not committed to the repo) | direct, primary confirmation that a real title reads `LPH`/`LPV` |
| **JOYSTICK.C** | `src/jerry/joystick.c` (this repo) | port-1 row-0 button decode, to place the trigger/`B0` pin-sharing fact |
| **STATE.H** | `src/core/state.h` (this repo) | savestate coverage of `tomRam8` |
| **AV_INFO** | `libretro.c` `retro_get_system_av_info()` (this repo) | hi-res scaling of the geometry libretro reports, which the coordinate transform must divide back out |
| **INPUTDEV** | `src/jerry/inputdev.h`/`.c`, `docs/superpowers/specs/2026-08-14-input-devices-mouse-rotary-design.md`, PRs #449/#451/#474 | the device-framework precedent this feature must fit into |
| **LIBRETRO.H** | `libretro-common/include/libretro.h` | `RETRO_DEVICE_LIGHTGUN` contract (screen coords, trigger, aux buttons, off-screen) |

`mdgames.de` fails TLS but serves plain `http://` (same caveat as the mouse spike, M §9
in the input-devices design doc).

---

## 0. Executive summary

The issue's own research thread already answered the hard question — **the silicon is
real, no first-party product existed, and the addressable software is two titles** (a
homebrew calibration/shooting demo and an open-source SDK sample), with a third
(*Operation: Followthrough Prologue*) claimed in one comment and explicitly dismissed as
unconfirmed folklore in an earlier comment on the *same issue*. This document resolves
that internal contradiction (§2.3: still unconfirmed, primary sources checked directly),
adds a new piece of hard evidence (§2.1: **Balloon's own binary read byte-for-byte, not
just its store page**), and turns the "how would this be built" question — which neither
prior comment attempted — into a concrete design.

**The central engineering fact driving this design:** this core's `TOM` model is
event-driven, not cycle-accurate (CLAUDE.md, confirmed by code: `TOMExecHalfline()`
updates `HC` to one coarse value per halfline, not once per video clock — §3.3).
MiSTer's reference implementation (the only other open-source lightgun) works by
comparing a **continuously advancing per-clock beam position** against a target window
every cycle. That model does not fit this codebase without a genuine TOM timing-model
change — the class of change CLAUDE.md gates behind `dram_scale_sweep.sh` for GPU/68K
work, disproportionate for two titles. The design instead **synthesizes `LPH`/`LPV`
directly from the target screen position at the moment the trigger fires**, inverting the
same forward transform `bullets`' `readgun()` already inverts, using geometry this core
already tracks (`HDB1`, `PWIDTH`, `VDB`/`VDE`). No beam sweep, no hit window, no new TOM
timing model. §3.4 argues why this is a legitimate design choice and not a shortcut that
quietly breaks accuracy.

A second concrete finding narrows the work further: **the trigger does not need a new
matrix mechanism.** Port 1's `LP` signal is wired onto the same physical pin as button
`B0` (TR10), and this core's own `joystick.c` shows port-1 row-0 `B0` already decodes to
`BUTTON_PAUSE` (§3.2 — a genuinely surprising fact worth flagging loudly, parallel to the
mouse/rotary precedent's "phantom digit" and "LMB reads as four buttons" quirks). So the
lightgun's trigger is *additive* to the existing pad, not a replacement for it, unlike the
mouse (which disconnects the port) or the rotary (which removes Up/Down). This is
materially simpler than either precedent.

---

## 1. Hardware findings

### 1.1 The registers and the latch (sourced: TR8, TR10, RM)

- `LPH` `$F00008` RO, `LPV` `$F0000A` RO — light pen horizontal/vertical, 11 bits each
  (RM). `LPV`'s low bits are half-lines, matching `VC`'s own half-line granularity (RM,
  TR10).
- TR10 pinout: Port 1 pin 6 = *"Button input / Light Gun on Port 1"*, shared with `B0`.
  Port 2 pin 6 is `B2` only — **the gun is port-1 only**, a hardware fact, not a software
  policy choice.
- TR10 prose: *"A TTL rising edge on the LP signal (pin 6 of Port 1, shared with B0)
  causes the light pen registers (LPH and LPV) to be latched."* This is an **edge-latch**,
  not a level or a continuous readout: the registers hold whatever the beam counters read
  at the last rising edge until the next one.
- TR10's controller-ID scheme (diodes C1/C2/C3) enumerates four types and none of them is
  a lightgun. **A gun has no ID bit pattern to report**, and this design does not invent
  one (§4.10).
- The manual calls the registers "light pen" but the pinout calls the same signal "Light
  Gun" — the same inconsistency the issue's earlier comment flagged; noted here again
  because it means don't be surprised finding both spellings across TR8/TR10/RM.

### 1.2 Current core behavior (verified directly, not just cited)

`grep -n "case HC\|0x08\|0x0A" src/tom/tom.c` plus a direct read of `TOMReadByte` (line
1556) and `TOMReadWord` (line 1588): **there is no `$F00008`/`$F0000A` case**. Both fall
through to `return tomRam8[offset & 0x3FFF]`, an array nothing ever writes at those
offsets. So today, on this core, `LPH`/`LPV` always read as whatever `tomRam8` happened to
be zero-initialized to. This independently confirms the issue's own claim (it did not cite
line numbers; this document does).

### 1.3 The savestate implication (a genuine simplification, sourced)

`src/core/state.h` line 100 states plainly: *"the state saves jaguarMainRAM (the low 2 MB
of jagMemSpace), tomRam8, and jerry_ram_8"* — i.e. `tomRam8` is saved **wholesale** as a
raw block, separately from `TOMStateSave()`'s named-field function (which only covers
auxiliary counters, confirmed by reading it — no `tomRam8` bytes there). **A latch that
writes `LPH`/`LPV` straight into `tomRam8[0x08..0x0B]` gets savestate coverage for free.**
No `INPUTDEV_STATE_SIZE`-style new chunk, no `STATE_VERSION` bump. This is a real
advantage over the mouse/rotary precedent, not an assumption — verified by reading both
files.

### 1.4 TOM's horizontal position is not continuously tracked (the load-bearing limit)

`src/tom/tom.c`, `TOMExecHalfline()` (lines ~1282-1295):

```c
// Update HC to approximate position within the scanline.
if (halfline & 0x01)
{
   SET16(tomRam8, HC, 0x0400 | (hp > 0 ? (hp + 1) / 2 : 0));
   tomHCReadPhase = hp > 0 ? (hp + 1) / 2 : 0;
}
else
{
   SET16(tomRam8, HC, 0);
   tomHCReadPhase = 0;
}
```

`HC` gets **one coarse value per halfline call**, not a value that advances every video
clock the way real silicon's horizontal counter does. `VC` (the halfline index itself,
`jaguar.c` line ~1252) genuinely does advance one halfline at a time — matching real
hardware's own granularity, per the code comment *"the VC is advanced every HALF line"*
and TR10. So the **vertical** axis has hardware-accurate granularity already; the
**horizontal** axis does not have a live per-clock counter to sample. This is the fact
that rules out a MiSTer-style beam sweep (§1.5) and motivates the direct-synthesis design
in §3.4.

### 1.5 MiSTer's reference model (sourced, fetched in full: `jag_lightgun.sv`)

MiSTer's `jaguar_lightgun` module — the only other open-source Jaguar core to implement
this, and per its author (cited in the issue's earlier comment) *"tested with both known
lightgun software"* — does **not** simulate a photodiode detecting CRT brightness. It:

1. Integrates a target reticle position from PS/2 mouse deltas or an analog stick
   (`ret_x_int`/`ret_y_int`, clamped to `0..319`/`0..239`).
2. Every video clock, converts the current raster beam position (`cycle`, `scanline`) to
   screen space and compares it against the reticle within a **±`WINDOW_PIX` (10px)
   tolerance window**, computed through a pipelined multiply/divide (`div204_fast`,
   `div240_fast`, `div273_fast` — reciprocal-shift constant dividers, because this runs at
   video-clock rate on an FPGA).
3. On entering the window, asserts `lp0`/`lp1` for a fixed `PULSE_CLKS` (64) cycles — this
   is the "TTL rising edge" TR10 describes, generated synthetically rather than by an
   actual photodiode.
4. Uses tunable `X_SKEW_PIX`/`Y_SKEW_PIX` fudge constants (-37, +11) to correct calibration
   drift empirically observed against real titles.

This confirms two things independently: (a) the ±window/pulse model IS the correct
electrical behavior to reproduce (TTL edge triggers a latch of *whatever HC/VC currently
read*), and (b) **even a from-scratch, hardware-accurate reimplementation needed empirical
skew constants** to match real games — nobody, including MiSTer's author, derived exact
calibration from the manual alone. That is corroborated independently by `bullets`' own
unexplained `"XXX Calibration Hack? add.w #170,d1"` (§2.2) — this is not a
coincidence, it is the same open problem showing up in two unrelated implementations.

MiSTer's model is **not directly portable** to this core: it depends on a live per-clock
beam position (`cycle`/`scanline` inputs that increment every video clock), which this
core's event-driven `TOMExecHalfline()` does not provide for the horizontal axis (§1.4).
Building one purely for lightgun support would be new TOM timing-model work, which
CLAUDE.md explicitly gates behind `dram_scale_sweep.sh` for GPU/68K changes and is not
proportionate to a two-title feature (§6, effort estimate).

---

## 2. Title-by-title requirements

### 2.1 Balloon (Matthias Domin, 2003) — CONFIRMED, with new primary evidence

mdgames.de (`http://www.mdgames.de/jag_eng.htm`, fetched directly for this document):

> *"Balloons is a small lightgun-testprogram (startaddress is $4000), after calibrating
> the lightgun (please attach it to Joypad-Port1 of the Jaguar) you can shoot on coloured
> balloons moving from the bottom to the top of the screen."*

This document goes further than the prior issue comments: **the ROM itself was fetched
(`http://www.mdgames.de/Balloons.zip`, verified reachable, `200 OK`, 3980 bytes) and
disassembled at the byte level**, using the same instruction-context methodology the
issue's corpus scan used for its positive control. `Balloons.bin` (15904 bytes) contains:

| Pattern searched | Hits | Offsets |
|---|---|---|
| `$F00008` (LPH) as a 4-byte big-endian literal | 3 | `0x26C`, `0x3DC`, `0x532` |
| `$F0000A` (LPV) as a 4-byte big-endian literal | 3 | `0x276`, `0x3E6`, `0x53C` |
| `$F14000` (JOYSTICK) | 8 | (standard pad polling for calibration UI) |
| `$F14002` (JOYBUTS) | 1 | |
| `$F00004`/`$F00006` (HC/VC) | 0 | (matches the corpus scan's expectation: games use LPH/LPV, not raw HC/VC) |

Each `LPH`/`LPV` hit was manually decoded, not just pattern-matched. The bytes at `0x26C`
are `33 F9 00 F0 00 08 00 00 7E 50` — 68000 opcode `$33F9` decodes to `MOVE.W
$F00008,$00007E50` (size=word, source EA mode/reg = 111/001 = absolute long, destination
EA mode/reg = 111/001 = absolute long): **an unambiguous `MOVE.W LPH,ram_variable`**, not
a coincidental byte sequence. The matching `LPV` hit two words later (`0x276`, dest
`$00007E52`) stores immediately after it — one word each, adjacent RAM slots, exactly the
shape of a `{lph_copy, lpv_copy}` struct. This is the strongest evidence in this document:
**a real title's actual binary, not just a website description**, reads exactly the two
registers TR8/TR10/RM document and no others.

Balloon's ROM is **not** present in `test/roms/private` (checked: `find -L
.../jaguar-roms-private -iname "*balloon*"` and a zip-content sweep both came back empty).
It was fetched to the session scratchpad for this analysis only, not committed or placed
in the private tree — acquiring it properly for the test corpus is a follow-up action
(§5, open questions), not something this research task should do unilaterally given the
private tree's documented fragility (CLAUDE.md).

### 2.2 Bullets (`cubanismo/bullets`) — CONFIRMED, source in hand, build path unclear

Full source fetched (`lightgun.s`, `bullets.c`, repo tree). It is an SDK demo, not a
packaged ROM — `git clone` plus a `Makefile` that expects `jaguar.inc`/`jaguar.h`, which
are **not present in the repo** (grepped; zero hits). These are presumably part of an
external Jaguar SDK (BSD `jagstudio` or similar) the demo was built against but did not
vendor. This is a genuine sourcing gap: the exact numeric values of `NTSC_HMID`,
`NTSC_VMID`, `NTSC_WIDTH`, and their PAL counterparts that `guninit()` reads are **not
recoverable from this repository** — the implementer must derive them independently from
this core's own TOM geometry (§3.4), not assume they match Bullets' unseen SDK constants.

`lightgun.s`'s `readgun()` is the field's only known decode algorithm and is transcribed
here in full because the design in §3.4 is its direct inverse:

```asm
_readgun:
        move.w  LPV,d0          ; d0.L16 = LPV
        andi.l  #$7ff,d0        ; Mask off "field" bit of LPV
        add.w   fb_height,d0    ; Add "half" FB height in halflines
        sub.w   vmid,d0         ; Subtract [NTSC|PAL]_VMID (halflines)
        lsr.w   #1,d0           ; halflines -> scanlines to get FB y coord
        swap    d0

        move.w  LPH,d0          ; d0.L16 = LPH
        move.w  d0,d1
        and.w   #$3ff,d0        ; Mask off bit 10+ from return value
        btst.l  #10,d1          ; If bit 10 was set, in 2nd half-line
        beq     .firsthalf
        add.w   hmid,d0         ; In 2nd half-line: add half-line size
.firsthalf:
        sub.w   xoff4,d0        ; FB X = (LPH - a_hdb - (XPOS*4)) / 4
        lsr.w   #2,d0
```

The commit message (`9eea1b41`, 2020-08-15) is worth quoting verbatim, since it is a
**JTRM-is-wrong finding on record from the field, not this project's own doc**:

> *"LPH = latched HC value, NOT horizontal position in pixels, as the manual states."*

And `guninit()` carries an unexplained fudge the author never resolved:

```asm
        add.w   a_hdb,d1
        ; XXX Callibration Hack?
        add.w   #170,d1
        ; XXX End CallibrationHack
```

Implication for this design (expanded in §3.4): our synthesized `LPH`/`LPV` do **not**
need to reproduce Bullets' specific `170` fudge, because we control both ends of the
transform (we compute `LPH` as the exact inverse of what our own geometry would produce,
so no residual error needs a fudge factor to cancel). But it does mean a test asserting
*exact* register values against a real-hardware capture would be the wrong test — the
right test is geometric monotonicity and round-trip correctness against our own inverse
(§5).

### 2.3 Operation: Followthrough Prologue — UNCONFIRMED, contradicts an earlier comment on this same issue

The issue's two research comments disagree with each other and neither resolved it. The
first (corpus-scan) comment explicitly *dismissed* this title: *"Operation: Followthrough
as a 'light gun shooter' (a Wikipedia genre label; its itch.io page never says
'gun')."* The second (2026-08-15) comment re-lists it as a target without addressing that
dismissal.

This document checked both primary pages directly for this design (not re-reading the
Wikipedia table, which is what apparently produced the disagreement):

- `https://stormplay.itch.io/operation-followthrough` — describes the game as an
  **"On-Rails Shooter"** and draws a **Virtua Cop** comparison in the comments (a genre
  cousin with real lightgun history), but **the official page text contains no mention of
  lightgun/gun peripheral support.**
- `https://stormplay.scot/games/followthrough/jaguar/` (dev's own devlog index) — lists
  devlog post titles/dates only; **no lightgun/`XLP`/`LPH`/`LPV` mention in any visible
  text.**
- A `WebSearch` snippet surfaced one unverified claim — *"someone on the Discord has a
  tool that supports the lightgun attachment for the game"* — third-hand, no link, not a
  primary source, not corroborated here.

**Conclusion: this document treats Followthrough as NOT a confirmed target.** It is an
on-rails shooter that resembles the lightgun genre by comparison, which is exactly the
kind of signal the corpus-scan comment already correctly flagged as folklore. If a
Followthrough build surfaces, the corpus scanner referenced in the issue (instruction-
context `$F00008`/`$F0000A` search, same as §2.1's method) is the way to re-check it — not
another web search.

### 2.4 Net addressable audience

**One confirmed homebrew title (Balloon) plus one open-source SDK sample (Bullets) that
is not itself a shippable ROM.** This should be stated plainly per the task's rules: it is
an honest, small number, not a reason to inflate scope. Section 6 sizes the work against
this reality rather than against a hypothetical larger catalog.

---

## 3. Emulation design

### 3.1 Scope: port 1 only, additive to the existing pad

TR10 places the gun exclusively on port 1 (§1.1) — this is a hardware constraint, not a
policy choice, so `InputDevSetType()` should refuse a lightgun on port 2 the same way it
already refuses a mouse on port 1 (existing precedent, `inputdev.c`).

Unlike the mouse (which must disconnect the RetroPad on its port — row-blind overlay,
INPUTDEV design doc §2) and the rotary (which must remove Up/Down — matrix device,
INPUTDEV design doc §4), **the lightgun changes nothing about how port 1's existing
matrix bits decode.** It only adds a new signal path (the `LP` edge latch) that happens to
share a physical pin with an existing button. So port 1's RetroPad functionality is
unaffected when a lightgun is selected — there is no equivalent of the mouse's "port loses
both devices until the mouse proves live" problem (`inputdev_live[]` in `libretro.c`).

### 3.2 The trigger: `B0` is `BUTTON_PAUSE` on port 1 row 0 — sourced, verified in this codebase

TR10's pin table lists port-1 pin 6 as `B0 / LP`. Reading this core's own
`src/jerry/joystick.c` (`JoystickReadWord`, offset `2`, lines ~123-159):

```c
const uint8_t mask[4][2] = { { BUTTON_A, BUTTON_PAUSE }, { BUTTON_B, 0xFF }, { BUTTON_C, 0xFF }, { BUTTON_OPTION, 0xFF } };
...
data &= (joypad0Buttons[mask[offset0][0]] ? 0xFFFD : 0xFFFF);   /* clears bit 1 -> BUTTON_A  */
if (mask[offset0][1] != 0xFF)
   data &= (joypad0Buttons[mask[offset0][1]] ? 0xFFFE : 0xFFFF); /* clears bit 0 -> BUTTON_PAUSE */
```

`JOYBUTS`'s documented layout (RM / mouse-adapter doc §2) is *"1-0 = button inputs B1 & B0
(Port 1)"* — bit 0 is `B0`. In the code above, **bit 0 is `BUTTON_PAUSE`**. So on port 1's
row 0, `B0` — the same physical pin as `LP` — reads as **Pause**, not as a natural "fire"
button like `A` or `B`.

This is a genuinely surprising, load-bearing fact worth flagging as loudly as the
mouse/rotary precedent flags its own quirks: **pulling the trigger, on real hardware, also
makes the game see Pause held down**, in addition to whatever the game does with
`LPH`/`LPV`. This is not a bug to design around — a title built against a real gun already
expects it (Balloon's calibration UI, per the JOYSTICK-register hits in §2.1, is polling
the pad; whether it happens to react to a Pause-looking bit during calibration is a
title-specific fact this design does not need to resolve, only reproduce faithfully).

**Design decision:** when port 1 is a lightgun, `RETRO_DEVICE_ID_LIGHTGUN_TRIGGER` drives
`joypad0Buttons[BUTTON_PAUSE]` directly — reusing the existing pad-array mechanism exactly
as the rotary reuses `joypadNButtons[]` (`inputdev.h`'s "matrix device" pattern), **not** a
new row-independent overlay like the mouse's. This is simpler than either existing
precedent and should be called out as such in the eventual PR, with a comment at the site
citing this pin-sharing fact so a future reader doesn't "fix" it into `BUTTON_A`.

### 3.3 No controller-ID bits (sourced: TR10)

TR10's controller-ID scheme (four types: 4-player adaptor, bank-switching, rotary,
reserved) has no lightgun entry (§1.1). Unlike the rotary's `InputDevSetRotaryID()`
(TR10-sourced C2/C3 diode emulation), **there is nothing to add here** — inventing an ID
value Atari never assigned would be exactly the kind of guess CLAUDE.md and this task's
rules prohibit. A lightgun on port 1 should read whatever the ID bits already read for a
standard pad (which is presumably what real gun-equipped controllers did too, since they
were "modified standard controllers" wired onto the LP pin, the same phrase TR10 uses for
the rotary — unverified by direct TR10 quote for the gun specifically; flagged in §5).

### 3.4 `LPH`/`LPV` synthesis: direct inverse of the target position, not a beam sweep

This is the core design decision, and it departs from MiSTer's model for a sourced reason
(§1.4, §1.5): this core has no live per-clock horizontal beam counter to sample, and
building one is disproportionate new timing-model work for two titles.

Instead, on the trigger's rising edge, compute `LPH`/`LPV` as the **direct inverse** of
`bullets`' `readgun()` transform (§2.2), using this core's own already-tracked TOM
geometry rather than Bullets' unseen SDK constants:

```
Given: libretro RETRO_DEVICE_ID_LIGHTGUN_SCREEN_X/Y, range [-0x8000, 0x7fff],
       0 = center of the CURRENTLY REPORTED frame (AV_INFO: game_width x game_height).

1. Normalize to a 0..game_width-1 / 0..game_height-1 pixel coordinate.
2. Divide out the hi-res factor (shadowHiresN, AV_INFO -- see 3.5) to get a
   NATIVE 0..319 / 0..239-ish coordinate in TOM's own terms.
3. Forward-map native (x, y) to (LPH, LPV) using the same quantities
   TOMGetLeftVisibleHC() / HDB1 / VDB / VDE / PWIDTH already expose:
     LPV = ((native_y * 2) - fb_height_halflines_offset) + vmid-equivalent,
           with bit 11 set/cleared to match the current field parity
           TOMExecHalfline() already tracks.
     LPH = (native_x * 4) + xoff4-equivalent, with bit 10 set when the
           computed value falls in the second half of the line (mirroring
           readgun()'s `btst #10` / `add hmid` pair in reverse).
4. Latch tomRam8[0x08..0x09] = LPH, tomRam8[0x0A..0x0B] = LPV (SET16, this
   core's existing byte-swap-safe macro -- CLAUDE.md's GET16/SET16 note).
```

The exact per-title `vmid`/`hmid`/`xoff4`-equivalent constants must be **derived from this
core's TOM geometry accessors**, not copied from Bullets' unseen `jaguar.inc` (§2.2's
sourcing gap) — this is real implementation work, not a fill-in-the-blank, and should be
verified against `docs/jtrm-clocks-timing.md`'s HC/VC field definitions before being
trusted.

**Why this sidesteps §1.4's limitation entirely, and why that's disclosed as a deliberate
trade-off, not hidden:** because the latch fires once, synthetically, from the *target*
position at the moment of the trigger edge, it never needs to ask "where was the beam
right now" — it computes "where would `LPH`/`LPV` read if the beam had just been at this
target," which is exactly what the real latch captures on real hardware. The trade-off:
real hardware (and MiSTer's model) has a natural ±window tolerance and can *miss* — pull
the trigger between beam passes near the target and get a slightly-off reading, which is
why calibration screens exist and why players learn to time-and-aim together. This design
**never misses and never gets timing jitter** — every trigger pull at (x, y) latches the
mathematically exact `LPH`/`LPV` for (x, y), every time. That is a real, disclosed
divergence from hardware feel, not an implementation bug. Section 5 flags it as something
to sanity-check against Balloon's calibration screen once the ROM is in the test corpus:
if Balloon's calibration routine assumes some sampling noise to average out (its own
`bullets.c` companion program does exactly this — `lpavgx`/`lpavgy` average the last 16
frames, §2.2's `bullets.c` listing), an emulation with zero noise should still calibrate
cleanly (averaging a constant converges to that constant), so this is expected to be
harmless, but it is an assumption worth confirming against the real ROM rather than
asserting.

### 3.5 Hi-res and PAL/NTSC (sourced: `retro_get_system_av_info`)

`libretro.c`'s `retro_get_system_av_info()` (read directly for this document):

```c
info->geometry.base_width   = game_width;
info->geometry.base_height  = game_height;
/* Hi-res: the maxima scale by the (load-time-fixed) internal
 * resolution factor; shadowHiresN is 1 when the option is off. */
info->geometry.max_width    = 652 * shadowHiresN;
info->geometry.max_height   = 256 * shadowHiresN;
```

`game_width`/`game_height` — and therefore the frame libretro's normalized
`[-0x8000, 0x7fff]` lightgun coordinates are relative to — **already bake in the
`virtualjaguar_internal_resolution` 2x factor**. So step 2 in §3.4's transform (divide out
`shadowHiresN`) is not optional plumbing, it is required correctness: without it, a 2x
session's gun coordinates would be double the native TOM geometry expects, and every shot
would land at roughly 2x its intended screen position. NTSC vs. PAL affects the `vmid`
equivalent (different field height/timing per `jtrm-clocks-timing.md`), which the geometry
accessors already resolve per `vjs.hardwareTypeNTSC` — no new PAL/NTSC branching beyond
what `TOMGetLeftVisibleHC()`-style accessors already do.

### 3.6 Off-screen shots and reload

`RETRO_DEVICE_ID_LIGHTGUN_IS_OFFSCREEN` and `_RELOAD` ("forced off-screen shot for
auto-reload") have no register-level hardware analog documented anywhere in this research
— a real gun pointed off the CRT simply never receives a brightness pulse, so `LP` never
gets a rising edge and `LPH`/`LPV` keep their last latched value. **Design: when
`IS_OFFSCREEN` is asserted, suppress the `LPH`/`LPV` latch on that trigger pull** (mirrors
"no pulse"), while `BUTTON_PAUSE` still asserts from the trigger itself (§3.2 — the pin is
electrically the same regardless of aim, only the photodiode pulse is aim-dependent).
`_RELOAD` is **not emulatable at the register level** with this model — there is no
"forced" pulse concept on the real pin — and should be treated as out of scope for v1
(§4.4).

### 3.7 Core option shape (epic guardrails: default off, joymatrix digests untouched)

Follows the existing `virtualjaguar_p1_device` scaffolding exactly (`libretro_core_options.h`,
`input_p1` category) — add one new enum value:

```c
{ "auto",     "Auto (per-title default)" },
{ "pad",      "Standard Joypad" },
{ "rotary",   "Rotary (Tempest)" },
{ "lightgun", "Lightgun (Balloon, homebrew)" },   /* NEW */
```

Default stays `"auto"`, which currently resolves to `pad` for every title with no
per-title default entry — **unchanged joymatrix behavior when not selected**, satisfying
the guardrail `test/tools/joymatrix_identity.c` exists to enforce (same pattern
`docs/superpowers/specs/2026-08-14-input-devices-mouse-rotary-design.md` established).
`InputDevType` gains `INPUTDEV_LIGHTGUN`; `InputDevSetType()` refuses it on port 2 (mirror
of the mouse's port-1 refusal, inverted).

---

## 4. What this design deliberately does NOT do (non-goals for v1)

1. **No crosshair overlay.** MiSTer draws one (`draw_crosshair`, §1.5) because it composites
   directly onto the video signal at the RTL level; this core has no equivalent
   post-blit overlay hook. A software overlay would be new rendering-pipeline surface
   area for a UX nicety, not a hardware-accuracy requirement — most frontends (RetroArch)
   already draw their own gun crosshair from the same `RETRO_DEVICE_LIGHTGUN` state, so
   this is likely redundant, not just deferred.
2. **No Team Tap support.** Plausible from the LP pin's direct TOM wiring (a `Team Tap`
   adapter multiplexes the row/column matrix; `LP` is a dedicated pin outside that mux),
   but **this document did not find an explicit TR10 sentence confirming it** — the claim
   in the issue's second comment should be re-verified against TR10's Team Tap section
   before it becomes a code comment asserting it as fact (§5).
3. **No `RETRO_DEVICE_ID_LIGHTGUN_RELOAD`.** No register-level hardware analog (§3.6).
4. **No attempt to reproduce Bullets' `170`-unit calibration fudge or MiSTer's
   `X_SKEW_PIX`/`Y_SKEW_PIX` constants.** Both are empirical corrections for problems
   specific to *their* transforms; this design's direct-inverse approach has no equivalent
   residual error to cancel (§3.4). If Balloon's own calibration screen disagrees once
   tested against the real ROM, that is new information to act on, not a constant to
   pre-guess.
5. **No Operation: Followthrough support claim.** Unconfirmed (§2.3) — not designed
   against, not promised in release notes, until a build or devlog explicitly shows
   `LPH`/`LPV` usage.

---

## 5. Open questions

| # | Question | Status |
|---|---|---|
| 1 | Exact `vmid`/`hmid`/`xoff4`-equivalent constants for the §3.4 forward map | Must be derived from this core's own TOM geometry accessors + `docs/jtrm-clocks-timing.md`, NOT copied from Bullets' unseen `jaguar.inc` (§2.2). Real implementation work. |
| 2 | Does a zero-noise synthetic latch (§3.4) actually calibrate cleanly against Balloon's real calibration routine? | Untested — Balloon's ROM was fetched for byte-level analysis (§2.1) but not booted; needs the harness extension in §6 and the ROM properly placed in the private corpus first. |
| 3 | Team Tap incompatibility (§4.2) | Plausible, not TR10-confirmed by this document. Re-verify before asserting in code. |
| 4 | Does a lightgun-equipped controller report standard-pad ID bits, or something else? | TR10's rotary section uses "modified standard controller" language; this document did not find the equivalent sentence for the gun specifically. Assumed standard-pad ID bits (§3.3); flag if TR10 says otherwise on a closer read. |
| 5 | Bullets' buildability | `jaguar.inc`/`jaguar.h` are external SDK headers not vendored in the repo; building Bullets from source for the test corpper requires acquiring that SDK. |
| 6 | Balloon ROM provenance/licensing for the test corpus | mdgames.de hosts it as a free download (same site/convention as the already-in-tree Domin PD titles); recommend fetching `http://www.mdgames.de/Balloons.zip` into `test/roms/private` through the normal (careful, `-n`-flagged symlink-respecting) acquisition process, not something this research task should do unilaterally. |

---

## 6. Test plan

No ROM is currently in the private corpus (§2.1), so day-one testing is necessarily
synthetic; ROM-based verification is a follow-up once §5 item 6 is resolved.

1. **Harness extension.** `test/harness/harness.h`'s scripted input (`--press
   FRAME:BUTTON[:HOLD]`) covers digital buttons only (CLAUDE.md). Lightgun testing needs a
   new injection path — `--gun FRAME:X:Y:TRIGGER[:OFFSCREEN]` or a programmatic
   `harness_gun()` analogous to `harness_press()` — that drives
   `RETRO_DEVICE_ID_LIGHTGUN_SCREEN_X/Y/TRIGGER/IS_OFFSCREEN` through `input_state_cb`.
   This is new test infrastructure, scoped and small, not a detour.
2. **Round-trip unit test (no ROM needed).** A new `test/tools/test_lightgun_transform.c`
   (or similar) that: picks a target native `(x, y)`, runs the §3.4 forward map to get
   `(LPH, LPV)`, then runs `bullets`' own `readgun()` arithmetic (transcribed from §2.2,
   not `#include`d — it depends on unavailable SDK headers) against those values, and
   asserts the round trip recovers `(x, y)` within ±1px. This validates the *shape* of the
   transform against the one real decode algorithm on record without needing hardware or
   a ROM.
3. **Guardrail.** `test/tools/joymatrix_identity.c` must stay green unchanged — the
   feature is opt-in and off by default (§3.7).
4. **c89 lint.** `bash scripts/c89-lint.sh` on every new/modified `src/` file (`tom.c` is
   not in the exempt list — CLAUDE.md).
5. **ROM-based verification (blocked on §5.6).** Once Balloon is properly in the private
   corpus: script a calibration pass with `--gun`, confirm the calibration screen accepts
   input and completes, then script a shot at a known balloon position and confirm a hit
   is registered (visual check via a screenshot tool in the `cd_visual_verify` family, or
   a simpler framebuffer-hash check if Balloon's hit feedback is a discrete color change).
6. **Savestate.** Confirm a save/load round-trip preserves the last-latched `LPH`/`LPV`
   (should be automatic per §1.3, but assert it rather than assume it).

---

## 7. Effort estimate

Sized against the mouse (#449, 2372/-69 lines) and rotary (#451, 1602/-102 lines) PRs as
reference points, but with a different risk profile in both directions:

**Smaller than mouse/rotary in scope:**
- No row-blind overlay mechanism (§3.1) — the trigger reuses the existing pad array.
- No new savestate chunk (§1.3) — free ride on the existing `tomRam8` blob.
- No controller-ID emulation (§3.3) — nothing to add.

**Larger in risk, for reasons the mouse/rotary work didn't have:**
- Touches `src/tom/tom.c` — a **third file** beyond the `src/jerry/` input-device
  territory the epic's precedent PRs stayed inside, and one of the most heavily-relied-on
  files in the codebase (TOM register decode). Needs careful review to avoid perturbing
  existing `HC`/`VC` behavior other subsystems depend on.
  - Also lands in `TOMReadByte`/`TOMReadWord`, which the CLAUDE.md-flagged concurrent
    input/options work is not touching, so collision risk with the two agents currently
    editing those files is low — but this should still be sequenced, not landed blind,
    against whatever `src/jerry/inputdev.c` shape those agents leave behind.
- The `LPH`/`LPV` forward-map constants (§5.1) are genuine derivation work against JTRM
  geometry, not a known-good formula to transcribe — budget research time, not just
  coding time.
- Test harness needs a new input-injection capability (§6.1) before any ROM-based
  assertion is possible.
- Balloon's ROM must be acquired into the private corpus before end-to-end verification
  (§5.6) — a process step, not code, but a real dependency.
- Bullets is not independently buildable from its public repo without an unspecified SDK
  (§2.2/§5.5) — usable as a reference for the algorithm (already fully transcribed here,
  §2.2), not as a buildable test fixture, unless that SDK is separately acquired.

**Rough estimate: 3-5 focused days** for a contributor already fluent in this codebase,
assuming the ROM acquisition (§5.6) goes smoothly — closer to 5-7 if the SDK/geometry
constants (§5.1) need real derivation-from-JTRM work rather than falling out cleanly from
existing `TOMGetLeftVisibleHC()`-style accessors. This is proportionate to the code
change, not to the audience — the audience is one homebrew title, honestly stated per
§2.4, and that should weigh on *scheduling* even though the code itself is a moderate,
well-scoped change.

## 8. Recommendation

Do not implement inside the current guardrail/mouse/rotary/tuning wave (#446/#449/#451/
#474, all merged) or the concurrent input/options work referenced by this task's
constraints. Epic #428 already carries this as its `#438` sub-issue with a milestone of
v3.5.0; this document does not see a reason to accelerate it ahead of that, given the
confirmed-audience size (§2.4). Recommended next action, in order: (1) resolve §5.6 by
acquiring Balloon into the private ROM corpus through the normal careful process, (2) spend
a short spike deriving the §3.4 forward-map constants against `docs/jtrm-clocks-timing.md`
and this core's own TOM accessors, (3) build the harness extension (§6.1) and the
round-trip unit test (§6.2) *before* touching `tom.c`, matching this repo's established
test-first discipline for hardware-timing-adjacent changes, (4) implement against a green
round-trip test and Balloon's actual calibration screen, not just this document's formulas
in isolation.
