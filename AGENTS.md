# AGENTS.md

Virtual Jaguar libretro core — Atari Jaguar / Jaguar CD emulator (C, GPLv3).

The authoritative developer guide is [`CLAUDE.md`](CLAUDE.md) (build system, C89
rules, hardware model, the full test/harness/probe catalog, release process).
Read it before doing non-trivial work. This file only records the few
cloud-environment specifics not covered there.

## Cursor Cloud specific instructions

The cloud VM is **Linux (Ubuntu 24.04)**, so the macOS-only notes in `CLAUDE.md`
(interactive `rm`/`cp`/`mv` aliases, `DEVELOPER_DIR`/Xcode) do **not** apply
here — plain `rm`/`cp`/`mv` are non-interactive and `make`/`cc` resolve to the
system GCC/Clang.

### Toolchain / dependencies

- No package-manager dependencies to install: `libretro-common/` is vendored and
  the core links only libc (`-ldl`/`-lm` for test harnesses). Requirements are
  just the system C/C++ toolchain (`gcc`/`g++`/`make`) plus `python3` and `git`,
  all preinstalled and ensured by the startup update script. The core build needs
  **no** SDL2, OpenGL, or pkg-config.

### Build / lint / test (see `CLAUDE.md` for the full matrix)

- Build: `make -j$(getconf _NPROCESSORS_ONLN)` → `virtualjaguar_libretro.so`.
- Lint: `make lint` (C89/MSVC gate). The NEON header check is skipped on this
  host (`clang` can't target NEON here) — CI covers it; that SKIP is expected.
- Test: `make test` — full white-box suite. Non-obvious: it silently rebuilds the
  whole tree with `TEST_EXPORTS=1` (wide ABI + `-DVJ_TRACE`), and flipping back to
  a plain `make` triggers another full rebuild (~2 min each). See the "Test ABI
  and re-linking" section of `CLAUDE.md`.
- `make test` reports **~15 skipped checks** here (Iron Soldier / Atari Karts /
  Skyhammer / Doom / CD discs, etc.). These are **not failures** — they need the
  private ROM corpus, which is absent in the cloud VM (`test/roms/private` is an
  external symlink that does not exist here). A clean run is `0 failed`.

### Running the emulator (there is no GUI app)

This is a libretro core, not a standalone app. To run it headless:

- `./test/regression_test.sh ./virtualjaguar_libretro.so` clones+builds
  `miniretro` from GitHub (**needs network**) and runs the two committed public
  ROMs `test/roms/yarc.j64` and `test/roms/jagniccc.j64`. Baselines in
  `test/baselines/` are committed and should match ("PASS ... identical"); it
  also exercises determinism, frameskip, savestate and rewind.
- Set `MINIRETRO_BIN=/path/to/miniretro-bin` to skip the clone/build.
- `imagemagick` is not installed; regression comparison falls back to `cmp` and
  dumped screenshots are plain PNG (viewable directly), so the diff-image path is
  the only thing lost.
- A direct one-ROM run for a quick screenshot:
  `miniretro --core ./virtualjaguar_libretro.so --rom test/roms/yarc.j64 --output OUT --system OUT --frames 600 --dump-frames-every 599 --no-alarm`
