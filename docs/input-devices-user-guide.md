# Jaguar Input Devices — User Guide

Virtual Jaguar emulates the **ST/Amiga mouse adapter**, a real port-2
peripheral sold by AtariAge and The Brewing Academy (and, in a PS/2 flavour,
by AtariAge as well). A handful of Jaguar titles read one instead of a joypad.

This guide covers what to turn on, which wiring to pick, and the one
limitation you should know about before filing a bug.

**The mouse is opt-in.** With the options left alone the core behaves exactly
as it did before the feature existed — `$F14000` is bit-identical, proven by
`test/tools/joymatrix_identity`. Nothing here can affect a title you play
with a pad.

## Core options

Twenty options together cover device selection and per-axis tuning for both
ports. The mouse rows live under **Port 2**; the rotary and analog rows are
shared by **both** ports, exactly like *Rotary Sensitivity* always has been —
a rotary plugged into either port draws on the same five rotary options, and
an analog or driving controller on the same six analog options.

| Option | Values | Default |
|---|---|---|
| *Port 1 > Controller Type* (`virtualjaguar_p1_device`) | Auto (per-title default), Standard Joypad, Pro Controller (6-button), Team Tap (4-player adaptor), Rotary (Tempest), Light Gun, Analog Joystick (bank-switching), Driving Controller (bank-switching), Analog Stick (paddle ADC), 6D Controller (bank-switching) | Auto |
| *Port 2 > Controller Type* (`virtualjaguar_p2_device`) | Auto (per-title default), Standard Joypad, Pro Controller (6-button), Team Tap (4-player adaptor), Atari ST / PS2 Mouse, Amiga Mouse (ST adapter), Amiga Mouse (Amiga adapter), Rotary (Tempest), Analog Joystick (bank-switching), Driving Controller (bank-switching), Analog Stick (paddle ADC), 6D Controller (bank-switching) | Auto |
| *Port 2 > Mouse Sensitivity* (`virtualjaguar_mouse_sensitivity`) | 25% – 400% | 100% |
| *Port 2 > Mouse Dead Zone (X)* (`virtualjaguar_mouse_deadzone_x`) | Off, 1 – 8 units | Off |
| *Port 2 > Mouse Dead Zone (Y)* (`virtualjaguar_mouse_deadzone_y`) | Off, 1 – 8 units | Off |
| *Port 2 > Mouse Offset (X)* (`virtualjaguar_mouse_offset_x`) | -4 – -1, Off, +1 – +4 | Off |
| *Port 2 > Mouse Offset (Y)* (`virtualjaguar_mouse_offset_y`) | -4 – -1, Off, +1 – +4 | Off |
| *Port 2 > Mouse Response Curve (X)* (`virtualjaguar_mouse_exponent_x`) | Linear (1.00) – 3.00 | Linear (1.00) |
| *Port 2 > Mouse Response Curve (Y)* (`virtualjaguar_mouse_exponent_y`) | Linear (1.00) – 3.00 | Linear (1.00) |
| *Rotary Sensitivity* (`virtualjaguar_rotary_sensitivity`) | 25% – 400% | 100% |
| *Rotary Reports Controller Type* (`virtualjaguar_rotary_id`) | Standard Joypad (no diode), Tempest Rotary (diode fitted) | Standard Joypad |
| *Rotary Dead Zone* (`virtualjaguar_rotary_deadzone`) | Off, 1 – 8 units | Off |
| *Rotary Offset* (`virtualjaguar_rotary_offset`) | -4 – -1, Off, +1 – +4 | Off |
| *Rotary Response Curve* (`virtualjaguar_rotary_exponent`) | Linear (1.00) – 3.00 | Linear (1.00) |
| *Analog Dead Zone (X)* (`virtualjaguar_analog_deadzone_x`) | Off, 1 – 8 ADC counts | Off |
| *Analog Dead Zone (Y)* (`virtualjaguar_analog_deadzone_y`) | Off, 1 – 8 ADC counts | Off |
| *Analog Offset (X)* (`virtualjaguar_analog_offset_x`) | -4 – -1, Off, +1 – +4 | Off |
| *Analog Offset (Y)* (`virtualjaguar_analog_offset_y`) | -4 – -1, Off, +1 – +4 | Off |
| *Analog Response Curve (X)* (`virtualjaguar_analog_exponent_x`) | Linear (1.00) – 3.00 | Linear (1.00) |
| *Analog Response Curve (Y)* (`virtualjaguar_analog_exponent_y`) | Linear (1.00) – 3.00 | Linear (1.00) |

