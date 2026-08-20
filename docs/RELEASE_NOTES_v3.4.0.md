# Virtual Jaguar v3.4.0 — Peripherals, texture dump, BIOS selection

The peripheral release. Every controller the Jaguar ever had — plus the one
that let you phone your opponent — now works, alongside a texture-dump
pipeline for HD pack authors and user-selectable boot ROMs.

## Highlights

### Ultra Vortek netplay, over the emulated Jaguar Voice Modem (#481)

The JVM was Atari's voice-and-data modem, and *Ultra Vortek* was the only
retail title that spoke to it. Both sides of that conversation now work:
the modem's command protocol is emulated on JERRY's UART, and the "phone
call" is carried over the existing netlink transport (TCP or RetroArch
netplay).

Type **911** on the numpad at the Ultra Vortek title screen, one player
dials any number, the other answers.

The protocol was reverse-engineered from the retail ROM rather than
guessed: a custom Phylon command set (not AT), 16-bit words console→modem,
3-byte replies back, and a mutual DTMF probe as the line-quality check. The
full decode is in [`docs/voice-modem.md`](docs/voice-modem.md).

Verified on two real iOS devices, not only headlessly.

### Network link setup that explains itself (#502)

Linking two consoles used to mean typing an IP address — which libretro
core options cannot do. They are strictly enumerated: the frontend stores a
value *index*, and no core on any platform can present a text field. The
only routes were a `.local` preset, a file, or localhost, and on iOS those
were the *only* options at all.

So the link now finds its own peers:

- **Automatic mode**, the new default: if a frontend netplay session is
  running, the link rides it with nothing to configure. That path already
  worked and nothing said so — it was buried in a blurb on the TCP *client*
  option, findable only once you had already committed to typing an address.
- **LAN discovery** — hosts beacon over UDP and appear in the host picker by
  name. Chosen over Bonjour because RetroArch's iOS/tvOS property lists
  enumerate only their own service types, so a core advertising its own is
  blocked, while the iOS entitlements *do* permit broadcast. UDP is not the
  fallback here; it is the only mechanism that works on the platform this
  was reported from.
- **On-screen narration** — what mode resolved, when the link comes up or
  drops, how many hosts were found, and a warning when the peer is running a
  different device than you (JagLink and Voice Modem cannot interoperate,
  and that used to fail silently).
- **`vj_netlink.txt` is finally documented**: one line, the address only, no
  port.

Automatic mode deliberately never opens a listener and never dials a
discovered peer by itself — with the Voice Modem that would place a call the
user did not initiate.

### The input-device epic, complete (#428)

| Device | Notes |
|---|---|
| ST/Amiga **mouse** | Port 2 |
| Tempest **rotary** | Both ports |
| **Analog / driving** | TR10 bank-switching protocol |
| **Lightgun** | Balloons homebrew verified playable |
| **Per-axis tuning** | Dead zone, offset, response exponent on every analog source |

Two of these needed hardware archaeology. The analog controller has **no
console-side ADC** — Atari deleted it from production silicon, so the
controller carries its own microcontroller and answers the ordinary
`$F14000` row scan. The lightgun latches TOM's **LPH/LPV light-pen
registers** continuously, not on the trigger, which means software cannot
detect a shot from those registers at all — the trigger is an ordinary pad
button.

### Texture dump mode (#369)

Every unique blit source a game uses is written out as a PNG with a stable
content hash, ready for pack authors to redraw. The identity contract is
frozen and documented in [`docs/texture-dump.md`](docs/texture-dump.md):
FNV-1a 64 over format, dimensions and raw source bytes — deliberately *not*
the palette, because the Jaguar blitter never sees one, and a key the
replacement pipeline cannot discriminate would strand every pack drawn
against it.

Default off. Enable `virtualjaguar_texture_dump`, play, and find the tiles
under `<system>/vj_texdump/<CRC32>/`.

### Texture replacement, tier 1 (#369)

The other half of the pipeline also ships: drop redrawn art into
`<system>/vj_texpacks/<CRC32>/<hash>.png` — the same hashes dump mode
writes — and it presents in place of the title's own tiles. Authoring is
"dump, redraw, move one directory over".

Packs are **true colour**; nothing in the path quantises RGB back to CRY,
and a pack pixel with alpha below 128 keeps the stock pixel instead.

The pipeline never writes the emulated machine. Replacement rides the
existing true-colour shadow framebuffer, gated on a per-pixel
straight-copy witness, so savestates, rewind, run-ahead and netplay stay
**bit-identical** with or without a pack — a pack cannot desync a link
game or invalidate a state.

Tier 1 covers 16bpp source tiles at 1x through straight-copy blits, which
is the sprite and UI blit class dump mode was built around. Indexed
sources (tier 2) and >1x Stage 2 surfaces (tier 3) are designed but not
implemented; entries of those kinds are skipped and the stock pixel
presents.

Default off, and the option only appears when a pack directory exists for
the loaded cartridge.

### Boot ROM selection (#469, #473, #477)

Cartridges can now boot the **Series K** or **Model M** console boot ROM
(both embedded), or a custom image from the system directory. External
images are identified by checksum and named in the log — Series K, Model M,
Stubulator '93, Stubulator '94 — with unrecognised images loaded anyway and
flagged, so homebrew and modified ROMs work.

