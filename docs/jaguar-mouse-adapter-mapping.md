# Atari ST / Amiga mouse adapter → Jaguar `$F14000` bit mapping

Research spike for [#433](https://github.com/libretro/virtualjaguar-libretro/issues/433),
blocking [#429](https://github.com/libretro/virtualjaguar-libretro/issues/429)
(ST/Amiga mouse support).

**Status: #429 is unblocked.** The ST and Amiga pin → row/column-bit tables are
**VERIFIED** against four mutually independent sources (a wiring diagram, the adapter
author's own 68000 driver, the Jaguar Technical Reference V10, and an independent vendor
pinout table). The row-0 *pad matrix* they land on agrees bit-for-bit with what
`src/jerry/joystick.c` already models — but a mouse adapter is **not** a matrix device
(it never sees the row select; see §7), so the device shape itself is new work. The PS/2
question is answered from vendor documentation (§6). Residual open
items — all sign/permutation choices settled by one test, none blocking — are in
[§8 Unresolved / not sourced](#8-unresolved--not-sourced).

Answers to #433's five questions: §4 (1 and 3), §2 + §4 (2), §6 (4), §5 (5).

No emulator code was written for this spike.

---

## 1. Sources

| # | Source | Kind | Used for |
|---|--------|------|----------|
| S1 | `http://www.mdgames.de/jag_eng.htm#DIGITALMOUSE` → `jagmouse.gif` — "Atari Jaguar Mouse Connector by Matthias Domin, for digital mice (Atari STM1 or PCM1)" | **Schematic / wiring diagram** | ST adapter wiring, quadrature waveform |
| S2 | `http://www.mdgames.de/twomice2_20071230.zip` → `twomice2.s` — Domin's own 68000 test program (derived from Atari's `JOYPAD.S`) | **Author's driver source** | ST *and* Amiga adapter pinouts (header comment), row select, decode tables |
| S3 | `http://www.mdgames.de/colmouse.zip` → `READMOUS.TXT` — Domin's mouse driver notes + earlier `readmouse` routine | Author's prose + source | Quadrature decode table, required poll rate |
| S4 | `docs/atari-jaguar-1999/Technical Reference v10.pdf` (Jaguar Technical Reference V10.0, SgM Electrosoft), §"Signals and Pin outs", §"Register Addressing – Digital Inputs", §"Standard Jaguar Controller Matrix", §"Rotary 'Tempest' Controller" | **Hardware manual** | Physical connector pin ↔ `J`/`B` signal names, register bit layout, row codes |
| S5 | `https://wiki.icomp.de/wiki/DE-9_Mouse` (Individual Computers hardware wiki, DE-9 mouse comparison table) | Vendor hardware wiki | Independent ST **and** Amiga mouse DE-9 pinouts |
| S6 | `https://retrostuff.org/2013/12/31/atari-jaguar-mouse-adapter/` | Build log | Corroborates S1 as *the* reference; ST/Amiga "not directly compatible"; Elansar `*`/`#` |
| S7 | `src/jerry/joystick.c` (this repo) | Our model | Terms the mapping is expressed in |

### Note on fetching S1/S2/S3

`https://mdgames.de` fails TLS negotiation (`TLSV1_ALERT_INTERNAL_ERROR`) — this is the
failure recorded on #433 and #429. **The site serves fine over plain HTTP.** Use:

```bash
curl -sL http://www.mdgames.de/jag_eng.htm            # index, has #DIGITALMOUSE anchor
curl -sL http://www.mdgames.de/jagmouse.gif           # the wiring diagram
curl -sL http://www.mdgames.de/twomice2_20071230.zip  # TwoMice demo + source
curl -sL http://www.mdgames.de/colmouse.zip           # driver + READMOUS.TXT
curl -sL http://www.mdgames.de/oneaxis20060222.zip    # single-axis variant
```

The pages are ISO-8859-1; pipe through `iconv -f ISO-8859-1 -t UTF-8`. No Wayback
Machine fallback was needed.

---

## 2. Jaguar side: what the hardware can express

From **S4**, §"Signals and Pin outs" (verbatim structure):

| Conn. pin | Port 1 | Port 2 | Description |
|-----------|--------|--------|-------------|
| 1 | J3 | J4 | Bi-directional, used as **output** (row select) |
| 2 | J2 | J5 | Bi-directional, used as output |
| 3 | J1 | J6 | Bi-directional, used as output |
| 4 | J0 | J7 | Bi-directional, used as output |
| 5 | — | — | Reserved |
| 6 | B0 / LP | **B2** | Button input (light gun on Port 1) |
| 7 | +5V | +5V | +5V, 50 mA max |
| 8 | n/c | n/c | Pulled to +5V on 4-player adaptor |
| 9 | Gnd | Gnd | Ground |
| 10 | B1 | **B3** | Button input |
| 11 | J11 | **J15** | Input only |
| 12 | J10 | **J14** | Input only |
| 13 | J9 | **J13** | Input only |
| 14 | J8 | **J12** | Input only |
| 15 | — | — | Reserved |

Register layout, **S4** §"Register Addressing – Digital Inputs":

```
JOYSTICK  $F14000  R/W
  Read    fedcba98 7654321q    f-1 = signals J15..J1 ; q = cart EEPROM data out
  Write   exxxxxxm 76543210    e = 1 enables J7..J0 outputs
                               m = audio mute (1 = audio enabled)
                               7-4 = J7..J4 outputs (Port 2)
                               3-0 = J3..J0 outputs (Port 1)
JOYBUTS   $F14002  RO
  Read    xxxxxxxx rrdv3210    v = 1 NTSC / 0 PAL
                               3-2 = button inputs B3 & B2 (Port 2)
                               1-0 = button inputs B1 & B0 (Port 1)
```

So for **port 2**: `J12..J15` are `$F14000` bits **12..15**, and `B2`/`B3` are
`$F14002` bits **2**/**3**. Reading a zero means asserted (active low, **S4**
§"Standard Jaguar Controller Matrix": *"Reading a zero means the appropriate button
is depressed."*).

### Row select

**S4**: *"writing a value of `$817E` to JOYSTICK would allow you read row 0 of the
first controller connected to Port 1 and the first controller connected to Port 2…
`$0070` = Setup read of row 0 (code `%0111`) of controller 0, port 2."*

Domin's driver (**S2**, **S3**) writes **`$817F`** — the same port-2 row 0 code
`%0111` in bits 7-4, with bits 3-0 all high so port 1 is not selected:

```
move.w  #$817f,JOYSTICK   ; Socket A of Port 2
move.l  JOYSTICK,d0       ; Read joypad, pause button, A button
```

Row 0 of the standard matrix (**S4**) is exactly the row that carries the four
direction lines plus `A` and `Pause` — which is why the adapter hangs the mouse off it:

| | B2 | B3 | J12 | J13 | J14 | J15 |
|---|---|---|---|---|---|---|
| Row 0 | Pause | A | Up | Down | Left | Right |

### The same in `src/jerry/joystick.c` terms

`JoystickWriteWord` stores `$817F` as `joystick_ram[0]=$81`, `joystick_ram[1]=$7F`.
The port-2 nibble is `(joystick_ram[1] >> 4) & 0x0F == 7`, and
`joypad1Offset[7] == 0x00` → **row 0**, base index 0 into `joypad1Buttons[]`.

| `joypad1Buttons[]` index | Enum | `$F14000` read mask | Bit | Port-2 signal | Conn. pin |
|---|---|---|---|---|---|
| 0 | `BUTTON_U` | `0xEFFF` | 12 | J12 | 14 |
| 1 | `BUTTON_D` | `0xDFFF` | 13 | J13 | 13 |
| 2 | `BUTTON_L` | `0xBFFF` | 14 | J14 | 12 |
| 3 | `BUTTON_R` | `0x7FFF` | 15 | J15 | 11 |

| `joypad1Buttons[]` entry | `$F14002` read mask | Bit | Port-2 signal | Conn. pin |
|---|---|---|---|---|
| `BUTTON_A` | `0xFFF7` | 3 | B3 | 10 |
| `BUTTON_PAUSE` | `0xFFFB` | 2 | B2 | 6 |

**Our existing model is bit-exact against S4.** No change to the matrix is required to
carry a mouse; the adapter is passive wiring onto lines we already emulate.

---

## 3. Mouse side: DE-9 pinouts

From **S5** (Individual Computers wiki, `DE-9_Mouse`), raw table columns:

| DE-9 pin | Atari ST mouse | Amiga mouse |
|---|---|---|
| 1 | **XB** | **/V-Pulse (YA)** |
| 2 | **XA** | **/H-Pulse (XA)** |
| 3 | **YA** | **/VQ-Pulse (YB)** |
| 4 | **YB** | **/HQ-Pulse (XB)** |
| 5 | unused | /Button 3, MMB, Wheel (POTX) |
| 6 | /Button 1, LMB | /Button 1, LMB |
| 7 | VCC (+5V) | VCC (+5V) |
| 8 | GND | GND |
| 9 | /Button 2, RMB | /Button 2, RMB (POTY) |

The ST column matches **S1**'s diagram exactly (S1 labels pins 1/2/3/4 as XB/XA/YA/YB
and 6/7/8/9 as left button/+5V/GND/right button). Two independent sources, identical.

Amiga naming convention: `H`/`HQ` are the two horizontal quadrature phases, `V`/`VQ`
the two vertical ones. So the **axis pairing** is `{1,3}` = vertical and `{2,4}` =
horizontal — the opposite grouping to the ST's `{1,2}` = X, `{3,4}` = Y. *That* is the
incompatibility, and it is why an adapter cannot serve both mice with one wiring.

---

## 4. Adapter wirings (VERIFIED)

### 4a. Atari ST adapter

**S1** (diagram table, verbatim) and **S2** (source header) agree exactly:

```
; The pinout for the adapter for Atari ST digital mice:
; DSUB 9 male - Jaguar DSUB-HD15 male
;           1 - 14     ( J8/J12)
;           2 - 13     ( J9/J13)
;           3 - 12     (J10/J14)
;           4 - 11     (J11/J15)
;           5 - not connected
;           6 - 10     ( B1/ B3)
;           7 -  7     ( +5V DC)
;           8 -  9     (    Gnd)
;           9 -  6     ( B0/ B2)
```

Resolved through §2 and §3 into the terms `joystick.c` uses — **port 2, row select
`$817F`**:

| ST mouse pin | Signal | Jag pin | Port-2 line | `$F14000`/`$F14002` bit | `joypad1Buttons[]` slot |
|---|---|---|---|---|---|
| 1 | **XB** (X phase B) | 14 | J12 | `$F14000` bit **12** | `BUTTON_U` (idx 0) |
| 2 | **XA** (X phase A) | 13 | J13 | `$F14000` bit **13** | `BUTTON_D` (idx 1) |
| 3 | **YA** (Y phase A) | 12 | J14 | `$F14000` bit **14** | `BUTTON_L` (idx 2) |
| 4 | **YB** (Y phase B) | 11 | J15 | `$F14000` bit **15** | `BUTTON_R` (idx 3) |
| 5 | n/c | — | — | — | — |
| 6 | **Left button** | 10 | B3 | `$F14002` bit **3** | `BUTTON_A` |
| 7 | +5V | 7 | +5V | — | — |
| 8 | GND | 9 | Gnd | — | — |
| 9 | **Right button** | 6 | B2 | `$F14002` bit **2** | `BUTTON_PAUSE` |

All active low (0 = asserted).

- **X axis** = bits **12 + 13** (the Up/Down lines), A phase on bit 13.
- **Y axis** = bits **14 + 15** (the Left/Right lines), A phase on bit 14.

Cross-check against **S2**'s ST decode table (`AxisTestbits`, first entry of each pair):
`XA = KEY_D` (bit 13) ✔, `XB = KEY_U` (bit 12) ✔, `YA = KEY_R` (bit 15),
`YB = KEY_L` (bit 14). The X entries match the wiring exactly; the Y entries are
A/B-swapped relative to the diagram — see
[Y-phase labelling](#c-y-phase-labelling-slip-in-s2s-st-table). This is a sign
convention only, absorbed by `dirtab_vertical`; the *pairing* (bits 14+15 = Y) is
unaffected and agreed by both.

Domin's own comments confirm the pairing in prose:

> `; Joyport-input pins for buttons R and L are tied to YA and YB of the mouse-adapter`
> `; Joyport-input pins for buttons U and D are tied to XA and XB of the mouse-adapter`

### 4b. Amiga adapter

**S2** source header, verbatim:

```
; And here ist the pinout for an Amiga-mouse-adapter.
; DSUB 9 male - Jaguar DSUB-HD15 male
;           1 - 11     (J11/J15)
;           2 - 13     ( J9/J13)
;           3 - 12     (J10/J14)
;           4 - 14     ( J8/J12)
;           5 - not connected
;           6 - 10     ( B1/ B3)
;           7 -  7     ( +5V DC)
;           8 -  9     (    Gnd)
;           9 -  6     ( B0/ B2)
```

Only **mouse pins 1 and 4 are swapped** relative to the ST adapter. Buttons and power
are identical.

| Amiga mouse pin | Signal | Jag pin | Port-2 line | Bit | `joypad1Buttons[]` slot |
|---|---|---|---|---|---|
| 1 | **V** (Y phase A) | 11 | J15 | `$F14000` bit **15** | `BUTTON_R` (idx 3) |
| 2 | **H** (X phase A) | 13 | J13 | `$F14000` bit **13** | `BUTTON_D` (idx 1) |
| 3 | **VQ** (Y phase B) | 12 | J14 | `$F14000` bit **14** | `BUTTON_L` (idx 2) |
| 4 | **HQ** (X phase B) | 14 | J12 | `$F14000` bit **12** | `BUTTON_U` (idx 0) |
| 5 | n/c | — | — | — | — |
| 6 | **Left button** | 10 | B3 | `$F14002` bit **3** | `BUTTON_A` |
| 7 | +5V | 7 | +5V | — | — |
| 8 | GND | 9 | Gnd | — | — |
| 9 | **Right button** | 6 | B2 | `$F14002` bit **2** | `BUTTON_PAUSE` |

- **X axis** = bits **12 + 13** — *same lines as ST*.
- **Y axis** = bits **14 + 15** — *same lines as ST*.

**The dedicated Amiga adapter deliberately normalises an Amiga mouse onto the identical
Jaguar-side lines as the ST adapter.** The pin-1/pin-4 swap exists precisely to undo the
Amiga's `{1,3}`-vertical / `{2,4}`-horizontal grouping. Domin says so operationally:
an Amiga mouse on the Amiga adapter is used in *mode 1*, the same mode as an ST mouse
on the ST adapter (**S2** header: *"If you have an Amiga-mouse and this adapter you can
use the software in mode '1' with the red canvas"*).

### 4c. Amiga mouse in an **ST-wired** adapter (the "pick your mouse type" case)

**S2** header: *"to run the software in mode '2' with the blue canvas is only necessary
if you have an Amiga-mouse and the above described 'Atari ST mouse'-adapter."*

This is the case a *software* ST/Amiga selector exists to serve: one physical adapter,
either mouse. Composing the ST adapter wiring (§4a) with the Amiga mouse pinout (§3):

| Amiga mouse pin | Signal | Jag pin | Bit | Axis role |
|---|---|---|---|---|
| 1 | **V** (Y phase A) | 14 | **12** | Y-A |
| 2 | **H** (X phase A) | 13 | **13** | X-A |
| 3 | **VQ** (Y phase B) | 12 | **14** | Y-B |
| 4 | **HQ** (X phase B) | 11 | **15** | X-B |

- **X axis** = bits **13 + 15** (A on 13, B on 15).
- **Y axis** = bits **12 + 14** (A on 12, B on 14).

**Cross-validation.** **S2**'s mode-2 (`Amiga`) entries in `AxisTestbits` are:

```
bitsYA:  dc.l KEY_R  (ST)   dc.l KEY_U  (Amiga)   -> bit 12
bitsYB:  dc.l KEY_L  (ST)   dc.l KEY_L  (Amiga)   -> bit 14
bitsXA:  dc.l KEY_D  (ST)   dc.l KEY_D  (Amiga)   -> bit 13
bitsXB:  dc.l KEY_U  (ST)   dc.l KEY_R  (Amiga)   -> bit 15
```

X = {13, 15}, Y = {12, 14}, with A on 13 and 12 respectively — an **exact match**,
including phase sense.

This agreement is the strongest Amiga evidence in this document, and it is *mutual*:
Domin's mode-2 table only resolves into clean, unmixed axis pairs if **S5**'s Amiga
column (1=V, 2=H, 3=VQ, 4=HQ) is correct; and **S5**'s Amiga column is only consistent
with **S2**'s Amiga *adapter* wiring (§4b) if that same grouping holds. Two sources with
no common ancestry — a German homebrew author's 2006 assembler and a German hardware
vendor's wiki table — each independently confirm the other. Neither was written with the
other in view.

### 4d. Summary of the three cases

| Case | Adapter | Mouse | X pair (A, B) | Y pair (A, B) |
|---|---|---|---|---|
| 1 | ST | ST | bits **13, 12** | bits **14, 15** |
| 2 | Amiga | Amiga | bits **13, 12** | bits **15, 14** |
| 3 | ST | Amiga | bits **13, 15** | bits **12, 14** |

Buttons are identical in all three: LMB → `$F14002` bit 3 (`BUTTON_A`),
RMB → `$F14002` bit 2 (`BUTTON_PAUSE`).

Cases 1 and 2 use the same *lines*; they differ only in Y **phase sense** (a sign flip
on the vertical axis — see [unresolved](#b-y-axis-sign-between-case-1-and-case-2)).
Case 3 is a genuinely different pairing.

---

## 5. Quadrature decode

### Phase ordering — there is none

**Item 5 of #433 asks whether phase output is "X then Y, or interleaved". Neither: the
premise does not apply.** XA, XB, YA and YB are four *parallel wires* (Jaguar pins 14,
13, 12, 11) sampled simultaneously by a single read of `$F14000` in row 0. There is no
time multiplexing and no ordering between the axes. Both axes advance independently and
concurrently. Verified from **S1** (four separate conductors) and **S2** (one
`move.l JOYSTICK,d0` yields all four bits).

### Encoding

**S1**'s waveform panel shows XA and XB as two square waves in quadrature, annotated
*"positive direction"* (XA leading) and *"negative direct."* (XB leading), with the
parenthetical `(YA)`/`(YB)` marking the Y axis as identical in form.

**S3** gives the state encoding as a weighted sum, `s = 2·A − 1·B`:

```
     XA (YA) 0  1  1  0  0  1  -->  0 +2 +2  0  0 +2
     XB (YB) 0  0  1  1  0  0  -->  0  0 -1 -1  0  0
                                   ------------------
                                    0  2  1 -1  0  2 --> positive

     XA (YA) 0  0  1  1  0  0  -->  0  0 +2 +2  0  0
     XB (YB) 0  1  1  0  0  1  -->  0 -1 -1  0  0 -1
                                   ------------------
                                    0 -1  1  2  0 -1 --> negative
```

Equivalently, in `(A, B)` pairs, the standard 2-bit Gray code:

```
positive:  (0,0) -> (1,0) -> (1,1) -> (0,1) -> (0,0) ...   A leads B
negative:  (0,0) -> (0,1) -> (1,1) -> (1,0) -> (0,0) ...   B leads A
```

**S2** replaces the arithmetic with a 16-entry lookup indexed by
`(old_state << 2) | new_state`, where `state = (B << 1) | A`. Its comment block states
the full truth table:

```
; New    Old
; 0,0  ; 0,0 --> no movement / 0,1 --> negative / 1,0 --> positive / 1,1 --> undetermined
; 0,1  ; 0,0 --> positive    / 0,1 --> no movement / 1,0 --> undetermined / 1,1 --> negative
; 1,0  ; 0,0 --> negative    / 0,1 --> no movement / 1,0 --> undetermined / 1,1 --> positive
; 1,1  ; 0,0 --> undetermined/ 0,1 --> positive / 1,0 --> negative / 1,1 --> no movement
```

Direction sense, from `dirtab_horizontal` / `dirtab_vertical` (**S2**):

| Transition | Horizontal | Vertical |
|---|---|---|
| A leads B (positive) | `KEY_R` | `KEY_D` |
| B leads A (negative) | `KEY_L` | `KEY_U` |
| No change | none | none |
| Diagonal jump (2 states) | `-1` — **silently discarded** | `-1` — **silently discarded** |

So **A-leads-B = right / down**.

### Corroborating Atari spec for a quadrature device on these very pins

**S4** documents the Rotary "Tempest" controller, which puts a two-phase optical encoder
on `J10`/`J11` (port 1 pins 12/11 — the *same physical pins* the mouse adapter uses for
its Y axis), read from row 0 alongside `Pause` and `A`:

```
Anticlockwise Sequence   J10 (pin 12)   0  1  1  0  0  1  1 ...
                         J11 (pin 11)   0  0  1  1  0  0  1 ...
Clockwise Sequence       J10 (pin 12)   0  0  1  1  0  0  1 ...
                         J11 (pin 11)   0  1  1  0  0  1  1 ...
```

Atari's own row-0 matrix for it reads `… J14 = Phase 0, J15 = Phase 1`. This is bit-for-bit
the same Gray-code sequence as **S3**'s table, on the same lines, sanctioned by the
hardware manual. It independently establishes that a quadrature device read from port
row 0 on the direction lines is a *supported, documented* Jaguar controller shape — the
ST mouse adapter is not an off-spec hack.

### Rate ceiling

**S3**: *"The readmouse-function should be called more than 1000 times per second to get
good results. After the readmouse()-call the RLDU-results should be evaluated
immediately; the mouse-buttons (redirected to FireA and Pause) should be evaluated the
normal 50/60 times per second."*

**S2** implements exactly that with a **1200 Hz timer IRQ**: *"Now a Timer-IRQ (1200Hz)
is used to read from port 2 where the digital mouse (or RotaryController) is plugged
in."*

The binding constraint is not the mouse's physical pulse rate — it is the decoder. A
transition of **two** Gray-code states between consecutive polls indexes a `-1` entry
and is **silently dropped** (see table above); the decoder cannot recover direction from
it. Therefore:

> **Emitted state transitions must not exceed the rate at which the game polls row 0 of
> port 2.** Exceeding it does not merely lag — it loses motion outright, and near the
> threshold it loses motion *non-uniformly*, which reads as jitter.

A game polling only at VBL (50/60 Hz) would cap out around 50–60 states/s ≈ 12–15
quadrature cycles/s, which is why Domin insists on a >1 kHz timer IRQ. The emulator
cannot know a given title's poll rate a priori.

**Recommendation (not a sourced fact):** advance the synthesised quadrature by **at most
one Gray-code state per read of `$F14000` with port 2's row select asserted** — i.e.
self-clock the emission against the game's own polling. That is correct for any poll rate
without needing to know it, and it degrades to "pointer lags" rather than "pointer
jitters or reverses" when the accumulated delta outruns the poll rate.

Note the hook must be *any* port-2 row, not row 0 specifically: per §7 the adapter is
row-blind, so a game is free to poll the mouse from rows 1–3, and a row-0-only hook would
return a frozen mouse to such a title.

---

## 6. PS/2 adapter — vendor-documented as ST-compatible

**Answer: yes, effectively — one implementation covers both.** This rests on a vendor
behavioural statement, not a schematic; see the caveat below.

AtariAge's own product page for the PS/2 adapter (**S8**,
`https://store.atariage.com/products/ps2-mouse-adapter-for-atari-jaguar`) states:

> "Use a PS/2 mouse with games that include **Atari ST mouse support**, as well as any
> future games that include ST mouse support."
>
> "Plug the 15-pin end of the adapter into the second controller port on your Jaguar and
> then plug your PS/2 mouse into the other end. Boot up the game and **select Atari ST
> mouse** as your controller."

The listed compatible titles are the same set as the ST/Amiga adapter — Defender of the
Crown, Gridrunner (Jeff Minter Classics), Elansar, Star Wars Arcade (ST port). The
instruction to select **Atari ST** in games that offer an ST/Amiga choice is a direct
statement that the adapter's Jaguar-side output is the ST arrangement of §4a.

**Caveat, and why no schematic can settle it.** PS/2 is a clocked bidirectional serial
protocol; the Jaguar side is parallel quadrature. A PS/2 adapter therefore *must* contain
a microcontroller synthesising quadrature from PS/2 packets. Whether that output is
ST-identical is a **firmware choice, not a wiring fact** — only firmware source or
measurement could prove it. The vendor instruction above is the strongest available
evidence and is consistent with the ecosystem argument: ST quadrature on port 2 row 0 is
the only mouse encoding any shipped Jaguar title is known to decode, so an adapter
emitting anything else would work with nothing.

**Consequence for #429:** the PS/2 adapter needs no separate emulation path. It is the
ST wiring (case 1 of §4d).

*(Songbird also sells a Jaguar PS/2 mouse adapter, **S9** — its page carries no mouse-type
or technical detail at all, listing only ImpulseX and Vroom as compatible titles.)*

---

## 7. Implementation traps (INFERRED from the S4 matrix, not from any mouse document)

### The generating principle: the adapter is row-blind

Look at what the adapter does **not** connect. In both wirings (§4a, §4b) the Jaguar-side
pins used are 6, 7, 9, 10, 11, 12, 13, 14. **Jaguar pins 1–4 are absent** — and those are
exactly `J4`–`J7`, the port-2 row-select *outputs* (**S4** §"Signals and Pin outs").

> **The adapter cannot see the row select at all.** It is passive wiring from a mouse
> that has no notion of rows. Therefore all six port-2 input lines — `J12`, `J13`, `J14`,
> `J15`, `B2`, `B3` — carry the mouse's state **continuously and identically in rows 0,
> 1, 2 and 3.**

A Jaguar pad is a matrix; a mouse adapter is not. This single fact generates every trap
below, and it is the main thing an implementer will otherwise rediscover the hard way.

Corroboration from **S2**: Domin's program contains a normal full four-row scan
(`$817F`/`$81BF`/`$81DF`/`$81EF`, lines 408–480) *and* a `readmouse` routine that
deliberately reads **only row 0** and ignores the rest. That asymmetry in the author's own
code is consistent with the effect being real and sidestepped rather than absent.

### Consequences

1. **Direction lines produce phantom keypad presses.** In rows 1/2/3, bits 12–15 carry
   `*`/7/4/1, then 2/5/8/0, then 3/6/9/# (**S4** standard matrix; `joypad1Offset[]` and
   `joypad1Buttons[]` indices 4–15 in `joystick.c`). With the mouse moving, a game that
   performs a full four-row scan sees keypad digits asserted on every row.

   The emulator's divergence runs the *opposite* way from a naive fix: if #429 simply
   sets `joypad1Buttons[BUTTON_U/D/L/R]` (indices 0–3), the core returns the mouse in
   row 0 and **nothing** in rows 1–3, where hardware returns the same bits. Both the
   naive model and the hardware differ from each other — the mouse device must drive the
   raw `$F14000` bits 12–15 irrespective of the selected row, not occupy pad slots.

2. **LMB reads as A *and* B *and* C *and* Option.** `joystick.c` reports
   `BUTTON_A`/`B`/`C`/`OPTION` on `$F14002` bit 3 according to the selected row
   (`mask[4][2]` in `JoystickReadWord`). A real adapter holds bit 3 low in every row
   while LMB is down.

3. **RMB perturbs controller-type identification.** `B2` is the `C1`/`C2`/`C3` type-ID
   bit in rows 1/2/3 (**S4** §"Standard Jaguar Controller Matrix" and §"Identifying
   Controller Types": `C2 C3 = 1 1` means "Standard Jaguar Joypad (or nothing
   connected)"). Holding RMB pulls `B2` low in those rows too, changing the reported
   type — so a title that re-probes controller type while the right button is held may
   misidentify the device. Our current model decodes `BUTTON_PAUSE` on bit 2 only in row
   0 and hard-codes `$FF6F` elsewhere, so it does not reproduce this today.

### Unrelated but adjacent

4. **`$F14002` bit 4 is the NTSC/PAL bit** (`v` in the S4 layout, `vjs.hardwareTypeNTSC`
   in `joystick.c`). Nothing mouse-related may disturb it.

5. **Guardrail for #429 holds trivially.** The adapter changes no register semantics — it
   only asserts existing port-2 input lines. With no mouse device selected,
   `$F14000`/`$F14002` behaviour is unchanged by construction.

---

## 8. Unresolved / not sourced

Everything below is explicitly **not** established. None of it blocks #429; each item is
a permutation or sign choice over the same four bits, settled by one test against a real
title.

### A. Which case an in-game "Atari / Amiga" selector expects

Elansar's `*` = Atari / `#` = Amiga selector is documented (**S6**, and #429's own
description), but **nothing retrieved connects Elansar's two modes to Domin's mode 1 /
mode 2.** Elansar is a different author (Orion_/Powertrip). Its "Amiga" mode could mean:

- **case 3** (Amiga mouse in an ST-wired adapter) — pairing X = {13,15}, Y = {12,14}; or
- **case 2** (a dedicated Amiga adapter) — same pairing as ST, Y phases swapped.

These are different bit pairings and produce visibly different wrong behaviour if
mismatched (case 3 read as case 1 gives motion coupled diagonally between axes, not just
an inverted axis).

**Evidence favouring case 3 (strong, but still not a schematic).** AtariAge's product
page for the ST/Amiga adapter (**S10**) describes **one** physical adapter serving both
mice, with the type chosen in software:

> "Plug the 15-pin end of the adapter into the second controller port on your Jaguar and
> then plug your **Atari ST or Amiga mouse** into the other end. Boot up the game and
> **select which type of mouse you have plugged in** and you're good to go!"

and it additionally supports "any **Atari ST-compatible** trackballs" — which only works
if that single adapter is ST-wired. So the shipping hardware is an ST-wired adapter into
which an Amiga mouse may be plugged, and the in-game selector exists to compensate. That
is precisely **case 3**, and it is the same situation Domin's mode 2 addresses. The
remaining gap is only that no source states Elansar implements the selector this way
rather than some other convention of its own.

*Resolution:* implement §4d as a 3-entry table and settle the Elansar mapping
empirically — drive synthetic quadrature per case and observe which produces clean
axis-aligned cursor motion. Expect case 3; a mismatch shows up as motion coupled
diagonally between axes, which is unmistakable.

*Still open.* The 3-entry table shipped (PR #449) but Elansar has not been run against
it. Note also that whichever way it lands, it cannot become a per-title default:
Elansar and Philia are **CD** titles, and `src/core/titledb.c` is keyed on cartridge
CRCs and deliberately never hashes a disc image (`libretro.c`, the
`info->data && !is_cd_content` guard). Elansar's wiring case will always be a manual
option choice, so the answer belongs in the user guide
([`docs/input-devices-user-guide.md`](input-devices-user-guide.md)), not in the
database.

### B. Y-axis sign between case 1 and case 2

The dedicated Amiga adapter (§4b) lands the Amiga's `V` (Y phase A) on bit 15 and `VQ`
(Y phase B) on bit 14, whereas the ST adapter lands `YA` on bit 14 and `YB` on bit 15 —
i.e. the two are A/B-swapped on the vertical axis, which inverts Y. Domin nonetheless
uses one software mode for both. Possible explanations, none sourced:

- ST and Amiga mice differ in encoder orientation, so the swap *cancels* a physical
  difference (plausible — it would explain why he chose that wiring);
- **S5**'s `(YA)`/`(YB)` parentheticals on the Amiga column are the wiki author's own
  editorial equivalence to ST naming rather than a measurement, so the A/B labels may
  not be comparable across columns at all;
- Domin simply tolerated an inverted Y.

*Resolution:* a sign flip, verifiable in one run against a real title. Recorded so it is
not silently guessed.

### C. Y-phase labelling slip in S2's ST table

**S1**'s diagram puts ST `YA` on Jaguar pin 12 (bit 14) and `YB` on pin 11 (bit 15).
**S2**'s ST software table names bit 15 `YA` and bit 14 `YB` — the reverse. The
*physical wiring* is not in doubt (S1 and S2's own header comment agree, and S5
independently confirms the ST mouse pinout); only the driver's internal variable naming
differs, and `dirtab_vertical` absorbs it. Flagged because anyone reading **S2** alone
would derive an inverted Y.

### D. PS/2 adapter — answered by vendor prose, not by schematic

§6 answers item 4 in the affirmative on the strength of AtariAge's own instruction to
"select Atari ST mouse". **No schematic or firmware source was located**, and by the
nature of the device none would be conclusive anyway (§6 caveat). Treat "PS/2 = ST
wiring" as vendor-documented behaviour, adequate for #429, not as a measured fact.

### E. jag_sim / MiSTer — no model; no netlist tie-breaker exists

Checked because netlists outrank prose in this project (cf. #354). **Confirmed negative
in both** (method and results in §9): neither models a mouse adapter.

This is the expected result, not a gap. The ST adapter is passive wiring onto the
standard pad matrix, so there is nothing for a netlist to model beyond the matrix itself
— which **S4** specifies and `joystick.c` already implements bit-exactly.

One useful positive did come out of jag_sim: `netlists/jerry/JMISC.NET` implements the
joystick output enable as `Joyen := LDP2Q (joyen, din[15], joy1w, resetl)` — the write
data bit 15 latched as the J7..J0 output enable, corroborating **S4**'s register layout
at netlist level (and hence Domin's `$81xx` writes).

### F. Not attempted

- Physical measurement of a real adapter (none on hand).
- Contacting AtariAge / The Brewing Academy for a schematic — worth doing for the PS/2
  question if it ever becomes load-bearing.
- ~~Disassembling Elansar or JagDoomEX to read their mouse routine directly.~~
  **JagDoomEX: DONE, and the answer is negative** (see F.1). **Elansar: still not
  attempted** — it is a CD title, so its executable has to be pulled out of the disc
  image first, and item A remains open until someone does that or runs the game against
  each option value.

#### F.1. JagDoomEX does NOT read a mouse — settled by disassembly

Checked against every JagDoomEX CRC carried in `src/core/titledb.c`
(`0x754096DB`, `0x4643E9DB`, `0x35743B9C`, `0xAD6B68BA`, `0xC4F4CACF`, `0x1F4EE4A5`,
`0x013A5359`, `0xB92D1CA3`, `0xEA12E234`), each reproduced by applying the matching IPS
patch to the verified retail dump (`Doom - Evil Unleashed (1994).jag`, `0x5E2CDBC0`),
plus the local `Doom (World) EX.j64` (`0xEE7B84EB`) and the newest patch archive
(`JagDoomEX-ips-250318.7z`, which reapplies to `0x35743B9C`).

Every one of them is **port 1 only**, by two independent mechanisms:

1. The pad poll writes only the port-1 row selects `$81FE`, `$81FD`, `$81FB`, `$81F7`.
   The port-2 nibble is `$F` in all four, i.e. **every port-2 row is deselected**.
2. The poll reads `move.l $F14000` and immediately does `or.l #$F0FFFFFC,d0` before
   folding into an 8-bit accumulator. That mask clears everything except `$F14000`
   bits 8-11 and `$F14002` bits 0-1 — the port-1 columns. `$F14000` bits 12-15, the
   four lines a mouse adapter drives, **cannot reach the result**.

And there is no third path: all **11** absolute-long `$F14000` references in the image
are accounted for — 8 in that poll (4 writes + 4 reads), one `move.l #$100,$F14000` at
init, and two `lea $F14000,a0` in the EEPROM driver (which reads bit 0 and `$F15000`).
A driver reading the register through a register-indirect base would still need one of
these to load the address.

Consequence, recorded at the site in `src/core/titledb.c`: **no
`virtualjaguar_p2_device` row ships for JagDoomEX**, and #429's "auto-select the mouse
for JagDoomEX by CRC" checklist item is not applicable to any released build. If a
future release adds mouse support it will hash differently from all nine CRCs above, so
it needs its own row and its own re-check.

---

## 9. Search log

Recorded so the next person does not repeat it.

### Succeeded

| Attempt | Outcome |
|---|---|
| `WebFetch https://mdgames.de/...` | **Failed** — `TLSV1_ALERT_INTERNAL_ERROR`. Same failure recorded on #429/#433. |
| `curl --tlsv1.2 https://mdgames.de/` | **Failed** — HTTP 000, TLS handshake refused. |
| `curl http://mdgames.de/` (plain HTTP) | **Worked, HTTP 200.** This is the fix. No Wayback needed. |
| `retrostuff.org` build log | Gave the exact primary URL (`jag_eng.htm#DIGITALMOUSE`). |
| `http://www.mdgames.de/jagmouse.gif` | Wiring diagram (S1). Converted GIF→PNG with `sips` to read. |
| `http://www.mdgames.de/twomice2_20071230.zip` | **Best single source** — `twomice2.s` header carries *both* ST and Amiga adapter pinouts plus the ST/Amiga decode tables (S2). |
| `http://www.mdgames.de/colmouse.zip` | `READMOUS.TXT`: decode arithmetic + poll-rate guidance (S3). |
| `docs/atari-jaguar-1999/Technical Reference v10.pdf` | Physical pinout, register bits, row codes, standard matrix, Rotary quadrature spec (S4). Extract with `pdftotext -layout`. |
| `wiki.icomp.de/wiki/DE-9_Mouse` | Independent ST **and** Amiga DE-9 pinouts (S5). **Fetch the raw HTML** — the summarising fetch transposed the ST and Amiga columns. |
| AtariAge product pages (S8, S10) | PS/2 = "select Atari ST mouse"; ST/Amiga = one adapter, in-game selection, ST-compatible trackballs. |

### Came up empty

| Attempt | Outcome |
|---|---|
| GitHub code search for `mouse`/`quadrature`/`F14000` in `ElectronAsh/jag_sim`, `Torlus/Jaguar_MiSTer_master` | 0 hits — but the API appears not to index these repos (0 hits for `joystick` too). **Do not trust this method here**; clone and grep. |
| `git clone` + grep `jag_sim` | Only "mouse" hit repo-wide is GPL boilerplate (`soft/hello/gpl.txt`). 0 hits for "quadrature". No mouse model. |
| `git clone` + grep `Torlus/Jaguar_MiSTer_master` | `ps2_mouse` appears **only** in `sys/hps_io.v`, the generic MiSTer framework file; never referenced by `Jaguar.sv` or any core source. The core never consumes it. No mouse model. |
| Web search for open-source Jaguar mouse/PS-2 adapter firmware | Nothing Jaguar-specific. Nearest neighbours are all *Atari-computer*-side or Jaguar-pad-to-USB (opposite direction): `jjmz/Atari-Quadrature-USB-Mouse-Adapter`, `IJeffray/WeeMouse`, `raphnet/jaguar_usb`, `dgrubb/Jaguar-USB-tap`. `jjmz`'s project does emit ST/Amiga quadrature and has explicit ST vs Amiga modes, so it may be useful as a *second opinion on phase conventions*, but it targets Atari/Amiga computers, not the Jaguar port, and was not relied on here. |
| Songbird PS/2 adapter page (S9) | No mouse-type or technical detail. |
| The Brewing Academy ST adapter page | No schematic. |

### Additional sources referenced above

| # | Source |
|---|---|
| S8 | `https://store.atariage.com/products/ps2-mouse-adapter-for-atari-jaguar` |
| S9 | `https://songbird-productions.com/product/ps2-mouse-adapter/` |
| S10 | `https://store.atariage.com/products/atari-stamiga-mouse-adapter-for-atari-jaguar` |
| S11 | `ElectronAsh/jag_sim`, `netlists/jerry/JMISC.NET` (joystick output enable) |

---

## 10. Verdict for #429

**Unblocked.** #433's "Done when" asks for a written pin → row/column-bit table for both
ST and Amiga with sources cited; §4a, §4b and §4c deliver that, with four independent
sources in agreement and no contradiction requiring a schematic-over-prose tiebreak
(the only two label discrepancies, §8 B and §8 C, are sign conventions internal to one
source's variable naming).

What an implementer needs is entirely in §2, §4d and §5:

- Row select `$817F` → port 2 row 0 is what the known drivers use (already
  `joypad1Offset[7] == 0x00` in `joystick.c`).
- Four direction bits 12–15 and two button bits 3/2, all active low — but driven
  **row-independently**, not through the pad matrix (§7). This is the one part that is
  genuinely new work rather than reuse.
- Three wiring cases as a table (§4d); case 1 for ST and PS/2, case 3 the expected Amiga
  selector meaning.
- Emit at most one Gray-code state per port-2 read.

Carry forward into #429: settle §8 A (which case the Elansar selector means) and §8 B
(Y sign) by test, and honour the two button traps in §7.
