# Jaguar Voice Modem (JVM) — protocol as spoken by Ultra Vortek

**Status:** derived 2026-08-17 by disassembling the retail Ultra Vortek ROM
(`Ultra Vortek (1995).jag`, 4 MB, the only JVM title). The game's own modem
driver is the authoritative spec for what a modem must say; no BigPEmu
assets were consulted. Emulated by `src/jerry/voicemodem.c` (issue #481).

## Hardware placement

The JVM plugs into the DSP port and talks to JERRY's asynchronous serial
UART (`$F10030` ASIDATA / `$F10032` ASICTRL+ASISTAT / `$F10034` ASICLK) —
the same silicon JagLink and CatBox use, so the netlink transport
(`docs/netlink-design.md`) carries it.

How Ultra Vortek services the port (all addresses are the retail ROM):

- **68K side** (driver at `$80AF96–$80BA19`, runs in ROM): transmits by
  polling ASISTAT TBE (bit 8) and writing ASIDATA; **never reads RX data
  registers** outside an init flush.
- **DSP side**: the audio engine's I2S interrupt handler tail
  (`$F1B168–$F1B1C2`, loaded to DSP RAM from ROM offset `$E31C`) polls
  ASISTAT once per I2S sample: on ERROR (bit 15) it writes CLRERR if SERIN
  is high; on RBF (bit 7) it reads ASIDATA and pushes the byte into a
  **256-byte ring in main RAM `$6274–$6373`**, write pointer at DSP RAM
  `$F1BA84`, read pointer at `$F1BA88`. The 68K driver consumes replies
  from that ring only. There is no RX interrupt use despite RINTEN being
  set; JINTCTRL bit 4 is enabled but the transfer path is the DSP poll.
- **UART programming**: ASICTRL = `$0021` = ODD (bit 0) + RINTEN (bit 5).
  Note **parity is DISABLED** here — PAREN is bit 1 and is clear. An earlier
  version of this line read "odd parity", which is wrong: only the ODD bit is
  set. Per JTRM Rev 8 p.94, "when parity is disabled the value of the ODD bit
  is transmitted in the parity bit time", so the slot is still sent and the
  frame is still **11 bit times** (1 start + 8 data + 1 parity slot + 1 stop).
  That 11 is what `UARTFrameUsec()` in `src/jerry/uart.c` multiplies by, and
  it is correct — verified against JTRM Rev 8 p.93, which also gives the
  divisor as `system clock / (N+1)` then /16.
  Wake attempts at ASICLK = `$1C` (~57.6 kbaud) falling back to `$56`
  (~19.2 kbaud); after wake succeeds the driver issues `$FFFE` (see below)
  and settles at ASICLK = `$56` — i.e. the DTE rate in use is **19200**,
  which works out to ~576 µs per byte.

**The 19200 rate is permanent, and the `$86xx` connect speed cannot change
it.** Confirmed from two independent directions:

- The official Atari spec (`docs/atari-jaguar-1999/07 - The Jaguar Voice
  Modem.pdf`, p.13) defines `$FFFE` as "Set host baud rate to 19200. **Only
  reset (`$FFFF`) can change the baud rate back to 57600.**" The DTE link has
  exactly two rates and only those two commands select them. The `$8100`
  reply reports the *analog* modem-to-modem handshake rate and never touches
  the baud register.
- In the retail ROM, `$F10034` (ASICLK) is written exactly **six** times in
  the whole 4 MB image, every one an immediate `MOVE.W #imm,$F10034` with a
  value of only `$1C` or `$56`, all inside the wake/reset routines
  (`$80B10A`–`$80B18C`, `$80B1EC`–`$80B238`, `$80B358`). There is no
  register-sourced write anywhere. The `$86xx` low byte is decoded at
  `$803C30` and stored solely to RAM `$57D4` for the MODEM CONNECT SPEED
  display.

So the ~5.8 ms of wire time each way for a 10-byte pad packet is authentic
and not tunable through the protocol. Reducing it is an emulator-side
enhancement — see issue #498.

**Note on sourcing:** this decode was derived by disassembling the retail
ROM, deliberately without consulting other emulators. The official Atari JVM
manual above (present in the gitignored `docs/atari-jaguar-1999/` tree)
independently corroborates the command set and both baud rates. Consult it
first for any future JVM work — it is the primary source.

Menu entry: type **`911` on the numpad while the title logo is on screen**
("AWESOME" sting → INITIALIZING VOICE MODEM). With no modem answering, the
game shows MODEM INITIALIZING FAILED / MAKE SURE THE MODEM IS ON.

## Wire format

Console → modem: **16-bit command words, low byte first** (`$8A21` goes out
as `$21 $8A`). Every command is answered except `$FFFE`.

Modem → console: **3-byte messages: sync `$FF` (`$FE` also accepted), then
high byte, then low byte**. Reply classes, per the driver's parsers
(`$80B3B0` transact, `$80B452` data-receive, `$80AFE2` ring scan):

| High byte | Meaning | Driver behavior |
|-----------|---------|-----------------|
| echo of command | command acknowledged | compared against what was sent |
| `$A4` | async status event | `$57F6 &= word` (a bit-clear mask); if the mask goes to zero **and bit 0 of the low byte is set** the driver returns error `$FFF3` ("lost phone connection" class); otherwise consumed silently |
| `$B1` | async ring indicate | returns status `$FFF4` (RING) to whatever call is in progress |
| `$F0` | data byte (low byte = payload) | data-phase receive |
| `$F3` | data frame end; `$F301` = clean end-of-packet | terminates a receive burst |
| `$86` | connect result for `$8100` (low byte = speed code, shown as MODEM CONNECT SPEED) | proceed with connection |
| `$80..$8F` (`& $F000 == $8000`) | `$8100` "not yet" | retry (up to 255×) |
| `$FFFE` (word) | "nothing" reply to `$6800` DTMF poll | poll again |

## Command set (as issued by Ultra Vortek)

| Word | Name (inferred) | Modem must do |
|------|-----------------|----------------|
| `$FFFF` (two raw `$FF` bytes) | wake/detect | reply word **`$B800`**; sent at 57.6k, then 19.2k, then 57.6k until answered |
| `$FFFE` | switch line rate (to 19200) | **no reply** (console switches ASICLK right after) |
| `$0102` | ident/ping after wake | echo |
| `$0501` | begin config block | echo |
| `$000F $B000 $3952 $A021 $F207 $B602 $B504 $B405 $A3FC $A060` | config parameters | echo each |
| `$2C80` | originate mode (appears in the dial config table, at dial start, and again after handshake) | echo |
| `$2480` | answer mode (same positions on the answer side) | echo; going off-hook to answer a pending ring |
| `$8C01` | dial-tone check | echo `$8C01` when line ok (anything else retried ~1000×, then NO DIALTONE) |
| `$8A2n` | send DTMF digit n (0–15; the dialed number, and the post-connect probe digits) | echo; deliver digit to the far side when connected |
| `$8C00` | call-progress query | low byte `$1x` = tone/dial still in progress, `$0x` = idle/done |
| `$6800` | poll for detected DTMF digit | `$68nn` = heard digit nn, word `$FFFE` = nothing yet |
| `$8000` | go to data mode (pre-connect) | echo |
| `$8100` | connect/carrier query | `$86xx` = connected at speed xx, `$80xx` = not yet (retried); after `$86xx` the console waits for `$A4` events to clear `$57F6` bits 0+1 (send `$A4FC`) |

**The `$86xx` speed byte is validated** (RAM `$803C30`, post-connect): the
game computes `(xx >> 4) - 8`; a negative result is declared TOO MUCH
TELEPHONE NOISE and the call is torn down. Nibbles `8..E` index a
displayed-speed table: 9600, 9600, 12000, 14400, 16800, 19200, 57600.
The emulation replies `$86D0` (19200, matching the programmed UART rate).
| `$0002`, `$A3FE` | post-connect config | echo |
| `$9000` | hang up / abort | echo (sent repeatedly until echoed) |
| `$A040`, `$A0A0` | audio path control (hangup path sends `$9000` then `$A040`) | echo |
| `$F0xx` | data byte xx to peer (sent raw, no echo consumed) | forward to peer; **do not echo** |

## Connection choreography (both consoles run Ultra Vortek)

Originator: wake → `$FFFE` → `$0102` → `$0501` + config(`$2C80`) →
`$8C01` dial tone → `$2C80` + `$8A2n` digits of the phone number (`$8C00`
polled between digits; keypad `$10` = 2 s pause) → poll `$6800` for the
answerer's probe digits `0,9,8,…,1` → send its own probe digits
`1,2,…,9,0` as `$8A2n`, polling `$8C00` after each → `$2C80`.

Answerer: wake/config happen when the user enters the modem screen; on
RING (`$B1xx`) and the user choosing ANSWER PHONE: config(`$2480`) → sends
probe digits `0,9,8,…,1` → listens via `$6800` for `1,2,…,9,0` → `$2480`.

The mutual DTMF probe is the line-quality check ("TOO MUCH TELEPHONE
NOISE" when it fails). Speed/carrier negotiation is the `$8000`/`$8100`/
`$86xx` + `$A4FC` sequence at `$80B278`.

## Top-level flow (RAM code; RAM address = ROM offset + $43FE)

The whole 68K driver is copied to RAM with the main program and runs
there (absolute `jsr $11298`-style refs prove it); the `$80Bxxx`
addresses in this document are the ROM image of RAM `$F394..$FE17`.
Dial UI path (`$803A50`): dial_seq (`$80B6DC`) -> go_online (`$80B278`)
-> `$57DB=0` (this console is player 1). Answer path (`$803ABC`):
answer_seq (`$80B850`) -> go_online -> `$57DB=1`. Both converge at
`$803C30`: speed-nibble check, then `$57DC=1` — the flag that switches
the game's input layer to modem-exchanged pad state.

**What UV's netplay actually exchanges:** with `$57DC` set, each frame
the game packs its local pad longword (plus a coin/meta bit 22) into
`$579A` and sends it as one 4-byte packet (`$803DDC` -> SendData
`$80B570`); the far side's packet is received into `$57A0` and the two
pads are fed to the player slots (`$57E4`/`$57E8`, selected by `$57DB`).
`$57DD` throttles to one packet per received packet — classic lockstep.

## Data phase (in-game)

- Sender: 4 payload bytes per exchange, each as a raw `$F0xx` word
  (`$80B570`, from `$579A`), no replies consumed.
- Receiver: expects 4 × `$F0xx` messages then **`$F301` end-of-packet**
  (`$80B596` → `$57A0`). The `$F301` is generated by the modem (packet
  framing), not sent by the far console — the emulation emits it at
  TX-burst end.
- Async `$A4` with bit 0 set (e.g. `$A401`) during a call = line drop →
  LOST PHONE CONNECTION.

## What the emulation does (src/jerry/voicemodem.c)

A virtual modem in front of the jlink transport (`virtualjaguar_uart_device
= voicemodem`; JagLink stays the default). Dial resolves to the existing
netlink TCP/netpacket/loopback session; no voice, no DTMF audio, no real
telephony. Inter-modem wire protocol (2-byte frames over the transport):
`$01 digit` DTMF, `$02 -` dial, `$03 -` answer, `$04 -` hangup, `$05 xx`
data byte, `$06 -` end-of-packet. Ring indications are paced ~1/second;
`$A4FC` is queued after each connected `$86xx` reply so the `$57F6` wait
can never miss it. ASICLK is ignored — the UART already paces bytes; the
modem is rate-agnostic. Modem session state is host-side (like jlink's
sockets) and deliberately not serialized in savestates: loading a state
mid-call behaves as a pulled phone line.

## Troubleshooting a call that never connects

Ultra Vortek prints INITIALIZING VOICE MODEM *before* any reply arrives, so
that screen looks identical whether or not a modem is answering — it is not
evidence the link works. The core logs the facts instead; look for
`[NETLINK]` in the frontend log.

At load, one line reports what the options actually resolved to:

```
[NETLINK] mode=tcp_client device=voicemodem peer=127.0.0.1:42171 (address from core option)
[NETLINK] mode=tcp_server device=voicemodem port=42171
[NETLINK] built-in TCP link disabled (device=voicemodem) -- frontend netplay will carry the link if a session is running
```

then the link state, on edges only:

```
[NETLINK] tcp_client open, waiting for peer...   <- no one on the other end yet
[NETLINK] link UP (tcp_client, device=voicemodem)
[NETLINK] link DOWN (tcp_client) -- peer disconnected
```

Reading them:

- **No `[NETLINK]` lines at all** — the core is too old to have the voice
  modem. Check the binary the frontend actually loaded, not the build tree:
  `strings <core> | grep -c virtualjaguar_uart_device` must be non-zero.
- **`device=jaglink`** — "Network Link Device" is still at its default. The
  raw cable cannot answer the wake handshake, so the game reports MODEM
  INITIALIZING FAILED. It must be set to Voice Modem on **both** sides.
- **Stuck at `waiting for peer...`** — the transport never paired. Check both
  sides agree on the port, and that exactly one is TCP Host.
- **`'From file' selected but no address read`** — the "From file" preset was
  chosen but `<system>/vj_netlink.txt` is missing or empty; the address fell
  back to 127.0.0.1.
- **`link UP` on both sides but the call still fails** — that is a modem-layer
  problem, not transport. Re-run with `VJ_VM_TRACE=1` for the command/reply
  stream, and see the choreography table above.

## Open questions

- `$B1xx` ring payload (we send `$B100`); the driver only matches the
  high byte.
- Config words (`$3952` etc.) are opaque Phylon chipset parameters;
  echoing satisfies the driver.
- The retail beta ROM and any voice-mode-specific commands beyond `$A0xx`
  echoes are unexplored.
