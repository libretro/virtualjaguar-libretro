# Community cheat source converter

Translate a limited subset of user-authored C cheat sources into the app's
existing `.cht` memory freezes. This is an offline, static source converter:
it does not execute or compile the input, follow includes, contact an emulator,
or require an SDK. Python 3.10+ and `pycparser` are needed only for conversion;
the app and libretro core gain no Python dependency or script VM.
`requirements.txt` pins `pycparser` exactly (3.11, the newest release that supports
Python 3.10, the oldest version CI runs). A `pycparser` bump is a deliberate PR: regenerate
`site/cheats` with the new version and check the output stays byte-identical. 2.22, 2.23,
3.0, 3.11 and 3.12 were verified to produce identical output.

Packs built from this converter by CI are published at `jaguar.provenance-emu.com/cheats/`
(`site/cheats/`) and installed from the app's Cheats window under "Community cheats": Install,
Update and Remove work on the loaded cartridge's pack by CRC32 (the app verifies size, SHA-256 and the
revision guard before keeping it). Using a `.cht` by hand, as described below, still works and is
independent of the store.

## Use

Use sources you have permission to translate and a matching native cartridge
dump. Review the author's provenance, including credits to other authors,
before choosing a source. Do not supply compiled scripts, bundled proprietary
scripts, SDK headers, or emulator binaries.

```sh
python3 -m venv /tmp/vj-cheat-import-env
/tmp/vj-cheat-import-env/bin/pip install -r tools/cheat-import/requirements.txt
/tmp/vj-cheat-import-env/bin/python tools/cheat-import/convert.py community-cheat.c \
  --rom '/path/to/matching-cart.j64' \
  --output '/path/to/cheats/Game Title.cht' \
  --report '/path/to/conversion-report.json'
```

All output files must be new. The converter creates a JSON report with source
SHA256, ROM CRC32/FNV1a64, and the result for each setting. It emits no ROM bytes.
The source's load callback must accept the supplied ROM hash and each write
must be controlled by that hash and a boolean setting. A wrong revision
produces no active cheats.

By default, any unsupported setting prevents `.cht` output (exit 1); the JSON
report explains the omissions. Add `--allow-partial` to export only complete
supported toggles. A toggle with one unsupported operation is omitted in full.
Parse errors, unsupported module structure, interacting toggles, and file errors
fail conversion (exit 2). Those structural failures are printed to stderr and
may occur before a report is created. Success is exit 0.

Put the `.cht` in the app's cheat directory or pass its directory with
`--cheats-dir`. Select it in **Cheats** and enable individual entries. Choose a
filename matching the game title for automatic set selection. Source default
settings never auto-enable entries. The existing per-game persistence, atomic
cheat updates, 256-write limit, and RetroAchievements hardcore block apply.

Generated files contain `vj_rom_crc32 = "XXXXXXXX"`. The Virtual Jaguar app
blocks the whole set when the loaded cartridge's CRC differs, is unknown, or
the guard is malformed/duplicated. Legacy files without this field still work.
**RetroArch does not enforce this app-specific guard**; only use converted
codes with the exact matching cartridge there. Keep the JSON report with the
file. The guard prevents accidental revision mismatches; it is not an
authenticity signature. Do not remove it to make a different dump pass.

## Supported subset

- One category, boolean and bounded integer settings, a software-load callback,
  and an emulation frame callback, using the registration/call names present in the author's
  community C sources.
- Constant byte, word, and long writes to the 2 MiB main RAM, including grouped
  writes and bounded integer loops. Word/long addresses must be even.
- Integer constants, object-like macros, enums, scalar locals, explicit integer
  casts, simple arithmetic/bitwise expressions and conditions. Fixed-width C
  integer promotion and unsigned wrapping are modeled; signed overflow fails.
- Initialized global one-dimensional `const` integer tables, with constant
  expressions for the size and entries (1024 elements total per evaluation).
  An omitted size is inferred from the initializer, and missing trailing entries
  are zero-filled. Indexing preserves integer types and setting dependencies;
  negative and out-of-range indices are refused. Pointers, mutations, local or
  multidimensional arrays, strings, and designated initializers remain unsupported.
- Top-level `const` scalar integer locals in `bigp_init`, initialized from pure
  integer expressions, macros, enums, globals, or earlier constant locals. Integer
  setting registrations see only declarations that precede them. The locals are
  confined to metadata evaluation and cannot shadow other identifiers or depend
  on registration handles. Mutations, nested declarations, static locals, and
  function calls in constant initializers remain unsupported. All declarations,
  including unused ones, are evaluated under the existing execution budget.
- Comma-separated statement lists, including loop updates such as `++i, --dest`,
  evaluated left to right under the existing step and persistent-state checks.
- `bigpemu_register_setting_int_full` with constant default, inclusive minimum/maximum
  and positive step, spanning at most 64 values. The default must be in range and
  on the step grid. Macros and enums can supply the bounds. Each value above the
  minimum becomes a disabled entry named `Setting: value`, including the default
  if applicable. The minimum supplies the off value and must have no memory
  effects with all cheats off. The source default never auto-enables a freeze.
- At most ten original settings and 4096 off/value combinations, with every
  combination checked for equivalent final byte writes. Integer choices must
  write the same set of bytes, so enabling several choices leaves the last
  enabled value in frontend order. Choose one value per setting. Cheats run as
  an ordered group per toggle; sources whose combined behavior cannot be preserved are refused.

