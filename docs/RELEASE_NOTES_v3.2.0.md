# Virtual Jaguar libretro v3.2.0

Enhancement-suite release: the first two visible pieces of the modern-emulation
track (#338) — full-precision gouraud rendering and 2x internal resolution —
plus a cluster of game-fix work: Raiden's two bugs, Power Drive Rally's
mid-game sound freeze, and damaged CDI V2 rips now booting.

## Highlights

- **True Color gouraud rendering** (#341). New core option
  `virtualjaguar_true_color` renders gouraud-shaded pixels at full precision
  (chroma × 24-bit intensity) through a shadow framebuffer, removing shading
  banding in 3D titles. The game-visible 16-bit framebuffer is unchanged —
  savestates, achievements, and emulation behaviour are bit-identical.
  CRY 16bpp video modes only. Default off.
- **2x internal resolution** (#351, #353, #359). New core option
  `virtualjaguar_internal_resolution` (`1x`/`2x`, restart required) renders
  internally at a multiple of native resolution — and qualifying blits are
  supersampled with real sub-pixel content (fractional-walk source sampling,
  the "Stage 2" of the design), so 3D titles gain genuine detail; anything
  that doesn't qualify falls back to exact 2×2 box replication. The emulated
  hash gate, and savestate digests prove the 1x path is bit-identical. Combines
  the release as box-replication-only with supersampling "in progress"; the
  `104ee5d` when #359 was squash-merged on top of the Stage 2 branch. The
- **OP shadow-resolve hit/miss counters** (#362) make the supersampled
  path's silent-fallback failure mode diagnosable at a glance (verbose
  crash-detect heartbeat), with misses split by cause.

## Bug fixes

- **Power Drive Rally: sound froze on a constant tone mid-game** (#355, #363,
  #365). GPU/DSP local RAM is a 16-bit port that hardware commits as ordered
  longword pairs (JTRM TR p.44/p.101); we committed each half-write
  immediately, so a lone 68K half-write corrupted a DSP voice-state longword
  and sent the mixer off a null dispatch pointer. Writes now latch and commit
  on the partner half, matching the documented port semantics (and the
  documented `clr.l`/`move.l <ea>,-(An)` erratum that latch implies).
- **Raiden: saved-options corruption killed the music** (#344). EEPROM
  data-out was sampled destructively at `$F14000`, so ISR joystick polls
  mid-READ stole EEPROM bits and corrupted the settings the game then saved.
  Reads no longer shift the transaction; existing corrupted `.srm` files heal
  on the next in-game save.
- **Raiden: black screen on HLE fast-boot** (#20, #70, #339). The HLE path now
  programs the vertical-interrupt registers exactly like the real boot ROM,
  so titles that trust the boot-time VI state get the hardware values.
- **Damaged CDI V2 rips boot** (#342). Four known V2 rips are missing the
  boot header itself; a per-track repair reconstructs it at load time, and
  intact rips are untouched.
- **Bus latencies are charged in wall time under clock scales** (#318, #337).
  Overclocking the 68K or RISC domains no longer implicitly speeds up
  DRAM/bus costs — the cycle-domain contract from v3.1.0's clock-scale work
  is now enforced everywhere, with `busArbiter` exported to the test ABI.

## Testing / diagnostics

- **`audio_timeline`** (#358) — unbounded per-window audio + DSP state
  timeline; existing harnesses stopped collecting at 20 s, exactly where the
  Power Drive Rally bug lived. This tool is what made #355 diagnosable.
- **CD boot-mode gotcha documented in `cd_visual_verify`** plus a real-BIOS
  FMV evidence table (#340).
- **jlink/netlink CI flake fixed** (#356) — tests hard-coded ports inside the
  ephemeral range and collided with the OS under load; suite-wide rule added.

## Docs / site

- The project site now serves at
  [jaguar.provenance-emu.com](https://jaguar.provenance-emu.com) (#343, #348,
  #350, #352) with an evidence-linked showcase, and the README was rewritten
  as the project's primary showcase surface (#345).
- Hi-res design review, corpus census, and Stage 2 on-screen verification are
  recorded under `docs/` (#347, #351, #359, #361, #364); GPU-compute offload
  exploration recorded in #349 (research only, no code).

## Savestates

- Format unchanged (v8). States from v3.1.0 load as-is, and every older
  released format continues to load via the v3.1.0 compatibility loader.

## Stats

- 75 files changed, ~9,785 insertions, ~278 deletions across 30 commits since
  v3.1.0 (`git diff --shortstat v3.1.0..develop`).

## Known issues

- Battle Morph (CD) runs too fast (#331) — most likely the unmodelled
  GPU/DSP pipeline hazards (#313), next up on the timing-accuracy epic
  (#319).
- Val D'Isere Skiing and Snowboarding ground visuals render as a static
  image (#354) — under investigation.
- See `docs/cd-known-issues.md` for the CD backlog.

## Downloads

Built for 16 platforms by CI on the `v3.2.0` tag.
