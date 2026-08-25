# Jaguar assembler toolchain

On-demand fetch + build for the Jaguar cross-development toolchain this
repo's test suite can use: `rmac`/`rln` (68000 assembler + linker),
`lyxass` (GPU/DSP assembler), `pc_jagcrypt` (BootIntro encrypt), and
`new_bjl` (BJL_ROOT — loader stub binaries + 68K includes some
JaguarDemos Makefiles reference).

None of these are vendored into this repo's git history — `setup.sh`
clones each from its pinned source in `PIN` into a gitignored
`tools/vendor/jaguar-toolchain/`, matching how `test/jaguar-demos/`
already handles its own on-demand clone.

## Usage

```bash
make jaguar-toolchain-build   # fetch + build everything
eval "$(tools/jaguar-toolchain/setup.sh env)"   # add to PATH, set BJL_ROOT
```

`smoke/hello68k.s` and `smoke/hellogpu.s` are the minimal
does-the-toolchain-work sources CI assembles on every change here (see
`.github/workflows/jaguar-toolchain.yml`). Their header comments record
the non-obvious syntax each assembler actually requires — notably that
rmac rejects `.org` in a 68K section (the address goes on `rln -a`) and
that lyxass emits **nothing** for code placed before its `.run`
directive, while still exiting 0. Read them before writing new Jaguar
assembly here.

Once built, `test/jaguar-demos/run.sh`'s `have_jag_toolchain()` finds
this automatically — `make jaguar-demos-build` starts actually building
demos from source instead of skipping. Toolchain discovery and the
rmac/rln/lyxass build path are proven working end-to-end; most
*individual* demo builds still fail downstream of that, for a reason
unrelated to this setup — see the license/build-gap note below.

## Sources and licenses

| tool | source | license |
|---|---|---|
| rmac / rln | github.com/mwenge/rmac, tiddly.mooo.com/rln | no LICENSE file found upstream |
| lyxass | github.com/42Bastian/lyxass | MIT |
| pc_jagcrypt | github.com/cubanismo/pc_jagcrypt | no LICENSE file found upstream |
| new_bjl | github.com/42Bastian/new_bjl | Unlicense (public domain) -- **credit 42Bastian on any use** |

**BootIntro-style demos and the `jagcrypt -tursi` gap:** 38 of the 54
JaguarDemos Makefiles invoke `jagcrypt -q -u -tursi` as a build step, and
the pinned `cubanismo/pc_jagcrypt` (verified against its real upstream
history) has never supported a `-tursi` flag. Those demos' individual
builds fail downstream of this toolchain for that reason -- a real gap in
the pinned `pc_jagcrypt` upstream, not a bug in the fetch/build/discovery
wiring here. Demos that only need `rmac`/`rln`/`lyxass` (no `jagcrypt
-tursi` step), and any future microbenchmark ROMs built for #536, are
unaffected.

## Refreshing the pin

1. Bump `SHA=` in `PIN` for the tool you're updating.
2. `make jaguar-toolchain-fetch && make jaguar-toolchain-build`
3. Confirm the built binaries still run (`rmac -v`, `lyxass`, `rln -v`,
   `jagcrypt` -- pc_jagcrypt's built binary is named `jagcrypt`, not
   `pc_jagcrypt`).
4. Commit the `PIN` change.
