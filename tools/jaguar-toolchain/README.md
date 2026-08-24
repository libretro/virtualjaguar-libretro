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

Once built, `test/jaguar-demos/run.sh`'s `have_jag_toolchain()` finds
this automatically — `make jaguar-demos-build` starts actually building
demos from source instead of skipping.

## Sources and licenses

| tool | source | license |
|---|---|---|
| rmac / rln | github.com/mwenge/rmac, tiddly.mooo.com/rln | no LICENSE file found upstream |
| lyxass | github.com/42Bastian/lyxass | MIT |
| pc_jagcrypt | github.com/cubanismo/pc_jagcrypt | no LICENSE file found upstream |
| new_bjl | github.com/42Bastian/new_bjl | Unlicense (public domain) -- **credit 42Bastian on any use** |

## Refreshing the pin

1. Bump `SHA=` in `PIN` for the tool you're updating.
2. `make jaguar-toolchain-fetch && make jaguar-toolchain-build`
3. Confirm the built binaries still run (`rmac -v`, `lyxass`, `rln -v`).
4. Commit the `PIN` change.
