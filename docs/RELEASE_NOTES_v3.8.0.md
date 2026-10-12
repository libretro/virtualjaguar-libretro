# Virtual Jaguar libretro v3.8.0

**Correct by default. Flip Out runs, Fight For Your Life boots on the real BIOS, Club Drive
has sound under HLE, White Men Can't Jump stops tearing, and Doom netplay holds 60 fps.**

v3.8.0 is the "Correct by default" release (milestone "v3.8.0 — Correct by default", epic
https://github.com/libretro/virtualjaguar-libretro/issues/746). It fixes titles that needed a non-default setting or misbehaved in one boot mode,
makes the per-title database explain itself in the frontend, and adds a release gate that
blocks a tag when any corpus row moves backward. There is one new core option (the
GameDrive SD card) and one new value for an existing option. The savestate format moves
from v16 to v17; v3.7.1 and older states still load (see Savestates).

Several fixes here are verified headlessly only, not yet in RetroArch. Each one says so
below, and they are collected under Known issues.

---

## Highlights

- **Flip Out runs in both boot modes.** 1, 2 and 4 MB cartridge images now repeat across
  the cartridge window as on hardware (https://github.com/libretro/virtualjaguar-libretro/pull/855, https://github.com/libretro/virtualjaguar-libretro/pull/851).
- **Fight For Your Life (1996) boots on the real BIOS.** Power-on RAM is now zero-filled in
  BIOS mode, as it already was in HLE (https://github.com/libretro/virtualjaguar-libretro/pull/868, https://github.com/libretro/virtualjaguar-libretro/pull/856).
- **Club Drive has audio under the default HLE boot.** DSP interrupts are taken at the
  DSP's next instruction instead of inside an event callback (https://github.com/libretro/virtualjaguar-libretro/pull/849, https://github.com/libretro/virtualjaguar-libretro/issues/744).
- **White Men Can't Jump gameplay stops tearing** at default settings, through a new
  "68000 only" value for Blitter Bus Timing (https://github.com/libretro/virtualjaguar-libretro/pull/870, https://github.com/libretro/virtualjaguar-libretro/issues/736).
- **Music Demo, Ruiner Pinball and Native Demo (bin) no longer run their DSP or GPU into
  garbage** (https://github.com/libretro/virtualjaguar-libretro/pull/862, https://github.com/libretro/virtualjaguar-libretro/pull/869, https://github.com/libretro/virtualjaguar-libretro/pull/872, https://github.com/libretro/virtualjaguar-libretro/pull/873).
- **Doom 2-player netplay holds 60 fps** instead of dropping to 30 Hz. Play over Wi-Fi is
  still slower than single player (https://github.com/libretro/virtualjaguar-libretro/pull/861, https://github.com/libretro/virtualjaguar-libretro/pull/871, https://github.com/libretro/virtualjaguar-libretro/pull/860).
- **The GameDrive SD card works**, backed by a folder on your device (https://github.com/libretro/virtualjaguar-libretro/pull/844, https://github.com/libretro/virtualjaguar-libretro/issues/783).
- **The frontend explains per-title presets**, and options that apply only at load say so
  (https://github.com/libretro/virtualjaguar-libretro/pull/847, https://github.com/libretro/virtualjaguar-libretro/pull/859, https://github.com/libretro/virtualjaguar-libretro/pull/863).

---

## Game fixes

### Cartridge titles

- **Flip Out (https://github.com/libretro/virtualjaguar-libretro/pull/851, https://github.com/libretro/virtualjaguar-libretro/pull/855).** The game checks the long at `$A02000` and executes `ILLEGAL`
  if it does not match. A 2 MB cartridge drives only the address lines it has, so on
  hardware its image repeats at `$A00000`; the core returned 0 there. HLE looped
  `ILLEGAL`/`RTE` from about frame 320 and BIOS trapped into the boot ROM at about frame
  810. Images of exactly 1, 2 or 4 MiB are now mirrored (1 MB every MB, 2 MB at `$A00000`
  and `$C00000`, 4 MB once at `$C00000`), matching the MiSTer core's `cart_mask`; the JTRM is
  silent on this. In a headless run HLE reaches the high-score screen by frame 1000 and BIOS
  the title screen by frame 1200. A 160-cart A/B changed only Flip Out, plus one pixel in
  Fight For Your Life, where a blit reads past the end of the 4 MB image. The cart window
  past an image is also cleared on load, so a previous title's bytes no longer survive a
  reload in the same session. Headless-verified only.
- **Fight For Your Life (1996), real BIOS (https://github.com/libretro/virtualjaguar-libretro/pull/856, https://github.com/libretro/virtualjaguar-libretro/pull/868).** The game sizes a fill from a RAM
  word nothing ever writes. BIOS mode filled power-on RAM with a per-word random pattern,
  so the fill overran into the GPU's parameter block, the GPU wrote a bad pointer, and the
  68K took an address error into the boot ROM at frame 1032. HLE zero-fills and never hit
  it. BIOS mode now zero-fills too. Power-on DRAM contents are undefined on hardware, but
  real parts come up in near-uniform runs, not independent random words. RAM-loaded images
  and the first 8 bytes are still preserved. `test_ffyl_856` now runs 1400 frames to game
  code. The good dump is CRC `B14C4753`; the `[a1]` dump is an authentic BIOS reject.
  Headless-verified only.
- **Club Drive audio under HLE (https://github.com/libretro/virtualjaguar-libretro/issues/744, https://github.com/libretro/virtualjaguar-libretro/pull/849).** Club Drive was silent under the default HLE
  boot in about 5 of 97 start-up phases. The core vectored the DSP inside the I2S event
  callback; the 68K then rewrote `D_PC` while the DSP was running (JTRM v8 p.110 allows that
  only while it is idle), so the interrupt service routine never ran and the game never
  re-armed it. The interrupt is now latched and taken at the DSP's next instruction.
  Club Drive HLE went from RMS 0.0 to 2948.6. A 160-cart A/B was framebuffer-identical in
  both modes, and the audio pair is unchanged on Wolfenstein 3D, Doom, White Men Can't Jump,
  Iron Soldier 1 and 2, Skyhammer, Atari Karts, Raiden, Tempest 2000 and Zoop. Confirmed by
  ear in RetroArch on iOS (Doom, Wolfenstein 3D, Club Drive, Music Demo).
- **White Men Can't Jump gameplay tearing (https://github.com/libretro/virtualjaguar-libretro/issues/736, https://github.com/libretro/virtualjaguar-libretro/pull/870).** v3.7.1 turned Blitter Bus Timing
  on for this title to fix its intro logo. That also made the GPU wait out each of its own
  blits at worst-case cost, so the GPU's draw started late and in 86 of 125 fields was
  still running when the 68000 swapped buffers: horizontal tears and sprites missing for a
  field. Blitter Bus Timing gains a **"68000 only"** value that stalls just the 68000, and
  the title's preset now uses it. The intro logo fix still holds (boot frames 0-300 hash
  identically; the logo is 43.8% lit). Over two 600-frame stretches of the attract match,
  reverted frames / spliced frames went from 68/12 and 95/15 to 21/2 and 23/4. Some
  flicker remains. Headless-verified only (`fb_flicker_probe`).
- **Music Demo DSP runaway (https://github.com/libretro/virtualjaguar-libretro/pull/853, https://github.com/libretro/virtualjaguar-libretro/pull/862).** The DSP code stores `D_FLAGS` to change register
  bank and then sets `r31` in the next instruction. On hardware that instruction still sees
  the old bank (JTRM v8 p.109); the core switched immediately, so the stack pointer landed in
  the wrong bank and the first interrupt returned into garbage. The bank switch now waits
  out the two-instruction retire window, and `jump`/`jr` commit it before their delay slot
  so the Doom and Wolfenstein 3D epilogues behave as before. A 160-cart A/B (HLE and BIOS,
  1200 frames, framebuffer and audio) was byte-identical except Music Demo. The crash
  watchdog now also sees DSP runaways in HLE. Confirmed in RetroArch on iOS: Music Demo
  runs in both modes with no watchdog lines.
- **GPU `G_FLAGS` retire window (https://github.com/libretro/virtualjaguar-libretro/pull/865, https://github.com/libretro/virtualjaguar-libretro/pull/869).** The GPU gets the same two-instruction window
  as the DSP. No title is known to change. Unit-tested (`test_gpu_ops` 66/66); a partial
  corpus A/B (98 of 155 carts) was identical. The full A/B and a visual check are still
  owed.
- **Ruiner Pinball GPU escape (https://github.com/libretro/virtualjaguar-libretro/pull/866, https://github.com/libretro/virtualjaguar-libretro/pull/873).** `store Rn,(r15+Rm)` was the one GPU long-store
  form that did not drop the low address bits, so a word-aligned store split across two
  longs, a jump table went wrong and the GPU ran off into DSP RAM at frame 56. Every form
  now aligns, as the MiSTer RTL addresses GPU local RAM by bits 11:2 only. Ruiner Pinball
  loses its `gpu_pc_escape` and `video_stall` in both modes. The core does not yet claim the
  game is playable. Headless-verified only.
- **Native Demo (bin) boots (https://github.com/libretro/virtualjaguar-libretro/pull/866, https://github.com/libretro/virtualjaguar-libretro/pull/872).** This headerless binary is linked at `$5000`,
  but every address it uses also fits `$4000`, so the loader picked `$4000` and the GPU ran
  away at frame 1. On a tie the loader now prefers the base under which `LEA` targets point
  at Jaguar I/O descriptor tables. Across 141 resolvable carts only this title changes base.
  It now reaches game code with video in both modes, like the `.jag` version.
  Headless-verified only.

### Netplay

- **Doom 2-player netplay holds 60 fps (https://github.com/libretro/virtualjaguar-libretro/pull/860, https://github.com/libretro/virtualjaguar-libretro/pull/861, https://github.com/libretro/virtualjaguar-libretro/pull/871).** The per-frame wait for the
  partner's link byte ignored how long the rest of the frame took, so frames overran vsync
  and the core ran at 30 Hz with half-rate audio. The wait now fits in what is left of one
  frame, with a self-adjusting margin. In the simulator (3 ms each way) Doom went from 14.5
  to 22.2 image updates/s with overruns from 30/s to 0. https://github.com/libretro/virtualjaguar-libretro/pull/871 adds an idle-side wait: a side
  that only answers used to finish its frame early and cost the link a whole video frame per
  byte. Simulated tic rates rose from 16.3 to 20.8 at 3 ms each way and from 8.9 to 16.0 on
  a jittery Wi-Fi profile. A `[NETLINK] link stats` line now appears in the log every 5 s
  while a link carries traffic.
  - On iOS to iOS over Wi-Fi the maintainer saw https://github.com/libretro/virtualjaguar-libretro/pull/861 hold 60 fps, but players still moved at
    about half single-player speed. A Doom tic is 12 one-way single-byte hops, each costing a
    network round trip, so better waiting cannot fix the rest. Epic https://github.com/libretro/virtualjaguar-libretro/pull/864 (two Jaguars in one
    core with standard rollback netplay) tracks it. https://github.com/libretro/virtualjaguar-libretro/pull/871's gains are simulator numbers and
    have not been measured on a device.

---

## GameDrive SD card (https://github.com/libretro/virtualjaguar-libretro/issues/783, https://github.com/libretro/virtualjaguar-libretro/pull/844)

GameDrive software can now open, read, write, seek, list and stat files on an SD card
backed by a host folder: `jaggd-sd` inside the frontend's save directory (system directory
if there is none). This is what JagOS 'Spot', homebrew with streamed assets and screenshot
tools use. Paths are confined to that folder: `..`, drive letters, FAT-invalid characters
and symlinks are refused, and names match case-insensitively.

- New option **Jaguar GameDrive SD Card** (`virtualjaguar_jgd_sd`): Read/write (default),
  Read-only, Disabled. It is active only when the GameDrive is (`Jaguar GameDrive`: Auto
  for images over 6 MB, or Enabled).
- Open files are saved in savestates (see below). Loading a state never creates, truncates
  or appends to a file, but writes already made stay made, so run-ahead and netplay can
  repeat host-side writes. See `docs/jgd-interface-notes.md`.
- Not yet: GPU and asynchronous reads still return -1. Tested with a synthetic probe only;
  acceptance with a real JagOS ROM is pending.

---

## Per-title database and frontend

- **Compatibility presets (https://github.com/libretro/virtualjaguar-libretro/issues/748, https://github.com/libretro/virtualjaguar-libretro/pull/842).** A per-title setting is now either an
  *enhancement* (the performance profile may withhold it) or a *compatibility* setting (never
  withheld, and it must cite the ticket for the real fix so it can be retired). White Men
  Can't Jump's Blitter Bus Timing row is the first compatibility row. Your own option value
  still wins over any preset.
- **CD titles can have rows (https://github.com/libretro/virtualjaguar-libretro/issues/747, https://github.com/libretro/virtualjaguar-libretro/pull/843).** Discs are keyed by the CRC32 of their boot stub,
  which is the same across CUE, CHD and CDI rips of one pressing. The disc table ships empty
  in this release. Every CD load logs `[titledb] disc boot stub CRC32 $XXXXXXXX`. Seven
  homebrew discs share one loader and so share a key; audio-only discs have none.
- **The frontend explains presets (https://github.com/libretro/virtualjaguar-libretro/issues/841, https://github.com/libretro/virtualjaguar-libretro/pull/847, https://github.com/libretro/virtualjaguar-libretro/pull/850, https://github.com/libretro/virtualjaguar-libretro/pull/859).** At load RetroArch shows an
  on-screen notice naming the presets applied (for example "White Men Can't Jump: Blitter Bus
  Timing on (compatibility preset)") and says when the performance profile withheld or
  demoted one. Option sublabels describe the current state. The core never rewrites your
  option values. Options that apply only at load now start their description with "Restart
  required: applied when content is loaded." With a CD loaded, cartridge-only options
  (GameDrive, GameDrive SD card, enhancement hooks, blit memo) are hidden.
- **BIOS and PAL apply only at load (https://github.com/libretro/virtualjaguar-libretro/pull/858, https://github.com/libretro/virtualjaguar-libretro/pull/863).** Changing an unrelated option mid-game
  used to switch the boot ROM flag back off on GPU-only carts and real-BIOS CD boots, and
  PAL switched clocks live while the frontend's AV timing stayed NTSC. Both options now take
  effect at the next content load; a mid-game change writes a one-time "takes effect on
  restart" line to the log.
- **Towers II** no longer defaults to 2x internal resolution; it gave no visible gain (https://github.com/libretro/virtualjaguar-libretro/issues/809,
  https://github.com/libretro/virtualjaguar-libretro/pull/848).

---

## Savestates

- **States are now v17.** v16 and older states still load. v17 states do not load in older
  builds.
- **One chunk is appended after `BLH1`:** `JGF1`, the GameDrive SD handles (per open file the
  card-relative path, access and position; per open directory its path and position). A
  state from another machine works because paths are card-relative. A missing file comes back
  closed.
- The DSP (https://github.com/libretro/virtualjaguar-libretro/pull/862) and GPU (https://github.com/libretro/virtualjaguar-libretro/pull/869) retire windows are stored in spare bits of an existing byte,
  with no layout change. Older states load as "no window pending".
- Details: `docs/savestate-compat.md`.

---

## Testing

Release gate, develop at `c556940` against the v3.7.1 tag, run on the corpus machine
(`docs/release-process.md`). `test/tools/matrix_diff.py`: **0 rows moved backward** in either
matrix.

- **Cart corpus, 155 titles (Fast blitter).** HLE: 155/155 reach game code. Real BIOS: 141
  reach game code and 14 are `BIOS_REJECT`.
  - The 14 are not regressions. BIOS rows are now scored from the boot-ROM handoff instead
    of frame 0 (https://github.com/libretro/virtualjaguar-libretro/pull/852, https://github.com/libretro/virtualjaguar-libretro/pull/854), so a dump the boot ROM refuses (the red "Jaguar" screen) no
    longer counts as running just because the logo animation played. They are alternate or
    bad dumps and BIOS-only demos: Assassin Demo Part 1, Drumpad2, Fight For Your Life
    `[a1]` and `[a2]`, Hubble Fade, Hubble Nebula, Iron Soldier `[a1]`, Kasumi Ninja `[a1]`,
    Phase Zero, Rayman Demo (two), Super Cross 3D (this dump; the good one is CRC
    `EC22F572`) and Ultra Vortek Beta (two).
  - 8 rows improved: Ruiner Pinball (HLE and BIOS) and Native Demo (bin) (HLE and BIOS) lose
    their GPU escape and video stall; Music Demo (BIOS) goes from `? (pc_escape)` to game
    code; Jaguar Server 50 Hz and 60 Hz and Painter (bin) (BIOS) lose `inframe_hang`.
  - Flip Out and Fight For Your Life (1996) do not show as improved: the old matrix already
    scored them as game code (Flip Out's HLE `ILLEGAL` loop runs in cartridge space, and the
    old BIOS rows counted the boot-ROM intro).
- **CD corpus: 28 of 28 discs** reach game code in both modes, no row changed.
- The matrices record a stage plus coarse video and audio flags; they cannot see most
  rendering or audio changes. The cart matrix runs the Fast blitter only.
- **`make TEST_EXPORTS=1 test` exits 0** on the corpus machine with the private ROMs, and the
  audio pair actually ran: Skyhammer 0.000% saturated (RMS 3083.3), Iron Soldier 2 0.000%
  (RMS 2598.3), Iron Soldier 1 presence in envelope, Club Drive HLE presence and clipping
  clean (RMS 2948.6). Four checks were skipped for missing optional inputs (UDP broadcast
  between local processes, a live Fountain ROM, a CHSE-capable `chdman`, a Red Book disc).

---

## Developer and CI

- **Release gate (https://github.com/libretro/virtualjaguar-libretro/issues/749, https://github.com/libretro/virtualjaguar-libretro/pull/846).** `test/tools/matrix_diff.py OLD.md NEW.md` compares two boot
  matrices and exits 1 if any row moved backward or gained a crash-watchdog signature.
  `docs/release-process.md` makes it a pre-tag step. `test/test_matrix_diff.sh` (43 checks)
  runs in `make test`.
- **BIOS scoring (https://github.com/libretro/virtualjaguar-libretro/pull/852, https://github.com/libretro/virtualjaguar-libretro/pull/854).** `cart_boot_probe` finds the boot-ROM handoff itself
  (`--post-handoff N`); classification moved to `test/tools/cart_classify.sh`, which adds
  `BIOS_REJECT` and `? (bios_trap)`.
- **Harness:** `--input-bitmasks` exercises the joypad bitmask path RetroArch uses (https://github.com/libretro/virtualjaguar-libretro/pull/857). It
  came out of re-triaging the FMV report https://github.com/libretro/virtualjaguar-libretro/issues/737, where no core defect was found.
- **Netlink tooling:** `netlink_sim` (a deterministic Doom-link simulator),
  `netlink_btrace_report.py`, and jitter, spike and loss options for `netlink_delay_proxy`.
- New and extended tests: `test_jaggd_fs`, `test_jgd` file API, `test_titledb`,
  `test_pertitle_db` 13-17, `test_disk_control` 9-10, `test_option_explain`,
  `test_option_visibility`, `test_cart_needs_bios` mid-session arm, `test_dsp_unit` 17-21,
  `test_music_demo_dsp_bank`, `test_gpu_ops` retire-window and alignment cases,
  `test_cart_format` mirror and tie-break cases, `fb_flicker_probe`, and ROM-gated
  `test_flipout_851`, `test_ffyl_856` and `test_ruiner_866`.
- `test_jaggd_fs` builds as gnu99 and the SD-root join is bounded (https://github.com/libretro/virtualjaguar-libretro/pull/845).

---

## Known issues

- **Verified headlessly only, not yet in RetroArch:** Flip Out (https://github.com/libretro/virtualjaguar-libretro/pull/855), Fight For Your Life
  BIOS (https://github.com/libretro/virtualjaguar-libretro/pull/868), White Men Can't Jump tearing and intro logo (https://github.com/libretro/virtualjaguar-libretro/pull/870), Ruiner Pinball (https://github.com/libretro/virtualjaguar-libretro/pull/873),
  Native Demo (bin) (https://github.com/libretro/virtualjaguar-libretro/pull/872), the GPU retire window (https://github.com/libretro/virtualjaguar-libretro/pull/869), the BIOS/PAL latching notice
  (https://github.com/libretro/virtualjaguar-libretro/pull/863), the frontend preset notices and option hiding (https://github.com/libretro/virtualjaguar-libretro/pull/847, https://github.com/libretro/virtualjaguar-libretro/pull/859) and the GameDrive SD
  card (https://github.com/libretro/virtualjaguar-libretro/pull/844). https://github.com/libretro/virtualjaguar-libretro/pull/871's netplay gains are simulator numbers.
- **Doom netplay over Wi-Fi** stays slower than single player; epic https://github.com/libretro/virtualjaguar-libretro/pull/864.
- **White Men Can't Jump** still flickers somewhat, less than before (https://github.com/libretro/virtualjaguar-libretro/issues/736).
- **Ruiner Pinball** no longer crashes its GPU; whether it plays correctly is unconfirmed.
- Still open in this milestone: the HLE-vs-BIOS asymmetry list (https://github.com/libretro/virtualjaguar-libretro/issues/744), I-War needing
  Hardware Timing options (https://github.com/libretro/virtualjaguar-libretro/issues/634) and Checkered Flag's frame cap (https://github.com/libretro/virtualjaguar-libretro/issues/656).
- **Closed without a code change:** the FMV interactive-scenes report (https://github.com/libretro/virtualjaguar-libretro/issues/737). Input works
  in both boot modes with RetroArch defaults; Space Ace's scenes need the right direction
  within about half a second.

---

## Upgrading

- **Savestates:** v16 and older load; v17 states do not load in older builds.
- **New option:** Jaguar GameDrive SD Card (default Read/write; creates `jaggd-sd` in the
  save directory when GameDrive software uses it).
- **New value:** Blitter Bus Timing "68000 only". Existing values keep their meaning.
- **BIOS (Cartridges)** and **PAL** now change only on the next content load.
- **BIOS-mode power-on RAM** is zero-filled, as in HLE.

## Downloads

Pre-built libretro cores are attached to the GitHub release, built by `release.yml` (24
targets). Each binary has a matching debug archive, and SHA256 checksums are in
`SHA256SUMS.txt`. Every push to `develop` publishes a nightly pre-release:
https://github.com/libretro/virtualjaguar-libretro/releases/tag/nightly

## Maintainers

Joseph Mattiello, with the Virtual Jaguar libretro contributors. Original Virtual Jaguar
by David Raingeard (Potato Emulation) and James Hammons.