## Fixes

- **Voice Modem corrupted video (#500)** — activating the modem tore the
  picture apart: scanlines out of order, content stamped repeatedly down the
  screen, alternating garbage and clean at 60 Hz. It needed no connection —
  starting to dial was enough. `UARTRaiseIRQ()` asserted a 68000 level-2
  interrupt after checking only JERRY's own mask, but JERRY's interrupt
  output reaches the 68000 solely through TOM's `C_JERENA` gate, which both
  PIT callbacks apply and the UART did not. Ultra Vortek never enables that
  gate (measured: 2598 of 2600 frames), because its DSP polls the serial
  status and the 68000 never uses the receive interrupt — so on real silicon
  those bytes raise nothing, while here each one re-entered the display
  handler at an arbitrary raster position. Measured 3.26 spurious interrupts
  per frame before the gate, 0.999 after.
- **Blitter cross-load state leak (#479)** — the B_CMD decode statics were
  never reset, so a session that used the fast blitter contaminated the
  *next* session's savestate. Invisible on desktop (a fresh `dlopen`
  re-zeroes statics) and live on iOS, where the core stays resident. Broke
  run-ahead and netplay determinism after an engine switch.
- **CHD session attribution and boot-header search (#476)** — two
  independent parser bugs. Session metadata was indexed by track number
  rather than among itself, mis-attributing session 2 on discs whose first
  session has more than one track; and the ATARI boot header was assumed at
  a fixed offset when it is really variable mastering filler (2 bytes on one
  disc, 378 on another). The second reproduces from CUE and is unrelated to
  CHD.
- **Idle analog offset manufactured input (#439)** — a non-zero offset fed
  phantom motion every frame from an untouched device, the exact failure the
  option exists to prevent.
- **JaguarDemos BootIntros (#469, #470, #473)** — GPU-only jagcrypt carts
  crashed the frontend; they now auto-enable the real boot ROM. 46 of 49
  demos boot, gated in CI.

## Accuracy groundwork

A real-hardware Doom capture was measured against our output: hardware
presents the attract demo at **4.0 fields/flip (15.01 flips/s)**; we present
2.00. That number now anchors the timing campaign, whose full plan ships in
[`docs/timing-campaign-plan.md`](docs/timing-campaign-plan.md) — including
the finding that Doom's own code gates flips on a two-field software floor,
which is why processor-clock scaling never moved the cadence.

The campaign itself lands in **v4.0.0**.

## Known issues

- Ultra Vortek netplay is verified over **both** transports: the direct TCP
  link by an automated two-instance test through the in-game lockstep
  exchange, and RetroArch netplay by play-testing on two devices. There is
  still no automated test for the netplay transport (#494) — RetroArch
  netplay cannot be driven by scripted input — so regressions there would
  not be caught by CI.
- The device-mismatch warning does not fire when the host was selected as
  `jaghub.local`, from `vj_netlink.txt`, or via `VJ_NETLINK_HOST` (#501). It
  compares against the raw option value rather than the resolved address, so
  the warning degrades to silence — connections themselves are unaffected.
- Link input latency is authentic rather than good: the modem's DTE rate is
  permanently 19200 (proven from Atari's own JVM manual and the retail ROM,
  where the connect-speed byte turns out to be cosmetic), which is ~5.8 ms of
  wire time each way per frame against a strictly lockstep exchange. An
  opt-in enhancement to compress it is tracked as #498 for v3.5.0.
- Voice audio over the modem is not emulated (#485) — the real JVM carried
  voice modem-to-modem, bypassing the console entirely.
- The analog/driving controller implements the TR10 bank-switching protocol
  on the `$F14000` joystick matrix. No released title is known to read *that*
  protocol, so its verification is the synthetic register-level suite in
  `test/tools/analog_decode_test.c`.

  Analog-capable software does exist, but it targets different hardware:
  **BattleSphere** and **BattleSphere Gold** sample the *motherboard* 8-bit
  ADC at `$F17C00` — JERRY GPIO5, the "Paddle Interface", an ADC0844 fitted
  only to early Jaguar boards and deleted from production silicon. They round-
  robin four channels from a JERRY Timer 1 handler and consume two of them in
  an "Analog Joystick Calibrator" screen and in GPU code, gated behind
  *Gameplay Options → 2nd Controller: Analog Stick*. Club Drive writes the
  same channel-select without ever reading it back. That interface is **not
  emulated**: this release reads `$F17C00` as `0xFF`, matching a production
  console with no ADC fitted (A/B-verified inert on BattleSphere across 900
  frames and Club Drive across 700). Emulating the ADC itself, and with it
  BattleSphere's analog stick, is #505.

## Downloads

Cores for 16 platforms are attached below. No BIOS files are required.

## Stats

`git diff --shortstat v3.3.0..v3.4.0` — 219 files changed, 98,856
insertions, 761 deletions, across 176 commits.

## Maintainers

Joseph Mattiello, with the Virtual Jaguar libretro contributors.
Original Virtual Jaguar by David Raingeard (Potato Emulation) and
James Hammons.
