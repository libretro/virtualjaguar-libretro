# JaguarDemos emulator corpus

On-demand boot suite driven by [42Bastian/JaguarDemos](https://codeberg.org/42Bastian/JaguarDemos)
(public-domain homebrew; any use of the source in whole or in part shall be
credited — see that repo's `LICENSE`).

This tree holds the **pin, ROM lists, baseline, and runner**. The clone itself
lives at `test/vendor/JaguarDemos/` (gitignored). That is a `git clone` of a
pinned SHA (`PIN`), not a git submodule: a submodule would bake ~23 MiB of
demo sources into every checkout of this core, including platforms that never
run the suite. Fetch is on demand (`make jaguar-demos-fetch` / CI).

## What it checks

Each selected `.j64` is run through `test/tools/cart_boot_probe` (same probe as
the private cart boot matrix). A ROM **PASS**es when it loads
and lights up the framebuffer within the frame budget. This is a boot + rendering
gate, not a screenshot golden or a "finished the demo" certificate.

BootIntros and GPU-only carts (`64/` `128/` `256/` `512/`, plus named
titles such as `yarc_reloaded`) are probed with `--bios` and PAL, matching
JaguarDemos' own `Rules.launch`.  Size-coded pads prefer `vj.j64` then
Series K (`*_K`) because the embedded cart boot ROM is Series K.
Remaining `[FAIL …]` rows in `BASELINE.txt` are accuracy gaps, not
"HLE never started".  Smoke never rewrites `BASELINE.txt`; use
`make jaguar-demos-baseline` (full sweep) when the PASS/FAIL set changes.

## Targets

```bash
make jaguar-demos-fetch          # clone/update to the pin in PIN
make jaguar-demos-build          # optional: rebuild with lyxass/rmac/rln
make jaguar-demos                # alias for smoke
make jaguar-demos-smoke          # curated smoke.list (PR CI)
make jaguar-demos-full           # every eligible committed .j64 (release CI)
make jaguar-demos-baseline       # rewrite BASELINE.txt from a full run
```

Requires a wide-export core: `make TEST_EXPORTS=1` (the targets build it).

## Jaguar toolchain (local rebuild only)

CI never assembles demos — it only runs the `.j64` files committed upstream.
Locally, `make jaguar-demos-build` walks every Makefile under the clone when
these are available:

- `lyxass` (GPU/DSP)
- `rmac` / `rln` (68000 + link)
- `BJL_ROOT` pointing at a BJL / new_bjl tree (padding / `allff.bin.bz2`)
- `jagcrypt` for some BootIntro encrypt steps

If any of those are missing, the build target prints a skip and exits 0; the
emulator suite still runs against committed ROMs.

## Refreshing the pin

1. Bump `SHA=` in `PIN` to the Codeberg commit you want.
2. `make jaguar-demos-fetch`
3. `make jaguar-demos-baseline` (or `JAGUAR_DEMOS_WRITE_BASELINE=1 make jaguar-demos-full`)
4. Review the `BASELINE.txt` diff and commit.

## CI

`.github/workflows/jaguar-demos.yml`:

- **Smoke** on PRs / pushes to `develop`+`master` (path-filtered).
- **Full** on `release/*` PRs, `v*` tags, and `workflow_dispatch`.

Regressions are PASS→FAIL only (same philosophy as `test/acid`).