The seven mouse rows (*Mouse Sensitivity* through *Mouse Response Curve (Y)*)
only appear once a mouse is actually attached to port 2. The five rotary rows
only appear once a rotary is attached to port 1 or port 2, and the six analog
rows once an analog or driving controller is attached to either port. Every
group is gated on the live device, not the option string, so a device your frontend
assigned directly (see below) reveals them too, exactly as a core-option
selection would.

The dead zone, offset and response-curve rows are per-axis tuning (#439,
landed in #474): a noise gate, a centring correction and a low-speed response
curve, in that order, applied identically to mouse and rotary motion by the
shared layer in `src/jerry/axistune.c`. Every default is the exact identity,
so a user who never opens this menu gets the pre-#439 path unchanged — the
same guarantee the mouse feature itself makes (see above).

**Auto currently means Standard Joypad for every title.** See
[No per-title auto-select](#no-per-title-auto-select-and-why) below — that is
a deliberate result, not an unfinished switch.

Your frontend can also select the device directly (RetroArch: *Controls →
Port 1/2 → Device Type*), which offers the same devices as each port's
*Controller Type* option: eight on port 1 (Standard Joypad, Team Tap, Rotary,
Analog Joystick, Driving Controller, Analog Stick (paddle ADC), 6D Controller,
Light Gun), ten on port 2 (Standard Joypad, Team Tap, the three mice, Rotary,
Analog Joystick, Driving Controller, Analog Stick (paddle ADC), 6D Controller)
— everything except *Auto*, which is not a real device. A device set that way
outranks the core option.
Setting the port back to *Joypad* or *None* releases that claim rather than
forcing a pad — the core option decides again, immediately.

**Pro Controller (#514) is the one exception: core option only.** It does not
appear in your frontend's *Controls → Device Type* list, because it is not a
distinct RetroPad-shaped device the way the others are — see
[Pro Controller](#pro-controller-514) below for why. Select it from the
*Controller Type* core option on the port you want it on.

### Once a mouse is live, the port-2 RetroPad is disconnected

That is the hardware being faithful, not a bug. The adapter occupies the port;
there is no pad plugged into it. Port 1 is untouched — a mouse on port 2
cannot perturb port 1's bits, and there is a test asserting exactly that.

**The pad is not dropped until the mouse has actually moved.** Selecting a
mouse must never leave the port with no working input, and whether your
frontend routes host mouse deltas to port 2 is not something the core can
know in advance (see [the limitation below](#known-limitation-frontend-mouse-routing-is-unverified)).
So port 2 keeps its RetroPad until the frontend reports a non-zero delta or a
mouse button; from that moment the pad stays suppressed for the session, or
until you change the device type. Consequence worth knowing: with a mouse
selected but not routed, port 2 behaves exactly like a joypad.

## Which wiring do I pick?

Three real hardware combinations exist, and **they are not compatible with
each other** — the four direction lines carry different axes in each. Picking
the wrong one gives you motion coupled diagonally between the axes, or one
axis inverted, rather than a subtle difference.

| Option value | The hardware it models | Pick it when |
|---|---|---|
| **Atari ST / PS2 Mouse** (`mouse_st`) | An Atari ST mouse (or an ST-compatible trackball) in an ST-wired adapter — the AtariAge and Brewing Academy ST adapters. **The PS/2 adapter presents the same signals**, so use this for a PS/2 mouse too. | Default choice. This is what almost everyone owns. |
| **Amiga Mouse (ST adapter)** (`mouse_amiga`) | An **Amiga** mouse plugged into that same ST-wired adapter. The two mice differ in how their quadrature lines are pinned, so the axes land differently. | A game offers an in-game "Atari / Amiga" mouse-type selector and you set it to Amiga. That selector normally chooses between these first two rows. |
| **Amiga Mouse (Amiga adapter)** (`mouse_amiga_adapter`) | The rarer dedicated Amiga-wired adapter with an Amiga mouse. | You specifically have that adapter. If you are not sure, you do not. |

If motion comes out diagonal — moving the mouse left also moves the cursor up
— you have picked the wrong row above. Try the other two.

The pin-level evidence for all three is in
[`docs/jaguar-mouse-adapter-mapping.md`](jaguar-mouse-adapter-mapping.md)
§4d. Nothing in the table is guessed.

## Sensitivity, and why more is not always faster

The emulated adapter can only emit **one quadrature pulse per controller
poll**, because that is the constraint a real game's decoder imposes: a
two-state jump between consecutive polls is ambiguous and the decoder throws
it away. So the core rate-limits to one state per poll, always.

The practical consequence: raising *Mouse Sensitivity* past what the game's
poll rate can carry buys you **lag, not speed** — the extra motion queues up
and drains at the game's own pace (bounded, so it cannot drift for seconds
after your hand stops). If the pointer feels like it keeps gliding after you
stop, lower the sensitivity rather than raising it.

## Titles that read a mouse

Reported mouse-capable Jaguar titles:

- **Elansar** (CD) — has an in-game mouse-type selector, `*` = Atari,
  `#` = Amiga
- **Philia — The Sequel to Elansar** (CD)
- **Defender of the Crown**
- **Gridrunner** (Jeff Minter Classics)
- **Star Wars Arcade** (ST port)

This list comes from issue #429 and community reports; the core has not
verified each of them against a dump. If you have one of these and the mouse
works (or does not), that is useful — please say so on
[#429](https://github.com/libretro/virtualjaguar-libretro/issues/429).

**JagDoomEX does not read a mouse** in any build released so far, despite
being the title that motivated the feature. See below.

## No per-title auto-select, and why

The core has a per-title defaults database (`src/core/titledb.c`, issue #368)
that can turn options on for a specific game by CRC. **No title gets the mouse
selected automatically**, for two separate reasons:

**1. JagDoomEX does not read port 2.** Its CRCs are in the database already
(nine of them), so the row was easy to add — but the ROM was disassembled
first, and every build reads port 1 only. It writes only the port-1 row
selects and then masks away the four bits a mouse adapter drives, so the mouse
could not reach the game even if the option were on. Selecting a mouse for it
would only disconnect the port-2 pad in exchange for nothing. If a future
JagDoomEX release adds mouse support it will hash differently from every CRC
we know, so it would need a fresh row anyway.

**2. The per-title database is cartridge-only.** It is keyed on the CRC of
cartridge content and deliberately never hashes a disc image (a disc CRC would
match nothing it knows while risking a collision with some cartridge's
settings). So **CD titles can never auto-select a device** — which is the
standing answer for Elansar and Philia, the two mouse titles most likely to
prompt the question. Select the mouse by hand for those.

## Known limitation: frontend mouse routing is unverified

The Jaguar mouse adapter is a **port 2** device — that is where the hardware
plugs in, that is where every known title reads it, and so that is the only
port the core offers it on.

RetroArch has historically bound the host mouse to **port index 0**, and we
have not verified on which frontends and versions a mouse selected on port 2
actually delivers its deltas there. The core deliberately ships **no
speculative port-0 fallback**: silently reading port 1's mouse into a port-2
device would be a guess, and a wrong guess would be invisible until it
misbehaved.

If you select a mouse on port 2 and nothing responds to the mouse — while the
port-2 **pad still works normally** — that is the symptom of this limitation
rather than of the emulation. (The pad still working is the deliberate
fallback described above, not a second bug.) Please
report it with your frontend and version on
[#429](https://github.com/libretro/virtualjaguar-libretro/issues/429) — real
frontend data is exactly what is needed to settle it.

## Behaviour that looks wrong but is not

The real adapter is wired straight onto the port's input lines and **cannot
see the row select**. It presents the mouse's state identically in all four
row scans. We reproduce that, so three odd-looking behaviours are faithful:

- while the mouse moves, a title that scans all four rows sees **phantom
  keypad digits** (the direction lines are the same wires as `*`/7/4/1,
  2/5/8/0, 3/6/9/`#`);
- the **left button reads as A, B, C and Option** at once;
- holding the **right button** perturbs the controller-type field, so a title
  that re-probes what is plugged in while you hold it will misidentify the
  device.

All three happen on real hardware with a real adapter. They are commented as
such at the code sites so nobody "fixes" them later.

## Analog / driving controller (#437)

Atari specified — but **never released** — an analog controller: a
bank-switching matrix device with its own ADC chip, documented in the Jaguar
Technical Reference V10 ("Analogue Joystick and 'Driving' Controllers",
"Reading Bank Switching Controllers"). The core emulates that specification
faithfully on both ports, as two option values that are one wire protocol:

| Option value | Host mapping |
|---|---|
| *Analog Joystick (bank-switching)* | Left stick X/Y (stick forward = TR10 +Y). A/B/C on the pad's A/B/Y, D on X, hat on the d-pad. |
| *Driving Controller (bank-switching)* | Left stick X = steering; R2/L2 analog triggers = accelerator/brake (stick Y as fallback). D-pad up/down = gear shift. |

Facts to know before selecting one:

- **No released title reads this protocol.** The research for #437
  established that the controller never shipped and no commercial software
  polls it (BigPEmu's Checkered Flag "analog" support is a game *patch*, not
  this device). It exists here for homebrew and BigPEmu parity, and its
  verification is the synthetic register-level suite
  `test/tools/analog_decode_test.c` — there is no game to boot against.
- **The port stays a RetroPad until the stick moves** (the same liveness rule
  as the mouse). Consequence: a title that probes controller types once at
  boot will see a standard pad unless you deflect the stick first.
- Per-axis tuning (`virtualjaguar_analog_*`) goes through the same shared
  layer as the mouse and rotary, but with *absolute* semantics: units are ADC
  counts (127 = full deflection), the dead zone re-bases smoothly instead of
  gating, and the response curve is anchored at full deflection (so it costs
  no range and there is no sensitivity option).

Two neighbouring findings, recorded so they stay asked-and-answered:

- **The motherboard ADC is a different device and now has its own section**
  (below). It used to be recorded here as out of scope for want of a register
  spec; #505 settled the protocol from the software plus the converter's own
  datasheet, so it is emulated.
- **Head tracker (Jaguar VR):** a bank-switching type ID exists for it in
  TR10, but no released software uses it (Missile Command 3D was a
  prototype). Out of scope, per the epic.

## Analog stick via the paddle ADC (#505)

Set *Port 1/2 > Controller Type* to **Analog Stick (paddle ADC)**, or pick it
for that port in your frontend's Controls menu. Default is *Auto*, which means
a plain joypad — nothing changes until you choose it.

**This is not the controller above.** TR10: *"Early versions of the Jaguar
included an 8 bit ADC on the motherboard. This has been deleted — analogue
controllers now require their own ADC chip."* The bank-switching controller is
the replacement; this is the deleted part — an ADC0844 at U16 behind JERRY's
GPIO5 decode at `$F17C00-$F17FFF`, labelled "Paddle Interface" in TRM rev 8,
digitising the joystick connector's PAD0X/PAD0Y/PAD1X/PAD1Y pins. Two channels
per socket, four in all.

**It is the one analog interface a released game reads.** **BattleSphere** and
**BattleSphere Gold** sample all four channels from a JERRY Timer 1 handler and
consume the port-2 pair, in an *Analog Joystick Calibrator* screen and in GPU
code. Club Drive writes the channel select and never reads it back. A sweep of
822 cart and CD images found no other title touching the register.

Facts to know before selecting it:

- **The game needs its own setting too.** BattleSphere gates its analog support
  behind *Gameplay Options → 2nd Controller: Analog Stick*, and reads the
  motherboard ADC's port-2 channels — so select the paddle on **port 2** for
  that game. Selecting the device in the core is necessary, not sufficient.
- **Your pad keeps working.** The potentiometers are separate connector pins
  from the switches, so unlike the bank-switching controller this device does
  not take the port over: the RetroPad on a paddle port stays fully connected,
  and there is no "deflect the stick to wake it up" rule. A centred stick reads
  centre, which is what the calibrator screen asks you to align on.
- **Off means a retail console.** A production Jaguar has no converter fitted
  and reads `$FF` here; that is what the core reports whenever no paddle is
  selected, and it is exactly what v3.4.0 shipped. Verified inert: BattleSphere
  over 900 frames and Club Drive over 700 produce byte-identical frame-hash
  logs with and without this feature compiled in.
- **A socket without a paddle reads `$00`,** not `$FF` — "converter fitted,
  nothing plugged into that pot line". So a paddle on port 1 alone leaves
  BattleSphere's channels reading zero, as the hardware would.
- Per-axis tuning shares the `virtualjaguar_analog_*` options with the
  bank-switching controller — same absolute semantics, same units, so a dead
  zone means the same thing on both. Y is **not** inverted here (it is on the
  other device, per TR10): BattleSphere's calibrator uses the Y channel
  directly as a screen coordinate, which grows downward.

Verification is `test/tools/paddle_decode_test.c`, which drives the register
through BattleSphere's own ISR sequence.

## Light gun (port 1)

Set *Port 1 > Controller Type* (`virtualjaguar_p1_device`) to **Light Gun**,
or pick *Light Gun* for port 1 in your frontend's Controls menu. Default is
*Auto*, which means a plain joypad — nothing changes until you choose it.

**Port 1 only, and that is the hardware.** The Jaguar wires TOM's light-pen
pin to port 1 and port 1 alone, so the option does not exist on port 2.

Aim with whatever your frontend maps to a light gun (a mouse and a Wiimote
both work). The mapping is:

| Light gun input | Jaguar |
|---|---|
| Trigger | **B** |
| Aux A / Aux B | A / C |
| Start / Select | Option / Pause |

The trigger is **B** rather than a dedicated "fire" line because a Jaguar gun
is a modified controller: the light-pen pin carries the *beam* pulse that
tells the console where you are pointing, and the shot itself has to arrive as
an ordinary button. Balloons — the one confirmed light gun title — reads B.

**Pointing off-screen freezes the aim** instead of jumping to a screen edge.
That is what a real gun does: no light reaches it, so it sends no pulse and
the console keeps the last position it saw. The trigger still registers.

The one title known to use it is **Balloons** (Matthias Domin, 2003), a free
homebrew calibration-and-shooting demo. It opens with a two-target calibration
screen: point at each target and pull the trigger. Aiming near the bottom of
the screen quits the program — that is the demo's own behaviour, not a bug.

## Team Tap (4-player adaptor)

Set *Port 1 > Controller Type* or *Port 2 > Controller Type* to **Team Tap
(4-player adaptor)**, or pick *Team Tap* for that port in your frontend's
Controls menu. Default is *Auto*, which means a plain joypad — nothing
changes until you choose it, and there is no per-title auto-select.

The Team Tap is Atari's four-socket adapter: one plugs into a controller
port and gives it four controller sockets. The Jaguar's row-select lines
address sixteen rows rather than four, and the adapter simply spreads those
sixteen across its four sockets — which is why **everything behind it is an
ordinary Jaguar joypad**. A pad in socket 2 has no idea the adapter is
there.

**One adapter per port, so up to eight pads.** You can select it on both
ports independently.

### Which frontend port is which player

The pad you already use on Jaguar port 1 stays on frontend port 1, and port
2's stays on frontend port 2, so nothing you have already bound moves. The
extra sockets are appended after them:

| Frontend port | Jaguar port | Tap socket |
|---|---|---|
| 1 | 1 | 0 (the pad plugged straight into the adapter's first socket) |
| 2 | 2 | 0 |
| 3 | 1 | 1 |
| 4 | 1 | 2 |
| 5 | 1 | 3 |
| 6 | 2 | 1 |
| 7 | 2 | 2 |
| 8 | 2 | 3 |

**So with one Team Tap on Jaguar port 1, your four players are on frontend
ports 1, 3, 4 and 5** — not 1 to 4. Frontend port 2 is still Jaguar port 2,
where a fifth pad would go.

**If the extra pads do nothing, check that your frontend has actually
assigned those ports.** The core advertises eight ports and reads all of
them, but a frontend that leaves ports it has never been told about set to
*None* reports no input for them. In RetroArch that is *Settings → Input →
Port N Controls → Device Type = Standard Joypad*, plus binding a physical
controller to the port.

### Remapping the extra pads

*Port N > Button Remapping* and *Numpad to Keyboard* apply to **socket 0
only** — they exist because RetroArch's own Controls menu cannot reach four
of the Jaguar keypad keys, and there are two of them, one per Jaguar port.
Pads 3 to 8 use the fixed default layout (D-pad, A/B/C on A/B/Y, Option on
Start, Pause on Select, keypad 0–6 on X/L/R/L2/R2/L3/R3) and are remapped
from your frontend's own Controls menu, which reaches all of it.

### Standard pads only

A mouse, rotary, analog/driving controller or light gun cannot be put in a
tap socket. That is a deliberate limit rather than an oversight: Atari's
manual allows an advanced controller behind the adapter but permits only a
plain controller *read* through it — "software control of advanced features
like rumble motors, force feedback and analogue/digital mode will not be
possible" — so the useful half of the combination does not exist on real
hardware either. Selecting one of those devices on a port turns that port's
Team Tap off.

**Do not assume the light gun is unaffected by an adapter it is sharing.**
The manual says the adapter's six input lines are wire-ORed across all four
sockets, and the light-pen pin is one of those six. The pulse still reaches
TOM, but it is not isolated from the other sockets' B0 line, and nobody has
documented what a gun plus pads on one adapter actually does. This is why
the core does not offer a gun in a tap socket.

### Which titles use it

**Known retail support is two titles**, and both come from the historical
record rather than from anything this core has verified:

| Title | Team Tap |
|---|---|
| *White Men Can't Jump* | **Required** for 3- and 4-player games |
| *NBA Jam Tournament Edition* | Optional — supports it, does not need it |

**Homebrew support is unestablished.** A static scan of 27 corpus ROMs
produced no negative result: about half compose their row codes in a
register, so the scan cannot tell whether they address a tap — and that half
includes both titles above. So the table is what the hardware's documentation
says, not a measurement we made.

**No title in this list has been validated in-game here.** The only program
confirmed to drive the adapter in this core is the PD **Joypad-TeamTap
Tester** (Matthias Domin, 2000), which sweeps all sixteen row codes on both
ports and prints its verdict on screen. With the option off it reports
"TeamTap not found!" for both ports; with it on for a port, that port reads
"TeamTap Found!". That proves the adapter answers detection correctly — it
does not prove any game plays correctly through it.

Turning the option on for a title that does not read the adapter is harmless:
it changes nothing that title looks at.

### Please test this and file what you find

This shipped default-off, on a best-effort reconstruction of the protocol
from Atari's manual, with exactly one PD tester as game-side evidence. That
is a thin basis, and the fastest way to thicken it is people trying it.

**What to try**

1. *White Men Can't Jump* with a Team Tap on Jaguar port 1, three or four
   players. This is the one title that **requires** the adapter, so it is
   the strongest test available.
2. *NBA Jam Tournament Edition*, same setup, three or four players.
3. The **Joypad-TeamTap Tester** if you have it — the quickest sanity check
   that the adapter is being seen at all.
4. Homebrew or demos that advertise 4-player support. Nothing here is known
   to work; a confirmed *working* title is as useful a report as a broken one.

**What "working" looks like**

- The title offers its 3- and 4-player modes instead of greying them out or
  reporting no adapter.
- All four pads move their own player, with no crosstalk — pressing a button
  on pad 3 must not move player 1, 2 or 4.
- Socket 0 (frontend port 1 or 2) behaves exactly as it did with no adapter
  selected.
- Nothing changes in a title that does not support the adapter.

**What to include in an issue**

Please open an issue at
<https://github.com/libretro/virtualjaguar-libretro/issues> with:

- the title and the dump you used;
- which port the tap was on, and which frontend ports you bound;
- what you expected and what happened — "player 3 mirrors player 1" is a far
  more useful report than "4-player does not work";
- your frontend and its version (RetroArch's port-assignment behaviour for
  the advertised-but-unconfigured ports 3–8 is the least-verified part of
  this feature);
- your RetroArch log if anything crashed or hung.

Reports that the adapter is **inert** on a title that should support it are
just as valuable as crash reports — that is the failure mode the tester ROM
cannot catch.

## Pro Controller (#514)

Set *Port 1/2 > Controller Type* to **Pro Controller (6-button)**. Default is
*Auto*, which means a plain joypad — nothing changes until you choose it, and
`test/tools/joymatrix_identity` and `test/tools/procontroller_decode_test`
both assert the default is bit-identical to a core without this option.

**What we implement.** The retail Pro Controller adds three fire buttons
(X, Y, Z) and a pair of shoulder buttons to the standard three-button pad.
Atari's own Jaguar SDK header (`jaguar.inc`, revision 8/08/95, *"added
ProController equates"*) and its developer newsletter of the same week both
say the same thing: those five extra buttons are **wired onto five existing
numeric-keypad positions**, not a new device with its own identifier. To
quote the newsletter directly — *"reading the new buttons is very simple,
because they map directly to keys on the numeric keypad ... to see if the 'X'
button is pressed, for example, you simply check the same bit as you would
for the '9' key."* The full citation trail is
[`docs/teamtap-procontroller-spike.md`](teamtap-procontroller-spike.md)
section 9 — the TR10 manual never mentions the device at all, which is why
that spike originally left #514 blocked before the SDK source was found.

| Pro Controller button | Aliased keypad key | Bound RetroPad button |
|---|---|---|
| Z (fire) | 7 | R2 |
| Y (fire) | 8 | L2 |
| X (fire) | 9 | X |
| Left shoulder | 4 | L1 |
| Right shoulder | 6 | R1 |

A, B, C, Option and Pause keep their standard bindings (A, B, Y, Start,
Select) — selecting Pro Controller only changes the five rows above.

**Why this is a core option and not a frontend device type.** Every other
device on this page (mouse, rotary, analog, driving, paddle, light gun) is a
genuinely different piece of electronics with its own signal on the wire, so
it gets its own `RETRO_DEVICE` subclass and shows up in your frontend's
*Controls → Device Type* list. The Pro Controller has **no such thing to
plug into**: register-for-register it is an ordinary standard pad, and the
"six-button" part is entirely in which five keypad intersections the extra
buttons happen to close. Exposing that as a separate frontend device would
imply a plumbing distinction that does not exist, so it lives as a preset on
the existing *Controller Type* option instead — see
[`docs/teamtap-procontroller-spike.md`](teamtap-procontroller-spike.md)
section 9.7 for the reasoning in full.

**Why this is opt-in, and will stay opt-in.** Because the aliasing is real
hardware, not an emulation shortcut, a title that reads its own keypad —
weapon select, level codes, a menu shortcut on `7`/`8`/`9`/`4`/`6` — cannot
tell a genuine keypad press from a Pro Controller press pressing the "same"
button; on real silicon those are the identical electrical event. With Pro
Controller selected, pressing RetroPad X/Y/L1/R1/L2/R2 on a title that never
heard of the Pro Controller will register as those keypad digits. Leave the
option on *Standard Joypad* unless a game specifically wants the extra
buttons.

**Coverage is register-level, not game-verified.** No detection method for
the Pro Controller was ever published — not by Atari, not since. A game
cannot ask the console "is a Pro Controller attached," so no title can be
shown to *require* this preset, and the retail catalogue includes no
confirmed Pro-Controller-only game in this project's testing so far.
`test/tools/procontroller_decode_test.c` proves the register-level claim
above — pressing each of the five slots clears exactly the predicted
`$F14000` bit on its own row and moves nothing else, on both ports — but
that is a unit-level guarantee about the matrix decode, not evidence any
specific game reacts correctly to it.

**Call for testing.** If you own a Jaguar title that specifically advertises
Pro Controller support (packaging, in-game controls screen, or a manual
"ProController / Standard" menu — community reports say some titles offer
one), please try this option and tell us what you find:

- Does the game's own controls screen show the Pro Controller's extra
  buttons responding?
- Does gameplay actually change (a fire button doing something a standard
  pad's A/B/C could not), or does the game merely read the same keypad keys
  it would from a standard pad's numeric entry?
- Did you find a game that reacts *incorrectly* — buttons landing on the
  wrong function, or a standard-pad-only game visibly confused by phantom
  keypad presses when this option is on?

File an issue on the tracker with the game, region and what you observed
either way — a working report and a "does nothing new" report are both
useful data, since no released title is currently confirmed to require this
preset.

## 6D controller (#538) — a best attempt, and we need testers

Set *Port 1/2 > Controller Type* to **6D Controller (bank-switching)**.
Default is *Auto*, which means a plain joypad — nothing changes until you
choose it.

**Read this first: nothing on earth is known to drive this controller.** It
was specified by Atari and never released, no released or homebrew title is
known to poll it, and there is therefore no software this implementation has
ever been checked against. What ships here is a best attempt built from the
*Jaguar Technical Reference V10* alone — pages 15, 16, 21, 22, 23 and 27–28.
It is conformant to the manual (`test/tools/sixd_decode_test` asserts that,
cell by cell) and **unvalidated against any real program**. Please do not
read "supported" as "verified".

### What the device is

Six degrees of freedom — three translations *X*, *Y*, *Z* and three torques
*TX*, *TY*, *TZ*, eight bits each — plus seven buttons **A–G** and a
**Rezero** control. TR10 names the torques after aircraft axes: *Pitch* is
TZ, *Yaw* is TX, *Roll* is TY. That is 55 bits, which does not fit the 24 a
standard controller can return, so the device answers the ordinary `$F14000`
row scan with **three banks** of data that advance automatically each time
the console's row select goes from row 3 back to row 0.

### Our interface

| Host input | Degree of freedom |
|---|---|
| Left stick X | **X** — translate left / right |
| Left stick Y | **Y** — translate up / down |
| R2 − L2 (analog triggers) | **Z** — translate fore / aft, "thrust" |
| Right stick X | **TX** — yaw |
| R − L (analog shoulders) | **TY** — roll |
| Right stick Y | **TZ** — pitch |

| Host button | Jaguar |
|---|---|
| A / B / Y / X | **A / B / C / D** |
| L3 / R3 (stick clicks) | **E / F** |
| Start | **G** |
| Select | **Rezero** |

A RetroPad exposes exactly six analog signals a frontend can reasonably
route — two sticks and the two shoulder pairs — so the six DOF map one to one
with nothing doubled up. The shoulder pairs are read as analog *buttons*, so
a frontend that reports real pressure gives you proportional roll and thrust
and one that does not gives you clean digital roll and thrust; you never lose
the axis. **This pairing is our choice, not a specification** — TR10 defines
what the six values mean to the machine and says nothing about what a human
holds. Expect to want it different, and say so on the issue.

The port stays a plain RetroPad until some axis actually deflects, the same
liveness rule the mouse and the analog controller use, so a title that probes
controller types once at boot only sees the 6D device if you move a stick
first. Per-axis tuning uses the shared `virtualjaguar_analog_*` rows; with
only two tuning slots for six DOF, the split is by *where the host value came
from*: **X and TX** take the X-axis rows, **Y, Z, TY and TZ** take the Y-axis
rows, so "X dead zone" still means "the dead zone on horizontal stick
motion".

### What we do NOT implement

- **Pause and Option are unreachable while the device is engaged**, and that
  is the hardware rather than an omission: the 6D bank tables contain no
  Pause bit and no Option bit anywhere. TR10's answer is a physical joypad
  plugged into a DB15 socket on the controller itself, relayed "as one of its
  banks" — and the 6D layout has no spare bank for it. We did not invent one.
- **The "output your last bank during controller identification" rule.** An
  identification read is indistinguishable from a game read on the bus, so
  there is nothing to detect. TR10's own driver recipe — read every bank into
  a table, then find bank 0 by its flag bit — works regardless.
- **The 100 µs row-0 validity rule**, which exists so a real microcontroller
  ignores Boot ROM row codes. Inert in a model with no settling time.
- **Settling delays** generally (~25 µs per row, ~200 µs extra per bank on an
  analogue device). A compliant driver's delay loop simply spins over data
  that is already valid.
- **The ≤ 50 mA / ≤ 10 mA current budget** and the DB15 connector itself:
  electrical and mechanical, nothing to emulate.

### Two places TR10 is ambiguous, and what we chose

1. **The X sign.** Page 23's prose says *"X is positive right to left"*; the
   figure on that same page draws its horizontal axis arrow pointing to the
   **right**. They contradict each other and the page settles nothing. We
   follow the prose, so pushing the host stick right makes X *decrease*. This
   is one negation in `InputDevFeed6D()` and one assertion in
   `sixd_decode_test`; if anyone ever proves it the other way, it is a
   two-line change.
2. **The three torque signs.** *"Counter-clockwise when facing the positive
   direction"* is a statement about a physical grip whose orientation
   relative to the player is undefined. We pass the host values through
   un-negated. That is a **named guess**, not a derivation.

One more thing worth checking against silicon if it ever exists: in bank 0
the buttons run **A, B, C, D** up the rows, but in bank 1 they run
**Rezero, G, F, E** *down* them. That asymmetry is what the manual prints and
we implement it as printed — it is the most likely place for the manual (or
our reading of it) to be wrong.

### Please test this, and tell us what you find

There is no game to try, so what would help is:

- **Homebrew.** Write something that scans the three banks the way TR10
  describes, and tell us whether the values arrive where you expect. If you
  are testing controller *detection*, remember to deflect an axis first —
  otherwise the port honestly reports a standard joypad.
- **Real hardware.** If a 6D prototype, or any device that implements this
  protocol, actually exists somewhere, a capture of a working scan would
  settle the X sign, the torque signs and the bank-1 button order in one go.
- **The mapping.** If the stick / trigger / shoulder assignment above is
  awkward for what you are building, say what you would rather have.

File findings on
[#538](https://github.com/libretro/virtualjaguar-libretro/issues/538) — a
negative result ("I drove it and X came out backwards") is exactly as useful
as a positive one, and much more likely.

## Already shipped

The **Tempest rotary** (#436), the **analog / driving controllers** (#437),
the **light gun** (#438), **per-axis tuning** (#439) and the **Pro
Controller preset** (#514) have all shipped — see [Core
options](#core-options) above. The two deliberate exclusions, "ADC-Reg" and
the Jaguar VR head tracker, are recorded with their reasons in the analog
section above; anything else still open lives on

the **light gun** (#438), **per-axis tuning** (#439), the **paddle ADC**
(#505), the **Team Tap** (#513) and the **6D controller** (#538) have all
shipped — see
[Core options](#core-options) above. The 6D controller is a *best attempt*
with no software to validate it against, and its section says so at length.
The one remaining deliberate exclusion, the Jaguar VR head tracker, is
recorded with its reason in the analog section above; anything else still
open lives on
[#428](https://github.com/libretro/virtualjaguar-libretro/issues/428).

## See also

- [`docs/jaguar-mouse-adapter-mapping.md`](jaguar-mouse-adapter-mapping.md) —
  the sourced pin → register-bit mapping this is built from
- [`docs/teamtap-procontroller-spike.md`](teamtap-procontroller-spike.md) —
  the Pro Controller (and Team Tap) source spike, including the SDK/devnews
  citations behind the keypad-aliasing table above
- [#428](https://github.com/libretro/virtualjaguar-libretro/issues/428) —
  input-devices epic
- [#429](https://github.com/libretro/virtualjaguar-libretro/issues/429) —
  ST/Amiga mouse
- [#514](https://github.com/libretro/virtualjaguar-libretro/issues/514) —
  Pro Controller

- [#538](https://github.com/libretro/virtualjaguar-libretro/issues/538) —
  6D controller (file testing findings here)
- [#522](https://github.com/libretro/virtualjaguar-libretro/issues/522) —
  why the 6D section cites *Technical Reference V10* page numbers rather than
  source files.  **Only V10 carries the controller chapter**: `Technical
  Reference v8.pdf` and Atari's original `04 - Technical Reference.pdf` do
  not, so anyone verifying against the wrong revision concludes the spec does
  not exist
