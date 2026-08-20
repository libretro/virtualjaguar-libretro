# Spike: Team Tap (#513) and Pro Controller (#514) — what the manual actually documents

Documentation spike gating issues
[#513](https://github.com/libretro/virtualjaguar-libretro/issues/513) (Team Tap, the
4-player adapter) and
[#514](https://github.com/libretro/virtualjaguar-libretro/issues/514) (Pro Controller,
the six-button pad). Both issues say implementation is blocked until someone establishes
what the Jaguar Technical Reference Manual documents about these devices. This is that
pass. **No emulator code was written.**

Format and honesty standard follow [`docs/lightgun-design.md`](lightgun-design.md):
every claim names its source, plausible-but-unconfirmed is labelled as such, and the
numbered open-questions table in §7 is the list of things this document could *not*
settle.

**Verdict up front:**

| Device | Verdict |
|---|---|
| **Team Tap (#513)** | **Implementable now.** TR10 documents it completely — the full 16-row socket/row-code table, the transparent row-code conversion, the detection method, and the identifying diode. Two independent manual revisions carry the same table, and a PD ROM in the corpus implements the documented detection byte-for-byte. Size **M**. |
| **Pro Controller (#514)** | **Blocked on a source we do not have.** The device is absent from every volume of the manual set on this machine. TR10's controller taxonomy is closed and its 21 data bits are fully allocated by the standard pad, which forces a dichotomy (§6.3) that no available document resolves. Partially answerable from ROM disassembly — but only partially, and the corpus scan run for this document did not answer it. |

---

## 1. Primary sources

| Ref | Source | Used for |
|---|---|---|
| **TR10** | `docs/atari-jaguar-1999/Technical Reference v10.pdf` — "Jaguar Technical Reference Manual Version 10.0", Stephen Moss, 18 Dec 2010, self-described as *"an amended and updated version of the original Atari documentation, Copyright Atari Corporation 1995"*. Text layer present. | The controller chapter: pinout, JOYSTICK/JOYBUTS bit layout, row codes, standard matrix, rotary, **4-Player Adaptor (Team Tap)**, bank-switching/advanced controllers, appendix schematic notes |
| **TR04** | `docs/atari-jaguar-1999/04 - Technical Reference.pdf` — the **Atari original**, footer *"© 1995 Atari Corp. / Confidential Information / Property of Atari Corporation / 26 April, 1995"*. Image-only scan; OCR'd (31 pp) and the key page read as a rendered image for this document. | Independent corroboration of TR10's Team Tap section, including the identical 16-row table; and the one place the two revisions genuinely disagree (§3.6) |
| **TR8** | `docs/atari-jaguar-1999/Technical Reference v8.pdf` — Atari, Revision 8, "SECRET CONFIDENTIAL". Text layer present. | JERRY-side electrical view: `JOY1 $F14000`, `JOY2 $F14002`, the DB15 pinout, `B0/LP0` pin sharing. Contains **no** controller-product documentation — no Team Tap, no socket concept |
| **IDX** | `docs/atari-jaguar-1999/00 - Index.pdf` — "Atari Jaguar Development Manual" master table of contents, Nov 1994. Image-only scan; OCR'd (16 pp). | Enumerating every controller subsection across the whole documentation set, to bound the §6 negative |
| **LIB** | `docs/atari-jaguar-1999/10 - Libraries.pdf` (65 pp) and `09 - Sample Programs.pdf` (13 pp). Image-only scans; OCR'd. | Second and third places a controller would surface if it had library or sample support |
| **TESTER** | `test/roms/private/ROMS/Public Domain/Joypad-TeamTap Tester by Matthias Domin (2000) (PD).jag` — 176,946 bytes, plus the `[a1]` (176,900) and `[a2]` (179,602) dumps. Already listed in [`docs/cart-boot-matrix.md`](cart-boot-matrix.md); two of three reach `GAME_CODE` in both boot modes. | Direct, primary confirmation that a real program drives the TR10 row codes and implements the TR10 detection sequence |
| **SURVEY** | Static scan of 27 corpus ROMs for `move.w #imm,$00F14000` (opcode `33FC iiii 00F1 4000`), classified against TR10's table. Scanner written for this document; not committed. | Bounded supporting exhibit — see §5.3 for what it can and cannot show |
| **JOYSTICK.C** | `src/jerry/joystick.c` (this repo) | The shipping decode the change has to extend, and what the core does *today* when a title probes for a Team Tap |
| **RM / JERRY** | `docs/jtrm-register-map.md` §"Joystick Registers", `docs/jtrm-jerry.md` §"Joystick Interface" | The distilled docs the issues point at. Both cover the base 4-to-16 mux only, and `jtrm-jerry.md`'s joystick section is sourced from `joystick.c`, not from the manual — see §7 Q6 |
| **JAG_SIM** | The Flare/Atari TOM & JERRY design netlists (ElectronAsh/Torlus `jag_sim`), cited in this repo by `docs/jtrm-object-processor.md` (`jag_sim/netlists/tom/OB.NET:55-67`) and `docs/gpu-timing-verilator-results.md` | **Consulted as authority #3 and found not applicable to either device.** Not checked out on this machine; the argument does not need it to be. The netlists model TOM/JERRY silicon, and **both devices sit outside that boundary** — the Team Tap is an external adapter on the far side of the DB15, the Pro Controller an external controller. TR10 §3.3 is the reason this is structural rather than incidental: the adapter *"converts the row codes for sockets 1-3 so that those controllers will only see socket 0 row codes"*, so there is no socket concept on the console side for a netlist to contain. JERRY sees sixteen row codes and six input lines whether or not a tap is plugged in. Anyone with a checkout can corroborate cheaply by grepping the JERRY joystick decode for any socket awareness; the expected result is none |

**What was NOT read.** The manual set is 20 PDFs; 18 are image-only scans with no text
layer (they extract to 6–103 bytes). Four of those were OCR'd in full for this document
(TR04, IDX, LIB, Sample Programs). The remaining 14 — Getting Started, Technical
Overview, Software Reference, Hardware Bugs & Warnings, Jaguar CD-ROM, Voice Modem,
Workshop Series, QSound, Cinepak, Tools, Appendices, Madmac, ALN Linker, DB — were not
OCR'd. The bound on that gap is IDX: it is the master table of contents across the whole
set, it enumerates the controller subsections (§6.2), and none of them lives in a volume
that was skipped. That is a bound, not a proof; §7 Q5 records it as such.

**Sources deliberately not used:** no wiki, no forum, no recollection. Where this
document cannot answer a question it says so rather than reaching for one.

---

## 2. What the core does today (verified in-repo, not assumed)

`src/jerry/joystick.c` decodes the JOYSTICK register's row-select byte through two
16-entry tables:

```c
/* E, D, B, 7 */
static const uint8_t joypad0Offset[16] = {
	0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x0C, 0xFF, 0xFF, 0xFF, 0x08, 0xFF, 0x04, 0x00, 0xFF
};
static const uint8_t joypad1Offset[16] = {
	0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x00, 0xFF, 0xFF, 0xFF, 0x04, 0xFF, 0x08, 0x0C, 0xFF
};
```

Two facts worth stating plainly, because both bear directly on §5:

1. **The tables are already 16 wide.** Twelve of the sixteen entries are `0xFF` — "this
   nibble is not a socket-0 row code, return idle". Those twelve entries are *exactly*
   the twelve Team Tap socket-1/2/3 row codes (§3.2). The socket-0 entries — nibbles
   `$E,$D,$B,$7` for port 1 — match TR10's table exactly, which is an independent check
   that the reconstruction in §3.2 is right.
2. **`joypad1Offset` already encodes TR10's port-2 bit reversal.** TR10: *"the codes used
   for port 2 are a mirror image of the codes for port 1 (the bit order is reversed)"*.
   Indexing by the raw high nibble, `[7]→row 0, [B]→row 1, [D]→row 2, [E]→row 3` is the
   bit-reverse of port 1's assignment, and it is what the table holds.

**Consequence for detection (verified by reading the code path, not by running it):** a
title probing for a Team Tap writes the socket-3 row-1 code and tests `B0`/`B2` in
JOYBUTS (§3.4). With that row code, `joypad0Offset[$A]` and `joypad1Offset[$5]` are both
`0xFF`, so `JoystickReadWord(2)` leaves its base value `0xFF6F | (NTSC ? 0x10 : 0)`
untouched — bit 0 and bit 2 both read **1**. Per TR10 that means *"no 4-player adaptor"*.
**The core already answers the detection probe correctly for the no-adapter case.** The
feature work is to make that bit read 0 when a tap is attached, and to route the other
twelve row codes to three more pads per port.

---

## 3. Team Tap — established facts

### 3.1 The device (TR10, "4-Player Adaptor (Team Tap)"; TR04 p.16, identical)

> *"The fact that 16 rows of data can be addressed allows a four controller adaptor to be
> connected to each console controller port (for a total of 8 controllers using two
> adaptors). […] It has four controller sockets (DB15 females, the same as on the
> console) for controllers to be connected, and a short cable with a DB15 male connector
> which plugs into the console."*

And the electrical description, which is the load-bearing sentence for §4:

> *"The controller sockets on the adaptor have the 6 inputs wire OR'd together. The four
> output lines are an active low, 4 to 16 de-multiplexed version of the 4 console
> outputs."*

So the adapter demultiplexes only the four **row-select outputs**. The six **inputs**
(`B0`, `B1`, `J8`–`J11` on port 1; `B2`, `B3`, `J12`–`J15` on port 2) are shared across
all four sockets. That is what makes the whole scheme work with the existing two
registers: nothing new is added to the console side at all.

### 3.2 The row-code table (TR10 p.18; TR04 p.17 — identical)

This table was read as a **rendered page image** from both documents, not transcribed
from `pdftotext` column positions, because it is the one artifact an implementer will
transcribe directly into code.

Row code = the 4-bit value written to the JOYSTICK register's output nibble
(bits 3-0 = `J3 J2 J1 J0` for port 1; bits 7-4 = `J7 J6 J5 J4` for port 2, and TR10's
table lists port 2's bits in `J4 J5 J6 J7` order — i.e. the register nibble bit-reversed,
§2 fact 2).

| Code (`J3 J2 J1 J0`) | Hex | Socket | Row |
|---|---|---|---|
| `0000` | `$0` | 1 | 0 |
| `0001` | `$1` | 1 | 1 |
| `0010` | `$2` | 1 | 2 |
| `0011` | `$3` | 1 | 3 |
| `0100` | `$4` | 2 | 0 |
| `0101` | `$5` | 2 | 1 |
| `0110` | `$6` | 2 | 2 |
| `0111` | `$7` | **0** | 3 |
| `1000` | `$8` | 2 | 3 |
| `1001` | `$9` | 3 | 0 |
| `1010` | `$A` | 3 | 1 |
| `1011` | `$B` | **0** | 2 |
| `1100` | `$C` | 3 | 2 |
| `1101` | `$D` | **0** | 1 |
| `1110` | `$E` | **0** | 0 |
| `1111` | `$F` | 3 | 3 |

Four independent checks on this reconstruction, all of which agree:

1. **The rendered TR10 page image** (v10.0 p.18) read directly.
2. **The rendered TR04 page image** (Atari original, 26 April 1995, p.17) read directly —
   same table, same assignment.
3. **TR10's own prose example**: *"when your program outputs the code %0101 that says it
   wants to read Row 1 of the controller connected to socket 2, the 4-player adaptor will
   convert the code to %1101"* — `%0101` = `$5` = socket 2 row 1 in the table above. ✔
4. **The shipping `joypad0Offset[]`** (§2): its four non-`0xFF` entries are exactly
   `$7,$B,$D,$E` = socket 0. ✔

Note the socket-0 codes are *scattered* through the table rather than contiguous. That is
not a transcription artifact — it is the whole point of the design: socket 0's codes are
the pre-existing single-controller codes, and the other twelve were fitted around them.

### 3.3 The adapter is transparent to the controllers (TR10; TR04 identical)

> *"Except for socket 0, the row codes shown in the table are not the row codes seen by
> the controllers themselves. In order to make itself as transparent as possible to the
> controllers themselves, the adaptor converts the row codes for sockets 1-3 so that
> those controllers will only see socket 0 row codes."*

This is the single most important fact for emulation cost. **A pad in socket 2 is a
completely ordinary pad.** It has no idea it is behind an adapter. So the emulation is
purely a decode change on the console side: map the row code to `(socket, row)`, then
index a per-socket copy of the existing 21-button array. No new device behaviour, no new
register surface, no per-socket state machine.

### 3.4 Detection (TR10; TR04 p.18, same wording)

> *"To detect the presence of a 4-player adaptor, program should inquire the status of Row
> 1 of controller socket #3. If a 4-Player adaptor is present, the B0/B2 bit will be clear
> (0). Otherwise it will be set (1)."*

Socket 3, row 1 = code `%1010` = `$A` (port 1) / register high nibble `$5` (port 2), from
§3.2. `B0` is JOYBUTS bit 0 (port 1), `B2` is bit 2 (port 2) — TR10's JOYBUTS layout,
`rrdv3210`, *"3-2 Button Inputs B3 & B2 (Port 2), 1-0 Button Inputs B1 & B0 (Port 1)"*.

TR10's pseudocode then scans all four sockets for controller types:

```
For PORT = 1 to 2
     if PORT:SOCKET3:C1 = 0 then { 4-player adaptor found }
           for SOCKET = 0 to 3
                 PORT:SOCKET:CONTROLLERTYPE = PORT:SOCKET:C2/C3
                 …
```

**Why that bit is clear — the mechanism, from the TR10 appendix** ("Standard Jaguar
Controller Supplemental (Schematic Diagram)"):

> *"Diodes D21, D22 & D23 (C1, C2 and C3 in the controller matrixes respectively) are not
> normally fitted to a standard Joypad controller […] **D21 is used to identify the
> 4-Player adaptor (fitted only to Socket 3 of a Team Tap)**"*

So the detect bit is not adapter logic at all — it is one diode soldered into socket 3's
harness, sitting at the `C1` position of the standard controller matrix, which is row 1's
`B0` bit. TR10's standard-matrix table (§6.1) confirms `C1` lives on row 1. This is
mechanism-level corroboration of the detection rule from a second, independent part of
the same document.

**Caveat TR10 attaches, worth reproducing in the implementation:**

> *"be aware that without a 4-player adaptor, reading sockets 1-3 of a port may return an
> 'echo' of the standard Joypad controller at socket 0. To avoid reading incorrect data,
> unless your program has detected that a 4-Player adaptor is connected, it should not try
> to read from sockets 1-3."*

The core today returns idle (all-released) for sockets 1-3, not an echo. See §7 Q2.

### 3.5 Primary confirmation from a real ROM (TESTER)

`Joypad-TeamTap Tester by Matthias Domin (2000) (PD).jag` contains 35 instances of
`move.w #imm,$00F14000` with literal immediates. Classified against §3.2's table, they
cover **every one of the 16 row codes, for each port independently**: the port-1 sweep
writes `$81F0`…`$81FF` (port-1 nibble swept `$0`–`$F`, port-2 nibble parked at `$F`), and
the port-2 sweep writes `$810F`…`$81FF` (mirror image). Holding the idle port's nibble at
all-ones rather than mirroring it is exactly the non-palindromic technique TR10's own
appendix recommends, so that a bank-switching controller on the other port is not
advanced by a read it was not the target of.

The two duplicated, *early* codes are the detection probe. At file offset `0x272`:

```
33FC 81FA 00F14000    move.w  #$81FA,$F14000   ; port 1 socket 3 row 1 ($A); port 2 parked ($F)
3039 00F14002         move.w  $F14002,d0       ; read JOYBUTS
0800 0000             btst    #0,d0            ; test B0  (port 1)
6632                  bne.s   ...              ; set  -> no adaptor on port 1
0039 0001 000068AA    ori.b   #$01,$68AA       ; clear -> record "tap on port 1"
```

and at `0x2F2` the port-2 twin, `#$815F` (port-2 nibble `$5`, port 1 parked) testing
`btst #2` — `B2`.

That is TR10's documented detection sequence, implemented literally, in a program written
by someone who owned the hardware. It independently confirms both the socket-3-row-1 code
and the polarity of the detect bit. It is also a ready-made test vector: two of the three
dumps already reach `GAME_CODE` in both boot modes per
[`docs/cart-boot-matrix.md`](cart-boot-matrix.md).

### 3.6 Where TR04 (1995) and TR10 (2010) disagree — advanced-controller socket

This is the one substantive difference found between the two revisions, and it does not
affect standard pads:

- **TR04 (Atari, 1995):** *"Advanced controllers normally respond to row codes for socket
  1 instead of the codes for socket 0 because they have a pass-through connector for a
  standard joypad controller, which sees socket 0 codes […] the 4-Player adapter provides
  a +5v DC signal on pin 8 of each socket […] Advanced controllers are expected to detect
  this signal when present, disable their pass-through connector, and then respond as
  socket 0 instead of socket 1."*
- **TR10 (2010):** *"This has now changed […] advanced controllers are no longer required
  to check for the +5V DC signal supplied on pin 8 […] however they may still do so if
  necessary."* TR10's summary table states advanced controllers respond to socket 0 for
  reads and socket 2 for mode selection when plugged directly into a port.

Both revisions agree on the pin-8 mechanism and on the Team Tap's own behaviour; they
disagree on what an *advanced* controller does by default. Since #513 is about standard
pads behind the adapter, this only matters if the implementation ever wants to place a
non-pad device (mouse, rotary, analog) in a Team Tap socket — which §5.4 recommends
against for v1. Recorded as §7 Q3.

TR10 adds one more constraint on that combination:

> *"Because the 4 Player adaptor converts socket1-3 row codes to socket 0 row codes only a
> controller read will be possible when Advance controllers are connected to a 4 Player
> adaptor, software control of advanced features like rumble motors, force feedback and
> analogue/digital mode will not be possible."*

---

## 4. The light gun / Team Tap interaction — `lightgun-design.md` open question 3, RESOLVED

[`docs/lightgun-design.md`](lightgun-design.md) §4.2 lists "No Team Tap support" as a
non-goal and its open-questions table row 3 says:

> *"Plausible from the LP pin's direct TOM wiring (a `Team Tap` adapter multiplexes the
> row/column matrix; `LP` is a dedicated pin outside that mux), but this document did not
> find an explicit TR10 sentence confirming it — the claim in the issue's second comment
> should be re-verified against TR10's Team Tap section before it becomes a code comment
> asserting it as fact."*

**The sentence exists, and the answer is the opposite of "immune".** TR10, Team Tap
section (§3.1 above):

> *"The controller sockets on the adaptor have the 6 inputs wire OR'd together. The four
> output lines are an active low, 4 to 16 de-multiplexed version of the 4 console
> outputs."*

Pin 6 of port 1 is `B0 / LP` — TR10's pinout, *"Button input / Light Gun on Port 1"* —
and TR8's DB15 table names the same pin `B0/LP0`. Pin 6 is one of the six inputs. So:

- **`LP` is not blocked by the adapter.** It is not demultiplexed and not gated by row
  select; the adapter passes the six input lines straight through, wired together. A gun
  in any socket can still pulse `LP` and latch `LPH`/`LPV`.
- **`LP` is not isolated either.** Because the line is shared across all four sockets, a
  gun's photodiode pulse appears on the same wire that carries `B0` for *whichever socket
  the console is currently addressing*. The gun's pulse is aim-and-beam driven, not
  row-driven, so it is asynchronous to the row scan.

The first bullet is a direct reading of TR10. **The second is an inference** from TR10's
wire-OR sentence plus TR10/TR8's pin-6 identity, not a sentence TR10 states — no source
consulted here discusses a gun sharing a Team Tap with pads. Flagged as §7 Q4.

**Do not over-read "not isolated" into "the gun fabricates button presses."** The two uses
of pin 6 have opposite polarities: the latch is edge/level-triggered *high* (TR10: *"A TTL
rising edge on the LP signal"*; TR8: *"a high level on these inputs transfers the current
horizontal and vertical counts"*), while `B0`-as-button is **active low** on a pulled-up
wire-OR bus where a device can only pull the line *down*. So a gun asserting `LP` is not
obviously the same event as a socket asserting a button, and the plausible interference
runs at least as much the other way — a real button press on any socket holding the shared
line low could mask or mistime a gun pulse. Which of those actually happens is not
determinable from any source here; that is the point of Q4.

Practical upshot for a future implementation: `lightgun-design.md`'s "the gun is additive
to the pad and changes nothing about the matrix decode" (its §3.1) remains true for a gun
plugged **directly** into port 1. Behind a Team Tap it stops being obviously true, because
the `B0` the gun shares is now four sockets' `B0`. **Do not write a code comment claiming
the gun is immune to the Team Tap.** The honest statement is: the manual says the inputs
are shared, and nobody has documented what a gun plus pads on one adapter actually does.

---

## 5. Team Tap — what implementation looks like (#513)

Not a design document — #513 will want its own. This is the sizing evidence.

### 5.1 It is a decode change, and the decode is already 16-wide

Per §3.3 the controllers are ordinary pads, so the change is: replace the two
`joypadNOffset[16]` tables' `0xFF` sentinels with real `(socket, row)` destinations, and
widen `joypad0Buttons[21]`/`joypad1Buttons[21]` to a per-socket array. Every other
mechanism — the active-low mask loops, `InputDevClock`/`InputDevRowSelect`'s row
argument, the `JOYBUTS` row table — already takes a row index and needs a socket index
alongside it.

The detection bit is one extra term in `JoystickReadWord(2)`: when a tap is attached to
that port and the decoded address is socket 3 row 1, clear `B0` (port 1) / `B2` (port 2).
§2 establishes the no-tap case already reads correctly.

### 5.2 The things that will actually cost time

- **Savestate.** `JoystickStateSave`/`Load` write `joypad0Buttons`/`joypad1Buttons`
  verbatim; widening them changes the blob and needs a `STATE_VERSION` bump. This repo
  has been bitten twice by input/enhancement state living outside the blob (#400, #479),
  so this is a must-do, not a nice-to-have.
- **Port count.** `SET_CONTROLLER_INFO` currently advertises two ports; TR10 allows two
  taps for eight pads total (§3.1). Whether to expose 4 or 8 is a product decision, not a
  hardware one.
- **The identity guardrail.** `test/tools/joymatrix_identity.c` must stay byte-identical
  with no tap selected — that is the #446 pattern the issue asks for, and §2's tables give
  a clean way to satisfy it (the socket-0 entries are untouched).
- **c89.** `src/jerry/joystick.c` is not in `scripts/c89-lint.sh`'s exempt list.

### 5.3 The corpus survey, and what it does not show

27 ROMs were scanned for `move.w #imm,$00F14000` with bit 15 set (JOY outputs enabled —
without it the low byte is not a row code). Results, honestly labelled:

| Result | ROMs |
|---|---|
| **Addresses non-socket-0 row codes** | Domin tester (all 12 non-socket-0 codes, both ports); Club Drive (1994) (port 1, socket 1 rows 1-3) |
| **Socket 0 only** | Doom, Wolfenstein 3D, Tempest 2000, Skyhammer, Fight For Your Life, Troy Aikman NFL, Brutal Sports Football, International Sensible Soccer, Ultra Vortek, Super Burnout, Power Drive Rally, Val d'Isère, Jaguar boot ROM |
| **UNRESOLVED** (composes the row code in a register — the scan cannot see it) | NBA Jam TE, White Men Can't Jump, Iron Soldier, Iron Soldier 2, Missile Command 3D, Ruiner Pinball, Atari Karts, Defender 2000, Fever Pitch Soccer, Zoop!, Alien vs Predator, Checkered Flag |

**This survey establishes no negatives.** The two titles most often named as Team Tap
titles — NBA Jam TE and White Men Can't Jump — both land in UNRESOLVED, so this pass says
nothing about whether they use it. A static immediate scan finds only the subset of
programs that happen to write literal constants; roughly half the corpus does not.
Establishing the actual Team Tap title list needs a *dynamic* check (run the ROM, log the
row codes the core is asked for), which is cheap once #513 exists and pointless before.

Club Drive's socket-1 sweep is interesting but not a Team Tap signal: under TR04's 1995
rules (§3.6) socket-1 row codes are how you read an *advanced* controller plugged
directly into a port, which is a plausible reading for a 1994 title. Not investigated
further — out of scope for this spike, and recorded as §7 Q7.

### 5.4 Recommended non-goals for a v1

1. **Do not put non-pad devices in Team Tap sockets.** TR10 forbids the useful half of it
   (§3.6: no mode control, reads only), and the mouse/rotary/analog/lightgun device
   framework's port model would need reworking for a socket model. Standard pads only.
2. **Do not claim a title list in release notes** until §5.3's dynamic check has been run.
3. **Do not assert the light gun is unaffected** (§4).

### 5.5 Size

**M**, consistent with the issue's own label. Reference points from this repo: the mouse
(#449, +2372/-69) and rotary (#451, +1602/-102) each had to invent device semantics; the
Team Tap has none to invent (§3.3). Against that, it touches the savestate blob and the
advertised port count, which neither of those did. Roughly **2-4 focused days** including
the guardrail work and a harness path that can drive pads 3 and 4.

---

## 6. Pro Controller — what the manual says (nothing), and what follows

### 6.1 The standard matrix has exactly 21 data bits, and all 21 are allocated

TR10 "Standard Jaguar Controller Matrix" (p.17, read as a rendered image):

| Row | `B0`/`B2` | `B1`/`B3` | `J8`/`J12` | `J9`/`J13` | `J10`/`J14` | `J11`/`J15` |
|---|---|---|---|---|---|---|
| Row 3 | `C3` | Option | `#` | `9` | `6` | `3` |
| Row 2 | `C2` (see §7 Q1) | `C` | `0` | `8` | `5` | `2` |
| Row 1 | `C1` | `B` | `*` | `7` | `4` | `1` |
| Row 0 | Pause | `A` | Up | Down | Left | Right |

*"Reading a zero means the appropriate button is depressed."*

TR10 states the accounting itself: *"each input supports up to 24 bits of data (4 rows of
6 bits). Three bits are reserved for the controller type identifier code, leaving 21 bits
for data."* And the standard pad uses **exactly 21**: 4 directions + A/B/C + Option +
Pause + 12 keypad keys. **There is no free slot in the standard matrix.**

### 6.2 The controller taxonomy is closed, and the Pro Controller is not in it

TR10 defines the basic type by the `C2`/`C3` diode bits:

| `C2` | `C3` | Controller Type |
|---|---|---|
| 0 | 0 | Reserved |
| 0 | 1 | Bank Switching (analogue joystick, head-mounted tracker, etc.) |
| 1 | 0 | "Tempest" Rotary |
| 1 | 1 | "Standard" Jaguar Joypad (**or nothing connected**) |

and the bank-switching sub-types by the last bank's row bits: Head-mounted Tracker
(`0111`), Keyboard/Mouse (`1101`), 6D Controller (`1110`), Analogue Joystick or Driving
Controller (`1111`). Every other code in that 16-row table is **Reserved**.

Searches run for this document, all returning nothing:

- TR10 and TR8 full text (`pro controller`, `procontroller`, `pro pad`, `propad`,
  `six button`, `6 button`, `shoulder`, `trigger`, `jagpad`): **no hits.**
- TR10's section list, enumerated in full: the controller chapter is Signals and Pin
  outs · Register Addressing · Device Addressing · Reading a Jaguar Controller ·
  Identifying Controller Types · Standard Jaguar Controller Matrix · Rotary "Tempest"
  Controller · 4-Player Adaptor (Team Tap) · Bank Switching Controllers · Detecting the
  4-Player adaptor · Advanced Controllers · 6D Controller · Head Mounted Tracker ·
  Analogue Joystick and "Driving" Controllers · Advanced Controller Mode Control ·
  Keyboard/Mouse Interface · Reading Bank Switching Controllers. **No Pro Controller
  section.**
- IDX (the master table of contents for the whole documentation set, Nov 1994), OCR'd:
  the controller subsections it lists are Signals And Pinouts · Register Addressing ·
  Device Addressing · Reading A Jaguar Controller · Standard Jaguar Controller Matrix ·
  **4 Player Adapter** · 6D Controller · Head-Mounted Trackers · Rotary "Tempest"
  Controller · Analog "Stick" and "Driving" Controllers · Reading Bank Switching
  Controllers. **No Pro Controller entry, and no controller subsection in a volume this
  spike did not read.**
- LIB (Libraries, 65 pp OCR'd) and Sample Programs (13 pp OCR'd): no joystick/controller
  library at all — the only controller content is a "Joypad Reading Example" for the
  standard pad. **No Pro Controller.**

The dating is consistent with the negative rather than surprising: IDX is Nov 1994, TR04
is 26 April 1995, and TR10 — although published in 2010 — is an amendment of the 1995
Atari text and its controller chapter tracks that taxonomy.

**Conclusion: the manual set on this machine does not document the Pro Controller.**
Per the rule this spike was written under, that is the answer, and it is what makes #514
`blocked` rather than ready.

### 6.3 The dichotomy this forces

Combining §6.1 and §6.2 — both TR10-grounded — a Pro Controller that works with
unmodified retail software must be one of exactly two things:

**(a) The extra buttons alias onto existing matrix slots.** The device reports `C2/C3 =
%11` (standard pad, which is also what "nothing connected" reports, so no diode is
needed), and its extra buttons close the same matrix intersections as buttons the
standard pad already has — most plausibly keypad digits, since a title that never shows a
keypad UI has 12 otherwise-unused inputs sitting there. **If this is what the hardware
does, the core already emulates the Pro Controller completely**, and #514 is a
frontend-mapping and input-descriptor question — "let the user bind RetroPad `X`/`Y`/`L`/
`R` to the Jaguar keypad slots this title reads" — not a decode change at all.

**This branch also answers #514's "does a standard-pad-only title misread one?" — yes, and
that is the argument for opt-in.** If the extra buttons close keypad intersections, then
on real hardware a title that reads keypad digits sees a *genuine* digit press when the
user hits `X`/`Y`/`Z` — there is no way for the console to tell the two apart (§6.4). A
title that predates the pad and uses keypad digits for weapon select, level codes or menu
shortcuts therefore reacts to buttons the player thinks are unbound. This is the same
shape as the mouse's documented "phantom digit" and "LMB reads as four buttons" quirks in
the input-devices design doc: authentic hardware behaviour that is nonetheless surprising,
and the reason any eventual mapping should be a per-port opt-in rather than a default. It
is a consequence of branch (a), not an independently sourced fact — if branch (b) turns
out to be true it does not apply.

**(b) It is a bank-switching device.** Then it needs a `C2/C3 = %01` ID plus a last-bank
sub-type code, and TR10 assigns no such code — every unassigned slot in that table is
explicitly `Reserved`. Emulating it would mean inventing an identifier Atari never
published, which is exactly the class of guess this repo prohibits (compare
`lightgun-design.md` §3.3, which declined to invent a gun ID for the same reason).

Branch (b) is the one this document can argue *against* from TR10, without being able to
prove branch (a). And even granting (a), the manual gives no way to learn **which** slots.
Those are two separate claims and only the first is currently supportable.

### 6.4 What ROM disassembly can and cannot settle

It can settle: whether a title claimed to support the Pro Controller ever addresses a
non-socket-0 row code, or runs a bank-switch read sequence (repeated row-0…row-3 cycles
watching `B0` for the bank-0 flag). A clean result across such titles supports branch (a).
The §5.3 survey is weak evidence in that direction — Skyhammer is socket-0-only; Missile
Command 3D, Iron Soldier 2 and Ruiner Pinball are UNRESOLVED — but "weak" is the operative
word and the scan was not designed for this question.

It probably cannot settle: **which** matrix slots the extra buttons occupy. A title
reading its Pro Controller's `X` button reads a keypad-slot bit; nothing in the code
distinguishes "the user pressed keypad 7" from "the user pressed `X` on a Pro Controller",
because on real hardware those are electrically the same event. Recovering the mapping
would need either an Atari document nobody in this repo has, or a hardware capture from
someone with the pad. §7 Q8.

### 6.5 Recommendation for #514

Move it to `blocked`, with the blocker stated as *"the Pro Controller is absent from the
entire manual set; the TR10 taxonomy is closed and the standard matrix is fully allocated,
so the device is either already emulated (aliased) or needs an identifier Atari never
published"*. Then, if someone wants to unblock it, the two cheap next steps in order are:

1. **Reframe as branch (a) and test the reframing.** Pick a title that documents Pro
   Controller support on its own packaging or in-game controls screen, boot it, and check
   whether its controls screen reacts to keypad inputs the core can already inject via
   `--press 0`…`--press 6`. If it does, the answer is "already emulated" and the remaining
   work is input descriptors and a remap default — an **S**, not an **M**.
2. **Only if that fails**, treat it as needing a hardware capture, and keep it blocked.

Sizing #514 now would be sizing a guess.

---

## 7. Open questions

| # | Question | Status |
|---|---|---|
| 1 | TR10's "Standard Jaguar Controller Matrix" (p.17) shows Row 2 → `C1`, but its own port-1 table (p.15) and its rotary matrix on the same page both show Row 2 → `C2`. | **Typo in TR10, near-certainly.** Two tables against one, and `C1`/`C2`/`C3` on rows 1/2/3 is the only assignment consistent with the appendix's D21/D22/D23 note (§3.4). **Does not affect #513**: `C1` is on row 1 in every version of the table, so Team Tap detection is unambiguous. Only `C2`/`C3` type ID would be affected — and for a pads-only v1 (§5.4 non-goal 1) every socket reports `C2 = C3 = 1` whichever row carries which, so the swap is moot until that non-goal is relaxed. |
| 2 | TR10 says reading sockets 1-3 with no adapter *"may return an 'echo' of the standard Joypad controller at socket 0"*. This core returns idle (all-released) instead. | Divergence from documented hardware behaviour, present in the shipping core today (§2). "May" is doing work in that sentence — it reads as an analogue artifact, not a specified behaviour. Not worth reproducing speculatively; worth a code comment so nobody "fixes" it into an echo without evidence. |
| 3 | TR04 (1995) and TR10 (2010) disagree on whether an *advanced* controller defaults to socket-1 or socket-0 row codes (§3.6). | Unresolved between the two revisions. Irrelevant to a pads-only Team Tap v1; becomes live only if a mouse/rotary/analog device is ever placed in a tap socket, which §5.4 recommends against. |
| 4 | What does a light gun behind a Team Tap actually do, given the six inputs are wire-ORed (§4)? | **`LP` reaches TOM** — direct from TR10. **`LP` is not isolated from other sockets' `B0`** — an inference from TR10's wire-OR sentence plus the pin-6 identity, not a TR10 statement. No source consulted describes a gun sharing an adapter with pads. Do not assert immunity in code. |
| 5 | 14 of the 20 manual PDFs are image-only scans that were not OCR'd (§1). | Bounded, not eliminated. IDX — the master TOC across the whole set — lists no controller subsection in any skipped volume, and no Pro Controller entry anywhere. If #514 is ever revisited, OCR'ing `03 - Software Reference.pdf` (103 pp) is the largest remaining stone, though TR8 already carries that volume's JERRY joystick material in text form. |
| 6 | `docs/jtrm-jerry.md` §"Joystick Interface" cites `src/jerry/joystick.c` as its source, not the manual — a distilled "JTRM" doc sourced from the code it is meant to validate. | Noted, not fixed here (this spike touched no other doc). CLAUDE.md's rule is that the distilled docs supersede source comments; a distilled doc *derived from* the source inverts that. Worth a follow-up that rewrites that section from TR10 §"Reading a Jaguar Controller" + the §3.2 table. |
| 7 | Why does Club Drive (1994) sweep socket-1 row codes on port 1 (§5.3)? | Unresolved; plausibly an advanced-controller read under TR04's 1995 rules. Not investigated — out of scope. |
| 8 | Which matrix slots do the Pro Controller's extra buttons occupy, if branch (a) in §6.3 is correct? | **Unanswerable from any source available to this spike**, and probably unanswerable from ROM disassembly (§6.4), because on real hardware "`X` pressed" and "the aliased keypad key pressed" are the same electrical event. Needs an Atari document nobody here has, or a hardware capture. |
| 9 | Which retail titles actually use the Team Tap? | Not established. The static scan (§5.3) leaves the two most-cited candidates UNRESOLVED. Answer this dynamically after #513 exists (log the row codes a running title requests), not from a title list. |

---

## 8. Recommendation

- **#513 (Team Tap): unblock and schedule.** TR10 documents the device completely, TR04
  independently corroborates the load-bearing table, the appendix explains the detect bit's
  mechanism, and a PD ROM in the corpus implements the documented detection byte-for-byte
  and provides a ready test vector. Size **M** (§5.5). Take §5.4's non-goals into the
  design doc.
- **#514 (Pro Controller): mark `blocked`.** The device is absent from the whole manual
  set (§6.2). The TR10-grounded dichotomy in §6.3 argues against the bank-switching branch
  but cannot prove the aliasing branch, and even granting it, the slot mapping is not
  recoverable from anything available here. §6.5's step 1 is a cheap way to find out
  whether the honest answer is "already emulated, needs a remap" — do that before sizing
  anything.
- **Update [`docs/lightgun-design.md`](lightgun-design.md) open question 3** with §4's
  finding when #438 or #513 is next touched. The current wording says "plausible, not
  TR10-confirmed"; TR10 does address it, and the answer leans the other way.
