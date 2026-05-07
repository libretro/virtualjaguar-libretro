# Virtual Jaguar libretro v2.3.0

Performance and accuracy release — 158 commits since v2.2.0.  Major blitter
and RISC performance work, hardware accuracy fixes across TOM/JERRY/DSP, and
improved iOS/Provenance stability.  No new features; this is a polish release.

## Highlights

- Significant blitter performance improvements: collapsed inner loops, SIMD
  ADDARRAY, ADDRGEN caching, fast-path RAM bypass, branchless COMP_CTRL.
- GPU and DSP computed-goto dispatch + inlined delay slots — measurable
  speedup on all platforms.
- AvP alpha noise / red artifact fix (bkgwren + dstd read).
- PIT timer corrected to full 26.59 MHz system clock rate.
- TOM visible window now derived from hardware registers (VDB/VDE/HDB/HDE).
- DAC resamples I2S output when SCLK changes at runtime — fixes audio pitch
  drift in games that reconfigure the sound clock.
- iOS / Provenance stability: all statics properly reset on core unload/deinit.
- Expanded test suite: 56,500+ automated assertions, framebuffer integrity
  checks, EEPROM lifecycle, audio pipeline, and timing diagnostics.

## What's new

### Bug fixes

- **AvP**: fixed alpha noise / red visual artifacts — blitter now skips
  destination data read when BKGWREN is set (PR #166).
- **DSP**: don't kill DSP when it's actively producing audio — prevents
  audio dropouts in games that restart the DSP mid-frame.
- **DSP**: correct ABS flags, DIV zero-guard, STORE alignment (PR #162).
- **DAC**: resample I2S output when SCLK changes at runtime (PR #163) —
  fixes audio pitch issues in games that reconfigure the I2S clock.
- **TOM**: derive visible window from VDB/VDE/HDB/HDE registers (PR #164)
  — proper overscan handling instead of hardcoded dimensions.
- **TOM**: restore fixed left-edge origin for scanline positioning.
- **PIT**: correct timer clock rate to 26.59 MHz (full system clock). Was
  previously running at half rate, affecting game timing (PR #154).
- **Blitter**: restore 64-bit register longword swap in BlitterWriteByte.
- **Blitter**: always read framebuffer in phrase mode for byte_merge.
- **Blitter**: multiple correctness fixes in collapsed inner loop paths
  (daddmode, zmode, patfill eligibility guards).
- **Libretro**: reset all static state on unload/deinit with NULL checks
  (PR #160) — fixes crashes on iOS/Provenance when relaunching the core.
- **Libretro**: move externs into proper headers (PR #161).

### Performance

- **RISC (GPU/DSP)**: computed-goto opcode dispatch for GCC/Clang (PR #146).
- **RISC**: inline delay-slot execution (PR #147).
- **RISC**: predecode opcode cache (PR #144).
- **Blitter**: SIMD-accelerated ADDARRAY cascade (PR #148).
- **Blitter**: cache ADDRGEN y*width outside inner loop (PR #149).
- **Blitter**: inline ADDRGEN and helper functions (PR #152).
- **Blitter**: fast-path RAM reads/writes bypassing full address dispatch
  (PR #155).
- **Blitter**: collapsed inner loops for simple copy blits (PR #157) and
  pattern fill blits (PR #156).
- **Blitter**: branchless COMP_CTRL dbinh + Kogge-Stone maskt prefix
  (PR #158).
- **Blitter**: inlined fast-RAM helpers in collapsed paths.

### Testing

- Test suite expanded to 56,500+ automated assertions across 15 test
  binaries.
- New test harnesses: framebuffer integrity (alpha corruption + screen
  position), TOM visible window registers, EEPROM lifecycle (write/read/
  persist across reload), audio pipeline, PIT clock rate verification.
- Shared test harness library (`test/harness/`) with common CLI, JSON
  output, audio/video stats, and probe modules (DSP, timing).
- Per-frame timing diagnostic tool for detecting halfline/cycle anomalies.
- Visual regression tests: determinism, frameskip invariance, save-state
  round-trip, rewind simulation.

## Known issues (unchanged from v2.2.0)

- **Doom**: runs at approximately 2x speed (GPU-rendered game, bus
  contention not yet modeled). Game music silent.
- **Wolfenstein 3D**: no game music (DSP audio engine issue).
- **Skyhammer / Iron Soldier 2**: saturated square-wave audio on HLE
  (DSP engine state not fully replicated).
- **Battle Sphere**: menu navigation issues.
- Jaguar CD support in progress on a separate branch, not in this release.
- The accurate blitter is still notably slower than the fast blitter;
  some games may need the fast blitter on lower-end hardware.

## Compared to v2.2.0

263 files changed, +25,240 / -2,805 lines across 158 commits.

## Downloads

Pre-built libretro cores for 16 platforms:

- Linux: x86_64, aarch64, i686
- macOS: arm64, x86_64
- Windows: x86_64, i686 (MSYS2/MinGW)
- iOS: arm64; tvOS: arm64
- Android: arm64-v8a, armeabi-v7a, x86_64, x86
- Web: Emscripten WASM
- Consoles: PS Vita, Nintendo Switch

Each binary has a matching `*-debug.tar.gz` with split debug symbols.
SHA256 checksums in `SHA256SUMS.txt`.

## Maintainers

libretro/Provenance fork: Joseph Mattiello (@JoeMatt, Provenance-Emu).
