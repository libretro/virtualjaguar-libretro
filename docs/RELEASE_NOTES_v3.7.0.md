# Virtual Jaguar libretro v3.7.0

**Broken-games release. Most of the fixes are for Jaguar CD titles that froze, showed a
black screen or would not boot, plus two cartridge fixes, three blitter fixes and an
OP fix. HLE-mode CD titles now behave under run-ahead and rollback.**

v3.7.0 is the first of the two trains that follow the 2026-09 re-plan (epic #745,
milestone "v3.7.0 — Broken games"). It is about titles that did not boot, froze, or
showed a black screen. There is no performance work in this release and no new core
option. The savestate format moves from v14 to v15; new builds still load v14 states
(see Savestates).

Fourteen of the commits since v3.6.1 are by Adrien Beudin (@beudbeud), who found and
fixed most of the Jaguar CD problems below. GitHub only lets the author mark a fork's
draft PR ready, so most of those arrived through carrier PRs that merged the original
commits unchanged. Authorship is preserved on the commits.

---

## Highlights

- **Baldies** no longer freezes ~600 frames into its intro in the default HLE boot
  mode, and no longer wedges under `cd_boot_mode=bios` either (#738, #750, #752).
- **Myst** boots from CHD images, and gets past the Cyan logo in both boot modes
  (#754, #755, #756, #757).
- **World Tour Racing** gets past its title screen (#589, #759).
- **PolyEngine** (42Bastian's demo) boots and draws polygons (#785, #790, #791).
- **PlaySFX** and other headerless raw-binary homebrew load in HLE (#739, #805).
- **Vid Grid**, **Ocean Depths**, **Simone**, **Fast Food 64**, **Frogz 64** and
  **Saucer Wars** each have a fix (#760 through #765, with #762 for Vid Grid's video).
- **HLE CD titles are deterministic under run-ahead and rollback**, and a state saved
  in the middle of a CD load resumes the load (#787, #803).

---

## Bug fixes

### Jaguar CD titles

- **Baldies, HLE boot mode (#738, #750).** The cutscene froze about 600 frames in.
  The game locates each stream chunk by scanning for 16 consecutive aligned longs of a
  mastering marker. HLE's alignment scan returned on the first candidate it found, and
  that candidate was a run of `{` characters in the wrong sector at an odd
  misalignment, so the real marker was never examined. The scan now skips
  odd-misaligned candidates. The cutscene renders from frame 600 to past frame 1700
  where it used to be frozen. Iron Soldier 2, listed in the same issue, was already
  fixed in v3.6.1.
- **Baldies, BIOS boot mode (#752, #757).** It stopped progressing in the intro with
  the 68K parked in `STOP #$2000`. Fixed by the GPU/DSP sync ordering change below.
  Baldies (`.cue`, `.chd`, `.cdi`) is a PASS in BIOS mode in the CD boot matrix again.
- **Movie refills landing two bytes off (#758, #766).** When the HLE alignment scan
  found nothing, refills could land two bytes off the right position. HLE now
  remembers the byte phase of the last located sync run per track and reuses it when
  the scan misses. The fix is aimed at Hover Strike, Baldies, BrainDead 13 and
  Robinson's Requiem, the titles named in the issue. The PR reports Hover Strike's
  story crawl and credits and Baldies' cutscenes continuing.
- **World Tour Racing (#589, #759, #767).** The title screen froze. The game starts
  its title music with a `CD_read` whose end address is below its destination, which
  means "seek only". HLE treated it as an unknown-size read and copied 367 KB over the
  GPU program the game launches next. An end at or below the destination is now a seek
  with no transfer. Both the USA and Songbird releases ran 7200 frames (two minutes)
  without a freeze in the maintainer's re-measurement.
- **Myst from CHD images (#754, #755).** chdman counts a virtual pregap inside a
  track's `FRAMES` and stores it as zero frames at the head of the track. The CHD
  reader treated it as not stored, so every later track was read about 149 sectors
  late and the game showed a black screen. After the fix the first data sector of all
  ten tracks matches the CUE/BIN produced by `chdman extractcd`.
- **Vid Grid from CHD (#763, #771).** The same pregap handling also truncated the
  `ATRI` sync run at the head of a data track, so the Vid Grid USA and Alt CHDs
  retried their boot read forever. The core now rebuilds the missing bytes, only
  when the sector shows a truncated run followed by the header text. Vid Grid (USA),
  (Alt) and (Rev 1) CHDs boot to the intro and title in the PR's testing.
- **Ocean Depths and Simone boot headers (#764, #772).** Both were refused as
  zero-filled bad rips because the boot header sits one sector into the track. The
  loader now searches 16 sectors for the 32-byte boot magic instead of one, and still
  fails if a track has no header. Simone boots to its title in the PR's testing.
- **Ocean Depths TOC reads (#765, #773, #788).** The first `$03nn`/`$14nn` TOC word
  was delivered when the command was written, and a dummy data read consumed it. It is
  now armed after the DSA turnaround like other responses, and data reads made while
  bit 13 is low return the latched word without advancing. Ocean Depths reaches its
  main menu in the PR's testing, and the carrier PR reports its HLE run getting from
  frame 466 to frame 706 on develop. This is a savestate change; see below.
- **Homebrew CDs: Fast Food 64, Frogz 64, Saucer Wars (#760, #761, #768, #769).** Two
  separate faults hit these discs. HLE redirected the session-1 program read, and the
  68K crashed. A second fault: these discs overwrite the `$2404` NVM stub, so HLE
  served the call and then the disc's own module polled a missing flash for minutes.
  HLE no longer redirects a read whose session-1 LBA holds the D1 sync run, and the
  NVM hook returns itself when a disc has overwritten its stub at `$2404`.
  The effect only shows with both fixes. Fast Food 64 (and Holiday Snacks), Frogz 64
  and Saucer Wars reach their title screens with audio, and the Frogz 64 menu responds
  to input.
- **Space Ace intro (#756, #757).** The intro was frozen and silent. Same cause as
  Myst, below.

### Cartridge titles

- **PolyEngine and other 2-9 block jagcrypt carts (#784, #785).** PolyEngine's program
  is in an encrypted boot block and runs on the GPU from the real boot ROM. The core
  only accepted header bytes `0xFC` and `0xFE` (4 and 2 blocks), and PolyEngine's is
  `0xFD` (3 blocks), so it stayed on HLE, which jumped to vertex data and showed a
  black screen.
  Header bytes `0xF7` through `0xFE` (2 to 9 blocks) are now detected as GPU-only
  carts and take the real boot ROM. `0xF6`, the standard 10-block header, is
  deliberately not matched: it is on every commercial cart in the maintainer's corpus,
  several of which have entry opcodes the detector does not recognise (Hover Strike,
  Rayman, NBA Jam TE, White Men Can't Jump, Battle Sphere). A corpus sweep of 1272
  files found `polygon.j64` as the only file whose verdict changed. At default
  options it now shows 885 of 900 frames non-black and has audio.
- **PlaySFX and other headerless raw binaries (#739, #805).** PlaySFX V1.0 is a
  headerless RAM image linked at `$4000`. The loader's load-address guess counted only
  `LEA abs.L,A0`, but PlaySFX loads its library base into A6, so it scored below the
  threshold and was mapped as a cartridge at `$800000`, where the 68K ran zeroed RAM.
  The guess now counts `LEA abs.L` into any address register. PlaySFX loads at `$4000`
  and runs in HLE and BIOS modes, and with keypad 1 pressed it produces audio. Its
  video is black in the headless harness, which the PR notes is undetermined, so audio
  was the proof of life. An offline sweep of 155 ROMs found PlaySFX is the only file
  whose inferred load address changes.

### Rendering and blitter

- **PolyEngine on the Fast blitter (#786, #790).** With the Fast blitter, PolyEngine
  drew a noise floor and no polygons. Two bugs were both needed: with `DSTEN` clear,
  destination Z came from the DSTZ register instead of memory, even though `DSTENZ`
  enables that read independently (JTRM v8 p.73), and Z was addressed as 16 bits per pixel even when the Z buffer is
  cleared with a 32bpp phrase copy. At frame 500 the differences against the Accurate
  engine fall from 35.4% of pixels to 0.02% (23 pixels). The PR found no change on 12
  in-game savestates or on 900 frames of Hover Strike attract.
- **Computed-Z ordering in the Accurate (default) blitter (#789, #791).** The first
  pixel of every Gouraud-Z strip got seed + ZINC instead of the seed, so each Z value
  was one step ahead. The core now writes the seed first, per the jag_sim netlist and
  the JTRM worked example on pp.81-82. Stored depth values move by one step in titles
  that use computed Z (Cybermorph and I-War run hundreds of thousands of such blits).
  No displayed frame changed across 12 in-game savestates and 900 frames of Hover
  Strike; PolyEngine's Accurate frame changes by 23 pixels. With this and #790, Fast
  and Accurate agree on all 268,258 PolyEngine blits.
- **Fast blitter phrase-mode Gouraud lanes and Z saturation (#792, #793).** The Fast
  engine started every phrase-mode strip at the right-most pixel's lane regardless of
  x, and let Z wrap instead of saturating. Fast now picks the lane by pixel position
  and saturates Z. Fast-versus-Accurate per-blit disagreement on in-game savestates
  falls from 155,314 to 0 for Cybermorph and from 11,315 to 0 for I-War. Titles that
  already agreed (AvP, Doom, Super Burnout, Checkered Flag) still agree. Only Fast
  output changes.
- **OP branch and GPU objects on clamped lines (#762, #770).** When a game sets VDE
  past VP the OP runs every line, and the #632 fix clamps those passes to the visible
  window. That also dropped every non-bitmap object parked on a blanking line. Vid
  Grid branches to a GPU object only at line 518 and builds its movie bitmaps from that interrupt, so it showed a black screen over
  running movie audio. On clamped lines the OP now walks the list with bitmap and
  scaled objects skipped, so branch, GPU and stop objects still fire. The clamp's
  purpose, no late HEIGHT/DATA write-back on bitmap objects, is kept. Vid Grid's logo,
  "DANGER HIGH VOLTAGE" intro and title screen appear, and the Super Burnout race HUD
  that #632 fixed is intact.

### Timing and synchronisation

- **GPU/DSP sync ordering for 68K writes to local RAM (#752, #756, #757).** The
  catch-up that runs the GPU and DSP up to the 68K's current time ran after a 68K
  write to GPU/DSP local RAM landed, so the RISC ran the cycles before the write with
  the write already visible. A short GPU job could finish and raise its interrupt
  before the 68K reached its next instruction. Myst posts a job and then runs
  `stop #$2000` with only the GPU interrupt enabled, so it slept forever (black screen
  after the Cyan logo); Space Ace's intro and Baldies in BIOS mode failed the same
  way. The catch-up now runs before the write, which keeps the earlier #138 guarantee
  that each later write still flushes the GPU first. Two follow-up commits keep the
  8-, 16- and 32-bit write paths in the same order and put the `vjtrace` watch record
  after the sync, so traces list events in the right order. Measured in the CD boot
  matrix: Myst (USA) `.cue` in BIOS mode FAIL to PASS, Myst and Space Ace in HLE no
  longer stuck in a loop, and Space Ace HLE runs 3000 frames where it used to freeze
  at frame 561. The PR reports Pitfall, Doom and Super Burnout unchanged.

---

## Savestates

The format is now **v15**. Two changes share the single bump this release carries:
`dsaLastMultiWord` for the TOC fix (#773), and the HLE CD streaming state (#803).

- **New builds load older states.** v3.6.1's v14 states load and run byte-identically
  to v3.6.1 loading its own state. This was measured for Hover Strike and Myst in
  HLE mode, comparing per-frame framebuffer hashes, with the state saved at frame 400.
- **Older builds cannot load v15 states.**
- **HLE-mode CD titles are deterministic under run-ahead and rollback (#787, #803).**
  The HLE CD path streamed data into RAM from statics in `jagcd_hle.c` that were not
  in the state, so a run-ahead reload leaked speculative progress into the real
  timeline. Before the change, `test_runahead_determinism` failed for Hover Strike HLE
  at warmup 200, 400, 700 and 1000, and for Myst HLE at 200 and 400. After it, all
  eight HLE runs pass all four checks (video identical, audio identical, state
  reconverges, repeated rollback agrees).
- **A state saved in the middle of a CD load now resumes the load.** Hover Strike
  gives 200 distinct frames after loading such a state; without the new chunk it is a
  frozen black screen.
- **Caveat.** A state saved by an older build while a CD read was in flight cannot
  resume that read, because the data was never saved. v3.6.1 cannot resume it either
  (black screen loading its own save), so nothing regresses.
- The new state chunk is appended last behind a magic word. v15 states made by
  develop builds before #803 load fine but any in-flight transfer is dropped, as it
  was before.
- **Not covered.** Hover Strike with the real BIOS (`cd_boot_mode=bios`) still shows a
  smaller run-ahead divergence at warmup 400 (video and state, not audio) that is
  gone by warmup 1000. That is tracked as #804.

---

## Tooling and diagnostics

For people who run the test harness or read the crash watchdog logs.

- **`cd_seek_wedge` false positives removed (#741, #798).** The watchdog counted every
  frame in which the FIFO drain did not move, so under `cd_boot_mode=bios` it fired
  15 times across Baldies, Battle Morph, BrainDead 13, Highlander, Primal Rage, Myst
  and both Myst demos, all of which went on to run because their transfer had simply
  finished. It now counts a frame only while a seek is outstanding, or while the
  transfer is still open (BUTCH bits 0 and 1 armed and I2CNTRL bit 2 set). The log
  line also prints `butch_int=` and `i2s_ctrl=`. The signature still fires on the two
  real wedges the PR reproduced as controls (Primal Rage and BrainDead 13 with the
  delay-slot fix disabled).
- **New `inframe_hang` signature (#740, #801).** The blitter runs synchronously inside
  the `B_CMD` register write, so a blit that never ends stops `retro_run` from
  returning and no per-frame check can see it. Before either engine runs a blit, the
  watchdog now checks `B_COUNT` and logs `inframe_hang` if the blit is larger than
  2^24 pixels, then runs it as before. It is log-only, latched, and gated on
  `virtualjaguar_crash_detect`. Pixels are counted the way the Accurate engine runs
  them (outer count 0 means 65536 lines; inner count 0 means one step). Across the
  maintainer's corpus there were no hits on commercial titles; the hits are homebrew
  or public-domain carts.
- **Harness rejects unknown core options (#742, #777).** `--option KEY=VALUE` used to
  be silently ignored when the core never registered the key, so a typo measured the
  defaults and reported a confident null result. An unregistered key now exits 2 and
  names the key. `VJ_HARNESS_ALLOW_UNKNOWN_OPTIONS=1` turns the error into a warning
  for A/B runs against an older core. `--option` without `=` and options past the
  limit of 32 are also fatal now, and the harness prints every option it applied. The
  audit found one stale key, `virtualjaguar_dsp`, used in the vj-debug skill's
  A/B example; it now uses `virtualjaguar_usefastblitter`.
- **Version-string checker covers all three copies (#734, #795).**
  `scripts/check-info-version.sh` compared two of the three places the version lives.
  It now also checks `src/core/version_fallback.h` and names the file that drifted.
- **Clang warning silenced (#797).** `make platform=ios-arm64` no longer emits a
  `-Wtypedef-redefinition` warning from the vendored libchdr/miniz headers.
- **Site footer (#751).** The project site footer links the Provenance family of sites
  and declares the publisher in its JSON-LD.

---

## Performance

No performance changes in this release. For the one change that touches a per-blit
path, `inframe_hang`, an instruction-count A/B on Alien vs Predator over 300 frames
measured -0.016% Ir with byte-identical output.

---

## Testing

- **Boot matrices regenerated (#735, #778).** The cart matrix (155 titles in HLE and
  BIOS, 600 frames) and the CD matrix (3000 frames) were re-run for the first time
  since 2026-08-13. A Myst (USA) `.chd` row was added alongside the `.cue` row, with
  identical results (#775).
- **Synthetic virtual-pregap CHD fixture (#774, #776).** Every earlier CHD fixture had
  no pregap, which is how #755 shipped. `test/roms/synth_jagcd_vpregap.chd` has
  per-track pregaps of 2, 3 and 5 sectors, and `test_cd_chd` checks that each track's
  INDEX 01 lands at the right MSF and every data sector reads its own tag. On the
  build before #755 it fails with silence where track 3 should begin.
- **Baldies HLE freeze detector (#750).** `cd_wedge_probe` now runs from `make test`
  on Baldies in HLE for 1200 frames. The boot classifier passed Baldies while it was
  frozen, so the cover is a freeze check. It exits 42 on the pre-fix core. It needs
  the private disc corpus and is recorded as a skip elsewhere.
- **Watchdog tests (#798, #801).** `test_crash_detect_cd_wedge` and
  `test_crash_detect_inframe` run in `make test` and pin the measured values, so the
  signatures cannot silently go blind or noisy again. A private-corpus regression
  script, `cd_seek_wedge_regress.sh`, asserts that no `cd_seek_wedge` line appears.
- **Blitter vectors (#790, #791, #793).** `test_blitter_cmd` gained
  `dstenz_without_dsten`, `z_clear_32bpp`, `gourz_seed_first` and `jtrm_gourz_strip`.
  Expected bytes are derived from the JTRM, not recorded from output, and each fails
  on the code before its fix. `jtrm_gourz_strip` runs under both engines.
- **Loader and jagcrypt tests (#785, #805).** `test_cart_needs_bios` gained
  `fd_jagcrypt_2mib` and `f6_commercial_unlisted_op`; `test_cart_format` gained
  `raw_binary_lea_any_register_loads_at_4000`.
- **HLE run-ahead check (#803).** `make test` gains a Hover Strike HLE run-ahead
  determinism check at warmup 400. It skips without the private discs, and exits 1 on
  the build before #803.
- **Disk-control test (#733, #802).** The framebuffer pitch guard now also rejects a
  misaligned pitch or base pointer, and the usage text says which cases need `--disc`.

---

## Known issues

Deferred to v3.7.1 ("Broken games, continued"), plus two issues found while
preparing this release.

- **Dragon's Lair, Space Ace, BrainDead 13: interactive scenes do not load (#737).**
  The FMV titles play their attract loop and intro but not their gameplay scenes.
  Not started; v3.7.0's CD boot and TOC fixes are groundwork for it.
- **White Men Can't Jump is still very glitchy (#736).** The menu freeze and audio
  loss were fixed earlier (#635); what remains is a separate symptom that needs its
  own DSP investigation.
- **Club Drive freezes in gameplay at default settings (#611).** The GPU runs away
  into main RAM. The freeze is reported after holding the accelerator for a few
  seconds, and it is not caused by the experimental timing options. Not yet
  root-caused; it needs a reproduction with driving input.
- **Alien vs Predator: high-pitched noise during gameplay (#633).** Reported on
  Windows, Linux and Android, not reproduced on macOS or iOS. Blocked on details of the
  reporter's platform.
- **Music Demo (2002), BIOS mode (#794).** The GPU wedges at frame 338, and with the
  Accurate blitter a single blit never returns. The new `inframe_hang` log points at
  the 68K writing garbage into the blitter registers at frame 158, so the root cause
  is not found.
- **Myst CHD sign-off (#774).** The fixture and matrix rows are done; the remaining
  item is the reporter's RPi 5 check on a release build, which needs v3.7.0 to exist.
- **Some homebrew carts freeze in HLE with the default Accurate blitter (#800).** Seven
  carts do: Chroma-Luma Color Pick (bin), DEMO1 (bin), DEMO1B (two dumps), JagMania
  (Jul 8), Ladybug Demo and Ladybug Demo (rom). Each is stuck inside one blit. All are
  homebrew or public-domain; no commercial title was affected. The cart boot matrix
  runs only the Fast blitter, so it did not see them. DEMO1 runs 600 frames
  in about a second under Fast, which the issue attributes to Fast treating a zero
  outer count as zero lines.
- **Real-BIOS CD run-ahead divergence (#804).** With `cd_boot_mode=bios`, Hover Strike
  at warmup 400 still diverges under run-ahead (video and state; audio matches). It
  is gone by warmup 1000.

Carried over from v3.6.1 (not re-checked for this release): inserting a disc restarts
the console, and the enhancement profile governs `internal_resolution` and `true_color`
only.

---

## Upgrading

- **Savestates:** v14 states load; v15 states do not load in older builds. States
  saved by an older build during a CD read cannot resume that read.
- **CHD images:** CHDs with virtual pregaps (Myst USA, Vid Grid) now read correctly.

---

## Stats

```
git diff --shortstat v3.6.1..v3.7.0
55 files changed, 2941 insertions(+), 379 deletions(-)
```

73 commits, 44 of them non-merge: 14 by Adrien Beudin, 30 by Joseph Mattiello
(including this release commit). 18 of the changed files are under `src/`
(+848 / -138) and 16 under `test/` (+1196 / -18).

---

## Downloads

Pre-built libretro cores are attached to the GitHub release and built by
`release.yml` (earlier notes say "16 platforms"; the v3.6.1 release carried 23 core
builds plus tools):

- Linux: x86_64, aarch64, i686, and Raspberry Pi 1 to 5 (armv6, Cortex-A7, A53, A72,
  A76, with 64-bit variants where applicable)
- macOS: arm64, x86_64
- Windows: x86_64, i686 (MSYS2/MinGW)
- iOS: arm64; tvOS: arm64
- Android: arm64-v8a, armeabi-v7a, x86_64, x86
- Web: Emscripten WASM
- Consoles: PS Vita, Nintendo Switch

Each binary has a matching `*-debug.tar.gz` with split debug symbols. SHA256 checksums
are in `SHA256SUMS.txt`. The `jagcd-tools` bundles and `cue2cdi` binaries ship
alongside.

Every push to `develop` publishes a **nightly** pre-release (tag `nightly`) with the
same assets: https://github.com/libretro/virtualjaguar-libretro/releases/tag/nightly

---

## Maintainers

Joseph Mattiello, with the Virtual Jaguar libretro contributors, in particular Adrien
Beudin (@beudbeud) for the Jaguar CD fixes above. Original Virtual Jaguar by David
Raingeard (Potato Emulation) and James Hammons.
