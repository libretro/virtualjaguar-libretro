# Community cheat source converter

Translate a limited subset of user-authored C cheat sources into the app's
existing `.cht` memory freezes. This is an offline, static source converter:
it does not execute or compile the input, follow includes, contact an emulator,
or require an SDK. Python 3.10+ and `pycparser` are needed only for conversion;
the app and libretro core gain no Python dependency or script VM.

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

- One category, boolean settings, a software-load callback, and an emulation
  frame callback, using the registration/call names present in the author's
  community C sources.
- Constant byte, word, and long writes to the 2 MiB main RAM, including grouped
  writes and bounded integer loops. Word/long addresses must be even.
- Integer constants, object-like macros, enums, scalar locals, explicit integer
  casts, simple arithmetic/bitwise expressions and conditions. Fixed-width C
  integer promotion and unsigned wrapping are modeled; signed overflow fails.
- At most ten settings, with every combination of the exported toggles checked
  for equivalent final byte writes. Cheats run as an ordered group per toggle;
  sources whose combined behavior cannot be preserved are refused.

RAM reads, copy/conditional/masked cheats, parameter-valued settings, helper
functions, pointers/arrays, static local state, frame-to-frame state changes,
extra events, early returns, general preprocessing, and other unsupported C
syntax need a manual port. No runtime values are guessed. Conversion assumes
omitted settings remain off and produces only the exported constant freezes;
it cannot provide the omitted source features. Overlapping cheats still follow
the app's normal application order. Test converted cheats in the game and
keep save backups before relying on them.

Raw uncompressed `.j64`, `.jag`, `.rom`, and `.bin` cartridge inputs up to 16 MiB
are accepted. Recognized copier headers are refused: provide the native payload
so both the source hash and app CRC refer to the same bytes. Disc images and
other software containers are outside this first version.

## Provenance and validation

This feature responds to [#1050](https://github.com/libretro/virtualjaguar-libretro/issues/1050),
where the owner of `jguffer/BigPEmu_Scripts` says they generated those scripts
and requests translation. That repository has no stated license. This project
does not redistribute its sources or a converted cheat pack, and does not
infer redistribution permission from the request.

Local conversion was checked against the author's `cheat_kasumi_ninja.c`,
`cheat_atari_karts.c`, and `cheat_doom.c` at community repository commit
`24211503339a969f8494b8e189b8f2aa0cb9eba6`, using matching private cartridges.
Their hashes matched; one, two of three, and eight toggles respectively
converted. Atari Karts' read/modify/write unlock setting was refused. An
additional file credited values to a third-party bundled script and was
excluded from conversion validation. No emulator binary, SDK header, compiled
script, or proprietary bundled script was used to implement the converter.
This is a statement of sources used, not a legal certification of a clean-room
process. Community conversion output remains local and uncommitted.

All committed tests use original synthetic inputs:

```sh
python3 -m unittest discover -s tools/cheat-import -v
```

CI also fetches **only the three reviewed C files** from the community commit
above and verifies their SHA256 before parsing. The committed
`community-cases.json` stores expected setting names, write counts, hashes of
the converted codes, and unsupported-operation reasons, without distributing
the scripts or a usable cheat pack. A changed value, missing grouped write,
unexpected omission, or wrong-ROM activation fails the integration test.
The input sources are held in memory, never executed or uploaded as artifacts.
Network/checksum failures fail the job instead of silently skipping cases.

Run the same test locally using a checkout of that exact revision:

```sh
python3 tools/cheat-import/community_test.py --source-dir /path/to/BigPEmu_Scripts
# Or download just the three reviewed sources:
python3 tools/cheat-import/community_test.py --fetch
# Optional local identity check with the matching native cartridges:
python3 tools/cheat-import/community_test.py --fetch --rom-dir /path/to/native-carts
```

Without `--rom-dir`, this checks conversion contracts using known ROM hashes;
it does not test actual cartridge identity or gameplay. ROM mode requires every
listed cartridge to exist and match, and fails on missing or mismatched inputs.
Private ROMs are never required by CI. Updating the community pin or adding a
source requires reviewing its author/credits, checksum and conversion contract.

A future author-submitted repository catalog could build on this explicit
source manifest: repository URL, author permission/license, reviewed source
paths, pinned revision and checksums, and compatibility results submitted by PR.
That catalog should remain separate from executable scripts and proprietary
bundled content. This version provides test provenance rather than an app
download/subscription registry.

The app's `cheats_unit` test additionally checks the revision guard, saved
toggles, legacy files, hardcore gating, and grouped byte/word/long replay through
the core's real parser. This validates translation and application mechanics;
it does not establish that a community author's cheat is correct in gameplay.
