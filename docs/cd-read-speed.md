# CD read speed core option

`virtualjaguar_cd_read_speed` — data-transfer rate for the HLE streamed
CD_read path (`src/cd/jagcd_hle.c::JaguarCDHLEStreamTick`). Values: `1x`,
`2x` (default, hardware-accurate: 150 double-speed sectors/s x 2352 B =
352,800 B/s), `4x`, `8x`, `instant` (whole transfer in one halfline tick,
still routed through `HLEStreamFinish` so status-struct writes / done
flags / FF-pad behave identically to a paced transfer).

Design constraints:

- **Latched per-read at arm time** (`hleStream.speedMult`, same pattern as
  the `statusBase` latch): flipping the option mid-transfer cannot change
  the rate a game is already pacing itself against.
- **HLE boot mode only.** The real-BIOS path (`src/cd/cdrom.c`) is NOT
  scaled: its FIFO half-full IRQ cadence (`FIFO_REFILL_PERIOD_X100`, the
  16-deep drain model) and DSA response delay (`DSA_RESPONSE_DELAY_TICKS`)
  encode race-avoidance fixes (DSA steal race, FIFO-storm LRXD
  corruption) that faster delivery would re-open, and the BIOS GPU ISR +
  DSP slave consumers assume the I2S-rate interrupt density. BIOS mode is
  experimental anyway; it stays at the accurate rate.
- The default (`2x`) is arithmetically identical to the pre-option
  behavior (`inc * 2 / 2`), verified by a full `cd_boot_matrix.sh` run
  (3000 frames, HLE + BIOS): stage results bit-identical to an unmodified
  build of the same HEAD.

## Per-title survival, non-default speeds (2026-07-26, HEAD d812093)

`cd_wedge_probe --frames 900 --arm 30 --freeze-frames 300`, HLE mode.
CLEAN = no frozen framebuffer in the boot window; flag = probe fired.

| Title | 1x | 2x (control) | 4x | 8x | instant |
|---|---|---|---|---|---|
| Battle Morph | CLEAN | CLEAN | CLEAN | CLEAN | CLEAN |
| Hover Strike | CLEAN | CLEAN | CLEAN | CLEAN | CLEAN |
| Primal Rage | CLEAN | CLEAN | CLEAN | CLEAN | CLEAN |
| Baldies | flag* | flag* | CLEAN | CLEAN | CLEAN |

\* Known static-load-screen false positive (see the `cd_wedge_probe`
caveat in CLAUDE.md): at 1x/2x the boot load holds a static screen longer
than the 300-frame freeze window while the 68K sits in its CD_poll loop
($004DAx, D1='FEND'). The full 3000-frame boot matrix passes at 2x, and a
1x run armed past the load phase (`--arm 2000`) runs clean.

Caveats: this is a **boot-window** check only. The failure class that
motivated the 2x default — instant delivery overwriting in-flight game
code (Hover Strike's mission loads span the address range of their own
poll loop) — occurs on **in-game** loads that a 900-frame headless boot
never reaches. Titles that survive `instant` at boot may still wedge on
later loads; that is expected and documented, not a bug to fix at
non-default speeds. Timing-sensitive titles (Primal Rage TOC-duration
countdown, Battle Morph 917KB mid-handshake reads) should be treated as
2x-only until verified in RetroArch.
