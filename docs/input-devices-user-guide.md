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

Fourteen options together cover device selection and per-axis tuning for both
ports. The mouse rows live under **Port 2**; the rotary rows are shared by
**both** ports, exactly like *Rotary Sensitivity* always has been — a rotary
plugged into either port draws on the same five rotary options.

| Option | Values | Default |
|---|---|---|
| *Port 1 > Controller Type* (`virtualjaguar_p1_device`) | Auto (per-title default), Standard Joypad, Rotary (Tempest) | Auto |
| *Port 2 > Controller Type* (`virtualjaguar_p2_device`) | Auto (per-title default), Standard Joypad, Atari ST / PS2 Mouse, Amiga Mouse (ST adapter), Amiga Mouse (Amiga adapter), Rotary (Tempest) | Auto |
| *Port 2 > Mouse Sensitivity* (`virtualjaguar_mouse_sensitivity`) | 25% – 400% | 100% |
| *Port 2 > Mouse Dead Zone (X)* (`virtualjaguar_mouse_deadzone_x`) | Off, 1 – 8 units | Off |
| *Port 2 > Mouse Dead Zone (Y)* (`virtualjaguar_mouse_deadzone_y`) | Off, 1 – 8 units | Off |
| *Port 2 > Mouse Offset (X)* (`virtualjaguar_mouse_offset_x`) | -4 – +4 | Off |
| *Port 2 > Mouse Offset (Y)* (`virtualjaguar_mouse_offset_y`) | -4 – +4 | Off |
| *Port 2 > Mouse Response Curve (X)* (`virtualjaguar_mouse_exponent_x`) | Linear (1.00) – 3.00 | Linear (1.00) |
| *Port 2 > Mouse Response Curve (Y)* (`virtualjaguar_mouse_exponent_y`) | Linear (1.00) – 3.00 | Linear (1.00) |
| *Rotary Sensitivity* (`virtualjaguar_rotary_sensitivity`) | 25% – 400% | 100% |
| *Rotary Reports Controller Type* (`virtualjaguar_rotary_id`) | Standard Joypad (no diode), Tempest Rotary (diode fitted) | Standard Joypad |
| *Rotary Dead Zone* (`virtualjaguar_rotary_deadzone`) | Off, 1 – 8 units | Off |
| *Rotary Offset* (`virtualjaguar_rotary_offset`) | -4 – +4 | Off |
| *Rotary Response Curve* (`virtualjaguar_rotary_exponent`) | Linear (1.00) – 3.00 | Linear (1.00) |

The seven mouse rows (*Mouse Sensitivity* through *Mouse Response Curve (Y)*)
only appear once a mouse is actually attached to port 2. The five rotary rows
only appear once a rotary is attached to port 1 or port 2. Both groups are
gated on the live device, not the option string, so a device your frontend
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
*Controller Type* option: two on port 1 (Standard Joypad, Rotary), five on
port 2 (Standard Joypad, the three mice, Rotary) — everything except *Auto*,
which is not a real device. A device set that way outranks the core option.
Setting the port back to *Joypad* or *None* releases that claim rather than
forcing a pad — the core option decides again, immediately.

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

## Not yet implemented

Lightgun support (#438) and analog / driving controllers (#437) are open.
The **Tempest rotary** (#436) and per-axis tuning (#439) have since shipped —
see [Core options](#core-options) above.

## See also

- [`docs/jaguar-mouse-adapter-mapping.md`](jaguar-mouse-adapter-mapping.md) —
  the sourced pin → register-bit mapping this is built from
- [#428](https://github.com/libretro/virtualjaguar-libretro/issues/428) —
  input-devices epic
- [#429](https://github.com/libretro/virtualjaguar-libretro/issues/429) —
  ST/Amiga mouse
