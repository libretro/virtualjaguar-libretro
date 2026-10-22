# Community Cheat Store Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** CI converts pinned community C cheat sources into per-cartridge `.cht` packs served from
`jaguar.provenance-emu.com/cheats/`, and the desktop app installs them from a "Community cheats"
section of the Cheats window (#1071).

**Architecture:** `tools/cheat-import/build_packs.py` drives the existing `convert.py` over every
source in `sources.json`, using a committed FNV1a64→CRC32 table (`cartridges.json`) because CI has
no ROMs, and writes `site/cheats/` (committed; `build_site.py` copies it like `site/thumbnails/`).
A CI job regenerates and diffs. The app adds a CRC-keyed second cheat set to `CheatManager` and a
`CheatStore` service (manifest fetch, sha256-verified atomic install) on a worker thread.

**Tech Stack:** Python 3.10+ / pycparser (generator), stdlib-only `scripts/build_site.py`,
GitHub Actions, C++17 app (SDL3, Dear ImGui, `neuro::Json`, `http::get`, `Worker`).

**Spec:** `docs/cheat-store-design.md` (Data and CI §1-10, App §1-9). Read both the spec section a
task cites and this plan; the spec's schemas are normative, copy field names verbatim.

## Global Constraints

- Two PRs into `develop`, branched from `libretro/develop`: PR 1 = Tasks 1-6 (`Refs #1071`), PR 2 = Tasks 7-11 (`Closes #1071`). Never touch `master` / `release/*`.
- Worktree: `~/Workspace/Provenance/vj-wt/1071-cheat-packs` (PR 1). PR 2 gets a fresh worktree under `~/Workspace/Provenance/vj-wt/` once PR 1's branch exists (branch off PR 1 until it merges).
- `test/roms/private` is a symlink to irreplaceable data: never `git clean -xfd`, never delete through it, never commit ROMs or ROM-derived binaries. Hashes (FNV1a64, CRC32, sizes, names) are fine to commit.
- `convert.py` stays the only conversion logic. No C++ port, no Python called from the app.
- Fetch only `scripts/cheat_*.c` from community repos. Never `.bigpcvm`, CHAD scripts, `descriptions.txt`.
- Excluded: `scripts/cheat_aliens_vs_predator.c`, reason "credits values to a third-party script bundled with BigPEmu (\"Taken from Whitehouse avp_common.c script\"); permission covers only the author's own work".
- Credit string (verbatim): `Cheats by jguffer (https://github.com/jguffer/BigPEmu_Scripts), converted by the Virtual Jaguar project with the author's permission.`
- Pin: `jguffer/BigPEmu_Scripts`, branch `main`, commit `24211503339a969f8494b8e189b8f2aa0cb9eba6`, commit_date `2026-04-15`.
- Generated output byte-deterministic: `json.dumps(obj, indent=2, sort_keys=True, ensure_ascii=True) + "\n"`, LF, no timestamps/paths/hostnames.
- CI budget (user rule): cheat workflows run only on schedule, `workflow_dispatch`, and push/PR touching `tools/cheat-import/**`, `site/cheats/**` or the workflow file; every workflow has a `concurrency` group with `cancel-in-progress: true` (bump workflow: `false`). Open a PR only when its slice is complete; batch fixups into one push.
- App build: `env -u DEVELOPER_DIR CC=/usr/bin/clang CXX=/usr/bin/clang++ cmake -S app -B build -DCMAKE_BUILD_TYPE=Release`; keep `-j` modest (≤8). NEVER open app windows on this Mac: `--headless` and unit tests only; windowed screenshots run on the Mac Studio (`vj-eval/remote/vjr.sh`, `ssh studio-runner`).
- Commits: conventional, 1Password-signed (retry on "failed to fill whole buffer", never disable signing), ending `Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>`. Shell `rm/cp/mv` are interactive aliases: use `command rm -f` etc.
- App C++ follows the surrounding code: comment density, naming, `namespace vj`, no exceptions in parsing paths.

## Review Focus

- A source whose 64-bit literal matches no table row, or matches two rows: must yield a worklist row / two packs, never a pack bound to a guessed CRC. (Task 3 tests `test_no_table_hit`, `test_two_revisions`.)
- Two sources for one CRC with a same-named cheat of different code (mc3d classic/virtual): `parseCht` merges consecutive identical names, so the generator must suffix; a silent merge would apply two cheats as one toggle. (Task 3 `test_same_name_suffix` asserts names in the parsed `.cht` are unique.)
- Pack update while a cheat is on: the unchanged cheat stays on, the changed one comes back off, and no stale `cp.` key survives. (Task 8 `testCommunityTogglesAcrossUpdate`.)
- Server serves bytes that differ from the manifest (stale CDN, truncated transfer): install must fail closed and keep the previous copy. (Task 9 `testInstallVerify` sha + size + 404 cases.)
- Downloads off / offline / hardcore: no network call, installed pack still usable (hardcore: visible, toggles blocked). (Task 9 `testStates`.)

---

## PR 1 — data, CI, site

### Task 1: Converter: BigPEmu API map, int settings, header split

**Files:**
- Create: `tools/cheat-import/bigpemu-api.json`
- Modify: `tools/cheat-import/convert.py` (Registrations allow-list ~line 371; `Runtime.call` ~line 234; `cht_text` ~line 500)
- Test: `tools/cheat-import/test_convert.py`

**API map (user request 2026-10-22).** `bigpemu-api.json` = `{"format": 1, "calls": {"<name>": {"status", "meaning", "ours", "todo"}}}`, keys sorted. `status` is one of `supported` (translated), `setting-omitted` (the setting shows as an omitted toggle), `ignored` (no memory effect; the pack gets a warning note), `unsupported` (blocks the toggle/module; worklist). `meaning` is our own description from how the community sources use the call (no SDK headers). `ours` names our equivalent or "none yet". `todo` is the follow-up pattern, or "". Initial rows, every call the 61 sources use:
- supported: `bigpemu_get_module_handle`, `bigpemu_set_module_usage_flags`, `bigpemu_register_setting_category`, `bigpemu_register_setting_bool`, `bigpemu_register_event_sw_loaded`, `bigpemu_register_event_emu_thread_frame`, `bigpemu_get_loaded_fnv1a64` (→ `vj_rom_crc32` guard via cartridges.json), `bigpemu_get_setting_value`, `bigpemu_jag_write8/16/32` (→ `ADDR:VV` / `ADDR:VVVV` / `ADDR:VVVVVVVV` segments).
- setting-omitted: `bigpemu_register_setting_int`, `bigpemu_register_setting_int_full` (ours: none yet; todo: expand a small value range into one toggle per value).
- ignored: `printf_notify`, `printf` (on-screen/log message only; note "messages from the original script are not shown"), `bigpemu_unregister_event` (shutdown only).
- unsupported: `bigpemu_jag_read8/16/32` (todo: conditional / read-modify-write cheats need core support in `src/core/cheat.c`), `bigpemu_jag_sysmemread`, `bigpemu_jag_sysmemwrite`, `bigpemu_jag_write` (width-parameterised write), `bigpemu_jag_m68k_bp_add`, `bigpemu_jag_m68k_get_areg` (todo: 68K breakpoint hook; candidate titlehook), `bigpemu_register_event_input_frame`, `bigpemu_jag_get_frame_count`, `memcpy`.
Any call not in the map is `unmapped`: the converter raises `Unsupported` naming it, the generator (Task 3) records it, and the CI regen job prints a `::warning::` per unmapped call so new upstream API use is visible in the bump PR.

**Interfaces:**
- Produces: `class Unsupported(Exception)` gains optional attribute `call: str | None` (set whenever a specific API call caused the refusal); `load_api_map(path=None) -> dict[str, dict]` (validates `format == 1`, `status` values); Task 3 consumes both to fill `calls` on worklist rows and `ignored` warnings on packs.
- Produces: `cht_header(crc: int, comment_lines: list[str]) -> list[str]` (comment lines prefixed `# `, then `vj_rom_crc32 = "%08X"`, `cheats = N` is NOT in the header), `cht_entries(entries: list[dict]) -> list[str]` (the `cheats = N` line, blank line, then `cheatI_desc/_code/_enable = false` blocks). `cht_text(title, crc, entries, source_name, source_sha)` keeps its signature and exact bytes, built from the two.

- [ ] **Step 1: Failing tests** in `test_convert.py`, using its existing `source()` builder:
  - `test_int_full_setting_is_omitted_not_fatal`: a module with one `bigpemu_register_setting_bool` toggle and one `bigpemu_register_setting_int_full(cat, "Start At Level", -1, -1, 9, 1)`; `convert(DIGEST)` returns two entries, the bool one has `code`, the int one has `unsupported == "parameter setting requires manual value selection"`.
  - `test_int_full_dependent_write_refused`: the bool toggle's write is additionally guarded by `if (level != -1)` read from the int setting → that entry is `unsupported` (dependency on a non-selected setting), and no entry carries the int-dependent address.
  - `test_printf_notify_ignored`: a toggle whose frame callback calls `printf_notify("x\n")` then writes → converts; the module reports `ignored_calls == {"printf_notify"}` (new `Module.ignored_calls: set[str]`, filled during `convert`).
  - `test_unknown_call_names_call`: an unmapped `bigpemu_jag_foo()` → `Unsupported` with `.call == "bigpemu_jag_foo"`.
  - `test_api_map_covers_converter`: every call name the converter special-cases appears in `bigpemu-api.json` with a matching status (supported/ignored/setting-omitted).
  - `test_cht_text_unchanged`: `cht_text(...)` for a fixed input equals a literal expected string (paste the current output before refactoring), and equals `"\n".join(cht_header(...) + cht_entries(...))` with the legacy comment lines.
- [ ] **Step 2:** `python3 -m unittest discover -s tools/cheat-import -v` → the int tests FAIL with "unsupported initialisation call bigpemu_register_setting_int_full".
- [ ] **Step 3:** Add `"bigpemu_register_setting_int_full"` to the allow-list (no other logic change: `name.endswith("_bool")` already classifies it non-boolean). In `Runtime.call`, `printf_notify`/`printf` return `Value(0)` without evaluating arguments and add the name to `module.ignored_calls`. Set `.call` on every `Unsupported` raised for a specific call. Write `bigpemu-api.json`. Split `cht_text`.
- [ ] **Step 4:** Same command → all PASS (existing tests unchanged).
- [ ] **Step 5:** Commit `feat(cheat-import): BigPEmu API map; int_full settings omitted; printf_notify ignored`.

### Task 2: Cartridge identity table

**Files:**
- Create: `tools/cheat-import/scan_cartridges.py`, `tools/cheat-import/cartridges.json`
- Test: `tools/cheat-import/test_scan_cartridges.py`

**Interfaces:**
- Consumes: `convert.rom_bytes(path) -> bytes`, `convert.fnv1a64(bytes) -> int`.
- Produces: `cartridges.json` schema per spec Data §2: `{"format": 1, "cartridges": [{"fnv1a64", "crc32", "size", "title", "name"}]}` sorted by `fnv1a64`; `load_cartridges(path) -> dict[str fnv1a64 upper, dict]` in `scan_cartridges.py` (raises `ValueError` on duplicate fnv or crc, bad hex, `format != 1`).
- CLI: `scan_cartridges.py --rom-dir DIR [--merge EXISTING.json] --output NEW.json` (output must not exist; recursive walk of `*.j64 *.jag *.rom *.bin`, case-insensitive; copier-headered files skipped with a stderr line; `name` = file stem, `title` = stem up to first ` (`; `--merge` keeps existing rows' `title`/`name`; identical-hash duplicates keep the first path in sorted order).

- [ ] **Step 1: Failing tests** with synthetic ROM bytes written to a temp dir: two distinct ROMs → two sorted rows with correct `zlib.crc32`/FNV/size; a copier-headered ROM (512-byte header + `\x04\x04\x04\x04` at 0x600, as `rom_bytes` detects) is skipped; `--merge` keeps a curated title; existing `--output` refused (exit 2); `load_cartridges` rejects duplicate crc32.
- [ ] **Step 2:** Run → FAIL (module missing).
- [ ] **Step 3:** Implement.
- [ ] **Step 4:** Tests PASS. Then generate the real table: `python3 tools/cheat-import/scan_cartridges.py --rom-dir test/roms/private/ROMS --output tools/cheat-import/cartridges.json` (no network, read-only on the corpus). Hand-check: no path components, no bytes; Doom row is `75F2216BD4F54AC2` / `5E2CDBC0`. Fix obvious `title` oddities (e.g. strip `[a1]`).
- [ ] **Step 5:** Commit `feat(cheat-import): committed cartridge identity table (hashes only)`.

### Task 3: Pack generator

**Files:**
- Create: `tools/cheat-import/sources.json`, `tools/cheat-import/build_packs.py`
- Test: `tools/cheat-import/test_build_packs.py`

**Interfaces:**
- Consumes: Task 1 `Module`, `cht_header`, `cht_entries`, `quoted`; Task 2 `load_cartridges`.
- Produces: CLI exactly as spec Data §7 (`--sources --cartridges --out [--source-dir ID=PATH ...] [--no-network] [--summary OLD NEW] [--bump ID=SHA:DATE] [--max-pack-bytes 65536] [--max-total-bytes 1048576]`); exit codes 0/1/2 as spec. Output files per spec Data §3: `manifest.json` (schema §4), `unsupported.json`, `packs/<CRC32>.cht` (header per §5), `index.html`, `README.md`, marker `.vj-cheat-packs`. Python API used by tests: `build(sources: dict, cartridges: dict, fetched: dict[str source_id, dict[str path, bytes]]) -> dict[str relpath, bytes]` (pure: no I/O) and `fetch_github(repo, commit, include) -> dict[str path, bytes]` (codeload tarball, 32 MiB cap, 60 s timeout, members filtered by fnmatch `include`, 1 MiB per member).
- `sources.json` content: spec Data §1 with the Global Constraints pin, exclude and credit; `notes` for the CD-title sources (baldies, battlemorph, highlander, blue_lightning if its literal is a disc hash — leave only titles confirmed as discs).

- [ ] **Step 1: Failing tests** (synthetic sources via `test_convert.source()`, synthetic table), each calling `build()`:
  - `test_single_pack`: one source/one cart → `packs/<CRC>.cht` parses with the credit comment, `vj_rom_crc32`, all `_enable = false`; manifest pack has `sha256 == sha256(cht bytes)`, `bytes`, `cheats`, `writes`, `partial false`, `updated == "2026-04-15"`.
  - `test_partial_notes`: one unsupported setting → pack `partial true`, `notes[0].setting/reason`, `# Omitted:` comment, and a `stage: "setting"` worklist row.
  - `test_merge_dedupe`: two sources, same CRC, identical (name, code) → one entry, both listed in `sources`.
  - `test_same_name_suffix`: same name, different code → second suffixed ` (<module title>)` (or ` (<file stem minus cheat_>)` when titles equal); all `cheatN_desc` unique.
  - `test_overlap_recorded`: two cheats writing the same byte → `overlaps == [[0, 1]]` and a `# Overlap:` line.
  - `test_two_revisions`: source naming two table hashes → two packs.
  - `test_no_table_hit` / `test_module_error`: worklist rows `stage: "identity"` (reason lists the literals) / `stage: "module"`; no pack.
  - `test_api_usage`: a module failing on `bigpemu_jag_read8` → its worklist row has `calls: ["bigpemu_jag_read8"]`; `unsupported.json` has top-level `api_usage: {"<call>": {"status": ..., "sources": [paths]}}` covering every call seen in any source (scan `\b(bigpemu_\w+|printf_notify|printf|memcpy)\b` on uncommented text); an unmapped call appears with `status: "unmapped"`; a pack whose source hit an ignored call gets `notes` row `{setting: null, reason: "ignored printf_notify: messages from the original script are not shown"}`.
  - `test_excluded_never_parsed`: excluded path with deliberately unparsable text → listed in `unsupported.json` with the exclude reason, no exception.
  - `test_include_must_be_c`: `include: ["scripts/*.bigpcvm"]` → `ValueError`.
  - `test_wrong_hash_activation_fails`: a synthetic source whose write is not hash-guarded → build raises (exit 1 path).
  - `test_core_limit_flag`: 257 writes across cheats → `exceeds_core_limit true`.
  - `test_deterministic`: two `build()` calls byte-equal; `json` outputs end with `\n`, sorted keys.
  - `test_budgets`: `--max-pack-bytes` exceeded → exit 1.
  - `test_summary`: old vs new manifest with one added, one changed, one removed cheat → Markdown names each.
  - `test_bump`: `--bump jguffer=<40hex>:2026-05-01` rewrites only `commit`/`commit_date`, canonical formatting.
  - `test_zero_packs_fails`: all sources unsupported → exit 1.
- [ ] **Step 2:** Run → FAIL.
- [ ] **Step 3:** Implement. Identity scan: `re.findall(r"0x([0-9A-Fa-f]{9,16})", convert.uncomment(text))` ∩ table keys (uppercased, zero-padded to 16). Each converted source also runs `module.convert(digest ^ 1)` and fails if any entry has `code`. `index.html` is generated from the manifest only: `noindex`, mirrors `site/thumbnails/index.html` styling, lists title, name, CRC, cheat count, partial flag, pack link, credit, link to `unsupported.json`. `README.md`: provenance, the credit, permission statement from #1071, how it's generated.
- [ ] **Step 4:** Tests PASS. Then `python3 tools/cheat-import/build_packs.py --sources tools/cheat-import/sources.json --cartridges tools/cheat-import/cartridges.json --out site/cheats` (network: one tarball). Expected: ~22 packs; Doom 8 cheats; Kasumi Ninja 1 (deduped); no pack for AvP. Run it twice into temp dirs, `diff -r` → empty.
- [ ] **Step 5:** Commit generator + tests, then separately `data(cheats): generate community packs from jguffer/BigPEmu_Scripts@2421150`.

### Task 4: Site integration, README, retire community_test

**Files:**
- Modify: `scripts/build_site.py` (~line 1756, next to the thumbnails copy), `scripts/check_site.py`, `.github/workflows/pages.yml` (paths), `tools/cheat-import/README.md`
- Delete: `tools/cheat-import/community_test.py`, `test_community.py`, `community-cases.json`
- Test: `scripts/testdata/` fixture + existing site check flow

**Interfaces:**
- Produces: `check_cheats(out: Path) -> list[str]` in `check_site.py`, called from its main; checks per spec Data §8 (manifest parses, `format == 1`, each `packs[].file` exists with matching sha256/bytes and `vj_rom_crc32 == crc32`, no unlisted `.cht`, `unsupported.json` parses, `index.html` local links resolve, manifest ≤ 256 KiB, tree ≤ 1 MiB). Skips when `cheats/` is absent.

- [ ] **Step 1:** Add a failing check: corrupt one byte of a copied pack in a temp site build and assert `check_site.py` exits non-zero naming the file (a small `unittest` in `scripts/` if one exists for check_site, else a shell step documented in the PR test plan).
- [ ] **Step 2:** Implement copytree + `check_cheats`; add `site/cheats/**` and `tools/cheat-import/**` to `pages.yml` paths.
- [ ] **Step 3:** `python3 scripts/build_site.py --releases scripts/testdata/releases.json --out "$TMPDIR/site" && python3 scripts/check_site.py "$TMPDIR/site"` → OK and `cheats/manifest.json` present.
- [ ] **Step 4:** Rewrite README provenance: drop "does not redistribute … converted cheat pack" and the community_test section; add the permission statement, credit requirement, sources/cartridges/packs workflow, "adding a source" (PR with author permission), and the `int_full` support line. Delete the three community_test files.
- [ ] **Step 5:** `python3 -m unittest discover -s tools/cheat-import -v` PASS; commit `feat(site): publish community cheat packs under /cheats/`.

### Task 5: Workflows

**Files:**
- Modify: `.github/workflows/cheat-import.yml`
- Create: `.github/workflows/cheat-packs-bump.yml`

- [ ] **Step 1:** `cheat-import.yml` per spec Data §8: paths add `site/cheats/**`; `concurrency: {group: cheat-import-${{ github.ref }}, cancel-in-progress: true}`; drop the `community_test.py --fetch` step; new job `packs` (3.13, `needs: converter`) that builds to `$RUNNER_TEMP/a` and `/b`, `diff -r a b`, `diff -r a site/cheats`, then build_site with the release fixture + check_site. Keep the existing `runs-on` expression.
- [ ] **Step 2:** `cheat-packs-bump.yml` per spec Data §8 steps 1-5 (cron `23 6 * * 1`, dispatch, `contents: write`, `pull-requests: write`, `actions: write` for the dispatch, concurrency `cheat-packs-bump` no-cancel, fixed branch `cheats/pin-bump`, `--bump`, `--summary` body, `gh workflow run cheat-import.yml --ref cheats/pin-bump`, body note about GITHUB_TOKEN). Commit message uses the attribution line.
- [ ] **Step 3:** `actionlint` if installed (else `python3 -c "import yaml,sys;[yaml.safe_load(open(f)) for f in sys.argv[1:]]"` on both files) → clean. Dry-run the bump logic locally with `--bump` to a fake SHA into a temp copy (no push).
- [ ] **Step 4:** Commit `ci(cheats): regen-diff job, concurrency, weekly pin-bump PR`.

### Task 6: Corpus verification + PR 1

Not committed code; the controller runs it.

- [ ] **Step 1:** Build the app (Global Constraints command, `cmake --build build -j8 --target vj`... use the app target name from `app/CMakeLists.txt`).
- [ ] **Step 2:** For each pack: find the corpus ROM whose row in `cartridges.json` has that CRC; run `build/<app> --headless --frames 120 --data-dir "$TMPDIR/vjd" --cheats-dir site/cheats/packs --cheat-set <CRC32> --cheat-on <first cheat desc> "<rom>"` and confirm exit 0 with no "restricted/does not match" line; also run one pack against a different ROM and confirm the set is refused. This exercises the core's CRC path and `loadChtFile`, not the table.
- [ ] **Step 3:** Record per-game counts (converted / omitted) for the PR body.
- [ ] **Step 4:** Open the worklist issue from `unsupported.json`: one checklist section per API call in `api_usage` (status unsupported / setting-omitted / unmapped, with the sources it blocks and the map's `todo`), plus the non-API reasons (pointer/array declarations, CD titles, >10 settings, unknown cartridge, 0/N converted), milestone v4.0.0, `Refs #1071`.
- [ ] **Step 5:** Push once, open PR 1 into `develop` (`Refs #1071`), report CI.

## PR 2 — app

### Task 7: SHA-256

**Files:** Create `app/src/sha256.h`, `app/src/sha256.cpp`; Modify `app/cmake/VJCheatTests.cmake`; Test `app/tests/cheats_test.cpp`

**Interfaces:** Produces `namespace vj { std::string sha256Hex(const void* data, size_t size); }` (lower-case 64 hex, FIPS 180-4).

- [ ] **Step 1:** `testSha256`: `""` → `e3b0c442…b855`, `"abc"` → `ba7816bf…15ad`, `"abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq"` → `248d6a61…06c1`, 1,000,000 × `'a'` → `cdc76e5c…c7dc0`.
- [ ] **Step 2:** Build `vj_cheats_test`, run → FAIL (link). **Step 3:** implement. **Step 4:** PASS. **Step 5:** commit `feat(app): sha256 for verified downloads`.

### Task 8: CheatManager community set

**Files:** Modify `app/src/cheats.h`, `app/src/cheats.cpp`; Test `app/tests/cheats_test.cpp`

**Interfaces (spec App §2):**
- `enum class CheatOrigin { Database, Community };`
- `bool loadChtText(const std::string& text, const std::string& path, ChtSet& out);` (`loadChtFile` reads then calls it)
- `CheatManager::setCommunityDir(const std::string& dir)`, `void reloadCommunity()`, `const ChtSet* communitySet() const`, `void removeCommunityToggles()` (erases every `cp.` key, persists, applies)
- `isEnabled(const ChtCheat&, CheatOrigin = CheatOrigin::Database)`, `setEnabled(const ChtCheat&, bool, CheatOrigin = CheatOrigin::Database)`; ini keys `on.<id>` / `cp.<id>`; `static constexpr unsigned kCommunityIndexBase = 5000;`
- `apply()` order: database set, community pack (`5000 + i`), user (`10000 + i`); `enabledCount`, `disableAll`, `setEnabledByName` (database → community → user) cover the pack.

- [ ] **Step 1: Failing tests:** `testCommunityLoad` (pack `<dir>/<%08X>.cht` loads for a cart with that CRC, independent of `selectSet`; crc 0 never loads; mismatched guard → all unsupported + `restriction`); `testCommunityTogglesAcrossUpdate` (enable 2, new manager reloads → still on; rewrite the file with one code changed, `reloadCommunity()` → changed one off, unchanged on, no stale `cp.` key in the ini); `testApplyOrder` (db + pack + user on → batch `reset`, db ops, `5000+i`, `10000+i`; `appliedSegments()` sums; warn above 256); `testCommunityHardcore` (`blocked` true → only `reset`); `testRemoveToggles`.
- [ ] **Step 2:** FAIL. **Step 3:** implement. **Step 4:** whole `vj_cheats_test` PASS (existing tests unchanged). **Step 5:** commit `feat(app): CRC-keyed community cheat set in CheatManager`.

### Task 9: CheatStore service

**Files:** Create `app/src/cheat_store.h`, `app/src/cheat_store.cpp`, `app/tests/fixtures/community/{manifest.json,<CRC>.cht}`; Modify `app/cmake/VJCheatTests.cmake`; Test `app/tests/cheats_test.cpp`

**Interfaces (spec App §1, §3, §4):**
- `struct PackMeta { std::string crc32, title, name, file, sha256, updated, credit; unsigned bytes = 0, cheats = 0, writes = 0; bool partial = false, exceedsCoreLimit = false; std::vector<std::pair<std::string,std::string>> notes; /* setting, reason */ std::string json; /* the packs[i] object, dumped */ };`
- `bool parseManifest(const std::string& text, std::vector<PackMeta>& out, std::string& err);` (format 1, required keys, bad entries skipped, `file` relative without `..`)
- `enum class PackState { NoGame, Disc, DownloadsOff, Checking, NotOffered, Offered, Installed, UpdateAvailable, NoLongerOffered, EditedLocally };`
- `class CheatStore { std::function<bool(const std::string& url, size_t maxBytes, std::string& body, std::string& err)> fetch; std::function<void(std::function<void()>)> run; std::function<bool()> downloadsAllowed; std::function<int64_t()> now; void setDir(const std::string& communityDir); void setBaseUrl(const std::string&); void windowOpened(uint32_t crc, Config& cfg); void checkNow(); bool install(uint32_t crc); bool remove(uint32_t crc, CheatManager&); void pump(CheatManager&); PackState state(uint32_t crc) const; const PackMeta* offered(uint32_t crc) const; const PackMeta* installed(uint32_t crc) const; const std::string& status() const; void stop(); };`
- Constants: manifest cap 256 KiB, pack cap 64 KiB, interval 6 h in `cheats.community.checked`, base `https://jaguar.provenance-emu.com/cheats/`, override `cheats.community.url`. Storage `<data>/cheats/community/{manifest.json, packs/<CRC>.cht, packs/<CRC>.json}` via `writeFileAtomic`.

- [ ] **Step 1: Failing tests** (map-backed `fetch`, inline `run`): `testManifestParse` (good; format 2; bad sha hex skipped; truncated; `..` in file); `testInstallVerify` (sha mismatch, byte mismatch, guard for other CRC, missing guard, no cheats, 404 → nothing written, status set; success → both files, `communitySet()` all off); `testStates` (Offered, Installed, UpdateAvailable when manifest sha changes, NoLongerOffered, EditedLocally, Disc never fetches, downloads off never calls `fetch`, hardcore still installs); `testRetryGate` (second `windowOpened` within 6 h no fetch; `checkNow()` fetches; failed fetch doesn't update `checked` or the cache); `testOfflineAfterInstall` (fetch always fails → installed pack + sidecar credits still available).
- [ ] **Step 2:** FAIL. **Step 3:** implement. **Step 4:** PASS. **Step 5:** commit `feat(app): community cheat store service`.

### Task 10: Wiring, UI, docs

**Files:** Modify `app/src/app.h`, `app/src/app.cpp`, `app/src/cheats_core.cpp`, `app/src/cheats_ui.cpp`, `app/src/library_ui.cpp:297`, `site/pages/app.html`, `site/pages/privacy.html`, `tools/cheat-import/README.md`, `docs/WHATSNEW`

- [ ] **Step 1:** `App` owns `CheatStore cheatStore; Worker cheatNet;`; `cheatsInit` sets dirs, base URL override, `downloadsAllowed = [&]{ return app.library.downloadArt && http::available(); }`, `fetch` = `http::get` (status != 200 → error), `run = cheatNet.post`; stop on shutdown before `cheats` is destroyed.
- [ ] **Step 2:** `cheats_ui.cpp`: `drawSetCheats` → `drawCheatRows(cm, set, CheatOrigin)`; `drawCommunity` between "Database cheats" and "Manual codes" with the state table of spec App §5 (credit `TextDisabled` wrapped, collapsed notes tree, core-limit warning, hardcore line, Install/Update/Remove/Check for updates via `ui::Flow`); call `cheatStore.windowOpened` when the window becomes visible and `pump` each frame.
- [ ] **Step 3:** Library checkbox label "Download art and cheat packs" (key unchanged).
- [ ] **Step 4:** Docs: privacy row (service, when, what is sent: manifest + the pack for the cartridge's CRC32, nothing else) and `cheats/community/` in the local-files list; app.html Cheats paragraph; README cross-link; WHATSNEW line.
- [ ] **Step 5:** Full app build + `ctest -R cheat` + `python3 scripts/build_site.py ... && check_site.py` PASS; headless smoke: `--headless --frames 60` with a cart and `cheats.community.url` pointing at a loopback `python3 -m http.server` serving `site/cheats/` (Task 9 code path, real HTTP) and confirm the pack installs to the data dir. Commit `feat(app): Community cheats section in the Cheats window`.

### Task 11: Final review + PR 2

- [ ] **Step 1:** Windowed screenshot of the Cheats window (Offered + Installed states) on the Mac Studio via `vj-eval/remote/vjr.sh`; update `site/assets/app_cheats.png` only if the layout changed visibly.
- [ ] **Step 2:** Whole-branch review (Fable). Fix findings.
- [ ] **Step 3:** Push once, open PR 2 (`Closes #1071`), report CI. Don't merge.