RAM reads, copy/conditional/masked cheats, unbounded or larger integer settings, helper
functions, pointers and other arrays, static local state, frame-to-frame state changes,
extra events, early returns, general preprocessing, and other unsupported C
syntax need a manual port. No runtime values are guessed. Conversion assumes
omitted boolean settings remain zero and bounded integer settings remain at their
minimum (unbounded or invalid registrations retain the legacy zero assumption).
It produces only the exported constant freezes; it cannot provide the omitted source features. Overlapping cheats still follow
the app's normal application order. Test converted cheats in the game and
keep save backups before relying on them.

`bigpemu-api.json` maps every community API call the reviewed sources use to a
status (`supported`, `setting-omitted`, `ignored`, `unsupported`) with our own
description of what the call does and the follow-up needed. `load_api_map()`
reads it; an unlisted call is `unmapped` and the refusal names it
(`Unsupported.call`). `bigpemu_register_setting_int` lacks explicit bounds and
remains omitted. Bounded
`bigpemu_register_setting_int_full` choices are converted individually; unsupported
choices are reported and omitted in full. Invalid or excessive ranges are reported
for the setting. A `printf_notify("literal")` reached at load for the
matching cartridge becomes `Module.messages` (at most four, 200 characters each);
other uses are listed in `Module.dropped_messages`, and `printf` in
`Module.ignored_calls`.

Raw uncompressed `.j64`, `.jag`, `.rom`, and `.bin` cartridge inputs up to 16 MiB
are accepted. Recognized copier headers are refused: provide the native payload
so both the source hash and app CRC refer to the same bytes. Disc images and
other software containers are outside this first version.

## Community cheat packs

Packs are published at <https://jaguar.provenance-emu.com/cheats/>.

jguffer, the author of BigPEmu_Scripts and of
[#1050](https://github.com/libretro/virtualjaguar-libretro/issues/1050), gave the
maintainer permission to convert and use the scripts (see
[#1071](https://github.com/libretro/virtualjaguar-libretro/issues/1071)). The
repository has no license file, so the permission is the basis for redistribution
and every pack credits him:

> Cheats by jguffer (https://github.com/jguffer/BigPEmu_Scripts), converted by the Virtual Jaguar project with the author's permission.

`scripts/cheat_aliens_vs_predator.c` is excluded: it credits values to a
third-party script bundled with BigPEmu, and the permission covers only the
author's own work. Disc titles are listed in the worklist but not converted; packs
cover cartridges only.

### Workflow

`sources.json` (pinned commit per source, include globs, exclusions with reasons,
permission and credit text) and `cartridges.json` (script FNV1a64 to the app's
CRC32, hashes and titles only, no ROM bytes) feed `build_packs.py`, which writes
the committed `site/cheats/` tree (`manifest.json`, `unsupported.json`,
`index.html`, `README.md`, `packs/<CRC32>.cht`):

```sh
/tmp/vj-cheat-import-env/bin/python tools/cheat-import/build_packs.py \
  --sources tools/cheat-import/sources.json \
  --cartridges tools/cheat-import/cartridges.json --out site/cheats
```

Sources are fetched as one pinned tarball per source, parsed by `convert.py` and
never compiled or executed (`--source-dir ID=PATH` uses a local checkout). Output
is byte-for-byte reproducible. The generator fails if a converted script ever
activates writes for a different cartridge hash. `unsupported.json` is the work
list of scripts and settings the converter cannot express yet, with the BigPEmu
API calls they use.

`manifest.json` embeds the sha256 of `convert.py`, `build_packs.py` and
`cartridges.json`, and CI regenerates `site/cheats` and diffs it. **Any edit to
`convert.py`, `build_packs.py` or `cartridges.json` needs a `site/cheats`
regeneration in the same commit.** `scripts/build_site.py` publishes the tree and
`scripts/check_site.py` validates it (hashes, sizes, CRC guards, unlisted files,
size limits).

### Adding a source

Open a PR that adds an entry to `sources.json` (repo, pinned commit, include
globs, credit, and a `permission` note recording the author's permission; a source
without it is not accepted), then regenerate `site/cheats`. If it needs cartridges
not in `cartridges.json`, refresh the table from a directory of native dumps:

```sh
python3 tools/cheat-import/scan_cartridges.py --rom-dir /path/to/native-carts \
  --merge tools/cheat-import/cartridges.json --output /tmp/cartridges.new.json
```

`--merge` keeps the curated titles; review the diff and replace the file. ROM bytes
and paths are never written. Review the author's credits to other authors before
including a file, and exclude it with a reason if the permission does not cover it.

## Validation

Local conversion was first checked against the author's `cheat_kasumi_ninja.c`,
`cheat_atari_karts.c` and `cheat_doom.c` at community repository commit
`24211503339a969f8494b8e189b8f2aa0cb9eba6`, using matching private cartridges. No
emulator binary, SDK header, compiled script or proprietary bundled script was used
to implement the converter. This is a statement of sources used, not a legal
certification of a clean-room process.

The committed packs plus the CI regeneration diff are the conversion contract: a
changed value shows up as a diff in `site/cheats/packs/*.cht`. Unit tests use
original synthetic inputs:

```sh
python3 -m unittest discover -s tools/cheat-import -v
```

The app's `cheats_unit` test additionally checks the revision guard, saved
toggles, legacy files, hardcore gating, and grouped byte/word/long replay through
the core's real parser. This validates translation and application mechanics; it
does not establish that a community author's cheat is correct in gameplay.
